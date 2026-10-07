#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/cli/command.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/runtime/runtime_control_host.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/schema.h>
#include <atlantis/world/access/runtime_world_access.h>
#include <atlantis/world/light.h>
#include <atlantis/world/ecs/world_components.h>
#include <atlantis/world/scene_instantiation.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world_matrix.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>

#include "png_codec.h"

// Plan 0013 Section D10: links Atlantis::RuntimeHost directly (never
// atlantis_runtime, which this test does not invoke as a subprocess) and
// reuses RuntimeApplication's own public API exactly as main.cpp does,
// with a bounded loop instead of an unbounded one. No CLI flag, no
// environment variable, no test-only constructor parameter is added to
// RuntimeApplication, BootstrapConfig, or atlantis_runtime's own
// main.cpp -- the bounded, deterministic exit comes purely from calling
// the already-public shutdown() method after a fixed number of
// runFrame() calls.
//
// This is the first gpu-labeled test in this repository that creates a
// real, visible OS window during an automated ctest run -- every prior
// GPU-required test is offscreen-only. Disclosed, deliberate consequence
// of Spec 0013's own approved design.

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::createRuntimeApplication;
using atlantis::runtime::FrameLightingData;
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;
using atlantis::world::BakedScene;
using atlantis::world::Light;
using atlantis::world::LightKind;
using atlantis::world::Transform;
using atlantis::world::WorldMatrix;
namespace ecs = atlantis::world::ecs;

// Plan 0014 Section D-Step 6: the one narrowly-scoped friend
// RuntimeApplication declares for this test only (see
// runtime_application.h's own comment) -- reads world_'s own
// renderableEntities() count, no new public API. Plan 0015 Section
// D2/D10: world_ is std::optional<World>; runFrame() has already run
// by the time this is called (below), so it is guaranteed populated.
//
// Plan 0022 Section M3: extends this same, already-existing,
// already-approved friend struct with two more narrow accessors --
// never a new friend declaration, never a new public API on
// RuntimeApplication itself (runtime_application.h's own `friend struct
// RuntimeSmokeTestAccess;` is already generic; granting this struct one
// more static method needs no header change at all). `worldAccess()`
// exposes the running app's Runtime World operation boundary (Spec 0052,
// Plan 0052 P9/P10), so the test edits the live Runtime World exactly as a
// client does -- commands submitted between frames, applied by runFrame(),
// observed as events -- never through the scene's ECS directly, never a
// second, test-private instance, and never a duplicated scene-load or
// frame-loop path. `lightingPayloadBytes()` reads the real
// `cameraBuffer_`'s own mapped bytes directly -- a host-visible,
// host-coherent read the app's own real frame writes into every frame
// (Spec 0022's own confirmed HOST_COHERENT contract) -- so this is a
// safe, ordinary CPU read of memory the app already owns and keeps
// current, not a new synchronization primitive.
namespace atlantis::runtime {

// Independently pins the real byte offset runtime_application.cpp's own
// construction establishes (createBuffer()'s own sizeof(float) * 32 +
// sizeof(FrameLightingData) size, and the cameraData + 32 write) --
// derived here from first principles (two 4x4 float matrices: view,
// then projection), not copied from that file's own expression, so a
// real drift between the two fails to *compile* here, not silently
// reads the wrong bytes.
constexpr std::size_t kLightingByteOffset = 2 * 16 * sizeof(float);  // 2 matrices, 16 floats each
static_assert(kLightingByteOffset == 128);
static_assert(kLightingByteOffset == sizeof(float) * 32, "must match runtime_application.cpp's own real offset");
// Plan 0027 Milestone 9 fix: 304 is FrameLightingData's own end offset,
// not cameraBuffer_'s own total size any more -- the buffer grew to 592
// bytes (the light-space tail, ADR-0072 D-1/P5), unaffected here since
// CameraMatrices/FrameLightingData/CameraWorldPositionData themselves
// stay byte-for-byte unmodified.
// Plan 0040 M1 window: 304 -> 2224 (4 -> 64 point lights); the shader
// side catches up in Milestone 2.
static_assert(kLightingByteOffset + sizeof(FrameLightingData) == 2224);

// Plan 0023 Milestone 2 (ADR-0062's own Accepted Amendment): independently
// pins the new tail region's own real byte offset -- derived here from
// first principles (the same 128-byte camera region plus
// FrameLightingData's own real sizeof), not copied from
// runtime_application.cpp's own `cameraData + 32 + 44` expression, so a
// real drift between the two fails to *compile* here.
constexpr std::size_t kCameraWorldPositionByteOffset = kLightingByteOffset + sizeof(FrameLightingData);
static_assert(kCameraWorldPositionByteOffset == 2224);
// Plan 0027 Milestone 9 fix: 320 is CameraWorldPositionData's own end
// offset, not cameraBuffer_'s own total size any more -- see the
// identical note on kLightingByteOffset's own static_assert above.
// Plan 0040 M1: 320 -> 2240.
static_assert(kCameraWorldPositionByteOffset + sizeof(CameraWorldPositionData) == 2240);

struct RuntimeSmokeTestAccess {
  static std::size_t renderableEntityCount(RuntimeApplication& app) {
    return collectRenderables(*app.scene_).size();
  }

