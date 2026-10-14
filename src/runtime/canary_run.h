#pragma once

// Spec 0060 Correction 2026-10-15 (GPU-state canary): `atlantis_runtime --canary`. Runs the gate's fixed GPU
// micro-workload (gate_canary.h) on a headless device and writes an atlantis.benchmark.canary/1 document. The
// gate launches it as a child, like `--benchmark`, before and after a session's runs. Private to the
// atlantis_runtime executable's startup layer, like benchmark_run.h; renders nothing the renderer owns.

#include "cli.h"

namespace atlantis::runtime::canary {

// Exit codes as for --benchmark (Plan 0059 J7): 0 success; 1 usage error; 2 runtime failure (a device or
// submission error, no GPU result); 3 environment (a build without ATLANTIS_PROFILING, a device that cannot
// write GPU timestamps, the process could not be pinned). Never a verdict.
[[nodiscard]] int runCanary(const cli::CanaryRequest& request);

}  // namespace atlantis::runtime::canary
