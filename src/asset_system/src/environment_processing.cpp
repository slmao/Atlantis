#include "environment_processing.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <limits>

namespace atlantis::asset_system::detail {

namespace {

constexpr double kPi = 3.14159265358979323846264338327950288;

struct Vec3 {
  double x;
  double y;
  double z;
};

[[nodiscard]] Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
[[nodiscard]] Vec3 operator*(Vec3 value, double scale) { return {value.x * scale, value.y * scale, value.z * scale}; }
[[nodiscard]] double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
[[nodiscard]] Vec3 cross(Vec3 a, Vec3 b) {
  return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
[[nodiscard]] Vec3 normalize(Vec3 value) {
  const double inverseLength = 1.0 / std::sqrt(dot(value, value));
  return value * inverseLength;
}

[[nodiscard]] Vec3 cubeDirection(std::uint32_t face, std::uint32_t x, std::uint32_t y, std::uint32_t size) {
  const double u = 2.0 * (static_cast<double>(x) + 0.5) / static_cast<double>(size) - 1.0;
  const double v = 2.0 * (static_cast<double>(y) + 0.5) / static_cast<double>(size) - 1.0;
  switch (face) {
    case 0:
      return normalize({1.0, -v, -u});
    case 1:
      return normalize({-1.0, -v, u});
    case 2:
      return normalize({u, 1.0, v});
    case 3:
      return normalize({u, -1.0, -v});
    case 4:
      return normalize({u, -v, 1.0});
    case 5:
      return normalize({-u, -v, -1.0});
  }
  return {0.0, 1.0, 0.0};
}

[[nodiscard]] std::array<double, 3> sampleEquirectangular(const float* pixels, std::uint32_t width,
                                                           std::uint32_t height, Vec3 direction) {
  double u = std::atan2(direction.z, direction.x) / (2.0 * kPi) + 0.5;
  u -= std::floor(u);
  const double v = std::acos(std::clamp(direction.y, -1.0, 1.0)) / kPi;
  const double sourceX = u * static_cast<double>(width) - 0.5;
  const double sourceY = v * static_cast<double>(height) - 0.5;
  const std::int64_t x0Unwrapped = static_cast<std::int64_t>(std::floor(sourceX));
  const std::int64_t y0Unclamped = static_cast<std::int64_t>(std::floor(sourceY));
  const double tx = sourceX - std::floor(sourceX);
  const double ty = sourceY - std::floor(sourceY);
  const auto wrapX = [width](std::int64_t x) {
    const std::int64_t width64 = static_cast<std::int64_t>(width);
    return static_cast<std::uint32_t>((x % width64 + width64) % width64);
  };
  const auto clampY = [height](std::int64_t y) {
    return static_cast<std::uint32_t>(std::clamp<std::int64_t>(y, 0, static_cast<std::int64_t>(height) - 1));
  };
  const std::uint32_t xs[2] = {wrapX(x0Unwrapped), wrapX(x0Unwrapped + 1)};
  const std::uint32_t ys[2] = {clampY(y0Unclamped), clampY(y0Unclamped + 1)};
  std::array<double, 3> result{};
  for (std::uint32_t iy = 0; iy < 2; ++iy) {
    for (std::uint32_t ix = 0; ix < 2; ++ix) {
      const double weight = (ix == 0 ? 1.0 - tx : tx) * (iy == 0 ? 1.0 - ty : ty);
      const std::size_t index = (static_cast<std::size_t>(ys[iy]) * width + xs[ix]) * 4;
      for (std::size_t channel = 0; channel < 3; ++channel) result[channel] += pixels[index + channel] * weight;
    }
  }
  return result;
}

[[nodiscard]] std::array<double, 9> shBasis(Vec3 d) {
  return {0.282095,
          0.488603 * d.z,
          0.488603 * d.y,
          0.488603 * d.x,
          1.092548 * d.x * d.z,
          1.092548 * d.z * d.y,
          0.315392 * (3.0 * d.y * d.y - 1.0),
          1.092548 * d.x * d.y,
          0.546274 * (d.x * d.x - d.z * d.z)};
}

void projectIrradianceSh(const float* pixels, std::uint32_t width, std::uint32_t height,
                         std::array<float, 36>& output) {
  std::array<std::array<double, 3>, 9> sums{};
  double weightSum = 0.0;
  for (std::uint32_t y = 0; y < height; ++y) {
    const double theta = kPi * (static_cast<double>(y) + 0.5) / static_cast<double>(height);
    const double weight = std::sin(theta);
    for (std::uint32_t x = 0; x < width; ++x) {
      const double phi = 2.0 * kPi * (static_cast<double>(x) + 0.5) / static_cast<double>(width) - kPi;
      const Vec3 direction{std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
      const auto basis = shBasis(direction);
      const std::size_t source = (static_cast<std::size_t>(y) * width + x) * 4;
      for (std::size_t coefficient = 0; coefficient < basis.size(); ++coefficient) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
          sums[coefficient][channel] += static_cast<double>(pixels[source + channel]) * basis[coefficient] * weight;
        }
      }
      weightSum += weight;
    }
  }
  const double solidAngleScale = 4.0 * kPi / weightSum;
  for (std::size_t coefficient = 0; coefficient < sums.size(); ++coefficient) {
    const double convolution = coefficient == 0 ? kPi : (coefficient <= 3 ? 2.0 * kPi / 3.0 : kPi / 4.0);
    for (std::size_t channel = 0; channel < 3; ++channel) {
      output[coefficient * 4 + channel] = static_cast<float>(sums[coefficient][channel] * solidAngleScale * convolution);
    }
    output[coefficient * 4 + 3] = 0.0F;
  }
}

[[nodiscard]] double radicalInverse(std::uint32_t bits) {
  bits = (bits << 16U) | (bits >> 16U);
  bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
  bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
  bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
  bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
  return static_cast<double>(bits) * 2.3283064365386963e-10;
}

[[nodiscard]] Vec3 importanceSampleGgx(std::uint32_t sample, std::uint32_t sampleCount, double alpha, Vec3 normal) {
  const double xiX = static_cast<double>(sample) / static_cast<double>(sampleCount);
  const double xiY = radicalInverse(sample);
  const double phi = 2.0 * kPi * xiX;
  const double alphaSquared = alpha * alpha;
  const double cosTheta = std::sqrt((1.0 - xiY) / (1.0 + (alphaSquared - 1.0) * xiY));
  const double sinTheta = std::sqrt(std::max(0.0, 1.0 - cosTheta * cosTheta));
  const Vec3 tangentH{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, cosTheta};
  const Vec3 up = std::abs(normal.z) < 0.999 ? Vec3{0.0, 0.0, 1.0} : Vec3{1.0, 0.0, 0.0};
  const Vec3 tangent = normalize(cross(up, normal));
  const Vec3 bitangent = cross(normal, tangent);
  return normalize(tangent * tangentH.x + bitangent * tangentH.y + normal * tangentH.z);
}

[[nodiscard]] std::array<double, 3> prefilterDirection(const float* pixels, std::uint32_t width,
                                                       std::uint32_t height, Vec3 direction, double roughness,
                                                       std::uint32_t sampleCount) {
  if (roughness == 0.0) return sampleEquirectangular(pixels, width, height, direction);
  const double alpha = std::max(roughness * roughness, 1.0e-3);
  std::array<double, 3> sum{};
  double weightSum = 0.0;
  for (std::uint32_t sample = 0; sample < sampleCount; ++sample) {
    const Vec3 halfVector = importanceSampleGgx(sample, sampleCount, alpha, direction);
    const Vec3 light = normalize(halfVector * (2.0 * dot(direction, halfVector)) + direction * -1.0);
    const double nDotL = std::max(dot(direction, light), 0.0);
    if (nDotL <= 0.0) continue;
    const auto radiance = sampleEquirectangular(pixels, width, height, light);
    for (std::size_t channel = 0; channel < 3; ++channel) sum[channel] += radiance[channel] * nDotL;
    weightSum += nDotL;
  }
  if (weightSum > 0.0) {
    for (double& channel : sum) channel /= weightSum;
  }
  return sum;
}

[[nodiscard]] double geometrySchlickGgx(double nDotDirection, double alpha) {
  const double k = alpha * 0.5;
  return nDotDirection / (nDotDirection * (1.0 - k) + k);
}

[[nodiscard]] std::array<double, 2> integrateDfg(double nDotV, double roughness, std::uint32_t sampleCount) {
  const Vec3 normal{0.0, 0.0, 1.0};
  const Vec3 view{std::sqrt(std::max(0.0, 1.0 - nDotV * nDotV)), 0.0, nDotV};
  const double alpha = std::max(roughness * roughness, 1.0e-3);
  std::array<double, 2> result{};
  for (std::uint32_t sample = 0; sample < sampleCount; ++sample) {
    const Vec3 halfVector = importanceSampleGgx(sample, sampleCount, alpha, normal);
    const Vec3 light = normalize(halfVector * (2.0 * dot(view, halfVector)) + view * -1.0);
    const double nDotL = std::max(light.z, 0.0);
    const double nDotH = std::max(halfVector.z, 0.0);
    const double vDotH = std::max(dot(view, halfVector), 0.0);
    if (nDotL <= 0.0 || nDotH <= 0.0 || vDotH <= 0.0) continue;
    const double geometry = geometrySchlickGgx(nDotV, alpha) * geometrySchlickGgx(nDotL, alpha);
    const double visibility = geometry * vDotH / (nDotH * nDotV);
    const double fresnel = std::pow(1.0 - vDotH, 5.0);
    result[0] += (1.0 - fresnel) * visibility;
    result[1] += fresnel * visibility;
  }
  result[0] /= sampleCount;
  result[1] /= sampleCount;
  return result;
}

}  // namespace

std::uint16_t floatToBinary16(float value) noexcept {
  const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
  const std::uint32_t sign = (bits >> 16U) & 0x8000U;
  const std::uint32_t exponent = (bits >> 23U) & 0xFFU;
  const std::uint32_t mantissa = bits & 0x7FFFFFU;
  if (exponent == 0xFFU) return static_cast<std::uint16_t>(sign | 0x7C00U | (mantissa != 0 ? 0x0200U : 0U));
  const std::int32_t halfExponent = static_cast<std::int32_t>(exponent) - 127 + 15;
  if (halfExponent >= 31) return static_cast<std::uint16_t>(sign | 0x7C00U);
  if (halfExponent <= 0) {
    if (halfExponent < -10) return static_cast<std::uint16_t>(sign);
    const std::uint32_t significand = mantissa | 0x800000U;
    const std::uint32_t shift = static_cast<std::uint32_t>(14 - halfExponent);
    std::uint32_t rounded = significand >> shift;
    const std::uint32_t remainder = significand & ((1U << shift) - 1U);
    const std::uint32_t halfway = 1U << (shift - 1U);
    if (remainder > halfway || (remainder == halfway && (rounded & 1U) != 0)) ++rounded;
    return static_cast<std::uint16_t>(sign | rounded);
  }
  std::uint32_t roundedMantissa = mantissa >> 13U;
  const std::uint32_t remainder = mantissa & 0x1FFFU;
  if (remainder > 0x1000U || (remainder == 0x1000U && (roundedMantissa & 1U) != 0)) {
    ++roundedMantissa;
    if (roundedMantissa == 0x400U) {
      roundedMantissa = 0;
      if (halfExponent + 1 >= 31) return static_cast<std::uint16_t>(sign | 0x7C00U);
      return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(halfExponent + 1) << 10U));
    }
  }
  return static_cast<std::uint16_t>(sign | (static_cast<std::uint32_t>(halfExponent) << 10U) | roundedMantissa);
}

