#pragma once

#include "pbr_normal_map_demo_fixture.h"

#include <string>

namespace atlantis::image_regression {

// Plan 0041 Milestone 3 (Plan 0041 P5, ruling Q1): the emissive dark-scene
// golden deliberately reuses PbrNormalMapDemoFixture -- the one fixture
// whose config carries BOTH direct-lit shader pairs (plain and normal-map)
// and whose environment is optional -- so emissive_demo differs only in
// its authored scene data, not in a second render path (the
// hdr_roll_off_demo_fixture.h precedent). The scene's no-emissive control
// sphere uses pbr_normal_mapped_control, which is also this fixture's own
// required control material.
using EmissiveDemoFixture = PbrNormalMapDemoFixture;
using EmissiveDemoSetupError = PbrNormalMapDemoSetupError;
using EmissiveDemoRenderError = PbrNormalMapDemoRenderError;

inline constexpr std::uint32_t kEmissiveDemoExtentPixels = kPbrNormalMapDemoExtentPixels;

[[nodiscard]] inline atlantis::Result<EmissiveDemoFixture, EmissiveDemoSetupError> setUpEmissiveDemoFixture(
    const atlantis::runtime::BootstrapConfig& config, const std::string& controlMaterialArtifactPath,
    const std::string& controlMaterialMetadataPath) {
  return setUpPbrNormalMapDemoFixture(config, controlMaterialArtifactPath, controlMaterialMetadataPath);
}

[[nodiscard]] inline atlantis::Result<PixelBuffer, EmissiveDemoRenderError> renderEmissiveDemoFrame(
    EmissiveDemoFixture& fixture) {
  return renderPbrNormalMapDemoFrame(fixture);
}

// Plan 0044 M2: bloom-forwarding overload.
[[nodiscard]] inline atlantis::Result<PixelBuffer, EmissiveDemoRenderError> renderEmissiveDemoFrame(
    EmissiveDemoFixture& fixture, bool includeShadowCasters, bool useControlMaterial,
    const atlantis::renderer::BloomInput* bloom) {
  return renderPbrNormalMapDemoFrame(fixture, includeShadowCasters, useControlMaterial, bloom);
}

}  // namespace atlantis::image_regression
