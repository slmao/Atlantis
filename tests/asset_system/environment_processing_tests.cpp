#include "environment_processing.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cmath>
#include <limits>
#include <vector>

using namespace atlantis::asset_system;

namespace {

[[nodiscard]] std::vector<float> constantSource(std::uint32_t width, std::uint32_t height, float red, float green,
                                                float blue) {
  std::vector<float> pixels(static_cast<std::size_t>(width) * height * 4);
  for (std::size_t i = 0; i < pixels.size(); i += 4) {
    pixels[i] = red;
    pixels[i + 1] = green;
    pixels[i + 2] = blue;
    pixels[i + 3] = 1.0F;
  }
  return pixels;
}

[[nodiscard]] double evaluateRedIrradiance(const EnvironmentAssetData& data, double x, double y, double z) {
  const double basis[9] = {0.282095,
                           0.488603 * z,
                           0.488603 * y,
                           0.488603 * x,
                           1.092548 * x * z,
                           1.092548 * z * y,
                           0.315392 * (3.0 * y * y - 1.0),
                           1.092548 * x * y,
                           0.546274 * (x * x - z * z)};
  double result = 0.0;
  for (std::size_t i = 0; i < 9; ++i) result += data.irradianceSh[i * 4] * basis[i];
  return result;
}

[[nodiscard]] float binary16ToFloat(std::uint16_t value) {
  const std::uint32_t sign = static_cast<std::uint32_t>(value & 0x8000U) << 16U;
  const std::uint32_t exponent = (value >> 10U) & 0x1FU;
  const std::uint32_t mantissa = value & 0x03FFU;
  std::uint32_t bits = sign;
  if (exponent == 0) {
    if (mantissa != 0) {
      std::uint32_t normalized = mantissa;
      std::int32_t unbiased = -14;
      while ((normalized & 0x0400U) == 0) {
        normalized <<= 1U;
        --unbiased;
      }
      bits |= static_cast<std::uint32_t>(unbiased + 127) << 23U;
      bits |= (normalized & 0x03FFU) << 13U;
    }
  } else if (exponent == 0x1FU) {
    bits |= 0x7F800000U | (mantissa << 13U);
  } else {
    bits |= (exponent + 112U) << 23U;
    bits |= mantissa << 13U;
  }
  return std::bit_cast<float>(bits);
}

}  // namespace

TEST_CASE("binary16 conversion is deterministic and round-to-nearest-even", "[asset_system]") {
  CHECK(detail::floatToBinary16(0.0F) == 0x0000U);
  CHECK(detail::floatToBinary16(-0.0F) == 0x8000U);
  CHECK(detail::floatToBinary16(1.0F) == 0x3C00U);
  CHECK(detail::floatToBinary16(0.5F) == 0x3800U);
  CHECK(detail::floatToBinary16(65504.0F) == 0x7BFFU);
  CHECK(detail::floatToBinary16(std::numeric_limits<float>::infinity()) == 0x7C00U);
}

TEST_CASE("constant HDR environment produces direction-independent irradiance and specular data", "[asset_system]") {
  const auto source = constantSource(16, 8, 1.0F, 0.5F, 0.25F);
  const detail::EnvironmentProcessingSettings settings{4, 4, 64};
  const EnvironmentAssetData data = detail::preprocessEnvironment(source.data(), 16, 8, settings);
  CHECK(data.faceSize == 4);
  CHECK(data.mipCount == 3);
  CHECK(data.specularRgba16Float.size() == (16 + 4 + 1) * 6 * 4);
  CHECK(data.dfgRg16Float.size() == 4 * 4 * 2);
  CHECK(evaluateRedIrradiance(data, 1.0, 0.0, 0.0) == Catch::Approx(3.14159265).margin(0.04));
  CHECK(evaluateRedIrradiance(data, 0.0, 1.0, 0.0) == Catch::Approx(3.14159265).margin(0.04));
  for (std::size_t i = 0; i < data.specularRgba16Float.size(); i += 4) {
    CHECK(data.specularRgba16Float[i] == 0x3C00U);
    CHECK(data.specularRgba16Float[i + 1] == 0x3800U);
    CHECK(data.specularRgba16Float[i + 2] == 0x3400U);
    CHECK(data.specularRgba16Float[i + 3] == 0x3C00U);
  }
  for (std::uint16_t value : data.dfgRg16Float) {
    CHECK(std::isfinite(binary16ToFloat(value)));
    CHECK(binary16ToFloat(value) >= 0.0F);
    CHECK(binary16ToFloat(value) <= 1.0F);
  }
}

TEST_CASE("cubemap mip-zero face order maps to +X -X +Y -Y +Z -Z", "[asset_system]") {
  constexpr std::uint32_t width = 64;
  constexpr std::uint32_t height = 32;
  std::vector<float> source(static_cast<std::size_t>(width) * height * 4);
  constexpr double pi = 3.14159265358979323846;
  for (std::uint32_t y = 0; y < height; ++y) {
    const double theta = pi * (static_cast<double>(y) + 0.5) / height;
    for (std::uint32_t x = 0; x < width; ++x) {
      const double phi = 2.0 * pi * (static_cast<double>(x) + 0.5) / width - pi;
      const std::size_t index = (static_cast<std::size_t>(y) * width + x) * 4;
      source[index] = static_cast<float>(0.5 + 0.5 * std::sin(theta) * std::cos(phi));
      source[index + 1] = static_cast<float>(0.5 + 0.5 * std::cos(theta));
      source[index + 2] = static_cast<float>(0.5 + 0.5 * std::sin(theta) * std::sin(phi));
      source[index + 3] = 1.0F;
    }
  }
  const EnvironmentAssetData data = detail::preprocessEnvironment(source.data(), width, height, {1, 1, 32});
  REQUIRE(data.specularRgba16Float.size() == 6 * 4);
  const auto channel = [&data](std::size_t face, std::size_t component) {
    return binary16ToFloat(data.specularRgba16Float[face * 4 + component]);
  };
  CHECK(channel(0, 0) > 0.95F);
  CHECK(channel(1, 0) < 0.05F);
  CHECK(channel(2, 1) > 0.95F);
  CHECK(channel(3, 1) < 0.05F);
  CHECK(channel(4, 2) > 0.95F);
  CHECK(channel(5, 2) < 0.05F);
}

TEST_CASE("environment preprocessing is byte deterministic", "[asset_system]") {
  const auto source = constantSource(8, 4, 0.75F, 1.0F, 2.0F);
  const detail::EnvironmentProcessingSettings settings{2, 2, 32};
  const EnvironmentAssetData first = detail::preprocessEnvironment(source.data(), 8, 4, settings);
  const EnvironmentAssetData second = detail::preprocessEnvironment(source.data(), 8, 4, settings);
  CHECK(first.irradianceSh == second.irradianceSh);
  CHECK(first.specularRgba16Float == second.specularRgba16Float);
  CHECK(first.dfgRg16Float == second.dfgRg16Float);
}