  // Plan 0028 Milestone 2: one GPU Mesh/Material resource per distinct
  // AssetId, regardless of how many entities reference it -- reads the
  // same private maps runFrame()'s own DrawItem loop already uses.
  static std::size_t meshResourceMapSize(const RuntimeApplication& app) { return app.meshResourceMap_.size(); }
  static std::size_t materialResourceMapSize(const RuntimeApplication& app) { return app.materialResourceMap_.size(); }

  [[nodiscard]] static atlantis::world::access::RuntimeWorldAccess& worldAccess(RuntimeApplication& app) {
    return *app.worldAccess_;
  }

  // Plan 0044 Milestone 1: the three bloom Pipelines, built at startup when
  // the bloom shader paths are set; sceneWantsBloom_ from the active camera.
  [[nodiscard]] static bool hasBloomPipelines(const RuntimeApplication& app) {
    for (const auto& pipeline : app.bloomPipelines_) {
      if (!pipeline) return false;
    }
    return true;
  }
  [[nodiscard]] static bool sceneWantsBloom(const RuntimeApplication& app) { return app.sceneWantsBloom_; }
  // Plan 0044 M2: the twelve bloom targets, built in the resize branch.
  [[nodiscard]] static bool hasBloomTargets(const RuntimeApplication& app) { return app.bloomTargets_.has_value(); }

  [[nodiscard]] static FrameLightingData lightingPayloadBytes(const RuntimeApplication& app) {
    const auto* cameraBytes = static_cast<const std::byte*>(app.cameraBuffer_->mappedData());
    FrameLightingData lighting{};
    std::memcpy(&lighting, cameraBytes + kLightingByteOffset, sizeof(FrameLightingData));
    return lighting;
  }

  // Plan 0055 M3: the camera view and projection the frame wrote, the
  // uniform's first 32 floats.
  [[nodiscard]] static std::array<float, 32> cameraMatrixFloats(const RuntimeApplication& app) {
    std::array<float, 32> floats{};
    std::memcpy(floats.data(), app.cameraBuffer_->mappedData(), sizeof(floats));
    return floats;
  }
};
}  // namespace atlantis::runtime

