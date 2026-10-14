#include "../../src/runtime/gate_canary.h"

#include "../../src/runtime/canary/canary_spirv.h"

#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

// Spec 0060 Correction 2026-10-15 (GPU-state canary): the pure side -- the child's document, the reading taken from
// it, the `canary` member of a set, the judgment against a baseline's reference and the recording's spread. No
// device; the workload itself is covered by the process-level test.

namespace {

namespace gate = atlantis::runtime::gate;
namespace json = atlantis::connection::json;
using Catch::Approx;
using gate_test::CanaryMs;
using gate_test::kStableCanary;

double number(const json::Value* v) { return *gate::numberOf(v); }

}  // namespace

TEST_CASE("the canary document carries the workload id, the fixed workload and a summary per component",
          "[runtime][gate][canary]") {
  const json::Value doc = gate_test::makeCanaryDocument({3.74, 3.8, 4.09});
  CHECK(doc.find("schema")->asString() == "atlantis.benchmark.canary/1");
  CHECK(gate::at(doc, {"workload", "id"})->asString() == "gate-canary/1");
  CHECK(number(gate::at(doc, {"workload", "extent", "width"})) == 2560.0);
  CHECK(number(gate::at(doc, {"workload", "extent", "height"})) == 1440.0);
  CHECK(number(gate::at(doc, {"workload", "repetitions"})) == 25.0);
  CHECK(number(gate::at(doc, {"components", "alu", "p50"})) == Approx(3.74));
  CHECK(number(gate::at(doc, {"components", "bw", "p50"})) == Approx(3.8));
  CHECK(number(gate::at(doc, {"components", "tex", "p50"})) == Approx(4.09));
  CHECK(gate::canarySystem(doc) != nullptr);
  // The same input gives the same bytes, and the text round-trips.
  CHECK(json::write(doc) == json::write(gate_test::makeCanaryDocument({3.74, 3.8, 4.09})));
  const auto parsed = json::parse(json::write(doc));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == doc);
}

TEST_CASE("a reading is the median of each component, and a foreign or incomplete document is refused",
          "[runtime][gate][canary]") {
  std::array<std::vector<double>, 3> repetitions = {std::vector<double>{3.7, 3.9, 3.8}, std::vector<double>{1.0, 1.2, 1.1},
                                                    std::vector<double>{4.0, 4.4, 4.2}};
  json::Value meta = json::Value::object();
  const json::Value doc = gate::buildCanaryDocument(meta, repetitions);
  const auto reading = gate::extractCanaryReading(doc);
  REQUIRE(reading.isOk());
  CHECK(reading.value().ms[0] == Approx(3.8));
  CHECK(reading.value().ms[1] == Approx(1.1));
  CHECK(reading.value().ms[2] == Approx(4.2));

  CHECK(gate::extractCanaryReading(json::Value::object()).isErr());
  json::Value wrongSchema = doc;
  wrongSchema.set("schema", json::Value::string("atlantis.benchmark/1"));
  CHECK(gate::extractCanaryReading(wrongSchema).isErr());
  json::Value wrongWorkload = doc;
  json::Value workload = *doc.find("workload");
  workload.set("id", json::Value::string("gate-canary/2"));
  wrongWorkload.set("workload", std::move(workload));
  const auto refused = gate::extractCanaryReading(wrongWorkload);
  REQUIRE(refused.isErr());
  CHECK(refused.error().find("gate-canary/1") != std::string::npos);
  std::array<std::vector<double>, 3> missing = {repetitions[0], {}, repetitions[2]};
  const auto incomplete = gate::extractCanaryReading(gate::buildCanaryDocument(meta, missing));
  REQUIRE(incomplete.isErr());
  CHECK(incomplete.error().find("bw") != std::string::npos);
}

