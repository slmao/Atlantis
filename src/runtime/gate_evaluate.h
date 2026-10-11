#pragma once

// Spec 0060 / ADR-0119 D3-D5 (Plan 0060 M2, P6, P7): the gate's evaluator -- a pure
// function of a candidate set, the baseline set and the policy. No clock, no
// randomness, no I/O: the same inputs always give the same verdict document, byte
// for byte.
//
// Order (P6): (1) identity and workload -- unlike runs are not judged
// (`incomparable`); (2) a counter that grew is deterministic evidence and fails the
// gate whatever the noise; (3) a candidate whose own run-to-run spread exceeds the
// baseline's by the policy's dispersion factor is `inconclusive`; (4) otherwise a
// regressed CPU or GPU metric fails the gate and none passes it. Improvements never
// fail it.
//
// Private to the atlantis_runtime executable's startup layer (see gate_set.h).

#include "gate_json.h"
#include "gate_policy.h"

#include <string>
#include <string_view>
#include <vector>

namespace atlantis::runtime::gate {

inline constexpr std::string_view kGateSchema = "atlantis.benchmark.gate/1";

// Exit codes (Spec 0060 R5; ADR-0119 D5), extending Plan 0059 J7's 0-3.
inline constexpr int kExitPass = 0;
inline constexpr int kExitFail = 4;        // a gated metric regressed
inline constexpr int kExitNotJudged = 5;   // incomparable, no baseline, or inconclusive

// Which baseline the verdict names.
struct BaselineRef {
  std::string profile;
  std::string file;
  std::string commit;
};

struct Verdict {
  Value document;       // atlantis.benchmark.gate/1
  std::string result;   // "pass", "fail", "incomparable" or "inconclusive"
  int exitCode = kExitPass;
};

// `candidate` and `baseline` are atlantis.benchmark.set/1 documents.
[[nodiscard]] Verdict evaluate(const Value& candidate, const Value& baseline, const GatePolicy& policy,
                               const BaselineRef& baselineRef);

// A verdict without a comparison: the run could not be judged (`incomparable`), e.g.
// no baseline exists for this machine's hardware profile. `reasons` say why and what
// to do.
[[nodiscard]] Verdict notJudged(const Value& candidate, const GatePolicy& policy, const BaselineRef& baselineRef,
                                std::vector<std::string> reasons);

[[nodiscard]] int exitCodeFor(std::string_view result) noexcept;

}  // namespace atlantis::runtime::gate