namespace {

// The ATLANTIS_RUNTIME_*_GUID definitions are generated from the committed
// catalog source at configure time (Plan 0047 P15).
[[nodiscard]] atlantis::asset_system::AssetGuid sceneGuidFromDefinition(const char* text) {
  auto parsed = atlantis::asset_system::parseAssetGuid(text);
  REQUIRE(parsed.isOk());
  return parsed.value();
}

// The windowed smoke config, as main.cpp populates it. Plan 0044 M2
// follow-up: shared by the bloom smoke TEST_CASE below, which swaps only
// the scene paths.
[[nodiscard]] BootstrapConfig buildSmokeConfig() {
  BootstrapConfig config;
  config.applicationName = "Atlantis Runtime GPU Smoke Test";
  config.vertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.spv";
  config.vertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.refl.json";
  config.fragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.spv";
  config.fragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.refl.json";
  config.assetArtifactPath = ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH;
  config.assetMetadataPath = ATLANTIS_RUNTIME_ASSET_METADATA_PATH;
  // Plan 0015 Section D11: the real, loaded scene path -- replaces the
  // former hardcoded six-entity validation scene.
  config.assetCatalogPath = ATLANTIS_ASSET_CATALOG_PATH;
  config.sceneAsset = sceneGuidFromDefinition(ATLANTIS_RUNTIME_SCENE_GUID);
  // Plan 0018 Section P10: mirrors main.cpp's own identical population
  // of the second, MaterialKind::UnlitTextured built-in shader pair.
  config.unlitTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  // Plan 0019 Section P6/P11: mirrors main.cpp's own identical
  // population of the third, MaterialKind::LitTextured built-in shader
  // pair.
  config.litTexturedVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  // Plan 0023 Milestone 5: mirrors main.cpp's own identical population
  // of the fourth, MaterialKind::PbrDirectLit built-in shader pair.
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
  // Plan 0029 Section P15: mirrors main.cpp's own identical population
  // of the fifth, normal-map MaterialKind::PbrDirectLit built-in shader
  // pair -- unconditionally required.
  config.pbrDirectLitNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) + "/pbr_direct_lit_normal_map.vert.spv";
  config.pbrDirectLitNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.vert.refl.json";
  config.pbrDirectLitNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) + "/pbr_direct_lit_normal_map.frag.spv";
  config.pbrDirectLitNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_NORMAL_MAP_SHADER_DIR) +
      "/pbr_direct_lit_normal_map.frag.refl.json";
  // Plan 0028 Milestone 2: mirrors main.cpp's own identical population --
  // the default scene now configures an environment (sky + IBL), which
  // this TEST_CASE's own config previously left unset.
  config.environmentArtifactPath = ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_RUNTIME_ENVIRONMENT_METADATA_PATH;
  config.pbrIblVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.refl.json";
  // Plan 0029 Section P15: mirrors main.cpp's own identical population
  // of the normal-map IBL PBR pair -- required in exactly the same
  // environment-configured case as pbrIbl above.
  config.pbrIblNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.vert.refl.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_ibl_normal_map.frag.refl.json";
  config.skyVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.vert.spv";
  config.skyVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.vert.refl.json";
  config.skyFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.frag.spv";
  config.skyFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SKY_SHADER_DIR) + "/sky.frag.refl.json";
  // Plan 0024 Milestone 6/7: mirrors main.cpp's own identical
  // population of the two output-transform shader pairs.
  config.outputTransformUnormVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.spv";
  config.outputTransformUnormVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.vert.refl.json";
  config.outputTransformUnormFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.frag.spv";
  config.outputTransformUnormFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_UNORM_SHADER_DIR) + "/output_transform_unorm.frag.refl.json";
  config.outputTransformSrgbVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.vert.spv";
  config.outputTransformSrgbVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.vert.refl.json";
  config.outputTransformSrgbFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.frag.spv";
  config.outputTransformSrgbFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_OUTPUT_TRANSFORM_SRGB_SHADER_DIR) + "/output_transform_srgb.frag.refl.json";
  // Plan 0027 Milestone 8 (ADR-0072 D-1): the shadow-casting shader pair
  // -- unconditionally required (mirrors main.cpp's own identical
  // population).
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  // Plan 0044 Milestone 1 (P9): the three bloom shader pairs, as main.cpp
  // sets them -- so the Runtime's bloom Pipelines are created here, under
  // Validation Layers, even when the scene does not turn bloom on.
  config.bloomDownsampleVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.vert.spv";
  config.bloomDownsampleVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.vert.refl.json";
  config.bloomDownsampleFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.frag.spv";
  config.bloomDownsampleFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_DOWNSAMPLE_SHADER_DIR) + "/bloom_downsample.frag.refl.json";
  config.bloomUpsampleVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.vert.spv";
  config.bloomUpsampleVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.vert.refl.json";
  config.bloomUpsampleFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.frag.spv";
  config.bloomUpsampleFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_UPSAMPLE_SHADER_DIR) + "/bloom_upsample.frag.refl.json";
  config.bloomCompositeVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.vert.spv";
  config.bloomCompositeVertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.vert.refl.json";
  config.bloomCompositeFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.frag.spv";
  config.bloomCompositeFragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_BLOOM_COMPOSITE_SHADER_DIR) + "/bloom_composite.frag.refl.json";
  config.enableValidationLayers = true;
  return config;
}

}  // namespace

