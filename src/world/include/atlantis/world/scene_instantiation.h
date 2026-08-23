#pragma once

#include <atlantis/asset_system/validated_scene_data.h>
#include <atlantis/world/world.h>

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

}  // namespace atlantis::world
