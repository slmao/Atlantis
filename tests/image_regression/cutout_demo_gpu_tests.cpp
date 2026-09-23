// Plan 0042 Milestone 2 (Spec 0042 R6, Plan 0042 P5/Q4): the alpha-test
// path, golden-independently. The cutout_demo scene is a 3x3 plane facing
// the camera, textured with a hard-edged disc (alpha 255 inside, 0
// outside, one flat RGB everywhere) at alpha_mode: mask / cutoff 0.5, in
// front of an opaque sphere. Screen geometry (512x512, fov 60 deg, camera
// at z = 4): the plane spans x, y in about [66, 446], the disc is centred
// at (256, 256) with a radius of about 119 px, and the sphere (behind the
// plane, upper right) is centred near (336, 221) with a radius of about
// 89 px.

#include "fixture/cutout_demo_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"
#include "support/tone_mapping_reference.h"

#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

using atlantis::image_regression::CutoutDemoFixture;
using atlantis::image_regression::kCutoutDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderCutoutDemoFrame;
using atlantis::image_regression::setUpCutoutDemoFixture;
using atlantis::image_regression::tonemapAndEncodeUnorm;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_cutout_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_cutout_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_cutout_demo_scene_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  // Plan 0027 Milestone 9 (ADR-0072 D-1): the shadow-casting shader pair
  // -- unconditionally required (BootstrapConfig, Milestone 8).
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_CUTOUT_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_CUTOUT_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.refl.json";
  return config;
}

using Rgb = std::array<int, 3>;

[[nodiscard]] Rgb rgbAt(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2]};
}

[[nodiscard]] bool near(const Rgb& a, const Rgb& b, int tolerance) {
  return std::abs(a[0] - b[0]) <= tolerance && std::abs(a[1] - b[1]) <= tolerance &&
         std::abs(a[2] - b[2]) <= tolerance;
}

[[nodiscard]] int encodedByte(float linearValue) {
  return static_cast<int>(std::lround(tonemapAndEncodeUnorm(linearValue) * 255.0f));
}

// PbrMaterialDemoFixture's clear colour, (0.05, 0.05, 0.08) linear.
[[nodiscard]] Rgb clearColour() { return {encodedByte(0.05f), encodedByte(0.05f), encodedByte(0.08f)}; }

constexpr std::uint32_t kDiscCentre = 256;
// Inside the plane, outside the disc, nothing behind: the clear colour.
constexpr std::uint32_t kHoleOverClearX = 90;
constexpr std::uint32_t kHoleOverClearY = 400;
// Inside the plane, outside the disc, the sphere behind it.
constexpr std::uint32_t kHoleOverSphereX = 400;
constexpr std::uint32_t kHoleOverSphereY = 200;
// Inside the disc with the sphere behind it: depth-correct occlusion.
constexpr std::uint32_t kDiscOverSphereX = 300;
constexpr std::uint32_t kDiscOverSphereY = 221;

// Every material the scene marked Mask, forced back to Opaque before the
// first render (the fixture realizes inside its render call).
void forceMaskMaterialsOpaque(CutoutDemoFixture& fixture) {
  int changed = 0;
  for (auto& [id, data] : fixture.materialDataMap) {
    if (data.alphaMode == atlantis::asset_system::MaterialAlphaMode::Mask) {
      data.alphaMode = atlantis::asset_system::MaterialAlphaMode::Opaque;
      ++changed;
    }
  }
  REQUIRE(changed == 1);
}

}  // namespace

TEST_CASE("Cutout demo: the Mask material's transparent texels are discarded, showing what is behind",
          "[image_regression][gpu][transparency][cutout]") {
  auto fixtureResult = setUpCutoutDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  CutoutDemoFixture& fixture = fixtureResult.value();
  auto frameResult = renderCutoutDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kCutoutDemoExtentPixels);
  REQUIRE(frame.height == kCutoutDemoExtentPixels);

  const Rgb disc = rgbAt(frame, kDiscCentre, kDiscCentre);
  INFO("disc centre " << disc[0] << "," << disc[1] << "," << disc[2]);
  CHECK_FALSE(near(disc, clearColour(), 8));
  // The disc is the texture's warm orange, lit: red well above blue.
  CHECK(disc[0] > disc[2] + 40);

  // A hole over nothing shows exactly the clear colour.
  const Rgb holeOverClear = rgbAt(frame, kHoleOverClearX, kHoleOverClearY);
  INFO("hole over clear " << holeOverClear[0] << "," << holeOverClear[1] << "," << holeOverClear[2]);
  CHECK(near(holeOverClear, clearColour(), 1));

  // A hole over the sphere shows the sphere: neither the disc nor the clear colour.
  const Rgb holeOverSphere = rgbAt(frame, kHoleOverSphereX, kHoleOverSphereY);
  INFO("hole over sphere " << holeOverSphere[0] << "," << holeOverSphere[1] << "," << holeOverSphere[2]);
  CHECK_FALSE(near(holeOverSphere, clearColour(), 8));
  CHECK_FALSE(near(holeOverSphere, disc, 8));

  // The surviving disc still occludes the sphere behind it (it writes depth).
  const Rgb discOverSphere = rgbAt(frame, kDiscOverSphereX, kDiscOverSphereY);
  INFO("disc over sphere " << discOverSphere[0] << "," << discOverSphere[1] << "," << discOverSphere[2]);
  CHECK(near(discOverSphere, disc, 8));
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Cutout demo: the cutout edge is crisp -- every pixel on a row across it is disc or clear colour",
          "[image_regression][gpu][transparency][cutout]") {
  auto fixtureResult = setUpCutoutDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  auto frameResult = renderCutoutDemoFrame(fixtureResult.value());
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();

  // Row y = 256 from inside the plane's left edge to the disc centre: only
  // clear colour lies behind it, and the texture's RGB is flat, so an alpha
  // test yields exactly two populations with no blended pixel between them.
  const Rgb disc = rgbAt(frame, kDiscCentre, kDiscCentre);
  int discCount = 0;
  int clearCount = 0;
  int transitions = 0;
  bool previousIsDisc = false;
  for (std::uint32_t x = 80; x <= kDiscCentre; ++x) {
    const Rgb pixel = rgbAt(frame, x, kDiscCentre);
    const bool isDisc = near(pixel, disc, 8);
    const bool isClear = near(pixel, clearColour(), 1);
    INFO("x " << x << ": " << pixel[0] << "," << pixel[1] << "," << pixel[2]);
    CHECK((isDisc || isClear));
    if (isDisc) ++discCount;
    if (isClear) ++clearCount;
    if (x > 80 && isDisc != previousIsDisc) ++transitions;
    previousIsDisc = isDisc;
  }
  CHECK(discCount > 60);
  CHECK(clearCount > 30);
  CHECK(transitions == 1);
  REQUIRE(fixtureResult.value().device->waitIdle().isOk());
}

