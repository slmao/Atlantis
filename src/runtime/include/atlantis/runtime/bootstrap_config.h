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
  // Plan 0029 Section P15 (Human Review Approved Plan Correction,
  // 2026-09-06, BootstrapConfig shader-pair path count): the two new
  // normal-map PBR shader pairs -- 4 fields each (8 total), mirroring
  // pbrDirectLitVertexShaderSpirvPath/pbrIblVertexShaderSpirvPath's own
  // four-field shape exactly, not the 3-field shape P15's own original
  // text mistakenly described. pbrDirectLitNormalMap* is unconditionally
  // required, like pbrDirectLit* above; pbrIblNormalMap* is required
  // only when an environment is configured, like pbrIbl* above.
  std::string pbrDirectLitNormalMapVertexShaderSpirvPath;
  std::string pbrDirectLitNormalMapVertexShaderReflectionPath;
  std::string pbrDirectLitNormalMapFragmentShaderSpirvPath;
  std::string pbrDirectLitNormalMapFragmentShaderReflectionPath;
  std::string pbrIblNormalMapVertexShaderSpirvPath;
  std::string pbrIblNormalMapVertexShaderReflectionPath;
  std::string pbrIblNormalMapFragmentShaderSpirvPath;
  std::string pbrIblNormalMapFragmentShaderReflectionPath;
  // Plan 0035 Milestone 2 (ADR-0081): PbrClearcoat's own two IBL-lit
  // shader pairs -- unlike every field group above, these are
  // genuinely OPTIONAL, even when environmentArtifactPath is populated:
  // MaterialKind::PbrClearcoat is new, real content using it does not
  // yet exist in every composition root (this Milestone's own scope is
  // the new per-BRDF golden scenes only, not every existing fixture),
  // and validateEnvironmentBootstrapConfig() below deliberately does
  // NOT require these fields the way it requires pbrIbl*/
  // pbrIblNormalMap* -- a composition root that never realizes a
  // PbrClearcoat material may leave all 8 of these fields empty; one
  // that does must populate them itself (initializeSteps() gates
  // loading on pbrClearcoatIblVertexShaderSpirvPath's own emptiness,
  // runtime_application.cpp).
  std::string pbrClearcoatIblVertexShaderSpirvPath;
  std::string pbrClearcoatIblVertexShaderReflectionPath;
  std::string pbrClearcoatIblFragmentShaderSpirvPath;
  std::string pbrClearcoatIblFragmentShaderReflectionPath;
  std::string pbrClearcoatIblNormalMapVertexShaderSpirvPath;
  std::string pbrClearcoatIblNormalMapVertexShaderReflectionPath;
  std::string pbrClearcoatIblNormalMapFragmentShaderSpirvPath;
  std::string pbrClearcoatIblNormalMapFragmentShaderReflectionPath;
  // Plan 0035 Milestone 3 (ADR-0081): PbrSheen's own two IBL-lit shader
  // pairs -- same "genuinely optional, gated on its own vertex path's
  // emptiness" shape as pbrClearcoatIbl*/pbrClearcoatIblNormalMap*
  // immediately above.
  std::string pbrSheenIblVertexShaderSpirvPath;
  std::string pbrSheenIblVertexShaderReflectionPath;
  std::string pbrSheenIblFragmentShaderSpirvPath;
  std::string pbrSheenIblFragmentShaderReflectionPath;
  std::string pbrSheenIblNormalMapVertexShaderSpirvPath;
  std::string pbrSheenIblNormalMapVertexShaderReflectionPath;
  std::string pbrSheenIblNormalMapFragmentShaderSpirvPath;
  std::string pbrSheenIblNormalMapFragmentShaderReflectionPath;
  // Plan 0035 Milestone 4 (ADR-0081): PbrAnisotropic's own two IBL-lit
  // shader pairs -- same "genuinely optional, gated on its own vertex
  // path's emptiness" shape as pbrClearcoatIbl*/pbrSheenIbl* immediately
  // above.
  std::string pbrAnisotropicIblVertexShaderSpirvPath;
  std::string pbrAnisotropicIblVertexShaderReflectionPath;
  std::string pbrAnisotropicIblFragmentShaderSpirvPath;
  std::string pbrAnisotropicIblFragmentShaderReflectionPath;
  std::string pbrAnisotropicIblNormalMapVertexShaderSpirvPath;
  std::string pbrAnisotropicIblNormalMapVertexShaderReflectionPath;
  std::string pbrAnisotropicIblNormalMapFragmentShaderSpirvPath;
  std::string pbrAnisotropicIblNormalMapFragmentShaderReflectionPath;
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
  // Plan 0044 P9 (ruling O2): the three bloom shader pairs -- optional as
  // a group, all twelve set or none (validateBloomBootstrapConfig()).
  // Required only when the loaded scene's active camera turns bloom on;
  // Runtime then reports BloomConfigInvalid instead of rendering without it.
  std::string bloomDownsampleVertexShaderSpirvPath;
  std::string bloomDownsampleVertexShaderReflectionPath;
  std::string bloomDownsampleFragmentShaderSpirvPath;
  std::string bloomDownsampleFragmentShaderReflectionPath;
  std::string bloomUpsampleVertexShaderSpirvPath;
  std::string bloomUpsampleVertexShaderReflectionPath;
  std::string bloomUpsampleFragmentShaderSpirvPath;
  std::string bloomUpsampleFragmentShaderReflectionPath;
  std::string bloomCompositeVertexShaderSpirvPath;
  std::string bloomCompositeVertexShaderReflectionPath;
  std::string bloomCompositeFragmentShaderSpirvPath;
  std::string bloomCompositeFragmentShaderReflectionPath;
  bool enableValidationLayers = true;
};

// GPU/window-independent validation for the optional environment portion.
// Plan 0044 P9: true when all twelve bloom shader paths are set.
[[nodiscard]] bool hasBloomShaderPaths(const BootstrapConfig& config);

// Plan 0044 P9 (ruling O2): Err(BloomConfigInvalid) when only some of the
// twelve bloom shader paths are set; Ok when all or none are. Whether a
// scene needs them is checked by RuntimeApplication once the scene loads.
[[nodiscard]] atlantis::Result<std::monostate, RuntimeInitError> validateBloomBootstrapConfig(
    const BootstrapConfig& config);

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
