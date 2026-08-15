// One of a small, explicitly-permitted set of files in this repository
// allowed to include <windows.h> directly (see
// src/platform/src/windows/windows_platform.cpp for the other) --
// gated behind this target's own Windows-only scope (Plan 0008 Section
// 4: atlantis_shader_compiler is a Windows-only Tools executable this
// round; SPIR-V compilation always happens on a host, never the
// Android target device).
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "process_launch.h"

#include <fstream>
#include <optional>
#include <sstream>

#include "command_line_quoting.h"

namespace atlantis::tools::shader_compiler {

namespace {

using LaunchResult = atlantis::Result<ProcessOutput, ProcessLaunchError>;

// RAII wrapper for a Win32 HANDLE -- closes on every exit path,
// including early-return error paths, per Plan 0008 Section 4's own
// explicit RAII requirement.
class Win32HandleGuard {
 public:
  Win32HandleGuard() = default;
  explicit Win32HandleGuard(HANDLE handle) : handle_(handle) {}
  ~Win32HandleGuard() { reset(); }

  Win32HandleGuard(const Win32HandleGuard&) = delete;
  Win32HandleGuard& operator=(const Win32HandleGuard&) = delete;
  Win32HandleGuard(Win32HandleGuard&& other) noexcept : handle_(other.release()) {}
  Win32HandleGuard& operator=(Win32HandleGuard&& other) noexcept {
    if (this != &other) {
      reset();
      handle_ = other.release();
    }
    return *this;
  }

  [[nodiscard]] HANDLE get() const { return handle_; }
  [[nodiscard]] bool valid() const { return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE; }

  HANDLE release() {
    HANDLE h = handle_;
    handle_ = nullptr;
    return h;
  }

  void reset() {
    if (valid()) CloseHandle(handle_);
    handle_ = nullptr;
  }

 private:
  HANDLE handle_ = nullptr;
};

// Deletes the temporary diagnostic file on every exit path (success or
// failure) -- matching process_launch's own "clean up on every exit
// path" discipline (Plan 0008 Section 4).
class TempFileGuard {
 public:
  explicit TempFileGuard(std::filesystem::path path) : path_(std::move(path)) {}
  ~TempFileGuard() {
    if (!path_.empty()) DeleteFileW(path_.c_str());
  }

  TempFileGuard(const TempFileGuard&) = delete;
  TempFileGuard& operator=(const TempFileGuard&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const { return path_; }

 private:
  std::filesystem::path path_;
};

// A single, per-call-unique temporary file for combined diagnostic
// capture -- independent of Plan 0008 Section 7's own per-shader-pair
// publish-transaction temp directory (a different layer/purpose: one
// launchProcess() call's own diagnostic file, vs. the whole shader-pair
// compile attempt's own artifact staging area).
[[nodiscard]] std::optional<std::filesystem::path> createUniqueTempFilePath() {
  wchar_t tempDir[MAX_PATH];
  const DWORD tempDirLength = GetTempPathW(MAX_PATH, tempDir);
  if (tempDirLength == 0 || tempDirLength > MAX_PATH) return std::nullopt;

  wchar_t tempFile[MAX_PATH];
  if (GetTempFileNameW(tempDir, L"asc", 0, tempFile) == 0) return std::nullopt;
  return std::filesystem::path(tempFile);
}

}  // namespace

atlantis::Result<ProcessOutput, ProcessLaunchError> launchProcess(const std::filesystem::path& executablePath,
                                                                    const std::vector<std::string>& arguments) {
  // Executable resolution -- lpApplicationName, never NULL (Plan 0008
  // Section 4's own Security Remarks citation). launchProcess() itself
  // performs no PATH search; executablePath must already be resolved.
  std::error_code existsError;
  if (!std::filesystem::exists(executablePath, existsError) || existsError) {
    return LaunchResult::Err(ProcessLaunchError::ExecutableNotFound);
  }

  const auto tempFilePath = createUniqueTempFilePath();
  if (!tempFilePath.has_value()) return LaunchResult::Err(ProcessLaunchError::DiagnosticFileCreationFailed);
  TempFileGuard tempFileGuard(*tempFilePath);

  // Diagnostic capture model -- a single temporary file, not pipes
  // (Plan 0008 Section 4): two anonymous pipes read sequentially carry
  // a well-known deadlock hazard (the child can block writing to a full
  // pipe buffer while the parent is still blocked reading the *other*
  // stream first); a single file has no such fixed buffer. Both
  // STARTUPINFOW::hStdOutput and hStdError are set to the SAME file
  // handle below, so the child's stdout/stderr writes interleave into
  // one file in their actual chronological order.
  SECURITY_ATTRIBUTES inheritableAttributes{};
  inheritableAttributes.nLength = sizeof(inheritableAttributes);
  inheritableAttributes.bInheritHandle = TRUE;
  inheritableAttributes.lpSecurityDescriptor = nullptr;

  Win32HandleGuard diagnosticHandle(CreateFileW(tempFilePath->c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                                                 &inheritableAttributes, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY,
                                                 nullptr));
  if (!diagnosticHandle.valid()) return LaunchResult::Err(ProcessLaunchError::DiagnosticFileCreationFailed);

  // STARTUPINFOW::dwFlags must explicitly include STARTF_USESTDHANDLES
  // -- per the official documentation, hStdOutput/hStdError/hStdInput
  // are otherwise silently ignored (the child would inherit the
  // parent's own console handles instead), which would defeat this
  // entire diagnostic-capture design. Once the flag is set, hStdInput
  // is no longer safely left unset -- slangc/spirv-val are one-shot CLI
  // invocations that never read standard input, so it is explicitly
  // redirected to the NUL device (immediate EOF on any read), never the
  // parent's own console input.
  Win32HandleGuard nulInputHandle(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                               &inheritableAttributes, OPEN_EXISTING, 0, nullptr));
  if (!nulInputHandle.valid()) return LaunchResult::Err(ProcessLaunchError::DiagnosticFileCreationFailed);

