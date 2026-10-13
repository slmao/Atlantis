#include "../../src/runtime/gate_run.h"

#include "../../src/runtime/gate_store.h"
#include "gate_test_fakes.h"
#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

// Spec 0060 R3, R4 / ADR-0118 D3, ADR-0119 D6 (Plan 0060 M6): `--gate --record` -- the protocol over
// several sessions, the provenance and the reason on the baseline, and the refusals that keep a
// smoke run or a nameless change out of the store. A fake launcher and clock; no device.

namespace {

namespace gate = atlantis::runtime::gate;
namespace cli = atlantis::runtime::cli;
namespace json = atlantis::connection::json;
namespace fs = std::filesystem;
using gate_test::DocumentIdentity;
using gate_test::FakeClock;
using gate_test::FakeLauncher;
using gate_test::FakePower;
using gate_test::FakeSampler;
using gate_test::makeRun;

std::map<std::string, double> metricsAt(double jitter) {
  return {{"cpu_active_ms", 12.0 * (1.0 + jitter)}, {"gpu_busy_ms", 17.0 * (1.0 + jitter)},
          {"zones.RenderExtraction", 4.5}, {"passes.draw.gpu_ms", 14.0 * (1.0 + jitter)},
          {"draw_calls", 4778.0}, {"lights_point", 59.0}, {"renderables_total", 2909.0}, {"frame_ms", 28.0}};
}

double wobble(std::size_t i) { return 0.001 * (static_cast<double>(i % 5) - 2.0); }

struct Fixture {
  Fixture() {
    root = gate_test::uniqueTempDir("atlantis_gate_record_test");
    fs::remove_all(root);
    fs::create_directories(root);
  }
  ~Fixture() { fs::remove_all(root); }

  struct Outcome {
    int exitCode = -1;
    std::string out;
    std::string err;
    std::size_t launches = 0;
    double slept = 0.0;
  };

  Outcome run(const cli::GateRequest& request, FakeLauncher& launcher) {
    FakeClock clock;
    FakeSampler idle({99.0});
    std::ostringstream out;
    std::ostringstream err;
    gate::GateEnvironment env;
    env.launcher = &launcher;
    env.power = &power;
    env.sampler = &idle;
    env.clock = &clock;
    env.tempDir = (root / "tmp").string();
    env.performanceDir = ATLANTIS_PERFORMANCE_DIR;
    env.out = &out;
    env.err = &err;
    Outcome outcome;
    outcome.exitCode = gate::runGateWith(request, env);
    outcome.out = out.str();
    outcome.err = err.str();
    outcome.launches = launcher.calls.size();
    outcome.slept = clock.slept;
    return outcome;
  }

  cli::GateRequest recordRequest(unsigned int sessions = 2) const {
    cli::GateRequest r;
    r.mode = cli::GateRequest::Mode::Record;
    r.sceneName = "bistro";
    r.sessions = sessions;
    r.sessionGapMinutes = 1.5;
    r.reasonCategory = 4;
    r.reasonText = "initial baseline for the test profile";
    r.recordTo = (root / "out").string();
    return r;
  }

  fs::path root;
  FakePower power;  // mains unless a test says otherwise
};

FakeLauncher steadyLauncher() {
  FakeLauncher launcher;
  launcher.makeDocument = [](std::size_t i) { return makeRun(metricsAt(wobble(i))); };
  return launcher;
}

// A recording whose sessions disagree: the CPU level sits at one of three states per session, so the pooled run-level
// spread (and with it the minimum detectable regression) is far above the bar. The GPU time stays steady.
FakeLauncher noisyCpuLauncher() {
  FakeLauncher launcher;
  launcher.makeDocument = [](std::size_t i) {
    auto m = metricsAt(wobble(i));
    const double states[] = {0.85, 1.0, 1.25};
    m["cpu_active_ms"] *= states[(i / 6) % 3] * (1.0 + 0.04 * (static_cast<double>(i % 5) - 2.0));
    return makeRun(m);
  };
  return launcher;
}

// The same, on the GPU: its time sits at different levels from session to session (the battery/mains case).
FakeLauncher noisyGpuLauncher() {
  FakeLauncher launcher;
  launcher.makeDocument = [](std::size_t i) {
    auto m = metricsAt(wobble(i));
    const double states[] = {1.0, 1.7, 1.0};
    m["gpu_busy_ms"] *= states[(i / 6) % 3] * (1.0 + 0.04 * (static_cast<double>(i % 5) - 2.0));
    return makeRun(m);
  };
  return launcher;
}

}  // namespace

