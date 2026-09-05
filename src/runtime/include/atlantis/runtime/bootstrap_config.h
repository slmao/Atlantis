#pragma once

#include <atlantis/result.h>
#include <atlantis/runtime/init_error.h>

#include <string>
#include <variant>

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
  // Plan 0025/P3: optional environment selection. Artifact and metadata
  // paths are either both empty or both populated. The pbrIbl shader paths
  // are required only in the populated case.
  std::string environmentArtifactPath;
  std::string environmentMetadataPath;
  std::string pbrIblVertexShaderSpirvPath;
  std::string pbrIblVertexShaderReflectionPath;
  std::string pbrIblFragmentShaderSpirvPath;
  std::string pbrIblFragmentShaderReflectionPath;
  // Plan 0026 Milestone 3 (ADR-0071): the sky shader pair -- mirrors
  // pbrIblVertexShaderSpirvPath/.../pbrIblFragmentShaderReflectionPath's
  // own four-field shape and "required only when environmentArtifactPath
  // is non-empty" sourcing exactly.
  std::string skyVertexShaderSpirvPath;
  std::string skyVertexShaderReflectionPath;
  std::string skyFragmentShaderSpirvPath;
  std::string skyFragmentShaderReflectionPath;
  // Plan 0027 Milestone 8 (ADR-0072 D-1/P1): the shadow-casting shader
  // pair -- unconditionally required, unlike pbrIblVertexShaderSpirvPath/
  // skyVertexShaderSpirvPath above (shadow infrastructure has no
  // environment dependency, P1). Mirrors pbrDirectLitVertexShaderSpirvPath's
  // own four-field shape.
  std::string shadowCastVertexShaderSpirvPath;
  std::string shadowCastVertexShaderReflectionPath;
  std::string shadowCastFragmentShaderSpirvPath;
  std::string shadowCastFragmentShaderReflectionPath;
  // Plan 0024 Milestone 6 (ADR-0068 D-6): the two output-transform
  // shader pairs -- mirrors pbrDirectLitVertexShaderSpirvPath/
  // .../pbrDirectLitFragmentShaderReflectionPath's own sourcing exactly.
  std::string outputTransformUnormVertexShaderSpirvPath;
  std::string outputTransformUnormVertexShaderReflectionPath;
  std::string outputTransformUnormFragmentShaderSpirvPath;
  std::string outputTransformUnormFragmentShaderReflectionPath;
  std::string outputTransformSrgbVertexShaderSpirvPath;
  std::string outputTransformSrgbVertexShaderReflectionPath;
  std::string outputTransformSrgbFragmentShaderSpirvPath;
  std::string outputTransformSrgbFragmentShaderReflectionPath;
  bool enableValidationLayers = true;
};

// GPU/window-independent validation for the optional environment portion.
[[nodiscard]] atlantis::Result<std::monostate, RuntimeInitError> validateEnvironmentBootstrapConfig(
    const BootstrapConfig& config);

// Plan 0027 Milestone 8: GPU/window-independent validation for the
// unconditionally-required shadow-casting shader pair -- always checked,
// unlike validateEnvironmentBootstrapConfig() above (which only applies
// when an environment is configured). Reuses RuntimeInitError::ShaderLoadFailed,
// the same enumerator every other built-in shader pair's load failure
// already maps to (see init_error.h).
[[nodiscard]] atlantis::Result<std::monostate, RuntimeInitError> validateShadowBootstrapConfig(
    const BootstrapConfig& config);

}  // namespace atlantis::runtime
