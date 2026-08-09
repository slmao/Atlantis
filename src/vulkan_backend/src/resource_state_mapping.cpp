#include "resource_state_mapping.h"

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

namespace {

using atlantis::rhi::ResourceState;

[[nodiscard]] ImageBarrierPlan undefinedToColorAttachmentWrite() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
  };
}

[[nodiscard]] ImageBarrierPlan colorAttachmentWriteToPresentSource() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = 0,
      .srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
  };
}

[[nodiscard]] ImageBarrierPlan undefinedToPresentSource() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .srcAccessMask = 0,
      .dstAccessMask = 0,
      .srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
  };
}

}  // namespace

ImageBarrierPlan planTransition(ResourceState before, ResourceState after) {
  ATLANTIS_CHECK_MSG(before != after, "planTransition() called with before == after -- execute() never does this");

  if (before == ResourceState::Undefined && after == ResourceState::ColorAttachmentWrite) {
    return undefinedToColorAttachmentWrite();
  }
  if (before == ResourceState::ColorAttachmentWrite && after == ResourceState::PresentSource) {
    return colorAttachmentWriteToPresentSource();
  }
  if (before == ResourceState::Undefined && after == ResourceState::PresentSource) {
    return undefinedToPresentSource();
  }

  ATLANTIS_CHECK_MSG(false, "planTransition() called with a (before, after) pair this round does not define");
  return undefinedToColorAttachmentWrite();
}

}  // namespace atlantis::vulkan_backend::detail
