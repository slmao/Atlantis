#pragma once

// Plan 0051 P8 (M5): test-only helpers for editing a fixture's baked scene
// (Spec 0051 ruling Q7 V3) the way Spec 0022's surviving contract allows --
// component values, world matrices, entity creation and removal (Spec 0022
// Correction 2026-10-06). The baked world has no hierarchy, so a test that
// wants "this local Transform" asks solveWorldMatrix() for the matrix the
// authoring stage would compute -- the same world::World solver the bake
// uses (ruling Q4 W1), so the result is bit-identical to a baked one.

#include <atlantis/assert.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/light.h>
#include <atlantis/world/renderable.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world.h>
#include <atlantis/world/world_matrix.h>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <vector>

namespace atlantis::image_regression {

// The baked scene's entities with a component T, in node order (index()).
template <typename T>
[[nodiscard]] std::vector<atlantis::world::ecs::EntityId> entitiesWith(atlantis::world::BakedScene& scene) {
  std::vector<atlantis::world::ecs::EntityId> ids;
  scene.world.query<const T>([&ids](atlantis::world::ecs::EntityId id, const T&) { ids.push_back(id); });
  std::sort(ids.begin(), ids.end(), [](const auto& lhs, const auto& rhs) { return lhs.index() < rhs.index(); });
  return ids;
}

[[nodiscard]] inline std::vector<atlantis::world::ecs::EntityId> lightEntities(atlantis::world::BakedScene& scene) {
  return entitiesWith<atlantis::world::Light>(scene);
}

[[nodiscard]] inline std::vector<atlantis::world::ecs::EntityId> renderableEntities(
    atlantis::world::BakedScene& scene) {
  return entitiesWith<atlantis::world::Renderable>(scene);
}

// The first Light of `kind`, in node order.
[[nodiscard]] inline std::optional<atlantis::world::ecs::EntityId> findLightByKind(atlantis::world::BakedScene& scene,
                                                                                  atlantis::world::LightKind kind) {
  for (const auto id : lightEntities(scene)) {
    if (scene.world.get<atlantis::world::Light>(id).value().kind == kind) return id;
  }
  return std::nullopt;
}

// The world matrix world::World's solver computes for `local` under a parent
// whose world matrix is `parentLocal`'s (or under none). A scratch World, one
// updateTransforms(): the arithmetic the bake itself uses.
[[nodiscard]] inline atlantis::world::WorldMatrix solveWorldMatrix(
    const atlantis::world::Transform& local, const std::optional<atlantis::world::Transform>& parentLocal = {}) {
  atlantis::world::World scratch;
  const auto entity = scratch.createEntity();
  ATLANTIS_CHECK(scratch.setLocalTransform(entity, local).isOk());
  if (parentLocal.has_value()) {
    const auto parent = scratch.createEntity();
    ATLANTIS_CHECK(scratch.setLocalTransform(parent, *parentLocal).isOk());
    ATLANTIS_CHECK(scratch.setParent(entity, parent).isOk());
  }
  scratch.updateTransforms();
  return atlantis::world::toWorldMatrix(scratch.getWorldMatrix(entity).value());
}

// Sets an entity's authored Transform and its world matrix together, as an
// unparented node: the baked counterpart of World::setLocalTransform() on a
// root entity.
inline void setRootTransform(atlantis::world::BakedScene& scene, atlantis::world::ecs::EntityId entity,
                             const atlantis::world::Transform& local) {
  ATLANTIS_CHECK(scene.world.set(entity, local).isOk());
  ATLANTIS_CHECK(scene.world.set(entity, solveWorldMatrix(local)).isOk());
}

}  // namespace atlantis::image_regression
