#pragma once

#include <atlantis/asset_system/environment_types.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <filesystem>

namespace atlantis::asset_system {

// Reads, validates, and cross-checks the environment artifact and metadata.
// This API is CPU-only and not thread-safe with concurrent mutation of either
// input file. Independent calls with stable files may run concurrently.
[[nodiscard]] atlantis::Result<EnvironmentAssetData, EnvironmentLoadError> loadEnvironmentAsset(
    const std::filesystem::path& artifactPath, const std::filesystem::path& metadataPath);

}  // namespace atlantis::asset_system
