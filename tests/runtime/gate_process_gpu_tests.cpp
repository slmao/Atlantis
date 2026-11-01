#include "../../src/runtime/gate_canary.h"
#include "../../src/runtime/gate_orchestrate.h"
#include "../../src/runtime/gate_set.h"
#include "../../src/runtime/gate_store.h"

#include <atlantis/connection/json.h>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>

// Spec 0060 R1 / ADR-0119 D1 (Plan 0060 M4): the gate's real child-process launcher and CPU sampler,
// against the real executable. Each case is its own process (catch_discover_tests), so one window
// lifecycle each. Nothing here asserts a measured time.

#if defined(_WIN32) && defined(ATLANTIS_RUNTIME_EXECUTABLE)

namespace {

namespace gate = atlantis::runtime::gate;
namespace fs = std::filesystem;

struct PinningOsIdentity {
  bool known = false;
  DWORD platform = 0;
  DWORD major = 0;
  DWORD minor = 0;
  DWORD build = 0;
  BYTE product = 0;
};

[[nodiscard]] PinningOsIdentity pinningOsIdentity() {
  const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll == nullptr) return {};
  using ReadVersion = LONG(WINAPI*)(OSVERSIONINFOW*);
  const auto readVersion = std::bit_cast<ReadVersion>(GetProcAddress(ntdll, "RtlGetVersion"));
  if (readVersion == nullptr) return {};
  OSVERSIONINFOEXW version{};
  version.dwOSVersionInfoSize = sizeof(version);
  // The export takes the base pointer; the size selects its extended record.
  if (readVersion(reinterpret_cast<OSVERSIONINFOW*>(&version)) != 0) return {};
  return {true, version.dwPlatformId, version.dwMajorVersion, version.dwMinorVersion,
          version.dwBuildNumber, version.wProductType};
}

struct PowerReadbackFacts {
  DWORD requestVersion;
  std::size_t requestSize;
  bool queried;
  DWORD error;
  DWORD controlMask;
  DWORD stateMask;
};

enum class PowerReadbackResult { Observed, Unavailable, Failure };

// Spec 0077 R3: only the known legacy readback is optional, never pinning setup.
[[nodiscard]] constexpr PowerReadbackResult classifyPowerReadback(const PinningOsIdentity& os,
                                                                const PowerReadbackFacts& facts) {
  if (facts.requestVersion != PROCESS_POWER_THROTTLING_CURRENT_VERSION ||
      facts.requestSize != sizeof(PROCESS_POWER_THROTTLING_STATE)) return PowerReadbackResult::Failure;
  if (facts.queried) {
    const DWORD speed = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    return (facts.controlMask & speed) != 0 && (facts.stateMask & speed) == 0
               ? PowerReadbackResult::Observed
               : PowerReadbackResult::Failure;
  }
  const bool legacy = os.known && os.platform == VER_PLATFORM_WIN32_NT && os.product == VER_NT_WORKSTATION &&
                      os.major == 10 && os.minor == 0 && os.build >= 10240 && os.build <= 22620;
  return legacy && facts.error == ERROR_INVALID_PARAMETER ? PowerReadbackResult::Unavailable
                                                        : PowerReadbackResult::Failure;
}

