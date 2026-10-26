#include "../../src/runtime/gate_evaluate.h"

#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Spec 0060 R5-R8 / ADR-0119 D3-D5 (Plan 0060 M2, P6): the evaluator's every status,
// tolerance boundary and refusal, on synthetic sets with no device. Nothing here
// measures a time: the numbers are the test's own.

namespace {

namespace gate = atlantis::runtime::gate;
namespace json = atlantis::connection::json;
using Catch::Approx;
using gate_test::Agg;
using gate_test::DocumentIdentity;
using gate_test::makeSet;
using gate_test::SetProtocol;
using gate_test::testPolicy;

gate::BaselineRef ref() { return {"test-profile", "baselines/test-profile/bistro/b.baseline.json", "abc123"}; }

// A baseline: a quiet CPU, a noisier GPU, a few exact counters.
std::map<std::string, Agg> baselineAggregates() {
  return {{"cpu_active_ms", {12.0, 0.1, 15.0}},
          {"zones.RenderExtraction", {4.5, 0.05, 15.0}},
          {"gpu_busy_ms", {17.0, 0.6, 15.0}},
          {"passes.draw.gpu_ms", {14.0, 0.5, 15.0}},
          {"draw_calls", {4778.0, 0.0, 15.0}},
          {"descriptor_writes", {1093.0, 0.0, 15.0}},
          {"fragment_invocations", {5928000.0, 100.0, 15.0}},
          {"frame_ms", {28.0, 1.0, 15.0}},
          {"lights_point", {59.0, 0.0, 15.0}},
          {"renderables_total", {2909.0, 0.0, 15.0}}};
}

gate::Verdict run(std::map<std::string, Agg> candidate, const DocumentIdentity& identity = {},
                  const SetProtocol& protocol = {}) {
  return gate::evaluate(makeSet(candidate, identity, protocol), makeSet(baselineAggregates()), testPolicy(), ref());
}

std::map<std::string, Agg> same() { return baselineAggregates(); }

std::map<std::string, Agg> withCenter(const std::string& key, double center) {
  auto aggregates = baselineAggregates();
  aggregates[key].center = center;
  return aggregates;
}

const json::Value& metric(const gate::Verdict& verdict, const std::string& key) {
  const json::Value* entry = gate::at(verdict.document, {"metrics", key});
  REQUIRE(entry != nullptr);
  return *entry;
}

std::string statusOf(const gate::Verdict& verdict, const std::string& key) {
  return metric(verdict, key).find("status")->asString();
}

}  // namespace

TEST_CASE("an unchanged candidate passes with exit 0 and every gated metric ok", "[runtime][gate][evaluate]") {
  const auto verdict = run(same());
  CHECK(verdict.result == "pass");
  CHECK(verdict.exitCode == gate::kExitPass);
  CHECK(verdict.document.find("schema")->asString() == "atlantis.benchmark.gate/1");
  CHECK(verdict.document.find("verdict")->asString() == "pass");
  CHECK(gate::numberOf(verdict.document.find("exit_code")) == 0.0);
  CHECK(verdict.document.find("comparable")->asBool());
  CHECK(verdict.document.find("reasons")->asArray().empty());
  CHECK(statusOf(verdict, "cpu_active_ms") == "ok");
  CHECK(statusOf(verdict, "draw_calls") == "ok");
  CHECK(gate::at(verdict.document, {"summary", "regressed"})->asArray().empty());
}

TEST_CASE("the verdict names the baseline and reports each metric's numbers", "[runtime][gate][evaluate]") {
  const auto verdict = run(withCenter("cpu_active_ms", 13.5));
  const json::Value& cpu = metric(verdict, "cpu_active_ms");
  CHECK(cpu.find("tier")->asString() == "cpu");
  CHECK(gate::numberOf(cpu.find("baseline")) == Approx(12.0));
  CHECK(gate::numberOf(cpu.find("candidate")) == Approx(13.5));
  CHECK(gate::numberOf(cpu.find("delta")) == Approx(1.5));
  CHECK(gate::numberOf(cpu.find("delta_pct")) == Approx(12.5));
  CHECK(gate::numberOf(cpu.find("tolerance")) > 0.0);
  CHECK(cpu.find("tolerance_source") != nullptr);
  CHECK(gate::at(verdict.document, {"baseline", "profile"})->asString() == "test-profile");
  CHECK(gate::at(verdict.document, {"baseline", "commit"})->asString() == "abc123");
}

TEST_CASE("a CPU regression past its tolerance fails the gate with exit 4 and names the metric",
          "[runtime][gate][evaluate]") {
  // 12.0 -> 13.5: +12.5 %, over the 5 % allowance (0.6 ms) and the noise term (0.114 ms).
  const auto verdict = run(withCenter("cpu_active_ms", 13.5));
  CHECK(verdict.result == "fail");
  CHECK(verdict.exitCode == gate::kExitFail);
  CHECK(statusOf(verdict, "cpu_active_ms") == "regressed");
  CHECK(metric(verdict, "cpu_active_ms").find("tolerance_source")->asString() == "max_pct");
  const auto& regressed = gate::at(verdict.document, {"summary", "regressed"})->asArray();
  REQUIRE(regressed.size() == 1);
  CHECK(regressed[0].asString() == "cpu_active_ms");
}

