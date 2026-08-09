#pragma once

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/submission_signal.h>

// Concrete Vulkan implementation of atlantis::rhi::SubmissionSignal
// (ADR-0019, ADR-0020). See vulkan_device.cpp for where this is
// constructed (VulkanDevice::submit()).
namespace atlantis::vulkan_backend::detail {

// Non-owning: the wrapped VkSemaphore is VulkanDevice's own persistent
// render-finished semaphore (created once, reused every frame under the
// single-frame-in-flight discipline -- see VulkanDevice's own header
// comment). This token's destructor performs no Vulkan call; the
// semaphore's lifetime is VulkanDevice's, not this token's. Not
// copyable, not movable -- held exclusively behind
// std::unique_ptr<atlantis::rhi::SubmissionSignal>.
class VulkanSubmissionSignal final : public atlantis::rhi::SubmissionSignal {
 public:
  explicit VulkanSubmissionSignal(VkSemaphore renderFinishedSemaphore);
  ~VulkanSubmissionSignal() override = default;

  VulkanSubmissionSignal(const VulkanSubmissionSignal&) = delete;
  VulkanSubmissionSignal& operator=(const VulkanSubmissionSignal&) = delete;
  VulkanSubmissionSignal(VulkanSubmissionSignal&&) = delete;
  VulkanSubmissionSignal& operator=(VulkanSubmissionSignal&&) = delete;

  [[nodiscard]] VkSemaphore semaphore() const noexcept { return semaphore_; }

 private:
  VkSemaphore semaphore_;
};

}  // namespace atlantis::vulkan_backend::detail
