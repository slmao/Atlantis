#include "vulkan_descriptor_pool_growth.h"

#include <numeric>

#include <catch2/catch_test_macros.hpp>

using atlantis::vulkan_backend::detail::descriptorPoolMaxSetsForGeneration;
using atlantis::vulkan_backend::detail::kDescriptorPoolMaxSetsByGeneration;
using atlantis::vulkan_backend::detail::kMaxDescriptorPoolCount;

// Spec 0021 D5/D6, ADR-0064, Plan 0021 V1-V3: the fixed, Approved
// descriptor-pool capacity table, and the one pure lookup function over
// it -- no real VkInstance/VkDevice anywhere in this file, matching
// vulkan_result_tests.cpp's own established "pure classification"
// precedent for this module.

TEST_CASE("kDescriptorPoolMaxSetsByGeneration matches the exact, Approved four-value sequence",
          "[vulkan_backend][descriptor_pool_growth]") {
  // V1: a direct, literal confirmation the fixed table matches Spec
  // 0021 D5/D6's own approved four-value sequence exactly.
  REQUIRE(kMaxDescriptorPoolCount == 4);
  REQUIRE(kDescriptorPoolMaxSetsByGeneration.size() == kMaxDescriptorPoolCount);
  REQUIRE(kDescriptorPoolMaxSetsByGeneration[0] == 4);
  REQUIRE(kDescriptorPoolMaxSetsByGeneration[1] == 8);
  REQUIRE(kDescriptorPoolMaxSetsByGeneration[2] == 16);
  REQUIRE(kDescriptorPoolMaxSetsByGeneration[3] == 32);
}

TEST_CASE("descriptorPoolMaxSetsForGeneration returns the table's own value for every legal generation",
          "[vulkan_backend][descriptor_pool_growth]") {
  // V2: all four legal generations, individually -- not merely the
  // table's own literal check (the prior TEST_CASE).
  REQUIRE(descriptorPoolMaxSetsForGeneration(0) == 4);
  REQUIRE(descriptorPoolMaxSetsForGeneration(1) == 8);
  REQUIRE(descriptorPoolMaxSetsForGeneration(2) == 16);
  REQUIRE(descriptorPoolMaxSetsForGeneration(3) == 32);
}

TEST_CASE("The four generations sum to the real, current hard ceiling on concurrent descriptor sets",
          "[vulkan_backend][descriptor_pool_growth]") {
  // V3: computed from the same constants Milestone 3's own GPU tests
  // read directly -- never a separately-hardcoded "60" anywhere in this
  // repository. Calling descriptorPoolMaxSetsForGeneration() with
  // generationIndex >= kMaxDescriptorPoolCount is a programmer error
  // (ATLANTIS_CHECK) -- matching AGENTS.md's own "programmer errors are
  // assertions, not error returns" rule, this is deliberately not
  // exercised here, matching this codebase's own existing precedent (no
  // test anywhere in this repository exercises an ATLANTIS_CHECK
  // failure path).
  const auto total = std::accumulate(kDescriptorPoolMaxSetsByGeneration.begin(),
                                      kDescriptorPoolMaxSetsByGeneration.end(), 0u);
  REQUIRE(total == 60);
}