TEST_CASE("the tolerance boundary: exactly at it is ok, just over it is regressed", "[runtime][gate][evaluate]") {
  // Round baselines, so the arithmetic of the boundary is exact in binary floating point.
  auto baseline = baselineAggregates();
  baseline["cpu_active_ms"] = {10.0, 0.1, 15.0};  // 5 % of 10.0 = 0.5 ms is the largest term (noise 0.137 ms)
  baseline["gpu_busy_ms"] = {20.0, 0.6, 15.0};    // 10 % of 20.0 = 2.0 ms (noise 0.82 ms)
  const auto at = [&](const std::string& key, double center) {
    auto candidate = baseline;
    candidate[key].center = center;
    return statusOf(gate::evaluate(makeSet(candidate), makeSet(baseline), testPolicy(), ref()), key);
  };
  CHECK(at("cpu_active_ms", 10.5) == "ok");
  CHECK(at("cpu_active_ms", 10.5001) == "regressed");
  CHECK(at("gpu_busy_ms", 22.0) == "ok");
  CHECK(at("gpu_busy_ms", 22.01) == "regressed");
}

TEST_CASE("each tier has its own allowance", "[runtime][gate][evaluate]") {
  // +8 % is inside the GPU tier's 10 % and outside the CPU tier's 5 %.
  CHECK(statusOf(run(withCenter("gpu_busy_ms", 17.0 * 1.08)), "gpu_busy_ms") == "ok");
  CHECK(statusOf(run(withCenter("cpu_active_ms", 12.0 * 1.08)), "cpu_active_ms") == "regressed");
  // The GPU tier's per-pass metrics are gated too, by pattern.
  CHECK(statusOf(run(withCenter("passes.draw.gpu_ms", 14.0 * 1.3)), "passes.draw.gpu_ms") == "regressed");
}

TEST_CASE("the noise term widens the tolerance when the baseline was noisy", "[runtime][gate][evaluate]") {
  auto noisyBaseline = baselineAggregates();
  noisyBaseline["cpu_active_ms"].sigma = 1.0;  // k*sigma*1.25*sqrt(1/15+1/5) = 3*1.0*1.25*0.516 = 1.936 ms
  auto candidate = same();
  candidate["cpu_active_ms"].n = 5.0;  // R = 5 against K = 15
  candidate["cpu_active_ms"].center = 12.0 + 1.5;  // over the 5 % allowance (0.6), under the noise term
  const auto verdict = gate::evaluate(makeSet(candidate), makeSet(noisyBaseline), testPolicy(), ref());
  CHECK(statusOf(verdict, "cpu_active_ms") == "ok");
  CHECK(metric(verdict, "cpu_active_ms").find("tolerance_source")->asString() == "noise");
  candidate["cpu_active_ms"].center = 12.0 + 2.0;
  const auto over = gate::evaluate(makeSet(candidate), makeSet(noisyBaseline), testPolicy(), ref());
  CHECK(statusOf(over, "cpu_active_ms") == "regressed");
}

TEST_CASE("a change smaller than the tier's absolute floor is ignored, and says so", "[runtime][gate][evaluate]") {
  auto baseline = baselineAggregates();
  baseline["zones.RenderExtraction"] = {0.5, 0.0, 15.0};  // 5 % of 0.5 ms is 0.025 ms; the floor is 0.10 ms
  auto candidate = baselineAggregates();
  candidate["zones.RenderExtraction"] = {0.58, 0.0, 5.0};  // +0.08 ms: past the percentage, inside the floor
  const auto verdict = gate::evaluate(makeSet(candidate), makeSet(baseline), testPolicy(), ref());
  CHECK(statusOf(verdict, "zones.RenderExtraction") == "ignored_floor");
  CHECK(verdict.result == "pass");
  const auto& ignored = gate::at(verdict.document, {"summary", "ignored"})->asArray();
  REQUIRE(ignored.size() == 1);
  CHECK(ignored[0].asString() == "zones.RenderExtraction");
}

TEST_CASE("an improvement is reported and never fails the gate", "[runtime][gate][evaluate]") {
  const auto verdict = run(withCenter("cpu_active_ms", 9.0));
  CHECK(statusOf(verdict, "cpu_active_ms") == "improved");
  CHECK(verdict.result == "pass");
  CHECK(verdict.exitCode == gate::kExitPass);
  const auto& improved = gate::at(verdict.document, {"summary", "improved"})->asArray();
  REQUIRE(improved.size() == 1);
  CHECK(improved[0].asString() == "cpu_active_ms");
}

TEST_CASE("a counter is exact: one more draw call per frame fails the gate", "[runtime][gate][evaluate]") {
  const auto verdict = run(withCenter("draw_calls", 4779.0));
  CHECK(verdict.result == "fail");
  CHECK(statusOf(verdict, "draw_calls") == "regressed");
  const json::Value& entry = metric(verdict, "draw_calls");
  CHECK(entry.find("tier")->asString() == "counter");
  CHECK(gate::numberOf(entry.find("tolerance")) == 0.0);
  CHECK(entry.find("tolerance_source")->asString() == "exact");
}

