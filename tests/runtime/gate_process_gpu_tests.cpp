#include "../../src/runtime/gate_orchestrate.h"
#include "../../src/runtime/gate_set.h"
#include "../../src/runtime/gate_store.h"

#include <atlantis/connection/json.h>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// Spec 0060 R1 / ADR-0119 D1 (Plan 0060 M4): the gate's real child-process launcher and CPU sampler,
// against the real executable. Each case is its own process (catch_discover_tests), so one window
// lifecycle each. Nothing here asserts a measured time.

#if defined(_WIN32) && defined(ATLANTIS_RUNTIME_EXECUTABLE)

namespace {

namespace gate = atlantis::runtime::gate;
namespace fs = std::filesystem;

struct TempDir {
  TempDir() {
    path = fs::temp_directory_path() / "atlantis_gate_process_test";
    fs::remove_all(path);
    fs::create_directories(path);
  }
  ~TempDir() { fs::remove_all(path); }
  fs::path path;
};

}  // namespace

TEST_CASE("the process launcher runs one real benchmark child and the document it writes is readable",
          "[runtime][gpu][gate][process]") {
  TempDir dir;
  const auto launcher = gate::makeProcessLauncher(ATLANTIS_RUNTIME_EXECUTABLE);
  const std::string output = (dir.path / "child.json").string();

  gate::ChildSpec spec;
  spec.args = {"--benchmark", "integrated_showcase_demo", "--warmup", "3", "--frames", "10", "--resolution", "1280x720",
               "--output",    output};
  spec.timeoutSeconds = 120.0;
  const gate::ChildResult result = launcher->run(spec);
  REQUIRE(result.status == gate::ChildResult::Status::Exited);
#if defined(ATLANTIS_PROFILING)
  CHECK(result.exitCode == 0);
  std::ifstream in(output, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto parsed = atlantis::connection::json::parse(text);
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().find("schema")->asString() == "atlantis.benchmark/1");
#else
  CHECK(result.exitCode == 3);  // a build without profiling refuses, with the documented code
#endif
}

TEST_CASE("a session over real children returns one document per run", "[runtime][gpu][gate][process]") {
#if !defined(ATLANTIS_PROFILING)
  SKIP("this build has no profiling: the benchmark children refuse");
#else
  TempDir dir;
  const auto launcher = gate::makeProcessLauncher(ATLANTIS_RUNTIME_EXECUTABLE);
  const auto sampler = gate::makeSystemCpuSampler();
  const auto clock = gate::makeSystemClock();
  gate::RunPlan plan;
  plan.scene = "integrated_showcase_demo";
  plan.width = 1280;
  plan.height = 720;
  plan.runs = 2;
  plan.preheatSeconds = 0.0;  // the smoke run: no preheat, no idle wait
  plan.idleWait = false;
  plan.childTimeoutSeconds = 120.0;
  plan.tempDir = dir.path.string();
  const gate::SessionResult result = gate::runSession(plan, *launcher, *sampler, *clock);
  REQUIRE(result.exitCode == 0);
  REQUIRE(result.runDocuments.size() == 2);
  for (const auto& doc : result.runDocuments) CHECK(doc.find("schema")->asString() == "atlantis.benchmark/1");
#endif
}

TEST_CASE("a child that outlives its timeout is killed and reported as timed out", "[runtime][gpu][gate][process]") {
#if !defined(ATLANTIS_PROFILING)
  SKIP("this build has no profiling: the benchmark child exits at once");
#else
  TempDir dir;
  const auto launcher = gate::makeProcessLauncher(ATLANTIS_RUNTIME_EXECUTABLE);
  gate::ChildSpec spec;
  // A run far longer than the timeout: 100,000 warmup frames.
  spec.args = {"--benchmark", "integrated_showcase_demo", "--warmup", "100000", "--frames", "10",
               "--output",    (dir.path / "never.json").string()};
  spec.timeoutSeconds = 3.0;
  const auto started = std::chrono::steady_clock::now();
  const gate::ChildResult result = launcher->run(spec);
  const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
  CHECK(result.status == gate::ChildResult::Status::TimedOut);
  CHECK(seconds < 60.0);  // it was killed, not waited for
  CHECK_FALSE(fs::exists(dir.path / "never.json"));
#endif
}

