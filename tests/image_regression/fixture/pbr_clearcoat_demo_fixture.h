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

// Plan 0035 Milestone 2: the pbr_clearcoat_demo fixture. Duplicated, not
// shared, from PbrMaterialDemoFixture (pbr_material_demo_fixture.h's own
// top-of-file comment establishes this repository's "duplicated, not
// shared" convention for from-scratch composition roots) -- this fixture
// links Atlantis::RuntimeHost directly and calls its real, Runtime-private
// loadAndInstantiateScene()/computePendingMaterialIds()/
// realizePendingMaterials()/extractCameraMatrices()/
// extractCameraWorldPosition()/extractFrameLightingData()/
// resolveMeshAsset()/resolveMaterialAsset()/checkConformalTransform()
// helpers -- never a fixture-private reimplementation of any of them.
//
// Unlike PbrMaterialDemoFixture, this fixture ALSO loads the real
// pbrClearcoatIbl*/pbrClearcoatIblNormalMap* shader trios and passes them
// as the genuine trailing trio arguments to realizePendingMaterials() (not
// the dead-path-filler reuse every other existing fixture uses for these
// two trios, since none of their own scenes reference a
// MaterialKind::PbrClearcoat material). This fixture's own scenes always
// configure an environment (ADR-0081: selectShaderPair() requires
// environmentEnabled for PbrClearcoat), so the clearcoat shader pairs are
// loaded unconditionally alongside pbrIbl*/sky* below, mirroring those
// trios' own identical gating.
struct PbrClearcoatDemoFixture {
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
  // Plan 0035 Milestone 2 (ADR-0081): the two new clearcoat shader
  // trios' own resolved layout/SPIR-V -- unlike every other existing
  // fixture, these are REAL, always populated (this fixture's own scenes
  // always configure an environment), and passed as the genuine trailing
  // trio arguments to realizePendingMaterials(), never dead-path filler.
  atlantis::rhi::VertexInputLayout pbrClearcoatIblVertexInputLayout;
  std::vector<std::uint32_t> pbrClearcoatIblVertexSpirv;
  std::vector<std::uint32_t> pbrClearcoatIblFragmentSpirv;
  atlantis::rhi::VertexInputLayout pbrClearcoatIblNormalMapVertexInputLayout;
  std::vector<std::uint32_t> pbrClearcoatIblNormalMapVertexSpirv;
  std::vector<std::uint32_t> pbrClearcoatIblNormalMapFragmentSpirv;

  // Phase 1 (CPU) outputs, published once by setUpPbrClearcoatDemoFixture()
  // and never mutated afterward.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;
  std::optional<atlantis::asset_system::EnvironmentAssetData> environmentData;

  // Phase 2 (GPU) outputs, grown incrementally by renderPbrClearcoatDemoFrame().
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
};

inline constexpr std::uint32_t kPbrClearcoatDemoExtentPixels = 512;
inline constexpr atlantis::rhi::Format kPbrClearcoatDemoColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class PbrClearcoatDemoSetupError {
  ShaderLoadFailed,
  DeviceCreationFailed,
  SceneLoadFailed,
  ResourceCreationFailed,
};

// config's sceneArtifactPath/sceneMetadataPath/sceneDependencyManifestPath
// must name pbr_clearcoat_demo_scene's (or pbr_clearcoat_normal_map_demo_
// scene's) own cooked artifacts; config must also configure a real
// environment (environmentArtifactPath/environmentMetadataPath) and every
// shader path field this fixture reads, including the two new
// pbrClearcoatIbl*/pbrClearcoatIblNormalMap* pairs -- mirrors
// setUpIblMaterialDemoFixture()'s own identical convention.
[[nodiscard]] atlantis::Result<PbrClearcoatDemoFixture, PbrClearcoatDemoSetupError> setUpPbrClearcoatDemoFixture(
    const atlantis::runtime::BootstrapConfig& config);

enum class PbrClearcoatDemoRenderError {
  AcquireFailed,
  NoActiveCamera,
  ExtractionFailed,
  LightExtractionFailed,
  CommandListCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
};

// One full acquire -> updateTransforms() -> Camera/Lighting/
// CameraWorldPosition extraction and publish -> realize (Phase 2) -> draw
// -> copy -> submit -> waitIdle cycle. May be called more than once
// against the same PbrClearcoatDemoFixture, reusing the same
// cameraBuffer/depthTexture/offscreenTarget/readbackBuffer/Mesh/Material/
// Pipeline/descriptor-set map across every cycle -- never recreated.
[[nodiscard]] atlantis::Result<PixelBuffer, PbrClearcoatDemoRenderError> renderPbrClearcoatDemoFrame(
    PbrClearcoatDemoFixture& fixture);

}  // namespace atlantis::image_regression
