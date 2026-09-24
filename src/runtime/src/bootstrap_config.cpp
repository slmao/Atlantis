#include <atlantis/runtime/bootstrap_config.h>

#include <cstddef>
#include <string>

namespace atlantis::runtime {

namespace {

[[nodiscard]] std::size_t bloomShaderPathsSet(const BootstrapConfig& config) {
  const std::string* const paths[] = {
      &config.bloomDownsampleVertexShaderSpirvPath,
      &config.bloomDownsampleVertexShaderReflectionPath,
      &config.bloomDownsampleFragmentShaderSpirvPath,
      &config.bloomDownsampleFragmentShaderReflectionPath,
      &config.bloomUpsampleVertexShaderSpirvPath,
      &config.bloomUpsampleVertexShaderReflectionPath,
      &config.bloomUpsampleFragmentShaderSpirvPath,
      &config.bloomUpsampleFragmentShaderReflectionPath,
      &config.bloomCompositeVertexShaderSpirvPath,
      &config.bloomCompositeVertexShaderReflectionPath,
      &config.bloomCompositeFragmentShaderSpirvPath,
      &config.bloomCompositeFragmentShaderReflectionPath};
  std::size_t set = 0;
  for (const std::string* path : paths) set += path->empty() ? 0 : 1;
  return set;
}

constexpr std::size_t kBloomShaderPathCount = 12;

}  // namespace

bool hasBloomShaderPaths(const BootstrapConfig& config) {
  return bloomShaderPathsSet(config) == kBloomShaderPathCount;
}

atlantis::Result<std::monostate, RuntimeInitError> validateBloomBootstrapConfig(const BootstrapConfig& config) {
  using ResultT = atlantis::Result<std::monostate, RuntimeInitError>;
  const std::size_t set = bloomShaderPathsSet(config);
  if (set != 0 && set != kBloomShaderPathCount) return ResultT::Err(RuntimeInitError::BloomConfigInvalid);
  return ResultT::Ok(std::monostate{});
}

atlantis::Result<std::monostate, RuntimeInitError> validateEnvironmentBootstrapConfig(
    const BootstrapConfig& config) {
  using ResultT = atlantis::Result<std::monostate, RuntimeInitError>;
  const bool hasEnvironment = !config.environmentArtifactPath.empty();
  if (hasEnvironment != !config.environmentMetadataPath.empty()) {
    return ResultT::Err(RuntimeInitError::EnvironmentConfigInvalid);
  }
  if (hasEnvironment &&
      (config.pbrIblVertexShaderSpirvPath.empty() || config.pbrIblVertexShaderReflectionPath.empty() ||
       config.pbrIblFragmentShaderSpirvPath.empty() || config.pbrIblFragmentShaderReflectionPath.empty())) {
    return ResultT::Err(RuntimeInitError::EnvironmentConfigInvalid);
  }
  // Plan 0026 Milestone 3 (ADR-0071): the sky shader pair is required in
  // exactly the same case, mirroring the pbrIbl check above.
  if (hasEnvironment &&
      (config.skyVertexShaderSpirvPath.empty() || config.skyVertexShaderReflectionPath.empty() ||
       config.skyFragmentShaderSpirvPath.empty() || config.skyFragmentShaderReflectionPath.empty())) {
    return ResultT::Err(RuntimeInitError::EnvironmentConfigInvalid);
  }
  // Plan 0029 Section P15 (BootstrapConfig shader-pair path count
  // correction): pbrIblNormalMap is required in exactly the same case,
  // mirroring the pbrIbl check above -- pbrDirectLitNormalMap is
  // unconditionally required like pbrDirectLit, which this function
  // never validates either (its own emptiness surfaces as
  // ShaderLoadFailed at load time, not here).
  if (hasEnvironment && (config.pbrIblNormalMapVertexShaderSpirvPath.empty() ||
                         config.pbrIblNormalMapVertexShaderReflectionPath.empty() ||
                         config.pbrIblNormalMapFragmentShaderSpirvPath.empty() ||
                         config.pbrIblNormalMapFragmentShaderReflectionPath.empty())) {
    return ResultT::Err(RuntimeInitError::EnvironmentConfigInvalid);
  }
  return ResultT::Ok(std::monostate{});
}

atlantis::Result<std::monostate, RuntimeInitError> validateShadowBootstrapConfig(const BootstrapConfig& config) {
  using ResultT = atlantis::Result<std::monostate, RuntimeInitError>;
  if (config.shadowCastVertexShaderSpirvPath.empty() || config.shadowCastVertexShaderReflectionPath.empty() ||
      config.shadowCastFragmentShaderSpirvPath.empty() || config.shadowCastFragmentShaderReflectionPath.empty()) {
    return ResultT::Err(RuntimeInitError::ShaderLoadFailed);
  }
  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::runtime
