#pragma once

namespace atlantis::rhi {

// A fixed graphics pipeline: one vertex + one fragment stage, a fixed
// vertex-input layout, depth-test/depth-write enabled, opaque
// rasterization, dynamic viewport/scissor (ADR-0025 -- so this object
// survives a resize without recreation), and color/depth attachment
// formats fixed at creation via dynamic-rendering pipeline info, never a
// VkRenderPass (ADR-0024). Move-only, single-owner, held behind
// std::unique_ptr<Pipeline>, owned exclusively by one Material
// (ADR-0022). If the bound color/depth format ever changes (ADR-0025's
// format-change contract), the *caller* destroys and recreates this
// object -- Pipeline itself has no "update format" method. Opaque: no
// accessor beyond the destructor -- CommandList::bindPipeline() takes it
// by reference and never needs to introspect it (mirrors
// SubmissionSignal's "intentionally declares no method" precedent, Spec
// 0006). Not internally thread-safe; caller-thread-only (ADR-0004).
class Pipeline {
 public:
  virtual ~Pipeline() = default;
};

}  // namespace atlantis::rhi