TEST_CASE("recording runs the protocol once per session, waits between sessions, and writes a baseline",
          "[runtime][gate][record]") {
  Fixture f;
  FakeLauncher launcher = steadyLauncher();
  const auto o = f.run(f.recordRequest(2), launcher);
  REQUIRE(o.exitCode == 0);
  CHECK(o.launches == 12);  // two sessions of one preheat and five runs
  CHECK(o.slept >= 90.0);   // the gap of 1.5 minutes between the sessions
  CHECK(o.out.find("recorded baseline for profile test-gpu-windows-60hz (new profile)") != std::string::npos);

  const std::string store = (f.root / "out").string();
  const auto listed = gate::listProfiles(store);
  REQUIRE(listed.isOk());
  REQUIRE(listed.value().size() == 1);
  CHECK(listed.value()[0].id == "test-gpu-windows-60hz");
  const auto baseline = gate::loadBaseline(gate::baselinePath(listed.value()[0], "bistro", "baseline", 1920, 1080));
  REQUIRE(baseline.error.empty());
  REQUIRE_FALSE(baseline.notFound);
  const json::Value& doc = baseline.document;
  CHECK(*gate::numberOf(gate::at(doc, {"aggregate", "cpu_active_ms", "n"})) == 10.0);  // both sessions' runs
  CHECK(*gate::numberOf(gate::at(doc, {"protocol", "runs"})) == 5.0);
  CHECK(gate::at(doc, {"protocol", "idle_wait"})->asBool());
  CHECK(*gate::numberOf(gate::at(doc, {"protocol", "preheat_seconds"})) == 60.0);
  CHECK(*gate::at(doc, {"protocol", "pin"}) == gate::pinValue(true));
  CHECK(*gate::at(doc, {"baseline", "pin"}) == gate::pinValue(true));  // the pinning state is in the provenance
  CHECK(gate::at(doc, {"protocol", "power"})->asString() == "mains");
  CHECK(gate::at(doc, {"baseline", "power"})->asString() == "mains");  // and so is the power source
  CHECK(gate::at(doc, {"baseline", "profile"})->asString() == "test-gpu-windows-60hz");
  CHECK(gate::at(doc, {"baseline", "recorded_at"})->asString() == "2026-10-12T00:00:00Z");
  CHECK(gate::at(doc, {"baseline", "sessions"})->asArray().size() == 2);
  CHECK(*gate::numberOf(gate::at(doc, {"baseline", "reason", "category"})) == 4.0);
  CHECK(gate::at(doc, {"baseline", "reason", "text"})->asString() == "initial baseline for the test profile");
  CHECK(gate::at(doc, {"mdr", "cpu"})->isNumber());
  CHECK(gate::at(doc, {"mdr", "gpu"})->isNumber());
  CHECK(*gate::numberOf(gate::at(doc, {"mdr", "counter"})) == 0.0);
  CHECK(gate::validateStore(store).empty());  // what it wrote passes the store's own guard
}

TEST_CASE("with --json the recording prints the baseline document itself", "[runtime][gate][record]") {
  Fixture f;
  FakeLauncher launcher = steadyLauncher();
  cli::GateRequest r = f.recordRequest(1);
  r.json = true;
  const auto o = f.run(r, launcher);
  REQUIRE(o.exitCode == 0);
  const auto parsed = json::parse(o.out);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().find("schema")->asString() == "atlantis.benchmark.set/1");
  CHECK(parsed.value().find("baseline") != nullptr);
  CHECK(std::count(o.out.begin(), o.out.end(), '\n') == 1);
}

