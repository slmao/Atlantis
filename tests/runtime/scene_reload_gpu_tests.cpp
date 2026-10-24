// Plan 0065 M4 (P4; Spec 0065 R7, R8, R10; ADR-0131 D2-D7): reloading the
// Runtime's scene from an applied authoring document, on the GPU, under the
// windowed smoke configuration with fatal Validation Layers.
//
// A reload is compared with a fresh start on the same document. A windowed
// RuntimeApplication is one per process, so the fresh start runs in its own
// hidden test ([.reload_fresh], a CTest fixture setup: it cooks the edited
// document into the build tree, adds it to a copy of the build's catalog,
// starts on it and records the frame's data and capture), and the reload test
// ([.reload_compare], requiring that fixture) compares against what it
// recorded. The remaining tests are discovered as usual; [.j3] measures
// Bistro's apply by hand (Plan 0065 J3: reported, not a verdict).

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/authoring_scene.h>
#include <atlantis/asset_system/authoring_scene_mapping.h>
#include <atlantis/asset_system/cook_scene.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/scene_source.h>
#include <atlantis/authoring/authoring_session.h>
#include <atlantis/connection/authoring_connection.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/connection/runtime_control.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/runtime/runtime_control_host.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/ecs/world_components.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace atlantis::runtime {

// Plan 0065 M4: the resource maps a reload swaps and releases (ADR-0131 D4).
struct SceneReloadTestAccess {
  template <typename Map>
  [[nodiscard]] static std::vector<atlantis::asset_system::AssetId> keys(const Map& map) {
    std::vector<atlantis::asset_system::AssetId> out;
    for (const auto& entry : map) out.push_back(entry.first);
    std::sort(out.begin(), out.end());
    return out;
  }
  [[nodiscard]] static std::vector<std::vector<atlantis::asset_system::AssetId>> resourceKeys(
      const RuntimeApplication& app) {
    return {keys(app.meshResourceMap_),  keys(app.pickGeometry_),          keys(app.materialDataMap_),
            keys(app.materialResourceMap_), keys(app.samplerResourceMap_), keys(app.textureDataMap_),
            keys(app.sampledTextureResourceMap_)};
  }
};

}  // namespace atlantis::runtime

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::createRuntimeApplication;
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;

