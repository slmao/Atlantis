#include <string>

#include <catch2/catch_test_macros.hpp>

#include "../../src/shader_system/src/json_parser.h"
#include "../../src/shader_system/src/json_value.h"

using atlantis::shader_system::detail::JsonParseError;
using atlantis::shader_system::detail::JsonValue;
using atlantis::shader_system::detail::parseJson;

TEST_CASE("parseJson round-trips every JSON value kind", "[shader_system][json]") {
  SECTION("object with nested array/string/number/bool/null") {
    const auto result = parseJson(R"({"a": 1, "b": [1, 2.5, "x", true, false, null], "c": {"d": -3}})");
    REQUIRE(result.isOk());
    const JsonValue& root = result.value();
    REQUIRE(root.isObject());
    REQUIRE(root.find("a") != nullptr);
    REQUIRE(root.find("a")->asNumber() == 1.0);
    REQUIRE(root.find("b")->isArray());
    REQUIRE(root.find("b")->asArray().size() == 6);
    REQUIRE(root.find("b")->asArray()[2].asString() == "x");
    REQUIRE(root.find("b")->asArray()[3].asBool() == true);
    REQUIRE(root.find("b")->asArray()[4].asBool() == false);
    REQUIRE(root.find("b")->asArray()[5].isNull());
    REQUIRE(root.find("c")->find("d")->asNumber() == -3.0);
  }

  SECTION("top-level scalar values") {
    REQUIRE(parseJson("42").isOk());
    REQUIRE(parseJson("-1.5e3").isOk());
    REQUIRE(parseJson("true").isOk());
    REQUIRE(parseJson("false").isOk());
    REQUIRE(parseJson("null").isOk());
    REQUIRE(parseJson("\"hello\"").isOk());
  }

  SECTION("empty object and array") {
    REQUIRE(parseJson("{}").value().asObject().empty());
    REQUIRE(parseJson("[]").value().asArray().empty());
  }
}

TEST_CASE("parseJson handles string escapes", "[shader_system][json]") {
  SECTION("standard escape set") {
    const auto result = parseJson(R"("a\"b\\c\/d\be\ff\ng\rh\ti")");
    REQUIRE(result.isOk());
    REQUIRE(result.value().asString() == "a\"b\\c/d\be\ff\ng\rh\ti");
  }

  SECTION("\\uXXXX escape in the Basic Multilingual Plane") {
    const auto result = parseJson(R"("A")");  // 'A'
    REQUIRE(result.isOk());
    REQUIRE(result.value().asString() == "A");
  }

  SECTION("surrogate pair escape outside the Basic Multilingual Plane") {
    // U+1F600 (GRINNING FACE) as a UTF-16 surrogate pair.
    const auto result = parseJson(R"("😀")");
    REQUIRE(result.isOk());
    const std::string& decoded = result.value().asString();
    REQUIRE(decoded.size() == 4);  // UTF-8 encoding of U+1F600 is 4 bytes
    REQUIRE(static_cast<unsigned char>(decoded[0]) == 0xF0);
  }

  SECTION("an unpaired high surrogate is a parse error") {
    const auto result = parseJson(R"("\uD83D")");
    REQUIRE(result.isErr());
    REQUIRE(result.error() == JsonParseError::InvalidUnicodeEscape);
  }

  SECTION("a lone low surrogate is a parse error") {
    const auto result = parseJson(R"("\uDE00")");
    REQUIRE(result.isErr());
    REQUIRE(result.error() == JsonParseError::InvalidUnicodeEscape);
  }
}

TEST_CASE("parseJson rejects malformed input", "[shader_system][json]") {
  SECTION("unterminated string") {
    REQUIRE(parseJson(R"("abc)").isErr());
  }

  SECTION("unexpected token") {
    REQUIRE(parseJson("nul").isErr());
  }

  SECTION("trailing garbage after the top-level value") {
    const auto result = parseJson(R"({"a": 1} garbage)");
    REQUIRE(result.isErr());
    REQUIRE(result.error() == JsonParseError::TrailingGarbage);
  }

  SECTION("empty input") {
    REQUIRE(parseJson("").isErr());
  }

  SECTION("invalid escape") {
    REQUIRE(parseJson(R"("\q")").isErr());
  }

  SECTION("leading zero is not valid JSON") {
    REQUIRE(parseJson("01").isErr());
  }
}

TEST_CASE("parseJson treats a duplicate object key as \"last one wins\"", "[shader_system][json]") {
  const auto result = parseJson(R"({"a": 1, "a": 2})");
  REQUIRE(result.isOk());
  const JsonValue& root = result.value();
  REQUIRE(root.asObject().size() == 1);
  REQUIRE(root.find("a")->asNumber() == 2.0);
}

TEST_CASE("parseJson enforces its four documented resource limits", "[shader_system][json]") {
  SECTION("input size") {
    const std::string oversized(16 * 1024 * 1024 + 1, ' ');
    const auto result = parseJson(oversized);
    REQUIRE(result.isErr());
    REQUIRE(result.error() == JsonParseError::InputTooLarge);
  }

  SECTION("nesting depth") {
    std::string deeplyNested;
    for (int i = 0; i < 70; ++i) deeplyNested += "[";
    for (int i = 0; i < 70; ++i) deeplyNested += "]";
    const auto result = parseJson(deeplyNested);
    REQUIRE(result.isErr());
    REQUIRE(result.error() == JsonParseError::NestingTooDeep);
  }

  SECTION("string length") {
    const std::string oversizedString = "\"" + std::string(64 * 1024 + 1, 'x') + "\"";
    const auto result = parseJson(oversizedString);
    REQUIRE(result.isErr());
    REQUIRE(result.error() == JsonParseError::StringTooLong);
  }

  SECTION("array element count") {
    std::string manyElements = "[";
    for (int i = 0; i < 4097; ++i) {
      if (i != 0) manyElements += ",";
      manyElements += "0";
    }
    manyElements += "]";
    const auto result = parseJson(manyElements);
    REQUIRE(result.isErr());
    REQUIRE(result.error() == JsonParseError::TooManyElements);
  }
}
