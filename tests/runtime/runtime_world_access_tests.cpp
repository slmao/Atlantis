#include <atlantis/world/access/runtime_world_access.h>

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <iterator>
#include <optional>
#include <unordered_map>
#include <variant>
#include <vector>

// Plan 0052 M4 (Spec 0052 R8-R10; rulings Q5, Q6, Q8; Corrections J1, J2):
// the boundary against Runtime's own collection code, GPU-independent, over
// the committed default whitelist scene -- the Transform contract (written,
// announced, inert for rendering), edits visible to the next collection, the
// light-limit static_assert chain, and J2's material filter.

namespace {

namespace access = atlantis::world::access;
namespace ecs = atlantis::world::ecs;
using atlantis::asset_system::AssetId;
using atlantis::asset_system::EntityGuid;
using namespace atlantis::runtime;

// Plan 0052 P8: the guard's limits are the counts extraction can hold.
static_assert(access::kMaxPointLights == atlantis::runtime::kMaxPointLights);
static_assert(std::size(FrameLightingData{}.directionalLights) == access::kMaxDirectionalLights);

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

// The GUID of the first baked entity (in GUID order) holding a T.
template <typename T>
[[nodiscard]] EntityGuid firstWith(const atlantis::world::BakedScene& baked) {
  for (const auto& [guid, entity] : baked.entities.entries()) {
    if (baked.world.has<T>(entity).value()) return guid;
  }
  FAIL("no baked entity holds the component");
  return {};
}

[[nodiscard]] bool sameRenderables(const std::vector<RenderableExtractionInput>& a,
                                   const std::vector<RenderableExtractionInput>& b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (a[i].renderable.meshAsset != b[i].renderable.meshAsset) return false;
    if (a[i].renderable.materialAsset != b[i].renderable.materialAsset) return false;
    if (std::memcmp(a[i].worldMatrix.data(), b[i].worldMatrix.data(), sizeof(Mat4)) != 0) return false;
  }
  return true;
}

[[nodiscard]] bool sameLights(const std::vector<LightExtractionInput>& a, const std::vector<LightExtractionInput>& b) {
  return a.size() == b.size() &&
         (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(LightExtractionInput)) == 0);
}

[[nodiscard]] EntityGuid newGuid(std::uint8_t n) {
  EntityGuid guid;
  guid.bytes[0] = std::byte{0x52};
  guid.bytes[15] = static_cast<std::byte>(n);
  return guid;
}

}  // namespace

TEST_CASE("runtime world access: a Transform write is applied and announced but does not affect rendering (Q8)",
          "[runtime][access]") {
  atlantis::world::BakedScene baked = bakeDefaultScene();
  access::RuntimeWorldAccess boundary(baked);
  const auto renderablesBefore = collectRenderables(baked);
  const auto lightsBefore = collectLights(baked);
  const auto cameraBefore = collectActiveCamera(baked);
  REQUIRE(cameraBefore.has_value());

  const EntityGuid target = firstWith<atlantis::world::Renderable>(baked);
  const access::PropertyAddress address{target, ecs::componentTypeId<atlantis::world::Transform>(),
                                        atlantis::schema::fieldId("world::Transform", "localPosition")};
  boundary.submit(access::SetProperty{address, std::array<float, 3>{9.0f, -9.0f, 9.0f}});
  REQUIRE(boundary.applyPending().failures.empty());

  CHECK(boundary.getProperty(address).value() == access::PropertyValue{std::array<float, 3>{9.0f, -9.0f, 9.0f}});
  const auto events = boundary.drainEvents();
  REQUIRE(events.size() == 1);
  CHECK(std::holds_alternative<access::PropertyChanged>(events[0]));

  // Everything the frame hands the GPU is byte-identical: WorldMatrix, not
  // Transform, is render-authoritative in the baked world (ADR-0102 D4).
  CHECK(sameRenderables(collectRenderables(baked), renderablesBefore));
  CHECK(sameLights(collectLights(baked), lightsBefore));
  const auto cameraAfter = collectActiveCamera(baked);
  REQUIRE(cameraAfter.has_value());
  CHECK(std::memcmp(&*cameraAfter, &*cameraBefore, sizeof(ActiveCameraInput)) == 0);
}

