#include <atlantis/remote/session_file.h>

#include <atlantis/assert.h>
#include <atlantis/connection/json.h>

namespace atlantis::remote {

namespace json = connection::json;

std::string_view toString(SessionFileError error) noexcept {
  switch (error) {
    case SessionFileError::NotFound: return "SessionFileNotFound";
    case SessionFileError::Unreadable: return "SessionFileUnreadable";
    case SessionFileError::Malformed: return "SessionFileMalformed";
    case SessionFileError::WrongProtocol: return "WrongProtocol";
    case SessionFileError::WriteFailed: return "SessionFileWriteFailed";
  }
  ATLANTIS_CHECK_MSG(false, "toString(SessionFileError): unhandled enumerator");
  return "(unrecognized SessionFileError)";
}

std::string formatSession(const SessionInfo& session) {
  json::Value out = json::Value::object();
  out.set("protocol", json::Value::string(std::string(kProtocol)));
  out.set("port", json::Value::number(static_cast<std::uint64_t>(session.port)));
  out.set("token", json::Value::string(session.token));
  out.set("pid", json::Value::number(static_cast<std::uint64_t>(session.pid)));
  out.set("scene", json::Value::string(atlantis::asset_system::toString(session.scene)));
  return json::write(out) + "\n";
}

atlantis::Result<SessionInfo, SessionFileError> parseSession(std::string_view text) {
  using ResultT = atlantis::Result<SessionInfo, SessionFileError>;
  const auto parsed = json::parse(text);
  if (parsed.isErr() || !parsed.value().isObject()) return ResultT::Err(SessionFileError::Malformed);
  const json::Value& object = parsed.value();
  const json::Value* protocol = object.find("protocol");
  const json::Value* port = object.find("port");
  const json::Value* token = object.find("token");
  const json::Value* pid = object.find("pid");
  const json::Value* scene = object.find("scene");
  if (protocol == nullptr || !protocol->isString()) return ResultT::Err(SessionFileError::Malformed);
  if (protocol->asString() != kProtocol) return ResultT::Err(SessionFileError::WrongProtocol);
  std::uint64_t portNumber = 0;
  std::uint64_t pidNumber = 0;
  if (port == nullptr || !port->toUInt64(portNumber) || portNumber == 0 || portNumber > 65535 || token == nullptr ||
      !token->isString() || pid == nullptr || !pid->toUInt64(pidNumber) || pidNumber > UINT32_MAX ||
      scene == nullptr || !scene->isString()) {
    return ResultT::Err(SessionFileError::Malformed);
  }
  const auto sceneGuid = atlantis::asset_system::parseAssetGuid(scene->asString());
  if (sceneGuid.isErr()) return ResultT::Err(SessionFileError::Malformed);
  SessionInfo session;
  session.port = static_cast<std::uint16_t>(portNumber);
  session.token = token->asString();
  session.pid = static_cast<std::uint32_t>(pidNumber);
  session.scene = sceneGuid.value();
  return ResultT::Ok(std::move(session));
}

std::filesystem::path defaultSessionPath() { return std::filesystem::path(".atlantis") / "runtime.session.json"; }

}  // namespace atlantis::remote
