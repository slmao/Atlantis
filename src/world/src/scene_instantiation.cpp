#include <atlantis/world/scene_instantiation.h>

#include <atlantis/assert.h>

#include <vector>

namespace atlantis::world {

World fromValidatedSceneData(const atlantis::asset_system::ValidatedSceneData& scene) {
  World world;
  std::vector<EntityId> byIndex;
  byIndex.reserve(scene.nodeCount());

  // Pass 1: every node exists before any parent link is set.
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    const auto& n = scene.node(i);
    const EntityId id = world.createEntity();

    Transform t;
    t.localPosition = {n.transform.positionX, n.transform.positionY, n.transform.positionZ};
    t.localEulerAnglesRadians = {n.transform.eulerXRadians, n.transform.eulerYRadians, n.transform.eulerZRadians};
    t.localScale = {n.transform.scaleX, n.transform.scaleY, n.transform.scaleZ};
    ATLANTIS_CHECK_MSG(world.setLocalTransform(id, t).isOk(),
                        "fromValidatedSceneData(): setLocalTransform() failed for a freshly-created entity");

    if (n.camera.has_value()) {
      ATLANTIS_CHECK_MSG(world.setCamera(id, Camera{n.camera->fovYRadians, n.camera->nearZ, n.camera->farZ}).isOk(),
                          "fromValidatedSceneData(): setCamera() failed for a freshly-created entity");
    }
    if (n.renderable.has_value()) {
      ATLANTIS_CHECK_MSG(
          world.setRenderable(id, Renderable{n.renderable->meshAsset, n.renderable->materialAsset}).isOk(),
          "fromValidatedSceneData(): setRenderable() failed for a freshly-created entity");
    }
    if (n.light.has_value()) {
      Light light;
      light.kind = n.light->kind == atlantis::asset_system::DecodedLightKind::Directional ? LightKind::Directional
                                                                                            : LightKind::Point;
      light.color = {n.light->colorR, n.light->colorG, n.light->colorB};
      light.intensity = n.light->intensity;
      light.range = n.light->range;
      ATLANTIS_CHECK_MSG(world.setLight(id, light).isOk(),
                          "fromValidatedSceneData(): setLight() failed for a freshly-created entity");
    }
    byIndex.push_back(id);
  }

  // Pass 2: parent links, using the pass-1 mapping -- discarded when
  // this function returns.
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    if (const auto parentIndex = scene.parentOf(i); parentIndex.has_value()) {
      ATLANTIS_CHECK_MSG(world.setParent(byIndex[i], byIndex[*parentIndex]).isOk(),
                          "fromValidatedSceneData(): setParent() failed for an already-validated, acyclic hierarchy");
    }
  }

  if (const auto activeCameraIndex = scene.activeCameraIndex(); activeCameraIndex.has_value()) {
    ATLANTIS_CHECK_MSG(
        world.setActiveCamera(byIndex[*activeCameraIndex]).isOk(),
        "fromValidatedSceneData(): setActiveCamera() failed for a node ValidatedSceneData already guarantees has a "
        "Camera");
  }

  return world;
}

}  // namespace atlantis::world
