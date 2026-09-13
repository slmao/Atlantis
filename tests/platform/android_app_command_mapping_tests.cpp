// Whitebox unit tests for src/platform/src/android/'s host-testable
// translation logic (app_command_mapping.h, android_app_injection.h) --
// no Android NDK header is needed to build or run this file on any
// host, per Spec 0034/Plan 0034 Milestone 2's own Testing &
// Verification requirement. tests/platform/CMakeLists.txt grants this
// target visibility into src/platform/src (mirroring
// tests/vulkan_backend/CMakeLists.txt's identical, already-established
// whitebox pattern for that module's own src/ headers).
#include <android_app_injection.h>
#include <app_command.h>
#include <app_command_mapping.h>

#include <atlantis/assert.h>
#include <atlantis/platform/native_window_handle.h>
#include <atlantis/platform/platform_event.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using atlantis::platform::FocusGained;
using atlantis::platform::FocusLost;
using atlantis::platform::NativeWindowHandle;
using atlantis::platform::PlatformKind;
using atlantis::platform::SurfaceCreated;
using atlantis::platform::SurfaceDestroyed;
using atlantis::platform::WindowResize;
using atlantis::platform::android_detail::androidApp;
using atlantis::platform::android_detail::AppCommand;
using atlantis::platform::android_detail::AppCommandContext;
using atlantis::platform::android_detail::mapAppCommand;
using atlantis::platform::android_detail::setAndroidApp;

namespace {

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Mirrors tests/render_graph/dependency_derivation_tests.cpp's own
// ScopedFailureHandler exactly -- intercepts ATLANTIS_CHECK/ASSERT
// failures for the duration of one test case instead of letting the
// default handler abort the process, restoring the previous handler on
// scope exit.
class ScopedFailureHandler {
 public:
  explicit ScopedFailureHandler(std::vector<RecordedFailure>& recorded)
      : previous_(atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
          recorded.push_back({std::string(info.expression), std::string(info.message)});
        })) {}

  ~ScopedFailureHandler() { atlantis::assertions::setFailureHandler(std::move(previous_)); }

  ScopedFailureHandler(const ScopedFailureHandler&) = delete;
  ScopedFailureHandler& operator=(const ScopedFailureHandler&) = delete;

 private:
  atlantis::AssertFailureHandler previous_;
};

// setAndroidApp()/androidApp() never dereference the stored pointer
// (see android_app_injection.h's own contract) -- an arbitrary non-null
// sentinel is therefore a safe stand-in for a real android_app* in every
// test below that only needs "injected" vs. "not injected", never a real
// android_app instance (which android_app's own forward-declared,
// incomplete type makes impossible to construct on a host anyway).
android_app* fakeAndroidApp() { return reinterpret_cast<android_app*>(0x1); }

// Resets the module-private injection state to "not injected" on
// construction and again on destruction, so each TEST_CASE below starts
// and ends from the same clean slate regardless of Catch2's own test
// execution order -- this file is the only consumer of
// android_app_injection.h's setAndroidApp() in this repository outside
// the real android_platform.cpp, so there is no other test to leak
// state into, but this guard makes that property structural rather than
// incidental.
class ScopedAndroidAppReset {
 public:
  ScopedAndroidAppReset() { setAndroidApp(nullptr); }
  ~ScopedAndroidAppReset() { setAndroidApp(nullptr); }

  ScopedAndroidAppReset(const ScopedAndroidAppReset&) = delete;
  ScopedAndroidAppReset& operator=(const ScopedAndroidAppReset&) = delete;
};

}  // namespace

TEST_CASE("AppCommand::InitWindow maps to SurfaceCreated carrying the given window pointer, kind Android",
          "[platform][android][app_command_mapping]") {
  int sentinel = 0;
  AppCommandContext context;
  context.nativeWindow = &sentinel;

  const auto mapped = mapAppCommand(AppCommand::InitWindow, context);
  REQUIRE(mapped.has_value());
  REQUIRE(std::holds_alternative<SurfaceCreated>(*mapped));
  const NativeWindowHandle& handle = std::get<SurfaceCreated>(*mapped).handle;
  CHECK(handle.kind == PlatformKind::Android);
  CHECK(handle.value0 == &sentinel);
  CHECK(handle.value1 == nullptr);
}

