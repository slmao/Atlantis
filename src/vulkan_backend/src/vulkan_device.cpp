#include "vulkan_device.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/log.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "dynamic_rendering.h"
#include "validation.h"
#include "vulkan_buffer.h"
#include "vulkan_command_list.h"
#include "vulkan_instance.h"
#include "vulkan_memory.h"
#include "vulkan_pipeline.h"
#include "vulkan_render_target.h"
#include "vulkan_result.h"
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
    VkPhysicalDevice physicalDevice, bool instanceExtensionAvailable,
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

  return decideDynamicRenderingPath(instanceExtensionAvailable, apiVersionAtLeast1_3, coreFeatureSupported,
                                     extensionAdvertised, extensionFeatureSupported);
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
    PFN_vkGetPhysicalDeviceFeatures2KHR getPhysicalDeviceFeatures2KHR) {
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
    const DynamicRenderingPath dynamicRenderingPath = queryDynamicRenderingPath(
        physicalDevice, physicalDeviceProperties2ExtensionAvailable, getPhysicalDeviceFeatures2KHR);
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

// Plan 0007 Section 10: guard for VulkanDevice's Device-level
// VkDescriptorPool (maxSets = 4, the camera-uniform-binding design's own
// fixed-capacity pool).
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
      cmdEndRendering_(cmdEndRendering),
      descriptorPool_(descriptorPool) {}

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
  // Plan 0007 Section 10: destroyed before VkDevice, after every
  // VulkanPipeline that could have held a VkDescriptorSet from this pool
  // -- caller discipline (Section 14), same tier as VkCommandPool above.
  vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);

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

  const auto& vulkanTarget = static_cast<const VulkanRenderTarget&>(target);
  const VkSemaphore waitSemaphore = vulkanTarget.acquireCompleteSemaphore();
  // Plan 0006 (found via GPU testing): read per-image-index render-
  // finished semaphore from the target itself, rather than a single
  // Device-owned one -- see VulkanPresentation's own header comment for
  // why a single shared semaphore is not safe to reuse across frames.
  const VkSemaphore signalSemaphore = vulkanTarget.renderFinishedSemaphore();
  const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  const VkCommandBuffer commandBuffer = vulkanCommandList.commandBuffer();

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &waitSemaphore;
  submitInfo.pWaitDstStageMask = &waitStage;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &signalSemaphore;

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
  }
  ATLANTIS_CHECK_MSG(false, "vertexAttributeFormatToVkFormat() called with an unhandled enumerator");
  return VK_FORMAT_UNDEFINED;
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

