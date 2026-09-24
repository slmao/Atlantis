// Plan 0043 Milestone 2 (Spec 0043 R1-R3, P6/P8/P9): the height-fog scenes.
// - fog_dark_demo (PbrNormalMapDemoFixture, no light, no environment):
//   emissive spheres at several distances and heights plus the black
//   control, each exactly tonemap(E * (1 - f) + C * f) -- checked against
//   fog_reference.h under the scene's own fog and over a 3 x 3
//   density x falloff sweep set on the World camera.
// - fog_distance_demo / fog_height_demo (PbrMaterialDemoFixture, lit): the
//   density and falloff sweeps as images; their goldens are the test:
//   commit's.
// - Neutrality: density 0 is fog off bit for bit, both through the scene
//   grammar (fog_zero_neutrality vs pbr_material_demo) and when the other
//   four parameters would overflow if evaluated.
// The per-variant saturating-fog differentials (P9) live in each variant's
// own test file, beside the config that realizes it.

#include "fixture/fog_demo_fixture.h"
#include "support/fog_differential.h"
#include "support/fog_reference.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/world/camera.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

using atlantis::image_regression::activeCameraFog;
using atlantis::image_regression::applyFog;
using atlantis::image_regression::encodeFogRgb;
using atlantis::image_regression::fogFactor;
using atlantis::image_regression::FogDarkDemoFixture;
using atlantis::image_regression::FogLitDemoFixture;
using atlantis::image_regression::FogParams;
using atlantis::image_regression::fogPixelRgb;
using atlantis::image_regression::FogRgb;
using atlantis::image_regression::fogRgbWithinOneLsb;
using atlantis::image_regression::FogVec3;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::projectMaterialSphere;
using atlantis::image_regression::renderFogDarkDemoFrame;
using atlantis::image_regression::renderFogLitDemoFrame;
using atlantis::image_regression::setActiveCameraFog;
using atlantis::image_regression::setUpFogDarkDemoFixture;
using atlantis::image_regression::setUpFogLitDemoFixture;
using atlantis::image_regression::sphereHitAtPixel;
using atlantis::image_regression::toFogParams;
using atlantis::runtime::BootstrapConfig;

namespace {

struct SceneFiles {
  const char* artifact;
  const char* metadata;
  const char* manifest;
};

[[nodiscard]] BootstrapConfig buildLitConfig(const SceneFiles& scene) {
  BootstrapConfig config;
  config.sceneArtifactPath = scene.artifact;
  config.sceneMetadataPath = scene.metadata;
  config.sceneDependencyManifestPath = scene.manifest;
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  // Plan 0027 Milestone 9 (ADR-0072 D-1): the shadow-casting shader pair
  // -- unconditionally required (BootstrapConfig, Milestone 8).
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_MATERIAL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.refl.json";
  return config;
}

[[nodiscard]] BootstrapConfig buildDarkConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_fog_dark_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_fog_dark_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_fog_dark_demo_scene_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  config.pbrDirectLitNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.vert.spv";
  config.pbrDirectLitNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.vert.refl.json";
  config.pbrDirectLitNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.frag.spv";
  config.pbrDirectLitNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.frag.refl.json";
  config.pbrIblVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.refl.json";
  config.pbrIblNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.refl.json";
  config.skyVertexShaderSpirvPath = std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_SKY_SHADER_DIR) + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_SKY_SHADER_DIR) + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_SKY_SHADER_DIR) + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_SKY_SHADER_DIR) + "/sky.frag.refl.json";
  // No environment: the dark scene (the fixture treats empty environment
  // paths as "none").
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_PBR_NORMAL_MAP_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.refl.json";
  return config;
}

constexpr SceneFiles kFogDistanceScene{ATLANTIS_fog_distance_demo_scene_ARTIFACT_PATH,
                                       ATLANTIS_fog_distance_demo_scene_METADATA_PATH,
                                       ATLANTIS_fog_distance_demo_scene_MANIFEST_PATH};
