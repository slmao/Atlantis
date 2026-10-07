#include <atlantis/remote/remote_client.h>

#include <atlantis/assert.h>

#include "codec.h"
#include "line_channel.h"
#include "os/socket.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <system_error>
#include <utility>

namespace atlantis::remote {

namespace json = connection::json;
namespace access = atlantis::world::access;
using atlantis::asset_system::EntityGuid;
using json::Value;

std::filesystem::path resolveSessionPath(const std::optional<std::string>& explicitPath, const char* environmentValue) {
  if (explicitPath.has_value()) return std::filesystem::path(*explicitPath);
  if (environmentValue != nullptr && *environmentValue != '\0') return std::filesystem::path(environmentValue);
  return defaultSessionPath();
}

atlantis::Result<SessionInfo, SessionFileError> readSessionFile(const std::filesystem::path& path) {
  using ResultT = atlantis::Result<SessionInfo, SessionFileError>;
  std::error_code error;
  if (!std::filesystem::exists(path, error)) return ResultT::Err(SessionFileError::NotFound);
  std::ifstream in(path, std::ios::binary);
  if (!in) return ResultT::Err(SessionFileError::Unreadable);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return parseSession(text);
}

std::string_view toString(RemoteError error) noexcept {
  switch (error) {
    case RemoteError::ConnectFailed: return "ConnectFailed";
    case RemoteError::BadToken: return "BadToken";
    case RemoteError::WrongProtocol: return "WrongProtocol";
    case RemoteError::Disconnected: return "Disconnected";
    case RemoteError::Timeout: return "Timeout";
    case RemoteError::ProtocolError: return "ProtocolError";
  }
  ATLANTIS_CHECK_MSG(false, "toString(RemoteError): unhandled enumerator");
  return "(unrecognized RemoteError)";
}

namespace {

constexpr std::size_t kMaxResponseLineBytes = 256 * 1024 * 1024;  // a large scene's listEntities
constexpr std::size_t kMaxPendingRequestBytes = 64 * 1024 * 1024;

class RemoteConnection;

}  // namespace

struct RemoteSession::State {
  State(LineChannel&& line, RemoteOptions remoteOptions)
      : channel(std::move(line)), options(std::move(remoteOptions)) {}

  LineChannel channel;
  RemoteOptions options;
  std::uint64_t nextId = 1;
  std::map<std::uint64_t, Value> responses;  // by id: arrived, not yet taken
  std::optional<RemoteError> failure;
  atlantis::asset_system::AssetGuid scene;
  codec::OwnedSchema schema;
  std::unique_ptr<atlantis::connection::RuntimeConnection> connection;
  std::unique_ptr<atlantis::connection::RuntimeControl> control;

  void fail(RemoteError error) {
    if (!failure) failure = error;
  }

  // Queues one request; its id, or 0 after a failure.
  std::uint64_t send(std::string_view method, Value params) {
    if (failure) return 0;
    const std::uint64_t id = nextId++;
    Value request = Value::object();
    request.set("id", Value::number(id));
    request.set("method", Value::string(std::string(method)));
    request.set("params", std::move(params));
    if (!channel.queue(json::write(request))) {
      fail(RemoteError::ProtocolError);
      return 0;
    }
    return id;
  }

  // Takes in every complete response line; false if the channel failed.
  bool receive() {
    std::vector<std::string> lines;
    const LineChannel::ReadStatus status = channel.read(lines);
    for (const std::string& line : lines) {
      auto parsed = json::parse(line);
      std::uint64_t id = 0;
      if (parsed.isErr() || !parsed.value().isObject() || parsed.value().find("id") == nullptr ||
          !parsed.value().find("id")->toUInt64(id)) {
        fail(RemoteError::ProtocolError);
        return false;
      }
      responses.insert_or_assign(id, std::move(parsed.value()));
    }
    if (status != LineChannel::ReadStatus::Ok) {
      fail(status == LineChannel::ReadStatus::Closed ? RemoteError::Disconnected : RemoteError::ProtocolError);
      return false;
    }
    return true;
  }