atlantis::Result<std::unique_ptr<atlantis::rhi::Pipeline>, atlantis::rhi::PipelineCreateError>
VulkanDevice::createPipeline(const atlantis::rhi::PipelineCreateParams& params) {
  using ResultT = atlantis::Result<std::unique_ptr<atlantis::rhi::Pipeline>, atlantis::rhi::PipelineCreateError>;

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
  // design: exactly one binding, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, vertex
  // stage only.
  VkDescriptorSetLayoutBinding uniformBinding{};
  uniformBinding.binding = 0;
  uniformBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  uniformBinding.descriptorCount = 1;
  uniformBinding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

  VkDescriptorSetLayoutCreateInfo setLayoutCreateInfo{};
  setLayoutCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  setLayoutCreateInfo.bindingCount = 1;
  setLayoutCreateInfo.pBindings = &uniformBinding;

  VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
  const VkResult setLayoutResult =
      vkCreateDescriptorSetLayout(device_, &setLayoutCreateInfo, nullptr, &descriptorSetLayout);
  if (setLayoutResult != VK_SUCCESS) {
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    return ResultT::Err(atlantis::rhi::PipelineCreateError::DescriptorSetLayoutCreationFailed);
  }

  VkDescriptorSetAllocateInfo setAllocInfo{};
  setAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  setAllocInfo.descriptorPool = descriptorPool_;
  setAllocInfo.descriptorSetCount = 1;
  setAllocInfo.pSetLayouts = &descriptorSetLayout;

  VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
  const VkResult setAllocResult = vkAllocateDescriptorSets(device_, &setAllocInfo, &descriptorSet);
  if (setAllocResult != VK_SUCCESS) {
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
    vkDestroyShaderModule(device_, fragmentModule, nullptr);
    vkDestroyShaderModule(device_, vertexModule, nullptr);
    return ResultT::Err(atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed);
  }

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
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
    vkFreeDescriptorSets(device_, descriptorPool_, 1, &descriptorSet);
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

  VkPipelineDepthStencilStateCreateInfo depthStencilState{};
  depthStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  depthStencilState.depthTestEnable = VK_TRUE;
  depthStencilState.depthWriteEnable = VK_TRUE;
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

  const VkFormat colorFormat = toVkFormat(params.colorFormat);
  const VkFormat depthFormat = toVkFormat(params.depthFormat);

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
    vkFreeDescriptorSets(device_, descriptorPool_, 1, &descriptorSet);
    vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
    return ResultT::Err(toPipelineCreateError(pipelineResult));
  }

  return ResultT::Ok(std::make_unique<VulkanPipeline>(device_, descriptorPool_, pipeline, pipelineLayout,
                                                        descriptorSetLayout, descriptorSet));
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
      instanceResult.value().getPhysicalDeviceFeatures2KHR);
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

  // Spec 0007 / ADR-0024 Section 8: device extension list and feature
  // pNext chain depend on which dynamic-rendering path was selected --
  // Core chains VkPhysicalDeviceVulkan13Features; Extension chains
  // VkPhysicalDeviceDynamicRenderingFeaturesKHR.
  //
  // Implementation-forced deviation from Plan 0007 Section 8's stated
  // "Core needs no device extension": this Plan deliberately keeps the
  // *instance's* requested apiVersion at VK_API_VERSION_1_0 (Section 8's
  // own instance-level constraint, unchanged) even when a Core-path
  // device's own reported apiVersion is 1.3+. In practice, on this
  // environment, resolving the *unsuffixed* core entry points
  // ("vkCmdBeginRendering"/"vkCmdEndRendering") via either static
  // linkage or vkGetDeviceProcAddr is unreliable when the instance
  // itself never requested 1.3+ -- only the KHR-suffixed extension
  // entry points, backed by the KHR extension actually being enabled at
  // device creation, resolve reliably via vkGetDeviceProcAddr regardless
  // of the instance's requested apiVersion. So this device creation now
  // requests "VK_KHR_dynamic_rendering" unconditionally, on *both*
  // paths, and resolves only the KHR-suffixed entry points below --
  // Core still chains VkPhysicalDeviceVulkan13Features (matching what
  // decideDynamicRenderingPath() actually detected), but now also
  // enables the (always present alongside a promoted core feature, on
  // every driver this Plan's own physical-device selection loop already
  // requires to have reported the extension or the core feature -- see
  // dynamic_rendering.cpp) KHR extension name purely so its aliased
  // entry points are resolvable. This changes only how the device is
  // configured and how the function pointers already described in
  // Section 8 are *obtained* -- not which capability was detected
  // (`decideDynamicRenderingPath()`'s own pure decision logic, already
  // exhaustively tested, is untouched), not which path is selected, and
  // not any public signature -- an implementation detail, not a Human
  // Review Blocker.
  // VK_KHR_dynamic_rendering's own extension dependency chain (required
  // any time the instance/device is below Vulkan 1.2, per the Vulkan
  // spec's extension-dependency table for VK_KHR_dynamic_rendering ->
  // VK_KHR_depth_stencil_resolve -> VK_KHR_create_renderpass2 ->
  // VK_KHR_multiview / VK_KHR_maintenance2) -- all promoted to core in
  // 1.2/1.3 and so ordinarily implicit, but this Plan's instance never
  // requests above VK_API_VERSION_1_0 (Section 8), so each dependency
  // must be listed explicitly for vkCreateDevice() to accept
  // "VK_KHR_dynamic_rendering" itself.
  std::vector<const char*> deviceExtensions{"VK_KHR_swapchain",     "VK_KHR_multiview",
                                             "VK_KHR_maintenance2",  "VK_KHR_create_renderpass2",
                                             "VK_KHR_depth_stencil_resolve", "VK_KHR_dynamic_rendering"};
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

  // Resolved entry points (Plan 0007 Section 8/10, see this function's
  // own deviation note above for why the KHR-suffixed names are used on
  // both paths): resolved once, here, via vkGetDeviceProcAddr, for
  // either dynamic-rendering path -- both are valid to call given
  // "VK_KHR_dynamic_rendering" is now unconditionally enabled above.
  PFN_vkCmdBeginRenderingKHR cmdBeginRendering =
      reinterpret_cast<PFN_vkCmdBeginRenderingKHR>(vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR"));
  PFN_vkCmdEndRenderingKHR cmdEndRendering =
      reinterpret_cast<PFN_vkCmdEndRenderingKHR>(vkGetDeviceProcAddr(device, "vkCmdEndRenderingKHR"));
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

  // Plan 0007 Section 10: the camera-uniform-binding design's fixed-
  // capacity descriptor pool, created once here, mirroring the command
  // pool/fence precedent above. maxSets = 4 (double this Plan's own
  // derived peak concurrent count of 2 -- see Section 10's own
  // derivation), VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT
  // required so vkFreeDescriptorSets (VulkanPipeline's destructor) is
  // valid usage.
  VkDescriptorPoolSize descriptorPoolSize{};
  descriptorPoolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  descriptorPoolSize.descriptorCount = 4;

  VkDescriptorPoolCreateInfo descriptorPoolCreateInfo{};
  descriptorPoolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolCreateInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  descriptorPoolCreateInfo.maxSets = 4;
  descriptorPoolCreateInfo.poolSizeCount = 1;
  descriptorPoolCreateInfo.pPoolSizes = &descriptorPoolSize;

  VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
  if (vkCreateDescriptorPool(device, &descriptorPoolCreateInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
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
