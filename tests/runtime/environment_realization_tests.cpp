#include <atlantis/runtime/environment_realization.h>

#include <array>
#include <cstddef>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Environment SH publication fills exactly the 144-byte camera-uniform tail",
          "[runtime][environment_realization][ibl]") {
  std::array<float, 116> cameraUniform;
  cameraUniform.fill(-7.0F);
  std::array<float, 36> coefficients;
  for (std::size_t i = 0; i < coefficients.size(); ++i) coefficients[i] = static_cast<float>(i) + 0.25F;

  atlantis::runtime::writeEnvironmentIrradianceSh(
      std::span<float, 36>(cameraUniform.data() + 80, 36), &coefficients);
  CHECK(cameraUniform[79] == -7.0F);
  CHECK(cameraUniform[80] == 0.25F);
  CHECK(cameraUniform[115] == 35.25F);

  atlantis::runtime::writeEnvironmentIrradianceSh(
      std::span<float, 36>(cameraUniform.data() + 80, 36), nullptr);
  CHECK(cameraUniform[79] == -7.0F);
  for (std::size_t i = 80; i < cameraUniform.size(); ++i) CHECK(cameraUniform[i] == 0.0F);
}
