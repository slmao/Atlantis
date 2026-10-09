// Plan 0057 M4 (Spec 0057 R8, ruling Q8; ADR-0110 D6; J8, J12): the example
// client in its own process against `atlantis_runtime --listen 0` in another
// -- the real two-process loop. atlantis_gameplay_demo must exit 0 with every
// step line in order; a second run against a fresh Runtime prints the same
// lines (logic steps, not frame numbers, ruling Q7); and the Runtime closes
// gracefully (WM_CLOSE, exit 0).
//
// The process helpers below (widen, quote, Child, windowsOf) are copied whole
// from tests/cli/cli_e2e_tests.cpp (Plan 0055 M7), not extracted from it, so
// that verified file stays untouched (Plan 0057 J8). Test-only Windows code.
//
// Plan 0058 M5 (Spec 0058 R5, ruling Q6; J10): one more case runs the C#
// beacon demo (`dotnet GameplayDemo.dll`) the same way, through the same
// helpers. It is compiled in only when C# is enabled, and SKIPs otherwise.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

namespace fs = std::filesystem;

// --- Copied from tests/cli/cli_e2e_tests.cpp (Plan 0057 J8). ----------------

[[nodiscard]] std::wstring widen(std::string_view text) {
  if (text.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
  std::wstring out(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), size);
  return out;
}

// The standard Windows argument quoting (CommandLineToArgvW's inverse).
[[nodiscard]] std::wstring quote(const std::wstring& argument) {
  if (!argument.empty() && argument.find_first_of(L" \t\"") == std::wstring::npos) return argument;
  std::wstring out = L"\"";
  std::size_t backslashes = 0;
  for (const wchar_t c : argument) {
    if (c == L'\\') {
      ++backslashes;
      continue;
    }
    if (c == L'"') out.append(backslashes * 2 + 1, L'\\');
    else out.append(backslashes, L'\\');
    backslashes = 0;
    out += c;
  }
  out.append(backslashes * 2, L'\\');
  out += L'"';
  return out;
}