TEST_CASE("with --output the recording also writes the baseline document", "[runtime][gate][record]") {
  Fixture f;
  FakeLauncher launcher = steadyLauncher();
  cli::GateRequest r = f.recordRequest(1);
  r.outputPath = (f.root / "report.json").string();
  REQUIRE(f.run(r, launcher).exitCode == 0);
  std::ifstream in(f.root / "report.json", std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto parsed = json::parse(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().find("baseline") != nullptr);
}

TEST_CASE("a recorded baseline is judged against, end to end, by the gate", "[runtime][gate][record]") {
  Fixture f;
  FakeLauncher launcher = steadyLauncher();
  REQUIRE(f.run(f.recordRequest(2), launcher).exitCode == 0);

  FakeLauncher candidate = steadyLauncher();
  cli::GateRequest gateRequest;
  gateRequest.sceneName = "bistro";
  gateRequest.json = true;
  gateRequest.baselinesDir = (f.root / "out").string();
  const auto pass = f.run(gateRequest, candidate);
  CHECK(pass.exitCode == 0);

  FakeLauncher slower;
  slower.makeDocument = [](std::size_t i) {
    auto m = metricsAt(wobble(i));
    m["cpu_active_ms"] *= 1.3;
    return makeRun(m);
  };
  CHECK(f.run(gateRequest, slower).exitCode == 4);
}

TEST_CASE("a smoke protocol is never recorded", "[runtime][gate][record]") {
  struct Case {
    const char* what;
    std::function<void(cli::GateRequest&)> change;
  };
  const Case cases[] = {
      {"idle wait off", [](cli::GateRequest& r) { r.idleWait = false; }},
      {"a short preheat", [](cli::GateRequest& r) { r.preheatSeconds = 5.0; }},
      {"too few runs", [](cli::GateRequest& r) { r.runs = 2; }},
      {"unpinned children", [](cli::GateRequest& r) { r.pinProcess = false; }},
      {"no mains-power precondition", [](cli::GateRequest& r) { r.requireMains = false; }},
  };
  for (const Case& c : cases) {
    INFO(c.what);
    Fixture f;
    FakeLauncher launcher = steadyLauncher();
    cli::GateRequest r = f.recordRequest(1);
    c.change(r);
    const auto o = f.run(r, launcher);
    CHECK(o.exitCode == 1);
    CHECK(o.err.find("full protocol") != std::string::npos);
    CHECK(o.launches == 0);
    CHECK_FALSE(fs::exists(f.root / "out"));
  }
}

TEST_CASE("recording on battery power is refused before any child starts", "[runtime][gate][record][power]") {
  Fixture f;
  f.power = FakePower({gate::PowerSource::Battery});
  FakeLauncher launcher = steadyLauncher();
  const auto o = f.run(f.recordRequest(2), launcher);
  CHECK(o.exitCode == 3);
  CHECK(o.launches == 0);
  CHECK(o.out.empty());
  CHECK(o.err.find("battery") != std::string::npos);
  CHECK_FALSE(fs::exists(f.root / "out"));
}

TEST_CASE("a charger pulled between the sessions stops the recording and writes nothing",
          "[runtime][gate][record][power]") {
  Fixture f;
  // Reads: before session 1, after session 1, then battery before session 2.
  f.power = FakePower({gate::PowerSource::Mains, gate::PowerSource::Mains, gate::PowerSource::Battery});
  FakeLauncher launcher = steadyLauncher();
  const auto o = f.run(f.recordRequest(2), launcher);
  CHECK(o.exitCode == 3);
  CHECK(o.launches == 6);  // only the first session ran
  CHECK_FALSE(fs::exists(f.root / "out"));
}

TEST_CASE("a child failure in any session stops the recording and writes nothing", "[runtime][gate][record]") {
  Fixture f;
  FakeLauncher launcher = steadyLauncher();
  gate::ChildResult environment;
  environment.exitCode = 3;
  launcher.scripted[8] = environment;  // the third call of the second session
  const auto o = f.run(f.recordRequest(2), launcher);
  CHECK(o.exitCode == 3);
  CHECK(o.launches == 9);
  CHECK_FALSE(fs::exists(f.root / "out"));
}

TEST_CASE("sessions on different hardware or builds do not make one baseline", "[runtime][gate][record]") {
  Fixture f;
  FakeLauncher launcher;
  launcher.makeDocument = [](std::size_t i) {
    DocumentIdentity id;
    if (i >= 6) id.gpu = "Swapped GPU";  // the second session ran elsewhere
    return makeRun(metricsAt(wobble(i)), id);
  };
  const auto o = f.run(f.recordRequest(2), launcher);
  CHECK(o.exitCode == 2);
  CHECK(o.err.find("do not make a set") != std::string::npos);
  CHECK_FALSE(fs::exists(f.root / "out"));
}

TEST_CASE("a named profile id is used, and a bad id is refused before anything runs", "[runtime][gate][record]") {
  {
    Fixture f;
    FakeLauncher launcher = steadyLauncher();
    cli::GateRequest r = f.recordRequest(1);
    r.profileId = "reference-laptop";
    REQUIRE(f.run(r, launcher).exitCode == 0);
    CHECK(fs::exists(f.root / "out" / "reference-laptop" / "profile.json"));
    CHECK(fs::exists(f.root / "out" / "reference-laptop" / "bistro" / "bistro_baseline_1920x1080.baseline.json"));
  }
  {
    Fixture f;
    FakeLauncher launcher = steadyLauncher();
    cli::GateRequest r = f.recordRequest(1);
    r.profileId = "Not A Slug";
    const auto o = f.run(r, launcher);
    CHECK(o.exitCode == 1);
    CHECK(o.launches == 0);
  }
}

TEST_CASE("recording over a profile id that describes other hardware is refused and changes nothing",
          "[runtime][gate][record]") {
  Fixture f;
  FakeLauncher first = steadyLauncher();
  REQUIRE(f.run(f.recordRequest(1), first).exitCode == 0);
  const fs::path profileFile = f.root / "out" / "test-gpu-windows-60hz" / "profile.json";
  std::ifstream before(profileFile, std::ios::binary);
  const std::string profileBefore((std::istreambuf_iterator<char>(before)), std::istreambuf_iterator<char>());

  FakeLauncher other;
  other.makeDocument = [](std::size_t i) {
    DocumentIdentity id;
    id.cpu = "A Different CPU";
    return makeRun(metricsAt(wobble(i)), id);
  };
  cli::GateRequest r = f.recordRequest(1);
  r.profileId = "test-gpu-windows-60hz";  // the same id, other hardware
  const auto o = f.run(r, other);
  CHECK(o.exitCode == 1);
  CHECK(o.err.find("different hardware") != std::string::npos);
  std::ifstream after(profileFile, std::ios::binary);
  CHECK(std::string((std::istreambuf_iterator<char>(after)), std::istreambuf_iterator<char>()) == profileBefore);
}

TEST_CASE("an unreadable policy is an initialization failure", "[runtime][gate][record]") {
  Fixture f;
  FakeLauncher launcher = steadyLauncher();
  cli::GateRequest r = f.recordRequest(1);
  r.policyPath = (f.root / "no_policy.json").string();
  CHECK(f.run(r, launcher).exitCode == 1);
  CHECK(launcher.calls.empty());
}

// Spec 0060 Correction 2026-10-14: the record-time quality bar.

TEST_CASE("a recording too noisy on the CPU is refused with exit 3 and writes nothing", "[runtime][gate][record][bar]") {
  Fixture f;
  FakeLauncher launcher = noisyCpuLauncher();
  const auto o = f.run(f.recordRequest(3), launcher);
  CHECK(o.exitCode == 3);
  CHECK(o.launches == 18);  // all three sessions ran: the bar judges the recording, not a session
  CHECK(o.out.empty());
  CHECK_FALSE(fs::exists(f.root / "out"));  // not a candidate file, not a profile directory
  CHECK(o.err.find("refused, nothing was written") != std::string::npos);
  CHECK(o.err.find("cpu ") != std::string::npos);
  CHECK(o.err.find("(bound 10 %)") != std::string::npos);
  CHECK(o.err.find("record again") != std::string::npos);
  CHECK(o.err.find("not raised to make a recording pass") != std::string::npos);
}

TEST_CASE("a recording too noisy on the GPU is refused too, naming the tier", "[runtime][gate][record][bar]") {
  Fixture f;
  FakeLauncher launcher = noisyGpuLauncher();
  const auto o = f.run(f.recordRequest(3), launcher);
  CHECK(o.exitCode == 3);
  CHECK_FALSE(fs::exists(f.root / "out"));
  CHECK(o.err.find("gpu ") != std::string::npos);
  CHECK(o.err.find("(bound 20 %)") != std::string::npos);
  CHECK(o.err.find("cpu ") == std::string::npos);  // the quiet tier is not blamed
}

TEST_CASE("a steady recording passes the bar and its numbers are inside it", "[runtime][gate][record][bar]") {
  Fixture f;
  FakeLauncher launcher = steadyLauncher();
  const auto o = f.run(f.recordRequest(3), launcher);
  REQUIRE(o.exitCode == 0);
  const auto listed = gate::listProfiles((f.root / "out").string());
  REQUIRE(listed.isOk());
  REQUIRE(listed.value().size() == 1);
  const auto baseline = gate::loadBaseline(gate::baselinePath(listed.value()[0], "bistro", "baseline", 1920, 1080));
  REQUIRE(baseline.error.empty());
  CHECK(*gate::numberOf(gate::at(baseline.document, {"mdr", "cpu"})) <= 10.0);
  CHECK(*gate::numberOf(gate::at(baseline.document, {"mdr", "gpu"})) <= 20.0);
}

TEST_CASE("it is the policy's bound that refuses, not a fixed number", "[runtime][gate][record][bar]") {
  Fixture f;
  // The same noisy recording against a policy file whose bounds are lifted: it is recorded. This proves the
  // refusal comes from the policy's record.max_mdr_* members; the committed policy is never edited to do this.
  std::ifstream in(std::string(ATLANTIS_PERFORMANCE_DIR) + "/gate_policy.json", std::ios::binary);
  std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto replaceOnce = [&](const std::string& needle, const std::string& replacement) {
    const auto at = text.find(needle);
    REQUIRE(at != std::string::npos);
    text.replace(at, needle.size(), replacement);
  };
  replaceOnce("\"max_mdr_cpu_pct\": 10", "\"max_mdr_cpu_pct\": 1000");
  replaceOnce("\"max_mdr_gpu_pct\": 20", "\"max_mdr_gpu_pct\": 1000");
  const fs::path lifted = f.root / "lifted_policy.json";
  { std::ofstream out(lifted, std::ios::binary); out << text; }
  FakeLauncher launcher = noisyCpuLauncher();
  cli::GateRequest r = f.recordRequest(3);
  r.policyPath = lifted.string();
  const auto o = f.run(r, launcher);
  CHECK(o.exitCode == 0);
  CHECK(fs::exists(f.root / "out"));
}
