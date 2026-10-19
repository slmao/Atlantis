#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/profile_recorder.h>
#include <atlantis/cli/command.h>
#include <atlantis/cli/invocation.h>
#include <atlantis/connection/json.h>
#include <atlantis/connection/text.h>
#include <atlantis/remote/remote_client.h>
#include <atlantis/render_graph/execution.h>
#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/remote/remote_server.h>
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

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "png_codec.h"
#include "../../src/runtime/editor_attachment.h"
#include "vulkan_debug_utils.h"  // Plan 0059 M5: the debug-name observer
#include "../../src/runtime/benchmark_run.h"
#include "../../src/runtime/benchmark_variants.h"

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
using atlantis::runtime::RuntimeSmokeTestAccess;  // Plan 0056 M6/M7
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

  // Plan 0056 M6: the overlay frame's targets, and a test-only readback of
  // the Viewport target (left in ShaderRead by the frame) between frames.
  [[nodiscard]] static std::optional<atlantis::rhi::Extent2D> lastSeenExtent(const RuntimeApplication& app) {
    return app.lastSeenExtent_;
  }
  [[nodiscard]] static atlantis::rhi::Extent2D depthExtent(const RuntimeApplication& app) {
    return app.depthTexture_->extent();
  }
  [[nodiscard]] static atlantis::rhi::Extent2D viewportTargetExtent(const RuntimeApplication& app) {
    return app.overlayViewportTarget_->extent();
  }
  struct Readback {
    atlantis::rhi::Extent2D extent;
    atlantis::rhi::Format format = atlantis::rhi::Format::Unknown;
    std::vector<std::uint8_t> pixels;
  };
  [[nodiscard]] static Readback readViewport(RuntimeApplication& app) {
    using atlantis::rhi::ResourceState;
    REQUIRE(app.device_->waitIdle().isOk());
    atlantis::rhi::RenderTarget& target = *app.overlayViewportTarget_;
    Readback out{target.extent(), target.format(), {}};
    const std::size_t bytes = static_cast<std::size_t>(out.extent.width) * out.extent.height * 4;
    auto buffer = app.device_->createBuffer({.purpose = atlantis::rhi::BufferPurpose::Readback, .sizeBytes = bytes});
    auto commandList = app.device_->createCommandList();
    REQUIRE(buffer.isOk());
    REQUIRE(commandList.isOk());
    atlantis::render_graph::RenderGraphBuilder builder;
    const auto resource = builder.declareResource("viewport-readback");
    const auto pass = builder.declarePass("viewport-readback");
    builder.writes(pass, resource, ResourceState::TransferSource);
    atlantis::rhi::Buffer& readback = *buffer.value();
    builder.setExecute(pass, [&target, &readback](atlantis::rhi::CommandList& cmd) {
      cmd.copyRenderTargetToBuffer(target, readback);
    });
    auto compiled = builder.compile();
    REQUIRE(compiled.isOk());
    atlantis::render_graph::execute(compiled.value(),
                                    {{.resource = compiled.value().resourceAt(0),
                                      .target = &target,
                                      .incomingState = ResourceState::ShaderRead,
                                      .finalState = std::nullopt}},
                                    *commandList.value());
    REQUIRE(app.device_->submit(std::move(commandList.value()), target).isOk());
    REQUIRE(app.device_->waitIdle().isOk());
    const auto* pixels = static_cast<const std::uint8_t*>(readback.mappedData());
    out.pixels.assign(pixels, pixels + bytes);
    return out;
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
  const bool identical = a == bytes(dir / "b.png");  // compared as a bool: Catch must not print PNG bytes
  CHECK(identical);  // determinism
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
    const bool changed = bytes(dir / "c.png") != a;
    CHECK(changed);  // sensitivity
  }

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// Plan 0055 M7 (Spec 0055 R8, ruling Q7 A-a; J4): the north star in one
// process, every link public -- the `atlantis` command layer and its JSON,
// over a RemoteConnection and RemoteControl on a real loopback socket, to a
// RemoteServer polled after each frame of a windowed RuntimeApplication whose
// RuntimeControlHost drives its frames, exactly as atlantis_runtime --listen
// runs them. The client's waits run those frames (J4). Under fatal
// Validation Layers.
namespace {

namespace json = atlantis::connection::json;

class NorthStarBatch final : public atlantis::cli::QueryBatch {
 public:
  explicit NorthStarBatch(atlantis::remote::RemoteSession& session) : session_(session) {}
  std::vector<atlantis::Result<std::vector<atlantis::schema::TypeId>, atlantis::world::access::AccessError>>
  listComponents(std::span<const atlantis::asset_system::EntityGuid> entities) override {
    return session_.listComponents(entities);
  }
  std::vector<atlantis::Result<atlantis::world::access::PropertyValue, atlantis::world::access::AccessError>>
  getProperties(std::span<const atlantis::world::access::PropertyAddress> addresses) override {
    return session_.getProperties(addresses);
  }

 private:
  atlantis::remote::RemoteSession& session_;
};

// The attached Runtime and a `--json` command line against it.
struct Attached {
  explicit Attached(RuntimeApplication& application)
      : app(application), control(atlantis::runtime::RuntimeControlHost::forApplication(app), options()) {
    auto listening = atlantis::remote::RemoteServer::listen(
        0, atlantis::remote::generateToken(), app.sceneGuid(), [this] { return app.openConnection(); }, &control);
    REQUIRE(listening.isOk());
    server = std::move(listening.value());
    atlantis::remote::RemoteOptions remoteOptions;
    remoteOptions.whileWaiting = [this] {
      control.beforeFrame();
      app.runFrame();
      control.afterFrame();
      server->poll();
    };
    auto connected = atlantis::remote::connectRemote(server->session(), remoteOptions);
    REQUIRE(connected.isOk());
    session = std::move(connected.value());
    batch = std::make_unique<NorthStarBatch>(*session);
    atlantis::cli::InvocationOptions invocationOptions;
    invocationOptions.json = true;
    invocation = std::make_unique<atlantis::cli::Invocation>(session->connection(), &session->control(), batch.get(),
                                                             out, err, invocationOptions);
  }

