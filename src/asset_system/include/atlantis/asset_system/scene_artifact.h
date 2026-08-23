#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/validated_scene_data.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace atlantis::asset_system {

// Plan 0015 Section D5: the scene artifact's binary layout --
// unconditionally little-endian, explicit shift/mask assembly, never a
// struct memcpy, matching mesh_artifact.h's own discipline exactly.
inline constexpr std::uint32_t kSceneArtifactSchemaVersion = 1;
inline constexpr std::size_t kSceneArtifactHeaderSizeBytes = 24;
// position(12) + rotation(12) + scale(12) + has_camera(4) +
// fov_y/near_z/far_z(12) + has_renderable(4) + mesh_asset_id(8) +
// has_parent(4) + parent_index(4) = 72 bytes.
inline constexpr std::size_t kSceneArtifactNodeRecordSizeBytes = 72;
// "Implausibly large" upper bound (D6 step 4) -- this format is hand-
// authored text at import time, never a high-poly runtime asset;
// mirrors kMaxVertexCount's own order of magnitude and role exactly
// (a safety bound checked before allocation, not a design target).
inline constexpr std::uint32_t kMaxSceneArtifactNodeCount = 65536;

// Encodes an already cook-time-validated, densely index-remapped node
// array (D4 step 9) -- ValidatedSceneNode/parents-by-index/
// activeCameraIndex-by-index is exactly the shape both cookScene()'s
// own remapped intermediate state and ValidatedSceneData's own private
// fields already share, so this reuses ValidatedSceneNode directly
// rather than introducing a parallel, cook-only type. parents.size()
// must equal nodes.size() (each node's own optional parent, by array
// index) -- an invariant only this module's own two callers
// (cookScene(), decodeSceneArtifact() below) need to uphold, not a
// public contract.
[[nodiscard]] std::vector<std::byte> encodeSceneArtifact(const std::vector<ValidatedSceneNode>& nodes,
                                                           const std::vector<std::optional<std::size_t>>& parents,
                                                           std::optional<std::size_t> activeCameraIndex);

struct DecodedSceneArtifact {
  std::vector<ValidatedSceneNode> nodes;
  std::vector<std::optional<std::size_t>> parents;
  std::optional<std::size_t> activeCameraIndex;
};

// Plan 0015 Section D6, steps 2-7: header decode, EmptyScene and
// NodeCountOutOfRange guards, per-node structural/finite-value
// decode, cycle re-check, and active-camera range/Camera-presence
// check -- entirely from the artifact's own bytes, never trusting the
// cooker. Steps 1 (file I/O) and 8-9 (metadata cross-check,
// ValidatedSceneData construction) are decodeScene()'s own job
// (decode_scene.h), which calls this function -- mirroring
// decodeMeshArtifact()'s own relationship to load.cpp's
// loadStaticMeshAsset() exactly.
[[nodiscard]] atlantis::Result<DecodedSceneArtifact, SceneArtifactDecodeError> decodeSceneArtifact(
    const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