TEST_CASE("a launcher pointed at nothing reports a launch failure", "[runtime][gpu][gate][process]") {
  const auto launcher = gate::makeProcessLauncher((fs::temp_directory_path() / "no_such_executable.exe").string());
  gate::ChildSpec spec;
  spec.args = {"--help"};
  const gate::ChildResult result = launcher->run(spec);
  CHECK(result.status == gate::ChildResult::Status::LaunchFailed);
  CHECK_FALSE(result.message.empty());
}

TEST_CASE("the system CPU sampler reads increasing cumulative times", "[runtime][gpu][gate][process]") {
  const auto sampler = gate::makeSystemCpuSampler();
  const auto first = sampler->read();
  REQUIRE(first.has_value());
  CHECK(first->total > 0);
  CHECK(first->idle <= first->total);
  volatile unsigned long long sink = 0;
  for (unsigned long long i = 0; i < 20'000'000ULL; ++i) sink = sink + i;
  const auto second = sampler->read();
  REQUIRE(second.has_value());
  CHECK(second->total >= first->total);
  CHECK(second->idle >= first->idle);
}

TEST_CASE("the running executable knows its own path", "[runtime][gpu][gate][process]") {
  const std::string path = gate::currentExecutablePath();
  CHECK_FALSE(path.empty());
  CHECK(fs::exists(fs::path(path)));
}

