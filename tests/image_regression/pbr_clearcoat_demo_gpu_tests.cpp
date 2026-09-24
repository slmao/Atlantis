#include "fixture/pbr_clearcoat_demo_fixture.h"
#include "support/fog_differential.h"
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

// Plan 0035 Milestone 2 (ADR-0081/ADR-0042): the two clearcoat golden
// tests -- an IBL-only, no-normal-map 3-sphere clearcoat-roughness sweep
// (pbr_clearcoat_demo) and a normal-mapped 1-sphere variant
// (pbr_clearcoat_normal_map_demo), mirroring
// ibl_material_demo_gpu_tests.cpp's own exact structure (buildConfig() +
// a rendering/determinism TEST_CASE + a golden capture-compare
// TEST_CASE), doubled for the two shader variants ("两变体各至少 1 个").

namespace {

atlantis::runtime::BootstrapConfig buildClearcoatSharedConfig() {
  atlantis::runtime::BootstrapConfig config;
  const std::string unlit = ATLANTIS_PBR_CLEARCOAT_DEMO_UNLIT_TEXTURED_SHADER_DIR;
  config.unlitTexturedVertexShaderSpirvPath = unlit + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath = unlit + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath = unlit + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath = unlit + "/textured_quad.frag.refl.json";
  const std::string lit = ATLANTIS_PBR_CLEARCOAT_DEMO_LIT_TEXTURED_SHADER_DIR;
  config.litTexturedVertexShaderSpirvPath = lit + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath = lit + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath = lit + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath = lit + "/lit_textured.frag.refl.json";
  const std::string direct = ATLANTIS_PBR_CLEARCOAT_DEMO_PBR_DIRECT_LIT_SHADER_DIR;
  config.pbrDirectLitVertexShaderSpirvPath = direct + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath = direct + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath = direct + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath = direct + "/pbr_direct_lit.frag.refl.json";
  const std::string ibl = ATLANTIS_PBR_CLEARCOAT_DEMO_PBR_IBL_SHADER_DIR;
  config.pbrIblVertexShaderSpirvPath = ibl + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath = ibl + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = ibl + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath = ibl + "/pbr_ibl.frag.refl.json";
  const std::string iblNormalMap = ATLANTIS_PBR_CLEARCOAT_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR;
  config.pbrIblNormalMapVertexShaderSpirvPath = iblNormalMap + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath = iblNormalMap + "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath = iblNormalMap + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath = iblNormalMap + "/pbr_ibl_normal_map.frag.refl.json";
  const std::string sky = ATLANTIS_PBR_CLEARCOAT_DEMO_SKY_SHADER_DIR;
  config.skyVertexShaderSpirvPath = sky + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = sky + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = sky + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = sky + "/sky.frag.refl.json";
  const std::string shadowCast = ATLANTIS_PBR_CLEARCOAT_DEMO_SHADOW_CAST_SHADER_DIR;
  config.shadowCastVertexShaderSpirvPath = shadowCast + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath = shadowCast + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath = shadowCast + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath = shadowCast + "/shadow_cast.frag.refl.json";
  const std::string output = ATLANTIS_PBR_CLEARCOAT_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR;
  config.outputTransformUnormVertexShaderSpirvPath = output + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = output + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = output + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = output + "/output_transform_unorm.frag.refl.json";
  const std::string clearcoat = ATLANTIS_PBR_CLEARCOAT_DEMO_PBR_CLEARCOAT_IBL_SHADER_DIR;
  config.pbrClearcoatIblVertexShaderSpirvPath = clearcoat + "/pbr_clearcoat_ibl.vert.spv";
  config.pbrClearcoatIblVertexShaderReflectionPath = clearcoat + "/pbr_clearcoat_ibl.vert.refl.json";
  config.pbrClearcoatIblFragmentShaderSpirvPath = clearcoat + "/pbr_clearcoat_ibl.frag.spv";
  config.pbrClearcoatIblFragmentShaderReflectionPath = clearcoat + "/pbr_clearcoat_ibl.frag.refl.json";
  const std::string clearcoatNormalMap = ATLANTIS_PBR_CLEARCOAT_DEMO_PBR_CLEARCOAT_IBL_NORMAL_MAP_SHADER_DIR;
  config.pbrClearcoatIblNormalMapVertexShaderSpirvPath = clearcoatNormalMap + "/pbr_clearcoat_ibl_normal_map.vert.spv";
  config.pbrClearcoatIblNormalMapVertexShaderReflectionPath =
      clearcoatNormalMap + "/pbr_clearcoat_ibl_normal_map.vert.refl.json";
  config.pbrClearcoatIblNormalMapFragmentShaderSpirvPath =
      clearcoatNormalMap + "/pbr_clearcoat_ibl_normal_map.frag.spv";
  config.pbrClearcoatIblNormalMapFragmentShaderReflectionPath =
      clearcoatNormalMap + "/pbr_clearcoat_ibl_normal_map.frag.refl.json";
  config.environmentArtifactPath = ATLANTIS_PBR_CLEARCOAT_DEMO_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_PBR_CLEARCOAT_DEMO_ENVIRONMENT_METADATA_PATH;
  return config;
}

atlantis::runtime::BootstrapConfig buildClearcoatConfig() {
  atlantis::runtime::BootstrapConfig config = buildClearcoatSharedConfig();
  config.sceneArtifactPath = ATLANTIS_pbr_clearcoat_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_clearcoat_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_clearcoat_demo_scene_MANIFEST_PATH;
  return config;
}

atlantis::runtime::BootstrapConfig buildClearcoatNormalMapConfig() {
  atlantis::runtime::BootstrapConfig config = buildClearcoatSharedConfig();
  config.sceneArtifactPath = ATLANTIS_pbr_clearcoat_normal_map_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_clearcoat_normal_map_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_clearcoat_normal_map_demo_scene_MANIFEST_PATH;
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
// directly -- a direct REQUIRE(a == b) on a failing multi-megabyte vector
// triggers a known Catch2 v3.7.1 console-reporter crash
// (catch_textflow.cpp's own internal assertion) when it tries to render
// the full diff.
std::size_t countMismatchedBytes(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  std::size_t mismatches = 0;
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) ++mismatches;
  }
  return mismatches;
}

}  // namespace

