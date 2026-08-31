#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/hdr_color_target.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>
#include <atlantis/world/world.h>

#include "../support/pixel_diff.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>

namespace atlantis::image_regression {

// Plan 0015 Section D11/Step 9: sibling to WorldSceneFixture, same
// target (atlantis_image_regression_fixture) -- builds the identical
// six-entity scene, but through the REAL cook -> decode -> resolve ->
// load -> instantiate pipeline (Plan 0015 Section D10's own steps),
// duplicated independently here rather than linking
// Atlantis::RuntimeHost (Runtime-private -- "No other top-level module
// may depend on Atlantis::RuntimeHost", runtime_application.h's own
// comment) -- matching this directory's own already-established
// "duplicated, not shared" precedent (WorldSceneFixture's own top-of-
// file comment) for exactly this class of scene-construction/
// extraction logic. meshResourceMap replaces WorldSceneFixture's own
// single optional<Mesh>, matching RuntimeApplication's own D10 change.
struct WorldSceneLoadedFixture {
  std::unique_ptr<atlantis::rhi::Device> device;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::optional<atlantis::renderer::Material> material;
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;
  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // independent HDR intermediate/fullscreen-triangle geometry/output-
  // transform Pipeline, created once at construction.
  std::unique_ptr<atlantis::rhi::HdrColorTarget> hdrColorTarget;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleVertexBuffer;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleIndexBuffer;
  std::unique_ptr<atlantis::rhi::Sampler> outputTransformSampler;
  std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline;
  // std::optional, not a bare World: World is move-constructible but
  // NOT move-assignable (ADR-0049/Spec 0014) -- fromValidatedSceneData()
  // returns a World by value, so publishing it here needs emplace()'s
  // own in-place move-construction, exactly matching
  // RuntimeApplication's own world_ (D2/D10).
  std::optional<atlantis::world::World> world;
};

enum class WorldSceneLoadedFixtureSetupError {
  DeviceCreationFailed,
  ShaderLoadFailed,
  ManifestLoadFailed,
  SceneArtifactLoadFailed,
  SceneDependencyUnresolved,
  SceneDependencyLoadFailed,
  ResourceCreationFailed,
};

// Must be called with the process's current working directory set to a
// location where "shaders/minimal_mesh.{vert,frag}.spv" resolves,
// matching every other fixture/tool in this directory's own
// established convention. sceneArtifactPath/sceneMetadataPath/
// sceneManifestPath are the real, build-tree world_scene paths
// atlantis_add_scene_asset() produces (Section D7).
[[nodiscard]] atlantis::Result<WorldSceneLoadedFixture, WorldSceneLoadedFixtureSetupError>
setUpWorldSceneLoadedFixture(const char* sceneArtifactPath, const char* sceneMetadataPath,
                              const char* sceneManifestPath);

enum class WorldSceneLoadedFixtureRenderError {
  AcquireFailed,
  NoActiveCamera,
  ExtractionFailed,
  CommandListCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
};

// One full acquire -> draw -> copy -> submit -> waitIdle cycle,
// mirroring renderOneWorldSceneFrame() exactly, with its own mesh
// lookup keyed by meshResourceMap instead of a single Mesh.
[[nodiscard]] atlantis::Result<PixelBuffer, WorldSceneLoadedFixtureRenderError> renderOneWorldSceneLoadedFrame(
    WorldSceneLoadedFixture& fixture);

}  // namespace atlantis::image_regression
