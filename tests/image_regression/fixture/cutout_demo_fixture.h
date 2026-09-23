#pragma once

// Plan 0042 Milestone 2 (Spec 0042 R6, ruling Q4): the cutout golden is
// rendered by PbrMaterialDemoFixture unchanged -- lit, no environment --
// reused by alias with its own scene (the hdr_roll_off/emissive_demo
// precedent).

#include "pbr_material_demo_fixture.h"

namespace atlantis::image_regression {

using CutoutDemoFixture = PbrMaterialDemoFixture;
using CutoutDemoSetupError = PbrMaterialDemoSetupError;
using CutoutDemoRenderError = PbrMaterialDemoRenderError;

inline constexpr std::uint32_t kCutoutDemoExtentPixels = kPbrMaterialDemoExtentPixels;

[[nodiscard]] inline atlantis::Result<CutoutDemoFixture, CutoutDemoSetupError> setUpCutoutDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  return setUpPbrMaterialDemoFixture(config);
}

[[nodiscard]] inline atlantis::Result<PixelBuffer, CutoutDemoRenderError> renderCutoutDemoFrame(
    CutoutDemoFixture& fixture) {
  return renderPbrMaterialDemoFrame(fixture);
}

}  // namespace atlantis::image_regression
