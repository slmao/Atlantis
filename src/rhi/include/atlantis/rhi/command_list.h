#pragma once

#include <cstddef>
#include <cstdint>

#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/pipeline.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/sampled_texture.h>
#include <atlantis/rhi/sampler.h>
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

  // Spec 0007 / ADR-0020's own anticipated low-cost future change: widens
  // transitionResource() to a depth Texture, via an overload rather than a
  // widened RenderTarget parameter -- keeps Spec 0006's RenderTarget
  // overload's own existing surface untouched. Used by
  // render_graph::execute() for the depth attachment's single
  // Undefined -> DepthAttachmentReadWrite transition (Section 7).
  virtual void transitionResource(Texture& target, ResourceState before, ResourceState after) = 0;

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

  // Spec 0010/ADR-0040: copies source's full, tightly-packed color image
  // into destination; destination must have been created with
  // BufferPurpose::Readback and be sized to match -- a mismatch is a
  // caller precondition violation, not checked here. Recording remains
  // legal only from inside a RenderGraph pass execution callback
  // (ADR-0020), the same, unenforced-by-the-type-system convention every
  // other CommandList method above already follows.
  virtual void copyRenderTargetToBuffer(RenderTarget& source, Buffer& destination) = 0;

  // Spec 0016/ADR-0056: a third transitionResource() overload, alongside
  // the RenderTarget/depth-Texture ones above -- same shape, new target
  // type.
  virtual void transitionResource(SampledTexture& target, ResourceState before, ResourceState after) = 0;

  // source must have been created with BufferPurpose::Staging.
  // destination must be in ResourceState::Undefined or
  // ResourceState::TransferDestination when this is recorded -- caller
  // precondition (ATLANTIS_CHECK), not checked here, matching every
  // other CommandList method's existing discipline. Tightly packed,
  // row-major, no padding -- matching copyRenderTargetToBuffer()'s own
  // bufferRowLength = 0 convention (ADR-0040).
  virtual void copyBufferToTexture(Buffer& source, SampledTexture& destination) = 0;

  // texture must be in ResourceState::ShaderRead when this is recorded
  // -- caller precondition, matching bindUniformBuffer()'s own
  // BufferPurpose precondition. Binds a combined image sampler at set
  // 0, binding 1, fragment stage (ADR-0056 Decision 9) -- the currently
  // bound Pipeline must have been created with
  // PipelineCreateParams::hasSampledTextureBinding = true.
  //
  // const&, unlike transitionResource(SampledTexture&, ...)/
  // copyBufferToTexture() above -- this call only reads the bound
  // texture/sampler's already-const-qualified accessors (VkImageView/
  // VkSampler) to populate a descriptor write; it does not transition or
  // otherwise mutate either resource. const is also what Material's own
  // borrowed sampledTexture()/sampler() accessors return (Spec 0016/D3),
  // so this is the only signature Renderer's own
  // cmd.bindTexture(*item.material->sampledTexture(), ...) call can pass
  // without an unsafe const_cast.
  virtual void bindTexture(const SampledTexture& texture, const Sampler& sampler) = 0;
};

}  // namespace atlantis::rhi
