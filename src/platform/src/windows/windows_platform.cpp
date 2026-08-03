// Task 2.1 stub: public API wiring only. No Win32 API is used here yet —
// window creation and message handling are Plan 0002 Task 2.2, per
// plans/0002-platform-foundation.md Sections 6-8. This file intentionally
// includes no OS header.
#include <atlantis/platform/platform.h>

namespace atlantis::platform {

atlantis::Result<std::monostate, PlatformError> initialize() {
  // Placeholder: nothing is actually initialized yet, so this honestly
  // reports failure rather than falsely claiming success. Replaced with
  // real window-class/HWND creation in Task 2.2.
  return atlantis::Result<std::monostate, PlatformError>::Err(
      PlatformError{PlatformErrorCode::WindowCreationFailed, 0});
}

std::span<const PlatformEvent> processEvents() {
  return {};
}

bool shouldQuit() {
  return true;
}

void shutdown() {
}

PlatformKind currentPlatform() {
  return PlatformKind::Windows;
}

}  // namespace atlantis::platform
