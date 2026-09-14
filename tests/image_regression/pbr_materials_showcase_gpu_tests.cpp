#include "fixture/pbr_materials_showcase_fixture.h"
#include "support/golden_validity.h"

#include <atlantis/renderer/material.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

// Plan 0035 Milestone 5 (Spec 0035 Requirement 6/ADR-0042): the
// showcase-scene golden test -- the full ~28-sphere fan/arc scene, all
// four MaterialKinds (PbrDirectLit majority + one each PbrClearcoat/
// PbrSheen/PbrAnisotropic), IBL-only under the warehouse_interior HDRI,
// mirroring pbr_anisotropic_demo_gpu_tests.cpp's own exact structure.

namespace {

atlantis::runtime::BootstrapConfig buildShowcaseConfig() {
  atlantis::runtime::BootstrapConfig config;
  const std::string unlit = ATLANTIS_PBR_MATERIALS_SHOWCASE_UNLIT_TEXTURED_SHADER_DIR;
  config.unlitTexturedVertexShaderSpirvPath = unlit + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath = unlit + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath = unlit + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath = unlit + "/textured_quad.frag.refl.json";
  const std::string lit = ATLANTIS_PBR_MATERIALS_SHOWCASE_LIT_TEXTURED_SHADER_DIR;
  config.litTexturedVertexShaderSpirvPath = lit + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath = lit + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath = lit + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath = lit + "/lit_textured.frag.refl.json";
  const std::string direct = ATLANTIS_PBR_MATERIALS_SHOWCASE_PBR_DIRECT_LIT_SHADER_DIR;
  config.pbrDirectLitVertexShaderSpirvPath = direct + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath = direct + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath = direct + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath = direct + "/pbr_direct_lit.frag.refl.json";
  const std::string ibl = ATLANTIS_PBR_MATERIALS_SHOWCASE_PBR_IBL_SHADER_DIR;
  config.pbrIblVertexShaderSpirvPath = ibl + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath = ibl + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = ibl + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath = ibl + "/pbr_ibl.frag.refl.json";
  // validateEnvironmentBootstrapConfig() requires this pair non-empty
  // whenever an environment is configured, even though this fixture's
  // own real trio-passing call never reads it (dead-path filler reuses
  // pbrIbl* instead) -- every sibling per-BRDF fixture populates this
  // same field for the identical reason.
  const std::string iblNormalMap = ATLANTIS_PBR_MATERIALS_SHOWCASE_PBR_IBL_NORMAL_MAP_SHADER_DIR;
  config.pbrIblNormalMapVertexShaderSpirvPath = iblNormalMap + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath = iblNormalMap + "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath = iblNormalMap + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath = iblNormalMap + "/pbr_ibl_normal_map.frag.refl.json";
  const std::string sky = ATLANTIS_PBR_MATERIALS_SHOWCASE_SKY_SHADER_DIR;
  config.skyVertexShaderSpirvPath = sky + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = sky + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = sky + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = sky + "/sky.frag.refl.json";
  const std::string shadowCast = ATLANTIS_PBR_MATERIALS_SHOWCASE_SHADOW_CAST_SHADER_DIR;
  config.shadowCastVertexShaderSpirvPath = shadowCast + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath = shadowCast + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath = shadowCast + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath = shadowCast + "/shadow_cast.frag.refl.json";
  const std::string output = ATLANTIS_PBR_MATERIALS_SHOWCASE_OUTPUT_TRANSFORM_UNORM_SHADER_DIR;
  config.outputTransformUnormVertexShaderSpirvPath = output + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = output + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = output + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = output + "/output_transform_unorm.frag.refl.json";
  const std::string clearcoat = ATLANTIS_PBR_MATERIALS_SHOWCASE_PBR_CLEARCOAT_IBL_SHADER_DIR;
  config.pbrClearcoatIblVertexShaderSpirvPath = clearcoat + "/pbr_clearcoat_ibl.vert.spv";
  config.pbrClearcoatIblVertexShaderReflectionPath = clearcoat + "/pbr_clearcoat_ibl.vert.refl.json";
  config.pbrClearcoatIblFragmentShaderSpirvPath = clearcoat + "/pbr_clearcoat_ibl.frag.spv";
  config.pbrClearcoatIblFragmentShaderReflectionPath = clearcoat + "/pbr_clearcoat_ibl.frag.refl.json";
  const std::string sheen = ATLANTIS_PBR_MATERIALS_SHOWCASE_PBR_SHEEN_IBL_SHADER_DIR;
  config.pbrSheenIblVertexShaderSpirvPath = sheen + "/pbr_sheen_ibl.vert.spv";
  config.pbrSheenIblVertexShaderReflectionPath = sheen + "/pbr_sheen_ibl.vert.refl.json";
  config.pbrSheenIblFragmentShaderSpirvPath = sheen + "/pbr_sheen_ibl.frag.spv";
  config.pbrSheenIblFragmentShaderReflectionPath = sheen + "/pbr_sheen_ibl.frag.refl.json";
  const std::string anisotropic = ATLANTIS_PBR_MATERIALS_SHOWCASE_PBR_ANISOTROPIC_IBL_SHADER_DIR;
  config.pbrAnisotropicIblVertexShaderSpirvPath = anisotropic + "/pbr_anisotropic_ibl.vert.spv";
  config.pbrAnisotropicIblVertexShaderReflectionPath = anisotropic + "/pbr_anisotropic_ibl.vert.refl.json";
  config.pbrAnisotropicIblFragmentShaderSpirvPath = anisotropic + "/pbr_anisotropic_ibl.frag.spv";
  config.pbrAnisotropicIblFragmentShaderReflectionPath = anisotropic + "/pbr_anisotropic_ibl.frag.refl.json";
  config.environmentArtifactPath = ATLANTIS_PBR_MATERIALS_SHOWCASE_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_PBR_MATERIALS_SHOWCASE_ENVIRONMENT_METADATA_PATH;
  config.sceneArtifactPath = ATLANTIS_pbr_materials_showcase_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_materials_showcase_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_materials_showcase_scene_MANIFEST_PATH;
  return config;
}

}  // namespace

