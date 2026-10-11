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
  "protocol": {"runs": 5, "preheat_seconds": 60, "k": 3, "child_timeout_seconds": 300, "pin_process": true,
               "idle": {"cpu_idle_pct": 90, "window_seconds": 10, "timeout_seconds": 120}},
  "dispersion_factor": 3,
  "tiers": {"counter": {"abs_floor": 0, "max_pct": 0, "metric_tolerance_pct": {"fragment_invocations": 0.1}},
            "cpu": {"abs_floor": 0.1, "max_pct": 5}, "gpu": {"abs_floor": 0.25, "max_pct": 10}},
  "gated": {"counter": ["draw_calls"], "cpu": ["cpu_active_ms", "passes.*.cpu_record_ms"], "gpu": ["gpu_busy_ms"]},
  "reported": ["frame_ms", "wait_*"],
  "workload": ["lights_point"],
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
  CHECK(policy.cpu.absFloor >= 0.0);
  CHECK(policy.gpu.maxPct > policy.cpu.maxPct);  // the noisier tier has the wider allowance
  CHECK(policy.counter.maxPct == 0.0);           // counters are exact
  CHECK_FALSE(policy.gatedCounter.empty());
  CHECK_FALSE(policy.gatedCpu.empty());
  CHECK_FALSE(policy.gatedGpu.empty());
  // Every metric the Spec names is in the tier the Spec names.
  CHECK(gate::gatedTier(policy, "draw_calls") == gate::Tier::Counter);
  CHECK(gate::gatedTier(policy, "descriptor_writes") == gate::Tier::Counter);
  CHECK(gate::gatedTier(policy, "cpu_active_ms") == gate::Tier::Cpu);
  CHECK(gate::gatedTier(policy, "zones.RenderExtraction") == gate::Tier::Cpu);
  CHECK(gate::gatedTier(policy, "passes.draw.cpu_record_ms") == gate::Tier::Cpu);
  CHECK(gate::gatedTier(policy, "gpu_busy_ms") == gate::Tier::Gpu);
  CHECK(gate::gatedTier(policy, "passes.draw.gpu_ms") == gate::Tier::Gpu);
  CHECK_FALSE(gate::gatedTier(policy, "frame_ms").has_value());
  CHECK(gate::isReported(policy, "frame_ms"));
  CHECK(gate::isReported(policy, "wait_previous_gpu_ms"));
  CHECK(gate::isWorkload(policy, "lights_point"));
  CHECK(gate::isWorkload(policy, "renderables_total"));
}

TEST_CASE("a valid policy parses into its numbers", "[runtime][gate][policy]") {
  const auto parsed = gate::parsePolicy(parseOrDie(kValidPolicy));
  REQUIRE(parsed.isOk());
  const gate::GatePolicy& policy = parsed.value();
  CHECK(policy.protocol.runs == 5);
  CHECK(policy.protocol.preheatSeconds == 60.0);
  CHECK(policy.protocol.pinProcess);
  CHECK(policy.protocol.idle.cpuIdlePct == 90.0);
  CHECK(policy.protocol.idle.timeoutSeconds == 120.0);
  CHECK(policy.dispersionFactor == 3.0);
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

TEST_CASE("a malformed policy is refused, naming the member", "[runtime][gate][policy]") {
  const struct {
    const char* needle;
    const char* replacement;
    const char* mention;
  } cases[] = {
      {R"("protocol":)", R"("protocolx":)", "protocol"},
      {R"("pin_process": true,)", R"("pin_process": "yes",)", "pin_process"},
      {R"("pin_process": true,)", "", "pin_process"},
      {R"("k": 3,)", R"("k": 0,)", "'k'"},
      {R"("runs": 5,)", R"("runs": 0,)", "runs"},
      {R"("abs_floor": 0.1,)", R"("abs_floor": -1,)", "abs_floor"},
      {R"("max_pct": 10)", R"("max_pct": "ten")", "max_pct"},
      {R"("cpu_idle_pct": 90,)", R"("cpu_idle_pct": 140,)", "cpu_idle_pct"},
      {R"("gated": {)", R"("gatedx": {)", "gated"},
      {R"("gpu": ["gpu_busy_ms"])", R"("gpu": "gpu_busy_ms")", "gpu"},
      {R"("reported": ["frame_ms", "wait_*"])", R"("reported": [1])", "reported"},
      {R"("measured": {})", R"("measuredx": {})", "measured"},
      {R"("dispersion_factor": 3,)", R"("dispersion_factor": 0.5,)", "dispersion_factor"},
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
