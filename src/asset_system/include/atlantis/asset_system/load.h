#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/static_mesh_asset_data.h>
#include <atlantis/result.h>

#include <string>

namespace atlantis::asset_system {

// Plan 0012 Step 5 / ADR-0043: reads the runtime artifact at
// artifactPath and its metadata sidecar at metadataPath, decodes/parses
// both, cross-checks that they agree (asset_id, vertex_count,
// index_count, vertex_stride_bytes -- MetadataArtifactMismatch if not),
// and returns CPU-side StaticMeshAssetData. Returns CPU data only -- no
// RHI type is named, included, or constructed anywhere in this file. A
// composition root outside Asset System is responsible for passing the
// result into atlantis::renderer::createMesh().
[[nodiscard]] atlantis::Result<StaticMeshAssetData, AssetLoadError> loadStaticMeshAsset(
    const std::string& artifactPath, const std::string& metadataPath);

}  // namespace atlantis::asset_system
