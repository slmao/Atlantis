#pragma once

#include <atlantis/asset_system/errors.h>
#include <atlantis/asset_system/static_mesh_asset_data.h>
#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
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

struct AssetCatalogRecord;
enum class MeshPreviewLoadError { InvalidMesh, TooLarge };
enum class PreviewArtifactLoadError { Invalid, TooLarge };
inline constexpr std::size_t kPreviewMeshMaxArtifactBytes = 32 * 1024 * 1024;
inline constexpr std::size_t kPreviewMeshMaxMetadataBytes = 64 * 1024;
inline constexpr std::uint32_t kPreviewMeshMaxIndices = 1'500'000;

// Spec 0091 R1/R3. Reads the resolved cooked files once, enforcing inclusive
// limits on their opened streams before allocation and checking their identity
// and schema against the retained catalog record. No GPU state is involved.
[[nodiscard]] atlantis::Result<StaticMeshAssetData, MeshPreviewLoadError> loadStaticMeshPreviewAsset(
    const AssetCatalogRecord& record);

}  // namespace atlantis::asset_system
