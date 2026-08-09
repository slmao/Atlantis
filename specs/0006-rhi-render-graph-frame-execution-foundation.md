# Spec: RHI / RenderGraph Frame Execution Foundation

- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; human authorship/ownership of this spec is pending
  confirmation at Human Review.
- **Created:** 2026-08-09
- **Related Plan(s):** None yet — a plan may be drafted only after this
  spec (and the ADRs below) reach `Approved`/`Accepted` and pass Human
  Review, per [AGENTS.md](../AGENTS.md).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)–[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
  (all `Accepted`). See **Architectural Impact** below — three new
  decisions are identified and drafted alongside this spec as
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md),
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
  and
  [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  — all `Proposed`, none yet `Accepted`.

## Summary

This spec closes the gap Spec 0003 and Spec 0005 both deliberately left
open: a real, GPU-visible frame. It extends RHI with the minimal
`RenderTarget` type, `Presentation` acquire/present protocol, and
`CommandList`/submission interface a frame actually needs, and extends
RenderGraph with an execution capability that turns a compiled graph
(Spec 0005) into recorded, barrier-correct GPU commands against those RHI
resources. It does **not** design a Renderer, a shader system, a general
resource system, or resource-lifetime/aliasing — it is scoped to exactly
the "acquire → RenderGraph-recorded work → submit → present" bundle
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
deferred in full, plus the minimal `CommandList` surface RenderGraph needs
to actually record something into it, per Spec 0005's own stated
boundary ("a future spec... consuming both that new RHI surface and this
spec's compiled pass order").

## Motivation / Problem Statement

Spec 0003 delivered `Presentation`'s **non-frame** lifecycle only —
construction, resize-driven recreation, destruction — and explicitly
excluded acquire, present, `RenderTarget`, and any command
recording/submission, bundling all of it into "a later, approved
RenderGraph specification." Spec 0005 delivered RenderGraph's
**GPU-independent** graph-compilation core only — pass/resource
declaration, dependency derivation, deterministic ordering — and
explicitly excluded execution, RHI resource binding, and any Vulkan call
of any kind, again pointing at "a future spec."

Both specs were right to stop where they did — inventing acquire/present
without a RenderGraph to validate it against (Spec 0003's own reasoning),
or inventing RHI execution without an approved acquire/present contract
to build on (Spec 0005's own reasoning), would each have been exactly the
kind of piecemeal, unreviewed architecture the Golden Rule exists to
prevent. But the result is that **nothing in this repository can put a
pixel on screen yet**, and
[docs/project-blueprint.md](../docs/project-blueprint.md)'s own Milestone
2 entry says so directly: RenderGraph Foundation "deliberately excludes
resource lifetime, command recording/submission, resource-state/barrier
resolution, and any integration with `Presentation`'s acquire/present (the
bundle Milestone 1 deferred)."

The Candidate Spec Backlog in
[specs/README.md](README.md) lists **Minimal Renderer** as the very next
candidate after Spec 0005, depending only on "Spec 0005 (RenderGraph
Foundation) — `Approved`." Read literally, that ordering suggests a
Minimal Renderer spec could be drafted directly against Spec 0005's
GPU-independent graph core. It cannot: Spec 0005's own Non-Goals exclude
"real GPU submission or queue scheduling of any kind," "Vulkan barriers,
image layout transitions, or any synchronization primitive," and "`
Presentation` acquire/present, or any change to `Presentation`'s existing
non-frame lifecycle contract." A Renderer spec attempting to draw a mesh
would immediately need all three — and, per the Golden Rule, would either
have to invent them ad hoc (exactly the failure mode this document exists
to prevent) or stall waiting on a foundation spec that does not yet exist.
**This spec is that missing foundation.** It does not reorder or edit the
backlog/roadmap documents themselves — see Out of Scope / Future Work —
it only states, for the human reviewer's benefit, that Minimal Renderer's
own future spec has an undeclared dependency on this one's outcome.

## Goals

- Define `RenderTarget` as a concrete RHI public type: a non-owning,
  frame-scoped borrow representing one presentable color attachment, per
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md) and
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md)'s
  already-`Accepted` ownership model.
