#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/validated_scene_data.h>
#include <atlantis/world/entity_id.h>
#include <atlantis/world/world.h>

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace atlantis::world {

// Plan 0015 Section D9: two-pass, deterministic instantiation of an
// already-validated scene into a fresh World -- genuinely infallible
// (returns World by value, no Result, no new WorldError enumerator)
// because ValidatedSceneData's own exhaustive prior validation (D6)
// removes every reachable failure case; every World call this function
// makes is guarded by an ATLANTIS_CHECK_MSG "should never happen"
// assertion, never a Result-propagating error path. Scene-local
// node indices are never persisted as EntityId -- the node-index-to-
// EntityId mapping this function builds internally is a local
// std::vector, discarded the instant this function returns (Spec 0015
// Human Review Approval item 6).
[[nodiscard]] World fromValidatedSceneData(const atlantis::asset_system::ValidatedSceneData& scene);

// Plan 0047 P17 (ADR-0097 D5): the persistent identity of a scene's entities
// within one instantiated World -- EntityGuid -> that instance's EntityId.
// Immutable once built (a sorted vector, found by binary search); empty when
// default-constructed. An EntityId is a per-World-instance token, so a map is
// meaningful only beside the World it was built with. Safe for concurrent
// reads.
struct SceneInstance;

class SceneEntityMap {
 public:
  SceneEntityMap() = default;

  // The entity instantiated for `guid`, or nullopt when the scene has no such
  // node. Says nothing about liveness: ask the World.
  [[nodiscard]] std::optional<EntityId> find(const atlantis::asset_system::EntityGuid& guid) const noexcept;
  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }

 private:
  friend SceneInstance instantiateScene(const atlantis::asset_system::ValidatedSceneData& scene);

  explicit SceneEntityMap(std::vector<std::pair<atlantis::asset_system::EntityGuid, EntityId>> sortedEntries)
      : entries_(std::move(sortedEntries)) {}

  std::vector<std::pair<atlantis::asset_system::EntityGuid, EntityId>> entries_;  // sorted by EntityGuid
};

struct SceneInstance {
  World world;
  SceneEntityMap entities;
};

// The instantiation proper: fromValidatedSceneData() is exactly
// instantiateScene(scene).world, so the two cannot diverge. Genuinely
// infallible, like fromValidatedSceneData().
[[nodiscard]] SceneInstance instantiateScene(const atlantis::asset_system::ValidatedSceneData& scene);

}  // namespace atlantis::world
