#include "vulkan_offscreen_target.h"

#include <atlantis/rhi/types.h>

#include <catch2/catch_test_macros.hpp>

// Plan 0056 M3 (P4; ADR-0108 D3): an OffscreenTarget's color image usage is
// unchanged unless the target is created sampled, which adds SAMPLED and
// nothing else. Existing creation sites (capture, the image-regression
// fixtures) default to not sampled.
TEST_CASE("an offscreen target's image usage: unchanged by default, plus SAMPLED when asked",
          "[vulkan_backend][offscreen_target]") {
  using atlantis::vulkan_backend::detail::offscreenTargetImageUsage;
  CHECK(offscreenTargetImageUsage(false) == (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT));
  CHECK(offscreenTargetImageUsage(true) ==
        (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT));
  CHECK(atlantis::rhi::OffscreenTargetCreateParams{}.sampled == false);
  CHECK_FALSE(atlantis::rhi::OffscreenTargetCreateParams{.sampled = true} == atlantis::rhi::OffscreenTargetCreateParams{});
}
