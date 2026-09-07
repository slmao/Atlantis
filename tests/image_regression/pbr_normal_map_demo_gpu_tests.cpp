#include "fixture/pbr_normal_map_demo_fixture.h"
#include "support/golden_validity.h"

#include <atlantis/result.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// Plan 0029 Section P19: the normal-map demo fixture's own GPU-required
// coverage. Milestone 5 landed the non-degenerate-frame proof and the
// two discriminative tests below with no golden PNG/sidecar and no
// capture-compare TEST_CASE yet (ADR-0042's own two-phase process).
// Milestone 6 (this file's own final TEST_CASE below) lands together
// with the golden PNG/sidecar themselves, in their own separate commit,
// mirroring integrated_showcase_demo_gpu_tests.cpp's own identical
// "Full capture-compare cycle ..." TEST_CASE exactly -- captured
// against R1 only (includeShadowCasters=true, useControlMaterial=false,
// the fixture's own default render path), via ADR-0042's own "Initial
// baseline bootstrap" category. Per the 2026-09-07 Human-Approved
// Implementation Deviation
// (../../plans/0029-tangent-space-normal-mapping-foundation.md#human-approved-implementation-deviation--2026-09-07),
// this golden's own sphere shows a disclosed, pre-existing Spec 0027/
// ADR-0072 shadow-bias limitation (grazing-angle self-shadow acne),
// confirmed unrelated to this file's own tangent-space/normal-map work
// -- accepted as this scene's own initial baseline as-is, not deferred
// further.

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::loadAndValidateGolden;
using atlantis::image_regression::PbrNormalMapDemoFixture;
using atlantis::image_regression::kPbrNormalMapDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderPbrNormalMapDemoFrame;
using atlantis::image_regression::setUpPbrNormalMapDemoFixture;
using atlantis::image_regression::writeFailureArtifacts;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_pbr_normal_map_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_normal_map_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_normal_map_demo_scene_MANIFEST_PATH;
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
  config.environmentArtifactPath = ATLANTIS_PBR_NORMAL_MAP_DEMO_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_PBR_NORMAL_MAP_DEMO_ENVIRONMENT_METADATA_PATH;
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

[[nodiscard]] atlantis::Result<PbrNormalMapDemoFixture, atlantis::image_regression::PbrNormalMapDemoSetupError>
setUpFixture() {
  return setUpPbrNormalMapDemoFixture(buildTestConfig(), ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH,
                                       ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
}

// File-local, mirroring integrated_showcase_demo_gpu_tests.cpp's own
// identical pattern.
[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& frame, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * frame.width + x) * 4;
  return {frame.rgba8[offset], frame.rgba8[offset + 1], frame.rgba8[offset + 2], frame.rgba8[offset + 3]};
}

[[nodiscard]] int rgbSum(const std::array<std::uint8_t, 4>& pixel) {
  return static_cast<int>(pixel[0]) + static_cast<int>(pixel[1]) + static_cast<int>(pixel[2]);
}

}  // namespace

