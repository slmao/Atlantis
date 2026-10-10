#include "vulkan_debug_utils.h"

#include <atlantis/log.h>

#include <array>
#include <cstring>

namespace atlantis::vulkan_backend::detail {

namespace {

// Names and labels are short by convention; a longer one is truncated rather
// than allocated, so a frame's labels never touch the heap.
constexpr std::size_t kMaxNameBytes = 191;

// Copies `a` then `b` into `buffer` (NUL-terminated, truncated) and returns the
// length without the NUL.
std::size_t compose(std::array<char, kMaxNameBytes + 1>& buffer, std::string_view a, std::string_view b) noexcept {
  std::size_t length = 0;
  for (const std::string_view part : {a, b}) {
    const std::size_t room = kMaxNameBytes - length;
    const std::size_t take = part.size() < room ? part.size() : room;
    std::memcpy(buffer.data() + length, part.data(), take);
    length += take;
  }
  buffer[length] = '\0';
  return length;
}

}  // namespace

DebugUtilsEntryPointNames debugUtilsEntryPointNames() noexcept {
  return {VK_EXT_DEBUG_UTILS_EXTENSION_NAME, "vkSetDebugUtilsObjectNameEXT", "vkCmdBeginDebugUtilsLabelEXT",
          "vkCmdEndDebugUtilsLabelEXT"};
}

DebugUtils DebugUtils::load(VkInstance instance, bool instanceEnabled) noexcept {
  DebugUtils utils;
  if (!instanceEnabled || instance == VK_NULL_HANDLE) return utils;
  const DebugUtilsEntryPointNames names = debugUtilsEntryPointNames();
  const auto setName =
      reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(instance, names.setObjectName));
  const auto begin =
      reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, names.cmdBeginLabel));
  const auto end =
      reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(instance, names.cmdEndLabel));
  if (setName == nullptr || begin == nullptr || end == nullptr) return utils;
  utils.setObjectName_ = setName;
  utils.cmdBeginLabel_ = begin;
  utils.cmdEndLabel_ = end;
  return utils;
}

void DebugUtils::name(VkDevice device, VkObjectType type, std::uint64_t handle, std::string_view name,
                      std::string_view suffix) const noexcept {
  if (name.empty()) return;
  std::array<char, kMaxNameBytes + 1> buffer{};
  const std::size_t length = compose(buffer, name, suffix);
  if (observer_ != nullptr && observer_->onName) observer_->onName(type, std::string_view(buffer.data(), length));
  if (setObjectName_ == nullptr) return;

  VkDebugUtilsObjectNameInfoEXT info{};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  info.objectType = type;
  info.objectHandle = handle;
  info.pObjectName = buffer.data();
  const VkResult result = setObjectName_(device, &info);
  if (result != VK_SUCCESS) {
    ATLANTIS_LOG_WARN("vkSetDebugUtilsObjectNameEXT() failed ({}) naming '{}'", static_cast<int>(result), buffer.data());
  }
}

void DebugUtils::beginLabel(VkCommandBuffer commandBuffer, std::string_view label) const noexcept {
  std::array<char, kMaxNameBytes + 1> buffer{};
  const std::size_t length = compose(buffer, label, {});
  if (observer_ != nullptr && observer_->onBeginLabel) observer_->onBeginLabel(std::string_view(buffer.data(), length));
  if (cmdBeginLabel_ == nullptr) return;
  VkDebugUtilsLabelEXT info{};
  info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
  info.pLabelName = buffer.data();
  cmdBeginLabel_(commandBuffer, &info);  // returns void: no VkResult exists
}

void DebugUtils::endLabel(VkCommandBuffer commandBuffer) const noexcept {
  if (observer_ != nullptr && observer_->onEndLabel) observer_->onEndLabel();
  if (cmdEndLabel_ == nullptr) return;
  cmdEndLabel_(commandBuffer);
}

}  // namespace atlantis::vulkan_backend::detail
