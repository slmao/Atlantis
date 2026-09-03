#include <atlantis/runtime/bootstrap_config.h>

namespace atlantis::runtime {

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
  return ResultT::Ok(std::monostate{});
}

}  // namespace atlantis::runtime
