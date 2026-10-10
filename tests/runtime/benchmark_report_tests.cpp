#include "../../src/runtime/benchmark_report.h"
#include "../../src/runtime/cli.h"

#include <atlantis/connection/json.h>
#include <atlantis/profile_recorder.h>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Spec 0059 R9, R11 / ADR-0117 D6-D7: the result document's schema, its
// statistics and its comparison with an earlier document -- on synthetic frames,
// with no device.

namespace {

namespace bm = atlantis::runtime::benchmark;
namespace json = atlantis::connection::json;
namespace profile = atlantis::profile;

constexpr std::uint64_t kMs = 1'000'000;

// A presented frame with the given CPU and GPU times, and a few counters.
bm::MeasuredFrame makeFrame(std::uint64_t id, double cpuMs, double waitPrevMs, double gpuDrawMs, double frameMs,
                            bool withGpu = true) {
  bm::MeasuredFrame frame;
  profile::FrameRecord& r = frame.record;
  r.frameId = id;
  r.presented = true;
  r.closed = true;
  r.zoneNs[profile::internName("Frame")] = static_cast<std::uint64_t>(cpuMs * kMs);
  r.zoneCount[profile::internName("Frame")] = 1;
  r.zoneNs[profile::internName("WaitPreviousGpu")] = static_cast<std::uint64_t>(waitPrevMs * kMs);
  r.zoneCount[profile::internName("WaitPreviousGpu")] = 1;
  r.zoneNs[profile::internName("RecordPass:draw")] = static_cast<std::uint64_t>(2 * kMs);
  r.zoneCount[profile::internName("RecordPass:draw")] = 1;
  const auto setCounter = [&r](const char* name, double v) {
    r.counter[profile::internName(name)] = v;
    r.counterSet.set(profile::internName(name));
  };
  setCounter("draw_calls", 100);
  setCounter("triangles", 5000);
  setCounter("renderables_total", 10);
  setCounter("renderables_drawn", 8);
  if (withGpu) {
    r.gpuDelivered = true;
    r.regionCount = 3;
    r.regions[0] = {profile::internName("shadow"), 0, 0, 1 * kMs};
    r.regions[1] = {profile::internName("draw"), 0, 1 * kMs, static_cast<std::uint64_t>((1 + gpuDrawMs) * kMs)};
    r.regions[2] = {profile::internName("Opaque"), 1, 1 * kMs, static_cast<std::uint64_t>((1 + gpuDrawMs / 2) * kMs)};
  }
  frame.frameMs = frameMs;
  return frame;
}

bm::ReportInput makeInput(std::size_t frames = 100) {
  bm::ReportInput input;
  input.build.type = "Release";
  input.build.compiler = "MSVC test";
  input.build.profiling = true;
  input.build.gpuMarkers = true;
  input.system.os = "windows";
  input.system.cpu = "Test CPU";
  input.device.gpu = "Test GPU";
  input.device.driver = "1.2.3";
  input.device.vulkanApi = "1.3.0";
  input.device.timestampsSupported = true;
  input.device.timestampPeriodNs = 1.0;
  input.device.presentMode = "FIFO";
  input.device.refreshHz = 60.0;
  input.scene = {"bistro", "11111111-2222-4333-8444-555555555555", "abc123"};
  input.settings = {1920, 1080, 120, static_cast<unsigned int>(frames), "baseline", true};
  for (std::size_t i = 0; i < frames; ++i) {
    // frameMs 1..N, so the nearest-rank percentiles are known: p50 = N/2 ...
    input.frames.push_back(makeFrame(i + 1, 10.0 + static_cast<double>(i), 2.0, 5.0, static_cast<double>(i + 1)));
  }
  return input;
}

[[nodiscard]] double number(const json::Value* value) {
  REQUIRE(value != nullptr);
  REQUIRE(value->isNumber());
  return std::stod(value->numberText());
}

}  // namespace

TEST_CASE("the document has the schema's top-level shape in order", "[runtime][benchmark][report]") {
  const json::Value doc = bm::buildDocument(makeInput());
  REQUIRE(doc.isObject());
  std::vector<std::string> keys;
  for (const auto& [key, value] : doc.asObject()) keys.push_back(key);
  CHECK(keys == std::vector<std::string>{"schema", "meta", "scene", "settings", "frames", "metrics"});
  CHECK(doc.find("schema")->asString() == "atlantis.benchmark/1");
  CHECK(doc.find("scene")->find("name")->asString() == "bistro");
  CHECK(doc.find("scene")->find("content_pin")->asString() == "abc123");
  CHECK(number(doc.find("settings")->find("resolution")->find("width")) == 1920);
  CHECK(number(doc.find("frames")->find("measured")) == 100);
  CHECK(doc.find("meta")->find("build")->find("type")->asString() == "Release");
  CHECK(doc.find("meta")->find("system")->find("gpu")->asString() == "Test GPU");
  CHECK(doc.find("meta")->find("display")->find("present_mode")->asString() == "FIFO");
  CHECK(number(doc.find("meta")->find("display")->find("refresh_hz")) == 60.0);
}

