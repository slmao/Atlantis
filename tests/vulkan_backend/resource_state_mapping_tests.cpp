#include "resource_state_mapping.h"

#include <atlantis/assert.h>

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using atlantis::rhi::ResourceState;
using atlantis::vulkan_backend::detail::planTransition;

namespace {

struct RecordedFailure {
  std::string expression;
  std::string message;
};

// Same RAII pattern as tests/render_graph/execution_tests.cpp's own
// ScopedFailureHandler -- installs a recording, non-terminating
// replacement failure handler for the lifetime of one test.
class ScopedFailureHandler {
 public:
  explicit ScopedFailureHandler(std::vector<RecordedFailure>& recorded)
      : previous_(atlantis::assertions::setFailureHandler([&recorded](const atlantis::AssertFailureInfo& info) {
          recorded.push_back({std::string(info.expression), std::string(info.message)});
        })) {}

  ~ScopedFailureHandler() { atlantis::assertions::setFailureHandler(std::move(previous_)); }

  ScopedFailureHandler(const ScopedFailureHandler&) = delete;
  ScopedFailureHandler& operator=(const ScopedFailureHandler&) = delete;

 private:
  atlantis::AssertFailureHandler previous_;
};

}  // namespace

TEST_CASE("planTransition maps Undefined -> ColorAttachmentWrite to a transfer-write discard barrier",
          "[vulkan_backend][resource_state_mapping]") {
  const auto plan = planTransition(ResourceState::Undefined, ResourceState::ColorAttachmentWrite);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  REQUIRE(plan.srcAccessMask == 0);
  REQUIRE(plan.dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
  // The acquire wait's stage, so the transition follows the wait (see below).
  REQUIRE(plan.srcStage == atlantis::vulkan_backend::detail::kAcquireWaitStage);
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
  REQUIRE(plan.srcStage == atlantis::vulkan_backend::detail::kAcquireWaitStage);
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

// The first barrier a frame records on an acquired swapchain image is one of
// the Undefined -> X entries. Its layout transition writes the image, so its
// source stage must be the stage submit() waits on the acquire semaphore at:
// with TOP_OF_PIPE the transition is unordered against the wait, the
// WRITE_AFTER_READ synchronization validation reports against
// vkAcquireNextImageKHR.
TEST_CASE("planTransition orders every Undefined -> X transition after the swapchain acquire wait",
          "[vulkan_backend][resource_state_mapping]") {
  using atlantis::vulkan_backend::detail::kAcquireWaitStage;
  CHECK(kAcquireWaitStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  for (const ResourceState after :
       {ResourceState::ColorAttachmentOutput, ResourceState::ColorAttachmentWrite, ResourceState::PresentSource}) {
    const auto plan = planTransition(ResourceState::Undefined, after);
    CHECK(plan.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
    CHECK(plan.srcAccessMask == 0);
    CHECK(plan.srcStage == kAcquireWaitStage);
  }
  const auto output = planTransition(ResourceState::Undefined, ResourceState::ColorAttachmentOutput);
  CHECK(output.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  CHECK(output.dstAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  CHECK(output.dstStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

TEST_CASE("planTransition maps ColorAttachmentOutput -> TransferSource to a color-output-to-transfer-read barrier",
          "[vulkan_backend][resource_state_mapping]") {
  // Spec 0010/ADR-0040: the one and only new table entry this spec's
  // design adds -- produced without asserting.
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  const auto plan = planTransition(ResourceState::ColorAttachmentOutput, ResourceState::TransferSource);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  REQUIRE(plan.srcAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  REQUIRE(plan.dstAccessMask == VK_ACCESS_TRANSFER_READ_BIT);
  REQUIRE(plan.srcStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_TRANSFER_BIT);
  REQUIRE(failures.empty());
}

TEST_CASE("planTransition still asserts on a still-unlisted (before, after) pair -- confirms exactly one new entry",
          "[vulkan_backend][resource_state_mapping]") {
  // Spec 0010 adds exactly one new planTransition() entry
  // (ColorAttachmentOutput -> TransferSource, above) -- every other
  // unlisted pair, including one that plausibly sounds adjacent, must
  // still fire the existing closed-table assertion.
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  static_cast<void>(planTransition(ResourceState::ColorAttachmentOutput, ResourceState::DepthAttachmentReadWrite));

  REQUIRE_FALSE(failures.empty());
}

TEST_CASE("planTransition maps Undefined -> TransferDestination to a transfer-write discard barrier",
          "[vulkan_backend][resource_state_mapping]") {
  // Spec 0016: SampledTexture's own upload-destination state -- same
  // concrete values as Undefined -> ColorAttachmentWrite above, kept as
  // its own table entry (see resource_state_mapping.cpp's own comment).
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  const auto plan = planTransition(ResourceState::Undefined, ResourceState::TransferDestination);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_UNDEFINED);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  REQUIRE(plan.srcAccessMask == 0);
  REQUIRE(plan.dstAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
  REQUIRE(plan.srcStage == VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_TRANSFER_BIT);
  REQUIRE(failures.empty());
}

TEST_CASE("planTransition maps TransferDestination -> ShaderRead to a transfer-to-fragment-read barrier",
          "[vulkan_backend][resource_state_mapping]") {
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  const auto plan = planTransition(ResourceState::TransferDestination, ResourceState::ShaderRead);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  REQUIRE(plan.srcAccessMask == VK_ACCESS_TRANSFER_WRITE_BIT);
  REQUIRE(plan.dstAccessMask == VK_ACCESS_SHADER_READ_BIT);
  REQUIRE(plan.srcStage == VK_PIPELINE_STAGE_TRANSFER_BIT);
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  REQUIRE(failures.empty());
}

TEST_CASE("planTransition maps ColorAttachmentOutput -> ShaderRead to a color-output-to-fragment-read barrier",
          "[vulkan_backend][resource_state_mapping]") {
  // Plan 0024 Milestone 2 (ADR-0068 D-3): the output-transform pass's
  // own read of the HDR intermediate the geometry pass just wrote --
  // this table entry was discovered missing during real-GPU testing
  // (Milestone 7), even though this exact transition was already
  // specified in Milestone 2's own text.
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  const auto plan = planTransition(ResourceState::ColorAttachmentOutput, ResourceState::ShaderRead);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  REQUIRE(plan.srcAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  REQUIRE(plan.dstAccessMask == VK_ACCESS_SHADER_READ_BIT);
  REQUIRE(plan.srcStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  REQUIRE(failures.empty());
}

TEST_CASE("planTransition maps DepthAttachmentReadWrite -> ShaderRead to a depth-output-to-fragment-read barrier",
          "[vulkan_backend][resource_state_mapping]") {
  // Plan 0027 Milestone 2 (ADR-0072 D-4): the main draw pass's own read
  // of the ShadowMap the shadow-casting pass just wrote -- discovered
  // missing from this table during Implementation, the same "found
  // missing, disclosed, added" pattern the ColorAttachmentOutput ->
  // ShaderRead entry above already establishes.
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  const auto plan = planTransition(ResourceState::DepthAttachmentReadWrite, ResourceState::ShaderRead);
  REQUIRE(plan.oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  REQUIRE(plan.newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  REQUIRE(plan.srcAccessMask == VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  REQUIRE(plan.dstAccessMask == VK_ACCESS_SHADER_READ_BIT);
  REQUIRE(plan.srcStage == (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT));
  REQUIRE(plan.dstStage == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  REQUIRE(failures.empty());
}

TEST_CASE("planTransition still asserts on a plausible-sounding but unlisted SampledTexture-adjacent pair",
          "[vulkan_backend][resource_state_mapping]") {
  // Spec 0016 adds exactly the two new entries above, and Plan 0024
  // Milestone 2 adds ColorAttachmentOutput -> ShaderRead (confirmed
  // real, above) -- an unlisted pair naming one of these states, even
  // one that sounds plausible, must still fire the existing closed-
  // table assertion (V14).
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  static_cast<void>(planTransition(ResourceState::ShaderRead, ResourceState::ColorAttachmentOutput));

  REQUIRE_FALSE(failures.empty());
}

// Plan 0056 M6: a sampled image read back after its last sampling.
TEST_CASE("planTransition maps ShaderRead -> TransferSource", "[vulkan_backend][resource_state]") {
  const auto plan = atlantis::vulkan_backend::detail::planTransition(atlantis::rhi::ResourceState::ShaderRead,
                                                                     atlantis::rhi::ResourceState::TransferSource);
  CHECK(plan.oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  CHECK(plan.newLayout == VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
  CHECK(plan.srcAccessMask == VK_ACCESS_SHADER_READ_BIT);
  CHECK(plan.dstAccessMask == VK_ACCESS_TRANSFER_READ_BIT);
  CHECK(plan.srcStage == VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  CHECK(plan.dstStage == VK_PIPELINE_STAGE_TRANSFER_BIT);
}

// Plan 0066 M3a (ADR-0133 D2): the two same-state entries -- no layout change,
// an attachment's earlier writes made available to a scope that loads it.
TEST_CASE("planTransition maps ColorAttachmentOutput -> ColorAttachmentOutput to a memory-only barrier",
          "[vulkan_backend][resource_state]") {
  const auto plan = atlantis::vulkan_backend::detail::planTransition(atlantis::rhi::ResourceState::ColorAttachmentOutput,
                                                                     atlantis::rhi::ResourceState::ColorAttachmentOutput);
  CHECK(plan.oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  CHECK(plan.newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
  CHECK(plan.srcAccessMask == VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);
  CHECK(plan.dstAccessMask == (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT));
  CHECK(plan.srcStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
  CHECK(plan.dstStage == VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

TEST_CASE("planTransition maps DepthAttachmentReadWrite -> DepthAttachmentReadWrite to a memory-only barrier",
          "[vulkan_backend][resource_state]") {
  const auto plan = atlantis::vulkan_backend::detail::planTransition(
      atlantis::rhi::ResourceState::DepthAttachmentReadWrite, atlantis::rhi::ResourceState::DepthAttachmentReadWrite);
  constexpr VkPipelineStageFlags kTests =
      VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
  CHECK(plan.oldLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  CHECK(plan.newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
  CHECK(plan.srcAccessMask == VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
  CHECK(plan.dstAccessMask ==
        (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT));
  CHECK(plan.srcStage == kTests);
  CHECK(plan.dstStage == kTests);
}

TEST_CASE("planTransition still asserts on any other before == after pair", "[vulkan_backend][resource_state]") {
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);
  static_cast<void>(planTransition(ResourceState::ShaderRead, ResourceState::ShaderRead));
  REQUIRE_FALSE(failures.empty());
}
