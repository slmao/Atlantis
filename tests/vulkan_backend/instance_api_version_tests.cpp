#include "instance_api_version.h"

#include <vulkan/vulkan_core.h>

#include <catch2/catch_test_macros.hpp>

using atlantis::vulkan_backend::detail::decideRequestedInstanceApiVersion;

// ADR-0024 "Accepted Amendment -- 2026-08-13" Section 3 / 6: exhaustive,
// GPU-independent unit tests for decideRequestedInstanceApiVersion().

TEST_CASE("decideRequestedInstanceApiVersion: version-query function unavailable requests 1.0",
          "[vulkan_backend][instance_api_version]") {
  // loaderVersion is ignored whenever loaderVersionQueryAvailable is
  // false -- passing an arbitrarily high value here still must not
  // change the outcome.
  CHECK(decideRequestedInstanceApiVersion(false, 0) == VK_API_VERSION_1_0);
  CHECK(decideRequestedInstanceApiVersion(false, VK_API_VERSION_1_3) == VK_API_VERSION_1_0);
  CHECK(decideRequestedInstanceApiVersion(false, VK_MAKE_API_VERSION(0, 1, 4, 0)) == VK_API_VERSION_1_0);
}

TEST_CASE("decideRequestedInstanceApiVersion: loader 1.0/1.1/1.2 requests 1.0",
          "[vulkan_backend][instance_api_version]") {
  CHECK(decideRequestedInstanceApiVersion(true, VK_API_VERSION_1_0) == VK_API_VERSION_1_0);
  CHECK(decideRequestedInstanceApiVersion(true, VK_API_VERSION_1_1) == VK_API_VERSION_1_0);
  CHECK(decideRequestedInstanceApiVersion(true, VK_API_VERSION_1_2) == VK_API_VERSION_1_0);
}

TEST_CASE("decideRequestedInstanceApiVersion: loader 1.3 requests 1.3", "[vulkan_backend][instance_api_version]") {
  CHECK(decideRequestedInstanceApiVersion(true, VK_API_VERSION_1_3) == VK_API_VERSION_1_3);
}

TEST_CASE("decideRequestedInstanceApiVersion: loader 1.4+ requests 1.3", "[vulkan_backend][instance_api_version]") {
  CHECK(decideRequestedInstanceApiVersion(true, VK_MAKE_API_VERSION(0, 1, 4, 0)) == VK_API_VERSION_1_3);
  // A hypothetical future loader version -- still clamps to 1.3, this
  // repository never requests above 1.3.
  CHECK(decideRequestedInstanceApiVersion(true, VK_MAKE_API_VERSION(0, 2, 0, 0)) == VK_API_VERSION_1_3);
}

TEST_CASE("decideRequestedInstanceApiVersion: patch-version variations do not affect the choice",
          "[vulkan_backend][instance_api_version]") {
  // Below 1.3, any patch/variant on 1.2 still requests 1.0.
  CHECK(decideRequestedInstanceApiVersion(true, VK_MAKE_API_VERSION(0, 1, 2, 0)) == VK_API_VERSION_1_0);
  CHECK(decideRequestedInstanceApiVersion(true, VK_MAKE_API_VERSION(0, 1, 2, 999)) == VK_API_VERSION_1_0);
  // At or above 1.3, any patch variation still requests 1.3.
  CHECK(decideRequestedInstanceApiVersion(true, VK_MAKE_API_VERSION(0, 1, 3, 0)) == VK_API_VERSION_1_3);
  CHECK(decideRequestedInstanceApiVersion(true, VK_MAKE_API_VERSION(0, 1, 3, 5)) == VK_API_VERSION_1_3);
  CHECK(decideRequestedInstanceApiVersion(true, VK_MAKE_API_VERSION(0, 1, 3, 999)) == VK_API_VERSION_1_3);
}

TEST_CASE("decideRequestedInstanceApiVersion: output is constrained to only 1.0 or 1.3",
          "[vulkan_backend][instance_api_version]") {
  const std::uint32_t candidates[] = {
      0,
      VK_API_VERSION_1_0,
      VK_API_VERSION_1_1,
      VK_API_VERSION_1_2,
      VK_API_VERSION_1_3,
      VK_MAKE_API_VERSION(0, 1, 4, 0),
      VK_MAKE_API_VERSION(0, 3, 0, 0),
  };
  for (const bool available : {false, true}) {
    for (const std::uint32_t candidate : candidates) {
      const std::uint32_t result = decideRequestedInstanceApiVersion(available, candidate);
      CHECK((result == VK_API_VERSION_1_0 || result == VK_API_VERSION_1_3));
    }
  }
}
