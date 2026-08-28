#include <atlantis/runtime/scene_load.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/load_material.h>
#include <atlantis/asset_system/load_texture.h>
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

  // (c) Collect distinct mesh AND material AssetIds, each in its own
  // ascending FIRST-REFERENCE order -- the sole source of this Plan's
  // own load-order guarantee (Spec 0015 Human Review Approval item 10),
  // widened by Plan 0018 Section P11 to a second, independent
  // distinct-id collection for materials. Walks the scene's own node
  // array in index order; the resolver (built in (a), AssetId-sorted
  // for lookup only) is never iterated here or anywhere else. Texture
  // AssetIds are NOT collected here -- a material's own texture
  // reference is only known once that material has itself already
  // loaded (step (e) below), since it lives inside the material's own
  // artifact, not the scene's.
  std::vector<atlantis::asset_system::AssetId> distinctMeshIds;
  std::vector<atlantis::asset_system::AssetId> distinctMaterialIds;
  for (std::size_t i = 0; i < scene.nodeCount(); ++i) {
    if (const auto& node = scene.node(i); node.renderable.has_value()) {
      const atlantis::asset_system::AssetId meshId = node.renderable->meshAsset;
      if (std::find(distinctMeshIds.begin(), distinctMeshIds.end(), meshId) == distinctMeshIds.end()) {
        distinctMeshIds.push_back(meshId);
      }
      if (node.renderable->materialAsset.has_value()) {
        const atlantis::asset_system::AssetId materialId = *node.renderable->materialAsset;
        if (std::find(distinctMaterialIds.begin(), distinctMaterialIds.end(), materialId) ==
            distinctMaterialIds.end()) {
          distinctMaterialIds.push_back(materialId);
        }
      }
    }
  }

  // (d) Phase 1: resolve every mesh AND material id -- no I/O, no Entity
  // yet, same order as (c). An unresolved material id fails the whole
  // scene load exactly like an unresolved mesh id already does (Spec
  // 0018 D4 case 2 -- never a silent fallback to the built-in Material).
  std::vector<const SceneDependencyResolver::Entry*> resolvedMeshEntries;
  for (atlantis::asset_system::AssetId id : distinctMeshIds) {
    const auto* entry = resolver.find(id);
    if (!entry) {
      ATLANTIS_LOG_ERROR("scene references AssetId with no manifest entry");
      return ResultT::Err(RuntimeInitError::SceneDependencyUnresolved);
    }
    resolvedMeshEntries.push_back(entry);
  }
  std::vector<const SceneDependencyResolver::Entry*> resolvedMaterialEntries;
  for (atlantis::asset_system::AssetId id : distinctMaterialIds) {
    const auto* entry = resolver.find(id);
    if (!entry) {
      ATLANTIS_LOG_ERROR("scene references AssetId with no manifest entry");
      return ResultT::Err(RuntimeInitError::SceneDependencyUnresolved);
    }
    resolvedMaterialEntries.push_back(entry);
  }

  // (e) Phase 2: load, same order as (c)/(d) -- distinctMeshIds' own
  // index order IS the mesh load order; the map below is populated in
  // that order but is a keyed store, never iterated afterward in any
  // order-sensitive way. device is only ever dereferenced here, and
  // only when distinctMeshIds is non-empty -- see this function's own
  // header comment on why a test may pass nullptr otherwise.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  for (std::size_t i = 0; i < distinctMeshIds.size(); ++i) {
    auto meshAssetResult = atlantis::asset_system::loadStaticMeshAsset(resolvedMeshEntries[i]->artifactPath,
                                                                        resolvedMeshEntries[i]->metadataPath);
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
    meshResourceMap.emplace(distinctMeshIds[i], std::move(createResult.value()));
  }

  // Plan 0018 Section P11: load each distinct material's own CPU-only
  // data, then resolve+load its own referenced texture the same
  // value-level-only way (deduplicated by texture AssetId into its own
  // map -- two materials naming the same texture load it once, D10).
  // Neither loadMaterialAsset() nor loadTextureAsset() names or
  // constructs any RHI type -- no SampledTexture/Sampler/Pipeline/
  // Material exists anywhere in this function, matching Spec 0018 D8
  // Phase 1's own hard constraint (no RenderTarget exists yet at this
  // point in initializeSteps()).
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;
  for (std::size_t i = 0; i < distinctMaterialIds.size(); ++i) {
    auto materialAssetResult = atlantis::asset_system::loadMaterialAsset(resolvedMaterialEntries[i]->artifactPath,
                                                                          resolvedMaterialEntries[i]->metadataPath);
    if (materialAssetResult.isErr()) {
      ATLANTIS_LOG_ERROR("loadMaterialAsset() failed for a scene dependency");
      return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
    }
    const atlantis::asset_system::MaterialAssetData& materialAssetData = materialAssetResult.value();

    if (!textureDataMap.contains(materialAssetData.textureAsset)) {
      const auto* textureEntry = resolver.find(materialAssetData.textureAsset);
      if (!textureEntry) {
        ATLANTIS_LOG_ERROR("a material's own referenced texture AssetId has no manifest entry");
        return ResultT::Err(RuntimeInitError::SceneDependencyUnresolved);
      }
      auto textureAssetResult =
          atlantis::asset_system::loadTextureAsset(textureEntry->artifactPath, textureEntry->metadataPath);
      if (textureAssetResult.isErr()) {
        ATLANTIS_LOG_ERROR("loadTextureAsset() failed for a material's own referenced texture");
        return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
      }
      textureDataMap.emplace(materialAssetData.textureAsset, std::move(textureAssetResult.value()));
    }

    materialDataMap.emplace(distinctMaterialIds[i], materialAssetData);
  }

  // (f) Instantiate -- infallible.
  atlantis::world::World world = atlantis::world::fromValidatedSceneData(scene);

  // (g) Publish -- the caller (RuntimeApplication::initializeSteps())
  //     performs the actual world_.emplace()/meshResourceMap_ = ...
  //     publish; this function's own return, by value, is itself
  //     already the transactional boundary -- nothing is written to
  //     any caller-owned state until this Ok() is actually consumed.
  return ResultT::Ok(SceneLoadOutcome{std::move(world), std::move(meshResourceMap), std::move(materialDataMap),
                                       std::move(textureDataMap)});
}

}  // namespace atlantis::runtime
