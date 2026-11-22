#include <atlantis/remote/remote_server.h>

#include <atlantis/assert.h>

#include "codec.h"
#include "line_channel.h"
#include "os/socket.h"

#include <cstdint>
#include <deque>
#include <fstream>
#include <map>
#include <optional>
#include <random>
#include <system_error>
#include <utility>
#include <vector>

namespace atlantis::remote {

namespace json = connection::json;
namespace access = atlantis::world::access;
using json::Value;

std::string generateToken() {
  static constexpr char kHex[] = "0123456789abcdef";
  std::random_device device;
  std::string token;
  token.reserve(32);
  for (int word = 0; word < 4; ++word) {
    const std::uint32_t bits = device();
    for (int nibble = 7; nibble >= 0; --nibble) token += kHex[(bits >> (nibble * 4)) & 0xF];
  }
  return token;
}

atlantis::Result<std::monostate, SessionFileError> writeSessionFile(const std::filesystem::path& path,
                                                                    const SessionInfo& session) {
  using ResultT = atlantis::Result<std::monostate, SessionFileError>;
  std::error_code error;
  if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), error);
  if (error) return ResultT::Err(SessionFileError::WriteFailed);
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  {
    std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
    out << formatSession(session);
    if (!out) return ResultT::Err(SessionFileError::WriteFailed);
  }
  std::filesystem::rename(temporary, path, error);  // replaces an existing file
  if (error) {
    std::filesystem::remove(temporary, error);
    return ResultT::Err(SessionFileError::WriteFailed);
  }
  return ResultT::Ok(std::monostate{});
}

void removeSessionFile(const std::filesystem::path& path) noexcept {
  std::error_code error;
  std::filesystem::remove(path, error);
}

std::string_view toString(ListenError error) noexcept {
  switch (error) {
    case ListenError::SocketUnavailable: return "SocketUnavailable";
    case ListenError::PortUnavailable: return "PortUnavailable";
  }
  ATLANTIS_CHECK_MSG(false, "toString(ListenError): unhandled enumerator");
  return "(unrecognized ListenError)";
}

