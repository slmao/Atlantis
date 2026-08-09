#include "vulkan_device.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include <atlantis/assert.h>
#include <atlantis/log.h>
#include <atlantis/vulkan_backend/vulkan_backend.h>

#include "validation.h"
#include "vulkan_command_list.h"
#include "vulkan_instance.h"
#include "vulkan_render_target.h"
#include "vulkan_result.h"
#include "vulkan_submission_signal.h"
#include "wsi/win32_surface.h"

namespace atlantis::vulkan_backend::detail {

namespace {

constexpr const char* kSwapchainExtension = "VK_KHR_swapchain";

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
};

// Selects the first physical device, in vkEnumeratePhysicalDevices' own
// returned order, meeting Phase 1's minimum requirement: Vulkan API
// version >= 1.0 (Spec 0003 needs nothing higher -- this is a structural
// check, not a stricter version this Plan has no basis to invent),
// VK_KHR_swapchain support, and a combined graphics+present queue family
// (Plan Section 7 items 2-3). No scoring, no discrete/integrated
// preference, no caller-visible enumeration/selection API.
[[nodiscard]] std::optional<PhysicalDeviceSelection> selectPhysicalDevice(
    const std::vector<VkPhysicalDevice>& physicalDevices) {
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
    return PhysicalDeviceSelection{physicalDevice, *queueFamilyIndex};
  }
  return std::nullopt;
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

}  // namespace

VulkanDevice::VulkanDevice(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, VkQueue queue,
                            std::uint32_t queueFamilyIndex, VkDebugUtilsMessengerEXT explicitMessenger,
                            PFN_vkDestroyDebugUtilsMessengerEXT destroyMessengerFn, VkCommandPool commandPool,
                            VkFence submissionFence)
    : instance_(instance),
      physicalDevice_(physicalDevice),
      device_(device),
      queue_(queue),
      queueFamilyIndex_(queueFamilyIndex),
      explicitMessenger_(explicitMessenger),
      destroyMessengerFn_(destroyMessengerFn),
      commandPool_(commandPool),
      submissionFence_(submissionFence) {}

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

  return ResultT::Ok(std::make_unique<VulkanCommandList>(device_, commandPool_, commandBuffer));
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
  const std::optional<detail::PhysicalDeviceSelection> selection = detail::selectPhysicalDevice(*physicalDevices);
  if (!selection.has_value()) {
    return ResultT::Err(DeviceCreateError::NoSuitablePhysicalDevice);
  }

  constexpr float kQueuePriority = 1.0f;
  VkDeviceQueueCreateInfo queueCreateInfo{};
  queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
  queueCreateInfo.queueFamilyIndex = selection->queueFamilyIndex;
  queueCreateInfo.queueCount = 1;
  queueCreateInfo.pQueuePriorities = &kQueuePriority;

  const char* deviceExtension = "VK_KHR_swapchain";
  VkDeviceCreateInfo deviceCreateInfo{};
  deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
  deviceCreateInfo.queueCreateInfoCount = 1;
  deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
  deviceCreateInfo.enabledExtensionCount = 1;
  deviceCreateInfo.ppEnabledExtensionNames = &deviceExtension;

  VkDevice device = VK_NULL_HANDLE;
  if (vkCreateDevice(selection->physicalDevice, &deviceCreateInfo, nullptr, &device) != VK_SUCCESS) {
    return ResultT::Err(DeviceCreateError::DeviceCreationFailed);
  }
  detail::DeviceGuard deviceGuard(device);

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

  // Two-phase ownership transfer. Phase 1: the guards still own every
  // handle here -- VulkanDevice is constructed from their non-owning
  // get() values only, so if std::make_unique's allocation or
  // VulkanDevice's constructor were ever to fail, every guard is still
  // intact and its destructor still runs (fence -> command pool -> device
  // -> messenger -> instance, reverse declaration order), leaking
  // nothing.
  std::unique_ptr<atlantis::rhi::Device> vulkanDevice = std::make_unique<detail::VulkanDevice>(
      instanceGuard.get(), selection->physicalDevice, deviceGuard.get(), queue, selection->queueFamilyIndex,
      messengerGuard.get(), destroyMessengerFn, commandPoolGuard.get(), fenceGuard.get());

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

  return ResultT::Ok(std::move(vulkanDevice));
}

}  // namespace atlantis::vulkan_backend
