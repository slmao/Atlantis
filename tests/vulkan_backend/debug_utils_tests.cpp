#include "vulkan_debug_utils.h"

#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <string>
#include <vector>

// Spec 0059 / ADR-0115 D5-D6: the GPU-independent part of the debug-utils
// wrapper -- entry-point selection, the disabled (extension absent) no-op, and
// how names and labels are composed. No Vulkan call is made: with nothing
// resolved, DebugUtils only tells its observer.

using atlantis::vulkan_backend::detail::DebugUtils;
using atlantis::vulkan_backend::detail::DebugUtilsObserver;
using atlantis::vulkan_backend::detail::debugUtilsEntryPointNames;
using atlantis::vulkan_backend::detail::handleValue;

TEST_CASE("the debug-utils entry points are the extension's own three commands", "[vulkan_backend][debug_utils]") {
  const auto names = debugUtilsEntryPointNames();
  CHECK(std::strcmp(names.extension, "VK_EXT_debug_utils") == 0);
  CHECK(std::strcmp(names.setObjectName, "vkSetDebugUtilsObjectNameEXT") == 0);
  CHECK(std::strcmp(names.cmdBeginLabel, "vkCmdBeginDebugUtilsLabelEXT") == 0);
  CHECK(std::strcmp(names.cmdEndLabel, "vkCmdEndDebugUtilsLabelEXT") == 0);
}

TEST_CASE("an absent extension is a silent no-op", "[vulkan_backend][debug_utils]") {
  DebugUtils utils;
  CHECK_FALSE(utils.enabled());
  // Nothing resolved and no observer: naming and labelling must not touch Vulkan.
  utils.name(VK_NULL_HANDLE, VK_OBJECT_TYPE_BUFFER, 1, "x");
  utils.beginLabel(VK_NULL_HANDLE, "x");
  utils.endLabel(VK_NULL_HANDLE);

  CHECK_FALSE(DebugUtils::load(VK_NULL_HANDLE, true).enabled());  // no instance to resolve through
  CHECK_FALSE(DebugUtils::load(VK_NULL_HANDLE, false).enabled());  // extension not enabled on the instance
}

TEST_CASE("names compose with their suffix, an empty name sets nothing, a long one is truncated",
          "[vulkan_backend][debug_utils]") {
  std::vector<std::pair<VkObjectType, std::string>> seen;
  DebugUtilsObserver observer;
  observer.onName = [&seen](VkObjectType type, std::string_view name) { seen.emplace_back(type, std::string(name)); };
  DebugUtils utils;
  utils.setObserver(&observer);

  utils.name(VK_NULL_HANDLE, VK_OBJECT_TYPE_IMAGE, 7, "hdr_color");
  utils.name(VK_NULL_HANDLE, VK_OBJECT_TYPE_IMAGE_VIEW, 8, "hdr_color", "/view");
  utils.name(VK_NULL_HANDLE, VK_OBJECT_TYPE_BUFFER, 9, "");
  REQUIRE(seen.size() == 2);
  CHECK(seen[0].second == "hdr_color");
  CHECK(seen[1].first == VK_OBJECT_TYPE_IMAGE_VIEW);
  CHECK(seen[1].second == "hdr_color/view");

  const std::string longName(400, 'a');
  utils.name(VK_NULL_HANDLE, VK_OBJECT_TYPE_BUFFER, 10, longName, "/memory");
  REQUIRE(seen.size() == 3);
  CHECK(seen[2].second.size() == 191);  // truncated into the stack buffer, never allocated
  CHECK(seen[2].second.find_first_not_of('a') == std::string::npos);
}

TEST_CASE("labels reach the observer in order", "[vulkan_backend][debug_utils]") {
  std::vector<std::string> events;
  DebugUtilsObserver observer;
  observer.onBeginLabel = [&events](std::string_view label) { events.push_back("begin:" + std::string(label)); };
  observer.onEndLabel = [&events]() { events.push_back("end"); };
  DebugUtils utils;
  utils.setObserver(&observer);

  utils.beginLabel(VK_NULL_HANDLE, "shadow");
  utils.endLabel(VK_NULL_HANDLE);
  REQUIRE(events.size() == 2);
  CHECK(events[0] == "begin:shadow");
  CHECK(events[1] == "end");
}

TEST_CASE("handleValue passes a handle's value through", "[vulkan_backend][debug_utils]") {
  CHECK(handleValue(static_cast<std::uint64_t>(42)) == 42);
  int object = 0;
  CHECK(handleValue(&object) == reinterpret_cast<std::uint64_t>(&object));
}
