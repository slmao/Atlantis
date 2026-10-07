#pragma once

#include <atlantis/remote/session_file.h>
#include <atlantis/result.h>

#include <filesystem>
#include <string>
#include <variant>

// Plan 0055 P1 (ADR-0106 D1, D7): the server half of Atlantis Remote, linked
// by the atlantis_runtime executable only.
namespace atlantis::remote {

// A fresh session token: 128 bits from std::random_device, as 32 lowercase
// hex digits (P4).
[[nodiscard]] std::string generateToken();

// Writes `session` to `path` -- creating its directory -- through a
// temporary file renamed over it, so a reader never sees a partial file (P4).
[[nodiscard]] atlantis::Result<std::monostate, SessionFileError> writeSessionFile(const std::filesystem::path& path,
                                                                                  const SessionInfo& session);
// Removes the file, if present; errors are ignored (clean shutdown, P4).
void removeSessionFile(const std::filesystem::path& path) noexcept;

}  // namespace atlantis::remote
