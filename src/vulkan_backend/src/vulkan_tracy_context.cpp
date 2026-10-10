#include "vulkan_tracy_context.h"

#if defined(ATLANTIS_PROFILING_TRACY)

// Vulkan headers first: TracyVulkan.hpp requires them.
#include <vulkan/vulkan_core.h>

#include <tracy/TracyVulkan.hpp>

#include <atlantis/log.h>

#include <array>
#include <cstring>
#include <new>

namespace atlantis::vulkan_backend::detail {

namespace {
constexpr std::size_t kMaxDepth = 8;
}

struct TracyGpuContext::Impl {
  TracyVkCtx context = nullptr;
  // VkCtxScope is neither copyable nor movable, so the open zones live in raw
  // storage, constructed by begin() and destroyed by end().
  alignas(tracy::VkCtxScope) std::array<std::array<unsigned char, sizeof(tracy::VkCtxScope)>, kMaxDepth> scopes{};
  std::size_t depth = 0;
  std::size_t overflow = 0;
};

TracyGpuContext::TracyGpuContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

TracyGpuContext::~TracyGpuContext() {
  if (!impl_) return;
  while (impl_->depth > 0) end();
  if (impl_->context != nullptr) TracyVkDestroy(impl_->context);
}

std::unique_ptr<TracyGpuContext> TracyGpuContext::create(VkPhysicalDevice physicalDevice, VkDevice device,
                                                         VkQueue queue, VkCommandPool commandPool) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.commandPool = commandPool;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandBufferCount = 1;
  VkCommandBuffer setup = VK_NULL_HANDLE;
  if (const VkResult result = vkAllocateCommandBuffers(device, &allocInfo, &setup); result != VK_SUCCESS) {
    ATLANTIS_LOG_WARN("Tracy GPU context unavailable: vkAllocateCommandBuffers returned {}", static_cast<int>(result));
    return nullptr;
  }
  auto impl = std::make_unique<Impl>();
  impl->context = TracyVkContext(physicalDevice, device, queue, setup);
  vkFreeCommandBuffers(device, commandPool, 1, &setup);
  if (impl->context == nullptr) return nullptr;
  TracyVkContextName(impl->context, "Atlantis graphics queue", 23);
  return std::unique_ptr<TracyGpuContext>(new TracyGpuContext(std::move(impl)));
}

void TracyGpuContext::begin(VkCommandBuffer commandBuffer, std::string_view name) {
  if (impl_->depth >= kMaxDepth) {
    ++impl_->overflow;
    return;
  }
  void* storage = impl_->scopes[impl_->depth].data();
  new (storage) tracy::VkCtxScope(impl_->context, 0, "", 0, "", 0, name.data(), name.size(), commandBuffer, true);
  ++impl_->depth;
}

void TracyGpuContext::end() {
  if (impl_->overflow > 0) {
    --impl_->overflow;
    return;
  }
  if (impl_->depth == 0) return;
  --impl_->depth;
  reinterpret_cast<tracy::VkCtxScope*>(impl_->scopes[impl_->depth].data())->~VkCtxScope();
}

void TracyGpuContext::collect(VkCommandBuffer commandBuffer) { TracyVkCollect(impl_->context, commandBuffer); }

}  // namespace atlantis::vulkan_backend::detail

#else  // !ATLANTIS_PROFILING_TRACY

namespace atlantis::vulkan_backend::detail {

struct TracyGpuContext::Impl {};

TracyGpuContext::TracyGpuContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
TracyGpuContext::~TracyGpuContext() = default;
std::unique_ptr<TracyGpuContext> TracyGpuContext::create(VkPhysicalDevice, VkDevice, VkQueue, VkCommandPool) {
  return nullptr;
}
void TracyGpuContext::begin(VkCommandBuffer, std::string_view) {}
void TracyGpuContext::end() {}
void TracyGpuContext::collect(VkCommandBuffer) {}

}  // namespace atlantis::vulkan_backend::detail

#endif  // ATLANTIS_PROFILING_TRACY