TEST_CASE("Cutout demo: the same scene with the material forced Opaque fills every hole with the plane",
          "[image_regression][gpu][transparency][cutout]") {
  auto fixtureResult = setUpCutoutDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  CutoutDemoFixture& fixture = fixtureResult.value();
  forceMaskMaterialsOpaque(fixture);
  auto frameResult = renderCutoutDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();

  // Opaque ignores alpha: the texture's flat orange RGB covers the whole
  // plane, so both former holes now show the lit plane -- not the clear
  // colour, not the grey-and-red sphere. (Not compared to the disc centre
  // exactly: the plane's specular term varies across its 3x3 extent.)
  for (const auto& [x, y] :
       {std::pair{kHoleOverClearX, kHoleOverClearY}, std::pair{kHoleOverSphereX, kHoleOverSphereY}}) {
    const Rgb pixel = rgbAt(frame, x, y);
    INFO("former hole (" << x << ", " << y << "): " << pixel[0] << "," << pixel[1] << "," << pixel[2]);
    CHECK_FALSE(near(pixel, clearColour(), 8));
    CHECK(pixel[0] > pixel[2] + 40);
  }
  REQUIRE(fixture.device->waitIdle().isOk());
}

// ---------------------------------------------------------------------------
// Plan 0042 Milestone 2, golden commit (ADR-0042 Initial baseline
// bootstrap): the golden was captured by
// atlantis_image_regression_cutout_demo_golden_generator against the clean,
// already-committed tree at its recorded source_revision; these two
// TEST_CASEs land with it.
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kCutoutDemoGoldenName = "cutout_demo/cutout_demo_512x512_rgba8unorm";
constexpr const char* kCutoutDemoGoldenSlug = "cutout_demo_512x512_rgba8unorm";

}  // namespace

TEST_CASE("Full capture-compare cycle against the committed cutout_demo golden passes",
          "[image_regression][gpu][transparency][cutout]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  std::filesystem::remove(outputDir / (std::string(kCutoutDemoGoldenSlug) + "_actual.png"));
  std::filesystem::remove(outputDir / (std::string(kCutoutDemoGoldenSlug) + "_diff.png"));

  auto fixtureResult = setUpCutoutDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  CutoutDemoFixture& fixture = fixtureResult.value();
  auto renderResult = renderCutoutDemoFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& actual = renderResult.value();

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult =
      atlantis::image_regression::loadAndValidateGolden(goldensDir / (std::string(kCutoutDemoGoldenName) + ".png"),
                                                        goldensDir / (std::string(kCutoutDemoGoldenName) + ".sidecar.txt"));
  {
    INFO("INVALID GOLDEN: the committed cutout_demo golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& validatedGolden = goldenResult.value();
  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const auto report = atlantis::image_regression::compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)atlantis::image_regression::writeFailureArtifacts(outputDir, kCutoutDemoGoldenSlug, actual,
                                                            validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("The cutout_demo frame with the material forced Opaque fails comparison against the real cutout_demo "
          "golden",
          "[image_regression][gpu][transparency][cutout]") {
  // The discriminator for the golden itself (Plan 0042 M2 item 5): without
  // the alpha test the whole 3x3 plane is drawn, so the image the golden
  // records cannot be produced unless transparent texels are discarded.
  auto fixtureResult = setUpCutoutDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  CutoutDemoFixture& fixture = fixtureResult.value();
  forceMaskMaterialsOpaque(fixture);
  auto renderResult = renderCutoutDemoFrame(fixture);
  REQUIRE(renderResult.isOk());

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult =
      atlantis::image_regression::loadAndValidateGolden(goldensDir / (std::string(kCutoutDemoGoldenName) + ".png"),
                                                        goldensDir / (std::string(kCutoutDemoGoldenName) + ".sidecar.txt"));
  REQUIRE(goldenResult.isOk());

  const auto report = atlantis::image_regression::compareBuffers(renderResult.value(), goldenResult.value().pixels);
  INFO("maxChannelDiff " << report.maxChannelDiff << ", out-of-tolerance pixels " << report.outOfToleranceCount);
  CHECK_FALSE(report.passed);
  CHECK(report.maxChannelDiff > 200);           // a hole's alpha: 255 -> 0
  CHECK(report.outOfToleranceCount > 50000);    // the plane outside the disc: about 380^2 - pi * 119^2 px

  REQUIRE(fixture.device->waitIdle().isOk());
}
