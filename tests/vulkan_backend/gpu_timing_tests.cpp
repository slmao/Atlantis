#include "vulkan_gpu_timing.h"

#include <catch2/catch_test_macros.hpp>

// Spec 0059 / ADR-0115 D3: the GPU-independent part of the timestamp machinery --
// tick arithmetic (including a counter wrap), the "timestamps unsupported" and
// "no pool" outcomes, and the per-list recorder when there is nothing to record
// into. No Vulkan call is made.

using atlantis::vulkan_backend::detail::GpuRegionRecorder;
using atlantis::vulkan_backend::detail::GpuTimingPool;
using atlantis::vulkan_backend::detail::maskTicks;
using atlantis::vulkan_backend::detail::ticksBetween;
using atlantis::vulkan_backend::detail::ticksToNs;

TEST_CASE("timestamp ticks are masked to the queue's valid bits", "[vulkan_backend][gpu_timing]") {
  CHECK(maskTicks(0xFFFFFFFFFFFFFFFFull, 64) == 0xFFFFFFFFFFFFFFFFull);
  CHECK(maskTicks(0x1FFull, 8) == 0xFFull);
  CHECK(maskTicks(0xDEADBEEFull, 32) == 0xDEADBEEFull);
  CHECK(maskTicks(0x1FFFFFFFFull, 32) == 0xFFFFFFFFull);
}

TEST_CASE("the tick difference is right across a counter wrap", "[vulkan_backend][gpu_timing]") {
  CHECK(ticksBetween(100, 160, 64) == 60);
  CHECK(ticksBetween(100, 160, 36) == 60);
  // A 36-bit counter that wrapped once between the two writes.
  const std::uint64_t modulus = std::uint64_t{1} << 36;
  CHECK(ticksBetween(modulus - 10, 5, 36) == 15);
  // Bits above the valid range are ignored.
  CHECK(ticksBetween((std::uint64_t{7} << 36) | 100, (std::uint64_t{9} << 36) | 160, 36) == 60);
}

TEST_CASE("ticks convert to nanoseconds by the device's period", "[vulkan_backend][gpu_timing]") {
  CHECK(ticksToNs(1000, 1.0) == 1000);
  CHECK(ticksToNs(1000, 0.5) == 500);
  CHECK(ticksToNs(0, 38.4) == 0);
}

TEST_CASE("a queue that cannot write timestamps yields no pool", "[vulkan_backend][gpu_timing]") {
  // validBits == 0 returns before any Vulkan object is touched.
  CHECK(GpuTimingPool::create(VK_NULL_HANDLE, 0, 1.0, false) == nullptr);
  CHECK(GpuTimingPool::create(VK_NULL_HANDLE, 36, 0.0, false) == nullptr);  // no usable period
}

TEST_CASE("with no pool the recorder tracks nesting and records nothing", "[vulkan_backend][gpu_timing]") {
  GpuRegionRecorder recorder;
  recorder.begin(nullptr, VK_NULL_HANDLE, "a", false);
  recorder.begin(nullptr, VK_NULL_HANDLE, "b", false);
  recorder.end(nullptr, VK_NULL_HANDLE);
  recorder.end(nullptr, VK_NULL_HANDLE);
  recorder.end(nullptr, VK_NULL_HANDLE);  // an unbalanced end is ignored, not undefined
  const auto frame = recorder.take();
  CHECK(frame.regionCount == 0);
  CHECK(frame.queryCount == 0);
  CHECK(frame.dropped == 0);
}

TEST_CASE("nesting deeper than the tracked limit is counted, not recorded, and stays balanced",
          "[vulkan_backend][gpu_timing]") {
  GpuRegionRecorder recorder;
  constexpr int kDepth = 12;
  for (int i = 0; i < kDepth; ++i) recorder.begin(nullptr, VK_NULL_HANDLE, "deep", false);
  for (int i = 0; i < kDepth; ++i) recorder.end(nullptr, VK_NULL_HANDLE);
  const auto frame = recorder.take();
  CHECK(frame.dropped == kDepth - atlantis::vulkan_backend::detail::kMaxOpenRegions);
  // The recorder is reusable after take().
  recorder.begin(nullptr, VK_NULL_HANDLE, "again", false);
  recorder.end(nullptr, VK_NULL_HANDLE);
  CHECK(recorder.take().dropped == 0);
}