TEST_CASE("the set's canary member lists the measurements and aggregates them per component",
          "[runtime][gate][canary]") {
  const json::Value member = gate_test::canaryOf({{3.7, 3.8, 4.0}, {3.9, 3.9, 4.2}, {3.8, 3.7, 4.1}});
  CHECK(member.find("workload")->asString() == "gate-canary/1");
  const auto& measurements = member.find("measurements")->asArray();
  REQUIRE(measurements.size() == 3);
  CHECK(measurements[0].find("phase")->asString() == "start");
  CHECK(measurements[1].find("phase")->asString() == "end");
  CHECK(number(measurements[1].find("alu")) == Approx(3.9));
  CHECK(number(gate::at(member, {"aggregate", "alu", "center"})) == Approx(3.8));
  CHECK(number(gate::at(member, {"aggregate", "alu", "min"})) == Approx(3.7));
  CHECK(number(gate::at(member, {"aggregate", "alu", "max"})) == Approx(3.9));
  CHECK(number(gate::at(member, {"aggregate", "alu", "n"})) == 3.0);
  CHECK(number(gate::at(member, {"aggregate", "tex", "center"})) == Approx(4.1));
}

namespace {

json::Value setWith(const std::vector<CanaryMs>& readings) {
  return gate_test::makeSet({}, {}, {}, gate_test::CanarySpec::of(readings));
}

}  // namespace

TEST_CASE("a candidate at the reference is stable; one above the threshold is shifted, naming the component",
          "[runtime][gate][canary]") {
  const json::Value baseline = setWith({kStableCanary, kStableCanary, kStableCanary});
  const auto stable = gate::judgeCanary(setWith({kStableCanary, kStableCanary}), baseline, 8.0);
  REQUIRE(stable.measured);
  CHECK_FALSE(stable.shifted);
  CHECK(stable.shiftPct[0] == Approx(0.0).margin(1e-9));

  // bw +9 % at the end of the session (the slowest reading counts, not the median).
  const CanaryMs slow = {kStableCanary[0], kStableCanary[1] * 1.09, kStableCanary[2]};
  const auto shifted = gate::judgeCanary(setWith({kStableCanary, slow}), baseline, 8.0);
  REQUIRE(shifted.measured);
  CHECK(shifted.shifted);
  REQUIRE(shifted.shiftedComponents.size() == 1);
  CHECK(shifted.shiftedComponents[0] == "bw");
  CHECK(shifted.shiftPct[1] == Approx(9.0));
  CHECK(shifted.worst[1] == Approx(kStableCanary[1] * 1.09));
  CHECK(shifted.reference[1] == Approx(kStableCanary[1]));
}

TEST_CASE("the canary threshold is exclusive at the limit and counts only a slower machine", "[runtime][gate][canary]") {
  const json::Value baseline = setWith({kStableCanary, kStableCanary});
  const CanaryMs atLimit = {kStableCanary[0] * 1.0799, kStableCanary[1], kStableCanary[2]};
  CHECK_FALSE(gate::judgeCanary(setWith({atLimit, atLimit}), baseline, 8.0).shifted);
  const CanaryMs over = {kStableCanary[0] * 1.081, kStableCanary[1], kStableCanary[2]};
  CHECK(gate::judgeCanary(setWith({over, over}), baseline, 8.0).shifted);
  // A faster machine cannot explain a GPU regression away.
  const CanaryMs faster = {kStableCanary[0] * 0.5, kStableCanary[1] * 0.5, kStableCanary[2] * 0.5};
  const auto judged = gate::judgeCanary(setWith({faster, faster}), baseline, 8.0);
  CHECK(judged.measured);
  CHECK_FALSE(judged.shifted);
  CHECK(judged.shiftPct[0] < 0.0);
}

TEST_CASE("a canary that cannot be compared says why", "[runtime][gate][canary]") {
  const json::Value with = setWith({kStableCanary, kStableCanary});
  const json::Value without = gate_test::makeSet({}, {}, {}, gate_test::CanarySpec::omit());
  const auto noBaseline = gate::judgeCanary(with, without, 8.0);
  CHECK_FALSE(noBaseline.measured);
  CHECK(noBaseline.problem.find("baseline has no GPU canary reference") != std::string::npos);
  const auto noCandidate = gate::judgeCanary(without, with, 8.0);
  CHECK_FALSE(noCandidate.measured);
  CHECK(noCandidate.problem.find("this run has no GPU canary measurement") != std::string::npos);

  json::Value otherWorkload = with;
  json::Value member = *with.find("canary");
  member.set("workload", json::Value::string("gate-canary/2"));
  otherWorkload.set("canary", std::move(member));
  const auto mismatch = gate::judgeCanary(with, otherWorkload, 8.0);
  CHECK_FALSE(mismatch.measured);
  CHECK(mismatch.problem.find("record the baseline again") != std::string::npos);
}