namespace {

// P2: a constant-time comparison, so response timing reveals nothing about
// how much of a guessed token was right. (The length is not secret.)
[[nodiscard]] bool tokensEqual(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  unsigned char difference = 0;
  for (std::size_t i = 0; i < a.size(); ++i) {
    difference = static_cast<unsigned char>(difference | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
  }
  return difference == 0;
}

struct Client {
  explicit Client(LineChannel&& line) : channel(std::move(line)) {}
  LineChannel channel;
  std::unique_ptr<atlantis::connection::RuntimeConnection> connection;  // after a good hello
  std::unique_ptr<atlantis::connection::AuthoringConnection> authoring;  // Plan 0065: at its first authoring.* request
  std::unique_ptr<atlantis::connection::AssetConnection> assets;
    bool closing = false;  // write what is queued, then drop
  bool overran = false;  // exceeded the write limit: drop now
};

[[nodiscard]] Value response(const Value& id, Value result) {
  Value out = Value::object();
  out.set("id", id);
  out.set("result", std::move(result));
  return out;
}

[[nodiscard]] Value errorResponse(const Value& id, std::string_view code, std::string_view message) {
  Value error = Value::object();
  error.set("code", Value::string(std::string(code)));
  error.set("message", Value::string(std::string(message)));
  Value out = Value::object();
  out.set("id", id);
  out.set("error", std::move(error));
  return out;
}

// A method's result, or the text of a BadRequest (malformed parameters).
using Outcome = atlantis::Result<Value, std::string>;

[[nodiscard]] Outcome bad(std::string_view what) { return Outcome::Err("malformed or missing " + std::string(what)); }

// The parameter `name` decoded by `decode`, or nullopt.
template <typename T>
[[nodiscard]] std::optional<T> decodeParam(const Value& params, std::string_view name,
                                           std::optional<T> (*decode)(const Value&)) {
  const Value* value = params.find(name);
  if (value == nullptr) return std::nullopt;
  return decode(*value);
}

template <typename T, typename E, typename Encode, typename EncodeError>
[[nodiscard]] Value inBand(const atlantis::Result<T, E>& result, Encode encode, EncodeError encodeError) {
  if (result.isOk()) return codec::ok(encode(result.value()));
  return codec::err(encodeError(result.error()));
}

[[nodiscard]] Value encodeAccessError(access::AccessError error) { return codec::encodeAccessError(error); }
[[nodiscard]] Value encodeConnectionError(atlantis::connection::ConnectionError error) {
  return codec::encodeConnectionError(error);
}

// The connection.* methods (P2): exactly RuntimeConnection's calls.
[[nodiscard]] Outcome callConnection(atlantis::connection::RuntimeConnection& connection, std::string_view method,
                                     const Value& p) {
  if (method == "connection.findEntity") {
    const auto entity = decodeParam(p, "entity", &codec::decodeEntity);
    if (!entity) return bad("entity");
    return Outcome::Ok(Value::boolean(connection.findEntity(*entity)));
  }
  if (method == "connection.listEntities") {
    Value out = Value::array();
    for (const auto& entity : connection.listEntities()) out.push(codec::encodeEntity(entity));
    return Outcome::Ok(std::move(out));
  }
  if (method == "connection.listComponents") {
    const auto entity = decodeParam(p, "entity", &codec::decodeEntity);
    if (!entity) return bad("entity");
    return Outcome::Ok(inBand(
        connection.listComponents(*entity),
        [](const std::vector<schema::TypeId>& types) {
          Value out = Value::array();
          for (const schema::TypeId type : types) out.push(codec::encodeTypeId(type));
          return out;
        },
        &encodeAccessError));
  }
  if (method == "connection.getProperty") {
    const auto address = decodeParam(p, "address", &codec::decodeAddress);
    if (!address) return bad("address");
    return Outcome::Ok(inBand(
        connection.getProperty(*address), [](const access::PropertyValue& value) { return codec::encode(value); },
        &encodeAccessError));
  }
  if (method == "connection.schema") return Outcome::Ok(codec::encodeSchema(connection.schema()));
  if (method == "connection.submit") {
    auto command = decodeParam(p, "command", &codec::decodeCommand);
    if (!command) return bad("command");
    return Outcome::Ok(Value::number(connection.submit(std::move(*command)).value));
  }
  if (method == "connection.submitTransaction") {
    const Value* list = p.find("commands");
    if (list == nullptr || !list->isArray()) return bad("commands");
    std::vector<access::Command> commands;
    for (const Value& item : list->asArray()) {
      auto command = codec::decodeCommand(item);
      if (!command) return bad("command");
      commands.push_back(std::move(*command));
    }
    return Outcome::Ok(codec::encode(connection.submitTransaction(std::move(commands))));
  }
  if (method == "connection.subscribe") {
    auto filter = decodeParam(p, "filter", &codec::decodeFilter);
    if (!filter) return bad("filter");
    return Outcome::Ok(Value::number(connection.subscribe(std::move(*filter)).value));
  }
  if (method == "connection.drainFailures") {
    Value out = Value::array();
    for (const access::CommandFailure& failure : connection.drainFailures()) out.push(codec::encode(failure));
    return Outcome::Ok(std::move(out));
  }
  if (method == "connection.unsubscribe" || method == "connection.drainEvents") {
    const auto subscription = decodeParam(p, "subscription", &codec::decodeUInt64);
    if (!subscription) return bad("subscription");
    const atlantis::connection::SubscriptionId id{*subscription};
    if (method == "connection.unsubscribe") {
      return Outcome::Ok(
          inBand(connection.unsubscribe(id), [](std::monostate) { return Value(); }, &encodeConnectionError));
    }
    return Outcome::Ok(inBand(
        connection.drainEvents(id),
        [](const std::vector<access::Event>& events) {
          Value out = Value::array();
          for (const access::Event& event : events) out.push(codec::encode(event));
          return out;
        },
        &encodeConnectionError));
  }
  return Outcome::Err("unknown method " + std::string(method));
}

// Plan 0065 P6 (ADR-0130 D3): authoring.* -- AuthoringConnection, one call per
// request, refusals in band.
namespace ac = atlantis::connection::authoring;

[[nodiscard]] Value encodeAuthoringError(ac::Error error) { return codec::encodeAuthoringError(error); }
[[nodiscard]] Value encodeNull(std::monostate) { return Value(); }

[[nodiscard]] Outcome callAuthoring(atlantis::connection::AuthoringConnection& authoring, std::string_view method,
                                    const Value& p) {
  if (method == "authoring.open") {
    return Outcome::Ok(inBand(authoring.open(), &encodeNull, &codec::encodeOpenError));
  }
  if (method == "authoring.documentState") {
    if (!p.isObject() || !p.asObject().empty()) return bad("expected empty parameters");
    const auto state = authoring.documentState();
    if (state.isErr()) return Outcome::Err("unknown method " + std::string(method));
    Value reply = Value::object();
    reply.set("ok", state.value() ? codec::encode(*state.value()) : Value());
    return Outcome::Ok(std::move(reply));
  }
  if (method == "authoring.saveIfCurrent" || method == "authoring.reopenIfCurrent") {
    const bool saving = method == "authoring.saveIfCurrent";
    const auto* expected = p.find("expected");
    if (!expected || p.asObject().size() != 1) return bad("expected");
    const auto token = codec::decodeDocumentToken(*expected);
    if ((!token && (saving || !expected->isNull())) || (token && expected->asObject().size() != 2))
      return bad("expected document token");
    const auto result = saving ? authoring.saveIfCurrent(*token) : authoring.reopenIfCurrent(token);
    if (result.isErr() && result.error().error == ac::DocumentError::Unsupported)
      return Outcome::Err("unknown method " + std::string(method));
    return Outcome::Ok(inBand(result, [](const auto& state) { return codec::encode(state); },
        [](auto failure) { return Value::string(std::string(ac::toString(failure.error))); }));
  }
  if (method == "authoring.recoveryStatus" || method == "authoring.restoreRecovery" ||
      method == "authoring.discardRecovery") {
    if (!p.isObject()) return bad("expected recovery parameters");
    auto reply = [&](const auto& result) {
      if (result.isErr() && result.error().error == ac::RecoveryError::Unsupported)
        return Outcome::Err("unknown method " + std::string(method));
      return Outcome::Ok(inBand(result, [](const auto& value) { return codec::encode(value); },
          [](auto failure) { return Value::string(std::string(ac::toString(failure.error))); }));
    };
    std::optional<ac::EntityGuid> id;
    if (const auto* value = p.find("copyId")) {
      if (!value->isString()) return bad("expected recovery copy ID");
      const auto parsed = atlantis::asset_system::parseEntityGuid(value->asString());
      if (parsed.isErr()) return bad("expected recovery copy ID");
      id = parsed.value();
    }
    if (method == "authoring.recoveryStatus") {
      if (p.asObject().size() != (id ? 1u : 0u)) return bad("expected optional copyId");
      return reply(authoring.recoveryStatus(id));
    }
    const auto* expected = p.find("expected");
    if (!id || !expected || !expected->isObject() || expected->asObject().size() != 2 ||
        p.asObject().size() != 2) return bad("expected copyId and document token");
    const auto token = codec::decodeDocumentToken(*expected);
    if (!token) return bad("expected document token");
    return reply(method == "authoring.restoreRecovery" ? authoring.restoreRecovery(*token, *id) :
                                                        authoring.discardRecovery(*token, *id));
  }
  if (method == "authoring.applyTracked") {
    return Outcome::Ok(inBand(authoring.applyTracked(),
        [](const auto& receipt) { return codec::encode(receipt); }, &codec::encodeApplyError));
  }
  if (method == "authoring.applyStatus") {
    std::optional<connection::authoring::ApplyId> id;
    if (const auto* v = p.find("id"); v && !v->isNull()) {
      if (!v->isString()) return bad("id");
      const auto parsed = connection::authoring::parseApplyId(v->asString());
      if (parsed.isErr()) return bad("id");
      id = parsed.value();
    }
    return Outcome::Ok(inBand(authoring.applyStatus(id),
        [](const auto& status) { return codec::encode(status); }, &codec::encodeApplyLookupError));
  }
  if (method == "authoring.apply") {
    return Outcome::Ok(inBand(authoring.apply(), &encodeNull, &codec::encodeApplyError));
  }
  if (method == "authoring.save") {
    return Outcome::Ok(inBand(authoring.save(), &encodeNull, &codec::encodeSaveError));
  }
  if (method == "authoring.updatePrefab" || method == "authoring.syncPrefab" || method == "authoring.replayPrefab") {
    const auto* node = p.find("node");
    const auto* expected = p.find("expected");
    const auto root = node ? codec::decodeEntity(*node) : std::nullopt;
    const auto token = expected ? codec::decodeDocumentToken(*expected) : std::nullopt;
    if (!p.isObject() || !root || *root == ac::EntityGuid{} || !token)
      return Outcome::Err("Prefab edits require node and expected document");
    const auto encodeEdit = [](const ac::PrefabEdit& edit) { return codec::encode(edit); };
    const auto encodeError = [](ac::PrefabEditError error) { return Value::string(std::string(ac::toString(error))); };
    if (method == "authoring.replayPrefab") {
      const auto* before = p.find("before");
      const auto* after = p.find("after");
      auto a = before ? codec::decodePrefabSubtree(*before) : std::nullopt;
      auto b = after ? codec::decodePrefabSubtree(*after) : std::nullopt;
      if (p.asObject().size() != 4 || !a || !b) return Outcome::Err("invalid Prefab replay");
      return Outcome::Ok(inBand(authoring.replayPrefab(*root, *a, *b, *token), encodeEdit, encodeError));
    }
    const auto* revision = p.find("revision");
    const auto* prefab = p.find("prefab");
    const auto guid = prefab ? codec::decodeAsset(*prefab) : std::nullopt;
    if (!revision || !revision->isString() || !ac::validPrefabDigest(revision->asString()) ||
        (prefab && (!guid || *guid == ac::AssetGuid{})) || p.asObject().size() != (prefab ? 4 : 3))
      return Outcome::Err("invalid Prefab revision or identity");
    if (method == "authoring.updatePrefab") {
      if (!guid) return Outcome::Err("Prefab source update requires the asset GUID");
      return Outcome::Ok(inBand(authoring.updatePrefab(*guid, revision->asString(), *root, *token),
          [](const ac::PrefabUpdate& update) { return codec::encode(update); }, &codec::encodeSaveAsError));
    }
    return Outcome::Ok(inBand(authoring.syncPrefab(*root, revision->asString(), *token, guid), encodeEdit, encodeError));
  }
  if (method == "authoring.createPrefab") {
    const auto* name = p.find("name");
    const auto* node = p.find("node");
    const auto* expected = p.find("expected");
    const auto guid = node ? codec::decodeEntity(*node) : std::nullopt;
    const auto token = expected ? codec::decodeDocumentToken(*expected) : std::nullopt;
    if (!p.isObject() || p.asObject().size() != 3 || !name || !name->isString() || !guid ||
        *guid == ac::EntityGuid{} || !token)
      return Outcome::Err("createPrefab requires name, node and expected document");
    return Outcome::Ok(inBand(authoring.createPrefab(name->asString(), *guid, *token),
        [](const ac::PrefabRegistration& receipt) { return codec::encode(receipt); }, &codec::encodeSaveAsError));
  }
  if (method == "authoring.createScene") {
    const auto* name = p.find("name");
    if (!name || !name->isString()) return Outcome::Err("createScene requires a name");
    return Outcome::Ok(inBand(authoring.createScene(name->asString()),
        [](const connection::authoring::CreatedScene& scene) { return codec::encode(scene); },
        &codec::encodeCreateSceneError));
  }
  if (method == "authoring.saveAs") {
    const auto* name = p.find("name");
    if (!name || !name->isString()) return Outcome::Err("saveAs requires a name");
    return Outcome::Ok(inBand(authoring.saveAs(name->asString()),
        [](const connection::authoring::SavedScene& scene) { return codec::encode(scene); }, &codec::encodeSaveAsError));
  }
  if (method == "authoring.document") {
    const auto document = authoring.document();
    return Outcome::Ok(document ? codec::encode(*document) : Value());
  }
  if (method == "authoring.listNodes") {
    return Outcome::Ok(inBand(
        authoring.listNodes(),
        [](const std::vector<ac::NodeSummary>& nodes) {
          Value out = Value::array();
          for (const ac::NodeSummary& node : nodes) out.push(codec::encode(node));
          return out;
        },
        &encodeAuthoringError));
  }
  if (method == "authoring.listComponents") {
    const auto node = decodeParam(p, "node", &codec::decodeEntity);
    if (!node) return bad("node");
    return Outcome::Ok(inBand(
        authoring.listComponents(*node),
        [](const std::vector<schema::TypeId>& types) {
          Value out = Value::array();
          for (const schema::TypeId type : types) out.push(codec::encodeTypeId(type));
          return out;
        },
        &encodeAuthoringError));
  }
  if (method == "authoring.getProperty") {
    const auto address = decodeParam(p, "address", &codec::decodeSceneAddress);
    if (!address) return bad("address");
    return Outcome::Ok(inBand(
        authoring.getProperty(*address), [](const access::PropertyValue& v) { return codec::encode(v); },
        &encodeAuthoringError));
  }
  if (method == "authoring.activeCamera") {
    return Outcome::Ok(inBand(
        authoring.activeCamera(),
        [](const std::optional<atlantis::asset_system::EntityGuid>& camera) {
          return camera ? codec::encodeEntity(*camera) : Value();
        },
        &encodeAuthoringError));
  }
  if (method == "authoring.schema") return Outcome::Ok(codec::encodeSchema(authoring.schema()));
  if (method == "authoring.submit") {
    auto command = decodeParam(p, "command", &codec::decodeAuthoringCommand);
    if (!command) return bad("command");
    return Outcome::Ok(Value::number(authoring.submit(std::move(*command)).value));
  }
  if (method == "authoring.submitTransaction") {
    const Value* list = p.find("commands");
    if (list == nullptr || !list->isArray()) return bad("commands");
    std::vector<ac::Command> commands;
    for (const Value& item : list->asArray()) {
      auto command = codec::decodeAuthoringCommand(item);
      if (!command) return bad("commands");
      commands.push_back(std::move(*command));
    }
    return Outcome::Ok(codec::encode(authoring.submitTransaction(std::move(commands))));
  }
  if (method == "authoring.subscribe") {
    auto filter = decodeParam(p, "filter", &codec::decodeAuthoringFilter);
    if (!filter) return bad("filter");
    return Outcome::Ok(Value::number(authoring.subscribe(std::move(*filter)).value));
  }
  if (method == "authoring.drainFailures") {
    Value out = Value::array();
    for (const ac::CommandFailure& failure : authoring.drainFailures()) out.push(codec::encode(failure));
    return Outcome::Ok(std::move(out));
  }
  if (method == "authoring.unsubscribe" || method == "authoring.drainEvents") {
    const auto subscription = decodeParam(p, "subscription", &codec::decodeUInt64);
    if (!subscription) return bad("subscription");
    const atlantis::connection::SubscriptionId id{*subscription};
    if (method == "authoring.unsubscribe") {
      return Outcome::Ok(inBand(authoring.unsubscribe(id), &encodeNull, &encodeConnectionError));
    }
    return Outcome::Ok(inBand(
        authoring.drainEvents(id),
        [](const std::vector<ac::Event>& events) {
          Value out = Value::array();
          for (const ac::Event& event : events) out.push(codec::encode(event));
          return out;
        },
        &encodeConnectionError));
  }
  return Outcome::Err("unknown method " + std::string(method));
}

// control.* other than step (P5): answered at once.
[[nodiscard]] Outcome callControl(atlantis::connection::RuntimeControl& control, std::string_view method,
                                  const Value& p) {
  if (method == "control.status") return Outcome::Ok(codec::encode(control.status()));
  if (method == "control.pause") {
    control.pause();
    return Outcome::Ok(Value());
  }
  if (method == "control.resume") {
    control.resume();
    return Outcome::Ok(Value());
  }
  if (method == "control.diagnostics") {
    const auto after = decodeParam(p, "after", &codec::decodeUInt64);
    const auto max = decodeParam(p, "max", &codec::decodeUInt64);
    if (!after || !max) return bad("after/max");
    return Outcome::Ok(codec::encode(control.diagnostics(*after, static_cast<std::size_t>(*max))));
  }
  // Spec 0064 / ADR-0128 D2: {"ok":<RayHit>|null} or {"err":"NoGeometry"}.
  if (method == "control.raycast") {
    const auto ray = decodeParam(p, "ray", &codec::decodeRay);
    if (!ray) return bad("ray");
    const auto hit = control.raycast(*ray);
    if (hit.isErr()) return Outcome::Ok(codec::err(codec::encodeControlError(hit.error())));
    return Outcome::Ok(codec::ok(hit.value().has_value() ? codec::encode(*hit.value()) : Value()));
  }
  if (method == "control.pickMaterialBinding") {
    namespace c = atlantis::connection;
    const auto request = codec::decodeMaterialBindingRequest(p);
    if (!request) return Outcome::Ok(codec::err(Value::string(std::string(c::toString(c::MaterialBindingPickError::InvalidRequest)))));
    const auto result = control.pickMaterialBinding(*request);
    return Outcome::Ok(result.isOk() ? codec::ok(codec::encode(result.value())) :
        codec::err(Value::string(std::string(c::toString(result.error())))));
  }
  if (method == "control.pickAlignmentFaces") {
    namespace c = atlantis::connection;
    const auto* values = p.find("rays");
    if (!p.isObject() || p.asObject().size() != 1 || !values || !values->isArray()) return bad("rays");
    const auto invalid = [] {
      return Outcome::Ok(codec::err(codec::encode(c::AlignmentFaceFailure{c::AlignmentFaceError::InvalidRequest})));
    };
    if (values->asArray().empty() || values->asArray().size() > c::kAlignmentRayLimit) return invalid();
    std::vector<c::Ray> rays;
    for (const auto& v : values->asArray()) {
      const auto vector = [&](std::string_view key) {
        const auto* coordinates = v.find(key);
        return coordinates && coordinates->isArray() && coordinates->asArray().size() == 3 &&
            std::ranges::all_of(coordinates->asArray(), [](const auto& n) { return n.isNumber(); });
      };
      if (!v.isObject() || v.asObject().size() != 2 || !vector("origin") || !vector("direction")) return bad("ray");
      const auto ray = codec::decodeRay(v);
      if (!ray || !c::validAlignmentRay(*ray)) return invalid();
      rays.push_back(*ray);
    }
    const auto result = control.pickAlignmentFaces(rays);
    return Outcome::Ok(result.isOk() ? codec::ok(codec::encode(result.value())) : codec::err(codec::encode(result.error())));
  }
  if (method == "control.viewportNavigation") {
    namespace c = atlantis::connection;
    const auto* values = p.find("targets");
    if (!p.isObject() || p.asObject().size() != 1 || !values || !values->isArray()) return bad("targets");
    const auto invalid = [] {
      return Outcome::Ok(codec::err(codec::encode(c::ViewportNavigationFailure{c::ViewportNavigationError::InvalidRequest})));
    };
    if (values->asArray().size() > c::kMaxViewportNavigationTargets) return invalid();
    std::vector<atlantis::asset_system::EntityGuid> targets;
    for (const auto& value : values->asArray()) {
      if (!value.isString()) return bad("target GUID");
      const auto parsed = atlantis::asset_system::parseEntityGuid(value.asString());
      if (parsed.isErr() && parsed.error() != atlantis::asset_system::GuidParseError::NilGuid) return bad("target GUID");
      targets.push_back(parsed.isOk() ? parsed.value() : atlantis::asset_system::EntityGuid{});
    }
    if (!c::validViewportNavigationTargets(targets)) return invalid();
    const auto result = control.viewportNavigation(targets);
    return Outcome::Ok(result.isOk() ? codec::ok(codec::encode(result.value())) : codec::err(codec::encode(result.error())));
  }
  return Outcome::Err("unknown method " + std::string(method));
}

}  // namespace

struct RemoteServer::State {
  os::Socket listener;
  std::string token;
  atlantis::asset_system::AssetGuid scene;
  OpenConnection openConnection;
  OpenAssets openAssets;
  OpenAuthoring openAuthoring;  // Plan 0065: serveAuthoring()
  atlantis::connection::RuntimeControl* control = nullptr;
  Limits limits;
  std::uint64_t nextClient = 1;
  std::map<std::uint64_t, Client> clients;
  // P3's parked FIFO: step requests awaiting their frame, in request order.
  struct Parked {
    std::uint64_t key = 0;
    std::uint64_t client = 0;
    Value id;
    std::optional<Value> response;  // set when the control completes the step
  };
  std::deque<Parked> parked;
  std::uint64_t nextParked = 1;
  std::weak_ptr<State> self;  // what a parked step's completion holds
  // Spec 0064 / ADR-0128 D1 (Plan 0064 J2): the client whose camera the
  // control draws from; cleared, with the camera, when that client goes.
  std::optional<std::uint64_t> cameraOwner;
  std::optional<std::uint64_t> visibilityOwner;

