#include "dynamic_rendering.h"

#include <catch2/catch_test_macros.hpp>

using atlantis::vulkan_backend::detail::decideDynamicRenderingPath;
using atlantis::vulkan_backend::detail::DynamicRenderingPath;

// Plan 0007 Section 15 / ADR-0024 "Accepted Amendment -- 2026-08-13"
// Section 6: exhaustive truth table over decideDynamicRenderingPath()'s
// six boolean parameters (physicalDeviceProperties2InstanceExtensionAvailable,
// instanceRequestedApiVersionAtLeast1_3, apiVersionAtLeast1_3,
// coreFeatureSupported, extensionAdvertised, extensionFeatureSupported).

TEST_CASE("decideDynamicRenderingPath collapses to Unavailable whenever the instance-level "
          "VK_KHR_get_physical_device_properties2 prerequisite is unavailable",
          "[vulkan_backend][dynamic_rendering]") {
  // Representative sample of the 2^5 combinations of the remaining five
  // booleans -- all must return Unavailable, per the function's own
  // short-circuit, regardless of instanceRequestedApiVersionAtLeast1_3.
  CHECK(decideDynamicRenderingPath(false, false, false, false, false, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(false, true, true, true, true, true) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(false, true, true, false, false, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(false, false, false, false, true, true) == DynamicRenderingPath::Unavailable);
}

TEST_CASE("decideDynamicRenderingPath: exhaustive 32-case table when the instance-level prerequisite is available",
          "[vulkan_backend][dynamic_rendering]") {
  constexpr bool kAvailable = true;

  for (const bool instanceRequestedApiVersionAtLeast1_3 : {false, true}) {
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, false, false, false,
                                      false) == DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, false, false, false, true) ==
          DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, false, false, true, false) ==
          DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, false, false, true, true) ==
          DynamicRenderingPath::Extension);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, false, true, false, false) ==
          DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, false, true, false, true) ==
          DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, false, true, true, false) ==
          DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, false, true, true, true) ==
          DynamicRenderingPath::Extension);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, true, false, false, false) ==
          DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, true, false, false, true) ==
          DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, true, false, true, false) ==
          DynamicRenderingPath::Unavailable);
    CHECK(decideDynamicRenderingPath(kAvailable, instanceRequestedApiVersionAtLeast1_3, true, false, true, true) ==
          DynamicRenderingPath::Extension);

    // apiVersionAtLeast1_3 && coreFeatureSupported == true: Core only
    // when instanceRequestedApiVersionAtLeast1_3 is also true -- this is
    // the amendment's own crux gate. When it is false, Core is never
    // selected regardless of device apiVersion/feature, and the outcome
    // falls through to whatever the Extension inputs allow.
    if (instanceRequestedApiVersionAtLeast1_3) {
      CHECK(decideDynamicRenderingPath(kAvailable, true, true, true, false, false) == DynamicRenderingPath::Core);
      CHECK(decideDynamicRenderingPath(kAvailable, true, true, true, false, true) == DynamicRenderingPath::Core);
      CHECK(decideDynamicRenderingPath(kAvailable, true, true, true, true, false) == DynamicRenderingPath::Core);
      CHECK(decideDynamicRenderingPath(kAvailable, true, true, true, true, true) == DynamicRenderingPath::Core);
    } else {
      CHECK(decideDynamicRenderingPath(kAvailable, false, true, true, false, false) ==
            DynamicRenderingPath::Unavailable);
      CHECK(decideDynamicRenderingPath(kAvailable, false, true, true, false, true) ==
            DynamicRenderingPath::Unavailable);
      CHECK(decideDynamicRenderingPath(kAvailable, false, true, true, true, false) ==
            DynamicRenderingPath::Unavailable);
      CHECK(decideDynamicRenderingPath(kAvailable, false, true, true, true, true) == DynamicRenderingPath::Extension);
    }
  }
}

TEST_CASE("decideDynamicRenderingPath: instance1.3 + device1.3 + coreFeature -> Core",
          "[vulkan_backend][dynamic_rendering]") {
  CHECK(decideDynamicRenderingPath(true, true, true, true, false, false) == DynamicRenderingPath::Core);
}

TEST_CASE("decideDynamicRenderingPath: instance1.0 + device1.3 + coreFeature + full KHR -> Extension",
          "[vulkan_backend][dynamic_rendering]") {
  CHECK(decideDynamicRenderingPath(true, false, true, true, true, true) == DynamicRenderingPath::Extension);
}

TEST_CASE("decideDynamicRenderingPath: instance1.0 + device1.3 + coreFeature + incomplete KHR -> Unavailable",
          "[vulkan_backend][dynamic_rendering]") {
  // Extension advertised but the feature itself not reported supported --
  // an incomplete/inconsistent KHR advertisement.
  CHECK(decideDynamicRenderingPath(true, false, true, true, true, false) == DynamicRenderingPath::Unavailable);
  // KHR extension not even advertised.
  CHECK(decideDynamicRenderingPath(true, false, true, true, false, false) == DynamicRenderingPath::Unavailable);
}

TEST_CASE("decideDynamicRenderingPath: instance1.3 + device1.3 + coreFeature + KHR absent -> Core",
          "[vulkan_backend][dynamic_rendering]") {
  CHECK(decideDynamicRenderingPath(true, true, true, true, false, false) == DynamicRenderingPath::Core);
}

TEST_CASE("decideDynamicRenderingPath: instance1.3 + device<1.3 + full KHR -> Extension",
          "[vulkan_backend][dynamic_rendering]") {
  CHECK(decideDynamicRenderingPath(true, true, false, false, true, true) == DynamicRenderingPath::Extension);
}

TEST_CASE("decideDynamicRenderingPath: missing feature/dependency -> Unavailable",
          "[vulkan_backend][dynamic_rendering]") {
  CHECK(decideDynamicRenderingPath(true, true, true, false, true, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(true, true, false, false, true, false) == DynamicRenderingPath::Unavailable);
  CHECK(decideDynamicRenderingPath(true, true, false, false, false, true) == DynamicRenderingPath::Unavailable);
}

TEST_CASE("decideDynamicRenderingPath: both Core and Extension available -> Core preferred (deterministic)",
          "[vulkan_backend][dynamic_rendering]") {
  CHECK(decideDynamicRenderingPath(true, true, true, true, true, true) == DynamicRenderingPath::Core);
}

TEST_CASE("decideDynamicRenderingPath: the amendment's own new case -- device >= 1.3 and coreFeatureSupported, but "
          "instance requested < 1.3 -- Extension if eligible, never Core",
          "[vulkan_backend][dynamic_rendering]") {
  CHECK(decideDynamicRenderingPath(true, false, true, true, true, true) == DynamicRenderingPath::Extension);
  CHECK(decideDynamicRenderingPath(true, false, true, true, false, false) == DynamicRenderingPath::Unavailable);
}
