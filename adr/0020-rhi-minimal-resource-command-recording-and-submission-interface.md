# ADR 0020: RHI Minimal GPU Resource, Command Recording, and Submission Interface

- **Status:** Accepted
- **Date:** 2026-08-09
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-09; see
  [specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md)'s
  Human Review Approval note for the full approval record this ADR's
  Decision is part of, including the explicit confirmation that
  `Device::submit()` takes ownership of `CommandList` and manages
  single-frame-in-flight submission lifetime internally.
- **Related Spec:** [specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md) (`Approved`)

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
  ([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)).
  `CommandList` itself does not enforce this at the type level — it is a
  plain RHI type, and nothing prevents a caller holding one from calling
  `transitionResource()`/`clearColor()` directly. This spec's own
  implementation introduces no code path that does so outside a
  RenderGraph pass callback, and the spec's Acceptance Criteria require
  this to hold by inspection — the same enforcement model AGENTS.md
  already relies on for "no direct `vkCmd*` call outside the Vulkan
  Backend's `CommandList` implementation" (also a convention verified by
  review/inspection, not a compiler-enforced guarantee). This is a
  documented, actively-relied-on discipline, not a structural guarantee —
  see Consequences.
- Caller-owned while being recorded into; not thread-safe; not copyable.
  **Ownership transfers to `Device` at the moment it is passed to
  `submit()`** (see below) — a caller does not hold, retain, or destroy a
  `CommandList` after submitting it.

**`Device` gains two new operations:**

- `createCommandList()` — vends a `CommandList` the caller records into
  and then submits exactly once. Phase 1 does not pool or reuse command
  lists across frames beyond `Device`'s own internal single-slot retention
  described below; an explicit pooling strategy for more than one
  in-flight command list is left to a future performance-motivated
  revision.
- `submit(CommandList commandList, WaitOn waitOn)` — **takes ownership**
  of `commandList` (moved in, not borrowed) and submits it to the
  device's graphics/present-capable queue, after first waiting on
  whatever the *previous* call to `submit()` on this `Device` needs
  waited on (see "Single frame-in-flight baseline" below — this is what
  makes ownership transfer safe: the caller never has to decide when a
  `CommandList` is safe to destroy). `waitOn` is an opaque reference to
  the acquire-complete signal a `RenderTarget` carries internally (see
  [ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)) —
  never a raw semaphore handle the caller constructs or owns itself. On
  success, `submit()` returns an opaque `SubmissionSignal` value the
  caller passes to `Presentation::present()` as the "render finished"
  signal to wait on before presenting.
  `Device` owns and internally manages whatever semaphore(s)/fence(s) this
  requires — **this spec does not introduce a general, publicly
  constructible `Semaphore`/`Fence` RHI type.** Exposing sync objects only
  as opaque tokens returned by/threaded through `submit()`/`present()`
  avoids inventing a general synchronization-primitive surface this
  round's one submission pattern has no second use for. Every `VkResult`
  along this path is checked and mapped to an explicit `atlantis::Result`
  error, never discarded.

**Single frame-in-flight baseline, and the exact ownership/lifetime
sequencing it fixes.** `Device` submits and waits on exactly one frame's
work being in flight at a time, and owns this bookkeeping entirely
internally — a caller has no fence or `CommandList` lifetime decision to
make:

- Internally, `Device` retains at most one previously-submitted
  `CommandList` (plus its associated fence) at a time.
- On a `submit()` call, `Device` first checks whether it is holding a
  prior submission: if so, it waits on that submission's fence, **then**
  destroys/releases the prior `CommandList` it was retaining, then
  proceeds with the new submission. If this is the first `submit()` call
  ever made on this `Device` (no prior submission retained), this wait
  step is skipped entirely — there is no "first frame has no fence to
  wait on" special case for a caller to handle, because the caller never
  sees a fence at all.
- This fixes, structurally rather than by caller discipline, the
  otherwise-easy-to-get-wrong ordering hazard of destroying/resetting a
  `CommandList` (or its underlying command buffer) before the GPU has
  actually finished executing it — a caller that only ever calls
  `createCommandList()` → record → `submit()` cannot get this wrong,
  because `Device` never returns the `CommandList` back to the caller for
  it to manage.

This is a deliberate simplification, not a performance target — multiple
frames in flight (a pool of retained command lists/sync objects instead
of `Device`'s single retained slot) is a natural, anticipated extension,
but is not designed or built here; see Consequences.

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
- `submit()` taking ownership of `CommandList` and `Device` internally
  retaining/releasing it against its own fence removes an entire class of
  caller-discipline bugs (destroying a command buffer the GPU is still
  executing, mishandling the first-submission-has-no-prior-fence case)
  without requiring the caller to reason about fence lifetime at all.
- Keeping `Semaphore`/`Fence` as opaque, `Device`-internal state rather
  than a new public RHI type keeps this round's public surface to exactly
  `ResourceState`, `CommandList`, `SubmissionSignal`, and `Device`'s two
  new methods — no general synchronization-primitive API is introduced
  ahead of a second use case that would justify one.

### Negative / Trade-offs

- Single frame-in-flight means the CPU can stall waiting on the GPU more
  than a double/triple-buffered design would — an accepted, explicit
  Phase 1 simplification, not a hidden cost; a future spec must revisit
  this once real frame-time data motivates it.
- `Device` now holds meaningful internal state across calls (a retained
  `CommandList` and its fence) rather than being a stateless factory for
  `CommandList` — a small increase in `Device`'s own implementation
  complexity, accepted as the cost of removing that complexity from every
  caller instead.
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
- **Expose raw `Semaphore`/`Fence` as public RHI types with their own
  creation functions, and have `SubmitInfo` accept them directly from the
  caller.** Considered and rejected: this round has exactly one
  submission pattern (one `CommandList`, one wait signal, one completion
  signal) with no second use case to validate a general synchronization-
  primitive type against, and pushing semaphore/fence lifetime
  correctness onto every caller reintroduces exactly the kind of
  ordering hazard `Device`-internal ownership (this ADR's actual Decision)
  exists to remove. A future spec may introduce a real `Semaphore`/`Fence`
  RHI type once a second consumer (e.g. multiple frames in flight, or a
  compute/graphics queue handoff) motivates one.
- **Have the caller (Runtime-equivalent code) retain the previous frame's
  `CommandList` and explicitly wait on its fence before destroying it**,
  rather than transferring ownership to `Device` on `submit()`. Rejected:
  this is exactly the caller-discipline pattern most prone to being
  silently gotten wrong (an early-return/error branch that skips the wait,
  a refactor that reorders destruction before the wait) — folding it into
  `Device`'s own `submit()` implementation makes the correct sequencing
  structural instead of a documented caller obligation.
- **Let `Presentation::present()` itself perform the final
  `ColorAttachmentWrite` → `PresentSource` transition internally, hiding
  it from the caller.** Considered, but placed instead on the RenderGraph
  execution side (see
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
  so that every resource-state transition — including the final one —
  goes through the same single `CommandList::transitionResource()`
  primitive and is recorded before submission, rather than splitting
  transition responsibility between `CommandList` and `Presentation`.
