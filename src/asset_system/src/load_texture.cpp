#include <atlantis/asset_system/load_texture.h>

#include "preview_file.h"

#include <atlantis/asset_system/asset_catalog.h>
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

atlantis::Result<TextureAssetData, TextureLoadError> decodeLoadedTexture(
    const std::vector<std::byte>& artifactBytes, std::string_view metadataText,
    const AssetCatalogRecord* expected = nullptr) {
  using ResultT = atlantis::Result<TextureAssetData, TextureLoadError>;

  auto artifactResult = decodeTextureArtifact(artifactBytes);
  if (artifactResult.isErr()) return ResultT::Err(TextureLoadError::ArtifactDecodeFailed);

  const auto metadataResult = parseTextureMetadata(metadataText);
  if (metadataResult.isErr()) return ResultT::Err(TextureLoadError::MetadataParseFailed);

  DecodedTextureArtifact& artifact = artifactResult.value();
  const TextureMetadata& metadata = metadataResult.value();

  if (expected && (expected->guid != metadata.assetGuid || expected->assetId != metadata.assetId)) {
    return ResultT::Err(TextureLoadError::MetadataArtifactMismatch);
  }
  if (artifact.width != metadata.width || artifact.height != metadata.height ||
      artifact.colorSpace != metadata.format || artifact.layout != metadata.layout ||
      artifact.mipCount != metadata.mipCount) {
    return ResultT::Err(TextureLoadError::MetadataArtifactMismatch);
  }

  // Self-consistency, not just artifact-vs-metadata agreement: the
  // sidecar's Asset ID must be the key of its GUID (Plan 0047 P8) --
  // mirrors loadStaticMeshAsset()'s own identical check.
  if (metadata.assetId != assetKey(metadata.assetGuid)) {
    return ResultT::Err(TextureLoadError::MetadataArtifactMismatch);
  }

  TextureAssetData data;
  data.width = artifact.width;
  data.height = artifact.height;
  data.colorSpace = artifact.colorSpace;
  data.layout = artifact.layout;
  data.mipCount = artifact.mipCount;
  data.pixelBytes = std::move(artifact.pixelBytes);
  return ResultT::Ok(std::move(data));
}

}  // namespace

atlantis::Result<TextureAssetData, TextureLoadError> loadTextureAsset(
    const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath) {
  using R = atlantis::Result<TextureAssetData, TextureLoadError>;
  std::vector<std::byte> artifactBytes;
  if (!readFileBytes(artifactPath, artifactBytes)) return R::Err(TextureLoadError::ArtifactDecodeFailed);

  std::string metadataText;
  if (!readFileText(metadataPath, metadataText)) return R::Err(TextureLoadError::MetadataReadFailed);
  return decodeLoadedTexture(artifactBytes, metadataText);
}

atlantis::Result<TextureAssetData, PreviewArtifactLoadError> loadPreviewBaseColorTexture(
    const AssetCatalogRecord& record) {
  using E = PreviewArtifactLoadError;
  using R = atlantis::Result<TextureAssetData, E>;
  if (record.type != CatalogAssetType::Texture) return R::Err(E::Invalid);
  std::vector<std::byte> artifactBytes;
  if (const auto error = detail::readPreviewFile(record.artifact, kPreviewBaseColorMaxArtifactBytes, artifactBytes)) {
    return R::Err(*error);
  }
  const auto header = peekTextureArtifactHeader(artifactBytes);
  if (header.isErr() || header.value().schemaVersion != record.artifactSchema) return R::Err(E::Invalid);
  if (header.value().width > kPreviewBaseColorMaxDimension || header.value().height > kPreviewBaseColorMaxDimension) {
    return R::Err(E::TooLarge);
  }

  std::string metadataText;
  if (const auto error = detail::readPreviewFile(record.metadata, kPreviewMeshMaxMetadataBytes, metadataText)) {
    return R::Err(*error);
  }
  auto decoded = decodeLoadedTexture(artifactBytes, metadataText, &record);
  if (decoded.isErr()) return R::Err(E::Invalid);
  return R::Ok(std::move(decoded.value()));
}

}  // namespace atlantis::asset_system
