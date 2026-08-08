#pragma once

#include <cstdint>

#include <vulkan/vulkan_core.h>

#include <atlantis/rhi/device.h>

// Concrete Vulkan implementation of atlantis::rhi::Device (ADR-0014). See
// vulkan_device.cpp for createDevice()'s full orchestration (instance
// creation, validation installation, physical device/queue-family
// selection, logical device/queue creation).
namespace atlantis::vulkan_backend::detail {

// Exclusively owns its VkInstance, the VkPhysicalDevice it selected, its
// VkDevice, the single VkQueue created from the selected combined
// graphics/present queue family, that queue family's index, and (when
// validation is enabled) the explicit VkDebugUtilsMessengerEXT installed
// immediately after instance creation succeeds (see
// plans/0003-rhi-vulkan-windowed-foundation.md Section 6) -- no separate
// validation *state* beyond that one handle.
//
// Not copyable, not movable -- held exclusively behind
// std::unique_ptr<atlantis::rhi::Device> (ADR-0014); nothing in this
// Plan needs to relocate a VulkanDevice, so this simplifies destruction-
// order reasoning at no real cost. Not internally thread-safe;
// construction, use by a future createPresentation() (Step 9), and
// destruction all happen on the single Phase 1 logical frame thread
// (ADR-0004). No global mutable state.
//
// The accessor methods below exist solely for this module's own future
// createPresentation() (Step 9) to build a VkSurfaceKHR, run the
// concrete-surface presentation-support check
// (vkGetPhysicalDeviceSurfaceSupportKHR), and construct a
// VulkanPresentation -- exactly the state that needs, nothing more. This
// class is declared in a private Vulkan Backend source header, never
// included by RHI or by Vulkan Backend's public construction header, so
// these accessors never reach RHI's or Vulkan Backend's public surface
// (Section 5) -- they are not a general GPU-handle escape hatch and are
// not reserved for a future RenderGraph, Renderer, or second backend.
class VulkanDevice final : public atlantis::rhi::Device {
 public:
  // destroyMessengerFn must be non-null whenever explicitMessenger is not
  // VK_NULL_HANDLE (validation enabled), and is otherwise never
  // dereferenced. Stored rather than re-resolved via vkGetInstanceProcAddr
  // at destruction time -- necessary destruction bookkeeping, not
  // additional diagnostics state.
  VulkanDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
               std::uint32_t queueFamilyIndex, VkDebugUtilsMessengerEXT explicitMessenger,
               PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn);
  ~VulkanDevice() override;

  VulkanDevice(const VulkanDevice&) = delete;
  VulkanDevice& operator=(const VulkanDevice&) = delete;
  VulkanDevice(VulkanDevice&&) = delete;
  VulkanDevice& operator=(VulkanDevice&&) = delete;

  [[nodiscard]] VkInstance instance() const noexcept { return instance_; }
  [[nodiscard]] VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
  [[nodiscard]] VkDevice device() const noexcept { return device_; }
  [[nodiscard]] std::uint32_t queueFamilyIndex() const noexcept { return queueFamilyIndex_; }

 private:
  VkInstance instance_;
  VkPhysicalDevice physicalDevice_;
  VkDevice device_;
  VkQueue queue_;
  std::uint32_t queueFamilyIndex_;
  VkDebugUtilsMessengerEXT explicitMessenger_;  // VK_NULL_HANDLE when validation is disabled
  PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn_;  // meaningful only when explicitMessenger_ is set
};

}  // namespace atlantis::vulkan_backend::detail