TEST_CASE("Runtime constructs a window and completes real windowed acquire/draw/submit/present frames",
          "[runtime][gpu]") {
  BootstrapConfig config = buildSmokeConfig();

  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  // Plan 0044 Milestone 1: the bloom Pipelines exist; the scene does not
  // turn bloom on, so no bloom targets are built.
  CHECK(atlantis::runtime::RuntimeSmokeTestAccess::hasBloomPipelines(app));
  CHECK_FALSE(atlantis::runtime::RuntimeSmokeTestAccess::sceneWantsBloom(app));

  // Matches this repository's own existing kCycleCount precedent
  // (frame_execution_demo, headless_rendering_demo,
  // image_regression_gpu_tests).
  constexpr int kSmokeTestFrameCount = 3;
  for (int i = 0; i < kSmokeTestFrameCount && app.shouldContinue(); ++i) {
    app.runFrame();
  }
  REQUIRE(app.shouldContinue());  // did not fail during those frames

  // Plan 0028 Milestone 2: exactly 6 DrawItems reach Renderer::drawFrame()
  // on a successful frame -- integrated_showcase_demo_scene.scene.txt
  // declares 6 Renderable nodes (one ground plus five spheres). Vulkan
  // Validation Layers reporting zero warnings/errors for the full
  // multi-item span is this test's own existing crash-on-validation-hit
  // mechanism (enableValidationLayers = true above), unchanged: reaching
  // this REQUIRE at all already proves no validation hit aborted the
  // process.
  REQUIRE(atlantis::runtime::RuntimeSmokeTestAccess::renderableEntityCount(app) == 6);

  // One GPU Mesh resource per distinct meshAsset (pbr_sphere,
  // ground_plane) and one GPU Material resource per distinct
  // materialAsset (the four PBR materials), regardless of the 6 entities
  // referencing them.
  REQUIRE(atlantis::runtime::RuntimeSmokeTestAccess::meshResourceMapSize(app) == 2);
  REQUIRE(atlantis::runtime::RuntimeSmokeTestAccess::materialResourceMapSize(app) == 4);

  // Plan 0022 Section M3: real, direct, byte-level proof that
  // RuntimeApplication::runFrame() itself -- not only the structurally
  // identical LightingDemoFixture path -- executes the Lighting write on
  // a genuine, later windowed frame, through the real windowed acquire/
  // Presentation Step 0/submit/present path, not a fabricated or
  // separately-orchestrated one. Deliberately extends this same
  // TEST_CASE's own already-running app/window, reusing the exact same
  // Platform session and RuntimeApplication lifecycle the 3-frame loop
  // above already established, rather than constructing a second,
  // separate windowed RuntimeApplication in a sibling TEST_CASE: this
  // executable's own pre-existing, out-of-scope Platform limitation
  // (same-process multiple windowed initialize()/shutdown() cycles,
  // found and explicitly disclosed as unrelated during an earlier,
  // separate Spec's own final review) makes two independent windowed
  // lifecycles inside one process unreliable -- confirmed directly: an
  // earlier draft of this test as its own separate TEST_CASE passed in
  // isolation but crashed the process when run together with the
  // TEST_CASE above. Extending the same lifecycle avoids that
  // pre-existing issue entirely, without attempting to fix it (out of
  // this Plan's own approved scope).
  //
  // Test-safety note (Human Review's own explicit requirement): this
  // extension never writes into cameraBuffer_'s own mapped bytes
  // directly, and never calls any new or unapproved synchronization API.
  // It only edits the real, running app's own baked Runtime World through
  // the ECS's public createEntity()/add()/set()/destroyEntity() between
  // ordinary app.runFrame() calls (Plan 0051 P9: Spec 0022's surviving
  // contract, its Correction 2026-10-06) -- and only ever
  // *reads* cameraBuffer_'s own bytes (via lightingPayloadBytes()), never
  // writes them.
  //
  // The precise host/device concurrency argument (HOST_COHERENT alone
  // does not settle this -- it only makes a write visible without an
  // explicit flush, it says nothing about read/read concurrency, which
  // needs its own argument): lightingPayloadBytes() is called only after
  // a runFrame() call has already returned. At that point, that same
  // frame's own real write into cameraBuffer_ (inside runFrame() itself)
  // has already completed on the CPU side -- ordinary sequential
  // execution, not a synchronization claim. Whether that frame's own
  // *GPU* work (which reads cameraBuffer_ via the shader's uniform
  // binding) has *also* finished executing by then is a separate
  // question this test does not need to answer, because it does not
  // matter: this test only ever *reads* cameraBuffer_'s bytes, never
  // writes them, so at worst this is a host read concurrent with a GPU
  // read of the identical bytes -- a read/read pair, which is never a
  // data race in any memory model, coherent or not (only a read/write or
  // write/write pair is). No new wait, no new synchronization primitive,
  // is needed for this test to be safe. Every further frame below goes
  // through the exact same acquireNextTarget()/Step 0/updateTransforms()/submit()/
  // present() sequence the 3 frames above already did -- nothing here
  // bypasses or reorders it.
  using atlantis::runtime::RuntimeSmokeTestAccess;

  // The real, default integrated_showcase_demo_scene.scene.txt declares
  // one Directional light node and zero Point lights -- this test adds
  // its own Point light below and checks it coexists with the scene's
  // own Directional one, not that no light is present yet.
  const FrameLightingData beforeAnyLight = RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  REQUIRE(beforeAnyLight.directionalLightCount == 1);
  REQUIRE(beforeAnyLight.pointLightCount == 0);

  // Plan 0052 P10 (Spec 0052 ruling Q5 H-a; Spec 0022's surviving contract,
  // Correction 2026-10-06): the live edits go through the Runtime World's
  // operation boundary, as any client's would -- submitted between frames,
  // applied by the next runFrame()'s first statement, observed as events.
  // The new light gets its Light before its WorldMatrix (Correction J1:
  // Light{} is Directional, and only a Light + WorldMatrix holder counts
  // toward the light limits).
  namespace access = atlantis::world::access;
  access::RuntimeWorldAccess& boundary = RuntimeSmokeTestAccess::worldAccess(app);
  const auto lightType = ecs::componentTypeId<Light>();
  const auto matrixType = ecs::componentTypeId<WorldMatrix>();
  const auto lightField = [](std::string_view name) { return atlantis::schema::fieldId("world::Light", name); };
  const auto column3 = atlantis::schema::fieldId("world::WorldMatrix", "column3");
  const auto newLight = atlantis::asset_system::parseEntityGuid("52005200-0000-4000-8000-000000000001").value();
  // Plan 0053 M5 (Spec 0053 ruling Q6, C2): the nine commands that build the
  // light are one transaction, applied all or nothing by the same first
  // statement of runFrame(); committed, they emit the same nine events.
  const access::TransactionTicket creation = boundary.submitTransaction({
      access::CreateEntity{newLight},
      access::AddComponent{newLight, lightType},
      access::SetProperty{{newLight, lightType, lightField("kind")}, access::EnumValue{1}},  // Point
      access::SetProperty{{newLight, lightType, lightField("color")}, std::array<float, 3>{0.2f, 0.4f, 0.9f}},
      access::SetProperty{{newLight, lightType, lightField("intensity")}, 2.5f},
      access::SetProperty{{newLight, lightType, lightField("range")}, 5.0f},
      access::AddComponent{newLight, ecs::componentTypeId<Transform>()},
      access::AddComponent{newLight, matrixType},
      access::SetProperty{{newLight, matrixType, column3}, std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}},
  });
  REQUIRE(creation.count == 9);

  // The next real windowed frame -- a real acquire/Step 0/submit/present
  // cycle, identical in shape to the 3 frames above -- applies and
  // publishes them.
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK(boundary.drainFailures().empty());
  const std::vector<access::Event> addedEvents = boundary.drainEvents();
  REQUIRE(addedEvents.size() == 9);  // one per command, in order
  CHECK(addedEvents.front() == access::Event{access::EntityCreated{newLight}});
  CHECK(addedEvents.back() ==
        access::Event{access::PropertyChanged{{newLight, matrixType, column3},
                                              std::array<float, 4>{1.0f, 1.0f, 1.0f, 1.0f}}});
  const FrameLightingData afterLightAdded = RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  CHECK(afterLightAdded.directionalLightCount == 1);
  CHECK(afterLightAdded.pointLightCount == 1);
  CHECK(afterLightAdded.pointLights[0].position[0] == 1.0f);
  CHECK(afterLightAdded.pointLights[0].position[1] == 1.0f);
  CHECK(afterLightAdded.pointLights[0].position[2] == 1.0f);
  CHECK(afterLightAdded.pointLights[0].color[0] == 0.2f);
  CHECK(afterLightAdded.pointLights[0].color[1] == 0.4f);
  CHECK(afterLightAdded.pointLights[0].color[2] == 0.9f);
  CHECK(afterLightAdded.pointLights[0].intensity == 2.5f);

  // A world-matrix edit alone is seen by the next frame's collection.
  boundary.submit(
      access::SetProperty{{newLight, matrixType, column3}, std::array<float, 4>{-2.0f, 3.0f, 0.5f, 1.0f}});
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK(boundary.drainEvents().size() == 1);
  const FrameLightingData afterTransformMoved = RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  CHECK(afterTransformMoved.pointLightCount == 1);
  CHECK(afterTransformMoved.pointLights[0].position[0] == -2.0f);
  CHECK(afterTransformMoved.pointLights[0].position[1] == 3.0f);
  CHECK(afterTransformMoved.pointLights[0].position[2] == 0.5f);

  // Light entity removal is seen by the next frame, and the freed slot is
  // zeroed.
  boundary.submit(access::DestroyEntity{newLight});
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK(boundary.drainEvents() == std::vector<access::Event>{access::EntityDestroyed{newLight}});
  CHECK(boundary.drainFailures().empty());
  const FrameLightingData afterLightRemoved = RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  CHECK(afterLightRemoved.directionalLightCount == 1);
  CHECK(afterLightRemoved.pointLightCount == 0);
  CHECK(afterLightRemoved.pointLights[0].intensity == 0.0f);

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
}

