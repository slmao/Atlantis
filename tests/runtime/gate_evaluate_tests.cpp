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
  CHECK(*gate::at(pinned.document, {"protocol", "pin"}) == gate::pinValue(true));
  CHECK(gate::at(pinned.document, {"protocol", "conformant"})->asBool());
}
