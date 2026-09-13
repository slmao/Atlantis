#pragma once

#include "app_command.h"

#include <atlantis/platform/platform_event.h>

#include <optional>

namespace atlantis::platform::android_detail {

// Extra data an AppCommand may carry, already extracted into primitives
// by the caller (android_platform.cpp, the only file that ever touches
// a real android_app*/ANativeWindow*) so this translation logic itself
// stays free of any Android NDK header and is unit-testable on a host
// machine with no NDK installed at all.
struct AppCommandContext {
  // Meaningful only for AppCommand::InitWindow: the raw ANativeWindow*
  // this command carries, reinterpreted as an opaque pointer per
  // NativeWindowHandle's own existing value0 convention (ADR-0011).
  // nullptr for every other command.
  void* nativeWindow = nullptr;
  // Meaningful only for AppCommand::WindowResized/ContentRectChanged:
  // the window's current pixel extent, already read by the caller via
  // ANativeWindow_getWidth()/getHeight() -- Android reports no separate
  // logical-vs-framebuffer distinction in Phase 1 (ADR-0077), so both
  // WindowResize fields this maps to carry the same value. {0, 0} for
  // every other command.
  unsigned int width = 0;
  unsigned int height = 0;
};

// Pure translation logic for ADR-0077's own APP_CMD_* -> PlatformEvent
// mapping table -- no Android headers, host-testable. Returns
// std::nullopt for a command with no PlatformEvent mapping: ADR-0077's
// own "observed and discarded" row (AppCommand::InputChanged,
// ConfigChanged, LowMemory, SaveState, Start, Stop) plus
// AppCommand::WindowRedrawNeeded, which that row's own table omitted
// but whose "no state change Atlantis's closed PlatformEvent set
// represents" rationale applies identically to (a completeness
// correction found at implementation time, not a new decision -- see
// this Spec's implementing PR).
[[nodiscard]] std::optional<atlantis::platform::PlatformEvent> mapAppCommand(AppCommand command,
                                                                              const AppCommandContext& context);

}  // namespace atlantis::platform::android_detail