  static atlantis::runtime::RuntimeControlHost::Options options() {
    atlantis::runtime::RuntimeControlHost::Options o;
    o.recordDiagnostics = false;  // Core's sink stays the test process's
    return o;
  }

  // One command; its envelope, which must say ok.
  json::Value run(const std::vector<std::string>& command) {
    out.str({});
    err.str({});
    INFO(command[0] << " " << (command.size() > 1 ? command[1] : ""));
    REQUIRE(invocation->run(command) == 0);
    auto envelope = json::parse(out.str());
    REQUIRE(envelope.isOk());
    CHECK(envelope.value().find("ok")->asBool());
    REQUIRE_FALSE(session->failure().has_value());
    return *envelope.value().find("result");
  }

  RuntimeApplication& app;
  atlantis::runtime::RuntimeControlHost control;
  std::unique_ptr<atlantis::remote::RemoteServer> server;
  std::unique_ptr<atlantis::remote::RemoteSession> session;
  std::unique_ptr<NorthStarBatch> batch;
  std::ostringstream out;
  std::ostringstream err;
  std::unique_ptr<atlantis::cli::Invocation> invocation;
};

[[nodiscard]] float asFloat(const json::Value& value) {
  float out = 0.0f;
  REQUIRE(value.toFloat(out));
  return out;
}

// Finds the light by its kind, reads its intensity, sets it to `to`, pauses,
// steps one frame with an image, and checks the frame data and the image.
// Returns the light's GUID.
// `imageChanges`: whether the light can change the image at all -- false for
// a light (and its range) outside the scene camera's view.
void runNorthStar(RuntimeApplication& app, std::string_view kind, std::string_view expectedGuid, float from, float to,
                  const std::optional<std::array<float, 3>>& pointPosition, bool imageChanges = true) {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "atlantis_north_star" / std::to_string(std::random_device{}());
  fs::create_directories(dir);
  Attached attached(app);

  // Find: entity list --with Light --where Light.kind=<kind>
  const json::Value found = attached.run(
      {"entity", "list", "--with", "Light", "--where", "Light.kind=" + std::string(kind)});
  std::vector<std::string> guids;
  for (const json::Value& entry : found.asArray()) guids.push_back(entry.find("guid")->asString());
  REQUIRE(std::find(guids.begin(), guids.end(), std::string(expectedGuid)) != guids.end());
  const std::string guid(expectedGuid);

  // Read.
  CHECK(asFloat(*attached.run({"property", "get", guid, "Light.intensity"}).find("value")) == from);

  // The pre-edit image.
  attached.run({"runtime", "pause"});
  const json::Value beforeStep =
      attached.run({"runtime", "step", "--capture", (dir / "before.png").string()});
  CHECK(beforeStep.find("applied")->asBool());

  // Modify: a write while paused stays pending until the step.
  const json::Value set = attached.run({"property", "set", guid, "Light.intensity", std::to_string(to)});
  CHECK(set.find("outcome")->find("pending")->asBool());
  CHECK(asFloat(*attached.run({"property", "get", guid, "Light.intensity"}).find("value")) == from);

  // Step and capture.
  const json::Value stepped = attached.run({"runtime", "step", "--capture", (dir / "after.png").string()});
  const json::Value& frameData = *stepped.find("capture")->find("frameData");
  if (pointPosition) {
    bool matched = false;
    for (const json::Value& light : frameData.find("pointLights")->asArray()) {
      const auto& position = light.find("position")->asArray();
      if (std::fabs(asFloat(position[0]) - (*pointPosition)[0]) < 1e-3f &&
          std::fabs(asFloat(position[1]) - (*pointPosition)[1]) < 1e-3f &&
          std::fabs(asFloat(position[2]) - (*pointPosition)[2]) < 1e-3f) {
        CHECK(asFloat(*light.find("intensity")) == to);  // exactly
        matched = true;
      }
    }
    CHECK(matched);
  } else {
    REQUIRE(frameData.find("directionalLights")->asArray().size() == 1);
    CHECK(asFloat(*frameData.find("directionalLights")->asArray()[0].find("intensity")) == to);  // exactly
  }
  CHECK(asFloat(*attached.run({"property", "get", guid, "Light.intensity"}).find("value")) == to);

  // The image changed.
  const auto bytes = [](const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };
  const std::string before = bytes(dir / "before.png");
  REQUIRE(before.size() > 8);
  const bool changed = bytes(dir / "after.png") != before;  // a bool: Catch must not print PNG bytes
  if (imageChanges) {
    CHECK(changed);
  } else {
    WARN("image changed: " << (changed ? "yes" : "no (the light is outside the camera's view)"));
  }
  attached.run({"runtime", "resume"});
  std::error_code ec;
  fs::remove_all(dir, ec);
}

}  // namespace

