#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace atlantis::shader_system {

enum class SlangShaderStageArg { Vertex, Fragment };

// Pure data -- describes ONE slangc invocation compiling ONE entry point
// from ONE Slang source file. No process is spawned by this type or by
// buildSlangcArgv() below; that is process_launch.h's job (Atlantis
// Tools).
struct SlangCompileRequest {
  std::filesystem::path sourcePath;
  std::string entryPointName;  // e.g. "vertexMain"
  SlangShaderStageArg stage = SlangShaderStageArg::Vertex;
  std::filesystem::path spirvOutputPath;
  std::filesystem::path reflectionJsonOutputPath;  // Slang's own raw JSON, an intermediate file
};

// Returns the exact argv (argv[0] is the slangc executable path itself,
// matching how process_launch.h's launchProcess() consumes it) for the
// request above. Fixes, as tested, non-Plan-revisable facts (per Spec
// 0008 Approval / ADR-0028 Decision):
//  -profile spirv_1_0             (Option A, mandatory -- NOT -capability,
//                                   which experimentally does not select
//                                   the output version, ADR-0028)
//  -warnings-disable 50011        (Policy S, mandatory)
//  no -fvk-use-entrypoint-name    (never passed, ever)
//  -target spirv
//  -stage <vertex|fragment>
//  -entry <entryPointName>
//  -o <spirvOutputPath>
//  -reflection-json <reflectionJsonOutputPath>
[[nodiscard]] std::vector<std::string> buildSlangcArgv(const std::filesystem::path& slangcExecutablePath,
                                                         const SlangCompileRequest& request);

// argv for `spirv-val --target-env vulkan1.0 <spirvPath>` (ADR-0031,
// mandatory). No flag beyond --target-env is added by default.
[[nodiscard]] std::vector<std::string> buildSpirvValArgv(const std::filesystem::path& spirvValExecutablePath,
                                                           const std::filesystem::path& spirvPath);

}  // namespace atlantis::shader_system
