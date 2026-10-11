#pragma once

// Spec 0060 / ADR-0119 D1 (Plan 0060 M4, P2, P10): the sampling protocol's orchestration --
// wait for an idle machine, one discarded preheat, then R independent child
// `--benchmark` runs, each reduced to its own `atlantis.benchmark/1` document.
//
// Everything that touches the OS or the clock sits behind three small interfaces (a
// clock, a CPU-time sampler, a child launcher), so the sequence is unit-tested with
// fakes and no process is started. The Windows implementations live in
// gate_process_windows.cpp, the only OS-specific file of the gate.
//
// Private to the atlantis_runtime executable's startup layer (see gate_set.h).

#include "cli.h"
#include "gate_json.h"
#include "gate_policy.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace atlantis::runtime::gate {

class Clock {
 public:
  virtual ~Clock() = default;
  [[nodiscard]] virtual double nowSeconds() = 0;
  virtual void sleepSeconds(double seconds) = 0;
  // The wall-clock time as `YYYY-MM-DDTHH:MM:SSZ`, for the provenance of a recorded baseline.
  [[nodiscard]] virtual std::string isoUtc() = 0;
};

// Cumulative system CPU times (any consistent unit); nullopt when the platform cannot say.
struct CpuTimes {
  std::uint64_t idle = 0;
  std::uint64_t total = 0;
};

class CpuSampler {
 public:
  virtual ~CpuSampler() = default;
  [[nodiscard]] virtual std::optional<CpuTimes> read() = 0;
};

enum class IdleResult {
  Idle,         // the machine stayed at or above the idle threshold for the whole window
  TimedOut,     // it did not within the timeout
  Unavailable,  // the sampler cannot read CPU times
};

// Samples once a second; the window must be unbroken: any interval under `cpuIdlePct`
// restarts it. Returns when a window completes or the timeout passes.
[[nodiscard]] IdleResult waitForIdle(const IdlePolicy& policy, CpuSampler& sampler, Clock& clock);

struct ChildSpec {
  std::vector<std::string> args;  // everything after the executable
  double timeoutSeconds = 300.0;
};

struct ChildResult {
  enum class Status { Exited, TimedOut, LaunchFailed };
  Status status = Status::Exited;
  int exitCode = 0;
  std::string message;  // why, for a failed launch or a timeout
};

// Starts one child process, discarding its stdin, stdout and stderr (the protocol: with
// stderr captured the per-frame log cost would be measured), waits for it up to the
// timeout, and kills it when the timeout passes or when this process exits first.
class ChildLauncher {
 public:
  virtual ~ChildLauncher() = default;
  [[nodiscard]] virtual ChildResult run(const ChildSpec& spec) = 0;
};

// What one session runs. The preheat and the runs differ only in the arguments.
struct RunPlan {
  std::string scene;
  atlantis::runtime::cli::BenchmarkVariant variant = atlantis::runtime::cli::BenchmarkVariant::Baseline;
  unsigned int width = 1920;
  unsigned int height = 1080;
  unsigned int runs = 5;
  double preheatSeconds = 90.0;  // 0: no preheat
  bool idleWait = true;
  double childTimeoutSeconds = 300.0;
  IdlePolicy idle;
  std::string tempDir;  // where each child writes its document; the caller creates it
};

struct SessionResult {
  // 0 on success; otherwise the exit code the gate returns with no verdict: 3 the machine
  // was not idle in time (or cannot be sampled), or a child reported an environment problem;
  // 2 a child failed, timed out or wrote no document; 1 a child could not be launched.
  int exitCode = 0;
  std::string message;
  std::vector<Value> runDocuments;  // atlantis.benchmark/1, one per measured run, in order
};

// Idle wait (if the plan asks), the discarded preheat (if its length is above zero), then the
// runs. Stops at the first failure.
[[nodiscard]] SessionResult runSession(const RunPlan& plan, ChildLauncher& launcher, CpuSampler& sampler,
                                       Clock& clock);

// The arguments of one child run (`--benchmark <scene> ...`), for the run at `outputPath`; with a
// warmup duration it is the preheat (a single measured frame, result discarded).
[[nodiscard]] std::vector<std::string> childArguments(const RunPlan& plan, const std::string& outputPath,
                                                      double warmupSeconds);

// The real implementations (Windows only; gate_process_windows.cpp).
[[nodiscard]] std::unique_ptr<Clock> makeSystemClock();
#if defined(_WIN32)
[[nodiscard]] std::unique_ptr<CpuSampler> makeSystemCpuSampler();
// Launches `executablePath` as the child; children die with this process (a job object).
[[nodiscard]] std::unique_ptr<ChildLauncher> makeProcessLauncher(std::string executablePath);
// The running executable's own path.
[[nodiscard]] std::string currentExecutablePath();
#endif

}  // namespace atlantis::runtime::gate
