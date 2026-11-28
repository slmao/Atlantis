#include <atlantis/asset_system/load_environment.h>

#include "preview_file.h"

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/cook_environment.h>
#include <atlantis/asset_system/environment_artifact.h>
#include <atlantis/asset_system/environment_metadata.h>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
  if (!out.empty()) file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
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

static atlantis::Result<EnvironmentAssetData, EnvironmentLoadError> decodeEnvironment(
    const std::vector<std::byte>& artifactBytes, std::string_view metadataText,
    std::optional<AssetGuid> expectedGuid = {}) {
  using ResultT = atlantis::Result<EnvironmentAssetData, EnvironmentLoadError>;
  auto artifactResult = decodeEnvironmentArtifact(artifactBytes);
  if (artifactResult.isErr()) return ResultT::Err(EnvironmentLoadError::ArtifactDecodeFailed);
  auto metadataResult = parseEnvironmentMetadata(metadataText);
  if (metadataResult.isErr()) return ResultT::Err(EnvironmentLoadError::MetadataParseFailed);

  DecodedEnvironmentArtifact& artifact = artifactResult.value();
  const EnvironmentMetadata& metadata = metadataResult.value();
  if ((expectedGuid && *expectedGuid != metadata.assetGuid) ||
      artifact.assetId != metadata.assetId || metadata.assetId != assetKey(metadata.assetGuid) ||
      artifact.data.faceSize != metadata.faceSize || artifact.data.mipCount != metadata.mipCount ||
      artifact.data.dfgWidth != metadata.dfgWidth || artifact.data.dfgHeight != metadata.dfgHeight) {
    return ResultT::Err(EnvironmentLoadError::MetadataArtifactMismatch);
  }
  return ResultT::Ok(std::move(artifact.data));
}

atlantis::Result<EnvironmentAssetData, EnvironmentLoadError> loadEnvironmentAsset(
    const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath) {
  using R = atlantis::Result<EnvironmentAssetData, EnvironmentLoadError>;
  std::vector<std::byte> artifact;
  if (!readFileBytes(artifactPath, artifact)) return R::Err(EnvironmentLoadError::ArtifactFileUnreadable);
  std::string metadata;
  if (!readFileText(metadataPath, metadata)) return R::Err(EnvironmentLoadError::MetadataFileUnreadable);
  return decodeEnvironment(artifact, metadata);
}

atlantis::Result<EnvironmentAssetData, EnvironmentLoadError> loadPreviewStudioEnvironment(
    const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath,
    std::optional<AssetGuid> expectedGuid) {
  using R = atlantis::Result<EnvironmentAssetData, EnvironmentLoadError>;
  constexpr auto maxArtifactBytes = [] {
    std::size_t bytes = kEnvironmentArtifactHeaderSizeBytes + kEnvironmentShPayloadSizeBytes +
                        kCookedEnvironmentDfgSize * kCookedEnvironmentDfgSize * 4u;
    for (auto size = kCookedEnvironmentFaceSize; size > 0; size /= 2) bytes += size * size * 6u * 8u;
    return bytes;
  }();
  std::vector<std::byte> artifact;
  if (detail::readPreviewFile(artifactPath, maxArtifactBytes, artifact))
    return R::Err(EnvironmentLoadError::ArtifactFileUnreadable);
  std::string metadata;
  if (detail::readPreviewFile(metadataPath, 64u * 1024u, metadata))
    return R::Err(EnvironmentLoadError::MetadataFileUnreadable);
  auto decoded = decodeEnvironment(artifact, metadata, expectedGuid);
  if (decoded.isErr()) return decoded;
  const auto& data = decoded.value();
  if (data.faceSize != kCookedEnvironmentFaceSize || data.mipCount != kCookedEnvironmentMipCount ||
      data.dfgWidth != kCookedEnvironmentDfgSize || data.dfgHeight != kCookedEnvironmentDfgSize)
    return R::Err(EnvironmentLoadError::MetadataArtifactMismatch);
  return decoded;
}

}  // namespace atlantis::asset_system
