#include <atlantis/asset_system/load_material.h>

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/material_metadata.h>

#include <fstream>
#include <sstream>

namespace atlantis::asset_system {

namespace {

// Duplicated from load_texture.cpp's own identical helpers rather than
// shared, matching that file's own file-local, not-exported precedent.
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

atlantis::Result<MaterialAssetData, MaterialLoadError> loadMaterialAsset(const std::filesystem::path& artifactPath,
                                                                          const std::filesystem::path& metadataPath) {
  using ResultT = atlantis::Result<MaterialAssetData, MaterialLoadError>;

  std::vector<std::byte> artifactBytes;
  if (!readFileBytes(artifactPath, artifactBytes)) return ResultT::Err(MaterialLoadError::ArtifactFileUnreadable);

  std::string metadataText;
  if (!readFileText(metadataPath, metadataText)) return ResultT::Err(MaterialLoadError::MetadataFileUnreadable);

  auto artifactResult = decodeMaterialArtifact(artifactBytes);
  if (artifactResult.isErr()) return ResultT::Err(MaterialLoadError::ArtifactDecodeFailed);

  const auto metadataResult = parseMaterialMetadata(metadataText);
  if (metadataResult.isErr()) return ResultT::Err(MaterialLoadError::MetadataParseFailed);

  const DecodedMaterialArtifact& artifact = artifactResult.value();
  const MaterialMetadata& metadata = metadataResult.value();

  if (artifact.kind != metadata.kind || artifact.textureAsset != metadata.textureAsset) {
    return ResultT::Err(MaterialLoadError::MetadataArtifactMismatch);
  }

  // Self-consistency, not just artifact-vs-metadata agreement: the
  // metadata sidecar's own two fields (its recorded Asset ID and its
  // recorded source path) must agree with each other too -- mirrors
  // loadTextureAsset()'s own identical check.
  if (metadata.assetId != computeAssetId(metadata.sourceLogicalPath)) {
    return ResultT::Err(MaterialLoadError::MetadataArtifactMismatch);
  }

  MaterialAssetData data;
  data.kind = artifact.kind;
  data.textureAsset = artifact.textureAsset;
  data.filter = artifact.filter;
  data.addressMode = artifact.addressMode;
  return ResultT::Ok(std::move(data));
}

}  // namespace atlantis::asset_system
