#include <atlantis/remote/remote_server.h>

#include <array>
#include <fstream>
#include <random>
#include <system_error>

namespace atlantis::remote {

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

}  // namespace atlantis::remote
