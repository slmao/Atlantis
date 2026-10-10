#include <atlantis/profile.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>
#include <vector>

// Spec 0059 R1/R2: the macros in this build's own configuration. With
// ATLANTIS_PROFILING they feed the recorder; without it they are no-ops (the
// forced-off expansion is proven in every configuration by
// profile_compiled_out_tests.cpp).

#if defined(ATLANTIS_PROFILING)

namespace {

using namespace atlantis::profile;

std::uint64_t gNow = 0;
std::uint64_t fakeClock() { return gNow; }

struct MacroFixture {
  MacroFixture() {
    gNow = 0;
    setClockForTesting(&fakeClock);
    startRecording();
  }
  ~MacroFixture() {
    stopRecording();
    setClockForTesting(nullptr);
  }
};

struct FakeCommandList {
  std::vector<std::string> events;
  void beginRegion(std::string_view name) { events.push_back("begin:" + std::string(name)); }
  void endRegion() { events.push_back("end"); }
};

}  // namespace

TEST_CASE("the macros feed the recorder: frame zone, scoped and named zones, counters", "[profile][macros]") {
  MacroFixture fixture;
  std::uint64_t frameId = 0;
  {
    gNow = 1000;
    ATL_PROFILE_FRAME();
    frameId = currentFrameId();
    {
      gNow = 1100;
      ATL_PROFILE_SCOPE("macro_test_scope");
      gNow = 1160;
    }
    gNow = 1200;
    ATL_PROFILE_SCOPE_NAMED(early, "macro_test_named");
    gNow = 1250;
    ATL_PROFILE_SCOPE_END(early);
    gNow = 1400;
    ATL_PROFILE_SCOPE_END(early);  // idempotent
    ATL_PROFILE_VALUE("macro_test_gauge", 4);
    ATL_PROFILE_ADD("macro_test_sum", 1);
    ATL_PROFILE_ADD("macro_test_sum", 2);
    ATL_PROFILE_SCOPE_DYNAMIC(std::string_view("macro_test_dynamic"));
    gNow = 1500;
  }
  FrameRecord record;
  REQUIRE(readFrame(frameId, record));
  CHECK(record.zoneNs[internName("Frame")] == 500);
  CHECK(record.zoneNs[internName("macro_test_scope")] == 60);
  CHECK(record.zoneNs[internName("macro_test_named")] == 50);
  CHECK(record.zoneNs[internName("macro_test_dynamic")] == 100);  // from gNow 1400 to 1500
  CHECK(record.counter[internName("macro_test_gauge")] == 4.0);
  CHECK(record.counter[internName("macro_test_sum")] == 3.0);
}

TEST_CASE("enabled macros evaluate their arguments exactly once", "[profile][macros]") {
  MacroFixture fixture;
  int calls = 0;
  ATL_PROFILE_FRAME();
  ATL_PROFILE_VALUE("macro_test_once", ++calls);
  ATL_PROFILE_ADD("macro_test_once_add", ++calls);
  CHECK(calls == 2);
}

TEST_CASE("macros are inert while the recorder is stopped", "[profile][macros]") {
  gNow = 0;
  setClockForTesting(&fakeClock);
  const std::uint64_t before = currentFrameId();
  {
    ATL_PROFILE_FRAME();
    ATL_PROFILE_VALUE("macro_test_stopped", 1);
  }
  CHECK(currentFrameId() == before);
  setClockForTesting(nullptr);
}

TEST_CASE("a GPU scope brackets begin/end on any command-list type", "[profile][macros]") {
  FakeCommandList cmd;
  {
    ATL_PROFILE_GPU_SCOPE(cmd, "pass");
    cmd.events.push_back("work");
  }
  REQUIRE(cmd.events.size() == 3);
  CHECK(cmd.events[0] == "begin:pass");
  CHECK(cmd.events[1] == "work");
  CHECK(cmd.events[2] == "end");
}

#else

TEST_CASE("the macros in a profiling-off build are no-ops (see the compiled-out tests)", "[profile][macros]") {
  int calls = 0;
  ATL_PROFILE_FRAME();
  ATL_PROFILE_VALUE("x", ++calls);
  CHECK(calls == 0);
}

#endif
