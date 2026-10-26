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
    path = gate_test::uniqueTempDir("atlantis_gate_orchestrate_test");
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
  p.pin = true;
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
    CHECK(argAfter(run, "--pin-process") == "on");  // the children run pinned
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

TEST_CASE("turning the idle wait off skips the machine check, and no preheat skips the preheat", "[runtime][gate][orchestrate]") {
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
  p.pin = false;
  p.width = 1280;
  p.height = 720;
  const auto args = gate::childArguments(p, "out.json", 0.0);
  const std::vector<std::string> expected = {"--benchmark", "bistro",      "--variant", "no-point-lights",
                                             "--resolution", "1280x720", "--output",  "out.json"};
  CHECK(args == expected);
  p.pin = false;  // an unpinned plan asks nothing of its children
  CHECK(gate::childArguments(p, "out.json", 0.0) == expected);
  p.pin = true;
  const auto pinned = gate::childArguments(p, "out.json", 0.0);
  CHECK(pinned.size() == expected.size() + 2);
  CHECK(pinned[pinned.size() - 2] == "--pin-process");
  CHECK(pinned[pinned.size() - 1] == "on");
  p.pin = false;
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

// Spec 0060 Correction 2026-10-15 (GPU-state canary): the canary children around a session.

TEST_CASE("a session with a canary measures it after the idle wait and again after the runs",
          "[runtime][gate][orchestrate][canary]") {
  TempDir dir;
  FakeClock clock;
  FakeSampler sampler({99.0});
  FakeLauncher launcher;
  gate::RunPlan p = plan(dir);
  p.canary = true;
  const auto result = gate::runSession(p, launcher, sampler, clock);
  REQUIRE(result.exitCode == 0);
  REQUIRE(launcher.calls.size() == 6);  // canary, preheat, three runs, canary
  CHECK(launcher.calls[0].args[0] == "--canary");
  CHECK(launcher.calls[1].args[0] == "--benchmark");
  CHECK(launcher.calls[5].args[0] == "--canary");
  CHECK(argAfter(launcher.calls[0], "--output").find("canary_start.json") != std::string::npos);
  CHECK(argAfter(launcher.calls[5], "--output").find("canary_end.json") != std::string::npos);
  CHECK(argAfter(launcher.calls[0], "--pin-process") == "on");
  CHECK(launcher.calls[0].timeoutSeconds == 300.0);
  REQUIRE(result.canaryDocuments.size() == 2);
  CHECK(result.canaryDocuments[0].first == "start");
  CHECK(result.canaryDocuments[1].first == "end");
  CHECK(result.canaryDocuments[0].second.find("schema")->asString() == "atlantis.benchmark.canary/1");
  CHECK(result.runDocuments.size() == 3);
  CHECK(fs::is_empty(dir.path));
  CHECK(clock.slept >= 10.0);  // the idle wait ran before the first child, the canary included
}

TEST_CASE("a plan without a canary launches none", "[runtime][gate][orchestrate][canary]") {
  TempDir dir;
  FakeClock clock;
  FakeSampler sampler({99.0});
  FakeLauncher launcher;
  const auto result = gate::runSession(plan(dir), launcher, sampler, clock);
  REQUIRE(result.exitCode == 0);
  CHECK(launcher.canaryCallCount() == 0);
  CHECK(result.canaryDocuments.empty());
}

TEST_CASE("the start check can refuse a session before the preheat and the runs are spent",
          "[runtime][gate][orchestrate][canary]") {
  TempDir dir;
  FakeClock clock;
  FakeSampler sampler({99.0});
  FakeLauncher launcher;
  gate::RunPlan p = plan(dir);
  p.canary = true;
  json::Value seen;
  p.startCheck = [&](const json::Value& document) -> std::optional<std::string> {
    seen = document;
    return std::string("the machine is not in the baseline's GPU state");
  };
  const auto result = gate::runSession(p, launcher, sampler, clock);
  CHECK(result.exitCode == 3);
  CHECK(result.message == "the machine is not in the baseline's GPU state");
  CHECK(launcher.calls.size() == 1);  // the start canary only
  CHECK(seen.find("schema") != nullptr);  // the check saw the canary document
  CHECK(fs::is_empty(dir.path));

  // A check that finds nothing wrong lets the session go on.
  TempDir dir2;
  FakeLauncher launcher2;
  gate::RunPlan q = plan(dir2);
  q.canary = true;
  q.startCheck = [](const json::Value&) -> std::optional<std::string> { return std::nullopt; };
  CHECK(gate::runSession(q, launcher2, sampler, clock).exitCode == 0);
  CHECK(launcher2.calls.size() == 6);
}

TEST_CASE("a canary child that fails ends the session with its failure, a document it did not write with exit 2",
          "[runtime][gate][orchestrate][canary]") {
  const struct {
    std::size_t canaryCall;
    gate::ChildResult::Status status;
    int exitCode;
    int expected;
    std::size_t calls;
  } cases[] = {{0, gate::ChildResult::Status::Exited, 3, 3, 1},       {0, gate::ChildResult::Status::Exited, 2, 2, 1},
               {0, gate::ChildResult::Status::TimedOut, 0, 2, 1},     {0, gate::ChildResult::Status::LaunchFailed, 0, 1, 1},
               {1, gate::ChildResult::Status::Exited, 3, 3, 6},       {1, gate::ChildResult::Status::Exited, 2, 2, 6}};
  for (const auto& c : cases) {
    TempDir dir;
    FakeClock clock;
    FakeSampler sampler({99.0});
    FakeLauncher launcher;
    gate::ChildResult failure;
    failure.status = c.status;
    failure.exitCode = c.exitCode;
    launcher.scriptedCanary[c.canaryCall] = failure;
    gate::RunPlan p = plan(dir);
    p.canary = true;
    const auto result = gate::runSession(p, launcher, sampler, clock);
    INFO("canary call " << c.canaryCall << " exit " << c.exitCode);
    CHECK(result.exitCode == c.expected);
    CHECK(launcher.calls.size() == c.calls);
    CHECK(result.message.find("GPU canary") != std::string::npos);
  }
  TempDir dir;
  FakeClock clock;
  FakeSampler sampler({99.0});
  FakeLauncher launcher;
  launcher.malformed = true;
  gate::RunPlan p = plan(dir);
  p.canary = true;
  const auto result = gate::runSession(p, launcher, sampler, clock);
  CHECK(result.exitCode == 2);
  CHECK(result.message.find("canary process wrote no valid document") != std::string::npos);
}

TEST_CASE("the canary child's arguments are --canary, its output, and the pin when the plan pins",
          "[runtime][gate][orchestrate][canary]") {
  TempDir dir;
  gate::RunPlan p = plan(dir);
  auto args = gate::canaryArguments(p, "out.json");
  REQUIRE(args.size() == 5);
  CHECK(args[0] == "--canary");
  CHECK(args[1] == "--output");
  CHECK(args[2] == "out.json");
  CHECK(args[3] == "--pin-process");
  CHECK(args[4] == "on");
  p.pin = false;
  CHECK(gate::canaryArguments(p, "out.json").size() == 3);
}

TEST_CASE("the end canary waits for the GPU to settle out of the benchmark children's tail; the start canary does not",
          "[runtime][gate][orchestrate][canary]") {
  TempDir dir;
  FakeClock clock;
  FakeSampler sampler({99.0});
  FakeLauncher launcher;
  std::vector<double> sleptAtLaunch;
  std::vector<std::string> kind;
  launcher.onLaunch = [&](const gate::ChildSpec& spec) {
    sleptAtLaunch.push_back(clock.slept);
    kind.push_back(spec.args[0]);
  };
  gate::RunPlan p = plan(dir);
  p.canary = true;
  p.canarySettleSeconds = 3.0;
  REQUIRE(gate::runSession(p, launcher, sampler, clock).exitCode == 0);
  REQUIRE(kind.size() == 6);  // canary, preheat, three runs, canary
  CHECK(kind.front() == "--canary");
  CHECK(kind.back() == "--canary");
  CHECK(sleptAtLaunch[5] - sleptAtLaunch[4] == 3.0);  // between the last run and the end canary
  CHECK(sleptAtLaunch[1] - sleptAtLaunch[0] == 0.0);  // none between the start canary and the preheat
  for (std::size_t i = 2; i <= 4; ++i) CHECK(sleptAtLaunch[i] == sleptAtLaunch[i - 1]);  // none between the runs

  // The default plan settles for no time at all, and a plan without a canary never sleeps for one.
  TempDir dir2;
  FakeClock clock2;
  FakeLauncher launcher2;
  gate::RunPlan q = plan(dir2);
  q.canary = true;
  REQUIRE(gate::runSession(q, launcher2, sampler, clock2).exitCode == 0);
  TempDir dir3;
  FakeClock clock3;
  FakeLauncher launcher3;
  gate::RunPlan r = plan(dir3);
  r.canarySettleSeconds = 3.0;  // set, but no canary to wait for
  REQUIRE(gate::runSession(r, launcher3, sampler, clock3).exitCode == 0);
  CHECK(clock.slept - clock2.slept == 3.0);
  CHECK(clock3.slept == clock2.slept);
}

// Spec 0060 eighth Correction: a pinned child is restricted to one fixed logical processor.

TEST_CASE("the pinned processor is the highest-numbered one, on a CPU of one class", "[runtime][gate][orchestrate]") {
  using atlantis::runtime::gate::CpuCore;
  // The reference desktop's Ryzen 7 9700X as Windows reports it there (SMT off): 8 processors of one class.
  std::vector<CpuCore> eight;
  for (unsigned i = 0; i < 8; ++i) eight.push_back({0, i, 0});
  CHECK(atlantis::runtime::gate::pinnedLogicalProcessor(eight) == 7u);
  // With SMT on, 16: the last thread of the last core, as far from processor 0 as it can be.
  std::vector<CpuCore> sixteen;
  for (unsigned i = 0; i < 16; ++i) sixteen.push_back({0, i, 0});
  CHECK(atlantis::runtime::gate::pinnedLogicalProcessor(sixteen) == 15u);
  // The order of the list does not matter.
  std::vector<CpuCore> reversed(eight.rbegin(), eight.rend());
  CHECK(atlantis::runtime::gate::pinnedLogicalProcessor(reversed) == 7u);
}

TEST_CASE("the pinned processor is of the highest efficiency class", "[runtime][gate][orchestrate]") {
  using atlantis::runtime::gate::CpuCore;
  // A 12-core hybrid layout: 4 P (class 2), 4 E (class 1), 4 LP-E (class 0) -- the archived Intel profile's machine.
  std::vector<CpuCore> hybrid;
  for (unsigned i = 0; i < 4; ++i) hybrid.push_back({0, i, 2});
  for (unsigned i = 4; i < 8; ++i) hybrid.push_back({0, i, 1});
  for (unsigned i = 8; i < 12; ++i) hybrid.push_back({0, i, 0});
  CHECK(atlantis::runtime::gate::pinnedLogicalProcessor(hybrid) == 3u);  // the last P core, not the last processor
  std::vector<CpuCore> topLater = {{0, 0, 0}, {0, 1, 0}, {0, 2, 1}, {0, 3, 1}};
  CHECK(atlantis::runtime::gate::pinnedLogicalProcessor(topLater) == 3u);
  // Only processor 0 of the top class: it is the one.
  CHECK(atlantis::runtime::gate::pinnedLogicalProcessor(std::vector<CpuCore>{{0, 0, 1}, {0, 1, 0}}) == 0u);
  CHECK(atlantis::runtime::gate::pinnedLogicalProcessor(std::vector<CpuCore>{{0, 0, 0}}) == 0u);
}

TEST_CASE("the pinned processor is in group 0, or there is none", "[runtime][gate][orchestrate]") {
  using atlantis::runtime::gate::CpuCore;
  CHECK_FALSE(atlantis::runtime::gate::pinnedLogicalProcessor({}).has_value());
  CHECK_FALSE(atlantis::runtime::gate::pinnedLogicalProcessor(std::vector<CpuCore>{{1, 0, 1}, {1, 1, 1}}).has_value());
  // Group 1's higher class does not count: the choice is among group 0's processors.
  std::vector<CpuCore> grouped = {{0, 0, 0}, {0, 1, 0}, {1, 0, 1}, {1, 1, 1}};
  CHECK(atlantis::runtime::gate::pinnedLogicalProcessor(grouped) == 1u);
}
