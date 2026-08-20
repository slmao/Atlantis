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

}  // namespace

atlantis::Result<StaticMeshAssetData, AssetLoadError> loadStaticMeshAsset(const std::string& artifactPath,
                                                                           const std::string& metadataPath) {
  using ResultT = atlantis::Result<StaticMeshAssetData, AssetLoadError>;

  std::vector<std::byte> artifactBytes;
  if (!readFileBytes(artifactPath, artifactBytes)) return ResultT::Err(AssetLoadError::ArtifactFileUnreadable);

  std::string metadataText;
  if (!readFileText(metadataPath, metadataText)) return ResultT::Err(AssetLoadError::MetadataFileUnreadable);

  auto artifactResult = decodeMeshArtifact(artifactBytes);
  if (artifactResult.isErr()) return ResultT::Err(AssetLoadError::ArtifactDecodeFailed);

  const auto metadataResult = parseAssetMetadata(metadataText);
  if (metadataResult.isErr()) return ResultT::Err(AssetLoadError::MetadataParseFailed);

  DecodedMeshArtifact& artifact = artifactResult.value();
  const AssetMetadata& metadata = metadataResult.value();

  const std::uint32_t artifactVertexCount =
      artifact.vertexStrideBytes != 0
          ? static_cast<std::uint32_t>(artifact.vertexBytes.size() / artifact.vertexStrideBytes)
          : 0;
  const auto artifactIndexCount = static_cast<std::uint32_t>(artifact.indices.size());

  if (artifact.assetId != metadata.assetId || artifact.vertexStrideBytes != metadata.vertexStrideBytes ||
      artifactVertexCount != metadata.vertexCount || artifactIndexCount != metadata.indexCount) {
    return ResultT::Err(AssetLoadError::MetadataArtifactMismatch);
  }

  // Self-consistency, not just artifact-vs-metadata agreement: the
  // metadata sidecar's own two fields (its recorded Asset ID and its
  // recorded source path) must agree with each other too. Without this,
  // a metadata file whose assetId happens to match the artifact's own
  // header (checked above) but whose sourceLogicalPath does not
  // actually hash to that assetId -- individually parseable, internally
  // contradictory -- would be silently accepted.
  if (metadata.assetId != computeAssetId(metadata.sourceLogicalPath)) {
    return ResultT::Err(AssetLoadError::MetadataArtifactMismatch);
  }

  return ResultT::Ok(
      StaticMeshAssetData(std::move(artifact.vertexBytes), std::move(artifact.indices), artifact.vertexStrideBytes));
}

}  // namespace atlantis::asset_system
