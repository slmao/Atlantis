#include "vulkan_sampler.h"

namespace atlantis::vulkan_backend::detail {

VulkanSampler::VulkanSampler(VkDevice device, VkSampler sampler, atlantis::rhi::Filter filter,
                              atlantis::rhi::AddressMode addressMode)
    : device_(device), sampler_(sampler), filter_(filter), addressMode_(addressMode) {}

VulkanSampler::~VulkanSampler() { vkDestroySampler(device_, sampler_, nullptr); }

}  // namespace atlantis::vulkan_backend::detail
