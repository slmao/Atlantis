#include "vulkan_device.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/log.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "device_extension_list.h"
#include "dynamic_rendering.h"
#include "dynamic_rendering_entry_points.h"
#include "hdr_color_target_capability.h"
#include "validation.h"
#include "vulkan_buffer.h"
#include "vulkan_command_list.h"
#include "vulkan_hdr_color_target.h"
#include "vulkan_instance.h"
#include "vulkan_memory.h"
#include "vulkan_offscreen_target.h"
#include "vulkan_pipeline.h"
#include "vulkan_render_target.h"
#include "vulkan_render_target_access.h"
#include "vulkan_result.h"
#include "vulkan_sampled_texture.h"
#include "vulkan_sampler.h"
#include "vulkan_submission_signal.h"
#include "vulkan_texture.h"
#include "wsi/win32_surface.h"

namespace atlantis::vulkan_backend::detail {

namespace {

constexpr const char* kSwapchainExtension = "VK_KHR_swapchain";
// Spec 0007 / ADR-0024: the device-level dynamic-rendering extension --
// only requested when the Extension path (Section 8) is selected for the
// chosen physical device; the Core path needs no device extension.
constexpr const char* kDynamicRenderingExtension = "VK_KHR_dynamic_rendering";

// Two-call idiom for vkEnumeratePhysicalDevices. Same VK_INCOMPLETE/
// failure handling rationale as vulkan_instance.cpp's
// enumerateInstanceExtensions(): partial data is never treated as a
// complete result, and the vector is resized to the second call's own
// returned count -- that count may be smaller than the first call's
// count, and the vector must never be returned with trailing, never-
// written default-constructed elements past it.
[[nodiscard]] std::optional<std::vector<VkPhysicalDevice>> enumeratePhysicalDevices(VkInstance instance) {
  std::uint32_t count = 0;
  if (vkEnumeratePhysicalDevices(instance, &count, nullptr) != VK_SUCCESS) {
    return std::nullopt;
  }
  if (count == 0) {
    return std::vector<VkPhysicalDevice>{};
  }

  std::vector<VkPhysicalDevice> devices(count);
  const VkResult fillResult = vkEnumeratePhysicalDevices(instance, &count, devices.data());
  if (fillResult != VK_SUCCESS) {
    return std::nullopt;
  }
  devices.resize(count);
  return devices;
}

// Two-call idiom for vkEnumerateDeviceExtensionProperties (pLayerName ==
// nullptr: the device's own extensions, not a specific layer's). Same
// final-count-resize rationale as enumeratePhysicalDevices() above.
[[nodiscard]] std::optional<std::vector<VkExtensionProperties>> enumerateDeviceExtensions(
    VkPhysicalDevice physicalDevice) {
  std::uint32_t count = 0;
  if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS) {
    return std::nullopt;
  }
  if (count == 0) {
    return std::vector<VkExtensionProperties>{};
  }

  std::vector<VkExtensionProperties> extensions(count);
  const VkResult fillResult = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data());
  if (fillResult != VK_SUCCESS) {
    return std::nullopt;
  }
  extensions.resize(count);
  return extensions;
}

[[nodiscard]] bool supportsSwapchainExtension(VkPhysicalDevice physicalDevice) {
  const std::optional<std::vector<VkExtensionProperties>> extensions = enumerateDeviceExtensions(physicalDevice);
  if (!extensions.has_value()) {
    return false;
  }
  for (const auto& extension : *extensions) {
    if (std::strcmp(extension.extensionName, kSwapchainExtension) == 0) {
      return true;
    }
  }
  return false;
}

// vkGetPhysicalDeviceQueueFamilyProperties returns void -- no VkResult
// exists for this function, so none is fabricated. Ordinary count-then-
// fill two-call form; the second call may still update count, so the
// vector is resized to that final count rather than returned at the
// first call's (possibly larger) size.
[[nodiscard]] std::vector<VkQueueFamilyProperties> queryQueueFamilies(VkPhysicalDevice physicalDevice) {
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
  if (count == 0) {
    return {};
  }
  std::vector<VkQueueFamilyProperties> families(count);
  vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());
  families.resize(count);
  return families;
}

// The first queue family index on physicalDevice supporting both
// VK_QUEUE_GRAPHICS_BIT and Win32 generic presentation (Plan Section 7
// item 2) -- a combined graphics+present family. Separate-family fallback
// is not implemented, per the Plan's explicit Phase 1 disposition.
[[nodiscard]] std::optional<std::uint32_t> findCombinedGraphicsPresentQueueFamily(VkPhysicalDevice physicalDevice) {
  const std::vector<VkQueueFamilyProperties> families = queryQueueFamilies(physicalDevice);
  for (std::uint32_t index = 0; index < families.size(); ++index) {
    const bool hasGraphics = (families[index].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
    if (hasGraphics && win32PresentationSupported(physicalDevice, index)) {
      return index;
    }
  }
  return std::nullopt;
}

struct PhysicalDeviceSelection {
  VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
  std::uint32_t queueFamilyIndex = 0;
  DynamicRenderingPath dynamicRenderingPath = DynamicRenderingPath::Unavailable;
};

[[nodiscard]] bool supportsDeviceExtension(VkPhysicalDevice physicalDevice, const char* extensionName) {
  const std::optional<std::vector<VkExtensionProperties>> extensions = enumerateDeviceExtensions(physicalDevice);
  if (!extensions.has_value()) {
    return false;
  }
  for (const auto& extension : *extensions) {
    if (std::strcmp(extension.extensionName, extensionName) == 0) {
      return true;
    }
  }
  return false;
}

// Spec 0007 / ADR-0024 Section 8's real capability-query wrapper: queries
// the four per-candidate booleans decideDynamicRenderingPath() needs for
// one physical-device candidate, then delegates the actual decision to
// that pure function. getPhysicalDeviceFeatures2KHR is non-null iff
// instanceExtensionAvailable is true (vulkan_instance.cpp's own
// resolution contract) -- never called otherwise.
[[nodiscard]] DynamicRenderingPath queryDynamicRenderingPath(
    VkPhysicalDevice physicalDevice, bool instanceExtensionAvailable, bool instanceRequestedApiVersionAtLeast1_3,
    PFN_vkGetPhysicalDeviceFeatures2KHR getPhysicalDeviceFeatures2KHR) {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(physicalDevice, &properties);
  const bool apiVersionAtLeast1_3 = properties.apiVersion >= VK_API_VERSION_1_3;

  bool coreFeatureSupported = false;
  if (instanceExtensionAvailable && apiVersionAtLeast1_3) {
    ATLANTIS_CHECK(getPhysicalDeviceFeatures2KHR != nullptr);
    VkPhysicalDeviceVulkan13Features vulkan13Features{};
    vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &vulkan13Features;
    getPhysicalDeviceFeatures2KHR(physicalDevice, &features2);
    coreFeatureSupported = vulkan13Features.dynamicRendering == VK_TRUE;
  }

  const bool extensionAdvertised = supportsDeviceExtension(physicalDevice, kDynamicRenderingExtension);
  bool extensionFeatureSupported = false;
  if (instanceExtensionAvailable && extensionAdvertised) {
    ATLANTIS_CHECK(getPhysicalDeviceFeatures2KHR != nullptr);
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures{};
    dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
    VkPhysicalDeviceFeatures2 features2{};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features2.pNext = &dynamicRenderingFeatures;
    getPhysicalDeviceFeatures2KHR(physicalDevice, &features2);
    extensionFeatureSupported = dynamicRenderingFeatures.dynamicRendering == VK_TRUE;
  }

  return decideDynamicRenderingPath(instanceExtensionAvailable, instanceRequestedApiVersionAtLeast1_3,
                                     apiVersionAtLeast1_3, coreFeatureSupported, extensionAdvertised,
                                     extensionFeatureSupported);
}

// Selects the first physical device, in vkEnumeratePhysicalDevices' own
// returned order, meeting Phase 1's minimum requirement: Vulkan API
// version >= 1.0 (Spec 0003 needs nothing higher -- this is a structural
// check, not a stricter version this Plan has no basis to invent),
// VK_KHR_swapchain support, a combined graphics+present queue family
// (Plan Section 7 items 2-3), and -- Spec 0007 -- a usable dynamic-
// rendering path (Core or Extension). No scoring, no discrete/integrated
// preference, no caller-visible enumeration/selection API.
struct PhysicalDeviceSelectionResult {
  std::optional<PhysicalDeviceSelection> selection;
  // Spec 0007 Section 8 step 6: true iff at least one candidate met every
  // pre-existing suitability criterion (Spec 0003) but was rejected
  // specifically because no dynamic-rendering path was available --
  // distinguishes DeviceCreateError::DynamicRenderingUnavailable from the
  // pre-existing DeviceCreateError::NoSuitablePhysicalDevice.
  bool foundSuitableExceptDynamicRendering = false;
};

[[nodiscard]] PhysicalDeviceSelectionResult selectPhysicalDevice(
    const std::vector<VkPhysicalDevice>& physicalDevices, bool physicalDeviceProperties2ExtensionAvailable,
    bool instanceRequestedApiVersionAtLeast1_3, PFN_vkGetPhysicalDeviceFeatures2KHR getPhysicalDeviceFeatures2KHR) {
  PhysicalDeviceSelectionResult result;
  for (VkPhysicalDevice physicalDevice : physicalDevices) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);
    if (properties.apiVersion < VK_API_VERSION_1_0) {
      continue;
    }
    if (!supportsSwapchainExtension(physicalDevice)) {
      continue;
    }
    const std::optional<std::uint32_t> queueFamilyIndex = findCombinedGraphicsPresentQueueFamily(physicalDevice);
    if (!queueFamilyIndex.has_value()) {
      continue;
    }
    const DynamicRenderingPath dynamicRenderingPath =
        queryDynamicRenderingPath(physicalDevice, physicalDeviceProperties2ExtensionAvailable,
                                   instanceRequestedApiVersionAtLeast1_3, getPhysicalDeviceFeatures2KHR);
    if (dynamicRenderingPath == DynamicRenderingPath::Unavailable) {
      result.foundSuitableExceptDynamicRendering = true;
      continue;
    }
    result.selection = PhysicalDeviceSelection{physicalDevice, *queueFamilyIndex, dynamicRenderingPath};
    return result;
  }
  return result;
}

