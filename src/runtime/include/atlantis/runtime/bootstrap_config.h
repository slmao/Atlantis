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
  bool enableValidationLayers = true;
};

}  // namespace atlantis::runtime
