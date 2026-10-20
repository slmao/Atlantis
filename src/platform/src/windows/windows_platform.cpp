// Task 2.2/2.3: real Win32 implementation of the Atlantis Platform
// lifecycle interface declared in platform.h. Per ADR-0005 (amended) and
// docs/plans/0002-platform-foundation.md Sections 6-9, this is the Win32
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
#include <optional>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <windowsx.h>

#include <atlantis/profile_recorder.h>

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

  // Spec 0059 / ADR-0117 D2: the outer size of a window created for a requested
  // client size; 0 when none was requested. WM_GETMINMAXINFO raises the maximum
  // tracking size to at least this, so Windows does not shrink the window to the
  // monitor.
  int requestedOuterWidth = 0;
  int requestedOuterHeight = 0;

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

  // Spec 0064 / ADR-0126 D1, D4: child-window mode -- the borrowed parent,
  // whether the parent's disappearance was reported, and whether the child
  // is being destroyed by shutdown() (else a WM_DESTROY came from the
  // parent's destruction).
  HWND parent = nullptr;
  bool parentGoneReported = false;
  bool destroying = false;

  // Spec 0056 / ADR-0109 (Plan 0056 P2): WM_CHAR delivers UTF-16 code units;
  // a high surrogate waits here for its low surrogate, so TextEntered always
  // carries one whole code point.
  wchar_t pendingHighSurrogate = 0;
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

// Spec 0056 / ADR-0109 (Plan 0056 P2): virtual-key code -> Platform's closed
// Key set. Keys outside the set are not reported. Left/right Ctrl and Alt
// are told apart by the extended-key bit, Shift by its scan code.
std::optional<Key> toKey(WPARAM virtualKey, LPARAM lParam) {
  const bool extended = (HIWORD(lParam) & KF_EXTENDED) != 0;
  if (virtualKey >= 'A' && virtualKey <= 'Z') {
    return static_cast<Key>(static_cast<int>(Key::A) + static_cast<int>(virtualKey - 'A'));
  }
  if (virtualKey >= '0' && virtualKey <= '9') {
    return static_cast<Key>(static_cast<int>(Key::Num0) + static_cast<int>(virtualKey - '0'));
  }
  if (virtualKey >= VK_F1 && virtualKey <= VK_F12) {
    return static_cast<Key>(static_cast<int>(Key::F1) + static_cast<int>(virtualKey - VK_F1));
  }
  switch (virtualKey) {
    case VK_TAB: return Key::Tab;
    case VK_LEFT: return Key::LeftArrow;
    case VK_RIGHT: return Key::RightArrow;
    case VK_UP: return Key::UpArrow;
    case VK_DOWN: return Key::DownArrow;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    case VK_INSERT: return Key::Insert;
    case VK_DELETE: return Key::Delete;
    case VK_BACK: return Key::Backspace;
    case VK_SPACE: return Key::Space;
    case VK_RETURN: return Key::Enter;
    case VK_ESCAPE: return Key::Escape;
    case VK_CONTROL: return extended ? Key::RightCtrl : Key::LeftCtrl;
    case VK_MENU: return extended ? Key::RightAlt : Key::LeftAlt;
    case VK_SHIFT: {
      const UINT scanCode = (HIWORD(lParam) & 0xFF);
      return MapVirtualKeyW(scanCode, MAPVK_VSC_TO_VK_EX) == VK_RSHIFT ? Key::RightShift : Key::LeftShift;
    }
    default: return std::nullopt;
  }
}

KeyModifiers currentModifiers() {
  return KeyModifiers{(GetKeyState(VK_CONTROL) & 0x8000) != 0, (GetKeyState(VK_SHIFT) & 0x8000) != 0,
                      (GetKeyState(VK_MENU) & 0x8000) != 0};
}

