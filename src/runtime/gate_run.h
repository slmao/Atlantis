#pragma once

// Spec 0060 / ADR-0119 D5 (Plan 0060 M3-M6): `atlantis_runtime --gate`, `--gate --record` and
// `--gate --applies`. The gate runs no frames itself: it launches child `--benchmark` runs,
// reduces them to a set document, and judges it against the committed baseline. Private to
// the atlantis_runtime executable's startup layer, like benchmark_run.h.

#include "cli.h"
#include "gate_orchestrate.h"

#include <iosfwd>
#include <string>

namespace atlantis::runtime::gate {

// Everything a gate run touches outside its own logic, so a test can drive it with fakes:
// the three OS-facing interfaces, the temporary directory the children write to, where the
// policy and the baseline store live by default, and the two output streams.
struct GateEnvironment {
  ChildLauncher* launcher = nullptr;
  CpuSampler* sampler = nullptr;
  Clock* clock = nullptr;
  PowerProbe* power = nullptr;  // where the machine's power comes from
  // The logical processor a pinned child is restricted to, as recorded (singleProcessorCores()); empty when
  // this machine offers none, and a pinned run then refuses (exit 3).
  std::string pinnedCores;
  std::string tempDir;         // created and removed by the run
  std::string performanceDir;  // holds gate_policy.json and baselines/ unless the request names others
  std::istream* in = nullptr;   // stdin: the changed paths for --applies
  std::ostream* out = nullptr;  // stdout: the verdict document, and only that
  std::ostream* err = nullptr;  // stderr: progress and problems
};

// Runs the requested mode and returns the process exit code: 0 pass, 1 usage or initialization
// failure, 2 runtime failure, 3 environment mismatch or unsupported build, 4 the gate failed,
// 5 not judged (ADR-0119 D5).
[[nodiscard]] int runGateWith(const atlantis::runtime::cli::GateRequest& request, GateEnvironment& environment);

// The same with the real clock, CPU sampler, child launcher and standard streams. main() creates
// the benchmark::StderrLogScope first.
[[nodiscard]] int runGate(const atlantis::runtime::cli::GateRequest& request);

}  // namespace atlantis::runtime::gate