TEST_CASE("a counter that fell is an improvement, not a failure", "[runtime][gate][evaluate]") {
  const auto verdict = run(withCenter("descriptor_writes", 1000.0));
  CHECK(statusOf(verdict, "descriptor_writes") == "improved");
  CHECK(verdict.result == "pass");
}

TEST_CASE("a counter the policy gives a relative tolerance may vary within it", "[runtime][gate][evaluate]") {
  // 0.1 % of 5,928,000 is 5,928.
  CHECK(statusOf(run(withCenter("fragment_invocations", 5928000.0 + 5000.0)), "fragment_invocations") == "ok");
  const auto over = run(withCenter("fragment_invocations", 5928000.0 + 7000.0));
  CHECK(statusOf(over, "fragment_invocations") == "regressed");
  CHECK(metric(over, "fragment_invocations").find("tolerance_source")->asString() == "relative");
}

TEST_CASE("a noisy candidate session is inconclusive, not a pass or a fail", "[runtime][gate][evaluate]") {
  auto candidate = same();
  candidate["gpu_busy_ms"].sigma = 5.0;  // baseline sigma 0.6; 3 x 0.6 = 1.8 < 5.0
  const auto verdict = run(candidate);
  CHECK(verdict.result == "inconclusive");
  CHECK(verdict.exitCode == gate::kExitNotJudged);
  const auto& exceeded = gate::at(verdict.document, {"dispersion", "exceeded"})->asArray();
  REQUIRE(exceeded.size() == 1);
  CHECK(exceeded[0].asString() == "gpu_busy_ms");
  CHECK(verdict.document.find("comparable")->asBool());
  CHECK(gate::at(verdict.document, {"dispersion", "reported"})->asArray().empty());
  const auto& deciding = gate::at(verdict.document, {"dispersion", "metrics"})->asArray();
  REQUIRE(deciding.size() == 2);  // the verdict says which metrics can decide
  CHECK(deciding[0].asString() == "cpu_active_ms");
  CHECK(deciding[1].asString() == "gpu_busy_ms");
}

TEST_CASE("noise withholds a verdict on a CPU or GPU regression it cannot confirm", "[runtime][gate][evaluate]") {
  auto candidate = withCenter("cpu_active_ms", 14.0);  // would be a regression...
  candidate["cpu_active_ms"].sigma = 2.0;              // ...in a session whose spread is 20 x the baseline's
  const auto verdict = run(candidate);
  CHECK(statusOf(verdict, "cpu_active_ms") == "regressed");
  CHECK(verdict.result == "inconclusive");
}

TEST_CASE("a counter regression fails the gate whatever the noise (protective ordering)",
          "[runtime][gate][evaluate]") {
  auto candidate = withCenter("draw_calls", 4779.0);
  candidate["gpu_busy_ms"].sigma = 5.0;
  const auto verdict = run(candidate);
  CHECK(verdict.result == "fail");
  CHECK(verdict.exitCode == gate::kExitFail);
}

TEST_CASE("a quiet baseline does not make every candidate spread inconclusive", "[runtime][gate][evaluate]") {
  auto baseline = baselineAggregates();
  baseline["cpu_active_ms"].sigma = 0.0;
  auto candidate = same();
  candidate["cpu_active_ms"].sigma = 0.02;  // under floor/k x factor = 0.1/3*3 = 0.1
  CHECK(gate::evaluate(makeSet(candidate), makeSet(baseline), testPolicy(), ref()).result == "pass");
  candidate["cpu_active_ms"].sigma = 0.5;
  CHECK(gate::evaluate(makeSet(candidate), makeSet(baseline), testPolicy(), ref()).result == "inconclusive");
}

TEST_CASE("runs on unlike hardware, builds or scenes are not judged", "[runtime][gate][evaluate]") {
  DocumentIdentity other;
  const struct {
    const char* field;
    void (*change)(DocumentIdentity&);
  } cases[] = {
      {"scene.guid", [](DocumentIdentity& d) { d.guid = "guid-2"; }},
      {"scene.content_pin", [](DocumentIdentity& d) { d.contentPin = "pin-2"; }},
      {"settings.resolution.width", [](DocumentIdentity& d) { d.width = 1280; }},
      {"settings.resolution.height", [](DocumentIdentity& d) { d.height = 720; }},
      {"settings.variant", [](DocumentIdentity& d) { d.variant = "no-fog"; }},
      {"meta.build.type", [](DocumentIdentity& d) { d.buildType = "Debug"; }},
      {"meta.build.validation", [](DocumentIdentity& d) { d.validation = true; }},
      {"meta.system.os", [](DocumentIdentity& d) { d.os = "android"; }},
      {"meta.system.cpu", [](DocumentIdentity& d) { d.cpu = "Other CPU"; }},
      {"meta.system.gpu", [](DocumentIdentity& d) { d.gpu = "Other GPU"; }},
      {"meta.system.driver", [](DocumentIdentity& d) { d.driver = "2.0"; }},
      {"meta.system.vulkan_api", [](DocumentIdentity& d) { d.vulkanApi = "1.3.0"; }},
      {"meta.display.present_mode", [](DocumentIdentity& d) { d.presentMode = "MAILBOX"; }},
      {"meta.display.refresh_hz", [](DocumentIdentity& d) { d.refreshHz = 144; }},
  };
  for (const auto& c : cases) {
    INFO(c.field);
    DocumentIdentity changed = other;
    c.change(changed);
    const auto verdict = run(same(), changed);
    CHECK(verdict.result == "incomparable");
    CHECK(verdict.exitCode == gate::kExitNotJudged);
    CHECK_FALSE(verdict.document.find("comparable")->asBool());
    const auto& reasons = verdict.document.find("reasons")->asArray();
    REQUIRE_FALSE(reasons.empty());
    CHECK(reasons[0].asString().find(c.field) != std::string::npos);
    CHECK(verdict.document.find("metrics")->asObject().empty());  // nothing is compared
  }
}

