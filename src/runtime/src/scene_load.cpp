#include <atlantis/runtime/scene_load.h>

#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_catalog.h>
#include <atlantis/asset_system/decode_scene.h>
#include <atlantis/asset_system/load.h>
#include <atlantis/asset_system/load_material.h>
#include <atlantis/asset_system/load_texture.h>
#include <atlantis/asset_system/material_artifact.h>
#include <atlantis/asset_system/mesh_artifact.h>
#include <atlantis/asset_system/scene_artifact.h>
#include <atlantis/asset_system/texture_artifact.h>
#include <atlantis/log.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/world/scene_instantiation.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace atlantis::runtime {

namespace {

using atlantis::asset_system::AssetCatalog;
using atlantis::asset_system::AssetCatalogRecord;
using atlantis::asset_system::CatalogAssetType;

// The artifact schemas this Runtime's loaders accept, per asset type (Plan
// 0047 P14): the catalog records each artifact's schema so a mismatch is
// reported before any artifact file is opened.
[[nodiscard]] bool isSupportedArtifactSchema(CatalogAssetType type, std::uint32_t schema) noexcept {
  switch (type) {
    case CatalogAssetType::Mesh:
      return schema == atlantis::asset_system::kMeshArtifactSchemaVersion ||
             schema == atlantis::asset_system::kMeshArtifactSchemaVersionU32;
    case CatalogAssetType::Texture:
      return schema == atlantis::asset_system::kTextureArtifactSchemaVersion;
    case CatalogAssetType::Material:
      return schema == atlantis::asset_system::kMaterialArtifactSchemaVersion;
    case CatalogAssetType::Scene:
      return schema == atlantis::asset_system::kSceneArtifactSchemaVersion;
    case CatalogAssetType::Environment:
    case CatalogAssetType::GltfImport:
      return false;
  }
  return false;
}

// Resolves `id` to its catalog record and checks it is `expected` with a
// supported artifact schema. Pure lookup, no I/O.
[[nodiscard]] atlantis::Result<const AssetCatalogRecord*, RuntimeInitError> resolveDependency(
    const AssetCatalog& catalog, atlantis::asset_system::AssetId id, CatalogAssetType expected) {
  using ResultT = atlantis::Result<const AssetCatalogRecord*, RuntimeInitError>;
  const AssetCatalogRecord* record = catalog.find(id);
  if (record == nullptr) {
    ATLANTIS_LOG_ERROR("scene references an AssetId with no catalog record");
    return ResultT::Err(RuntimeInitError::SceneDependencyUnresolved);
  }
  if (record->type != expected) {
    ATLANTIS_LOG_ERROR("a catalog record is not the asset type its referrer needs");
    return ResultT::Err(RuntimeInitError::DependencyTypeMismatch);
  }
  if (!isSupportedArtifactSchema(record->type, record->artifactSchema)) {
    ATLANTIS_LOG_ERROR("a catalog record's artifact schema is not supported by this Runtime");
    return ResultT::Err(RuntimeInitError::UnsupportedArtifactSchema);
  }
  return ResultT::Ok(record);
}

}  // namespace

