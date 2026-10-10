#pragma once

// Spec 0059 / ADR-0117 D4 (Plan 0059 M9, P9): the benchmark's measured loop.
//
// A pure function of three injected things -- "run one frame", the recorder, and
// a clock -- so it is tested with a fake frame and a fake clock and no GPU;
// benchmark_run.cpp wires it to RuntimeApplication::runFrame(). Private to the
// atlantis_runtime executable's startup layer (see benchmark_report.h).

#include <atlantis/profile_recorder.h>

#include <chrono>
#include <cstddef>
#include <functional>
#include <vector>

namespace atlantis::runtime::benchmark {

struct LoopOptions {
  unsigned int warmup = 0;   // presented frames run and not measured
  unsigned int frames = 1;   // presented frames measured
  // No presented frame for this long ends the run: a minimized or occluded window
  // renders nothing, and waiting would never end.
  std::chrono::milliseconds noProgressTimeout{15000};
};

struct LoopHooks {
  // Runs one frame (RuntimeApplication::runFrame()); false stops the loop (the
  // application failed, or the caller found something wrong).
  std::function<bool()> runFrame;
  // The clock for the no-progress timeout.
  std::function<std::chrono::steady_clock::time_point()> now;
};

enum class LoopStatus {
  Ok,
  Stopped,            // runFrame() returned false
  NoPresentedFrames,  // no frame reached Present within the timeout
};

struct LoopResult {
  LoopStatus status = LoopStatus::Ok;
  // Every frame run, in order, as the recorder saw it with its late GPU results
  // merged in. When status is Ok the last record is the extra frame that lets
  // the final measured frame's GPU results (and wall interval) arrive.
  std::vector<atlantis::profile::FrameRecord> records;
  std::size_t warmupRecords = 0;    // records[0, warmupRecords) are warmup
  std::size_t measuredRecords = 0;  // records[warmupRecords, warmupRecords + measuredRecords) are measured
  std::size_t presentedMeasured = 0;
  std::size_t skippedMeasured = 0;  // measured-range frames that never reached Present
};

// Starts the recorder, runs frames until `warmup + frames` of them have been
// presented, runs one more, and stops the recorder. Frames that never reach
// Present are run and counted but belong to no total.
[[nodiscard]] LoopResult runMeasuredLoop(const LoopOptions& options, const LoopHooks& hooks);

}  // namespace atlantis::runtime::benchmark