TEST_CASE("a changed workload is incomparable, not a regression", "[runtime][gate][evaluate]") {
  const auto verdict = run(withCenter("lights_point", 24.0));
  CHECK(verdict.result == "incomparable");
  const auto& reasons = verdict.document.find("reasons")->asArray();
  REQUIRE(reasons.size() == 1);
  CHECK(reasons[0].asString().find("workload changed: lights_point") != std::string::npos);
  CHECK(run(withCenter("renderables_total", 2900.0)).result == "incomparable");
}

TEST_CASE("a metric missing on either side is unavailable and does not decide the verdict",
          "[runtime][gate][evaluate]") {
  auto candidate = same();
  candidate.erase("gpu_busy_ms");
  const auto verdict = run(candidate);
  CHECK(verdict.result == "pass");
  CHECK(statusOf(verdict, "gpu_busy_ms") == "unavailable");
  const auto& unavailable = gate::at(verdict.document, {"summary", "unavailable"})->asArray();
  REQUIRE(unavailable.size() == 1);
  CHECK(unavailable[0].asString() == "gpu_busy_ms");
}

TEST_CASE("reported metrics are listed with their change and never gated", "[runtime][gate][evaluate]") {
  const auto verdict = run(withCenter("frame_ms", 56.0));  // frame time doubled
  CHECK(verdict.result == "pass");
  CHECK(gate::at(verdict.document, {"metrics", "frame_ms"}) == nullptr);
  const json::Value* reported = gate::at(verdict.document, {"reported", "frame_ms"});
  REQUIRE(reported != nullptr);
  CHECK(gate::numberOf(reported->find("delta")) == Approx(28.0));
}

TEST_CASE("the protocol block says whether the run followed the policy", "[runtime][gate][evaluate]") {
  const auto conformant = run(same());
  CHECK(gate::at(conformant.document, {"protocol", "conformant"})->asBool());
  SetProtocol smoke;
  smoke.idleWait = false;
  smoke.preheatSeconds = 0.0;
  smoke.runs = 2;
  const auto verdict = run(same(), {}, smoke);
  CHECK_FALSE(gate::at(verdict.document, {"protocol", "conformant"})->asBool());
  CHECK(gate::numberOf(gate::at(verdict.document, {"protocol", "runs"})) == 2.0);
  CHECK_FALSE(gate::at(verdict.document, {"protocol", "idle_wait"})->asBool());
}

TEST_CASE("a verdict is byte-identical for identical inputs and carries no timestamp", "[runtime][gate][evaluate]") {
  const auto a = run(withCenter("cpu_active_ms", 13.5));
  const auto b = run(withCenter("cpu_active_ms", 13.5));
  CHECK(json::write(a.document) == json::write(b.document));
  const std::string text = json::write(a.document);
  CHECK(text.find("timestamp") == std::string::npos);
  CHECK(text.find("recorded_at") == std::string::npos);
  CHECK(text.find("generated") == std::string::npos);
  const auto parsed = json::parse(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == a.document);
}

TEST_CASE("notJudged is an incomparable verdict that says why, with exit 5", "[runtime][gate][evaluate]") {
  const auto candidate = makeSet(same());
  const auto verdict = gate::notJudged(candidate, testPolicy(), {"", "", ""},
                                       {"no baseline for this hardware profile; record one with --gate --record"});
  CHECK(verdict.result == "incomparable");
  CHECK(verdict.exitCode == gate::kExitNotJudged);
  CHECK(gate::numberOf(verdict.document.find("exit_code")) == 5.0);
  CHECK(verdict.document.find("reasons")->asArray().size() == 1);
  CHECK_FALSE(verdict.document.find("comparable")->asBool());
}

TEST_CASE("the exit codes are 0 for pass, 4 for fail and 5 for not judged", "[runtime][gate][evaluate]") {
  CHECK(gate::exitCodeFor("pass") == 0);
  CHECK(gate::exitCodeFor("fail") == 4);
  CHECK(gate::exitCodeFor("incomparable") == 5);
  CHECK(gate::exitCodeFor("inconclusive") == 5);
  CHECK(gate::kExitPass == 0);
  CHECK(gate::kExitFail == 4);
  CHECK(gate::kExitNotJudged == 5);
}