namespace {

namespace fs = std::filesystem;
namespace as = atlantis::asset_system;
namespace scene = atlantis::asset_system::scene;
namespace ac = atlantis::connection::authoring;
namespace access = atlantis::world::access;
using atlantis::authoring::AuthoringSession;
using atlantis::authoring::DocumentSource;
using atlantis::authoring::HandAuthored;
using atlantis::authoring::Imported;

// --- Copied from runtime_smoke_gpu_tests.cpp's anonymous namespace (the
// windowed smoke config, as main.cpp populates it), so that verified file
// stays untouched. --------------------------------------------------------------

// The ATLANTIS_RUNTIME_*_GUID definitions are generated from the committed
// catalog source at configure time (Plan 0047 P15).
[[nodiscard]] atlantis::asset_system::AssetGuid sceneGuidFromDefinition(const char* text) {
  auto parsed = atlantis::asset_system::parseAssetGuid(text);
  REQUIRE(parsed.isOk());
  return parsed.value();
}

// The windowed smoke config, as main.cpp populates it. Plan 0044 M2
// follow-up: shared by the bloom smoke TEST_CASE below, which swaps only
// the scene paths.
[[nodiscard]] BootstrapConfig buildSmokeConfig() {
  BootstrapConfig config;
  config.applicationName = "Atlantis Runtime GPU Smoke Test";
  config.vertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.spv";
  config.vertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.refl.json";
  config.fragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.spv";
  config.fragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.refl.json";
  config.assetArtifactPath = ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH;
  config.assetMetadataPath = ATLANTIS_RUNTIME_ASSET_METADATA_PATH;
  // Plan 0015 Section D11: the real, loaded scene path -- replaces the
  // former hardcoded six-entity validation scene.
  config.assetCatalogPath = ATLANTIS_ASSET_CATALOG_PATH;
  config.sceneAsset = sceneGuidFromDefinition(ATLANTIS_RUNTIME_SCENE_GUID);
  // Plan 0018 Section P10: mirrors main.cpp's own identical population
  // of the second, MaterialKind::UnlitTextured built-in shader pair.
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  // Plan 0019 Section P6/P11: mirrors main.cpp's own identical
  // population of the third, MaterialKind::LitTextured built-in shader
  // pair.
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  // Plan 0023 Milestone 5: mirrors main.cpp's own identical population
  // of the fourth, MaterialKind::PbrDirectLit built-in shader pair.
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  // Plan 0029 Section P15: mirrors main.cpp's own identical population
  // of the fifth, normal-map MaterialKind::PbrDirectLit built-in shader
  // pair -- unconditionally required.
  config.pbrDirectLitNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) + "/pbr_direct_lit_normal_map.vert.spv";
  config.pbrDirectLitNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.vert.refl.json";
  config.pbrDirectLitNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) + "/pbr_direct_lit_normal_map.frag.spv";
  config.pbrDirectLitNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.frag.refl.json";
  // Plan 0028 Milestone 2: mirrors main.cpp's own identical population --
  // the default scene now configures an environment (sky + IBL), which
  // this TEST_CASE's own config previously left unset.
  config.environmentArtifactPath = ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_RUNTIME_ENVIRONMENT_METADATA_PATH;
  config.pbrIblVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.refl.json";
  // Plan 0029 Section P15: mirrors main.cpp's own identical population
  // of the normal-map IBL PBR pair -- required in exactly the same
  // environment-configured case as pbrIbl above.
  config.pbrIblNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.refl.json";
  config.skyVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.frag.refl.json";
  // Plan 0024 Milestone 6/7: mirrors main.cpp's own identical
  // population of the two output-transform shader pairs.
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.frag.refl.json";
  config.outputTransformSrgbVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.vert.spv";
  config.outputTransformSrgbVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.vert.refl.json";
  config.outputTransformSrgbFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.frag.spv";
  config.outputTransformSrgbFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.frag.refl.json";
  // Plan 0027 Milestone 8 (ADR-0072 D-1): the shadow-casting shader pair
  // -- unconditionally required (mirrors main.cpp's own identical
  // population).
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  // Plan 0044 Milestone 1 (P9): the three bloom shader pairs, as main.cpp
  // sets them -- so the Runtime's bloom Pipelines are created here, under
  // Validation Layers, even when the scene does not turn bloom on.
  config.bloomDownsampleVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.vert.spv";
  config.bloomDownsampleVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.vert.refl.json";
  config.bloomDownsampleFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.frag.spv";
  config.bloomDownsampleFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.frag.refl.json";
  config.bloomUpsampleVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.vert.spv";
  config.bloomUpsampleVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.vert.refl.json";
  config.bloomUpsampleFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.frag.spv";
  config.bloomUpsampleFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.frag.refl.json";
  config.bloomCompositeVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.vert.spv";
  config.bloomCompositeVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.vert.refl.json";
  config.bloomCompositeFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.frag.spv";
  config.bloomCompositeFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.frag.refl.json";
  config.enableValidationLayers = true;
  return config;
}

// ---------------------------------------------------------------- helpers

[[nodiscard]] std::string readText(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream text;
  text << in.rdbuf();
  return text.str();
}

void writeText(const fs::path& path, const std::string& text) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out << text;
}

[[nodiscard]] scene::AuthoringScene documentOf(const std::string& text) {
  const auto parsed = as::parseSceneSource(text);
  REQUIRE(parsed.isOk());
  const auto document = scene::toAuthoringScene(parsed.value());
  REQUIRE(document.isOk());
  return document.value();
}

[[nodiscard]] std::string textOf(const scene::AuthoringScene& document) {
  const auto parsed = scene::toParsedSceneSource(document);
  REQUIRE(parsed.isOk());
  return as::serializeSceneSource(parsed.value());
}

[[nodiscard]] fs::path committedScene(std::string_view stem) {
  return fs::path{ATLANTIS_ASSETS_DIR} / "scenes" / (std::string(stem) + ".scene.txt");
}

[[nodiscard]] as::AssetGuid defaultScene() { return sceneGuidFromDefinition(ATLANTIS_RUNTIME_SCENE_GUID); }

