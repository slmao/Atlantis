#pragma once

// Spec 0059 / ADR-0117 D1, D4-D5 (Plan 0059 M9, P9): `atlantis_runtime
// --benchmark`. Creates the application at a requested client size, applies the
// variant, runs the measured loop around RuntimeApplication::runFrame(), and
// writes the result document and, if asked, its comparison with an earlier one.
// Private to the atlantis_runtime executable's startup layer, like
// editor_attachment.h: never added to atlantis_runtime_host.

#include "cli.h"

#include <atlantis/runtime/bootstrap_config.h>

namespace atlantis::runtime::benchmark {

// Exit codes (Plan 0059 J7): 0 success; 1 usage error or initialization failure
// (the existing meaning); 2 runtime failure (the application failed, no frame was
// presented, or the variant did not take); 3 environment mismatch or unsupported
// build (the window is not the requested size, the build has no profiling, the
// scene is absent). Never a performance verdict.
inline constexpr int kExitOk = 0;
inline constexpr int kExitInitFailure = 1;
inline constexpr int kExitRuntimeFailure = 2;
inline constexpr int kExitEnvironment = 3;

// While one exists, every log record goes to stderr (the default sink writes Info
// to stdout, which would corrupt the one JSON document stdout is reserved for).
// main() creates it before anything logs when --benchmark is given; restores the
// default sink at destruction. Not thread-safe; one at a time.
class StderrLogScope {
 public:
  StderrLogScope();
  ~StderrLogScope();
  StderrLogScope(const StderrLogScope&) = delete;
  StderrLogScope& operator=(const StderrLogScope&) = delete;
};

// `config` is the fully populated bootstrap configuration for the scene (the
// caller owns paths and shader pairs, as for any scene run); the benchmark sets
// its window size and validation. stdout carries exactly the one result document,
// provided a StderrLogScope is alive (main() makes one); stderr carries the logs.
[[nodiscard]] int runBenchmark(const cli::BenchmarkRequest& request, atlantis::runtime::BootstrapConfig config);

}  // namespace atlantis::runtime::benchmark