TEST_CASE("a mains run is never judged against a battery baseline, or anything else", "[runtime][gate][evaluate]") {
  SetProtocol battery;
  battery.power = "battery";
  for (const char* other : {"battery", "mixed", "unknown"}) {
    SetProtocol p;
    p.power = other;
    const auto against = gate::evaluate(makeSet(baselineAggregates(), {}, p), makeSet(baselineAggregates()), testPolicy(), ref());
    INFO(other);
    CHECK(against.result == "incomparable");
    const auto& reasons = against.document.find("reasons")->asArray();
    REQUIRE(reasons.size() == 1);
    CHECK(reasons[0].asString().find("protocol.power differs") != std::string::npos);
    CHECK_FALSE(gate::at(against.document, {"protocol", "conformant"})->asBool());
  }
  // Two battery sets agree with each other, but neither is evidence: the run is not conformant.
  const auto both = gate::evaluate(makeSet(baselineAggregates(), {}, battery), makeSet(baselineAggregates(), {}, battery),
                                   testPolicy(), ref());
  CHECK(gate::at(both.document, {"protocol", "power"})->asString() == "battery");
  CHECK_FALSE(gate::at(both.document, {"protocol", "conformant"})->asBool());
  // On mains it is conformant.
  const auto mains = gate::evaluate(makeSet(baselineAggregates()), makeSet(baselineAggregates()), testPolicy(), ref());
  CHECK(gate::at(mains.document, {"protocol", "conformant"})->asBool());
  // A policy that does not ask for mains does not make a battery run non-conformant.
  gate::GatePolicy relaxed = testPolicy();
  relaxed.protocol.requireMainsPower = false;
  const auto lenient = gate::evaluate(makeSet(baselineAggregates(), {}, battery), makeSet(baselineAggregates(), {}, battery),
                                      relaxed, ref());
  CHECK(gate::at(lenient.document, {"protocol", "conformant"})->asBool());
}

TEST_CASE("a pinned run is never judged against an unpinned baseline, or the other way round", "[runtime][gate][evaluate]") {
  SetProtocol unpinned;
  unpinned.pin = false;
  const auto pinnedAgainstUnpinned = gate::evaluate(makeSet(baselineAggregates()), makeSet(baselineAggregates(), {}, unpinned),
                                                    testPolicy(), ref());
  CHECK(pinnedAgainstUnpinned.result == "incomparable");
  const auto& reasons = pinnedAgainstUnpinned.document.find("reasons")->asArray();
  REQUIRE(reasons.size() == 1);
  CHECK(reasons[0].asString().find("protocol.pin differs") != std::string::npos);
  const auto unpinnedAgainstPinned = gate::evaluate(makeSet(baselineAggregates(), {}, unpinned), makeSet(baselineAggregates()),
                                                    testPolicy(), ref());
  CHECK(unpinnedAgainstPinned.result == "incomparable");
  // Two unpinned documents are comparable with each other; whether such a run follows the policy is a different question.
  const auto both = gate::evaluate(makeSet(baselineAggregates(), {}, unpinned), makeSet(baselineAggregates(), {}, unpinned),
                                   testPolicy(), ref());
  CHECK(both.result == "pass");
  CHECK_FALSE(gate::at(both.document, {"protocol", "conformant"})->asBool());  // the policy asks for pinned children
}

TEST_CASE("the verdict records the pinning state and conformance needs it", "[runtime][gate][evaluate]") {
  const auto pinned = run(same());
  CHECK(*gate::at(pinned.document, {"protocol", "pin"}) == gate::pinValue(true, "logical_processor_2"));
  CHECK(gate::at(pinned.document, {"protocol", "conformant"})->asBool());
}

// Spec 0060 eighth Correction: a pinned run names its one logical processor.

TEST_CASE("a run pinned under the previous core rule is not conformant and is not judged against a single-processor baseline",
          "[runtime][gate][evaluate]") {
  SetProtocol previous;
  previous.cores = "highest_efficiency_class";  // what a run recorded before the eighth Correction names
  // The candidate (first) under the previous rule against a single-processor baseline (second).
  const auto verdict = gate::evaluate(makeSet(baselineAggregates(), {}, previous), makeSet(baselineAggregates()),
                                      testPolicy(), ref());
  CHECK(verdict.result == "incomparable");
  const std::string reason = verdict.document.find("reasons")->asArray()[0].asString();
  CHECK(reason.find("protocol.pin differs") != std::string::npos);
  // The reason shows both pins, so an operator sees which rule each was recorded under.
  CHECK(reason.find("logical_processor_2") != std::string::npos);
  CHECK(reason.find("highest_efficiency_class") != std::string::npos);
  CHECK_FALSE(gate::at(verdict.document, {"protocol", "conformant"})->asBool());
  // Two such documents compare, but the run still does not follow the policy.
  const auto both = gate::evaluate(makeSet(baselineAggregates(), {}, previous), makeSet(baselineAggregates(), {}, previous),
                                   testPolicy(), ref());
  CHECK(both.result == "pass");
  CHECK_FALSE(gate::at(both.document, {"protocol", "conformant"})->asBool());
}

TEST_CASE("a run pinned to another processor than its baseline's is not judged against it", "[runtime][gate][evaluate]") {
  SetProtocol other;
  other.cores = "logical_processor_4";
  // The candidate (first) on processor 4 against a baseline (second) recorded on processor 2.
  const auto verdict = gate::evaluate(makeSet(baselineAggregates(), {}, other), makeSet(baselineAggregates()),
                                      testPolicy(), ref());
  CHECK(verdict.result == "incomparable");
  CHECK(gate::at(verdict.document, {"protocol", "conformant"})->asBool());  // the run itself follows the policy
}