// Plan 0044 Milestone 2 item 3: the full Runtime bloom path -- a scene
// whose active camera turns bloom on (bloom_demo, bloom=0.8 1.0), so the
// Runtime builds the twelve bloom targets in its resize branch and hands
// drawFrame() a BloomInput from the camera every frame, through the real
// windowed acquire/submit/present path, under fatal Validation Layers. A
// separate TEST_CASE: catch_discover_tests runs each in its own process,
// so the same-process multiple-windowed-lifecycle limitation noted above
// does not apply under ctest.
TEST_CASE("Runtime renders real windowed frames with bloom on when the scene's camera turns it on",
          "[runtime][gpu][bloom]") {
  BootstrapConfig config = buildSmokeConfig();
  config.assetCatalogPath = ATLANTIS_ASSET_CATALOG_PATH;
  config.sceneAsset = sceneGuidFromDefinition(ATLANTIS_RUNTIME_BLOOM_SCENE_GUID);

  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  CHECK(atlantis::runtime::RuntimeSmokeTestAccess::hasBloomPipelines(app));
  CHECK(atlantis::runtime::RuntimeSmokeTestAccess::sceneWantsBloom(app));

  constexpr int kSmokeTestFrameCount = 3;
  for (int i = 0; i < kSmokeTestFrameCount && app.shouldContinue(); ++i) {
    app.runFrame();
  }
  REQUIRE(app.shouldContinue());  // no frame failed; no validation hit aborted
  CHECK(atlantis::runtime::RuntimeSmokeTestAccess::hasBloomTargets(app));

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
}

