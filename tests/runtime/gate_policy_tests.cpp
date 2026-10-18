#include "../../src/runtime/gate_policy.h"

#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

// Spec 0060 R8 / ADR-0119 D3 (Plan 0060 M2, P5): the policy file's loader and the
// metric-key matching, on the committed file and on hand-made documents.

namespace {

namespace gate = atlantis::runtime::gate;
namespace json = atlantis::connection::json;

constexpr const char* kValidPolicy = R"({
  "version": 1,
  "protocol": {"runs": 5, "preheat_seconds": 60, "k": 3, "child_timeout_seconds": 300, "pin_process": true, "require_mains_power": true,
               "idle": {"cpu_idle_pct": 90, "window_seconds": 10, "timeout_seconds": 120}},
  "dispersion_factor": 3,
  "dispersion_metrics": ["cpu_active_ms", "gpu_busy_ms"],
  "tiers": {"counter": {"abs_floor": 0, "max_pct": 0, "metric_tolerance_pct": {"fragment_invocations": 0.1}},
            "cpu": {"abs_floor": 0.1, "max_pct": 5}, "gpu": {"abs_floor": 0.25, "max_pct": 10}},
  "gated": {"counter": ["draw_calls"], "cpu": ["cpu_active_ms", "passes.*.cpu_record_ms"], "gpu": ["gpu_busy_ms"]},
  "reported": ["frame_ms", "wait_*"],
  "workload": ["lights_point"],
  "canary": {"max_shift_pct": 8, "required": true, "settle_seconds": 3},
  "record": {"max_mdr_cpu_pct": 10, "max_mdr_gpu_pct": 20, "max_canary_spread_pct": 5},
  "measured": {}
})";

json::Value parseOrDie(const std::string& text) {
  const auto parsed = json::parse(text);
  REQUIRE(parsed.isOk());
  return parsed.value();
}

// The valid policy with one thing replaced: `needle` -> `replacement`.
std::string mutated(const std::string& needle, const std::string& replacement) {
  std::string text = kValidPolicy;
  const auto at = text.find(needle);
  REQUIRE(at != std::string::npos);
  return text.replace(at, needle.size(), replacement);
}

}  // namespace