// Spec 0060 Correction 2026-10-14: only the headline totals' dispersion can make a verdict inconclusive.

TEST_CASE("a headline metric's dispersion trip makes the verdict inconclusive, on either tier", "[runtime][gate][evaluate][dispersion]") {
  auto cpu = same();
  cpu["cpu_active_ms"].sigma = 2.0;  // baseline 0.1, factor 3: the limit is 0.3
  auto verdict = run(cpu);
  CHECK(verdict.result == "inconclusive");
  CHECK(gate::at(verdict.document, {"dispersion", "exceeded"})->asArray().size() == 1);
  auto gpu = same();
  gpu["gpu_busy_ms"].sigma = 5.0;
  verdict = run(gpu);
  CHECK(verdict.result == "inconclusive");
  CHECK(gate::at(verdict.document, {"dispersion", "exceeded"})->asArray()[0].asString() == "gpu_busy_ms");
}

TEST_CASE("a zone or pass metric's dispersion trip is reported and never makes the verdict inconclusive",
          "[runtime][gate][evaluate][dispersion]") {
  auto candidate = same();
  candidate["zones.RenderExtraction"].sigma = 5.0;  // baseline 0.05: 100 x
  candidate["passes.draw.gpu_ms"].sigma = 8.0;      // baseline 0.5: 16 x
  const auto verdict = run(candidate);
  CHECK(verdict.result == "pass");
  CHECK(verdict.exitCode == gate::kExitPass);
  CHECK(gate::at(verdict.document, {"dispersion", "exceeded"})->asArray().empty());
  const auto& reported = gate::at(verdict.document, {"dispersion", "reported"})->asArray();
  REQUIRE(reported.size() == 2);
  CHECK(reported[0].asString() == "passes.draw.gpu_ms");  // in the baseline's key order
  CHECK(reported[1].asString() == "zones.RenderExtraction");
  CHECK(statusOf(verdict, "zones.RenderExtraction") == "ok");  // still judged, by the tolerance rule
}

TEST_CASE("a noisy zone is judged by the tolerance rule alone: a real regression fails, it is not withheld",
          "[runtime][gate][evaluate][dispersion]") {
  auto candidate = same();
  candidate["zones.RenderExtraction"].center = 5.5;  // 4.5 + 1.0, far past 5 % and the noise term
  candidate["zones.RenderExtraction"].sigma = 3.0;
  const auto verdict = run(candidate);
  CHECK(statusOf(verdict, "zones.RenderExtraction") == "regressed");
  CHECK(verdict.result == "fail");
  CHECK(verdict.exitCode == gate::kExitFail);
  CHECK(gate::at(verdict.document, {"dispersion", "reported"})->asArray().size() == 1);
}

TEST_CASE("a headline trip still withholds a verdict while a zone is also noisy", "[runtime][gate][evaluate][dispersion]") {
  auto candidate = withCenter("cpu_active_ms", 14.0);  // a regression in a session whose total is unstable
  candidate["cpu_active_ms"].sigma = 2.0;
  candidate["zones.RenderExtraction"].sigma = 3.0;
  const auto verdict = run(candidate);
  CHECK(verdict.result == "inconclusive");
  CHECK(gate::at(verdict.document, {"dispersion", "exceeded"})->asArray().size() == 1);
  CHECK(gate::at(verdict.document, {"dispersion", "reported"})->asArray().size() == 1);
}

TEST_CASE("the dispersion floor applies to a headline metric whose baseline is quiet", "[runtime][gate][evaluate][dispersion]") {
  auto baseline = baselineAggregates();
  baseline["gpu_busy_ms"].sigma = 0.0;
  auto candidate = same();
  candidate["gpu_busy_ms"].sigma = 0.2;  // the tier floor 0.25 / k 3 x factor 3 = 0.25
  CHECK(gate::evaluate(makeSet(candidate), makeSet(baseline), testPolicy(), ref()).result == "pass");
  candidate["gpu_busy_ms"].sigma = 0.4;
  CHECK(gate::evaluate(makeSet(candidate), makeSet(baseline), testPolicy(), ref()).result == "inconclusive");
}

TEST_CASE("each gated metric carries the baseline's and the candidate's sigma, and they survive the text",
          "[runtime][gate][evaluate][dispersion]") {
  auto candidate = same();
  candidate["gpu_busy_ms"].sigma = 0.9;
  candidate["zones.RenderExtraction"].sigma = 0.07;
  const auto verdict = run(candidate);
  CHECK(*gate::numberOf(metric(verdict, "gpu_busy_ms").find("baseline_sigma")) == 0.6);
  CHECK(*gate::numberOf(metric(verdict, "gpu_busy_ms").find("candidate_sigma")) == 0.9);
  CHECK(*gate::numberOf(metric(verdict, "zones.RenderExtraction").find("candidate_sigma")) == 0.07);
  CHECK(*gate::numberOf(metric(verdict, "draw_calls").find("baseline_sigma")) == 0.0);  // counters too: exact, no spread
  const auto parsed = json::parse(json::write(verdict.document));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == verdict.document);
  CHECK(*gate::numberOf(gate::at(parsed.value(), {"metrics", "gpu_busy_ms", "candidate_sigma"})) == 0.9);
  // The same inputs give the same bytes (the document stays deterministic).
  CHECK(json::write(run(candidate).document) == json::write(verdict.document));
}

