#include "fixture/hdr_roll_off_demo_fixture.h"
#include "support/tone_mapping_reference.h"

#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>

using atlantis::image_regression::HdrRollOffDemoFixture;
using atlantis::image_regression::kHdrRollOffDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderHdrRollOffDemoFrame;
using atlantis::image_regression::setUpHdrRollOffDemoFixture;
using atlantis::image_regression::tonemapAndEncodeUnorm;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_hdr_roll_off_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_hdr_roll_off_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_hdr_roll_off_demo_scene_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_HDR_ROLL_OFF_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.refl.json";
  return config;
}

[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2], buffer.rgba8[offset + 3]};
}

[[nodiscard]] std::uint8_t encodedByte(float linearValue) {
  return static_cast<std::uint8_t>(std::lround(tonemapAndEncodeUnorm(linearValue) * 255.0f));
}

[[nodiscard]] bool nearBackgroundClearColor(const std::array<std::uint8_t, 4>& pixel) {
  const std::array<std::uint8_t, 3> expected = {encodedByte(0.05f), encodedByte(0.05f), encodedByte(0.08f)};
  constexpr int kTolerance = 2;
  return std::abs(static_cast<int>(pixel[0]) - expected[0]) <= kTolerance &&
         std::abs(static_cast<int>(pixel[1]) - expected[1]) <= kTolerance &&
         std::abs(static_cast<int>(pixel[2]) - expected[2]) <= kTolerance;
}

}  // namespace

TEST_CASE("HDR roll-off demo fixture renders a non-degenerate frame with real sphere coverage",
          "[image_regression][gpu][hdr_roll_off]") {
  auto fixtureResult = setUpHdrRollOffDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  HdrRollOffDemoFixture& fixture = fixtureResult.value();

  auto frameResult = renderHdrRollOffDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kHdrRollOffDemoExtentPixels);
  REQUIRE(frame.height == kHdrRollOffDemoExtentPixels);

  std::size_t nonBackgroundCount = 0;
  std::size_t sampledCount = 0;
  for (std::uint32_t y = 0; y < frame.height; y += 4) {
    for (std::uint32_t x = 0; x < frame.width; x += 4) {
      const auto pixel = pixelAt(frame, x, y);
      CHECK(pixel[3] == 255);
      ++sampledCount;
      if (!nearBackgroundClearColor(pixel)) ++nonBackgroundCount;
    }
  }

  REQUIRE(sampledCount > 0);
  CHECK(static_cast<double>(nonBackgroundCount) / static_cast<double>(sampledCount) > 0.05);
}

TEST_CASE("HDR roll-off demo's over-range light produces a bright gradient without an RGB hard-clip plateau",
          "[image_regression][gpu][hdr_roll_off]") {
  auto fixtureResult = setUpHdrRollOffDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());

  auto frameResult = renderHdrRollOffDemoFrame(fixtureResult.value());
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();

  std::size_t brightPixelCount = 0;
  std::size_t clippedChannelCount = 0;
  std::set<std::uint8_t> brightLevels;
  for (std::size_t offset = 0; offset < frame.rgba8.size(); offset += 4) {
    const std::uint8_t brightest =
        std::max(frame.rgba8[offset], std::max(frame.rgba8[offset + 1], frame.rgba8[offset + 2]));
    if (brightest >= 200) {
      ++brightPixelCount;
      brightLevels.insert(brightest);
    }
    for (std::size_t channel = 0; channel < 3; ++channel) {
      if (frame.rgba8[offset + channel] == 255) ++clippedChannelCount;
    }
  }

  CHECK(brightPixelCount > 1000);
  CHECK(brightLevels.size() >= 12);
  CHECK(clippedChannelCount == 0);
}

TEST_CASE("HDR roll-off demo realizes one shared PBR material and renders repeatably",
          "[image_regression][gpu][hdr_roll_off]") {
  auto fixtureResult = setUpHdrRollOffDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  HdrRollOffDemoFixture& fixture = fixtureResult.value();

  auto firstResult = renderHdrRollOffDemoFrame(fixture);
  REQUIRE(firstResult.isOk());
  auto secondResult = renderHdrRollOffDemoFrame(fixture);
  REQUIRE(secondResult.isOk());

  CHECK(firstResult.value().rgba8 == secondResult.value().rgba8);
  CHECK(fixture.materialResourceMap.size() == 1);
  CHECK(fixture.sampledTextureResourceMap.size() == 1);
}
