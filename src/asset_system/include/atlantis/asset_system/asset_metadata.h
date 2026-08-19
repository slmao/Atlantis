#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <cstdint>
#include <string>
#include <string_view>

namespace atlantis::asset_system {

// Plan 0012 Section D3 / ADR-0044: the metadata sidecar's field
// semantics. Wire encoding (strict, anchored-prefix, versioned flat
// text, exactly 8 lines) matches ADR-0042's own sidecar *pattern*; this
// module's parser is new, independent code -- never a dependency on, or
// shared implementation with,
// tests/image_regression/support/provenance.*.
struct AssetMetadata {
  AssetId assetId = 0;
  std::string sourceLogicalPath;
  std::string importerVersion;
  std::string assetType;
  std::uint32_t vertexCount = 0;
  std::uint32_t indexCount = 0;
  std::uint32_t vertexStrideBytes = 0;
};

[[nodiscard]] atlantis::Result<AssetMetadata, MetadataParseError> parseAssetMetadata(std::string_view text);
[[nodiscard]] std::string serializeAssetMetadata(const AssetMetadata& metadata);

}  // namespace atlantis::asset_system
