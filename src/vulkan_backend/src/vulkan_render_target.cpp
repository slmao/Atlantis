#include "vulkan_render_target.h"

namespace atlantis::vulkan_backend::detail {

VulkanRenderTarget::VulkanRenderTarget(VkImage image, std::uint32_t imageIndex, atlantis::rhi::Extent2D extent,
                                        atlantis::rhi::Format format, VkSemaphore acquireCompleteSemaphore,
                                        VkSemaphore renderFinishedSemaphore)
    : image_(image),
      imageIndex_(imageIndex),
      extent_(extent),
      format_(format),
      acquireCompleteSemaphore_(acquireCompleteSemaphore),
      renderFinishedSemaphore_(renderFinishedSemaphore) {}

}  // namespace atlantis::vulkan_backend::detail
