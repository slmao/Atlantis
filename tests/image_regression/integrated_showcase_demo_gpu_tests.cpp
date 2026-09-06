#include "fixture/integrated_showcase_demo_fixture.h"

#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

// Plan 0028 Milestone 4 (Spec 0028 FR3/FR4/FR6): the integrated showcase
// demo fixture's own GPU-required coverage, landed WITHOUT a golden yet,
// per ADR-0042's own two-phase capture process (matching every sibling
// demo's own identical precedent). The golden PNG/sidecar and its own
// capture-compare TEST_CASE land in Milestone 5's own separate, later
// commit.

using atlantis::image_regression::IntegratedShowcaseDemoFixture;
using atlantis::image_regression::kIntegratedShowcaseDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderIntegratedShowcaseDemoFrame;
using atlantis::image_regression::setUpIntegratedShowcaseDemoFixture;
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

  // Plan 0028's own conservative, not-yet-measured floor (carried over
  // from shadow_gpu_tests.cpp's own already-validated Group B R2-R1
  // check) -- if this does not hold on real hardware, stop and request
  // Human Review per the Plan's own Non-negotiable rule, do not retune
  // silently.
  CHECK(r2RgbSum - r1RgbSum > 15);

  REQUIRE(fixture.device->waitIdle().isOk());
}