TEST_CASE("PBR normal-map demo fixture renders a non-degenerate frame with the real sphere and ground plane",
          "[image_regression][gpu][pbr_normal_map]") {
  auto fixtureResult = setUpFixture();
  REQUIRE(fixtureResult.isOk());
  PbrNormalMapDemoFixture& fixture = fixtureResult.value();

  auto frameResult = renderPbrNormalMapDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kPbrNormalMapDemoExtentPixels);
  REQUIRE(frame.height == kPbrNormalMapDemoExtentPixels);

  CHECK(fixture.world->renderableEntities().size() == 2);
  CHECK(fixture.meshResourceMap.size() == 2);
  CHECK(fixture.materialResourceMap.size() == 2);
  CHECK(fixture.lastDrawItemCount == 2);
  CHECK(fixture.controlMaterial != nullptr);

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("PBR normal-map demo: the normal map visibly brightens the sphere at (256,256) vs. the control material "
          "(R1 normal-mapped vs. R2 control, same pixel)",
          "[image_regression][gpu][pbr_normal_map]") {
  // Plan 0029 Section P19: a same-pixel, two-render differential --
  // every input identical except which Material the sphere's own
  // DrawItem borrows -- rather than a two-scene comparison, so shadow/
  // IBL/view-angle differences can never be mistaken for the normal
  // map's own effect. Sampled point: vertex 200's own world position
  // (0,0,1), projecting to pixel (256,256) under this scene's fixed
  // camera.
  constexpr std::uint32_t kNormalMapPixelX = 256;
  constexpr std::uint32_t kNormalMapPixelY = 256;

  auto fixtureResult = setUpFixture();
  REQUIRE(fixtureResult.isOk());
  PbrNormalMapDemoFixture& fixture = fixtureResult.value();

  // Warm-up (not asserted): environment/material realization (including
  // the control material's own lazy, one-time realization) happens on
  // first use, so this ensures R1 and R2 below both render against
  // already-realized, steady-state resources -- differing only in
  // which Material the sphere borrows.
  auto warmUpResult = renderPbrNormalMapDemoFrame(fixture, /*includeShadowCasters=*/true,
                                                   /*useControlMaterial=*/false);
  REQUIRE(warmUpResult.isOk());

  auto r1Result =
      renderPbrNormalMapDemoFrame(fixture, /*includeShadowCasters=*/true, /*useControlMaterial=*/false);
  REQUIRE(r1Result.isOk());
  const std::array<std::uint8_t, 4> r1Pixel = pixelAt(r1Result.value(), kNormalMapPixelX, kNormalMapPixelY);
  const int r1RgbSum = rgbSum(r1Pixel);

  auto r2Result =
      renderPbrNormalMapDemoFrame(fixture, /*includeShadowCasters=*/true, /*useControlMaterial=*/true);
  REQUIRE(r2Result.isOk());
  const std::array<std::uint8_t, 4> r2Pixel = pixelAt(r2Result.value(), kNormalMapPixelX, kNormalMapPixelY);
  const int r2RgbSum = rgbSum(r2Pixel);

  INFO("R1 (normal-mapped) RGB at (" << kNormalMapPixelX << "," << kNormalMapPixelY << ") = ("
                                      << static_cast<int>(r1Pixel[0]) << "," << static_cast<int>(r1Pixel[1]) << ","
                                      << static_cast<int>(r1Pixel[2]) << "), rgbSum = " << r1RgbSum);
  INFO("R2 (control) RGB at (" << kNormalMapPixelX << "," << kNormalMapPixelY << ") = (" << static_cast<int>(r2Pixel[0])
                                << "," << static_cast<int>(r2Pixel[1]) << "," << static_cast<int>(r2Pixel[2])
                                << "), rgbSum = " << r2RgbSum);
  INFO("delta (R1 - R2) = " << (r1RgbSum - r2RgbSum));

  // Plan 0029 Section P19's own computed threshold -- explicitly
  // disclosed as not yet measured on real GPU hardware for this exact
  // scene. If this bound does not hold, this assertion fails and
  // Implementation must stop and request Human Review, never silently
  // retune it.
  CHECK(r1RgbSum - r2RgbSum > 50);

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("PBR normal-map demo: the sphere casts a real shadow onto the ground at (198,273) (R1 shadowed vs. R2 "
          "unshadowed, same pixel)",
          "[image_regression][gpu][pbr_normal_map]") {
  // Plan 0029 Section P19: mirrors the normal-map differential above,
  // shadow axis instead -- every input identical except
  // shadowCasterDrawItems' own content.
  constexpr std::uint32_t kShadowPixelX = 198;
  constexpr std::uint32_t kShadowPixelY = 273;

  auto fixtureResult = setUpFixture();
  REQUIRE(fixtureResult.isOk());
  PbrNormalMapDemoFixture& fixture = fixtureResult.value();

  auto warmUpResult = renderPbrNormalMapDemoFrame(fixture, /*includeShadowCasters=*/true,
                                                   /*useControlMaterial=*/false);
  REQUIRE(warmUpResult.isOk());

  auto r1Result =
      renderPbrNormalMapDemoFrame(fixture, /*includeShadowCasters=*/true, /*useControlMaterial=*/false);
  REQUIRE(r1Result.isOk());
  const std::array<std::uint8_t, 4> r1Pixel = pixelAt(r1Result.value(), kShadowPixelX, kShadowPixelY);
  const int r1RgbSum = rgbSum(r1Pixel);

  auto r2Result =
      renderPbrNormalMapDemoFrame(fixture, /*includeShadowCasters=*/false, /*useControlMaterial=*/false);
  REQUIRE(r2Result.isOk());
  const std::array<std::uint8_t, 4> r2Pixel = pixelAt(r2Result.value(), kShadowPixelX, kShadowPixelY);
  const int r2RgbSum = rgbSum(r2Pixel);

  INFO("R1 (shadowed) RGB at (" << kShadowPixelX << "," << kShadowPixelY << ") = (" << static_cast<int>(r1Pixel[0])
                                 << "," << static_cast<int>(r1Pixel[1]) << "," << static_cast<int>(r1Pixel[2])
                                 << "), rgbSum = " << r1RgbSum);
  INFO("R2 (unshadowed) RGB at (" << kShadowPixelX << "," << kShadowPixelY << ") = (" << static_cast<int>(r2Pixel[0])
                                   << "," << static_cast<int>(r2Pixel[1]) << "," << static_cast<int>(r2Pixel[2])
                                   << "), rgbSum = " << r2RgbSum);
  INFO("delta (R2 - R1) = " << (r2RgbSum - r1RgbSum));

  // Plan 0029 Section P19's own borrowed, conservative threshold --
  // explicitly disclosed as not yet measured for this exact scene. If
  // this bound does not hold, this assertion fails and Implementation
  // must stop and request Human Review, never silently retune it.
  CHECK(r2RgbSum - r1RgbSum > 15);

  REQUIRE(fixture.device->waitIdle().isOk());
}

namespace {
constexpr const char* kPbrNormalMapDemoGoldenName = "pbr_normal_map_demo/pbr_normal_map_demo_512x512_rgba8unorm";
constexpr const char* kPbrNormalMapDemoGoldenSlug = "pbr_normal_map_demo_512x512_rgba8unorm";
}  // namespace

// Milestone 6: lands together with the golden PNG/sidecar themselves,
// in their own separate commit, per ADR-0042's own two-phase capture
// process -- mirrors integrated_showcase_demo_gpu_tests.cpp's own
// identical "Full capture-compare cycle ..." TEST_CASE exactly. Uses
// the fixture's own default (R1: real casters, real normal-mapped
// material) render path, matching the golden generator's own path
// exactly.
TEST_CASE("Full capture-compare cycle against the committed pbr_normal_map_demo golden passes",
          "[image_regression][gpu][pbr_normal_map]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact = outputDir / (std::string(kPbrNormalMapDemoGoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact = outputDir / (std::string(kPbrNormalMapDemoGoldenSlug) + "_diff.png");
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  auto fixtureResult = setUpFixture();
  REQUIRE(fixtureResult.isOk());
  PbrNormalMapDemoFixture& fixture = fixtureResult.value();

  auto renderResult = renderPbrNormalMapDemoFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& actual = renderResult.value();

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult = loadAndValidateGolden(goldensDir / (std::string(kPbrNormalMapDemoGoldenName) + ".png"),
                                             goldensDir / (std::string(kPbrNormalMapDemoGoldenName) + ".sidecar.txt"));
  {
    INFO("INVALID GOLDEN: the committed pbr_normal_map_demo golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& validatedGolden = goldenResult.value();

  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const auto report = compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(outputDir, kPbrNormalMapDemoGoldenSlug, actual, validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}
