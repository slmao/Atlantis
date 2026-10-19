// Plan 0062 M3 (Spec 0062 R4, R6; ADR-0123 D2): the editor's pick services,
// GPU-free -- pick geometry built from asset-shaped vertex bytes, the pick
// walk through the World's operation boundary over the committed default
// scene, and the nearest-hit search. The retention switch itself (no
// geometry without it) needs a Device and is checked by the GPU suite.

#include "../../src/runtime/src/scene_picking.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world_matrix.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

namespace {

namespace access = atlantis::world::access;
namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::AssetId;
using atlantis::asset_system::EntityGuid;
using atlantis::runtime::PickHit;
using atlantis::runtime::PickMeshGeometry;
using atlantis::runtime::PickRay;
using atlantis::runtime::detail::buildPickGeometry;
using atlantis::runtime::detail::collectPickInstances;
using atlantis::runtime::detail::pickNearest;
using atlantis::runtime::detail::PickInstance;

constexpr AssetId kCubeMesh = 0x0062006200620062ull;

[[nodiscard]] atlantis::world::BakedScene bakeDefaultScene() {
  auto catalog = atlantis::asset_system::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(catalog.isOk());
  const auto guid = atlantis::asset_system::parseAssetGuid(ATLANTIS_WHITELIST_INTEGRATED_SHOWCASE_SCENE_GUID);
  REQUIRE(guid.isOk());
  const auto* record = catalog.value().find(guid.value());
  REQUIRE(record != nullptr);
  auto scene = atlantis::asset_system::decodeScene(record->artifact, record->metadata);
  REQUIRE(scene.isOk());
  return atlantis::world::bakeScene(scene.value());
}

[[nodiscard]] EntityGuid newGuid(std::uint8_t n) {
  EntityGuid guid;
  guid.bytes[0] = std::byte{0x62};
  guid.bytes[15] = static_cast<std::byte>(n);
  return guid;
}

// An asset-shaped vertex buffer: position at location 0 after a 4-byte lead,
// then a colour, 28-byte stride -- the reader must take the layout's offset.
const atlantis::rhi::VertexInputLayout kLayout{
    28,
    {atlantis::rhi::VertexAttribute{1, 0, atlantis::rhi::VertexAttributeFormat::Float2},
     atlantis::rhi::VertexAttribute{0, 8, atlantis::rhi::VertexAttributeFormat::Float3}}};

// A unit cube centred on the origin: 8 corners, 12 triangles.
[[nodiscard]] std::vector<std::byte> cubeVertexBytes() {
  std::vector<std::byte> bytes(8 * 28, std::byte{0});
  for (int corner = 0; corner < 8; ++corner) {
    const float xyz[3] = {(corner & 1) ? 0.5f : -0.5f, (corner & 2) ? 0.5f : -0.5f, (corner & 4) ? 0.5f : -0.5f};
    std::memcpy(bytes.data() + corner * 28 + 8, xyz, sizeof(xyz));
  }
  return bytes;
}

constexpr std::array<std::uint32_t, 36> kCubeIndices{
    0, 2, 1, 1, 2, 3,  // -z
    4, 5, 6, 5, 7, 6,  // +z
    0, 1, 4, 1, 5, 4,  // -y
    2, 6, 3, 3, 6, 7,  // +y
    0, 4, 2, 2, 4, 6,  // -x
    1, 3, 5, 3, 7, 5,  // +x
};

[[nodiscard]] std::unordered_map<AssetId, PickMeshGeometry> cubeGeometry() {
  std::unordered_map<AssetId, PickMeshGeometry> geometry;
  geometry.emplace(kCubeMesh, buildPickGeometry(kLayout, cubeVertexBytes(), std::span<const std::uint32_t>(kCubeIndices)));
  return geometry;
}

// Creates an entity with a Renderable of kCubeMesh and a WorldMatrix whose
// column 3 is `at`, through the boundary.
void createCube(access::RuntimeWorldAccess& boundary, const EntityGuid& entity, const std::array<float, 3>& at) {
  const auto renderable = ecs::componentTypeId<atlantis::world::Renderable>();
  const auto matrix = ecs::componentTypeId<atlantis::world::WorldMatrix>();
  (void)boundary.submitTransaction(
      {access::CreateEntity{entity}, access::AddComponent{entity, renderable}, access::AddComponent{entity, matrix},
       access::SetProperty{{entity, renderable, atlantis::schema::fieldId("world::Renderable", "meshAsset")},
                           std::uint64_t{kCubeMesh}},
       access::SetProperty{{entity, matrix, atlantis::schema::fieldId("world::WorldMatrix", "column3")},
                           std::array<float, 4>{at[0], at[1], at[2], 1.0f}}});
  REQUIRE(boundary.applyPending().failures.empty());
}

[[nodiscard]] std::optional<PickHit> pick(access::RuntimeWorldAccess& boundary, const atlantis::world::BakedScene& baked,
                                          const PickRay& ray) {
  const std::vector<PickInstance> instances = collectPickInstances(boundary, baked);
  return pickNearest(instances, cubeGeometry(), ray);
}

}  // namespace

