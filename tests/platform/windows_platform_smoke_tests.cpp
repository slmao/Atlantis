#include <atlantis/platform/platform.h>

#include <catch2/catch_test_macros.hpp>

// Task 2.3: Windows integration/smoke tests, per
// docs/plans/0002-platform-foundation.md Section 11. This file tests Atlantis
// Platform's own lifecycle/event contract specifically -- a separate,
// explicit Windows test boundary from
// tests/vulkan_backend/vulkan_presentation_gpu_tests.cpp (Spec 0003's
// approved Vulkan GPU integration test, which drives the same real
// window's HWND to exercise Presentation's non-frame lifecycle, strictly
// confined to its own #if defined(_WIN32) block). Neither test's code or
// behavior is changed by the other's existence; windows_platform.cpp,
// this file, src/vulkan_backend/src/wsi/win32_surface.cpp, and that GPU
// test file are the only files in the repository permitted to include
// <windows.h> or use Win32 window-management APIs, each strictly
// confined to its own #if defined(_WIN32) block. No Linux test, build
// configuration, or conditional branch is added anywhere in this file.
#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>

#include <cstddef>
#include <string_view>
#include <variant>
#include <vector>

namespace {

// Phase 1 does not support re-initializing Platform after shutdown()
// (docs/plans/0002-platform-foundation.md Section 6 / Unresolved
// Implementation Details #7), so every scenario below shares one
// initialize()->shutdown() lifecycle within a single TEST_CASE rather
// than being split into Catch2 SECTIONs, which would re-run the test body
// -- and therefore initialize() -- once per SECTION. This guard makes a
// best effort to still call shutdown() exactly once if a REQUIRE fails
// partway through, so a failing assertion doesn't leak the real window.
class PlatformLifecycleGuard {
 public:
  PlatformLifecycleGuard() = default;
  ~PlatformLifecycleGuard() {
    if (!shutDown_) {
      atlantis::platform::shutdown();
    }
  }

  PlatformLifecycleGuard(const PlatformLifecycleGuard&) = delete;
  PlatformLifecycleGuard& operator=(const PlatformLifecycleGuard&) = delete;

  void markShutDown() { shutDown_ = true; }

 private:
  bool shutDown_ = false;
};

}  // namespace

