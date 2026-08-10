# ADR 0025: RHI Minimal Graphics Pipeline, Binding, and Draw Command Surface

- **Status:** Proposed
- **Date:** 2026-08-11
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)

## Context

[ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
deliberately introduced `CommandList` with exactly two operations
(`transitionResource`, `clearColor`) and explicitly named "a real draw
call" as future Minimal Renderer scope not touched by that decision. That
future has arrived: Spec 0007's acceptance target is a real, visible,
depth-tested mesh, which needs a graphics pipeline object, a way to bind
vertex/index/uniform data to it, and a real draw command — none of which
exist anywhere in RHI today.

[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md) has
just introduced `Buffer` and `Texture`. [ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)
has fixed how the Vulkan Backend scopes a draw to its attachments. Someone
still has to decide the shape of the pipeline object itself, how
shader bytecode and vertex-layout/binding information reach it, and what
`CommandList` gains to actually bind and draw.

`Buffer`'s creation ([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md))
fixed three purposes: vertex, index, uniform. `Texture`'s creation fixed
one: depth attachment. `ResourceState`
([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
currently has exactly three variants, all describing a color image —
nothing describes a depth attachment's states, or the states a vertex/
index/uniform buffer might need tracked (this round, none do — see
Decision).

## Decision

**`Pipeline`** — a new RHI interface representing a fixed graphics
pipeline: one vertex shader stage, one fragment shader stage, a fixed
vertex input layout (position + one additional per-vertex attribute — the
exact attribute set is a Plan-stage detail, sufficient for this spec's
minimal material to visually distinguish geometry), depth-test-enabled/
depth-write-enabled fixed state, opaque (no blending) fixed rasterization
state, and — per
[ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md) — pipeline
rendering info naming its target color/depth attachment formats directly,
never a `VkRenderPass` handle. **Viewport and scissor are Vulkan dynamic
pipeline state** (`VK_DYNAMIC_STATE_VIEWPORT`/`VK_DYNAMIC_STATE_SCISSOR`),
not baked into the `Pipeline` object at creation time — `CommandList`'s
draw-recording path sets both, each time it records, to match whichever
`RenderTarget`/depth `Texture` extent that recording targets (read from
`RenderTarget::extent()`, unchanged by this decision). This is what lets
one `Material`'s `Pipeline`, created once, remain valid across every
future window resize without recreation — a static, pipeline-baked
viewport would instead require recreating the `Pipeline` on every resize,
which nothing in this spec's ownership model
([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md))
provides for and which this spec does not want to require.

**Attachment format staleness across a swapchain recreation is a known,
unresolved gap, not silently assumed away.** `Pipeline` bakes in its
target color/depth attachment *formats* (not extent — see above) at
creation time, because `VkPipelineRenderingCreateInfo`
([ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)) requires
them. `Presentation`'s existing swapchain format selection
([ADR-0016](0016-presentation-acquire-present-and-recreation-contract.md))
can, in principle, select a different (format, color space) pair on a
later recreation (e.g. the window moves to a different monitor with
different surface capabilities) — if that ever happens after a
`Material`'s `Pipeline` was already created against the original format,
that `Pipeline` becomes silently invalid against the new one. This spec's
own manual verification (see
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)
Testing & Verification Plan) exercises a single monitor/format for its
whole session and does not exercise this case — it is recorded as an
open risk in that spec's own Risks & Open Questions, not resolved here.

- **`Device` gains `createPipeline(PipelineCreateParams)`**, returning
  `atlantis::Result<std::unique_ptr<Pipeline>, PipelineCreateError>`
  (exact error type naming left to the Plan). `PipelineCreateParams`
  carries: raw SPIR-V bytecode for each shader stage (as an opaque byte
  span — RHI does not parse, validate, or reflect it; see
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)), the
  fixed vertex-input layout description, and one small, fixed set of
  per-draw parameters this round's material needs (see "Binding" below).
  No shader source, no shader language, and no compilation step is
  involved anywhere in RHI or Vulkan Backend — RHI's contract is "hand it
  compiled SPIR-V bytes plus a hand-specified layout, get back a usable
  `Pipeline`."
- **`Pipeline` is a move-only, single-owner RAII type**, same shape as
  `Buffer`/`Texture` ([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md))
  — held behind `std::unique_ptr<Pipeline>`, owned by `Material`
  ([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)).
  No pipeline cache, no pipeline deduplication, no pipeline-variant/
  permutation system of any kind — one `Material` owns exactly one
  `Pipeline`, created once, for as long as that `Material` is alive.

**Binding and drawing.** `CommandList` gains a small, fixed set of new
operations — the exact method names/signatures are a Plan-stage detail;
this decision fixes their existence, shape category, and ordering
contract:

- **Attachment scoping**: an operation pair (e.g.
  `beginRendering(RenderTarget& color, Texture* depth, ClearColorValue
  colorClear, float depthClear)`/`endRendering()`) that the Vulkan Backend
  implements via
  [ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)'s
  mechanism, unconditionally using `VK_ATTACHMENT_LOAD_OP_CLEAR` for both
  the color and depth attachment this round (`VK_ATTACHMENT_STORE_OP_STORE`
  for both, so the drawn result is actually kept) — this spec's single
  draw pass always fully repaints both attachments from a known clear
  value, never accumulates into a prior frame's contents, so there is no
  load-existing-contents case to design this round. **This supersedes any
  use of `CommandList::clearColor()` for this spec's own draw pass**:
  Spec 0006's `clearColor()`/`ResourceState::ColorAttachmentWrite`
  mechanism is a `vkCmdClearColorImage`-based transfer operation (see the
  `ResourceState` discussion below for why it is not, and must not be,
  reused here) and remains exactly as it was for its own existing
  consumer (`examples/frame_execution_demo`); this spec's draw pass
  clears via attachment load-op instead, as part of `beginRendering()`
  itself. `depth` is nullable — a draw with no depth attachment remains
  legal (this spec's own acceptance target always supplies one, but the
  type itself does not forbid omitting it). Per
  [ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md),
  RenderGraph's `execute()` — not a pass's own execution callback — is
  what calls this pair, derived automatically from a pass's declared
  attachment usages, exactly mirroring how `transitionResource()` is
  already derived and inserted automatically rather than hand-called.
- **`bindPipeline(Pipeline&)`** — binds the pipeline that subsequent draw
  calls (until the next `bindPipeline()` or the end of the current
  attachment scope) use.
- **`bindVertexBuffer(Buffer&)`**, **`bindIndexBuffer(Buffer&)`** — bind
  the vertex/index data a subsequent draw call reads. Both `Buffer`s must
  have been created with the matching `BufferPurpose`
  ([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)); a
  mismatched purpose is a programmer error (assertion), not a silently
  accepted call.
- **A minimal per-object binding mechanism** — the camera's view/
  projection data travels via the uniform `Buffer`
  ([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)),
  written once per frame by the caller and bound once per draw pass; the
  per-draw-item object-to-world transform **travels as a Vulkan push
  constant, not a second uniform buffer — fixed here, not left to the
  Plan.** This is a correctness decision, not a style preference: this
  spec's draw items are all recorded into the *same* `CommandList` within
  one `execute()` call, and recording happens before submission — if the
  per-object transform instead lived in a second, caller-writable uniform
  `Buffer`, that `Buffer` would hold only one value at submission time
  (whichever draw item wrote it last), silently corrupting every earlier
  draw item's transform in a multi-draw-item frame. A push constant is
  copied into the command buffer's own recorded state at
  `vkCmdPushConstants` time, so each recorded draw call carries its own
  correct transform regardless of how many draw items a frame has. This
  is the *only* per-object parameter this round's material supports — no
  general descriptor-set/binding-slot system, no arbitrary shader-resource
  binding API, no bindless mechanism of any kind.
- **`drawIndexed(std::uint32_t indexCount)`** — records one indexed draw
  call against whatever pipeline/vertex/index/binding state is currently
  bound. No instancing, no indirect draw, no multi-draw, no non-indexed
  draw path. **Indexed draw, rather than a plain non-indexed draw call, is
  a deliberate choice, not an unexamined default**: a non-indexed draw
  would remove `Buffer`'s index purpose and `bindIndexBuffer()` entirely,
  which is a real, available scope reduction this spec considered — but
  this spec's own acceptance target (Risks & Open Questions in
  [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)
  names a non-planar shape, e.g. a cube, needed to genuinely exercise
  depth testing) benefits concretely from vertex reuse (a cube needs 8
  vertices with an index buffer vs. 36 without), and indexed draw is
  otherwise the standard shape essentially every subsequent real mesh
  will need — keeping it now avoids near-certain, near-term rework for
  the very next mesh a future spec draws.
