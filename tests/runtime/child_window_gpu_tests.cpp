// Spec 0064 R3 / ADR-0126 D1-D3 (Plan 0064 M1): the real atlantis_runtime
// rendering as a child of a window in another process -- this test's -- the
// cross-process parent/child pair Atlantis Editor App will create. The host
// pumps its own messages throughout (the two threads' input queues are
// attached). Nothing here asserts a measured time.

#include <atlantis/remote/remote_client.h>
#include <atlantis/remote/session_file.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>

#if defined(_WIN32) && defined(ATLANTIS_RUNTIME_EXECUTABLE)

namespace {

namespace fs = std::filesystem;

constexpr const wchar_t* kHostClass = L"AtlantisChildWindowTestHost";

LRESULT CALLBACK hostProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

// Pumps this thread's messages for `seconds`, or until `done()`.
template <typename Done>
bool pumpUntil(double seconds, Done done) {
  const auto end = std::chrono::steady_clock::now() + std::chrono::duration<double>(seconds);
  while (std::chrono::steady_clock::now() < end) {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    if (done()) return true;
    Sleep(10);
  }
  return done();
}

struct TempDir {
  TempDir() {
    std::random_device device;
    char text[32];
    std::snprintf(text, sizeof(text), "%08x%08x", device(), device());
    path = fs::temp_directory_path() / ("atlantis_child_window_test_" + std::string(text));
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
  fs::path path;
};

}  // namespace

TEST_CASE("atlantis_runtime --parent-window renders as a child of another process's window and exits with it",
          "[runtime][gpu][child_window]") {
  // Sizes in physical pixels, as the Runtime (per-monitor aware v2) sees them.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  TempDir dir;
  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(windowClass);
  windowClass.lpfnWndProc = &hostProc;
  windowClass.hInstance = GetModuleHandleW(nullptr);
  windowClass.lpszClassName = kHostClass;
  RegisterClassExW(&windowClass);
  HWND host = CreateWindowExW(0, kHostClass, L"host", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 80, 80, 960, 600, nullptr,
                              nullptr, GetModuleHandleW(nullptr), nullptr);
  REQUIRE(host != nullptr);
  ShowWindow(host, SW_SHOW);

  const fs::path session = dir.path / "runtime.session.json";
  std::wstring command = L"\"" + fs::path(ATLANTIS_RUNTIME_EXECUTABLE).wstring() + L"\" --listen 0 --session-file \"" +
                         session.wstring() + L"\" --parent-window " +
                         std::to_wstring(static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(host)));
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  REQUIRE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process));

  // The session file names the child once the first frame has seen it.
  std::optional<atlantis::remote::SessionInfo> info;
  const bool written = pumpUntil(120.0, [&] {
    std::ifstream in(session, std::ios::binary);
    if (!in) return false;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const auto parsed = atlantis::remote::parseSession(text);
    if (parsed.isErr() || !parsed.value().window.has_value()) return false;
    info = parsed.value();
    return true;
  });
  REQUIRE(written);
  const HWND child = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(*info->window));
  CHECK(IsWindow(child));
  CHECK(GetParent(child) == host);
  CHECK((GetWindowLongPtrW(child, GWL_STYLE) & WS_CHILD) != 0);
  DWORD childProcess = 0;
  GetWindowThreadProcessId(child, &childProcess);
  CHECK(childProcess == process.dwProcessId);
  CHECK(info->pid == process.dwProcessId);

  // It renders before anyone resizes it (Plan 0064 M6: a child whose host
  // never resized it drew nothing), and after the host sizes it, as Atlantis
  // Editor App will -- each step captures a frame at the child's size.
  atlantis::remote::RemoteOptions options;
  options.whileWaiting = [] {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) DispatchMessageW(&msg);
  };
  auto connected = atlantis::remote::connectRemote(*info, options);
  REQUIRE(connected.isOk());
  const auto stepImage = [&](const char* name) {
    std::optional<atlantis::Result<atlantis::connection::FrameReport, atlantis::connection::ControlError>> result;
    connected.value()->control().step(atlantis::connection::StepRequest{1, (dir.path / name).string()},
                                      [&](auto r) { result = std::move(r); });
    REQUIRE(result.has_value());
    return *result;
  };
  RECT hostClient{};
  GetClientRect(host, &hostClient);
  const auto first = stepImage("first.png");
  REQUIRE(first.isOk());  // not NotRendering
  REQUIRE(first.value().image.has_value());
  CHECK(first.value().image->width == static_cast<std::uint32_t>(hostClient.right));
  CHECK(first.value().image->height == static_cast<std::uint32_t>(hostClient.bottom));

  SetWindowPos(child, nullptr, 20, 40, 640, 360, SWP_NOZORDER | SWP_NOACTIVATE);
  CHECK_FALSE(pumpUntil(2.0, [&] { return WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0; }));
  RECT client{};
  GetClientRect(child, &client);
  CHECK(client.right == 640);
  CHECK(client.bottom == 360);
  const auto resized = stepImage("resized.png");
  REQUIRE(resized.isOk());
  REQUIRE(resized.value().image.has_value());
  CHECK(resized.value().image->width == 640);
  CHECK(resized.value().image->height == 360);
  connected.value().reset();

  // The host goes away: the Runtime's child goes with it, and the Runtime exits cleanly.
  DestroyWindow(host);
  const bool exited = pumpUntil(60.0, [&] { return WaitForSingleObject(process.hProcess, 0) == WAIT_OBJECT_0; });
  if (!exited) TerminateProcess(process.hProcess, 99);
  DWORD exitCode = 0;
  GetExitCodeProcess(process.hProcess, &exitCode);
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  UnregisterClassW(kHostClass, GetModuleHandleW(nullptr));
  REQUIRE(exited);
  CHECK(exitCode == 0);
  CHECK_FALSE(fs::exists(session));  // a clean shutdown removes it
}

#endif  // defined(_WIN32) && defined(ATLANTIS_RUNTIME_EXECUTABLE)
