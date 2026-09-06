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
#include <unordered_map>
#include <vector>

namespace atlantis::image_regression {

// Plan 0028 Milestone 3: the integrated showcase demo fixture. Same
// resource-creation skeleton as PbrMaterialDemoFixture
// (pbr_material_demo_fixture.h/.cpp) -- this fixture links
// Atlantis::RuntimeHost directly and calls its real
// loadAndInstantiateScene()/realizePendingMaterials()/
// extractCameraMatrices()/extractFrameLightingData()/
// computeShadowLightSpaceMatrices() -- never a fixture-private
// reimplementation. Unlike that fixture, this one's shadow path is
// real: it writes the scene's own directional light's light-space
// matrices (not an identity sentinel) and, by default, casts all of
// its own DrawItems into the shadow map (Spec 0028 FR2/FR6).
struct IntegratedShowcaseDemoFixture {
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
  atlantis::rhi::VertexInputLayout skyVertexInputLayout;
  std::vector<std::uint32_t> skyVertexSpirv;
  std::vector<std::uint32_t> skyFragmentSpirv;
  atlantis::rhi::VertexInputLayout outputTransformUnormVertexInputLayout;
  std::vector<std::uint32_t> outputTransformUnormVertexSpirv;
  std::vector<std::uint32_t> outputTransformUnormFragmentSpirv;
  atlantis::rhi::VertexInputLayout shadowCastVertexInputLayout;
  std::vector<std::uint32_t> shadowCastVertexSpirv;
  std::vector<std::uint32_t> shadowCastFragmentSpirv;

  // Phase 1 (CPU) outputs, published once by
  // setUpIntegratedShowcaseDemoFixture() and never mutated afterward.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;
  std::optional<atlantis::asset_system::EnvironmentAssetData> environmentData;

  // Phase 2 (GPU) outputs, grown incrementally by
  // renderIntegratedShowcaseDemoFrame().
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

  // Plan 0028 Milestone 3 (FR4): the most recent
  // renderIntegratedShowcaseDemoFrame() call's own DrawItem count --
  // fixed at 6 for this scene (one ground plus five spheres), exposed
  // for the GPU test's own direct assertion.
  std::size_t lastDrawItemCount = 0;
};

inline constexpr std::uint32_t kIntegratedShowcaseDemoExtentPixels = 512;
inline constexpr atlantis::rhi::Format kIntegratedShowcaseDemoColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class IntegratedShowcaseDemoSetupError {
  ShaderLoadFailed,
  DeviceCreationFailed,
  SceneLoadFailed,
  ResourceCreationFailed,
};

// config's scene*/environment*/shader path fields must name
// integrated_showcase_demo_scene's own cooked artifacts and the real
// compiled shader outputs -- reusing atlantis::runtime::BootstrapConfig
// directly, mirroring setUpPbrMaterialDemoFixture()'s own identical
// convention.
[[nodiscard]] atlantis::Result<IntegratedShowcaseDemoFixture, IntegratedShowcaseDemoSetupError>
setUpIntegratedShowcaseDemoFixture(const atlantis::runtime::BootstrapConfig& config);

enum class IntegratedShowcaseDemoRenderError {
  AcquireFailed,
  NoActiveCamera,
  ExtractionFailed,
  LightExtractionFailed,
  CommandListCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
};

// One full acquire -> updateTransforms() -> Camera/Lighting/
// CameraWorldPosition/shadow-light-space extraction and publish ->
// realize (Phase 2) -> draw -> copy -> submit -> waitIdle cycle. May be
// called more than once against the same fixture, reusing the same
// GPU resources across every cycle -- never recreated.
//
// includeShadowCasters selects the frame's own shadowCasterDrawItems
// span (Spec 0028 FR6's own R1/R2 differential): true (the default,
// and the only path the golden generator ever uses) passes all of this
// frame's own DrawItems, matching RuntimeApplication::runFrame()'s own
// unconditional "shadowCasterDrawItems is drawItems itself" contract
// (Spec 0027 P6); false passes an empty span. Both variants otherwise
// render identically -- same scene, camera, light, materials.
[[nodiscard]] atlantis::Result<PixelBuffer, IntegratedShowcaseDemoRenderError> renderIntegratedShowcaseDemoFrame(
    IntegratedShowcaseDemoFixture& fixture, bool includeShadowCasters = true);

}  // namespace atlantis::image_regression
