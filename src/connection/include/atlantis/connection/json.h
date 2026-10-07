#pragma once

#include <atlantis/result.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// Plan 0055 P1 / J2: a minimal JSON value, writer and parser, shared by the
// CLI's `--json` output (Spec 0055 ruling Q3) and Atlantis Remote's wire codec
// (ADR-0106 D1). Not a general-purpose library: no comments, no trailing
// commas, no streaming. Pure value types and functions; safe for concurrent use
// of distinct values.
namespace atlantis::connection::json {

// A JSON value. Objects keep their members in insertion order, so a writer
// that builds members in a fixed order produces the same bytes every time.
// Numbers keep their literal text, so a 64-bit integer or a float written
// with its shortest round-trip form loses nothing on the way through.
class Value {
 public:
  enum class Kind : std::uint8_t { Null, Bool, Number, String, Array, Object };
  using Array = std::vector<Value>;
  using Member = std::pair<std::string, Value>;
  using Object = std::vector<Member>;

  Value() = default;  // null

  [[nodiscard]] static Value boolean(bool value);
  // The shortest text that reads back as exactly `value`. A non-finite float
  // has no JSON number form: passing one is a programmer error (CHECKed); the
  // caller encodes it some other way.
  [[nodiscard]] static Value number(float value);
  [[nodiscard]] static Value number(std::int64_t value);
  [[nodiscard]] static Value number(std::uint64_t value);
  // A number from its literal text, kept as written. The text must be a JSON
  // number literal (RFC 8259 grammar; CHECKed).
  [[nodiscard]] static Value numberLiteral(std::string text);
  [[nodiscard]] static Value string(std::string value);
  [[nodiscard]] static Value array(Array items = {});
  [[nodiscard]] static Value object(Object members = {});

  [[nodiscard]] Kind kind() const noexcept { return kind_; }
  [[nodiscard]] bool isNull() const noexcept { return kind_ == Kind::Null; }
  [[nodiscard]] bool isBool() const noexcept { return kind_ == Kind::Bool; }
  [[nodiscard]] bool isNumber() const noexcept { return kind_ == Kind::Number; }
  [[nodiscard]] bool isString() const noexcept { return kind_ == Kind::String; }
  [[nodiscard]] bool isArray() const noexcept { return kind_ == Kind::Array; }
  [[nodiscard]] bool isObject() const noexcept { return kind_ == Kind::Object; }

  // Accessors CHECK the kind: asking a string for its items is a programmer
  // error. Decoders test the kind first.
  [[nodiscard]] bool asBool() const;
  [[nodiscard]] const std::string& numberText() const;
  [[nodiscard]] const std::string& asString() const;
  [[nodiscard]] const Array& asArray() const;
  [[nodiscard]] Array& asArray();
  [[nodiscard]] const Object& asObject() const;

  // A number read as a type; false if it is not a number of that type
  // (a fraction or exponent for an integer, out of range, or not a number).
  [[nodiscard]] bool toFloat(float& out) const;
  [[nodiscard]] bool toUInt64(std::uint64_t& out) const;
  [[nodiscard]] bool toInt64(std::int64_t& out) const;

  // Object members: the first member named `key`, or nullptr (also for a
  // non-object). set() appends, or replaces an existing member in place.
  [[nodiscard]] const Value* find(std::string_view key) const;
  Value& set(std::string key, Value value);
  // Array items: appends (CHECKs that this is an array).
  Value& push(Value item);

  friend bool operator==(const Value&, const Value&) = default;

 private:
  Kind kind_ = Kind::Null;
  bool bool_ = false;
  std::string text_;  // a number's literal text, or a string's bytes
  Array array_;
  Object object_;
};

// Compact UTF-8 text: no whitespace, members in stored order. Strings are
// escaped per RFC 8259 (quote, backslash and control characters); other bytes
// pass through.
[[nodiscard]] std::string write(const Value& value);

struct ParseError {
  std::size_t offset = 0;  // byte offset of the problem in the input
  std::string_view what;   // static text
};

// One JSON document, with optional surrounding whitespace. Nesting deeper
// than 64 levels is refused, so hostile input cannot exhaust the stack.
[[nodiscard]] atlantis::Result<Value, ParseError> parse(std::string_view text);

}  // namespace atlantis::connection::json
