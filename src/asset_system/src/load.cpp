#include <atlantis/asset_system/load.h>

#include "preview_file.h"

#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/asset_metadata.h>
#include <atlantis/asset_system/mesh_artifact.h>

#include <fstream>
#include <sstream>

namespace atlantis::asset_system {

namespace {

[[nodiscard]] bool readFileBytes(const std::string& path, std::vector<std::byte>& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  file.seekg(0, std::ios::end);
  const auto size = file.tellg();
  if (size < 0) return false;
  out.resize(static_cast<std::size_t>(size));
  file.seekg(0, std::ios::beg);
  if (!out.empty()) {
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
  }
  return static_cast<bool>(file) || file.eof();
}

[[nodiscard]] bool readFileText(const std::string& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) return false;
  out = buffer.str();
  return true;
}

// Spec 0039/ADR-0087: the artifact/metadata cross-check both schema
// paths run, identically. Counts are passed in because each decoder
// yields its own index vector type; everything else is version-agnostic.
[[nodiscard]] bool metadataAgreesWithArtifact(const AssetMetadata& metadata, AssetId artifactAssetId,
                                                std::uint32_t artifactVertexStrideBytes,
                                                std::uint32_t artifactVertexCount,
                                                std::uint32_t artifactIndexCount) {
  if (artifactAssetId != metadata.assetId || artifactVertexStrideBytes != metadata.vertexStrideBytes ||
      artifactVertexCount != metadata.vertexCount || artifactIndexCount != metadata.indexCount) {
    return false;
  }

  // Self-consistency, not just artifact-vs-metadata agreement: the
  // sidecar's recorded Asset ID must be the key of its recorded GUID
  // (Plan 0047 P8, ADR-0097 D2). The source path is provenance only.
  return metadata.assetId == assetKey(metadata.assetGuid);
}

[[nodiscard]] std::uint32_t vertexCountOf(std::uint32_t vertexStrideBytes, std::size_t vertexByteCount) {
  return vertexStrideBytes != 0 ? static_cast<std::uint32_t>(vertexByteCount / vertexStrideBytes) : 0;
}

[[nodiscard]] atlantis::Result<StaticMeshAssetData, AssetLoadError> decodeLoadedMesh(
    const std::vector<std::byte>& artifactBytes, const AssetMetadata& metadata, const MeshArtifactHeaderPeek& header) {
  using ResultT = atlantis::Result<StaticMeshAssetData, AssetLoadError>;
  if (header.schemaVersion == kMeshArtifactSchemaVersionU32) {
    // Spec 0039 ruling O1 / Plan 0039 P2, applied to the header before
    // the decode, not after it: decodeMeshArtifactU32() rejects every
    // index >= vertex_count, so bounding the vertex count bounds every
    // index value in the artifact exactly. One comparison, no per-index
    // pass, and no gigabyte decoded only to be thrown away.
    if (header.vertexCount > kMaxDrawableVertexCount) {
      return ResultT::Err(AssetLoadError::IndexValueExceedsDrawableRange);
    }

    auto artifactResult = decodeMeshArtifactU32(artifactBytes);
    if (artifactResult.isErr()) return ResultT::Err(AssetLoadError::ArtifactDecodeFailed);
    DecodedMeshArtifactU32& artifact = artifactResult.value();

    const std::uint32_t artifactVertexCount =
        vertexCountOf(artifact.vertexStrideBytes, artifact.vertexBytes.size());

    if (!metadataAgreesWithArtifact(metadata, artifact.assetId, artifact.vertexStrideBytes, artifactVertexCount,
                                    static_cast<std::uint32_t>(artifact.indices.size()))) {
      return ResultT::Err(AssetLoadError::MetadataArtifactMismatch);
    }

    return ResultT::Ok(
        StaticMeshAssetData(std::move(artifact.vertexBytes), std::move(artifact.indices), artifact.vertexStrideBytes));
  }

  // Every other version, including an unknown one, goes to the schema-4
  // decoder, which rejects anything but 4 exactly as it always has.
  auto artifactResult = decodeMeshArtifact(artifactBytes);
  if (artifactResult.isErr()) return ResultT::Err(AssetLoadError::ArtifactDecodeFailed);
  DecodedMeshArtifact& artifact = artifactResult.value();

  const std::uint32_t artifactVertexCount = vertexCountOf(artifact.vertexStrideBytes, artifact.vertexBytes.size());

  // No drawable-range check on this path: a schema-4 index is a
  // std::uint16_t, so it cannot reach kMaxDrawableIndexValue.
  if (!metadataAgreesWithArtifact(metadata, artifact.assetId, artifact.vertexStrideBytes, artifactVertexCount,
                                  static_cast<std::uint32_t>(artifact.indices.size()))) {
    return ResultT::Err(AssetLoadError::MetadataArtifactMismatch);
  }

  return ResultT::Ok(
      StaticMeshAssetData(std::move(artifact.vertexBytes), std::move(artifact.indices), artifact.vertexStrideBytes));
}

}  // namespace

