#include <atlantis/platform/clock.h>

#include <catch2/catch_test_macros.hpp>

#include <thread>

TEST_CASE("monotonicNow is monotonically non-decreasing", "[platform][clock]") {
  const auto first = atlantis::platform::monotonicNow();
  std::this_thread::sleep_for(std::chrono::milliseconds(1));
  const auto second = atlantis::platform::monotonicNow();
  REQUIRE(second >= first);
}
