// Task 2.2/2.3: real Win32 implementation of the Atlantis Platform
// lifecycle interface declared in platform.h. Per ADR-0005 (amended) and
// plans/0002-platform-foundation.md Sections 6-9, this is the Win32
// isolation boundary *for the Atlantis Platform module*: no Win32 header,
// type, macro, or call appears anywhere else in src/platform, including
// its public headers (include/atlantis/platform/*.h) or any other .cpp
// under src/platform. (Atlantis Core's src/core/src/assert.cpp has its
// own, unrelated #if defined(_WIN32) use of <windows.h> for a
// debugger-break helper -- that predates this module and is out of this
// file's scope.) tests/platform/windows_platform_smoke_tests.cpp is the
// only other file in the repository permitted to include <windows.h>,
// gated behind #if defined(_WIN32). This file includes no Vulkan header
// and references no Vk* type -- Vulkan WSI is entirely out of scope here.
#include <atlantis/platform/platform.h>

#include <iterator>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

namespace atlantis::platform {

namespace {

constexpr const wchar_t* kWindowClassName = L"AtlantisWindowClass";

// File-local implementation state. HWND/HINSTANCE never appear outside
// this translation unit (see native_window_handle.h: NativeWindowHandle's
// value0/value1 hold them only as opaque, reinterpret_cast'd void*, with
// no accessor that reconstitutes the typed pointer outside this file).
struct State {
  HWND hwnd = nullptr;
  HINSTANCE hInstance = nullptr;
  bool initialized = false;
  bool shutDown = false;
  bool quit = false;

  // windowProc (below) always appends here -- whether it is invoked
  // synchronously from something outside processEvents() entirely (e.g.
  // SendMessage, which dispatches straight to the window procedure on the
  // same thread; SetWindowPos/ShowWindow/UpdateWindow, which may
  // themselves synchronously send WM_SIZE/WM_SETFOCUS/etc.; or
  // shutdown()'s DestroyWindow call, which synchronously dispatches
  // WM_DESTROY), or from PeekMessageW/DispatchMessageW's drain loop
  // inside processEvents() itself. pendingBuffer is never exposed to a
  // caller directly -- only processEvents() reads it, to fold its
  // contents into outputBuffer below.
  std::vector<PlatformEvent> pendingBuffer;

