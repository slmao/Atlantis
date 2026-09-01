#include "hdr_roll_off_demo_fixture.h"

namespace atlantis::image_regression {

atlantis::Result<HdrRollOffDemoFixture, HdrRollOffDemoSetupError> setUpHdrRollOffDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  return setUpPbrMaterialDemoFixture(config);
}

atlantis::Result<PixelBuffer, HdrRollOffDemoRenderError> renderHdrRollOffDemoFrame(
    HdrRollOffDemoFixture& fixture) {
  return renderPbrMaterialDemoFrame(fixture);
}

}  // namespace atlantis::image_regression
