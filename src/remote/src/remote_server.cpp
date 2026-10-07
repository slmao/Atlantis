#include <atlantis/remote/remote_server.h>

#include <atlantis/assert.h>

#include "codec.h"
#include "line_channel.h"
#include "os/socket.h"

#include <cstdint>
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
  bool closing = false;  // write what is queued, then drop
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

}  // namespace

struct RemoteServer::State {
  os::Socket listener;
  std::string token;
  atlantis::asset_system::AssetGuid scene;
  OpenConnection openConnection;
  Limits limits;
  std::uint64_t nextClient = 1;
  std::map<std::uint64_t, Client> clients;

  void accept() {
    // Bounded per poll; the rest wait in the OS backlog for the next frame.
    for (int i = 0; i < 16; ++i) {
      os::Socket socket = os::acceptPending(listener);
      if (!socket.valid()) return;
      clients.emplace(nextClient++, Client(LineChannel(std::move(socket), limits.maxLineBytes, limits.maxPendingWriteBytes)));
    }
  }

  // One request line; returns the response to queue (always one per line).
  Value dispatch(Client& client, std::string_view line) {
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
    Outcome outcome = callConnection(*client.connection, method->asString(), p);
    if (outcome.isErr()) return errorResponse(requestId, "BadRequest", outcome.error());
    return response(requestId, std::move(outcome.value()));
  }

  // Reads and answers what `client` sent; false if it must be dropped (it
  // closed, failed, sent a line over the limit, or overran its write limit).
  [[nodiscard]] bool serve(Client& client) {
    std::vector<std::string> lines;
    const LineChannel::ReadStatus status = client.channel.read(lines);
    for (const std::string& line : lines) {
      if (client.closing) break;  // after a refused hello nothing more is answered
      if (!client.channel.queue(json::write(dispatch(client, line)))) return false;
    }
    return status == LineChannel::ReadStatus::Ok;
  }
};

RemoteServer::RemoteServer(Key, std::shared_ptr<State> state) : state_(std::move(state)) {}

RemoteServer::~RemoteServer() = default;

atlantis::Result<std::unique_ptr<RemoteServer>, ListenError> RemoteServer::listen(
    std::uint16_t port, std::string token, atlantis::asset_system::AssetGuid scene, OpenConnection openConnection,
    Limits limits) {
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
  state->limits = limits;
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
  for (auto it = state.clients.begin(); it != state.clients.end();) {
    Client& client = it->second;
    bool keep = client.closing || state.serve(client);
    keep = keep && client.channel.flush();
    if (client.closing && !client.channel.hasPendingWrite()) keep = false;
    it = keep ? std::next(it) : state.clients.erase(it);
  }
}

std::size_t RemoteServer::clientCount() const noexcept { return state_->clients.size(); }

}  // namespace atlantis::remote