  // The whole response object for `id`, once it arrives; nullopt on failure.
  std::optional<Value> awaitResponse(std::uint64_t id) {
    if (failure || id == 0) return std::nullopt;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(options.responseTimeoutMilliseconds);
    while (true) {
      if (const auto found = responses.find(id); found != responses.end()) {
        Value response = std::move(found->second);
        responses.erase(found);
        return response;
      }
      if (!channel.flush()) {
        fail(RemoteError::Disconnected);
        return std::nullopt;
      }
      const bool alive = receive();
      if (responses.count(id) != 0) continue;
      if (!alive) return std::nullopt;
      const auto now = std::chrono::steady_clock::now();
      if (now >= deadline) {
        fail(RemoteError::Timeout);
        return std::nullopt;
      }
      if (options.whileWaiting) {
        options.whileWaiting();
      } else {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
        (void)os::waitReadable(channel.socket(), static_cast<int>(std::min<long long>(remaining, 50)));
      }
    }
  }

  // The result of `id`; nullopt (and a failure) for an error response.
  std::optional<Value> awaitResult(std::uint64_t id) {
    std::optional<Value> response = awaitResponse(id);
    if (!response) return std::nullopt;
    if (const Value* result = response->find("result")) return *result;
    fail(RemoteError::ProtocolError);
    return std::nullopt;
  }

