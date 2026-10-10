#include <atlantis/profile_recorder.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

// Spec 0059 / ADR-0114 D4: the recorder's own behavior, driven directly (not
// through the macros) on a fake clock, so it runs in every build configuration.

namespace {

using namespace atlantis::profile;

std::uint64_t gNow = 0;
std::uint64_t fakeClock() { return gNow; }

struct RecorderFixture {
  RecorderFixture() {
    gNow = 0;
    setClockForTesting(&fakeClock);
    startRecording();
  }
  ~RecorderFixture() {
    stopRecording();
    setClockForTesting(nullptr);
  }
};

[[nodiscard]] FrameRecord frame(std::uint64_t id) {
  FrameRecord record;
  REQUIRE(readFrame(id, record));
  return record;
}

}  // namespace

TEST_CASE("names intern to stable dense ids", "[profile][recorder]") {
  const NameId a = internName("recorder_test_a");
  const NameId b = internName("recorder_test_b");
  CHECK(a != b);
  CHECK(internName("recorder_test_a") == a);
  CHECK(nameOf(a) == "recorder_test_a");
  CHECK(nameOf(b) == "recorder_test_b");
  CHECK(nameOf(kInvalidName).empty());
}

TEST_CASE("frames are numbered from the first beginFrame and stay monotonic across recordings",
          "[profile][recorder]") {
  RecorderFixture fixture;
  beginFrame();
  const std::uint64_t first = currentFrameId();
  beginFrame();
  CHECK(currentFrameId() == first + 1);
  CHECK(frame(first).closed);
  CHECK_FALSE(frame(first + 1).closed);

  stopRecording();
  startRecording();
  beginFrame();
  CHECK(currentFrameId() > first + 1);
  FrameRecord old;
  CHECK_FALSE(readFrame(first, old));  // the ring was cleared
}

TEST_CASE("nothing is recorded while stopped", "[profile][recorder]") {
  RecorderFixture fixture;
  stopRecording();
  const std::uint64_t before = currentFrameId();
  beginFrame();
  CHECK(currentFrameId() == before);
  CHECK_FALSE(isRecording());
}

TEST_CASE("same-named zones in one frame are summed; nesting counts the outermost only", "[profile][recorder]") {
  RecorderFixture fixture;
  const NameId zone = internName("recorder_test_zone");
  beginFrame();

  gNow = 100;
  const auto s1 = zoneBegin(zone);
  gNow = 130;
  zoneEnd(zone, s1);  // 30

  gNow = 200;
  const auto s2 = zoneBegin(zone);
  gNow = 210;
  const auto inner = zoneBegin(zone);  // nested: not counted again
  gNow = 220;
  zoneEnd(zone, inner);
  gNow = 260;
  zoneEnd(zone, s2);  // 60

  const FrameRecord record = frame(currentFrameId());
  CHECK(record.zoneNs[zone] == 90);
  CHECK(record.zoneCount[zone] == 2);
}

TEST_CASE("a Present zone marks the frame presented", "[profile][recorder]") {
  RecorderFixture fixture;
  const NameId present = internName("Present");
  beginFrame();
  const std::uint64_t skipped = currentFrameId();
  beginFrame();
  const std::uint64_t presented = currentFrameId();
  zoneEnd(present, zoneBegin(present));
  CHECK_FALSE(frame(skipped).presented);
  CHECK(frame(presented).presented);
}

TEST_CASE("a zone that ends after stopRecording still balances", "[profile][recorder]") {
  RecorderFixture fixture;
  const NameId zone = internName("recorder_test_balance");
  beginFrame();
  const auto start = zoneBegin(zone);
  stopRecording();
  zoneEnd(zone, start);  // must not underflow the depth
  startRecording();
  beginFrame();
  gNow = 5;
  const auto again = zoneBegin(zone);
  gNow = 9;
  zoneEnd(zone, again);
  CHECK(frame(currentFrameId()).zoneNs[zone] == 4);
}

TEST_CASE("a gauge keeps the last write, an accumulator sums, and unset counters stay unset",
          "[profile][recorder]") {
  RecorderFixture fixture;
  const NameId gauge = internName("recorder_test_gauge");
  const NameId sum = internName("recorder_test_sum");
  const NameId untouched = internName("recorder_test_untouched");
  beginFrame();
  counterValue(gauge, 3.0);
  counterValue(gauge, 9.0);
  counterAdd(sum, 2.0);
  counterAdd(sum, 3.0);
  const FrameRecord record = frame(currentFrameId());
  CHECK(record.counter[gauge] == 9.0);
  CHECK(record.counter[sum] == 5.0);
  CHECK(record.counterSet.test(gauge));
  CHECK(record.counterSet.test(sum));
  CHECK_FALSE(record.counterSet.test(untouched));

  beginFrame();  // accumulators restart with each frame
  counterAdd(sum, 1.0);
  CHECK(frame(currentFrameId()).counter[sum] == 1.0);
}

TEST_CASE("GPU regions arrive one frame late and land in the earlier frame", "[profile][recorder]") {
  RecorderFixture fixture;
  beginFrame();
  const std::uint64_t first = currentFrameId();
  beginFrame();  // the next frame has begun

  const std::vector<GpuRegion> regions{{internName("shadow"), 0, 1000, 5000}, {internName("draw"), 0, 5000, 20000}};
  CHECK(deliverGpuRegions(first, regions, 0));
  const FrameRecord record = frame(first);
  CHECK(record.gpuDelivered);
  CHECK(record.regionCount == 2);
  CHECK(record.regions[1].endNs == 20000);
  CHECK(frame(currentFrameId()).regionCount == 0);
}

TEST_CASE("GPU results for a frame that left the ring are refused; overflow is counted", "[profile][recorder]") {
  RecorderFixture fixture;
  beginFrame();
  const std::uint64_t first = currentFrameId();
  for (std::size_t i = 0; i < kFrameRing; ++i) beginFrame();  // the ring has wrapped past `first`
  CHECK_FALSE(deliverGpuRegions(first, {}, 0));
  FrameRecord gone;
  CHECK_FALSE(readFrame(first, gone));

  const std::uint64_t current = currentFrameId();
  std::vector<GpuRegion> many(kMaxGpuRegions + 2);
  CHECK(deliverGpuRegions(current, many, 1));
  const FrameRecord record = frame(current);
  CHECK(record.regionCount == kMaxGpuRegions);
  CHECK(record.droppedRegions == 3);  // 2 beyond capacity + 1 reported by the caller
}

TEST_CASE("device info round-trips", "[profile][recorder]") {
  DeviceInfo info;
  info.gpu = "Test GPU";
  info.timestampPeriodNs = 1.5;
  info.timestampsSupported = true;
  setDeviceInfo(info);
  const DeviceInfo back = deviceInfo();
  CHECK(back.gpu == "Test GPU");
  CHECK(back.timestampPeriodNs == 1.5);
  CHECK(back.timestampsSupported);
  setDeviceInfo({});
}