TEST_CASE("metrics carry nearest-rank statistics", "[runtime][benchmark][report]") {
  const json::Value doc = bm::buildDocument(makeInput());
  const json::Value* frameMs = doc.find("metrics")->find("frame_ms");
  REQUIRE(frameMs != nullptr);
  CHECK(number(frameMs->find("n")) == 100);
  CHECK(number(frameMs->find("p50")) == 50.0);  // 1..100
  CHECK(number(frameMs->find("p95")) == 95.0);
  CHECK(number(frameMs->find("p99")) == 99.0);
  CHECK(number(frameMs->find("avg")) == 50.5);
  CHECK(number(frameMs->find("min")) == 1.0);
  CHECK(number(frameMs->find("max")) == 100.0);

  const json::Value* metrics = doc.find("metrics");
  // cpu_active_ms = Frame - waits: (10 + i) - 2, so p50 is 10 + 49 - 2.
  CHECK(number(metrics->find("cpu_active_ms")->find("p50")) == 57.0);
  CHECK(number(metrics->find("wait_previous_gpu_ms")->find("avg")) == 2.0);
  CHECK(number(metrics->find("draw_calls")->find("p50")) == 100.0);
  CHECK(number(metrics->find("renderables_skipped")->find("p50")) == 2.0);  // total - drawn
  // GPU: the depth-0 regions are the passes (shadow 1 ms + draw 5 ms).
  CHECK(number(metrics->find("gpu_busy_ms")->find("p50")) == 6.0);
  CHECK(number(metrics->find("gpu_span_ms")->find("p50")) == 6.0);
  CHECK(number(metrics->find("passes")->find("draw")->find("gpu_ms")->find("p50")) == 5.0);
  CHECK(number(metrics->find("passes")->find("draw")->find("cpu_record_ms")->find("p50")) == 2.0);
  CHECK(number(metrics->find("passes")->find("Opaque")->find("gpu_ms")->find("p50")) == 2.5);
  CHECK(number(metrics->find("zones")->find("Frame")->find("n")) == 100);
}

TEST_CASE("a metric the build or hardware cannot supply is unavailable with a reason, never zero",
          "[runtime][benchmark][report]") {
  // No pipeline-statistics counters were written, so they are unavailable.
  const json::Value doc = bm::buildDocument(makeInput());
  const json::Value* fragments = doc.find("metrics")->find("fragment_invocations");
  REQUIRE(fragments != nullptr);
  REQUIRE(fragments->find("unavailable") != nullptr);
  CHECK(fragments->find("p50") == nullptr);

  // GPU timing off: the reason says so.
  bm::ReportInput off = makeInput(10);
  off.settings.gpuTiming = false;
  for (auto& frame : off.frames) frame.record.gpuDelivered = false;
  const json::Value offDoc = bm::buildDocument(off);
  CHECK(offDoc.find("metrics")->find("gpu_busy_ms")->find("unavailable")->asString() == "disabled by --gpu-timing=off");

  // A device with no timestamps.
  bm::ReportInput unsupported = makeInput(10);
  unsupported.device.timestampsSupported = false;
  for (auto& frame : unsupported.frames) frame.record.gpuDelivered = false;
  const json::Value unsupportedDoc = bm::buildDocument(unsupported);
  CHECK(unsupportedDoc.find("metrics")->find("gpu_busy_ms")->find("unavailable")->asString().find("cannot write timestamps") !=
        std::string::npos);
  CHECK(unsupportedDoc.find("meta")->find("system")->find("timestamp_period_ns")->isNull());
}

TEST_CASE("Bloom is derived as the sum of the bloom_* passes", "[runtime][benchmark][report]") {
  bm::ReportInput input = makeInput(5);
  for (auto& frame : input.frames) {
    profile::FrameRecord& r = frame.record;
    r.regions[r.regionCount++] = {profile::internName("bloom_down_1"), 0, 10 * kMs, 11 * kMs};
    r.regions[r.regionCount++] = {profile::internName("bloom_up_1"), 0, 11 * kMs, 13 * kMs};
  }
  const json::Value doc = bm::buildDocument(input);
  const json::Value* bloom = doc.find("metrics")->find("passes")->find("Bloom");
  REQUIRE(bloom != nullptr);
  CHECK(number(bloom->find("gpu_ms")->find("p50")) == 3.0);
}

