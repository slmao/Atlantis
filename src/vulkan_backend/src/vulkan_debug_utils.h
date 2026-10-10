#pragma once

#include <vulkan/vulkan_core.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <type_traits>

// Spec 0059 / ADR-0115 D5-D6: VK_EXT_debug_utils object names and command-buffer
// labels, for RenderDoc and validation messages. Private to the Vulkan Backend.
// Everything here is a silent no-op unless the extension was enabled on the
// instance and its three commands resolved; absence is never an error.
namespace atlantis::vulkan_backend::detail {

// The names resolved by DebugUtils::load(). A pure function, so the selection is
// unit-testable with no Vulkan call (the dynamic_rendering_entry_points shape).
struct DebugUtilsEntryPointNames {
  const char* extension;
  const char* setObjectName;
  const char* cmdBeginLabel;
  const char* cmdEndLabel;
};
[[nodiscard]] DebugUtilsEntryPointNames debugUtilsEntryPointNames() noexcept;

// A test observer: sees every name set and every label begin/end, so a GPU-free
// or GPU test can assert on them (Vulkan has no call to read a name back).
struct DebugUtilsObserver {
  std::function<void(VkObjectType type, std::string_view name)> onName;
  std::function<void(std::string_view label)> onBeginLabel;
  std::function<void()> onEndLabel;
};

// Borrowed from the Device that resolved it; cheap to copy (three pointers and
// an observer pointer). Not thread-safe: frame thread only (ADR-0004).
class DebugUtils {
 public:
  DebugUtils() = default;

  // Resolves the three commands through vkGetInstanceProcAddr. `instanceEnabled`
  // must be true only if VK_EXT_debug_utils was enabled on `instance`. Any
  // unresolved command leaves the whole set disabled.
  [[nodiscard]] static DebugUtils load(VkInstance instance, bool instanceEnabled) noexcept;

  // True if names and labels will reach the driver (an observer alone does not
  // enable it).
  [[nodiscard]] bool enabled() const noexcept { return setObjectName_ != nullptr; }

  // Names `handle`. Empty `name` is a no-op. With a suffix the name is
  // "<name><suffix>" (e.g. "/memory"), built in a stack buffer.
  void name(VkDevice device, VkObjectType type, std::uint64_t handle, std::string_view name,
            std::string_view suffix = {}) const noexcept;

  void beginLabel(VkCommandBuffer commandBuffer, std::string_view label) const noexcept;
  void endLabel(VkCommandBuffer commandBuffer) const noexcept;

  // For tests. Not owned; must outlive every call made through this object.
  void setObserver(const DebugUtilsObserver* observer) noexcept { observer_ = observer; }
  [[nodiscard]] const DebugUtilsObserver* observer() const noexcept { return observer_; }

 private:
  PFN_vkSetDebugUtilsObjectNameEXT setObjectName_ = nullptr;
  PFN_vkCmdBeginDebugUtilsLabelEXT cmdBeginLabel_ = nullptr;
  PFN_vkCmdEndDebugUtilsLabelEXT cmdEndLabel_ = nullptr;
  const DebugUtilsObserver* observer_ = nullptr;
};

// A Vulkan handle's value as the debug-utils commands take it.
template <class Handle>
[[nodiscard]] std::uint64_t handleValue(Handle handle) noexcept {
  if constexpr (std::is_pointer_v<Handle>) {
    return reinterpret_cast<std::uint64_t>(handle);
  } else {
    return static_cast<std::uint64_t>(handle);
  }
}

}  // namespace atlantis::vulkan_backend::detail