TEST_CASE("the committed policy file loads and describes the three tiers", "[runtime][gate][policy]") {
  const auto loaded = gate::loadPolicy(std::string(ATLANTIS_PERFORMANCE_DIR) + "/gate_policy.json");
  REQUIRE(loaded.isOk());
  const gate::GatePolicy& policy = loaded.value();
  CHECK(policy.version == 1);
  CHECK(policy.protocol.runs >= 1);
  CHECK(policy.protocol.k > 0.0);
  CHECK(policy.protocol.preheatSeconds == 60.0);  // the owner's ruling on the M7 stop (Q11)
  CHECK(policy.protocol.pinProcess);              // the children run pinned
  CHECK(policy.protocol.requireMainsPower);       // and only on mains power
  CHECK(policy.protocol.runs == 5);               // the locked calibration numbers (Plan 0060 M7)
  CHECK(policy.protocol.k == 3.0);
  CHECK(policy.cpu.maxPct == 5.0);
  CHECK(policy.gpu.maxPct == 10.0);
  CHECK(policy.dispersionFactor == 4.0);
  // Only the headline total can make a verdict inconclusive (Spec 0060 Corrections 2026-10-14 and 2026-10-17).
  REQUIRE(policy.dispersionMetrics.size() == 1);
  CHECK(gate::isDispersionMetric(policy, "cpu_active_ms"));
  CHECK_FALSE(gate::isDispersionMetric(policy, "gpu_busy_ms"));
  CHECK_FALSE(gate::isDispersionMetric(policy, "zones.Renderer"));
  CHECK_FALSE(gate::isDispersionMetric(policy, "passes.draw.cpu_record_ms"));
  CHECK_FALSE(gate::isDispersionMetric(policy, "passes.draw.gpu_ms"));
  // The record-time quality bar: above what the calibration study could resolve (cpu 5.4-6.5 %, gpu 10.6-19.1 %
  // for a 3-session baseline), below the recordings that motivated it (cpu 18.2 %, gpu 21.5 %).
  CHECK(policy.record.maxMdrCpuPct == 10.0);
  CHECK(policy.record.maxMdrGpuPct == 20.0);
  // The GPU canary: above its measured noise (0.1-1.7 % across child runs), far below the shifts it must catch.
  CHECK(policy.canary.maxShiftPct == 8.0);
  CHECK_FALSE(policy.canary.required);  // the GPU tier is report-only, so nothing needs the canary (sixth Correction)
  CHECK_FALSE(policy.record.requireCanary);  // and the store guard does not ask for one
  CHECK(policy.canary.settleSeconds == 3.0);  // past the benchmark's tail (about a second), see the policy file
  CHECK(policy.record.maxCanarySpreadPct == 5.0);
  CHECK(policy.measured.present());  // the calibration study that fixed them is named in the file
  CHECK_FALSE(policy.measured.date.empty());
  CHECK_FALSE(policy.measured.profile.empty());
  CHECK(policy.cpu.absFloor >= 0.0);
  CHECK(policy.gpu.maxPct > policy.cpu.maxPct);  // the noisier tier has the wider allowance
  CHECK(policy.counter.maxPct == 0.0);           // counters are exact
  CHECK_FALSE(policy.gatedCounter.empty());
  CHECK_FALSE(policy.gatedCpu.empty());
  CHECK(policy.gatedGpu.empty());  // Spec 0060 sixth Correction 2026-10-17: the GPU tier is report-only
  // Every metric the Spec names is in the tier the Spec names.
  CHECK(gate::gatedTier(policy, "draw_calls") == gate::Tier::Counter);
  CHECK(gate::gatedTier(policy, "descriptor_writes") == gate::Tier::Counter);
  CHECK(gate::gatedTier(policy, "cpu_active_ms") == gate::Tier::Cpu);
  CHECK(gate::gatedTier(policy, "zones.RenderExtraction") == gate::Tier::Cpu);
  CHECK(gate::gatedTier(policy, "passes.draw.cpu_record_ms") == gate::Tier::Cpu);
  // Every GPU time is reported and none is gated; a shadow pass's CPU time is still gated.
  CHECK_FALSE(gate::gatedTier(policy, "gpu_busy_ms").has_value());
  CHECK_FALSE(gate::gatedTier(policy, "passes.draw.gpu_ms").has_value());
  CHECK_FALSE(gate::gatedTier(policy, "passes.shadow.gpu_ms").has_value());
  CHECK(gate::isReported(policy, "gpu_busy_ms"));
  CHECK(gate::isReported(policy, "passes.draw.gpu_ms"));
  CHECK(gate::isReported(policy, "passes.shadow.gpu_ms"));
  CHECK(gate::gatedTier(policy, "passes.shadow.cpu_record_ms") == gate::Tier::Cpu);
  CHECK_FALSE(gate::gatedTier(policy, "frame_ms").has_value());
  CHECK(gate::isReported(policy, "frame_ms"));
  CHECK(gate::isReported(policy, "wait_previous_gpu_ms"));
  CHECK(gate::isWorkload(policy, "lights_point"));
  CHECK(gate::isWorkload(policy, "renderables_total"));
}

TEST_CASE("the all-tiers fixture still gates the GPU tier, so the engine's GPU path stays tested", "[runtime][gate][policy]") {
  const auto loaded = gate::loadPolicy(ATLANTIS_GATE_ALL_TIERS_POLICY);
  REQUIRE(loaded.isOk());
  const gate::GatePolicy& policy = loaded.value();
  CHECK(gate::gatedTier(policy, "gpu_busy_ms") == gate::Tier::Gpu);
  CHECK(gate::gatedTier(policy, "passes.draw.gpu_ms") == gate::Tier::Gpu);
  CHECK(gate::isDispersionMetric(policy, "gpu_busy_ms"));
  CHECK(policy.canary.required);
}

