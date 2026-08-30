#pragma once

#include <atlantis/asset_system/asset_id.h>
#include <atlantis/asset_system/material_types.h>
#include <atlantis/asset_system/texture_types.h>
#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>
#include <atlantis/result.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/offscreen_target.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>
#include <atlantis/runtime/bootstrap_config.h>
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

  // Phase 1 (CPU) outputs, published once by setUpPbrMaterialDemoFixture()
  // and never mutated afterward.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;

  // Phase 2 (GPU) outputs, grown incrementally by renderPbrMaterialDemoFrame().
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>
      sampledTextureResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::Sampler>> samplerResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::renderer::Material>>
      materialResourceMap;

  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;

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
// setUpLightingDemoFixture()'s own identical convention.
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
