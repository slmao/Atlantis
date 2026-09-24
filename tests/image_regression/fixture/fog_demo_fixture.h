#pragma once

// Plan 0043 Milestone 2 (Spec 0043 R1-R3, P8): the height-fog scenes reuse
// two existing fixtures by alias, differing only in their authored scene
// data (the cutout_demo/emissive_demo precedent):
// - the lit goldens, fog_distance_demo and fog_height_demo, and the
//   fog_zero_neutrality scene: PbrMaterialDemoFixture (lit, no environment,
//   pbr_direct_lit);
// - the exact dark golden, fog_dark_demo: PbrNormalMapDemoFixture without
//   an environment, the emissive_demo fixture, so every sphere is exactly
//   E * (1 - f) + C * f.
// The fog itself comes from each scene's camera node, or is set on the
// World's camera before a render (fog_differential.h).

#include "pbr_material_demo_fixture.h"
#include "pbr_normal_map_demo_fixture.h"

#include <string>

namespace atlantis::image_regression {

using FogLitDemoFixture = PbrMaterialDemoFixture;
using FogLitDemoSetupError = PbrMaterialDemoSetupError;
using FogLitDemoRenderError = PbrMaterialDemoRenderError;

inline constexpr std::uint32_t kFogLitDemoExtentPixels = kPbrMaterialDemoExtentPixels;

[[nodiscard]] inline atlantis::Result<FogLitDemoFixture, FogLitDemoSetupError> setUpFogLitDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  return setUpPbrMaterialDemoFixture(config);
}

[[nodiscard]] inline atlantis::Result<PixelBuffer, FogLitDemoRenderError> renderFogLitDemoFrame(
    FogLitDemoFixture& fixture) {
  return renderPbrMaterialDemoFrame(fixture);
}

using FogDarkDemoFixture = PbrNormalMapDemoFixture;
using FogDarkDemoSetupError = PbrNormalMapDemoSetupError;
using FogDarkDemoRenderError = PbrNormalMapDemoRenderError;

inline constexpr std::uint32_t kFogDarkDemoExtentPixels = kPbrNormalMapDemoExtentPixels;

[[nodiscard]] inline atlantis::Result<FogDarkDemoFixture, FogDarkDemoSetupError> setUpFogDarkDemoFixture(
    const atlantis::runtime::BootstrapConfig& config, const std::string& controlMaterialArtifactPath,
    const std::string& controlMaterialMetadataPath) {
  return setUpPbrNormalMapDemoFixture(config, controlMaterialArtifactPath, controlMaterialMetadataPath);
}

[[nodiscard]] inline atlantis::Result<PixelBuffer, FogDarkDemoRenderError> renderFogDarkDemoFrame(
    FogDarkDemoFixture& fixture) {
  return renderPbrNormalMapDemoFrame(fixture);
}

}  // namespace atlantis::image_regression
