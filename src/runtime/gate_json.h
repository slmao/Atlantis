#pragma once

// Spec 0060 (Plan 0060 M1): the few JSON helpers the gate's documents share --
// number formatting (the shortest round-trip form, as the benchmark report writes
// numbers), reading a number back, and walking a path. Private to the
// atlantis_runtime executable's startup layer, like gate_set.h.

#include <atlantis/connection/json.h>

#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>

namespace atlantis::runtime::gate {

using Value = atlantis::connection::json::Value;

[[nodiscard]] inline std::string formatNumber(double value) {
  if (!std::isfinite(value)) value = 0.0;
  std::array<char, 64> buffer{};
  const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  return std::string(buffer.data(), result.ptr);
}

[[nodiscard]] inline Value num(double value) { return Value::numberLiteral(formatNumber(value)); }
[[nodiscard]] inline Value count(std::size_t value) { return Value::number(static_cast<std::uint64_t>(value)); }

// A JSON number read as a double; nullopt for a missing or non-number value.
[[nodiscard]] inline std::optional<double> numberOf(const Value* value) {
  if (value == nullptr || !value->isNumber()) return std::nullopt;
  const std::string& text = value->numberText();
  double out = 0.0;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
  if (result.ec != std::errc() || result.ptr != text.data() + text.size()) return std::nullopt;
  return out;
}

// The value at `path` below `root`, or nullptr when any step is missing.
[[nodiscard]] inline const Value* at(const Value& root, std::initializer_list<std::string_view> path) {
  const Value* current = &root;
  for (const std::string_view step : path) {
    current = current->find(step);
    if (current == nullptr) return nullptr;
  }
  return current;
}

}  // namespace atlantis::runtime::gate
