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
#include <atlantis/rhi/shadow_map.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/environment_realization.h>
#include <atlantis/world/world.h>

#include "../support/pixel_diff.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlantis::image_regression {

// Plan 0029 Section P19 (ADR-0074): the normal-map demo fixture. Same
// resource-creation skeleton as IntegratedShowcaseDemoFixture
// (integrated_showcase_demo_fixture.h/.cpp) -- combined PBR+IBL+sky+
// shadow wiring, a real (non-empty-by-default) shadow path -- plus the
// two new normal-map PBR shader trios (Milestone 3) and a second,
// fixture-owned "control" Material (the discriminative test's own "B"
// twin), realized once, independently of the scene's own real
// material, and never captured as a golden.
struct PbrNormalMapDemoFixture {
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
  // Plan 0029 Section P19: the two normal-map PBR shader pairs' own
  // resolved layout/SPIR-V -- unlike pbrDirectLit*/pbrIbl* above, these
  // are genuinely used by this fixture's own real, normal-mapped
  // sphere material, not dead-path filler.
  atlantis::rhi::VertexInputLayout pbrDirectLitNormalMapVertexInputLayout;
  std::vector<std::uint32_t> pbrDirectLitNormalMapVertexSpirv;
  std::vector<std::uint32_t> pbrDirectLitNormalMapFragmentSpirv;
  atlantis::rhi::VertexInputLayout pbrIblNormalMapVertexInputLayout;
  std::vector<std::uint32_t> pbrIblNormalMapVertexSpirv;
  std::vector<std::uint32_t> pbrIblNormalMapFragmentSpirv;
  atlantis::rhi::VertexInputLayout skyVertexInputLayout;
  std::vector<std::uint32_t> skyVertexSpirv;
  std::vector<std::uint32_t> skyFragmentSpirv;
  atlantis::rhi::VertexInputLayout outputTransformUnormVertexInputLayout;
  std::vector<std::uint32_t> outputTransformUnormVertexSpirv;
  std::vector<std::uint32_t> outputTransformUnormFragmentSpirv;
  atlantis::rhi::VertexInputLayout shadowCastVertexInputLayout;
  std::vector<std::uint32_t> shadowCastVertexSpirv;
  std::vector<std::uint32_t> shadowCastFragmentSpirv;

  // Phase 1 (CPU) outputs, published once by setUpPbrNormalMapDemoFixture()
  // and never mutated afterward.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;
  std::optional<atlantis::asset_system::EnvironmentAssetData> environmentData;

  // Plan 0029 Section P19 (Fixture A/B mechanism, step 3): the control
  // material's own CPU-side data, resolved once via loadMaterialAsset()
  // against the compile-time-known
  // ATLANTIS_pbr_normal_mapped_control_{ARTIFACT,METADATA}_PATH --
  // never through the scene's own materialDataMap (referenced by zero
  // scene nodes).
  std::optional<atlantis::asset_system::MaterialAssetData> controlMaterialData;
  atlantis::asset_system::AssetId controlMaterialAssetId = 0;

  // Phase 2 (GPU) outputs, grown incrementally by
  // renderPbrNormalMapDemoFrame().
  std::optional<atlantis::runtime::EnvironmentLightingResources> environmentLightingResources;
  std::size_t environmentUploadCount = 0;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>
      sampledTextureResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::Sampler>> samplerResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::renderer::Material>>
      materialResourceMap;