// Plan 0054 M5 (Spec 0054 R11, the north star): CLI -> Connection -> World API
// -> ECS -> render extraction -> Renderer, on the running default scene, under
// fatal Validation Layers. The CLI holds only its connection (opened through
// RuntimeApplication::openConnection()); the test reads the frame's lighting
// bytes through the existing smoke friend. The default scene's one light is
// Directional, 0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea, intensity 3, and
// extraction copies Light::intensity verbatim (scene_extraction.cpp).
TEST_CASE("Runtime: a CLI `entity set` on the default scene's light reaches the next frame's lighting",
          "[runtime][gpu][connection]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  app.runFrame();
  REQUIRE(app.shouldContinue());
  const FrameLightingData before = atlantis::runtime::RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  REQUIRE(before.directionalLightCount == 1);
  CHECK(before.directionalLights[0].intensity == 3.0f);

  // Declared after `app`, so destroyed before it (the endpoint CHECKs this).
  const auto connection = app.openConnection();
  std::ostringstream out;
  atlantis::cli::Commands commands(*connection, out);
  const std::string light = "0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea";
  REQUIRE(commands.run("entity set " + light + " Light.intensity 6") == atlantis::cli::Outcome::Submitted);

  // The next real windowed frame applies the command first, then renders it.
  app.runFrame();
  REQUIRE(app.shouldContinue());
  const FrameLightingData after = atlantis::runtime::RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  REQUIRE(after.directionalLightCount == 1);
  CHECK(after.directionalLights[0].intensity == 6.0f);
  // Nothing else about the light moved.
  CHECK(std::memcmp(before.directionalLights[0].direction, after.directionalLights[0].direction,
                    sizeof(before.directionalLights[0].direction)) == 0);
  CHECK(std::memcmp(before.directionalLights[0].color, after.directionalLights[0].color,
                    sizeof(before.directionalLights[0].color)) == 0);
  CHECK(commands.reportPending() == atlantis::cli::Outcome::Done);
  CHECK(out.str() == "ok " + light + " Light.intensity = 6\n");

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
}

