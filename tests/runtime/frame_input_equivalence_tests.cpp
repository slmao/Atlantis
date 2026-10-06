#include <atlantis/runtime/scene_extraction.h>

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

// Plan 0051 M3 / P7 (Spec 0051 R6, ruling Q7 V2, J4): frame-input
// equivalence. For every scene of the assembled build catalog -- the committed
// scenes, and Bistro when its content was built -- the inputs Runtime's frame hands
// the GPU are computed twice -- through today's world::World walks (the
// oracle below) and through bakeScene() plus the shared collection
// functions -- and compared byte for byte. GPU-independent: no Device, no
// mesh or texture load.

namespace {

using atlantis::asset_system::AssetId;
using atlantis::asset_system::CatalogAssetType;
using atlantis::asset_system::ValidatedSceneData;
using namespace atlantis::runtime;

// The oracle: runtime_application.cpp's runFrame() walks over world::World as
// of origin/main 754b563 (before Spec 0051), transcribed verbatim minus the
// GPU writes. Test-only; it never returns to src/ (Plan 0051 P7).
namespace legacy {

struct FrameInputs {
  std::optional<ActiveCameraInput> camera;
  std::vector<LightExtractionInput> lights;
  std::vector<AssetId> referencedMaterialIds;
  std::vector<RenderableExtractionInput> renderables;
};

[[nodiscard]] FrameInputs collect(atlantis::world::World& world) {
  FrameInputs out;
  world.updateTransforms();

  if (const auto activeCamera = world.activeCamera(); activeCamera.has_value()) {
    const auto cameraWorldMatrixResult = world.getWorldMatrix(*activeCamera);
    const auto cameraComponentResult = world.getCamera(*activeCamera);
    REQUIRE((cameraWorldMatrixResult.isOk() && cameraComponentResult.isOk()));
    out.camera = ActiveCameraInput{cameraComponentResult.value(), cameraWorldMatrixResult.value()};
  }

  for (const atlantis::world::EntityId& id : world.lightEntities()) {
    const auto lightResult = world.getLight(id);
    const auto lightWorldMatrixResult = world.getWorldMatrix(id);
    REQUIRE((lightResult.isOk() && lightWorldMatrixResult.isOk()));
    out.lights.push_back({lightResult.value(), lightWorldMatrixResult.value()});
  }

  for (const atlantis::world::EntityId& id : world.renderableEntities()) {
    const auto renderableResult = world.getRenderable(id);
    REQUIRE(renderableResult.isOk());
    if (const auto& materialAsset = renderableResult.value().materialAsset; materialAsset.has_value()) {
      if (std::find(out.referencedMaterialIds.begin(), out.referencedMaterialIds.end(), *materialAsset) ==
          out.referencedMaterialIds.end()) {
        out.referencedMaterialIds.push_back(*materialAsset);
      }
    }
  }

  for (const atlantis::world::EntityId& id : world.renderableEntities()) {
    const auto renderableResult = world.getRenderable(id);
    const auto worldMatrixResult = world.getWorldMatrix(id);
    REQUIRE((renderableResult.isOk() && worldMatrixResult.isOk()));
    out.renderables.push_back({renderableResult.value(), worldMatrixResult.value()});
  }
  return out;
}

}  // namespace legacy

template <typename T>
[[nodiscard]] bool bytesEqual(const T& a, const T& b) {
  static_assert(std::is_trivially_copyable_v<T>);
  return std::memcmp(&a, &b, sizeof(T)) == 0;
}

template <typename T, typename E>
void expectSameResultBytes(const atlantis::Result<T, E>& expected, const atlantis::Result<T, E>& actual) {
  REQUIRE(expected.isOk() == actual.isOk());
  if (expected.isOk()) {
    CHECK(bytesEqual(expected.value(), actual.value()));
  } else {
    CHECK(expected.error() == actual.error());
  }
}

// Every input the frame turns into GPU bytes, compared byte for byte.
void expectEquivalent(const ValidatedSceneData& scene) {
  atlantis::world::SceneInstance authoring = atlantis::world::instantiateScene(scene);
  const legacy::FrameInputs expected = legacy::collect(authoring.world);

  atlantis::world::BakedScene baked = atlantis::world::bakeScene(scene);
  const std::optional<ActiveCameraInput> camera = collectActiveCamera(baked);
  const std::vector<LightExtractionInput> lights = collectLights(baked);
  const std::vector<RenderableExtractionInput> renderables = collectRenderables(baked);
  const std::vector<AssetId> referencedMaterialIds = collectReferencedMaterialIds(renderables);

  // The camera: component, world matrix, and what the frame derives from them.
  REQUIRE(expected.camera.has_value() == camera.has_value());
  if (camera.has_value()) {
    CHECK(bytesEqual(expected.camera->camera, camera->camera));  // fov/near/far/exposure, fog, bloom
    CHECK(bytesEqual(expected.camera->worldMatrix, camera->worldMatrix));
    for (const float aspect : {1.0f, 16.0f / 9.0f}) {
      INFO("aspect " << aspect);
      expectSameResultBytes(
          extractCameraMatrices(expected.camera->worldMatrix, expected.camera->camera.fovYRadians,
                                expected.camera->camera.nearZ, expected.camera->camera.farZ, aspect),
          extractCameraMatrices(camera->worldMatrix, camera->camera.fovYRadians, camera->camera.nearZ,
                                camera->camera.farZ, aspect));
    }
    CHECK(bytesEqual(extractCameraWorldPosition(expected.camera->worldMatrix),
                     extractCameraWorldPosition(camera->worldMatrix)));
    CHECK(bytesEqual(extractFogData(expected.camera->camera.fog), extractFogData(camera->camera.fog)));
  }

  // Lights: the sequence, then the packed FrameLightingData (2096 bytes).
  REQUIRE(expected.lights.size() == lights.size());
  for (std::size_t i = 0; i < lights.size(); ++i) {
    INFO("light " << i);
    CHECK(bytesEqual(expected.lights[i].light, lights[i].light));
    CHECK(bytesEqual(expected.lights[i].worldMatrix, lights[i].worldMatrix));
  }
  expectSameResultBytes(extractFrameLightingData(expected.lights), extractFrameLightingData(lights));

  // The referenced-material sequence, in first-reference order.
  CHECK(expected.referencedMaterialIds == referencedMaterialIds);

  // The renderable sequence the DrawItem walk consumes: mesh, material, matrix.
  REQUIRE(expected.renderables.size() == renderables.size());
  for (std::size_t i = 0; i < renderables.size(); ++i) {
    INFO("renderable " << i);
    CHECK(expected.renderables[i].renderable.meshAsset == renderables[i].renderable.meshAsset);
    CHECK(expected.renderables[i].renderable.materialAsset == renderables[i].renderable.materialAsset);
    CHECK(bytesEqual(expected.renderables[i].worldMatrix, renderables[i].worldMatrix));
  }
}

[[nodiscard]] atlantis::asset_system::AssetCatalog loadBuildCatalog() {
  auto catalog = atlantis::asset_system::loadAssetCatalog(ATLANTIS_ASSET_CATALOG_PATH);
  REQUIRE(catalog.isOk());
  return std::move(catalog.value());
}

[[nodiscard]] ValidatedSceneData decodeRecord(const atlantis::asset_system::AssetCatalogRecord& record) {
  INFO("scene artifact " << record.artifact);
  auto scene = atlantis::asset_system::decodeScene(record.artifact, record.metadata);
  REQUIRE(scene.isOk());
  return std::move(scene.value());
}

[[nodiscard]] atlantis::asset_system::AssetGuid guidFromDefinition(const char* text) {
  auto parsed = atlantis::asset_system::parseAssetGuid(text);
  REQUIRE(parsed.isOk());
  return parsed.value();
}

}  // namespace