const as::AssetGuid kSeed = as::parseAssetGuid("00650065-0065-4065-8065-00650065c0de").value();

// The edited default scene (Plan 0065 M4's scene-sized state): the default
// scene (integrated_showcase_demo) plus eight renderables whose materials and
// textures it does not load -- six textured showcase materials, a normal-mapped
// one and an emissive one, each with a mesh a committed scene draws it with --
// and four point lights, which it has none of. Within the scene limits; every
// material kind the smoke configuration's shaders cover.
[[nodiscard]] scene::AuthoringScene editedDocument() {
  scene::AuthoringScene document = documentOf(readText(committedScene("integrated_showcase_demo")));
  const std::array<std::string_view, 8> materials{
      "057d2d26-8bb6-4a8a-a05a-39ec65a84857", "20de5aa4-036f-463a-850e-7459d9d65e60",
      "263c6710-5f23-44a9-894d-833b6db666ac", "356c994e-46f6-4968-90fa-10f4aa68e53c",
      "3aea4db8-a2d9-48ae-b1f8-b3675c45afcf", "4d9e0e73-68b3-431a-901f-09fb26f09cc5",
      "c37331e0-dc7e-44c8-9728-303f0fce5ed3", "0c18ffa6-4446-4120-b6f9-841e8c09c070"};
  std::vector<scene::AuthoringScene> sources;
  for (const std::string_view stem : {"pbr_materials_showcase", "pbr_normal_map_demo", "emissive_demo"}) {
    sources.push_back(documentOf(readText(committedScene(stem))));
  }
  for (std::size_t i = 0; i < materials.size(); ++i) {
    const as::AssetGuid material = as::parseAssetGuid(materials[i]).value();
    std::optional<scene::AuthoringNode> found;
    for (const auto& source : sources) {
      for (const auto& node : source.nodes) {
        if (!found && node.renderable && node.renderable->materialAsset == material) found = node;
      }
    }
    REQUIRE(found.has_value());
    scene::AuthoringNode added;
    added.guid = as::deriveEntityGuid(kSeed, "reload/renderable/" + std::to_string(i));
    added.transform = found->transform;
    added.transform.localPosition = {-3.5f + static_cast<float>(i), 0.5f, -1.0f};
    added.renderable = found->renderable;
    document.nodes.push_back(added);
  }
  for (std::size_t i = 0; i < 4; ++i) {
    scene::AuthoringNode light;
    light.guid = as::deriveEntityGuid(kSeed, "reload/light/" + std::to_string(i));
    light.transform.localPosition = {-3.0f + 2.0f * static_cast<float>(i), 2.0f, 1.0f};
    light.light = scene::Light{scene::LightKind::Point, {1.0f, 0.8f, 0.6f}, 4.0f, 6.0f};
    document.nodes.push_back(light);
  }
  return document;
}

// The edits that turn the default scene into editedDocument(), as authoring
// commands: one CreateNode and its properties per added node.
[[nodiscard]] std::vector<ac::Command> editsToEditedDocument() {
  const scene::AuthoringScene original = documentOf(readText(committedScene("integrated_showcase_demo")));
  const scene::AuthoringScene edited = editedDocument();
  const auto transform = atlantis::schema::typeId("asset_system::scene::Transform");
  const auto renderable = atlantis::schema::typeId("asset_system::scene::Renderable");
  const auto light = atlantis::schema::typeId("asset_system::scene::Light");
  const auto field = [](std::string_view owner, std::string_view name) {
    return atlantis::schema::fieldId(std::string("asset_system::scene::") + std::string(owner), name);
  };
  std::vector<ac::Command> commands;
  for (std::size_t i = original.nodes.size(); i < edited.nodes.size(); ++i) {
    const scene::AuthoringNode& node = edited.nodes[i];
    const auto at = [&](atlantis::schema::TypeId component, std::string_view owner, std::string_view name) {
      return ac::PropertyAddress{node.guid, component, field(owner, name)};
    };
    commands.push_back(ac::CreateNode{node.guid, i, {node.renderable ? renderable : light}});
    commands.push_back(ac::SetProperty{at(transform, "Transform", "localPosition"), node.transform.localPosition});
    commands.push_back(
        ac::SetProperty{at(transform, "Transform", "localEulerAnglesRadians"), node.transform.localEulerAnglesRadians});
    commands.push_back(ac::SetProperty{at(transform, "Transform", "localScale"), node.transform.localScale});
    if (node.renderable) {
      commands.push_back(ac::SetProperty{at(renderable, "Renderable", "meshAsset"), node.renderable->meshAsset});
      commands.push_back(ac::SetProperty{at(renderable, "Renderable", "materialAsset"), *node.renderable->materialAsset});
    } else {
      commands.push_back(ac::SetProperty{at(light, "Light", "kind"),
                                         access::EnumValue{static_cast<std::int64_t>(node.light->kind)}});
      commands.push_back(ac::SetProperty{at(light, "Light", "color"), node.light->color});
      commands.push_back(ac::SetProperty{at(light, "Light", "intensity"), node.light->intensity});
      commands.push_back(ac::SetProperty{at(light, "Light", "range"), node.light->range});
    }
  }
  return commands;
}