EnvironmentAssetData preprocessEnvironment(const float* rgbaPixels, std::uint32_t width, std::uint32_t height,
                                           const EnvironmentProcessingSettings& settings) {
  EnvironmentAssetData data;
  data.faceSize = settings.faceSize;
  data.mipCount = 0;
  for (std::uint32_t size = settings.faceSize; size != 0; size >>= 1U) ++data.mipCount;
  data.dfgWidth = settings.dfgSize;
  data.dfgHeight = settings.dfgSize;
  projectIrradianceSh(rgbaPixels, width, height, data.irradianceSh);

  for (std::uint32_t mip = 0, mipSize = settings.faceSize; mip < data.mipCount;
       ++mip, mipSize = std::max(1U, mipSize / 2U)) {
    const double roughness = data.mipCount > 1U
                                 ? static_cast<double>(mip) / static_cast<double>(data.mipCount - 1U)
                                 : 0.0;
    for (std::uint32_t face = 0; face < 6; ++face) {
      for (std::uint32_t y = 0; y < mipSize; ++y) {
        for (std::uint32_t x = 0; x < mipSize; ++x) {
          const auto radiance = prefilterDirection(rgbaPixels, width, height, cubeDirection(face, x, y, mipSize),
                                                   roughness, settings.sampleCount);
          for (double channel : radiance) data.specularRgba16Float.push_back(floatToBinary16(static_cast<float>(channel)));
          data.specularRgba16Float.push_back(floatToBinary16(1.0F));
        }
      }
    }
  }

  for (std::uint32_t y = 0; y < settings.dfgSize; ++y) {
    const double roughness = (static_cast<double>(y) + 0.5) / static_cast<double>(settings.dfgSize);
    for (std::uint32_t x = 0; x < settings.dfgSize; ++x) {
      const double nDotV = std::max((static_cast<double>(x) + 0.5) / static_cast<double>(settings.dfgSize), 1.0e-4);
      const auto dfg = integrateDfg(nDotV, roughness, settings.sampleCount);
      data.dfgRg16Float.push_back(floatToBinary16(static_cast<float>(dfg[0])));
      data.dfgRg16Float.push_back(floatToBinary16(static_cast<float>(dfg[1])));
    }
  }
  return data;
}

}  // namespace atlantis::asset_system::detail
