#include "android_app_injection.h"

#include <atlantis/assert.h>

namespace atlantis::platform::android_detail {

namespace {

// File-local, process-wide storage -- mirrors windows_platform.cpp's own
// single-instance State pattern (Phase 1 has exactly one Platform
// instance per process on every OS). android_app itself is never
// dereferenced in this translation unit, only stored/returned as a
// pointer, so this file needs no Android NDK header and compiles on any
// host.
android_app*& storedApp() {
  static android_app* instance = nullptr;
  return instance;
}

}  // namespace

void setAndroidApp(android_app* app) { storedApp() = app; }

android_app* androidApp() {
  ATLANTIS_CHECK_MSG(storedApp() != nullptr,
                      "atlantis::platform's Android backend was accessed before "
                      "atlantis_runtime_android's android_main called setAndroidApp() "
                      "(ADR-0077 amendment) -- this is a programmer error, not a "
                      "recoverable PlatformError.");
  return storedApp();
}

}  // namespace atlantis::platform::android_detail
