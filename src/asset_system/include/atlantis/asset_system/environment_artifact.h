#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/environment_types.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace atlantis::asset_system {

inline constexpr std::uint32_t kEnvironmentArtifactSchemaVersion = 1;
inline constexpr std::size_t kEnvironmentArtifactHeaderSizeBytes = 60;
inline constexpr std::size_t kEnvironmentShPayloadSizeBytes = 9 * 4 * sizeof(float);
inline constexpr std::uint32_t kMaxEnvironmentDimension = 4096;

struct DecodedEnvironmentArtifact {
  AssetId assetId = 0;
  EnvironmentAssetData data;
};

// Input is already validated cooker output. Serialization is explicitly
// little-endian and never copies a native C++ struct representation.
[[nodiscard]] std::vector<std::byte> encodeEnvironmentArtifact(AssetId assetId,
                                                                const EnvironmentAssetData& data);

[[nodiscard]] atlantis::Result<DecodedEnvironmentArtifact, EnvironmentArtifactDecodeError>
decodeEnvironmentArtifact(const std::vector<std::byte>& bytes);

}  // namespace atlantis::asset_system
