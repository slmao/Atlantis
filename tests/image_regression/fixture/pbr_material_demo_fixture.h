#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/environment_types.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/hdr_color_target.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/environment_realization.h>
#include <atlantis/world/world.h>

#include "../support/pixel_diff.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace atlantis::image_regression {

// Plan 0023 Milestone 8: the pbr_material_demo fixture. Mirrors
// LightingDemoFixture's own exact shape (lighting_demo_fixture.h's own
// top-of-file comment) -- this fixture links Atlantis::RuntimeHost
// directly and calls its real, Runtime-private loadAndInstantiateScene()
// (Phase 1 CPU load), computePendingMaterialIds()/realizePendingMaterials()
// (Phase 2 GPU realization), extractCameraMatrices()/
// extractCameraWorldPosition()/extractFrameLightingData()/
// resolveMeshAsset()/resolveMaterialAsset()/checkConformalTransform()
// helpers -- never a fixture-private reimplementation of any of them.
//
// Unlike LightingDemoFixture, this fixture owns its own, independent
//320-byte Camera/Lighting/CameraWorldPosition buffer (Plan 0023
// Milestone 2) -- a from-scratch composition root, not
// RuntimeApplication, so it is free to size its own buffer against the
// CURRENT layout directly, never the pre-Milestone-2 304-byte one
// LightingDemoFixture itself still uses (confirmed out of scope by
// Milestone 2, unaffected by this Plan).
struct PbrMaterialDemoFixture {
  std::unique_ptr<atlantis::rhi::Device> device;
  atlantis::rhi::VertexInputLayout unlitTexturedVertexInputLayout;
  std::vector<std::uint32_t> unlitTexturedVertexSpirv;
  std::vector<std::uint32_t> unlitTexturedFragmentSpirv;
  atlantis::rhi::VertexInputLayout litTexturedVertexInputLayout;
  std::vector<std::uint32_t> litTexturedVertexSpirv;
  std::vector<std::uint32_t> litTexturedFragmentSpirv;
  atlantis::rhi::VertexInputLayout pbrDirectLitVertexInputLayout;
  std::vector<std::uint32_t> pbrDirectLitVertexSpirv;
  std::vector<std::uint32_t> pbrDirectLitFragmentSpirv;
  atlantis::rhi::VertexInputLayout pbrIblVertexInputLayout;
  std::vector<std::uint32_t> pbrIblVertexSpirv;
  std::vector<std::uint32_t> pbrIblFragmentSpirv;
  // Plan 0026 Milestone 5 (ADR-0071): the sky shader pair's own resolved
  // layout/SPIR-V, populated only when config.environmentArtifactPath is
  // non-empty -- mirrors pbrIblVertexInputLayout/.../pbrIblFragmentSpirv's
  // own identical shape and gating.
  atlantis::rhi::VertexInputLayout skyVertexInputLayout;
  std::vector<std::uint32_t> skyVertexSpirv;
  std::vector<std::uint32_t> skyFragmentSpirv;
  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // colorFormat (kPbrMaterialDemoColorFormat below) is Rgba8Unorm and
  // never changes at runtime -- unlike RuntimeApplication, no
  // isSrgbFormat() call is needed here; this fixture loads and builds
  // only the UNORM output-transform variant, once, at construction.
  atlantis::rhi::VertexInputLayout outputTransformUnormVertexInputLayout;
  std::vector<std::uint32_t> outputTransformUnormVertexSpirv;
  std::vector<std::uint32_t> outputTransformUnormFragmentSpirv;

  // Phase 1 (CPU) outputs, published once by setUpPbrMaterialDemoFixture()
  // and never mutated afterward.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;
  std::optional<atlantis::asset_system::EnvironmentAssetData> environmentData;

  // Phase 2 (GPU) outputs, grown incrementally by renderPbrMaterialDemoFrame().
  std::optional<atlantis::runtime::EnvironmentLightingResources> environmentLightingResources;
  std::size_t environmentUploadCount = 0;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>
      sampledTextureResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::Sampler>> samplerResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::renderer::Material>>
      materialResourceMap;

  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;
  // Plan 0024 Milestone 7 (ADR-0068 D-1/D-3/D-6): this fixture's own
  // independent HDR intermediate/fullscreen-triangle geometry/output-
  // transform Pipeline, created once at construction (Plan 0023's own
  // "fixture owns its own camera buffer, not shared code" precedent) --
  // never RuntimeApplication's own members.
  std::unique_ptr<atlantis::rhi::HdrColorTarget> hdrColorTarget;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleVertexBuffer;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleIndexBuffer;
  std::unique_ptr<atlantis::rhi::Sampler> outputTransformSampler;
  std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline;
  // Plan 0026 Milestone 5 (ADR-0071 P5): the sky Pipeline -- a raw
  // Pipeline, not a Material, created once at construction only when an
  // environment is configured, mirroring outputTransformPipeline's own
  // "created once, format/extent-independent" lifecycle exactly. Null
  // for pbr_material_demo's own no-environment config.
  std::unique_ptr<atlantis::rhi::Pipeline> skyPipeline;

  std::optional<atlantis::world::World> world;
};

inline constexpr std::uint32_t kPbrMaterialDemoExtentPixels = 512;
inline constexpr atlantis::rhi::Format kPbrMaterialDemoColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class PbrMaterialDemoSetupError {
  ShaderLoadFailed,
  DeviceCreationFailed,
  SceneLoadFailed,
  ResourceCreationFailed,
};

// config's sceneArtifactPath/sceneMetadataPath/sceneDependencyManifestPath
// must name pbr_material_demo_scene's own cooked artifacts, and the
// unlitTextured*/litTextured*/pbrDirectLit* shader path fields must name
// their own respective compiled shader pairs' outputs -- reusing
// atlantis::runtime::BootstrapConfig directly, mirroring
// setUpLightingDemoFixture()'s own identical convention. Plan 0024
// Milestone 7: config's outputTransformUnorm* fields must likewise name
// the output_transform_unorm shader pair's own compiled outputs --
// outputTransformSrgb* is never read (this fixture's own colorFormat is
// fixed Rgba8Unorm).
[[nodiscard]] atlantis::Result<PbrMaterialDemoFixture, PbrMaterialDemoSetupError> setUpPbrMaterialDemoFixture(
    const atlantis::runtime::BootstrapConfig& config);

enum class PbrMaterialDemoRenderError {
  AcquireFailed,
  NoActiveCamera,
  ExtractionFailed,
  LightExtractionFailed,
  CommandListCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
};

// One full acquire -> updateTransforms() -> Camera/Lighting/
// CameraWorldPosition extraction and publish (unconditional, every
// call, matching Plan 0022's own corrected dynamic contract) ->
// realize (Phase 2) -> draw (gated by checkConformalTransform() for
// every LitTextured- or PbrDirectLit-bound entity) -> copy -> submit
// -> waitIdle cycle. May be called more than once against the same
// PbrMaterialDemoFixture, reusing the same cameraBuffer/depthTexture/
// offscreenTarget/readbackBuffer/Mesh/Material/Pipeline/descriptor-set
// map across every cycle -- never recreated.
[[nodiscard]] atlantis::Result<PixelBuffer, PbrMaterialDemoRenderError> renderPbrMaterialDemoFrame(
    PbrMaterialDemoFixture& fixture);

}  // namespace atlantis::image_regression