TEST_CASE("frame-input equivalence: every scene of the build catalog (the committed scenes, plus Bistro when "
          "built), bake path vs the world::World oracle, byte for byte",
          "[runtime][bake][equivalence]") {
  const atlantis::asset_system::AssetCatalog catalog = loadBuildCatalog();
  std::size_t scenes = 0;
  for (const auto& record : catalog.records()) {
    if (record.type != CatalogAssetType::Scene) continue;
    INFO("scene " << atlantis::asset_system::toString(record.guid) << " (" << record.artifact << ")");
    expectEquivalent(decodeRecord(record));
    ++scenes;
  }
  // The four Runtime whitelist scenes are among them (Spec 0051 Testing).
  for (const char* whitelisted : {ATLANTIS_WHITELIST_INTEGRATED_SHOWCASE_SCENE_GUID,
                                  ATLANTIS_WHITELIST_IBL_MATERIAL_DEMO_SCENE_GUID,
                                  ATLANTIS_WHITELIST_PBR_NORMAL_MAP_DEMO_SCENE_GUID,
                                  ATLANTIS_WHITELIST_PBR_MATERIALS_SHOWCASE_SCENE_GUID}) {
    INFO("whitelisted scene " << whitelisted);
    const auto* record = catalog.find(guidFromDefinition(whitelisted));
    REQUIRE(record != nullptr);
    CHECK(record->type == CatalogAssetType::Scene);
  }
  WARN("frame-input equivalence checked over " << scenes << " catalog scenes");
  CHECK(scenes >= 4);
}

TEST_CASE("frame-input equivalence: the imported Bistro scene (content-gated)", "[runtime][bake][equivalence][bistro]") {
#if !defined(ATLANTIS_BISTRO_IMPORT_GUID)
  SKIP("No Bistro build step: content/bistro was absent at configure time -- run tools/content/fetch_bistro.ps1");
#else
  const atlantis::asset_system::AssetCatalog catalog = loadBuildCatalog();
  const auto sceneGuid =
      atlantis::asset_system::deriveAssetGuid(guidFromDefinition(ATLANTIS_BISTRO_IMPORT_GUID), "scene");
  const auto* record = catalog.find(sceneGuid);
  if (record == nullptr) SKIP("The Bistro scene record is not in the build catalog: its build step has not run");
  REQUIRE(record->type == CatalogAssetType::Scene);
  const ValidatedSceneData scene = decodeRecord(*record);
  WARN("Bistro: " << scene.nodeCount() << " nodes");
  expectEquivalent(scene);
#endif
}