constexpr SceneFiles kFogHeightScene{ATLANTIS_fog_height_demo_scene_ARTIFACT_PATH,
                                     ATLANTIS_fog_height_demo_scene_METADATA_PATH,
                                     ATLANTIS_fog_height_demo_scene_MANIFEST_PATH};
constexpr SceneFiles kFogZeroNeutralityScene{ATLANTIS_fog_zero_neutrality_scene_ARTIFACT_PATH,
                                             ATLANTIS_fog_zero_neutrality_scene_METADATA_PATH,
                                             ATLANTIS_fog_zero_neutrality_scene_MANIFEST_PATH};
constexpr SceneFiles kPbrMaterialDemoScene{ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH,
                                           ATLANTIS_pbr_material_demo_scene_METADATA_PATH,
                                           ATLANTIS_pbr_material_demo_scene_MANIFEST_PATH};

[[nodiscard]] FogDarkDemoFixture setUpDark() {
  auto fixtureResult = setUpFogDarkDemoFixture(buildDarkConfig(), ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH,
                                               ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  return std::move(fixtureResult.value());
}

[[nodiscard]] FogLitDemoFixture setUpLit(const SceneFiles& scene) {
  auto fixtureResult = setUpFogLitDemoFixture(buildLitConfig(scene));
  REQUIRE(fixtureResult.isOk());
  return std::move(fixtureResult.value());
}

// assets/scenes/fog_dark_demo.scene.txt's five spheres: four emissive
// materials (two above 1 in a channel) and the black control, which
// becomes exactly C * f. Two variants: pbr_direct_lit, and
// pbr_direct_lit_normal_map for emissive_demo_normal_mapped.
constexpr std::array<const char*, 5> kDarkSphereMaterials = {
    "materials/emissive_demo_orange.material.txt",       "materials/emissive_demo_green.material.txt",
    "materials/emissive_demo_blue.material.txt",         "materials/emissive_demo_normal_mapped.material.txt",
    "materials/pbr_normal_mapped_control.material.txt",
};

struct DarkSceneComparison {
  int worstLsb = 0;
  int samples = 0;
  int controlSamples = 0;  // on the black control, where the pixel is C * f alone
};

// Spec 0043 R1-R2, exact: with no light and no environment, a sphere's
// unfogged radiance is exactly its emissiveFactor E, so every sampled
// pixel must be tonemap(E * (1 - f) + C * f), with f from fog_reference.h
// at the surface point that pixel sees. Five samples per sphere: the
// centre and four points half a radius out.
[[nodiscard]] DarkSceneComparison compareDarkSceneToReference(FogDarkDemoFixture& fixture, const PixelBuffer& frame) {
  const FogParams fog = toFogParams(activeCameraFog(fixture));
  DarkSceneComparison result;
  for (const char* materialPath : kDarkSphereMaterials) {
    INFO("sphere: " << materialPath);
    const auto asset = atlantis::asset_system::computeAssetId(materialPath);
    const auto material = fixture.materialDataMap.find(asset);
    REQUIRE(material != fixture.materialDataMap.end());
    const FogVec3 emissive{material->second.emissiveFactor[0], material->second.emissiveFactor[1],
                           material->second.emissiveFactor[2]};
    const auto circle = projectMaterialSphere(fixture, asset, 1.0f, frame.width);
    REQUIRE(circle.has_value());
    const float half = circle->radius / (1.25f * 2.0f);
    const std::array<std::array<float, 2>, 5> offsets = {
        {{0.0f, 0.0f}, {half, 0.0f}, {-half, 0.0f}, {0.0f, half}, {0.0f, -half}}};
    for (const auto& offset : offsets) {
      const auto x = static_cast<std::uint32_t>(circle->centerX + offset[0]);
      const auto y = static_cast<std::uint32_t>(circle->centerY + offset[1]);
      const auto hit = sphereHitAtPixel(fixture, asset, 1.0f, x, y, frame.width);
      REQUIRE(hit.has_value());
      const FogRgb expected = encodeFogRgb(applyFog(emissive, fog, hit->eye, hit->point));
      const FogRgb actual = fogPixelRgb(frame, x, y);
      for (std::size_t c = 0; c < 3; ++c) {
        result.worstLsb = std::max(result.worstLsb, std::abs(actual[c] - expected[c]));
      }
      INFO("pixel (" << x << ", " << y << "): actual " << actual[0] << "," << actual[1] << "," << actual[2]
                     << ", expected " << expected[0] << "," << expected[1] << "," << expected[2]);
      CHECK(fogRgbWithinOneLsb(actual, expected));
      ++result.samples;
      if (emissive[0] == 0.0f && emissive[1] == 0.0f && emissive[2] == 0.0f) ++result.controlSamples;
    }
  }
  return result;
}

[[nodiscard]] bool framesIdentical(const PixelBuffer& a, const PixelBuffer& b) {
  return a.width == b.width && a.height == b.height && a.rgba8 == b.rgba8;
}

[[nodiscard]] std::size_t countDifferingPixels(const PixelBuffer& a, const PixelBuffer& b) {
  std::size_t count = 0;
  for (std::size_t i = 0; i + 3 < a.rgba8.size(); i += 4) {
    if (a.rgba8[i] != b.rgba8[i] || a.rgba8[i + 1] != b.rgba8[i + 1] || a.rgba8[i + 2] != b.rgba8[i + 2] ||
        a.rgba8[i + 3] != b.rgba8[i + 3]) {
      ++count;
    }
  }
  return count;
}

}  // namespace

