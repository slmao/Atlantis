#include "fixture/ibl_material_demo_fixture.h"

#include <atlantis/renderer/material.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

atlantis::runtime::BootstrapConfig buildIblConfig() {
  atlantis::runtime::BootstrapConfig config;
  config.sceneArtifactPath = ATLANTIS_ibl_material_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_ibl_material_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_ibl_material_demo_scene_MANIFEST_PATH;
  const std::string unlit = ATLANTIS_IBL_DEMO_UNLIT_TEXTURED_SHADER_DIR;
  config.unlitTexturedVertexShaderSpirvPath = unlit + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath = unlit + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath = unlit + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath = unlit + "/textured_quad.frag.refl.json";
  const std::string lit = ATLANTIS_IBL_DEMO_LIT_TEXTURED_SHADER_DIR;
  config.litTexturedVertexShaderSpirvPath = lit + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath = lit + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath = lit + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath = lit + "/lit_textured.frag.refl.json";
  const std::string direct = ATLANTIS_IBL_DEMO_PBR_DIRECT_LIT_SHADER_DIR;
  config.pbrDirectLitVertexShaderSpirvPath = direct + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath = direct + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath = direct + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath = direct + "/pbr_direct_lit.frag.refl.json";
  const std::string ibl = ATLANTIS_IBL_DEMO_PBR_IBL_SHADER_DIR;
  config.pbrIblVertexShaderSpirvPath = ibl + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath = ibl + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = ibl + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath = ibl + "/pbr_ibl.frag.refl.json";
  const std::string output = ATLANTIS_IBL_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR;
  config.outputTransformUnormVertexShaderSpirvPath = output + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = output + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = output + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = output + "/output_transform_unorm.frag.refl.json";
  config.environmentArtifactPath = ATLANTIS_IBL_DEMO_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_IBL_DEMO_ENVIRONMENT_METADATA_PATH;
  return config;
}

std::array<std::uint8_t, 3> rgbAt(const atlantis::image_regression::PixelBuffer& frame, std::uint32_t x,
                                  std::uint32_t y) {
  const std::size_t offset = (static_cast<std::size_t>(y) * frame.width + x) * 4;
  return {frame.rgba8[offset], frame.rgba8[offset + 1], frame.rgba8[offset + 2]};
}

int colorDistance(const std::array<std::uint8_t, 3>& a, const std::array<std::uint8_t, 3>& b) {
  return std::abs(static_cast<int>(a[0]) - b[0]) + std::abs(static_cast<int>(a[1]) - b[1]) +
         std::abs(static_cast<int>(a[2]) - b[2]);
}

}  // namespace

TEST_CASE("IBL material demo renders four environment-lit PBR spheres without direct lights",
          "[image_regression][gpu][ibl]") {
  auto fixtureResult = atlantis::image_regression::setUpIblMaterialDemoFixture(buildIblConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  CHECK(fixture.world->lightEntities().empty());

  auto first = atlantis::image_regression::renderIblMaterialDemoFrame(fixture);
  REQUIRE(first.isOk());
  const auto& frame = first.value();
  REQUIRE(frame.width == atlantis::image_regression::kIblMaterialDemoExtentPixels);
  REQUIRE(fixture.materialResourceMap.size() == 4);
  CHECK(fixture.sampledTextureResourceMap.size() == 1);
  CHECK(fixture.environmentUploadCount == 1);
  for (const auto& [id, material] : fixture.materialResourceMap) {
    CHECK(material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  }

  const auto dielectricRough = rgbAt(frame, 184, 184);
  const auto dielectricSmooth = rgbAt(frame, 328, 184);
  const auto metallicRough = rgbAt(frame, 184, 328);
  const auto metallicSmooth = rgbAt(frame, 328, 328);
  CHECK(static_cast<int>(dielectricRough[0]) + dielectricRough[1] + dielectricRough[2] > 30);
  CHECK(static_cast<int>(dielectricSmooth[0]) + dielectricSmooth[1] + dielectricSmooth[2] > 30);
  CHECK(std::max({metallicRough[0], metallicRough[1], metallicRough[2]}) -
            std::min({metallicRough[0], metallicRough[1], metallicRough[2]}) >
        3);
  CHECK(colorDistance(dielectricRough, dielectricSmooth) > 3);
  CHECK(colorDistance(metallicRough, metallicSmooth) > 3);

  auto second = atlantis::image_regression::renderIblMaterialDemoFrame(fixture);
  REQUIRE(second.isOk());
  CHECK(second.value().rgba8 == frame.rgba8);
  CHECK(fixture.environmentUploadCount == 1);
}