// A frame's data as bytes, for comparison across processes.
[[nodiscard]] std::string bytesOf(const atlantis::connection::FrameData& data) {
  std::string out;
  const auto put = [&out](const void* p, std::size_t n) { out.append(static_cast<const char*>(p), n); };
  const std::uint64_t directional = data.directionalLights.size();
  const std::uint64_t point = data.pointLights.size();
  put(&directional, sizeof(directional));
  for (const auto& l : data.directionalLights) {
    put(l.direction.data(), sizeof(l.direction));
    put(l.color.data(), sizeof(l.color));
    put(&l.intensity, sizeof(l.intensity));
  }
  put(&point, sizeof(point));
  for (const auto& l : data.pointLights) {
    put(l.position.data(), sizeof(l.position));
    put(l.color.data(), sizeof(l.color));
    put(&l.intensity, sizeof(l.intensity));
    put(&l.range, sizeof(l.range));
  }
  put(data.view.data(), sizeof(data.view));
  put(data.projection.data(), sizeof(data.projection));
  put(&data.drawItemCount, sizeof(data.drawItemCount));
  return out;
}

// What the hidden fixture pair shares, in the build tree: the catalog must sit
// where the build's does (its locations are relative and may not climb).
[[nodiscard]] fs::path fixtureDir() { return fs::path{ATLANTIS_BUILD_DIR} / "scene_reload_test"; }
[[nodiscard]] fs::path fixtureCatalog() { return fs::path{ATLANTIS_BUILD_DIR} / "scene_reload_test.catalog.txt"; }
const as::AssetGuid kEditedScene = as::deriveAssetGuid(kSeed, "reload/edited_scene");

// The host's part (ADR-0129 D2, ADR-0131 D3): an authoring session over a copy
// of a committed scene source, whose sink is the application's reload request
// and which is told of every swap.
struct AuthoringHost {
  AuthoringHost(RuntimeApplication& application, const fs::path& source, const as::AssetGuid& sceneGuid)
      : app(application),
        copy(copied(source)),
        session(DocumentSource{sceneGuid, HandAuthored{copy}}),
        connection(session.connect()) {
    session.setSceneSink([this](as::ValidatedSceneData scene) { return app.requestSceneReload(std::move(scene)); });
    REQUIRE(connection->open().isOk());
  }
  ~AuthoringHost() {
    connection.reset();
    std::error_code ec;
    fs::remove_all(dir(), ec);
  }
  AuthoringHost(const AuthoringHost&) = delete;
  AuthoringHost& operator=(const AuthoringHost&) = delete;

  // The session edits a copy, never the committed source.
  [[nodiscard]] static fs::path copied(const fs::path& source) {
    fs::create_directories(dir());
    const fs::path target = dir() / "scene.scene.txt";
    REQUIRE(fs::copy_file(source, target, fs::copy_options::overwrite_existing));
    return target;
  }

  [[nodiscard]] static fs::path dir() {
    static const fs::path path = fs::temp_directory_path() / "atlantis_scene_reload_gpu_tests" /
                                 std::to_string(std::random_device{}());
    return path;
  }

  // The host loop: frames, and Applied{generation} after every swap.
  void frames(int count) {
    for (int i = 0; i < count; ++i) {
      REQUIRE(app.shouldContinue());
      app.runFrame();
      if (app.sceneGeneration() != generation) {
        generation = app.sceneGeneration();
        session.reportApplied(generation);
      }
    }
    REQUIRE(app.shouldContinue());
  }

