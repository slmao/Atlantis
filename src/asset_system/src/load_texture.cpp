#include <atlantis/asset_system/load_texture.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/asset_system/texture_metadata.h>

#include <fstream>
#include <sstream>

namespace atlantis::asset_system {

namespace {

[[nodiscard]] bool readFileBytes(const std::filesystem::path& path, std::vector<std::byte>& out) {
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

[[nodiscard]] bool readFileText(const std::filesystem::path& path, std::string& out) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) return false;
  std::ostringstream buffer;
  buffer << file.rdbuf();
  if (file.bad()) return false;
  out = buffer.str();
  return true;
}

}  // namespace

atlantis::Result<TextureAssetData, TextureLoadError> loadTextureAsset(const std::filesystem::path& artifactPath,
                                                                        const std::filesystem::path& metadataPath) {
  using ResultT = atlantis::Result<TextureAssetData, TextureLoadError>;

  std::vector<std::byte> artifactBytes;
  if (!readFileBytes(artifactPath, artifactBytes)) return ResultT::Err(TextureLoadError::ArtifactDecodeFailed);

  std::string metadataText;
  if (!readFileText(metadataPath, metadataText)) return ResultT::Err(TextureLoadError::MetadataReadFailed);

  auto artifactResult = decodeTextureArtifact(artifactBytes);
  if (artifactResult.isErr()) return ResultT::Err(TextureLoadError::ArtifactDecodeFailed);

  const auto metadataResult = parseTextureMetadata(metadataText);
  if (metadataResult.isErr()) return ResultT::Err(TextureLoadError::MetadataParseFailed);

  DecodedTextureArtifact& artifact = artifactResult.value();
  const TextureMetadata& metadata = metadataResult.value();

  if (artifact.width != metadata.width || artifact.height != metadata.height ||
      artifact.colorSpace != metadata.format) {
    return ResultT::Err(TextureLoadError::MetadataArtifactMismatch);
  }

  // Self-consistency, not just artifact-vs-metadata agreement: the
  // metadata sidecar's own two fields (its recorded Asset ID and its
  // recorded source path) must agree with each other too -- mirrors
  // loadStaticMeshAsset()'s own identical check.
  if (metadata.assetId != computeAssetId(metadata.sourceLogicalPath)) {
    return ResultT::Err(TextureLoadError::MetadataArtifactMismatch);
  }

  TextureAssetData data;
  data.width = artifact.width;
  data.height = artifact.height;
  data.colorSpace = artifact.colorSpace;
  data.pixelBytes = std::move(artifact.pixelBytes);
  return ResultT::Ok(std::move(data));
}

}  // namespace atlantis::asset_system