- All of the above remain subject to
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  existing rule: recording happens only from inside a RenderGraph pass
  execution callback, enforced by inspection, not by the type system.

**`ResourceState` gains new variants — critically, distinct from and
never conflated with `ColorAttachmentWrite`.** Reviewing this decision
against the Vulkan Backend's actual, already-shipped mapping for
`ResourceState::ColorAttachmentWrite`
(`src/vulkan_backend/src/resource_state_mapping.cpp`,
`undefinedToColorAttachmentWrite()`) shows it maps to
`VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`, `VK_ACCESS_TRANSFER_WRITE_BIT`,
and `VK_PIPELINE_STAGE_TRANSFER_BIT` — a **transfer-destination** state
for `vkCmdClearColorImage`
([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)),
not `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL` or a color-attachment-
output stage/access of any kind, despite its name. Reusing
`ColorAttachmentWrite` for this spec's real graphics-pipeline draw output
would therefore be a genuine correctness bug, not merely a naming
inconsistency: `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL` is not a valid
layout for an image used as a color attachment inside a
`vkCmdBeginRendering` scope
([ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)). It would
also make `execute()`'s draw-pass recognition
([ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md))
ambiguous: Spec 0006's own existing `clearColor()` pass already declares
a `ColorAttachmentWrite`-tagged usage, so a recognition rule keyed on that
same state would incorrectly wrap that pre-existing, unrelated pass in a
`beginRendering()`/`endRendering()` scope too — and `vkCmdClearColorImage`
is illegal inside such a scope. This decision therefore introduces:

