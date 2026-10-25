#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/scene_load.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/scene_instantiation.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

// Plan 0062 P5/P6 (Spec 0062 R4, R6; ADR-0123 D2): the editor's pick services
// over the Runtime's own CPU copy of mesh geometry. Private to
// atlantis_runtime_host: the public surface is RuntimeApplication's
// pickRenderable() and renderableLocalBounds(). Pure CPU work, frame thread
// only, between frames (ADR-0004).
namespace atlantis::runtime::detail {

// One mesh's pick geometry from its asset data: the positions are the
// layout's location-0 Float3 attribute (as computeLocalBoundsCentre() reads
// them), the indices widened to 32 bits. Preconditions as createMesh()'s: a
// non-zero stride dividing the vertex bytes, a Float3 at location 0.
[[nodiscard]] PickMeshGeometry buildPickGeometry(const atlantis::rhi::VertexInputLayout& layout,
                                                 const std::vector<std::byte>& vertexBytes,
                                                 std::span<const std::uint16_t> indices);
[[nodiscard]] PickMeshGeometry buildPickGeometry(const atlantis::rhi::VertexInputLayout& layout,
                                                 const std::vector<std::byte>& vertexBytes,
                                                 std::span<const std::uint32_t> indices);

// One pickable entity: an addressable entity with a Renderable and a
// WorldMatrix, as the World holds them now.
struct PickInstance {
  atlantis::asset_system::EntityGuid entity;
  atlantis::asset_system::AssetId mesh = 0;
  std::array<float, 16> worldMatrix{};  // column-major
};

// Every pickable entity, read through the World's operation boundary (so
// entities created since load are included), in node order: the bake's
// entities by their node, then entities created since, in GUID order.
[[nodiscard]] std::vector<PickInstance> collectPickInstances(const atlantis::world::access::RuntimeWorldAccess& access,
                                                             const atlantis::world::BakedScene& scene);

// The nearest hit of `ray` (unit direction) on any instance's triangles, both
// faces, in world space; per instance its world-space bounds are tested
// first. A tie goes to the instance earlier in `instances`. Instances whose
// mesh has no geometry here, or whose world matrix is not finite, are skipped.
[[nodiscard]] std::optional<PickHit> pickNearest(
    std::span<const PickInstance> instances,
    const std::unordered_map<atlantis::asset_system::AssetId, PickMeshGeometry>& geometry, const PickRay& ray);

// The entity's Renderable mesh, through the operation boundary; empty when it
// is not addressable or has no Renderable.
[[nodiscard]] std::optional<atlantis::asset_system::AssetId> renderableMesh(
    const atlantis::world::access::RuntimeWorldAccess& access, const atlantis::asset_system::EntityGuid& entity);

// The entity's Renderable material, through the operation boundary; empty when
// it is not addressable, has no Renderable, or its Renderable has no material.
[[nodiscard]] std::optional<atlantis::asset_system::AssetId> renderableMaterial(
    const atlantis::world::access::RuntimeWorldAccess& access, const atlantis::asset_system::EntityGuid& entity);

}  // namespace atlantis::runtime::detail
