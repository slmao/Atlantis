#include <atlantis/assert.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <utility>
#include <vector>

namespace {

struct RecordedFailure {
  std::string expression;
  std::string message;
};

}  // namespace

TEST_CASE("ATLANTIS_CHECK does not report on a passing condition", "[assert]") {
  std::vector<RecordedFailure> recorded;
  auto previous = atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
    recorded.push_back({std::string(info.expression), std::string(info.message)});
  });

  ATLANTIS_CHECK(1 + 1 == 2);

  REQUIRE(recorded.empty());

  atlantis::assertions::setFailureHandler(std::move(previous));
}

TEST_CASE("ATLANTIS_CHECK reports on a failing condition without aborting", "[assert]") {
  std::vector<RecordedFailure> recorded;
  auto previous = atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
    recorded.push_back({std::string(info.expression), std::string(info.message)});
  });

  ATLANTIS_CHECK(1 == 2);

  REQUIRE(recorded.size() == 1);
  REQUIRE(recorded[0].expression == "1 == 2");

  atlantis::assertions::setFailureHandler(std::move(previous));
}

TEST_CASE("ATLANTIS_CHECK_MSG carries the supplied message", "[assert]") {
  std::vector<RecordedFailure> recorded;
  auto previous = atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
    recorded.push_back({std::string(info.expression), std::string(info.message)});
  });

  ATLANTIS_CHECK_MSG(false, "custom message");

  REQUIRE(recorded.size() == 1);
  REQUIRE(recorded[0].message == "custom message");

  atlantis::assertions::setFailureHandler(std::move(previous));
}

#if !defined(NDEBUG)
TEST_CASE("ATLANTIS_ASSERT reports on a failing condition in Debug builds", "[assert]") {
  std::vector<RecordedFailure> recorded;
  auto previous = atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
    recorded.push_back({std::string(info.expression), std::string(info.message)});
  });

  ATLANTIS_ASSERT(1 == 2);

  REQUIRE(recorded.size() == 1);

  atlantis::assertions::setFailureHandler(std::move(previous));
}
#endif
