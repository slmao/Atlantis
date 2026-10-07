#include <atlantis/connection/json.h>

#include <atlantis/assert.h>

#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <string>
#include <system_error>

namespace atlantis::connection::json {

namespace {

constexpr std::size_t kMaxDepth = 64;

[[nodiscard]] bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

// The length of the JSON number literal at the start of `text`, or 0.
[[nodiscard]] std::size_t numberLiteralLength(std::string_view text) noexcept {
  std::size_t at = 0;
  const auto digits = [&] {
    const std::size_t start = at;
    while (at < text.size() && isDigit(text[at])) ++at;
    return at > start;
  };
  if (at < text.size() && text[at] == '-') ++at;
  if (at < text.size() && text[at] == '0') {
    ++at;
  } else if (!digits()) {
    return 0;
  }
  if (at < text.size() && text[at] == '.') {
    ++at;
    if (!digits()) return 0;
  }
  if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
    ++at;
    if (at < text.size() && (text[at] == '+' || text[at] == '-')) ++at;
    if (!digits()) return 0;
  }
  return at;
}

void writeString(std::string& out, std::string_view text) {
  out += '"';
  for (const char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(c) < 0x20) {
          static constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out += kHex[(static_cast<unsigned char>(c) >> 4) & 0xF];
          out += kHex[static_cast<unsigned char>(c) & 0xF];
        } else {
          out += c;
        }
    }
  }
  out += '"';
}

void writeValue(std::string& out, const Value& value) {
  switch (value.kind()) {
    case Value::Kind::Null: out += "null"; return;
    case Value::Kind::Bool: out += value.asBool() ? "true" : "false"; return;
    case Value::Kind::Number: out += value.numberText(); return;
    case Value::Kind::String: writeString(out, value.asString()); return;
    case Value::Kind::Array: {
      out += '[';
      bool first = true;
      for (const Value& item : value.asArray()) {
        if (!first) out += ',';
        first = false;
        writeValue(out, item);
      }
      out += ']';
      return;
    }
    case Value::Kind::Object: {
      out += '{';
      bool first = true;
      for (const auto& [key, member] : value.asObject()) {
        if (!first) out += ',';
        first = false;
        writeString(out, key);
        out += ':';
        writeValue(out, member);
      }
      out += '}';
      return;
    }
  }
}

