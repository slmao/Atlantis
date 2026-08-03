#pragma once

#include <atlantis/platform/platform_event.h>
#include <atlantis/platform/platform_kind.h>
#include <atlantis/result.h>

#include <span>
#include <variant>

namespace atlantis::platform {

// See specs/0002-platform-foundation.md Error Handling and ADR-0009.
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
// plans/0002-platform-foundation.md's implementation notes for this
// task. No change to Result's own decision or shape.
[[nodiscard]] atlantis::Result<std::monostate, PlatformError> initialize();

// Drains and returns this call's events. The returned span is valid only
// until the next call to processEvents() or shutdown() — see ADR-0012
// and plans/0002-platform-foundation.md Section 4. Callers must copy out
// anything they need to retain longer.
[[nodiscard]] std::span<const PlatformEvent> processEvents();

[[nodiscard]] bool shouldQuit();

void shutdown();

[[nodiscard]] PlatformKind currentPlatform();

}  // namespace atlantis::platform