TEST_CASE("the document is deterministic and --series adds the raw values", "[runtime][benchmark][report]") {
  const bm::ReportInput input = makeInput(20);
  CHECK(json::write(bm::buildDocument(input)) == json::write(bm::buildDocument(input)));
  CHECK(bm::buildDocument(input).find("series") == nullptr);

  bm::ReportInput withSeries = input;
  withSeries.includeSeries = true;
  const json::Value doc = bm::buildDocument(withSeries);
  const json::Value* series = doc.find("series");
  REQUIRE(series != nullptr);
  REQUIRE(series->find("frame_ms") != nullptr);
  CHECK(series->find("frame_ms")->asArray().size() == 20);
  CHECK(number(&series->find("frame_ms")->asArray()[19]) == 20.0);
  // And it parses back.
  CHECK(json::parse(json::write(doc)).isOk());
}

TEST_CASE("a document compared with itself is comparable with zero deltas", "[runtime][benchmark][compare]") {
  const json::Value doc = bm::buildDocument(makeInput());
  const json::Value comparison = bm::compareDocuments(doc, doc);
  CHECK(comparison.find("comparable")->asBool());
  CHECK(comparison.find("reasons")->asArray().empty());
  const json::Value* metric = comparison.find("metrics")->find("frame_ms");
  REQUIRE(metric != nullptr);
  CHECK(number(metric->find("p50")->find("delta")) == 0.0);
  CHECK(number(metric->find("p50")->find("delta_pct")) == 0.0);
  CHECK(number(metric->find("noise")->find("current")) == 45.0);  // p95 - p50
  CHECK(comparison.find("metrics")->find("passes.draw.gpu_ms") != nullptr);
  CHECK(comparison.find("metrics")->find("zones.Frame") != nullptr);
  CHECK(comparison.find("metrics")->find("fragment_invocations") == nullptr);  // unavailable on both sides
}

TEST_CASE("a slower run shows its change, without a verdict", "[runtime][benchmark][compare]") {
  bm::ReportInput slower = makeInput();
  for (auto& frame : slower.frames) frame.frameMs *= 2.0;
  const json::Value comparison = bm::compareDocuments(bm::buildDocument(makeInput()), bm::buildDocument(slower));
  CHECK(comparison.find("comparable")->asBool());
  const json::Value* p50 = comparison.find("metrics")->find("frame_ms")->find("p50");
  CHECK(number(p50->find("baseline")) == 50.0);
  CHECK(number(p50->find("current")) == 100.0);
  CHECK(number(p50->find("delta")) == 50.0);
  CHECK(number(p50->find("delta_pct")) == 100.0);
  CHECK(comparison.find("verdict") == nullptr);
}

TEST_CASE("each metadata mismatch makes the documents incomparable, with its reason", "[runtime][benchmark][compare]") {
  const json::Value baseline = bm::buildDocument(makeInput());
  const auto reasonsFor = [&baseline](const bm::ReportInput& changed) {
    const json::Value comparison = bm::compareDocuments(baseline, bm::buildDocument(changed));
    std::vector<std::string> reasons;
    for (const json::Value& reason : comparison.find("reasons")->asArray()) reasons.push_back(reason.asString());
    return std::pair{comparison.find("comparable")->asBool(), reasons};
  };

  const auto expectMismatch = [&](const bm::ReportInput& changed, const std::string& field) {
    const auto [comparable, reasons] = reasonsFor(changed);
    INFO(field);
    CHECK_FALSE(comparable);
    REQUIRE(reasons.size() == 1);
    CHECK(reasons[0].find(field) == 0);
  };

  bm::ReportInput variant = makeInput();
  variant.settings.variant = "no-fog";
  expectMismatch(variant, "settings.variant");
  bm::ReportInput resolution = makeInput();
  resolution.settings.width = 1280;
  expectMismatch(resolution, "settings.resolution.width");
  bm::ReportInput gpu = makeInput();
  gpu.device.gpu = "Other GPU";
  expectMismatch(gpu, "meta.system.gpu");
  bm::ReportInput driver = makeInput();
  driver.device.driver = "9.9.9";
  expectMismatch(driver, "meta.system.driver");
  bm::ReportInput build = makeInput();
  build.build.type = "Debug";
  expectMismatch(build, "meta.build.type");
  bm::ReportInput content = makeInput();
  content.scene.contentPin = "different";
  expectMismatch(content, "scene.content_pin");
  bm::ReportInput scene = makeInput();
  scene.scene.guid = "99999999-2222-4333-8444-555555555555";
  expectMismatch(scene, "scene.guid");
  bm::ReportInput timing = makeInput();
  timing.settings.gpuTiming = false;
  expectMismatch(timing, "settings.gpu_timing");
}