// Move-only RAII guards for partial-construction cleanup. Declared and
// used in createDevice() strictly in creation order, so C++'s automatic
// reverse-destruction-order tears them down device -> messenger ->
// instance on any early-return failure -- exactly the order Section 6's
// destruction-boundary table requires. Each guard still owns its handle
// (and will destroy it on scope exit) through get()'s non-owning read;
// only release() relinquishes ownership, and createDevice() below calls
// release() only after VulkanDevice has already been successfully
// constructed and holds its own copy of every handle value -- never
// earlier, and never as a side effect of evaluating a constructor's
// argument list.

class InstanceGuard {
 public:
  explicit InstanceGuard(VkInstance instance) : instance_(instance) {}
  ~InstanceGuard() {
    if (instance_ != VK_NULL_HANDLE) {
      vkDestroyInstance(instance_, nullptr);
    }
  }
  InstanceGuard(const InstanceGuard&) = delete;
  InstanceGuard& operator=(const InstanceGuard&) = delete;

  [[nodiscard]] VkInstance get() const noexcept { return instance_; }
  [[nodiscard]] VkInstance release() noexcept {
    VkInstance released = instance_;
    instance_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkInstance instance_;
};

class MessengerGuard {
 public:
  MessengerGuard(VkInstance instance, VkDebugUtilsMessengerEXT messenger,
                 PFN_vkDestroyDebugUtilsMessengerEXT destroyFn)
      : instance_(instance), messenger_(messenger), destroyFn_(destroyFn) {}
  ~MessengerGuard() {
    if (messenger_ != VK_NULL_HANDLE) {
      ATLANTIS_CHECK(destroyFn_ != nullptr);
      destroyFn_(instance_, messenger_, nullptr);
    }
  }
  MessengerGuard(const MessengerGuard&) = delete;
  MessengerGuard& operator=(const MessengerGuard&) = delete;

  [[nodiscard]] VkDebugUtilsMessengerEXT get() const noexcept { return messenger_; }
  [[nodiscard]] VkDebugUtilsMessengerEXT release() noexcept {
    VkDebugUtilsMessengerEXT released = messenger_;
    messenger_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkInstance instance_;
  VkDebugUtilsMessengerEXT messenger_;
  PFN_vkDestroyDebugUtilsMessengerEXT destroyFn_;
};

class DeviceGuard {
 public:
  explicit DeviceGuard(VkDevice device) : device_(device) {}
  ~DeviceGuard() {
    if (device_ != VK_NULL_HANDLE) {
      vkDestroyDevice(device_, nullptr);
    }
  }
  DeviceGuard(const DeviceGuard&) = delete;
  DeviceGuard& operator=(const DeviceGuard&) = delete;

  [[nodiscard]] VkDevice get() const noexcept { return device_; }
  [[nodiscard]] VkDevice release() noexcept {
    VkDevice released = device_;
    device_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkDevice device_;
};

// Plan 0006 Step 4: guards for VulkanDevice's three new persistent
// objects (command pool, render-finished semaphore, submission fence),
// same partial-construction-cleanup pattern as the three guards above.
class CommandPoolGuard {
 public:
  CommandPoolGuard(VkDevice device, VkCommandPool commandPool) : device_(device), commandPool_(commandPool) {}
  ~CommandPoolGuard() {
    if (commandPool_ != VK_NULL_HANDLE) {
      vkDestroyCommandPool(device_, commandPool_, nullptr);
    }
  }
  CommandPoolGuard(const CommandPoolGuard&) = delete;
  CommandPoolGuard& operator=(const CommandPoolGuard&) = delete;

  [[nodiscard]] VkCommandPool get() const noexcept { return commandPool_; }
  [[nodiscard]] VkCommandPool release() noexcept {
    VkCommandPool released = commandPool_;
    commandPool_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkDevice device_;
  VkCommandPool commandPool_;
};

class FenceGuard {
 public:
  FenceGuard(VkDevice device, VkFence fence) : device_(device), fence_(fence) {}
  ~FenceGuard() {
    if (fence_ != VK_NULL_HANDLE) {
      vkDestroyFence(device_, fence_, nullptr);
    }
  }
  FenceGuard(const FenceGuard&) = delete;
  FenceGuard& operator=(const FenceGuard&) = delete;

  [[nodiscard]] VkFence get() const noexcept { return fence_; }
  [[nodiscard]] VkFence release() noexcept {
    VkFence released = fence_;
    fence_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkDevice device_;
  VkFence fence_;
};

// Plan 0007 Section 10 / Spec 0021 D9: guard for a single VkDescriptorPool
// -- used only for createDevice()'s own one-time, construction-time
// initial pool (generation 0). VulkanDevice's own later growth path
// (allocateDescriptorSet()) does not use this guard: with the fixed-
// array-based pool set, the publish step (an array-slot assignment plus
// a count increment) is the literal next statement after pool creation
// succeeds, with zero intervening fallible operation, so the "guarded
// until published" property holds by direct construction of that code
// -- see Plan 0021's own "Pool RAII and publish order" note.
class DescriptorPoolGuard {
 public:
  DescriptorPoolGuard(VkDevice device, VkDescriptorPool descriptorPool) : device_(device), pool_(descriptorPool) {}
  ~DescriptorPoolGuard() {
    if (pool_ != VK_NULL_HANDLE) {
      vkDestroyDescriptorPool(device_, pool_, nullptr);
    }
  }
  DescriptorPoolGuard(const DescriptorPoolGuard&) = delete;
  DescriptorPoolGuard& operator=(const DescriptorPoolGuard&) = delete;

  [[nodiscard]] VkDescriptorPool get() const noexcept { return pool_; }
  [[nodiscard]] VkDescriptorPool release() noexcept {
    VkDescriptorPool released = pool_;
    pool_ = VK_NULL_HANDLE;
    return released;
  }

