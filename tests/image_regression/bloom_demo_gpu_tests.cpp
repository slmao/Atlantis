// Plan 0044 Milestone 1 (Spec 0044, ADR-0092 Decision 3, P10): the bloom
// infrastructure on the dark emissive fixture (PbrNormalMapDemoFixture,
// ruling O4) -- the three bloom Pipelines created under Validation Layers,
// the twelve targets at the Plan's extents, and no rendering change: in
// Milestone 1 no bloom pass is recorded. Milestone 2 adds the bloom scenes,
// passes, property tests and goldens to this file.

#include "fixture/emissive_demo_fixture.h"
#include "support/pixel_diff.h"

#include <atlantis/renderer/bloom.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

using atlantis::image_regression::EmissiveDemoFixture;
using atlantis::image_regression::kEmissiveDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderEmissiveDemoFrame;
using atlantis::image_regression::setUpEmissiveDemoFixture;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildEmissiveConfig() {
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

// The same config plus all twelve bloom shader paths.
[[nodiscard]] BootstrapConfig buildBloomConfig() {
  BootstrapConfig config = buildEmissiveConfig();
  const std::string down = ATLANTIS_BLOOM_DOWNSAMPLE_SHADER_DIR;
  const std::string up = ATLANTIS_BLOOM_UPSAMPLE_SHADER_DIR;
  const std::string composite = ATLANTIS_BLOOM_COMPOSITE_SHADER_DIR;
  config.bloomDownsampleVertexShaderSpirvPath = down + "/bloom_downsample.vert.spv";
  config.bloomDownsampleVertexShaderReflectionPath = down + "/bloom_downsample.vert.refl.json";
  config.bloomDownsampleFragmentShaderSpirvPath = down + "/bloom_downsample.frag.spv";
  config.bloomDownsampleFragmentShaderReflectionPath = down + "/bloom_downsample.frag.refl.json";
  config.bloomUpsampleVertexShaderSpirvPath = up + "/bloom_upsample.vert.spv";
  config.bloomUpsampleVertexShaderReflectionPath = up + "/bloom_upsample.vert.refl.json";
  config.bloomUpsampleFragmentShaderSpirvPath = up + "/bloom_upsample.frag.spv";
  config.bloomUpsampleFragmentShaderReflectionPath = up + "/bloom_upsample.frag.refl.json";
  config.bloomCompositeVertexShaderSpirvPath = composite + "/bloom_composite.vert.spv";
  config.bloomCompositeVertexShaderReflectionPath = composite + "/bloom_composite.vert.refl.json";
  config.bloomCompositeFragmentShaderSpirvPath = composite + "/bloom_composite.frag.spv";
  config.bloomCompositeFragmentShaderReflectionPath = composite + "/bloom_composite.frag.refl.json";
  REQUIRE(atlantis::runtime::hasBloomShaderPaths(config));
  return config;
}

[[nodiscard]] EmissiveDemoFixture setUp(const BootstrapConfig& config) {
  auto fixtureResult = setUpEmissiveDemoFixture(config, ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH,
                                                ATLANTIS_pbr_normal_mapped_control_METADATA_PATH);
  REQUIRE(fixtureResult.isOk());
  return std::move(fixtureResult.value());
}

}  // namespace

TEST_CASE("Bloom infrastructure: the configured fixture creates the three bloom Pipelines and the twelve targets "
          "at the Plan 0044 extents",
          "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture fixture = setUp(buildBloomConfig());
  CHECK(fixture.bloomDownsamplePipeline != nullptr);
  CHECK(fixture.bloomUpsamplePipeline != nullptr);
  CHECK(fixture.bloomCompositePipeline != nullptr);
  REQUIRE(fixture.bloomTargets.has_value());

  const atlantis::renderer::BloomTargets& targets = *fixture.bloomTargets;
  const atlantis::rhi::Extent2D hdr{kEmissiveDemoExtentPixels, kEmissiveDemoExtentPixels};
  CHECK(targets.extent() == hdr);
  const auto levels = atlantis::renderer::bloomLevelExtents(hdr);
  for (std::size_t level = 0; level < atlantis::renderer::kBloomLevelCount; ++level) {
    INFO("level " << (level + 1));
    CHECK(targets.downsampleTarget(level).extent() == levels[level]);
    if (level + 1 < atlantis::renderer::kBloomLevelCount) {
      CHECK(targets.upsampleTarget(level).extent() == levels[level]);
    }
  }
  CHECK(targets.compositeTarget().extent() == hdr);
  CHECK(targets.downsampleTarget(5).extent() == atlantis::rhi::Extent2D{8, 8});
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Bloom infrastructure: without bloom shader paths the fixture creates no bloom resources",
          "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture fixture = setUp(buildEmissiveConfig());
  CHECK(fixture.bloomDownsamplePipeline == nullptr);
  CHECK(fixture.bloomUpsamplePipeline == nullptr);
  CHECK(fixture.bloomCompositePipeline == nullptr);
  CHECK_FALSE(fixture.bloomTargets.has_value());
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Bloom infrastructure: configuring bloom changes nothing rendered in Milestone 1 (byte-identical frame)",
          "[image_regression][gpu][bloom]") {
  EmissiveDemoFixture plain = setUp(buildEmissiveConfig());
  auto plainFrame = renderEmissiveDemoFrame(plain);
  REQUIRE(plainFrame.isOk());
  REQUIRE(plain.device->waitIdle().isOk());

  EmissiveDemoFixture configured = setUp(buildBloomConfig());
  auto configuredFrame = renderEmissiveDemoFrame(configured);
  REQUIRE(configuredFrame.isOk());
  REQUIRE(configured.device->waitIdle().isOk());

  REQUIRE(plainFrame.value().width == configuredFrame.value().width);
  CHECK(plainFrame.value().rgba8 == configuredFrame.value().rgba8);
}
