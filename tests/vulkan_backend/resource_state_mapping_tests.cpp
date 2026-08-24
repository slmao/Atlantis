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

TEST_CASE("planTransition still asserts on a plausible-sounding but unlisted SampledTexture-adjacent pair",
          "[vulkan_backend][resource_state_mapping]") {
  // Spec 0016 adds exactly the two new entries above -- an unlisted pair
  // naming one of the new states, even one that sounds plausible, must
  // still fire the existing closed-table assertion (V14).
  std::vector<RecordedFailure> failures;
  ScopedFailureHandler handler(failures);

  static_cast<void>(planTransition(ResourceState::ColorAttachmentOutput, ResourceState::ShaderRead));

  REQUIRE_FALSE(failures.empty());
}