void appendUtf8(std::string& out, std::uint32_t codePoint) {
  if (codePoint < 0x80) {
    out += static_cast<char>(codePoint);
  } else if (codePoint < 0x800) {
    out += static_cast<char>(0xC0 | (codePoint >> 6));
    out += static_cast<char>(0x80 | (codePoint & 0x3F));
  } else if (codePoint < 0x10000) {
    out += static_cast<char>(0xE0 | (codePoint >> 12));
    out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codePoint & 0x3F));
  } else {
    out += static_cast<char>(0xF0 | (codePoint >> 18));
    out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codePoint & 0x3F));
  }
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  atlantis::Result<Value, ParseError> document() {
    using ResultT = atlantis::Result<Value, ParseError>;
    skipSpace();
    Value value;
    if (!parseValue(value, 0)) return ResultT::Err(error_);
    skipSpace();
    if (at_ != text_.size()) return ResultT::Err(ParseError{at_, "trailing characters after the document"});
    return ResultT::Ok(std::move(value));
  }

 private:
  bool fail(std::string_view what) {
    error_ = ParseError{at_, what};
    return false;
  }
  void skipSpace() {
    while (at_ < text_.size() && (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\n' || text_[at_] == '\r')) {
      ++at_;
    }
  }
  bool literal(std::string_view word) {
    if (text_.substr(at_, word.size()) != word) return fail("invalid literal");
    at_ += word.size();
    return true;
  }

  bool parseValue(Value& out, std::size_t depth) {
    if (depth >= kMaxDepth) return fail("nesting too deep");
    if (at_ >= text_.size()) return fail("unexpected end of input");
    const char c = text_[at_];
    if (c == 'n') {
      out = Value();
      return literal("null");
    }
    if (c == 't') {
      out = Value::boolean(true);
      return literal("true");
    }
    if (c == 'f') {
      out = Value::boolean(false);
      return literal("false");
    }
    if (c == '"') {
      std::string text;
      if (!parseString(text)) return false;
      out = Value::string(std::move(text));
      return true;
    }
    if (c == '[') return parseArray(out, depth);
    if (c == '{') return parseObject(out, depth);
    if (c == '-' || isDigit(c)) return parseNumber(out);
    return fail("unexpected character");
  }

  bool parseNumber(Value& out) {
    const std::size_t length = numberLiteralLength(text_.substr(at_));
    if (length == 0) return fail("malformed number");
    out = Value::numberLiteral(std::string(text_.substr(at_, length)));
    at_ += length;
    return true;
  }

  bool parseHex4(std::uint32_t& out) {
    if (at_ + 4 > text_.size()) return fail("truncated \\u escape");
    out = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = text_[at_++];
      out <<= 4;
      if (c >= '0' && c <= '9') {
        out |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        out |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        out |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return fail("malformed \\u escape");
      }
    }
    return true;
  }

  bool parseString(std::string& out) {
    ++at_;  // opening quote
    while (true) {
      if (at_ >= text_.size()) return fail("unterminated string");
      const char c = text_[at_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return fail("control character in string");
      if (c != '\\') {
        out += c;
        continue;
      }
      if (at_ >= text_.size()) return fail("unterminated escape");
      const char e = text_[at_++];
      switch (e) {
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case '/': out += '/'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'u': {
          std::uint32_t unit = 0;
          if (!parseHex4(unit)) return false;
          if (unit >= 0xDC00 && unit <= 0xDFFF) return fail("lone low surrogate");
          if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (text_.substr(at_, 2) != "\\u") return fail("lone high surrogate");
            at_ += 2;
            std::uint32_t low = 0;
            if (!parseHex4(low)) return false;
            if (low < 0xDC00 || low > 0xDFFF) return fail("invalid surrogate pair");
            unit = 0x10000 + ((unit - 0xD800) << 10) + (low - 0xDC00);
          }
          appendUtf8(out, unit);
          break;
        }
        default: return fail("invalid escape");
      }
    }
  }

  bool parseArray(Value& out, std::size_t depth) {
    ++at_;
    out = Value::array();
    skipSpace();
    if (at_ < text_.size() && text_[at_] == ']') {
      ++at_;
      return true;
    }
    while (true) {
      skipSpace();
      Value item;
      if (!parseValue(item, depth + 1)) return false;
      out.push(std::move(item));
      skipSpace();
      if (at_ >= text_.size()) return fail("unterminated array");
      if (text_[at_] == ',') {
        ++at_;
        continue;
      }
      if (text_[at_] == ']') {
        ++at_;
        return true;
      }
      return fail("expected ',' or ']'");
    }
  }

  bool parseObject(Value& out, std::size_t depth) {
    ++at_;
    Value::Object members;
    skipSpace();
    if (at_ < text_.size() && text_[at_] == '}') {
      ++at_;
      out = Value::object();
      return true;
    }
    while (true) {
      skipSpace();
      if (at_ >= text_.size() || text_[at_] != '"') return fail("expected a member name");
      std::string key;
      if (!parseString(key)) return false;
      skipSpace();
      if (at_ >= text_.size() || text_[at_] != ':') return fail("expected ':'");
      ++at_;
      skipSpace();
      Value member;
      if (!parseValue(member, depth + 1)) return false;
      members.emplace_back(std::move(key), std::move(member));
      skipSpace();
      if (at_ >= text_.size()) return fail("unterminated object");
      if (text_[at_] == ',') {
        ++at_;
        continue;
      }
      if (text_[at_] == '}') {
        ++at_;
        out = Value::object(std::move(members));
        return true;
      }
      return fail("expected ',' or '}'");
    }
  }

  std::string_view text_;
  std::size_t at_ = 0;
  ParseError error_;
};

}  // namespace

Value Value::boolean(bool value) {
  Value v;
  v.kind_ = Kind::Bool;
  v.bool_ = value;
  return v;
}

Value Value::number(float value) {
  ATLANTIS_CHECK_MSG(std::isfinite(value), "json::Value::number(): a non-finite float has no JSON number form");
  std::array<char, 64> buffer{};
  const auto [end, ec] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  ATLANTIS_CHECK(ec == std::errc());
  Value v;
  v.kind_ = Kind::Number;
  v.text_.assign(buffer.data(), end);
  return v;
}

