#pragma once

#include <atlantis/platform/native_window_handle.h>

#include <variant>

namespace atlantis::platform {

// See specs/0002-platform-foundation.md Window Extent. logical and
// framebuffer are independent fields — not assumed equal — even though
// the Windows implementation reports them equal in Phase 1 (see
// plans/0002-platform-foundation.md Section 7).
struct WindowExtent {
  unsigned int width = 0;
  unsigned int height = 0;
  [[nodiscard]] bool isZero() const { return width == 0 && height == 0; }
};

[[nodiscard]] bool operator==(const WindowExtent& lhs, const WindowExtent& rhs);
[[nodiscard]] inline bool operator!=(const WindowExtent& lhs, const WindowExtent& rhs) {
  return !(lhs == rhs);
}

// The PlatformEvent set — see ADR-0012. Closed and minimal by design: no
// input events (keyboard/mouse/touch/controller/gesture) are part of
// this set.
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

// std::variant chosen over a polymorphic event base, consistent with
// atlantis::Result's existing value-type style — see ADR-0011's Open
// Questions and plans/0002-platform-foundation.md Section 2.
using PlatformEvent = std::variant<WindowResize, WindowCloseRequested, FocusGained, FocusLost,
                                    ApplicationPause, ApplicationResume, SurfaceCreated,
                                    SurfaceDestroyed, Quit>;

}  // namespace atlantis::platform
