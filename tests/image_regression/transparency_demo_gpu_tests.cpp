// Plan 0042 Milestone 3 (Spec 0042 R7, ADR-0090 Decisions 2/3, ruling Q5):
// the blend path end to end. transparency_demo is two overlapping
// alpha_mode: blend spheres -- green (alpha 0.45, nearer the camera) and
// blue (alpha 0.7, farther) -- over the opaque pbr_dielectric_rough floor.
// transparency_demo_swapped declares the same two nodes in the opposite
// order; everything else is identical.

#include "fixture/transparency_demo_fixture.h"
#include "support/golden_validity.h"
#include "support/pixel_diff.h"

#include <atlantis/asset_system/material_types.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using atlantis::image_regression::kTransparencyDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderTransparencyDemoFrame;
using atlantis::image_regression::setUpTransparencyDemoFixture;
using atlantis::image_regression::TransparencyDemoFixture;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig(bool swapped = false) {
  BootstrapConfig config;
  config.sceneArtifactPath = swapped ? ATLANTIS_transparency_demo_swapped_scene_ARTIFACT_PATH
                                     : ATLANTIS_transparency_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = swapped ? ATLANTIS_transparency_demo_swapped_scene_METADATA_PATH
                                     : ATLANTIS_transparency_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = swapped ? ATLANTIS_transparency_demo_swapped_scene_MANIFEST_PATH
                                               : ATLANTIS_transparency_demo_scene_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  // Plan 0027 Milestone 9 (ADR-0072 D-1): the shadow-casting shader pair
  // -- unconditionally required (BootstrapConfig, Milestone 8).
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_TRANSPARENCY_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) +
      "/output_transform_unorm.frag.refl.json";
  return config;
}

// The base-colour alpha of every blended material, in the order the
// fixture builds its draw list (World::renderableEntities() -- the caller
// order drawFrame() receives).
[[nodiscard]] std::vector<float> blendedAlphasInCallerOrder(const TransparencyDemoFixture& fixture) {
  std::vector<float> alphas;
  for (const auto& id : fixture.world->renderableEntities()) {
    const auto renderable = fixture.world->getRenderable(id);
    if (renderable.isErr() || !renderable.value().materialAsset.has_value()) continue;
    const auto& data = fixture.materialDataMap.at(*renderable.value().materialAsset);
    if (data.alphaMode == atlantis::asset_system::MaterialAlphaMode::Blend) alphas.push_back(data.baseColorFactor[3]);
  }
  return alphas;
}

[[nodiscard]] PixelBuffer renderScene(bool swapped) {
  auto fixtureResult = setUpTransparencyDemoFixture(buildTestConfig(swapped));
  REQUIRE(fixtureResult.isOk());
  auto frameResult = renderTransparencyDemoFrame(fixtureResult.value());
  REQUIRE(frameResult.isOk());
  REQUIRE(fixtureResult.value().device->waitIdle().isOk());
  return frameResult.value();
}

[[nodiscard]] std::size_t countDifferingPixels(const PixelBuffer& a, const PixelBuffer& b) {
  std::size_t count = 0;
  for (std::size_t offset = 0; offset < a.rgba8.size(); offset += 4) {
    for (std::size_t channel = 0; channel < 4; ++channel) {
      if (a.rgba8[offset + channel] != b.rgba8[offset + channel]) {
        ++count;
        break;
      }
    }
  }
  return count;
}

}  // namespace

TEST_CASE("transparency_demo and its swapped twin hand drawFrame() the two blended spheres in opposite caller order",
          "[image_regression][gpu][transparency]") {
  // The precondition that makes the swap-order test meaningful: the
  // scenes really do differ in caller order.
  auto original = setUpTransparencyDemoFixture(buildTestConfig(false));
  REQUIRE(original.isOk());
  auto swapped = setUpTransparencyDemoFixture(buildTestConfig(true));
  REQUIRE(swapped.isOk());
  CHECK(blendedAlphasInCallerOrder(original.value()) == std::vector<float>{0.45f, 0.7f});
  CHECK(blendedAlphasInCallerOrder(swapped.value()) == std::vector<float>{0.7f, 0.45f});
}

TEST_CASE("transparency_demo: the swapped-declaration scene renders byte-identically (the sort decides the order)",
          "[image_regression][gpu][transparency]") {
  // Spec 0036's "real back-to-front ordering test": "over" is not
  // commutative where the two spheres overlap, so identical bytes mean
  // drawFrame() issued them in the same order for both caller orders.
  const PixelBuffer original = renderScene(false);
  const PixelBuffer swapped = renderScene(true);
  REQUIRE(original.width == kTransparencyDemoExtentPixels);
  REQUIRE(swapped.width == original.width);
  REQUIRE(swapped.height == original.height);
  // Measured with the sort disabled (a local probe, not committed): 7849
  // pixels differ -- the overlap region.
  CHECK(countDifferingPixels(original, swapped) == 0);
}