TextEntered encodeUtf8(char32_t codePoint) {
  TextEntered text;
  auto put = [&text](unsigned value) { text.utf8[text.size++] = static_cast<char>(static_cast<unsigned char>(value)); };
  if (codePoint < 0x80) {
    put(codePoint);
  } else if (codePoint < 0x800) {
    put(0xC0 | (codePoint >> 6));
    put(0x80 | (codePoint & 0x3F));
  } else if (codePoint < 0x10000) {
    put(0xE0 | (codePoint >> 12));
    put(0x80 | ((codePoint >> 6) & 0x3F));
    put(0x80 | (codePoint & 0x3F));
  } else {
    put(0xF0 | (codePoint >> 18));
    put(0x80 | ((codePoint >> 12) & 0x3F));
    put(0x80 | ((codePoint >> 6) & 0x3F));
    put(0x80 | (codePoint & 0x3F));
  }
  return text;
}

void pushPointerButton(State& s, PointerButton button, bool down, LPARAM lParam) {
  s.pendingBuffer.push_back(PlatformEvent{PointerButtonChanged{button, down, static_cast<float>(GET_X_LPARAM(lParam)),
                                                                static_cast<float>(GET_Y_LPARAM(lParam))}});
  // Keep receiving the pointer while a button is held outside the window, so
  // a drag ends with its release.
  if (down) {
    SetCapture(s.hwnd);
  } else if ((GetKeyState(VK_LBUTTON) & 0x8000) == 0 && (GetKeyState(VK_RBUTTON) & 0x8000) == 0 &&
             (GetKeyState(VK_MBUTTON) & 0x8000) == 0) {
    ReleaseCapture();
  }
}

// Spec 0064 / ADR-0126 D4 (route R-a): in child-window mode, the message is
// re-posted to the parent after Platform has emitted its own event, so the
// host's toolkit handles it as input to its own window. Client-coordinate
// pointer messages are mapped into the parent's client space; the wheel's
// coordinates are already screen coordinates.
void forwardToParent(const State& s, HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  if (s.parent == nullptr) return;
  switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP: {
      POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
      MapWindowPoints(hwnd, s.parent, &point, 1);
      PostMessageW(s.parent, message, wParam, MAKELPARAM(point.x, point.y));
      return;
    }
    default:
      PostMessageW(s.parent, message, wParam, lParam);
      return;
  }
}

NativeWindowHandle currentHandle() {
  const State& s = state();
  return NativeWindowHandle{PlatformKind::Windows, reinterpret_cast<void*>(s.hwnd),
                             reinterpret_cast<void*>(s.hInstance)};
}