TEST_CASE("PBR materials showcase renders a ~28-sphere fan/arc under warehouse_interior IBL",
          "[image_regression][gpu][materials_showcase]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrMaterialsShowcaseFixture(buildShowcaseConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  CHECK(fixture.world->lightEntities().empty());

  auto first = atlantis::image_regression::renderPbrMaterialsShowcaseFrame(fixture);
  REQUIRE(first.isOk());
  const auto& frame = first.value();
  REQUIRE(frame.width == atlantis::image_regression::kPbrMaterialsShowcaseExtentPixels);
  // 28 distinct spheres + 1 shared pedestal material + 1 ground
  // material -- Spec 0035 Requirement 6's own material-variety bar
  // (real, distinct MaterialAssetData per sphere; pedestals dedup to
  // one shared material, this Milestone's own real, measured
  // descriptor-set-budget choice, PR-disclosed).
  REQUIRE(fixture.materialResourceMap.size() == 30);
  for (const auto& [id, material] : fixture.materialResourceMap) {
    CHECK(material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  }

  auto second = atlantis::image_regression::renderPbrMaterialsShowcaseFrame(fixture);
  REQUIRE(second.isOk());
  CHECK(fixture.environmentUploadCount == 1);
}

TEST_CASE("Full capture-compare cycle against the committed PBR materials showcase golden passes",
          "[image_regression][gpu][materials_showcase][golden]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrMaterialsShowcaseFixture(buildShowcaseConfig());
  REQUIRE(fixtureResult.isOk());
  auto rendered = atlantis::image_regression::renderPbrMaterialsShowcaseFrame(fixtureResult.value());
  REQUIRE(rendered.isOk());

  const std::filesystem::path goldens = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  const std::string stem = "pbr_materials_showcase/pbr_materials_showcase_768x768_rgba8unorm";
  auto golden = atlantis::image_regression::loadAndValidateGolden(goldens / (stem + ".png"),
                                                                  goldens / (stem + ".sidecar.txt"));
  REQUIRE(golden.isOk());
  const auto report = atlantis::image_regression::compareBuffers(rendered.value(), golden.value().pixels);
  if (!report.passed) {
    static_cast<void>(atlantis::image_regression::writeFailureArtifacts(
        ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, "pbr_materials_showcase_768x768_rgba8unorm", rendered.value(),
        golden.value().pixels));
  }
  REQUIRE(report.passed);
}
