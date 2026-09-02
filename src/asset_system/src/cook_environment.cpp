#include <atlantis/asset_system/cook_environment.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/environment_artifact.h>
#include <atlantis/asset_system/environment_metadata.h>
#include <atlantis/asset_system/logical_path.h>

#include "environment_processing.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>

namespace atlantis::asset_system {

namespace {

[[nodiscard]] bool writeBytesAtomically(const std::filesystem::path& finalPath, const char* data, std::size_t size) {
  std::error_code error;
  const std::filesystem::path directory = finalPath.parent_path();
  if (!directory.empty()) std::filesystem::create_directories(directory, error);
  std::random_device random;
  const std::filesystem::path temporary =
      directory / (finalPath.filename().string() + ".tmp-" + std::to_string(random()) + std::to_string(random()));
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output.is_open()) return false;
    output.write(data, static_cast<std::streamsize>(size));
    output.flush();
    if (!output.good()) {
      output.close();
      std::filesystem::remove(temporary, error);
      return false;
    }
  }
  std::filesystem::rename(temporary, finalPath, error);
  if (error) {
    std::filesystem::remove(temporary, error);
    return false;
  }
  return true;
}

[[nodiscard]] bool outputIsFinite(const EnvironmentAssetData& data) {
  for (float coefficient : data.irradianceSh) {
    if (!std::isfinite(coefficient)) return false;
  }
  for (std::uint16_t value : data.specularRgba16Float) {
    if ((value & 0x7C00U) == 0x7C00U) return false;
  }
  for (std::uint16_t value : data.dfgRg16Float) {
    if ((value & 0x7C00U) == 0x7C00U) return false;
  }
  return true;
}

}  // namespace

atlantis::Result<std::monostate, EnvironmentCookError> cookEnvironment(
    const float* rgbaPixels, std::uint32_t width, std::uint32_t height, const std::string& logicalPathInput,
    const std::filesystem::path& artifactOutputPath, const std::filesystem::path& metadataOutputPath) {
  using ResultT = atlantis::Result<std::monostate, EnvironmentCookError>;
  const auto normalizedResult = normalizeLogicalPath(logicalPathInput);
  if (normalizedResult.isErr()) return ResultT::Err(EnvironmentCookError::LogicalPathInvalid);
  if (width == 0 || height == 0 || static_cast<std::uint64_t>(height) * 2ULL != width) {
    return ResultT::Err(EnvironmentCookError::InvalidSourceDimensions);
  }
  const std::uint64_t texelCount = static_cast<std::uint64_t>(width) * height;
  if (texelCount > std::numeric_limits<std::size_t>::max() / 4ULL) {
    return ResultT::Err(EnvironmentCookError::SourceSizeOverflow);
  }
  ATLANTIS_CHECK(rgbaPixels != nullptr);
  for (std::size_t texel = 0; texel < static_cast<std::size_t>(texelCount); ++texel) {
    for (std::size_t channel = 0; channel < 3; ++channel) {
      const float value = rgbaPixels[texel * 4 + channel];
      if (!std::isfinite(value)) return ResultT::Err(EnvironmentCookError::NonFiniteSourceValue);
      if (value < 0.0F) return ResultT::Err(EnvironmentCookError::NegativeSourceValue);
    }
  }

  const detail::EnvironmentProcessingSettings settings{kCookedEnvironmentFaceSize, kCookedEnvironmentDfgSize,
                                                        kCookedEnvironmentSampleCount};
  const EnvironmentAssetData data = detail::preprocessEnvironment(rgbaPixels, width, height, settings);
  if (!outputIsFinite(data)) return ResultT::Err(EnvironmentCookError::OutputValueOverflow);

  const std::string& logicalPath = normalizedResult.value();
  const AssetId assetId = computeAssetId(logicalPath);
  const std::vector<std::byte> artifact = encodeEnvironmentArtifact(assetId, data);
  const EnvironmentMetadata metadata{assetId, logicalPath, data.faceSize, data.mipCount, data.dfgWidth,
                                     data.dfgHeight};
  const std::string metadataText = serializeEnvironmentMetadata(metadata);
  if (!writeBytesAtomically(artifactOutputPath, reinterpret_cast<const char*>(artifact.data()), artifact.size()) ||
      !writeBytesAtomically(metadataOutputPath, metadataText.data(), metadataText.size())) {
    return ResultT::Err(EnvironmentCookError::AtomicWriteFailed);
  }
  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::asset_system