// Both WindowResize fields are populated from the same client-area pixel
// rect -- Windows reports logical and framebuffer extents as equal in
// Phase 1; see docs/plans/0002-platform-foundation.md Section 7.
WindowExtent clientExtent(HWND hwnd) {
  RECT rect{};
  GetClientRect(hwnd, &rect);
  return WindowExtent{static_cast<unsigned int>(rect.right - rect.left),
                       static_cast<unsigned int>(rect.bottom - rect.top)};
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
  State& s = state();
  // Spec 0064 / ADR-0126 D4: in child-window mode input reaches the parent
  // too. Posting does not re-enter this procedure, so the order relative to
  // this window's own handling below does not matter.
  switch (message) {
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEHWHEEL:
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_CHAR:
      forwardToParent(s, hwnd, message, wParam, lParam);
      break;
    default:
      break;
  }
  switch (message) {
    case WM_MOUSEACTIVATE:
      // Spec 0064 / ADR-0126 D4: a child never takes activation from its host.
      if (s.parent != nullptr) return MA_NOACTIVATE;
      return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_GETMINMAXINFO: {
      if (s.requestedOuterWidth > 0 && s.requestedOuterHeight > 0) {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        if (info->ptMaxTrackSize.x < s.requestedOuterWidth) info->ptMaxTrackSize.x = s.requestedOuterWidth;
        if (info->ptMaxTrackSize.y < s.requestedOuterHeight) info->ptMaxTrackSize.y = s.requestedOuterHeight;
        return 0;
      }
      return DefWindowProcW(hwnd, message, wParam, lParam);
    }
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
      // Request only, per docs/plans/0002-platform-foundation.md Section 6:
      // enqueue and return 0 without calling DestroyWindow or
      // DefWindowProc for this message. The window stays fully valid;
      // only shutdown() may destroy it.
      s.pendingBuffer.push_back(PlatformEvent{WindowCloseRequested{}});
      return 0;
    // Spec 0056 / ADR-0109 (Plan 0056 P2): input, as plain values. Client-area
    // coordinates are framebuffer pixels (the process is per-monitor DPI
    // aware, initialize()).
    case WM_MOUSEMOVE:
      s.pendingBuffer.push_back(PlatformEvent{
          PointerMoved{static_cast<float>(GET_X_LPARAM(lParam)), static_cast<float>(GET_Y_LPARAM(lParam))}});
      return 0;
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
      pushPointerButton(s, PointerButton::Left, message == WM_LBUTTONDOWN, lParam);
      return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
      pushPointerButton(s, PointerButton::Right, message == WM_RBUTTONDOWN, lParam);
      return 0;
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
      pushPointerButton(s, PointerButton::Middle, message == WM_MBUTTONDOWN, lParam);
      return 0;
    case WM_MOUSEWHEEL:
      s.pendingBuffer.push_back(PlatformEvent{
          WheelScrolled{0.0f, static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA)}});
      return 0;
    case WM_MOUSEHWHEEL:
      s.pendingBuffer.push_back(PlatformEvent{
          WheelScrolled{static_cast<float>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<float>(WHEEL_DELTA), 0.0f}});
      return 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP: {
      if (const std::optional<Key> key = toKey(wParam, lParam)) {
        const bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
        s.pendingBuffer.push_back(PlatformEvent{KeyChanged{*key, down, currentModifiers()}});
      }
      // System keys keep their default handling (Alt+F4 still requests close).
      if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) return DefWindowProcW(hwnd, message, wParam, lParam);
      return 0;
    }
    case WM_CHAR: {
      const auto unit = static_cast<wchar_t>(wParam);
      if (unit >= 0xD800 && unit <= 0xDBFF) {
        s.pendingHighSurrogate = unit;
        return 0;
      }
      char32_t codePoint = unit;
      if (unit >= 0xDC00 && unit <= 0xDFFF) {
        if (s.pendingHighSurrogate == 0) return 0;  // an unpaired low surrogate: dropped
        codePoint = 0x10000 + ((static_cast<char32_t>(s.pendingHighSurrogate) - 0xD800) << 10) +
                    (static_cast<char32_t>(unit) - 0xDC00);
      }
      s.pendingHighSurrogate = 0;
      // Control characters (Backspace, Tab, Enter, Escape...) arrive as key
      // events; text carries printable code points only.
      if (codePoint < 0x20 || codePoint == 0x7F) return 0;
      s.pendingBuffer.push_back(PlatformEvent{encodeUtf8(codePoint)});
      return 0;
    }
    case WM_DESTROY:
      // Spec 0064 / ADR-0126 D1: a child is also destroyed with its parent.
      // That is a close request, not shutdown(): the surface is gone, so it is
      // reported destroyed too, and shutdown() later has no window left to
      // destroy.
      if (s.parent != nullptr && !s.destroying) {
        s.pendingBuffer.push_back(PlatformEvent{WindowCloseRequested{}});
        s.pendingBuffer.push_back(PlatformEvent{SurfaceDestroyed{}});
        s.parentGoneReported = true;
        s.hwnd = nullptr;
        return 0;
      }
      // Otherwise only ever reached synchronously from shutdown()'s
      // DestroyWindow call below -- nothing else in this file destroys the
      // window.
      s.pendingBuffer.push_back(PlatformEvent{SurfaceDestroyed{}});
      s.pendingBuffer.push_back(PlatformEvent{Quit{}});
      s.quit = true;
      return 0;
    default:
      return DefWindowProcW(hwnd, message, wParam, lParam);
  }
}

}  // namespace

atlantis::Result<std::monostate, PlatformError> initialize() { return initialize(WindowOptions{}); }