TEST_CASE("the summary names the scene and the headline metrics", "[runtime][benchmark][report]") {
  const std::string text = bm::summarize(bm::buildDocument(makeInput()));
  CHECK(text.find("scene bistro") != std::string::npos);
  CHECK(text.find("1920x1080") != std::string::npos);
  CHECK(text.find("frame_ms") != std::string::npos);
  CHECK(text.find("gpu_busy_ms") != std::string::npos);
}

TEST_CASE("the command line's benchmark flags parse, conflict and default as ruled", "[runtime][benchmark][cli]") {
  using namespace atlantis::runtime::cli;
  const std::array<SceneWhitelistEntry, 2> whitelist{{{"integrated_showcase_demo", {}}, {"bistro", {}}}};
  const auto parse = [&](std::vector<const char*> args) {
    args.insert(args.begin(), "atlantis_runtime");
    return parseCommandLine(static_cast<int>(args.size()), const_cast<char**>(args.data()), whitelist);
  };

  const auto minimal = parse({"--benchmark", "bistro"});
  REQUIRE(minimal.outcome == CommandLineOutcome::RunScene);
  REQUIRE(minimal.benchmark.has_value());
  CHECK(minimal.benchmark->sceneName == "bistro");
  CHECK(minimal.benchmark->scene.has_value());
  CHECK(minimal.benchmark->warmup == 120);
  CHECK(minimal.benchmark->frames == 300);
  CHECK(minimal.benchmark->width == 1920);
  CHECK(minimal.benchmark->height == 1080);
  CHECK(minimal.benchmark->variant == BenchmarkVariant::Baseline);
  CHECK(minimal.benchmark->gpuTiming);
  CHECK_FALSE(minimal.benchmark->json);
  CHECK_FALSE(minimal.selectedScene.has_value());

  const auto full = parse({"--benchmark", "bistro", "--warmup", "5", "--frames", "20", "--resolution", "1280x720",
                           "--variant", "no-fog", "--json", "--output", "out.json", "--baseline", "base.json",
                           "--series", "--gpu-timing=off"});
  REQUIRE(full.outcome == CommandLineOutcome::RunScene);
  CHECK(full.benchmark->warmup == 5);
  CHECK(full.benchmark->frames == 20);
  CHECK(full.benchmark->width == 1280);
  CHECK(full.benchmark->height == 720);
  CHECK(full.benchmark->variant == BenchmarkVariant::NoFog);
  CHECK(full.benchmark->json);
  CHECK(*full.benchmark->outputPath == "out.json");
  CHECK(*full.benchmark->baselinePath == "base.json");
  CHECK(full.benchmark->series);
  CHECK_FALSE(full.benchmark->gpuTiming);

  // A scene that is not on the whitelist parses (it is an environment problem).
  const auto absent = parse({"--benchmark", "no_such_scene"});
  REQUIRE(absent.outcome == CommandLineOutcome::RunScene);
  CHECK_FALSE(absent.benchmark->scene.has_value());

  for (const auto& args : std::vector<std::vector<const char*>>{
           {"--benchmark", "bistro", "--scene", "bistro"},
           {"--benchmark", "bistro", "--exec", "x"},
           {"--benchmark", "bistro", "--listen", "0"},
           {"--benchmark", "bistro", "--editor"},
           {"--benchmark", "bistro", "--help"},
           {"--benchmark"},
           {"--benchmark", "bistro", "--warmup", "x"},
           {"--benchmark", "bistro", "--frames", "0"},
           {"--benchmark", "bistro", "--resolution", "1920"},
           {"--benchmark", "bistro", "--resolution", "0x1080"},
           {"--benchmark", "bistro", "--variant", "no-shadows"},
           {"--benchmark", "bistro", "--gpu-timing=maybe"},
           {"--benchmark", "bistro", "--benchmark", "bistro"},
           {"--json"},
           {"--warmup", "5"},
           {"--gpu-timing=off"},
       }) {
    INFO(args.size());
    CHECK(parse(args).outcome == CommandLineOutcome::PrintErrorAndExit);
  }
}
