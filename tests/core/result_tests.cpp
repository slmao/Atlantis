#include <atlantis/result.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("Result stores and exposes an Ok value", "[result]") {
  auto result = atlantis::Result<int, std::string>::Ok(42);
  REQUIRE(result.isOk());
  REQUIRE_FALSE(result.isErr());
  REQUIRE(result.value() == 42);
}

TEST_CASE("Result stores and exposes an Err value", "[result]") {
  auto result = atlantis::Result<int, std::string>::Err("bad input");
  REQUIRE(result.isErr());
  REQUIRE_FALSE(result.isOk());
  REQUIRE(result.error() == "bad input");
}

TEST_CASE("Result value() is mutable for an Ok result", "[result]") {
  auto result = atlantis::Result<int, std::string>::Ok(1);
  result.value() = 2;
  REQUIRE(result.value() == 2);
}
