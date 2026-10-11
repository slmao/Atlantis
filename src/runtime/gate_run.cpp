#include "gate_run.h"

#include "benchmark_run.h"

#include <iostream>

namespace atlantis::runtime::gate {

namespace {

namespace bench = atlantis::runtime::benchmark;
using atlantis::runtime::cli::GateRequest;

}  // namespace

int runGate(const GateRequest& request) {
  // --applies judges nothing and needs no device and no profiling build.
  if (request.mode == GateRequest::Mode::Applies) {
    std::cerr << "atlantis_runtime: --gate --applies: not yet wired (Plan 0060 M8)\n";
    return bench::kExitRuntimeFailure;
  }
#if !defined(ATLANTIS_PROFILING)
  std::cerr << "atlantis_runtime: --gate needs a build with ATLANTIS_PROFILING=ON (this build has none); "
               "configure with -DATLANTIS_PROFILING=ON\n";
  return bench::kExitEnvironment;
#else
  if (!request.scene.has_value()) {
    std::cerr << "atlantis_runtime: --gate: scene '" << request.sceneName
              << "' is not available in this build (the bistro scene needs its content: run "
                 "tools/content/fetch_bistro.ps1 and re-configure)\n";
    return bench::kExitEnvironment;
  }
  std::cerr << "atlantis_runtime: --gate: not yet wired to the protocol (Plan 0060 M5)\n";
  return bench::kExitRuntimeFailure;
#endif
}

}  // namespace atlantis::runtime::gate
