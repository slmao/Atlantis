#include "resource_state_mapping.h"

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::ResourceState;
using atlantis::vulkan_backend::detail::planTransition;

TEST_CASE("planTransition maps Undefined -> ColorAttachmentWrite to a transfer-write discard barrier",
          "[vulkan_backend][resource_state_mapping]") {
  const auto plan = planTransition(ResourceState::Undefined, ResourceState::ColorAttachmentWrite);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  REQUIRE(plan.srcAccessMask == 0);
  REQUIRE(plan.dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
  REQUIRE(plan.srcStage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_TRANSFER_BIT);
}

TEST_CASE("planTransition maps ColorAttachmentWrite -> PresentSource to a transfer-to-present barrier",
          "[vulkan_backend][resource_state_mapping]") {
  const auto plan = planTransition(ResourceState::ColorAttachmentWrite, ResourceState::PresentSource);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  REQUIRE(plan.srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
  REQUIRE(plan.dstAccessMask == 0);
  REQUIRE(plan.srcStage == VK_PIPELINE_STAGE_TRANSFER_BIT);
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

TEST_CASE("planTransition maps Undefined -> PresentSource to a no-op-access discard barrier",
          "[vulkan_backend][resource_state_mapping]") {
  const auto plan = planTransition(ResourceState::Undefined, ResourceState::PresentSource);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
  REQUIRE(plan.srcAccessMask == 0);
  REQUIRE(plan.dstAccessMask == 0);
  REQUIRE(plan.srcStage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}
