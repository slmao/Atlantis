#include "../../src/runtime/gate_run.h"

#include "../../src/runtime/gate_set.h"
#include "../../src/runtime/gate_store.h"
#include "gate_test_fakes.h"
#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

// Spec 0060 R3, R5-R7 / ADR-0119 D4-D5 (Plan 0060 M5): the gate driver end to end against a fake
// launcher and a synthetic baseline store -- every verdict, every refusal, every exit code. No
// process, no device, no measured time.

namespace {

namespace gate = atlantis::runtime::gate;
namespace cli = atlantis::runtime::cli;
namespace json = atlantis::connection::json;
namespace fs = std::filesystem;
using gate_test::DocumentIdentity;
using gate_test::FakeClock;
using gate_test::FakeLauncher;
using gate_test::FakeSampler;
using gate_test::makeRun;

// The metrics of a synthetic run: `scale` multiplies the times, `jitter` is a per-run wobble.
std::map<std::string, double> metricsAt(double scale, double jitter = 0.0) {
  return {{"cpu_active_ms", 12.0 * scale * (1.0 + jitter)},
          {"gpu_busy_ms", 17.0 * scale * (1.0 + jitter)},
          {"zones.RenderExtraction", 4.5 * scale * (1.0 + jitter)},
          {"passes.draw.gpu_ms", 14.0 * scale * (1.0 + jitter)},
          {"passes.draw.cpu_record_ms", 3.3 * scale * (1.0 + jitter)},
          {"draw_calls", 4778.0},
          {"descriptor_writes", 1093.0},
          {"triangles", 5518696.0},
          {"lights_point", 59.0},
          {"renderables_total", 2909.0},
          {"frame_ms", 28.0 * scale}};
}

// A tiny, fixed wobble: run i is off by 0.05 % * (i mod 3 - 1).
double wobble(std::size_t i) { return 0.0005 * (static_cast<double>(i % 3) - 1.0); }

struct Fixture {
  Fixture() {
    root = fs::temp_directory_path() / "atlantis_gate_run_test";
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~Fixture() { fs::remove_all(root); }

  // Writes <root>/store/test-profile/{profile.json, bistro/bistro_baseline_1920x1080.baseline.json}.
  void writeStore(const DocumentIdentity& identity = {}, bool withProvenance = true) {
    std::vector<json::Value> runs;
    for (std::size_t i = 0; i < 5; ++i) runs.push_back(makeRun(metricsAt(1.0, wobble(i)), identity));
    gate::Protocol protocol;
    auto set = gate::buildSet(protocol, runs);
    REQUIRE(set.isOk());
    json::Value doc = set.value();
    if (withProvenance) {
      json::Value provenance = json::Value::object();
      provenance.set("profile", json::Value::string("test-profile"));
      provenance.set("recorded_commit", json::Value::string("abc123"));
      provenance.set("recorded_at", json::Value::string("2026-10-12"));
      json::Value reason = json::Value::object();
      reason.set("category", json::Value::number(std::int64_t{4}));
      reason.set("text", json::Value::string("initial baseline"));
      provenance.set("reason", std::move(reason));
      doc.set("baseline", std::move(provenance));
    }

    const fs::path profileDir = root / "store" / "test-profile";
    fs::create_directories(profileDir / "bistro");
    json::Value profile = json::Value::object();
    profile.set("schema", json::Value::string("atlantis.benchmark.profile/1"));
    profile.set("id", json::Value::string("test-profile"));
    profile.set("label", json::Value::string("Test machine"));
    profile.set("identity", gate::identityOf(doc));
    std::ofstream(profileDir / "profile.json", std::ios::binary) << json::write(profile) << "\n";
    std::ofstream(profileDir / "bistro" / "bistro_baseline_1920x1080.baseline.json", std::ios::binary)
        << json::write(doc) << "\n";
  }

  // One gate run over the store, with `candidate(i)` the document child i writes.
  struct Outcome {
    int exitCode = -1;
    std::string out;
    std::string err;
    std::vector<gate::ChildSpec> calls;
    json::Value verdict;
  };

  Outcome run(cli::GateRequest request, std::function<json::Value(std::size_t)> candidate = {}, FakeLauncher* use = nullptr,
              FakeSampler* sampler = nullptr) {
    FakeLauncher local;
    FakeLauncher& launcher = use != nullptr ? *use : local;
    launcher.makeDocument = candidate ? candidate : [](std::size_t i) { return makeRun(metricsAt(1.0, wobble(i))); };
    FakeClock clock;
    FakeSampler idle({99.0});
    std::ostringstream out;
    std::ostringstream err;
    gate::GateEnvironment env;
    env.launcher = &launcher;
    env.sampler = sampler != nullptr ? sampler : &idle;
    env.clock = &clock;
    env.tempDir = (root / "tmp").string();
    env.performanceDir = ATLANTIS_PERFORMANCE_DIR;
    env.out = &out;
    env.err = &err;
    if (!request.baselinesDir.has_value()) request.baselinesDir = (root / "store").string();
    Outcome outcome;
    outcome.exitCode = gate::runGateWith(request, env);
    outcome.out = out.str();
    outcome.err = err.str();
    outcome.calls = launcher.calls;
    if (const auto parsed = json::parse(outcome.out); parsed.isOk()) outcome.verdict = parsed.value();
    return outcome;
  }

  fs::path root;
};

cli::GateRequest request() {
  cli::GateRequest r;
  r.sceneName = "bistro";
  r.json = true;
  return r;
}

std::string verdictOf(const Fixture::Outcome& o) { return o.verdict.find("verdict")->asString(); }

}  // namespace

TEST_CASE("an unchanged candidate passes: exit 0 and exactly one gate document on stdout", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  const auto o = f.run(request());
  CHECK(o.exitCode == 0);
  REQUIRE(o.verdict.isObject());
  CHECK(o.verdict.find("schema")->asString() == "atlantis.benchmark.gate/1");
  CHECK(verdictOf(o) == "pass");
  CHECK(gate::at(o.verdict, {"baseline", "profile"})->asString() == "test-profile");
  CHECK(gate::at(o.verdict, {"baseline", "commit"})->asString() == "abc123");
  CHECK(gate::at(o.verdict, {"protocol", "conformant"})->asBool());
  CHECK(o.calls.size() == 6);  // the preheat and the five runs of the policy
  CHECK(o.out.back() == '\n');
  CHECK(std::count(o.out.begin(), o.out.end(), '\n') == 1);  // one document, one line
  CHECK(o.err.empty());
  CHECK_FALSE(fs::exists(f.root / "tmp"));  // the children's directory is gone
}

TEST_CASE("without --json stdout is a short summary, not the document", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  cli::GateRequest r = request();
  r.json = false;
  const auto o = f.run(r);
  CHECK(o.exitCode == 0);
  CHECK(o.out.rfind("verdict: pass (exit 0)", 0) == 0);
  CHECK(o.out.find('{') == std::string::npos);
}

TEST_CASE("a slower candidate fails with exit 4 and a per-metric report", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  const auto o = f.run(request(), [](std::size_t i) {
    auto m = metricsAt(1.0, wobble(i));
    m["cpu_active_ms"] *= 1.25;
    m["zones.RenderExtraction"] *= 1.25;
    return makeRun(m);
  });
  CHECK(o.exitCode == 4);
  CHECK(verdictOf(o) == "fail");
  const auto& regressed = gate::at(o.verdict, {"summary", "regressed"})->asArray();
  REQUIRE(regressed.size() >= 2);
  std::vector<std::string> names;
  for (const auto& n : regressed) names.push_back(n.asString());
  CHECK(std::find(names.begin(), names.end(), "cpu_active_ms") != names.end());
  CHECK(std::find(names.begin(), names.end(), "zones.RenderExtraction") != names.end());
  const json::Value* cpu = gate::at(o.verdict, {"metrics", "cpu_active_ms"});
  REQUIRE(cpu != nullptr);
  CHECK(cpu->find("status")->asString() == "regressed");
  CHECK(gate::numberOf(cpu->find("delta_pct")) > 20.0);
}

TEST_CASE("the human summary names what regressed and by how much", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  cli::GateRequest r = request();
  r.json = false;
  const auto o = f.run(r, [](std::size_t i) {
    auto m = metricsAt(1.0, wobble(i));
    m["cpu_active_ms"] *= 1.25;
    return makeRun(m);
  });
  CHECK(o.exitCode == 4);
  CHECK(o.out.rfind("verdict: fail (exit 4)", 0) == 0);
  CHECK(o.out.find("regressed: cpu_active_ms") != std::string::npos);
  CHECK(o.out.find("tolerance") != std::string::npos);
}

TEST_CASE("one more draw call per frame fails the gate", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  const auto o = f.run(request(), [](std::size_t i) {
    auto m = metricsAt(1.0, wobble(i));
    m["draw_calls"] += 1.0;
    return makeRun(m);
  });
  CHECK(o.exitCode == 4);
  CHECK(gate::at(o.verdict, {"metrics", "draw_calls", "status"})->asString() == "regressed");
}

TEST_CASE("a machine no profile describes is not judged, and the verdict says how to record one",
          "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  DocumentIdentity other;
  other.gpu = "Some Other GPU";
  const auto o = f.run(request(), [other](std::size_t i) { return makeRun(metricsAt(1.0, wobble(i)), other); });
  CHECK(o.exitCode == 5);
  CHECK(verdictOf(o) == "incomparable");
  CHECK_FALSE(o.verdict.find("comparable")->asBool());
  const auto& reasons = o.verdict.find("reasons")->asArray();
  REQUIRE(reasons.size() == 1);
  CHECK(reasons[0].asString().find("--gate --record") != std::string::npos);
}

TEST_CASE("an empty or absent store is a refusal with the same way forward", "[runtime][gate][run]") {
  Fixture f;  // no store written
  const auto o = f.run(request());
  CHECK(o.exitCode == 5);
  CHECK(verdictOf(o) == "incomparable");
  fs::create_directories(f.root / "store");  // present but empty
  CHECK(f.run(request()).exitCode == 5);
}

TEST_CASE("a profile with no baseline for the requested variant is not judged", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  cli::GateRequest r = request();
  r.variant = cli::BenchmarkVariant::NoFog;
  const auto o = f.run(r, [](std::size_t i) {
    DocumentIdentity id;
    id.variant = "no-fog";
    return makeRun(metricsAt(1.0, wobble(i)), id);
  });
  CHECK(o.exitCode == 5);
  const auto& reasons = o.verdict.find("reasons")->asArray();
  REQUIRE(reasons.size() == 1);
  CHECK(reasons[0].asString().find("no baseline for bistro/no-fog") != std::string::npos);
  CHECK(gate::at(o.verdict, {"baseline", "profile"})->asString() == "test-profile");
}

TEST_CASE("a session far noisier than the baseline is inconclusive, not a verdict", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  const auto o = f.run(request(), [](std::size_t i) {
    auto m = metricsAt(1.0, wobble(i));
    const double factors[] = {1.0, 0.6, 1.0, 1.5, 0.8, 1.3};  // a different run every time
    m["gpu_busy_ms"] *= factors[i % 6];
    return makeRun(m);
  });
  CHECK(o.exitCode == 5);
  CHECK(verdictOf(o) == "inconclusive");
  CHECK(o.verdict.find("comparable")->asBool());
  CHECK_FALSE(gate::at(o.verdict, {"dispersion", "exceeded"})->asArray().empty());
}

TEST_CASE("a child failure ends the gate with that failure's exit code and no verdict", "[runtime][gate][run]") {
  const struct {
    std::size_t call;
    int childExit;
    int expected;
  } cases[] = {{0, 3, 3}, {2, 3, 3}, {1, 2, 2}, {3, 139, 2}, {0, 1, 1}};
  for (const auto& c : cases) {
    INFO("call " << c.call << " exit " << c.childExit);
    Fixture f;
    f.writeStore();
    FakeLauncher launcher;
    gate::ChildResult failed;
    failed.exitCode = c.childExit;
    launcher.scripted[c.call] = failed;
    const auto o = f.run(request(), {}, &launcher);
    CHECK(o.exitCode == c.expected);
    CHECK(o.out.empty());  // stdout carries a verdict or nothing
    CHECK_FALSE(o.err.empty());
    CHECK(o.calls.size() == c.call + 1);
  }
}

TEST_CASE("a machine that is never idle ends the gate with exit 3 before any child runs", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  FakeSampler busy({10.0});
  const auto o = f.run(request(), {}, nullptr, &busy);
  CHECK(o.exitCode == 3);
  CHECK(o.out.empty());
  CHECK(o.calls.empty());
}

TEST_CASE("an unreadable policy, an unknown profile and a broken store are initialization failures",
          "[runtime][gate][run]") {
  {
    Fixture f;
    f.writeStore();
    cli::GateRequest r = request();
    r.policyPath = (f.root / "no_policy.json").string();
    const auto o = f.run(r);
    CHECK(o.exitCode == 1);
    CHECK(o.out.empty());
  }
  {
    Fixture f;
    f.writeStore();
    cli::GateRequest r = request();
    r.profileId = "no-such-profile";
    CHECK(f.run(r).exitCode == 1);
  }
  {
    Fixture f;
    f.writeStore();
    std::ofstream(f.root / "store" / "test-profile" / "bistro" / "bistro_baseline_1920x1080.baseline.json",
                  std::ios::binary | std::ios::trunc)
        << "{not json";
    const auto o = f.run(request());
    CHECK(o.exitCode == 1);
    CHECK(o.err.find("not valid JSON") != std::string::npos);
  }
  {
    Fixture f;
    f.writeStore({}, /*withProvenance=*/false);  // a set that was not recorded with --gate --record
    const auto o = f.run(request());
    CHECK(o.exitCode == 1);
    CHECK(o.err.find("no baseline provenance") != std::string::npos);
  }
}

TEST_CASE("--profile names the profile explicitly", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  cli::GateRequest r = request();
  r.profileId = "test-profile";
  CHECK(f.run(r).exitCode == 0);
  r.profileId = "auto";
  CHECK(f.run(r).exitCode == 0);
}

TEST_CASE("--output writes the same document the gate prints", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  cli::GateRequest r = request();
  r.outputPath = (f.root / "verdict.json").string();
  const auto o = f.run(r);
  REQUIRE(fs::exists(f.root / "verdict.json"));
  std::ifstream in(f.root / "verdict.json", std::ios::binary);
  const std::string file((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  CHECK(file == o.out);
}

TEST_CASE("a smoke run says it did not follow the protocol", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  cli::GateRequest r = request();
  r.runs = 2;
  r.preheatSeconds = 0.0;
  r.idleWait = false;
  const auto o = f.run(r);
  CHECK_FALSE(gate::at(o.verdict, {"protocol", "conformant"})->asBool());
  CHECK(o.err.find("did not follow the policy") != std::string::npos);
  CHECK(o.calls.size() == 2);  // no preheat
}

TEST_CASE("the same inputs give the same bytes", "[runtime][gate][run]") {
  Fixture f;
  f.writeStore();
  const auto a = f.run(request());
  const auto b = f.run(request());
  CHECK(a.out == b.out);
  CHECK(a.exitCode == b.exitCode);
}
