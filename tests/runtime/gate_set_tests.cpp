#include "../../src/runtime/gate_set.h"

#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

// Spec 0060 R1, R2 / ADR-0119 D2 (Plan 0060 M1): run-level extraction and the
// `atlantis.benchmark.set/1` aggregate, on synthetic documents with no device.

namespace {

namespace gate = atlantis::runtime::gate;
namespace json = atlantis::connection::json;
using Catch::Approx;
using gate_test::makeRun;
using gate_test::DocumentIdentity;

double number(const json::Value* v) { return *gate::numberOf(v); }

gate::Protocol protocol() {
  gate::Protocol p;
  p.runs = 5;
  p.preheatSeconds = 90.0;
  p.idleWait = true;
  p.k = 3.0;
  return p;
}

}  // namespace

TEST_CASE("run-level values carry the p50 and p95 of every metric, named as the comparison names them",
          "[runtime][gate][set]") {
  const json::Value doc = makeRun({{"cpu_active_ms", 12.0},
                                   {"gpu_busy_ms", 17.0},
                                   {"zones.RenderExtraction", 4.5},
                                   {"passes.draw.gpu_ms", 14.0},
                                   {"passes.draw.cpu_record_ms", 3.3},
                                   {"draw_calls", 4778.0}});
  const auto extracted = gate::extractRunValues(doc);
  REQUIRE(extracted.isOk());
  const gate::RunValues& values = extracted.value();
  CHECK(values.p50.at("cpu_active_ms") == Approx(12.0));
  CHECK(values.p95.at("cpu_active_ms") == Approx(13.2));
  CHECK(values.p50.at("zones.RenderExtraction") == Approx(4.5));
  CHECK(values.p50.at("passes.draw.gpu_ms") == Approx(14.0));
  CHECK(values.p50.at("passes.draw.cpu_record_ms") == Approx(3.3));
  CHECK(values.p50.at("draw_calls") == Approx(4778.0));
  CHECK(values.unavailable.empty());
}

TEST_CASE("an unavailable metric stays unavailable and keeps its reason", "[runtime][gate][set]") {
  const json::Value doc =
      makeRun({{"cpu_active_ms", 12.0}}, {}, {{"gpu_busy_ms", "disabled by --gpu-timing=off"}});
  const auto extracted = gate::extractRunValues(doc);
  REQUIRE(extracted.isOk());
  CHECK(extracted.value().p50.count("gpu_busy_ms") == 0);
  CHECK(extracted.value().unavailable.at("gpu_busy_ms") == "disabled by --gpu-timing=off");
}

TEST_CASE("a document that is not atlantis.benchmark/1 is refused", "[runtime][gate][set]") {
  json::Value wrong = json::Value::object();
  wrong.set("schema", json::Value::string("atlantis.benchmark/2"));
  CHECK(gate::extractRunValues(wrong).isErr());
  CHECK(gate::extractRunValues(json::Value::object()).isErr());
  json::Value noMetrics = json::Value::object();
  noMetrics.set("schema", json::Value::string("atlantis.benchmark/1"));
  CHECK(gate::extractRunValues(noMetrics).isErr());
}

TEST_CASE("the aggregate is the median of the run p50s with a robust sigma, min and max", "[runtime][gate][set]") {
  // cpu_active_ms over five runs: 12.0 12.2 11.8 12.1 12.4 -> median 12.1; deviations .1 .1 .3 0 .3 -> MAD .1.
  std::vector<json::Value> runs;
  for (const double v : {12.0, 12.2, 11.8, 12.1, 12.4}) runs.push_back(makeRun({{"cpu_active_ms", v}, {"draw_calls", 4778.0}}));
  const auto set = gate::buildSet(protocol(), runs);
  REQUIRE(set.isOk());
  const json::Value& doc = set.value();
  CHECK(doc.find("schema")->asString() == "atlantis.benchmark.set/1");
  const json::Value* cpu = gate::at(doc, {"aggregate", "cpu_active_ms"});
  REQUIRE(cpu != nullptr);
  CHECK(number(cpu->find("center")) == Approx(12.1));
  CHECK(number(cpu->find("sigma")) == Approx(1.4826 * 0.1));
  CHECK(number(cpu->find("min")) == Approx(11.8));
  CHECK(number(cpu->find("max")) == Approx(12.4));
  CHECK(number(cpu->find("n")) == 5.0);
  const json::Value* calls = gate::at(doc, {"aggregate", "draw_calls"});
  REQUIRE(calls != nullptr);
  CHECK(number(calls->find("sigma")) == 0.0);  // a deterministic counter has no spread
  CHECK(doc.find("runs")->asArray().size() == 5);
  CHECK(number(gate::at(doc, {"runs"})->asArray()[1].find("values")->find("cpu_active_ms")) == Approx(12.2));
}