 private:
  VkDevice device_;
  VkDescriptorPool pool_;
};

// Spec 0021/ADR-0064, Plan 0021 P6: the single source of truth for "what
// one descriptor pool in VulkanDevice's own growable set looks like" --
// called by BOTH createDevice()'s own one-time initial-pool creation
// (generation 0, below) AND VulkanDevice::allocateDescriptorSet()'s own
// runtime growth path (generations 1-3) -- the two call sites cannot
// drift apart from each other because they execute the same code.
// VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT is required (not
// optional) so vkFreeDescriptorSets (VulkanPipeline's destructor) is
// valid usage; both descriptor types are always sized equal to maxSets
// itself (Spec 0021 D4/P8's own derivation: every real descriptor set
// consumes at most one UNIFORM_BUFFER descriptor and at most three
// COMBINED_IMAGE_SAMPLER descriptors, so the latter receives three
// descriptors per set (Spec 0025/P2). Returns
// VK_NULL_HANDLE on vkCreateDescriptorPool failure -- this function
// itself makes no judgment about how a caller should map that; each
// call site below does.
[[nodiscard]] VkDescriptorPool createDescriptorPoolOfSize(VkDevice device, std::uint32_t maxSets) {
  VkDescriptorPoolSize poolSizes[2]{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = maxSets;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = 3U * maxSets;

  VkDescriptorPoolCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  createInfo.maxSets = maxSets;
  createInfo.poolSizeCount = 2;
  createInfo.pPoolSizes = poolSizes;

  VkDescriptorPool pool = VK_NULL_HANDLE;
  if (vkCreateDescriptorPool(device, &createInfo, nullptr, &pool) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  return pool;
}

}  // namespace

VulkanDevice::VulkanDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
                            std::uint32_t queueFamilyIndex, VkDebugUtilsMessengerEXT explicitMessenger,
                            PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn, VkCommandPool commandPool,
                            VkFence submissionFence, PFN_vkCmdBeginRenderingKHR cmdBeginRendering,
                            PFN_vkCmdEndRenderingKHR cmdEndRendering, VkDescriptorPool descriptorPool)
    : instance_(instance),
      physicalDevice_(physicalDevice),
      device_(device),
      queue_(queue),
      queueFamilyIndex_(queueFamilyIndex),
      explicitMessenger_(explicitMessenger),
      destroyMessengerFn_(destroyMessengerFn),
      commandPool_(commandPool),
      submissionFence_(submissionFence),
      cmdBeginRendering_(cmdBeginRendering),
      cmdEndRendering_(cmdEndRendering) {
  // Spec 0021 D5: the first pool (generation 0) is always sized to
  // kDescriptorPoolMaxSetsByGeneration[0] -- createDevice() already
  // created it via that exact value (see createDescriptorPoolOfSize()'s
  // own call site there), so this simply publishes the already-created
  // pool as this growable set's own first, live entry.
  descriptorPools_[0] = DescriptorPoolEntry{descriptorPool, kDescriptorPoolMaxSetsByGeneration[0]};
  descriptorPoolCount_ = 1;
}

VulkanDevice::~VulkanDevice() {
  // Plan 0006 Section 9: defensive drain, run unconditionally. The
  // documented, tested discipline is that a caller calls waitIdle()
  // explicitly and checks its Result before destroying Presentation/
  // Device on every exit path; this fallback exists only to prevent a
  // crash if that discipline is ever violated, not to make explicit
  // draining optional. Failure here cannot be returned from a
  // destructor, so it is logged and swallowed.
  const auto drainResult = waitAndReleaseRetainedSubmission();
  if (drainResult.isErr()) {
    ATLANTIS_LOG_ERROR("~VulkanDevice(): waitAndReleaseRetainedSubmission() failed during destruction");
  }
  if (vkDeviceWaitIdle(device_) != VK_SUCCESS) {
    ATLANTIS_LOG_ERROR("~VulkanDevice(): vkDeviceWaitIdle() failed during destruction");
  }

  vkDestroyFence(device_, submissionFence_, nullptr);
  vkDestroyCommandPool(device_, commandPool_, nullptr);
  // Plan 0007 Section 10 / Spec 0021 D10: destroyed before VkDevice,
  // after every VulkanPipeline that could have held a VkDescriptorSet
  // from any of these pools -- caller discipline (Section 14), same
  // tier as VkCommandPool above, unchanged by the growable-set widening.
  // Bounded by descriptorPoolCount_ -- every live pool destroyed exactly
  // once; order among them is immaterial (no pool depends on another,
  // matching this file's own Phase-2 release-order precedent below).
  for (std::size_t i = 0; i < descriptorPoolCount_; ++i) {
    vkDestroyDescriptorPool(device_, descriptorPools_[i].pool, nullptr);
  }

  // Destruction order: the three objects above, then VkDevice, then the
  // explicit VkDebugUtilsMessengerEXT (if any), then VkInstance -- matches
  // Section 6's destruction-boundary table and this file's guard-based
  // creation-order pattern.
  vkDestroyDevice(device_, nullptr);
  if (explicitMessenger_ != VK_NULL_HANDLE) {
    ATLANTIS_CHECK(destroyMessengerFn_ != nullptr);
    destroyMessengerFn_(instance_, explicitMessenger_, nullptr);
  }
  vkDestroyInstance(instance_, nullptr);
}

atlantis::Result<std::monostate, atlantis::rhi::SubmitError> VulkanDevice::waitAndReleaseRetainedSubmission() {
  using ResultT = atlantis::Result<std::monostate, atlantis::rhi::SubmitError>;

  if (!hasRetainedSubmission_) {
    return ResultT::Ok({});
  }

  const VkResult waitResult = vkWaitForFences(device_, 1, &submissionFence_, VK_TRUE, UINT64_MAX);
  if (waitResult != VK_SUCCESS) {
    return ResultT::Err(toSubmitError(waitResult));
  }
  const VkResult resetResult = vkResetFences(device_, 1, &submissionFence_);
  if (resetResult != VK_SUCCESS) {
    return ResultT::Err(toSubmitError(resetResult));
  }

  retainedSubmission_.reset();
  hasRetainedSubmission_ = false;
  return ResultT::Ok({});
}

atlantis::Result<std::unique_ptr<atlantis::rhi::CommandList>, atlantis::rhi::CommandListCreateError>
VulkanDevice::createCommandList() {
  using ResultT =
      atlantis::Result<std::unique_ptr<atlantis::rhi::CommandList>, atlantis::rhi::CommandListCreateError>;

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool_;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
  const VkResult allocResult = vkAllocateCommandBuffers(device_, &allocInfo, &commandBuffer);
  if (allocResult != VK_SUCCESS) {
    return ResultT::Err(toCommandListCreateError(allocResult));
  }

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  const VkResult beginResult = vkBeginCommandBuffer(commandBuffer, &beginInfo);
  if (beginResult != VK_SUCCESS) {
    vkFreeCommandBuffers(device_, commandPool_, 1, &commandBuffer);
    return ResultT::Err(toCommandListCreateError(beginResult));
  }

  return ResultT::Ok(
      std::make_unique<VulkanCommandList>(device_, commandPool_, commandBuffer, cmdBeginRendering_, cmdEndRendering_));
}

atlantis::Result<std::unique_ptr<atlantis::rhi::SubmissionSignal>, atlantis::rhi::SubmitError> VulkanDevice::submit(
    std::unique_ptr<atlantis::rhi::CommandList> commandList, const atlantis::rhi::RenderTarget& target) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::SubmissionSignal>, atlantis::rhi::SubmitError>;

  auto& vulkanCommandList = static_cast<VulkanCommandList&>(*commandList);
  const VkResult endResult = vkEndCommandBuffer(vulkanCommandList.commandBuffer());
  if (endResult != VK_SUCCESS) {
    return ResultT::Err(toSubmitError(endResult));
  }

  const auto drainResult = waitAndReleaseRetainedSubmission();
  if (drainResult.isErr()) {
    return ResultT::Err(drainResult.error());
  }

  // Pointer-form, exception-free (Spec 0010/ADR-0038): target may now be
  // a VulkanRenderTarget or a VulkanOffscreenRenderTarget -- a mismatched
  // type here is a programmer error (a RenderTarget this module itself
  // did not produce, ADR-0014), caught by this assertion, never by a
  // thrown std::bad_cast reaching the render path. Mirrors the existing
  // vulkan_presentation.cpp:createPresentation() pointer-form-dynamic_cast
  // precedent.
  const auto* access = dynamic_cast<const VulkanRenderTargetAccess*>(&target);
  ATLANTIS_CHECK_MSG(access != nullptr, "submit() received a RenderTarget not produced by this module");
  const VkSemaphore waitSemaphore = access->acquireCompleteSemaphore();
  // Plan 0006 (found via GPU testing): read per-image-index render-
  // finished semaphore from the target itself, rather than a single
  // Device-owned one -- see VulkanPresentation's own header comment for
  // why a single shared semaphore is not safe to reuse across frames.
  // VK_NULL_HANDLE (Spec 0010: an offscreen target's own accessors) is a
  // legal "nothing to wait on / signal" value, handled by the conditional
  // VkSubmitInfo construction below -- never passed as a wait/signal
  // semaphore entry itself.
  const VkSemaphore signalSemaphore = access->renderFinishedSemaphore();
  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  const VkCommandBuffer commandBuffer = vulkanCommandList.commandBuffer();

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = waitSemaphore != VK_NULL_HANDLE ? 1 : 0;
  submitInfo.pWaitSemaphores = waitSemaphore != VK_NULL_HANDLE ? &waitSemaphore : nullptr;
  submitInfo.pWaitDstStageMask = waitSemaphore != VK_NULL_HANDLE ? &waitStage : nullptr;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  submitInfo.signalSemaphoreCount = signalSemaphore != VK_NULL_HANDLE ? 1 : 0;
  submitInfo.pSignalSemaphores = signalSemaphore != VK_NULL_HANDLE ? &signalSemaphore : nullptr;

  const VkResult submitResult = vkQueueSubmit(queue_, 1, &submitInfo, submissionFence_);
  if (submitResult != VK_SUCCESS) {
    return ResultT::Err(toSubmitError(submitResult));
  }

  retainedSubmission_ = std::move(commandList);
  hasRetainedSubmission_ = true;

