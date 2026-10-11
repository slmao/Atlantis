#include "vulkan_gpu_timing.h"

#include <atlantis/log.h>

#include <algorithm>
#include <vector>

namespace atlantis::vulkan_backend::detail {

namespace {

constexpr VkQueryPipelineStatisticFlags kStatisticFlags =
    VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;

}  // namespace

std::uint64_t maskTicks(std::uint64_t ticks, std::uint32_t validBits) noexcept {
  if (validBits >= 64) return ticks;
  return ticks & ((std::uint64_t{1} << validBits) - 1);
}

std::uint64_t ticksBetween(std::uint64_t begin, std::uint64_t end, std::uint32_t validBits) noexcept {
  const std::uint64_t b = maskTicks(begin, validBits);
  const std::uint64_t e = maskTicks(end, validBits);
  if (e >= b) return e - b;
  // The counter wrapped once between the two writes.
  const std::uint64_t modulus = validBits >= 64 ? 0 : (std::uint64_t{1} << validBits);
  return modulus - b + e;
}

std::uint64_t ticksToNs(std::uint64_t ticks, double periodNs) noexcept {
  return static_cast<std::uint64_t>(static_cast<double>(ticks) * periodNs);
}

GpuTimingPool::GpuTimingPool(VkDevice device, VkQueryPool timestamps, VkQueryPool statistics, std::uint32_t validBits,
                             double periodNs)
    : device_(device), timestamps_(timestamps), statistics_(statistics), validBits_(validBits), periodNs_(periodNs) {}

GpuTimingPool::~GpuTimingPool() {
  if (statistics_ != VK_NULL_HANDLE) vkDestroyQueryPool(device_, statistics_, nullptr);
  if (timestamps_ != VK_NULL_HANDLE) vkDestroyQueryPool(device_, timestamps_, nullptr);
}

std::unique_ptr<GpuTimingPool> GpuTimingPool::create(VkDevice device, std::uint32_t validBits, double periodNs,
                                                     bool pipelineStatistics) {
  if (validBits == 0 || periodNs <= 0.0) return nullptr;

  VkQueryPoolCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
  info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  info.queryCount = kTimestampQueryCount;
  VkQueryPool timestamps = VK_NULL_HANDLE;
  if (const VkResult result = vkCreateQueryPool(device, &info, nullptr, &timestamps); result != VK_SUCCESS) {
    ATLANTIS_LOG_WARN("GPU timing unavailable: vkCreateQueryPool(timestamp) returned {}", static_cast<int>(result));
    return nullptr;
  }

  VkQueryPool statistics = VK_NULL_HANDLE;
  if (pipelineStatistics) {
    VkQueryPoolCreateInfo statisticsInfo{};
    statisticsInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    statisticsInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
    statisticsInfo.queryCount = 1;
    statisticsInfo.pipelineStatistics = kStatisticFlags;
    if (const VkResult result = vkCreateQueryPool(device, &statisticsInfo, nullptr, &statistics);
        result != VK_SUCCESS) {
      ATLANTIS_LOG_WARN("Pipeline statistics unavailable: vkCreateQueryPool returned {}", static_cast<int>(result));
      statistics = VK_NULL_HANDLE;
    }
  }
  return std::unique_ptr<GpuTimingPool>(new GpuTimingPool(device, timestamps, statistics, validBits, periodNs));
}

void GpuTimingPool::collect(const PendingGpuFrame& frame) const {
  if (frame.regionCount == 0 || frame.queryCount == 0) return;

  // Two 64-bit words per query: the value, then its availability.
  std::array<std::uint64_t, 2 * kTimestampQueryCount> data{};
  const VkResult result =
      vkGetQueryPoolResults(device_, timestamps_, 0, frame.queryCount, sizeof(std::uint64_t) * 2 * frame.queryCount,
                            data.data(), sizeof(std::uint64_t) * 2, VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
  if (result != VK_SUCCESS && result != VK_NOT_READY) {
    ATLANTIS_LOG_WARN("vkGetQueryPoolResults(timestamps) returned {}; this frame's GPU times are dropped",
                      static_cast<int>(result));
    return;
  }

  std::array<atlantis::profile::GpuRegion, atlantis::profile::kMaxGpuRegions> out{};
  std::uint32_t outCount = 0;
  std::uint32_t dropped = frame.dropped;
  for (std::uint32_t i = 0; i < frame.regionCount; ++i) {
    const RecordedRegion& region = frame.regions[i];
    const bool available = data[2 * region.beginQuery + 1] != 0 && data[2 * region.endQuery + 1] != 0;
    if (!available) {
      ++dropped;
      continue;
    }
    const std::uint64_t beginTicks = maskTicks(data[2 * region.beginQuery], validBits_);
    out[outCount].name = region.name;
    out[outCount].depth = region.depth;
    out[outCount].beginNs = ticksToNs(beginTicks, periodNs_);
    out[outCount].endNs = out[outCount].beginNs +
                          ticksToNs(ticksBetween(data[2 * region.beginQuery], data[2 * region.endQuery], validBits_), periodNs_);
    ++outCount;
  }
  atlantis::profile::deliverGpuRegions(frame.frameId, std::span<const atlantis::profile::GpuRegion>(out.data(), outCount),
                                       dropped);

  if (frame.statistics && statistics_ != VK_NULL_HANDLE) {
    // Result order follows the statistic bits, ascending: input-assembly
    // primitives (bit 1), then fragment-shader invocations (bit 7).
    std::array<std::uint64_t, 2> values{};
    const VkResult statisticsResult = vkGetQueryPoolResults(device_, statistics_, 0, 1, sizeof(values), values.data(),
                                                            sizeof(values), VK_QUERY_RESULT_64_BIT);
    if (statisticsResult == VK_SUCCESS) {
      atlantis::profile::counterValueForFrame(frame.frameId, atlantis::profile::internName("ia_primitives"),
                                              static_cast<double>(values[0]));
      atlantis::profile::counterValueForFrame(frame.frameId, atlantis::profile::internName("fragment_invocations"),
                                              static_cast<double>(values[1]));
    }
  }
}

void GpuRegionRecorder::begin(const GpuTimingPool* pool, VkCommandBuffer commandBuffer, std::string_view name,
                              bool renderingActive) {
  if (openCount_ >= kMaxOpenRegions) {
    ++overflowDepth_;
    ++frame_.dropped;
    return;
  }
  const auto pushUntimed = [this] { open_[openCount_++] = kUntimed; };

  if (pool == nullptr || !timingThisList_ || !atlantis::profile::isRecording() ||
      !atlantis::profile::gpuTimingEnabled()) {
    pushUntimed();
    return;
  }
  if (!resetDone_) {
    if (renderingActive) {  // a reset cannot be recorded inside a rendering scope
      timingThisList_ = false;
      ++frame_.dropped;
      pushUntimed();
      return;
    }
    vkCmdResetQueryPool(commandBuffer, pool->timestamps(), 0, kTimestampQueryCount);
    if (pool->statistics() != VK_NULL_HANDLE) vkCmdResetQueryPool(commandBuffer, pool->statistics(), 0, 1);
    resetDone_ = true;
  }
  if (frame_.regionCount >= atlantis::profile::kMaxGpuRegions || frame_.queryCount + 2 > kTimestampQueryCount) {
    ++frame_.dropped;
    pushUntimed();
    return;
  }

  const std::uint32_t index = frame_.regionCount++;
  RecordedRegion& region = frame_.regions[index];
  region.name = atlantis::profile::internName(name);
  region.depth = openCount_;
  region.beginQuery = frame_.queryCount++;
  region.endQuery = frame_.queryCount++;
  vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, pool->timestamps(), region.beginQuery);

  if (openCount_ == 0 && name == kPipelineStatisticsRegion && pool->statistics() != VK_NULL_HANDLE &&
      statisticsRegion_ == kUntimed && !renderingActive) {
    vkCmdBeginQuery(commandBuffer, pool->statistics(), 0, 0);
    statisticsRegion_ = index;
  }
  open_[openCount_++] = index;
}

void GpuRegionRecorder::end(const GpuTimingPool* pool, VkCommandBuffer commandBuffer) {
  if (overflowDepth_ > 0) {
    --overflowDepth_;
    return;
  }
  if (openCount_ == 0) return;  // unbalanced end: nothing to close
  const std::uint32_t index = open_[--openCount_];
  if (index == kUntimed || pool == nullptr) return;
  vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, pool->timestamps(), frame_.regions[index].endQuery);
  if (index == statisticsRegion_) {
    vkCmdEndQuery(commandBuffer, pool->statistics(), 0);
    frame_.statistics = true;
  }
}

PendingGpuFrame GpuRegionRecorder::take() noexcept {
  PendingGpuFrame out = frame_;
  frame_ = PendingGpuFrame{};
  resetDone_ = false;
  timingThisList_ = true;
  statisticsRegion_ = kUntimed;
  openCount_ = 0;
  overflowDepth_ = 0;
  return out;
}

}  // namespace atlantis::vulkan_backend::detail
