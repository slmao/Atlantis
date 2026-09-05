#include "fixture/pbr_material_demo_fixture.h"
#include "support/golden_validity.h"

#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

// Plan 0023 Milestone 8 (Spec 0023 D16/D17): the pbr_material_demo
// fixture's own GPU-required coverage. The three cases below are self-
// contained (no committed golden required): structural non-degeneracy,
// the D10 shared-texture dedup proof (all four spheres reference
// DIFFERENT materials but the SAME textured_quad_srgb texture -- this
// scene's own defining reuse, Milestone 6), and repeated-call
// idempotency -- landed WITHOUT a golden yet, per ADR-0042's own two-
// phase capture process, matching material_demo_gpu_tests.cpp's own
// identical precedent. The golden PNG/sidecar and its own capture-
// compare test case land in Milestone 9's own separate, later commit.

using atlantis::image_regression::compareBuffers;
using atlantis::image_regression::kPbrMaterialDemoExtentPixels;
using atlantis::image_regression::loadAndValidateGolden;
using atlantis::image_regression::PbrMaterialDemoFixture;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderPbrMaterialDemoFrame;
using atlantis::image_regression::setUpPbrMaterialDemoFixture;
using atlantis::image_regression::writeFailureArtifacts;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildTestConfig() {
  BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_material_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_material_demo_scene_MANIFEST_PATH;
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

[[nodiscard]] std::array<std::uint8_t, 4> pixelAt(const PixelBuffer& buffer, std::uint32_t x, std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * buffer.width + x) * 4;
  return {buffer.rgba8[offset], buffer.rgba8[offset + 1], buffer.rgba8[offset + 2], buffer.rgba8[offset + 3]};
}

[[nodiscard]] bool nearBackgroundClearColor(const std::array<std::uint8_t, 4>& pixel) {
  // Renderer's own fixed kBackgroundClearColor (0.05, 0.05, 0.08, 1.0) ->
  // approximately (13, 13, 20, 255) in Rgba8Unorm bytes, matching
  // material_demo_gpu_tests.cpp's own identical constant.
  const int tolerance = 6;
  return std::abs(static_cast<int>(pixel[0]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[1]) - 13) <= tolerance &&
         std::abs(static_cast<int>(pixel[2]) - 20) <= tolerance;
}

}  // namespace

TEST_CASE("PBR material demo fixture renders a non-degenerate frame with real four-sphere coverage",
          "[image_regression][gpu][pbr]") {
  auto fixtureResult = setUpPbrMaterialDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  PbrMaterialDemoFixture& fixture = fixtureResult.value();

  auto frameResult = renderPbrMaterialDemoFrame(fixture);
  REQUIRE(frameResult.isOk());
  const PixelBuffer& frame = frameResult.value();
  REQUIRE(frame.width == kPbrMaterialDemoExtentPixels);
  REQUIRE(frame.height == kPbrMaterialDemoExtentPixels);

  // Not black/garbage: every alpha byte in the captured color image is
  // 255 (Rgba8Unorm color attachment, no blending) -- sampled on a grid
  // across the whole frame, not just the sphere regions.
  for (std::uint32_t y = 0; y < frame.height; y += 16) {
    for (std::uint32_t x = 0; x < frame.width; x += 16) {
      CHECK(pixelAt(frame, x, y)[3] == 255);
    }
  }

  // Four spheres were genuinely drawn: a meaningful fraction of sampled
  // pixels differ from the untouched background clear color -- a
  // coarse, projection-independent proof, matching material_demo's/
  // lighting_demo's own identical established convention.
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
  CHECK(coverageFraction > 0.05);
}

