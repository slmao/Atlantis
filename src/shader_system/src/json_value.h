#pragma once

// Private, minimal JSON DOM -- see Plan 0008 Section 3 for why this
// module owns its own JSON representation rather than a third-party
// library or a Core-wide facility. Not part of atlantis_shader_system's
// public include path; only json_parser.cpp/.h and this module's own
// readers (reflection_loader.cpp, slang_json_transform.cpp) see this
// type.

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace atlantis::shader_system::detail {

class JsonValue;

using JsonArray = std::vector<JsonValue>;
// Insertion-ordered key/value pairs, not a hash map -- real reflection
// JSON objects are small (a handful of fields), and preserving
// insertion order keeps parser behavior simple to reason about.
// Duplicate keys are resolved at parse time ("last one wins", Section
// 3) -- json_parser.cpp overwrites an existing entry in place rather
// than appending a second one, so this vector never holds two entries
// with the same key.
using JsonObject = std::vector<std::pair<std::string, JsonValue>>;

class JsonValue {
 public:
  JsonValue() : storage_(nullptr) {}
  explicit JsonValue(std::nullptr_t) : storage_(nullptr) {}
  explicit JsonValue(bool value) : storage_(value) {}
  explicit JsonValue(double value) : storage_(value) {}
  explicit JsonValue(std::string value) : storage_(std::move(value)) {}
  explicit JsonValue(JsonArray value) : storage_(std::move(value)) {}
  explicit JsonValue(JsonObject value) : storage_(std::move(value)) {}

  [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(storage_); }
  [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(storage_); }
  [[nodiscard]] bool isNumber() const { return std::holds_alternative<double>(storage_); }
  [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(storage_); }
  [[nodiscard]] bool isArray() const { return std::holds_alternative<JsonArray>(storage_); }
  [[nodiscard]] bool isObject() const { return std::holds_alternative<JsonObject>(storage_); }

  // Precondition: the matching isXxx() above is true. A programmer
  // error (ATLANTIS_CHECK inside std::get's own std::bad_variant_access
  // path would instead throw -- callers in this module always guard
  // with isXxx() first, per this module's own "no exceptions across
  // this module's own internal parsing" convention).
  [[nodiscard]] bool asBool() const { return std::get<bool>(storage_); }
  [[nodiscard]] double asNumber() const { return std::get<double>(storage_); }
  [[nodiscard]] const std::string& asString() const { return std::get<std::string>(storage_); }
  [[nodiscard]] const JsonArray& asArray() const { return std::get<JsonArray>(storage_); }
  [[nodiscard]] const JsonObject& asObject() const { return std::get<JsonObject>(storage_); }

  // Returns nullptr if this is not an object or `key` is not present.
  [[nodiscard]] const JsonValue* find(const std::string& key) const {
    if (!isObject()) return nullptr;
    for (const auto& [entryKey, entryValue] : std::get<JsonObject>(storage_)) {
      if (entryKey == key) return &entryValue;
    }
    return nullptr;
  }

 private:
  std::variant<std::nullptr_t, bool, double, std::string, JsonArray, JsonObject> storage_;
};

}  // namespace atlantis::shader_system::detail
