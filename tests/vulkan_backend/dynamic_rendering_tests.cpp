#include "dynamic_rendering.h"

#include <catch2/catch_test_macros.hpp>

using atlantis::vulkan_backend::detail::decideDynamicRenderingPath;
using atlantis::vulkan_backend::detail::DynamicRenderingPath;

// Plan 0007 Section 15: exhaustive truth table over decideDynamicRenderingPath()'s
// five boolean parameters.

TEST_CASE("decideDynamicRenderingPath collapses to Unavailable whenever the instance-level "
          "VK_KHR_get_physical_device_properties2 prerequisite is unavailable",
          "[vulkan_backend][dynamic_rendering]") {
  // Representative sample of the 2^4 = 16 combinations of the remaining
  // four booleans -- all must return Unavailable, per the function's own
  // short-circuit.
  CHECK(decideDynamicRenderingPath(false, false, false, false, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(false, true, true, true, true) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(false, true, false, false, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(false, false, false, true, true) == DynamicRenderingPath::Unavailable);
}

TEST_CASE("decideDynamicRenderingPath: exhaustive 16-case table when the instance-level prerequisite is available",
          "[vulkan_backend][dynamic_rendering]") {
  constexpr bool kAvailable = true;

  CHECK(decideDynamicRenderingPath(kAvailable, false, false, false, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, false, false, false, true) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, false, false, true, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, false, false, true, true) == DynamicRenderingPath::Extension);
  CHECK(decideDynamicRenderingPath(kAvailable, false, true, false, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, false, true, false, true) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, false, true, true, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, false, true, true, true) == DynamicRenderingPath::Extension);
  CHECK(decideDynamicRenderingPath(kAvailable, true, false, false, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, true, false, false, true) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, true, false, true, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(kAvailable, true, false, true, true) == DynamicRenderingPath::Extension);
  CHECK(decideDynamicRenderingPath(kAvailable, true, true, false, false) == DynamicRenderingPath::Core);
  CHECK(decideDynamicRenderingPath(kAvailable, true, true, false, true) == DynamicRenderingPath::Core);
  CHECK(decideDynamicRenderingPath(kAvailable, true, true, true, false) == DynamicRenderingPath::Core);
  CHECK(decideDynamicRenderingPath(kAvailable, true, true, true, true) == DynamicRenderingPath::Core);
}
