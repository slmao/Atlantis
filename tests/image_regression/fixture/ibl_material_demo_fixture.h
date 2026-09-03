#pragma once

#include "pbr_material_demo_fixture.h"

namespace atlantis::image_regression {

// The IBL fixture deliberately reuses the established PBR fixture substrate;
// its distinct config selects the light-free scene, environment asset, and
// pbr_ibl shader. This keeps one composition path for the same four materials.
using IblMaterialDemoFixture = PbrMaterialDemoFixture;
using IblMaterialDemoSetupError = PbrMaterialDemoSetupError;
using IblMaterialDemoRenderError = PbrMaterialDemoRenderError;

inline constexpr std::uint32_t kIblMaterialDemoExtentPixels = kPbrMaterialDemoExtentPixels;

[[nodiscard]] atlantis::Result<IblMaterialDemoFixture, IblMaterialDemoSetupError> setUpIblMaterialDemoFixture(
    const atlantis::runtime::BootstrapConfig& config);
[[nodiscard]] atlantis::Result<PixelBuffer, IblMaterialDemoRenderError> renderIblMaterialDemoFrame(
    IblMaterialDemoFixture& fixture);

}  // namespace atlantis::image_regression
