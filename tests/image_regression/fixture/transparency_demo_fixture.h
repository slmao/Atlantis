#pragma once

// Plan 0042 Milestone 3 (Spec 0042 R7, ruling Q5): the transparency golden
// and its swap-order twin are rendered by PbrMaterialDemoFixture unchanged
// -- lit, no environment -- reused by alias with their own scenes (the
// cutout_demo precedent). The fixture passes the camera's world position
// to drawFrame(), which the blended spheres require.

#include "pbr_material_demo_fixture.h"

namespace atlantis::image_regression {

using TransparencyDemoFixture = PbrMaterialDemoFixture;
using TransparencyDemoSetupError = PbrMaterialDemoSetupError;
using TransparencyDemoRenderError = PbrMaterialDemoRenderError;

inline constexpr std::uint32_t kTransparencyDemoExtentPixels = kPbrMaterialDemoExtentPixels;

[[nodiscard]] inline atlantis::Result<TransparencyDemoFixture, TransparencyDemoSetupError> setUpTransparencyDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  return setUpPbrMaterialDemoFixture(config);
}

[[nodiscard]] inline atlantis::Result<PixelBuffer, TransparencyDemoRenderError> renderTransparencyDemoFrame(
    TransparencyDemoFixture& fixture) {
  return renderPbrMaterialDemoFrame(fixture);
}

}  // namespace atlantis::image_regression
