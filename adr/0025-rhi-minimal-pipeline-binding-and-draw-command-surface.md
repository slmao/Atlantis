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
never a `VkRenderPass` handle.

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
  `beginRendering(RenderTarget& color, Texture* depth)`/`endRendering()`)
  that the Vulkan Backend implements via
  [ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md)'s
  mechanism. `depth` is nullable — a draw with no depth attachment remains
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
- **A minimal per-object binding mechanism** — carrying, at minimum, the
  camera's view/projection data (via the uniform `Buffer`
  [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  introduced) and, per draw item, an object-to-world transform. Whether
  the per-draw transform travels as a Vulkan push constant or as a second,
  small uniform binding is a Plan-stage detail; either way it is the
  *only* per-object parameter this round's material supports — no general
  descriptor-set/binding-slot system, no arbitrary shader-resource
  binding API, no bindless mechanism of any kind.
- **`drawIndexed(std::uint32_t indexCount)`** — records one indexed draw
  call against whatever pipeline/vertex/index/binding state is currently
  bound. No instancing, no indirect draw, no multi-draw, no non-indexed
  draw path — a single mesh's index buffer is this spec's only supported
  geometry-submission shape.
- All of the above remain subject to
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  existing rule: recording happens only from inside a RenderGraph pass
  execution callback, enforced by inspection, not by the type system.

**`ResourceState` gains new variants** for the resource kinds this spec
introduces — at minimum, a depth-attachment-write state and whatever
states the new buffer purposes (vertex/index/uniform) need tracked for
their own initial-transition bookkeeping (a buffer's "state" this round
is degenerate — it has exactly one valid state for its whole lifetime
once uploaded, so whether this needs a real enum variant per purpose or a
simpler always-valid convention is left to the Plan, provided it does not
regress [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
existing color-image variants or their meaning). This is the first
extension of `ResourceState` beyond its original three color-only
variants, anticipated (not designed) by that ADR's own Negative/
Trade-offs.

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