TEST_CASE("PBR clearcoat demo renders a clearcoat-roughness sweep of three IBL-lit spheres",
          "[image_regression][gpu][clearcoat]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrClearcoatDemoFixture(buildClearcoatConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  CHECK(fixture.world->lightEntities().empty());

  auto first = atlantis::image_regression::renderPbrClearcoatDemoFrame(fixture);
  REQUIRE(first.isOk());
  const auto& frame = first.value();
  REQUIRE(frame.width == atlantis::image_regression::kPbrClearcoatDemoExtentPixels);
  REQUIRE(fixture.materialResourceMap.size() == 3);
  for (const auto& [id, material] : fixture.materialResourceMap) {
    CHECK(material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  }

  // Three spheres at world x = -2, 0, 2, camera at z = 8, fovY 60deg,
  // aspect 1 -- approximate screen-space centers.
  const auto noCoat = rgbAt(frame, 145, 256);
  const auto lowRoughness = rgbAt(frame, 256, 256);
  const auto highRoughness = rgbAt(frame, 367, 256);
  CHECK(static_cast<int>(noCoat[0]) + noCoat[1] + noCoat[2] > 10);
  CHECK(static_cast<int>(lowRoughness[0]) + lowRoughness[1] + lowRoughness[2] > 10);
  CHECK(static_cast<int>(highRoughness[0]) + highRoughness[1] + highRoughness[2] > 10);
  // clearcoat_factor 0.0 vs 1.0 (same base material otherwise) must visibly
  // differ at the sweep's own no-coat/low-roughness boundary.
  CHECK(colorDistance(noCoat, lowRoughness) > 0);

  auto second = atlantis::image_regression::renderPbrClearcoatDemoFrame(fixture);
  REQUIRE(second.isOk());
  CHECK(countMismatchedBytes(second.value().rgba8, frame.rgba8) == 0);
  CHECK(fixture.environmentUploadCount == 1);
}

TEST_CASE("Full capture-compare cycle against the committed PBR clearcoat demo golden passes",
          "[image_regression][gpu][clearcoat][golden]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrClearcoatDemoFixture(buildClearcoatConfig());
  REQUIRE(fixtureResult.isOk());
  auto rendered = atlantis::image_regression::renderPbrClearcoatDemoFrame(fixtureResult.value());
  REQUIRE(rendered.isOk());

  const std::filesystem::path goldens = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  const std::string stem = "pbr_clearcoat_demo/pbr_clearcoat_demo_512x512_rgba8unorm";
  auto golden = atlantis::image_regression::loadAndValidateGolden(goldens / (stem + ".png"),
                                                                  goldens / (stem + ".sidecar.txt"));
  REQUIRE(golden.isOk());
  const auto report = atlantis::image_regression::compareBuffers(rendered.value(), golden.value().pixels);
  if (!report.passed) {
    static_cast<void>(atlantis::image_regression::writeFailureArtifacts(
        ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, "pbr_clearcoat_demo_512x512_rgba8unorm", rendered.value(),
        golden.value().pixels));
  }
  REQUIRE(report.passed);
}

TEST_CASE("PBR clearcoat normal map demo renders a single normal-mapped, IBL-lit clearcoat sphere",
          "[image_regression][gpu][clearcoat]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrClearcoatDemoFixture(buildClearcoatNormalMapConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  CHECK(fixture.world->lightEntities().empty());

  auto first = atlantis::image_regression::renderPbrClearcoatDemoFrame(fixture);
  REQUIRE(first.isOk());
  const auto& frame = first.value();
  REQUIRE(frame.width == atlantis::image_regression::kPbrClearcoatDemoExtentPixels);
  REQUIRE(fixture.materialResourceMap.size() == 1);
  for (const auto& [id, material] : fixture.materialResourceMap) {
    CHECK(material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  }

  const auto center = rgbAt(frame, 256, 256);
  CHECK(static_cast<int>(center[0]) + center[1] + center[2] > 10);

  auto second = atlantis::image_regression::renderPbrClearcoatDemoFrame(fixture);
  REQUIRE(second.isOk());
  CHECK(countMismatchedBytes(second.value().rgba8, frame.rgba8) == 0);
  CHECK(fixture.environmentUploadCount == 1);
}

TEST_CASE("Full capture-compare cycle against the committed PBR clearcoat normal map demo golden passes",
          "[image_regression][gpu][clearcoat][golden]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrClearcoatDemoFixture(buildClearcoatNormalMapConfig());
  REQUIRE(fixtureResult.isOk());
  auto rendered = atlantis::image_regression::renderPbrClearcoatDemoFrame(fixtureResult.value());
  REQUIRE(rendered.isOk());

  const std::filesystem::path goldens = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  const std::string stem = "pbr_clearcoat_normal_map_demo/pbr_clearcoat_normal_map_demo_512x512_rgba8unorm";
  auto golden = atlantis::image_regression::loadAndValidateGolden(goldens / (stem + ".png"),
                                                                  goldens / (stem + ".sidecar.txt"));
  REQUIRE(golden.isOk());
  const auto report = atlantis::image_regression::compareBuffers(rendered.value(), golden.value().pixels);
  if (!report.passed) {
    static_cast<void>(atlantis::image_regression::writeFailureArtifacts(
        ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, "pbr_clearcoat_normal_map_demo_512x512_rgba8unorm", rendered.value(),
        golden.value().pixels));
  }
  REQUIRE(report.passed);
}

// ---------------------------------------------------------------------------
// Plan 0041 Milestone 3 (Spec 0041 R6, rulings O5/Q4): the emissive on/off
// differential over this file's own existing scene -- no new asset.
// ---------------------------------------------------------------------------

TEST_CASE("Emissive on/off (pbr_clearcoat_ibl): setting one material's emissiveFactor only brightens its own sphere",
          "[image_regression][gpu][emissive]") {
  // The clearcoat demo scene.
  const auto result = atlantis::image_regression::runEmissiveOnOff(
      [] { return atlantis::image_regression::setUpPbrClearcoatDemoFixture(buildClearcoatConfig()); }, [](auto& fixture) { return atlantis::image_regression::renderPbrClearcoatDemoFrame(fixture); },
      "materials/pbr_clearcoat_low_roughness.material.txt");
  atlantis::image_regression::checkEmissiveOnlyBrightensItsSphere(result);
}

TEST_CASE("Emissive on/off (pbr_clearcoat_ibl_normal_map): setting one material's emissiveFactor only brightens its own sphere",
          "[image_regression][gpu][emissive]") {
  // The clearcoat normal-map demo scene.
  const auto result = atlantis::image_regression::runEmissiveOnOff(
      [] { return atlantis::image_regression::setUpPbrClearcoatDemoFixture(buildClearcoatNormalMapConfig()); }, [](auto& fixture) { return atlantis::image_regression::renderPbrClearcoatDemoFrame(fixture); },
      "materials/pbr_clearcoat_normal_mapped.material.txt");
  atlantis::image_regression::checkEmissiveOnlyBrightensItsSphere(result);
}

// Plan 0043 Milestone 2 (Spec 0043 R2-R3, P9): the saturating-fog differential
// for the shader variant(s) this file's config realizes -- see
// support/fog_differential.h.

TEST_CASE("Height fog (Plan 0043 P9): pbr_clearcoat_ibl fogs its surfaces, and every other pixel stays byte-identical",
          "[image_regression][gpu][fog]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrClearcoatDemoFixture(buildClearcoatConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  atlantis::image_regression::checkSaturatedFogDifferential(
      fixture, [](auto& f) { return atlantis::image_regression::renderPbrClearcoatDemoFrame(f); }, "materials/pbr_clearcoat_low_roughness.material.txt");
  REQUIRE(fixture.device->waitIdle().isOk());
}

TEST_CASE("Height fog (Plan 0043 P9): pbr_clearcoat_ibl_normal_map fogs its surfaces, and every other pixel stays byte-identical",
          "[image_regression][gpu][fog]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrClearcoatDemoFixture(buildClearcoatNormalMapConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  atlantis::image_regression::checkSaturatedFogDifferential(
      fixture, [](auto& f) { return atlantis::image_regression::renderPbrClearcoatDemoFrame(f); }, "materials/pbr_clearcoat_normal_mapped.material.txt");
  REQUIRE(fixture.device->waitIdle().isOk());
}
