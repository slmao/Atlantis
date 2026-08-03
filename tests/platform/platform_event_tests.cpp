#include <atlantis/platform/platform_event.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using atlantis::platform::ApplicationPause;
using atlantis::platform::FocusGained;
using atlantis::platform::PlatformEvent;
using atlantis::platform::Quit;
using atlantis::platform::WindowCloseRequested;
using atlantis::platform::WindowExtent;
using atlantis::platform::WindowResize;

TEST_CASE("WindowExtent detects the zero state", "[platform][window_extent]") {
  REQUIRE(WindowExtent{}.isZero());
  REQUIRE(WindowExtent{0, 0}.isZero());
  REQUIRE_FALSE(WindowExtent{1, 0}.isZero());
  REQUIRE_FALSE(WindowExtent{0, 1}.isZero());
  REQUIRE_FALSE(WindowExtent{800, 600}.isZero());
}

TEST_CASE("WindowExtent equality and inequality", "[platform][window_extent]") {
  REQUIRE(WindowExtent{800, 600} == WindowExtent{800, 600});
  REQUIRE(WindowExtent{800, 600} != WindowExtent{640, 480});
}

TEST_CASE("PlatformEvent alternatives construct and are retrievable", "[platform][platform_event]") {
  const PlatformEvent resize = WindowResize{WindowExtent{800, 600}, WindowExtent{800, 600}};
  REQUIRE(std::holds_alternative<WindowResize>(resize));
  REQUIRE(std::get<WindowResize>(resize).logical == WindowExtent{800, 600});
  REQUIRE(std::get<WindowResize>(resize).logical == std::get<WindowResize>(resize).framebuffer);

  const PlatformEvent quit = Quit{};
  REQUIRE(std::holds_alternative<Quit>(quit));
  REQUIRE_FALSE(std::holds_alternative<WindowResize>(quit));
}

TEST_CASE("A sequence of PlatformEvents preserves insertion order", "[platform][platform_event]") {
  std::vector<PlatformEvent> events;
  events.emplace_back(FocusGained{});
  events.emplace_back(WindowCloseRequested{});
  events.emplace_back(Quit{});

  REQUIRE(events.size() == 3);
  REQUIRE(std::holds_alternative<FocusGained>(events[0]));
  REQUIRE(std::holds_alternative<WindowCloseRequested>(events[1]));
  REQUIRE(std::holds_alternative<Quit>(events[2]));
}