  STARTUPINFOW startupInfo{};
  startupInfo.cb = sizeof(startupInfo);
  startupInfo.dwFlags = STARTF_USESTDHANDLES;
  startupInfo.hStdOutput = diagnosticHandle.get();
  startupInfo.hStdError = diagnosticHandle.get();
  startupInfo.hStdInput = nulInputHandle.get();

  // Command-line construction -- a mutable, owned buffer, never a
  // string literal (Plan 0008 Section 4's own citation: CreateProcessW
  // may write into this buffer; a const/literal pointer risks an access
  // violation). commandLine's storage outlives the CreateProcessW call
  // below.
  std::wstring commandLine = detail::buildCommandLine(arguments);
  const std::wstring applicationName = executablePath.wstring();

  PROCESS_INFORMATION processInfo{};
  // Working directory and environment -- both inherited (Plan 0008
  // Section 4): lpCurrentDirectory/lpEnvironment both NULL.
  const BOOL created = CreateProcessW(applicationName.c_str(), commandLine.data(), nullptr, nullptr,
                                       /*bInheritHandles=*/TRUE, /*dwCreationFlags=*/0, nullptr, nullptr,
                                       &startupInfo, &processInfo);
  if (!created) return LaunchResult::Err(ProcessLaunchError::LaunchFailed);

  Win32HandleGuard processHandle(processInfo.hProcess);
  Win32HandleGuard threadHandle(processInfo.hThread);

  // The parent closes its own copies of the diagnostic/NUL handles
  // immediately once CreateProcessW has returned successfully -- the
  // child now holds its own inherited copies (Plan 0008 Section 4).
  diagnosticHandle.reset();
  nulInputHandle.reset();

  // Timeout/cancellation -- explicitly out of scope (Plan 0008 Section
  // 4): INFINITE, no timeout. A hung slangc/spirv-val invocation would
  // hang this whole build indefinitely; accepted as a Phase 1
  // limitation, not silently assumed safe.
  if (WaitForSingleObject(processHandle.get(), INFINITE) == WAIT_FAILED) {
    return LaunchResult::Err(ProcessLaunchError::WaitFailed);
  }

  DWORD exitCode = 0;
  if (!GetExitCodeProcess(processHandle.get(), &exitCode)) {
    return LaunchResult::Err(ProcessLaunchError::ExitCodeQueryFailed);
  }

  std::ifstream diagnosticsFile(tempFileGuard.path(), std::ios::binary);
  if (!diagnosticsFile.is_open()) return LaunchResult::Err(ProcessLaunchError::DiagnosticFileReadFailed);
  std::ostringstream diagnosticsBuffer;
  diagnosticsBuffer << diagnosticsFile.rdbuf();
  if (diagnosticsFile.bad()) return LaunchResult::Err(ProcessLaunchError::DiagnosticFileReadFailed);

  ProcessOutput output;
  output.diagnostics = diagnosticsBuffer.str();
  output.exitCode = static_cast<std::int32_t>(exitCode);
  return LaunchResult::Ok(std::move(output));
}

}  // namespace atlantis::tools::shader_compiler