  std::optional<Value> call(std::string_view method, Value params = Value::object()) {
    return awaitResult(send(method, std::move(params)));
  }
};

namespace {

// Decodes an in-band {"ok":...}/{"err":"..."} result.
template <typename T, typename E, typename DecodeOk, typename DecodeErr>
[[nodiscard]] std::optional<atlantis::Result<T, E>> decodeInBand(const Value& value, DecodeOk decodeOk,
                                                                  DecodeErr decodeErr) {
  if (const Value* ok = value.find("ok")) {
    std::optional<T> decoded = decodeOk(*ok);
    if (!decoded) return std::nullopt;
    return atlantis::Result<T, E>::Ok(std::move(*decoded));
  }
  if (const Value* err = value.find("err")) {
    const std::optional<E> decoded = decodeErr(*err);
    if (!decoded) return std::nullopt;
    return atlantis::Result<T, E>::Err(*decoded);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::vector<schema::TypeId>> decodeTypeIds(const Value& value) {
  if (!value.isArray()) return std::nullopt;
  std::vector<schema::TypeId> out;
  for (const Value& item : value.asArray()) {
    const auto id = codec::decodeTypeId(item);
    if (!id) return std::nullopt;
    out.push_back(*id);
  }
  return out;
}

using ComponentsResult = atlantis::Result<std::vector<schema::TypeId>, access::AccessError>;
using PropertyResult = atlantis::Result<access::PropertyValue, access::AccessError>;

[[nodiscard]] Value entityParams(const EntityGuid& entity) {
  Value params = Value::object();
  params.set("entity", codec::encodeEntity(entity));
  return params;
}

[[nodiscard]] Value addressParams(const access::PropertyAddress& address) {
  Value params = Value::object();
  params.set("address", codec::encode(address));
  return params;
}

// Plan 0055 P2: RuntimeConnection over the wire. Each call is one request;
// its result decodes into the interface's own value types.
class RemoteConnection final : public atlantis::connection::RuntimeConnection {
 public:
  explicit RemoteConnection(RemoteSession::State& state) : state_(&state) {}

  bool findEntity(const EntityGuid& entity) const override {
    const auto result = state_->call("connection.findEntity", entityParams(entity));
    if (!result || !result->isBool()) return failed(false);
    return result->asBool();
  }
  std::vector<EntityGuid> listEntities() const override {
    const auto result = state_->call("connection.listEntities");
    std::vector<EntityGuid> out;
    if (!result || !result->isArray()) return failed(out);
    for (const Value& item : result->asArray()) {
      const auto entity = codec::decodeEntity(item);
      if (!entity) return failed(std::vector<EntityGuid>{});
      out.push_back(*entity);
    }
    return out;
  }
  ComponentsResult listComponents(const EntityGuid& entity) const override {
    return decodeComponents(state_->call("connection.listComponents", entityParams(entity)));
  }
  PropertyResult getProperty(const access::PropertyAddress& address) const override {
    return decodeProperty(state_->call("connection.getProperty", addressParams(address)));
  }
  std::span<const schema::TypeDescriptor> schema() const override { return state_->schema.types(); }

  access::CommandTicket submit(access::Command command) override {
    Value params = Value::object();
    params.set("command", codec::encode(command));
    const auto result = state_->call("connection.submit", std::move(params));
    const auto ticket = result ? codec::decodeUInt64(*result) : std::nullopt;
    if (!ticket) return failed(access::CommandTicket{});
    return access::CommandTicket{*ticket};
  }
  access::TransactionTicket submitTransaction(std::vector<access::Command> commands) override {
    Value list = Value::array();
    for (const access::Command& command : commands) list.push(codec::encode(command));
    Value params = Value::object();
    params.set("commands", std::move(list));
    const auto result = state_->call("connection.submitTransaction", std::move(params));
    const auto ticket = result ? codec::decodeTransactionTicket(*result) : std::nullopt;
    if (!ticket) return failed(access::TransactionTicket{});
    return *ticket;
  }

  atlantis::connection::SubscriptionId subscribe(atlantis::connection::EventFilter filter) override {
    Value params = Value::object();
    params.set("filter", codec::encode(filter));
    const auto result = state_->call("connection.subscribe", std::move(params));
    const auto id = result ? codec::decodeUInt64(*result) : std::nullopt;
    if (!id) return failed(atlantis::connection::SubscriptionId{});
    return atlantis::connection::SubscriptionId{*id};
  }
  atlantis::Result<std::monostate, atlantis::connection::ConnectionError> unsubscribe(
      atlantis::connection::SubscriptionId subscription) override {
    using ResultT = atlantis::Result<std::monostate, atlantis::connection::ConnectionError>;
    const auto result = state_->call("connection.unsubscribe", subscriptionParams(subscription));
    const auto decoded = result ? decodeInBand<std::monostate, atlantis::connection::ConnectionError>(
                                      *result,
                                      [](const Value& v) -> std::optional<std::monostate> {
                                        if (!v.isNull()) return std::nullopt;
                                        return std::monostate{};
                                      },
                                      &codec::decodeConnectionError)
                                : std::nullopt;
    if (!decoded) return failed(ResultT::Err(atlantis::connection::ConnectionError::UnknownSubscription));
    return *decoded;
  }
  atlantis::Result<std::vector<access::Event>, atlantis::connection::ConnectionError> drainEvents(
      atlantis::connection::SubscriptionId subscription) override {
    using ResultT = atlantis::Result<std::vector<access::Event>, atlantis::connection::ConnectionError>;
    const auto result = state_->call("connection.drainEvents", subscriptionParams(subscription));
    const auto decoded = result ? decodeInBand<std::vector<access::Event>, atlantis::connection::ConnectionError>(
                                      *result,
                                      [](const Value& v) -> std::optional<std::vector<access::Event>> {
                                        if (!v.isArray()) return std::nullopt;
                                        std::vector<access::Event> events;
                                        for (const Value& item : v.asArray()) {
                                          auto event = codec::decodeEvent(item);
                                          if (!event) return std::nullopt;
                                          events.push_back(std::move(*event));
                                        }
                                        return events;
                                      },
                                      &codec::decodeConnectionError)
                                : std::nullopt;
    if (!decoded) return failed(ResultT::Err(atlantis::connection::ConnectionError::UnknownSubscription));
    return *decoded;
  }
  std::vector<access::CommandFailure> drainFailures() override {
    const auto result = state_->call("connection.drainFailures");
    std::vector<access::CommandFailure> out;
    if (!result || !result->isArray()) return failed(out);
    for (const Value& item : result->asArray()) {
      const auto failure = codec::decodeFailure(item);
      if (!failure) return failed(std::vector<access::CommandFailure>{});
      out.push_back(*failure);
    }
    return out;
  }

  ComponentsResult decodeComponents(const std::optional<Value>& result) const {
    const auto decoded = result ? decodeInBand<std::vector<schema::TypeId>, access::AccessError>(
                                      *result, &decodeTypeIds, &codec::decodeAccessError)
                                : std::nullopt;
    if (!decoded) return failed(ComponentsResult::Err(access::AccessError::UnknownEntity));
    return *decoded;
  }
  PropertyResult decodeProperty(const std::optional<Value>& result) const {
    const auto decoded = result ? decodeInBand<access::PropertyValue, access::AccessError>(
                                      *result, &codec::decodePropertyValue, &codec::decodeAccessError)
                                : std::nullopt;
    if (!decoded) return failed(PropertyResult::Err(access::AccessError::UnknownEntity));
    return *decoded;
  }

 private:
  // The empty value a call returns once the session has failed; a result
  // that does not decode is itself a failure.
  template <typename T>
  T failed(T empty) const {
    state_->fail(RemoteError::ProtocolError);  // no-op if a failure is already recorded
    return empty;
  }
  static Value subscriptionParams(atlantis::connection::SubscriptionId subscription) {
    Value params = Value::object();
    params.set("subscription", Value::number(subscription.value));
    return params;
  }

  RemoteSession::State* state_;
};

// Plan 0055 P5: RuntimeControl over the wire.
class RemoteControl final : public atlantis::connection::RuntimeControl {
 public:
  explicit RemoteControl(RemoteSession::State& state) : state_(&state) {}

  atlantis::connection::RuntimeStatus status() override {
    const auto result = state_->call("control.status");
    const auto status = result ? codec::decodeStatus(*result) : std::nullopt;
    if (!status) {
      state_->fail(RemoteError::ProtocolError);
      return {};
    }
    return *status;
  }
  void pause() override { (void)state_->call("control.pause"); }
  void resume() override { (void)state_->call("control.resume"); }
  void step(atlantis::connection::StepRequest request,
            std::function<void(atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>)>
                done) override {
    using ResultT = atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>;
    Value params = Value::object();
    params.set("request", codec::encode(request));
    const auto result = state_->call("control.step", std::move(params));
    const auto decoded = result ? decodeInBand<atlantis::connection::FrameReport, atlantis::connection::ControlError>(
                                      *result, &codec::decodeFrameReport, &codec::decodeControlError)
                                : std::nullopt;
    if (!decoded) {
      state_->fail(RemoteError::ProtocolError);
      done(ResultT::Err(atlantis::connection::ControlError::Stopped));
      return;
    }
    done(*decoded);
  }
  atlantis::connection::DiagnosticBatch diagnostics(std::uint64_t afterSequence, std::size_t max) override {
    Value params = Value::object();
    params.set("after", Value::number(afterSequence));
    params.set("max", Value::number(static_cast<std::uint64_t>(max)));
    const auto result = state_->call("control.diagnostics", std::move(params));
    const auto batch = result ? codec::decodeDiagnosticBatch(*result) : std::nullopt;
    if (!batch) {
      state_->fail(RemoteError::ProtocolError);
      return {};
    }
    return *batch;
  }

 private:
  RemoteSession::State* state_;
};

}  // namespace

RemoteSession::RemoteSession(std::unique_ptr<State> state) : state_(std::move(state)) {}

RemoteSession::~RemoteSession() = default;

atlantis::connection::RuntimeConnection& RemoteSession::connection() noexcept { return *state_->connection; }

atlantis::connection::RuntimeControl& RemoteSession::control() noexcept { return *state_->control; }

const atlantis::asset_system::AssetGuid& RemoteSession::scene() const noexcept { return state_->scene; }

std::optional<RemoteError> RemoteSession::failure() const noexcept { return state_->failure; }

std::vector<atlantis::Result<std::vector<schema::TypeId>, access::AccessError>> RemoteSession::listComponents(
    std::span<const EntityGuid> entities) {
  std::vector<std::uint64_t> ids;
  ids.reserve(entities.size());
  for (const EntityGuid& entity : entities) ids.push_back(state_->send("connection.listComponents", entityParams(entity)));
  auto& connection = static_cast<RemoteConnection&>(*state_->connection);
  std::vector<ComponentsResult> out;
  out.reserve(ids.size());
  for (const std::uint64_t id : ids) out.push_back(connection.decodeComponents(state_->awaitResult(id)));
  return out;
}

std::vector<atlantis::Result<access::PropertyValue, access::AccessError>> RemoteSession::getProperties(
    std::span<const access::PropertyAddress> addresses) {
  std::vector<std::uint64_t> ids;
  ids.reserve(addresses.size());
  for (const access::PropertyAddress& address : addresses) {
    ids.push_back(state_->send("connection.getProperty", addressParams(address)));
  }
  auto& connection = static_cast<RemoteConnection&>(*state_->connection);
  std::vector<PropertyResult> out;
  out.reserve(ids.size());
  for (const std::uint64_t id : ids) out.push_back(connection.decodeProperty(state_->awaitResult(id)));
  return out;
}

atlantis::Result<std::unique_ptr<RemoteSession>, RemoteError> connectRemote(const SessionInfo& session,
                                                                           RemoteOptions options) {
  using ResultT = atlantis::Result<std::unique_ptr<RemoteSession>, RemoteError>;
  auto socket = os::connectLoopback(session.port, options.connectTimeoutMilliseconds);
  if (socket.isErr()) return ResultT::Err(RemoteError::ConnectFailed);
  auto state = std::make_unique<RemoteSession::State>(
      LineChannel(std::move(socket.value()), kMaxResponseLineBytes, kMaxPendingRequestBytes), std::move(options));

  Value hello = Value::object();
  hello.set("protocol", Value::string(std::string(kProtocol)));
  hello.set("token", Value::string(session.token));
  const std::optional<Value> response = state->awaitResponse(state->send("hello", std::move(hello)));
  if (!response) return ResultT::Err(state->failure.value_or(RemoteError::Disconnected));
  if (const Value* error = response->find("error")) {
    const Value* code = error->find("code");
    if (code != nullptr && code->isString() && code->asString() == "BadToken") return ResultT::Err(RemoteError::BadToken);
    if (code != nullptr && code->isString() && code->asString() == "WrongProtocol") {
      return ResultT::Err(RemoteError::WrongProtocol);
    }
    return ResultT::Err(RemoteError::ProtocolError);
  }
  const Value* result = response->find("result");
  const Value* scene = result != nullptr ? result->find("scene") : nullptr;
  const auto sceneGuid = scene != nullptr ? codec::decodeAsset(*scene) : std::nullopt;
  if (!sceneGuid) return ResultT::Err(RemoteError::ProtocolError);
  state->scene = *sceneGuid;

  const std::optional<Value> schema = state->call("connection.schema");
  if (!schema || !state->schema.decode(*schema)) return ResultT::Err(state->failure.value_or(RemoteError::ProtocolError));
  state->connection = std::make_unique<RemoteConnection>(*state);
  state->control = std::make_unique<RemoteControl>(*state);
  return ResultT::Ok(std::make_unique<RemoteSession>(std::move(state)));
}

}  // namespace atlantis::remote
