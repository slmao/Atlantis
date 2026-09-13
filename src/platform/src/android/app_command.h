#pragma once

#include <cstdint>

namespace atlantis::platform::android_detail {

// Mirrors the Android NDK's own android_native_app_glue.h APP_CMD_*
// enum (a plain, unqualified C `enum { ... }` -- these are implicit,
// sequential int32_t values starting at 0, confirmed against the real
// header shipped with NDK 29.0.14206865:
// $NDK/sources/android/native_app_glue/android_native_app_glue.h). This
// module-private, NDK-header-free mirror exists so ADR-0077's own
// APP_CMD_* -> PlatformEvent mapping table (app_command_mapping.h) can
// be unit-tested on a host machine with no Android NDK installed at
// all -- android_platform.cpp (the only file that ever sees a real
// android_native_app_glue.h) `static_assert`s these values against the
// real APP_CMD_* constants, so a future NDK renumbering these (unlikely
// -- it is a long-published, stable ABI, but not something this file
// can enforce on its own) would fail that build rather than silently
// mismap.
enum class AppCommand : std::int32_t {
  InputChanged = 0,       // APP_CMD_INPUT_CHANGED
  InitWindow = 1,         // APP_CMD_INIT_WINDOW
  TermWindow = 2,         // APP_CMD_TERM_WINDOW
  WindowResized = 3,      // APP_CMD_WINDOW_RESIZED
  WindowRedrawNeeded = 4,  // APP_CMD_WINDOW_REDRAW_NEEDED
  ContentRectChanged = 5,  // APP_CMD_CONTENT_RECT_CHANGED
  GainedFocus = 6,        // APP_CMD_GAINED_FOCUS
  LostFocus = 7,          // APP_CMD_LOST_FOCUS
  ConfigChanged = 8,      // APP_CMD_CONFIG_CHANGED
  LowMemory = 9,          // APP_CMD_LOW_MEMORY
  Start = 10,             // APP_CMD_START
  Resume = 11,            // APP_CMD_RESUME
  SaveState = 12,         // APP_CMD_SAVE_STATE
  Pause = 13,             // APP_CMD_PAUSE
  Stop = 14,              // APP_CMD_STOP
  Destroy = 15,           // APP_CMD_DESTROY
};

}  // namespace atlantis::platform::android_detail
