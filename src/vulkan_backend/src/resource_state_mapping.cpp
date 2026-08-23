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

// Spec 0007 / ADR-0025: the real graphics-pipeline color-attachment-output
// write state -- deliberately distinct from undefinedToColorAttachmentWrite()
// above (Spec 0006's transfer-dst-based clearColor() state).
[[nodiscard]] ImageBarrierPlan undefinedToColorAttachmentOutput() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
  };
}

[[nodiscard]] ImageBarrierPlan colorAttachmentOutputToPresentSource() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = 0,
      .srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStage = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
  };
}

// Spec 0010/ADR-0040: the one and only new table entry this spec's design
// adds -- direct structural sibling of colorAttachmentOutputToPresentSource()
// above (same source layout/access/stage), destined for
// vkCmdCopyImageToBuffer() instead of vkQueuePresentKHR.
[[nodiscard]] ImageBarrierPlan colorAttachmentOutputToTransferSource() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
      .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT,
      .srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
      .dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
  };
}

[[nodiscard]] ImageBarrierPlan undefinedToDepthAttachmentReadWrite() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      // VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL (depth-only) requires
      // Vulkan 1.2 core or VK_KHR_separate_depth_stencil_layouts, neither
      // of which this Plan's instance/device creation raises/enables
      // (Section 8: apiVersion stays VK_API_VERSION_1_0). The combined
      // depth/stencil layout below has been valid, unconditionally, since
      // Vulkan 1.0 core and is correct here even though this round's one
      // DepthFormat (D32Sfloat) carries no stencil aspect.
      .newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
      .srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
  };
}

// Spec 0016/D5: SampledTexture's own upload-destination state. Same
// concrete layout/access/stage values as undefinedToColorAttachmentWrite()
// above (both are "first write, from nothing, via a transfer-style copy"),
// kept as a separate named function since the two states
// (ColorAttachmentWrite vs. TransferDestination) mean different things and
// are never interchangeable in the table dispatch below.
[[nodiscard]] ImageBarrierPlan undefinedToTransferDestination() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
      .dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
  };
}

// Spec 0016/D5: the buffer-to-image copy destination becoming available for
// fragment-shader sampling. Destination stage is fragment-shader-only --
// this Spec's fixed, single-purpose combined-image-sampler binding is
// VK_SHADER_STAGE_FRAGMENT_BIT only (PipelineCreateParams::
// hasSampledTextureBinding's own descriptor-set-layout binding, Spec 0016).
[[nodiscard]] ImageBarrierPlan transferDestinationToShaderRead() {
  return ImageBarrierPlan{
      .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT,
      .dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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
  if (before == ResourceState::Undefined && after == ResourceState::ColorAttachmentOutput) {
    return undefinedToColorAttachmentOutput();
  }
  if (before == ResourceState::ColorAttachmentOutput && after == ResourceState::PresentSource) {
    return colorAttachmentOutputToPresentSource();
  }
  if (before == ResourceState::ColorAttachmentOutput && after == ResourceState::TransferSource) {
    return colorAttachmentOutputToTransferSource();
  }
  if (before == ResourceState::Undefined && after == ResourceState::DepthAttachmentReadWrite) {
    return undefinedToDepthAttachmentReadWrite();
  }
  if (before == ResourceState::Undefined && after == ResourceState::TransferDestination) {
    return undefinedToTransferDestination();
  }
  if (before == ResourceState::TransferDestination && after == ResourceState::ShaderRead) {
    return transferDestinationToShaderRead();
  }

  ATLANTIS_CHECK_MSG(false, "planTransition() called with a (before, after) pair this round does not define");
  return undefinedToColorAttachmentWrite();
}

}  // namespace atlantis::vulkan_backend::detail