atlantis::Result<SceneLoadOutcome, RuntimeInitError> loadAndInstantiateScene(
    const BootstrapConfig& config, atlantis::rhi::Device* device,
    const atlantis::rhi::VertexInputLayout& vertexInputLayout) {
  using ResultT = atlantis::Result<SceneLoadOutcome, RuntimeInitError>;
  using atlantis::renderer::createMesh;

  // (a) Load and validate the asset catalog -- local, immutable resolver
  // (Plan 0047 P14, ADR-0098 D3).
  auto catalogResult = atlantis::asset_system::loadAssetCatalog(config.assetCatalogPath);
  if (catalogResult.isErr()) {
    ATLANTIS_LOG_ERROR("loadAssetCatalog() failed: {}", toString(catalogResult.error()));
    return ResultT::Err(RuntimeInitError::AssetCatalogLoadFailed);
  }
  const AssetCatalog catalog = std::move(catalogResult.value());

  // (b) Resolve the configured scene, then decode its artifact -- fully
  // validated ValidatedSceneData.
  const AssetCatalogRecord* sceneRecord = catalog.find(config.sceneAsset);
  if (sceneRecord == nullptr || sceneRecord->type != CatalogAssetType::Scene) {
    ATLANTIS_LOG_ERROR("the configured scene GUID is not a scene record in the catalog");
    return ResultT::Err(RuntimeInitError::SceneNotInCatalog);
  }
  if (!isSupportedArtifactSchema(sceneRecord->type, sceneRecord->artifactSchema)) {
    ATLANTIS_LOG_ERROR("the scene record's artifact schema is not supported by this Runtime");
    return ResultT::Err(RuntimeInitError::UnsupportedArtifactSchema);
  }
  auto sceneResult = atlantis::asset_system::decodeScene(sceneRecord->artifact, sceneRecord->metadata);
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
  std::vector<const AssetCatalogRecord*> resolvedMeshEntries;
  for (atlantis::asset_system::AssetId id : distinctMeshIds) {
    auto resolved = resolveDependency(catalog, id, CatalogAssetType::Mesh);
    if (resolved.isErr()) return ResultT::Err(resolved.error());
    resolvedMeshEntries.push_back(resolved.value());
  }
  std::vector<const AssetCatalogRecord*> resolvedMaterialEntries;
  for (atlantis::asset_system::AssetId id : distinctMaterialIds) {
    auto resolved = resolveDependency(catalog, id, CatalogAssetType::Material);
    if (resolved.isErr()) return ResultT::Err(resolved.error());
    resolvedMaterialEntries.push_back(resolved.value());
  }

  // (e) Phase 2: load, same order as (c)/(d) -- distinctMeshIds' own
  // index order IS the mesh load order; the map below is populated in
  // that order but is a keyed store, never iterated afterward in any
  // order-sensitive way. device is only ever dereferenced here, and
  // only when distinctMeshIds is non-empty -- see this function's own
  // header comment on why a test may pass nullptr otherwise.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  for (std::size_t i = 0; i < distinctMeshIds.size(); ++i) {
    auto meshAssetResult = atlantis::asset_system::loadStaticMeshAsset(resolvedMeshEntries[i]->artifact,
                                                                        resolvedMeshEntries[i]->metadata);
    if (meshAssetResult.isErr()) {
      ATLANTIS_LOG_ERROR("loadStaticMeshAsset() failed for a scene dependency");
      return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
    }
    const atlantis::asset_system::StaticMeshAssetData& meshAssetData = meshAssetResult.value();
    ATLANTIS_CHECK_MSG(device != nullptr, "loadAndInstantiateScene(): a real Device is required once a scene has "
                                           "at least one distinct mesh dependency to load");
    // Spec 0039 Requirement 6 / ADR-0087: the composition-root
    // translation between the Asset System's own MeshIndexType and the
    // RHI's IndexType, which ADR-0043 forbids those two modules from
    // sharing. Reading the wrong accessor for the asset's own width is
    // a precondition violation, so this branch is what keeps a
    // schema-5 dependency from aborting here -- one scene may mix both
    // widths freely.
    auto createResult = meshAssetData.indexType() == atlantis::asset_system::MeshIndexType::Uint32
                             ? createMesh(*device, vertexInputLayout, meshAssetData.vertexBytes().data(),
                                          meshAssetData.vertexBytes().size(), meshAssetData.indices32().data(),
                                          static_cast<std::uint32_t>(meshAssetData.indices32().size()))
                             : createMesh(*device, vertexInputLayout, meshAssetData.vertexBytes().data(),
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
    auto materialAssetResult = atlantis::asset_system::loadMaterialAsset(resolvedMaterialEntries[i]->artifact,
                                                                          resolvedMaterialEntries[i]->metadata);
    if (materialAssetResult.isErr()) {
      ATLANTIS_LOG_ERROR("loadMaterialAsset() failed for a scene dependency");
      return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
    }
    const atlantis::asset_system::MaterialAssetData& materialAssetData = materialAssetResult.value();

    if (!textureDataMap.contains(materialAssetData.textureAsset)) {
      auto textureResolved = resolveDependency(catalog, materialAssetData.textureAsset, CatalogAssetType::Texture);
      if (textureResolved.isErr()) return ResultT::Err(textureResolved.error());
      const AssetCatalogRecord* textureEntry = textureResolved.value();
      auto textureAssetResult =
          atlantis::asset_system::loadTextureAsset(textureEntry->artifact, textureEntry->metadata);
      if (textureAssetResult.isErr()) {
        ATLANTIS_LOG_ERROR("loadTextureAsset() failed for a material's own referenced texture");
        return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
      }
      textureDataMap.emplace(materialAssetData.textureAsset, std::move(textureAssetResult.value()));
    }

    // Plan 0023 Milestone 5 (ADR-0066 item 6): PbrDirectLit-only base-
    // color-texture Rgba8Srgb requirement -- cookMaterial() can never
    // run this check (it never resolves its own texture reference,
    // ADR-0059 Decision 7), so this is the first point in the pipeline
    // with both this material's own kind and its resolved texture's own
    // real colorSpace. Looked up uniformly here, not duplicated into
    // both branches above -- correct whether this material's own
    // texture was just loaded this iteration or already present in
    // textureDataMap from an earlier material's own dedup (D10). Every
    // existing UnlitTextured/LitTextured Material is unaffected.
    if (materialAssetData.kind == atlantis::asset_system::MaterialKind::PbrDirectLit) {
      const atlantis::asset_system::TextureAssetData& textureData = textureDataMap.at(materialAssetData.textureAsset);
      if (textureData.colorSpace != atlantis::asset_system::TextureColorSpace::Srgb) {
        ATLANTIS_LOG_ERROR("PbrDirectLit material's own base-color texture is not Rgba8Srgb");
        return ResultT::Err(RuntimeInitError::PbrBaseColorTextureNotSrgb);
      }
    }

    // Plan 0029 Section P8 (ADR-0074 Section 1 item 6): the optional
    // normal-map texture -- resolved/loaded/deduplicated through the
    // same textureDataMap the base-color texture already uses (no new
    // map, no new cache), then cross-validated for Unorm exactly like
    // the base-color texture's own Srgb requirement above. A material
    // with no normal map (normalMapTexture == 0) is completely
    // unaffected.
    if (materialAssetData.normalMapTexture != 0 && !textureDataMap.contains(materialAssetData.normalMapTexture)) {
      auto normalMapResolved =
          resolveDependency(catalog, materialAssetData.normalMapTexture, CatalogAssetType::Texture);
      if (normalMapResolved.isErr()) return ResultT::Err(normalMapResolved.error());
      const AssetCatalogRecord* normalMapEntry = normalMapResolved.value();
      auto normalMapResult =
          atlantis::asset_system::loadTextureAsset(normalMapEntry->artifact, normalMapEntry->metadata);
      if (normalMapResult.isErr()) {
        ATLANTIS_LOG_ERROR("loadTextureAsset() failed for a material's own referenced normal-map texture");
        return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
      }
      textureDataMap.emplace(materialAssetData.normalMapTexture, std::move(normalMapResult.value()));
    }
    if (materialAssetData.normalMapTexture != 0) {
      const auto& normalMapData = textureDataMap.at(materialAssetData.normalMapTexture);
      if (normalMapData.colorSpace != atlantis::asset_system::TextureColorSpace::Unorm) {
        ATLANTIS_LOG_ERROR("PbrDirectLit material's own normal-map texture is not Unorm");
        return ResultT::Err(RuntimeInitError::PbrNormalMapTextureNotUnorm);
      }
    }

    // Plan 0046 Milestone 1 (ADR-0096): the optional emissive texture,
    // resolved/loaded/deduplicated through the same textureDataMap. No
    // colour-space requirement: glTF's emissive is sRGB colour, and a
    // Unorm mask samples just as well.
    if (materialAssetData.emissiveTexture != 0 && !textureDataMap.contains(materialAssetData.emissiveTexture)) {
      auto emissiveResolved =
          resolveDependency(catalog, materialAssetData.emissiveTexture, CatalogAssetType::Texture);
      if (emissiveResolved.isErr()) return ResultT::Err(emissiveResolved.error());
      const AssetCatalogRecord* emissiveEntry = emissiveResolved.value();
      auto emissiveResult =
          atlantis::asset_system::loadTextureAsset(emissiveEntry->artifact, emissiveEntry->metadata);
      if (emissiveResult.isErr()) {
        ATLANTIS_LOG_ERROR("loadTextureAsset() failed for a material's own referenced emissive texture");
        return ResultT::Err(RuntimeInitError::SceneDependencyLoadFailed);
      }
      textureDataMap.emplace(materialAssetData.emissiveTexture, std::move(emissiveResult.value()));
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
