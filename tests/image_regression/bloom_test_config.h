#pragma once

// Plan 0044 Milestone 2 follow-up: the BootstrapConfig every bloom GPU test
// builds for PbrNormalMapDemoFixture -- the dark emissive fixture's shader
// pairs (no environment) over a caller-chosen scene, plus the twelve bloom
// shader paths. Shared by bloom_demo_gpu_tests.cpp and bloom_on_tests.cpp;
// reads the atlantis_image_regression_gpu_tests compile definitions, so it
// is included only from that executable's sources.

#include <atlantis/runtime/bootstrap_config.h>

#include <string>

namespace atlantis::image_regression {

struct BloomTestSceneFiles {
  const char* artifact;
  const char* metadata;
  const char* manifest;
};

[[nodiscard]] inline atlantis::runtime::BootstrapConfig buildDarkEmissiveConfig(const BloomTestSceneFiles& scene) {
  atlantis::runtime::BootstrapConfig config;
  config.sceneArtifactPath = scene.artifact;
  config.sceneMetadataPath = scene.metadata;
  config.sceneDependencyManifestPath = scene.manifest;
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

// config plus all twelve bloom shader paths.
inline void addBloomShaderPaths(atlantis::runtime::BootstrapConfig& config) {
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
}

}  // namespace atlantis::image_regression
