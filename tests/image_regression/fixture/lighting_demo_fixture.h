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
// Unlike MaterialDemoFixture, this fixture's own lightingDataCaptured
// flag is load-bearing, not merely mirrored for shape-consistency: the
// D10 static-snapshot negative test explicitly calls
// renderLightingDemoFrame() TWICE against the same fixture object, and
// the second call must NOT recapture the frame lighting data even if
// World's own light state changed in between (Plan 0019 Section P9).
struct LightingDemoFixture {
  std::unique_ptr<atlantis::rhi::Device> device;
  atlantis::rhi::VertexInputLayout unlitTexturedVertexInputLayout;
  std::vector<std::uint32_t> unlitTexturedVertexSpirv;
  std::vector<std::uint32_t> unlitTexturedFragmentSpirv;
  atlantis::rhi::VertexInputLayout litTexturedVertexInputLayout;
  std::vector<std::uint32_t> litTexturedVertexSpirv;
  std::vector<std::uint32_t> litTexturedFragmentSpirv;

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

  // Plan 0019 Section P9: the fixture-local direct analog of
  // RuntimeApplication::lightingDataCaptured_ -- guards the one-time
  // frame lighting data capture inside renderLightingDemoFrame(), never
  // reset by anything other than a fresh setUpLightingDemoFixture() call.
  bool lightingDataCaptured = false;
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

// One full acquire -> realize (Phase 2) -> one-time-light-capture (Plan
// 0019 Section P9, guarded by fixture.lightingDataCaptured) -> draw
// (gated by checkConformalTransform() for every LitTextured-bound
// entity, Section P15) -> copy -> submit -> waitIdle cycle. May be
// called more than once against the same LightingDemoFixture -- a
// second call finds every material already realized and, critically,
// never recaptures the frame lighting data even if World's own light
// state changed in between (the D10 static-snapshot proof).
[[nodiscard]] atlantis::Result<PixelBuffer, LightingDemoRenderError> renderLightingDemoFrame(
    LightingDemoFixture& fixture);

}  // namespace atlantis::image_regression