TEST_CASE("an unavailable metric has no sigma to report", "[runtime][gate][evaluate][dispersion]") {
  auto candidate = same();
  candidate.erase("gpu_busy_ms");
  const auto verdict = run(candidate);
  CHECK(statusOf(verdict, "gpu_busy_ms") == "unavailable");
  CHECK(metric(verdict, "gpu_busy_ms").find("candidate_sigma") == nullptr);
}

// Spec 0060 Correction 2026-10-15 (GPU-state canary): the four cases, canary stable or shifted x GPU tier stable or
// regressed, and what the canary does not explain.

namespace {

gate::Verdict runWithCanary(std::map<std::string, Agg> candidate, const gate_test::CanarySpec& canary,
                            const gate::GatePolicy& policy = testPolicy()) {
  return gate::evaluate(makeSet(candidate, {}, {}, canary), makeSet(baselineAggregates()), policy, ref());
}

// The GPU tier regressed: the totals and the main pass +25 % (17 -> 21.25 ms, 14 -> 17.5 ms).
std::map<std::string, Agg> gpuRegressed() {
  auto candidate = baselineAggregates();
  candidate["gpu_busy_ms"].center = 21.25;
  candidate["passes.draw.gpu_ms"].center = 17.5;
  return candidate;
}

const gate_test::CanarySpec kShifted = gate_test::CanarySpec::scaled(1.0, 2.0, 2.0);  // memory-bound components x2

}  // namespace

TEST_CASE("canary stable and GPU stable: a pass", "[runtime][gate][evaluate][canary]") {
  const auto verdict = runWithCanary(same(), gate_test::CanarySpec{});
  CHECK(verdict.result == "pass");
  CHECK(gate::at(verdict.document, {"machine_state", "classification"})->asString() == "stable");
  CHECK_FALSE(gate::at(verdict.document, {"canary", "shifted"})->asBool());
  CHECK(gate::at(verdict.document, {"machine_state", "explains"})->asArray().empty());
}

TEST_CASE("canary stable and GPU regressed: the code did it, the gate fails", "[runtime][gate][evaluate][canary]") {
  const auto verdict = runWithCanary(gpuRegressed(), gate_test::CanarySpec{});
  CHECK(verdict.result == "fail");
  CHECK(verdict.exitCode == gate::kExitFail);
  CHECK(statusOf(verdict, "gpu_busy_ms") == "regressed");
  CHECK(gate::at(verdict.document, {"machine_state", "classification"})->asString() == "stable");
  CHECK(gate::at(verdict.document, {"machine_state", "explains"})->asArray().empty());
  CHECK(gate::at(verdict.document, {"reasons"})->asArray().empty());
}

TEST_CASE("canary shifted and GPU stable: a pass, with the shift noted", "[runtime][gate][evaluate][canary]") {
  const auto verdict = runWithCanary(same(), kShifted);
  CHECK(verdict.result == "pass");
  CHECK(gate::at(verdict.document, {"machine_state", "classification"})->asString() == "shifted");
  CHECK(gate::at(verdict.document, {"canary", "shifted"})->asBool());
  CHECK(gate::at(verdict.document, {"machine_state", "explains"})->asArray().empty());
}

TEST_CASE("canary shifted and GPU regressed: the machine state, withheld as inconclusive with the reason",
          "[runtime][gate][evaluate][canary]") {
  const auto verdict = runWithCanary(gpuRegressed(), kShifted);
  CHECK(verdict.result == "inconclusive");
  CHECK(verdict.exitCode == gate::kExitNotJudged);
  CHECK(verdict.document.find("comparable")->asBool());
  CHECK(statusOf(verdict, "gpu_busy_ms") == "regressed");  // the numbers are not hidden
  CHECK(gate::at(verdict.document, {"machine_state", "classification"})->asString() == "shifted");
  const auto& explains = gate::at(verdict.document, {"machine_state", "explains"})->asArray();
  REQUIRE(explains.size() == 2);
  CHECK(explains[0].asString() == "gpu_busy_ms");
  CHECK(explains[1].asString() == "passes.draw.gpu_ms");
  const auto& reasons = verdict.document.find("reasons")->asArray();
  REQUIRE(reasons.size() == 1);
  CHECK(reasons[0].asString().find("machine state") != std::string::npos);
  CHECK(reasons[0].asString().find("bw +100 %") != std::string::npos);
  CHECK(reasons[0].asString().find("tex +100 %") != std::string::npos);
  CHECK(reasons[0].asString().find("alu") == std::string::npos);  // the stable component is not blamed
}

TEST_CASE("a shifted canary explains the GPU tier and nothing else", "[runtime][gate][evaluate][canary]") {
  auto cpu = same();
  cpu["cpu_active_ms"].center = 14.0;  // a real CPU regression
  CHECK(runWithCanary(cpu, kShifted).result == "fail");
  auto both = gpuRegressed();
  both["cpu_active_ms"].center = 14.0;
  CHECK(runWithCanary(both, kShifted).result == "fail");  // the CPU regression stands whatever the GPU did
  auto counter = gpuRegressed();
  counter["draw_calls"].center = 4779.0;
  CHECK(runWithCanary(counter, kShifted).result == "fail");  // a counter is exact
}

