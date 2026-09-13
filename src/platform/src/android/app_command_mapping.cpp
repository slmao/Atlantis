#include "app_command_mapping.h"

namespace atlantis::platform::android_detail {

std::optional<atlantis::platform::PlatformEvent> mapAppCommand(AppCommand command, const AppCommandContext& context) {
  // Exhaustive, no-default switch -- this repository's own -Wswitch
  // (enabled under -Wall, promoted to a hard error by -Werror on every
  // Android build) already gives real, compiler-enforced protection
  // against a missed AppCommand case, the same exhaustiveness guarantee
  // this codebase's MSVC-side switches get from /w14062 (Plan 0013
  // Section D3) -- no equivalent extra flag needed here since Clang's
  // -Wswitch fires on an incomplete enum-class switch by default.
  switch (command) {
    case AppCommand::InitWindow:
      // ADR-0079: context.nativeWindow is already the new, valid
      // ANativeWindow* by the time this command's onAppCmd callback
      // fires (android_native_app_glue's own android_app_pre_exec_cmd()
      // sets android_app->window before invoking onAppCmd) -- stored
      // verbatim as NativeWindowHandle::value0, no acquire/release.
      return atlantis::platform::PlatformEvent{atlantis::platform::SurfaceCreated{
          atlantis::platform::NativeWindowHandle{atlantis::platform::PlatformKind::Android, context.nativeWindow,
                                                  nullptr}}};
    case AppCommand::TermWindow:
      // ADR-0077: fired before the glue library invalidates
      // android_app->window (android_app_post_exec_cmd() clears it only
      // after onAppCmd returns) -- this event carries no payload, so
      // that ordering has no bearing on this translation itself; it
      // matters to android_platform.cpp's own Presentation-teardown
      // timing (ADR-0079's Ordering discipline), not to this mapping.
      return atlantis::platform::PlatformEvent{atlantis::platform::SurfaceDestroyed{}};
    case AppCommand::WindowResized:
    case AppCommand::ContentRectChanged:
      return atlantis::platform::PlatformEvent{atlantis::platform::WindowResize{
          atlantis::platform::WindowExtent{context.width, context.height},
          atlantis::platform::WindowExtent{context.width, context.height}}};
    case AppCommand::GainedFocus:
      return atlantis::platform::PlatformEvent{atlantis::platform::FocusGained{}};
    case AppCommand::LostFocus:
      return atlantis::platform::PlatformEvent{atlantis::platform::FocusLost{}};
    case AppCommand::Pause:
      return atlantis::platform::PlatformEvent{atlantis::platform::ApplicationPause{}};
    case AppCommand::Resume:
      return atlantis::platform::PlatformEvent{atlantis::platform::ApplicationResume{}};
    case AppCommand::Destroy:
      return atlantis::platform::PlatformEvent{atlantis::platform::Quit{}};
    case AppCommand::InputChanged:
    case AppCommand::WindowRedrawNeeded:
    case AppCommand::ConfigChanged:
    case AppCommand::LowMemory:
    case AppCommand::Start:
    case AppCommand::SaveState:
    case AppCommand::Stop:
      // ADR-0077's own "observed and discarded" row: input, a redraw
      // request, configuration change beyond resize, low-memory
      // handling, and state save/restore are all explicitly Non-Goals
      // of Spec 0002/Spec 0034 -- no PlatformEvent mapping exists for
      // any of them, kept forward-compatible rather than asserted on.
      return std::nullopt;
  }
  return std::nullopt;
}

}  // namespace atlantis::platform::android_detail
