#include "vulkan_pipeline.h"

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

bool isSampledTextureBindingInRange(std::uint32_t firstBinding, std::uint32_t bindingCount,
                                    std::uint32_t binding) noexcept {
  return binding >= firstBinding && binding - firstBinding < bindingCount;
}

VulkanPipeline::VulkanPipeline(VkDevice device, VkDescriptorPool descriptorPool, VkPipeline pipeline,
                                VkPipelineLayout pipelineLayout, VkDescriptorSetLayout descriptorSetLayout,
                                VkDescriptorSet descriptorSet, std::uint32_t sampledTextureFirstBinding,
                                std::uint32_t sampledTextureBindingCount)
    : device_(device),
      descriptorPool_(descriptorPool),
      pipeline_(pipeline),
      pipelineLayout_(pipelineLayout),
      descriptorSetLayout_(descriptorSetLayout),
      descriptorSet_(descriptorSet),
      sampledTextureFirstBinding_(sampledTextureFirstBinding),
      sampledTextureBindingCount_(sampledTextureBindingCount) {}

VulkanPipeline::~VulkanPipeline() {
  // Destruction order (Plan 0007 Section 10): vkDestroyPipeline ->
  // vkFreeDescriptorSets (this Pipeline's one set, from VulkanDevice's
  // pool -- before that pool itself may be destroyed) ->
  // vkDestroyDescriptorSetLayout -> vkDestroyPipelineLayout.
  vkDestroyPipeline(device_, pipeline_, nullptr);

  // vkFreeDescriptorSets is documented to only ever return VK_SUCCESS --
  // checked anyway, per this module's own "every VkResult is checked"
  // rule with no silent exceptions.
  const VkResult freeResult = vkFreeDescriptorSets(device_, descriptorPool_, 1, &descriptorSet_);
  ATLANTIS_CHECK(freeResult == VK_SUCCESS);

  vkDestroyDescriptorSetLayout(device_, descriptorSetLayout_, nullptr);
  vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
}

}  // namespace atlantis::vulkan_backend::detail
