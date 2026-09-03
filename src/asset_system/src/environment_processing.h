#pragma once

#include <atlantis/asset_system/environment_types.h>

#include <cstdint>

namespace atlantis::asset_system::detail {

struct EnvironmentProcessingSettings {
  std::uint32_t faceSize;
  std::uint32_t dfgSize;
  std::uint32_t sampleCount;
};

[[nodiscard]] EnvironmentAssetData preprocessEnvironment(const float* rgbaPixels, std::uint32_t width,
                                                          std::uint32_t height,
                                                          const EnvironmentProcessingSettings& settings);

[[nodiscard]] std::uint16_t floatToBinary16(float value) noexcept;

}  // namespace atlantis::asset_system::detail
