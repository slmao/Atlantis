#include <atlantis/assert.h>
#include <atlantis/asset_system/asset_guid.h>
#include <atlantis/log.h>
#include <atlantis/runtime/bootstrap_config.h>
#include <atlantis/runtime/exit_reason.h>
#include <atlantis/runtime/init_error.h>
#include <atlantis/runtime/runtime_application.h>

#include "cli.h"

#include <atlantis/cli/script_runner.h>
#include <atlantis/connection/runtime_connection.h>
#include <atlantis/remote/remote_server.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <utility>

// Plan 0013 Section D9: the four ATLANTIS_RUNTIME_* macros are supplied
// by src/runtime/CMakeLists.txt via target_compile_definitions(),
// themselves reusing the already-exported, absolute, configuration-
// independent ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR/
// ATLANTIS_minimal_cube_{ARTIFACT_PATH,METADATA_PATH} CMake variables --
// never a working-directory-relative path.
// Plan 0047 P15: the scenes are selected by catalog GUID --
// ATLANTIS_RUNTIME_ASSET_CATALOG_PATH names the one assembled build
// catalog, and each whitelist scene has an ATLANTIS_RUNTIME_<SCENE>_GUID
// generated from assets/asset_catalog.txt.

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::createRuntimeApplication;
using atlantis::runtime::RuntimeApplication;
using atlantis::runtime::RuntimeExitReason;
using atlantis::runtime::toProcessExitCode;
using atlantis::runtime::cli::CommandLineOutcome;
using atlantis::runtime::cli::CommandLineResult;
using atlantis::runtime::cli::parseCommandLine;
using atlantis::runtime::cli::SceneSelection;
using atlantis::runtime::cli::SceneWhitelistEntry;

namespace {

// The ATLANTIS_RUNTIME_*_GUID definitions are generated from the committed
// catalog source at configure time (Plan 0047 P15), so a malformed value is
// a build-system defect, not user input.
[[nodiscard]] atlantis::asset_system::AssetGuid guidFromDefinition(const char* text) {
  auto parsed = atlantis::asset_system::parseAssetGuid(text);
  ATLANTIS_CHECK_MSG(parsed.isOk(), "a generated ATLANTIS_RUNTIME_*_GUID compile definition is not a valid GUID");
  return parsed.value();
}

}  // namespace

