#pragma once

#include <atlantis/asset_system/asset_guid.h>
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

// Plan 0047 P7 (ADR-0097 D1/D2): assetGuid is the asset's persistent
// identity, resolved by the caller (the cooker, from the catalog source or
// a cook-manifest --guid=); it must not be nil. The Asset ID written into
// the artifact and sidecar is assetKey(assetGuid); logicalPathInput is
// normalized and recorded as provenance only.
// rgbaPixels contains width*height tightly packed float RGBA texels decoded
// from a linear Radiance HDR source. Alpha is ignored. Not thread-safe with
// concurrent mutation of the source buffer or output paths.
[[nodiscard]] atlantis::Result<std::monostate, EnvironmentCookError> cookEnvironment(
    const float* rgbaPixels, std::uint32_t width, std::uint32_t height, const std::string& logicalPathInput,
    const AssetGuid& assetGuid, const std::filesystem::path& artifactOutputPath,
    const std::filesystem::path& metadataOutputPath);

}  // namespace atlantis::asset_system