TEST_CASE("Windows Platform lifecycle: SurfaceCreated ordering, close, resize, minimize, focus, shutdown ordering",
          "[platform][integration]") {
  using atlantis::platform::FocusGained;
  using atlantis::platform::FocusLost;
  using atlantis::platform::Quit;
  using atlantis::platform::SurfaceCreated;
  using atlantis::platform::SurfaceDestroyed;
  using atlantis::platform::WindowCloseRequested;
  using atlantis::platform::WindowResize;

  const auto initResult = atlantis::platform::initialize();
  REQUIRE(initResult.isOk());

  // Constructed only after a successful initialize(), so its destructor
  // never calls shutdown() on a Platform that was never initialized.
  PlatformLifecycleGuard lifecycleGuard;

  // Initial batch: SurfaceCreated must be the first event observed, per
  // this task's explicit requirement -- asserted directly, not relaxed.
  // If window creation ever produced a WindowResize or other event ahead
  // of SurfaceCreated, this REQUIRE is meant to fail and surface that as
  // a real implementation defect, not be loosened to accommodate it.
  const auto initialBatch = atlantis::platform::processEvents();
  REQUIRE(initialBatch.size() >= 1);
  REQUIRE(std::holds_alternative<SurfaceCreated>(initialBatch[0]));

  const HWND hwnd = reinterpret_cast<HWND>(std::get<SurfaceCreated>(initialBatch[0]).handle.value0);
  REQUIRE(hwnd != nullptr);
  REQUIRE(IsWindow(hwnd));

  // 1. Request-not-destruction: WM_CLOSE only enqueues a request; the
  // window is not destroyed and shouldQuit() is unaffected.
  SendMessageW(hwnd, WM_CLOSE, 0, 0);
  {
    const auto events = atlantis::platform::processEvents();
    REQUIRE(events.size() == 1);
    REQUIRE(std::holds_alternative<WindowCloseRequested>(events[0]));
  }
  REQUIRE(IsWindow(hwnd));
  REQUIRE_FALSE(atlantis::platform::shouldQuit());

  // 2. Duplicate-request tolerance: two WM_CLOSE messages before draining
  // yield two WindowCloseRequested events, in order; not an error.
  SendMessageW(hwnd, WM_CLOSE, 0, 0);
  SendMessageW(hwnd, WM_CLOSE, 0, 0);
  {
    const auto events = atlantis::platform::processEvents();
    REQUIRE(events.size() == 2);
    REQUIRE(std::holds_alternative<WindowCloseRequested>(events[0]));
    REQUIRE(std::holds_alternative<WindowCloseRequested>(events[1]));
  }
  REQUIRE(IsWindow(hwnd));
  REQUIRE_FALSE(atlantis::platform::shouldQuit());

  // Regression: events raised via synchronous Win32 dispatch (e.g.
  // SendMessage) between two processEvents() calls must be preserved and
  // delivered, in order, on the *next* processEvents() call -- the caller
  // is not required to drain them as they happen. That a previously-
  // returned span stays valid and unchanged until the next
  // processEvents()/shutdown() call is a *structural* guarantee of the
  // output/pending buffer split above (windowProc only ever appends to
  // pendingBuffer; nothing but processEvents()/shutdown() ever touches
  // outputBuffer), not something this test re-verifies by deliberately
  // provoking a potential dangling span and then dereferencing it --
  // doing so would itself be undefined behavior against any
  // implementation that got the buffer split wrong, so this test never
  // reads a batch after triggering further events that could affect it.
  {
    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    SendMessageW(hwnd, WM_CLOSE, 0, 0);
    const auto firstBatch = atlantis::platform::processEvents();
    REQUIRE(firstBatch.size() == 2);
    REQUIRE(std::holds_alternative<WindowCloseRequested>(firstBatch[0]));
    REQUIRE(std::holds_alternative<WindowCloseRequested>(firstBatch[1]));

    // A burst well beyond any capacity this buffer has needed so far in
    // this test -- not to provoke and then re-read a stale span (see
    // above), but simply to demonstrate that a large number of events
    // queued between calls are all preserved and delivered, in order, on
    // the next processEvents() call. firstBatch is not touched again
    // after this point.
    constexpr int kBurstCount = 128;
    for (int i = 0; i < kBurstCount; ++i) {
      SendMessageW(hwnd, WM_CLOSE, 0, 0);
    }

    const auto burstBatch = atlantis::platform::processEvents();
    REQUIRE(burstBatch.size() == static_cast<std::size_t>(kBurstCount));
    for (const auto& event : burstBatch) {
      REQUIRE(std::holds_alternative<WindowCloseRequested>(event));
    }
  }
  REQUIRE(IsWindow(hwnd));
  REQUIRE_FALSE(atlantis::platform::shouldQuit());

  // 3. Resize: a program-driven SetWindowPos to a known non-zero size
  // must be observable as a WindowResize with matching, equal,
  // non-zero logical/framebuffer extents. Other untranslated messages may
  // share the same batch (Section 6 only maps WM_SIZE/WM_SETFOCUS/
  // WM_KILLFOCUS/WM_CLOSE to a PlatformEvent), so this only requires a
  // matching WindowResize to be present, not that it be the sole event.
  constexpr int kResizeWidth = 400;
  constexpr int kResizeHeight = 300;
  SetWindowPos(hwnd, nullptr, 0, 0, kResizeWidth, kResizeHeight, SWP_NOMOVE | SWP_NOZORDER);
  {
    const auto events = atlantis::platform::processEvents();
    bool foundResize = false;
    for (const auto& event : events) {
      if (const auto* resize = std::get_if<WindowResize>(&event)) {
        if (!resize->logical.isZero() && resize->logical == resize->framebuffer) {
          foundResize = true;
        }
      }
    }
    REQUIRE(foundResize);
  }

  // 4. Minimize: ShowWindow(SW_MINIMIZE) must be observable as
  // WindowResize{{0,0},{0,0}}.
  ShowWindow(hwnd, SW_MINIMIZE);
  {
    const auto events = atlantis::platform::processEvents();
    bool foundMinimize = false;
    for (const auto& event : events) {
      if (const auto* resize = std::get_if<WindowResize>(&event)) {
        if (resize->logical.isZero() && resize->framebuffer.isZero()) {
          foundMinimize = true;
        }
      }
    }
    REQUIRE(foundMinimize);
  }

  // Restore before the focus scenario below, and drain the restore's own
  // resize so it doesn't muddy the focus assertions that follow.
  ShowWindow(hwnd, SW_RESTORE);
  static_cast<void>(atlantis::platform::processEvents());

  // 5. Focus: synthesized via SendMessage rather than real desktop focus
  // changes, so this does not depend on interactive input, actual window
  // activation, or any timing-sensitive external behavior -- it must run
  // deterministically under automated ctest.
  SendMessageW(hwnd, WM_KILLFOCUS, 0, 0);
  {
    const auto events = atlantis::platform::processEvents();
    bool foundFocusLost = false;
    for (const auto& event : events) {
      if (std::holds_alternative<FocusLost>(event)) {
        foundFocusLost = true;
      }
    }
    REQUIRE(foundFocusLost);
  }

  SendMessageW(hwnd, WM_SETFOCUS, 0, 0);
  {
    const auto events = atlantis::platform::processEvents();
    bool foundFocusGained = false;
    for (const auto& event : events) {
      if (std::holds_alternative<FocusGained>(event)) {
        foundFocusGained = true;
      }
    }
    REQUIRE(foundFocusGained);
  }

  // The WM_SETFOCUS/WM_KILLFOCUS pair above was synthesized directly via
  // SendMessage and does not change the window's *real* OS focus state.
  // If the window still genuinely holds OS focus at this point (it does,
  // by default, as this process's only top-level window), DestroyWindow()
  // below would itself generate a real WM_KILLFOCUS as a side effect of
  // removing focus from the window being destroyed -- leaking an
  // unrelated extra FocusLost into the {SurfaceDestroyed, Quit} batch
  // scenario 6 requires to be exact. Explicitly move real focus away and
  // drain that (unasserted) event first so shutdown()'s own batch starts
  // clean.
  SetFocus(nullptr);
  static_cast<void>(atlantis::platform::processEvents());

  // 6. Close/destroy/quit ordering: called only after everything above
  // has been drained. shutdown() is the only DestroyWindow call in this
  // test -- it is never called directly here.
  atlantis::platform::shutdown();
  lifecycleGuard.markShutDown();

  {
    const auto events = atlantis::platform::processEvents();
    REQUIRE(events.size() == 2);
    REQUIRE(std::holds_alternative<SurfaceDestroyed>(events[0]));
    REQUIRE(std::holds_alternative<Quit>(events[1]));
  }
  REQUIRE(atlantis::platform::shouldQuit());

  {
    const auto events = atlantis::platform::processEvents();
    REQUIRE(events.empty());
  }
}

