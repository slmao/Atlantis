#pragma once

// Spec 0060 / ADR-0119 D2 (Plan 0060 M1): the aggregate of the gate's R runs,
// `atlantis.benchmark.set/1`.
//
// Each run is a child `--benchmark` run that wrote an `atlantis.benchmark/1`
// document. A run is reduced to its run-level values -- the p50 (and p95, for
// reporting) of every metric that has statistics, named as compareDocuments()
// names them -- and the gated statistic per metric is the median of the R run
// p50s, with the robust sigma and the min/max of those values as its noise
// band. The set carries the runs' `meta`, `scene` and `settings` unchanged, so
// the benchmark's comparability code applies to it as it stands.
//
// Pure functions over values; GPU-independent. Private to the atlantis_runtime
// executable's startup layer (see benchmark_report.h).

#include "gate_json.h"

#include <atlantis/result.h>

#include <map>
#include <span>
#include <string>
#include <string_view>

namespace atlantis::runtime::gate {

inline constexpr std::string_view kSetSchema = "atlantis.benchmark.set/1";

// What a gate invocation was asked to do, recorded in the set so a reader can
// tell a conformant run from a smoke run.
struct Protocol {
  unsigned int runs = 5;
  double preheatSeconds = 90.0;
  bool idleWait = true;
  double k = 3.0;
  bool pin = false;  // the children ran with --pin-process on
};

// The pinning state a run was made under, as the documents record it: a pinned run is never
// compared against an unpinned baseline (the evaluator refuses).
[[nodiscard]] Value pinValue(bool pinned);

// One run's run-level values, keyed `cpu_active_ms`, `zones.<Zone>`,
// `passes.<label>.gpu_ms`, ...
struct RunValues {
  std::map<std::string, double> p50;
  std::map<std::string, double> p95;
  std::map<std::string, std::string> unavailable;  // a metric the run could not supply -> why
};

// Reads the run-level values out of an `atlantis.benchmark/1` document; an error
// text when it is not one.
[[nodiscard]] atlantis::Result<RunValues, std::string> extractRunValues(const Value& benchmarkDocument);

// The set of the given run documents. An error text when there are none, when one is
// not a benchmark document, or when the runs disagree on `meta`, `scene` or
// `settings` (a set of unlike runs is not a measurement). Members appear in a fixed
// order, so the same input always yields the same bytes.
[[nodiscard]] atlantis::Result<Value, std::string> buildSet(const Protocol& protocol,
                                                            std::span<const Value> runDocuments);

}  // namespace atlantis::runtime::gate
