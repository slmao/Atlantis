#pragma once

#include <cstddef>
#include <cstdint>

#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/texture.h>
#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A sequence of recorded GPU commands (ADR-0020). Recording is only ever
// performed from inside a RenderGraph pass execution callback
// (render_graph::execute()) -- not enforced by this type itself (see
// ADR-0020's own note on this), enforced by inspection/review, matching
// this codebase's existing "no direct vkCmd* outside Vulkan Backend's
// CommandList implementation" convention. The two exceptions are
// beginRendering()/endRendering() (Spec 0007/ADR-0026), which are called
// only by render_graph::execute() itself, never by a pass callback.
// Caller-owned only while being recorded into; ownership transfers to
// Device at submit() -- a caller never destroys a CommandList it has
// submitted. Not copyable, not thread-safe.
class CommandList {
 public:
  virtual ~CommandList() = default;

  virtual void transitionResource(RenderTarget& target, ResourceState before, ResourceState after) = 0;
  virtual void clearColor(RenderTarget& target, ClearColorValue color) = 0;

  // Spec 0007 / ADR-0024 / ADR-0026: attachment scoping via Vulkan
  // dynamic rendering, called only by render_graph::execute() -- never by
  // a pass execution callback. depth may be nullptr (no depth attachment
  // for this scope) -- this round's own draw pass always supplies one,
  // but the type itself does not forbid omitting it. Precondition,
  // caller-enforced by execute()'s own algorithm, not re-checked here:
  // both attachments must already be in their declared ResourceState
  // (via transitionResource()) before this is called.
  virtual void beginRendering(RenderTarget& color, Texture* depth, ClearColorValue colorClear, float depthClear) = 0;
  virtual void endRendering() = 0;

  // Binds the pipeline subsequent draw calls use, until the next
  // bindPipeline() or the end of the current attachment scope.
  virtual void bindPipeline(Pipeline& pipeline) = 0;

  // buffer must have been created with BufferPurpose::Vertex/Index
  // respectively -- a mismatched purpose is a programmer error
  // (ATLANTIS_CHECK), not a silently accepted call.
  virtual void bindVertexBuffer(Buffer& buffer) = 0;
  virtual void bindIndexBuffer(Buffer& buffer) = 0;

  // buffer must have been created with BufferPurpose::Uniform.
  virtual void bindUniformBuffer(Buffer& buffer) = 0;

  // Records the per-draw-item object-to-world transform as a Vulkan push
  // constant (ADR-0025) -- not a second uniform buffer; see that ADR's
  // Decision for the correctness argument (a shared uniform buffer,
  // overwritten once per draw item during recording, would corrupt every
  // earlier draw item's transform by the time the GPU actually executes
  // any of them). sizeBytes must not exceed the bound Pipeline's
  // pushConstantSizeBytes.
  virtual void pushConstant(const void* data, std::size_t sizeBytes) = 0;

  // Records one indexed draw call against whatever pipeline/vertex/index/
  // binding state is currently bound.
  virtual void drawIndexed(std::uint32_t indexCount) = 0;
};

}  // namespace atlantis::rhi
