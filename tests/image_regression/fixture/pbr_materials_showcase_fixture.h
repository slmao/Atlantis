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

// Plan 0035 Milestone 5 (Spec 0035 Requirement 6): the
// pbr_materials_showcase fixture. Duplicated, not shared, from
// PbrAnisotropicDemoFixture (that file's own top-of-file comment
// establishes this repository's "duplicated, not shared" convention for
// from-scratch composition roots) -- this fixture links
// Atlantis::RuntimeHost directly and calls its real, Runtime-private
// loadAndInstantiateScene()/computePendingMaterialIds()/
// realizePendingMaterials()/extractCameraMatrices()/
// extractCameraWorldPosition()/extractFrameLightingData()/
// resolveMeshAsset()/resolveMaterialAsset()/checkConformalTransform()
// helpers -- never a fixture-private reimplementation of any of them.
//
// UNLIKE every per-BRDF sibling fixture (which each load exactly ONE
// new kind's own real shader trio and dead-fill the other two), this
// fixture's own scene references all THREE new kinds at once (Spec
// 0035 Requirement 6's own "one clearcoat, one sheen, one anisotropic"
// bar), so it loads all three of pbrClearcoatIbl*/pbrSheenIbl*/
// pbrAnisotropicIbl* as REAL, genuine trailing trio data -- only their
// own *NormalMap counterparts stay dead-path filler (this scene's own
// three special spheres are all non-normal-mapped, scalar-only
// showcase materials).
struct PbrMaterialsShowcaseFixture {
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
  // Plan 0035 Milestone 5: the three real shader pairs -- always
  // populated (this fixture's own scene always configures an
  // environment), passed as genuine trailing trio arguments to
  // realizePendingMaterials(), never dead-path filler. Mirrors
  // pbrClearcoatIblVertexInputLayout resolved via
  // pbrDirectLitVertexLayout() (3-attribute, no tangent) exactly like
  // PbrClearcoatDemoFixture/PbrSheenDemoFixture's own non-normal-map
  // trio; pbrAnisotropicIblVertexInputLayout resolved via
  // pbrNormalMapVertexLayout() (4-attribute, WITH tangent) exactly like
  // PbrAnisotropicDemoFixture's own non-normal-map trio -- Milestone 4's
  // own established exception (anisotropic tangent rotation needs the
  // tangent attribute even without a normal map texture).
  atlantis::rhi::VertexInputLayout pbrClearcoatIblVertexInputLayout;
  std::vector<std::uint32_t> pbrClearcoatIblVertexSpirv;
  std::vector<std::uint32_t> pbrClearcoatIblFragmentSpirv;
  atlantis::rhi::VertexInputLayout pbrSheenIblVertexInputLayout;
  std::vector<std::uint32_t> pbrSheenIblVertexSpirv;
  std::vector<std::uint32_t> pbrSheenIblFragmentSpirv;
  atlantis::rhi::VertexInputLayout pbrAnisotropicIblVertexInputLayout;
  std::vector<std::uint32_t> pbrAnisotropicIblVertexSpirv;
  std::vector<std::uint32_t> pbrAnisotropicIblFragmentSpirv;

  // Phase 1 (CPU) outputs, published once by
  // setUpPbrMaterialsShowcaseFixture() and never mutated afterward.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;
  std::optional<atlantis::asset_system::EnvironmentAssetData> environmentData;

  // Phase 2 (GPU) outputs, grown incrementally by
  // renderPbrMaterialsShowcaseFrame().
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

inline constexpr std::uint32_t kPbrMaterialsShowcaseExtentPixels = 768;
inline constexpr atlantis::rhi::Format kPbrMaterialsShowcaseColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class PbrMaterialsShowcaseSetupError {
  ShaderLoadFailed,
  DeviceCreationFailed,
  SceneLoadFailed,
  ResourceCreationFailed,
};

// config's sceneArtifactPath/sceneMetadataPath/sceneDependencyManifestPath
// must name pbr_materials_showcase_scene's own cooked artifacts; config
// must also configure a real environment (environmentArtifactPath/
// environmentMetadataPath -- the warehouse_interior HDRI, Spec 0035
// Requirement 6) and every shader path field this fixture reads,
// including the three new pbrClearcoatIbl*/pbrSheenIbl*/
// pbrAnisotropicIbl* pairs -- mirrors setUpPbrAnisotropicDemoFixture()'s
// own identical convention.
[[nodiscard]] atlantis::Result<PbrMaterialsShowcaseFixture, PbrMaterialsShowcaseSetupError>
setUpPbrMaterialsShowcaseFixture(const atlantis::runtime::BootstrapConfig& config);

enum class PbrMaterialsShowcaseRenderError {
  AcquireFailed,
  NoActiveCamera,
  ExtractionFailed,
  LightExtractionFailed,
  CommandListCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
};

// One full acquire -> updateTransforms() -> Camera/Lighting/
// CameraWorldPosition extraction and publish -> realize (Phase 2) ->
// draw -> copy -> submit -> waitIdle cycle. May be called more than
// once against the same PbrMaterialsShowcaseFixture, reusing the same
// cameraBuffer/depthTexture/offscreenTarget/readbackBuffer/Mesh/
// Material/Pipeline/descriptor-set map across every cycle -- never
// recreated.
[[nodiscard]] atlantis::Result<PixelBuffer, PbrMaterialsShowcaseRenderError> renderPbrMaterialsShowcaseFrame(
    PbrMaterialsShowcaseFixture& fixture);

}  // namespace atlantis::image_regression
