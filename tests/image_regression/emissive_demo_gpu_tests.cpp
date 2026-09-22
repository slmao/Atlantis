#include "fixture/emissive_demo_fixture.h"
#include "support/golden_validity.h"
#include "support/emissive_differential.h"
#include "support/pixel_diff.h"
#include "support/tone_mapping_reference.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>

// Plan 0041 Milestone 3 (Spec 0041 R6, rulings Q1/Q4): the emissive dark
// scene, assets/scenes/emissive_demo.scene.txt -- five pbr_sphere nodes
// (orange, green and blue emissive PbrDirectLit spheres, the orange one
// above 1 in its red channel; a normal-mapped emissive sphere; the
// no-emissive control), no light, no environment. The config is the
// golden generator's own (golden_generator/emissive_demo_main.cpp),
// copied rather than shared, as every fixture/generator pair here does.

using atlantis::image_regression::EmissiveDemoFixture;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::projectMaterialSphere;
using atlantis::image_regression::renderEmissiveDemoFrame;
using atlantis::image_regression::setUpEmissiveDemoFixture;
using atlantis::image_regression::tonemapAndEncodeUnorm;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_emissive_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_emissive_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_emissive_demo_scene_MANIFEST_PATH;
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
  // Plan 0041 Milestone 3: no environment -- the dark scene (the fixture
  // treats empty environment paths as "none").
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

struct EmissiveSphere {
  const char* materialPath;
  float emissive[3];  // (0, 0, 0) for the control
};

// assets/scenes/emissive_demo.scene.txt's own five spheres, left to right.
constexpr std::array<EmissiveSphere, 5> kSpheres = {{
    {"materials/emissive_demo_orange.material.txt", {4.0f, 0.5f, 0.0f}},
    {"materials/emissive_demo_green.material.txt", {0.1f, 1.0f, 0.25f}},
    {"materials/emissive_demo_blue.material.txt", {0.2f, 0.3f, 2.5f}},
    {"materials/emissive_demo_normal_mapped.material.txt", {0.75f, 0.75f, 0.75f}},
    {"materials/pbr_normal_mapped_control.material.txt", {0.0f, 0.0f, 0.0f}},
}};

[[nodiscard]] int expectedChannel(float emissive) {
  return static_cast<int>(std::lround(tonemapAndEncodeUnorm(emissive) * 255.0f));
}

}  // namespace

// Spec 0041 R6 (ruling Q4, "dark-scene exact"): with no light and no
// environment, pbr_direct_lit's accumulated lighting is exactly 0 (it has
// no ambient term), so each sphere's pixels are exactly the output
// transform of its emissiveFactor -- independent of its base colour, its
// normal map and any light. The control sphere, with no emissive, must be
// exactly black: nothing else contributes anything.
TEST_CASE("emissive_demo: each sphere is exactly tonemap(emissiveFactor) with no light at all, and the "
          "no-emissive control sphere is black",
          "[image_regression][gpu][emissive]") {
  auto fixtureResult = setUpEmissiveDemoFixture(buildConfig(), ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH,
                                                ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  EmissiveDemoFixture& fixture = fixtureResult.value();
  auto frameResult = renderEmissiveDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();

  for (const EmissiveSphere& sphere : kSpheres) {
    INFO("sphere: " << sphere.materialPath);
    const auto circle = projectMaterialSphere(fixture, atlantis::asset_system::computeAssetId(sphere.materialPath),
                                              1.0f, frame.width);
    REQUIRE(circle.has_value());
    // The centre and four points half a radius away -- the whole disc is
    // one flat colour when nothing but emissive contributes.
    const float half = circle->radius / (1.25f * 2.0f);
    const std::array<std::array<float, 2>, 5> samples = {{{0.0f, 0.0f},
                                                          {half, 0.0f},
                                                          {-half, 0.0f},
                                                          {0.0f, half},
                                                          {0.0f, -half}}};
    for (const auto& offset : samples) {
      const auto x = static_cast<std::uint32_t>(circle->centerX + offset[0]);
      const auto y = static_cast<std::uint32_t>(circle->centerY + offset[1]);
      const std::size_t index = (static_cast<std::size_t>(y) * frame.width + x) * 4;
      for (int c = 0; c < 3; ++c) {
        const int actual = frame.rgba8[index + static_cast<std::size_t>(c)];
        const int expected = expectedChannel(sphere.emissive[c]);
        INFO("pixel (" << x << ", " << y << ") channel " << c << ": actual " << actual << ", expected " << expected);
        CHECK(std::abs(actual - expected) <= 1);
      }
    }
  }

  REQUIRE(fixture.device->waitIdle().isOk());
}

// ---------------------------------------------------------------------------
// Plan 0041 Milestone 3, golden commit (ADR-0042 Initial baseline
// bootstrap): the golden was captured by
// atlantis_image_regression_emissive_demo_golden_generator against the
// clean, already-committed tree at its recorded source_revision; these two
// TEST_CASEs land with it.
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kEmissiveDemoGoldenName = "emissive_demo/emissive_demo_512x512_rgba8unorm";
constexpr const char* kEmissiveDemoGoldenSlug = "emissive_demo_512x512_rgba8unorm";

}  // namespace

