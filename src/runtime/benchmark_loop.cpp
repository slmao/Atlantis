#include "benchmark_loop.h"

namespace atlantis::runtime::benchmark {

namespace profile = atlantis::profile;

namespace {

// Folds the previous frame's record up to date: its GPU results arrive while the
// next frame is submitted, so a record read right after its own frame lacks them.
void refreshPrevious(std::vector<profile::FrameRecord>& records) {
  if (records.size() < 2) return;
  profile::FrameRecord& previous = records[records.size() - 2];
  profile::FrameRecord fresh;
  if (profile::readFrame(previous.frameId, fresh)) previous = fresh;
}

}  // namespace

LoopResult runMeasuredLoop(const LoopOptions& options, const LoopHooks& hooks) {
  LoopResult result;
  // While a warmup duration is pending the end of warmup is not known in frames, so the target is
  // open until it ends; without one it is `warmup + frames`, as it always was.
  const bool timedWarmup = options.warmupDuration.count() > 0;
  std::size_t target = timedWarmup ? ~std::size_t{0} : static_cast<std::size_t>(options.warmup) + options.frames;
  std::size_t presented = 0;

  profile::startRecording();
  const auto start = hooks.now();
  auto lastProgress = start;
  bool warmupDone = options.warmup == 0 && !timedWarmup;

  const auto runOne = [&]() -> bool {
    if (!hooks.runFrame()) return false;
    profile::FrameRecord record;
    if (!profile::readFrame(profile::currentFrameId(), record)) return true;  // not recorded: nothing to keep
    result.records.push_back(record);
    refreshPrevious(result.records);
    return true;
  };

  while (presented < target) {
    if (!runOne()) {
      result.status = LoopStatus::Stopped;
      break;
    }
    const profile::FrameRecord& latest = result.records.back();
    const bool isPresented = latest.presented;
    if (isPresented) {
      ++presented;
      lastProgress = hooks.now();
      if (!warmupDone && presented >= options.warmup && (!timedWarmup || hooks.now() - start >= options.warmupDuration)) {
        warmupDone = true;
        result.warmupRecords = result.records.size();
        if (timedWarmup) target = presented + options.frames;
      }
    } else if (hooks.now() - lastProgress > options.noProgressTimeout) {
      result.status = LoopStatus::NoPresentedFrames;
      break;
    }
  }

  if (result.status == LoopStatus::Ok) {
    // The extra frame: delivers the last measured frame's GPU results and gives
    // it a next frame to measure its wall interval against.
    result.measuredRecords = result.records.size() - result.warmupRecords;
    if (!runOne()) result.status = LoopStatus::Stopped;
    for (std::size_t i = result.warmupRecords; i < result.warmupRecords + result.measuredRecords; ++i) {
      if (result.records[i].presented) {
        ++result.presentedMeasured;
      } else {
        ++result.skippedMeasured;
      }
    }
  }
  profile::stopRecording();
  return result;
}

}  // namespace atlantis::runtime::benchmark