TEST_CASE("Runtime: the north star in process -- the default scene's Directional light, 3 to 6",
          "[runtime][gpu][north_star]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  app.runFrame();
  REQUIRE(app.shouldContinue());
  runNorthStar(app, "Directional", "0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea", 3.0f, 6.0f, std::nullopt);
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

TEST_CASE("Runtime: the north star in process -- Bistro's point light 6b63b12c, 12 to 24",
          "[runtime][gpu][north_star][bistro]") {
#if !defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
  SKIP("Bistro content is not present (the content-gated build step was not declared)");
#else
  BootstrapConfig config = buildSmokeConfig();
  config.sceneAsset = atlantis::asset_system::deriveAssetGuid(
      sceneGuidFromDefinition(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID), "scene");
  // As atlantis_runtime's `bistro` entry: lit by its own lights only.
  config.environmentArtifactPath.clear();
  config.environmentMetadataPath.clear();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  app.runFrame();
  REQUIRE(app.shouldContinue());
  // Spec 0055 R8's light, exactly: found, read (12), set (24), stepped and
  // captured, its frame data 24 exactly. It and its 6 m range lie about 82
  // degrees off the scene camera's axis (camera at (-22, 1.7, 22), yaw -0.86),
  // outside the view, so no pixel can change (verified: even 2000 leaves the
  // capture byte-identical) -- reported as a deviation in the implementing PR.
  runNorthStar(app, "Point", "6b63b12c-9cde-4ae2-8391-c0b4cefadb7d", 12.0f, 24.0f,
               std::array<float, 3>{-39.615f, 3.255f, -5.032f}, /*imageChanges=*/false);
  // The same chain on a point light in view (the cafe's, overlay node 59)
  // shows the next frame's image change on Bistro.
  runNorthStar(app, "Point", "425c3b17-dcac-4148-831c-22a454829357", 4.5f, 9.0f,
               std::array<float, 3>{-7.966f, 3.42f, 9.084f});
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
#endif
}

// --- Plan 0056 M6 (Spec 0056 R4; P6, P7, ADR-0108 D3/D4): the frame with an
// overlay attached, under fatal Validation Layers. ------------------------------

namespace {

[[nodiscard]] atlantis::runtime::OverlayShaderPaths overlayShaders() {
  static const std::string vertex = std::string(ATLANTIS_RUNTIME_EDITOR_UI_SHADER_DIR) + "/editor_ui.vert.spv";
  static const std::string fragment = std::string(ATLANTIS_RUNTIME_EDITOR_UI_SHADER_DIR) + "/editor_ui.frag.spv";
  return {vertex, fragment};
}

// A FrameOverlay with a settable Viewport size, an empty UI and a 1x1 atlas.
class FakeOverlay final : public atlantis::runtime::FrameOverlay {
 public:
  void onPlatformEvent(const atlantis::platform::PlatformEvent&) override { ++events; }
  [[nodiscard]] atlantis::rhi::Extent2D viewportExtent() const override { return extent; }
  [[nodiscard]] const atlantis::renderer::UiDrawList& drawList() const override { return list; }
  [[nodiscard]] atlantis::runtime::FontAtlasPixels fontAtlas() const override { return {1, 1, atlas}; }
  [[nodiscard]] std::optional<atlantis::runtime::ViewportCameraOverride> viewportCamera() const override {
    return camera;
  }

  atlantis::rhi::Extent2D extent{320, 200};
  std::optional<atlantis::runtime::ViewportCameraOverride> camera;  // Plan 0062 M2
  atlantis::renderer::UiDrawList list;
  std::array<std::uint8_t, 4> atlas{255, 255, 255, 255};
  int events = 0;
};

}  // namespace

TEST_CASE("Runtime with an editor attached: the Viewport target holds exactly what a capture of the frame shows",
          "[runtime][gpu][editor]") {
  namespace fs = std::filesystem;
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  atlantis::runtime::RuntimeControlHost::Options options;
  options.recordDiagnostics = false;
  atlantis::runtime::RuntimeControlHost control(atlantis::runtime::RuntimeControlHost::forApplication(app), options);
  atlantis::runtime::EditorAttachment editor(app, control);
  REQUIRE(app.attachOverlay(editor, overlayShaders()).isOk());
  CHECK(app.attachOverlay(editor, overlayShaders()).error() == atlantis::runtime::OverlayAttachError::AlreadyAttached);
  for (int i = 0; i < 4; ++i) {
    app.runFrame();
    editor.update();
  }
  app.runFrame();
  REQUIRE(app.shouldContinue());

  // The scene is rendered at the editor's Viewport panel size.
  const auto panel = editor.editor().viewportSize();
  REQUIRE(panel.width > 0);
  const atlantis::rhi::Extent2D viewportExtent = RuntimeSmokeTestAccess::viewportTargetExtent(app);
  CHECK(viewportExtent == atlantis::rhi::Extent2D{panel.width, panel.height});
  CHECK(RuntimeSmokeTestAccess::lastSeenExtent(app) == viewportExtent);
  CHECK(RuntimeSmokeTestAccess::depthExtent(app) == viewportExtent);
  CHECK_FALSE(editor.drawList().commands.empty());

  // The Viewport target, read back, equals a capture of the same world at the
  // same size (the Spec 0055 offscreen path), byte for byte.
  const auto viewport = RuntimeSmokeTestAccess::readViewport(app);
  const fs::path dir = fs::temp_directory_path() / "atlantis_editor_tests" / std::to_string(std::random_device{}());
  fs::create_directories(dir);
  const auto captured = app.captureImage((dir / "viewport.png").string());
  REQUIRE(captured.isOk());
  CHECK(captured.value().width == viewport.extent.width);
  CHECK(captured.value().height == viewport.extent.height);
  const auto decoded = atlantis::image_regression::decodePng(dir / "viewport.png");
  REQUIRE(decoded.isOk());
  const bool bgra = viewport.format == atlantis::rhi::Format::Bgra8Unorm ||
                    viewport.format == atlantis::rhi::Format::Bgra8Srgb;
  std::size_t mismatches = 0;
  const auto& png = decoded.value().pixels.rgba8;
  REQUIRE(png.size() == viewport.pixels.size());
  for (std::size_t i = 0; i < viewport.pixels.size(); i += 4) {
    const std::uint8_t r = viewport.pixels[i + (bgra ? 2 : 0)];
    const std::uint8_t g = viewport.pixels[i + 1];
    const std::uint8_t b = viewport.pixels[i + (bgra ? 0 : 2)];
    if (png[i] != r || png[i + 1] != g || png[i + 2] != b) ++mismatches;
  }
  CHECK(mismatches == 0);

  // The frame loop goes on; detaching restores the plain frame at the window's extent.
  app.runFrame();
  editor.update();
  app.detachOverlay();
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK_FALSE(RuntimeSmokeTestAccess::lastSeenExtent(app) == viewportExtent);
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("Runtime with an overlay attached: resizing the Viewport recreates the scene's targets at its size",
          "[runtime][gpu][editor]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  FakeOverlay overlay;
  REQUIRE(app.attachOverlay(overlay, overlayShaders()).isOk());
  app.runFrame();
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK(overlay.events > 0);  // the pump's events reach the overlay
  CHECK(RuntimeSmokeTestAccess::viewportTargetExtent(app) == atlantis::rhi::Extent2D{320, 200});
  CHECK(RuntimeSmokeTestAccess::depthExtent(app) == atlantis::rhi::Extent2D{320, 200});
  overlay.extent = {200, 120};
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK(RuntimeSmokeTestAccess::viewportTargetExtent(app) == atlantis::rhi::Extent2D{200, 120});
  CHECK(RuntimeSmokeTestAccess::depthExtent(app) == atlantis::rhi::Extent2D{200, 120});
  CHECK(RuntimeSmokeTestAccess::lastSeenExtent(app) == atlantis::rhi::Extent2D{200, 120});
  overlay.extent = {0, 0};  // a collapsed panel renders at 1x1
  app.runFrame();
  CHECK(RuntimeSmokeTestAccess::viewportTargetExtent(app) == atlantis::rhi::Extent2D{1, 1});
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);  // shutdown() detaches
}

// --- Plan 0062 M2 (Spec 0062 R3, R9; ADR-0123 D1, D3; ruling Q3 F-a): the
// Viewport rendered from an overlay's camera, under fatal Validation Layers. ----

TEST_CASE("Runtime with an overlay camera: the Viewport is drawn from it, and frame data and captures follow it",
          "[runtime][gpu][editor][camera]") {
  namespace fs = std::filesystem;
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  FakeOverlay overlay;
  REQUIRE(app.attachOverlay(overlay, overlayShaders()).isOk());
  app.runFrame();
  app.runFrame();
  REQUIRE(app.shouldContinue());
  const auto scenePose = app.activeCameraPose();
  REQUIRE(scenePose.has_value());
  const float aspect = 320.0f / 200.0f;
  const auto sceneData = app.captureFrameData();
  REQUIRE(sceneData.isOk());
  const auto sceneImage = RuntimeSmokeTestAccess::readViewport(app);

  // The scene camera's own pose and lens, supplied: byte for byte the same frame.
  atlantis::runtime::ViewportCameraOverride same{scenePose->worldMatrix, scenePose->camera.fovYRadians,
                                                 scenePose->camera.nearZ, scenePose->camera.farZ};
  overlay.camera = same;
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK(app.captureFrameData().value() == sceneData.value());
  const auto sameImage = RuntimeSmokeTestAccess::readViewport(app);
  CHECK((sameImage.pixels == sceneImage.pixels));

  // Another camera: moved back and up, a narrower lens.
  atlantis::runtime::ViewportCameraOverride other = same;
  other.worldMatrix[12] += 1.5f;
  other.worldMatrix[13] += 1.0f;
  other.worldMatrix[14] += 3.0f;
  other.fovYRadians = 0.8f;
  other.nearZ = 0.2f;
  other.farZ = 60.0f;
  overlay.camera = other;
  app.runFrame();
  REQUIRE(app.shouldContinue());
  const auto expected = atlantis::runtime::extractCameraMatrices(other.worldMatrix, other.fovYRadians, other.nearZ,
                                                                 other.farZ, aspect);
  REQUIRE(expected.isOk());
  const std::array<float, 32> written = RuntimeSmokeTestAccess::cameraMatrixFloats(app);  // the frame's camera block
  CHECK(std::equal(written.begin(), written.begin() + 16, expected.value().view.begin()));
  CHECK(std::equal(written.begin() + 16, written.end(), expected.value().projection.begin()));
  const auto otherData = app.captureFrameData();
  REQUIRE(otherData.isOk());
  CHECK(otherData.value().view == expected.value().view);
  CHECK(otherData.value().projection == expected.value().projection);
  CHECK(otherData.value().directionalLights == sceneData.value().directionalLights);
  const auto otherImage = RuntimeSmokeTestAccess::readViewport(app);
  CHECK_FALSE((otherImage.pixels == sceneImage.pixels));
  CHECK(app.activeCameraPose()->worldMatrix == scenePose->worldMatrix);  // the World's camera is untouched

  // A capture renders the camera drawn: it equals the Viewport target.
  const fs::path dir = fs::temp_directory_path() / "atlantis_editor_tests" / std::to_string(std::random_device{}());
  fs::create_directories(dir);
  const auto captured = app.captureImage((dir / "override.png").string());
  REQUIRE(captured.isOk());
  const auto decoded = atlantis::image_regression::decodePng(dir / "override.png");
  REQUIRE(decoded.isOk());
  const bool bgra = otherImage.format == atlantis::rhi::Format::Bgra8Unorm ||
                    otherImage.format == atlantis::rhi::Format::Bgra8Srgb;
  const auto& png = decoded.value().pixels.rgba8;
  REQUIRE(png.size() == otherImage.pixels.size());
  std::size_t mismatches = 0;
  for (std::size_t i = 0; i < otherImage.pixels.size(); i += 4) {
    if (png[i] != otherImage.pixels[i + (bgra ? 2 : 0)] || png[i + 1] != otherImage.pixels[i + 1] ||
        png[i + 2] != otherImage.pixels[i + (bgra ? 0 : 2)]) {
      ++mismatches;
    }
  }
  CHECK(mismatches == 0);

  // An unusable camera (looking straight up; then a near plane of zero) is
  // ignored: the scene's camera is drawn and the frame goes on.
  atlantis::runtime::ViewportCameraOverride up = same;
  up.worldMatrix = {1, 0, 0, 0, 0, 0, -1, 0, 0, -1, 0, 0, 0, 1, 0, 1};
  overlay.camera = up;
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK(app.captureFrameData().value() == sceneData.value());
  atlantis::runtime::ViewportCameraOverride zeroNear = other;
  zeroNear.nearZ = 0.0f;
  overlay.camera = zeroNear;
  app.runFrame();
  REQUIRE(app.shouldContinue());
  CHECK(app.captureFrameData().value() == sceneData.value());

  // No camera: the scene's again.
  overlay.camera.reset();
  app.runFrame();
  CHECK(app.captureFrameData().value() == sceneData.value());
  CHECK((RuntimeSmokeTestAccess::readViewport(app).pixels == sceneImage.pixels));
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

// --- Plan 0056 M7 (Spec 0056 north star, ruling Q8): the editor drives the
// World through its own model actions -- the calls its view makes on a click
// (M5 covers the clicks) -- with the editor attached to the frame, under fatal
// Validation Layers. -------------------------------------------------------------

namespace {

// An application with the editor attached and the control host the
// executable creates for it; frame() is atlantis_runtime's loop body.
struct EditorHost {
  explicit EditorHost(RuntimeApplication& application)
      : app(application),
        control(atlantis::runtime::RuntimeControlHost::forApplication(application), [] {
          atlantis::runtime::RuntimeControlHost::Options options;
          options.recordDiagnostics = false;
          return options;
        }()),
        editor(application, control) {
    REQUIRE(app.attachOverlay(editor, overlayShaders()).isOk());
  }
  ~EditorHost() { app.detachOverlay(); }
  EditorHost(const EditorHost&) = delete;
  EditorHost& operator=(const EditorHost&) = delete;

  void frame() {
    control.beforeFrame();
    app.runFrame();
    control.afterFrame();
    editor.update();
    REQUIRE(app.shouldContinue());
  }

  RuntimeApplication& app;
  atlantis::runtime::RuntimeControlHost control;
  atlantis::runtime::EditorAttachment editor;
};

[[nodiscard]] std::size_t inspectorField(atlantis::editor::Editor& editor, std::string_view path) {
  const auto& fields = editor.inspector().fields();
  for (std::size_t i = 0; i < fields.size(); ++i) {
    if (fields[i].path == path) return i;
  }
  FAIL("no Inspector field " << path);
  return 0;
}

// The light's intensity in the frame data just drawn: the directional light,
// or the point light at `pointPosition`.
[[nodiscard]] float frameIntensity(RuntimeApplication& app, const std::optional<std::array<float, 3>>& pointPosition) {
  const auto data = app.captureFrameData();
  REQUIRE(data.isOk());
  if (!pointPosition) {
    REQUIRE(data.value().directionalLights.size() == 1);
    return data.value().directionalLights[0].intensity;
  }
  for (const auto& light : data.value().pointLights) {
    if (std::fabs(light.position[0] - (*pointPosition)[0]) < 1e-3f &&
        std::fabs(light.position[1] - (*pointPosition)[1]) < 1e-3f &&
        std::fabs(light.position[2] - (*pointPosition)[2]) < 1e-3f) {
      return light.intensity;
    }
  }
  FAIL("no point light at the expected position");
  return 0.0f;
}

// Select in the Hierarchy -> the generated Inspector shows Light.intensity =
// `from` -> commit `to` -> the next frame's frame data has `to` exactly and the
// Viewport image changed; then pause holds a further edit across three frames
// and one step applies it.
void runEditorNorthStar(EditorHost& host, std::string_view guidText, float from, float to,
                        const std::optional<std::array<float, 3>>& pointPosition, bool imageChanges = true) {
  atlantis::editor::Editor& editor = host.editor.editor();
  const auto guid = atlantis::asset_system::parseEntityGuid(guidText).value();
  editor.hierarchy().select(guid);
  host.frame();  // the editor's frame takes up the selection
  REQUIRE(editor.inspector().subject() == guid);
  const std::size_t intensity = inspectorField(editor, "Light.intensity");
  REQUIRE(editor.inspector().fields()[intensity].value == atlantis::world::access::PropertyValue{from});
  CHECK(editor.inspector().fields()[intensity].editable);
  CHECK(frameIntensity(host.app, pointPosition) == from);
  const auto before = RuntimeSmokeTestAccess::readViewport(host.app);

  (void)editor.inspector().commit(intensity, to);  // the field's edit, completed
  host.frame();
  CHECK(frameIntensity(host.app, pointPosition) == to);  // exactly
  CHECK(editor.inspector().fields()[intensity].value == atlantis::world::access::PropertyValue{to});
  REQUIRE(editor.inspector().outcome(intensity).has_value());
  CHECK(editor.inspector().outcome(intensity)->status == atlantis::editor::EditStatus::Applied);
  const auto after = RuntimeSmokeTestAccess::readViewport(host.app);
  REQUIRE(after.extent == before.extent);
  const bool changed = after.pixels != before.pixels;  // a bool: Catch must not print the pixels
  if (imageChanges) {
    CHECK(changed);
  } else {
    WARN("Viewport changed: " << (changed ? "yes" : "no (the light is outside the camera's view)"));
  }

  // Pause (the toolbar's Pause calls RuntimeControl::pause()): a further edit
  // is held across three frames; Step applies it.
  host.control.pause();
  (void)editor.inspector().commit(intensity, from);
  for (int i = 0; i < 3; ++i) host.frame();
  CHECK(frameIntensity(host.app, pointPosition) == to);
  CHECK(editor.inspector().outcome(intensity)->status == atlantis::editor::EditStatus::Pending);
  host.control.step(atlantis::connection::StepRequest{1, std::nullopt}, [](auto) {});
  host.frame();
  CHECK(frameIntensity(host.app, pointPosition) == from);
  CHECK(editor.inspector().outcome(intensity)->status == atlantis::editor::EditStatus::Applied);
  host.control.resume();
  host.frame();
}

}  // namespace

TEST_CASE("Editor north star: the default scene's Directional light, 3 to 6, through the editor",
          "[runtime][gpu][editor][north_star]") {
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    EditorHost host(app);
    for (int i = 0; i < 3; ++i) host.frame();
    runEditorNorthStar(host, "0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea", 3.0f, 6.0f, std::nullopt);
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}

TEST_CASE("Editor north star: Bistro's point light 6b63b12c 12 to 24, and the cafe light's image change",
          "[runtime][gpu][editor][north_star][bistro]") {
#if !defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
  SKIP("Bistro content is not present (the content-gated build step was not declared)");
#else
  BootstrapConfig config = buildSmokeConfig();
  config.sceneAsset = atlantis::asset_system::deriveAssetGuid(
      sceneGuidFromDefinition(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID), "scene");
  config.environmentArtifactPath.clear();
  config.environmentMetadataPath.clear();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  {
    EditorHost host(app);
    for (int i = 0; i < 3; ++i) host.frame();
    // 6b63b12c lies outside the camera's view (the Spec 0055 Correction
    // 2026-10-08): exact in frame data; the in-view cafe light 425c3b17
    // shows the Viewport's image change.
    runEditorNorthStar(host, "6b63b12c-9cde-4ae2-8391-c0b4cefadb7d", 12.0f, 24.0f,
                       std::array<float, 3>{-39.615f, 3.255f, -5.032f}, /*imageChanges=*/false);
    runEditorNorthStar(host, "425c3b17-dcac-4148-831c-22a454829357", 4.5f, 9.0f,
                       std::array<float, 3>{-7.966f, 3.42f, 9.084f});
  }
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
#endif
}

// Plan 0059 M5 (Spec 0059 R5, ADR-0115 D5): the Runtime names the GPU objects
// it creates and the frame's command list, so a capture shows them by name.
// Vulkan has no call to read a name back; the backend's debug-utils wrapper
// reports every name it is asked to set to a process-wide test observer.
TEST_CASE("Runtime gives its GPU objects and its frame's command list stable names",
          "[runtime][gpu][debug_names]") {
  std::vector<std::pair<VkObjectType, std::string>> names;
  atlantis::vulkan_backend::detail::DebugUtilsObserver observer;
  observer.onName = [&names](VkObjectType type, std::string_view name) { names.emplace_back(type, std::string(name)); };
  atlantis::vulkan_backend::detail::setGlobalDebugUtilsObserverForTesting(&observer);

  BootstrapConfig config = buildSmokeConfig();
  config.sceneAsset = sceneGuidFromDefinition(ATLANTIS_RUNTIME_BLOOM_SCENE_GUID);
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  for (int i = 0; i < 3 && app.shouldContinue(); ++i) app.runFrame();
  REQUIRE(app.shouldContinue());
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
  atlantis::vulkan_backend::detail::setGlobalDebugUtilsObserverForTesting(nullptr);

  const auto has = [&names](VkObjectType type, const std::string& name) {
    for (const auto& [t, n] : names) {
      if (t == type && n == name) return true;
    }
    return false;
  };
  const auto hasPrefix = [&names](const std::string& prefix) {
    for (const auto& [t, n] : names) {
      if (n.rfind(prefix, 0) == 0) return true;
    }
    return false;
  };
#if defined(ATLANTIS_GPU_MARKERS)
  CHECK(has(VK_OBJECT_TYPE_BUFFER, "camera_uniform"));
  CHECK(has(VK_OBJECT_TYPE_BUFFER, "fullscreen_triangle_vb"));
  CHECK(has(VK_OBJECT_TYPE_BUFFER, "shadow_light_space_uniform"));
  CHECK(has(VK_OBJECT_TYPE_IMAGE, "shadow_map"));
  CHECK(has(VK_OBJECT_TYPE_IMAGE, "depth"));
  CHECK(has(VK_OBJECT_TYPE_IMAGE, "hdr_color"));
  CHECK(has(VK_OBJECT_TYPE_IMAGE, "bloom_d1"));
  CHECK(has(VK_OBJECT_TYPE_IMAGE, "bloom_u1"));
  CHECK(has(VK_OBJECT_TYPE_IMAGE, "bloom_composite"));
  CHECK(has(VK_OBJECT_TYPE_SAMPLER, "bloom_sampler"));
  CHECK(has(VK_OBJECT_TYPE_PIPELINE, "pipeline:shadow_cast"));
  CHECK(has(VK_OBJECT_TYPE_PIPELINE, "pipeline:output_transform"));
  CHECK(has(VK_OBJECT_TYPE_PIPELINE, "pipeline:bloom_0"));
  CHECK(has(VK_OBJECT_TYPE_COMMAND_BUFFER, "frame"));
  CHECK(hasPrefix("mesh:"));
  CHECK(hasPrefix("pipeline:material:"));
  CHECK(hasPrefix("texture:"));
  CHECK(hasPrefix("sampler:"));
  // A scene resource's name is "<kind>:<16 lowercase hex digits>".
  for (const auto& [type, name] : names) {
    if (name.rfind("mesh:", 0) == 0 && name.find('/') == std::string::npos) {
      CHECK(name.size() == 5 + 16);
    }
  }
#else
  CHECK(names.empty());
#endif
}

// Plan 0059 M7 (Spec 0059 R3, R4, R6): a recorded Runtime frame carries every zone
// of the Zone map, every counter-set key the Runtime and the backend produce, and
// the GPU regions of its passes -- on the default scene, under fatal validation.
#if defined(ATLANTIS_PROFILING)
TEST_CASE("Runtime: a profiled frame carries every Zone-map zone, counter and pass region",
          "[runtime][gpu][profile]") {
  namespace profile = atlantis::profile;
  BootstrapConfig config = buildSmokeConfig();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());

  const auto zoneCount = [](const profile::FrameRecord& record, const char* name) {
    return record.zoneCount[profile::internName(name)];
  };
  const auto counter = [](const profile::FrameRecord& record, const char* name) {
    return record.counter[profile::internName(name)];
  };

  profile::startRecording();
  const std::uint64_t firstFrame = profile::currentFrameId() + 1;
  app.runFrame();
  REQUIRE(app.shouldContinue());

  // The first frame realizes the scene's materials: the one-off stall and upload.
  // (Read now: the recorder keeps only the last few frames.)
  profile::FrameRecord first;
  REQUIRE(profile::readFrame(firstFrame, first));
  CHECK(first.presented);
  CHECK(zoneCount(first, "WaitIdle") > 0);
  CHECK(counter(first, "materials_realized") == 4.0);
  CHECK(counter(first, "upload_bytes") > 0.0);

  constexpr int kFrames = 6;
  for (int i = 1; i < kFrames && app.shouldContinue(); ++i) app.runFrame();
  REQUIRE(app.shouldContinue());
  profile::stopRecording();

  // A later frame whose GPU results have arrived (they come one frame late).
  profile::FrameRecord steady;
  bool found = false;
  for (std::uint64_t id = firstFrame + 2; id + 1 < firstFrame + kFrames; ++id) {
    if (profile::readFrame(id, steady) && steady.presented && steady.gpuDelivered) {
      found = true;
      break;
    }
  }
  REQUIRE(found);

  for (const char* zone : {"Frame", "World", "Runtime", "Acquire", "RenderExtraction", "Renderer", "RenderGraph",
                           "Submit", "Present", "WaitPreviousGpu", "RecordPass:shadow", "RecordPass:draw",
                           "RecordPass:output_transform"}) {
    INFO(zone);
    CHECK(zoneCount(steady, zone) > 0);
  }
  CHECK(zoneCount(steady, "WaitIdle") == 0);  // not a realization frame
  CHECK(steady.zoneNs[profile::internName("Frame")] > 0);

  CHECK(counter(steady, "renderables_total") == 6.0);  // integrated_showcase_demo: one ground plane, five spheres
  CHECK(counter(steady, "renderables_drawn") == 6.0);
  CHECK(counter(steady, "renderables_culled") == 0.0);  // there is no culling
  CHECK(counter(steady, "lights_directional") >= 1.0);
  CHECK(counter(steady, "uniform_write_bytes") > 0.0);
  CHECK(counter(steady, "draw_calls") >= 12.0);  // six shadow-pass draws and six main-pass draws
  CHECK(counter(steady, "triangles") > 0.0);
  CHECK(counter(steady, "pipeline_binds") > 0.0);
  CHECK(counter(steady, "vertex_buffer_binds") > 0.0);
  CHECK(counter(steady, "index_buffer_binds") > 0.0);
  CHECK(counter(steady, "descriptor_set_binds") > 0.0);
  CHECK(counter(steady, "descriptor_writes") > 0.0);
  CHECK(counter(steady, "push_constants") > 0.0);

  // GPU regions: the passes (depth 0) and the draw pass's Opaque segment (depth 1).
  bool shadow = false, draw = false, outputTransform = false, opaque = false;
  for (std::uint32_t i = 0; i < steady.regionCount; ++i) {
    const std::string_view name = profile::nameOf(steady.regions[i].name);
    if (name == "shadow") shadow = true;
    if (name == "draw") draw = true;
    if (name == "output_transform") outputTransform = true;
    if (name == "Opaque") {
      opaque = true;
      CHECK(steady.regions[i].depth == 1);
    }
    CHECK(steady.regions[i].endNs >= steady.regions[i].beginNs);
  }
  CHECK(shadow);
  CHECK(draw);
  CHECK(outputTransform);
  CHECK(opaque);

  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
}
#endif

// ---------------------------------------------------------------------------
// Plan 0059 M9 (Spec 0059 R8, R9; ADR-0117 D1-D4): the benchmark. The variants on
// the real Bistro scene (content-gated), and a whole `--benchmark` run on the
// default scene. Each in its own TEST_CASE: catch_discover_tests runs every case
// in its own process, and a process creates one window lifecycle.
// ---------------------------------------------------------------------------

namespace {

namespace bench = atlantis::runtime::benchmark;

#if defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
// A Bistro application, three frames in, and the variant submitted and applied;
// returns the application's own view of the lights after the edit.
struct VariantRun {
  std::size_t pointLightsBefore = 0;
  std::size_t pointLightsAfter = 0;
  float fogBefore = 0.0f;
  float bloomBefore = 0.0f;
  std::string problem;
  std::size_t commands = 0;
};

[[nodiscard]] VariantRun runBistroVariant(atlantis::runtime::cli::BenchmarkVariant variant) {
  namespace cli = atlantis::runtime::cli;
  BootstrapConfig config = buildSmokeConfig();
  config.sceneAsset = atlantis::asset_system::deriveAssetGuid(
      sceneGuidFromDefinition(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID), "scene");
  config.environmentArtifactPath.clear();
  config.environmentMetadataPath.clear();
  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());
  auto connection = app.openConnection();
  for (int i = 0; i < 2; ++i) app.runFrame();

  VariantRun out;
  const auto before = app.captureFrameData();
  REQUIRE(before.isOk());
  out.pointLightsBefore = before.value().pointLights.size();
  const auto schema = connection->schema();
  const auto fog = atlantis::connection::text::parsePath(schema, "Camera.fog.density");
  const auto bloom = atlantis::connection::text::parsePath(schema, "Camera.bloom.strength");
  REQUIRE(fog.isOk());
  REQUIRE(bloom.isOk());
  for (const auto& entity : connection->listEntities()) {
    const auto components = connection->listComponents(entity);
    if (!components.isOk() ||
        std::find(components.value().begin(), components.value().end(), fog.value().component) ==
            components.value().end()) {
      continue;
    }
    const auto f = connection->getProperty({entity, fog.value().component, fog.value().leaf->id});
    const auto b = connection->getProperty({entity, bloom.value().component, bloom.value().leaf->id});
    if (f.isOk()) out.fogBefore = std::get<float>(f.value());
    if (b.isOk()) out.bloomBefore = std::get<float>(b.value());
  }

  const auto edit = bench::submitVariant(*connection, variant);
  REQUIRE(edit.isOk());
  out.commands = edit.value().commands;
  app.runFrame();  // the edit applies at this frame's start
  out.problem = bench::verifyVariant(*connection, variant);
  const auto after = app.captureFrameData();
  REQUIRE(after.isOk());
  out.pointLightsAfter = after.value().pointLights.size();
  connection.reset();
  REQUIRE(app.shutdown() == RuntimeExitReason::Success);
  return out;
}
#endif

}  // namespace

