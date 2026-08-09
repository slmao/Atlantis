#pragma once

#include <atlantis/rhi/command_list.h>

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

  enum class EventKind { Transition, Clear };

  std::vector<RecordedTransition> transitions;
  std::vector<RecordedClear> clears;
  std::vector<EventKind> events;  // interleaved order across all recorded calls
};

}  // namespace atlantis::render_graph::test
