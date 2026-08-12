#include "dynamic_rendering_entry_points.h"

#include <cstring>
#include <string>

#include <catch2/catch_test_macros.hpp>

using atlantis::vulkan_backend::detail::DynamicRenderingPath;
using atlantis::vulkan_backend::detail::selectDynamicRenderingEntryPointNames;

// ADR-0024 "Accepted Amendment -- 2026-08-13" Section 3 / 6: GPU-independent
// unit tests for selectDynamicRenderingEntryPointNames().

TEST_CASE("selectDynamicRenderingEntryPointNames: Core requests the unsuffixed names",
          "[vulkan_backend][dynamic_rendering_entry_points]") {
  const auto names = selectDynamicRenderingEntryPointNames(DynamicRenderingPath::Core);
  REQUIRE(names.has_value());
  CHECK(std::strcmp(names->beginRenderingName, "vkCmdBeginRendering") == 0);
  CHECK(std::strcmp(names->endRenderingName, "vkCmdEndRendering") == 0);
}

TEST_CASE("selectDynamicRenderingEntryPointNames: Extension requests the KHR-suffixed names",
          "[vulkan_backend][dynamic_rendering_entry_points]") {
  const auto names = selectDynamicRenderingEntryPointNames(DynamicRenderingPath::Extension);
  REQUIRE(names.has_value());
  CHECK(std::strcmp(names->beginRenderingName, "vkCmdBeginRenderingKHR") == 0);
  CHECK(std::strcmp(names->endRenderingName, "vkCmdEndRenderingKHR") == 0);
}

TEST_CASE("selectDynamicRenderingEntryPointNames: Unavailable requests neither",
          "[vulkan_backend][dynamic_rendering_entry_points]") {
  const auto names = selectDynamicRenderingEntryPointNames(DynamicRenderingPath::Unavailable);
  CHECK_FALSE(names.has_value());
}

TEST_CASE("selectDynamicRenderingEntryPointNames: no cross-contamination between paths",
          "[vulkan_backend][dynamic_rendering_entry_points]") {
  const auto coreNames = selectDynamicRenderingEntryPointNames(DynamicRenderingPath::Core);
  const auto extensionNames = selectDynamicRenderingEntryPointNames(DynamicRenderingPath::Extension);
  REQUIRE(coreNames.has_value());
  REQUIRE(extensionNames.has_value());
  CHECK(std::strcmp(coreNames->beginRenderingName, extensionNames->beginRenderingName) != 0);
  CHECK(std::strcmp(coreNames->endRenderingName, extensionNames->endRenderingName) != 0);
  // Core's names must never be KHR-suffixed, and vice versa.
  CHECK(std::string(coreNames->beginRenderingName).find("KHR") == std::string::npos);
  CHECK(std::string(coreNames->endRenderingName).find("KHR") == std::string::npos);
  CHECK(std::string(extensionNames->beginRenderingName).find("KHR") != std::string::npos);
  CHECK(std::string(extensionNames->endRenderingName).find("KHR") != std::string::npos);
}