  return ResultT::Ok(std::make_unique<VulkanSubmissionSignal>(signalSemaphore));
}

atlantis::Result<std::monostate, atlantis::rhi::SubmitError> VulkanDevice::waitIdle() {
  using ResultT = atlantis::Result<std::monostate, atlantis::rhi::SubmitError>;

  const auto drainResult = waitAndReleaseRetainedSubmission();
  if (drainResult.isErr()) {
    return drainResult;
  }

  // Belt-and-suspenders: also drains any presentation-engine-internal
  // work not tracked by submissionFence_ -- e.g. an acquired-but-never-
  // submitted RenderTarget's acquire semaphore on a mid-frame-exit path
  // (Plan 0006 Section 11).
  const VkResult idleResult = vkDeviceWaitIdle(device_);
  if (idleResult != VK_SUCCESS) {
    return ResultT::Err(toSubmitError(idleResult));
  }
  return ResultT::Ok({});
}

namespace {

// Spec 0007 Section 9's shared allocation sequence, factored out since
// VulkanDevice::createBuffer()/createTexture() each need it independently
// (not a shared allocation object -- ADR-0023's own "no pooling" note).
[[nodiscard]] std::optional<std::uint32_t> selectMemoryTypeIndexForDevice(VkPhysicalDevice physicalDevice,
                                                                            std::uint32_t typeFilterBits,
                                                                            VkMemoryPropertyFlags requiredProperties) {
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
  return selectMemoryTypeIndex(memoryProperties, typeFilterBits, requiredProperties);
}

[[nodiscard]] VkFormat toVkFormat(atlantis::rhi::Format format) {
  switch (format) {
    case atlantis::rhi::Format::Bgra8Unorm:
      return VK_FORMAT_B8G8R8A8_UNORM;
    case atlantis::rhi::Format::Bgra8Srgb:
      return VK_FORMAT_B8G8R8A8_SRGB;
    case atlantis::rhi::Format::Rgba8Unorm:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case atlantis::rhi::Format::Rgba8Srgb:
      return VK_FORMAT_R8G8B8A8_SRGB;
    case atlantis::rhi::Format::Unknown:
      break;
  }
  ATLANTIS_CHECK_MSG(false, "toVkFormat() called with Format::Unknown");
  return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkFormat toVkFormat(atlantis::rhi::DepthFormat format) {
  switch (format) {
    case atlantis::rhi::DepthFormat::D32Sfloat:
      return VK_FORMAT_D32_SFLOAT;
  }
  ATLANTIS_CHECK_MSG(false, "toVkFormat(DepthFormat) called with an unhandled enumerator");
  return VK_FORMAT_UNDEFINED;
}

[[nodiscard]] VkFormat vertexAttributeFormatToVkFormat(atlantis::rhi::VertexAttributeFormat format) {
  switch (format) {
    case atlantis::rhi::VertexAttributeFormat::Float3:
      return VK_FORMAT_R32G32B32_SFLOAT;
    case atlantis::rhi::VertexAttributeFormat::Float2:
      return VK_FORMAT_R32G32_SFLOAT;
  }
  ATLANTIS_CHECK_MSG(false, "vertexAttributeFormatToVkFormat() called with an unhandled enumerator");
  return VK_FORMAT_UNDEFINED;
}

// Spec 0016: SampledTexture's own format vocabulary is deliberately
// separate from Texture's depth-only Format (D2's "depth-only Texture
// stays unchanged" constraint), so it gets its own toVkFormat() overload
// rather than extending the existing one.
[[nodiscard]] VkFormat toVkFormat(atlantis::rhi::SampledTextureFormat format) {
  switch (format) {
    case atlantis::rhi::SampledTextureFormat::Rgba8Unorm:
      return VK_FORMAT_R8G8B8A8_UNORM;
    case atlantis::rhi::SampledTextureFormat::Rgba8Srgb:
      return VK_FORMAT_R8G8B8A8_SRGB;
    case atlantis::rhi::SampledTextureFormat::Rgba16Float:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
    case atlantis::rhi::SampledTextureFormat::Rg16Float:
      return VK_FORMAT_R16G16_SFLOAT;
  }
  ATLANTIS_CHECK_MSG(false, "toVkFormat(SampledTextureFormat) called with an unhandled enumerator");
  return VK_FORMAT_UNDEFINED;
}

// Plan 0024 Milestone 1 (ADR-0068 D-2): one variant this round, a
// separate overload from every toVkFormat() above, mirroring
// SampledTextureFormat's own identical "own vocabulary, own overload"
// precedent -- HdrFormat is not a Format value.
[[nodiscard]] VkFormat toVkFormat(atlantis::rhi::HdrFormat format) {
  switch (format) {
    case atlantis::rhi::HdrFormat::Rgba16Float:
      return VK_FORMAT_R16G16B16A16_SFLOAT;
  }
  ATLANTIS_CHECK_MSG(false, "toVkFormat(HdrFormat) called with an unhandled enumerator");
  return VK_FORMAT_UNDEFINED;
}

// Plan 0024 Milestone 8: hasRequiredHdrColorTargetFeatures() moved to
// its own private header/source (hdr_color_target_capability.h/.cpp),
// mirroring resource_state_mapping.h's own identical "pure function,
// unit-testable without Vulkan" shape -- the anonymous namespace it
// used to live in (translation-unit-private) could not be reached by a
// GPU-independent test in a different .cpp file.

[[nodiscard]] VkFilter toVkFilter(atlantis::rhi::Filter filter) {
  switch (filter) {
    case atlantis::rhi::Filter::Nearest:
      return VK_FILTER_NEAREST;
    case atlantis::rhi::Filter::Linear:
      return VK_FILTER_LINEAR;
  }
  ATLANTIS_CHECK_MSG(false, "toVkFilter() called with an unhandled enumerator");
  return VK_FILTER_NEAREST;
}

[[nodiscard]] VkSamplerAddressMode toVkSamplerAddressMode(atlantis::rhi::AddressMode addressMode) {
  switch (addressMode) {
    case atlantis::rhi::AddressMode::Repeat:
      return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case atlantis::rhi::AddressMode::ClampToEdge:
      return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  }
  ATLANTIS_CHECK_MSG(false, "toVkSamplerAddressMode() called with an unhandled enumerator");
  return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

[[nodiscard]] VkSamplerMipmapMode toVkSamplerMipmapMode(atlantis::rhi::MipFilter filter) {
  switch (filter) {
    case atlantis::rhi::MipFilter::Nearest:
      return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case atlantis::rhi::MipFilter::Linear:
      return VK_SAMPLER_MIPMAP_MODE_LINEAR;
  }
  ATLANTIS_CHECK_MSG(false, "toVkSamplerMipmapMode() called with an unhandled enumerator");
  return VK_SAMPLER_MIPMAP_MODE_NEAREST;
}

}  // namespace

atlantis::Result<std::unique_ptr<atlantis::rhi::Buffer>, atlantis::rhi::BufferCreateError> VulkanDevice::createBuffer(
    const atlantis::rhi::BufferCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::Buffer>, atlantis::rhi::BufferCreateError>;

  VkBufferUsageFlags usage = 0;
  switch (params.purpose) {
    case atlantis::rhi::BufferPurpose::Vertex:
      usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
      break;
    case atlantis::rhi::BufferPurpose::Index:
      usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
      break;
    case atlantis::rhi::BufferPurpose::Uniform:
      usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      break;
    case atlantis::rhi::BufferPurpose::Readback:
      usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
      break;
    case atlantis::rhi::BufferPurpose::Staging:
      usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
      break;
  }

  VkBufferCreateInfo bufferCreateInfo{};
  bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferCreateInfo.size = params.sizeBytes;
  bufferCreateInfo.usage = usage;
  bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  VkBuffer buffer = VK_NULL_HANDLE;
  const VkResult createResult = vkCreateBuffer(device_, &bufferCreateInfo, nullptr, &buffer);
  if (createResult != VK_SUCCESS) {
    return ResultT::Err(toBufferCreateError(createResult));
  }

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device_, buffer, &requirements);

  const std::optional<std::uint32_t> memoryTypeIndex = selectMemoryTypeIndexForDevice(
      physicalDevice_, requirements.memoryTypeBits,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  if (!memoryTypeIndex.has_value()) {
    vkDestroyBuffer(device_, buffer, nullptr);
    return ResultT::Err(atlantis::rhi::BufferCreateError::AllocationFailed);
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex = *memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  const VkResult allocResult = vkAllocateMemory(device_, &allocInfo, nullptr, &memory);
  if (allocResult != VK_SUCCESS) {
    vkDestroyBuffer(device_, buffer, nullptr);
    return ResultT::Err(atlantis::rhi::BufferCreateError::AllocationFailed);
  }

  const VkResult bindResult = vkBindBufferMemory(device_, buffer, memory, 0);
  if (bindResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyBuffer(device_, buffer, nullptr);
    return ResultT::Err(toBufferCreateError(bindResult));
  }

  void* mappedData = nullptr;
  const VkResult mapResult = vkMapMemory(device_, memory, 0, VK_WHOLE_SIZE, 0, &mappedData);
  if (mapResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyBuffer(device_, buffer, nullptr);
    return ResultT::Err(toBufferCreateError(mapResult));
  }

  return ResultT::Ok(
      std::make_unique<VulkanBuffer>(device_, buffer, memory, mappedData, params.purpose, params.sizeBytes));
}

atlantis::Result<std::unique_ptr<atlantis::rhi::Texture>, atlantis::rhi::TextureCreateError>
VulkanDevice::createTexture(const atlantis::rhi::TextureCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::Texture>, atlantis::rhi::TextureCreateError>;

  const VkFormat vkFormat = toVkFormat(params.format);

  VkImageCreateInfo imageCreateInfo{};
  imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.format = vkFormat;
  imageCreateInfo.extent = VkExtent3D{params.extent.width, params.extent.height, 1};
  imageCreateInfo.mipLevels = 1;
  imageCreateInfo.arrayLayers = 1;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage image = VK_NULL_HANDLE;
  const VkResult createResult = vkCreateImage(device_, &imageCreateInfo, nullptr, &image);
  if (createResult != VK_SUCCESS) {
    return ResultT::Err(toTextureCreateError(createResult));
  }

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, image, &requirements);

  const std::optional<std::uint32_t> memoryTypeIndex = selectMemoryTypeIndexForDevice(
      physicalDevice_, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!memoryTypeIndex.has_value()) {
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::TextureCreateError::AllocationFailed);
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex = *memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  const VkResult allocResult = vkAllocateMemory(device_, &allocInfo, nullptr, &memory);
  if (allocResult != VK_SUCCESS) {
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::TextureCreateError::AllocationFailed);
  }

  const VkResult bindResult = vkBindImageMemory(device_, image, memory, 0);
  if (bindResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(toTextureCreateError(bindResult));
  }

  VkImageViewCreateInfo viewCreateInfo{};
  viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewCreateInfo.image = image;
  viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewCreateInfo.format = vkFormat;
  viewCreateInfo.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};

  VkImageView imageView = VK_NULL_HANDLE;
  const VkResult viewResult = vkCreateImageView(device_, &viewCreateInfo, nullptr, &imageView);
  if (viewResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::TextureCreateError::ImageViewCreationFailed);
  }

  return ResultT::Ok(
      std::make_unique<VulkanTexture>(device_, image, memory, imageView, params.extent, params.format));
}

std::optional<atlantis::rhi::PipelineCreateError> VulkanDevice::allocateDescriptorSet(
    VkDescriptorSetLayout layout, VkDescriptorSet& outDescriptorSet, VkDescriptorPool& outOriginPool) {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  // Step 1: scan every existing (live) pool, in creation order --
  // naturally reusing whatever capacity an earlier vkFreeDescriptorSets
  // call already returned to any of them (Spec 0021 D3/D7), never
  // assuming only the most-recently-created pool can have room.
  for (std::size_t i = 0; i < descriptorPoolCount_; ++i) {
    allocInfo.descriptorPool = descriptorPools_[i].pool;
    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, &set);
    if (result == VK_SUCCESS) {
      outDescriptorSet = set;
      outOriginPool = descriptorPools_[i].pool;
      return std::nullopt;
    }
    if (!isDescriptorPoolGrowthEligible(result)) {
      // VK_ERROR_DEVICE_LOST / host-or-device OOM -- immediate,
      // unchanged failure. No further pool tried, no growth attempted.
      return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
    }
    // OUT_OF_POOL_MEMORY / FRAGMENTED_POOL -- try the next existing pool.
  }

