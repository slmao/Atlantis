#pragma once

#include <atlantis/rhi/command_list.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Test-only rhi::CommandList implementation (Plan 0006 Section 13) that
// records which calls it received -- state, target identity, and order --
// for assertion, with no Vulkan device anywhere in this test binary.
namespace atlantis::render_graph::test {

struct RecordedTransition {
  const atlantis::rhi::RenderTarget* target;
  atlantis::rhi::ResourceState before;
  atlantis::rhi::ResourceState after;
};

struct RecordedClear {
  const atlantis::rhi::RenderTarget* target;
  atlantis::rhi::ClearColorValue color;
};

// Also usable as a bindable "RenderTarget" stand-in in tests -- carries
// no real GPU resource, just an identity (label) and fixed extent/format
// so ResourceBinding::target has something concrete to point at.
class FakeRenderTarget final : public atlantis::rhi::RenderTarget {
 public:
  explicit FakeRenderTarget(std::string_view label) : label_(label) {}

  [[nodiscard]] atlantis::rhi::Extent2D extent() const override { return atlantis::rhi::Extent2D{1, 1}; }
  [[nodiscard]] atlantis::rhi::Format format() const override { return atlantis::rhi::Format::Bgra8Unorm; }
  [[nodiscard]] std::string_view label() const { return label_; }

 private:
  std::string label_;
};

struct RecordedBeginRendering {
  const atlantis::rhi::RenderTarget* color;
  const atlantis::rhi::Texture* depth;  // nullptr if this scope had no depth attachment
  atlantis::rhi::ClearColorValue colorClear;
  float depthClear;
};

struct RecordedPushConstant {
  std::size_t sizeBytes;
};

// Spec 0007 / Plan 0007 Section 15: extended to record the new attachment-
// scoping and draw-call methods (beginRendering/endRendering/bindPipeline/
// bindVertexBuffer/bindIndexBuffer/bindUniformBuffer/pushConstant/
// drawIndexed) so GPU-independent tests (attachment_execution_tests.cpp,
// renderer_ownership_tests.cpp) can assert on them with no Vulkan device
// anywhere in this test binary.
class FakeCommandList final : public atlantis::rhi::CommandList {
 public:
  void transitionResource(atlantis::rhi::RenderTarget& target, atlantis::rhi::ResourceState before,
                           atlantis::rhi::ResourceState after) override {
    transitions.push_back(RecordedTransition{&target, before, after});
    events.push_back(EventKind::Transition);
  }

  void clearColor(atlantis::rhi::RenderTarget& target, atlantis::rhi::ClearColorValue color) override {
    clears.push_back(RecordedClear{&target, color});
    events.push_back(EventKind::Clear);
  }

  void beginRendering(atlantis::rhi::RenderTarget& color, atlantis::rhi::Texture* depth,
                       atlantis::rhi::ClearColorValue colorClear, float depthClear) override {
    beginRenderingCalls.push_back(RecordedBeginRendering{&color, depth, colorClear, depthClear});
    events.push_back(EventKind::BeginRendering);
  }

  void endRendering() override { events.push_back(EventKind::EndRendering); }

  void bindPipeline(atlantis::rhi::Pipeline& pipeline) override {
    boundPipelines.push_back(&pipeline);
    events.push_back(EventKind::BindPipeline);
  }

  void bindVertexBuffer(atlantis::rhi::Buffer& buffer) override {
    boundVertexBuffers.push_back(&buffer);
    events.push_back(EventKind::BindVertexBuffer);
  }

  void bindIndexBuffer(atlantis::rhi::Buffer& buffer) override {
    boundIndexBuffers.push_back(&buffer);
    events.push_back(EventKind::BindIndexBuffer);
  }

  void bindUniformBuffer(atlantis::rhi::Buffer& buffer) override {
    boundUniformBuffers.push_back(&buffer);
    events.push_back(EventKind::BindUniformBuffer);
  }

  void pushConstant(const void* data, std::size_t sizeBytes) override {
    const auto* bytes = static_cast<const std::byte*>(data);
    pushConstants.push_back(RecordedPushConstant{sizeBytes});
    pushConstantData.emplace_back(bytes, bytes + sizeBytes);
    events.push_back(EventKind::PushConstant);
  }

  void drawIndexed(std::uint32_t indexCount) override {
    drawIndexedCounts.push_back(indexCount);
    events.push_back(EventKind::DrawIndexed);
  }

  enum class EventKind {
    Transition,
    Clear,
    BeginRendering,
    EndRendering,
    BindPipeline,
    BindVertexBuffer,
    BindIndexBuffer,
    BindUniformBuffer,
    PushConstant,
    DrawIndexed,
  };

  std::vector<RecordedTransition> transitions;
  std::vector<RecordedClear> clears;
  std::vector<RecordedBeginRendering> beginRenderingCalls;
  std::vector<const atlantis::rhi::Pipeline*> boundPipelines;
  std::vector<const atlantis::rhi::Buffer*> boundVertexBuffers;
  std::vector<const atlantis::rhi::Buffer*> boundIndexBuffers;
  std::vector<const atlantis::rhi::Buffer*> boundUniformBuffers;
  std::vector<RecordedPushConstant> pushConstants;
  std::vector<std::vector<std::byte>> pushConstantData;  // parallel to pushConstants -- the actual bytes copied
  std::vector<std::uint32_t> drawIndexedCounts;
  std::vector<EventKind> events;  // interleaved order across all recorded calls
};

}  // namespace atlantis::render_graph::test
