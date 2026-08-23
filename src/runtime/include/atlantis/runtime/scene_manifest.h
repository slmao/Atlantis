#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/result.h>

#include <cstddef>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace atlantis::runtime {

// Plan 0015 Section D8: Runtime-private -- never reused by AssetSystem
// or World, matching ADR-0054's own explicit "never
// WorldError/SceneCookError/SceneArtifactDecodeError/AssetLoadError
// directly" requirement's own spirit for Runtime-side conditions too.
enum class SceneManifestError {
  ManifestUnreadable,
  MalformedEntry,
  DuplicateLogicalPath,
  AssetIdCollision,
  MetadataArtifactMismatch,
};

// A sorted std::vector<std::pair<AssetId, Entry>>, looked up via
// std::lower_bound() -- never std::unordered_map -- so no code path in
// this Plan ever exposes hash-iteration order anywhere, even by
// accident. This choice is about lookup-container hygiene only; it
// does NOT establish load order. find() is a point query -- this
// struct is never iterated end-to-end by any caller. The load-bearing
// load-order guarantee (Spec 0015 Human Review Approval item 10;
// ADR-0054's own Decision item 3) comes entirely from D10's own
// distinctIds collection, built by walking ValidatedSceneData's own
// node array in ascending index order -- a property of D10's own loop,
// independent of how this resolver itself happens to be stored.
struct SceneDependencyResolver {
  struct Entry {
    std::string artifactPath;
    std::string metadataPath;
  };
  std::vector<std::pair<atlantis::asset_system::AssetId, Entry>> entries;  // sorted by AssetId -- lookup speed only

  [[nodiscard]] const Entry* find(atlantis::asset_system::AssetId id) const noexcept;
};

// Plan 0015 Section D8: reads, validates, and indexes a scene's own
// generated dependency manifest (Section D7's atlantis_add_scene_asset()
// own file(GENERATE) output) -- a build-tree-private, tab-separated
// triple per line (<logical path>\t<artifact path>\t<metadata path>),
// never a portable part of any artifact.
[[nodiscard]] atlantis::Result<SceneDependencyResolver, SceneManifestError> loadSceneDependencyManifest(
    const std::string& manifestPath);

// For logging only -- not part of any Result/error contract, matching
// init_error.h's own toString() precedent exactly.
[[nodiscard]] const char* toString(SceneManifestError error) noexcept;

// D8 steps 3-4 (duplicate logical path, AssetId collision), factored
// out of loadSceneDependencyManifest() as an internal seam so V15's
// own "two distinct logical paths engineered to hash to the same
// AssetId" test does not need a genuine brute-forced 64-bit FNV-1a
// collision (computationally infeasible for a unit test) -- it can
// instead supply two already-fabricated AssetId values directly,
// exactly as asset_set_validation_tests.cpp's own already-Accepted
// AssetSetError::AssetIdCollision test does for validateAssetSet().
// Not part of this module's own public contract otherwise;
// loadSceneDependencyManifest() is still the only intended entry
// point for real manifest loading.
namespace detail {
struct ManifestEntryForCollisionCheck {
  atlantis::asset_system::AssetId assetId;
  std::string normalizedLogicalPath;
};

[[nodiscard]] atlantis::Result<std::monostate, SceneManifestError> checkForDuplicatesAndCollisions(
    const std::vector<ManifestEntryForCollisionCheck>& entries);
}  // namespace detail

}  // namespace atlantis::runtime
