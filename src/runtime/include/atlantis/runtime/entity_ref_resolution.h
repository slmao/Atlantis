#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/entity_ref.h>
#include <atlantis/result.h>
#include <atlantis/world/ecs/entity_id.h>
#include <atlantis/world/scene_instantiation.h>

namespace atlantis::runtime {

// Plan 0047 P18 / ADR-0097 D6: Runtime-private -- not a public API, not shared
// with any other module. Resolving a persisted EntityRef is only meaningful
// through a loaded instance of its scene.
enum class EntityRefError {
  UnknownScene,   // the reference names a scene other than the loaded one
  UnknownEntity,  // the loaded scene has no node with that EntityGuid
  DeadEntity,     // the node's entity has been destroyed in the baked World
};

[[nodiscard]] const char* toString(EntityRefError error) noexcept;

// A borrowed view of one loaded scene: its GUID and its bake output, whose
// EntityGuid -> EntityId map was built when the scene was baked (Spec 0051
// R7, Plan 0051 P6). The scene must outlive the view. Not thread-safe (the
// ECS is not).
struct LoadedSceneView {
  atlantis::asset_system::AssetGuid sceneGuid;
  const atlantis::world::BakedScene& scene;
};

// The EntityId `ref` names in the loaded scene. Never a null: a reference to
// another scene, to a GUID the scene lacks, or to an entity the baked World no
// longer holds each has its own error. Liveness comes from the baked World
// (isValid()).
[[nodiscard]] atlantis::Result<atlantis::world::ecs::EntityId, EntityRefError> resolveEntityRef(
    const LoadedSceneView& loaded, const atlantis::asset_system::EntityRef& ref);

}  // namespace atlantis::runtime
