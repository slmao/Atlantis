#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/errors.h>
#include <atlantis/result.h>

#include <string>
#include <vector>

namespace atlantis::asset_system {

// Plan 0012 Section D4: one declared asset's logical path and its own
// already-computed Asset ID. The caller (the cooker's --validate-set
// mode, or a test) computes assetId -- validateAssetSet() below never
// calls computeAssetId() itself, so tests can inject a deliberately-
// equal AssetId pair to exercise collision detection without needing to
// discover a genuine 64-bit FNV-1a collision, while the production path
// always supplies a real, independently-computed value.
struct DeclaredAsset {
  std::string logicalPath;
  AssetId assetId;
};

// Detects, across exactly the declared set passed in one call: an Asset
// ID collision between two distinct logical paths (AssetIdCollision); a
// case-only-differing logical-path pair (CaseOnlyPathConflict); an exact
// duplicate logical path (DuplicateLogicalPath). Scoped to this one
// declared set only (ADR-0044) -- not a repository-global uniqueness
// guarantee across independent build invocations; a future asset
// registry/database would need to re-run this kind of check globally.
[[nodiscard]] atlantis::Result<std::monostate, AssetSetError> validateAssetSet(
    const std::vector<DeclaredAsset>& assets);

}  // namespace atlantis::asset_system