  // Step 2: every existing pool exhausted for a growth-eligible reason.
  if (descriptorPoolCount_ >= kMaxDescriptorPoolCount) {
    return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
  }

  // Step 3: grow -- create exactly one new pool, generation
  // descriptorPoolCount_ (the fixed generation table, never computed).
  const std::uint32_t newMaxSets = descriptorPoolMaxSetsForGeneration(descriptorPoolCount_);
  const VkDescriptorPool newPool = createDescriptorPoolOfSize(device_, newMaxSets);
  if (newPool == VK_NULL_HANDLE) {
    // Pre-publish failure -- descriptorPoolCount_ is NOT incremented;
    // no handle exists to leak.
    return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
  }
  // Publish: a plain assignment into an already-allocated array slot,
  // immediately following pool creation with zero intervening fallible
  // operation -- cannot throw (DescriptorPoolEntry's own members are
  // trivial), cannot leave newPool unpublished. Kept in descriptorPools_
  // regardless of the retry's own outcome below (Spec 0021 D9) -- safe,
  // since no set has been allocated from it yet.
  descriptorPools_[descriptorPoolCount_] = DescriptorPoolEntry{newPool, newMaxSets};
  ++descriptorPoolCount_;

  // Step 4: retry exactly once against the new pool. No loop, no
  // recursive call back into this function -- this is the ONE retry
  // Spec 0021 D3 specifies, never a second growth attempt within the
  // same call.
  allocInfo.descriptorPool = newPool;
  VkDescriptorSet set = VK_NULL_HANDLE;
  const VkResult retryResult = vkAllocateDescriptorSets(device_, &allocInfo, &set);
  if (retryResult != VK_SUCCESS) {
    return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
  }
  outDescriptorSet = set;
  outOriginPool = newPool;
  return std::nullopt;
}

atlantis::Result<std::unique_ptr<atlantis::rhi::Pipeline>, atlantis::rhi::PipelineCreateError>
VulkanDevice::createPipeline(const atlantis::rhi::PipelineCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::Pipeline>, atlantis::rhi::PipelineCreateError>;

  ATLANTIS_CHECK(params.sampledTextureBindingCount == 0 || params.sampledTextureBindingCount == 1 ||
                 params.sampledTextureBindingCount == 3);

  auto createShaderModule = [this](const atlantis::rhi::ShaderStageBytecode& bytecode,
                                    VkShaderModule& outModule) -> VkResult {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = bytecode.wordCount * sizeof(std::uint32_t);
    createInfo.pCode = bytecode.spirvWords;
    return vkCreateShaderModule(device_, &createInfo, nullptr, &outModule);
  };

  VkShaderModule vertexModule = VK_NULL_HANDLE;
  const VkResult vertexModuleResult = createShaderModule(params.vertexShader, vertexModule);
  if (vertexModuleResult != VK_SUCCESS) {
    return ResultT::Err(atlantis::rhi::PipelineCreateError::ShaderModuleCreationFailed);
  }
  VkShaderModule fragmentModule = VK_NULL_HANDLE;
  const VkResult fragmentModuleResult = createShaderModule(params.fragmentShader, fragmentModule);
  if (fragmentModuleResult != VK_SUCCESS) {
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    return ResultT::Err(atlantis::rhi::PipelineCreateError::ShaderModuleCreationFailed);
  }

  // Camera uniform binding -- Plan 0007 Section 10's fixed, single-purpose
  // design: exactly one binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER.
  // Plan 0019 Section P12 / ADR-0062 Decision 2: stageFlags widened,
  // unconditionally, from vertex-only to vertex-and-fragment, for every
  // Pipeline this engine creates -- the one real RHI-internal change
  // Lighting Foundation requires (the frame lighting data, appended
  // after the existing camera floats in this same buffer, is read from
  // the fragment stage by lit_textured's own shader). Legal, zero-cost
  // Vulkan; every existing shader (minimal_mesh, textured_quad) simply
  // continues not referencing this binding from its own fragment stage,
  // unaffected.
  VkDescriptorSetLayoutBinding uniformBinding{};
  uniformBinding.binding = 0;
  uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uniformBinding.descriptorCount = 1;
  uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

  // Spec 0016 D5: the second, combined-image-sampler binding a textured
  // Material's pipeline needs -- present only when
  // params.sampledTextureBindingCount is non-zero (an uniform-only Material's
  // pipeline keeps the single-binding layout above unchanged, per D5a's
  // "uniform-only Material behavior unchanged" constraint).
  //
  // Plan 0024 Milestone 6 (discovered during Implementation, Human
  // Review direction 2026-09-01 -- see PipelineCreateParams::
  // hasCameraUniformBinding's own comment, types.h): this binding's own
  // slot moves from index 1 to index 0 when the caller omits the
  // uniform binding entirely (params.hasCameraUniformBinding == false)
  // -- the exact, only shape the new output-transform descriptor
  // contract (ADR-0068 D-10) needs: one Sampler, at binding 0, no
  // uniform buffer.
  // Built as a small, ordered vector rather than a fixed 2-slot array --
  // params.hasCameraUniformBinding == false (new this Milestone) means
  // the uniform binding is not merely unused but genuinely ABSENT from
  // the layout, not just excluded from the count with a stale pointer
  // still referencing it.
  std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
  if (params.hasCameraUniformBinding) setLayoutBindings.push_back(uniformBinding);
  const std::uint32_t sampledTextureFirstBinding = params.hasCameraUniformBinding ? 1U : 0U;
  for (std::uint32_t index = 0; index < params.sampledTextureBindingCount; ++index) {
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = sampledTextureFirstBinding + index;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    setLayoutBindings.push_back(binding);
  }

  VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo{};
  setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  setLayoutCreateInfo.bindingCount = static_cast<std::uint32_t>(setLayoutBindings.size());
  setLayoutCreateInfo.pBindings = setLayoutBindings.data();

  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  const VkResult setLayoutResult =
      vkCreateDescriptorSetLayout(device_, &setLayoutCreateInfo, nullptr, &descriptorSetLayout);
  if (setLayoutResult != VK_SUCCESS) {
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    return ResultT::Err(atlantis::rhi::PipelineCreateError::DescriptorSetLayoutCreationFailed);
  }

  // Spec 0021/ADR-0064: scans the growable pool set, growing (up to the
  // Approved hard ceiling) on real, observed exhaustion -- see
  // allocateDescriptorSet()'s own doc comment for the full algorithm.
  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  VkDescriptorPool originPool = VK_NULL_HANDLE;
  if (const auto allocError = allocateDescriptorSet(descriptorSetLayout, descriptorSet, originPool);
      allocError.has_value()) {
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    return ResultT::Err(*allocError);
  }

  VkPushConstantRange pushConstantRange{};
  // Plan 0023 Milestone 3 (ADR-0067 D-4): uniformly widened to VERTEX |
  // FRAGMENT for every Pipeline, unconditionally -- not gated by
  // MaterialKind. A Vulkan-Backend-private implementation change only;
  // confirmed safe for UnlitTextured/LitTextured (neither shader's
  // fragment stage declares or reads push-constant data) and required
  // by PbrDirectLit, whose fragment stage genuinely reads it.
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = static_cast<std::uint32_t>(params.pushConstantSizeBytes);

  VkPipelineLayoutCreateInfo layoutCreateInfo{};
  layoutCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutCreateInfo.setLayoutCount = 1;
  layoutCreateInfo.pSetLayouts = &descriptorSetLayout;
  layoutCreateInfo.pushConstantRangeCount = params.pushConstantSizeBytes > 0 ? 1u : 0u;
  layoutCreateInfo.pPushConstantRanges = params.pushConstantSizeBytes > 0 ? &pushConstantRange : nullptr;

  VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
  const VkResult layoutResult = vkCreatePipelineLayout(device_, &layoutCreateInfo, nullptr, &pipelineLayout);
  if (layoutResult != VK_SUCCESS) {
    // vkFreeDescriptorSets is documented to only ever return VK_SUCCESS
    // when the pool was created with FREE_DESCRIPTOR_SET_BIT (every pool
    // in this Device's own set is, unconditionally) -- checked anyway,
    // per this module's own "every VkResult is checked" rule with no
    // silent exceptions, matching VulkanPipeline's own destructor.
    const VkResult freeResult = vkFreeDescriptorSets(device_, originPool, 1, &descriptorSet);
    ATLANTIS_CHECK(freeResult == VK_SUCCESS);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    return ResultT::Err(atlantis::rhi::PipelineCreateError::PipelineLayoutCreationFailed);
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vertexModule;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fragmentModule;
  stages[1].pName = "main";

  VkVertexInputBindingDescription bindingDescription{};
  bindingDescription.binding = 0;
  bindingDescription.stride = params.vertexInputLayout.strideBytes;
  bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

  std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
  attributeDescriptions.reserve(params.vertexInputLayout.attributes.size());
  for (const auto& attribute : params.vertexInputLayout.attributes) {
    VkVertexInputAttributeDescription description{};
    description.location = attribute.location;
    description.binding = 0;
    description.format = vertexAttributeFormatToVkFormat(attribute.format);
    description.offset = attribute.offsetBytes;
    attributeDescriptions.push_back(description);
  }

  VkPipelineVertexInputStateCreateInfo vertexInputState{};
  vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
  vertexInputState.vertexBindingDescriptionCount = 1;
  vertexInputState.pVertexBindingDescriptions = &bindingDescription;
  vertexInputState.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributeDescriptions.size());
  vertexInputState.pVertexAttributeDescriptions = attributeDescriptions.data();

  VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{};
  inputAssemblyState.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssemblyState.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rasterizationState{};
  rasterizationState.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
  rasterizationState.cullMode = VK_CULL_MODE_NONE;
  rasterizationState.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rasterizationState.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisampleState{};
  multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  // Plan 0024 Milestone 6 (discovered during Implementation, Human
  // Review direction 2026-09-01 -- see PipelineCreateParams::
  // hasDepthAttachment's own comment, types.h): both fields default to
  // their existing unconditional values; params.hasDepthAttachment ==
  // false (the output-transform Pipeline pair alone) disables depth
  // test/write entirely, matching that pass's own real, depth-attachment-
  // free VkRenderingInfo.
  VkPipelineDepthStencilStateCreateInfo depthStencilState{};
  depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilState.depthTestEnable = params.hasDepthAttachment ? VK_TRUE : VK_FALSE;
  depthStencilState.depthWriteEnable = params.hasDepthAttachment ? VK_TRUE : VK_FALSE;
  depthStencilState.depthCompareOp = VK_COMPARE_OP_LESS;

  VkPipelineColorBlendAttachmentState colorBlendAttachment{};
  colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                         VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  colorBlendAttachment.blendEnable = VK_FALSE;

  VkPipelineColorBlendStateCreateInfo colorBlendState{};
  colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlendState.attachmentCount = 1;
  colorBlendState.pAttachments = &colorBlendAttachment;

  const VkDynamicState dynamicStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dynamicState{};
  dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
  dynamicState.dynamicStateCount = 2;
  dynamicState.pDynamicStates = dynamicStates;

  // Plan 0024 Milestone 1 (ADR-0068 D-2/D-4): params.colorFormat is a
  // structural std::variant<Format, HdrFormat> -- std::visit resolves
  // to whichever toVkFormat() overload matches the alternative
  // actually held, by ordinary overload resolution. Every geometry
  // Pipeline now holds HdrFormat::Rgba16Float here; the output-
  // transform Pipeline pair alone holds a real Format.
  const VkFormat colorFormat =
      std::visit([](auto format) { return toVkFormat(format); }, params.colorFormat);
  // Plan 0024 Milestone 6 (see hasDepthAttachment's own comment,
  // types.h): VK_FORMAT_UNDEFINED when this Pipeline has no depth
  // attachment at all -- matching the real, depth-attachment-free
  // VkRenderingInfo the output-transform pass's own beginRendering()
  // call site produces; toVkFormat(DepthFormat) is only ever called
  // when a real depth attachment format is actually needed.
  const VkFormat depthFormat = params.hasDepthAttachment ? toVkFormat(params.depthFormat) : VK_FORMAT_UNDEFINED;

  VkPipelineRenderingCreateInfo renderingCreateInfo{};
  renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
  renderingCreateInfo.colorAttachmentCount = 1;
  renderingCreateInfo.pColorAttachmentFormats = &colorFormat;
  renderingCreateInfo.depthAttachmentFormat = depthFormat;

  VkGraphicsPipelineCreateInfo pipelineCreateInfo{};
  pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  pipelineCreateInfo.pNext = &renderingCreateInfo;
  pipelineCreateInfo.stageCount = 2;
  pipelineCreateInfo.pStages = stages;
  pipelineCreateInfo.pVertexInputState = &vertexInputState;
  pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
  pipelineCreateInfo.pViewportState = &viewportState;
  pipelineCreateInfo.pRasterizationState = &rasterizationState;
  pipelineCreateInfo.pMultisampleState = &multisampleState;
  pipelineCreateInfo.pDepthStencilState = &depthStencilState;
  pipelineCreateInfo.pColorBlendState = &colorBlendState;
  pipelineCreateInfo.pDynamicState = &dynamicState;
  pipelineCreateInfo.layout = pipelineLayout;

  VkPipeline pipeline = VK_NULL_HANDLE;
  const VkResult pipelineResult =
      vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipelineCreateInfo, nullptr, &pipeline);

