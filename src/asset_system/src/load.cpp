#include <atlantis/asset_system/load.h>

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
  // metadata sidecar's own two fields (its recorded Asset ID and its
  // recorded source path) must agree with each other too. Without this,
  // a metadata file whose assetId happens to match the artifact's own
  // header (checked above) but whose sourceLogicalPath does not
  // actually hash to that assetId -- individually parseable, internally
  // contradictory -- would be silently accepted.
  return metadata.assetId == computeAssetId(metadata.sourceLogicalPath);
}

[[nodiscard]] std::uint32_t vertexCountOf(std::uint32_t vertexStrideBytes, std::size_t vertexByteCount) {
  return vertexStrideBytes != 0 ? static_cast<std::uint32_t>(vertexByteCount / vertexStrideBytes) : 0;
}

}  // namespace

atlantis::Result<StaticMeshAssetData, AssetLoadError> loadStaticMeshAsset(const std::string& artifactPath,
                                                                           const std::string& metadataPath) {
  using ResultT = atlantis::Result<StaticMeshAssetData, AssetLoadError>;

  std::vector<std::byte> artifactBytes;
  if (!readFileBytes(artifactPath, artifactBytes)) return ResultT::Err(AssetLoadError::ArtifactFileUnreadable);

  std::string metadataText;
  if (!readFileText(metadataPath, metadataText)) return ResultT::Err(AssetLoadError::MetadataFileUnreadable);

  const auto metadataResult = parseAssetMetadata(metadataText);
  if (metadataResult.isErr()) return ResultT::Err(AssetLoadError::MetadataParseFailed);
  const AssetMetadata& metadata = metadataResult.value();

  // Spec 0039 Requirement 4 / ADR-0087 Decision item 3: the artifact's
  // own header decides which decoder runs. Not a caller-supplied hint,
  // not the filename, not a manifest field -- the scene dependency
  // manifest records paths and AssetIds and deliberately never records a
  // schema version (ruling O5), so this is the only place that knows.
  const auto headerResult = peekMeshArtifactHeader(artifactBytes);
  if (headerResult.isErr()) return ResultT::Err(AssetLoadError::ArtifactDecodeFailed);

  if (headerResult.value().schemaVersion == kMeshArtifactSchemaVersionU32) {
    // Spec 0039 ruling O1 / Plan 0039 P2, applied to the header before
    // the decode, not after it: decodeMeshArtifactU32() rejects every
    // index >= vertex_count, so bounding the vertex count bounds every
    // index value in the artifact exactly. One comparison, no per-index
    // pass, and no gigabyte decoded only to be thrown away.
    if (headerResult.value().vertexCount > kMaxDrawableVertexCount) {
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

}  // namespace atlantis::asset_system
