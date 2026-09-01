#pragma once

#include "pbr_material_demo_fixture.h"

namespace atlantis::image_regression {

// Plan 0024 Milestone 10: the roll-off scene deliberately exercises the
// same PBR/Runtime/HDR composition path as PbrMaterialDemoFixture. Keep
// the fixture state and error contracts identical so the new baseline
// differs only in its authored scene data, not in a second render path.
using HdrRollOffDemoFixture = PbrMaterialDemoFixture;
using HdrRollOffDemoSetupError = PbrMaterialDemoSetupError;
using HdrRollOffDemoRenderError = PbrMaterialDemoRenderError;

inline constexpr std::uint32_t kHdrRollOffDemoExtentPixels = kPbrMaterialDemoExtentPixels;
inline constexpr atlantis::rhi::Format kHdrRollOffDemoColorFormat = kPbrMaterialDemoColorFormat;

[[nodiscard]] atlantis::Result<HdrRollOffDemoFixture, HdrRollOffDemoSetupError> setUpHdrRollOffDemoFixture(
    const atlantis::runtime::BootstrapConfig& config);

[[nodiscard]] atlantis::Result<PixelBuffer, HdrRollOffDemoRenderError> renderHdrRollOffDemoFrame(
    HdrRollOffDemoFixture& fixture);

}  // namespace atlantis::image_regression
