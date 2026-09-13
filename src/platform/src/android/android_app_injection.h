#pragma once

// Opaque forward declaration only -- this header needs no Android NDK
// header at all (setAndroidApp()/androidApp() only ever store and
// return the pointer, never dereference it), so it stays host-testable
// on a machine with no NDK installed. android_platform.cpp (the real
// implementation, which does include the NDK's own
// android_native_app_glue.h) sees the exact same type here, since a
// forward declaration and the NDK's full struct definition name one and
// the same type -- not two incompatible ones.
struct android_app;

namespace atlantis::platform::android_detail {

// ADR-0077's own "android_app* injection" amendment: atlantis::platform's
// public initialize()/processEvents()/etc. take no android_app*
// parameter, and Android Platform has no other way to reach the
// android_app* atlantis_runtime_android's own android_main received from
// the NDK entry point. Borrowed, not owned -- Android Platform never
// outlives or destroys it, and never calls this a second time with a
// different pointer within one process lifetime (Plan 0034 Milestone 5's
// own responsibility to uphold, not enforced here).
//
// Cross-module visibility (Plan 0034 Milestone 5): this header lives
// under src/platform/src/android/, per ADR-0077's own instruction that
// it never reach src/platform/include/ -- ADR-0010's include-path
// hygiene rule would otherwise keep it structurally absent from every
// consumer's include path, including atlantis_runtime_android's
// android_main.cpp (a different top-level module, Atlantis Runtime).
// Resolved via a narrow, single-consumer extension point:
// src/platform/CMakeLists.txt declares an INTERFACE target,
// atlantis_platform_android_entry (Atlantis::PlatformAndroidEntry),
// under if(ANDROID), granting include-path visibility into this
// directory to whichever target links it -- mirroring the whitebox-test
// pattern tests/platform/CMakeLists.txt already uses for this same
// directory, but as a real build-graph dependency edge rather than a
// test-only grant.
void setAndroidApp(android_app* app);

// Returns the pointer set by setAndroidApp(), or triggers
// ATLANTIS_CHECK_MSG (a programmer error, not a recoverable
// PlatformError) if it was never called. Every real caller in this
// module (initialize(), processEvents()) goes through this single
// accessor, so the guard is enforced exactly once rather than
// duplicated at each call site.
[[nodiscard]] android_app* androidApp();

}  // namespace atlantis::platform::android_detail
