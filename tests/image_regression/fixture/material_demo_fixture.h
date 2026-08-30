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

// Plan 0018 Milestone 16 (Spec 0018 D12): the material_demo_scene fixture.
// Unlike every other fixture in this directory (which duplicate Runtime's
// own scene-loading/extraction logic independently, per
// world_scene_loaded_fixture.h's own top-of-file comment), this fixture
// links Atlantis::RuntimeHost directly and calls its real, Runtime-private
// loadAndInstantiateScene() (Phase 1 CPU load) and
// computePendingMaterialIds()/realizePendingMaterials() (Phase 2 GPU
// realization) functions, plus its extractCameraMatrices()/
// resolveMeshAsset()/resolveMaterialAsset() helpers -- a deliberate,
// disclosed departure required by Spec 0018 D12's explicit "the new
// fixture must call the same CPU resolve/load and GPU realization helper
// Runtime uses, not a duplicate" contract. Only the ordinary
// device/camera/depth/offscreen-target/readback plumbing and the local
// Vertex/vertex-layout schema still follow this directory's own established
// per-fixture shape.
//
// There is no format-rebuild concept here (unlike RuntimeApplication's own
// runFrame()): this fixture's OffscreenTarget's format never changes across
// repeated renderMaterialDemoFrame() calls, so materialResourceMap only
// ever grows, never gets rebuilt.
struct MaterialDemoFixture {
  std::unique_ptr<atlantis::rhi::Device> device;
  atlantis::rhi::VertexInputLayout unlitTexturedVertexInputLayout;
  std::vector<std::uint32_t> unlitTexturedVertexSpirv;
  std::vector<std::uint32_t> unlitTexturedFragmentSpirv;
  // Plan 0019 Section P6: realizePendingMaterials()'s own widened
  // signature requires a real litTextured* trio at every call site, even
  // here, where material_demo_scene never actually realizes a
  // MaterialKind::LitTextured material (selectShaderPair() never
  // dispatches into this branch for this fixture) -- loaded the same way
  // unlitTextured* already is.
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

  // Phase 1 (CPU) outputs, published once by setUpMaterialDemoFixture() and
  // never mutated afterward.
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::renderer::Mesh> meshResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::MaterialAssetData> materialDataMap;
  std::unordered_map<atlantis::asset_system::AssetId, atlantis::asset_system::TextureAssetData> textureDataMap;

  // Phase 2 (GPU) outputs, grown incrementally by renderMaterialDemoFrame()
  // -- keyed exactly as RuntimeApplication's own identically-named members
  // (runtime_application.h): sampledTextureResourceMap by TEXTURE AssetId,
  // samplerResourceMap/materialResourceMap by MATERIAL AssetId.
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::SampledTexture>>
      sampledTextureResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::rhi::Sampler>> samplerResourceMap;
  std::unordered_map<atlantis::asset_system::AssetId, std::unique_ptr<atlantis::renderer::Material>>
      materialResourceMap;

  std::unique_ptr<atlantis::rhi::Buffer> cameraBuffer;
  std::unique_ptr<atlantis::rhi::Texture> depthTexture;
  std::unique_ptr<atlantis::rhi::OffscreenTarget> offscreenTarget;
  std::unique_ptr<atlantis::rhi::Buffer> readbackBuffer;

  // std::optional, not a bare World: World is move-constructible but not
  // move-assignable (ADR-0049/Spec 0014) -- matches
  // WorldSceneLoadedFixture's own identical field exactly.
  std::optional<atlantis::world::World> world;
};

inline constexpr std::uint32_t kMaterialDemoExtentPixels = 512;
inline constexpr atlantis::rhi::Format kMaterialDemoColorFormat = atlantis::rhi::Format::Rgba8Unorm;

enum class MaterialDemoSetupError {
  ShaderLoadFailed,
  DeviceCreationFailed,
  SceneLoadFailed,
  ResourceCreationFailed,
};

// config's sceneArtifactPath/sceneMetadataPath/sceneDependencyManifestPath
// must name material_demo_scene's own cooked artifacts,
// unlitTexturedVertexShader{SpirvPath,ReflectionPath}/
// unlitTexturedFragmentShader{SpirvPath,ReflectionPath} must name
// textured_quad_shaders' own compiled outputs, and (Plan 0019 Section
// P6) litTexturedVertexShader{SpirvPath,ReflectionPath}/
// litTexturedFragmentShader{SpirvPath,ReflectionPath} must name
// lit_textured_shaders' own compiled outputs -- reusing
// atlantis::runtime::BootstrapConfig directly (rather than inventing a
// second, fixture-only parameter list) since loadAndInstantiateScene()
// itself already takes one. Must be called with the process's current
// working directory set to a location where "shaders/textured_quad.{vert,
// frag}.spv" resolves, matching every other fixture's own established
// convention (loadAndInstantiateScene() itself never reads the shader
// path fields -- only this function does, to build the
// unlitTexturedVertexInputLayout it then also hands to
// loadAndInstantiateScene() for mesh creation).
[[nodiscard]] atlantis::Result<MaterialDemoFixture, MaterialDemoSetupError> setUpMaterialDemoFixture(
    const atlantis::runtime::BootstrapConfig& config);

enum class MaterialDemoRenderError {
  AcquireFailed,
  NoActiveCamera,
  ExtractionFailed,
  CommandListCreationFailed,
  SubmitFailed,
  WaitIdleFailed,
};

// One full acquire -> realize (Phase 2, via the real
// realizePendingMaterials()) -> draw -> copy -> submit -> waitIdle cycle.
// Unlike RuntimeApplication::runFrame(), waitIdle() runs unconditionally
// (this fixture always needs the readback Buffer's contents CPU-visible
// immediately, matching every other fixture in this directory) -- so the
// newly-realized candidates are always safe to publish into
// sampledTextureResourceMap/samplerResourceMap/materialResourceMap right
// after, unconditionally too. May be called more than once against the
// same MaterialDemoFixture (OffscreenTarget's own repeated-cycle contract,
// ADR-0038): a second call finds every material already realized
// (computePendingMaterialIds() returns empty) and performs no new upload.
[[nodiscard]] atlantis::Result<PixelBuffer, MaterialDemoRenderError> renderMaterialDemoFrame(
    MaterialDemoFixture& fixture);

}  // namespace atlantis::image_regression
