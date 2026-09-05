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
#include <atlantis/rhi/shadow_map.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>
#include <atlantis/world/world.h>

#include "../support/pixel_diff.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace atlantis::image_regression {

// Plan 0014 Section D10: sibling to MinimalCubeFixture, same target
// (atlantis_image_regression_fixture), same "duplicated, not shared"
// precedent -- this fixture's own scene-construction and per-frame
// extraction code duplicates src/runtime/'s own logic independently,
// matching ADR-0051's own explicit rejection of a shared "Extraction
// module."
struct WorldSceneFixture {
  std::unique_ptr<atlantis::rhi::Device> device;
  std::optional<atlantis::renderer::Mesh> mesh;
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
  // Plan 0027 Milestone 9 (ADR-0072 D-1/P9e): a minimal, always-possible
  // real ShadowMap/Sampler/Pipeline/Buffer -- created once at
  // construction, alongside outputTransformPipeline above.
  std::unique_ptr<atlantis::rhi::ShadowMap> shadowMap;
  std::unique_ptr<atlantis::rhi::Sampler> shadowMapSampler;
  std::unique_ptr<atlantis::rhi::Pipeline> shadowCastPipeline;
  std::unique_ptr<atlantis::rhi::Buffer> shadowLightSpaceBuffer;
  atlantis::world::World world;
  atlantis::asset_system::AssetId knownMinimalCubeAssetId = 0;
};

enum class WorldSceneFixtureSetupError { DeviceCreationFailed, ShaderLoadFailed, ResourceCreationFailed,
                                           AssetLoadFailed, SceneConstructionFailed };

// Constructs every long-lived resource once and builds Plan 0014
// Section D9's own six-entity validation scene via the same World
// public API calls Runtime uses (duplicated construction code, not a
// shared "build the scene" function). Must be called with the process's
// current working directory set to a location where
// "shaders/minimal_mesh.{vert,frag}.spv" resolves, matching every other
// fixture/tool in this directory's own established convention.
[[nodiscard]] atlantis::Result<WorldSceneFixture, WorldSceneFixtureSetupError> setUpWorldSceneFixture(
    const char* artifactPath, const char* metadataPath);

enum class WorldSceneFixtureRenderError { AcquireFailed, NoActiveCamera, ExtractionFailed,
                                            CommandListCreationFailed, SubmitFailed, WaitIdleFailed };

// One full acquire -> draw -> copy -> submit -> waitIdle cycle, mirroring
// renderOneFrame() exactly through the sequence, with its own
// camera-matrix-write and DrawItem-building steps replaced by the same
// logic src/runtime/'s own runFrame() uses (updateTransforms(), camera
// extraction, per-entity resolveMeshAsset()), duplicated independently.
[[nodiscard]] atlantis::Result<PixelBuffer, WorldSceneFixtureRenderError> renderOneWorldSceneFrame(
    WorldSceneFixture& fixture);

}  // namespace atlantis::image_regression