- Define `Presentation::acquireNextTarget()` and
  `Presentation::present(RenderTarget)`, resolving in full the bundle
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
  deferred: acquire's shape, `RenderTarget`'s frame ownership,
  acquire-complete synchronization, graph-to-present synchronization,
  image layout handoff, and present's own shape and out-of-date/
  suboptimal handling.
- Define the minimal RHI `CommandList`/`ResourceState`/`Device::submit()`
  surface needed to record and submit exactly one resource-state
  transition and one drawable operation (`clearColor`) against a
  `RenderTarget` — no more.
- Extend RenderGraph (Spec 0005) with an execution capability: a
  per-pass execution callback, a `ResourceState` tag on resource usages,
  a frame-scoped external-resource-binding mechanism, and an `execute()`
  entry point that records — but does not submit or present — GPU work
  in compiled pass order.
- Fix, as a reviewed architectural boundary rather than an
  implementation-time improvisation, the responsibility split between
  RenderGraph (decides *when*/*between what states* a transition is
  needed, from already-compiled dependency data) and RHI/Vulkan Backend
  (decides *how* to perform one).
- Handle window resize, zero-extent (minimize), and swapchain
  out-of-date/suboptimal conditions correctly across the whole
  acquire/execute/submit/present cycle — not just at construction/
  recreation time, as Spec 0003 scoped it.
- Verify all of the above end-to-end on Windows with a real GPU: acquire a
  frame target, execute at least one GPU pass through RenderGraph and
  RHI, submit, and present a visible frame, with correct behavior across
  resize and minimize/restore, and Vulkan Validation Layers running clean
  throughout.
- Resolve the three architectural decisions this spec identifies via
  dedicated ADRs
  ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md),
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
  [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)),
  so a future Minimal Renderer spec inherits a settled frame-execution
  contract instead of having to invent one.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Atlantis Renderer** — no scene/mesh/camera/material concept, no
  `src/renderer/` directory. This spec's own manual/verification
  composition (mirroring `examples/rhi_vulkan_demo`) is not a preview of
  Renderer.
- **Shader System** — no shader authoring, compilation, reflection, or
  SPIR-V of any kind. `CommandList::clearColor()` is the only drawable
  operation this spec introduces, deliberately not pipeline-bound or
  shader-driven — see
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md).
- **Graphics/compute pipeline objects** (`VkPipeline` or any
  backend-agnostic equivalent), vertex/index buffers, or any general draw
  call. Left entirely to a future Minimal Renderer / Shader System spec.
- **General RHI resources** — no `Buffer`, `Texture`, or `Sampler` type.
  `RenderTarget` remains the only concrete resource type RHI exposes
  after this spec, unchanged in kind from
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)'s
  original scope, just given a concrete shape.
- **A GPU memory allocator (VMA or hand-rolled).**
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s deferral
  is unaffected — `RenderTarget`'s backing memory remains
  `Presentation`-owned swapchain memory throughout this spec.
- **Resource lifetime, aliasing, or a resource-versioning model.** Spec
  0005's Non-Goals on this point are unchanged; this spec adds a
  `ResourceState` tag for transition bookkeeping only, never a lifetime
  interval or a physical-memory-reuse plan.
- **Any caller-authored pass-to-pass dependency edge, or any pass
  culling.** Spec 0005's dependency-derivation and pass-retention rules
  ([ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md))
  are unchanged and not reopened by this spec.
- **Multiple frames in flight / double- or triple-buffered command
  recording.** Phase 1 baseline for this spec is a single frame in flight
  — see
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md).
- **Multi-threaded command recording, submission, or graph execution; any
  job/task system.** Phase 1's single-logical-frame-thread baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is unchanged.
- **Android, iOS, or a second graphics backend of any kind**, and no
  abstraction knob added "for" one. Windows/Vulkan only, per
  [AGENTS.md](../AGENTS.md).
- **Headless rendering, image regression testing.** Windowed ships first,
  per [AGENTS.md](../AGENTS.md); this spec is squarely part of the
  windowed path.
- **GPU-driven rendering, neural rendering/shading, 3D Gaussian
  Splatting, or any world-model workload** — future phases, must not
  shape this spec's abstractions, per AGENTS.md.
- **Editing [specs/README.md](README.md),
  [docs/project-blueprint.md](../docs/project-blueprint.md), or any other
  governance/roadmap document.** This spec states, in its own Motivation,
  that Minimal Renderer's future spec depends on this one's outcome; it
  does not itself reorder, renumber, or edit the backlog or roadmap
  documents that record that candidate.
- **A `RenderTarget` that supports being read from within the same
  graph.** This round's `RenderTarget` is write-only, per
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md) —
  no post-process-from-swapchain or similar read-back pattern is designed
  or implemented here.
- **Depth attachments or multi-attachment `RenderTarget`s.** This round's
  `RenderTarget` represents exactly one color attachment.

## Requirements

### Functional

**`RenderTarget`**

- A concrete RHI public type representing one presentable color
  attachment (the acquired swapchain image and its view), per
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md).
- Non-owning: `Presentation` continues to own every swapchain-backed
  resource behind it, per
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md).
- Frame-scoped: valid from the `acquireNextTarget()` call that vended it
  until the matching `present()` call in the same frame consumes it.
  Using it outside that window is a lifetime precondition violation, not
  a guaranteed-detectable error — consistent with this codebase's
  existing handle-misuse tiering (Spec 0005 Error Model).
- Exposes read-only extent/format queries; no resize/mutation API of its
  own.

**`Presentation` acquire/present**

- `acquireNextTarget()` returns a tri-state outcome: `Err(AcquireError)`
  for an unrecoverable failure; `Ok(std::nullopt)` when the tracked
  framebuffer extent is `{0, 0}` (nothing to draw this frame — not an
  error); `Ok(RenderTarget{...})` otherwise. See
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  for the exact contract, including that it internally performs
  `recreateIfNeeded()`'s existing recreation logic
  ([ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md))
  as its first step, every call.
- `VK_ERROR_OUT_OF_DATE_KHR` from the underlying acquire call results in
  `Ok(std::nullopt)` for that call and recreation marked needed for the
  next call — no immediate in-call retry. `VK_SUBOPTIMAL_KHR` returns the
  acquired target normally but marks recreation needed for the next call.
- `present(RenderTarget)` consumes the target by value, waits on the
  submission's signaled "render finished" semaphore before calling
  `vkQueuePresentKHR`, and treats `VK_ERROR_OUT_OF_DATE_KHR`/
  `VK_SUBOPTIMAL_KHR` from present itself as routine (marks recreation
  needed, not a `Result::Err`); any other Vulkan error from present is a
  genuine `Result::Err`.
- `recreateIfNeeded()` (Spec 0003) is unchanged and remains independently
  callable; its own acceptance criteria (zero-extent skip, resize-driven
  recreation) continue to hold.
- Every `RenderTarget`'s image is treated as entering its first
  transition of the frame from `ResourceState::Undefined`, regardless of
  true prior layout — valid because `RenderTarget` is write-only this
  round. See
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md).

**Minimal RHI GPU resource, command recording, and submission**

- `ResourceState` enum: `Undefined`, `ColorAttachmentWrite`,
  `PresentSource` — sufficient for this round's one resource kind, per
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md).
- `CommandList` (RHI interface): `transitionResource(RenderTarget&,
  ResourceState before, ResourceState after)` and `clearColor(RenderTarget&,
  ClearColorValue)` — the only two recordable operations this spec
  introduces. Not thread-safe, not copyable, caller-owned, must not
  outlive its `Device`.
- `Device::createCommandList()` vends a `CommandList`.
- `Device::submit(CommandList, SubmitInfo)` submits a recorded command
  list to the graphics/present-capable queue, with an explicit wait
  semaphore + wait stage, signal semaphore, and fence, per
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md).
  Every `VkResult` is checked; failures surface through
  `atlantis::Result`, never discarded.
- Single frame-in-flight baseline: the caller waits on the previous
  frame's fence before recording/submitting the next.
- No `Vk*` type, and no Vulkan header, appears in any RHI public header —
  same structural rule as Spec 0003, verified the same way.
- No direct `vkCmd*` call exists anywhere outside the Vulkan Backend's
  `CommandList` implementation.

**RenderGraph execution integration**

- `RenderGraphBuilder::reads()`/`writes()` (Spec 0005) are extended to
  additionally accept a `ResourceState`, used only for transition
  bookkeeping — Spec 0005's single-producer model, dependency derivation,
  cycle detection, and deterministic ordering
  ([ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md))
  are unchanged in every other respect.
- A pass declaration gains an execution callback
  (`std::function<void(CommandList&)>` or equivalent), recorded at
  declaration time.
- A new `execute(CompiledGraph, bindings, CommandList&)` entry point
  walks the compiled pass order and, for each pass: inserts a
  `transitionResource()` call whenever a resource usage's declared state
  differs from that resource's most-recently-recorded state, then invokes
  the pass's execution callback. See
  [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  for the full responsibility split.
- A frame-scoped binding mechanism associates each producer-less logical
  resource used by the graph with a concrete RHI `RenderTarget`, valid
  only for that one `execute()` call.
- `execute()` inserts one trailing `transitionResource()` call to
  `ResourceState::PresentSource` for any bound `RenderTarget`, after the
  last pass in compiled order that uses it.
- `execute()` only records into the caller-provided `CommandList`; it
  never calls `Device::submit()` or `Presentation::present()` — those
  remain explicit, separate calls made by the caller after `execute()`
  returns.
- RenderGraph's public headers still contain no `Vk*` type, no Vulkan
  header, and no Atlantis Platform type — this spec's new RHI dependency
  does not relax that rule.

**Windows resize / zero-extent / out-of-date handling across the whole
frame cycle**

- Observing a `WindowResize` event and calling `notifyResized()` (Spec
  0003, unchanged) followed by the next frame's `acquireNextTarget()`
  results in the swapchain being recreated at the new extent before
  acquisition, transparently to the caller.
- A zero-extent window (minimized) results in every frame's
  `acquireNextTarget()` returning `Ok(std::nullopt)`; the caller skips
  RenderGraph execution, submission, and present entirely for that frame
  — no Vulkan call is made on this path, structurally, mirroring
  `recreateIfNeeded()`'s existing zero-extent guarantee.
- Restoring from minimized (extent becomes non-zero again) results in the
  very next `acquireNextTarget()` recreating and then successfully
  acquiring, with no special-cased "recovery" call needed.
- An out-of-date or suboptimal swapchain, encountered at either acquire
  or present time, never crashes, hangs, or produces a Validation Layer
  warning/error — it is absorbed into the recreation-needed bookkeeping
  described above.

**Phase 1 single-threaded orchestration and thread-safety contracts**

- Every new public type this spec introduces (`RenderTarget`,
  `CommandList`, the `execute()` entry point, and the extended
  `Presentation`/`RenderGraphBuilder` methods) documents its thread-safety
  contract at its public API, per AGENTS.md's Threading rules — the
  contract is, in every case, "not thread-safe; caller-thread-only," on
  the single Phase 1 logical frame thread that also owns the Windows
  Platform message pump, per
  [ADR-0004](../adr/0004-phase1-threading-baseline.md).
- No mutex, atomic, job/task system, or lock-free structure is introduced
  anywhere in this spec's scope.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" — the same bar Spec 0003 set. No frame-pacing or
  micro-benchmark target is introduced; single-frame-in-flight is an
  explicit simplification, not a performance claim.
- **Memory:** no GPU memory suballocation strategy is introduced or
  assumed — see [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md).
  Host-side allocation (e.g. the resource-state bookkeeping map inside
  `execute()`) uses ordinary RAII and standard containers.
- **Portability (within the Vulkan-only Phase 1 constraint):**
  implemented and verified on Windows only. RHI's and RenderGraph's public
  interface shapes must not preclude Android's future implementation —
  verified by inspection (no Windows type in any RHI/RenderGraph public
  header), not by building an Android target.
- **Other:** no new third-party dependency. Unit tests use the existing
  Catch2 v3 framework ([ADR-0007](../adr/0007-test-framework.md)).

## Proposed Design

### Module boundaries (realizing, not moving, an existing one)

This spec does not move or reinterpret any existing module boundary. It
realizes the RenderGraph → RHI dependency
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
already lists but Spec 0005 deliberately left unrealized for its own
GPU-independent round:

```
Windows Platform (existing) -- WindowResize event --> Runtime-equivalent
  code (this spec's own minimal, non-shipping verification composition,
  same status as Spec 0003/0005's own demos -- NOT the future Runtime
  module)
    -- notifyResized() --> Presentation (RHI interface, Vulkan Backend
       implementation)

Runtime-equivalent code, once per frame:
  Presentation::acquireNextTarget()
    -> Ok(std::nullopt): skip this frame entirely
    -> Err: propagate/log, per this spec's error handling
    -> Ok(RenderTarget): continue below

  Device::createCommandList() -> CommandList

  RenderGraphBuilder (Spec 0005, extended by this spec)
    .declarePass(), .declareResource(), .reads()/.writes() with
    ResourceState, .setExecute(pass, callback)
    .compile() -> CompiledGraph (Spec 0005, unchanged)

  render_graph::execute(CompiledGraph, bindings{resource -> RenderTarget},
                         CommandList&)
    -- walks compiled pass order, calls CommandList::transitionResource()
       between differing declared states, invokes each pass's execution
       callback (which may call CommandList::clearColor(), etc.),
       inserts the final PresentSource transition --

  Device::submit(CommandList, SubmitInfo{acquire-complete semaphore,
                  render-finished semaphore, fence})

  Presentation::present(RenderTarget)  -- waits on render-finished
    semaphore, calls vkQueuePresentKHR, absorbs out-of-date/suboptimal
```

RHI, RenderGraph, and Vulkan Backend keep exactly the dependency
directions [module_boundaries.md](../docs/architecture/module_boundaries.md)
already states: RenderGraph → RHI, Core; Vulkan Backend → RHI, Core,
Vulkan SDK; RenderGraph never depends on Vulkan Backend, Atlantis
Platform, or Runtime, directly or indirectly. Renderer still does not
exist and is not depended on by anything this spec touches.

### RenderTarget, acquire/present, and image layout handoff

See
[ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
for the full decision and rationale: `RenderTarget`'s non-owning,
frame-scoped, write-only borrow contract; `acquireNextTarget()`'s
tri-state `Err`/`Ok(std::nullopt)`/`Ok(RenderTarget)` outcome and its
folding-in of `recreateIfNeeded()`; `present()`'s out-of-date/suboptimal
absorption; and the deliberate always-`Undefined`-incoming-layout
simplification.

### Minimal RHI resource, command recording, and submission

See
[ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
for the full decision and rationale: the `ResourceState` enum,
`CommandList`'s two operations (`transitionResource`, `clearColor`),
`Device::createCommandList()`/`submit()`, and the single-frame-in-flight
baseline.

### RenderGraph execution and the dependency-to-barrier boundary

See
[ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
for the full decision and rationale: the new `ResourceState`-tagged usage
declarations, the per-pass execution callback, the frame-scoped external
binding mechanism, the `execute()` entry point's transition-insertion
algorithm (including the trailing `PresentSource` transition), and why
RenderGraph records but never submits or presents.

### Threading

Single logical frame thread, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md): `acquireNextTarget()`/
`present()`, `Device::createCommandList()`/`submit()`, RenderGraph pass
declaration/`compile()`/`execute()`, and every `CommandList` recording
call all happen on the same thread that owns the Windows Platform message
pump. No type this spec introduces claims any stronger thread-safety
guarantee.

### Error handling

- Recoverable runtime errors (acquire failure, submission failure, a
  genuine present failure) use `atlantis::Result<T, E>`, consistent with
  every prior spec's convention — no exception is introduced anywhere in
  RHI, Vulkan Backend, or RenderGraph's public or private surface.
- Programmer errors (using a `RenderTarget` or `CommandList` outside its
  valid frame/lifetime window in a way this spec claims to detect;
  calling `execute()` with a binding missing for a resource the graph
  requires) use `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`, per existing
  convention.
- Every `VkResult` along acquire, command recording, submission, and
  present is checked; no `VkResult` is discarded, including ones
  "expected" to always succeed.
- Vulkan Validation Layers are enabled unconditionally in Debug builds and
  any GPU-touching CI job; a validation warning or error is a build/test
  failure.

## Architectural Impact

This spec introduces architecture and requires three new ADRs before it
can move from `Draft`/`In Review` to `Approved`, per
[AGENTS.md](../AGENTS.md). None of the following is decided by this
spec's prose alone — each is filed as its own ADR:

1. **Presentation acquire/present protocol and `RenderTarget` frame-borrow
   contract** — resolves in full the bundle
   [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
   deferred: acquire's shape, `RenderTarget`'s concrete type and
   frame-ownership contract, acquire-complete synchronization,
   graph-to-present synchronization, image layout handoff, and present's
   own shape and out-of-date/suboptimal handling. Filed as
   [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
   (`Proposed`).
2. **Minimal RHI GPU resource, command recording, and submission
   interface** — `ResourceState`, `CommandList`, `Device::createCommandList()`/
   `submit()`, and the single-frame-in-flight baseline. Filed as
   [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
   (`Proposed`).
3. **RenderGraph/RHI execution integration and dependency-to-barrier
   responsibility** — RenderGraph's new RHI dependency, the
   `ResourceState`-tagged usage model, the per-pass execution callback,
   the frame-scoped binding mechanism, and the exact split between
   RenderGraph (deciding when/between what states) and RHI/Vulkan Backend
   (deciding how) for every resource-state transition. Filed as
   [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
   (`Proposed`).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified by this spec or by the three new ADRs above — each new ADR
references and builds on the existing ones (particularly ADR-0001,
ADR-0002, ADR-0003, ADR-0004, ADR-0016, ADR-0017, and ADR-0018) without
altering them. Architectural Impact for this spec is not "None" —
`RenderTarget`, `CommandList`, and RenderGraph's execution capability are
each a new public API surface, exactly the kind of change AGENTS.md's
"What counts as significant" section requires the full Spec → Plan →
Human Review path for.

## Alternatives Considered

- **Scope this spec to RHI's frame-execution surface only, leave
  RenderGraph execution to a separate, later spec.** Rejected: RHI's
  `CommandList`/transition surface has no real consumer or validation
  target without RenderGraph's execution phase to record into it, and
  splitting them risks the same "invent RHI's surface without a
  RenderGraph to validate it against" mistake Spec 0003 avoided by
  stopping short — better to review both halves of one coherent frame
  cycle together, as this spec does.
- **Extend this spec further to also add a minimal graphics pipeline/
  shader-binding surface, so the demonstrated pass is a real draw call
  rather than a clear.** Rejected — see
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  Alternatives Considered: pulls Shader System's and Minimal Renderer's
  own future scope into this spec, contradicting the same
  sequencing discipline Spec 0005's Alternatives Considered already
  established for RenderGraph itself.
- **Have Runtime-equivalent code manually insert barriers between passes,
  rather than deriving them from the compiled graph's dependency data.**
  Rejected — see
  [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  Alternatives Considered: reintroduces caller-authored, hand-scheduled
  GPU work, which AGENTS.md's mandatory-RenderGraph-path rule exists to
  forbid.
- **Silently amend `docs/project-blueprint.md`'s Milestone 3 (Minimal
  Renderer) entry or `specs/README.md`'s backlog to insert this spec as an
  explicit prerequisite.** Rejected: per AGENTS.md, governance/roadmap
  documents change only through their own review, not as a side effect of
  drafting an unrelated spec; this spec states the dependency in its own
  Motivation section instead, leaving the roadmap documents themselves for
  a human (or a future, explicitly-scoped docs change) to update.
- **Support multiple frames in flight from the start, to avoid a known
  future rework.** Rejected for this round — see
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  Alternatives Considered: no current performance requirement justifies
  the added synchronization-object complexity; the single-frame baseline
  is simpler to verify correct first.

## Testing & Verification Plan

- **Unit tests:** GPU-independent bookkeeping and validation logic —
  the tri-state acquire-outcome mapping logic where exercisable without a
  real device; RenderGraph's extended `ResourceState`-aware usage
  declaration and the `execute()` transition-insertion algorithm's
  decision logic (which resource-state pairs trigger a transition,
  including the trailing `PresentSource` transition), exercised against a
  fake/mock `CommandList` that records which calls it received rather
  than a real Vulkan one — per
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  layer 1, no Vulkan device required. At minimum, tests must cover:
  - `execute()` inserts no transition when consecutive usages of the same
    resource declare the same `ResourceState`.
  - `execute()` inserts exactly one transition when a resource's declared
    state changes between two usages adjacent in compiled order.
  - `execute()` inserts the trailing `PresentSource` transition exactly
    once, after the last pass that uses a bound `RenderTarget`, for a
    graph with one, and with more than one, pass touching that resource.
  - A graph whose bound `RenderTarget` is never used by any pass performs
    no transition on it at all (no spurious trailing transition for an
    unused binding).
  - Every pass's execution callback is invoked, on a fake `CommandList`,
    in exactly the compiled pass order Spec 0005 already guarantees
    deterministic.
  - Calling `execute()` without a binding for a resource the graph
    requires triggers the programmer-error/assertion policy.
- **Headless integration tests:** not applicable in
  [testing-strategy.md](../docs/process/testing-strategy.md)'s current
  sense (layer 2 is headless *rendering*, which remains future work); a
  real-device, no-window `Device`/`CommandList` submission test is
  possible but not required by this spec's Acceptance Criteria, flagged
  under Risks & Open Questions consistent with Spec 0003's own equivalent
  flag.
- **Image regression tests:** not applicable — this spec's manual
  verification checks for a visible, expected-color frame by direct
  observation, not automated pixel comparison; that remains gated on
  headless rendering per [AGENTS.md](../AGENTS.md) sequencing.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of acquire, command recording,
  transition, submission, and present — per
  [AGENTS.md](../AGENTS.md) and
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- **Manual verification:** a minimal, non-shipping composition (see
  Non-Goals) creates a Windows Platform window, constructs a `Device` and
  `Presentation` (reusing Spec 0003's existing construction path),
  declares a one-pass RenderGraph that clears the acquired `RenderTarget`
  to a known, visually-distinct color, and — driven by the existing
  non-blocking Platform event loop — runs the full acquire → execute →
  submit → present cycle every frame. It confirms:
  - A visible window shows the expected clear color and continues to do
    so across repeated frames.
  - Interactive resize continues to show the correct color at the new
    window size, with no visible corruption, tearing artifact attributable
    to a missing transition, or validation warning during or immediately
    after the resize.
  - Minimizing the window results in no crash, no busy-spin, and no
    Vulkan call being made while minimized (verifiable by inspection of
    the zero-extent skip path, mirroring Spec 0003's equivalent
    guarantee); restoring resumes correct rendering with no special
    recovery step visible to the user.
  - The application exits cleanly at any point in this sequence — at
    startup, mid-resize, minimized, or after any number of frames — with
    no outstanding acquired `RenderTarget`, no leaked `CommandList`, and
    no Validation Layer warning or error at any point, including at
    shutdown.

## Acceptance Criteria

- [ ] RHI's and RenderGraph's public headers contain no `Vk*` type and no
      `#include <vulkan/...>` — verifiable by inspection/grep.
- [ ] No direct `vkCmd*` call, and no `VkImageMemoryBarrier`/
      `vkCmdPipelineBarrier` construction, exists anywhere outside the
      Vulkan Backend's `CommandList` implementation.
- [ ] `RenderTarget` is non-owning, frame-scoped, and write-only in every
      code path this spec implements — no read-back-from-`RenderTarget`
      capability exists anywhere.
- [ ] `acquireNextTarget()` returns `Ok(std::nullopt)` (not an error) at
      zero framebuffer extent, both for a freshly-minimized window and an
      initially-zero-extent window at startup — verifiable by code
      inspection of that path, mirroring Spec 0003's equivalent
      structural guarantee for `recreateIfNeeded()`.
- [ ] A Windows resize results in the next frame's `acquireNextTarget()`
      transparently recreating the swapchain and successfully acquiring
      at the new extent, with the resulting frame rendered and presented
      correctly.
- [ ] `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` at either acquire or
      present time never crashes, hangs, or produces a validation
      warning/error — absorbed into recreation-needed bookkeeping as
      specified.
- [ ] `execute()` never calls `Device::submit()` or
      `Presentation::present()` — verifiable by inspection that
      RenderGraph's implementation has no reference to either.
- [ ] No GPU command is recorded into any `CommandList` anywhere in this
      spec's implementation outside a RenderGraph pass execution
      callback.
- [ ] Every `VkResult` along acquire, command recording, submission, and
      present is checked; no `VkResult` is discarded.
- [ ] Debug builds and any GPU-touching CI job run with Vulkan Validation
      Layers enabled; a validation warning or error fails the run.
- [ ] The manual verification demo shows a visible, correctly-colored
      frame; continues to do so across interactive resize; makes zero
      Vulkan calls while minimized; and resumes correctly on restore.
- [ ] No pipeline object, shader, vertex/index buffer, general draw call,
      `Buffer`/`Texture`/`Sampler` type, or GPU memory allocator is
      created anywhere by this spec's implementation.
- [ ] No caller-authored pass-to-pass dependency edge, and no pass
      culling, is implemented anywhere this spec touches — Spec 0005's
      existing rules on both are unchanged.
- [ ] No `src/renderer/` or Shader System source is created by this
      spec's implementation.
- [ ] No Android NDK build configuration, no second graphics backend, and
      no thread/job system is introduced anywhere this spec touches.
- [ ] No multiple-frames-in-flight machinery (command list pooling beyond
      ordinary RAII, per-frame semaphore/fence arrays) is implemented —
      the single-frame-in-flight baseline is a structural property of the
      implementation, not merely a documented intention.
- [ ] All three ADRs listed in Architectural Impact
      ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md),
      [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
      [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
      reach `Accepted` before this spec is marked `Approved`.

## Risks & Open Questions

- Whether `Device::createCommandList()`/`submit()` needs its own new
  test-harness category distinct from Spec 0003's already-flagged
  open question about `Device`/`Presentation` construction testing — both
  need a real Vulkan device but neither fits
  [testing-strategy.md](../docs/process/testing-strategy.md)'s layer 1
  (must not require a device) or layer 2 (headless *rendering*, not yet
  implemented) as currently named. Flagged, not resolved — the Plan stage
  or a future testing-strategy amendment should address both together.
- The exact `ClearColorValue` representation (a plain RGBA struct vs. a
  variant covering future non-float formats) is left to the Plan — this
  spec fixes that `clearColor()` exists and what it conceptually does,
  not its parameter's concrete C++ shape.
- Whether `CommandList` should be reset-and-reused across frames (a pool)
  rather than destroyed/recreated each frame is left to the Plan as a
  performance-neutral implementation choice, provided it does not
  introduce multi-frame-in-flight semantics this spec's Acceptance
  Criteria forbid.
- The concrete representation of `execute()`'s frame-scoped resource
  binding (a small vector of pairs vs. a dedicated map type) is left to
  the Plan.
- Whether a future Minimal Renderer spec will need to widen
  `transitionResource()`'s parameter from `RenderTarget&` to a general
  resource reference, and whether `ResourceState` will need meaningfully
  more variants (depth-attachment states, shader-read states) once real
  `Buffer`/`Texture` types exist, is explicitly left open — this spec
  does not attempt to predict or pre-widen either, per AGENTS.md's "no
  speculative abstraction" principle.
- Whether the single-frame-in-flight baseline this spec adopts will need
  revisiting once a real frame-time/performance signal exists is left
  open, per [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  own Negative/Trade-offs.

## Out of Scope / Future Work

Atlantis Renderer (Milestone 3 candidate, "Minimal Renderer" in
[specs/README.md](README.md)'s backlog), Shader System (Milestone 4),
Android Platform and Vulkan presentation (Milestone 5), headless
rendering (Milestone 6), and image regression testing (Milestone 7) all
remain later, separately-specced work per
[docs/project-blueprint.md](../docs/project-blueprint.md) and are not
advanced, designed, or unblocked by this spec beyond satisfying this
frame-execution foundation as their shared, previously-undeclared
dependency — see Motivation. A future Minimal Renderer spec is expected
to be the first consumer that needs `CommandList` to grow beyond
`transitionResource()`/`clearColor()` into a real pipeline-bound
draw-call surface, and the first to need general `Buffer`/`Texture`
resources and a wider `ResourceState` set; none of that shape is
predicted or pre-designed here. A future performance-motivated spec may
revisit the single-frame-in-flight baseline this spec adopts. A future
resource-lifetime/versioning spec (already anticipated by Spec 0005's own
Out of Scope / Future Work) remains entirely separate from this spec's
`ResourceState` transition-bookkeeping tag, which is not, and does not
attempt to be, a lifetime or aliasing mechanism.