// Plan 0055 M3 (Spec 0055 R3; P8, ruling Q2 C1): the frame data a step
// reports is the frame just drawn -- its lighting block and camera matrices
// as the frame wrote them to the camera uniform (read through the smoke
// friend), and its draw-item count -- before and after an edit applies.
TEST_CASE("Runtime: captureFrameData() equals the frame just drawn", "[runtime][gpu][control]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  CHECK(app.captureFrameData().error() == atlantis::connection::ControlError::NotRendering);  // nothing drawn yet

  const auto equalsFrame = [&](const atlantis::connection::FrameData& data) {
    const FrameLightingData lighting = atlantis::runtime::RuntimeSmokeTestAccess::lightingPayloadBytes(app);
    REQUIRE(data.directionalLights.size() == lighting.directionalLightCount);
    REQUIRE(data.pointLights.size() == lighting.pointLightCount);
    for (std::size_t i = 0; i < data.directionalLights.size(); ++i) {
      const auto& gpu = lighting.directionalLights[i];
      CHECK(std::memcmp(data.directionalLights[i].direction.data(), gpu.direction, sizeof(gpu.direction)) == 0);
      CHECK(std::memcmp(data.directionalLights[i].color.data(), gpu.color, sizeof(gpu.color)) == 0);
      CHECK(std::memcmp(&data.directionalLights[i].intensity, &gpu.intensity, sizeof(float)) == 0);
    }
    for (std::size_t i = 0; i < data.pointLights.size(); ++i) {
      const auto& gpu = lighting.pointLights[i];
      CHECK(std::memcmp(data.pointLights[i].position.data(), gpu.position, sizeof(gpu.position)) == 0);
      CHECK(std::memcmp(data.pointLights[i].color.data(), gpu.color, sizeof(gpu.color)) == 0);
      CHECK(std::memcmp(&data.pointLights[i].intensity, &gpu.intensity, sizeof(float)) == 0);
      CHECK(std::memcmp(&data.pointLights[i].range, &gpu.range, sizeof(float)) == 0);
    }
    const std::array<float, 32> camera = atlantis::runtime::RuntimeSmokeTestAccess::cameraMatrixFloats(app);
    CHECK(std::memcmp(data.view.data(), camera.data(), sizeof(float) * 16) == 0);
    CHECK(std::memcmp(data.projection.data(), camera.data() + 16, sizeof(float) * 16) == 0);
    // integrated_showcase_demo: six Renderables, all drawn (see the smoke test above).
    CHECK(data.drawItemCount == 6);
  };

  app.runFrame();
  app.runFrame();
  REQUIRE(app.shouldContinue());
  const auto before = app.captureFrameData();
  REQUIRE(before.isOk());
  equalsFrame(before.value());
  REQUIRE(before.value().directionalLights.size() == 1);
  CHECK(before.value().directionalLights[0].intensity == 3.0f);

  {
    const auto connection = app.openConnection();
    const auto light = atlantis::asset_system::parseEntityGuid("0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea").value();
    connection->submit(atlantis::world::access::SetProperty{
        {light, atlantis::world::ecs::componentTypeId<atlantis::world::Light>(),
         atlantis::schema::fieldId("world::Light", "intensity")},
        6.0f});
    app.runFrame();
    REQUIRE(app.shouldContinue());
    const auto after = app.captureFrameData();
    REQUIRE(after.isOk());
    equalsFrame(after.value());
    CHECK(after.value().directionalLights[0].intensity == 6.0f);
  }

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
}

// Plan 0055 M4 (Spec 0055 R3, ruling Q2 P-a; ADR-0106 D4): on the running
// default scene, under fatal Validation Layers, with the control driving the
// frames as atlantis_runtime's loop does: paused, submitted commands stay
// pending across frames (no event, the frame's lighting unchanged); a step of
// n applies exactly n frames' worth (one command submitted before each); once
// resumed, every frame applies again.
TEST_CASE("Runtime: pause holds command application; step n applies exactly n frames; resume restores it",
          "[runtime][gpu][control]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  atlantis::runtime::RuntimeControlHost::Options options;
  options.recordDiagnostics = false;
  atlantis::runtime::RuntimeControlHost control(atlantis::runtime::RuntimeControlHost::forApplication(app), options);
  const auto frame = [&] {
    control.beforeFrame();
    app.runFrame();
    control.afterFrame();
    REQUIRE(app.shouldContinue());
  };
  const auto intensity = [&] {
    return atlantis::runtime::RuntimeSmokeTestAccess::lightingPayloadBytes(app).directionalLights[0].intensity;
  };
  frame();
  REQUIRE(intensity() == 3.0f);

  const auto connection = app.openConnection();
  const auto light = atlantis::asset_system::parseEntityGuid("0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea").value();
  const atlantis::world::access::PropertyAddress address{
      light, atlantis::world::ecs::componentTypeId<atlantis::world::Light>(),
      atlantis::schema::fieldId("world::Light", "intensity")};
  const auto changes = connection->subscribe(atlantis::connection::EventFilter{});
  const auto set = [&](float value) { connection->submit(atlantis::world::access::SetProperty{address, value}); };

  control.pause();
  set(4.0f);
  frame();
  frame();
  frame();
  CHECK(intensity() == 3.0f);  // held: the command is still pending
  CHECK(connection->drainEvents(changes).value().empty());
  CHECK(connection->getProperty(address).value() == atlantis::world::access::PropertyValue(3.0f));

  int completed = 0;
  control.step(atlantis::connection::StepRequest{2, std::nullopt},
               [&](const atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>& r) {
                 REQUIRE(r.isOk());
                 CHECK(r.value().data.directionalLights[0].intensity == 5.0f);
                 ++completed;
               });
  frame();  // released: applies 4
  CHECK(intensity() == 4.0f);
  set(5.0f);
  frame();  // released: applies 5; the step completes
  CHECK(intensity() == 5.0f);
  CHECK(completed == 1);
  set(6.0f);
  frame();
  frame();  // held again
  CHECK(intensity() == 5.0f);
  CHECK(connection->drainEvents(changes).value().size() == 2);  // exactly the two released applications

  control.resume();
  frame();
  CHECK(intensity() == 6.0f);
  set(7.0f);
  frame();
  CHECK(intensity() == 7.0f);
}