Value Value::number(std::int64_t value) {
  Value v;
  v.kind_ = Kind::Number;
  v.text_ = std::to_string(value);
  return v;
}

Value Value::number(std::uint64_t value) {
  Value v;
  v.kind_ = Kind::Number;
  v.text_ = std::to_string(value);
  return v;
}

Value Value::string(std::string value) {
  Value v;
  v.kind_ = Kind::String;
  v.text_ = std::move(value);
  return v;
}

Value Value::array(Array items) {
  Value v;
  v.kind_ = Kind::Array;
  v.array_ = std::move(items);
  return v;
}

Value Value::object(Object members) {
  Value v;
  v.kind_ = Kind::Object;
  v.object_ = std::move(members);
  return v;
}

Value Value::numberLiteral(std::string text) {
  ATLANTIS_CHECK_MSG(!text.empty() && numberLiteralLength(text) == text.size(),
                     "json::Value::numberLiteral(): not a JSON number literal");
  Value v;
  v.kind_ = Kind::Number;
  v.text_ = std::move(text);
  return v;
}

bool Value::asBool() const {
  ATLANTIS_CHECK(kind_ == Kind::Bool);
  return bool_;
}

const std::string& Value::numberText() const {
  ATLANTIS_CHECK(kind_ == Kind::Number);
  return text_;
}

const std::string& Value::asString() const {
  ATLANTIS_CHECK(kind_ == Kind::String);
  return text_;
}

const Value::Array& Value::asArray() const {
  ATLANTIS_CHECK(kind_ == Kind::Array);
  return array_;
}

Value::Array& Value::asArray() {
  ATLANTIS_CHECK(kind_ == Kind::Array);
  return array_;
}

const Value::Object& Value::asObject() const {
  ATLANTIS_CHECK(kind_ == Kind::Object);
  return object_;
}

bool Value::toFloat(float& out) const {
  if (kind_ != Kind::Number) return false;
  // strtof in the C locale (Atlantis never calls setlocale), as text.cpp.
  char* end = nullptr;
  errno = 0;
  const float value = std::strtof(text_.c_str(), &end);
  if (end != text_.c_str() + text_.size() || errno == ERANGE || !std::isfinite(value)) return false;
  out = value;
  return true;
}

bool Value::toUInt64(std::uint64_t& out) const {
  if (kind_ != Kind::Number || text_.empty()) return false;
  for (const char c : text_) {
    if (!isDigit(c)) return false;
  }
  char* end = nullptr;
  errno = 0;
  const unsigned long long value = std::strtoull(text_.c_str(), &end, 10);
  if (errno == ERANGE || end != text_.c_str() + text_.size()) return false;
  out = static_cast<std::uint64_t>(value);
  return true;
}

bool Value::toInt64(std::int64_t& out) const {
  if (kind_ != Kind::Number || text_.empty()) return false;
  for (std::size_t i = 0; i < text_.size(); ++i) {
    if (!(isDigit(text_[i]) || (i == 0 && text_[i] == '-'))) return false;
  }
  char* end = nullptr;
  errno = 0;
  const long long value = std::strtoll(text_.c_str(), &end, 10);
  if (errno == ERANGE || end != text_.c_str() + text_.size()) return false;
  out = static_cast<std::int64_t>(value);
  return true;
}

const Value* Value::find(std::string_view key) const {
  if (kind_ != Kind::Object) return nullptr;
  for (const auto& [name, member] : object_) {
    if (name == key) return &member;
  }
  return nullptr;
}

Value& Value::set(std::string key, Value value) {
  ATLANTIS_CHECK(kind_ == Kind::Object);
  for (auto& [name, member] : object_) {
    if (name == key) {
      member = std::move(value);
      return member;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
  return object_.back().second;
}

Value& Value::push(Value item) {
  ATLANTIS_CHECK(kind_ == Kind::Array);
  array_.push_back(std::move(item));
  return array_.back();
}

std::string write(const Value& value) {
  std::string out;
  writeValue(out, value);
  return out;
}

atlantis::Result<Value, ParseError> parse(std::string_view text) { return Parser(text).document(); }

}  // namespace atlantis::connection::json
