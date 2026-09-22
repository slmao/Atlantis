#include "fixture/pbr_anisotropic_demo_fixture.h"
#include "support/emissive_differential.h"
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

// Plan 0035 Milestone 4 (ADR-0081/ADR-0042): the two anisotropic golden
// tests -- an IBL-only, no-normal-map 3-sphere rotation sweep
// (pbr_anisotropic_demo) and a normal-mapped 1-sphere variant
// (pbr_anisotropic_normal_map_demo), mirroring
// pbr_sheen_demo_gpu_tests.cpp's own exact structure.

namespace {

atlantis::runtime::BootstrapConfig buildAnisotropicSharedConfig() {
  atlantis::runtime::BootstrapConfig config;
  const std::string unlit = ATLANTIS_PBR_ANISOTROPIC_DEMO_UNLIT_TEXTURED_SHADER_DIR;
  config.unlitTexturedVertexShaderSpirvPath = unlit + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath = unlit + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath = unlit + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath = unlit + "/textured_quad.frag.refl.json";
  const std::string lit = ATLANTIS_PBR_ANISOTROPIC_DEMO_LIT_TEXTURED_SHADER_DIR;
  config.litTexturedVertexShaderSpirvPath = lit + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath = lit + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath = lit + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath = lit + "/lit_textured.frag.refl.json";
  const std::string direct = ATLANTIS_PBR_ANISOTROPIC_DEMO_PBR_DIRECT_LIT_SHADER_DIR;
  config.pbrDirectLitVertexShaderSpirvPath = direct + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath = direct + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath = direct + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath = direct + "/pbr_direct_lit.frag.refl.json";
  const std::string ibl = ATLANTIS_PBR_ANISOTROPIC_DEMO_PBR_IBL_SHADER_DIR;
  config.pbrIblVertexShaderSpirvPath = ibl + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath = ibl + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = ibl + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath = ibl + "/pbr_ibl.frag.refl.json";
  const std::string iblNormalMap = ATLANTIS_PBR_ANISOTROPIC_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR;
  config.pbrIblNormalMapVertexShaderSpirvPath = iblNormalMap + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath = iblNormalMap + "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath = iblNormalMap + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath = iblNormalMap + "/pbr_ibl_normal_map.frag.refl.json";
  const std::string sky = ATLANTIS_PBR_ANISOTROPIC_DEMO_SKY_SHADER_DIR;
  config.skyVertexShaderSpirvPath = sky + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = sky + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = sky + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = sky + "/sky.frag.refl.json";
  const std::string shadowCast = ATLANTIS_PBR_ANISOTROPIC_DEMO_SHADOW_CAST_SHADER_DIR;
  config.shadowCastVertexShaderSpirvPath = shadowCast + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath = shadowCast + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath = shadowCast + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath = shadowCast + "/shadow_cast.frag.refl.json";
  const std::string output = ATLANTIS_PBR_ANISOTROPIC_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR;
  config.outputTransformUnormVertexShaderSpirvPath = output + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = output + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = output + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = output + "/output_transform_unorm.frag.refl.json";
  const std::string anisotropic = ATLANTIS_PBR_ANISOTROPIC_DEMO_PBR_ANISOTROPIC_IBL_SHADER_DIR;
  config.pbrAnisotropicIblVertexShaderSpirvPath = anisotropic + "/pbr_anisotropic_ibl.vert.spv";
  config.pbrAnisotropicIblVertexShaderReflectionPath = anisotropic + "/pbr_anisotropic_ibl.vert.refl.json";
  config.pbrAnisotropicIblFragmentShaderSpirvPath = anisotropic + "/pbr_anisotropic_ibl.frag.spv";
  config.pbrAnisotropicIblFragmentShaderReflectionPath = anisotropic + "/pbr_anisotropic_ibl.frag.refl.json";
  const std::string anisotropicNormalMap = ATLANTIS_PBR_ANISOTROPIC_DEMO_PBR_ANISOTROPIC_IBL_NORMAL_MAP_SHADER_DIR;
  config.pbrAnisotropicIblNormalMapVertexShaderSpirvPath =
      anisotropicNormalMap + "/pbr_anisotropic_ibl_normal_map.vert.spv";
  config.pbrAnisotropicIblNormalMapVertexShaderReflectionPath =
      anisotropicNormalMap + "/pbr_anisotropic_ibl_normal_map.vert.refl.json";
  config.pbrAnisotropicIblNormalMapFragmentShaderSpirvPath =
      anisotropicNormalMap + "/pbr_anisotropic_ibl_normal_map.frag.spv";
  config.pbrAnisotropicIblNormalMapFragmentShaderReflectionPath =
      anisotropicNormalMap + "/pbr_anisotropic_ibl_normal_map.frag.refl.json";
  config.environmentArtifactPath = ATLANTIS_PBR_ANISOTROPIC_DEMO_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_PBR_ANISOTROPIC_DEMO_ENVIRONMENT_METADATA_PATH;
  return config;
}

atlantis::runtime::BootstrapConfig buildAnisotropicConfig() {
  atlantis::runtime::BootstrapConfig config = buildAnisotropicSharedConfig();
  config.sceneArtifactPath = ATLANTIS_pbr_anisotropic_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_anisotropic_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_anisotropic_demo_scene_MANIFEST_PATH;
  return config;
}

atlantis::runtime::BootstrapConfig buildAnisotropicNormalMapConfig() {
  atlantis::runtime::BootstrapConfig config = buildAnisotropicSharedConfig();
  config.sceneArtifactPath = ATLANTIS_pbr_anisotropic_normal_map_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_anisotropic_normal_map_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_anisotropic_normal_map_demo_scene_MANIFEST_PATH;
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

// Counts mismatching bytes rather than comparing the two ~1MB vectors
// directly -- a direct REQUIRE(a == b) on a failing multi-megabyte
// vector triggers a known Catch2 v3.7.1 console-reporter crash
// (catch_textflow.cpp's own internal assertion) when it tries to render
// the full diff -- see pbr_sheen_demo_gpu_tests.cpp's own identical
// helper.
std::size_t countMismatchedBytes(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  std::size_t mismatches = 0;
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) ++mismatches;
  }
  return mismatches;
}

}  // namespace