  RuntimeApplication& app;
  fs::path copy;
  AuthoringSession session;
  std::unique_ptr<atlantis::connection::AuthoringConnection> connection;
  std::uint64_t generation = 0;
};

[[nodiscard]] std::vector<ac::Event> drain(atlantis::connection::AuthoringConnection& connection,
                                           atlantis::connection::SubscriptionId subscription) {
  auto events = connection.drainEvents(subscription);
  REQUIRE(events.isOk());
  return events.value();
}

const auto kSun = as::parseEntityGuid("0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea").value();  // the default scene's light

[[nodiscard]] access::PropertyAddress sunIntensity() {
  return {kSun, atlantis::world::ecs::componentTypeId<atlantis::world::Light>(),
          atlantis::schema::fieldId("world::Light", "intensity")};
}

}  // namespace

// ============================================ reload = fresh start (fixture)

TEST_CASE("scene reload fixture: a fresh start on the edited default scene", "[.reload_fresh][runtime][gpu][reload]") {
  fs::create_directories(fixtureDir());
  const scene::AuthoringScene edited = editedDocument();
  const auto cooked = as::cookSceneToMemory(edited, kEditedScene);
  REQUIRE(cooked.isOk());
  {
    std::ofstream artifact(fixtureDir() / "edited.ascene", std::ios::binary | std::ios::trunc);
    artifact.write(reinterpret_cast<const char*>(cooked.value().artifactBytes.data()),
                   static_cast<std::streamsize>(cooked.value().artifactBytes.size()));
  }
  writeText(fixtureDir() / "edited.ascene.meta.txt", cooked.value().metadataText);

  // The build's catalog, plus the edited scene.
  auto records = as::parseAssetCatalogRecords(readText(ATLANTIS_ASSET_CATALOG_PATH));
  REQUIRE(records.isOk());
  as::AssetCatalogRecord record;
  record.guid = kEditedScene;
  record.assetId = as::assetKey(kEditedScene);
  record.type = as::CatalogAssetType::Scene;
  record.source = as::CatalogSourceId{as::CatalogRoot::Assets, "scene_reload_test/edited.scene.txt", ""};
  record.artifact = "scene_reload_test/edited.ascene";
  record.metadata = "scene_reload_test/edited.ascene.meta.txt";
  record.artifactSchema = as::kSceneArtifactSchemaVersion;
  record.tool = "test";
  records.value().push_back(record);
  writeText(fixtureCatalog(), as::serializeAssetCatalog(records.value()));

  BootstrapConfig config = buildSmokeConfig();
  config.assetCatalogPath = fixtureCatalog().string();
  config.sceneAsset = kEditedScene;
  config.retainPickGeometry = true;
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  for (int i = 0; i < 3; ++i) app.runFrame();
  REQUIRE(app.shouldContinue());
  const auto data = app.captureFrameData();
  REQUIRE(data.isOk());
  writeText(fixtureDir() / "fresh.frame.bin", bytesOf(data.value()));
  REQUIRE(app.captureImage((fixtureDir() / "fresh.png").string()).isOk());
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

TEST_CASE("scene reload: a reload of an edited document equals a fresh start on it; applying back releases",
          "[.reload_compare][runtime][gpu][reload]") {
  REQUIRE(fs::exists(fixtureDir() / "fresh.frame.bin"));  // the fixture ran
  BootstrapConfig config = buildSmokeConfig();
  config.retainPickGeometry = true;
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    AuthoringHost host(app, committedScene("integrated_showcase_demo"), defaultScene());
    host.frames(3);
    const auto initialKeys = atlantis::runtime::SceneReloadTestAccess::resourceKeys(app);
    const auto initial = app.captureFrameData();
    REQUIRE(initial.isOk());
    REQUIRE(app.captureImage((AuthoringHost::dir() / "initial.png").string()).isOk());
    const auto applied = host.connection->subscribe(
        {ac::EventKindSet::only(ac::EventKind::Applied), std::nullopt, std::nullopt});

    // Edit the document into the edited scene, then apply.
    (void)host.connection->submitTransaction(editsToEditedDocument());
    REQUIRE(host.connection->drainFailures().empty());
    REQUIRE(*host.session.document() == editedDocument());
    REQUIRE(host.connection->apply().isOk());
    CHECK(app.sceneGeneration() == 0);  // the swap waits for the frame boundary
    host.frames(3);
    CHECK(app.sceneGeneration() == 1);
    CHECK(drain(*host.connection, applied) == std::vector<ac::Event>{ac::Applied{1}});

    // The reloaded World's frame and capture equal a fresh start's.
    const auto reloaded = app.captureFrameData();
    REQUIRE(reloaded.isOk());
    CHECK(bytesOf(reloaded.value()) == readText(fixtureDir() / "fresh.frame.bin"));
    CHECK(reloaded.value().pointLights.size() == 4);
    REQUIRE(app.captureImage((AuthoringHost::dir() / "reloaded.png").string()).isOk());
    CHECK(readText(AuthoringHost::dir() / "reloaded.png") == readText(fixtureDir() / "fresh.png"));
    const auto editedKeys = atlantis::runtime::SceneReloadTestAccess::resourceKeys(app);
    CHECK(editedKeys[3].size() == initialKeys[3].size() + 8);  // eight more realized materials

    // Applying the original back releases what it does not reference.
    REQUIRE(host.connection->open().isOk());
    REQUIRE(host.connection->apply().isOk());
    host.frames(3);
    CHECK(app.sceneGeneration() == 2);
    CHECK(drain(*host.connection, applied) == std::vector<ac::Event>{ac::Applied{2}});
    CHECK(atlantis::runtime::SceneReloadTestAccess::resourceKeys(app) == initialKeys);
    const auto back = app.captureFrameData();
    REQUIRE(back.isOk());
    CHECK(back.value() == initial.value());
    REQUIRE(app.captureImage((AuthoringHost::dir() / "back.png").string()).isOk());
    CHECK(readText(AuthoringHost::dir() / "back.png") == readText(AuthoringHost::dir() / "initial.png"));
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

// ===================================================================== others

TEST_CASE("scene reload: a reload that cannot resolve its assets leaves the current World", "[runtime][gpu][reload]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    AuthoringHost host(app, committedScene("integrated_showcase_demo"), defaultScene());
    host.frames(2);
    const auto before = app.captureFrameData();
    REQUIRE(before.isOk());
    const auto all = host.connection->subscribe({});

    // A renderable whose mesh no catalog record names: the document is valid
    // and cooks (references are values), the Runtime cannot resolve it.
    const auto guid = as::deriveEntityGuid(kSeed, "reload/unresolvable");
    const auto renderable = atlantis::schema::typeId("asset_system::scene::Renderable");
    (void)host.connection->submitTransaction(
        {ac::CreateNode{guid, 0, {renderable}},
         ac::SetProperty{{guid, renderable, atlantis::schema::fieldId("asset_system::scene::Renderable", "meshAsset")},
                         as::deriveAssetGuid(kSeed, "reload/no_such_mesh")}});
    REQUIRE(host.connection->drainFailures().empty());
    (void)host.connection->drainEvents(all);
    REQUIRE(host.connection->apply().isOk());  // accepted: the reload happens at the boundary
    host.frames(2);
    CHECK(app.sceneGeneration() == 0);
    CHECK(drain(*host.connection, all).empty());  // no Applied
    const auto after = app.captureFrameData();
    REQUIRE(after.isOk());
    CHECK(after.value() == before.value());
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

TEST_CASE("scene reload: connections survive with their filters, the generation is reported, and a runtime-side "
          "edit lasts until the next reload",
          "[runtime][gpu][reload]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    AuthoringHost host(app, committedScene("integrated_showcase_demo"), defaultScene());
    atlantis::runtime::RuntimeControlHost control(atlantis::runtime::RuntimeControlHost::forApplication(app),
                                                  {.recordDiagnostics = false});
    const auto runtime = app.openConnection();
    const auto sunEvents = runtime->subscribe(atlantis::connection::EventFilter{
        atlantis::connection::EventKindSet::only(atlantis::connection::EventKind::PropertyChanged), kSun,
        std::nullopt});
    host.frames(2);
    CHECK(control.status().sceneGeneration == 0);

    // A runtime-side edit (as the CLI's `property set` makes one).
    const access::CommandTicket edit = runtime->submit(access::SetProperty{sunIntensity(), 6.0f});
    host.frames(1);
    CHECK(app.captureFrameData().value().directionalLights.at(0).intensity == 6.0f);
    CHECK(runtime->drainEvents(sunEvents).value().size() == 1);

    // Apply the unchanged document: the edit is gone, the generation reported.
    REQUIRE(host.connection->apply().isOk());
    host.frames(1);
    CHECK(app.sceneGeneration() == 1);
    CHECK(control.status().sceneGeneration == 1);
    CHECK(app.captureFrameData().value().directionalLights.at(0).intensity == 3.0f);
    CHECK(runtime->getProperty(sunIntensity()).value() == access::PropertyValue{3.0f});
    CHECK(runtime->drainEvents(sunEvents).value().empty());

    // The same connection, re-bound: its subscription keeps its filter, its
    // tickets keep increasing, its refusals reach it.
    const access::CommandTicket later = runtime->submit(access::SetProperty{sunIntensity(), 7.0f});
    CHECK(later.value > edit.value);
    const access::CommandTicket refused = runtime->submit(access::DestroyEntity{as::deriveEntityGuid(kSeed, "none")});
    host.frames(1);
    CHECK(runtime->drainEvents(sunEvents).value() ==
          std::vector<access::Event>{access::PropertyChanged{sunIntensity(), 7.0f}});
    CHECK(runtime->drainFailures() ==
          std::vector<access::CommandFailure>{{refused, access::AccessError::UnknownEntity}});
    CHECK(app.captureFrameData().value().directionalLights.at(0).intensity == 7.0f);

    // Two requests before one frame: only the newer is performed (Plan 0065 J5).
    const auto sun = atlantis::schema::typeId("asset_system::scene::Light");
    const ac::PropertyAddress documentSun{kSun, sun, atlantis::schema::fieldId("asset_system::scene::Light", "intensity")};
    host.connection->submit(ac::SetProperty{documentSun, 9.0f});
    REQUIRE(host.connection->apply().isOk());
    host.connection->submit(ac::SetProperty{documentSun, 4.0f});
    REQUIRE(host.connection->apply().isOk());
    host.frames(1);
    CHECK(app.sceneGeneration() == 2);
    CHECK(app.captureFrameData().value().directionalLights.at(0).intensity == 4.0f);
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

// Plan 0065 M4 (Spec 0065 third Correction; ADR-0131 Accepted Amendment
// 2026-10-23): under pause a reload waits with the held commands. Paused, a
// World command and an apply leave the World unswapped and the command
// pending; the Step applies the command to the old World, then swaps,
// incrementing the generation, and delivers both Applied{generation} and the
// command's event -- every submitted command settles.
TEST_CASE("scene reload: under pause the reload waits with the held commands and follows them on Step",
          "[runtime][gpu][reload][hold]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    AuthoringHost host(app, committedScene("integrated_showcase_demo"), defaultScene());
    atlantis::runtime::RuntimeControlHost control(atlantis::runtime::RuntimeControlHost::forApplication(app),
                                                  {.recordDiagnostics = false});
    const auto runtime = app.openConnection();
    const auto sunEvents = runtime->subscribe(atlantis::connection::EventFilter{
        atlantis::connection::EventKindSet::only(atlantis::connection::EventKind::PropertyChanged), kSun,
        std::nullopt});
    const auto applied = host.connection->subscribe(
        ac::EventFilter{ac::EventKindSet::only(ac::EventKind::Applied), std::nullopt, std::nullopt});
    // atlantis_runtime's loop: the control's hold decision around each frame,
    // then Applied{generation} after a swap.
    const auto frame = [&] {
      REQUIRE(app.shouldContinue());
      control.beforeFrame();
      app.runFrame();
      control.afterFrame();
      if (app.sceneGeneration() != host.generation) {
        host.generation = app.sceneGeneration();
        host.session.reportApplied(host.generation);
      }
    };
    frame();
    frame();

    control.pause();
    const access::CommandTicket edit = runtime->submit(access::SetProperty{sunIntensity(), 6.0f});
    REQUIRE(host.connection->apply().isOk());
    frame();
    frame();
    CHECK(app.sceneGeneration() == 0);
    CHECK(control.status().sceneGeneration == 0);
    CHECK(runtime->getProperty(sunIntensity()).value() == access::PropertyValue{3.0f});  // still pending
    CHECK(runtime->drainEvents(sunEvents).value().empty());
    CHECK(runtime->drainFailures().empty());
    CHECK(drain(*host.connection, applied).empty());

    std::optional<atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>> stepped;
    control.step(atlantis::connection::StepRequest{1, std::nullopt},
                 [&stepped](atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError> r) {
                   stepped.emplace(std::move(r));
                 });
    frame();
    REQUIRE(stepped.has_value());
    CHECK(stepped->isOk());
    CHECK(app.sceneGeneration() == 1);
    CHECK(control.status().sceneGeneration == 1);
    CHECK(drain(*host.connection, applied) == std::vector<ac::Event>{ac::Applied{1}});
    // The held command applied to the old World before the swap, and settled.
    CHECK(runtime->drainEvents(sunEvents).value() ==
          std::vector<access::Event>{access::PropertyChanged{sunIntensity(), 6.0f}});
    CHECK(runtime->drainFailures().empty());
    // The new World is the document's.
    CHECK(runtime->getProperty(sunIntensity()).value() == access::PropertyValue{3.0f});
    CHECK(app.captureFrameData().value().directionalLights.at(0).intensity == 3.0f);
    CHECK(edit.value > 0);
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

// ====================================================== J3 (by hand, Release)

TEST_CASE("J3: apply on Bistro's generated document", "[.j3][runtime][gpu][reload][bistro]") {
#if !defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
  SKIP("Bistro content is not present (the content-gated build step was not declared)");
#else
  using Clock = std::chrono::steady_clock;
  const auto ms = [](Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
  };
  BootstrapConfig config = buildSmokeConfig();
  const as::AssetGuid bistro =
      as::deriveAssetGuid(sceneGuidFromDefinition(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID), "scene");
  config.sceneAsset = bistro;
  config.environmentArtifactPath.clear();
  config.environmentMetadataPath.clear();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    AuthoringSession session(DocumentSource{bistro, Imported{fs::path{ATLANTIS_RUNTIME_BISTRO_SCENE_SOURCE}}});
    session.setSceneSink([&app](as::ValidatedSceneData scene) { return app.requestSceneReload(std::move(scene)); });
    auto connection = session.connect();
    REQUIRE(connection->open().isOk());
    const auto lamp = as::parseEntityGuid("6b63b12c-9cde-4ae2-8391-c0b4cefadb7d").value();
    const ac::PropertyAddress intensity{lamp, atlantis::schema::typeId("asset_system::scene::Light"),
                                        atlantis::schema::fieldId("asset_system::scene::Light", "intensity")};
    for (int i = 0; i < 5; ++i) app.runFrame();
    std::vector<double> frame, apply, reloadFrame;
    for (int run = 0; run < 5; ++run) {
      for (int i = 0; i < 3; ++i) {
        const auto t0 = Clock::now();
        app.runFrame();
        frame.push_back(ms(t0, Clock::now()));
      }
      connection->submit(ac::SetProperty{intensity, 12.0f + static_cast<float>(run)});
      REQUIRE(connection->drainFailures().empty());
      const auto t0 = Clock::now();
      REQUIRE(connection->apply().isOk());
      const auto t1 = Clock::now();
      app.runFrame();
      const auto t2 = Clock::now();
      apply.push_back(ms(t0, t1));
      reloadFrame.push_back(ms(t1, t2));
      REQUIRE(app.sceneGeneration() == static_cast<std::uint64_t>(run + 1));
    }
    const auto median = [](std::vector<double> v) {
      std::sort(v.begin(), v.end());
      return v[v.size() / 2];
    };
    std::printf("J3 Bistro: apply (cook + decode + request) median %.1f ms; the reload frame median %.1f ms vs a "
                "frame %.1f ms -- apply to the swapped frame ~%.1f ms (target < 1000 ms)\n",
                median(apply), median(reloadFrame), median(frame),
                median(apply) + median(reloadFrame) - median(frame));
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
#endif
}
