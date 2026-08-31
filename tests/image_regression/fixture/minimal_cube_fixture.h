#pragma once

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

#include "../support/pixel_diff.h"

#include <cstdint>
#include <memory>
#include <optional>

namespace atlantis::image_regression {

// Matches examples/headless_rendering_demo's own fixture exactly --
// duplicated, not shared via a new cross-example library (Plan 0010
// Section 7.1's own precedent: duplication is this project's default
// over introducing a shared-fixture module neither prior example
// needed). This duplication is deliberate and load-bearing: the
// calibration evidence ADR-0042's Context cites was captured against
// examples/headless_rendering_demo's exact vertex/camera/material
// values, and this fixture must reproduce them exactly, not merely "a
// similar cube."
struct MinimalCubeFixture {
  std::unique_ptr<atlantis::rhi::Device> device;
  std::optional<atlantis::renderer::Mesh> mesh;
  std::optional<atlantis::renderer::Material> material;
  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;
  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // independent HDR intermediate/fullscreen-triangle geometry/output-
  // transform Pipeline, created once at construction. The output-
  // transform shader pair's own SPIR-V is not retained past setup
  // (unlike the mesh shader pair above) -- it is consumed only to build
  // outputTransformPipeline below and, like vertexSpirv/fragmentSpirv,
  // never needed again after that.
  std::unique_ptr<atlantis::rhi::HdrColorTarget> hdrColorTarget;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleVertexBuffer;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleIndexBuffer;
  std::unique_ptr<atlantis::rhi::Sampler> outputTransformSampler;
  std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline;
};

inline constexpr std::uint32_t kFixtureExtentPixels = 512;
inline constexpr atlantis::rhi::Format kFixtureColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class FixtureSetupError { DeviceCreationFailed, ShaderLoadFailed, ResourceCreationFailed, AssetLoadFailed };

// Constructs every long-lived resource once (mirrors
// headless_rendering_demo's own setup sequence). Must be called with
// the process's current working directory set to a location where
// "shaders/minimal_mesh.{vert,frag}.spv" resolves (same relative-path
// convention every prior demo/GPU-test uses) -- see the golden
// generator's and image_regression_gpu_tests.cpp's own
// WORKING_DIRECTORY wiring.
[[nodiscard]] atlantis::Result<MinimalCubeFixture, FixtureSetupError> setUpMinimalCubeFixture();

// Plan 0012 Step 6: identical to setUpMinimalCubeFixture() in every
// respect except where the cube's vertex/index bytes come from -- here,
// loaded from a cooked Asset System runtime artifact (artifactPath) and
// its metadata sidecar (metadataPath) via
// atlantis::asset_system::loadStaticMeshAsset(), instead of the
// hand-authored kCubeVertices/kCubeIndices arrays. This is the
// composition root ADR-0043 requires: it resolves the VertexInputLayout
// via Shader System's existing rhi_integration surface and calls the
// existing, unmodified atlantis::renderer::createMesh() itself -- Asset
// System's own code never does either. setUpMinimalCubeFixture() itself
// is untouched, so the hand-authored path remains available as the
// comparison baseline.
[[nodiscard]] atlantis::Result<MinimalCubeFixture, FixtureSetupError> setUpMinimalCubeFixtureFromAsset(
    const char* artifactPath, const char* metadataPath);

enum class FixtureRenderError { AcquireFailed, CommandListCreationFailed, SubmitFailed, WaitIdleFailed };

// One full acquire -> draw -> copy -> submit -> waitIdle cycle (Spec
// 0010's own flow, unchanged), returning the readback buffer's contents
// as a PixelBuffer. May be called more than once against the same
// MinimalCubeFixture (OffscreenTarget's own repeated-cycle contract,
// ADR-0038) -- each call is independent.
[[nodiscard]] atlantis::Result<PixelBuffer, FixtureRenderError> renderOneFrame(MinimalCubeFixture& fixture);

}  // namespace atlantis::image_regression
