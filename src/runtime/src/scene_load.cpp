#include <atlantis/runtime/scene_load.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/log.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/runtime/scene_manifest.h>
#include <atlantis/world/scene_instantiation.h>

#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace atlantis::runtime {

atlantis::Result<SceneLoadOutcome, RuntimeInitError> loadAndInstantiateScene(
    const BootstrapConfig& config, atlantis::rhi::Device* device,
    const atlantis::rhi::VertexInputLayout& vertexInputLayout) {
  using ResultT = atlantis::Result<SceneLoadOutcome, RuntimeInitError>;
  using atlantis::renderer::createMesh;

  // (a) Read and validate the dependency manifest -- local, immutable resolver.
  auto manifestResult = loadSceneDependencyManifest(config.sceneDependencyManifestPath);
  if (manifestResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadSceneDependencyManifest() failed: {}", toString(manifestResult.error()));
    return ResultT::Err(RuntimeInitError::SceneManifestLoadFailed);
  }
  const SceneDependencyResolver resolver = std::move(manifestResult.value());

  // (b) Decode the scene artifact -- fully validated ValidatedSceneData.
  auto sceneResult = atlantis::asset_system::decodeScene(config.sceneArtifactPath, config.sceneMetadataPath);
  if (sceneResult.isErr()) {
    ATLANTIS_LOG_ERROR("decodeScene() failed");
    return ResultT::Err(RuntimeInitError::SceneArtifactLoadFailed);
  }
  const atlantis::asset_system::ValidatedSceneData scene = std::move(sceneResult.value());

  // (c) Collect distinct AssetIds in ascending FIRST-REFERENCE order --
  //     the sole source of this Plan's own load-order guarantee (Spec
  //     0015 Human Review Approval item 10). Walks scene's own node
  //     array in index order; the resolver (built in (a), AssetId-sorted
  //     for lookup only) is never iterated here or anywhere else.
  std::vector<atlantis::asset_system::AssetId> distinctIds;
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    if (const auto& node = scene.node(i); node.renderable.has_value()) {
      const atlantis::asset_system::AssetId id = node.renderable->meshAsset;
      if (std::find(distinctIds.begin(), distinctIds.end(), id) == distinctIds.end()) distinctIds.push_back(id);
    }
  }

  // (d) Phase 1: resolve every one -- no I/O, no Entity yet, same order as (c).
  std::vector<const SceneDependencyResolver::Entry*> resolvedEntries;
  for (atlantis::asset_system::AssetId id : distinctIds) {
    const auto* entry = resolver.find(id);
    if (!entry) {
      ATLANTIS_LOG_ERROR("scene references AssetId with no manifest entry");
      return ResultT::Err(RuntimeInitError::SceneDependencyUnresolved);
    }
    resolvedEntries.push_back(entry);
  }

  // (e) Phase 2: load, same order as (c)/(d) -- distinctIds' own index
  //     order IS the load order; the map below is populated in that
  //     order but is a keyed store, never iterated afterward in any
  //     order-sensitive way. device is only ever dereferenced here,
  //     and only when distinctIds is non-empty -- see this function's
  //     own header comment on why a test may pass nullptr otherwise.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  for (std::size_t i = 0; i < distinctIds.size(); ++i) {
    auto meshAssetResult =
        atlantis::asset_system::loadStaticMeshAsset(resolvedEntries[i]->artifactPath, resolvedEntries[i]->metadataPath);
    if (meshAssetResult.isErr()) {
      ATLANTIS_LOG_ERROR("loadStaticMeshAsset() failed for a scene dependency");
      return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
    }
    const atlantis::asset_system::StaticMeshAssetData& meshAssetData = meshAssetResult.value();
    ATLANTIS_CHECK_MSG(device != nullptr, "loadAndInstantiateScene(): a real Device is required once a scene has "
                                           "at least one distinct mesh dependency to load");
    auto createResult = createMesh(*device, vertexInputLayout, meshAssetData.vertexBytes().data(),
                                    meshAssetData.vertexBytes().size(), meshAssetData.indices().data(),
                                    static_cast<std::uint32_t>(meshAssetData.indices().size()));
    if (createResult.isErr()) {
      ATLANTIS_LOG_ERROR("createMesh() failed for a scene dependency");
      return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
    }
    meshResourceMap.emplace(distinctIds[i], std::move(createResult.value()));
  }

  // (f) Instantiate -- infallible.
  atlantis::world::World world = atlantis::world::fromValidatedSceneData(scene);

  // (g) Publish -- the caller (RuntimeApplication::initializeSteps())
  //     performs the actual world_.emplace()/meshResourceMap_ = ...
  //     publish; this function's own return, by value, is itself
  //     already the transactional boundary -- nothing is written to
  //     any caller-owned state until this Ok() is actually consumed.
  return ResultT::Ok(SceneLoadOutcome{std::move(world), std::move(meshResourceMap)});
}

}  // namespace atlantis::runtime
