#pragma once

#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/asset_system/validated_scene_data.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/result.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/world.h>

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlantis::runtime {

// Plan 0018 Section P11: materialDataMap/textureDataMap are CPU-only --
// no SampledTexture, Sampler, Pipeline, or Material is ever constructed
// by the scene load (Spec 0018 D8 Phase 1's own hard constraint: no
// RenderTarget exists yet at this point in initializeSteps()).
// materialDataMap is keyed by material AssetId; textureDataMap is keyed by
// texture AssetId (D10's own free deduplication -- two materials naming the
// same texture load it once).
//
// Spec 0051 R7 / ADR-0102 D6 (Plan 0051 P5): the scene is the bake output --
// the Runtime World Runtime's frame reads -- not an authoring world::World.
// Its EntityGuid -> EntityId map (scene.entities) is the EntityRef
// resolver's input (ADR-0097 D6).
// Plan 0062 P5 (ADR-0123 D2): one mesh's CPU geometry for picking -- its
// positions (the vertex layout's location-0 Float3), its triangle list's
// indices widened to 32 bits, and its local axis-aligned bounds. Retained
// only when BootstrapConfig::retainPickGeometry is set.
struct PickMeshGeometry {
  std::vector<std::array<float, 3>> positions;
  std::vector<std::uint32_t> indices;
  std::array<float, 3> localMin{0.0f, 0.0f, 0.0f};
  std::array<float, 3> localMax{0.0f, 0.0f, 0.0f};
};

// Plan 0062 P3 (ADR-0123 D2, D3): the pick service's ray (world space; the
// direction unit length) and its answer -- the nearest hit entity and the
// distance to it along the ray.
struct PickRay {
  std::array<float, 3> origin{0.0f, 0.0f, 0.0f};
  std::array<float, 3> direction{0.0f, 0.0f, -1.0f};
};

struct PickHit {
  atlantis::asset_system::EntityGuid entity;
  float distance = 0.0f;
  friend bool operator==(const PickHit&, const PickHit&) = default;
};

// An entity's mesh local bounds (RuntimeApplication::renderableLocalBounds()).
struct LocalBounds {
  std::array<float, 3> min{0.0f, 0.0f, 0.0f};
  std::array<float, 3> max{0.0f, 0.0f, 0.0f};
  friend bool operator==(const LocalBounds&, const LocalBounds&) = default;
};

struct SceneLoadOutcome {
  atlantis::world::BakedScene scene;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;
  // Plan 0047 P17: the loaded scene's persistent identity.
  atlantis::asset_system::AssetGuid sceneGuid;
  // Plan 0062 P5: keyed by mesh AssetId; empty unless the configuration
  // asked for it (BootstrapConfig::retainPickGeometry).
  std::unordered_map<atlantis::asset_system::AssetId, PickMeshGeometry> pickGeometry;
};

// Plan 0015 Section D10, steps (a)-(g) -- factored out of
// RuntimeApplication::initializeSteps() into its own free function so
// V20's own manifest-load, scene-decode, and dependency-unresolved
// failure paths are directly testable without a real Platform session
// or GPU Device: each of those three conditions is detected at steps
// (a)/(b)/(d), strictly before device is ever dereferenced (step (e),
// mesh creation, is the only step that touches it, and only when
// distinctIds is non-empty) -- a test exercising only those three
// conditions, or a scene with no Renderable references at all, may
// safely pass nullptr. RuntimeApplication::initializeSteps() itself
// always passes a real, already-constructed Device. Spec 0051 (ruling
// Q1 B2): catalog -> decode -> dependency load -> bakeScene().
[[nodiscard]] atlantis::Result<SceneLoadOutcome, RuntimeInitError> loadAndBakeScene(
    const BootstrapConfig& config, atlantis::rhi::Device* device,
    const atlantis::rhi::VertexInputLayout& vertexInputLayout);

// Plan 0065 P4 (ADR-0131 D4): what a running Runtime has loaded, which a
// reload reuses instead of loading again.
struct LoadedSceneResources {
  const std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh>& meshes;
  const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData>& materials;
  const std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData>& textures;
};

// A reload's load and bake: `scene`'s mesh and material references resolved
// through the catalog at `assetCatalogPath` and everything `loaded` lacks
// loaded -- meshes uploaded (and their pick geometry built when retained),
// material and texture data read -- with loadAndBakeScene()'s own checks and
// errors; then bakeScene(). Nothing in `loaded` is touched: the caller swaps.
// meshes/materials/textures name everything the new scene references, new or
// reused, so the swap can release the rest.
struct SceneReloadOutcome {
  atlantis::world::BakedScene scene;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> newMeshes;
  std::unordered_map<atlantis::asset_system::AssetId, PickMeshGeometry> newPickGeometry;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> newMaterials;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> newTextures;
  std::vector<atlantis::asset_system::AssetId> meshes;
  std::vector<atlantis::asset_system::AssetId> materials;
  std::vector<atlantis::asset_system::AssetId> textures;
};

[[nodiscard]] atlantis::Result<SceneReloadOutcome, RuntimeInitError> loadSceneForReload(
    const std::string& assetCatalogPath, const atlantis::asset_system::ValidatedSceneData& scene,
    const LoadedSceneResources& loaded, atlantis::rhi::Device* device,
    const atlantis::rhi::VertexInputLayout& vertexInputLayout, bool retainPickGeometry);

}  // namespace atlantis::runtime
