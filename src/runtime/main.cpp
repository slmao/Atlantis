#include <atlantis/log.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/runtime_application.h>

#include "cli.h"

#include <array>
#include <iostream>
#include <string>
#include <utility>

// Plan 0013 Section D9: the four ATLANTIS_RUNTIME_* macros are supplied
// by src/runtime/CMakeLists.txt via target_compile_definitions(),
// themselves reusing the already-exported, absolute, configuration-
// independent ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR/
// ATLANTIS_minimal_cube_{ARTIFACT_PATH,METADATA_PATH} CMake variables --
// never a working-directory-relative path.
// Plan 0032 M2: ATLANTIS_RUNTIME_SCENE_{ARTIFACT,METADATA,MANIFEST}_PATH
// (below) is left unrenamed -- it already is integrated_showcase_demo's
// own triple, the default whitelist entry. ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_*
// / ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_* are new.

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::createRuntimeApplication;
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;
using atlantis::runtime::toProcessExitCode;
using atlantis::runtime::cli::CommandLineOutcome;
using atlantis::runtime::cli::CommandLineResult;
using atlantis::runtime::cli::parseCommandLine;
using atlantis::runtime::cli::SceneBootstrapPaths;
using atlantis::runtime::cli::SceneWhitelistEntry;

int main(int argc, char** argv) {
  // Plan 0032 M2: fixed order matches Spec 0032 Requirement 2 exactly --
  // also fixes --list-scenes' own output order (cli.cpp prints
  // whitelist order verbatim). Real, absolute, CMake-injected paths
  // only -- cli.h's own parseCommandLine() never reads a macro itself
  // (Requirement 5). All parsing/printing/exit happens strictly before
  // any BootstrapConfig field is populated or createRuntimeApplication()
  // is called.
  const std::array<SceneWhitelistEntry, 3> whitelist{{
      {"integrated_showcase_demo",
       SceneBootstrapPaths{ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH, ATLANTIS_RUNTIME_SCENE_METADATA_PATH,
                            ATLANTIS_RUNTIME_SCENE_MANIFEST_PATH}},
      {"ibl_material_demo",
       SceneBootstrapPaths{ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_ARTIFACT_PATH,
                            ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_METADATA_PATH,
                            ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_MANIFEST_PATH}},
      {"pbr_normal_map_demo",
       SceneBootstrapPaths{ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_ARTIFACT_PATH,
                            ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_METADATA_PATH,
                            ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_MANIFEST_PATH}},
  }};

  const CommandLineResult cliResult = parseCommandLine(argc, argv, whitelist);
  if (cliResult.outcome == CommandLineOutcome::PrintUsageAndExit) {
    std::cout << cliResult.message;
    return toProcessExitCode(RuntimeExitReason::Success);
  }
  if (cliResult.outcome == CommandLineOutcome::PrintErrorAndExit) {
    std::cerr << cliResult.message;
    return toProcessExitCode(RuntimeExitReason::InitializationFailed);
  }

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
  // Plan 0032 M2: the only three fields the CLI selection above
  // varies -- every other assignment in this function is byte-for-byte
  // unchanged from before this Milestone.
  config.sceneArtifactPath = cliResult.selectedScene->sceneArtifactPath;
  config.sceneMetadataPath = cliResult.selectedScene->sceneMetadataPath;
  config.sceneDependencyManifestPath = cliResult.selectedScene->sceneDependencyManifestPath;
  config.unlitTexturedVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.spv";
  config.unlitTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.vert.refl.json";
  config.unlitTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.spv";
  config.unlitTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR) + "/textured_quad.frag.refl.json";
  config.litTexturedVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.spv";
  config.litTexturedVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.vert.refl.json";
  config.litTexturedFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.spv";
  config.litTexturedFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_LIT_TEXTURED_SHADER_DIR) + "/lit_textured.frag.refl.json";
  config.pbrDirectLitVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.spv";
  config.pbrDirectLitVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.vert.refl.json";
  config.pbrDirectLitFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.spv";
  config.pbrDirectLitFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_DIRECT_LIT_SHADER_DIR) + "/pbr_direct_lit.frag.refl.json";
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
  config.environmentArtifactPath = ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH;
  config.environmentMetadataPath = ATLANTIS_RUNTIME_ENVIRONMENT_METADATA_PATH;
  config.pbrIblVertexShaderSpirvPath = std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.spv";
  config.pbrIblVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.vert.refl.json";
  config.pbrIblFragmentShaderSpirvPath = std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.spv";
  config.pbrIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_IBL_SHADER_DIR) + "/pbr_ibl.frag.refl.json";
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
  config.shadowCastVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.spv";
  config.shadowCastVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.vert.refl.json";
  config.shadowCastFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.spv";
  config.shadowCastFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR) + "/shadow_cast.frag.refl.json";
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