TEST_CASE("runtime world access: applied edits are visible to the next collection (Spec 0022 surviving contract)",
          "[runtime][access]") {
  atlantis::world::BakedScene baked = bakeDefaultScene();
  access::RuntimeWorldAccess boundary(baked);
  using atlantis::world::Light;
  using atlantis::world::WorldMatrix;
  const auto lightType = ecs::componentTypeId<Light>();
  const auto matrixType = ecs::componentTypeId<WorldMatrix>();
  const EntityGuid sun = firstWith<Light>(baked);
  REQUIRE(collectLights(baked).size() == 1);

  // A Light value, then a WorldMatrix.
  boundary.submit(access::SetProperty{{sun, lightType, atlantis::schema::fieldId("world::Light", "intensity")}, 4.5f});
  REQUIRE(boundary.applyPending().failures.empty());
  CHECK(collectLights(baked)[0].light.intensity == 4.5f);
  boundary.submit(access::SetProperty{{sun, matrixType, atlantis::schema::fieldId("world::WorldMatrix", "column3")},
                                      std::array<float, 4>{3.0f, 4.0f, 5.0f, 1.0f}});
  REQUIRE(boundary.applyPending().failures.empty());
  CHECK(collectLights(baked)[0].worldMatrix[12] == 3.0f);

  // A Point light created (Light before WorldMatrix, J1), then destroyed.
  const EntityGuid point = newGuid(1);
  boundary.submit(access::CreateEntity{point});
  boundary.submit(access::AddComponent{point, lightType});
  boundary.submit(access::SetProperty{{point, lightType, atlantis::schema::fieldId("world::Light", "kind")},
                                      access::EnumValue{1}});
  boundary.submit(access::AddComponent{point, matrixType});
  REQUIRE(boundary.applyPending().failures.empty());
  const auto withPoint = collectLights(baked);
  REQUIRE(withPoint.size() == 2);
  CHECK(withPoint[1].light.kind == atlantis::world::LightKind::Point);  // created last: sorts last
  boundary.submit(access::DestroyEntity{point});
  REQUIRE(boundary.applyPending().failures.empty());
  CHECK(collectLights(baked).size() == 1);
}

TEST_CASE("runtime world access: J2 -- a material id the scene load did not load is never realized",
          "[runtime][access]") {
  atlantis::world::BakedScene baked = bakeDefaultScene();
  access::RuntimeWorldAccess boundary(baked);

  // The scene load's materialDataMap holds exactly the referenced ids.
  std::unordered_map<AssetId, atlantis::asset_system::MaterialAssetData> loaded;
  for (const AssetId id : collectReferencedMaterialIds(collectRenderables(baked))) loaded.emplace(id, atlantis::asset_system::MaterialAssetData{});
  REQUIRE_FALSE(loaded.empty());
  const auto before = collectReferencedMaterialIds(collectRenderables(baked));
  CHECK(loadedMaterialIdsOnly(before, loaded) == before);  // identity for a loaded scene

  constexpr AssetId kUnloaded = 0x5200520052005200ULL;
  REQUIRE_FALSE(loaded.contains(kUnloaded));
  const EntityGuid target = firstWith<atlantis::world::Renderable>(baked);
  boundary.submit(access::SetProperty{{target, ecs::componentTypeId<atlantis::world::Renderable>(),
                                       atlantis::schema::fieldId("world::Renderable", "materialAsset")},
                                      std::uint64_t{kUnloaded}});
  REQUIRE(boundary.applyPending().failures.empty());  // World does not know what Runtime loaded

  const auto referenced = collectReferencedMaterialIds(collectRenderables(baked));
  CHECK(std::find(referenced.begin(), referenced.end(), kUnloaded) != referenced.end());
  const auto realizable = loadedMaterialIdsOnly(referenced, loaded);
  CHECK(std::find(realizable.begin(), realizable.end(), kUnloaded) == realizable.end());
  for (const AssetId id : realizable) CHECK(loaded.contains(id));
}