TEST_CASE("AppCommand::TermWindow maps to SurfaceDestroyed", "[platform][android][app_command_mapping]") {
  const auto mapped = mapAppCommand(AppCommand::TermWindow, AppCommandContext{});
  REQUIRE(mapped.has_value());
  CHECK(std::holds_alternative<SurfaceDestroyed>(*mapped));
}

TEST_CASE("AppCommand::WindowResized and ContentRectChanged both map to WindowResize with equal logical/framebuffer "
          "extents",
          "[platform][android][app_command_mapping]") {
  AppCommandContext context;
  context.width = 1080;
  context.height = 2400;

  for (const AppCommand command : {AppCommand::WindowResized, AppCommand::ContentRectChanged}) {
    const auto mapped = mapAppCommand(command, context);
    REQUIRE(mapped.has_value());
    REQUIRE(std::holds_alternative<WindowResize>(*mapped));
    const WindowResize& resize = std::get<WindowResize>(*mapped);
    CHECK(resize.logical.width == 1080);
    CHECK(resize.logical.height == 2400);
    CHECK(resize.framebuffer.width == 1080);
    CHECK(resize.framebuffer.height == 2400);
    CHECK(resize.logical == resize.framebuffer);
  }
}

TEST_CASE("AppCommand::GainedFocus/LostFocus/Pause/Resume/Destroy each map to their own single-field PlatformEvent",
          "[platform][android][app_command_mapping]") {
  CHECK(std::holds_alternative<FocusGained>(*mapAppCommand(AppCommand::GainedFocus, AppCommandContext{})));
  CHECK(std::holds_alternative<FocusLost>(*mapAppCommand(AppCommand::LostFocus, AppCommandContext{})));
  CHECK(std::holds_alternative<atlantis::platform::ApplicationPause>(
      *mapAppCommand(AppCommand::Pause, AppCommandContext{})));
  CHECK(std::holds_alternative<atlantis::platform::ApplicationResume>(
      *mapAppCommand(AppCommand::Resume, AppCommandContext{})));
  CHECK(std::holds_alternative<atlantis::platform::Quit>(*mapAppCommand(AppCommand::Destroy, AppCommandContext{})));
}

TEST_CASE("Every command in ADR-0077's own \"observed and discarded\" row, plus WindowRedrawNeeded, maps to "
          "std::nullopt",
          "[platform][android][app_command_mapping]") {
  for (const AppCommand command : {AppCommand::InputChanged, AppCommand::WindowRedrawNeeded,
                                    AppCommand::ConfigChanged, AppCommand::LowMemory, AppCommand::Start,
                                    AppCommand::SaveState, AppCommand::Stop}) {
    CHECK_FALSE(mapAppCommand(command, AppCommandContext{}).has_value());
  }
}

TEST_CASE("androidApp() before any setAndroidApp() call is a programmer error (assertion failure), not a crash",
          "[platform][android][app_command_mapping]") {
  ScopedAndroidAppReset resetGuard;
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler failureGuard(recorded);

  const android_app* result = androidApp();

  REQUIRE(recorded.size() == 1);
  CHECK(recorded[0].expression == "storedApp() != nullptr");
  CHECK(result == nullptr);
}

TEST_CASE("setAndroidApp() followed by androidApp() returns the exact injected pointer, no assertion failure",
          "[platform][android][app_command_mapping]") {
  ScopedAndroidAppReset resetGuard;
  std::vector<RecordedFailure> recorded;
  ScopedFailureHandler failureGuard(recorded);

  android_app* const injected = fakeAndroidApp();
  setAndroidApp(injected);

  CHECK(androidApp() == injected);
  CHECK(recorded.empty());
}
