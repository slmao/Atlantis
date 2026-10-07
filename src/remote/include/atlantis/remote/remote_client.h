#pragma once

#include <atlantis/remote/session_file.h>
#include <atlantis/result.h>

#include <filesystem>
#include <optional>
#include <string>

// Plan 0055 P1 (ADR-0106 D2, D7): the client half of Atlantis Remote, linked
// by the `atlantis` executable (and tests).
namespace atlantis::remote {

// P4's resolution order: `explicitPath` (`atlantis --session <path>`), else
// `environmentValue` (ATLANTIS_SESSION; nullptr or empty when unset), else
// defaultSessionPath().
[[nodiscard]] std::filesystem::path resolveSessionPath(const std::optional<std::string>& explicitPath,
                                                       const char* environmentValue);
[[nodiscard]] atlantis::Result<SessionInfo, SessionFileError> readSessionFile(const std::filesystem::path& path);

}  // namespace atlantis::remote
