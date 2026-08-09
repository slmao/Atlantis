# ADR 0020: RHI Minimal GPU Resource, Command Recording, and Submission Interface

- **Status:** Proposed
- **Date:** 2026-08-09
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md)

## Context

Spec 0003 deliberately stopped at `Presentation`'s non-frame lifecycle: no
command list, no resource-state/barrier API, no queue submission of any
kind exists in RHI today. Spec 0005 built a GPU-independent RenderGraph
core specifically to avoid inventing that surface under pressure. Both
specs point at "a future spec" to add it once a real consumer — this
one — exists to validate the design against.

[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
already anticipates RHI's eventual responsibilities: "`Device`, command
recording (`CommandList`/`CommandBuffer`), resources (`Buffer`, `Texture`,
`Sampler`), pipeline/pipeline-state objects, `RenderTarget`, the
`Presentation` abstraction, and synchronization primitives." This ADR does
not need to invent that whole surface — only the minimal slice
[specs/0006](../specs/0006-rhi-render-graph-frame-execution-foundation.md)'s
acceptance bar (acquire → execute at least one GPU pass → submit →
present) actually requires: enough to record one resource-state
transition and one minimal drawable operation against a `RenderTarget`,
and enough to submit that recording to a queue with correct
CPU/GPU synchronization.

Building a general `Buffer`/`Texture`/pipeline-object system now would
compound this decision with Shader System's and Minimal Renderer's own
future scope — both explicitly out of this spec's Non-Goals — and would
repeat the exact over-scoping mistake Spec 0005's own Alternatives
Considered already rejected once for RenderGraph.

## Decision

RHI gains exactly the following minimal surface, and nothing more:

**`ResourceState`** — a small, backend-agnostic enum sufficient for this
round's one resource kind (a `RenderTarget`'s color image):
`Undefined`, `ColorAttachmentWrite`, `PresentSource`. Extending this enum
for buffers, depth attachments, shader-read states, etc. is explicitly
future work, gated on a real consumer (Minimal Renderer or a
resource-lifetime spec).

**`CommandList`** — an RHI interface representing one sequence of
recorded GPU commands:

- `transitionResource(RenderTarget&, ResourceState before, ResourceState after)`
  — the **only** resource-state transition primitive this round exposes.
  Backend-agnostic in signature; the Vulkan Backend's implementation is
  the sole place a `VkImageMemoryBarrier`/`vkCmdPipelineBarrier` (access
  masks, pipeline stages, layouts) is constructed, per AGENTS.md's
  existing "no direct `vkCmd*` calls outside the Vulkan Backend's
  `CommandList` implementation" rule — this ADR does not relax that rule,
  it gives `CommandList` its first real implementation to enforce it
  against.
- `clearColor(RenderTarget&, ClearColorValue)` — the **only** recordable
  drawable operation this round exposes. Deliberately not a general draw
  call, not a pipeline-bound operation, and not a shader invocation of any
  kind — it exists solely so this spec's "execute at least one GPU pass"
  acceptance bar is achievable without inventing pipeline objects,
  shaders, or a graphics-pipeline abstraction, all of which remain future
  Renderer/Shader System scope untouched by this decision.
- Recording is only ever performed from inside a RenderGraph pass's
  execution callback
  ([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)) —
  `CommandList` itself does not enforce this (it is a plain RHI type,
  usable by anyone holding one), but no code outside RenderGraph's
  execution path is introduced by this spec to obtain or record into one,
  preserving AGENTS.md's "RenderGraph is the mandatory path for GPU work"
  rule in practice, not just by omission.
- Not thread-safe; not copyable; owned by the caller that requested it
  from `Device`; must not outlive the `Device` it was created from — same
  ownership discipline as every other RHI type under
  [ADR-0003](0003-resource-rendertarget-ownership-model.md).

**`Device` gains two new operations:**

- `createCommandList()` — vends a `CommandList` the caller records into
  and later submits. Phase 1 does not pool or reuse command lists across
  frames beyond ordinary RAII destruction/recreation; an explicit pooling
  strategy is left to a future performance-motivated revision.