- **`ResourceState::ColorAttachmentOutput`** — the real graphics-pipeline
  color-attachment-output-merger write state (Vulkan Backend mapping:
  `VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL`,
  `VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT`,
  `VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT`; the exact struct-level
  spelling is a Plan-stage detail, this ADR fixes the semantics and the
  name's distinctness from `ColorAttachmentWrite`). This is the state a
  bound `RenderTarget` carries while participating in this spec's draw
  pass; `ColorAttachmentWrite` remains exactly as it was, scoped to
  `clearColor()`'s transfer-style operation, untouched by this spec.
- **`ResourceState::DepthAttachmentReadWrite`** — the depth-test-read-plus-
  depth-write state for the bound depth `Texture` (Vulkan Backend mapping:
  `VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL`,
  `VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT`,
  `VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
  VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT`). Named `ReadWrite`, not
  `Write`, specifically to make its combined access semantics explicit —
  see
  [ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md)
  for why this is expressed as **exactly one** `RenderGraphBuilder::writes()`
  usage declaration (never a paired `reads()` + `writes()` on the same
  pass, which [ADR-0018](0018-render-graph-dependency-derivation-and-ordering.md)'s
  existing, unmodified rule already rejects) whose single `ResourceState`
  tag carries both access directions internally.
- **`execute()`'s draw-pass recognition
  ([ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md))
  is keyed exactly on `ColorAttachmentOutput`/`DepthAttachmentReadWrite`,
  and never on `ColorAttachmentWrite`.** This is what keeps Spec 0006's
  existing clear-only pass structurally unaffected by this spec's new
  derivation rule.
- Whatever states the new buffer purposes (vertex/index/uniform) need
  tracked for their own initial-transition bookkeeping remain a Plan-stage
  detail (a buffer's "state" this round is degenerate — exactly one valid
  state for its whole lifetime once uploaded), provided no such variant is
  named or mapped in a way that could be confused with
  `ColorAttachmentWrite`/`ColorAttachmentOutput`'s now-distinct meanings.

This is the first extension of `ResourceState` beyond its original three
color-only variants, anticipated (not designed) by
[ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
own Negative/Trade-offs — and the first point at which two states with
easily-confusable names carry meaningfully different Vulkan semantics,
which is why this decision calls out the distinction explicitly rather
than leaving it to be discovered during implementation.

**No general descriptor-set system, no bindless, no shader-resource
reflection, and no pipeline-permutation/variant cache is introduced.**
These remain future Shader-System/Minimal-Renderer-follow-on scope,
exactly as [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
already flagged for the general draw-call surface as a whole.

## Consequences

### Positive

- Gives this spec's acceptance target (a real, visible, depth-tested
  mesh) exactly the primitives it needs, in the same "minimal, no more
  than the concrete need" style
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  already established for `CommandList`'s first two operations.
- Keeping attachment-scoping call placement inside RenderGraph's
  `execute()` (per [ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md)),
  rather than a pass author's own responsibility, preserves
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
  existing "RenderGraph decides *when*, RHI decides *how*" split instead
  of introducing a second, inconsistent pattern for this one new
  operation pair.
- A fixed, small per-object binding mechanism (view/projection + one
  transform) is sufficient to prove the whole pipeline/binding/draw path
  end-to-end without inventing a general descriptor system this round has
  no second consumer to validate.
- Introducing `ColorAttachmentOutput`/`DepthAttachmentReadWrite` as
  distinct from `ColorAttachmentWrite`, rather than reusing the latter,
  avoids both a real layout-correctness bug (`ColorAttachmentWrite` maps
  to a transfer-destination layout, not a color-attachment-optimal one —
  see Decision above) and an `execute()` draw-pass-recognition ambiguity
  against Spec 0006's pre-existing `clearColor()` pass.

### Negative / Trade-offs

- `drawIndexed()`'s single-mesh, single-draw-call-per-invocation shape is
  not a stepping stone to instancing, indirect draw, or multi-mesh
  batching by construction — a future spec adding those will likely need
  to add new `CommandList` operations rather than extend this one,
  mirroring the same trade-off `clearColor()` already accepted in
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md).
- No descriptor-set/binding-slot abstraction means any future material
  needing more than "one uniform buffer, one per-object transform" (e.g. a
  texture, a second uniform block) requires real new RHI surface, not an
  extension of this round's binding mechanism.
- `ResourceState`'s extension for depth/buffer purposes adds real
  complexity to the enum and to `execute()`'s transition-insertion
  bookkeeping (now tracking more than one resource kind's states) — a
  necessary, not accidental, consequence of this spec's own scope.
- A push-constant-carried per-object transform is bounded by whatever
  push-constant byte budget the Vulkan Backend's pipeline layout allocates
  — Vulkan guarantees at least 128 bytes across all stages, comfortably
  enough for one 4×4 float matrix (64 bytes), but this ceiling is real and
  a future spec adding more per-object data (e.g. a second matrix, a
  material index) must account for it, possibly by moving to a per-object
  uniform-buffer-array/dynamic-offset scheme instead of extending push
  constants indefinitely.

## Alternatives Considered

- **A general descriptor-set/binding-slot system now**, so a future
  material with a texture or a second uniform block would already be
  served. Rejected — pulls a real Shader-System/general-material-system
  design into this spec, contradicting the same sequencing discipline
  Spec 0005's and Spec 0006's own Alternatives Considered already
  established for their respective rounds; no second consumer exists yet
  to validate a general binding model against.
- **Reflection-driven pipeline layout** (deriving vertex input/binding
  layout from the SPIR-V bytecode itself, rather than hand-specifying it
  alongside the bytes). Rejected for this round — see
  [ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md):
  reflection is explicitly Shader System's future responsibility, not
  something this spec's temporary shader-artifact path introduces early.
- **Push descriptors or bindless textures/buffers** for the per-object
  binding mechanism. Rejected: both are meaningfully more complex
  mechanisms this round's single-uniform-plus-one-transform need does not
  justify; a future spec with a real multi-material, multi-texture need
  can choose either against that concrete requirement.
- **Instanced or indirect draw support from the start**, anticipating a
  future need to draw many objects efficiently. Rejected: no current
  acceptance target needs more than a handful of individually-recorded
  `drawIndexed()` calls; adding instancing/indirect-draw plumbing now would
  be exactly the kind of premature generalization AGENTS.md's "No
  speculative abstraction" principle warns against.
