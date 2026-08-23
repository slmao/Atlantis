# ADR 0056: Texture Upload, Resource State, and Descriptor Binding

- **Status:** Proposed
- **Date:** 2026-08-23
- **Deciders:** Pending Human Review (as part of Spec 0016)
- **Related Spec:** [specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md)

## Context

This repository's RHI has no CPU→GPU copy-command path today of any
kind. `Buffer` (`src/rhi/include/atlantis/rhi/buffer.h:9-16`) states
plainly: *"This round, every Buffer is host-visible and host-coherent
regardless of purpose ... (no staging/upload path this round)."* The
existing upload path for mesh data (`renderer::createMesh()`,
`src/renderer/src/mesh.cpp:11-41`) is a direct `std::memcpy` into a
permanently host-mapped buffer — no GPU-side copy command exists at all.
`ResourceState` (`types.h:56-63`) has exactly six values (`Undefined`,
`ColorAttachmentWrite`, `PresentSource`, `ColorAttachmentOutput`,
`DepthAttachmentReadWrite`, `TransferSource`); Vulkan Backend's own
`(before, after)` barrier-plan table
(`src/vulkan_backend/src/resource_state_mapping.cpp:104-131`) is
exhaustive by design — any pair not explicitly listed hits an
`ATLANTIS_CHECK_MSG(false, ...)` hard failure, never a silent
best-effort transition.

`CommandList::copyRenderTargetToBuffer()`
(`src/rhi/include/atlantis/rhi/command_list.h:77-84`, Spec 0010/
ADR-0040) is the only copy command that exists, and it moves data in the
opposite direction this Spec needs (GPU image → CPU-visible buffer, for
readback) — nothing copies a CPU-populated buffer into a GPU image.

RenderGraph's own execution model
(`src/render_graph/include/atlantis/render_graph/execution.h:41-49`)
binds exactly two resource kinds per `ResourceBinding`: `RenderTarget*
target` and `Texture* depthTexture` (depth-only). `execute()`
(`src/render_graph/src/execution.cpp:94-127`) dereferences one of these
two per binding to insert transitions — there is no third slot. AGENTS.md
states, without qualification: *"Render Graph is the mandatory path for
GPU work. No subsystem submits ad hoc, hand-scheduled GPU work outside
it"* (`AGENTS.md:142-143`), and the RenderGraph module boundary's own
Responsibilities line states the same constraint (*"no ad hoc
direct-submission rendering path is allowed to bypass it"*). This is not
advisory language — it is the same governing principle that already
shapes every existing GPU-work path in this codebase (clear, draw,
readback all run through RenderGraph passes today).

`Material` (`src/renderer/include/atlantis/renderer/material.h:18-32`)
today owns only a `Pipeline`; the entire descriptor-binding surface lives
on `CommandList`/`VulkanDevice`, hardcoded to exactly one binding — a
uniform buffer at set 0/binding 0, vertex stage
(`vulkan_device.cpp:808-861`), against a device-level descriptor pool
sized for exactly that one binding type
(`vulkan_device.cpp:1245-1254`, `maxSets = 4`, one
`VkDescriptorPoolSize` entry).

## Decision

1. **New `BufferPurpose::Staging`**, extending the existing four-value
   `BufferPurpose` enum exactly as `Readback` (Spec 0010) already did —
   host-visible, host-coherent, matching `Buffer`'s own existing,
   unmodified contract. A `Staging`-purpose buffer holds decoded pixel
   bytes transiently, between CPU load and the GPU copy below; it is not
   retained past the one-time upload that consumes it.
2. **New `ResourceState::TransferDestination` and
   `ResourceState::ShaderRead`**, extending the existing 6-value
   `ResourceState` enum. Vulkan Backend's exhaustive barrier-plan table
   (`resource_state_mapping.cpp`) gains exactly two new, explicit
   entries: `Undefined → TransferDestination` and
   `TransferDestination → ShaderRead` — no wildcard/catch-all transition,
   preserving the table's own existing exhaustiveness guarantee.
3. **New `CommandList::copyBufferToTexture(Buffer&, SampledTexture&)`**
   and a third `CommandList::transitionResource(SampledTexture&,
   ResourceState before, ResourceState after)` overload, alongside the
   two existing `RenderTarget`/depth-`Texture` overloads
   (`command_list.h:29,38`).
4. **The one-time texture upload runs as a genuine, minimal RenderGraph
   execution — never a raw `CommandList` sequence outside it.**
   `ResourceBinding` gains a third resource-carrying field, a generic
   `SampledTexture*`, alongside the existing `target`/`depthTexture`
   fields; `execute()`'s transition-insertion logic is extended to drive
   `Undefined → TransferDestination` (before the copy) and
   `TransferDestination → ShaderRead` (after it) for this binding kind.
   A caller builds, compiles, and executes one small, single-pass
   RenderGraph graph, once, containing exactly this upload — run before
   Runtime's (or, at this Spec's own scope, this Spec's own fixture's)
   first per-frame graph, never merged into or sharing state with any
   later per-frame graph.
