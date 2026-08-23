#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0015 Section D5: the scene metadata sidecar's field semantics
// -- mirrors AssetMetadata's own shape minus the mesh-specific fields
// (a scene has no AssetId, vertex count, or vertex stride of its
// own). Wire encoding: strict, anchored-prefix, versioned flat text,
// exactly 3 lines, matching AssetMetadata's own established grammar
// discipline -- this module's parser is new, independent code, never
// shared with asset_metadata.cpp's own implementation.
struct SceneMetadata {
  std::uint32_t schemaVersion = 0;
  std::uint32_t nodeCount = 0;
};

[[nodiscard]] atlantis::Result<SceneMetadata, MetadataParseError> parseSceneMetadata(std::string_view text);
[[nodiscard]] std::string serializeSceneMetadata(const SceneMetadata& metadata);

}  // namespace atlantis::asset_system
