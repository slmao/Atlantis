#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/result.h>

#include <filesystem>

namespace atlantis::asset_system {

// Plan 0018 Section P4: reads the runtime material artifact at
// artifactPath and its metadata sidecar at metadataPath, decodes/parses
// both, cross-checks that they agree (kind, texture_asset_id --
// MetadataArtifactMismatch if not), independently re-derives the
// metadata's own self-consistency (asset_id vs. source_logical_path via
// computeAssetId(), mirroring loadTextureAsset()'s own precedent), and
// returns CPU-side MaterialAssetData. Names no RHI type anywhere in this
// file. A composition root outside Asset System is responsible for
// resolving textureAsset (via loadTextureAsset()) and constructing any
// RHI Sampler/SampledTexture/Material from the result.
[[nodiscard]] atlantis::Result<MaterialAssetData, MaterialLoadError> loadMaterialAsset(
    const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath);

}  // namespace atlantis::asset_system