// A child process with its three standard streams on pipes; output is read
// by two threads so neither pipe can fill and stall the child.
class Child {
 public:
  Child(const fs::path& exe, const std::vector<std::string>& arguments) {
    SECURITY_ATTRIBUTES inherit{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
    HANDLE inRead = nullptr;
    HANDLE outWrite = nullptr;
    HANDLE errWrite = nullptr;
    REQUIRE(CreatePipe(&inRead, &stdinWrite_, &inherit, 0));
    REQUIRE(CreatePipe(&stdoutRead_, &outWrite, &inherit, 0));
    REQUIRE(CreatePipe(&stderrRead_, &errWrite, &inherit, 0));
    SetHandleInformation(stdinWrite_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stdoutRead_, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(stderrRead_, HANDLE_FLAG_INHERIT, 0);
    std::wstring commandLine = quote(exe.wstring());
    for (const std::string& argument : arguments) commandLine += L" " + quote(widen(argument));
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = inRead;
    startup.hStdOutput = outWrite;
    startup.hStdError = errWrite;
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(exe.wstring().c_str(), commandLine.data(), nullptr, nullptr, TRUE,
                                        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    CloseHandle(inRead);
    CloseHandle(outWrite);
    CloseHandle(errWrite);
    REQUIRE(created);
    process_ = info.hProcess;
    processId_ = info.dwProcessId;
    CloseHandle(info.hThread);
    outReader_ = std::thread([this] { drain(stdoutRead_, out_); });
    errReader_ = std::thread([this] { drain(stderrRead_, err_); });
  }
  ~Child() {
    closeStdin();
    if (process_ != nullptr && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) TerminateProcess(process_, 99);
    if (outReader_.joinable()) outReader_.join();
    if (errReader_.joinable()) errReader_.join();
    if (process_ != nullptr) CloseHandle(process_);
    CloseHandle(stdoutRead_);
    CloseHandle(stderrRead_);
  }
  Child(const Child&) = delete;
  Child& operator=(const Child&) = delete;

  void write(std::string_view text) {
    DWORD written = 0;
    REQUIRE(WriteFile(stdinWrite_, text.data(), static_cast<DWORD>(text.size()), &written, nullptr));
  }
  void closeStdin() {
    if (stdinWrite_ != nullptr) CloseHandle(stdinWrite_);
    stdinWrite_ = nullptr;
  }
  // The exit code, once it exits within `timeout`.
  [[nodiscard]] std::optional<DWORD> wait(std::chrono::milliseconds timeout) {
    if (WaitForSingleObject(process_, static_cast<DWORD>(timeout.count())) != WAIT_OBJECT_0) return std::nullopt;
    DWORD code = 0;
    GetExitCodeProcess(process_, &code);
    if (outReader_.joinable()) outReader_.join();
    if (errReader_.joinable()) errReader_.join();
    return code;
  }
  [[nodiscard]] DWORD processId() const noexcept { return processId_; }
  [[nodiscard]] const std::string& out() const noexcept { return out_; }
  [[nodiscard]] const std::string& err() const noexcept { return err_; }

 private:
  static void drain(HANDLE pipe, std::string& sink) {
    char buffer[4096];
    DWORD read = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &read, nullptr) && read > 0) sink.append(buffer, read);
  }

  HANDLE stdinWrite_ = nullptr;
  HANDLE stdoutRead_ = nullptr;
  HANDLE stderrRead_ = nullptr;
  HANDLE process_ = nullptr;
  DWORD processId_ = 0;
  std::string out_;
  std::string err_;
  std::thread outReader_;
  std::thread errReader_;
};

// The top-level windows of a process.
[[nodiscard]] std::vector<HWND> windowsOf(DWORD processId) {
  struct Search {
    DWORD processId;
    std::vector<HWND> found;
  } search{processId, {}};
  EnumWindows(
      [](HWND window, LPARAM parameter) -> BOOL {
        auto* s = reinterpret_cast<Search*>(parameter);
        DWORD owner = 0;
        GetWindowThreadProcessId(window, &owner);
        if (owner == s->processId) s->found.push_back(window);
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&search));
  return search.found;
}

// --- End of the copy. ----------------------------------------------------------

// The step lines atlantis_gameplay_demo prints, in order (K = 8).
[[nodiscard]] std::vector<std::string> stepLines(const std::string& out) {
  std::vector<std::string> lines;
  std::size_t start = 0;
  while (start < out.size()) {
    const std::size_t end = out.find('\n', start);
    std::string line = out.substr(start, end == std::string::npos ? std::string::npos : end - start);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (!line.empty()) lines.push_back(std::move(line));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return lines;
}

// One Runtime with --listen, the demo against it, and the Runtime closed
// gracefully. Returns the demo's output lines. The demo is `demoExe` with
// `demoArguments` before its own (Plan 0058: dotnet and GameplayDemo.dll).
[[nodiscard]] std::vector<std::string> runOnce(const fs::path& dir, const fs::path& demoExe = ATLANTIS_GAMEPLAY_DEMO_EXE,
                                               std::vector<std::string> demoArguments = {}) {
  const fs::path session = dir / "runtime.session.json";
  Child runtime(ATLANTIS_RUNTIME_EXE,
                {"--scene", "integrated_showcase_demo", "--listen", "0", "--session-file", session.string()});
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
  while (!fs::exists(session) && std::chrono::steady_clock::now() < deadline) {
    REQUIRE_FALSE(runtime.wait(std::chrono::milliseconds(100)).has_value());  // still running
  }
  REQUIRE(fs::exists(session));

  demoArguments.insert(demoArguments.end(), {"--session", session.string(), "--capture-dir", dir.string()});
  Child demo(demoExe, demoArguments);
  demo.closeStdin();
  const auto demoExit = demo.wait(std::chrono::seconds(300));
  INFO("demo stdout:\n" << demo.out() << "\ndemo stderr:\n" << demo.err());
  REQUIRE(demoExit.has_value());
  CHECK(*demoExit == 0);

  const std::vector<HWND> windows = windowsOf(runtime.processId());
  REQUIRE_FALSE(windows.empty());
  for (const HWND window : windows) PostMessageW(window, WM_CLOSE, 0, 0);
  const auto exited = runtime.wait(std::chrono::seconds(30));
  INFO("runtime stderr tail: " << runtime.err().substr(runtime.err().size() > 2000 ? runtime.err().size() - 2000 : 0));
  REQUIRE(exited.has_value());  // a forced kill (the Child destructor) would be a failure
  CHECK(*exited == 0);
  CHECK_FALSE(fs::exists(session));
  return stepLines(demo.out());
}

}  // namespace

TEST_CASE("atlantis_gameplay_demo runs the Gameplay SDK loop against atlantis_runtime --listen, identically twice",
          "[gameplay_sdk][e2e][gpu]") {
  const fs::path root = fs::temp_directory_path() / "atlantis_gameplay_e2e" / std::to_string(std::random_device{}());
  fs::create_directories(root / "first");
  fs::create_directories(root / "second");

  const std::vector<std::string> first = runOnce(root / "first");
  // 1 connect, 2 find, 3 baseline, 4 spawn, 5.1-5.8 move, 6 refuse,
  // 7 capture, 8 destroy, 9 resume.
  REQUIRE(first.size() == 16);
  const std::vector<std::string_view> prefixes{"1 connect: ok", "2 find: ok",    "3 baseline: ok", "4 spawn: ok",
                                               "5.1 move: ok",  "5.2 move: ok",  "5.3 move: ok",   "5.4 move: ok",
                                               "5.5 move: ok",  "5.6 move: ok",  "5.7 move: ok",   "5.8 move: ok",
                                               "6 refuse: ok",  "7 capture: ok", "8 destroy: ok",  "9 resume: ok"};
  for (std::size_t i = 0; i < prefixes.size(); ++i) {
    INFO(first[i]);
    CHECK(first[i].starts_with(prefixes[i]));
  }

  // A second Runtime, a second run: the same lines (J12's fixed beacon GUID;
  // logic steps, not frame numbers). The capture lines name no path.
  const std::vector<std::string> second = runOnce(root / "second");
  CHECK(second == first);

  std::error_code ec;
  fs::remove_all(root, ec);
}

// Plan 0058 M5 (Spec 0058 R5; J10): the C# Gameplay SDK's beacon demo, a
// managed process speaking atlantis.remote/1 itself, in the same loop.
TEST_CASE("the C# GameplayDemo runs the C# Gameplay SDK loop against atlantis_runtime --listen, identically twice",
          "[gameplay_sdk][e2e][gpu][csharp]") {
#if defined(ATLANTIS_CSHARP_GAMEPLAY_DEMO_DLL)
  const fs::path root =
      fs::temp_directory_path() / "atlantis_csharp_gameplay_e2e" / std::to_string(std::random_device{}());
  fs::create_directories(root / "first");
  fs::create_directories(root / "second");

  const std::vector<std::string> first =
      runOnce(root / "first", ATLANTIS_DOTNET_EXE, {ATLANTIS_CSHARP_GAMEPLAY_DEMO_DLL});
  // 1 connect, 2 find, 3 baseline, 4 spawn, 5.1-5.8 move, 6 refuse,
  // 7 capture, 8 destroy, 9 resume.
  REQUIRE(first.size() == 16);
  const std::vector<std::string_view> prefixes{"1 connect: ok", "2 find: ok",    "3 baseline: ok", "4 spawn: ok",
                                               "5.1 move: ok",  "5.2 move: ok",  "5.3 move: ok",   "5.4 move: ok",
                                               "5.5 move: ok",  "5.6 move: ok",  "5.7 move: ok",   "5.8 move: ok",
                                               "6 refuse: ok",  "7 capture: ok", "8 destroy: ok",  "9 resume: ok"};
  for (std::size_t i = 0; i < prefixes.size(); ++i) {
    INFO(first[i]);
    CHECK(first[i].starts_with(prefixes[i]));
  }
  // The square trajectory with K = 8: corners and edge midpoints, 4 or 6.
  CHECK(first[4] == "5.1 move: ok (position 0 2 1.5, intensity 6)");
  CHECK(first[11] == "5.8 move: ok (position 1.5 2 1.5, intensity 4)");

  // A second Runtime, a second run: the same lines (the fixed beacon GUID;
  // logic steps, not frame numbers; no path in any line).
  const std::vector<std::string> second =
      runOnce(root / "second", ATLANTIS_DOTNET_EXE, {ATLANTIS_CSHARP_GAMEPLAY_DEMO_DLL});
  CHECK(second == first);

  std::error_code ec;
  fs::remove_all(root, ec);
#else
  SKIP("C# is not enabled: no .NET 10.0.1xx SDK at configure (Plan 0058 P7)");
#endif
}
