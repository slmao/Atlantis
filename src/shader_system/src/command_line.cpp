#include <atlantis/shader_system/command_line.h>

namespace atlantis::shader_system {

std::vector<std::string> buildSlangcArgv(const std::filesystem::path& slangcExecutablePath,
                                          const SlangCompileRequest& request) {
  std::vector<std::string> argv;
  argv.push_back(slangcExecutablePath.string());
  argv.push_back("-target");
  argv.push_back("spirv");
  // Option A (Spec 0008 Human Review Approval / ADR-0028): -profile,
  // never -capability, selects the emitted SPIR-V version. Mandatory,
  // never omitted -- slangc's own undecorated default emits SPIR-V 1.5.
  argv.push_back("-profile");
  argv.push_back("spirv_1_0");
  // Policy S (ADR-0028): suppress exactly E50011 ("SPIR-V version too
  // old"), the known, disclosed consequence of the Option A decision --
  // and no other warning.
  argv.push_back("-warnings-disable");
  argv.push_back("50011");
  argv.push_back("-stage");
  argv.push_back(request.stage == SlangShaderStageArg::Vertex ? "vertex" : "fragment");
  argv.push_back("-entry");
  argv.push_back(request.entryPointName);
  argv.push_back("-o");
  argv.push_back(request.spirvOutputPath.string());
  argv.push_back("-reflection-json");
  argv.push_back(request.reflectionJsonOutputPath.string());
  // -fvk-use-entrypoint-name is deliberately NEVER passed here (Section
  // 5 / ADR-0030's entry-point-naming policy) -- the default rename-to-
  // "main" behavior matches the Vulkan Backend's hard-coded pName="main".
  argv.push_back(request.sourcePath.string());
  return argv;
}

std::vector<std::string> buildSpirvValArgv(const std::filesystem::path& spirvValExecutablePath,
                                            const std::filesystem::path& spirvPath) {
  return {spirvValExecutablePath.string(), "--target-env", "vulkan1.0", spirvPath.string()};
}

}  // namespace atlantis::shader_system
