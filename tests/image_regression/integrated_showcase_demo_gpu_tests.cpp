#include "fixture/integrated_showcase_demo_fixture.h"
#include "support/golden_validity.h"

#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// Plan 0028 Milestone 4/5 (Spec 0028 FR3/FR4/FR6/FR9): the integrated
// showcase demo fixture's own GPU-required coverage. The first two
// TEST_CASEs below landed without a golden (Milestone 4); the final
// capture-compare TEST_CASE landed together with the golden PNG/sidecar
// themselves (Milestone 5), per ADR-0042's own two-phase process --
// mirrors pbr_material_demo_gpu_tests.cpp's own identical structure.

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::IntegratedShowcaseDemoFixture;
using atlantis::image_regression::kIntegratedShowcaseDemoExtentPixels;
using atlantis::image_regression::loadAndValidateGolden;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderIntegratedShowcaseDemoFrame;
using atlantis::image_regression::setUpIntegratedShowcaseDemoFixture;
using atlantis::image_regression::writeFailureArtifacts;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_integrated_showcase_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_integrated_showcase_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_integrated_showcase_demo_scene_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  config.pbrIblVertexShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.refl.json";
  // Plan 0029 Section P15: required by validateEnvironmentBootstrapConfig()
  // (this scene always configures an environment), mirroring the pbrIbl*
  // block above -- unused by this fixture's own scene (no normal-mapped
  // material), but still validated.
  config.pbrIblNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_ibl_normal_map.frag.refl.json";
  config.skyVertexShaderSpirvPath = std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_SKY_SHADER_DIR) + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_SKY_SHADER_DIR) + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_SKY_SHADER_DIR) + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_SKY_SHADER_DIR) + "/sky.frag.refl.json";
  config.environmentArtifactPath = ATLANTIS_INTEGRATED_SHOWCASE_DEMO_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_INTEGRATED_SHOWCASE_DEMO_ENVIRONMENT_METADATA_PATH;
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_INTEGRATED_SHOWCASE_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.refl.json";
  return config;
}

// File-local, mirroring shadow_gpu_tests.cpp's own identical pattern
// (pixel_diff.h provides only PixelBuffer/compareBuffers() -- no
// per-pixel accessor, no channel-sum helper). rgbSum, not luminance: a
// plain sum of three raw RGB8 channel values, not a photometric
// quantity.
[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& frame, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * frame.width + x) * 4;
  return {frame.rgba8[offset], frame.rgba8[offset + 1], frame.rgba8[offset + 2], frame.rgba8[offset + 3]};
}

[[nodiscard]] int rgbSum(const std::array<std::uint8_t, 4>& pixel) {
  return static_cast<int>(pixel[0]) + static_cast<int>(pixel[1]) + static_cast<int>(pixel[2]);
}

}  // namespace

TEST_CASE("Integrated showcase demo fixture realizes exactly 6 renderables, 2 GPU Meshes, and 4 GPU Materials",
          "[image_regression][gpu][integrated_showcase]") {
  auto fixtureResult = setUpIntegratedShowcaseDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  IntegratedShowcaseDemoFixture& fixture = fixtureResult.value();

  auto frameResult = renderIntegratedShowcaseDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kIntegratedShowcaseDemoExtentPixels);
  REQUIRE(frame.height == kIntegratedShowcaseDemoExtentPixels);

  CHECK(fixture.world->renderableEntities().size() == 6);
  CHECK(fixture.meshResourceMap.size() == 2);
  CHECK(fixture.materialResourceMap.size() == 4);
  CHECK(fixture.lastDrawItemCount == 6);
}