TEST_CASE("the set records the protocol and the runs' common meta, scene and settings", "[runtime][gate][set]") {
  const std::vector<json::Value> runs{makeRun({{"cpu_active_ms", 12.0}}), makeRun({{"cpu_active_ms", 12.1}})};
  gate::Protocol p = protocol();
  p.runs = 2;
  p.idleWait = false;
  p.preheatSeconds = 0.0;
  const auto set = gate::buildSet(p, runs);
  REQUIRE(set.isOk());
  const json::Value& doc = set.value();
  CHECK(number(gate::at(doc, {"protocol", "runs"})) == 2.0);
  CHECK(gate::at(doc, {"protocol", "idle_wait"})->asBool() == false);
  CHECK(number(gate::at(doc, {"protocol", "preheat_seconds"})) == 0.0);
  CHECK(gate::at(doc, {"protocol", "child_stderr"})->asString() == "discarded");
  CHECK(*gate::at(doc, {"protocol", "pin"}) == gate::pinValue(false));  // the default protocol is unpinned
  CHECK(gate::at(doc, {"scene", "name"})->asString() == "bistro");
  CHECK(number(gate::at(doc, {"settings", "resolution", "width"})) == 1920.0);
  CHECK(gate::at(doc, {"meta", "system", "gpu"})->asString() == "Test GPU");
}

TEST_CASE("runs that disagree on scene, resolution or hardware do not make a set", "[runtime][gate][set]") {
  DocumentIdentity otherScene;
  otherScene.guid = "guid-2";
  DocumentIdentity otherSize;
  otherSize.width = 1280;
  DocumentIdentity otherGpu;
  otherGpu.gpu = "Other GPU";
  for (const DocumentIdentity& other : {otherScene, otherSize, otherGpu}) {
    const std::vector<json::Value> runs{makeRun({{"cpu_active_ms", 12.0}}), makeRun({{"cpu_active_ms", 12.0}}, other)};
    const auto set = gate::buildSet(protocol(), runs);
    CHECK(set.isErr());
  }
  CHECK(gate::buildSet(protocol(), {}).isErr());
}

TEST_CASE("a metric some runs could not supply is unavailable in the aggregate", "[runtime][gate][set]") {
  const std::vector<json::Value> runs{makeRun({{"cpu_active_ms", 12.0}, {"gpu_busy_ms", 17.0}}),
                                      makeRun({{"cpu_active_ms", 12.2}}, {}, {{"gpu_busy_ms", "no GPU results"}})};
  const auto set = gate::buildSet(protocol(), runs);
  REQUIRE(set.isOk());
  const json::Value* gpu = gate::at(set.value(), {"aggregate", "gpu_busy_ms"});
  REQUIRE(gpu != nullptr);
  CHECK(gpu->find("unavailable") != nullptr);
  CHECK(gpu->find("center") == nullptr);
  CHECK(gate::at(set.value(), {"aggregate", "cpu_active_ms", "center"}) != nullptr);
}

TEST_CASE("the same runs always give the same bytes", "[runtime][gate][set]") {
  std::vector<json::Value> runs;
  for (const double v : {12.0, 12.2, 11.8}) runs.push_back(makeRun({{"cpu_active_ms", v}, {"passes.draw.gpu_ms", v + 2.0}}));
  const auto a = gate::buildSet(protocol(), runs);
  const auto b = gate::buildSet(protocol(), runs);
  REQUIRE(a.isOk());
  REQUIRE(b.isOk());
  CHECK(json::write(a.value()) == json::write(b.value()));
  // The set survives a round trip through its own text.
  const auto parsed = json::parse(json::write(a.value()));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value() == a.value());
}

TEST_CASE("the power source is recorded in the set", "[runtime][gate][set]") {
  const std::vector<json::Value> runs{makeRun({{"cpu_active_ms", 12.0}})};
  CHECK(gate::at(gate::buildSet(protocol(), runs).value(), {"protocol", "power"})->asString() == "unknown");  // never assumed
  for (const char* source : {"mains", "battery", "mixed"}) {
    gate::Protocol p = protocol();
    p.power = source;
    const auto set = gate::buildSet(p, runs);
    REQUIRE(set.isOk());
    CHECK(gate::at(set.value(), {"protocol", "power"})->asString() == source);
  }
}

TEST_CASE("the pinning state is recorded in the set", "[runtime][gate][set]") {
  CHECK(gate::at(gate::pinValue(true), {"power_throttling"})->asString() == "disabled");
  CHECK(gate::at(gate::pinValue(true), {"priority_class"})->asString() == "high");
  CHECK(gate::at(gate::pinValue(false), {"power_throttling"})->asString() == "default");
  CHECK(gate::at(gate::pinValue(false), {"priority_class"})->asString() == "normal");
  const std::vector<json::Value> runs{makeRun({{"cpu_active_ms", 12.0}})};
  gate::Protocol pinned = protocol();
  pinned.pin = true;
  const auto set = gate::buildSet(pinned, runs);
  REQUIRE(set.isOk());
  CHECK(*gate::at(set.value(), {"protocol", "pin"}) == gate::pinValue(true));
  CHECK_FALSE(*gate::at(set.value(), {"protocol", "pin"}) == gate::pinValue(false));
}
