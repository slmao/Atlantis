// atlantis_runtime_android's own android_main entry point -- the
// Android peer of atlantis_runtime's main.cpp (ADR-0077/ADR-0080).
// Performs asset extraction, injects this process's android_app* into
// Android Platform, builds the same BootstrapConfig shape main.cpp
// already builds (Android-specific path values only), and drives the
// same shouldContinue()/runFrame()/shutdown() loop, adapted to
// android_native_app_glue's own non-blocking, event-driven pacing.
//
// Reached via Atlantis::PlatformAndroidEntry (src/platform/CMakeLists.txt)
// for android_app_injection.h -- see that header's own top comment for
// why this cross-module include needs a dedicated CMake extension
// point rather than an ordinary target_include_directories(PUBLIC ...).
#include <atlantis/log.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/runtime_application.h>

#include "android_log_sink.h"
#include "asset_extraction.h"
#include <android_app_injection.h>

#include <android_native_app_glue.h>

#include <chrono>
#include <memory>
#include <string>
#include <thread>

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::createRuntimeApplication;
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;
using atlantis::runtime::android_detail::AndroidLogSink;
using atlantis::runtime::android_detail::extractAsset;
using atlantis::runtime::android_detail::extractSceneManifest;

namespace {

// The one, fixed sample scene this Milestone targets -- no
// scene-selection UI or command-line equivalent on Android (Plan 0034
// Milestone 5), matching atlantis_runtime's own default whitelist entry
// (main.cpp, Spec 0032 Requirement 2's own first/default row).
constexpr const char* kSceneRelativeArtifactPath = "assets/scenes/integrated_showcase_demo.ascene";
constexpr const char* kSceneRelativeMetadataPath = "assets/scenes/integrated_showcase_demo.ascene.meta.txt";
constexpr const char* kSceneRelativeManifestPath = "assets/scenes/integrated_showcase_demo.ascene.manifest.txt";

}  // namespace

