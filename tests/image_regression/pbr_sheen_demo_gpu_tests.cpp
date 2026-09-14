#include "fixture/pbr_sheen_demo_fixture.h"
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

// Plan 0035 Milestone 3 (ADR-0081/ADR-0042): the two sheen golden tests
// -- an IBL-only, no-normal-map 3-sphere sheen-roughness sweep
// (pbr_sheen_demo) and a normal-mapped 1-sphere variant
// (pbr_sheen_normal_map_demo), mirroring
// pbr_clearcoat_demo_gpu_tests.cpp's own exact structure.

namespace {

atlantis::runtime::BootstrapConfig buildSheenSharedConfig() {
  atlantis::runtime::BootstrapConfig config;
  const std::string unlit = ATLANTIS_PBR_SHEEN_DEMO_UNLIT_TEXTURED_SHADER_DIR;
  config.unlitTexturedVertexShaderSpirvPath = unlit + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath = unlit + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath = unlit + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath = unlit + "/textured_quad.frag.refl.json";
  const std::string lit = ATLANTIS_PBR_SHEEN_DEMO_LIT_TEXTURED_SHADER_DIR;
  config.litTexturedVertexShaderSpirvPath = lit + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath = lit + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath = lit + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath = lit + "/lit_textured.frag.refl.json";
  const std::string direct = ATLANTIS_PBR_SHEEN_DEMO_PBR_DIRECT_LIT_SHADER_DIR;
  config.pbrDirectLitVertexShaderSpirvPath = direct + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath = direct + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath = direct + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath = direct + "/pbr_direct_lit.frag.refl.json";
  const std::string ibl = ATLANTIS_PBR_SHEEN_DEMO_PBR_IBL_SHADER_DIR;
  config.pbrIblVertexShaderSpirvPath = ibl + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath = ibl + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = ibl + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath = ibl + "/pbr_ibl.frag.refl.json";
  const std::string iblNormalMap = ATLANTIS_PBR_SHEEN_DEMO_PBR_IBL_NORMAL_MAP_SHADER_DIR;
  config.pbrIblNormalMapVertexShaderSpirvPath = iblNormalMap + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath = iblNormalMap + "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath = iblNormalMap + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath = iblNormalMap + "/pbr_ibl_normal_map.frag.refl.json";
  const std::string sky = ATLANTIS_PBR_SHEEN_DEMO_SKY_SHADER_DIR;
  config.skyVertexShaderSpirvPath = sky + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = sky + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = sky + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = sky + "/sky.frag.refl.json";
  const std::string shadowCast = ATLANTIS_PBR_SHEEN_DEMO_SHADOW_CAST_SHADER_DIR;
  config.shadowCastVertexShaderSpirvPath = shadowCast + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath = shadowCast + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath = shadowCast + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath = shadowCast + "/shadow_cast.frag.refl.json";
  const std::string output = ATLANTIS_PBR_SHEEN_DEMO_OUTPUT_TRANSFORM_UNORM_SHADER_DIR;
  config.outputTransformUnormVertexShaderSpirvPath = output + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath = output + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath = output + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath = output + "/output_transform_unorm.frag.refl.json";
  const std::string sheen = ATLANTIS_PBR_SHEEN_DEMO_PBR_SHEEN_IBL_SHADER_DIR;
  config.pbrSheenIblVertexShaderSpirvPath = sheen + "/pbr_sheen_ibl.vert.spv";
  config.pbrSheenIblVertexShaderReflectionPath = sheen + "/pbr_sheen_ibl.vert.refl.json";
  config.pbrSheenIblFragmentShaderSpirvPath = sheen + "/pbr_sheen_ibl.frag.spv";
  config.pbrSheenIblFragmentShaderReflectionPath = sheen + "/pbr_sheen_ibl.frag.refl.json";
  const std::string sheenNormalMap = ATLANTIS_PBR_SHEEN_DEMO_PBR_SHEEN_IBL_NORMAL_MAP_SHADER_DIR;
  config.pbrSheenIblNormalMapVertexShaderSpirvPath = sheenNormalMap + "/pbr_sheen_ibl_normal_map.vert.spv";
  config.pbrSheenIblNormalMapVertexShaderReflectionPath =
      sheenNormalMap + "/pbr_sheen_ibl_normal_map.vert.refl.json";
  config.pbrSheenIblNormalMapFragmentShaderSpirvPath = sheenNormalMap + "/pbr_sheen_ibl_normal_map.frag.spv";
  config.pbrSheenIblNormalMapFragmentShaderReflectionPath =
      sheenNormalMap + "/pbr_sheen_ibl_normal_map.frag.refl.json";
  config.environmentArtifactPath = ATLANTIS_PBR_SHEEN_DEMO_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_PBR_SHEEN_DEMO_ENVIRONMENT_METADATA_PATH;
  return config;
}

atlantis::runtime::BootstrapConfig buildSheenConfig() {
  atlantis::runtime::BootstrapConfig config = buildSheenSharedConfig();
  config.sceneArtifactPath = ATLANTIS_pbr_sheen_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_sheen_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_sheen_demo_scene_MANIFEST_PATH;
  return config;
}

atlantis::runtime::BootstrapConfig buildSheenNormalMapConfig() {
  atlantis::runtime::BootstrapConfig config = buildSheenSharedConfig();
  config.sceneArtifactPath = ATLANTIS_pbr_sheen_normal_map_demo_scene_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_pbr_sheen_normal_map_demo_scene_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_pbr_sheen_normal_map_demo_scene_MANIFEST_PATH;
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
// the full diff -- see pbr_clearcoat_demo_gpu_tests.cpp's own identical
// helper and the real dangling-normal-map-texture bug it caught here.
std::size_t countMismatchedBytes(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
  std::size_t mismatches = 0;
  const std::size_t n = std::min(a.size(), b.size());
  for (std::size_t i = 0; i < n; ++i) {
    if (a[i] != b[i]) ++mismatches;
  }
  return mismatches;
}

}  // namespace

TEST_CASE("PBR sheen demo renders a sheen-roughness sweep of three IBL-lit spheres",
          "[image_regression][gpu][sheen]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrSheenDemoFixture(buildSheenConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  CHECK(fixture.world->lightEntities().empty());

  auto first = atlantis::image_regression::renderPbrSheenDemoFrame(fixture);
  REQUIRE(first.isOk());
  const auto& frame = first.value();
  REQUIRE(frame.width == atlantis::image_regression::kPbrSheenDemoExtentPixels);
  REQUIRE(fixture.materialResourceMap.size() == 3);
  for (const auto& [id, material] : fixture.materialResourceMap) {
    CHECK(material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  }

  // Three spheres at world x = -2, 0, 2, camera at z = 8, fovY 60deg,
  // aspect 1 -- approximate screen-space centers, matching
  // pbr_clearcoat_demo_gpu_tests.cpp's own identical geometry.
  const auto lowRoughness = rgbAt(frame, 145, 256);
  const auto midRoughness = rgbAt(frame, 256, 256);
  const auto highRoughness = rgbAt(frame, 367, 256);
  CHECK(static_cast<int>(lowRoughness[0]) + lowRoughness[1] + lowRoughness[2] > 10);
  CHECK(static_cast<int>(midRoughness[0]) + midRoughness[1] + midRoughness[2] > 10);
  CHECK(static_cast<int>(highRoughness[0]) + highRoughness[1] + highRoughness[2] > 10);
  // sheen_roughness 0.1 vs 1.0 (same sheenColor/base material otherwise)
  // must visibly differ at the sweep's own low/high boundary.
  CHECK(colorDistance(lowRoughness, highRoughness) > 0);

  auto second = atlantis::image_regression::renderPbrSheenDemoFrame(fixture);
  REQUIRE(second.isOk());
  CHECK(countMismatchedBytes(second.value().rgba8, frame.rgba8) == 0);
  CHECK(fixture.environmentUploadCount == 1);
}

TEST_CASE("Full capture-compare cycle against the committed PBR sheen demo golden passes",
          "[image_regression][gpu][sheen][golden]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrSheenDemoFixture(buildSheenConfig());
  REQUIRE(fixtureResult.isOk());
  auto rendered = atlantis::image_regression::renderPbrSheenDemoFrame(fixtureResult.value());
  REQUIRE(rendered.isOk());

  const std::filesystem::path goldens = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  const std::string stem = "pbr_sheen_demo/pbr_sheen_demo_512x512_rgba8unorm";
  auto golden = atlantis::image_regression::loadAndValidateGolden(goldens / (stem + ".png"),
                                                                  goldens / (stem + ".sidecar.txt"));
  REQUIRE(golden.isOk());
  const auto report = atlantis::image_regression::compareBuffers(rendered.value(), golden.value().pixels);
  if (!report.passed) {
    static_cast<void>(atlantis::image_regression::writeFailureArtifacts(
        ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, "pbr_sheen_demo_512x512_rgba8unorm", rendered.value(),
        golden.value().pixels));
  }
  REQUIRE(report.passed);
}

TEST_CASE("PBR sheen normal map demo renders a single normal-mapped, IBL-lit sheen sphere",
          "[image_regression][gpu][sheen]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrSheenDemoFixture(buildSheenNormalMapConfig());
  REQUIRE(fixtureResult.isOk());
  auto& fixture = fixtureResult.value();
  CHECK(fixture.world->lightEntities().empty());

  auto first = atlantis::image_regression::renderPbrSheenDemoFrame(fixture);
  REQUIRE(first.isOk());
  const auto& frame = first.value();
  REQUIRE(frame.width == atlantis::image_regression::kPbrSheenDemoExtentPixels);
  REQUIRE(fixture.materialResourceMap.size() == 1);
  for (const auto& [id, material] : fixture.materialResourceMap) {
    CHECK(material->environmentBinding() == atlantis::renderer::MaterialEnvironmentBinding::Ibl);
  }

  const auto center = rgbAt(frame, 256, 256);
  CHECK(static_cast<int>(center[0]) + center[1] + center[2] > 10);

  // This is the real, non-regression proof for Milestone 3's own fix-0
  // commit: a second render call against a fixture that genuinely
  // realized a normal-mapped material must not dereference a dangling
  // SampledTexture (the bug fix-0 found and fixed).
  auto second = atlantis::image_regression::renderPbrSheenDemoFrame(fixture);
  REQUIRE(second.isOk());
  CHECK(countMismatchedBytes(second.value().rgba8, frame.rgba8) == 0);
  CHECK(fixture.environmentUploadCount == 1);
}

TEST_CASE("Full capture-compare cycle against the committed PBR sheen normal map demo golden passes",
          "[image_regression][gpu][sheen][golden]") {
  auto fixtureResult = atlantis::image_regression::setUpPbrSheenDemoFixture(buildSheenNormalMapConfig());
  REQUIRE(fixtureResult.isOk());
  auto rendered = atlantis::image_regression::renderPbrSheenDemoFrame(fixtureResult.value());
  REQUIRE(rendered.isOk());

  const std::filesystem::path goldens = ATLANTIS_IMAGE_REGRESSION_GOLDENS_DIR;
  const std::string stem = "pbr_sheen_normal_map_demo/pbr_sheen_normal_map_demo_512x512_rgba8unorm";
  auto golden = atlantis::image_regression::loadAndValidateGolden(goldens / (stem + ".png"),
                                                                  goldens / (stem + ".sidecar.txt"));
  REQUIRE(golden.isOk());
  const auto report = atlantis::image_regression::compareBuffers(rendered.value(), golden.value().pixels);
  if (!report.passed) {
    static_cast<void>(atlantis::image_regression::writeFailureArtifacts(
        ATLANTIS_IMAGE_REGRESSION_OUTPUT_DIR, "pbr_sheen_normal_map_demo_512x512_rgba8unorm", rendered.value(),
        golden.value().pixels));
  }
  REQUIRE(report.passed);
}