// Plan 0056 M2 (Spec 0056 Q9 / ADR-0109, P2): input messages, synthesized with
// SendMessageW (dispatched synchronously, so they precede anything the drain
// loop later pulls from the queue), arrive as plain-value input events in
// order. Keys outside the closed set and control characters produce nothing;
// a UTF-16 surrogate pair becomes one 4-byte code point. A real pointer over
// the window may add trailing PointerMoved events, which are tolerated.
TEST_CASE("Windows Platform input: pointer, button, wheel, key and text messages become input events in order",
          "[platform][integration][input]") {
  using atlantis::platform::Key;
  using atlantis::platform::KeyChanged;
  using atlantis::platform::PlatformEvent;
  using atlantis::platform::PointerButton;
  using atlantis::platform::PointerButtonChanged;
  using atlantis::platform::PointerMoved;
  using atlantis::platform::SurfaceCreated;
  using atlantis::platform::TextEntered;
  using atlantis::platform::WheelScrolled;

  REQUIRE(atlantis::platform::initialize().isOk());
  PlatformLifecycleGuard lifecycleGuard;
  const auto initialBatch = atlantis::platform::processEvents();
  REQUIRE(initialBatch.size() >= 1);
  REQUIRE(std::holds_alternative<SurfaceCreated>(initialBatch[0]));
  const HWND hwnd = reinterpret_cast<HWND>(std::get<SurfaceCreated>(initialBatch[0]).handle.value0);
  static_cast<void>(atlantis::platform::processEvents());

  const auto at = [](int x, int y) { return static_cast<LPARAM>(MAKELPARAM(x, y)); };
  SendMessageW(hwnd, WM_MOUSEMOVE, 0, at(10, 20));
  SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at(11, 21));
  SendMessageW(hwnd, WM_LBUTTONUP, 0, at(12, 22));
  SendMessageW(hwnd, WM_RBUTTONDOWN, MK_RBUTTON, at(13, 23));
  SendMessageW(hwnd, WM_RBUTTONUP, 0, at(13, 23));
  SendMessageW(hwnd, WM_MBUTTONUP, 0, at(14, 24));
  SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), at(0, 0));
  SendMessageW(hwnd, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-2 * WHEEL_DELTA)), at(0, 0));
  SendMessageW(hwnd, WM_KEYDOWN, 'W', 0);
  SendMessageW(hwnd, WM_KEYUP, VK_ESCAPE, 0);
  SendMessageW(hwnd, WM_KEYDOWN, VK_F5, 0);
  SendMessageW(hwnd, WM_KEYDOWN, VK_CONTROL, static_cast<LPARAM>(KF_EXTENDED) << 16);  // right Ctrl
  SendMessageW(hwnd, WM_KEYDOWN, VK_SNAPSHOT, 0);                                     // not in the set: nothing
  SendMessageW(hwnd, WM_CHAR, 'a', 0);
  SendMessageW(hwnd, WM_CHAR, 0x00E9, 0);  // e acute: 2 UTF-8 bytes
  SendMessageW(hwnd, WM_CHAR, 0x08, 0);    // Backspace as a character: nothing
  SendMessageW(hwnd, WM_CHAR, 0xD83D, 0);  // U+1F600, as a surrogate pair
  SendMessageW(hwnd, WM_CHAR, 0xDE00, 0);

  const auto events = atlantis::platform::processEvents();
  std::vector<PlatformEvent> input;
  for (const PlatformEvent& event : events) {
    if (event.index() >= 9) input.push_back(event);  // the input alternatives
  }
  REQUIRE(input.size() >= 15);

  const auto moved = [&](std::size_t i, float x, float y) {
    REQUIRE(std::holds_alternative<PointerMoved>(input[i]));
    CHECK(std::get<PointerMoved>(input[i]).x == x);
    CHECK(std::get<PointerMoved>(input[i]).y == y);
  };
  const auto button = [&](std::size_t i, PointerButton which, bool down, float x, float y) {
    REQUIRE(std::holds_alternative<PointerButtonChanged>(input[i]));
    const auto& e = std::get<PointerButtonChanged>(input[i]);
    CHECK(e.button == which);
    CHECK(e.down == down);
    CHECK(e.x == x);
    CHECK(e.y == y);
  };
  const auto key = [&](std::size_t i, Key which, bool down) {
    REQUIRE(std::holds_alternative<KeyChanged>(input[i]));
    CHECK(std::get<KeyChanged>(input[i]).key == which);
    CHECK(std::get<KeyChanged>(input[i]).down == down);
  };
  const auto text = [&](std::size_t i, std::string_view utf8) {
    REQUIRE(std::holds_alternative<TextEntered>(input[i]));
    const auto& e = std::get<TextEntered>(input[i]);
    CHECK(std::string_view(e.utf8.data(), e.size) == utf8);
  };

  moved(0, 10.0f, 20.0f);
  button(1, PointerButton::Left, true, 11.0f, 21.0f);
  button(2, PointerButton::Left, false, 12.0f, 22.0f);
  button(3, PointerButton::Right, true, 13.0f, 23.0f);
  button(4, PointerButton::Right, false, 13.0f, 23.0f);
  button(5, PointerButton::Middle, false, 14.0f, 24.0f);
  REQUIRE(std::holds_alternative<WheelScrolled>(input[6]));
  CHECK(std::get<WheelScrolled>(input[6]).dx == 0.0f);
  CHECK(std::get<WheelScrolled>(input[6]).dy == 1.0f);
  REQUIRE(std::holds_alternative<WheelScrolled>(input[7]));
  CHECK(std::get<WheelScrolled>(input[7]).dx == -2.0f);
  CHECK(std::get<WheelScrolled>(input[7]).dy == 0.0f);
  key(8, Key::W, true);
  key(9, Key::Escape, false);
  key(10, Key::F5, true);
  key(11, Key::RightCtrl, true);
  text(12, "a");
  text(13, "\xC3\xA9");
  text(14, "\xF0\x9F\x98\x80");
  for (std::size_t i = 15; i < input.size(); ++i) CHECK(std::holds_alternative<PointerMoved>(input[i]));
}

#endif  // defined(_WIN32)