  // VkShaderModules are not needed after pipeline creation, regardless of
  // outcome (Plan 0007 Section 10).
  vkDestroyShaderModule(device_, fragmentModule, nullptr);
  vkDestroyShaderModule(device_, vertexModule, nullptr);

  if (pipelineResult != VK_SUCCESS) {
    vkDestroyPipelineLayout(device_, pipelineLayout, nullptr);
    // See the identical vkFreeDescriptorSets rationale above.
    const VkResult freeResult = vkFreeDescriptorSets(device_, originPool, 1, &descriptorSet);
    ATLANTIS_CHECK(freeResult == VK_SUCCESS);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
    return ResultT::Err(toPipelineCreateError(pipelineResult));
  }

  return ResultT::Ok(std::make_unique<VulkanPipeline>(device_, originPool, pipeline, pipelineLayout,
                                                        descriptorSetLayout, descriptorSet,
                                                        sampledTextureFirstBinding,
                                                        params.sampledTextureBindingCount));
}

atlantis::Result<std::unique_ptr<atlantis::rhi::OffscreenTarget>, atlantis::rhi::OffscreenTargetCreateError>
VulkanDevice::createOffscreenTarget(const atlantis::rhi::OffscreenTargetCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::OffscreenTarget>,
                                    atlantis::rhi::OffscreenTargetCreateError>;

  const VkFormat vkFormat = toVkFormat(params.format);

  VkImageCreateInfo imageCreateInfo{};
  imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.format = vkFormat;
  imageCreateInfo.extent = VkExtent3D{params.extent.width, params.extent.height, 1};
  imageCreateInfo.mipLevels = 1;
  imageCreateInfo.arrayLayers = 1;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  // Color-attachment for the draw pass, transfer-source for the readback
  // copy -- both required (Spec 0010 Requirements, ADR-0040's copy
  // contract).
  imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage image = VK_NULL_HANDLE;
  const VkResult createResult = vkCreateImage(device_, &imageCreateInfo, nullptr, &image);
  if (createResult != VK_SUCCESS) {
    return ResultT::Err(toOffscreenTargetCreateError(createResult));
  }

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, image, &requirements);

  // Device-local: an offscreen color target is GPU-written, GPU-read (by
  // the copy), never CPU-mapped directly -- only the separate readback
  // Buffer (createBuffer()'s BufferPurpose::Readback case) is host-visible.
  const std::optional<std::uint32_t> memoryTypeIndex = selectMemoryTypeIndexForDevice(
      physicalDevice_, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!memoryTypeIndex.has_value()) {
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::OffscreenTargetCreateError::AllocationFailed);
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex = *memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  const VkResult allocResult = vkAllocateMemory(device_, &allocInfo, nullptr, &memory);
  if (allocResult != VK_SUCCESS) {
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::OffscreenTargetCreateError::AllocationFailed);
  }

  const VkResult bindResult = vkBindImageMemory(device_, image, memory, 0);
  if (bindResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(toOffscreenTargetCreateError(bindResult));
  }

  VkImageViewCreateInfo viewCreateInfo{};
  viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewCreateInfo.image = image;
  viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewCreateInfo.format = vkFormat;
  viewCreateInfo.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  VkImageView imageView = VK_NULL_HANDLE;
  const VkResult viewResult = vkCreateImageView(device_, &viewCreateInfo, nullptr, &imageView);
  if (viewResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::OffscreenTargetCreateError::ImageViewCreationFailed);
  }

  return ResultT::Ok(
      std::make_unique<VulkanOffscreenTarget>(device_, image, memory, imageView, params.extent, params.format));
}

