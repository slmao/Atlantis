#include <atlantis/world/scene_instantiation.h>

#include <atlantis/assert.h>

#include <algorithm>
#include <vector>

namespace atlantis::world {

std::optional<EntityId> SceneEntityMap::find(const atlantis::asset_system::EntityGuid& guid) const noexcept {
  const auto it = std::lower_bound(entries_.begin(), entries_.end(), guid,
                                   [](const auto& entry, const atlantis::asset_system::EntityGuid& key) {
                                     return entry.first < key;
                                   });
  if (it == entries_.end() || it->first != guid) return std::nullopt;
  return it->second;
}

World fromValidatedSceneData(const atlantis::asset_system::ValidatedSceneData& scene) {
  return instantiateScene(scene).world;
}

SceneInstance instantiateScene(const atlantis::asset_system::ValidatedSceneData& scene) {
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
                        "instantiateScene(): setLocalTransform() failed for a freshly-created entity");

    if (n.camera.has_value()) {
      Camera camera;
      camera.fovYRadians = n.camera->fovYRadians;
      camera.nearZ = n.camera->nearZ;
      camera.farZ = n.camera->farZ;
      camera.exposureCompensationEv = n.camera->exposureCompensationEv;
      // Plan 0043 P3: the fog group, carried through as plain data.
      const atlantis::asset_system::DecodedCameraFog& fog = n.camera->fog;
      camera.fog.color = {fog.colorR, fog.colorG, fog.colorB};
      camera.fog.density = fog.density;
      camera.fog.height = fog.height;
      camera.fog.heightFalloff = fog.heightFalloff;
      camera.fog.maxOpacity = fog.maxOpacity;
      // Plan 0044 P3: the bloom group, likewise.
      camera.bloom.strength = n.camera->bloom.strength;
      camera.bloom.threshold = n.camera->bloom.threshold;
      ATLANTIS_CHECK_MSG(world.setCamera(id, camera).isOk(),
                          "instantiateScene(): setCamera() failed for a freshly-created entity");
    }
    if (n.renderable.has_value()) {
      ATLANTIS_CHECK_MSG(
          world.setRenderable(id, Renderable{n.renderable->meshAsset, n.renderable->materialAsset}).isOk(),
          "instantiateScene(): setRenderable() failed for a freshly-created entity");
    }
    if (n.light.has_value()) {
      Light light;
      light.kind = n.light->kind == atlantis::asset_system::DecodedLightKind::Directional ? LightKind::Directional
                                                                                            : LightKind::Point;
      light.color = {n.light->colorR, n.light->colorG, n.light->colorB};
      light.intensity = n.light->intensity;
      light.range = n.light->range;
      ATLANTIS_CHECK_MSG(world.setLight(id, light).isOk(),
                          "instantiateScene(): setLight() failed for a freshly-created entity");
    }
    byIndex.push_back(id);
  }

  // Pass 2: parent links, using the pass-1 mapping -- discarded when
  // this function returns.
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    if (const auto parentIndex = scene.parentOf(i); parentIndex.has_value()) {
      ATLANTIS_CHECK_MSG(world.setParent(byIndex[i], byIndex[*parentIndex]).isOk(),
                          "instantiateScene(): setParent() failed for an already-validated, acyclic hierarchy");
    }
  }

  if (const auto activeCameraIndex = scene.activeCameraIndex(); activeCameraIndex.has_value()) {
    ATLANTIS_CHECK_MSG(
        world.setActiveCamera(byIndex[*activeCameraIndex]).isOk(),
        "instantiateScene(): setActiveCamera() failed for a node ValidatedSceneData already guarantees has a "
        "Camera");
  }

  // The node-index-to-EntityId mapping is still discarded; what outlives this
  // call is the persistent EntityGuid -> EntityId map (ADR-0097 D5).
  std::vector<std::pair<atlantis::asset_system::EntityGuid, EntityId>> entries;
  entries.reserve(scene.nodeCount());
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) entries.emplace_back(scene.entityGuid(i), byIndex[i]);
  std::sort(entries.begin(), entries.end(), [](const auto& lhs, const auto& rhs) { return lhs.first < rhs.first; });

  return SceneInstance{std::move(world), SceneEntityMap(std::move(entries))};
}

}  // namespace atlantis::world
