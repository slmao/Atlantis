#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0015 Section D4: a hand-bumped constant identifying this
// cooker's own behavior version -- independently versioned from
// kImporterVersion (cook.h), matching that constant's own
// determinism-preserving role and rationale exactly.
inline constexpr std::string_view kSceneCookerVersion = "atlantis-scene-cooker/1";

// Plan 0015 Section D4: reads and validates the authoring source at
// sourceFilePath, remaps node_id/parent/active-camera references to
// dense array indices, encodes the runtime artifact and metadata
// sidecar, and writes both atomically (write-to-temp-then-rename in
// artifactOutputPath's/metadataOutputPath's own directory, identical
// to cookStaticMesh()'s own established pattern) -- a failed cook
// never leaves a partial file and never touches a pre-existing valid
// one. No separate "logical path for self" parameter -- a scene has
// no AssetId of its own, only its Renderable references do (D2).
[[nodiscard]] atlantis::Result<std::monostate, SceneCookError> cookScene(const std::string& sourceFilePath,
                                                                          const std::string& artifactOutputPath,
                                                                          const std::string& metadataOutputPath);

}  // namespace atlantis::asset_system