atlantis::Result<std::unique_ptr<atlantis::rhi::SampledTexture>, atlantis::rhi::SampledTextureCreateError>
VulkanDevice::createSampledTexture(const atlantis::rhi::SampledTextureCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::SampledTexture>,
                                    atlantis::rhi::SampledTextureCreateError>;

  ATLANTIS_CHECK(isValidSampledTextureCreateParams(params));

  const VkFormat vkFormat = toVkFormat(params.format);

  VkFormatProperties formatProperties{};
  vkGetPhysicalDeviceFormatProperties(physicalDevice_, vkFormat, &formatProperties);
  if (!hasRequiredSampledTextureFeatures(formatProperties.optimalTilingFeatures)) {
    return ResultT::Err(atlantis::rhi::SampledTextureCreateError::FormatFeaturesUnsupported);
  }

  VkImageCreateInfo imageCreateInfo{};
  imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.format = vkFormat;
  imageCreateInfo.extent = VkExtent3D{params.extent.width, params.extent.height, 1};
  const bool isCube = params.dimension == atlantis::rhi::SampledTextureDimension::TextureCube;
  imageCreateInfo.flags = isCube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
  imageCreateInfo.mipLevels = params.mipLevelCount;
  imageCreateInfo.arrayLayers = isCube ? 6U : 1U;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  // Shader-sampled, and the copy destination for buildTextureUploadPass()'s
  // buffer-to-image copy (Spec 0016 D4) -- both required.
  imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImageFormatProperties imageFormatProperties{};
  const VkResult imageFormatResult = vkGetPhysicalDeviceImageFormatProperties(
      physicalDevice_, vkFormat, imageCreateInfo.imageType, imageCreateInfo.tiling, imageCreateInfo.usage,
      imageCreateInfo.flags, &imageFormatProperties);
  if (imageFormatResult != VK_SUCCESS || params.extent.width > imageFormatProperties.maxExtent.width ||
      params.extent.height > imageFormatProperties.maxExtent.height ||
      params.mipLevelCount > imageFormatProperties.maxMipLevels ||
      imageCreateInfo.arrayLayers > imageFormatProperties.maxArrayLayers ||
      (imageFormatProperties.sampleCounts & VK_SAMPLE_COUNT_1_BIT) == 0) {
    return ResultT::Err(atlantis::rhi::SampledTextureCreateError::ImageFormatUnsupported);
  }

  VkImage image = VK_NULL_HANDLE;
  const VkResult createResult = vkCreateImage(device_, &imageCreateInfo, nullptr, &image);
  if (createResult != VK_SUCCESS) {
    return ResultT::Err(toSampledTextureCreateError(createResult));
  }

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, image, &requirements);

  const std::optional<std::uint32_t> memoryTypeIndex = selectMemoryTypeIndexForDevice(
      physicalDevice_, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!memoryTypeIndex.has_value()) {
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::SampledTextureCreateError::AllocationFailed);
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex = *memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  const VkResult allocResult = vkAllocateMemory(device_, &allocInfo, nullptr, &memory);
  if (allocResult != VK_SUCCESS) {
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::SampledTextureCreateError::AllocationFailed);
  }

  const VkResult bindResult = vkBindImageMemory(device_, image, memory, 0);
  if (bindResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(toSampledTextureCreateError(bindResult));
  }

  VkImageViewCreateInfo viewCreateInfo{};
  viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewCreateInfo.image = image;
  viewCreateInfo.viewType = isCube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
  viewCreateInfo.format = vkFormat;
  viewCreateInfo.subresourceRange =
      VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, params.mipLevelCount, 0, isCube ? 6U : 1U};

  VkImageView imageView = VK_NULL_HANDLE;
  const VkResult viewResult = vkCreateImageView(device_, &viewCreateInfo, nullptr, &imageView);
  if (viewResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::SampledTextureCreateError::ImageViewCreationFailed);
  }

  return ResultT::Ok(
      std::make_unique<VulkanSampledTexture>(device_, image, memory, imageView, params.extent, params.format,
                                             params.dimension, params.mipLevelCount));
}

// Plan 0024 Milestone 1 (ADR-0068 D-1/D-2): mirrors createOffscreenTarget()/
// createSampledTexture()'s own identical create/alloc/bind/view
// sequence, usage VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
// VK_IMAGE_USAGE_SAMPLED_BIT -- but checks the real device's own
// VkFormatFeatureFlags FIRST, before any VkResult-producing Vulkan
// call: a missing capability is a real runtime fact, not a programmer
// error, so it is a real Result::Err (FormatFeaturesUnsupported),
// never ATLANTIS_CHECK.
atlantis::Result<std::unique_ptr<atlantis::rhi::HdrColorTarget>, atlantis::rhi::HdrColorTargetCreateError>
VulkanDevice::createHdrColorTarget(const atlantis::rhi::HdrColorTargetCreateParams& params) {
  using ResultT =
      atlantis::Result<std::unique_ptr<atlantis::rhi::HdrColorTarget>, atlantis::rhi::HdrColorTargetCreateError>;

  const VkFormat vkFormat = toVkFormat(params.format);

  VkFormatProperties formatProperties{};
  vkGetPhysicalDeviceFormatProperties(physicalDevice_, vkFormat, &formatProperties);
  if (!hasRequiredHdrColorTargetFeatures(formatProperties.optimalTilingFeatures)) {
    return ResultT::Err(atlantis::rhi::HdrColorTargetCreateError::FormatFeaturesUnsupported);
  }

  VkImageCreateInfo imageCreateInfo{};
  imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
  imageCreateInfo.format = vkFormat;
  imageCreateInfo.extent = VkExtent3D{params.extent.width, params.extent.height, 1};
  imageCreateInfo.mipLevels = 1;
  imageCreateInfo.arrayLayers = 1;
  imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  // Color-attachment for the geometry pass, sampled for the
  // output-transform pass -- both required (ADR-0068 D-1).
  imageCreateInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

  VkImage image = VK_NULL_HANDLE;
  const VkResult createResult = vkCreateImage(device_, &imageCreateInfo, nullptr, &image);
  if (createResult != VK_SUCCESS) {
    return ResultT::Err(toHdrColorTargetCreateError(createResult));
  }

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device_, image, &requirements);

  const std::optional<std::uint32_t> memoryTypeIndex = selectMemoryTypeIndexForDevice(
      physicalDevice_, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  if (!memoryTypeIndex.has_value()) {
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::HdrColorTargetCreateError::AllocationFailed);
  }

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = requirements.size;
  allocInfo.memoryTypeIndex = *memoryTypeIndex;

  VkDeviceMemory memory = VK_NULL_HANDLE;
  const VkResult allocResult = vkAllocateMemory(device_, &allocInfo, nullptr, &memory);
  if (allocResult != VK_SUCCESS) {
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::HdrColorTargetCreateError::AllocationFailed);
  }

  const VkResult bindResult = vkBindImageMemory(device_, image, memory, 0);
  if (bindResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(toHdrColorTargetCreateError(bindResult));
  }

  VkImageViewCreateInfo viewCreateInfo{};
  viewCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewCreateInfo.image = image;
  viewCreateInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewCreateInfo.format = vkFormat;
  viewCreateInfo.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

  VkImageView imageView = VK_NULL_HANDLE;
  const VkResult viewResult = vkCreateImageView(device_, &viewCreateInfo, nullptr, &imageView);
  if (viewResult != VK_SUCCESS) {
    vkFreeMemory(device_, memory, nullptr);
    vkDestroyImage(device_, image, nullptr);
    return ResultT::Err(atlantis::rhi::HdrColorTargetCreateError::ImageViewCreationFailed);
  }

  return ResultT::Ok(
      std::make_unique<VulkanHdrColorTarget>(device_, image, memory, imageView, params.extent, params.format));
}

atlantis::Result<std::unique_ptr<atlantis::rhi::Sampler>, atlantis::rhi::SamplerCreateError>
VulkanDevice::createSampler(const atlantis::rhi::SamplerCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::Sampler>, atlantis::rhi::SamplerCreateError>;

  VkSamplerCreateInfo samplerCreateInfo{};
  samplerCreateInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerCreateInfo.magFilter = toVkFilter(params.filter);
  samplerCreateInfo.minFilter = toVkFilter(params.filter);
  samplerCreateInfo.addressModeU = toVkSamplerAddressMode(params.addressMode);
  samplerCreateInfo.addressModeV = toVkSamplerAddressMode(params.addressMode);
  // 2D-only (Spec 0016 D5) -- W is unused by any sampling this module
  // performs, so it is pinned to the same clamp behavior as U/V rather
  // than exposed as its own RHI parameter.
  samplerCreateInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  ATLANTIS_CHECK(std::isfinite(params.minLod));
  ATLANTIS_CHECK(std::isfinite(params.maxLod));
  ATLANTIS_CHECK(params.minLod >= 0.0F && params.maxLod >= params.minLod);
  samplerCreateInfo.mipmapMode = toVkSamplerMipmapMode(params.mipFilter);
  samplerCreateInfo.minLod = params.minLod;
  samplerCreateInfo.maxLod = params.maxLod;

  VkSampler sampler = VK_NULL_HANDLE;
  const VkResult createResult = vkCreateSampler(device_, &samplerCreateInfo, nullptr, &sampler);
  if (createResult != VK_SUCCESS) {
    return ResultT::Err(atlantis::rhi::SamplerCreateError::SamplerCreationFailed);
  }

  return ResultT::Ok(std::make_unique<VulkanSampler>(device_, sampler, params.filter, params.addressMode,
                                                      params.mipFilter, params.minLod, params.maxLod));
}

}  // namespace atlantis::vulkan_backend::detail