TEST_CASE("the judgment is a verdict member, deterministic and round-tripping", "[runtime][gate][canary]") {
  const json::Value baseline = setWith({kStableCanary, kStableCanary});
  const CanaryMs slow = {kStableCanary[0] * 1.5, kStableCanary[1] * 2.0, kStableCanary[2] * 2.0};
  const auto judgment = gate::judgeCanary(setWith({slow, slow}), baseline, 8.0);
  const json::Value value = gate::canaryJudgmentValue(judgment);
  CHECK(value.find("shifted")->asBool());
  CHECK(number(value.find("threshold_pct")) == 8.0);
  CHECK(number(gate::at(value, {"shift_pct", "bw"})) == Approx(100.0));
  CHECK(number(gate::at(value, {"reference", "alu", "center"})) == Approx(kStableCanary[0]));
  CHECK(value.find("shifted_components")->asArray().size() == 3);
  CHECK(json::write(value) == json::write(gate::canaryJudgmentValue(judgment)));
  const auto parsed = json::parse(json::write(value));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == value);
  // An unmeasured canary is a member too, with the reason.
  const json::Value unmeasured = gate::canaryJudgmentValue(gate::judgeCanary(baseline, gate_test::makeSet({}, {}, {}, gate_test::CanarySpec::omit()), 8.0));
  CHECK_FALSE(unmeasured.find("measured")->asBool());
  CHECK(unmeasured.find("problem")->asString().size() > 0);
}

TEST_CASE("a recording's canary spread is the widest component's max over min", "[runtime][gate][canary]") {
  const json::Value steady = setWith({kStableCanary, kStableCanary, kStableCanary, kStableCanary});
  CHECK(gate::canarySpreadPct(steady) == Approx(0.0).margin(1e-9));
  const CanaryMs high = {kStableCanary[0], kStableCanary[1] * 1.12, kStableCanary[2] * 1.03};
  const json::Value straddled = setWith({kStableCanary, kStableCanary, high, high});
  CHECK(gate::canarySpreadPct(straddled) == Approx(12.0));
  CHECK(gate::canarySpreadPct(gate_test::makeSet({}, {}, {}, gate_test::CanarySpec::omit())) < 0.0);
}

TEST_CASE("component names map to their index", "[runtime][gate][canary]") {
  CHECK(gate::canaryComponentIndex("alu") == 0);
  CHECK(gate::canaryComponentIndex("bw") == 1);
  CHECK(gate::canaryComponentIndex("tex") == 2);
}

TEST_CASE("the embedded canary SPIR-V modules are well-formed modules the gate can hand to the device",
          "[runtime][gate][canary]") {
  const auto check = [](const auto& words, const char* name) {
    INFO(name);
    REQUIRE(words.size() > 5);
    CHECK(words[0] == 0x07230203U);  // the SPIR-V magic number
    CHECK(words[1] == 0x00010000U);  // version 1.0, the profile the shader build uses (ADR-0028)
    CHECK(words[3] > 0U);            // an id bound
    CHECK(words[3] < 4096U);
    CHECK(words[4] == 0U);           // the reserved schema word
  };
  check(atlantis::runtime::canary::kVertexSpirv, "vertex");
  check(atlantis::runtime::canary::kBwSpirv, "bw");
  check(atlantis::runtime::canary::kTexSpirv, "tex");
  check(atlantis::runtime::canary::kAluSpirv, "alu");
}

TEST_CASE("the canary workload is the fixed one the baselines are recorded against", "[runtime][gate][canary]") {
  // Changing any of these is a new workload: bump kCanaryWorkloadId so every baseline is refreshed on purpose.
  CHECK(gate::kCanaryWorkloadId == "gate-canary/1");
  CHECK(gate::kCanaryWidth == 2560U);
  CHECK(gate::kCanaryHeight == 1440U);
  CHECK(gate::kCanaryRepetitions == 25U);
  CHECK(gate::kCanaryWarmup == 5U);
  CHECK(gate::kCanaryAluIterations == 768U);
  CHECK(gate::kCanaryBwOverdraw == 32U);
  CHECK(gate::kCanaryTexFetches == 16U);
  CHECK(gate::kCanaryComponents.size() == 3);
}
