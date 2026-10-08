#pragma once

#include <array>
#include <cstdint>
#include <variant>

// Plan 0056 P6/P8 (ADR-0108 D2): the editor's own input values. The host
// translates Atlantis Platform's input events (ADR-0109) into these; nothing
// here is a Platform or OS type. Positions are framebuffer pixels relative to
// the window's client area.
namespace atlantis::editor {

enum class PointerButton : std::uint8_t { Left, Right, Middle };

// The editor's key set, mirroring Platform's closed set enumerator for
// enumerator and in the same order (the host's translation relies on it).
enum class Key : std::uint16_t {
  Tab,
  LeftArrow,
  RightArrow,
  UpArrow,
  DownArrow,
  Home,
  End,
  PageUp,
  PageDown,
  Insert,
  Delete,
  Backspace,
  Space,
  Enter,
  Escape,
  A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
  Num0, Num1, Num2, Num3, Num4, Num5, Num6, Num7, Num8, Num9,
  F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
  LeftCtrl,
  LeftShift,
  LeftAlt,
  RightCtrl,
  RightShift,
  RightAlt,
};

struct KeyModifiers {
  bool ctrl = false;
  bool shift = false;
  bool alt = false;
  friend bool operator==(const KeyModifiers&, const KeyModifiers&) = default;
};

struct PointerMoved {
  float x = 0.0f;
  float y = 0.0f;
  friend bool operator==(const PointerMoved&, const PointerMoved&) = default;
};

struct PointerButtonChanged {
  PointerButton button = PointerButton::Left;
  bool down = false;
  float x = 0.0f;
  float y = 0.0f;
  friend bool operator==(const PointerButtonChanged&, const PointerButtonChanged&) = default;
};

// In wheel notches; positive dy scrolls up (away from the user), positive dx
// to the right.
struct WheelScrolled {
  float dx = 0.0f;
  float dy = 0.0f;
  friend bool operator==(const WheelScrolled&, const WheelScrolled&) = default;
};

struct KeyChanged {
  Key key = Key::Tab;
  bool down = false;
  KeyModifiers modifiers;
  friend bool operator==(const KeyChanged&, const KeyChanged&) = default;
};

// One Unicode code point as UTF-8: utf8[0, size).
struct TextEntered {
  std::array<char, 8> utf8{};
  std::uint8_t size = 0;
  friend bool operator==(const TextEntered&, const TextEntered&) = default;
};

using InputEvent = std::variant<PointerMoved, PointerButtonChanged, WheelScrolled, KeyChanged, TextEntered>;

}  // namespace atlantis::editor
