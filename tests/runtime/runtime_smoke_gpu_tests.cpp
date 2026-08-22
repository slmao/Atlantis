#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/runtime_application.h>

#include <cstddef>
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
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;

// Plan 0014 Section D-Step 6: the one narrowly-scoped friend
// RuntimeApplication declares for this test only (see
// runtime_application.h's own comment) -- reads world_'s own
// renderableEntities() count, no new public API.
namespace atlantis::runtime {
struct RuntimeSmokeTestAccess {
  static std::size_t renderableEntityCount(const RuntimeApplication& app) {
    return app.world_.renderableEntities().size();
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

  // V17: exactly 5 DrawItems reach Renderer::drawFrame() on a successful
  // frame -- every validation-scene Renderable entity resolved. Vulkan
  // Validation Layers reporting zero warnings/errors for the full
  // multi-item span is this test's own existing crash-on-validation-hit
  // mechanism (enableValidationLayers = true above), unchanged: reaching
  // this REQUIRE at all already proves no validation hit aborted the
  // process.
  REQUIRE(atlantis::runtime::RuntimeSmokeTestAccess::renderableEntityCount(app) == 5);

  const RuntimeExitReason reason = app.shutdown();
  REQUIRE(reason == RuntimeExitReason::Success);
}