TEST_CASE("a faster canary cannot explain a GPU regression away", "[runtime][gate][evaluate][canary]") {
  const auto verdict = runWithCanary(gpuRegressed(), gate_test::CanarySpec::scaled(0.7, 0.7, 0.7));
  CHECK(verdict.result == "fail");
  CHECK(gate::at(verdict.document, {"machine_state", "classification"})->asString() == "stable");
}

TEST_CASE("one shifted canary component is enough, and each is judged against the same limit",
          "[runtime][gate][evaluate][canary]") {
  const auto verdict = runWithCanary(gpuRegressed(), gate_test::CanarySpec::scaled(1.0, 1.0, 1.09));
  CHECK(verdict.result == "inconclusive");
  CHECK(gate::at(verdict.document, {"canary", "shifted_components"})->asArray().size() == 1);
  CHECK(runWithCanary(gpuRegressed(), gate_test::CanarySpec::scaled(1.05, 1.05, 1.05)).result == "fail");  // inside 8 %
}

TEST_CASE("a global GPU slowdown with the canary stable fails at every size (M7 injection model)",
          "[runtime][gate][evaluate][canary]") {
  for (const double slowdown : {0.20, 0.30, 0.50}) {
    auto candidate = baselineAggregates();
    for (auto& [key, agg] : candidate) {
      if (key == "gpu_busy_ms" || key == "passes.draw.gpu_ms") agg.center *= 1.0 + slowdown;
    }
    INFO(slowdown);
    CHECK(runWithCanary(candidate, gate_test::CanarySpec{}).result == "fail");
    CHECK(runWithCanary(candidate, kShifted).result == "inconclusive");  // the same numbers on a shifted machine
  }
}

TEST_CASE("without a canary on either side the runs are not comparable, unless the policy does not ask for one",
          "[runtime][gate][evaluate][canary]") {
  const auto noCandidate = runWithCanary(same(), gate_test::CanarySpec::omit());
  CHECK(noCandidate.result == "incomparable");
  CHECK(noCandidate.exitCode == gate::kExitNotJudged);
  CHECK(noCandidate.document.find("reasons")->asArray()[0].asString().find("this run has no GPU canary") != std::string::npos);
  const auto noBaseline = gate::evaluate(makeSet(same()), makeSet(baselineAggregates(), {}, {}, gate_test::CanarySpec::omit()),
                                         testPolicy(), ref());
  CHECK(noBaseline.result == "incomparable");
  CHECK(noBaseline.document.find("reasons")->asArray()[0].asString().find("baseline has no GPU canary reference") != std::string::npos);
  CHECK(gate::at(noBaseline.document, {"machine_state", "classification"})->asString() == "unjudged");

  gate::GatePolicy lenient = testPolicy();
  lenient.canary.required = false;
  const auto passes = runWithCanary(same(), gate_test::CanarySpec::omit(), lenient);
  CHECK(passes.result == "pass");
  CHECK(gate::at(passes.document, {"machine_state", "classification"})->asString() == "unmeasured");
  CHECK(runWithCanary(gpuRegressed(), gate_test::CanarySpec::omit(), lenient).result == "fail");  // nothing to explain it
}

TEST_CASE("the verdict with a canary is still byte-deterministic and round-trips", "[runtime][gate][evaluate][canary]") {
  const auto first = runWithCanary(gpuRegressed(), kShifted);
  CHECK(json::write(first.document) == json::write(runWithCanary(gpuRegressed(), kShifted).document));
  const auto parsed = json::parse(json::write(first.document));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == first.document);
}

// Spec 0060 sixth Correction 2026-10-17: the GPU tier is report-only in the committed policy.
TEST_CASE("with the GPU tier report-only, a GPU slowdown is reported and never decides", "[runtime][gate][evaluate][gpureport]") {
  gate::GatePolicy policy = testPolicy();
  policy.gatedGpu.clear();
  policy.reported.push_back("gpu_busy_ms");
  policy.reported.push_back("passes.*.gpu_ms");
  policy.dispersionMetrics = {"cpu_active_ms"};
  policy.canary.required = false;

  auto slow = baselineAggregates();
  for (auto& [key, agg] : slow) {
    if (key == "gpu_busy_ms" || key == "passes.draw.gpu_ms") agg.center *= 1.5;
  }
  const auto verdict = gate::evaluate(makeSet(slow), makeSet(baselineAggregates()), policy, ref());
  CHECK(verdict.result == "pass");
  CHECK(gate::at(verdict.document, {"metrics", "gpu_busy_ms"}) == nullptr);
  const json::Value* reported = gate::at(verdict.document, {"reported", "gpu_busy_ms"});
  REQUIRE(reported != nullptr);  // shown, with its change
  CHECK(gate::numberOf(reported->find("delta")) > 0.0);

  // The CPU and counter tiers still decide.
  auto cpuSlow = slow;
  cpuSlow["cpu_active_ms"].center *= 1.2;
  CHECK(gate::evaluate(makeSet(cpuSlow), makeSet(baselineAggregates()), policy, ref()).result == "fail");
  auto counter = slow;
  counter["draw_calls"].center += 1.0;
  CHECK(gate::evaluate(makeSet(counter), makeSet(baselineAggregates()), policy, ref()).result == "fail");
}