namespace atlantis::vulkan_backend {

atlantis::Result<std::unique_ptr<atlantis::rhi::Device>, DeviceCreateError> createDevice(
    const DeviceCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::Device>, DeviceCreateError>;

  // Computed exactly once; every later decision in this function uses
  // this value, never params.enableValidationLayers directly (validation.h).
  const bool validationEnabled =
      detail::effectiveValidationLayersEnabled(detail::IsDebugBuild, params.enableValidationLayers);

  atlantis::Result<detail::InstanceCreateResult, DeviceCreateError> instanceResult =
      detail::createInstance(params, validationEnabled);
  if (instanceResult.isErr()) {
    return ResultT::Err(instanceResult.error());
  }
  detail::InstanceGuard instanceGuard(instanceResult.value().instance);

  VkDebugUtilsMessengerEXT explicitMessenger = VK_NULL_HANDLE;
  PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn = nullptr;
  if (validationEnabled) {
    auto createMessengerFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instanceGuard.get(), "vkCreateDebugUtilsMessengerEXT"));
    destroyMessengerFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instanceGuard.get(), "vkDestroyDebugUtilsMessengerEXT"));
    if (createMessengerFn == nullptr || destroyMessengerFn == nullptr) {
      return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
    }

    const VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo = detail::makeDebugMessengerCreateInfo();
    if (createMessengerFn(instanceGuard.get(), &messengerCreateInfo, nullptr, &explicitMessenger) != VK_SUCCESS) {
      return ResultT::Err(DeviceCreateError::InstanceCreationFailed);
    }
  }
  detail::MessengerGuard messengerGuard(instanceGuard.get(), explicitMessenger, destroyMessengerFn);

  const std::optional<std::vector<VkPhysicalDevice>> physicalDevices =
      detail::enumeratePhysicalDevices(instanceGuard.get());
  if (!physicalDevices.has_value()) {
    return ResultT::Err(DeviceCreateError::NoSuitablePhysicalDevice);
  }
  const detail::PhysicalDeviceSelectionResult selectionResult = detail::selectPhysicalDevice(
      *physicalDevices, instanceResult.value().physicalDeviceProperties2ExtensionAvailable,
      instanceResult.value().instanceRequestedApiVersionAtLeast1_3, instanceResult.value().getPhysicalDeviceFeatures2KHR);
  if (!selectionResult.selection.has_value()) {
    return ResultT::Err(selectionResult.foundSuitableExceptDynamicRendering
                             ? DeviceCreateError::DynamicRenderingUnavailable
                             : DeviceCreateError::NoSuitablePhysicalDevice);
  }
  const detail::PhysicalDeviceSelection& selectionValue = *selectionResult.selection;
  // Local alias mirroring the previous `selection->` usage below, kept as
  // a pointer for minimal diff against the pre-existing code shape.
  const detail::PhysicalDeviceSelection* selection = &selectionValue;

  constexpr float kQueuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo{};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = selection->queueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &kQueuePriority;

  // ADR-0024 "Accepted Amendment -- 2026-08-13", Section 3, points 5-6:
  // device extension list and feature pNext chain depend on which
  // dynamic-rendering path was selected. Core: no device extension at
  // all (VkPhysicalDeviceVulkan13Features chained, unsuffixed entry
  // points resolved below -- reliable specifically because this Device's
  // owning instance requested apiVersion >= 1.3, see
  // instanceRequestedApiVersionAtLeast1_3's role in
  // decideDynamicRenderingPath()). Extension: VK_KHR_dynamic_rendering
  // plus its full prerequisite chain enabled
  // (VkPhysicalDeviceDynamicRenderingFeaturesKHR chained, KHR-suffixed
  // entry points resolved below). selection->dynamicRenderingPath is
  // never Unavailable here -- selectPhysicalDevice() only ever accepts a
  // candidate whose resolved path is Core or Extension.
  const std::vector<std::string> deviceExtensionNames =
      detail::buildDeviceExtensionList(selection->dynamicRenderingPath, {"VK_KHR_swapchain"});
  std::vector<const char*> deviceExtensions;
  deviceExtensions.reserve(deviceExtensionNames.size());
  for (const std::string& name : deviceExtensionNames) {
    deviceExtensions.push_back(name.c_str());
  }

  VkPhysicalDeviceVulkan13Features vulkan13Features{};
  vulkan13Features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
  vulkan13Features.dynamicRendering = VK_TRUE;
  VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures{};
  dynamicRenderingFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR;
  dynamicRenderingFeatures.dynamicRendering = VK_TRUE;

  void* featureChain = nullptr;
  if (selection->dynamicRenderingPath == detail::DynamicRenderingPath::Core) {
    featureChain = &vulkan13Features;
  } else {
    ATLANTIS_CHECK(selection->dynamicRenderingPath == detail::DynamicRenderingPath::Extension);
    featureChain = &dynamicRenderingFeatures;
  }

  VkDeviceCreateInfo deviceCreateInfo{};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.pNext = featureChain;
  deviceCreateInfo.queueCreateInfoCount = 1;
  deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
  deviceCreateInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
  deviceCreateInfo.ppEnabledExtensionNames = deviceExtensions.data();

  VkDevice device = VK_NULL_HANDLE;
  if (vkCreateDevice(selection->physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
    return ResultT::Err(DeviceCreateError::DeviceCreationFailed);
  }
  detail::DeviceGuard deviceGuard(device);

  // Resolved entry points (ADR-0024 Amendment Section 3, points 5-6):
  // resolved once, here, via vkGetDeviceProcAddr, using exactly the name
  // pair selectDynamicRenderingEntryPointNames() selects for the
  // selected path -- unsuffixed for Core, KHR-suffixed for Extension.
  // Missing either name for the selected path is a checked, recoverable
  // failure (never a cross-path fallback, never an unresolved pointer
  // silently called) -- the same DeviceCreateError::DynamicRenderingUnavailable
  // tier as every other dynamic-rendering-unavailable outcome.
  const std::optional<detail::DynamicRenderingEntryPointNames> entryPointNames =
      detail::selectDynamicRenderingEntryPointNames(selection->dynamicRenderingPath);
  ATLANTIS_CHECK(entryPointNames.has_value());
  PFN_vkCmdBeginRenderingKHR cmdBeginRendering =
      reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(device, entryPointNames->beginRenderingName));
  PFN_vkCmdEndRenderingKHR cmdEndRendering =
      reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(device, entryPointNames->endRenderingName));
  if (cmdBeginRendering == nullptr || cmdEndRendering == nullptr) {
    return ResultT::Err(DeviceCreateError::DynamicRenderingUnavailable);
  }

  // vkGetDeviceQueue has no VkResult -- none is fabricated. device just
  // succeeded above and selection->queueFamilyIndex came from real
  // enumeration/selection, so both inputs are already known valid.
  VkQueue queue = VK_NULL_HANDLE;
  vkGetDeviceQueue(device, selection->queueFamilyIndex, 0, &queue);

  // Plan 0006 Section 9: the two objects VulkanDevice's own
  // createCommandList()/submit()/waitIdle() need, created once here and
  // owned by VulkanDevice for its whole lifetime. RESET_COMMAND_BUFFER_BIT
  // allows individual vkFreeCommandBuffers calls (VulkanCommandList's own
  // destructor) rather than only whole-pool resets. No render-finished
  // semaphore is created here -- VulkanPresentation owns a per-swapchain-
  // image pool instead (found via GPU testing; see that class's own
  // header comment).
  VkCommandPoolCreateInfo commandPoolCreateInfo{};
  commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  commandPoolCreateInfo.queueFamilyIndex = selection->queueFamilyIndex;

  VkCommandPool commandPool = VK_NULL_HANDLE;
  if (vkCreateCommandPool(device, &commandPoolCreateInfo, nullptr, &commandPool) != VK_SUCCESS) {
    return ResultT::Err(DeviceCreateError::DeviceCreationFailed);
  }
  detail::CommandPoolGuard commandPoolGuard(device, commandPool);

  // No VK_FENCE_CREATE_SIGNALED_BIT: initially unsignaled, matching "no
  // submission retained yet" -- VulkanDevice's own hasRetainedSubmission_
  // flag (not this fence's signal state) is what distinguishes "never
  // submitted" from "fence not yet waited," so the fence's own initial
  // state only needs to be a value vkWaitForFences would never be called
  // on before the first real submission anyway.
  VkFenceCreateInfo fenceCreateInfo{};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

  VkFence submissionFence = VK_NULL_HANDLE;
  if (vkCreateFence(device, &fenceCreateInfo, nullptr, &submissionFence) != VK_SUCCESS) {
    return ResultT::Err(DeviceCreateError::DeviceCreationFailed);
  }
  detail::FenceGuard fenceGuard(device, submissionFence);

  // Spec 0021/ADR-0064 (originally Plan 0007 Section 10's own fixed-
  // capacity descriptor pool): the FIRST pool (generation 0) of
  // VulkanDevice's own growable set, created once here, mirroring the
  // command pool/fence precedent above -- sized to
  // kDescriptorPoolMaxSetsByGeneration[0] (4, unchanged from this
  // codebase's own pre-Spec-0021 value, but now justified differently:
  // correctness no longer depends on this number being "big enough" --
  // VulkanDevice::allocateDescriptorSet()'s own growth guarantees that
  // -- it is kept small and cheap so the steady-state, already-verified
  // single/double-material scenes this codebase ships today never
  // trigger a growth event at all). createDescriptorPoolOfSize() is the
  // single, shared helper this pool and every later-grown pool both use
  // (Spec 0021 D4/P8) -- they cannot drift apart from each other.
  const VkDescriptorPool descriptorPool =
      detail::createDescriptorPoolOfSize(device, detail::kDescriptorPoolMaxSetsByGeneration[0]);
  if (descriptorPool == VK_NULL_HANDLE) {
    return ResultT::Err(DeviceCreateError::DeviceCreationFailed);
  }
  detail::DescriptorPoolGuard descriptorPoolGuard(device, descriptorPool);

  // Two-phase ownership transfer. Phase 1: the guards still own every
  // handle here -- VulkanDevice is constructed from their non-owning
  // get() values only, so if std::make_unique's allocation or
  // VulkanDevice's constructor were ever to fail, every guard is still
  // intact and its destructor still runs (fence -> command pool -> device
  // -> messenger -> instance, reverse declaration order), leaking
  // nothing.
  std::unique_ptr<atlantis::rhi::Device> vulkanDevice = std::make_unique<detail::VulkanDevice>(
      instanceGuard.get(), selection->physicalDevice, deviceGuard.get(), queue, selection->queueFamilyIndex,
      messengerGuard.get(), destroyMessengerFn, commandPoolGuard.get(), fenceGuard.get(), cmdBeginRendering,
      cmdEndRendering, descriptorPoolGuard.get());

  // Phase 2: std::make_unique returned successfully, so VulkanDevice now
  // holds its own copy of every handle value and is the sole owner going
  // forward. Only now do the guards relinquish ownership -- each release()
  // is its own statement, not folded into an expression whose evaluation
  // order this code would otherwise have to rely on. Order among these
  // five calls does not matter: release() only clears each guard's own
  // internal handle to VK_NULL_HANDLE, it does not call any Vulkan
  // function, so there is no dependency between the calls to sequence.
  (void)instanceGuard.release();
  (void)deviceGuard.release();
  (void)messengerGuard.release();
  (void)commandPoolGuard.release();
  (void)fenceGuard.release();
  (void)descriptorPoolGuard.release();

  return ResultT::Ok(std::move(vulkanDevice));
}

}  // namespace atlantis::vulkan_backend
