#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/runtime_application.h>
#include <atlantis/runtime/scene_extraction.h>
#include <atlantis/world/light.h>
#include <atlantis/world/transform.h>
#include <atlantis/world/world.h>

#include <array>
#include <cstddef>
#include <cstring>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

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
using atlantis::world::EntityId;
using atlantis::world::Light;
using atlantis::world::LightKind;
using atlantis::world::Transform;
using atlantis::world::World;

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
// more static method needs no header change at all). `world()` exposes
// the same `world_` member `renderableEntityCount()` already reads, by
// mutable reference, so a test can call World's own already-public
// createEntity()/setLight()/setLocalTransform()/setParent() directly
// against the real, running app's own live World -- never a second,
// test-private World instance, and never a duplicated scene-load or
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
static_assert(kLightingByteOffset + sizeof(FrameLightingData) == 304,
              "must match cameraBuffer_'s own real, constructed size");

// Plan 0023 Milestone 2 (ADR-0062's own Accepted Amendment): independently
// pins the new tail region's own real byte offset -- derived here from
// first principles (the same 128-byte camera region plus
// FrameLightingData's own real sizeof), not copied from
// runtime_application.cpp's own `cameraData + 32 + 44` expression, so a
// real drift between the two fails to *compile* here.
constexpr std::size_t kCameraWorldPositionByteOffset = kLightingByteOffset + sizeof(FrameLightingData);
static_assert(kCameraWorldPositionByteOffset == 304);
static_assert(kCameraWorldPositionByteOffset + sizeof(CameraWorldPositionData) == 320,
              "must match cameraBuffer_'s own real, constructed size");

struct RuntimeSmokeTestAccess {
  static std::size_t renderableEntityCount(const RuntimeApplication& app) {
    return app.world_->renderableEntities().size();
  }

  [[nodiscard]] static World& world(RuntimeApplication& app) { return *app.world_; }

  [[nodiscard]] static FrameLightingData lightingPayloadBytes(const RuntimeApplication& app) {
    const auto* cameraBytes = static_cast<const std::byte*>(app.cameraBuffer_->mappedData());
    FrameLightingData lighting{};
    std::memcpy(&lighting, cameraBytes + kLightingByteOffset, sizeof(FrameLightingData));
    return lighting;
  }
};
}  // namespace atlantis::runtime

TEST_CASE("Runtime constructs a window and completes real windowed acquire/draw/submit/present frames",
          "[runtime][gpu]") {
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
  config.sceneArtifactPath = ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_RUNTIME_SCENE_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_RUNTIME_SCENE_MANIFEST_PATH;
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
  // population), regardless of this test's own no-environment
  // configuration above.
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
  config.enableValidationLayers = true;

  auto appResult = createRuntimeApplication(config);
  REQUIRE(appResult.isOk());
  RuntimeApplication app = std::move(appResult.value());

  // Matches this repository's own existing kCycleCount precedent
  // (frame_execution_demo, headless_rendering_demo,
  // image_regression_gpu_tests).
  constexpr int kSmokeTestFrameCount = 3;
  for (int i = 0; i < kSmokeTestFrameCount && app.shouldContinue(); ++i) {
    app.runFrame();
  }
  REQUIRE(app.shouldContinue());  // did not fail during those frames

  // V22: exactly 5 DrawItems reach Renderer::drawFrame() on a successful
  // frame -- world_scene.scene.txt declares 5 Renderable nodes (D11),
  // matching the former hardcoded validation scene's own count exactly.
  // Vulkan Validation Layers reporting zero warnings/errors for the full
  // multi-item span is this test's own existing crash-on-validation-hit
  // mechanism (enableValidationLayers = true above), unchanged: reaching
  // this REQUIRE at all already proves no validation hit aborted the
  // process.
  REQUIRE(atlantis::runtime::RuntimeSmokeTestAccess::renderableEntityCount(app) == 5);

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
  // It only calls World's own already-public
  // createEntity()/setLight()/setLocalTransform() against the real,
  // running app's own live World between ordinary app.runFrame() calls
  // -- the identical thing World::setLight() is for -- and only ever
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

  // The real, default world_scene.scene.txt declares zero light nodes
  // (confirmed: `grep -c light assets/scenes/world_scene.scene.txt`
  // returns no matches) -- this test does not assume a light is already
  // present after the 3 frames above, it adds one.
  const FrameLightingData beforeAnyLight = RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  REQUIRE(beforeAnyLight.directionalLightCount == 0);
  REQUIRE(beforeAnyLight.pointLightCount == 0);

  // World::createEntity()/setLight() against the real, running app's own
  // live World -- the exact same public World API World::setLight()'s
  // own existing GPU test coverage already exercises, called here
  // against Runtime's own real instance instead of a fixture's.
  World& world = RuntimeSmokeTestAccess::world(app);
  const EntityId newLight = world.createEntity();
  Light point;
  point.kind = LightKind::Point;
  point.color = {0.2f, 0.4f, 0.9f};
  point.intensity = 2.5f;
  point.range = 5.0f;
  REQUIRE(world.setLight(newLight, point).isOk());
  Transform lightTransform;
  lightTransform.localPosition = {1.0f, 1.0f, 1.0f};
  REQUIRE(world.setLocalTransform(newLight, lightTransform).isOk());

  // The next real windowed frame -- a real acquire/Step 0/
  // updateTransforms()/submit()/present() cycle, identical in shape to
  // the 3 frames above -- publishes the mutation above.
  app.runFrame();
  REQUIRE(app.shouldContinue());
  const FrameLightingData afterLightAdded = RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  CHECK(afterLightAdded.directionalLightCount == 0);
  CHECK(afterLightAdded.pointLightCount == 1);
  CHECK(afterLightAdded.pointLights[0].position[0] == 1.0f);
  CHECK(afterLightAdded.pointLights[0].position[1] == 1.0f);
  CHECK(afterLightAdded.pointLights[0].position[2] == 1.0f);
  CHECK(afterLightAdded.pointLights[0].color[0] == 0.2f);
  CHECK(afterLightAdded.pointLights[0].color[1] == 0.4f);
  CHECK(afterLightAdded.pointLights[0].color[2] == 0.9f);
  CHECK(afterLightAdded.pointLights[0].intensity == 2.5f);

  // setLocalTransform() alone, on the same entity -- proves
  // World::updateTransforms()'s own hierarchy/leaf recompute (already
  // unconditional, every frame, before Lighting extraction -- Plan
  // 0022's own Milestone 1) is what makes this observable, not a
  // special-cased "light just changed" path.
  Transform movedTransform;
  movedTransform.localPosition = {-2.0f, 3.0f, 0.5f};
  REQUIRE(world.setLocalTransform(newLight, movedTransform).isOk());

  app.runFrame();
  REQUIRE(app.shouldContinue());
  const FrameLightingData afterTransformMoved = RuntimeSmokeTestAccess::lightingPayloadBytes(app);
  CHECK(afterTransformMoved.pointLightCount == 1);
  CHECK(afterTransformMoved.pointLights[0].position[0] == -2.0f);
  CHECK(afterTransformMoved.pointLights[0].position[1] == 3.0f);
  CHECK(afterTransformMoved.pointLights[0].position[2] == 0.5f);

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
}
