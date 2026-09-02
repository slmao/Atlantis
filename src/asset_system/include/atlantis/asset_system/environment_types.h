#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

inline constexpr std::uint32_t kEnvironmentShCoefficientCount = 9;
inline constexpr std::uint32_t kEnvironmentShComponentCount = 4;

// CPU-only environment data. Half-float payloads retain their exact artifact
// bit patterns so Runtime can upload them without a lossy decode/re-encode.
struct EnvironmentAssetData {
  std::uint32_t faceSize = 0;
  std::uint32_t mipCount = 0;
  std::uint32_t dfgWidth = 0;
  std::uint32_t dfgHeight = 0;
  std::array<float, kEnvironmentShCoefficientCount * kEnvironmentShComponentCount> irradianceSh{};
  std::vector<std::uint16_t> specularRgba16Float;
  std::vector<std::uint16_t> dfgRg16Float;
};

}  // namespace atlantis::asset_system