atlantis::Result<std::monostate, PlatformError> initialize(const WindowOptions& options) {
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

  // Spec 0059 / ADR-0117 D2: size the window so its CLIENT area is the requested
  // size (the process is per-monitor-DPI-aware, so client pixels are framebuffer
  // pixels); otherwise the OS chooses, as before.
  int windowWidth = CW_USEDEFAULT;
  int windowHeight = CW_USEDEFAULT;
  s.requestedOuterWidth = 0;
  s.requestedOuterHeight = 0;
  // Spec 0064 / ADR-0126 D1: child-window mode under a live foreign parent.
  s.parent = nullptr;
  s.parentGoneReported = false;
  s.destroying = false;
  if (options.parent.has_value()) {
    const HWND parent = options.parent->kind == PlatformKind::Windows ? reinterpret_cast<HWND>(options.parent->value0)
                                                                      : nullptr;
    if (parent == nullptr || !IsWindow(parent)) {
      return atlantis::Result<std::monostate, PlatformError>::Err(
          PlatformError{PlatformErrorCode::WindowCreationFailed, ERROR_INVALID_WINDOW_HANDLE});
    }
    s.parent = parent;
  }
  if (s.parent == nullptr && options.requestedClientSize.has_value() && options.requestedClientSize->width > 0 &&
      options.requestedClientSize->height > 0) {
    RECT rect{0, 0, static_cast<LONG>(options.requestedClientSize->width),
              static_cast<LONG>(options.requestedClientSize->height)};
    if (AdjustWindowRectExForDpi(&rect, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForSystem())) {
      windowWidth = rect.right - rect.left;
      windowHeight = rect.bottom - rect.top;
      s.requestedOuterWidth = windowWidth;
      s.requestedOuterHeight = windowHeight;
    }
  }

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

  if (s.parent != nullptr) {
    // The child fills the parent's client area; its host resizes it later.
    RECT parentClient{};
    GetClientRect(s.parent, &parentClient);
    s.hwnd = CreateWindowExW(0, kWindowClassName, L"Atlantis", WS_CHILD | WS_CLIPSIBLINGS, 0, 0,
                              parentClient.right - parentClient.left, parentClient.bottom - parentClient.top, s.parent,
                              nullptr, s.hInstance, nullptr);
  } else {
    s.hwnd = CreateWindowExW(0, kWindowClassName, L"Atlantis", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                              windowWidth, windowHeight, nullptr, nullptr, s.hInstance, nullptr);
  }
  if (s.hwnd == nullptr) {
    const DWORD lastError = GetLastError();
    UnregisterClassW(kWindowClassName, s.hInstance);
    return atlantis::Result<std::monostate, PlatformError>::Err(
        PlatformError{PlatformErrorCode::WindowCreationFailed, lastError});
  }

  s.initialized = true;

#if defined(ATLANTIS_PROFILING)
  {
    // Spec 0059 (ADR-0117 D6): the display's refresh rate, for the benchmark's
    // metadata. Best effort: 0 when the OS does not say.
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    atlantis::profile::DeviceInfo info = atlantis::profile::deviceInfo();
    info.refreshHz = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode) ? static_cast<double>(mode.dmDisplayFrequency) : 0.0;
    atlantis::profile::setDeviceInfo(info);
  }
#endif

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
  // focus event -- see docs/plans/0002-platform-foundation.md Section 6.
  s.pendingBuffer.push_back(PlatformEvent{SurfaceCreated{currentHandle()}});

  ShowWindow(s.hwnd, s.parent != nullptr ? SW_SHOW : SW_SHOWDEFAULT);
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

  // Spec 0064 / ADR-0126 D1: a child whose parent has gone without taking
  // the child with it yet asks to close, once.
  if (s.parent != nullptr && !s.parentGoneReported && !IsWindow(s.parent)) {
    s.pendingBuffer.push_back(PlatformEvent{WindowCloseRequested{}});
    s.parentGoneReported = true;
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

  s.destroying = true;
  if (s.hwnd == nullptr) {
    // Spec 0064 / ADR-0126 D1: a child destroyed with its parent -- nothing
    // left to destroy; the quit its destruction would have reported.
    s.pendingBuffer.push_back(PlatformEvent{Quit{}});
    s.quit = true;
  } else {
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
