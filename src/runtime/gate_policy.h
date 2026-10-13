#pragma once

// Spec 0060 / ADR-0119 D1, D3 (Plan 0060 M2, P5): the gate's committed policy --
// the protocol's parameters, the per-tier tolerance numbers, which metrics are
// gated in which tier, which are only reported, and which counters describe the
// workload (a change in one of those makes two runs unlike, not slower).
//
// Loaded from tests/performance/gate_policy.json. GPU-independent; private to the
// atlantis_runtime executable's startup layer (see gate_set.h).

#include "gate_json.h"

#include <atlantis/result.h>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace atlantis::runtime::gate {

enum class Tier { Counter, Cpu, Gpu };
[[nodiscard]] std::string_view toString(Tier tier) noexcept;

struct TierPolicy {
  double absFloor = 0.0;  // ms for Cpu and Gpu: a delta at or under this is never a regression
  double maxPct = 0.0;    // percent of the baseline center a delta may reach before it is a regression
  // Counters only: a per-metric relative tolerance in percent (default 0 = exact).
  std::map<std::string, double> metricTolerancePct;
};

struct IdlePolicy {
  double cpuIdlePct = 90.0;
  double windowSeconds = 10.0;
  double timeoutSeconds = 120.0;
};

struct ProtocolPolicy {
  unsigned int runs = 5;
  double preheatSeconds = 90.0;
  double k = 3.0;
  double childTimeoutSeconds = 300.0;
  bool pinProcess = true;  // the children run pinned (owner ruling on the M7 stop)
  bool requireMainsPower = true;  // the gate refuses to run on battery (owner ruling after the M7 study)
  IdlePolicy idle;
};

// What fixed the numbers (Plan 0060 M7); empty until the calibration study has run.
struct MeasuredBlock {
  std::string study;
  std::string date;
  std::string profile;
  [[nodiscard]] bool present() const noexcept { return !study.empty(); }
};

// The record-time quality bar (Spec 0060 Correction 2026-10-14): `--record` refuses a recording whose
// minimum detectable regression, per tier, exceeds these bounds (percent of the metric's center).
struct RecordPolicy {
  double maxMdrCpuPct = 10.0;
  double maxMdrGpuPct = 20.0;
};

struct GatePolicy {
  unsigned int version = 1;
  ProtocolPolicy protocol;
  double dispersionFactor = 3.0;
  TierPolicy counter;
  TierPolicy cpu;
  TierPolicy gpu;
  std::vector<std::string> gatedCounter;
  std::vector<std::string> gatedCpu;
  std::vector<std::string> gatedGpu;
  std::vector<std::string> reported;
  std::vector<std::string> workload;
  RecordPolicy record;
  MeasuredBlock measured;

  [[nodiscard]] const TierPolicy& tierPolicy(Tier tier) const noexcept;
};

// The policy in `document`, or why it is not one (every problem names its member).
[[nodiscard]] atlantis::Result<GatePolicy, std::string> parsePolicy(const Value& document);
// Reads and parses the file at `path`.
[[nodiscard]] atlantis::Result<GatePolicy, std::string> loadPolicy(const std::string& path);

// A glob over a metric key: `*` matches any run of characters except '.'; everything
// else is literal. `passes.*.gpu_ms` matches `passes.draw.gpu_ms`.
[[nodiscard]] bool matchesPattern(std::string_view pattern, std::string_view key);

// The tier `key` is gated in, or nullopt when it is not gated (a reported or unlisted metric). A key
// matched by `reported` is not gated even if a gated pattern matches it too.
[[nodiscard]] std::optional<Tier> gatedTier(const GatePolicy& policy, std::string_view key);
[[nodiscard]] bool isReported(const GatePolicy& policy, std::string_view key);
[[nodiscard]] bool isWorkload(const GatePolicy& policy, std::string_view key);

}  // namespace atlantis::runtime::gate