5. **`Material` gains an optional, construction-time, borrowed
   `SampledTexture`/`Sampler` pair** (matching `DrawItem`'s existing
   non-owning-reference conventions for `Mesh`/`Material` itself). A new
   `CommandList::bindTexture(SampledTexture&, Sampler&)`, called inside
   `Renderer`'s existing per-`DrawItem` pass-callback loop
   (`src/renderer/src/renderer.cpp:26-31`), immediately alongside the
   existing `bindUniformBuffer()` call. Vulkan Backend's per-`Pipeline`
   descriptor-set-layout creation and the device-level descriptor pool
   each gain one new, fixed, second entry: binding 1,
   `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, fragment stage.
6. **Combined image sampler, not separate texture/sampler descriptor
   types**, for the one new Vulkan binding this ADR adds — one binding
   slot, one pool-size entry, one `VkDescriptorImageInfo` pairing an
   image view and a sampler at bind time. This is a decision about the
   Vulkan-level binding mechanism only; `Sampler`'s own independence at
   the RHI level (ADR-0055) is unaffected.
7. **Single mip level, fully synchronous upload.** Every `SampledTexture`
   this round has exactly one mip level; the upload's own RenderGraph
   execution completes (submitted and waited on) before any per-frame
   graph runs — no asynchronous or deferred upload, no partial-readiness
   state.

## Consequences

### Positive

- Fully respects AGENTS.md's own already-established, non-negotiable
  Golden Rule constraint — introduces no new "GPU work outside
  RenderGraph" precedent for any future spec to point back to.
- The new barrier-plan entries are explicit and additive; the existing
  table's own hard-failure-on-unlisted-pair behavior is preserved
  unchanged for every other transition.
- `Material`'s new binding is additive (a second, fixed slot) — the
  existing one-uniform-buffer contract, and every shader that declares
  only it, continues to validate and bind exactly as before.
- Combined image sampler keeps the Vulkan descriptor-pool/layout change
  to the minimum needed for this Spec's own one-texture, one-sampler
  scope.

### Negative / Trade-offs

- RenderGraph's `ResourceBinding` union grows a third case, and
  `execute()` grows new branch logic for it — real, disclosed complexity
  this Spec's own suggested core scope did not originally anticipate
  (found only by checking AGENTS.md's own constraint against the actual
  proposed design, not assumed away).
- A one-time, single-pass RenderGraph execution per texture is not
  designed to batch multiple textures' uploads into one pass/graph —
  acceptable for this Spec's own one-texture scope; a future spec with
  many textures to upload at once will need to revisit this.
- The device-level descriptor pool's fixed `maxSets`/pool-size-entry
  model (already a known, narrow, hardcoded shape before this ADR) now
  carries two hardcoded entry kinds instead of one — still not a general
  descriptor-pool sizing strategy, deferred exactly as before.

## Alternatives Considered

- **A raw, one-time `CommandList` sequence issued directly against
  `Device`, bypassing RenderGraph.** Rejected outright — this was this
  Spec's own first-drafted design and was corrected during self-review
  specifically because AGENTS.md forbids it without qualification; not a
  style preference.
- **Teach RenderGraph to manage the texture's full lifecycle (creation,
  upload, and every subsequent per-frame binding) as a tracked,
  persistent resource.** Rejected as more machinery than this Spec's own
  one-time-upload-then-read-only-forever shape needs; RenderGraph tracks
  the upload transition only, not the texture's own creation or its
  read-only use in later, unrelated per-frame graphs.
- **Separate, non-combined texture/sampler descriptor types.** Rejected
  — more descriptor-pool/layout complexity for no benefit until a real
  consumer needs to reuse one sampler across many images independently
  bound.
- **A general, variable-length descriptor-binding list on `Material`.**
  Rejected as premature — matches this codebase's own "one fixed,
  hardcoded contract, extended only when a real need appears" discipline
  already established by `minimalRendererExpectedDescriptorContract()`.
