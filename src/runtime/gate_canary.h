#pragma once

// Spec 0060 Correction 2026-10-15 (GPU-state canary): the pure side of the canary -- its document, how a
// reading is taken from it, the `canary` member of a set/1 document, and the judgment of a candidate against a
// baseline's reference. No device, no process: the GPU workload is canary_run.h.
//
// The canary is a fixed GPU micro-workload (three fragment workloads: ALU, write bandwidth, read bandwidth) that
// shares no code with the renderer. Measured before and after a session's runs and at record time, it tells the
// gate whether the machine's GPU is in the state the baseline was recorded in. A GPU regression with the canary
// stable is real and fails; a GPU regression with the canary shifted is the machine's state, not the code's, and
// is withheld as `inconclusive` with the reason stated.
//
// Private to the atlantis_runtime executable's startup layer, like gate_set.h.

#include "gate_json.h"

#include <atlantis/result.h>

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace atlantis::runtime::gate {

inline constexpr std::string_view kCanarySchema = "atlantis.benchmark.canary/1";
// Changing anything about the workload (the shaders, the draw counts, the target sizes) is a new id, and a
// baseline recorded under another id is not comparable.
inline constexpr std::string_view kCanaryWorkloadId = "gate-canary/1";

inline constexpr std::size_t kCanaryComponentCount = 3;
inline constexpr std::array<std::string_view, kCanaryComponentCount> kCanaryComponents = {"alu", "bw", "tex"};

// The fixed workload (canary_run.cpp draws exactly this).
inline constexpr unsigned int kCanaryWidth = 2560;
inline constexpr unsigned int kCanaryHeight = 1440;
inline constexpr unsigned int kCanaryRepetitions = 25;  // measured; the median is the reading
inline constexpr unsigned int kCanaryWarmup = 5;        // discarded
inline constexpr unsigned int kCanaryAluIterations = 768;
inline constexpr unsigned int kCanaryBwOverdraw = 32;
inline constexpr unsigned int kCanaryTexFetches = 16;

// One reading: the median GPU time in milliseconds of each component over the repetitions.
struct CanaryReading {
  std::array<double, kCanaryComponentCount> ms{};
};

// The child's document. `meta` is the build/system block (same members as atlantis.benchmark/1's meta, minus
// display); `repetitionMs[c]` is component c's GPU time per repetition. Members appear in a fixed order.
[[nodiscard]] Value buildCanaryDocument(Value meta,
                                        const std::array<std::vector<double>, kCanaryComponentCount>& repetitionMs);

// The reading in a canary document; an error names what is wrong (schema, workload id, a missing component).
[[nodiscard]] atlantis::Result<CanaryReading, std::string> extractCanaryReading(const Value& document);

// The document's meta.system block, for matching a machine to a baseline profile before any benchmark ran.
[[nodiscard]] const Value* canarySystem(const Value& document);

// The set/1 `canary` member for these readings, each labelled with the phase it was taken in ("start", "end"):
// {"workload", "measurements": [{"phase", "alu", "bw", "tex"}...], "aggregate": {<c>: {center, sigma, min, max, n}}}.
[[nodiscard]] Value canarySetValue(const std::vector<std::pair<std::string, CanaryReading>>& readings);

struct CanaryJudgment {
  bool measured = false;   // both sides have a canary under the same workload id
  bool shifted = false;    // some component's slowest candidate reading is above the reference by more than the threshold
  std::string problem;     // why it could not be judged, when `measured` is false
  std::array<double, kCanaryComponentCount> reference{};
  std::array<double, kCanaryComponentCount> referenceSigma{};
  std::array<double, kCanaryComponentCount> worst{};     // the slowest candidate reading per component
  std::array<double, kCanaryComponentCount> shiftPct{};  // (worst / reference - 1) x 100
  double thresholdPct = 0.0;
  std::vector<std::string> shiftedComponents;
};

// `candidate` and `baseline` are set/1 documents (the baseline with its `canary` aggregate). Only a slower
// candidate counts as a shift: a faster machine cannot explain a GPU regression away.
[[nodiscard]] CanaryJudgment judgeCanary(const Value& candidate, const Value& baseline, double maxShiftPct);

// The index of a component by name (0 for an unknown name).
[[nodiscard]] std::size_t canaryComponentIndex(std::string_view name) noexcept;

// The judgment as the verdict's `canary` member.
[[nodiscard]] Value canaryJudgmentValue(const CanaryJudgment& judgment);

// How far a document's canary readings spread, as (max / min - 1) x 100 of the worst component: the record bar's
// measure of whether a recording was made in one GPU state. Negative when the document has no canary.
[[nodiscard]] double canarySpreadPct(const Value& set);

}  // namespace atlantis::runtime::gate
