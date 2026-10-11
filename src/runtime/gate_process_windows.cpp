#include "gate_orchestrate.h"

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <memory>
#include <string>
#include <vector>

// Spec 0060 / ADR-0119 D1 (Plan 0060 M4, P2, P10): the gate's only OS-specific code -- the
// system CPU times, and a child-process launcher whose children die with this process.

namespace atlantis::runtime::gate {

namespace {

[[nodiscard]] std::uint64_t fileTimeValue(const FILETIME& time) {
  return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
}

[[nodiscard]] std::wstring widen(const std::string& text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

// One argument as the Windows command line reads it back (quoted, quotes and the backslashes
// before them escaped).
void appendQuoted(std::wstring& line, const std::wstring& argument) {
  line.push_back(L'"');
  std::size_t backslashes = 0;
  for (const wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') {
      line.append(backslashes * 2 + 1, L'\\');
    } else {
      line.append(backslashes, L'\\');
    }
    backslashes = 0;
    line.push_back(c);
  }
  line.append(backslashes * 2, L'\\');
  line.push_back(L'"');
}

class WindowsCpuSampler final : public CpuSampler {
 public:
  std::optional<CpuTimes> read() override {
    FILETIME idle{}, kernel{}, user{};
    if (!GetSystemTimes(&idle, &kernel, &user)) return std::nullopt;
    CpuTimes times;
    times.idle = fileTimeValue(idle);
    times.total = fileTimeValue(kernel) + fileTimeValue(user);  // kernel time includes idle time
    return times;
  }
};

class ProcessLauncher final : public ChildLauncher {
 public:
  explicit ProcessLauncher(std::string executablePath) : executable_(widen(executablePath)) {
    // Children are killed if this process goes away first, so a gate that is interrupted never
    // leaves benchmark processes running.
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (job_ != nullptr) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits));
    }
  }
  ~ProcessLauncher() override {
    if (job_ != nullptr) CloseHandle(job_);
  }
  ProcessLauncher(const ProcessLauncher&) = delete;
  ProcessLauncher& operator=(const ProcessLauncher&) = delete;

  ChildResult run(const ChildSpec& spec) override {
    ChildResult result;

    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;
    HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE nulOut = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nulIn == INVALID_HANDLE_VALUE || nulOut == INVALID_HANDLE_VALUE) {
      if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
      if (nulOut != INVALID_HANDLE_VALUE) CloseHandle(nulOut);
      result.status = ChildResult::Status::LaunchFailed;
      result.message = "cannot open NUL";
      return result;
    }

    std::wstring commandLine;
    appendQuoted(commandLine, executable_);
    for (const std::string& argument : spec.args) {
      commandLine.push_back(L' ');
      appendQuoted(commandLine, widen(argument));
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = nulIn;
    startup.hStdOutput = nulOut;
    startup.hStdError = nulOut;
    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(executable_.c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_SUSPENDED, nullptr, nullptr, &startup, &process);
    CloseHandle(nulIn);
    CloseHandle(nulOut);
    if (!created) {
      result.status = ChildResult::Status::LaunchFailed;
      result.message = "CreateProcess failed with error " + std::to_string(GetLastError());
      return result;
    }
    if (job_ != nullptr) AssignProcessToJobObject(job_, process.hProcess);
    ResumeThread(process.hThread);
    CloseHandle(process.hThread);

    const DWORD waitMs = spec.timeoutSeconds * 1000.0 >= static_cast<double>(INFINITE - 1)
                             ? INFINITE - 1
                             : static_cast<DWORD>(spec.timeoutSeconds * 1000.0);
    const DWORD waited = WaitForSingleObject(process.hProcess, waitMs);
    if (waited == WAIT_TIMEOUT) {
      TerminateProcess(process.hProcess, 1);
      WaitForSingleObject(process.hProcess, 10000);
      result.status = ChildResult::Status::TimedOut;
      result.message = "killed after " + std::to_string(static_cast<long long>(spec.timeoutSeconds)) + " s";
      CloseHandle(process.hProcess);
      return result;
    }
    DWORD exitCode = 0;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hProcess);
    result.status = ChildResult::Status::Exited;
    result.exitCode = static_cast<int>(exitCode);
    return result;
  }

 private:
  std::wstring executable_;
  HANDLE job_ = nullptr;
};

}  // namespace

std::unique_ptr<CpuSampler> makeSystemCpuSampler() { return std::make_unique<WindowsCpuSampler>(); }

std::unique_ptr<ChildLauncher> makeProcessLauncher(std::string executablePath) {
  return std::make_unique<ProcessLauncher>(std::move(executablePath));
}

std::string currentExecutablePath() {
  std::wstring buffer(1024, L'\0');
  for (;;) {
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0) return {};
    if (length < buffer.size()) {
      buffer.resize(length);
      break;
    }
    buffer.resize(buffer.size() * 2);
  }
  const int size = WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), nullptr, 0, nullptr,
                                       nullptr);
  std::string out(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, buffer.data(), static_cast<int>(buffer.size()), out.data(), size, nullptr, nullptr);
  return out;
}

}  // namespace atlantis::runtime::gate

#endif  // _WIN32