  atlantis::connection::ViewportVisibilityResult viewportVisibility(std::uint64_t clientKey) {
    namespace c = atlantis::connection;
    auto result = control->viewportVisibility();
    if (result.isErr()) return result;
    if (result.value().owner == c::ViewportOwner::None) visibilityOwner.reset();
    else if (visibilityOwner != clientKey) {
      result.value().owner = c::ViewportOwner::Other;
      result.value().policy.reset();
    }
    return result;
  }
  // Spec 0066 R2 / ADR-0132 D2 (Plan 0066 J2): each client's Viewport overlay;
  // the control draws their union, and a client's goes when it does.
  std::map<std::uint64_t, atlantis::connection::ViewportOverlay> overlays;

  void forget(std::uint64_t key) {
    if (cameraOwner == key && control != nullptr) {
      control->setViewportCamera(std::nullopt);
      cameraOwner.reset();
    }
    if (visibilityOwner == key && control != nullptr) {
      auto current = viewportVisibility(key);
      if (current.isOk() && current.value().owner == atlantis::connection::ViewportOwner::Caller)
        (void)control->setViewportVisibility(current.value().stamp, std::nullopt);
      visibilityOwner.reset();
    }
    if (overlays.erase(key) > 0) setOverlayUnion();
  }

  // Hands the control every client's overlay as one, in client order; none when no client has one.
  void setOverlayUnion() {
    if (control == nullptr) return;
    std::optional<atlantis::connection::ViewportOverlay> merged;
    for (const auto& [key, overlay] : overlays) {
      if (!merged) merged.emplace();
      merged->selection.insert(merged->selection.end(), overlay.selection.begin(), overlay.selection.end());
      merged->groups.insert(merged->groups.end(), overlay.groups.begin(), overlay.groups.end());
    }
    (void)control->setViewportOverlay(std::move(merged));
  }

