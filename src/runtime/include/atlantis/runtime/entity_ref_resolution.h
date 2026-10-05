#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/entity_ref.h>
#include <atlantis/result.h>
#include <atlantis/world/entity_id.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world.h>

namespace atlantis::runtime {

// Plan 0047 P18 / ADR-0097 D6: Runtime-private -- not a public API, not shared
// with any other module. Resolving a persisted EntityRef is only meaningful
// through a loaded instance of its scene.
enum class EntityRefError {
  UnknownScene,   // the reference names a scene other than the loaded one
  UnknownEntity,  // the loaded scene has no node with that EntityGuid
  DeadEntity,     // the node's entity has been destroyed in the World
};

[[nodiscard]] const char* toString(EntityRefError error) noexcept;

// A borrowed view of one loaded scene: its GUID, the EntityGuid -> EntityId
// map built when it was instantiated, and the World that map belongs to. The
// referents must outlive the view. Not thread-safe (World is not).
struct LoadedSceneView {
  atlantis::asset_system::AssetGuid sceneGuid;
  const atlantis::world::SceneEntityMap& entities;
  const atlantis::world::World& world;
};

// The EntityId `ref` names in the loaded scene. Never a null: a reference to
// another scene, to a GUID the scene lacks, or to an entity World no longer
// holds each has its own error. Liveness comes from World (isValid()).
[[nodiscard]] atlantis::Result<atlantis::world::EntityId, EntityRefError> resolveEntityRef(
    const LoadedSceneView& loaded, const atlantis::asset_system::EntityRef& ref);

}  // namespace atlantis::runtime
