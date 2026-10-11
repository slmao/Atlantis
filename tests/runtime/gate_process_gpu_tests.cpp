#include "../../src/runtime/gate_orchestrate.h"

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

#endif
