#include <atlantis/world/scene_instantiation.h>

#include <atlantis/assert.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/world_matrix.h>

#include <utility>
#include <vector>

namespace atlantis::world {

namespace {

// Every add() below targets an entity createEntities() made a moment ago and
// a component type it does not have yet, outside any query -- so a failure
// is a programming error, never a reachable state (Plan 0051 P2, ruling E1).
template <ecs::Component T>
void addBaked(ecs::World& world, ecs::EntityId entity, const T& value) {
  ATLANTIS_CHECK_MSG(world.add<T>(entity, value).isOk(), "bakeScene(): add() failed for a freshly-created entity");
}

}  // namespace

BakedScene bakeScene(const atlantis::asset_system::ValidatedSceneData& scene) {
  // (1) The authoring stage: the existing instantiation, its hierarchy solved
  // once by the same updateTransforms() Runtime's frame used to run, so each
  // world matrix is bit-identical to the one that frame computed.
  SceneInstance authoring = instantiateScene(scene);
  authoring.world.updateTransforms();

  // (2) One entity per node, in node order: in a fresh ECS world each
  // entity's index() is its node index (Spec 0051 ruling Q3, D3).
  std::vector<atlantis::asset_system::EntityGuid> guids;
  guids.reserve(scene.nodeCount());
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) guids.push_back(scene.entityGuid(i));
  ecs::World baked;
  auto created = ecs::createEntities(baked, guids);
  ATLANTIS_CHECK_MSG(created.isOk(),
                      "bakeScene(): createEntities() failed, but decodeScene() already rejects a nil or repeated "
                      "EntityGuid");
  ecs::EntityGuidMap entities = std::move(created.value());

  // (3) Each node's authored components, plus its resolved world matrix. The
  // baked world carries no hierarchy (ruling Q2, H1).
  std::optional<ecs::EntityId> activeCamera;
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    const auto source = authoring.entities.find(guids[i]);
    const auto target = entities.find(guids[i]);
    ATLANTIS_CHECK_MSG(source.has_value() && target.has_value(),
                        "bakeScene(): a node's EntityGuid is missing from the instantiation or the bake map");

    const auto transform = authoring.world.getLocalTransform(*source);
    const auto worldMatrix = authoring.world.getWorldMatrix(*source);
    ATLANTIS_CHECK_MSG(transform.isOk() && worldMatrix.isOk(),
                        "bakeScene(): an instantiated entity has no Transform or world matrix");
    addBaked(baked, *target, transform.value());
    addBaked(baked, *target, toWorldMatrix(worldMatrix.value()));

    // Presence follows the instantiated World's own components exactly.
    if (const auto camera = authoring.world.getCamera(*source); camera.isOk()) {
      addBaked(baked, *target, camera.value());
    }
    if (const auto light = authoring.world.getLight(*source); light.isOk()) {
      addBaked(baked, *target, light.value());
    }
    if (const auto renderable = authoring.world.getRenderable(*source); renderable.isOk()) {
      addBaked(baked, *target, renderable.value());
    }
    if (scene.activeCameraIndex() == i) activeCamera = *target;
  }

  // (4) The authoring world and its map are discarded here (ADR-0102 D1).
  return BakedScene{std::move(baked), std::move(entities), activeCamera};
}

}  // namespace atlantis::world