// Plan 0055 M5 (Spec 0055 R3, R8; P8, ruling Q2 C2): a captured image is the
// world the last frame drew, rendered once more offscreen at the
// presentation's extent -- deterministic (two captures of an unchanged world
// are byte-identical), sensitive (an intensity edit changes it), a decodable
// RGBA PNG, and what a step with an image path reports -- under fatal
// Validation Layers.
TEST_CASE("Runtime: a capture is a deterministic, edit-sensitive PNG of the frame at the presentation extent",
          "[runtime][gpu][control][capture]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "atlantis_capture_tests" / std::to_string(std::random_device{}());
  fs::create_directories(dir);
  const auto bytes = [](const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };

  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  CHECK(app.captureImage((dir / "none.png").string()).error() == atlantis::connection::ControlError::NotRendering);

  app.runFrame();
  app.runFrame();
  REQUIRE(app.shouldContinue());
  const auto first = app.captureImage((dir / "a.png").string());
  REQUIRE(first.isOk());
  const auto second = app.captureImage((dir / "b.png").string());
  REQUIRE(second.isOk());
  CHECK(first.value().width > 0);
  CHECK(first.value().height > 0);
  CHECK(second.value() == atlantis::connection::CapturedImage{(dir / "b.png").string(), first.value().width,
                                                              first.value().height});
  const std::string a = bytes(dir / "a.png");
  REQUIRE(a.size() > 8);
  CHECK(a == bytes(dir / "b.png"));  // determinism
  const auto decoded = atlantis::image_regression::decodePng(dir / "a.png");
  REQUIRE(decoded.isOk());
  CHECK(decoded.value().pixels.width == first.value().width);
  CHECK(decoded.value().pixels.height == first.value().height);

  // The frame loop goes on after a capture.
  app.runFrame();
  REQUIRE(app.shouldContinue());

  {
    const auto connection = app.openConnection();
    const auto light = atlantis::asset_system::parseEntityGuid("0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea").value();
    connection->submit(atlantis::world::access::SetProperty{
        {light, atlantis::world::ecs::componentTypeId<atlantis::world::Light>(),
         atlantis::schema::fieldId("world::Light", "intensity")},
        6.0f});
    atlantis::runtime::RuntimeControlHost::Options options;
    options.recordDiagnostics = false;
    atlantis::runtime::RuntimeControlHost control(atlantis::runtime::RuntimeControlHost::forApplication(app),
                                                  options);
    std::optional<atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>> report;
    control.step(atlantis::connection::StepRequest{1, (dir / "c.png").string()},
                 [&](atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError> r) {
                   report = std::move(r);
                 });
    control.beforeFrame();
    app.runFrame();
    control.afterFrame();
    REQUIRE(app.shouldContinue());
    REQUIRE(report.has_value());
    REQUIRE(report->isOk());
    CHECK(report->value().data.directionalLights[0].intensity == 6.0f);
    REQUIRE(report->value().image.has_value());
    CHECK(report->value().image->path == (dir / "c.png").string());
    CHECK(bytes(dir / "c.png") != a);  // sensitivity
  }

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
  std::error_code ec;
  fs::remove_all(dir, ec);
}