TEST_CASE("transparency_demo: the blended spheres really blend -- forcing them Opaque changes the frame",
          "[image_regression][gpu][transparency]") {
  const PixelBuffer blended = renderScene(false);

  auto fixtureResult = setUpTransparencyDemoFixture(buildTestConfig(false));
  REQUIRE(fixtureResult.isOk());
  TransparencyDemoFixture& fixture = fixtureResult.value();
  int changed = 0;
  for (auto& [id, data] : fixture.materialDataMap) {
    if (data.alphaMode == atlantis::asset_system::MaterialAlphaMode::Blend) {
      data.alphaMode = atlantis::asset_system::MaterialAlphaMode::Opaque;
      ++changed;
    }
  }
  REQUIRE(changed == 2);
  auto frameResult = renderTransparencyDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  // Both spheres' full screen coverage changes (see-through vs solid): far
  // more than a stray edge.
  CHECK(countDifferingPixels(blended, frameResult.value()) > 10000);
  REQUIRE(fixture.device->waitIdle().isOk());
}

// ---------------------------------------------------------------------------
// Plan 0042 Milestone 3, golden commit (ADR-0042 Initial baseline
// bootstrap): the golden was captured by
// atlantis_image_regression_transparency_demo_golden_generator against the
// clean, already-committed tree at its recorded source_revision; these
// TEST_CASEs land with it.
// ---------------------------------------------------------------------------

namespace {

constexpr const char* kTransparencyDemoGoldenName = "transparency_demo/transparency_demo_512x512_rgba8unorm";
constexpr const char* kTransparencyDemoGoldenSlug = "transparency_demo_512x512_rgba8unorm";

[[nodiscard]] atlantis::image_regression::ComparisonReport compareAgainstGolden(const PixelBuffer& actual,
                                                                                const char* failureSlug) {
  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult = atlantis::image_regression::loadAndValidateGolden(
      goldensDir / (std::string(kTransparencyDemoGoldenName) + ".png"),
      goldensDir / (std::string(kTransparencyDemoGoldenName) + ".sidecar.txt"));
  {
    INFO("INVALID GOLDEN: the committed transparency_demo golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& golden = goldenResult.value().pixels;
  REQUIRE(actual.width == golden.width);
  REQUIRE(actual.height == golden.height);
  const auto report = atlantis::image_regression::compareBuffers(actual, golden);
  if (!report.passed && failureSlug != nullptr) {
    (void)atlantis::image_regression::writeFailureArtifacts(ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, failureSlug, actual,
                                                            golden);
  }
  return report;
}

}  // namespace

TEST_CASE("Full capture-compare cycle against the committed transparency_demo golden passes",
          "[image_regression][gpu][transparency]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  std::filesystem::remove(outputDir / (std::string(kTransparencyDemoGoldenSlug) + "_actual.png"));
  std::filesystem::remove(outputDir / (std::string(kTransparencyDemoGoldenSlug) + "_diff.png"));
  const auto report = compareAgainstGolden(renderScene(false), kTransparencyDemoGoldenSlug);
  REQUIRE(report.passed);
}

TEST_CASE("The swapped-declaration transparency scene also passes against the transparency_demo golden (Q5)",
          "[image_regression][gpu][transparency]") {
  const auto report = compareAgainstGolden(renderScene(true), "transparency_demo_swapped_512x512_rgba8unorm");
  REQUIRE(report.passed);
}

TEST_CASE("The transparency_demo frame with both spheres forced Opaque fails comparison against the real "
          "transparency_demo golden",
          "[image_regression][gpu][transparency]") {
  // The discriminator for the golden itself: without blending, both
  // spheres are solid and hide the floor behind them, so the image the
  // golden records cannot be produced unless the spheres are composited.
  auto fixtureResult = setUpTransparencyDemoFixture(buildTestConfig(false));
  REQUIRE(fixtureResult.isOk());
  TransparencyDemoFixture& fixture = fixtureResult.value();
  for (auto& [id, data] : fixture.materialDataMap) {
    if (data.alphaMode == atlantis::asset_system::MaterialAlphaMode::Blend) {
      data.alphaMode = atlantis::asset_system::MaterialAlphaMode::Opaque;
    }
  }
  auto frameResult = renderTransparencyDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const auto report = compareAgainstGolden(frameResult.value(), nullptr);
  INFO("maxChannelDiff " << report.maxChannelDiff << ", out-of-tolerance pixels " << report.outOfToleranceCount);
  CHECK_FALSE(report.passed);
  CHECK(report.outOfToleranceCount > 10000);
  REQUIRE(fixture.device->waitIdle().isOk());
}
