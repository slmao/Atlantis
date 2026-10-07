#pragma once

#include <atlantis/platform/native_window_handle.h>

#include <array>
#include <cstdint>
#include <variant>

namespace atlantis::platform {

// See docs/specs/0002-platform-foundation.md Window Extent. logical and
// framebuffer are independent fields — not assumed equal — even though
// the Windows implementation reports them equal in Phase 1 (see
// docs/plans/0002-platform-foundation.md Section 7).
struct WindowExtent {
  unsigned int width = 0;
  unsigned int height = 0;
  [[nodiscard]] bool isZero() const { return width == 0 && height == 0; }
};

[[nodiscard]] bool operator==(const WindowExtent& lhs, const WindowExtent& rhs);
[[nodiscard]] inline bool operator!=(const WindowExtent& lhs, const WindowExtent& rhs) {
  return !(lhs == rhs);
}

// The PlatformEvent set — see ADR-0012. Closed and minimal by design. Spec
// 0056 / ADR-0109 adds pointer, wheel, key and text input below; touch,
// controller and gesture input are not part of this set.
struct WindowResize {
  WindowExtent logical;
  WindowExtent framebuffer;
};
struct WindowCloseRequested {};
struct FocusGained {};
struct FocusLost {};
struct ApplicationPause {};
struct ApplicationResume {};
struct SurfaceCreated {
  NativeWindowHandle handle;
};
struct SurfaceDestroyed {};
struct Quit {};

// Spec 0056 / ADR-0109 (Plan 0056 P2): input events, plain values, no OS
// type. Emitted by Windows Platform from its message pump in the order
// received; Android Platform emits none in v1. Positions are framebuffer
// pixels relative to the window's client area.
enum class PointerButton : std::uint8_t { Left, Right, Middle };

// A closed, Platform-defined key set: keys outside it are not reported.
// Atlantis Editor's own key set mirrors this one in the same order.
enum class Key : std::uint16_t {
  Tab,
  LeftArrow,
  RightArrow,
  UpArrow,
  DownArrow,
  Home,
  End,
  PageUp,
  PageDown,
  Insert,
  Delete,
  Backspace,
  Space,
  Enter,
  Escape,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  LeftCtrl,
  LeftShift,
  LeftAlt,
  RightCtrl,
  RightShift,
  RightAlt,
};

// The modifier state when a key event was generated (either side counts).
struct KeyModifiers {
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
};
struct PointerMoved {
  float x = 0.0f;
  float y = 0.0f;
};
struct PointerButtonChanged {
  PointerButton button = PointerButton::Left;
  bool down = false;
  float x = 0.0f;
  float y = 0.0f;
};
// In wheel notches (one detent = 1.0); positive dy scrolls up (away from the
// user), positive dx to the right.
struct WheelScrolled {
  float dx = 0.0f;
  float dy = 0.0f;
};
struct KeyChanged {
  Key key = Key::Tab;
  bool down = false;
  KeyModifiers modifiers;
};
// One Unicode code point, UTF-8 encoded in utf8[0, size).
struct TextEntered {
  std::array<char, 8> utf8{};
  std::uint8_t size = 0;
};

// std::variant chosen over a polymorphic event base, consistent with
// atlantis::Result's existing value-type style — see ADR-0011's Open
// Questions and docs/plans/0002-platform-foundation.md Section 2.
using PlatformEvent = std::variant<WindowResize, WindowCloseRequested, FocusGained, FocusLost,
                                    ApplicationPause, ApplicationResume, SurfaceCreated,
                                    SurfaceDestroyed, Quit, PointerMoved, PointerButtonChanged,
                                    WheelScrolled, KeyChanged, TextEntered>;

}  // namespace atlantis::platform
