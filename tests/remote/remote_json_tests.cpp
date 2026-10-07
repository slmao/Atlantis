#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>

// Plan 0055 M1 (P1, J2): the minimal JSON shared by the CLI's `--json` and
// Remote's codec -- round trips, exact numbers, escapes, member order, and
// refusal of malformed or too-deep input.

namespace {

using atlantis::connection::json::parse;
using atlantis::connection::json::Value;
using atlantis::connection::json::write;

[[nodiscard]] Value parsed(std::string_view text) {
  auto result = parse(text);
  REQUIRE(result.isOk());
  return result.value();
}

}  // namespace

TEST_CASE("json: values write compactly, members in insertion order", "[remote][json]") {
  Value object = Value::object();
  object.set("z", Value::number(std::uint64_t{1}));
  object.set("a", Value::array({Value(), Value::boolean(true), Value::boolean(false), Value::string("x")}));
  object.set("m", Value::object());
  CHECK(write(object) == R"({"z":1,"a":[null,true,false,"x"],"m":{}})");
  object.set("z", Value::number(std::int64_t{-2}));  // replaced in place
  CHECK(write(object) == R"({"z":-2,"a":[null,true,false,"x"],"m":{}})");
  CHECK(write(Value::array()) == "[]");
}

TEST_CASE("json: floats use their shortest round-trip text and read back exactly", "[remote][json]") {
  for (const float f : {0.0f, -0.0f, 1.0f, 0.1f, 3.0f, 12.0f, 24.0f, -39.615f, 1e-38f, 3.4028235e38f,
                        std::numeric_limits<float>::denorm_min(), 0.3333333f}) {
    const Value number = Value::number(f);
    float back = 0.0f;
    REQUIRE(parsed(write(number)).toFloat(back));
    CHECK(std::memcmp(&back, &f, sizeof f) == 0);
  }
  CHECK(Value::number(0.1f).numberText() == "0.1");
  CHECK(Value::number(3.0f).numberText() == "3");
}

TEST_CASE("json: 64-bit integers survive exactly", "[remote][json]") {
  const std::uint64_t big = std::numeric_limits<std::uint64_t>::max();
  std::uint64_t back = 0;
  REQUIRE(parsed(write(Value::number(big))).toUInt64(back));
  CHECK(back == big);
  std::int64_t negative = 0;
  REQUIRE(parsed("-9223372036854775808").toInt64(negative));
  CHECK(negative == std::numeric_limits<std::int64_t>::min());
  CHECK_FALSE(parsed("18446744073709551616").toUInt64(back));  // out of range
  CHECK_FALSE(parsed("1.5").toUInt64(back));
  CHECK_FALSE(parsed("-1").toUInt64(back));
  float f = 0.0f;
  CHECK_FALSE(parsed("1e39").toFloat(f));  // not a finite float
}

TEST_CASE("json: strings escape and unescape, including \\u and surrogate pairs", "[remote][json]") {
  const std::string text = std::string("q\"b\\n\nt\tc") + '\x01' + "\xC3\xA9";
  CHECK(write(Value::string(text)) == "\"q\\\"b\\\\n\\nt\\tc\\u0001\xC3\xA9\"");
  CHECK(parsed(write(Value::string(text))).asString() == text);
  CHECK(parsed(R"("\u00e9\/\ud83d\ude00")").asString() == "\xC3\xA9/\xF0\x9F\x98\x80");
}

TEST_CASE("json: whitespace is accepted around tokens, and parse(write(v)) == v", "[remote][json]") {
  const Value v = parsed(" { \"a\" : [ 1 , 2.5e3 , { } ] , \"b\" : null }\r\n");
  CHECK(write(v) == R"({"a":[1,2.5e3,{}],"b":null})");
  CHECK(parsed(write(v)) == v);
}

TEST_CASE("json: malformed and hostile input is refused with an offset", "[remote][json]") {
  for (const std::string_view bad :
       {"", "{", "[1,]", "{\"a\"}", "{\"a\":1,}", "01", "1.", "-", "1e", "tru", "\"unterminated", "\"\x01\"",
        "\"\\x\"", "\"\\ud800\"", "\"\\udc00\"", "[1] 2", "{'a':1}", "nan"}) {
    INFO(bad);
    CHECK(parse(bad).isErr());
  }
  std::string deep(65, '[');
  deep += std::string(65, ']');
  CHECK(parse(deep).isErr());
  std::string ok(64, '[');
  ok += std::string(64, ']');
  CHECK(parse(ok).isOk());
  CHECK(parse("[1,x]").error().offset == 3);
}