atlantis::Result<StaticMeshAssetData, AssetLoadError> loadStaticMeshAsset(const std::string& artifactPath,
                                                                      const std::string& metadataPath) {
  using ResultT = atlantis::Result<StaticMeshAssetData, AssetLoadError>;
  std::vector<std::byte> artifactBytes;
  if (!readFileBytes(artifactPath, artifactBytes)) return ResultT::Err(AssetLoadError::ArtifactFileUnreadable);

  std::string metadataText;
  if (!readFileText(metadataPath, metadataText)) return ResultT::Err(AssetLoadError::MetadataFileUnreadable);

  const auto metadata = parseAssetMetadata(metadataText);
  if (metadata.isErr()) return ResultT::Err(AssetLoadError::MetadataParseFailed);
  // ADR-0087: the header chooses the decoder; caller input is not a version hint.
  const auto header = peekMeshArtifactHeader(artifactBytes);
  if (header.isErr()) return ResultT::Err(AssetLoadError::ArtifactDecodeFailed);
  return decodeLoadedMesh(artifactBytes, metadata.value(), header.value());
}

atlantis::Result<StaticMeshAssetData, MeshPreviewLoadError> loadStaticMeshPreviewAsset(
    const AssetCatalogRecord& record) {
  using ResultT = atlantis::Result<StaticMeshAssetData, MeshPreviewLoadError>;
  if (record.type != CatalogAssetType::Mesh) return ResultT::Err(MeshPreviewLoadError::InvalidMesh);

  std::vector<std::byte> artifactBytes;
  const auto readError = [](PreviewArtifactLoadError error) {
    return error == PreviewArtifactLoadError::TooLarge ? MeshPreviewLoadError::TooLarge
                                                       : MeshPreviewLoadError::InvalidMesh;
  };
  if (const auto error = detail::readPreviewFile(record.artifact, kPreviewMeshMaxArtifactBytes, artifactBytes)) {
    return ResultT::Err(readError(*error));
  }
  std::string metadataText;
  if (const auto error = detail::readPreviewFile(record.metadata, kPreviewMeshMaxMetadataBytes, metadataText)) {
    return ResultT::Err(readError(*error));
  }
  const auto metadata = parseAssetMetadata(metadataText);
  const auto header = peekMeshArtifactHeader(artifactBytes);
  if (metadata.isErr() || header.isErr() || metadata.value().assetGuid != record.guid ||
      metadata.value().assetId != record.assetId || metadata.value().assetType != "static_mesh" ||
      header.value().schemaVersion != record.artifactSchema) {
    return ResultT::Err(MeshPreviewLoadError::InvalidMesh);
  }

  auto decoded = decodeLoadedMesh(artifactBytes, metadata.value(), header.value());
  if (decoded.isErr()) return ResultT::Err(MeshPreviewLoadError::InvalidMesh);
  if (decoded.value().indexCount() > kPreviewMeshMaxIndices) return ResultT::Err(MeshPreviewLoadError::TooLarge);
  return ResultT::Ok(std::move(decoded.value()));
}

}  // namespace atlantis::asset_system
