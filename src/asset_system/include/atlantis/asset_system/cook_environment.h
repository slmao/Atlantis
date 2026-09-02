#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <variant>

namespace atlantis::asset_system {

inline constexpr std::uint32_t kCookedEnvironmentFaceSize = 256;
inline constexpr std::uint32_t kCookedEnvironmentMipCount = 9;
inline constexpr std::uint32_t kCookedEnvironmentDfgSize = 128;
inline constexpr std::uint32_t kCookedEnvironmentSampleCount = 1024;

// rgbaPixels contains width*height tightly packed float RGBA texels decoded
// from a linear Radiance HDR source. Alpha is ignored. Not thread-safe with
// concurrent mutation of the source buffer or output paths.
[[nodiscard]] atlantis::Result<std::monostate, EnvironmentCookError> cookEnvironment(
    const float* rgbaPixels, std::uint32_t width, std::uint32_t height, const std::string& logicalPathInput,
    const std::filesystem::path& artifactOutputPath, const std::filesystem::path& metadataOutputPath);

}  // namespace atlantis::asset_system
