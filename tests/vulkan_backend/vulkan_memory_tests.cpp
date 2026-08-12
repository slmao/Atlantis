#include "vulkan_memory.h"

#include <catch2/catch_test_macros.hpp>

using atlantis::vulkan_backend::detail::selectMemoryTypeIndex;

namespace {

[[nodiscard]] VkPhysicalDeviceMemoryProperties makeSyntheticProperties() {
  VkPhysicalDeviceMemoryProperties properties{};
  properties.memoryTypeCount = 4;
  // index 0: device-local only
  properties.memoryTypes[0] = VkMemoryType{VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0};
  // index 1: host-visible only (not coherent)
  properties.memoryTypes[1] = VkMemoryType{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, 0};
  // index 2: host-visible + host-coherent
  properties.memoryTypes[2] =
      VkMemoryType{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0};
  // index 3: also host-visible + host-coherent (second candidate)
  properties.memoryTypes[3] =
      VkMemoryType{VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 0};
  return properties;
}

}  // namespace

TEST_CASE("selectMemoryTypeIndex finds an exact match", "[vulkan_backend][vulkan_memory]") {
  const auto properties = makeSyntheticProperties();
  const auto index = selectMemoryTypeIndex(properties, 0b0001u, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  REQUIRE(index.has_value());
  REQUIRE(*index == 0);
}

TEST_CASE("selectMemoryTypeIndex chooses the first matching index among several candidates",
          "[vulkan_backend][vulkan_memory]") {
  const auto properties = makeSyntheticProperties();
  const auto index = selectMemoryTypeIndex(properties, 0b1111u,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  REQUIRE(index.has_value());
  REQUIRE(*index == 2);  // first of {2, 3} that satisfies both the type filter bit and the property flags
}

TEST_CASE("selectMemoryTypeIndex returns an empty optional when no type matches",
          "[vulkan_backend][vulkan_memory]") {
  const auto properties = makeSyntheticProperties();
  // Type filter excludes every index that has HOST_COHERENT (2 and 3);
  // index 1 has HOST_VISIBLE but not HOST_COHERENT.
  const auto index = selectMemoryTypeIndex(properties, 0b0010u,
                                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  REQUIRE_FALSE(index.has_value());
}

TEST_CASE("selectMemoryTypeIndex returns an empty optional when the type filter excludes every type",
          "[vulkan_backend][vulkan_memory]") {
  const auto properties = makeSyntheticProperties();
  const auto index = selectMemoryTypeIndex(properties, 0u, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  REQUIRE_FALSE(index.has_value());
}
