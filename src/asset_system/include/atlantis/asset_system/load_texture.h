#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace atlantis::asset_system {

// Plan 0016 Section D8: reads the runtime texture artifact at
// artifactPath and its metadata sidecar at metadataPath, decodes/parses
// both, cross-checks that they agree (width, height, format --
// MetadataArtifactMismatch if not), independently re-derives the
// metadata's own self-consistency (asset_id vs. assetKey(asset_guid),
// Plan 0047 P8, mirroring loadStaticMeshAsset()'s own precedent),
// and returns CPU-side TextureAssetData. Returns CPU data only -- no RHI
// type is named, included, or constructed anywhere in this file. A
// composition root outside Asset System is responsible for passing the
// result into atlantis::rhi::Device::createSampledTexture()/
// copyBufferToTexture().
[[nodiscard]] atlantis::Result<TextureAssetData, TextureLoadError> loadTextureAsset(
    const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath);

struct AssetCatalogRecord;
enum class PreviewArtifactLoadError;
inline constexpr std::size_t kPreviewBaseColorMaxArtifactBytes = 32 * 1024 * 1024;
inline constexpr std::uint32_t kPreviewBaseColorMaxDimension = 2048;

// Spec 0092 R2: validates the bounded catalog texture before returning its
// pixels. The preview host checks that the cooked layout is supported.
[[nodiscard]] atlantis::Result<TextureAssetData, PreviewArtifactLoadError> loadPreviewBaseColorTexture(
    const AssetCatalogRecord& record);

}  // namespace atlantis::asset_system