TEST_CASE("fog_dark_demo: every emissive sphere is exactly tonemap(E * (1 - f) + C * f) under the scene's fog, "
          "and the black control sphere is C * f",
          "[image_regression][gpu][fog]") {
  FogDarkDemoFixture fixture = setUpDark();
  const auto fog = activeCameraFog(fixture);
  REQUIRE(fog.density == 0.05f);  // authored on the scene's camera node
  auto frameResult = renderFogDarkDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const DarkSceneComparison comparison = compareDarkSceneToReference(fixture, frameResult.value());
  CHECK(comparison.samples == 25);
  CHECK(comparison.controlSamples == 5);
  CHECK(comparison.worstLsb <= 1);
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("fog_dark_demo: a 3 x 3 density x height-falloff sweep matches the fog reference within 1 LSB at every "
          "sampled pixel (Plan 0043 P8)",
          "[image_regression][gpu][fog]") {
  FogDarkDemoFixture fixture = setUpDark();
  const std::array<float, 3> densities = {0.02f, 0.06f, 0.15f};
  const std::array<float, 3> falloffs = {0.0f, 0.15f, 0.4f};
  for (const float density : densities) {
    for (const float falloff : falloffs) {
      atlantis::world::CameraFog fog;
      fog.color = {0.5f, 0.55f, 0.7f};
      fog.density = density;
      fog.height = -2.0f;
      fog.heightFalloff = falloff;
      fog.maxOpacity = 0.9f;  // reached by the densest cells: the clamp is swept too
      setActiveCameraFog(fixture, fog);
      auto frameResult = renderFogDarkDemoFrame(fixture);
      REQUIRE(frameResult.isOk());
      const DarkSceneComparison comparison = compareDarkSceneToReference(fixture, frameResult.value());
      // The control sphere's centre (C * f alone) doubles as a readable
      // record of the fog factor each cell produced.
      const auto controlAsset = atlantis::asset_system::computeAssetId(kDarkSphereMaterials[4]);
      const auto control = projectMaterialSphere(fixture, controlAsset, 1.0f, frameResult.value().width);
      REQUIRE(control.has_value());
      const auto cx = static_cast<std::uint32_t>(control->centerX);
      const auto cy = static_cast<std::uint32_t>(control->centerY);
      const auto hit = sphereHitAtPixel(fixture, controlAsset, 1.0f, cx, cy, frameResult.value().width);
      REQUIRE(hit.has_value());
      const float f = fogFactor(toFogParams(fog), hit->eye, hit->point);
      const FogRgb controlActual = fogPixelRgb(frameResult.value(), cx, cy);
      const FogRgb controlExpected = encodeFogRgb(applyFog({0.0f, 0.0f, 0.0f}, toFogParams(fog), hit->eye, hit->point));
      INFO("SWEEP density " << density << " falloff " << falloff << ": control f " << f << ", control pixel "
                            << controlActual[0] << "," << controlActual[1] << "," << controlActual[2]
                            << " vs reference " << controlExpected[0] << "," << controlExpected[1] << ","
                            << controlExpected[2] << "; worst |diff| over " << comparison.samples
                            << " samples = " << comparison.worstLsb << " LSB");
      CHECK(comparison.worstLsb <= 1);
    }
  }
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Height fog neutrality: a density-0 fog group renders byte-identically to no fog group (Plan 0043 P9)",
          "[image_regression][gpu][fog]") {
  // fog_zero_neutrality is pbr_material_demo with
  // "fog=0 2.0 0.5 0.8 fog_color=0.7 0.7 0.8" on its camera node, parsed,
  // cooked, decoded and uploaded like any other fog group.
  FogLitDemoFixture withGroup = setUpLit(kFogZeroNeutralityScene);
  REQUIRE(activeCameraFog(withGroup).density == 0.0f);
  REQUIRE(activeCameraFog(withGroup).heightFalloff == 0.5f);
  auto withGroupFrame = renderFogLitDemoFrame(withGroup);
  REQUIRE(withGroupFrame.isOk());
  REQUIRE(withGroup.device->waitIdle().isOk());

  FogLitDemoFixture withoutGroup = setUpLit(kPbrMaterialDemoScene);
  auto withoutGroupFrame = renderFogLitDemoFrame(withoutGroup);
  REQUIRE(withoutGroupFrame.isOk());
  CHECK(countDifferingPixels(withGroupFrame.value(), withoutGroupFrame.value()) == 0);
  CHECK(framesIdentical(withGroupFrame.value(), withoutGroupFrame.value()));
  REQUIRE(withoutGroup.device->waitIdle().isOk());
}

TEST_CASE("Height fog neutrality: density 0 skips the fog term even when evaluating it would overflow to NaN "
          "(Plan 0043 P6)",
          "[image_regression][gpu][fog]") {
  // height 1e4 above a camera at y = 0 with falloff 50 makes
  // e^(-falloff * h) = e^(5e5) = inf; a multiply-by-zero form would give
  // 0 * inf = NaN on every PBR pixel. The uniform branch must not run it.
  FogLitDemoFixture fixture = setUpLit(kPbrMaterialDemoScene);
  auto offFrame = renderFogLitDemoFrame(fixture);
  REQUIRE(offFrame.isOk());
  const PixelBuffer off = std::move(offFrame.value());

  atlantis::world::CameraFog hostile;
  hostile.color = {0.0f, 3.0f, 0.0f};
  hostile.density = 0.0f;
  hostile.height = 1.0e4f;
  hostile.heightFalloff = 50.0f;
  hostile.maxOpacity = 1.0f;
  setActiveCameraFog(fixture, hostile);
  auto hostileFrame = renderFogLitDemoFrame(fixture);
  REQUIRE(hostileFrame.isOk());
  CHECK(countDifferingPixels(off, hostileFrame.value()) == 0);
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("fog_distance_demo and fog_height_demo: fog is on, and switching it off changes only PBR surfaces",
          "[image_regression][gpu][fog]") {
  for (const SceneFiles* scene : {&kFogDistanceScene, &kFogHeightScene}) {
    INFO("scene: " << scene->artifact);
    FogLitDemoFixture fixture = setUpLit(*scene);
    const auto authored = activeCameraFog(fixture);
    REQUIRE(authored.density > 0.0f);
    auto onFrame = renderFogLitDemoFrame(fixture);
    REQUIRE(onFrame.isOk());
    const PixelBuffer on = std::move(onFrame.value());

    atlantis::world::CameraFog off = authored;
    off.density = 0.0f;
    setActiveCameraFog(fixture, off);
    auto offFrame = renderFogLitDemoFrame(fixture);
    REQUIRE(offFrame.isOk());
    // Every sphere and the floor are fogged: tens of thousands of pixels.
    CHECK(countDifferingPixels(on, offFrame.value()) > 20000);
    REQUIRE(fixture.device->waitIdle().isOk());
  }
}
