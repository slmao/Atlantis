#pragma once

#include <string>

namespace atlantis::runtime {

// See Plan 0013 Section D4. A plain, caller-populated value struct --
// not a service, not a builder. Every path is supplied by the caller
// (atlantis_runtime's main.cpp, or tests/runtime/'s own GPU smoke test),
// sourced from CMake-injected compile definitions -- no path is ever
// hardcoded inside src/runtime/'s own library sources. This is the
// whole of Runtime's own configuration surface: no command-line
// parsing, no config file, no environment variable is read by
// Atlantis::RuntimeHost.
struct BootstrapConfig {
  std::string applicationName = "Atlantis Runtime";
  std::string vertexShaderSpirvPath;
  std::string vertexShaderReflectionPath;
  std::string fragmentShaderSpirvPath;
  std::string fragmentShaderReflectionPath;
  std::string assetArtifactPath;
  std::string assetMetadataPath;
  // Plan 0015 Section D2/D11: the scene asset atlantis_add_scene_asset()
  // (assets/CMakeLists.txt) declares -- sourced from new CMake compile
  // definitions, matching assetArtifactPath/assetMetadataPath's own
  // established sourcing exactly.
  std::string sceneArtifactPath;
  std::string sceneMetadataPath;
  std::string sceneDependencyManifestPath;
  // Plan 0018 Section P10: the second, MaterialKind::UnlitTextured
  // built-in shader pair -- mirrors vertexShaderSpirvPath/
  // vertexShaderReflectionPath/fragmentShaderSpirvPath/
  // fragmentShaderReflectionPath's own sourcing exactly (a new CMake
  // compile definition plus the compiled shader's own literal
  // filenames, main.cpp's job, never hardcoded here).
  std::string unlitTexturedVertexShaderSpirvPath;
  std::string unlitTexturedVertexShaderReflectionPath;
  std::string unlitTexturedFragmentShaderSpirvPath;
  std::string unlitTexturedFragmentShaderReflectionPath;
  // Plan 0019 Section P6/P11: the third, MaterialKind::LitTextured
  // built-in shader pair -- mirrors unlitTexturedVertexShaderSpirvPath/
  // .../unlitTexturedFragmentShaderReflectionPath's own sourcing exactly.
  std::string litTexturedVertexShaderSpirvPath;
  std::string litTexturedVertexShaderReflectionPath;
  std::string litTexturedFragmentShaderSpirvPath;
  std::string litTexturedFragmentShaderReflectionPath;
  // Plan 0023 Milestone 5: the fourth, MaterialKind::PbrDirectLit
  // built-in shader pair -- mirrors litTexturedVertexShaderSpirvPath/
  // .../litTexturedFragmentShaderReflectionPath's own sourcing exactly.
  std::string pbrDirectLitVertexShaderSpirvPath;
  std::string pbrDirectLitVertexShaderReflectionPath;
  std::string pbrDirectLitFragmentShaderSpirvPath;
  std::string pbrDirectLitFragmentShaderReflectionPath;
  bool enableValidationLayers = true;
};

}  // namespace atlantis::runtime