TEST_CASE("a valid policy parses into its numbers", "[runtime][gate][policy]") {
  const auto parsed = gate::parsePolicy(parseOrDie(kValidPolicy));
  REQUIRE(parsed.isOk());
  const gate::GatePolicy& policy = parsed.value();
  CHECK(policy.protocol.runs == 5);
  CHECK(policy.protocol.preheatSeconds == 60.0);
  CHECK(policy.protocol.pinProcess);
  CHECK(policy.protocol.requireMainsPower);
  CHECK(policy.protocol.idle.cpuIdlePct == 90.0);
  CHECK(policy.protocol.idle.timeoutSeconds == 120.0);
  CHECK(policy.dispersionFactor == 3.0);
  CHECK(policy.dispersionMetrics == std::vector<std::string>{"cpu_active_ms", "gpu_busy_ms"});
  CHECK(policy.record.maxMdrCpuPct == 10.0);
  CHECK(policy.record.maxMdrGpuPct == 20.0);
  CHECK(policy.canary.maxShiftPct == 8.0);
  CHECK(policy.canary.required);
  CHECK(policy.record.maxCanarySpreadPct == 5.0);
  CHECK(policy.cpu.absFloor == 0.1);
  CHECK(policy.gpu.maxPct == 10.0);
  CHECK(policy.counter.metricTolerancePct.at("fragment_invocations") == 0.1);
  CHECK_FALSE(policy.measured.present());
}

TEST_CASE("a measured block, once filled, is read back", "[runtime][gate][policy]") {
  const auto parsed = gate::parsePolicy(parseOrDie(
      mutated(R"("measured": {})", R"("measured": {"study": "calibration-1", "date": "2026-10-12", "profile": "p"})")));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().measured.present());
  CHECK(parsed.value().measured.study == "calibration-1");
  CHECK(parsed.value().measured.profile == "p");
}

TEST_CASE("a reported metric is never gated, even when a gated pattern also matches it", "[runtime][gate][policy]") {
  // The wildcard gates every pass's GPU time...
  std::string text = mutated(R"("gpu": ["gpu_busy_ms"])", R"("gpu": ["gpu_busy_ms", "passes.*.gpu_ms"])");
  const auto gatedAll = gate::parsePolicy(parseOrDie(text));
  REQUIRE(gatedAll.isOk());
  CHECK(gate::gatedTier(gatedAll.value(), "passes.shadow.gpu_ms") == gate::Tier::Gpu);
  // ...until one pass is named in `reported`: that one is out, the others stay in.
  const std::string needle = R"("reported": ["frame_ms", "wait_*"])";
  text.replace(text.find(needle), needle.size(), R"("reported": ["passes.shadow.gpu_ms", "wait_*"])");
  const auto demoted = gate::parsePolicy(parseOrDie(text));
  REQUIRE(demoted.isOk());
  CHECK_FALSE(gate::gatedTier(demoted.value(), "passes.shadow.gpu_ms").has_value());
  CHECK(gate::isReported(demoted.value(), "passes.shadow.gpu_ms"));
  CHECK(gate::gatedTier(demoted.value(), "passes.draw.gpu_ms") == gate::Tier::Gpu);
}

