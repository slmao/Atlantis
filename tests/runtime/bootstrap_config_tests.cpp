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
  REQUIRE(validateEnvironmentBootstrapConfig(config).isOk());

  config.environmentArtifactPath.clear();
  REQUIRE(validateEnvironmentBootstrapConfig(config).isErr());
}

TEST_CASE("No-environment bootstrap ignores IBL shader paths", "[runtime][bootstrap][ibl]") {
  BootstrapConfig config;
  populateIblShaders(config);
  config.pbrIblVertexShaderSpirvPath = "not-a-real-file";
  REQUIRE(validateEnvironmentBootstrapConfig(config).isOk());
}
