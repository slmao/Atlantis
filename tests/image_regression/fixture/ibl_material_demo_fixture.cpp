#include "ibl_material_demo_fixture.h"

namespace atlantis::image_regression {

atlantis::Result<IblMaterialDemoFixture, IblMaterialDemoSetupError> setUpIblMaterialDemoFixture(
    const atlantis::runtime::BootstrapConfig& config) {
  return setUpPbrMaterialDemoFixture(config);
}

atlantis::Result<PixelBuffer, IblMaterialDemoRenderError> renderIblMaterialDemoFrame(
    IblMaterialDemoFixture& fixture) {
  return renderPbrMaterialDemoFrame(fixture);
}

}  // namespace atlantis::image_regression
