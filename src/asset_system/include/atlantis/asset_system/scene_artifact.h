#pragma once

#include <atlantis/asset_system/asset_guid.h>
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
// Plan 0018 Section P7: schema version 2 inserted a 12-byte material
// slot (has_material u32 + material_asset_id u64) immediately after the
// existing renderable slot, before the parent slot. Plan 0019 Section
// P4: schema version 3 inserts a 28-byte light slot (has_light u32 +
// light_kind u32 + color_r/g/b f32x3 + intensity f32 + range f32) at
// the identical insertion point -- after material, before parent.
// Plan 0031: schema version 4 inserts a 4-byte exposure_compensation_ev
// f32 immediately after far_z, before has_renderable. Plan 0043 P4:
// schema version 5 inserts the 28-byte camera fog slot (fog_color
// f32x3, fog_density, fog_height, fog_height_falloff, fog_max_opacity)
// immediately after exposure_compensation_ev, before has_renderable;
// a node with no camera writes the DecodedCameraFog defaults there.
// Plan 0044 P4: schema version 6 inserts the 8-byte camera bloom slot
// (bloom_strength f32, bloom_threshold f32) immediately after the fog
// slot, before has_renderable; a camera-less node writes the
// DecodedCameraBloom defaults. Plan 0047 P9 (ADR-0097 D3/D5): schema
// version 7 appends the node's 16-byte entity_guid (text byte order) at
// record offset 152. Versions 1-6 are all rejected outright, no
// dual-version reader.
inline constexpr std::uint32_t kSceneArtifactSchemaVersion = 7;
// Plan 0049 P5 (Spec 0049 R5/R6, ruling Q2): the scene semantic version this
// artifact format projects -- see scene::kSemanticVersion. Independent of
// the format version above: a layout-only change leaves it alone.
inline constexpr std::uint32_t kSceneArtifactSemanticVersion = 1;
inline constexpr std::size_t kSceneArtifactHeaderSizeBytes = 24;
// position(12) + rotation(12) + scale(12) + has_camera(4) +
// fov_y/near_z/far_z(12) + exposure_compensation_ev(4) +
// fog_color_r/g/b(12) + fog_density(4) + fog_height(4) +
// fog_height_falloff(4) + fog_max_opacity(4) +
// bloom_strength(4) + bloom_threshold(4) +
// has_renderable(4) + mesh_asset_id(8) + has_material(4) +
// material_asset_id(8) + has_light(4) + light_kind(4) +
// color_r/g/b(12) + intensity(4) + range(4) + has_parent(4) +
// parent_index(4) + entity_guid(16) = 168 bytes.
inline constexpr std::size_t kSceneArtifactNodeRecordSizeBytes = 168;
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
// entityGuids.size() must equal nodes.size() too (Plan 0047 P9).
[[nodiscard]] std::vector<std::byte> encodeSceneArtifact(const std::vector<ValidatedSceneNode>& nodes,
                                                           const std::vector<std::optional<std::size_t>>& parents,
                                                           std::optional<std::size_t> activeCameraIndex,
                                                           const std::vector<EntityGuid>& entityGuids);

struct DecodedSceneArtifact {
  std::vector<ValidatedSceneNode> nodes;
  std::vector<std::optional<std::size_t>> parents;
  std::optional<std::size_t> activeCameraIndex;
  std::vector<EntityGuid> entityGuids;  // Plan 0047 P9: one per node, non-nil and unique
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