TEST_CASE("Benchmark variant no-point-lights removes every point light of Bistro", "[runtime][gpu][benchmark][bistro]") {
#if !defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
  SKIP("Bistro content is not present (the content-gated build step was not declared)");
#else
  const VariantRun run = runBistroVariant(atlantis::runtime::cli::BenchmarkVariant::NoPointLights);
  CHECK(run.pointLightsBefore == 59);
  CHECK(run.commands == 59);
  CHECK(run.problem.empty());
  CHECK(run.pointLightsAfter == 0);  // the lighting block really has none, not zero-intensity ones
#endif
}

TEST_CASE("Benchmark variant no-fog zeroes the camera's fog density on Bistro", "[runtime][gpu][benchmark][bistro]") {
#if !defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
  SKIP("Bistro content is not present (the content-gated build step was not declared)");
#else
  const VariantRun run = runBistroVariant(atlantis::runtime::cli::BenchmarkVariant::NoFog);
  CHECK(run.fogBefore > 0.0f);
  CHECK(run.commands == 1);
  CHECK(run.problem.empty());
  CHECK(run.pointLightsAfter == run.pointLightsBefore);  // nothing else changed
#endif
}

TEST_CASE("Benchmark variant no-bloom zeroes the camera's bloom strength on Bistro",
          "[runtime][gpu][benchmark][bistro]") {
#if !defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
  SKIP("Bistro content is not present (the content-gated build step was not declared)");
#else
  const VariantRun run = runBistroVariant(atlantis::runtime::cli::BenchmarkVariant::NoBloom);
  CHECK(run.bloomBefore > 0.0f);
  CHECK(run.commands == 1);
  CHECK(run.problem.empty());
#endif
}

