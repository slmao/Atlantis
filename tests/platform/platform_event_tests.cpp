#include <atlantis/platform/platform_event.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include "app_command_mapping.h"

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

// Plan 0056 M2 (Spec 0056 Q9 / ADR-0109, P2): the input events are plain
// values carried by the same variant, appended after the window and lifecycle
// events so every existing alternative keeps its index.
TEST_CASE("PlatformEvent input alternatives are plain values", "[platform][platform_event][input]") {
  using atlantis::platform::Key;
  using atlantis::platform::KeyChanged;
  using atlantis::platform::KeyModifiers;
  using atlantis::platform::PointerButton;
  using atlantis::platform::PointerButtonChanged;
  using atlantis::platform::PointerMoved;
  using atlantis::platform::TextEntered;
  using atlantis::platform::WheelScrolled;

  const PlatformEvent moved = PointerMoved{12.0f, 34.0f};
  REQUIRE(std::holds_alternative<PointerMoved>(moved));
  CHECK(std::get<PointerMoved>(moved).x == 12.0f);
  CHECK(std::get<PointerMoved>(moved).y == 34.0f);

  const PlatformEvent button = PointerButtonChanged{PointerButton::Right, true, 5.0f, 6.0f};
  REQUIRE(std::holds_alternative<PointerButtonChanged>(button));
  CHECK(std::get<PointerButtonChanged>(button).button == PointerButton::Right);
  CHECK(std::get<PointerButtonChanged>(button).down);

  const PlatformEvent wheel = WheelScrolled{-1.0f, 2.0f};
  REQUIRE(std::holds_alternative<WheelScrolled>(wheel));
  CHECK(std::get<WheelScrolled>(wheel).dx == -1.0f);
  CHECK(std::get<WheelScrolled>(wheel).dy == 2.0f);

  const PlatformEvent key = KeyChanged{Key::W, true, KeyModifiers{true, false, true}};
  REQUIRE(std::holds_alternative<KeyChanged>(key));
  CHECK(std::get<KeyChanged>(key).key == Key::W);
  CHECK(std::get<KeyChanged>(key).modifiers.ctrl);
  CHECK_FALSE(std::get<KeyChanged>(key).modifiers.shift);
  CHECK(std::get<KeyChanged>(key).modifiers.alt);

  TextEntered text;
  std::memcpy(text.utf8.data(), "\xC3\xA9", 2);
  text.size = 2;
  const PlatformEvent typed = text;
  REQUIRE(std::holds_alternative<TextEntered>(typed));
  CHECK(std::get<TextEntered>(typed).size == 2);

  // The window and lifecycle alternatives keep their indices (0..8).
  CHECK(PlatformEvent{WindowResize{}}.index() == 0);
  CHECK(PlatformEvent{Quit{}}.index() == 8);
  CHECK(moved.index() == 9);
  CHECK(typed.index() == 13);
  CHECK(std::variant_size_v<PlatformEvent> == 14);
}

TEST_CASE("the Key set is closed and contiguous", "[platform][platform_event][input]") {
  using atlantis::platform::Key;
  CHECK(static_cast<int>(Key::Z) - static_cast<int>(Key::A) == 25);
  CHECK(static_cast<int>(Key::Num9) - static_cast<int>(Key::Num0) == 9);
  CHECK(static_cast<int>(Key::F12) - static_cast<int>(Key::F1) == 11);
  CHECK(static_cast<int>(Key::RightAlt) == 68);  // 15 named + 26 letters + 10 digits + 12 F-keys + 6 modifiers, from 0
}

// ADR-0109: Android Platform emits no input event in v1 -- no app command maps
// to one.
TEST_CASE("Android app commands never map to an input event", "[platform][platform_event][input][android]") {
  using atlantis::platform::android_detail::AppCommand;
  using atlantis::platform::android_detail::AppCommandContext;
  using atlantis::platform::android_detail::mapAppCommand;
  int nativeWindow = 0;
  for (std::int32_t value = 0; value <= static_cast<std::int32_t>(AppCommand::Destroy); ++value) {
    AppCommandContext context;
    context.nativeWindow = &nativeWindow;
    const auto mapped = mapAppCommand(static_cast<AppCommand>(value), context);
    if (!mapped.has_value()) continue;
    INFO("AppCommand " << value);
    CHECK(mapped->index() <= 8);
  }
}