int main(int argc, char** argv) {
  // Plan 0032 M2: fixed order matches Spec 0032 Requirement 2 exactly --
  // also fixes --list-scenes' own output order (cli.cpp prints
  // whitelist order verbatim). Real, absolute, CMake-injected paths
  // only -- cli.h's own parseCommandLine() never reads a macro itself
  // (Requirement 5). All parsing/printing/exit happens strictly before
  // any BootstrapConfig field is populated or createRuntimeApplication()
  // is called.
  // Plan 0035 Milestone 5 (Spec 0035 Requirement 7): the 4th, additive
  // whitelist entry -- purely appended, no change to the existing three
  // entries' own behavior (Spec 0032's own established "closed
  // whitelist, additive-only" design).
  // Plan 0046 Milestone 3 (Spec 0046 Q7, ruling O1): the 5th entry,
  // `bistro`, exists only when the Bistro build step was declared (the
  // content present at configure time), and renders without the global
  // environment -- a night street lit by its own lights, not a studio IBL.
#if defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
  constexpr std::size_t kWhitelistSize = 5;
#else
  constexpr std::size_t kWhitelistSize = 4;
#endif
  const std::array<SceneWhitelistEntry, kWhitelistSize> whitelist{{
      {"integrated_showcase_demo", SceneSelection{guidFromDefinition(ATLANTIS_RUNTIME_SCENE_GUID)}},
      {"ibl_material_demo", SceneSelection{guidFromDefinition(ATLANTIS_RUNTIME_IBL_MATERIAL_DEMO_SCENE_GUID)}},
      {"pbr_normal_map_demo", SceneSelection{guidFromDefinition(ATLANTIS_RUNTIME_PBR_NORMAL_MAP_DEMO_SCENE_GUID)}},
      {"pbr_materials_showcase",
       SceneSelection{guidFromDefinition(ATLANTIS_RUNTIME_PBR_MATERIALS_SHOWCASE_SCENE_GUID)}},
#if defined(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID)
      // The import root's GUID is the catalog's; the scene derives from it
      // (ADR-0097 D4, Plan 0047 P15).
      {"bistro",
       SceneSelection{atlantis::asset_system::deriveAssetGuid(guidFromDefinition(ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID),
                                                              "scene"),
                      /*disableEnvironmentLight=*/true}},
#endif
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

  // Spec 0054 ruling Q3 (Plan 0054 P7): an --exec script is read whole now,
  // before the window exists, so nothing reads input once frames run.
  std::optional<std::string> execScript;
  if (cliResult.execScript.has_value()) {
    const std::string& source = *cliResult.execScript;
    if (source == "-") {
      execScript.emplace(std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>());
    } else {
      std::ifstream in(source, std::ios::binary);
      if (!in) {
        std::cerr << "atlantis_runtime: cannot read --exec script: " << source << "\n";
        return toProcessExitCode(RuntimeExitReason::InitializationFailed);
      }
      execScript.emplace(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
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
  // Plan 0032 M2: the scene is the only field the CLI selection above
  // varies -- every other assignment in this function is unchanged.
  config.assetCatalogPath = ATLANTIS_RUNTIME_ASSET_CATALOG_PATH;
  config.sceneAsset = cliResult.selectedScene->sceneAsset;
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
  // Plan 0046 Milestone 3 (ruling O1): an entry flagged disableEnvironmentLight
  // leaves the environment unconfigured -- BootstrapConfig's own "no
  // environment" case (no IBL shaders selected, no sky).
  if (!cliResult.selectedScene->disableEnvironmentLight) {
    config.environmentArtifactPath = ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH;
    config.environmentMetadataPath = ATLANTIS_RUNTIME_ENVIRONMENT_METADATA_PATH;
  }
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
  // Plan 0035 Milestone 2 (ADR-0081): PbrClearcoat's own two IBL-lit
  // shader pairs -- populated unconditionally here (bootstrap_config.h's
  // own comment: genuinely optional, but this real product binary makes
  // the shader available regardless of whether any of its own three
  // whitelisted scenes currently uses a PbrClearcoat material).
  config.pbrClearcoatIblVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_CLEARCOAT_IBL_SHADER_DIR) + "/pbr_clearcoat_ibl.vert.spv";
  config.pbrClearcoatIblVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_CLEARCOAT_IBL_SHADER_DIR) + "/pbr_clearcoat_ibl.vert.refl.json";
  config.pbrClearcoatIblFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_CLEARCOAT_IBL_SHADER_DIR) + "/pbr_clearcoat_ibl.frag.spv";
  config.pbrClearcoatIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_CLEARCOAT_IBL_SHADER_DIR) + "/pbr_clearcoat_ibl.frag.refl.json";
  config.pbrClearcoatIblNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_CLEARCOAT_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_clearcoat_ibl_normal_map.vert.spv";
  config.pbrClearcoatIblNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_CLEARCOAT_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_clearcoat_ibl_normal_map.vert.refl.json";
  config.pbrClearcoatIblNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_CLEARCOAT_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_clearcoat_ibl_normal_map.frag.spv";
  config.pbrClearcoatIblNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_CLEARCOAT_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_clearcoat_ibl_normal_map.frag.refl.json";
  // Plan 0035 Milestone 3 (ADR-0081): PbrSheen's own two IBL-lit shader
  // pairs -- same unconditional-population shape as PbrClearcoat's own
  // pair immediately above.
  config.pbrSheenIblVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_SHEEN_IBL_SHADER_DIR) + "/pbr_sheen_ibl.vert.spv";
  config.pbrSheenIblVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_SHEEN_IBL_SHADER_DIR) + "/pbr_sheen_ibl.vert.refl.json";
  config.pbrSheenIblFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_SHEEN_IBL_SHADER_DIR) + "/pbr_sheen_ibl.frag.spv";
  config.pbrSheenIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_SHEEN_IBL_SHADER_DIR) + "/pbr_sheen_ibl.frag.refl.json";
  config.pbrSheenIblNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_SHEEN_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_sheen_ibl_normal_map.vert.spv";
  config.pbrSheenIblNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_SHEEN_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_sheen_ibl_normal_map.vert.refl.json";
  config.pbrSheenIblNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_SHEEN_IBL_NORMAL_MAP_SHADER_DIR) + "/pbr_sheen_ibl_normal_map.frag.spv";
  config.pbrSheenIblNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_SHEEN_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_sheen_ibl_normal_map.frag.refl.json";
  // Plan 0035 Milestone 4 (ADR-0081): PbrAnisotropic's own two IBL-lit
  // shader pairs -- same unconditional-population shape as
  // PbrClearcoat/PbrSheen's own pairs immediately above.
  config.pbrAnisotropicIblVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_ANISOTROPIC_IBL_SHADER_DIR) + "/pbr_anisotropic_ibl.vert.spv";
  config.pbrAnisotropicIblVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_ANISOTROPIC_IBL_SHADER_DIR) + "/pbr_anisotropic_ibl.vert.refl.json";
  config.pbrAnisotropicIblFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_ANISOTROPIC_IBL_SHADER_DIR) + "/pbr_anisotropic_ibl.frag.spv";
  config.pbrAnisotropicIblFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_ANISOTROPIC_IBL_SHADER_DIR) + "/pbr_anisotropic_ibl.frag.refl.json";
  config.pbrAnisotropicIblNormalMapVertexShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_ANISOTROPIC_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_anisotropic_ibl_normal_map.vert.spv";
  config.pbrAnisotropicIblNormalMapVertexShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_ANISOTROPIC_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_anisotropic_ibl_normal_map.vert.refl.json";
  config.pbrAnisotropicIblNormalMapFragmentShaderSpirvPath =
      std::string(ATLANTIS_RUNTIME_PBR_ANISOTROPIC_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_anisotropic_ibl_normal_map.frag.spv";
  config.pbrAnisotropicIblNormalMapFragmentShaderReflectionPath =
      std::string(ATLANTIS_RUNTIME_PBR_ANISOTROPIC_IBL_NORMAL_MAP_SHADER_DIR) +
      "/pbr_anisotropic_ibl_normal_map.frag.refl.json";
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
  // Plan 0044 P9: the three bloom shader pairs, set as a group.
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

  auto appResult = createRuntimeApplication(config);
  if (appResult.isErr()) {
    ATLANTIS_LOG_ERROR("createRuntimeApplication() failed: {}", atlantis::runtime::toString(appResult.error()));
    return toProcessExitCode(RuntimeExitReason::InitializationFailed);
  }
  RuntimeApplication app = std::move(appResult.value());
  ATLANTIS_LOG_INFO("Runtime initialized");

  // Spec 0054 R9 (ruling Q3, H-a; Plan 0054 P7): with --exec, a CLI client on
  // a connection to this application runs one script line after each frame,
  // on this thread -- runFrame() itself is unchanged. Declared after `app`
  // (connection, then runner) so both are destroyed before it.
  std::unique_ptr<atlantis::connection::RuntimeConnection> connection;
  std::optional<atlantis::cli::ScriptRunner> runner;
  if (execScript.has_value()) {
    connection = app.openConnection();
    runner.emplace(*connection, atlantis::cli::splitLines(*execScript), std::cout);
  }

  // Spec 0055 R1/R2 (Plan 0055 P3, P4, P11; ADR-0106 D1): with --listen, a
  // RemoteServer on 127.0.0.1 is polled once after each frame, on this thread;
  // each attached client gets its own connection to this application. The
  // session file tells clients the port and token. Without --listen no socket
  // exists. Declared after `app` so it is destroyed before it.
  std::unique_ptr<atlantis::remote::RemoteServer> server;
  std::filesystem::path sessionPath;
  if (cliResult.listenPort.has_value()) {
    auto listening = atlantis::remote::RemoteServer::listen(*cliResult.listenPort, atlantis::remote::generateToken(),
                                                            config.sceneAsset, [&app] { return app.openConnection(); });
    if (listening.isErr()) {
      ATLANTIS_LOG_ERROR("--listen {}: {}", *cliResult.listenPort, atlantis::remote::toString(listening.error()));
      (void)app.shutdown();
      return toProcessExitCode(RuntimeExitReason::InitializationFailed);
    }
    server = std::move(listening.value());
    sessionPath = cliResult.sessionFile.has_value() ? std::filesystem::path(*cliResult.sessionFile)
                                                    : atlantis::remote::defaultSessionPath();
    const atlantis::remote::SessionInfo session = server->session();
    if (atlantis::remote::writeSessionFile(sessionPath, session).isErr()) {
      ATLANTIS_LOG_ERROR("cannot write the session file {}", sessionPath.string());
      (void)app.shutdown();
      return toProcessExitCode(RuntimeExitReason::InitializationFailed);
    }
    ATLANTIS_LOG_INFO("Listening on 127.0.0.1:{} (session file {})", session.port, sessionPath.string());
  }

  while (app.shouldContinue()) {
    app.runFrame();
    if (server) server->poll();
    if (runner) runner->step();
  }

  if (server) {
    server.reset();  // closes every client connection before the application shuts down
    atlantis::remote::removeSessionFile(sessionPath);
  }
  const RuntimeExitReason reason = app.shutdown();
  ATLANTIS_LOG_INFO("Atlantis Runtime finished");
  return toProcessExitCode(reason);
}
