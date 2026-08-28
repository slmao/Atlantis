#include "fixture/material_demo_fixture.h"

#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

// Plan 0018 Milestone 16 (Spec 0018 D12, V27-ish coverage extended to the
// real material path): the material_demo fixture's own GPU-required
// coverage. This file is committed WITHOUT the golden PNG/sidecar existing
// yet (ADR-0042's own "Initial baseline bootstrap" two-phase capture
// process) -- the full capture-compare-against-golden TEST_CASE is added
// in a separate, later commit alongside the golden itself, matching
// textured_quad_gpu_tests.cpp's own established precedent exactly. The
// three coverage cases below are all self-contained (no committed golden
// required): structural non-degeneracy, the D10 same-AssetId dedup proof
// (both scene nodes reference the SAME material -- this scene's own
// defining property, assets/scenes/material_demo.scene.txt), and repeated-
// call idempotency.

using atlantis::image_regression::kMaterialDemoExtentPixels;
using atlantis::image_regression::MaterialDemoFixture;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderMaterialDemoFrame;
using atlantis::image_regression::setUpMaterialDemoFixture;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_material_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_material_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_material_demo_scene_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath = std::string(ATLANTIS_MATERIAL_DEMO_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_MATERIAL_DEMO_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_MATERIAL_DEMO_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_MATERIAL_DEMO_SHADER_DIR) + "/textured_quad.frag.refl.json";
  return config;
}

[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2], buffer.rgba8[offset + 3]};
}

[[nodiscard]] bool nearBackgroundClearColor(const std::array<std::uint8_t, 4>& pixel) {
  // Renderer's own fixed kBackgroundClearColor (0.05, 0.05, 0.08, 1.0) ->
  // approximately (13, 13, 20, 255) in Rgba8Unorm bytes, matching
  // textured_quad_gpu_tests.cpp's own identical constant.
  const int tolerance = 6;
  return std::abs(static_cast<int>(pixel[0]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[1]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[2]) - 20) <= tolerance;
}

}  // namespace

TEST_CASE("Material demo fixture renders a non-degenerate frame with real texture coverage",
          "[image_regression][gpu][material_demo]") {
  auto fixtureResult = setUpMaterialDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  MaterialDemoFixture& fixture = fixtureResult.value();

  auto frameResult = renderMaterialDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kMaterialDemoExtentPixels);
  REQUIRE(frame.height == kMaterialDemoExtentPixels);

  // Not black/garbage: every alpha byte in the captured color image is 255
  // (Rgba8Unorm color attachment, no blending) -- sampled on a grid across
  // the whole frame, not just the quad regions.
  for (std::uint32_t y = 0; y < frame.height; y += 16) {
    for (std::uint32_t x = 0; x < frame.width; x += 16) {
      CHECK(pixelAt(frame, x, y)[3] == 255);
    }
  }

  // Both quads were genuinely drawn: a meaningful fraction of sampled
  // pixels differ from the untouched background clear color. This does
  // not hardcode a specific quad screen region (unlike
  // textured_quad_gpu_tests.cpp's own orthographic-camera fixture) since
  // material_demo_scene uses a real perspective camera -- a coarse
  // whole-frame coverage check is the robust, projection-independent
  // proof that something real was drawn.
  std::size_t nonBackgroundCount = 0;
  std::size_t sampledCount = 0;
  for (std::uint32_t y = 0; y < frame.height; y += 4) {
    for (std::uint32_t x = 0; x < frame.width; x += 4) {
      ++sampledCount;
      if (!nearBackgroundClearColor(pixelAt(frame, x, y))) ++nonBackgroundCount;
    }
  }
  REQUIRE(sampledCount > 0);
  const double coverageFraction = static_cast<double>(nonBackgroundCount) / static_cast<double>(sampledCount);
  // Real, measured coverage at this scene's own camera/quad geometry
  // (camera at (0,0,5), fovY=60 deg, two ~0.8x1.0 quads at z=0) is
  // ~4.7% of the frame -- consistent with the geometric estimate
  // (quad area / visible-plane area at z=0). The 0.02 floor leaves
  // comfortable margin while still ruling out "a handful of stray
  // pixels" (which would be well under 1%).
  CHECK(coverageFraction > 0.02);
}

TEST_CASE("Material demo fixture realizes the shared material exactly once, not once per node",
          "[image_regression][gpu][material_demo]") {
  // Spec 0018 D10's own dedup contract, proved end to end against the real
  // scene: material_demo.scene.txt declares two Renderable nodes that both
  // reference the SAME material AssetId (assets/scenes/material_demo.scene.txt).
  // computePendingMaterialIds()/realizePendingMaterials() (called for real
  // by renderMaterialDemoFrame(), never duplicated here) must therefore
  // realize exactly one SampledTexture/Sampler/Material, not two.
  auto fixtureResult = setUpMaterialDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  MaterialDemoFixture& fixture = fixtureResult.value();

  auto frameResult = renderMaterialDemoFrame(fixture);
  REQUIRE(frameResult.isOk());

  CHECK(fixture.materialResourceMap.size() == 1);
  CHECK(fixture.sampledTextureResourceMap.size() == 1);
  CHECK(fixture.samplerResourceMap.size() == 1);
}

TEST_CASE("Material demo fixture: repeated render cycles against the same fixture succeed independently",
          "[image_regression][gpu][material_demo]") {
  auto fixtureResult = setUpMaterialDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  MaterialDemoFixture& fixture = fixtureResult.value();

  auto firstResult = renderMaterialDemoFrame(fixture);
  REQUIRE(firstResult.isOk());
  // The first call already realized the shared material -- a second call
  // must find computePendingMaterialIds() empty and perform no new upload,
  // yet still produce byte-identical output.
  auto secondResult = renderMaterialDemoFrame(fixture);
  REQUIRE(secondResult.isOk());

  CHECK(firstResult.value().rgba8 == secondResult.value().rgba8);
  CHECK(fixture.materialResourceMap.size() == 1);
}
