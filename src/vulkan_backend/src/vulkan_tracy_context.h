#pragma once

#include <vulkan/vulkan_core.h>

#include <memory>
#include <string_view>

// Spec 0059 / ADR-0116 D5: Tracy's Vulkan GPU context, fed by the same
// beginRegion()/endRegion() calls as the Atlantis timestamp pool (ADR-0115 D2).
// The only Vulkan Backend file that includes a Tracy header is
// vulkan_tracy_context.cpp; this header names no Tracy type. The class is
// declared in every build so callers need no conditional; without
// ATLANTIS_PROFILING_TRACY create() returns nullptr and nothing else is reached.
// Not thread-safe: frame thread only (ADR-0004).
namespace atlantis::vulkan_backend::detail {

class TracyGpuContext {
 public:
  ~TracyGpuContext();
  TracyGpuContext(const TracyGpuContext&) = delete;
  TracyGpuContext& operator=(const TracyGpuContext&) = delete;

  // Creates an uncalibrated context. Tracy records and submits its own set-up
  // commands through `commandPool` on `queue` and waits for them, so this runs
  // once at device creation, never per frame. nullptr if it cannot be created.
  [[nodiscard]] static std::unique_ptr<TracyGpuContext> create(VkPhysicalDevice physicalDevice, VkDevice device,
                                                               VkQueue queue, VkCommandPool commandPool);

  // A zone on `commandBuffer`; nest up to kMaxDepth deep, end in reverse order.
  void begin(VkCommandBuffer commandBuffer, std::string_view name);
  void end();
  // Reads back finished zones; records into `commandBuffer` (outside any
  // rendering scope), once per frame before the buffer is ended.
  void collect(VkCommandBuffer commandBuffer);

 private:
  struct Impl;
  explicit TracyGpuContext(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};

}  // namespace atlantis::vulkan_backend::detail