  // Exactly the batch the *last* processEvents() call returned a span
  // over. Nothing outside processEvents()/shutdown() ever writes to this
  // vector, so a span returned from a previous processEvents() call stays
  // valid for exactly as long as platform.h's contract promises ("until
  // the next call to processEvents() or shutdown()") -- a synchronous
  // Win32 dispatch landing in pendingBuffer above can never reallocate or
  // otherwise invalidate it. This is the fix for the span-lifetime defect
  // in the prior single-buffer design, where windowProc's push_back into
  // the very vector a live span pointed into could reallocate that
  // vector's storage out from under the span before the caller's next
  // processEvents()/shutdown() call.
  std::vector<PlatformEvent> outputBuffer;
};

State& state() {
  static State instance;
  return instance;
}

// Moves everything currently in pendingBuffer onto the end of
// outputBuffer, in order, then empties pendingBuffer (keeping its
// capacity, so this stays allocation-free once capacity has stabilized).
// Called both to absorb events queued between processEvents() calls (as
// this call's batch prefix) and, again, to absorb whatever windowProc
// produced during this call's own drain loop below.
void movePendingIntoOutput(State& s) {
  s.outputBuffer.insert(s.outputBuffer.end(), std::make_move_iterator(s.pendingBuffer.begin()),
                         std::make_move_iterator(s.pendingBuffer.end()));
  s.pendingBuffer.clear();
}

NativeWindowHandle currentHandle() {
  const State& s = state();
  return NativeWindowHandle{PlatformKind::Windows, reinterpret_cast<void*>(s.hwnd),
                             reinterpret_cast<void*>(s.hInstance)};
}

// Both WindowResize fields are populated from the same client-area pixel
// rect -- Windows reports logical and framebuffer extents as equal in
// Phase 1; see plans/0002-platform-foundation.md Section 7.
WindowExtent clientExtent(HWND hwnd) {
  RECT rect{};
  GetClientRect(hwnd, &rect);
  return WindowExtent{static_cast<unsigned int>(rect.right - rect.left),
                       static_cast<unsigned int>(rect.bottom - rect.top)};
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  State& s = state();
  switch (message) {
    case WM_SIZE: {
      if (wParam == SIZE_MINIMIZED) {
        s.pendingBuffer.push_back(PlatformEvent{WindowResize{WindowExtent{0, 0}, WindowExtent{0, 0}}});
      } else {
        const WindowExtent extent = clientExtent(hwnd);
        s.pendingBuffer.push_back(PlatformEvent{WindowResize{extent, extent}});
      }
      return 0;
    }
    case WM_SETFOCUS:
      s.pendingBuffer.push_back(PlatformEvent{FocusGained{}});
      return 0;
    case WM_KILLFOCUS:
      s.pendingBuffer.push_back(PlatformEvent{FocusLost{}});
      return 0;
    case WM_CLOSE:
      // Request only, per plans/0002-platform-foundation.md Section 6:
      // enqueue and return 0 without calling DestroyWindow or
      // DefWindowProc for this message. The window stays fully valid;
      // only shutdown() may destroy it.
      s.pendingBuffer.push_back(PlatformEvent{WindowCloseRequested{}});
      return 0;
    case WM_DESTROY:
      // Only ever reached synchronously from shutdown()'s DestroyWindow
      // call below -- nothing else in this file destroys the window.
      s.pendingBuffer.push_back(PlatformEvent{SurfaceDestroyed{}});
      s.pendingBuffer.push_back(PlatformEvent{Quit{}});
      s.quit = true;
      return 0;
    default:
      return DefWindowProcW(hwnd, message, wParam, lParam);
  }
}

}  // namespace

atlantis::Result<std::monostate, PlatformError> initialize() {
  State& s = state();

  // Start from a clean slate.
  s.outputBuffer.clear();
  s.pendingBuffer.clear();

  // Must be configured before window creation (Section 7) so
  // GetClientRect below reflects real pixels rather than a DPI-virtualized
  // rect. Best-effort: a process whose manifest already declares
  // per-monitor-V2 awareness will fail this call (e.g.
  // ERROR_ACCESS_DENIED); that is not itself a Platform initialization
  // failure, so the result is intentionally not treated as fatal here --
  // this call configures a process-wide setting, not a per-window
  // resource this function owns.
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  s.hInstance = GetModuleHandleW(nullptr);

  WNDCLASSEXW windowClass{};
  windowClass.cbSize = sizeof(WNDCLASSEXW);
  windowClass.style = CS_HREDRAW | CS_VREDRAW;
  windowClass.lpfnWndProc = &windowProc;
  windowClass.hInstance = s.hInstance;
  windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
  windowClass.lpszClassName = kWindowClassName;

  if (RegisterClassExW(&windowClass) == 0) {
    return atlantis::Result<std::monostate, PlatformError>::Err(
        PlatformError{PlatformErrorCode::WindowClassRegistrationFailed, GetLastError()});
  }

  s.hwnd = CreateWindowExW(0, kWindowClassName, L"Atlantis", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                            CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, s.hInstance, nullptr);
  if (s.hwnd == nullptr) {
    const DWORD lastError = GetLastError();
    UnregisterClassW(kWindowClassName, s.hInstance);
    return atlantis::Result<std::monostate, PlatformError>::Err(
        PlatformError{PlatformErrorCode::WindowCreationFailed, lastError});
  }

  s.initialized = true;

  // CreateWindowExW() above can itself synchronously dispatch window
  // messages to windowProc before returning (e.g. WM_NCCREATE/WM_CREATE
  // and, depending on styles/host configuration, potentially a translated
  // one such as WM_SIZE), which would land in pendingBuffer ahead of
  // SurfaceCreated below. Discard any such creation-phase pending events
  // here: they occurred before this function had even obtained a valid
  // HWND to hand out via SurfaceCreated, so there is nothing a caller
  // could meaningfully have observed yet. This makes "SurfaceCreated is
  // the first observable event" a structural property of this function --
  // pendingBuffer is unconditionally empty at the point SurfaceCreated is
  // pushed below -- rather than incidental to whichever messages a given
  // Windows version/configuration happens to dispatch during
  // CreateWindowExW.
  s.pendingBuffer.clear();

  // Enqueue SurfaceCreated into pendingBuffer *before* ShowWindow()/
  // UpdateWindow() below. Those calls may themselves synchronously
  // dispatch WM_SIZE/WM_SETFOCUS through windowProc, which also appends to
  // pendingBuffer -- after SurfaceCreated, preserving order, since
  // pendingBuffer is guaranteed empty (see above) at this point. The first
  // processEvents() call folds pendingBuffer into outputBuffer as that
  // call's batch prefix (see movePendingIntoOutput), so SurfaceCreated
  // ends up first in the batch, ahead of any such synchronous resize/
  // focus event -- see plans/0002-platform-foundation.md Section 6.
  s.pendingBuffer.push_back(PlatformEvent{SurfaceCreated{currentHandle()}});

  ShowWindow(s.hwnd, SW_SHOWDEFAULT);
  UpdateWindow(s.hwnd);

  return atlantis::Result<std::monostate, PlatformError>::Ok(std::monostate{});
}

std::span<const PlatformEvent> processEvents() {
  State& s = state();
  ATLANTIS_CHECK_MSG(s.initialized, "processEvents() called before a successful initialize()");

  // Invalidate the batch returned by the previous call (this is exactly
  // the point at which platform.h's contract permits/expects that), then
  // absorb anything windowProc queued into pendingBuffer since then --
  // e.g. a synchronous SendMessage/SetWindowPos/ShowWindow dispatch
  // between calls, or a lifecycle event from initialize()/shutdown() --
  // as this batch's prefix.
  s.outputBuffer.clear();
  movePendingIntoOutput(s);

  MSG msg;
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  // windowProc only ever appends to pendingBuffer (see its definition
  // above), never to outputBuffer directly; absorb whatever this call's
  // own drain loop just produced.
  movePendingIntoOutput(s);

  return std::span<const PlatformEvent>{s.outputBuffer.data(), s.outputBuffer.size()};
}

bool shouldQuit() {
  ATLANTIS_CHECK_MSG(state().initialized, "shouldQuit() called before a successful initialize()");
  return state().quit;
}

void shutdown() {
  State& s = state();
  ATLANTIS_CHECK_MSG(s.initialized && !s.shutDown,
                      "shutdown() called without a successful initialize(), or called twice");

  // Explicitly invalidate the batch from the last processEvents() call,
  // per platform.h's documented contract ("valid only until the next call
  // to processEvents() or shutdown()").
  s.outputBuffer.clear();

  if (s.hwnd != nullptr) {
    DestroyWindow(s.hwnd);  // Synchronously dispatches WM_DESTROY to windowProc above,
                             // which appends {SurfaceDestroyed, Quit} to pendingBuffer --
                             // observed by the next processEvents() call's initial
                             // movePendingIntoOutput() call, ahead of that call's own
                             // drain loop.
    s.hwnd = nullptr;
  }

  UnregisterClassW(kWindowClassName, s.hInstance);

  // Re-initialization after shutdown() is unsupported in Phase 1 (Section
  // 6 / Unresolved Implementation Details #7): not designed, not guarded.
  s.shutDown = true;
}

PlatformKind currentPlatform() {
  return PlatformKind::Windows;
}

}  // namespace atlantis::platform
