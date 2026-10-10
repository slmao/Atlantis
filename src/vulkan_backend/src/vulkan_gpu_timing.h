#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/profile_recorder.h>

#include <array>
#include <cstdint>
#include <memory>
#include <string_view>

// Spec 0059 / ADR-0115 D3: GPU region timestamps, owned by the Vulkan Backend.
// One query pool (one frame is in flight), reset by the first region of a
// command list, read back right after the fence wait Device::submit() already
// performs, and delivered to the Core recorder one frame late. Everything here
// is compiled in every configuration but only reached from a profiling build
// (ATLANTIS_PROFILING); a build without it never creates a pool.
// Not thread-safe: frame thread only (ADR-0004).
namespace atlantis::vulkan_backend::detail {

// 256 queries: 128 regions of two timestamps each (profile::kMaxGpuRegions).
inline constexpr std::uint32_t kTimestampQueryCount = 256;
// Deepest tracked nesting; a deeper region is counted as dropped and untimed.
inline constexpr std::uint32_t kMaxOpenRegions = 8;
// The one top-level region that also carries a pipeline-statistics query
// (fragment invocations, input-assembly primitives), if the device offers them.
inline constexpr std::string_view kPipelineStatisticsRegion = "draw";

// Pure helpers (unit-testable without a device).
[[nodiscard]] std::uint64_t maskTicks(std::uint64_t ticks, std::uint32_t validBits) noexcept;
// end - begin in ticks on a counter of `validBits` bits, correct across one wrap.
[[nodiscard]] std::uint64_t ticksBetween(std::uint64_t begin, std::uint64_t end, std::uint32_t validBits) noexcept;
[[nodiscard]] std::uint64_t ticksToNs(std::uint64_t ticks, double periodNs) noexcept;

struct RecordedRegion {
  atlantis::profile::NameId name = atlantis::profile::kInvalidName;
  std::uint32_t depth = 0;
  std::uint32_t beginQuery = 0;
  std::uint32_t endQuery = 0;
};

// What one submitted command list recorded, handed from the list to the Device
// at submit() and read back when that submission's fence has been waited.
struct PendingGpuFrame {
  std::uint64_t frameId = 0;  // the recorder frame that was current at submit()
  std::array<RecordedRegion, atlantis::profile::kMaxGpuRegions> regions{};
  std::uint32_t regionCount = 0;
  std::uint32_t queryCount = 0;
  std::uint32_t dropped = 0;
  bool statistics = false;  // the pipeline-statistics query was begun and ended
};

class GpuTimingPool {
 public:
  ~GpuTimingPool();
  GpuTimingPool(const GpuTimingPool&) = delete;
  GpuTimingPool& operator=(const GpuTimingPool&) = delete;

  // nullptr if the queue cannot write timestamps (`validBits` == 0) or a pool
  // cannot be created: GPU timing is then reported unavailable, never an error.
  [[nodiscard]] static std::unique_ptr<GpuTimingPool> create(VkDevice device, std::uint32_t validBits,
                                                             double periodNs, bool pipelineStatistics);

  [[nodiscard]] VkQueryPool timestamps() const noexcept { return timestamps_; }
  [[nodiscard]] VkQueryPool statistics() const noexcept { return statistics_; }

  // Reads `frame`'s queries (the caller has already waited the submission's
  // fence, so they are complete) and delivers them to the recorder.
  void collect(const PendingGpuFrame& frame) const;

 private:
  GpuTimingPool(VkDevice device, VkQueryPool timestamps, VkQueryPool statistics, std::uint32_t validBits,
                double periodNs);

  VkDevice device_;
  VkQueryPool timestamps_;
  VkQueryPool statistics_;
  std::uint32_t validBits_;
  double periodNs_;
};

// Per command list: records region timestamps as beginRegion()/endRegion() are
// called, and hands the result to the Device at submit().
class GpuRegionRecorder {
 public:
  void begin(const GpuTimingPool* pool, VkCommandBuffer commandBuffer, std::string_view name, bool renderingActive);
  void end(const GpuTimingPool* pool, VkCommandBuffer commandBuffer);
  // Moves the recorded regions out; the caller sets frameId.
  [[nodiscard]] PendingGpuFrame take() noexcept;

 private:
  static constexpr std::uint32_t kUntimed = 0xFFFFFFFFu;

  PendingGpuFrame frame_;
  bool resetDone_ = false;
  bool timingThisList_ = true;
  std::uint32_t statisticsRegion_ = kUntimed;
  std::array<std::uint32_t, kMaxOpenRegions> open_{};
  std::uint32_t openCount_ = 0;
  std::uint32_t overflowDepth_ = 0;
};

}  // namespace atlantis::vulkan_backend::detail