  // Plan 0029 Section P19: the control material's own GPU resources --
  // realized once (lazily, on the first renderPbrNormalMapDemoFrame()
  // call, alongside the scene's own real material realization, so it
  // can dedup its shared base-color texture against whatever this SAME
  // frame just realized), kept alive independently of
  // materialResourceMap above (this Material is never scene-referenced,
  // so it never belongs in that AssetId-keyed map).
  std::unique_ptr<atlantis::rhi::Sampler> controlSampler;
  std::unique_ptr<atlantis::renderer::Material> controlMaterial;

  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;
  std::unique_ptr<atlantis::rhi::HdrColorTarget> hdrColorTarget;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleVertexBuffer;
  std::unique_ptr<atlantis::rhi::Buffer> fullscreenTriangleIndexBuffer;
  std::unique_ptr<atlantis::rhi::Sampler> outputTransformSampler;
  std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline;
  std::unique_ptr<atlantis::rhi::Pipeline> skyPipeline;
  std::unique_ptr<atlantis::rhi::ShadowMap> shadowMap;
  std::unique_ptr<atlantis::rhi::Sampler> shadowMapSampler;
  std::unique_ptr<atlantis::rhi::Pipeline> shadowCastPipeline;
  std::unique_ptr<atlantis::rhi::Buffer> shadowLightSpaceBuffer;

  std::optional<atlantis::world::World> world;

  std::size_t lastDrawItemCount = 0;
};

inline constexpr std::uint32_t kPbrNormalMapDemoExtentPixels = 512;
inline constexpr atlantis::rhi::Format kPbrNormalMapDemoColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class PbrNormalMapDemoSetupError {
  ShaderLoadFailed,
  DeviceCreationFailed,
  SceneLoadFailed,
  ResourceCreationFailed,
};

// config's scene*/environment*/shader path fields must name
// pbr_normal_map_demo_scene's own cooked artifacts and the real
// compiled shader outputs (including the two new normal-map pairs) --
// reusing atlantis::runtime::BootstrapConfig directly, mirroring
// setUpIntegratedShowcaseDemoFixture()'s own identical convention.
//
// controlMaterialArtifactPath/controlMaterialMetadataPath name the
// control material's own cooked artifacts -- passed as explicit
// parameters, not new BootstrapConfig fields, since the control
// material is this one demo's own fixture-private concept (referenced
// by zero scene nodes, never part of Runtime's shared, cross-demo
// BootstrapConfig contract). The caller reads these from the
// ATLANTIS_pbr_normal_mapped_control_{ARTIFACT,METADATA}_PATH compile
// definitions, mirroring how every other fixture/gpu_tests file already
// reads a scene's own compile-def-sourced paths before building its own
// BootstrapConfig.
[[nodiscard]] atlantis::Result<PbrNormalMapDemoFixture, PbrNormalMapDemoSetupError> setUpPbrNormalMapDemoFixture(
    const atlantis::runtime::BootstrapConfig& config, const std::string& controlMaterialArtifactPath,
    const std::string& controlMaterialMetadataPath);

enum class PbrNormalMapDemoRenderError {
  AcquireFailed,
  NoActiveCamera,
  ExtractionFailed,
  LightExtractionFailed,
  CommandListCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
  ControlMaterialRealizationFailed,
};

// One full acquire -> updateTransforms() -> Camera/Lighting/
// CameraWorldPosition/shadow-light-space extraction and publish ->
// realize (Phase 2, including the control material's own one-time,
// lazy realization) -> draw -> copy -> submit -> waitIdle cycle. May be
// called more than once against the same fixture, reusing the same GPU
// resources across every cycle -- never recreated.
//
// includeShadowCasters selects the frame's own shadowCasterDrawItems
// span (Plan 0029 Section P19's own R1/R2 shadow differential): true
// (the default, and the only path the golden generator ever uses)
// passes every DrawItem; false passes an empty span.
//
// useControlMaterial selects which Material the sphere's own DrawItem
// borrows (Plan 0029 Section P19's own Fixture A/B mechanism, the
// normal-map differential): false (the default, and the only path the
// golden generator ever uses) is R1, the scene's real, normal-mapped
// Material; true is R2, a copy of R1's own DrawItems with only the
// sphere's `.material` pointer reassigned to `&fixture.controlMaterial`
// -- mesh, transform, camera, lighting, environment, and shadow
// resources are all identical to R1.
[[nodiscard]] atlantis::Result<PixelBuffer, PbrNormalMapDemoRenderError> renderPbrNormalMapDemoFrame(
    PbrNormalMapDemoFixture& fixture, bool includeShadowCasters = true, bool useControlMaterial = false);

}  // namespace atlantis::image_regression
