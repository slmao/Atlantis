#include <atlantis/asset_system/asset_set_validation.h>

#include <atlantis/asset_system/logical_path.h>

#include <unordered_map>

namespace atlantis::asset_system {

namespace {

[[nodiscard]] std::string toLowerAscii(std::string_view s) {
  std::string result(s);
  for (char& c : result) {
    if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
  }
  return result;
}

}  // namespace

atlantis::Result<std::monostate, AssetSetError> validateAssetSet(const std::vector<DeclaredAsset>& assets) {
  using ResultT = atlantis::Result<std::monostate, AssetSetError>;

  std::unordered_map<std::string, AssetId> seenExactPaths;
  std::unordered_map<std::string, std::string> seenLowerPaths;  // lowercase -> first-seen exact path
  std::unordered_map<AssetId, std::string> seenIds;              // AssetId -> first-seen exact path

  for (const DeclaredAsset& asset : assets) {
    // Every declared logical path must already be in normalized form --
    // validateAssetSet() never normalizes on the caller's behalf, so a
    // caller passing a raw, unnormalized path is a distinct, caught
    // mistake rather than silently accepted.
    const auto normalizedResult = normalizeLogicalPath(asset.logicalPath);
    if (normalizedResult.isErr() || normalizedResult.value() != asset.logicalPath) {
      return ResultT::Err(AssetSetError::InvalidLogicalPath);
    }

    if (seenExactPaths.contains(asset.logicalPath)) {
      return ResultT::Err(AssetSetError::DuplicateLogicalPath);
    }

    const std::string lower = toLowerAscii(asset.logicalPath);
    const auto lowerIt = seenLowerPaths.find(lower);
    if (lowerIt != seenLowerPaths.end() && lowerIt->second != asset.logicalPath) {
      return ResultT::Err(AssetSetError::CaseOnlyPathConflict);
    }

    const auto idIt = seenIds.find(asset.assetId);
    if (idIt != seenIds.end() && idIt->second != asset.logicalPath) {
      return ResultT::Err(AssetSetError::AssetIdCollision);
    }

    seenExactPaths.emplace(asset.logicalPath, asset.assetId);
    seenLowerPaths.emplace(lower, asset.logicalPath);
    seenIds.emplace(asset.assetId, asset.logicalPath);
  }

  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::asset_system
