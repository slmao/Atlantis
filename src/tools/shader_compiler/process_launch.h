#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <atlantis/result.h>

namespace atlantis::tools::shader_compiler {

// A single combined capture -- see process_launch.cpp's "Diagnostic
// capture model" comment for why this is one string, not separate
// stdout/stderr fields.
struct ProcessOutput {
  std::string diagnostics;
  std::int32_t exitCode = 0;
};

enum class ProcessLaunchError {
  ExecutableNotFound,          // executablePath did not exist at launch time
                                // (checked before ever calling CreateProcessW)
  DiagnosticFileCreationFailed,
  LaunchFailed,                 // CreateProcessW itself returned FALSE;
                                 // GetLastError() text folded into this
                                 // error's own diagnostic, not a separate variant
  WaitFailed,                   // WaitForSingleObject() returned WAIT_FAILED
  ExitCodeQueryFailed,          // GetExitCodeProcess() itself failed
  DiagnosticFileReadFailed,
};

// Windows-only (WIN32_LEAN_AND_MEAN, CreateProcessW). Tools-internal
// implementation detail -- never an Atlantis::Platform API, never a
// ShaderSystem type. Blocking; runs the child process to completion.
// Not thread-safe; caller-thread-only, matching this whole executable's
// single-threaded, single-invocation model. See process_launch.cpp's
// "Timeout/cancellation" comment for this function's one explicitly
// accepted limitation.
[[nodiscard]] atlantis::Result<ProcessOutput, ProcessLaunchError> launchProcess(
    const std::filesystem::path& executablePath, const std::vector<std::string>& arguments);

}  // namespace atlantis::tools::shader_compiler