TEST_CASE("PBR material demo fixture realizes four distinct PbrDirectLit materials but dedups their shared "
          "textured_quad_srgb texture to exactly one real upload",
          "[image_regression][gpu][pbr]") {
  // Spec 0023 D16/Milestone 6's own defining reuse: four distinct
  // material AssetIds (pbr_dielectric_rough/smooth, pbr_metallic_rough/
  // smooth) all name the SAME texture (textured_quad_srgb).
  // computePendingMaterialIds()/realizePendingMaterials() (called for
  // real by renderPbrMaterialDemoFrame(), never duplicated here) must
  // therefore realize four Materials but only one real SampledTexture/
  // Sampler upload -- Runtime's own existing D10 dedup (AssetId-keyed
  // textureResourceMap_), unmodified by this Plan, automatically
  // applying to PbrDirectLit exactly as it already does for every other
  // MaterialKind.
  auto fixtureResult = setUpPbrMaterialDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  PbrMaterialDemoFixture& fixture = fixtureResult.value();

  auto frameResult = renderPbrMaterialDemoFrame(fixture);
  REQUIRE(frameResult.isOk());

  CHECK(fixture.materialResourceMap.size() == 4);
  CHECK(fixture.sampledTextureResourceMap.size() == 1);
}

TEST_CASE("PBR material demo fixture: repeated render cycles against the same fixture succeed independently",
          "[image_regression][gpu][pbr]") {
  auto fixtureResult = setUpPbrMaterialDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  PbrMaterialDemoFixture& fixture = fixtureResult.value();

  auto firstResult = renderPbrMaterialDemoFrame(fixture);
  REQUIRE(firstResult.isOk());
  // The first call already realized every material -- a second call
  // must find computePendingMaterialIds() empty and perform no new
  // upload, yet still produce byte-identical output (this scene's own
  // camera/lights are static -- no World mutation between calls).
  auto secondResult = renderPbrMaterialDemoFrame(fixture);
  REQUIRE(secondResult.isOk());

  CHECK(firstResult.value().rgba8 == secondResult.value().rgba8);
  CHECK(fixture.materialResourceMap.size() == 4);
}

namespace {
constexpr const char* kPbrMaterialDemoGoldenName = "pbr_material_demo/pbr_material_demo_512x512_rgba8unorm";
constexpr const char* kPbrMaterialDemoGoldenSlug = "pbr_material_demo_512x512_rgba8unorm";
}  // namespace

// Plan 0023 Milestone 9: lands together with the golden PNG/sidecar
// themselves, in their own separate commit, per ADR-0042's own two-
// phase capture process -- mirrors
// "Full capture-compare cycle against the committed material_demo
// golden passes"'s own identical structure exactly.
TEST_CASE("Full capture-compare cycle against the committed pbr_material_demo golden passes",
          "[image_regression][gpu][pbr]") {
  const std::filesystem::path outputDir = ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR;
  const std::filesystem::path actualArtifact = outputDir / (std::string(kPbrMaterialDemoGoldenSlug) + "_actual.png");
  const std::filesystem::path diffArtifact = outputDir / (std::string(kPbrMaterialDemoGoldenSlug) + "_diff.png");
  std::filesystem::remove(actualArtifact);
  std::filesystem::remove(diffArtifact);

  auto fixtureResult = setUpPbrMaterialDemoFixture(buildTestConfig());
  REQUIRE(fixtureResult.isOk());
  PbrMaterialDemoFixture& fixture = fixtureResult.value();

  auto renderResult = renderPbrMaterialDemoFrame(fixture);
  REQUIRE(renderResult.isOk());
  const PixelBuffer& actual = renderResult.value();

  const std::filesystem::path goldensDir = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  auto goldenResult = loadAndValidateGolden(goldensDir / (std::string(kPbrMaterialDemoGoldenName) + ".png"),
                                             goldensDir / (std::string(kPbrMaterialDemoGoldenName) + ".sidecar.txt"));
  {
    INFO("INVALID GOLDEN: the committed pbr_material_demo golden must load and validate cleanly");
    REQUIRE(goldenResult.isOk());
  }
  const auto& validatedGolden = goldenResult.value();

  REQUIRE(actual.width == validatedGolden.pixels.width);
  REQUIRE(actual.height == validatedGolden.pixels.height);

  const auto report = compareBuffers(actual, validatedGolden.pixels);
  if (!report.passed) {
    (void)writeFailureArtifacts(outputDir, kPbrMaterialDemoGoldenSlug, actual, validatedGolden.pixels);
  }
  REQUIRE(report.passed);

  REQUIRE(fixture.device->waitIdle().isOk());
}