void android_main(struct android_app* app) {
  // The very first statement, before any other log call: Core's default
  // ConsoleLogSink is invisible on Android (see android_log_sink.h's own
  // comment) -- every ATLANTIS_LOG_* call below this line, in this file
  // and in every module createRuntimeApplication() reaches, must go
  // through the Android-visible sink instead.
  atlantis::log::initialize(std::make_shared<AndroidLogSink>());
  atlantis::log::setMinLevel(atlantis::LogLevel::Info);
  ATLANTIS_LOG_INFO("Atlantis Runtime (Android) starting");

  AAssetManager* const assetManager = app->activity->assetManager;
  const std::string internalDataPath = app->activity->internalDataPath;

  // Every packaged relative path (relative to the assets root Gradle's
  // packaging task copies into, android/app/build.gradle) BootstrapConfig
  // needs by name -- mirrors main.cpp's own CMake-macro-sourced path
  // construction field-for-field, substituting fixed relative-path
  // string literals for CMake compile definitions, since Android's own
  // CMake configure never runs the shader/asset-cooking subtree that
  // defines those macros (Plan 0034 Milestone 1's own host/target
  // build-graph exclusion). Each literal below was verified, at
  // Plan-implementation time, to exist under the Windows build tree
  // this Spec's own packaging task reads from.
  BootstrapConfig config;
  config.applicationName = "Atlantis Runtime";
  config.vertexShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/minimal_renderer/minimal_mesh.vert.spv");
  config.vertexShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/minimal_renderer/minimal_mesh.vert.refl.json");
  config.fragmentShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/minimal_renderer/minimal_mesh.frag.spv");
  config.fragmentShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/minimal_renderer/minimal_mesh.frag.refl.json");
  config.assetArtifactPath = extractAsset(assetManager, internalDataPath, "assets/meshes/minimal_cube.amesh");
  config.assetMetadataPath =
      extractAsset(assetManager, internalDataPath, "assets/meshes/minimal_cube.amesh.meta.txt");
  config.sceneArtifactPath = extractAsset(assetManager, internalDataPath, kSceneRelativeArtifactPath);
  config.sceneMetadataPath = extractAsset(assetManager, internalDataPath, kSceneRelativeMetadataPath);
  // extractSceneManifest(), not extractAsset(): this one file's
  // artifact/metadata columns need runtime rewriting, not a verbatim
  // byte copy -- see asset_extraction.h's own contract.
  config.sceneDependencyManifestPath = extractSceneManifest(assetManager, internalDataPath, kSceneRelativeManifestPath);
  config.unlitTexturedVertexShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/textured_quad/textured_quad.vert.spv");
  config.unlitTexturedVertexShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/textured_quad/textured_quad.vert.refl.json");
  config.unlitTexturedFragmentShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/textured_quad/textured_quad.frag.spv");
  config.unlitTexturedFragmentShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/textured_quad/textured_quad.frag.refl.json");
  config.litTexturedVertexShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/lit_textured/lit_textured.vert.spv");
  config.litTexturedVertexShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/lit_textured/lit_textured.vert.refl.json");
  config.litTexturedFragmentShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/lit_textured/lit_textured.frag.spv");
  config.litTexturedFragmentShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/lit_textured/lit_textured.frag.refl.json");
  config.pbrDirectLitVertexShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_direct_lit/pbr_direct_lit.vert.spv");
  config.pbrDirectLitVertexShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_direct_lit/pbr_direct_lit.vert.refl.json");
  config.pbrDirectLitFragmentShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_direct_lit/pbr_direct_lit.frag.spv");
  config.pbrDirectLitFragmentShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_direct_lit/pbr_direct_lit.frag.refl.json");
  config.pbrDirectLitNormalMapVertexShaderSpirvPath = extractAsset(
      assetManager, internalDataPath, "shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.vert.spv");
  config.pbrDirectLitNormalMapVertexShaderReflectionPath = extractAsset(
      assetManager, internalDataPath, "shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.vert.refl.json");
  config.pbrDirectLitNormalMapFragmentShaderSpirvPath = extractAsset(
      assetManager, internalDataPath, "shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.frag.spv");
  config.pbrDirectLitNormalMapFragmentShaderReflectionPath = extractAsset(
      assetManager, internalDataPath, "shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.frag.refl.json");
  config.environmentArtifactPath = extractAsset(assetManager, internalDataPath, "assets/ibl_studio.aenv");
  config.environmentMetadataPath = extractAsset(assetManager, internalDataPath, "assets/ibl_studio.aenv.meta.txt");
  config.pbrIblVertexShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_ibl/pbr_ibl.vert.spv");
  config.pbrIblVertexShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_ibl/pbr_ibl.vert.refl.json");
  config.pbrIblFragmentShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_ibl/pbr_ibl.frag.spv");
  config.pbrIblFragmentShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_ibl/pbr_ibl.frag.refl.json");
  config.pbrIblNormalMapVertexShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.vert.spv");
  config.pbrIblNormalMapVertexShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.vert.refl.json");
  config.pbrIblNormalMapFragmentShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.frag.spv");
  config.pbrIblNormalMapFragmentShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.frag.refl.json");
  config.skyVertexShaderSpirvPath = extractAsset(assetManager, internalDataPath, "shaders/sky/sky.vert.spv");
  config.skyVertexShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/sky/sky.vert.refl.json");
  config.skyFragmentShaderSpirvPath = extractAsset(assetManager, internalDataPath, "shaders/sky/sky.frag.spv");
  config.skyFragmentShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/sky/sky.frag.refl.json");
  config.shadowCastVertexShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/shadow_cast/shadow_cast.vert.spv");
  config.shadowCastVertexShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/shadow_cast/shadow_cast.vert.refl.json");
  config.shadowCastFragmentShaderSpirvPath =
      extractAsset(assetManager, internalDataPath, "shaders/shadow_cast/shadow_cast.frag.spv");
  config.shadowCastFragmentShaderReflectionPath =
      extractAsset(assetManager, internalDataPath, "shaders/shadow_cast/shadow_cast.frag.refl.json");
  config.outputTransformUnormVertexShaderSpirvPath = extractAsset(
      assetManager, internalDataPath, "shaders/output_transform_unorm/output_transform_unorm.vert.spv");
  config.outputTransformUnormVertexShaderReflectionPath = extractAsset(
      assetManager, internalDataPath, "shaders/output_transform_unorm/output_transform_unorm.vert.refl.json");
  config.outputTransformUnormFragmentShaderSpirvPath = extractAsset(
      assetManager, internalDataPath, "shaders/output_transform_unorm/output_transform_unorm.frag.spv");
  config.outputTransformUnormFragmentShaderReflectionPath = extractAsset(
      assetManager, internalDataPath, "shaders/output_transform_unorm/output_transform_unorm.frag.refl.json");
  config.outputTransformSrgbVertexShaderSpirvPath = extractAsset(
      assetManager, internalDataPath, "shaders/output_transform_srgb/output_transform_srgb.vert.spv");
  config.outputTransformSrgbVertexShaderReflectionPath = extractAsset(
      assetManager, internalDataPath, "shaders/output_transform_srgb/output_transform_srgb.vert.refl.json");
  config.outputTransformSrgbFragmentShaderSpirvPath = extractAsset(
      assetManager, internalDataPath, "shaders/output_transform_srgb/output_transform_srgb.frag.spv");
  config.outputTransformSrgbFragmentShaderReflectionPath = extractAsset(
      assetManager, internalDataPath, "shaders/output_transform_srgb/output_transform_srgb.frag.refl.json");
  config.enableValidationLayers = true;

  // ADR-0077's own amendment: the only point in this sequence with
  // direct access to the android_app* android_main itself received --
  // must run before createRuntimeApplication() below, since that call
  // transitively reaches createPlatformSession()'s own
  // atlantis::platform::initialize().
  atlantis::platform::android_detail::setAndroidApp(app);

  auto appResult = createRuntimeApplication(config);
  if (appResult.isErr()) {
    ATLANTIS_LOG_ERROR("createRuntimeApplication() failed: {}", atlantis::runtime::toString(appResult.error()));
    return;
  }
  RuntimeApplication runtimeApp = std::move(appResult.value());
  ATLANTIS_LOG_INFO("Runtime initialized");

  // The same three-call shouldContinue()/runFrame()/shutdown() shape
  // main.cpp already uses (ADR-0080), adapted to Android's
  // non-blocking, event-driven pacing: runFrame() itself calls
  // Android Platform's processEvents() (a non-blocking
  // ALooper_pollOnce() drain, Plan 0034 Milestone 2), and once a real
  // window/Presentation exists, Presentation::present()'s own
  // FIFO-present-mode vsync block paces the loop exactly the way it
  // already does on Windows -- no new, Android-specific frame-pacing
  // mechanism is introduced. The one gap that scheme doesn't cover is
  // the brief window before the very first APP_CMD_INIT_WINDOW, where
  // no Presentation exists yet and runFrame() has nothing to block on;
  // a short sleep there only avoids busy-spinning while waiting for the
  // framework to hand over a window, never becoming part of any actual
  // frame's timing once one exists.
  while (runtimeApp.shouldContinue() && app->destroyRequested == 0) {
    runtimeApp.runFrame();
    if (app->window == nullptr) {
      std::this_thread::sleep_for(std::chrono::milliseconds(16));
    }
  }

  const RuntimeExitReason reason = runtimeApp.shutdown();
  ATLANTIS_LOG_INFO("Atlantis Runtime (Android) finished, exit reason {}", static_cast<int>(reason));
}