TEST_CASE("Integrated showcase demo: the ground receives a real shadow (R1 real casters vs. R2 no casters, "
          "same pixel)",
          "[image_regression][gpu][integrated_showcase]") {
  // Spec 0028 FR6: a same-pixel, two-render differential -- every input
  // identical except the shadow-caster list -- rather than a two-point
  // comparison, so checker-texture/view-angle/IBL differences can never
  // be mistaken for the shadow's own effect. Sampled point: Sphere A's
  // own shadow footprint on the ground, pixel (158,347) (Plan 0028
  // "Shadow Derivation").
  constexpr std::uint32_t kShadowPixelX = 158;
  constexpr std::uint32_t kShadowPixelY = 347;

  auto fixtureResult = setUpIntegratedShowcaseDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  IntegratedShowcaseDemoFixture& fixture = fixtureResult.value();

  // Warm-up (not asserted): environment/material realization happens
  // lazily on first use, so this ensures R1 and R2 below both render
  // against already-realized, steady-state resources -- differing only
  // in caster-list content.
  auto warmUpResult = renderIntegratedShowcaseDemoFrame(fixture, /*includeShadowCasters=*/true);
  REQUIRE(warmUpResult.isOk());

  auto r1Result = renderIntegratedShowcaseDemoFrame(fixture, /*includeShadowCasters=*/true);
  REQUIRE(r1Result.isOk());
  const int r1RgbSum = rgbSum(pixelAt(r1Result.value(), kShadowPixelX, kShadowPixelY));

  auto r2Result = renderIntegratedShowcaseDemoFrame(fixture, /*includeShadowCasters=*/false);
  REQUIRE(r2Result.isOk());
  const int r2RgbSum = rgbSum(pixelAt(r2Result.value(), kShadowPixelX, kShadowPixelY));

  INFO("R1 (real casters) rgbSum at (" << kShadowPixelX << "," << kShadowPixelY << ") = " << r1RgbSum);
  INFO("R2 (no casters) rgbSum at (" << kShadowPixelX << "," << kShadowPixelY << ") = " << r2RgbSum);

  // Plan 0028's own conservative floor (carried over from
  // shadow_gpu_tests.cpp's own already-validated Group B R2-R1 check) --
  // confirmed on real hardware at 299 (R1=200, R2=499), comfortably
  // above it. If a future real GPU capture no longer clears it, stop
  // and request Human Review per the Plan's own Non-negotiable rule, do
  // not retune silently.
  CHECK(r2RgbSum - r1RgbSum > 15);

  REQUIRE(fixture.device->waitIdle().isOk());
}

namespace {
constexpr const char* kIntegratedShowcaseDemoGoldenName =
    "integrated_showcase_demo/integrated_showcase_demo_512x512_rgba8unorm";
constexpr const char* kIntegratedShowcaseDemoGoldenSlug = "integrated_showcase_demo_512x512_rgba8unorm";
}  // namespace

// Plan 0028 Milestone 5: lands together with the golden PNG/sidecar
// themselves, in their own separate commit, per ADR-0042's own two-
// phase capture process -- mirrors pbr_material_demo_gpu_tests.cpp's
// own identical "Full capture-compare cycle..." TEST_CASE exactly. Uses
// the fixture's own default (real-caster) render path, matching the
// golden generator's own path exactly.
TEST_CASE("Full capture-compare cycle against the committed integrated_showcase_demo golden passes",
          "[image_regression][gpu][integrated_showcase]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact =
      outputDir / (std::string(kIntegratedShowcaseDemoGoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact =
      outputDir / (std::string(kIntegratedShowcaseDemoGoldenSlug) + "_diff.png");
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  auto fixtureResult = setUpIntegratedShowcaseDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  IntegratedShowcaseDemoFixture& fixture = fixtureResult.value();

  auto renderResult = renderIntegratedShowcaseDemoFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& actual = renderResult.value();

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult =
      loadAndValidateGolden(goldensDir / (std::string(kIntegratedShowcaseDemoGoldenName) + ".png"),
                             goldensDir / (std::string(kIntegratedShowcaseDemoGoldenName) + ".sidecar.txt"));
  {
    INFO("INVALID GOLDEN: the committed integrated_showcase_demo golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& validatedGolden = goldenResult.value();

  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const auto report = compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(outputDir, kIntegratedShowcaseDemoGoldenSlug, actual, validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}