- `submit(CommandList, SubmitInfo)` — submits a recorded, non-empty
  `CommandList` to the device's graphics/present-capable queue.
  `SubmitInfo` carries: the wait semaphore and wait pipeline stage (the
  acquire-complete semaphore from
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)),
  the signal semaphore (`present()`'s wait target), and a fence the caller
  uses to know when the submission has finished executing on the GPU.
  Every `VkResult` along this path is checked and mapped to an explicit
  `atlantis::Result` error, never discarded.

**Single frame-in-flight baseline.** This round submits and waits on
exactly one frame's work being in flight at a time: the caller waits on
the previous frame's fence before recording/submitting the next. This is
a deliberate simplification, not a performance target — multiple frames
in flight (double/triple buffering of command lists and sync objects) is
a natural, anticipated extension, but is not designed or built here; see
Consequences.

**No general `Buffer`/`Texture`/`Sampler`/pipeline-object type is
introduced.** [ADR-0015](0015-vulkan-memory-allocation-deferred.md)'s GPU
memory allocation deferral is unaffected and not resolved by this
decision — `RenderTarget`'s backing image continues to be
`Presentation`-owned swapchain memory, never a general allocation this
round's `CommandList`/`Device` surface has to reason about.

## Consequences

### Positive

- Gives RenderGraph's execution phase
  ([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
  exactly the primitives it needs and no more, keeping this decision
  reviewable on its own terms rather than bundled with pipeline/shader
  design.
- Keeps every `vkCmd*` call and every `VkImageMemoryBarrier` construction
  inside the Vulkan Backend's `CommandList` implementation, satisfying
  AGENTS.md's existing rule concretely rather than only in principle.
- The single-frame-in-flight baseline keeps synchronization reasoning
  simple enough to verify by inspection and manual testing, consistent
  with this spec's non-functional goal of "does not stall, leak, or
  busy-spin" without introducing a frame-pacing/performance target.

### Negative / Trade-offs

- Single frame-in-flight means the CPU can stall waiting on the GPU more
  than a double/triple-buffered design would — an accepted, explicit
  Phase 1 simplification, not a hidden cost; a future spec must revisit
  this once real frame-time data motivates it.
- `clearColor()` as the only drawable operation is not a stepping stone to
  a general draw-call API by construction — Minimal Renderer's future spec
  will need to add a real command-recording surface (bound pipeline,
  vertex/index buffers, draw calls) essentially from scratch on top of
  `CommandList`, not by extending `clearColor()`.
- No buffer/texture resource type means this round cannot demonstrate
  anything beyond a solid-color pass — accepted, since that is exactly
  this spec's minimal acceptance bar, not a general rendering milestone.

## Alternatives Considered

- **Add a minimal graphics pipeline/shader-binding surface now, so the
  demonstrated pass is a real draw rather than a clear.** Rejected: pulls
  Shader System's and Minimal Renderer's own future scope into this spec,
  which AGENTS.md's Golden Rule and this repository's own sequencing
  (Spec 0005's Alternatives Considered rejected exactly this kind of
  bundling for the same reason) both argue against. A clear-color pass is
  sufficient to prove the acquire → RenderGraph-recorded work →
  submit → present path end-to-end, which is this spec's actual goal.
- **Support multiple frames in flight from the start.** Rejected for this
  round: adds synchronization-object pooling and lifetime complexity with
  no current performance requirement to justify it; the single-frame
  baseline is simpler to verify correct first, and multi-buffering can be
  added later without changing `CommandList`'s or `transitionResource()`'s
  public shape.
- **Give `CommandList` a general resource-transition primitive
  parameterized by an opaque resource handle, not specifically
  `RenderTarget&`.** Rejected for this round: there is exactly one
  resource kind (`RenderTarget`) to transition; a general resource
  abstraction with no second resource type to validate it against would
  be exactly the kind of premature generalization AGENTS.md's "No
  speculative abstraction" principle warns against. Widening
  `transitionResource()`'s parameter type is a natural, low-cost future
  change once `Buffer`/`Texture` exist.
- **Let `Presentation::present()` itself perform the final
  `ColorAttachmentWrite` → `PresentSource` transition internally, hiding
  it from the caller.** Considered, but placed instead on the RenderGraph
  execution side (see
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
  so that every resource-state transition — including the final one —
  goes through the same single `CommandList::transitionResource()`
  primitive and is recorded before submission, rather than splitting
  transition responsibility between `CommandList` and `Presentation`.
