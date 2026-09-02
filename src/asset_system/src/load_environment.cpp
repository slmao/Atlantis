#include <atlantis/asset_system/load_environment.h>

#include <atlantis/asset_system/asset_id.h>
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

atlantis::Result<EnvironmentAssetData, EnvironmentLoadError> loadEnvironmentAsset(
    const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath) {
  using ResultT = atlantis::Result<EnvironmentAssetData, EnvironmentLoadError>;
  std::vector<std::byte> artifactBytes;
  if (!readFileBytes(artifactPath, artifactBytes)) {
    return ResultT::Err(EnvironmentLoadError::ArtifactFileUnreadable);
  }
  std::string metadataText;
  if (!readFileText(metadataPath, metadataText)) {
    return ResultT::Err(EnvironmentLoadError::MetadataFileUnreadable);
  }
  auto artifactResult = decodeEnvironmentArtifact(artifactBytes);
  if (artifactResult.isErr()) return ResultT::Err(EnvironmentLoadError::ArtifactDecodeFailed);
  auto metadataResult = parseEnvironmentMetadata(metadataText);
  if (metadataResult.isErr()) return ResultT::Err(EnvironmentLoadError::MetadataParseFailed);

  DecodedEnvironmentArtifact& artifact = artifactResult.value();
  const EnvironmentMetadata& metadata = metadataResult.value();
  if (artifact.assetId != metadata.assetId || metadata.assetId != computeAssetId(metadata.sourceLogicalPath) ||
      artifact.data.faceSize != metadata.faceSize || artifact.data.mipCount != metadata.mipCount ||
      artifact.data.dfgWidth != metadata.dfgWidth || artifact.data.dfgHeight != metadata.dfgHeight) {
    return ResultT::Err(EnvironmentLoadError::MetadataArtifactMismatch);
  }
  return ResultT::Ok(std::move(artifact.data));
}

}  // namespace atlantis::asset_system
