// Plan 0044 Milestone 1 (Spec 0044, ADR-0092 Decision 3, P10): the bloom
// infrastructure on the dark emissive fixture (PbrNormalMapDemoFixture,
// ruling O4) -- the three bloom Pipelines created under Validation Layers,
// the twelve targets at the Plan's extents, and no rendering change: in
// Milestone 1 no bloom pass is recorded. Milestone 2's property tests,
// gates and goldens are in bloom_on_tests.cpp; this file's last TEST_CASE
// still holds, since a fixture configured for bloom renders bloom only when
// asked to (emissive_demo's camera declares none).

#include "bloom_test_config.h"
#include "fixture/emissive_demo_fixture.h"
#include "support/pixel_diff.h"

#include <atlantis/renderer/bloom.h>
#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <string>

using atlantis::image_regression::addBloomShaderPaths;
using atlantis::image_regression::buildDarkEmissiveConfig;
using atlantis::image_regression::EmissiveDemoFixture;
using atlantis::image_regression::kEmissiveDemoExtentPixels;
using atlantis::image_regression::PixelBuffer;
using atlantis::image_regression::renderEmissiveDemoFrame;
using atlantis::image_regression::setUpEmissiveDemoFixture;
using atlantis::runtime::BootstrapConfig;

namespace {

[[nodiscard]] BootstrapConfig buildEmissiveConfig() {
  return buildDarkEmissiveConfig({ATLANTIS_emissive_demo_scene_ARTIFACT_PATH, ATLANTIS_emissive_demo_scene_METADATA_PATH,
                                  ATLANTIS_emissive_demo_scene_MANIFEST_PATH});
}

// The same config plus all twelve bloom shader paths.
[[nodiscard]] BootstrapConfig buildBloomConfig() {
  BootstrapConfig config = buildEmissiveConfig();
  addBloomShaderPaths(config);
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
  // ADR-0092 Accepted Correction 2026-09-25: twelve distinct instances.
  for (std::size_t i = 0; i < fixture.bloomPipelines.size(); ++i) {
    INFO("pipeline " << i);
    CHECK(fixture.bloomPipelines[i] != nullptr);
    for (std::size_t j = 0; j < i; ++j) CHECK(fixture.bloomPipelines[i].get() != fixture.bloomPipelines[j].get());
  }
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
  for (const auto& pipeline : fixture.bloomPipelines) CHECK(pipeline == nullptr);
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
