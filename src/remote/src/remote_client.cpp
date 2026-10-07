#include <atlantis/remote/remote_client.h>

#include <fstream>
#include <iterator>
#include <system_error>

namespace atlantis::remote {

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

}  // namespace atlantis::remote
