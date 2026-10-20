#pragma once

#include <atlantis/platform/native_window_handle.h>
#include <atlantis/platform/platform_event.h>
#include <atlantis/platform/platform_kind.h>
#include <atlantis/result.h>

#include <optional>
#include <span>
#include <variant>

namespace atlantis::platform {

// See docs/specs/0002-platform-foundation.md Error Handling and ADR-0009.
enum class PlatformErrorCode {
  WindowClassRegistrationFailed,
  WindowCreationFailed,
};

struct PlatformError {
  PlatformErrorCode code;
  unsigned long nativeErrorCode = 0;  // e.g. GetLastError(); a plain integer, no OS header needed
};

// atlantis::Result<T, E> (see atlantis/result.h) is std::variant-backed,
// and std::variant cannot hold `void`. std::monostate is the standard
// idiom for an empty-but-valid success payload; see
// docs/plans/0002-platform-foundation.md's implementation notes for this
// task. No change to Result's own decision or shape.
[[nodiscard]] atlantis::Result<std::monostate, PlatformError> initialize();

// Spec 0059 / ADR-0117 D2: a client (drawable) size in pixels.
struct ClientSize {
  unsigned int width = 0;
  unsigned int height = 0;
};

// Spec 0059 / ADR-0117 D2: options for the window initialize() creates. Empty
// (the default) means the OS-chosen size, exactly as initialize() always did.
struct WindowOptions {
  // The window's initial client size, e.g. for a fixed-resolution benchmark.
  // Windows creates the window to this client size (and lets it exceed the
  // monitor); Android ignores it -- its surface is the system's. The actual size
  // arrives as the first WindowResize event, as ever; a caller that needs the
  // exact size checks it there.
  std::optional<ClientSize> requestedClientSize;
  // Spec 0064 / ADR-0126 D1: a foreign window to create this window inside, as
  // a child filling its client area (Windows only; Android ignores it). The
  // parent is borrowed: Platform never destroys it. The child is Platform's,
  // as ever (ADR-0013): created here, destroyed by shutdown(). It does not
  // take activation on click and re-posts its pointer, wheel and key messages
  // to the parent (coordinates in the parent's client space) after emitting
  // its own input events, so the host's toolkit sees them too (ADR-0126 D4,
  // route R-a). When the parent goes away, WindowCloseRequested is emitted. A
  // parent that is not a live window fails initialize() with
  // WindowCreationFailed. requestedClientSize is ignored with a parent.
  std::optional<NativeWindowHandle> parent;
};

// initialize() above is initialize(WindowOptions{}).
[[nodiscard]] atlantis::Result<std::monostate, PlatformError> initialize(const WindowOptions& options);

// Drains and returns this call's events. The returned span is valid only
// until the next call to processEvents() or shutdown() — see ADR-0012
// and docs/plans/0002-platform-foundation.md Section 4. Callers must copy out
// anything they need to retain longer.
[[nodiscard]] std::span<const PlatformEvent> processEvents();

[[nodiscard]] bool shouldQuit();

void shutdown();

[[nodiscard]] PlatformKind currentPlatform();

}  // namespace atlantis::platform
