#include "../../src/runtime/gate_orchestrate.h"

#include "gate_test_fakes.h"
#include "gate_test_support.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

// Spec 0060 R1 / ADR-0119 D1 (Plan 0060 M4): the protocol's orchestration -- the idle wait, the
// discarded preheat, the R runs and what each failure maps to -- driven by a fake clock, a fake
// CPU sampler and a fake launcher. No process is started and nothing is timed.

namespace {

namespace gate = atlantis::runtime::gate;
namespace json = atlantis::connection::json;
namespace fs = std::filesystem;

using gate_test::FakeClock;
using gate_test::FakeLauncher;
using gate_test::FakeSampler;

gate::ChildResult exited(int code) {
  gate::ChildResult r;
  r.exitCode = code;
  return r;
}

struct TempDir {
  TempDir() {
    path = fs::temp_directory_path() / "atlantis_gate_orchestrate_test";
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
  fs::path path;
};

gate::RunPlan plan(const TempDir& dir) {
  gate::RunPlan p;
  p.scene = "bistro";
  p.runs = 3;
  p.preheatSeconds = 90.0;
  p.idleWait = true;
  p.idle = {90.0, 10.0, 120.0};
  p.childTimeoutSeconds = 300.0;
  p.tempDir = dir.path.string();
  return p;
}

bool hasArg(const gate::ChildSpec& spec, const std::string& arg) {
  return std::find(spec.args.begin(), spec.args.end(), arg) != spec.args.end();
}

std::string argAfter(const gate::ChildSpec& spec, const std::string& arg) {
  for (std::size_t i = 0; i + 1 < spec.args.size(); ++i) {
    if (spec.args[i] == arg) return spec.args[i + 1];
  }
  return {};
}

}  // namespace

TEST_CASE("an idle machine ends the wait after one unbroken window", "[runtime][gate][orchestrate]") {
  FakeClock clock;
  FakeSampler sampler({99.0});
  const auto result = gate::waitForIdle({90.0, 10.0, 120.0}, sampler, clock);
  CHECK(result == gate::IdleResult::Idle);
  CHECK(clock.slept == 10.0);
  CHECK(sampler.reads == 11);  // one reading to start, then one per second
}

TEST_CASE("a busy interval restarts the idle window", "[runtime][gate][orchestrate]") {
  FakeClock clock;
  // Intervals 1-5 idle, interval 6 busy (50 %), then idle for good: the window runs from the end of
  // interval 6, so the wait ends at second 6 + 10 = 16.
  FakeSampler sampler({99.0, 99.0, 99.0, 99.0, 99.0, 50.0, 99.0});
  CHECK(gate::waitForIdle({90.0, 10.0, 120.0}, sampler, clock) == gate::IdleResult::Idle);
  CHECK(clock.slept == 16.0);
}

TEST_CASE("a machine that never goes idle times out", "[runtime][gate][orchestrate]") {
  FakeClock clock;
  FakeSampler sampler({40.0});
  CHECK(gate::waitForIdle({90.0, 10.0, 30.0}, sampler, clock) == gate::IdleResult::TimedOut);
  CHECK(clock.slept >= 30.0);
  CHECK(clock.slept < 32.0);
}

TEST_CASE("a platform that cannot read CPU times is reported, not guessed", "[runtime][gate][orchestrate]") {
  FakeClock clock;
  FakeSampler sampler({99.0});
  sampler.unavailable = true;
  CHECK(gate::waitForIdle({90.0, 10.0, 120.0}, sampler, clock) == gate::IdleResult::Unavailable);
}

TEST_CASE("an interval with no elapsed CPU time does not break the window", "[runtime][gate][orchestrate]") {
  FakeClock clock;
  struct Flat final : gate::CpuSampler {
    std::optional<gate::CpuTimes> read() override { return gate::CpuTimes{100, 100}; }  // never advances
  } flat;
  CHECK(gate::waitForIdle({90.0, 3.0, 120.0}, flat, clock) == gate::IdleResult::Idle);
}

TEST_CASE("a session waits for idle, preheats once, then runs R children in order", "[runtime][gate][orchestrate]") {
  TempDir dir;
  FakeClock clock;
  FakeSampler sampler({99.0});
  FakeLauncher launcher;
  const auto result = gate::runSession(plan(dir), launcher, sampler, clock);
  REQUIRE(result.exitCode == 0);
  REQUIRE(launcher.calls.size() == 4);  // the preheat, then three runs
  CHECK(result.runDocuments.size() == 3);
  CHECK(clock.slept >= 10.0);  // the idle wait ran before the first child

  const gate::ChildSpec& preheat = launcher.calls[0];
  CHECK(hasArg(preheat, "--benchmark"));
  CHECK(hasArg(preheat, "--warmup-seconds"));
  CHECK(argAfter(preheat, "--warmup-seconds") == "90");
  CHECK(argAfter(preheat, "--frames") == "1");
  CHECK(preheat.timeoutSeconds == 390.0);  // the child timeout plus the preheat

  for (std::size_t i = 1; i < 4; ++i) {
    const gate::ChildSpec& run = launcher.calls[i];
    CHECK(argAfter(run, "--benchmark") == "bistro");
    CHECK(argAfter(run, "--variant") == "baseline");
    CHECK(argAfter(run, "--resolution") == "1920x1080");
    CHECK(argAfter(run, "--output").find("run_" + std::to_string(i) + ".json") != std::string::npos);
    CHECK_FALSE(hasArg(run, "--warmup-seconds"));
    CHECK_FALSE(hasArg(run, "--warmup"));  // the defaults of ADR-0117 apply
    CHECK_FALSE(hasArg(run, "--frames"));
    CHECK(run.timeoutSeconds == 300.0);
  }
  // The documents are the children's, in run order.
  CHECK(gate::numberOf(result.runDocuments[0].find("metrics")->find("cpu_active_ms")->find("p50")) == 13.0);
  CHECK(gate::numberOf(result.runDocuments[2].find("metrics")->find("cpu_active_ms")->find("p50")) == 15.0);
  // The temporary documents are gone.
  CHECK(fs::is_empty(dir.path));
}

TEST_CASE("--idle-wait off skips the machine check, and no preheat skips the preheat", "[runtime][gate][orchestrate]") {
  TempDir dir;
  FakeClock clock;
  FakeSampler sampler({10.0});  // a busy machine: would time out if it were asked
  FakeLauncher launcher;
  gate::RunPlan p = plan(dir);
  p.idleWait = false;
  p.preheatSeconds = 0.0;
  const auto result = gate::runSession(p, launcher, sampler, clock);
  REQUIRE(result.exitCode == 0);
  CHECK(sampler.reads == 0);
  CHECK(clock.sleeps == 0);
  CHECK(launcher.calls.size() == 3);  // runs only
  CHECK_FALSE(hasArg(launcher.calls[0], "--warmup-seconds"));
}

TEST_CASE("a machine that is not idle in time ends the session with exit 3 before any child starts",
          "[runtime][gate][orchestrate]") {
  TempDir dir;
  FakeClock clock;
  FakeSampler sampler({10.0});
  FakeLauncher launcher;
  const auto result = gate::runSession(plan(dir), launcher, sampler, clock);
  CHECK(result.exitCode == 3);
  CHECK(result.message.find("did not become idle") != std::string::npos);
  CHECK(launcher.calls.empty());
  FakeSampler broken({99.0});
  broken.unavailable = true;
  const auto unavailable = gate::runSession(plan(dir), launcher, broken, clock);
  CHECK(unavailable.exitCode == 3);
  CHECK(launcher.calls.empty());
}

TEST_CASE("each child failure maps to the exit code of its kind and stops the session", "[runtime][gate][orchestrate]") {
  const struct {
    std::size_t atCall;  // 0 is the preheat
    gate::ChildResult result;
    int expected;
  } cases[] = {
      {0, exited(3), 3},   // an environment problem stays one, even in the preheat
      {0, exited(2), 2},
      {0, exited(1), 1},   // usage or initialization failure
      {0, exited(139), 2},  // anything else is a runtime failure
      {2, exited(3), 3},
      {2, exited(2), 2},
      {1, {gate::ChildResult::Status::TimedOut, 0, "killed after 300 s"}, 2},
      {1, {gate::ChildResult::Status::LaunchFailed, 0, "no such file"}, 1},
  };
  for (const auto& c : cases) {
    INFO("call " << c.atCall << " expects " << c.expected);
    TempDir dir;
    FakeClock clock;
    FakeSampler sampler({99.0});
    FakeLauncher launcher;
    launcher.scripted[c.atCall] = c.result;
    const auto result = gate::runSession(plan(dir), launcher, sampler, clock);
    CHECK(result.exitCode == c.expected);
    CHECK_FALSE(result.message.empty());
    CHECK(launcher.calls.size() == c.atCall + 1);  // nothing after the failure ran
  }
}

TEST_CASE("a run that wrote no document, or one that is not JSON, fails the session with exit 2",
          "[runtime][gate][orchestrate]") {
  {
    TempDir dir;
    FakeClock clock;
    FakeSampler sampler({99.0});
    FakeLauncher launcher;
    launcher.writeDocuments = false;
    const auto result = gate::runSession(plan(dir), launcher, sampler, clock);
    CHECK(result.exitCode == 2);
    CHECK(result.message.find("wrote no valid document") != std::string::npos);
    CHECK(launcher.calls.size() == 2);  // the preheat, then the first run
  }
  {
    TempDir dir;
    FakeClock clock;
    FakeSampler sampler({99.0});
    FakeLauncher launcher;
    launcher.malformed = true;
    CHECK(gate::runSession(plan(dir), launcher, sampler, clock).exitCode == 2);
  }
}

TEST_CASE("the child arguments are the benchmark's own flags, in a fixed order", "[runtime][gate][orchestrate]") {
  TempDir dir;
  gate::RunPlan p = plan(dir);
  p.variant = atlantis::runtime::cli::BenchmarkVariant::NoPointLights;
  p.width = 1280;
  p.height = 720;
  const auto args = gate::childArguments(p, "out.json", 0.0);
  const std::vector<std::string> expected = {"--benchmark", "bistro",      "--variant", "no-point-lights",
                                             "--resolution", "1280x720", "--output",  "out.json"};
  CHECK(args == expected);
  const auto preheat = gate::childArguments(p, "out.json", 2.5);
  CHECK(preheat.size() == expected.size() + 4);
  CHECK(preheat[preheat.size() - 4] == "--warmup-seconds");
  CHECK(preheat[preheat.size() - 3] == "2.5");
  CHECK(preheat[preheat.size() - 2] == "--frames");
  CHECK(preheat[preheat.size() - 1] == "1");
}

TEST_CASE("the system clock moves forward and can sleep", "[runtime][gate][orchestrate]") {
  const auto clock = gate::makeSystemClock();
  const double before = clock->nowSeconds();
  clock->sleepSeconds(0.01);
  CHECK(clock->nowSeconds() >= before);
}