TEST_CASE("PBR anisotropic demo renders a rotation sweep of three IBL-lit spheres",
          "[image_regression][gpu][anisotropic]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrAnisotropicDemoFixture(buildAnisotropicConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  CHECK(fixture.world->lightEntities().empty());

  auto first = atlantis::image_regression::renderPbrAnisotropicDemoFrame(fixture);
  REQUIRE(first.isOk());
  const auto& frame = first.value();
  REQUIRE(frame.width == atlantis::image_regression::kPbrAnisotropicDemoExtentPixels);
  REQUIRE(fixture.materialResourceMap.size() == 3);
  for (const auto& [id, material] : fixture.materialResourceMap) {
    CHECK(material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  }

  // Three spheres at world x = -2, 0, 2, camera at z = 8, fovY 60deg,
  // aspect 1 -- approximate screen-space centers, matching
  // pbr_sheen_demo_gpu_tests.cpp's own identical geometry.
  const auto rotation0 = rgbAt(frame, 145, 256);
  const auto rotation45 = rgbAt(frame, 256, 256);
  const auto rotation90 = rgbAt(frame, 367, 256);
  CHECK(static_cast<int>(rotation0[0]) + rotation0[1] + rotation0[2] > 10);
  CHECK(static_cast<int>(rotation45[0]) + rotation45[1] + rotation45[2] > 10);
  CHECK(static_cast<int>(rotation90[0]) + rotation90[1] + rotation90[2] > 10);
  // anisotropy_rotation 0 vs 90 degrees (same anisotropy_factor/base
  // material otherwise) must visibly differ at this sweep's own two
  // endpoints -- the real, coarse non-regression proof that
  // anisotropyRotation reaches the shader at all (the fine-grained
  // "highlight elongates and rotates correctly" claim is the golden
  // capture's own pre-commit visual sanity check, not this assertion).
  CHECK(colorDistance(rotation0, rotation90) > 0);

  auto second = atlantis::image_regression::renderPbrAnisotropicDemoFrame(fixture);
  REQUIRE(second.isOk());
  CHECK(countMismatchedBytes(second.value().rgba8, frame.rgba8) == 0);
  CHECK(fixture.environmentUploadCount == 1);
}

TEST_CASE("Full capture-compare cycle against the committed PBR anisotropic demo golden passes",
          "[image_regression][gpu][anisotropic][golden]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrAnisotropicDemoFixture(buildAnisotropicConfig());
  REQUIRE(fixtureResult.isOk());
  auto rendered = atlantis::image_regression::renderPbrAnisotropicDemoFrame(fixtureResult.value());
  REQUIRE(rendered.isOk());

  const std::filesystem::path goldens = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  const std::string stem = "pbr_anisotropic_demo/pbr_anisotropic_demo_512x512_rgba8unorm";
  auto golden = atlantis::image_regression::loadAndValidateGolden(goldens / (stem + ".png"),
                                                                  goldens / (stem + ".sidecar.txt"));
  REQUIRE(golden.isOk());
  const auto report = atlantis::image_regression::compareBuffers(rendered.value(), golden.value().pixels);
  if (!report.passed) {
    static_cast<void>(atlantis::image_regression::writeFailureArtifacts(
        ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, "pbr_anisotropic_demo_512x512_rgba8unorm", rendered.value(),
        golden.value().pixels));
  }
  REQUIRE(report.passed);
}

TEST_CASE("PBR anisotropic normal map demo renders a single normal-mapped, IBL-lit anisotropic sphere",
          "[image_regression][gpu][anisotropic]") {
  auto fixtureResult =
      atlantis::image_regression::setUpPbrAnisotropicDemoFixture(buildAnisotropicNormalMapConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  CHECK(fixture.world->lightEntities().empty());

  auto first = atlantis::image_regression::renderPbrAnisotropicDemoFrame(fixture);
  REQUIRE(first.isOk());
  const auto& frame = first.value();
  REQUIRE(frame.width == atlantis::image_regression::kPbrAnisotropicDemoExtentPixels);
  REQUIRE(fixture.materialResourceMap.size() == 1);
  for (const auto& [id, material] : fixture.materialResourceMap) {
    CHECK(material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  }

  const auto center = rgbAt(frame, 256, 256);
  CHECK(static_cast<int>(center[0]) + center[1] + center[2] > 10);

  // This is the real, non-regression proof that this fixture's own
  // resource-commit loop, inherited already-fixed from Milestone 3's own
  // fix-0 commit, persists candidate.newNormalMapTexture from the start:
  // a second render call against a fixture that genuinely realized a
  // normal-mapped material must not dereference a dangling
  // SampledTexture.
  auto second = atlantis::image_regression::renderPbrAnisotropicDemoFrame(fixture);
  REQUIRE(second.isOk());
  CHECK(countMismatchedBytes(second.value().rgba8, frame.rgba8) == 0);
  CHECK(fixture.environmentUploadCount == 1);
}

TEST_CASE("Full capture-compare cycle against the committed PBR anisotropic normal map demo golden passes",
          "[image_regression][gpu][anisotropic][golden]") {
  auto fixtureResult =
      atlantis::image_regression::setUpPbrAnisotropicDemoFixture(buildAnisotropicNormalMapConfig());
  REQUIRE(fixtureResult.isOk());
  auto rendered = atlantis::image_regression::renderPbrAnisotropicDemoFrame(fixtureResult.value());
  REQUIRE(rendered.isOk());

  const std::filesystem::path goldens = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  const std::string stem = "pbr_anisotropic_normal_map_demo/pbr_anisotropic_normal_map_demo_512x512_rgba8unorm";
  auto golden = atlantis::image_regression::loadAndValidateGolden(goldens / (stem + ".png"),
                                                                  goldens / (stem + ".sidecar.txt"));
  REQUIRE(golden.isOk());
  const auto report = atlantis::image_regression::compareBuffers(rendered.value(), golden.value().pixels);
  if (!report.passed) {
    static_cast<void>(atlantis::image_regression::writeFailureArtifacts(
        ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, "pbr_anisotropic_normal_map_demo_512x512_rgba8unorm", rendered.value(),
        golden.value().pixels));
  }
  REQUIRE(report.passed);
}

// ---------------------------------------------------------------------------
// Plan 0041 Milestone 3 (Spec 0041 R6, rulings O5/Q4): the emissive on/off
// differential over this file's own existing scene -- no new asset.
// ---------------------------------------------------------------------------

TEST_CASE("Emissive on/off (pbr_anisotropic_ibl): setting one material's emissiveFactor only brightens its own sphere",
          "[image_regression][gpu][emissive]") {
  // The anisotropic demo scene.
  const auto result = atlantis::image_regression::runEmissiveOnOff(
      [] { return atlantis::image_regression::setUpPbrAnisotropicDemoFixture(buildAnisotropicConfig()); }, [](auto& fixture) { return atlantis::image_regression::renderPbrAnisotropicDemoFrame(fixture); },
      "materials/pbr_anisotropic_rotation_45.material.txt");
  atlantis::image_regression::checkEmissiveOnlyBrightensItsSphere(result);
}

TEST_CASE("Emissive on/off (pbr_anisotropic_ibl_normal_map): setting one material's emissiveFactor only brightens its own sphere",
          "[image_regression][gpu][emissive]") {
  // The anisotropic normal-map demo scene.
  const auto result = atlantis::image_regression::runEmissiveOnOff(
      [] { return atlantis::image_regression::setUpPbrAnisotropicDemoFixture(buildAnisotropicNormalMapConfig()); }, [](auto& fixture) { return atlantis::image_regression::renderPbrAnisotropicDemoFrame(fixture); },
      "materials/pbr_anisotropic_normal_mapped.material.txt");
  atlantis::image_regression::checkEmissiveOnlyBrightensItsSphere(result);
}
