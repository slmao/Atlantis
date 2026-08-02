#include <atlantis/log.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {

struct CapturedMessage {
  atlantis::LogLevel level;
  std::string message;
};

class TestLogSink final : public atlantis::LogSink {
 public:
  void write(atlantis::LogLevel level, std::string_view message) override {
    captured.push_back({level, std::string(message)});
  }

  std::vector<CapturedMessage> captured;
};

}  // namespace

TEST_CASE("Logging dispatches messages to the active sink", "[log]") {
  auto sink = std::make_shared<TestLogSink>();
  atlantis::log::initialize(sink);
  atlantis::log::setMinLevel(atlantis::LogLevel::Trace);

  ATLANTIS_LOG_INFO("hello {}", "world");

  REQUIRE(sink->captured.size() == 1);
  REQUIRE(sink->captured[0].level == atlantis::LogLevel::Info);
  REQUIRE(sink->captured[0].message.find("hello world") != std::string::npos);

  atlantis::log::initialize(nullptr);
  atlantis::log::setMinLevel(atlantis::LogLevel::Trace);
}

TEST_CASE("Messages below the minimum level are filtered out", "[log]") {
  auto sink = std::make_shared<TestLogSink>();
  atlantis::log::initialize(sink);
  atlantis::log::setMinLevel(atlantis::LogLevel::Warn);

  ATLANTIS_LOG_INFO("should be filtered");
  ATLANTIS_LOG_ERROR("should pass through");

  REQUIRE(sink->captured.size() == 1);
  REQUIRE(sink->captured[0].level == atlantis::LogLevel::Error);

  atlantis::log::setMinLevel(atlantis::LogLevel::Trace);
  atlantis::log::initialize(nullptr);
}
