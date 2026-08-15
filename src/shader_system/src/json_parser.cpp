#include "json_parser.h"

#include <charconv>
#include <cstdint>
#include <optional>
#include <string_view>

namespace atlantis::shader_system::detail {

namespace {

// Plan 0008 Section 3's four fixed, conservative resource limits --
// private to this translation unit, not a runtime-configurable surface
// (PHR-0008-14).
constexpr std::size_t kMaxInputSizeBytes = 16 * 1024 * 1024;
constexpr int kMaxNestingDepth = 64;
constexpr std::size_t kMaxStringLength = 64 * 1024;
constexpr std::size_t kMaxElementCount = 4096;

using ParseResult = atlantis::Result<JsonValue, JsonParseError>;

void appendUtf8(std::string& out, std::uint32_t codepoint) {
  if (codepoint <= 0x7F) {
    out.push_back(static_cast<char>(codepoint));
  } else if (codepoint <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else if (codepoint <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
  }
}

// Recursive-descent parser over the full input string. One instance per
// parseJson() call; not reused, not thread-shared.
class Parser {
 public:
  explicit Parser(const std::string& input) : input_(input) {}

  [[nodiscard]] ParseResult parse() {
    if (input_.size() > kMaxInputSizeBytes) return ParseResult::Err(JsonParseError::InputTooLarge);
    if (input_.empty()) return ParseResult::Err(JsonParseError::EmptyInput);

    skipWhitespace();
    ParseResult value = parseValue(0);
    if (value.isErr()) return value;

    skipWhitespace();
    if (pos_ != input_.size()) return ParseResult::Err(JsonParseError::TrailingGarbage);
    return value;
  }

 private:
  const std::string& input_;
  std::size_t pos_ = 0;

  [[nodiscard]] bool atEnd() const { return pos_ >= input_.size(); }
  [[nodiscard]] char peek() const { return input_[pos_]; }

  void skipWhitespace() {
    while (!atEnd()) {
      const char c = peek();
      if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  [[nodiscard]] ParseResult parseValue(int depth) {
    if (depth > kMaxNestingDepth) return ParseResult::Err(JsonParseError::NestingTooDeep);
    if (atEnd()) return ParseResult::Err(JsonParseError::UnexpectedToken);

    switch (peek()) {
      case '{':
        return parseObject(depth);
      case '[':
        return parseArray(depth);
      case '"':
        return parseStringValue();
      case 't':
        return parseLiteral("true", JsonValue(true));
      case 'f':
        return parseLiteral("false", JsonValue(false));
      case 'n':
        return parseLiteral("null", JsonValue(nullptr));
      default:
        return parseNumber();
    }
  }

  [[nodiscard]] ParseResult parseLiteral(std::string_view word, JsonValue value) {
    if (input_.compare(pos_, word.size(), word) != 0) return ParseResult::Err(JsonParseError::UnexpectedToken);
    pos_ += word.size();
    return ParseResult::Ok(std::move(value));
  }

  [[nodiscard]] ParseResult parseObject(int depth) {
    ++pos_;  // consume '{'
    JsonObject object;
    skipWhitespace();
    if (!atEnd() && peek() == '}') {
      ++pos_;
      return ParseResult::Ok(JsonValue(std::move(object)));
    }

    while (true) {
      skipWhitespace();
      if (atEnd() || peek() != '"') return ParseResult::Err(JsonParseError::UnexpectedToken);
      auto key = parseRawString();
      if (key.isErr()) return ParseResult::Err(key.error());

      skipWhitespace();
      if (atEnd() || peek() != ':') return ParseResult::Err(JsonParseError::UnexpectedToken);
      ++pos_;  // consume ':'
      skipWhitespace();

      ParseResult entryValue = parseValue(depth + 1);
      if (entryValue.isErr()) return entryValue;

      // Duplicate key: "last one wins" (Plan 0008 Section 3) -- overwrite
      // the existing entry in place rather than appending a second one.
      bool replaced = false;
      for (auto& [existingKey, existingValue] : object) {
        if (existingKey == key.value()) {
          existingValue = std::move(entryValue.value());
          replaced = true;
          break;
        }
      }
      if (!replaced) {
        if (object.size() >= kMaxElementCount) return ParseResult::Err(JsonParseError::TooManyElements);
        object.emplace_back(std::move(key.value()), std::move(entryValue.value()));
      }

      skipWhitespace();
      if (atEnd()) return ParseResult::Err(JsonParseError::UnexpectedToken);
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      if (peek() == '}') {
        ++pos_;
        break;
      }
      return ParseResult::Err(JsonParseError::UnexpectedToken);
    }

    return ParseResult::Ok(JsonValue(std::move(object)));
  }

  [[nodiscard]] ParseResult parseArray(int depth) {
    ++pos_;  // consume '['
    JsonArray array;
    skipWhitespace();
    if (!atEnd() && peek() == ']') {
      ++pos_;
      return ParseResult::Ok(JsonValue(std::move(array)));
    }

    while (true) {
      skipWhitespace();
      ParseResult element = parseValue(depth + 1);
      if (element.isErr()) return element;
      if (array.size() >= kMaxElementCount) return ParseResult::Err(JsonParseError::TooManyElements);
      array.push_back(std::move(element.value()));

      skipWhitespace();
      if (atEnd()) return ParseResult::Err(JsonParseError::UnexpectedToken);
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      if (peek() == ']') {
        ++pos_;
        break;
      }
      return ParseResult::Err(JsonParseError::UnexpectedToken);
    }

    return ParseResult::Ok(JsonValue(std::move(array)));
  }

  [[nodiscard]] ParseResult parseStringValue() {
    auto raw = parseRawString();
    if (raw.isErr()) return ParseResult::Err(raw.error());
    return ParseResult::Ok(JsonValue(std::move(raw.value())));
  }

  // Parses a JSON string literal (the current character must be '"')
  // and returns its decoded content (escapes resolved, \uXXXX surrogate
  // pairs combined and UTF-8 encoded).
  [[nodiscard]] atlantis::Result<std::string, JsonParseError> parseRawString() {
    ++pos_;  // consume opening '"'
    std::string result;

    while (true) {
      if (atEnd()) return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::UnterminatedString);
      const char c = input_[pos_];

      if (c == '"') {
        ++pos_;
        return atlantis::Result<std::string, JsonParseError>::Ok(std::move(result));
      }

      if (static_cast<unsigned char>(c) < 0x20) {
        // Raw control characters are not valid inside a JSON string.
        return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::UnterminatedString);
      }

      if (c == '\\') {
        ++pos_;
        if (atEnd()) return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::UnterminatedString);
        const char escapeChar = input_[pos_];
        switch (escapeChar) {
          case '"':
            result.push_back('"');
            ++pos_;
            break;
          case '\\':
            result.push_back('\\');
            ++pos_;
            break;
          case '/':
            result.push_back('/');
            ++pos_;
            break;
          case 'b':
            result.push_back('\b');
            ++pos_;
            break;
          case 'f':
            result.push_back('\f');
            ++pos_;
            break;
          case 'n':
            result.push_back('\n');
            ++pos_;
            break;
          case 'r':
            result.push_back('\r');
            ++pos_;
            break;
          case 't':
            result.push_back('\t');
            ++pos_;
            break;
          case 'u': {
            ++pos_;
            auto codeUnit = parseHex4();
            if (!codeUnit.has_value()) {
              return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::InvalidUnicodeEscape);
            }
            std::uint32_t codepoint = *codeUnit;
            if (codepoint >= 0xD800 && codepoint <= 0xDBFF) {
              // High surrogate -- must be immediately followed by a low
              // surrogate \uXXXX escape to form one codepoint.
              if (pos_ + 1 >= input_.size() || input_[pos_] != '\\' || input_[pos_ + 1] != 'u') {
                return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::InvalidUnicodeEscape);
              }
              pos_ += 2;
              auto lowUnit = parseHex4();
              if (!lowUnit.has_value() || *lowUnit < 0xDC00 || *lowUnit > 0xDFFF) {
                return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::InvalidUnicodeEscape);
              }
              codepoint = 0x10000 + ((codepoint - 0xD800) << 10) + (*lowUnit - 0xDC00);
            } else if (codepoint >= 0xDC00 && codepoint <= 0xDFFF) {
              // Lone low surrogate with no preceding high surrogate.
              return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::InvalidUnicodeEscape);
            }
            appendUtf8(result, codepoint);
            break;
          }
          default:
            return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::InvalidEscape);
        }
      } else {
        result.push_back(c);
        ++pos_;
      }

      if (result.size() > kMaxStringLength) {
        return atlantis::Result<std::string, JsonParseError>::Err(JsonParseError::StringTooLong);
      }
    }
  }

  // Parses exactly 4 hex digits at the current position and advances
  // past them. Returns std::nullopt on anything else (including running
  // past the end of input).
  [[nodiscard]] std::optional<std::uint32_t> parseHex4() {
    if (pos_ + 4 > input_.size()) return std::nullopt;
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = input_[pos_ + static_cast<std::size_t>(i)];
      value <<= 4;
      if (c >= '0' && c <= '9') {
        value |= static_cast<std::uint32_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        value |= static_cast<std::uint32_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        value |= static_cast<std::uint32_t>(c - 'A' + 10);
      } else {
        return std::nullopt;
      }
    }
    pos_ += 4;
    return value;
  }

  [[nodiscard]] ParseResult parseNumber() {
    const std::size_t start = pos_;

    if (!atEnd() && peek() == '-') ++pos_;

    if (atEnd() || !isDigit(peek())) return ParseResult::Err(JsonParseError::InvalidNumber);
    if (peek() == '0') {
      ++pos_;
    } else {
      while (!atEnd() && isDigit(peek())) ++pos_;
    }

    if (!atEnd() && peek() == '.') {
      ++pos_;
      if (atEnd() || !isDigit(peek())) return ParseResult::Err(JsonParseError::InvalidNumber);
      while (!atEnd() && isDigit(peek())) ++pos_;
    }

    if (!atEnd() && (peek() == 'e' || peek() == 'E')) {
      ++pos_;
      if (!atEnd() && (peek() == '+' || peek() == '-')) ++pos_;
      if (atEnd() || !isDigit(peek())) return ParseResult::Err(JsonParseError::InvalidNumber);
      while (!atEnd() && isDigit(peek())) ++pos_;
    }

    double value = 0.0;
    const char* first = input_.data() + start;
    const char* last = input_.data() + pos_;
    const auto conversion = std::from_chars(first, last, value);
    if (conversion.ec != std::errc{} || conversion.ptr != last) {
      return ParseResult::Err(JsonParseError::InvalidNumber);
    }

    return ParseResult::Ok(JsonValue(value));
  }

  [[nodiscard]] static bool isDigit(char c) { return c >= '0' && c <= '9'; }
};

}  // namespace

atlantis::Result<JsonValue, JsonParseError> parseJson(const std::string& input) {
  Parser parser(input);
  return parser.parse();
}

}  // namespace atlantis::shader_system::detail
