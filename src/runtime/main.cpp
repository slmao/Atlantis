#include <atlantis/log.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/runtime_application.h>

#include <string>
#include <utility>

// Plan 0013 Section D9: the four ATLANTIS_RUNTIME_* macros are supplied
// by src/runtime/CMakeLists.txt via target_compile_definitions(),
// themselves reusing the already-exported, absolute, configuration-
// independent ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR/
// ATLANTIS_minimal_cube_{ARTIFACT_PATH,METADATA_PATH} CMake variables --
// never a working-directory-relative path.

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::createRuntimeApplication;
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;
using atlantis::runtime::toProcessExitCode;

int main() {
  atlantis::log::setMinLevel(atlantis::LogLevel::Info);
  ATLANTIS_LOG_INFO("Atlantis Runtime starting");

  BootstrapConfig config;
  config.applicationName = "Atlantis Runtime";
  config.vertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.spv";
  config.vertexShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.vert.refl.json";
  config.fragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.spv";
  config.fragmentShaderReflectionPath = std::string(ATLANTIS_RUNTIME_SHADER_DIR) + "/minimal_mesh.frag.refl.json";
  config.assetArtifactPath = ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH;
  config.assetMetadataPath = ATLANTIS_RUNTIME_ASSET_METADATA_PATH;
  config.sceneArtifactPath = ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH;
  config.sceneMetadataPath = ATLANTIS_RUNTIME_SCENE_METADATA_PATH;
  config.sceneDependencyManifestPath = ATLANTIS_RUNTIME_SCENE_MANIFEST_PATH;
  config.unlitTexturedVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.enableValidationLayers = true;

  auto appResult = createRuntimeApplication(config);
  if (appResult.isErr()) {
    ATLANTIS_LOG_ERROR("createRuntimeApplication() failed: {}", atlantis::runtime::toString(appResult.error()));
    return toProcessExitCode(RuntimeExitReason::InitializationFailed);
  }
  RuntimeApplication app = std::move(appResult.value());
  ATLANTIS_LOG_INFO("Runtime initialized");

  while (app.shouldContinue()) {
    app.runFrame();
  }

  const RuntimeExitReason reason = app.shutdown();
  ATLANTIS_LOG_INFO("Atlantis Runtime finished");
  return toProcessExitCode(reason);
}