TEST_CASE("Full capture-compare cycle against the committed emissive_demo golden passes",
          "[image_regression][gpu][emissive]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  std::filesystem::remove(outputDir / (std::string(kEmissiveDemoGoldenSlug) + "_actual.png"));
  std::filesystem::remove(outputDir / (std::string(kEmissiveDemoGoldenSlug) + "_diff.png"));

  auto fixtureResult = setUpEmissiveDemoFixture(buildConfig(), ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH,
                                                ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  EmissiveDemoFixture& fixture = fixtureResult.value();
  auto renderResult = renderEmissiveDemoFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& actual = renderResult.value();

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult =
      atlantis::image_regression::loadAndValidateGolden(goldensDir / (std::string(kEmissiveDemoGoldenName) + ".png"),
                                                        goldensDir / (std::string(kEmissiveDemoGoldenName) + ".sidecar.txt"));
  {
    INFO("INVALID GOLDEN: the committed emissive_demo golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& validatedGolden = goldenResult.value();
  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const auto report = atlantis::image_regression::compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)atlantis::image_regression::writeFailureArtifacts(outputDir, kEmissiveDemoGoldenSlug, actual,
                                                            validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("The emissive_demo frame with every emissiveFactor zeroed fails comparison against the real "
          "emissive_demo golden",
          "[image_regression][gpu][emissive]") {
  // The discriminator for the golden itself: with no emissive, the four
  // emissive discs collapse to the control's black, so the image the
  // golden records cannot be produced without the emissive term.
  auto fixtureResult = setUpEmissiveDemoFixture(buildConfig(), ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH,
                                                ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  EmissiveDemoFixture& fixture = fixtureResult.value();
  for (auto& [id, material] : fixture.materialDataMap) {
    for (float& component : material.emissiveFactor) component = 0.0f;
  }
  auto renderResult = renderEmissiveDemoFrame(fixture);
  REQUIRE(renderResult.isOk());

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult =
      atlantis::image_regression::loadAndValidateGolden(goldensDir / (std::string(kEmissiveDemoGoldenName) + ".png"),
                                                        goldensDir / (std::string(kEmissiveDemoGoldenName) + ".sidecar.txt"));
  REQUIRE(goldenResult.isOk());

  const auto report = atlantis::image_regression::compareBuffers(renderResult.value(), goldenResult.value().pixels);
  CHECK_FALSE(report.passed);
  CHECK(report.maxChannelDiff > 200);  // orange's red channel: 231 -> 0

  REQUIRE(fixture.device->waitIdle().isOk());
}
