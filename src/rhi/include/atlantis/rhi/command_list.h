#pragma once

#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A sequence of recorded GPU commands (ADR-0020). Exactly two recordable
// operations exist this round -- no general draw call, no pipeline
// binding. Recording is only ever performed from inside a RenderGraph
// pass execution callback (render_graph::execute()) -- not enforced by
// this type itself (see ADR-0020's own note on this), enforced by
// inspection/review, matching this codebase's existing "no direct
// vkCmd* outside Vulkan Backend's CommandList implementation"
// convention. Caller-owned only while being recorded into; ownership
// transfers to Device at submit() -- a caller never destroys a
// CommandList it has submitted. Not copyable, not thread-safe.
class CommandList {
 public:
  virtual ~CommandList() = default;

  virtual void transitionResource(RenderTarget& target, ResourceState before, ResourceState after) = 0;
  virtual void clearColor(RenderTarget& target, ClearColorValue color) = 0;
};

}  // namespace atlantis::rhi
