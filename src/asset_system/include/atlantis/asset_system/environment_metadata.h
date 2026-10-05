#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace atlantis::asset_system {

struct EnvironmentMetadata {
  AssetGuid assetGuid;  // Plan 0047 P8: line 2 of the sidecar
  AssetId assetId = 0;
  std::string sourceLogicalPath;
  std::uint32_t faceSize = 0;
  std::uint32_t mipCount = 0;
  std::uint32_t dfgWidth = 0;
  std::uint32_t dfgHeight = 0;
};

[[nodiscard]] atlantis::Result<EnvironmentMetadata, MetadataParseError> parseEnvironmentMetadata(
    std::string_view text);
[[nodiscard]] std::string serializeEnvironmentMetadata(const EnvironmentMetadata& metadata);

}  // namespace atlantis::asset_system