// Plan 0060 M5: the real executable's `--gate`, end to end on the default scene. A baseline is made
// from two real children (not recorded: the recording flow is M6), then the gate runs against it.
// What is asserted is structure and exit-code membership, never a time.
namespace {

using atlantis::connection::json::Value;

[[nodiscard]] std::string readAll(const fs::path& path) {
  std::ifstream in(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

// cmd.exe strips one outer pair of quotes, so the whole command is quoted once more.
[[nodiscard]] int runExecutable(const std::string& args, const fs::path& stdoutFile) {
  const std::string command = "\"\"" + std::string(ATLANTIS_RUNTIME_EXECUTABLE) + "\" " + args + " > \"" +
                              stdoutFile.string() + "\" 2> NUL\"";
  return std::system(command.c_str());
}

// A store with one profile (named for `gpu` in its identity) and a baseline from two real runs.
void writeThrowawayStore(const fs::path& root, const std::string& profileGpu) {
  const auto launcher = gate::makeProcessLauncher(ATLANTIS_RUNTIME_EXECUTABLE);
  const auto sampler = gate::makeSystemCpuSampler();
  const auto clock = gate::makeSystemClock();
  gate::RunPlan plan;
  plan.scene = "integrated_showcase_demo";
  plan.width = 1280;
  plan.height = 720;
  plan.runs = 2;
  plan.preheatSeconds = 0.0;
  plan.idleWait = false;
  plan.childTimeoutSeconds = 120.0;
  plan.tempDir = (root / "children").string();
  fs::create_directories(plan.tempDir);
  const gate::SessionResult session = gate::runSession(plan, *launcher, *sampler, *clock);
  REQUIRE(session.exitCode == 0);
  gate::Protocol protocol;
  protocol.runs = 2;
  protocol.preheatSeconds = 0.0;
  protocol.idleWait = false;
  auto set = gate::buildSet(protocol, session.runDocuments);
  REQUIRE(set.isOk());
  Value doc = set.value();
  Value provenance = Value::object();
  provenance.set("profile", Value::string("smoke-profile"));
  provenance.set("recorded_commit", Value::string("smoke"));
  doc.set("baseline", std::move(provenance));

  Value identity = gate::identityOf(doc);
  if (!profileGpu.empty()) identity.set("gpu", Value::string(profileGpu));
  Value profile = Value::object();
  profile.set("schema", Value::string("atlantis.benchmark.profile/1"));
  profile.set("id", Value::string("smoke-profile"));
  profile.set("label", Value::string("smoke"));
  profile.set("identity", std::move(identity));
  const fs::path profileDir = root / "store" / "smoke-profile";
  fs::create_directories(profileDir / "integrated_showcase_demo");
  std::ofstream(profileDir / "profile.json", std::ios::binary) << atlantis::connection::json::write(profile) << "\n";
  std::ofstream(profileDir / "integrated_showcase_demo" / "integrated_showcase_demo_baseline_1280x720.baseline.json",
                std::ios::binary)
      << atlantis::connection::json::write(doc) << "\n";
}

}  // namespace

TEST_CASE("atlantis_runtime --gate: one document on stdout, an exit code that matches it, on the default scene",
          "[runtime][gpu][gate][process]") {
  TempDir dir;
#if !defined(ATLANTIS_PROFILING)
  // A build without profiling refuses with the documented environment code and prints no verdict.
  CHECK(runExecutable("--gate --json", dir.path / "out.txt") == 3);
  CHECK(readAll(dir.path / "out.txt").empty());
#else
  writeThrowawayStore(dir.path, "");
  const std::string common = "--gate --scene integrated_showcase_demo --resolution 1280x720 --runs 2 "
                             "--preheat-seconds 0 --idle-wait off --json --policy \"" +
                             std::string(ATLANTIS_PERFORMANCE_DIR) + "/gate_policy.json\" --baselines \"" +
                             (dir.path / "store").string() + "\"";
  const int code = runExecutable(common, dir.path / "verdict.txt");
  CHECK((code == 0 || code == 4 || code == 5));  // a verdict, never a crash and never a usage error
  const auto parsed = atlantis::connection::json::parse(readAll(dir.path / "verdict.txt"));
  REQUIRE(parsed.isOk());  // a log line on stdout would fail this
  const Value& verdict = parsed.value();
  CHECK(verdict.find("schema")->asString() == "atlantis.benchmark.gate/1");
  CHECK(*gate::numberOf(verdict.find("exit_code")) == static_cast<double>(code));
  CHECK_FALSE(gate::at(verdict, {"protocol", "conformant"})->asBool());  // a smoke run says so
#endif
}

TEST_CASE("atlantis_runtime --gate: a baseline for other hardware is not judged, with exit 5",
          "[runtime][gpu][gate][process]") {
#if !defined(ATLANTIS_PROFILING)
  SKIP("this build has no profiling: --gate refuses");
#else
  TempDir dir;
  writeThrowawayStore(dir.path, "A Different GPU");
  const std::string args = "--gate --scene integrated_showcase_demo --resolution 1280x720 --runs 2 "
                           "--preheat-seconds 0 --idle-wait off --json --policy \"" +
                           std::string(ATLANTIS_PERFORMANCE_DIR) + "/gate_policy.json\" --baselines \"" +
                           (dir.path / "store").string() + "\"";
  CHECK(runExecutable(args, dir.path / "verdict.txt") == 5);
  const auto parsed = atlantis::connection::json::parse(readAll(dir.path / "verdict.txt"));
  REQUIRE(parsed.isOk());
  CHECK(parsed.value().find("verdict")->asString() == "incomparable");
#endif
}

TEST_CASE("atlantis_runtime --gate: usage errors and an absent scene use the documented codes",
          "[runtime][gpu][gate][process]") {
  TempDir dir;
  CHECK(runExecutable("--gate --runs 0", dir.path / "a.txt") == 1);
  CHECK(runExecutable("--gate --benchmark integrated_showcase_demo", dir.path / "a.txt") == 1);
  CHECK(runExecutable("--gate --record", dir.path / "a.txt") == 1);  // no reason: refused
  CHECK(readAll(dir.path / "a.txt").empty());
#if defined(ATLANTIS_PROFILING)
  CHECK(runExecutable("--gate --scene no_such_scene --json", dir.path / "b.txt") == 3);
  CHECK(readAll(dir.path / "b.txt").empty());
#endif
}

#endif
