#include <atlantis/platform/platform.h>

#include <catch2/catch_test_macros.hpp>

// Task 2.3: Platform's Phase 1 lifecycle does not support
// re-initialization (plans/0002-platform-foundation.md Section 6 /
// Unresolved Implementation Details #7), and Windows Platform's state is
// process-wide, file-local state. Actually calling initialize()/
// shutdown() here would contend with windows_platform_smoke_tests.cpp's
// own single real lifecycle over that same state, depending on Catch2's
// test execution order. This test therefore only confirms the public API
// compiles and links against Atlantis::Platform -- it never calls
// initialize(), processEvents(), shouldQuit(), or shutdown(), and creates
// no window. The real Windows lifecycle is exercised exactly once, start
// to finish, by windows_platform_smoke_tests.cpp.
TEST_CASE("Platform public API compiles and links", "[platform][compile]") {
  const auto kind = atlantis::platform::currentPlatform();
  REQUIRE((kind == atlantis::platform::PlatformKind::Windows ||
           kind == atlantis::platform::PlatformKind::Android ||
           kind == atlantis::platform::PlatformKind::IOS));

  // Taking each function's address (without calling it) proves the
  // declared interface exists and is linkable, without touching any
  // file-local Platform state.
  using InitializeFn = atlantis::Result<std::monostate, atlantis::platform::PlatformError> (*)();
  using ProcessEventsFn = std::span<const atlantis::platform::PlatformEvent> (*)();
  using ShouldQuitFn = bool (*)();
  using ShutdownFn = void (*)();

  [[maybe_unused]] InitializeFn initializeFn = &atlantis::platform::initialize;
  [[maybe_unused]] ProcessEventsFn processEventsFn = &atlantis::platform::processEvents;
  [[maybe_unused]] ShouldQuitFn shouldQuitFn = &atlantis::platform::shouldQuit;
  [[maybe_unused]] ShutdownFn shutdownFn = &atlantis::platform::shutdown;
}
