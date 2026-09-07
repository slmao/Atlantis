#include <atlantis/runtime/bootstrap_config.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::runtime::BootstrapConfig;
using atlantis::runtime::RuntimeInitError;
using atlantis::runtime::validateEnvironmentBootstrapConfig;

namespace {

void populateIblShaders(BootstrapConfig& config) {
  config.pbrIblVertexShaderSpirvPath = "v.spv";
  config.pbrIblVertexShaderReflectionPath = "v.json";
  config.pbrIblFragmentShaderSpirvPath = "f.spv";
  config.pbrIblFragmentShaderReflectionPath = "f.json";
}

// Plan 0026 Milestone 3 (ADR-0071): the sky shader pair is required in
// exactly the same case as the IBL pair above -- a separate helper since
// they are conceptually distinct shader pairs, even though both gate on
// the same environmentArtifactPath condition.
void populateSkyShaders(BootstrapConfig& config) {
  config.skyVertexShaderSpirvPath = "sky_v.spv";
  config.skyVertexShaderReflectionPath = "sky_v.json";
  config.skyFragmentShaderSpirvPath = "sky_f.spv";
  config.skyFragmentShaderReflectionPath = "sky_f.json";
}

// Plan 0029 Section P15 (BootstrapConfig shader-pair path count
// correction): pbrIblNormalMap is required in exactly the same case as
// the IBL pair above -- mirrors populateIblShaders()'s own shape.
void populatePbrIblNormalMapShaders(BootstrapConfig& config) {
  config.pbrIblNormalMapVertexShaderSpirvPath = "ibl_nm_v.spv";
  config.pbrIblNormalMapVertexShaderReflectionPath = "ibl_nm_v.json";
  config.pbrIblNormalMapFragmentShaderSpirvPath = "ibl_nm_f.spv";
  config.pbrIblNormalMapFragmentShaderReflectionPath = "ibl_nm_f.json";
}

}  // namespace

TEST_CASE("Environment bootstrap paths are absent or complete", "[runtime][bootstrap][ibl]") {
  BootstrapConfig config;
  REQUIRE(validateEnvironmentBootstrapConfig(config).isOk());

  config.environmentArtifactPath = "studio.aenv";
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());
  CHECK(validateEnvironmentBootstrapConfig(config).error() == RuntimeInitError::EnvironmentConfigInvalid);

  config.environmentMetadataPath = "studio.meta";
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());
  populateIblShaders(config);
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());  // sky/normal-map paths still empty
  populateSkyShaders(config);
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());  // normal-map paths still empty
  populatePbrIblNormalMapShaders(config);
  REQUIRE(validateEnvironmentBootstrapConfig(config).isOk());

  config.environmentArtifactPath.clear();
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());
}

TEST_CASE("No-environment bootstrap ignores IBL and sky shader paths", "[runtime][bootstrap][ibl][sky]") {
  BootstrapConfig config;
  populateIblShaders(config);
  populateSkyShaders(config);
  populatePbrIblNormalMapShaders(config);
  config.pbrIblVertexShaderSpirvPath = "not-a-real-file";
  config.skyVertexShaderSpirvPath = "not-a-real-file";
  REQUIRE(validateEnvironmentBootstrapConfig(config).isOk());
}

TEST_CASE("Environment bootstrap requires the sky shader paths too, independent of the IBL pair",
          "[runtime][bootstrap][sky]") {
  BootstrapConfig config;
  config.environmentArtifactPath = "studio.aenv";
  config.environmentMetadataPath = "studio.meta";
  populateIblShaders(config);
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());
  populateSkyShaders(config);
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());  // normal-map paths still empty
  populatePbrIblNormalMapShaders(config);
  REQUIRE(validateEnvironmentBootstrapConfig(config).isOk());

  config.skyFragmentShaderReflectionPath.clear();
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());
}

TEST_CASE("Environment bootstrap requires the pbrIblNormalMap shader paths too, independent of the IBL/sky pairs",
          "[runtime][bootstrap][ibl][normal-map]") {
  BootstrapConfig config;
  config.environmentArtifactPath = "studio.aenv";
  config.environmentMetadataPath = "studio.meta";
  populateIblShaders(config);
  populateSkyShaders(config);
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());
  populatePbrIblNormalMapShaders(config);
  REQUIRE(validateEnvironmentBootstrapConfig(config).isOk());

  config.pbrIblNormalMapFragmentShaderReflectionPath.clear();
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());
}
