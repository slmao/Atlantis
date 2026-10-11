#pragma once

// Spec 0060 / ADR-0119 D5 (Plan 0060 M3-M6): `atlantis_runtime --gate`, `--gate --record` and
// `--gate --applies`. The gate runs no frames itself: it launches child `--benchmark` runs,
// reduces them to a set document, and judges it against the committed baseline. Private to
// the atlantis_runtime executable's startup layer, like benchmark_run.h.

#include "cli.h"

namespace atlantis::runtime::gate {

// Runs the requested mode and returns the process exit code: 0 pass, 1 usage or initialization
// failure, 2 runtime failure, 3 environment mismatch or unsupported build, 4 the gate failed,
// 5 not judged (ADR-0119 D5). stdout carries exactly the verdict document (for --applies, the
// helper's one JSON line); logs go to stderr -- main() creates the benchmark::StderrLogScope first.
[[nodiscard]] int runGate(const atlantis::runtime::cli::GateRequest& request);

}  // namespace atlantis::runtime::gate