struct TempDir {
  TempDir() {
    static const std::string suffix = [] {
      std::random_device device;
      char text[32];
      std::snprintf(text, sizeof(text), "%08x%08x", device(), device());
      return std::string(text);
    }();
    path = fs::temp_directory_path() / ("atlantis_gate_process_test_" + suffix);
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
  SUCCEED("this build has no profiling: the benchmark children refuse");
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
  SUCCEED("this build has no profiling: the benchmark child exits at once");
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

TEST_CASE("pinning the process opts out of power throttling and raises the priority class",
          "[runtime][gpu][gate][process]") {
  std::string error;
  REQUIRE(gate::pinCurrentProcess(error));
  CHECK(error.empty());
  CHECK(GetPriorityClass(GetCurrentProcess()) == HIGH_PRIORITY_CLASS);
  // The affinity is the one logical processor pinnedLogicalProcessor() chooses on this machine (Spec 0060 eighth
  // Correction), inside the system's mask.
  DWORD_PTR processMask = 0;
  DWORD_PTR systemMask = 0;
  REQUIRE(GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask));
  std::string coresError;
  const auto processor = gate::pinnedLogicalProcessor(gate::systemCpuCores(coresError));
  CHECK(coresError.empty());
  REQUIRE(processor.has_value());
  REQUIRE(*processor < sizeof(DWORD_PTR) * 8);
  CHECK(processMask == (DWORD_PTR{1} << *processor));
  CHECK((processMask & systemMask) == processMask);

  const PinningOsIdentity os = pinningOsIdentity();
  PROCESS_POWER_THROTTLING_STATE state{};
  state.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  const DWORD requestVersion = state.Version;
  const BOOL queried = GetProcessInformation(GetCurrentProcess(), ProcessPowerThrottling, &state, sizeof(state));
  const DWORD queryError = queried ? ERROR_SUCCESS : GetLastError();
  const PowerReadbackResult result = classifyPowerReadback(
      os, {requestVersion, sizeof(state), queried != FALSE, queryError, state.ControlMask, state.StateMask});
  const char* status = result == PowerReadbackResult::Observed ? "observed" :
                       result == PowerReadbackResult::Unavailable ? "unavailable" : "failure";
  std::printf("pinning readback=%s; known=%u; platform=%lu; os=%lu.%lu.%lu; product=%u; requestVersion=%lu; "
              "requestSize=%zu; query=%u; error=%lu; selected=%u; affinity=%llu; systemAffinity=%llu; ",
              status, static_cast<unsigned>(os.known), static_cast<unsigned long>(os.platform),
              static_cast<unsigned long>(os.major), static_cast<unsigned long>(os.minor),
              static_cast<unsigned long>(os.build), static_cast<unsigned>(os.product),
              static_cast<unsigned long>(requestVersion), sizeof(state), static_cast<unsigned>(queried != FALSE),
              static_cast<unsigned long>(queryError), *processor, static_cast<unsigned long long>(processMask),
              static_cast<unsigned long long>(systemMask));
  if (queried) {
    std::printf("control=%lu; state=%lu\n", static_cast<unsigned long>(state.ControlMask),
                static_cast<unsigned long>(state.StateMask));
  } else {
    std::printf("control/state=unobserved\n");
  }
  CHECK(os.known);  // an unknown identity cannot establish real-host acceptance
  CHECK(os.platform == VER_PLATFORM_WIN32_NT);
  CHECK(os.product == VER_NT_WORKSTATION);
  CHECK(result != PowerReadbackResult::Failure);
}

TEST_CASE("pinning readback classification admits only the reviewed legacy capability",
          "[runtime][gate][process][pinning_classification]") {
  // Injected values only; supported/early-Windows-11 cases are not real-host evidence.
  const PinningOsIdentity legacy{true, VER_PLATFORM_WIN32_NT, 10, 0, 19045, VER_NT_WORKSTATION};
  const PinningOsIdentity supported{true, VER_PLATFORM_WIN32_NT, 10, 0, 22621, VER_NT_WORKSTATION};
  constexpr DWORD current = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
  constexpr DWORD speed = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
  constexpr std::size_t size = sizeof(PROCESS_POWER_THROTTLING_STATE);
  const PowerReadbackFacts observed{current, size, true, ERROR_SUCCESS, speed, 0};
  const PowerReadbackFacts error87{current, size, false, ERROR_INVALID_PARAMETER, 0, 0};
  using Result = PowerReadbackResult;
  struct Sample {
    const char* name;
    PinningOsIdentity os;
    PowerReadbackFacts query;
    Result expected;
  };
  const Sample samples[]{
      {"supported correct bits", supported, observed, Result::Observed},
      {"legacy correct bits still verified", legacy, observed, Result::Observed},
      {"unknown identity successful query", {}, observed, Result::Observed},
      {"future identity successful query", {true, VER_PLATFORM_WIN32_NT, 11, 0, 1, VER_NT_WORKSTATION}, observed, Result::Observed},
      {"current Windows 10 legacy exception", legacy, error87, Result::Unavailable},
      {"early Windows 11 synthetic only", {true, VER_PLATFORM_WIN32_NT, 10, 0, 22000, VER_NT_WORKSTATION}, error87, Result::Unavailable},
      {"lower included boundary", {true, VER_PLATFORM_WIN32_NT, 10, 0, 10240, VER_NT_WORKSTATION}, error87, Result::Unavailable},
      {"upper included boundary", {true, VER_PLATFORM_WIN32_NT, 10, 0, 22620, VER_NT_WORKSTATION}, error87, Result::Unavailable},
      {"below legacy boundary", {true, VER_PLATFORM_WIN32_NT, 10, 0, 10239, VER_NT_WORKSTATION}, error87, Result::Failure},
      {"supported boundary failure", supported, error87, Result::Failure},
      {"later supported failure", {true, VER_PLATFORM_WIN32_NT, 10, 0, 26100, VER_NT_WORKSTATION}, error87, Result::Failure},
      {"unknown identity failure", {}, error87, Result::Failure},
      {"failed identity probe", {false, VER_PLATFORM_WIN32_NT, 10, 0, 19045, VER_NT_WORKSTATION}, error87, Result::Failure},
      {"non NT platform", {true, VER_PLATFORM_WIN32_WINDOWS, 10, 0, 19045, VER_NT_WORKSTATION}, error87, Result::Failure},
      {"server product", {true, VER_PLATFORM_WIN32_NT, 10, 0, 19045, VER_NT_SERVER}, error87, Result::Failure},
      {"unknown product", {true, VER_PLATFORM_WIN32_NT, 10, 0, 19045, 0}, error87, Result::Failure},
      {"other major version", {true, VER_PLATFORM_WIN32_NT, 11, 0, 19045, VER_NT_WORKSTATION}, error87, Result::Failure},
      {"other minor version", {true, VER_PLATFORM_WIN32_NT, 10, 1, 19045, VER_NT_WORKSTATION}, error87, Result::Failure},
      {"access denied", legacy, {current, size, false, ERROR_ACCESS_DENIED, 0, 0}, Result::Failure},
      {"not supported error", legacy, {current, size, false, ERROR_NOT_SUPPORTED, 0, 0}, Result::Failure},
      {"failed query with zero error", legacy, {current, size, false, ERROR_SUCCESS, 0, 0}, Result::Failure},
      {"failed query masks do not establish observed", supported, {current, size, false, ERROR_INVALID_PARAMETER, speed, 0}, Result::Failure},
      {"legacy failure masks stay unobserved", legacy, {current, size, false, ERROR_INVALID_PARAMETER, speed, 0}, Result::Unavailable},
      {"missing control bit", supported, {current, size, true, 0, 0, 0}, Result::Failure},
      {"legacy throttling enabled", legacy, {current, size, true, 0, speed, speed}, Result::Failure},
      {"unrelated bits are allowed", supported, {current, size, true, 0, speed | 2u, 2u}, Result::Observed},
      {"bad request version on success", supported, {0, size, true, 0, speed, 0}, Result::Failure},
      {"bad request size on success", supported, {current, 0, true, 0, speed, 0}, Result::Failure},
      {"bad request version on failure", legacy, {0, size, false, ERROR_INVALID_PARAMETER, 0, 0}, Result::Failure},
      {"undersized request on failure", legacy, {current, size - 1, false, ERROR_INVALID_PARAMETER, 0, 0}, Result::Failure},
      {"oversized request on failure", legacy, {current, size + 1, false, ERROR_INVALID_PARAMETER, 0, 0}, Result::Failure},
  };
  for (const Sample& sample : samples) {
    DYNAMIC_SECTION(sample.name) { CHECK(classifyPowerReadback(sample.os, sample.query) == sample.expected); }
  }
}

TEST_CASE("a pinned benchmark child runs and writes its document", "[runtime][gpu][gate][process]") {
#if !defined(ATLANTIS_PROFILING)
  SUCCEED("this build has no profiling: the benchmark children refuse");
#else
  TempDir dir;
  const auto launcher = gate::makeProcessLauncher(ATLANTIS_RUNTIME_EXECUTABLE);
  gate::ChildSpec spec;
  spec.args = {"--benchmark", "integrated_showcase_demo", "--warmup", "3", "--frames", "10", "--resolution", "1280x720",
               "--pin-process", "on", "--output", (dir.path / "pinned.json").string()};
  spec.timeoutSeconds = 120.0;
  const gate::ChildResult result = launcher->run(spec);
  REQUIRE(result.status == gate::ChildResult::Status::Exited);
  CHECK(result.exitCode == 0);  // pinning succeeded on this machine, or the child would have exited 3
  CHECK(fs::exists(dir.path / "pinned.json"));
#endif
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
  plan.pin = true;  // the real children run pinned, as the policy asks
  plan.canary = true;  // and the real GPU canary is measured around the runs
  plan.childTimeoutSeconds = 120.0;
  plan.tempDir = (root / "children").string();
  fs::create_directories(plan.tempDir);
  const gate::SessionResult session = gate::runSession(plan, *launcher, *sampler, *clock);
  REQUIRE(session.exitCode == 0);
  gate::Protocol protocol;
  protocol.runs = 2;
  protocol.preheatSeconds = 0.0;
  protocol.idleWait = false;
  protocol.pin = true;
  protocol.cores = gate::singleProcessorCores(2);
  protocol.power = "mains";
  auto set = gate::buildSet(protocol, session.runDocuments);
  REQUIRE(set.isOk());
  Value doc = set.value();
  std::vector<std::pair<std::string, gate::CanaryReading>> readings;
  for (const auto& [phase, document] : session.canaryDocuments) {
    const auto reading = gate::extractCanaryReading(document);
    REQUIRE(reading.isOk());
    readings.emplace_back(phase, reading.value());
  }
  doc.set("canary", gate::canarySetValue(readings));
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
                             "--preheat-seconds 0 --idle-wait off --require-mains off --json --policy \"" +
                             std::string(ATLANTIS_PERFORMANCE_DIR) + "/gate_policy.json\" --baselines \"" +
                             (dir.path / "store").string() + "\"";
  const int code = runExecutable(common, dir.path / "verdict.txt");
  if (code == 3) {
    // The GPU canary already ran slower than the baseline's reference at the start of the session -- this test runs
    // beside other GPU processes, which is exactly what the start-of-session precondition exists to refuse. It
    // prints no verdict, and the other cases below are not reachable.
    CHECK(readAll(dir.path / "verdict.txt").empty());
    return;
  }
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
  SUCCEED("this build has no profiling: --gate refuses");
#else
  TempDir dir;
  writeThrowawayStore(dir.path, "A Different GPU");
  const std::string args = "--gate --scene integrated_showcase_demo --resolution 1280x720 --runs 2 "
                           "--preheat-seconds 0 --idle-wait off --require-mains off --json --policy \"" +
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

// Owner ruling after the M7 study: the real executable's `--gate` runs on mains power only. The machine
// running this test decides which half of the contract it can see, so it asks the same probe the gate does.
TEST_CASE("atlantis_runtime --gate: runs on mains power and refuses on battery, as this machine's power says",
          "[runtime][gpu][gate][process][power]") {
#if !defined(ATLANTIS_PROFILING)
  SUCCEED("this build has no profiling: --gate refuses before it looks at the power");
#elif !defined(_WIN32)
  SUCCEED("the gate is Windows only");
#else
  TempDir dir;
  writeThrowawayStore(dir.path, "A Different GPU");
  const std::string args = "--gate --scene integrated_showcase_demo --resolution 1280x720 --runs 1 "
                           "--preheat-seconds 0 --idle-wait off --json --policy \"" +
                           std::string(ATLANTIS_PERFORMANCE_DIR) + "/gate_policy.json\" --baselines \"" +
                           (dir.path / "store").string() + "\"";
  const auto source = gate::makeSystemPowerProbe()->read();
  const int code = runExecutable(args, dir.path / "verdict.txt");
  if (source == gate::PowerSource::Mains) {
    CHECK((code == 0 || code == 4 || code == 5));  // a verdict
    const auto parsed = atlantis::connection::json::parse(readAll(dir.path / "verdict.txt"));
    REQUIRE(parsed.isOk());
    CHECK(gate::at(parsed.value(), {"protocol", "power"})->asString() == "mains");
  } else {
    CHECK(code == 3);  // refused: nothing measured, nothing printed
    CHECK(readAll(dir.path / "verdict.txt").empty());
  }
#endif
}

// Spec 0060 Correction 2026-10-15 (GPU-state canary): the real child, on this machine's device.

TEST_CASE("atlantis_runtime --canary: a real run writes a valid document with a reading for every component",
          "[runtime][gpu][gate][process][canary]") {
  TempDir dir;
  const auto launcher = gate::makeProcessLauncher(ATLANTIS_RUNTIME_EXECUTABLE);
  gate::ChildSpec spec;
  const std::string output = (dir.path / "canary.json").string();
  spec.args = {"--canary", "--output", output, "--pin-process", "on"};
  spec.timeoutSeconds = 120.0;
  const gate::ChildResult result = launcher->run(spec);
  REQUIRE(result.status == gate::ChildResult::Status::Exited);
#if defined(ATLANTIS_PROFILING)
  REQUIRE(result.exitCode == 0);
  std::ifstream in(output, std::ios::binary);
  const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  const auto parsed = atlantis::connection::json::parse(text);
  REQUIRE(parsed.isOk());
  const auto reading = gate::extractCanaryReading(parsed.value());
  REQUIRE(reading.isOk());
  for (const double ms : reading.value().ms) {
    CHECK(ms > 0.0);  // a reading, not a time budget: nothing here asserts how fast the machine is
    CHECK(std::isfinite(ms));
  }
  CHECK(*gate::numberOf(gate::at(parsed.value(), {"components", "alu", "n"})) == static_cast<double>(gate::kCanaryRepetitions));
  CHECK(gate::at(parsed.value(), {"meta", "system", "gpu"})->isString());  // the machine identity the start check matches on
  CHECK(gate::at(parsed.value(), {"meta", "build", "profiling"})->asBool());
#else
  CHECK(result.exitCode == 3);  // a build without profiling refuses, with the documented code
  CHECK_FALSE(fs::exists(output));
#endif
}

TEST_CASE("atlantis_runtime --canary: the document goes to stdout alone without --output",
          "[runtime][gpu][gate][process][canary]") {
#if !defined(ATLANTIS_PROFILING)
  SUCCEED("this build has no profiling: --canary refuses");
#else
  TempDir dir;
  CHECK(runExecutable("--canary", dir.path / "stdout.txt") == 0);
  const auto parsed = atlantis::connection::json::parse(readAll(dir.path / "stdout.txt"));
  REQUIRE(parsed.isOk());  // a log line on stdout would fail this
  CHECK(parsed.value().find("schema")->asString() == "atlantis.benchmark.canary/1");
#endif
}
