#include "../../src/runtime/benchmark_loop.h"

#include <atlantis/profile_recorder.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <vector>

// Spec 0059 R8 / ADR-0117 D4: the measured loop's accounting -- warmup, measured,
// skipped and the extra frame -- driven by a fake frame and a fake clock, so no
// device and no window are involved.

namespace {

namespace bm = atlantis::runtime::benchmark;
namespace profile = atlantis::profile;
using std::chrono::milliseconds;

std::uint64_t gRecorderNow = 0;
std::uint64_t recorderClock() { return gRecorderNow; }

struct FakeRun {
  FakeRun() {
    gRecorderNow = 0;
    profile::setClockForTesting(&recorderClock);
  }
  ~FakeRun() {
    profile::stopRecording();
    profile::setClockForTesting(nullptr);
  }

  // A frame the way the Runtime records one: a frame boundary, a Frame zone, and a
  // Present zone unless the frame is "skipped"; and GPU results for the PREVIOUS
  // frame delivered during this one, as submit() does.
  bool runOne(bool present) {
    ++calls;
    gRecorderNow += 1'000'000;  // 1 ms per frame
    profile::beginFrame();
    const std::uint64_t id = profile::currentFrameId();
    const auto frameZone = profile::internName("Frame");
    const auto start = profile::zoneBegin(frameZone);
    if (lastFrame != 0) {
      const std::vector<profile::GpuRegion> regions{{profile::internName("draw"), 0, 0, 2'000'000}};
      profile::deliverGpuRegions(lastFrame, regions, 0);
    }
    if (present) {
      const auto presentZone = profile::internName("Present");
      profile::zoneEnd(presentZone, profile::zoneBegin(presentZone));
    }
    profile::zoneEnd(frameZone, start);
    lastFrame = id;
    return !stopAt || calls < *stopAt;
  }

  int calls = 0;
  std::uint64_t lastFrame = 0;
  std::optional<int> stopAt;
};

bm::LoopHooks hooksFor(FakeRun& run, std::vector<bool> pattern, std::chrono::steady_clock::time_point& clock,
                       milliseconds perFrame = milliseconds(1)) {
  bm::LoopHooks hooks;
  hooks.now = [&clock] { return clock; };
  hooks.runFrame = [&run, pattern = std::move(pattern), &clock, perFrame]() mutable {
    clock += perFrame;
    const bool present = pattern.empty() ? true : pattern[static_cast<std::size_t>(run.calls) % pattern.size()];
    return run.runOne(present);
  };
  return hooks;
}

}  // namespace

TEST_CASE("the loop runs warmup then measured presented frames, then one extra frame", "[runtime][benchmark][loop]") {
  FakeRun run;
  std::chrono::steady_clock::time_point clock{};
  const auto result = bm::runMeasuredLoop({.warmup = 4, .frames = 10}, hooksFor(run, {}, clock));
  REQUIRE(result.status == bm::LoopStatus::Ok);
  CHECK(run.calls == 4 + 10 + 1);
  CHECK(result.warmupRecords == 4);
  CHECK(result.measuredRecords == 10);
  CHECK(result.presentedMeasured == 10);
  CHECK(result.skippedMeasured == 0);
  REQUIRE(result.records.size() == 15);  // the extra frame is the last record
  CHECK_FALSE(profile::isRecording());
}

TEST_CASE("every measured frame has its GPU results merged in, the last one by the extra frame",
          "[runtime][benchmark][loop]") {
  FakeRun run;
  std::chrono::steady_clock::time_point clock{};
  const auto result = bm::runMeasuredLoop({.warmup = 2, .frames = 5}, hooksFor(run, {}, clock));
  REQUIRE(result.status == bm::LoopStatus::Ok);
  for (std::size_t i = result.warmupRecords; i < result.warmupRecords + result.measuredRecords; ++i) {
    INFO(i);
    CHECK(result.records[i].gpuDelivered);
    CHECK(result.records[i].regionCount == 1);
  }
  CHECK_FALSE(result.records.back().gpuDelivered);  // the extra frame's own results never arrive
}

TEST_CASE("frames that never reach Present are counted as skipped and measured by none", "[runtime][benchmark][loop]") {
  FakeRun run;
  std::chrono::steady_clock::time_point clock{};
  // present, skip, present, skip, ... : every other frame is skipped.
  const auto result = bm::runMeasuredLoop({.warmup = 2, .frames = 6}, hooksFor(run, {true, false}, clock));
  REQUIRE(result.status == bm::LoopStatus::Ok);
  CHECK(result.presentedMeasured == 6);
  CHECK(result.skippedMeasured >= 5);
  std::size_t presented = 0;
  for (std::size_t i = result.warmupRecords; i < result.warmupRecords + result.measuredRecords; ++i) {
    if (result.records[i].presented) ++presented;
  }
  CHECK(presented == 6);
  CHECK(result.records[result.warmupRecords + result.measuredRecords - 1].presented);  // the last measured is presented
}

TEST_CASE("zero warmup measures from the first frame", "[runtime][benchmark][loop]") {
  FakeRun run;
  std::chrono::steady_clock::time_point clock{};
  const auto result = bm::runMeasuredLoop({.warmup = 0, .frames = 3}, hooksFor(run, {}, clock));
  REQUIRE(result.status == bm::LoopStatus::Ok);
  CHECK(result.warmupRecords == 0);
  CHECK(result.measuredRecords == 3);
}

TEST_CASE("a stopped frame function ends the run as Stopped", "[runtime][benchmark][loop]") {
  FakeRun run;
  run.stopAt = 3;
  std::chrono::steady_clock::time_point clock{};
  const auto result = bm::runMeasuredLoop({.warmup = 2, .frames = 10}, hooksFor(run, {}, clock));
  CHECK(result.status == bm::LoopStatus::Stopped);
  CHECK(run.calls == 3);
}

TEST_CASE("no presented frame within the timeout ends the run", "[runtime][benchmark][loop]") {
  FakeRun run;
  std::chrono::steady_clock::time_point clock{};
  // Nothing is ever presented (a minimized window); each "frame" takes a second of
  // fake time, so the 15 s timeout elapses after sixteen of them.
  const auto result =
      bm::runMeasuredLoop({.warmup = 2, .frames = 10}, hooksFor(run, {false}, clock, std::chrono::seconds(1)));
  CHECK(result.status == bm::LoopStatus::NoPresentedFrames);
  CHECK(run.calls >= 15);
  CHECK(run.calls <= 17);
  CHECK(result.presentedMeasured == 0);
}