TEST_CASE("a malformed policy is refused, naming the member", "[runtime][gate][policy]") {
  const struct {
    const char* needle;
    const char* replacement;
    const char* mention;
  } cases[] = {
      {R"("protocol":)", R"("protocolx":)", "protocol"},
      {R"("pin_process": true,)", R"("pin_process": "yes",)", "pin_process"},
      {R"("pin_process": true,)", "", "pin_process"},
      {R"("require_mains_power": true,)", "", "require_mains_power"},
      {R"("require_mains_power": true,)", R"("require_mains_power": "yes",)", "require_mains_power"},
      {R"("k": 3,)", R"("k": 0,)", "'k'"},
      {R"("runs": 5,)", R"("runs": 0,)", "runs"},
      {R"("abs_floor": 0.1,)", R"("abs_floor": -1,)", "abs_floor"},
      {R"("max_pct": 10)", R"("max_pct": "ten")", "max_pct"},
      {R"("cpu_idle_pct": 90,)", R"("cpu_idle_pct": 140,)", "cpu_idle_pct"},
      {R"("gated": {)", R"("gatedx": {)", "gated"},
      {R"("gpu": ["gpu_busy_ms"])", R"("gpu": "gpu_busy_ms")", "gpu"},
      {R"("reported": ["frame_ms", "wait_*"])", R"("reported": [1])", "reported"},
      {R"("measured": {})", R"("measuredx": {})", "measured"},
      {R"("record": {"max_mdr_cpu_pct": 10, "max_mdr_gpu_pct": 20, "max_canary_spread_pct": 5},)", "", "record"},
      {R"("max_mdr_cpu_pct": 10,)", "", "max_mdr_cpu_pct"},
      {R"("canary": {"max_shift_pct": 8, "required": true, "settle_seconds": 3},)", "", "canary"},
      {R"("max_shift_pct": 8,)", "", "max_shift_pct"},
      {R"(, "settle_seconds": 3)", "", "settle_seconds"},
      {R"("settle_seconds": 3)", R"("settle_seconds": -1)", "settle_seconds"},
      {R"("max_shift_pct": 8,)", R"("max_shift_pct": 0,)", "max_shift_pct"},
      {R"("required": true)", R"("required": "yes")", "required"},
      {R"(, "max_canary_spread_pct": 5)", "", "max_canary_spread_pct"},
      {R"("max_canary_spread_pct": 5)", R"("max_canary_spread_pct": -1)", "max_canary_spread_pct"},
      {R"("max_mdr_gpu_pct": 20)", R"("max_mdr_gpu_pct": "twenty")", "max_mdr_gpu_pct"},
      {R"("max_mdr_cpu_pct": 10,)", R"("max_mdr_cpu_pct": 0,)", "bounds must be positive"},
      {R"("dispersion_factor": 3,)", R"("dispersion_factor": 0.5,)", "dispersion_factor"},
      {R"("dispersion_metrics": ["cpu_active_ms", "gpu_busy_ms"],)", "", "dispersion_metrics"},
      {R"("dispersion_metrics": ["cpu_active_ms", "gpu_busy_ms"],)", R"("dispersion_metrics": "cpu_active_ms",)", "dispersion_metrics"},
      {R"("dispersion_metrics": ["cpu_active_ms", "gpu_busy_ms"],)", R"("dispersion_metrics": [],)", "dispersion_metrics"},
      {R"("dispersion_metrics": ["cpu_active_ms", "gpu_busy_ms"],)", R"("dispersion_metrics": ["draw_calls"],)", "not a gated CPU or GPU metric"},
      {R"("dispersion_metrics": ["cpu_active_ms", "gpu_busy_ms"],)", R"("dispersion_metrics": ["frame_ms"],)", "not a gated CPU or GPU metric"},
      {R"("fragment_invocations": 0.1)", R"("fragment_invocations": -0.1)", "fragment_invocations"},
  };
  for (const auto& c : cases) {
    INFO(c.needle);
    const auto parsed = gate::parsePolicy(parseOrDie(mutated(c.needle, c.replacement)));
    REQUIRE(parsed.isErr());
    CHECK(parsed.error().find(c.mention) != std::string::npos);
  }
  CHECK(gate::parsePolicy(json::Value::array()).isErr());
}

TEST_CASE("a missing or unreadable policy file is an error, not a default", "[runtime][gate][policy]") {
  CHECK(gate::loadPolicy(std::string(ATLANTIS_PERFORMANCE_DIR) + "/no_such_policy.json").isErr());
}

TEST_CASE("a metric pattern's star stands for one dotted segment", "[runtime][gate][policy]") {
  CHECK(gate::matchesPattern("cpu_active_ms", "cpu_active_ms"));
  CHECK_FALSE(gate::matchesPattern("cpu_active_ms", "cpu_active_msx"));
  CHECK(gate::matchesPattern("passes.*.gpu_ms", "passes.draw.gpu_ms"));
  CHECK(gate::matchesPattern("passes.*.gpu_ms", "passes.bloom_down_1.gpu_ms"));
  CHECK_FALSE(gate::matchesPattern("passes.*.gpu_ms", "passes.draw.cpu_record_ms"));
  CHECK_FALSE(gate::matchesPattern("passes.*.gpu_ms", "passes.a.b.gpu_ms"));  // a star never spans a dot
  CHECK(gate::matchesPattern("wait_*", "wait_previous_gpu_ms"));
  CHECK(gate::matchesPattern("wait_*", "wait_"));
  CHECK_FALSE(gate::matchesPattern("wait_*", "acquire_ms"));
  CHECK(gate::matchesPattern("zones.RecordPass:*", "zones.RecordPass:draw"));
  CHECK(gate::matchesPattern("", ""));
  CHECK_FALSE(gate::matchesPattern("", "x"));
  CHECK_FALSE(gate::matchesPattern("x", ""));
}

TEST_CASE("a tier name prints as written in the policy", "[runtime][gate][policy]") {
  CHECK(gate::toString(gate::Tier::Counter) == "counter");
  CHECK(gate::toString(gate::Tier::Cpu) == "cpu");
  CHECK(gate::toString(gate::Tier::Gpu) == "gpu");
}