  void resolve(std::uint64_t key, const atlantis::Result<atlantis::connection::FrameReport,
                                                         atlantis::connection::ControlError>& result) {
    for (Parked& entry : parked) {
      if (entry.key != key) continue;
      entry.response = response(entry.id, result.isOk() ? codec::ok(codec::encode(result.value()))
                                                        : codec::err(codec::encodeControlError(result.error())));
      return;
    }
  }

  // Queues every completed step at the head of the FIFO to its client.
  void releaseParked() {
    while (!parked.empty() && parked.front().response.has_value()) {
      const auto client = clients.find(parked.front().client);
      if (client != clients.end() && !client->second.closing &&
          !client->second.channel.queue(json::write(*parked.front().response))) {
        client->second.closing = true;  // over its write limit: dropped below
        client->second.overran = true;
      }
      parked.pop_front();
    }
  }

  void accept() {
    // Bounded per poll; the rest wait in the OS backlog for the next frame.
    for (int i = 0; i < 16; ++i) {
      os::Socket socket = os::acceptPending(listener);
      if (!socket.valid()) return;
      clients.emplace(nextClient++, Client(LineChannel(std::move(socket), limits.maxLineBytes, limits.maxPendingWriteBytes)));
    }
  }

  // One request line; returns the response to queue, or nullopt for a step
  // that was parked.
  std::optional<Value> dispatch(std::uint64_t clientKey, Client& client, std::string_view line) {
    const auto parsed = json::parse(line);
    if (parsed.isErr() || !parsed.value().isObject()) {
      return errorResponse(Value(), "BadRequest", "a request is one JSON object per line");
    }
    const Value& request = parsed.value();
    const Value* id = request.find("id");
    const Value* method = request.find("method");
    const Value* params = request.find("params");
    const Value requestId = id != nullptr ? *id : Value();
    if (method == nullptr || !method->isString()) return errorResponse(requestId, "BadRequest", "no method");
    const Value noParams = Value::object();
    const Value& p = params != nullptr ? *params : noParams;

    if (method->asString() == "hello") {
      if (client.connection) return errorResponse(requestId, "BadRequest", "hello already done");
      const Value* protocol = p.find("protocol");
      const Value* presented = p.find("token");
      client.closing = true;  // unless the hello succeeds
      if (protocol == nullptr || !protocol->isString() || protocol->asString() != kProtocol) {
        return errorResponse(requestId, "WrongProtocol", "this Runtime speaks atlantis.remote/1");
      }
      if (presented == nullptr || !presented->isString() || !tokensEqual(presented->asString(), token)) {
        return errorResponse(requestId, "BadToken", "the session token does not match");
      }
      client.closing = false;
      client.connection = openConnection();
      Value result = Value::object();
      result.set("protocol", Value::string(std::string(kProtocol)));
      result.set("scene", codec::encodeAsset(scene));
      return response(requestId, std::move(result));
    }
    if (!client.connection) {
      client.closing = true;
      return errorResponse(requestId, "HelloRequired", "the first request must be hello");
    }
    const std::string_view name = method->asString();
    if (name.starts_with("control.")) {
      if (control == nullptr) return errorResponse(requestId, "NoControl", "this server carries no RuntimeControl");
      if (name == "control.viewportVisibility" || name == "control.setViewportVisibility" ||
          name == "control.pickViewport") {
        namespace c = atlantis::connection;
        using E = c::ViewportVisibilityError;
        const auto reply = [&](const auto& result) {
          return response(requestId, result.isOk() ? codec::ok(codec::encode(result.value())) :
                                                    codec::err(codec::encode(result.error())));
        };
        const auto refuse = [&](E error) {
          return response(requestId, codec::err(codec::encode(c::ViewportVisibilityFailure{error})));
        };
        if (!p.isObject()) return refuse(E::InvalidRequest);
        if (name == "control.viewportVisibility") {
          if (!p.asObject().empty()) return refuse(E::InvalidRequest);
          return reply(viewportVisibility(clientKey));
        }
        const auto stamp = codec::decodeViewportStamp(p);
        if (!stamp) return refuse(E::InvalidRequest);
        const auto current = viewportVisibility(clientKey);
        if (current.isErr()) return reply(current);
        if (current.value().owner == c::ViewportOwner::Other) return refuse(E::NotOwner);
        if (name == "control.setViewportVisibility") {
          const auto* encoded = p.find("policy");
          if (!encoded || p.asObject().size() != 3) return refuse(E::InvalidRequest);
          std::optional<c::ViewportPolicy> policy;
          if (!encoded->isNull()) {
            E error;
            policy = codec::decodeViewportPolicy(*encoded, error);
            if (!policy) return refuse(error);
          }
          const bool claim = policy.has_value();
          const auto result = control->setViewportVisibility(*stamp, std::move(policy));
          if (result.isOk()) visibilityOwner = claim ? std::optional{clientKey} : std::nullopt;
          return reply(result);
        }
        const auto* kind = p.find("kind");
        const auto* rays = p.find("rays");
        if (!kind || !kind->isString() || !rays || p.asObject().size() != 4 ||
            (kind->asString() != "Selection" && kind->asString() != "Alignment"))
          return refuse(E::InvalidRequest);
        const auto mode = kind->asString() == "Alignment" ? c::ViewportPickKind::Alignment :
                                                           c::ViewportPickKind::Selection;
        const auto decoded = codec::decodeViewportRays(*rays, mode);
        if (!decoded) return refuse(E::InvalidRequest);
        return reply(control->pickViewport(*stamp, mode, *decoded));
      }
      // Spec 0064 / ADR-0128 D1: {"camera":<ViewportCamera>|null} -> null.
      if (name == "control.setViewportCamera") {
        const Value* camera = p.find("camera");
        if (camera == nullptr) return errorResponse(requestId, "BadRequest", "missing camera");
        if (camera->isNull()) {
          control->setViewportCamera(std::nullopt);
          cameraOwner.reset();
        } else {
          const auto decoded = codec::decodeViewportCamera(*camera);
          if (!decoded) return errorResponse(requestId, "BadRequest", "malformed camera");
          control->setViewportCamera(*decoded);
          cameraOwner = clientKey;
        }
        return response(requestId, Value());
      }
      // Spec 0066 / ADR-0132 D2: {"overlay":<ViewportOverlay>|null} -> {"ok":null} or {"err":"InvalidRequest"}.
      if (name == "control.setViewportOverlay") {
        const Value* overlay = p.find("overlay");
        if (overlay == nullptr) return errorResponse(requestId, "BadRequest", "missing overlay");
        if (overlay->isNull()) {
          if (overlays.erase(clientKey) > 0) setOverlayUnion();
          return response(requestId, codec::ok(Value()));
        }
        auto decoded = codec::decodeViewportOverlay(*overlay);
        if (!decoded) return errorResponse(requestId, "BadRequest", "malformed overlay");
        if (!atlantis::connection::overlayWellFormed(*decoded) || !atlantis::connection::overlayWithinLimits(*decoded)) {
          return response(requestId,
                          codec::err(codec::encodeControlError(atlantis::connection::ControlError::InvalidRequest)));
        }
        overlays[clientKey] = std::move(*decoded);
        setOverlayUnion();
        return response(requestId, codec::ok(Value()));
      }
      if (name == "control.step") {
        auto step = decodeParam(p, "request", &codec::decodeStepRequest);
        if (!step) return errorResponse(requestId, "BadRequest", "malformed or missing request");
        // Spec 0066 R5: a capture shows only the requester's own overlay, and only when asked.
        step->captureOverlay.reset();
        if (step->withOverlay) {
          if (const auto own = overlays.find(clientKey); own != overlays.end()) step->captureOverlay = own->second;
        }
        const std::uint64_t key = nextParked++;
        parked.push_back(Parked{key, clientKey, requestId, std::nullopt});
        control->step(*step, [weak = self, key](const auto& result) {
          if (const auto state = weak.lock()) state->resolve(key, result);
        });
        return std::nullopt;
      }
      Outcome outcome = callControl(*control, name, p);
      if (outcome.isErr()) return errorResponse(requestId, "BadRequest", outcome.error());
      return response(requestId, std::move(outcome.value()));
    }
    if (name == "assets.checkTextureRemoval" || name == "assets.textureRemovalCheckStatus" ||
        name == "assets.removeTexture") {
      namespace ca = connection::assets;
      using Guid = asset_system::AssetGuid;
      const bool status = name == "assets.textureRemovalCheckStatus";
      const bool remove = name == "assets.removeTexture";
      const auto guid = [&](std::string_view field) -> std::optional<Guid> {
        const auto* value = p.find(field);
        const auto id = value ? codec::decodeAsset(*value) : std::nullopt;
        return id && *id != Guid{} && codec::encodeAsset(*id) == *value ? id : std::nullopt;
      };
      if (!p.isObject() || p.asObject().size() != (status ? 1u : remove ? 3u : 2u))
        return errorResponse(requestId, "BadRequest", "malformed texture removal request");
      const auto check = guid("checkId");
      const auto asset = guid("asset");
      const auto generation = guid("expectedGeneration");
      if ((status && !check) || (!status && (!asset || !generation || (remove && !check))))
        return errorResponse(requestId, "BadRequest", "texture removal requires canonical non-nil GUIDs");
      if (!client.assets && openAssets) client.assets = openAssets();
      const auto reply = [&](const auto& result) {
        return response(requestId, result.isOk() ? codec::ok(codec::encode(result.value()))
            : codec::err(Value::string(std::string(ca::toString(result.error())))));
      };
      if (remove) return reply(client.assets ? client.assets->removeTexture({*asset, {*generation}}, {*check})
          : ca::RemoveTextureResult::Err(ca::TextureRemovalError::Unavailable));
      if (status) return reply(client.assets ? client.assets->textureRemovalCheckStatus({*check})
          : ca::TextureRemovalCheckResult::Err(ca::TextureRemovalError::Unavailable));
      return reply(client.assets ? client.assets->checkTextureRemoval({*asset, {*generation}})
          : ca::TextureRemovalCheckResult::Err(ca::TextureRemovalError::Unavailable));
    }
    if (name == "assets.renameTexture") {
      namespace ca = connection::assets;
      if (!p.isObject() || p.asObject().size() != 3 || !p.find("asset") ||
          !p.find("expectedGeneration") || !p.find("displayName"))
        return errorResponse(requestId, "BadRequest", "malformed texture rename request");
      const auto asset = codec::decodeAsset(*p.find("asset"));
      const auto generation = codec::decodeAsset(*p.find("expectedGeneration"));
      const auto* label = p.find("displayName");
      if (!asset || *asset == asset_system::AssetGuid{} || !generation || *generation == asset_system::AssetGuid{} ||
          codec::encodeAsset(*asset) != *p.find("asset") || codec::encodeAsset(*generation) != *p.find("expectedGeneration") ||
          !label->isString())
        return errorResponse(requestId, "BadRequest", "texture rename requires two non-nil GUIDs and a name");
      if (!client.assets && openAssets) client.assets = openAssets();
      const auto result = client.assets
          ? client.assets->renameTexture({*asset, {*generation}, label->asString()})
          : ca::RenameTextureResult::Err(ca::RenameTextureError::Unavailable);
      return response(requestId, result.isOk() ? codec::ok(codec::encode(result.value()))
          : codec::err(Value::string(std::string(ca::toString(result.error())))));
    }
    if (name == "assets.moveTexture") {
      namespace ca = connection::assets;
      if (!p.isObject() || p.asObject().size() != 4 || !p.find("asset") ||
          !p.find("expectedGeneration") || !p.find("expectedLogicalPath") || !p.find("destinationLogicalPath"))
        return errorResponse(requestId, "BadRequest", "malformed texture move request");
      const auto asset = codec::decodeAsset(*p.find("asset"));
      const auto generation = codec::decodeAsset(*p.find("expectedGeneration"));
      const auto* before = p.find("expectedLogicalPath");
      const auto* after = p.find("destinationLogicalPath");
      if (!asset || *asset == asset_system::AssetGuid{} || !generation || *generation == asset_system::AssetGuid{} ||
          codec::encodeAsset(*asset) != *p.find("asset") || codec::encodeAsset(*generation) != *p.find("expectedGeneration") ||
          !before->isString() || !after->isString())
        return errorResponse(requestId, "BadRequest", "texture move requires two non-nil GUIDs and two logical paths");
      if (!client.assets && openAssets) client.assets = openAssets();
      const auto result = client.assets
          ? client.assets->moveTexture({*asset, {*generation}, before->asString(), after->asString()})
          : ca::MoveTextureResult::Err(ca::MoveTextureError::Unavailable);
      return response(requestId, result.isOk() ? codec::ok(codec::encode(result.value()))
          : codec::err(Value::string(std::string(ca::toString(result.error())))));
    }
    if (name == "assets.staticGroupReimportReview" || name == "assets.commitStaticGroupReimport") {
      namespace ca = connection::assets;
      using Mode = ca::StaticGroupReviewMode;
      const bool commit = name == "assets.commitStaticGroupReimport";
      const auto* command = p.find("mode");
      const auto mode = command && command->isString() ? std::string_view(command->asString()) : std::string_view{};
      const auto* target = p.find(mode == "begin" ? "scene" : "id");
      const auto guid = target ? codec::decodeAsset(*target) : std::nullopt;
      if (!p.isObject() || p.asObject().size() != (commit ? 1u : 2u) || !guid ||
          *guid == asset_system::AssetGuid{} || codec::encodeAsset(*guid) != *target ||
          (!commit && mode != "begin" && mode != "read" && mode != "cancel"))
        return errorResponse(requestId, "BadRequest", "malformed static glTF reimport request");
      if (!client.assets && openAssets) client.assets = openAssets();
      const auto refuse = [&](ca::RebuildError refused) {
        return response(requestId, codec::err(Value::string(std::string(ca::toString(refused)))));
      };
      if (!client.assets) return refuse(ca::RebuildError::Unavailable);
      if (commit) {
        const auto result = client.assets->commitStaticGroupReimport(*guid);
        return result.isOk() ? response(requestId, codec::ok(codec::encode(result.value()))) : refuse(result.error());
      }
      const auto result = client.assets->staticGroupReimportReview({
          mode == "begin" ? Mode::Begin : mode == "read" ? Mode::Read : Mode::Cancel, *guid});
      if (result.isErr()) return refuse(result.error());
      auto encoded = ca::encodeStaticGroupReview(result.value());
      return encoded ? response(requestId, codec::ok(std::move(*encoded))) : refuse(ca::RebuildError::Unavailable);
    }
    if (name == "assets.rebuild" || name == "assets.rebuildStatus") {
      namespace ca = connection::assets;
      const bool start = name == "assets.rebuild";
      if (!p.isObject()) return errorResponse(requestId, "BadRequest", "malformed asset rebuild request");
      const auto* member = p.find(start ? "asset" : "jobId");
      if (p.asObject().size() != (member ? 1u : 0u))
        return errorResponse(requestId, "BadRequest", "unexpected asset rebuild parameter");
      std::optional<asset_system::AssetGuid> guid;
      if (member) guid = codec::decodeAsset(*member);
      if ((start || member) && (!guid || *guid == asset_system::AssetGuid{}))
        return errorResponse(requestId, "BadRequest", "asset rebuild requires a non-nil canonical GUID");
      if (!client.assets && openAssets) client.assets = openAssets();
      const auto reply = [&](const auto& result) {
        return response(requestId, result.isOk() ? codec::ok(codec::encode(result.value()))
            : codec::err(Value::string(std::string(ca::toString(result.error())))));
      };
      if (start) return reply(client.assets ? client.assets->rebuild(*guid)
                                          : ca::RebuildStartResult::Err(ca::RebuildError::Unavailable));
      return reply(client.assets ? client.assets->rebuildStatus(guid ? std::optional{ca::RebuildId{*guid}} : std::nullopt)
                                 : ca::RebuildStatusResult::Err(ca::RebuildError::Unavailable));
    }
    if (name == "assets.textureImport" || name == "assets.textureImportStatus" ||
        name == "assets.normalMapImport" || name == "assets.normalMapImportStatus" ||
        name == "assets.metallicRoughnessImport" || name == "assets.metallicRoughnessImportStatus" ||
        name == "assets.occlusionImport" || name == "assets.occlusionImportStatus") {
      namespace ca = connection::assets;
      const bool normal = name.starts_with("assets.normalMap");
      const bool packed = name.starts_with("assets.metallicRoughness");
      const bool occlusion = name.starts_with("assets.occlusion");
      const auto invalid = [&] { return errorResponse(requestId, "BadRequest", "invalid texture import parameters"); };
      std::uint64_t numericId = 0;
      if (!id || !id->toUInt64(numericId) || !params || request.asObject().size() != 3 || !p.isObject() ||
          ((normal || packed) && !numericId)) return invalid();
      const auto reply = [&](const auto& result) {
        return response(requestId, result.isOk() ? codec::ok(codec::encode(result.value()))
            : codec::err(Value::string(std::string(ca::toString(result.error())))));
      };
      if (name == "assets.textureImport" || name == "assets.normalMapImport" || name == "assets.metallicRoughnessImport" || name == "assets.occlusionImport") {
        const auto* slug = p.find("name");
        const auto* mode = p.find("mode");
        if (p.asObject().size() != 2 || !slug || !slug->isString() || !ca::validImportName(slug->asString()) ||
            !mode || !mode->isString() || (mode->asString() != "Register" && mode->asString() != "Resume"))
          return invalid();
        if (!client.assets && openAssets) client.assets = openAssets();
        if (!client.assets) return reply(ca::TextureImportStartResult::Err(ca::ImportError::Unavailable));
        const auto action = mode->asString() == "Register" ? ca::ImportMode::Register : ca::ImportMode::Resume;
        if (occlusion) return reply(client.assets->occlusionImport(slug->asString(), action));
        if (packed) return reply(client.assets->metallicRoughnessImport(slug->asString(), action));
        return reply(normal ? client.assets->normalMapImport(slug->asString(), action)
                            : client.assets->textureImport(slug->asString(), action));
      }
      const auto* member = p.find("jobId");
      const auto guid = member ? codec::decodeAsset(*member) : std::nullopt;
      if (p.asObject().size() != (member ? 1u : 0u) || (member && (!guid || *guid == asset_system::AssetGuid{})))
        return invalid();
      if (!client.assets && openAssets) client.assets = openAssets();
      if (!client.assets) return reply(ca::TextureImportStatusResult::Err(ca::ImportError::Unavailable));
      const auto job = guid ? std::optional{ca::TextureImportId{*guid}} : std::nullopt;
      if (occlusion) return reply(client.assets->occlusionImportStatus(job));
      if (packed) return reply(client.assets->metallicRoughnessImportStatus(job));
      return reply(normal ? client.assets->normalMapImportStatus(job) : client.assets->textureImportStatus(job));
    }
    if (name == "assets.import" || name == "assets.importStatus") {
      namespace ca = connection::assets;
      if (!p.isObject()) return errorResponse(requestId, "BadRequest", "malformed asset import request");
      const auto reply = [&](const auto& result) {
        return response(requestId, result.isOk() ? codec::ok(codec::encode(result.value()))
            : codec::err(Value::string(std::string(ca::toString(result.error())))));
      };
      if (name == "assets.import") {
        const auto* slug = p.find("name");
        const auto* mode = p.find("mode");
        if (p.asObject().size() != 2 || !slug || !slug->isString() || !ca::validImportName(slug->asString()) ||
            !mode || !mode->isString() || (mode->asString() != "Register" && mode->asString() != "Resume"))
          return errorResponse(requestId, "BadRequest", "invalid import name or mode");
        if (!client.assets && openAssets) client.assets = openAssets();
        return reply(client.assets ? client.assets->importAsset(slug->asString(), mode->asString() == "Register"
                        ? ca::ImportMode::Register : ca::ImportMode::Resume)
            : ca::ImportStartResult::Err(ca::ImportError::Unavailable));
      }
      const auto* member = p.find("jobId");
      const auto guid = member ? codec::decodeAsset(*member) : std::nullopt;
      if (p.asObject().size() != (member ? 1u : 0u) || (member && (!guid || *guid == asset_system::AssetGuid{})))
        return errorResponse(requestId, "BadRequest", "invalid import job ID");
      if (!client.assets && openAssets) client.assets = openAssets();
      return reply(client.assets ? client.assets->importStatus(guid ? std::optional{ca::ImportId{*guid}} : std::nullopt)
          : ca::ImportStatusResult::Err(ca::ImportError::Unavailable));
    }
    if (name == "assets.scanProject" || name == "assets.projectScanStatus") {
      namespace ca = connection::assets;
      const bool start = name == "assets.scanProject";
      const auto* member = p.find("id");
      const auto scanId = member ? codec::decodeAsset(*member) : std::nullopt;
      if (!p.isObject() || (start ? !p.asObject().empty() : p.asObject().size() != 1 || !scanId ||
          *scanId == asset_system::AssetGuid{} || asset_system::toString(*scanId) != member->asString()))
        return errorResponse(requestId, "BadRequest", "expected empty scan parameters or one canonical scan ID");
      if (!client.assets && openAssets) client.assets = openAssets();
      const auto result = client.assets ? (start ? client.assets->scanProject() : client.assets->projectScanStatus(*scanId))
          : ca::ProjectScanResult::Err(ca::ProjectScanError::Unsupported);
      return response(requestId, result.isOk() ? codec::ok(ca::encodeProjectScan(result.value()))
          : codec::err(Value::string(std::string(ca::toString(result.error())))));
    }
    if (name == "assets.prefabRevision") {
      namespace ca = connection::assets;
      const auto* member = p.find("guid");
      const auto guid = member ? codec::decodeAsset(*member) : std::nullopt;
      if (!p.isObject() || p.asObject().size() != 1 || !guid || *guid == asset_system::AssetGuid{})
        return errorResponse(requestId, "BadRequest", "Prefab revision requires one non-nil canonical GUID");
      if (!client.assets && openAssets) client.assets = openAssets();
      auto result = client.assets ? client.assets->prefabRevision(*guid)
                                  : ca::PrefabRevisionResult::Err(ca::StaticGroupError::CatalogUnavailable);
      if (result.isErr()) return response(requestId, codec::err(Value::string(std::string(ca::toString(result.error())))));
      Value payload = Value::object();
      payload.set("prefab", codec::encodeAsset(result.value().prefab));
      payload.set("revision", Value::string(result.value().revision));
      return response(requestId, codec::ok(std::move(payload)));
    }
    if (name == "assets.contentMetrics") {
      namespace ca = connection::assets;
      const auto* ids = p.find("guids");
      const auto guids = ids ? ca::decodeContentMetricsGuids(*ids) : std::nullopt;
      if (!p.isObject() || p.asObject().size() != 1 || !guids)
        return errorResponse(requestId, "BadRequest", "expected one to 32 distinct canonical asset GUIDs");
      if (!client.assets && openAssets) client.assets = openAssets();
      const auto result = client.assets ? client.assets->contentMetrics(*guids)
          : ca::ContentMetricsResult::Err(ca::ContentMetricsError::CatalogUnavailable);
      if (result.isErr()) return response(requestId, codec::err(Value::string(std::string(ca::toString(result.error())))));
      const auto encoded = ca::encodeContentMetrics(result.value(), *guids);
      if (!encoded) return errorResponse(requestId, "BadRequest", "invalid content metrics result");
      return response(requestId, codec::ok(*encoded));
    }
    if (name == "assets.staticGroup" || name == "assets.prefab") {
      namespace ca = connection::assets;
      const bool prefab = name == "assets.prefab";
      const auto* member = p.find("guid");
      const auto guid = member ? codec::decodeAsset(*member) : std::nullopt;
      if (!p.isObject() || p.asObject().size() != 1 || !guid || *guid == asset_system::AssetGuid{})
        return errorResponse(requestId, "BadRequest", "static group requires one non-nil canonical Scene GUID");
      if (!client.assets && openAssets) client.assets = openAssets();
      auto result = client.assets ? (prefab ? client.assets->prefab(*guid) : client.assets->staticGroup(*guid))
                                  : ca::StaticGroupResult::Err(ca::StaticGroupError::CatalogUnavailable);
      if (result.isOk()) {
        if (auto group = ca::encodeStaticGroup(result.value(), *guid, prefab))
          return response(requestId, codec::ok(std::move(*group)));
      }
      const auto error = result.isErr() ? result.error() : ca::StaticGroupError::InvalidGroup;
      return response(requestId, codec::err(Value::string(std::string(ca::toString(error)))));
    }
    if (name == "assets.previewMesh") {
      namespace ca = connection::assets;
      const auto* member = p.find("guid");
      const auto guid = member ? codec::decodeAsset(*member) : std::nullopt;
      if (!p.isObject() || p.asObject().size() != 1 || !guid || *guid == asset_system::AssetGuid{})
        return errorResponse(requestId, "BadRequest", "asset preview requires one non-nil canonical GUID");
      if (!client.assets && openAssets) client.assets = openAssets();
      auto result = client.assets ? client.assets->previewMesh(*guid)
                                  : ca::MeshPreviewResult::Err(ca::PreviewError::Unsupported);
      if (result.isOk()) {
        if (auto image = ca::encodeMeshPreview(result.value(), *guid))
          return response(requestId, codec::ok(std::move(*image)));
      }
      const auto error = result.isErr() ? result.error() : ca::PreviewError::RenderFailed;
      return response(requestId, codec::err(Value::string(std::string(ca::toString(error)))));
    }
    if (name == "assets.previewMeshMaterial") {
      namespace ca = connection::assets;
      const auto* mesh = p.find("meshGuid");
      const auto* material = p.find("materialGuid");
      const auto meshGuid = mesh ? codec::decodeAsset(*mesh) : std::nullopt;
      const auto materialGuid = material ? codec::decodeAsset(*material) : std::nullopt;
      if (!p.isObject() || p.asObject().size() != 2 || !meshGuid || *meshGuid == asset_system::AssetGuid{} ||
          !materialGuid || *materialGuid == asset_system::AssetGuid{})
        return errorResponse(requestId, "BadRequest", "asset pair preview requires non-nil canonical mesh and material GUIDs");
      if (!client.assets && openAssets) client.assets = openAssets();
      const auto result = client.assets
          ? client.assets->previewMeshMaterial(*meshGuid, *materialGuid)
          : ca::MeshMaterialPreviewResult::Err(ca::MeshMaterialPreviewError::Unsupported);
      if (result.isOk()) {
        if (auto image = ca::encodeMeshMaterialPreview(result.value(), *meshGuid, *materialGuid))
          return response(requestId, codec::ok(std::move(*image)));
      }
      const auto error = result.isErr() ? result.error() : ca::MeshMaterialPreviewError::RenderFailed;
      return response(requestId, codec::err(Value::string(std::string(ca::toString(error)))));
    }
    if (name == "assets.materialRead" || name == "assets.materialSave" || name == "assets.materialStatus" ||
        name == "assets.previewMaterialDraft" || name == "assets.materialReadWithMaps" ||
        name == "assets.materialSaveWithMaps" || name == "assets.previewMaterialDraftWithMaps" ||
        name == "assets.materialReadWithMetallicRoughness" || name == "assets.materialSaveWithMetallicRoughness" ||
        name == "assets.previewMaterialDraftWithMetallicRoughness" || name == "assets.materialReadWithOcclusion" ||
        name == "assets.materialSaveWithOcclusion" || name == "assets.previewMaterialDraftWithOcclusion" ||
        name == "assets.materialReadWithCombinedMaps" || name == "assets.materialSaveWithCombinedMaps" ||
        name == "assets.previewMaterialDraftWithCombinedMaps") {
      const bool packed = name.ends_with("WithMetallicRoughness");
      const bool occlusion = name.ends_with("WithOcclusion");
      const bool combined = name.ends_with("WithCombinedMaps");
      const bool withMaps = combined || occlusion || packed || name.ends_with("WithMaps");
      namespace ca = connection::assets;
      using Guid = asset_system::AssetGuid;
      const auto version = combined ? ca::MaterialRecipeVersion::WithCombinedMaps :
          occlusion ? ca::MaterialRecipeVersion::WithOcclusion :
          packed ? ca::MaterialRecipeVersion::WithMetallicRoughness :
          withMaps ? ca::MaterialRecipeVersion::WithMaps : ca::MaterialRecipeVersion::Basic;
      const auto bad = [&] { return errorResponse(requestId, "BadRequest", "invalid material operation parameters"); };
      const auto guid = [&](std::string_view field) -> std::optional<Guid> {
        const auto* member = p.find(field);
        auto value = member ? codec::decodeAsset(*member) : std::nullopt;
        return value && *value != Guid{} && (!withMaps || (member->isString() &&
            member->asString() == asset_system::toString(*value))) ? value : std::nullopt;
      };
      const auto open = [&] {
        if (!client.assets && openAssets) client.assets = openAssets();
        return client.assets.get();
      };
      const auto reply = [&](const auto& result, auto encode) {
        return response(requestId, result.isOk() ? codec::ok(encode(result.value()))
            : codec::err(Value::string(std::string(ca::toString(result.error())))));
      };
      const auto count = [&](std::string_view key) {
        return std::ranges::count_if(request.asObject(), [key](const auto& member) { return member.first == key; });
      };
      std::uint64_t materialRequestId = 0;
      if (!p.isObject() || !id || !id->toUInt64(materialRequestId) || materialRequestId == 0 ||
          count("id") != 1 || count("method") != 1 || count("params") > 1 ||
          (withMaps && (count("params") != 1 || request.asObject().size() != 3))) return bad();
      if (name.starts_with("assets.materialRead")) {
        const auto material = guid("material");
        if (!material || p.asObject().size() != 1) return bad();
        auto* provider = open();
        return reply(provider ? (combined ? provider->materialReadWithCombinedMaps(*material) :
                                  occlusion ? provider->materialReadWithOcclusion(*material) :
                                  packed ? provider->materialReadWithMetallicRoughness(*material) :
                                  withMaps ? provider->materialReadWithMaps(*material) : provider->materialRead(*material))
                              : ca::MaterialReadResult::Err(ca::MaterialError::CatalogUnavailable),
                     [version](const auto& value) { return ca::encodeMaterialRead(value, version); });
      }
      if (name.starts_with("assets.materialSave")) {
        const auto materialSave = ca::decodeMaterialSave(p, version);
        if (!materialSave) return bad();
        auto* provider = open();
        return reply(provider ? (combined ? provider->materialSaveWithCombinedMaps(*materialSave) :
                                  occlusion ? provider->materialSaveWithOcclusion(*materialSave) :
                                  packed ? provider->materialSaveWithMetallicRoughness(*materialSave) :
                                  withMaps ? provider->materialSaveWithMaps(*materialSave) : provider->materialSave(*materialSave))
                              : ca::MaterialSaveResult::Err(ca::MaterialError::Unavailable), ca::encodeMaterialJob);
      }
      if (name == "assets.materialStatus") {
        const auto job = guid("jobId");
        if (p.asObject().size() != (job ? 1u : 0u)) return bad();
        auto* provider = open();
        return reply(provider ? provider->materialStatus(job ? std::optional{ca::MaterialJobId{*job}} : std::nullopt)
                              : ca::MaterialStatusResult::Err(ca::MaterialError::Unavailable), ca::encodeMaterialStatus);
      }
      const auto mesh = guid("mesh");
      const auto* member = p.find("recipe");
      const auto recipe = member ? ca::decodeMaterialRecipe(*member, version) : std::nullopt;
      if (!mesh || !recipe || p.asObject().size() != 2) return bad();
      auto* provider = open();
      auto result = provider ? (combined ? provider->previewMaterialDraftWithCombinedMaps(*mesh, *recipe) :
                                  occlusion ? provider->previewMaterialDraftWithOcclusion(*mesh, *recipe) :
                               packed ? provider->previewMaterialDraftWithMetallicRoughness(*mesh, *recipe) :
                               withMaps ? provider->previewMaterialDraftWithMaps(*mesh, *recipe)
                                         : provider->previewMaterialDraft(*mesh, *recipe))
                             : ca::MaterialDraftPreviewResult::Err(withMaps ? ca::MaterialError::Unavailable
                                                                           : ca::MaterialError::Unsupported);
      if (result.isOk())
        if (auto png = ca::encodeMaterialDraftPreview(result.value(), version))
          return response(requestId, codec::ok(std::move(*png)));
      return response(requestId, codec::err(Value::string(std::string(
          ca::toString(result.isErr() ? result.error() : ca::MaterialError::RenderFailed)))));
    }
    if (name.starts_with("assets.")) {
      std::optional<asset_system::AssetGuid> guid;
      if (!p.isObject() || (name != "assets.list" && name != "assets.get"))
        return errorResponse(requestId, "BadRequest", "unknown or malformed asset query");
      if (name == "assets.get") {
        const auto* member = p.find("guid");
        if (member) guid = codec::decodeAsset(*member);
        if (!guid || *guid == asset_system::AssetGuid{}) return errorResponse(requestId, "BadRequest", "malformed or missing guid");
      }
      if (!client.assets && openAssets) client.assets = openAssets();
      if (!client.assets) return errorResponse(requestId, "NoAssets", "this Runtime serves no asset catalog");
      if (guid) {
        auto result = client.assets->getAsset(*guid);
        return response(requestId, result.isOk() ? codec::ok(codec::encode(result.value()))
            : codec::err(Value::string(std::string(connection::assets::toString(result.error())))));
      }
      auto result = client.assets->listAssets();
      if (result.isErr()) return response(requestId, codec::err(Value::string(std::string(connection::assets::toString(result.error())))));
      Value items = Value::array();
      for (const auto& item : result.value()) items.push(codec::encode(item));
      return response(requestId, codec::ok(std::move(items)));
    }
    if (name.starts_with("authoring.")) {
      if (!client.authoring && openAuthoring) client.authoring = openAuthoring();
      if (!client.authoring) {
        return errorResponse(requestId, "NoAuthoring", "this Runtime serves no authoring session");
      }
      Outcome outcome = callAuthoring(*client.authoring, name, p);
      if (outcome.isErr()) return errorResponse(requestId, "BadRequest", outcome.error());
      return response(requestId, std::move(outcome.value()));
    }
    Outcome outcome = callConnection(*client.connection, name, p);
    if (outcome.isErr()) return errorResponse(requestId, "BadRequest", outcome.error());
    return response(requestId, std::move(outcome.value()));
  }

