#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0012 Section D9 / ADR-0044: normalizes an authoring-source path
// into the canonical logical-path form the Asset ID hashes and the
// metadata sidecar records. A pure string algorithm -- never constructs
// a std::filesystem::path or calls any of its members, so behavior does
// not depend on host OS path-parsing conventions (Windows and a future
// Android build compute the same normalized form for the same input).
[[nodiscard]] atlantis::Result<std::string, LogicalPathError> normalizeLogicalPath(std::string_view input);

}  // namespace atlantis::asset_system