TEST_CASE("pick geometry: positions from the layout's location 0, both index widths, local bounds",
          "[runtime][editor][picking]") {
  const auto bytes = cubeVertexBytes();
  std::array<std::uint16_t, 36> narrow{};
  for (std::size_t i = 0; i < narrow.size(); ++i) narrow[i] = static_cast<std::uint16_t>(kCubeIndices[i]);
  const PickMeshGeometry wide = buildPickGeometry(kLayout, bytes, std::span<const std::uint32_t>(kCubeIndices));
  const PickMeshGeometry fromNarrow = buildPickGeometry(kLayout, bytes, std::span<const std::uint16_t>(narrow));
  REQUIRE(wide.positions.size() == 8);
  CHECK(wide.positions[7] == std::array<float, 3>{0.5f, 0.5f, 0.5f});
  CHECK(wide.positions == fromNarrow.positions);
  CHECK(wide.indices == fromNarrow.indices);
  CHECK(wide.localMin == std::array<float, 3>{-0.5f, -0.5f, -0.5f});
  CHECK(wide.localMax == std::array<float, 3>{0.5f, 0.5f, 0.5f});
}

TEST_CASE("picking: the nearest instance along the ray, both faces, misses and instancing",
          "[runtime][editor][picking]") {
  atlantis::world::BakedScene baked = bakeDefaultScene();
  access::RuntimeWorldAccess boundary(baked);
  // The scene's own meshes have no pick geometry here: only the cubes count.
  createCube(boundary, newGuid(1), {0.0f, 0.0f, -5.0f});
  createCube(boundary, newGuid(2), {0.0f, 0.0f, -10.0f});  // the same mesh again, behind

  const PickRay forward{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
  const auto hit = pick(boundary, baked, forward);
  REQUIRE(hit.has_value());
  CHECK(hit->entity == newGuid(1));
  CHECK(std::fabs(hit->distance - 4.5f) < 1e-5f);

  // From inside the first cube: its back face, the nearer surface.
  const auto inside = pick(boundary, baked, PickRay{{0.0f, 0.0f, -5.0f}, {0.0f, 0.0f, -1.0f}});
  REQUIRE(inside.has_value());
  CHECK(inside->entity == newGuid(1));
  CHECK(std::fabs(inside->distance - 0.5f) < 1e-5f);

  // Off to the side, and away from both.
  CHECK_FALSE(pick(boundary, baked, PickRay{{3.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}}).has_value());
  CHECK_FALSE(pick(boundary, baked, PickRay{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}).has_value());

  // A ray at an angle hits the second cube past the first.
  const float s = std::sqrt(0.5f);
  const auto angled = pick(boundary, baked, PickRay{{-10.0f, 0.0f, 0.0f}, {s, 0.0f, -s}});
  REQUIRE(angled.has_value());
  CHECK(angled->entity == newGuid(2));
}

TEST_CASE("picking follows a WorldMatrix edited through the boundary", "[runtime][editor][picking]") {
  atlantis::world::BakedScene baked = bakeDefaultScene();
  access::RuntimeWorldAccess boundary(baked);
  createCube(boundary, newGuid(1), {0.0f, 0.0f, -5.0f});
  createCube(boundary, newGuid(2), {0.0f, 0.0f, -10.0f});
  const PickRay forward{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}};
  REQUIRE(pick(boundary, baked, forward)->entity == newGuid(1));

  const auto matrix = ecs::componentTypeId<atlantis::world::WorldMatrix>();
  boundary.submit(access::SetProperty{{newGuid(1), matrix, atlantis::schema::fieldId("world::WorldMatrix", "column3")},
                                      std::array<float, 4>{4.0f, 0.0f, -5.0f, 1.0f}});
  REQUIRE(boundary.applyPending().failures.empty());
  CHECK(pick(boundary, baked, forward)->entity == newGuid(2));
  const auto moved = pick(boundary, baked, PickRay{{4.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});
  REQUIRE(moved.has_value());
  CHECK(moved->entity == newGuid(1));

  // A scaled column: the cube is twice as deep, its near face half a unit closer.
  boundary.submit(access::SetProperty{{newGuid(2), matrix, atlantis::schema::fieldId("world::WorldMatrix", "column2")},
                                      std::array<float, 4>{0.0f, 0.0f, 2.0f, 0.0f}});
  REQUIRE(boundary.applyPending().failures.empty());
  CHECK(std::fabs(pick(boundary, baked, forward)->distance - 9.0f) < 1e-5f);

  // Destroyed: no longer pickable.
  boundary.submit(access::DestroyEntity{newGuid(2)});
  REQUIRE(boundary.applyPending().failures.empty());
  CHECK_FALSE(pick(boundary, baked, forward).has_value());
}

TEST_CASE("picking walks every Renderable in node order, created entities after, and a tie goes to the earlier",
          "[runtime][editor][picking]") {
  atlantis::world::BakedScene baked = bakeDefaultScene();
  access::RuntimeWorldAccess boundary(baked);
  const std::size_t bakedRenderables = atlantis::runtime::collectRenderables(baked).size();
  REQUIRE(bakedRenderables > 0);
  createCube(boundary, newGuid(3), {0.0f, 50.0f, -5.0f});
  createCube(boundary, newGuid(2), {0.0f, 50.0f, -5.0f});  // the same place: a tie
  const std::vector<PickInstance> instances = collectPickInstances(boundary, baked);
  REQUIRE(instances.size() == bakedRenderables + 2);
  // The baked part is the frame's own collection order.
  const auto renderables = atlantis::runtime::collectRenderables(baked);
  for (std::size_t i = 0; i < bakedRenderables; ++i) {
    CHECK(instances[i].mesh == renderables[i].renderable.meshAsset);
    CHECK(instances[i].worldMatrix == renderables[i].worldMatrix);
  }
  CHECK(instances[bakedRenderables].entity == newGuid(2));  // created ones in GUID order
  CHECK(instances[bakedRenderables + 1].entity == newGuid(3));
  const auto tie = pickNearest(instances, cubeGeometry(), PickRay{{0.0f, 50.0f, 0.0f}, {0.0f, 0.0f, -1.0f}});
  REQUIRE(tie.has_value());
  CHECK(tie->entity == newGuid(2));
}

TEST_CASE("an entity's Renderable mesh is read through the boundary", "[runtime][editor][picking]") {
  atlantis::world::BakedScene baked = bakeDefaultScene();
  access::RuntimeWorldAccess boundary(baked);
  createCube(boundary, newGuid(1), {0.0f, 0.0f, 0.0f});
  CHECK(atlantis::runtime::detail::renderableMesh(boundary, newGuid(1)) == std::optional<AssetId>{kCubeMesh});
  CHECK_FALSE(atlantis::runtime::detail::renderableMesh(boundary, newGuid(9)).has_value());
  const EntityGuid empty = newGuid(4);
  boundary.submit(access::CreateEntity{empty});
  REQUIRE(boundary.applyPending().failures.empty());
  CHECK_FALSE(atlantis::runtime::detail::renderableMesh(boundary, empty).has_value());
}