  // Reads and answers what `client` sent; false if it must be dropped (it
  // closed, failed, sent a line over the limit, or overran its write limit).
  [[nodiscard]] bool serve(std::uint64_t clientKey, Client& client) {
    std::vector<std::string> lines;
    const LineChannel::ReadStatus status = client.channel.read(lines);
    for (const std::string& line : lines) {
      if (client.closing) break;  // after a refused hello nothing more is answered
      const std::optional<Value> reply = dispatch(clientKey, client, line);
      if (reply && !client.channel.queue(json::write(*reply))) return false;
    }
    return status == LineChannel::ReadStatus::Ok;
  }
};

RemoteServer::RemoteServer(Key, std::shared_ptr<State> state) : state_(std::move(state)) {}

RemoteServer::~RemoteServer() = default;

atlantis::Result<std::unique_ptr<RemoteServer>, ListenError> RemoteServer::listen(
    std::uint16_t port, std::string token, atlantis::asset_system::AssetGuid scene, OpenConnection openConnection,
    atlantis::connection::RuntimeControl* control, Limits limits) {
  using ResultT = atlantis::Result<std::unique_ptr<RemoteServer>, ListenError>;
  auto listener = os::listenLoopback(port);
  if (listener.isErr()) {
    const os::SocketError error = listener.error();
    return ResultT::Err(error == os::SocketError::BindFailed || error == os::SocketError::ListenFailed
                            ? ListenError::PortUnavailable
                            : ListenError::SocketUnavailable);
  }
  auto state = std::make_shared<State>();
  state->listener = std::move(listener.value());
  state->token = std::move(token);
  state->scene = scene;
  state->openConnection = std::move(openConnection);
  state->control = control;
  state->limits = limits;
  state->self = state;
  return ResultT::Ok(std::make_unique<RemoteServer>(Key{}, std::move(state)));
}

SessionInfo RemoteServer::session() const {
  SessionInfo session;
  session.port = os::localPort(state_->listener);
  session.token = state_->token;
  session.pid = os::currentProcessId();
  session.scene = state_->scene;
  return session;
}

void RemoteServer::poll() {
  State& state = *state_;
  state.accept();
  std::vector<std::uint64_t> dropped;
  for (auto& [key, client] : state.clients) {
    if (!client.closing && !state.serve(key, client)) dropped.push_back(key);
  }
  for (const std::uint64_t key : dropped) {
    state.forget(key);
    state.clients.erase(key);
  }
  state.releaseParked();
  for (auto it = state.clients.begin(); it != state.clients.end();) {
    Client& client = it->second;
    bool keep = !client.overran && client.channel.flush();
    if (client.closing && !client.channel.hasPendingWrite()) keep = false;
    if (!keep) state.forget(it->first);
    it = keep ? std::next(it) : state.clients.erase(it);
  }
}

std::size_t RemoteServer::clientCount() const noexcept { return state_->clients.size(); }

void RemoteServer::serveAssets(OpenAssets open) { state_->openAssets = std::move(open); }

void RemoteServer::serveAuthoring(OpenAuthoring open) { state_->openAuthoring = std::move(open); }

}  // namespace atlantis::remote
