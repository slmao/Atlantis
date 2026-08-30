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

// Plan 0019 Section P10/Milestone 10 (Spec 0019 D10): the lighting_demo
// fixture. Mirrors MaterialDemoFixture's own exact shape (material_demo_fixture.h's
// own top-of-file comment) -- this fixture links Atlantis::RuntimeHost
// directly and calls its real, Runtime-private loadAndInstantiateScene()
// (Phase 1 CPU load), computePendingMaterialIds()/realizePendingMaterials()
// (Phase 2 GPU realization), extractCameraMatrices()/resolveMeshAsset()/
// resolveMaterialAsset() helpers, AND (new to this fixture)
// extractFrameLightingData()/checkConformalTransform() -- never a
// fixture-private reimplementation of any of them (Spec 0019 D10's own
// named requirement).
//
// Plan 0022 Section M2: this fixture is now the single, authoritative
// dynamic-Lighting fixture, mirroring RuntimeApplication's own corrected
// design exactly. renderLightingDemoFrame() re-extracts and republishes
// the complete FrameLightingData from World's live state on every call
// against this same fixture -- not a one-time capture. The former
// one-time-capture guard (a real, currently-shipped Spec-0019 behavior
// this Plan supersedes, per Spec 0022's own explicit authority to revise
// Spec 0019's own D1) is removed; there is no longer a
// lightingDataCaptured field here to load-bear anything.
struct LightingDemoFixture {
  std::unique_ptr<atlantis::rhi::Device> device;
  atlantis::rhi::VertexInputLayout unlitTexturedVertexInputLayout;
  std::vector<std::uint32_t> unlitTexturedVertexSpirv;
  std::vector<std::uint32_t> unlitTexturedFragmentSpirv;
  atlantis::rhi::VertexInputLayout litTexturedVertexInputLayout;
  std::vector<std::uint32_t> litTexturedVertexSpirv;
  std::vector<std::uint32_t> litTexturedFragmentSpirv;
  // Plan 0023 Milestone 5: realizePendingMaterials()'s own further-
  // widened signature requires a real pbrDirectLit* trio too, even
  // here, where this fixture never realizes a MaterialKind::PbrDirectLit
  // material -- loaded the same way litTextured* already is.
  atlantis::rhi::VertexInputLayout pbrDirectLitVertexInputLayout;
  std::vector<std::uint32_t> pbrDirectLitVertexSpirv;
  std::vector<std::uint32_t> pbrDirectLitFragmentSpirv;

  // Phase 1 (CPU) outputs, published once by setUpLightingDemoFixture()
  // and never mutated afterward.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;

  // Phase 2 (GPU) outputs, grown incrementally by renderLightingDemoFrame().
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

inline constexpr std::uint32_t kLightingDemoExtentPixels = 512;
inline constexpr atlantis::rhi::Format kLightingDemoColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class LightingDemoSetupError {
  ShaderLoadFailed,
  DeviceCreationFailed,
  SceneLoadFailed,
  ResourceCreationFailed,
};

// config's sceneArtifactPath/sceneMetadataPath/sceneDependencyManifestPath
// must name lighting_demo_scene's own cooked artifacts,
// unlitTexturedVertexShader{SpirvPath,ReflectionPath}/
// unlitTexturedFragmentShader{SpirvPath,ReflectionPath} must name
// textured_quad_shaders' own compiled outputs, and
// litTexturedVertexShader{SpirvPath,ReflectionPath}/
// litTexturedFragmentShader{SpirvPath,ReflectionPath} must name
// lit_textured_shaders' own compiled outputs -- reusing
// atlantis::runtime::BootstrapConfig directly, mirroring
// setUpMaterialDemoFixture()'s own identical convention.
[[nodiscard]] atlantis::Result<LightingDemoFixture, LightingDemoSetupError> setUpLightingDemoFixture(
    const atlantis::runtime::BootstrapConfig& config);

enum class LightingDemoRenderError {
  AcquireFailed,
  NoActiveCamera,
  ExtractionFailed,
  LightExtractionFailed,
  CommandListCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
};

// One full acquire -> updateTransforms() -> Lighting extraction and
// publish (Plan 0022 Section M2: every call re-extracts, unconditional)
// -> realize (Phase 2) -> draw (gated by checkConformalTransform() for
// every LitTextured-bound entity, Section P15) -> copy -> submit ->
// waitIdle cycle. May be called more than once against the same
// LightingDemoFixture, reusing the same cameraBuffer/depthTexture/
// offscreenTarget/readbackBuffer/Mesh/Material/Pipeline/descriptor-set
// map across every cycle -- never recreated. Each call's own
// world->updateTransforms()/Lighting-extraction/publish step is safe
// only because the *previous* call's own waitIdle() already confirmed
// that call's GPU work has finished before this call's own CPU writes
// begin -- see this fixture's own .cpp for the exact call order.
[[nodiscard]] atlantis::Result<PixelBuffer, LightingDemoRenderError> renderLightingDemoFrame(
    LightingDemoFixture& fixture);

}  // namespace atlantis::image_regression
