#include "device_extension_list.h"

#include <algorithm>
#include <set>

#include <catch2/catch_test_macros.hpp>

using atlantis::vulkan_backend::detail::buildDeviceExtensionList;
using atlantis::vulkan_backend::detail::DynamicRenderingPath;

// ADR-0024 "Accepted Amendment -- 2026-08-13" Section 3 / 6: GPU-independent
// unit tests for buildDeviceExtensionList().

TEST_CASE("buildDeviceExtensionList: Core path excludes the dynamic-rendering KHR chain entirely",
          "[vulkan_backend][device_extension_list]") {
  const std::vector<std::string> result = buildDeviceExtensionList(DynamicRenderingPath::Core, {"VK_KHR_swapchain"});
  CHECK(result == std::vector<std::string>{"VK_KHR_swapchain"});
  for (const char* excluded : {"VK_KHR_dynamic_rendering", "VK_KHR_multiview", "VK_KHR_maintenance2",
                               "VK_KHR_create_renderpass2", "VK_KHR_depth_stencil_resolve"}) {
    CHECK(std::find(result.begin(), result.end(), excluded) == result.end());
  }
}

TEST_CASE("buildDeviceExtensionList: Extension path includes exactly the necessary chain",
          "[vulkan_backend][device_extension_list]") {
  const std::vector<std::string> result =
      buildDeviceExtensionList(DynamicRenderingPath::Extension, {"VK_KHR_swapchain"});
  const std::set<std::string> expected = {"VK_KHR_swapchain",           "VK_KHR_multiview",
                                           "VK_KHR_maintenance2",        "VK_KHR_create_renderpass2",
                                           "VK_KHR_depth_stencil_resolve", "VK_KHR_dynamic_rendering"};
  const std::set<std::string> actual(result.begin(), result.end());
  CHECK(actual == expected);
  CHECK(result.size() == expected.size());
}

TEST_CASE("buildDeviceExtensionList: other required extensions are unaffected by path",
          "[vulkan_backend][device_extension_list]") {
  const std::vector<std::string> required = {"VK_KHR_swapchain", "VK_SOME_OTHER_EXTENSION"};
  const std::vector<std::string> corePath = buildDeviceExtensionList(DynamicRenderingPath::Core, required);
  CHECK(corePath == required);

  const std::vector<std::string> extensionPath = buildDeviceExtensionList(DynamicRenderingPath::Extension, required);
  CHECK(std::find(extensionPath.begin(), extensionPath.end(), "VK_KHR_swapchain") != extensionPath.end());
  CHECK(std::find(extensionPath.begin(), extensionPath.end(), "VK_SOME_OTHER_EXTENSION") != extensionPath.end());
}

TEST_CASE("buildDeviceExtensionList: deterministic, stable order across repeated calls",
          "[vulkan_backend][device_extension_list]") {
  const std::vector<std::string> required = {"VK_KHR_swapchain"};
  const std::vector<std::string> first = buildDeviceExtensionList(DynamicRenderingPath::Extension, required);
  const std::vector<std::string> second = buildDeviceExtensionList(DynamicRenderingPath::Extension, required);
  CHECK(first == second);
}

TEST_CASE("buildDeviceExtensionList: no duplicate names in either path's output",
          "[vulkan_backend][device_extension_list]") {
  const std::vector<std::string> required = {"VK_KHR_swapchain"};
  for (const DynamicRenderingPath path : {DynamicRenderingPath::Core, DynamicRenderingPath::Extension}) {
    const std::vector<std::string> result = buildDeviceExtensionList(path, required);
    const std::set<std::string> unique(result.begin(), result.end());
    CHECK(unique.size() == result.size());
  }
}
