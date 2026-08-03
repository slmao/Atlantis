#include <atlantis/platform/platform.h>

#include <catch2/catch_test_macros.hpp>

// Task 2.1: confirms the public Platform API compiles and links against
// Atlantis::Platform. Deliberately does not assert the stub's specific
// return values as a real requirement — that behavior is replaced by a
// real Windows implementation in Plan 0002 Task 2.2, per
// plans/0002-platform-foundation.md.
TEST_CASE("Platform public API is callable", "[platform][compile]") {
  const auto kind = atlantis::platform::currentPlatform();
  REQUIRE((kind == atlantis::platform::PlatformKind::Windows ||
           kind == atlantis::platform::PlatformKind::Android ||
           kind == atlantis::platform::PlatformKind::IOS));

  auto initResult = atlantis::platform::initialize();
  (void)initResult;  // Task 2.1 stub value intentionally not asserted here

  const auto events = atlantis::platform::processEvents();
  (void)events;

  (void)atlantis::platform::shouldQuit();
  atlantis::platform::shutdown();
}