TEST_CASE("Benchmark: a whole run on the default scene writes a valid document, or says the build cannot",
          "[runtime][gpu][benchmark]") {
  namespace cli = atlantis::runtime::cli;
  const std::filesystem::path output = std::filesystem::temp_directory_path() / "atlantis_benchmark_smoke.json";
  std::filesystem::remove(output);

  cli::BenchmarkRequest request;
  request.sceneName = "integrated_showcase_demo";
  request.warmup = 3;
  request.frames = 12;
  request.width = 1280;
  request.height = 720;
  request.series = true;
  request.outputPath = output.string();
  const int code = bench::runBenchmark(request, buildSmokeConfig());

#if defined(ATLANTIS_PROFILING)
  REQUIRE(code == bench::kExitOk);
  std::ifstream in(output, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  in.close();  // the file must be closed before it is removed below
  const auto parsed = atlantis::connection::json::parse(text);
  REQUIRE(parsed.isOk());
  const auto& doc = parsed.value();
  CHECK(doc.find("schema")->asString() == "atlantis.benchmark/1");
  CHECK(std::stoi(doc.find("frames")->find("measured")->numberText()) == 12);
  CHECK(std::stoi(doc.find("frames")->find("discarded")->numberText()) >= 3);
  // The window really was the requested size.
  CHECK(std::stoi(doc.find("settings")->find("resolution")->find("width")->numberText()) == 1280);
  CHECK(std::stoi(doc.find("settings")->find("resolution")->find("height")->numberText()) == 720);
  CHECK(doc.find("settings")->find("variant")->asString() == "baseline");
  const auto* metrics = doc.find("metrics");
  REQUIRE(metrics != nullptr);
  for (const char* key : {"frame_ms", "cpu_ms", "cpu_active_ms", "draw_calls", "triangles", "descriptor_writes"}) {
    INFO(key);
    REQUIRE(metrics->find(key) != nullptr);
    CHECK(metrics->find(key)->find("p50") != nullptr);  // available, with statistics
  }
  CHECK(std::stod(metrics->find("draw_calls")->find("p50")->numberText()) >= 12.0);
  if (metrics->find("gpu_busy_ms")->find("p50") != nullptr) {
    CHECK(std::stod(metrics->find("passes")->find("draw")->find("gpu_ms")->find("p50")->numberText()) > 0.0);
  }
  CHECK(doc.find("series")->find("frame_ms")->asArray().size() == 12);
  CHECK(doc.find("meta")->find("build")->find("profiling")->asBool());
  std::filesystem::remove(output);
#else
  CHECK(code == bench::kExitEnvironment);  // a build without profiling refuses, with the documented code
  CHECK_FALSE(std::filesystem::exists(output));
#endif
}

// Plan 0059 M9 (ADR-0117 D1, D5): the real executable. stdout is exactly one JSON
// document -- logs go to stderr -- and the exit codes are the documented ones.
#if defined(_WIN32) && defined(ATLANTIS_RUNTIME_EXECUTABLE)
TEST_CASE("atlantis_runtime --benchmark: stdout is exactly one JSON document, and the exit codes are as documented",
          "[runtime][gpu][benchmark][process]") {
  namespace fs = std::filesystem;
  const fs::path dir = fs::temp_directory_path() / "atlantis_benchmark_process_test";
  fs::create_directories(dir);
  // cmd.exe strips one outer pair of quotes, so the whole command is quoted once more.
  const auto run = [&](const std::string& args, const fs::path& stdoutFile) {
    const std::string command = "\"\"" + std::string(ATLANTIS_RUNTIME_EXECUTABLE) + "\" " + args + " > \"" +
                                stdoutFile.string() + "\" 2> NUL\"";
    return std::system(command.c_str());
  };
  const auto read = [](const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  };

  const fs::path ok = dir / "ok.json";
  const int code = run("--benchmark integrated_showcase_demo --warmup 3 --frames 10 --resolution 1280x720 --json", ok);
#if defined(ATLANTIS_PROFILING)
  CHECK(code == 0);
  const auto parsed = atlantis::connection::json::parse(read(ok));  // a log line on stdout would fail this
  CHECK(parsed.isOk());
  if (parsed.isOk()) CHECK(parsed.value().find("schema")->asString() == "atlantis.benchmark/1");
#else
  CHECK(code == 3);
  CHECK(read(ok).empty());
#endif

  const fs::path absent = dir / "absent.txt";
  CHECK(run("--benchmark no_such_scene", absent) == 3);
  CHECK(read(absent).empty());

  const fs::path usage = dir / "usage.txt";
  CHECK(run("--benchmark integrated_showcase_demo --frames 0", usage) == 1);
  CHECK(run("--benchmark integrated_showcase_demo --listen 0", usage) == 1);
  fs::remove_all(dir);
}
#endif
