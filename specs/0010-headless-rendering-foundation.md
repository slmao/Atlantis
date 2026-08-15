# Spec: Headless Rendering Foundation

- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; human authorship/ownership of this spec is pending
  confirmation at Human Review.
- **Created:** 2026-08-15
- **Related Plan(s):** None yet — a plan may be drafted once this spec is
  `Approved`, per [AGENTS.md](../AGENTS.md); this spec's own PR must merge
  into `main` first (same sequencing every prior spec in this line has
  followed).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0009](../adr/0009-assertion.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  (all `Accepted`). See **Architectural Impact** below — three new
  decisions are identified and drafted alongside this spec:
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  (offscreen `RenderTarget` construction and ownership),
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  (RenderGraph execution — caller-specified incoming/final resource
  states), and
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)
  (GPU-to-CPU readback capability) — all currently `Proposed`, pending
  Human Review alongside this spec.

## Summary

This spec introduces headless rendering: the ability to drive the exact
same `Renderer` → RenderGraph → RHI → Vulkan Backend stack Spec 0007
already shipped, without any window, `Presentation`, or `VkSwapchainKHR`
involved, and to read the rendered pixels back to the CPU. It extends RHI
with an offscreen `RenderTarget`-vending type (`OffscreenTarget`, no
`Presentation`), a GPU-to-CPU readback capability (a new `ResourceState`,
a new `Buffer` purpose, and a new `CommandList` copy operation), and
generalizes RenderGraph's `execute()` to support both a non-presentable
bound `RenderTarget` and a second, chained `execute()` call sharing one
`CommandList` with `Renderer::drawFrame()`'s own internal one. It does
**not** design golden-image comparison, tolerance methodology, or CI
gating — this spec is the rendering-and-readback foundation a future
Image Regression Testing spec depends on, not that spec itself.

## Motivation / Problem Statement

[AGENTS.md](../AGENTS.md)'s Phase 1 constraints state plainly: "Headless
rendering and image regression testing follow once the windowed/swapchain
path works — they are still Phase 1 scope, not deferred to a future
phase, but they are not the first milestone... don't skip headless once
windowed is working." Windowed rendering is now genuinely working: Spec
0007 (Minimal Renderer) is `Approved` and implemented — a real,
depth-tested, camera-driven, material-shaded mesh draws correctly through
the full stack on Windows/Vulkan, verified with Vulkan Validation Layers
clean. [specs/README.md](README.md)'s Candidate Spec Backlog lists
Headless Rendering (Candidate 2) as depending only on Spec 0007, already
`Approved`/implemented — the stated dependency is satisfied.

Three genuine architectural gaps stand between "a mesh draws in a window"
and "the same mesh draws and its pixels can be read back with no window,"
none of which any existing `Accepted` ADR resolves:

- **RHI has no way to produce a `RenderTarget` without `Presentation`.**
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)
  already named this as the intended future shape ("an explicitly-
  requested offscreen target → `RenderTarget` backed by an offscreen
  image, no `Presentation` involved") but fixed no concrete construction
  API, ownership type, or lifecycle for it.
- **RHI has no GPU-to-CPU data path of any kind.** No `ResourceState`
  supports being copied out of, no `Buffer` purpose supports being a
  copy destination, and `CommandList` has no copy operation.
- **RenderGraph's `execute()` hardcodes two assumptions that were true
  only because, until now, exactly one origin and exactly one call per
  frame ever existed:** every bound resource starts each `execute()` call
  from `ResourceState::Undefined`, and every bound `RenderTarget` ends the
  frame at `ResourceState::PresentSource`. Both break for a headless
  target read back via a second, chained `execute()` call — see
  **Architectural Impact** and
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  for the full analysis of why this is a genuine correctness gap, not a
  hypothetical one, and how this spec resolves it without reopening
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)'s
  unification promise or
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  already-shipped `Renderer` public API.

A fourth question, procedural rather than architectural but requiring an
explicit answer rather than a silent default: whether this spec's own
implementation pressure forces an early, general GPU-memory-allocator
decision. It does not — see Requirements' "GPU memory allocation" bullet
and
[ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md),
which extends
[ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
already-`Accepted`, narrow, direct-allocation policy rather than reopening
[ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s general
deferral.

## Goals

- Introduce `OffscreenTarget` as a concrete RHI public type vending the
  exact same `RenderTarget` value
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  already defines, with no `Presentation` object involved
  ([ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)).
- Extend RHI with a minimal, narrow GPU-to-CPU readback capability: one
  new `ResourceState` (`TransferSource`), one new `Buffer` purpose
  (readback), and one new `CommandList` copy operation
  ([ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)).
- Generalize RenderGraph's `execute()` binding to accept a caller-
  specified incoming and final `ResourceState` per bound resource, fixing
  the hardcoded-`Undefined`/hardcoded-`PresentSource` assumptions that
  only ever held because a single origin and a single per-frame
  `execute()` call previously existed
  ([ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)).
- Demonstrate, end-to-end, that the **unmodified** `Renderer::drawFrame()`
  draws the same mesh/material/camera Spec 0007 already verifies, into an
  offscreen `RenderTarget`, with no window, `Presentation`, or swapchain
  anywhere in the composition — proving
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)'s
  unification claim for the first time with a real second origin, not
  just windowed alone.
- Verify a full render → readback cycle produces a reproducible, basic
  content check (not a golden-image comparison — see Non-Goals) on
  Windows with a real GPU, with Vulkan Validation Layers clean throughout.
- Resolve the three architectural decisions this spec identifies via
  dedicated ADRs, so a future Image Regression Testing spec inherits a
  settled headless-rendering-and-readback contract instead of having to
  invent one under its own implementation pressure.
- Confirm, explicitly, that no general GPU memory allocator decision is
  forced by this spec's scope — extending
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  existing direct-allocation policy is sufficient
  ([ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)).

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Golden-image storage, comparison, tolerance/diff methodology, or CI
  image-regression gating of any kind.** This spec proves rendering and
  readback work and produce a reproducible basic content result; it does
  not design or implement the comparison harness a future Image
  Regression Testing spec (Candidate 3 in
  [specs/README.md](README.md)'s backlog) will build on top of this
  spec's own output. No golden-image file format, storage location, or
  diff algorithm is chosen here.
- **Android Platform, iOS Platform, or a second graphics backend of any
  kind.** Windows/Vulkan only, per [AGENTS.md](../AGENTS.md). This spec's
  headless path uses no Atlantis Platform code at all (no window is
  created), so it has no Android/iOS-specific content to design in either
  direction.
- **Linux as a target platform, or any Linux-specific build/CI/runtime
  content.** Per [AGENTS.md](../AGENTS.md), Linux is not a target platform
  for Atlantis; nothing in this spec's headless rendering path implies or
  requires one — "headless" here means "no window/swapchain," not "a
  different operating system."
- **A general, sampled/shader-readable `Texture` usage, or a general
  `Sampler` type.** [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)'s
  readback capability is scoped strictly to a copy-out-to-`Buffer`
  operation, not to making the offscreen color image readable by a
  shader. Spec 0007's "no general `Sampler`/sampled `Texture`" Non-Goal is
  unchanged.
- **Depth-buffer readback.** Only the color `RenderTarget` gains a
  readback path this round; the depth `Texture`'s existing, unchanged
  ownership/lifecycle ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md))
  is reused verbatim for depth-testing correctness, but nothing reads its
  contents back to the CPU.
- **Asynchronous, non-blocking, or multi-frame-latency-amortized
  readback.** Readback this round is synchronous and blocking
  (`Device::waitIdle()`-based), per
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md). No
  future/promise-style API, no double-buffered staging.
- **Multiple frames in flight, multi-threaded command recording/resource
  creation/graph execution, or any job/task system.** Phase 1's
  single-logical-thread, single-frame-in-flight baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
  is unchanged and unreopened.
- **A general GPU memory suballocator (VMA or hand-rolled), or any change
  to [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s
  general deferral.**
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  narrowly extends [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  already-`Accepted` direct-allocation policy to one more resource kind —
  it does not adopt or scaffold for a general allocator.
- **Any change to `Renderer`'s public API**
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)).
  `Renderer::drawFrame()` is called by this spec's verification
  composition exactly as Spec 0007's windowed composition already calls
  it — same signature, same borrowed-reference contract, no new
  parameter, no headless-awareness added to `Renderer` itself. See
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  Alternatives Considered for why a shared-state-threading alternative
  that *would* have touched `Renderer`'s API was rejected in this spec's
  favor.
- **A second rendering path, a second `RenderGraph`/RHI implementation,
  or any fork of `Renderer`.** Headless reuses `Renderer`, RenderGraph,
  RHI, and the Vulkan Backend completely unchanged in their core
  responsibilities — see Requirements and Proposed Design.
- **Multiple simultaneous `OffscreenTarget` instances, or any pooling/
  reuse-across-instances policy for them.** This spec's own verification
  composition constructs one `OffscreenTarget` and exercises its
  acquire/draw/readback cycle; whether/how a future Image Regression
  Testing harness pools or reuses instances across many test cases is
  left entirely to that future spec.
- **Any regression to the windowed path.** `examples/frame_execution_demo`
  and `examples/minimal_renderer_demo` must continue to build and behave
  identically after this spec's mechanical binding-call-site update (see
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)) —
  no windowed behavior, output, or Vulkan call sequence changes.
- **Editing [specs/README.md](README.md)'s Section A entries for prior
  specs, [docs/project-blueprint.md](../docs/project-blueprint.md), or any
  other governance/roadmap document beyond this spec's own required
  backlog-registry update** (see Out of Scope / Future Work and this
  spec's own PR description for the precise, minimal registry edit this
  round makes).

## Requirements

### Functional

**Offscreen `RenderTarget` construction and ownership**

- A new RHI type, `OffscreenTarget`, constructed via `Device` with a
  fixed extent and color format (caller-specified, never changed for that
  instance's lifetime — no resize/recreation concept), owning exactly one
  color image (and its backing memory) for its whole lifetime. Full
  contract in
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).
- `OffscreenTarget` vends a `RenderTarget` via a two-outcome (`Err`/`Ok`)
  acquire-equivalent call — no zero-extent `Ok(std::nullopt)` case, unlike
  `Presentation`, because an offscreen target's extent cannot become
  `{0, 0}` after construction.
- The vended `RenderTarget` is the exact same public type
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  already defines — no new field, method, or capability-query is added to
  it by this spec.
- `OffscreenTarget` has no `present()` counterpart; the vended borrow is
  consumed by the readback operation below (see "GPU-to-CPU readback
  capability"). One acquire, one consuming call, same discipline
  `Presentation` already enforces.
- The same `OffscreenTarget` instance may be acquired-and-consumed more
  than once across its lifetime, each cycle independent, following the
  existing single-frame-in-flight baseline
  ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)).
- Destruction preconditions mirror `Presentation`'s exactly (no outstanding
  acquired-but-unconsumed `RenderTarget`, no outstanding unwaited
  submission) — same lifetime-precondition tier, same
  `Device::waitIdle()`-satisfies-it mechanism.
- The depth `Texture` used alongside an `OffscreenTarget` is constructed,
  owned, and destroyed via the existing, unchanged
  `Device::createTexture()` path
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)) —
  `OffscreenTarget` itself never owns, creates, or references a depth
  resource.
- **GPU memory allocation:** every allocation `OffscreenTarget`'s color
  image requires uses
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  existing direct, unpooled, Vulkan-Backend-private policy (its own
  individual `vkAllocateMemory`/`vkFreeMemory` pair) — no VMA dependency,
  no hand-rolled suballocator, no RHI/Renderer signature shaped around any
  particular allocation strategy is introduced by this spec.

**GPU-to-CPU readback capability**

- `ResourceState` gains exactly one new variant: `TransferSource`. Full
  contract in
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md).
- `Buffer` gains a fourth, fixed purpose: readback — host-visible,
  host-coherent, mapped once for its whole lifetime, created via the
  existing, unchanged `Device::createBuffer()`. Sized by the caller to
  match the source color image's known extent/format; no automatic
  format negotiation or size query is introduced.
- `CommandList` gains exactly one new recordable operation:
  `copyTextureToBuffer(RenderTarget&, Buffer&)` (exact signature left to
  the Plan) — copies the full, tightly-packed color image into a
  readback-purpose `Buffer`; no partial-region copy, no format
  conversion. Recording remains legal only from inside a RenderGraph pass
  execution callback, unchanged from
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  existing rule.
- The copy is recorded as a RenderGraph pass declaring **exactly one
  `writes()` usage tagged `ResourceState::TransferSource`** against the
  logical resource the color `RenderTarget` is bound to — never a paired
  `reads()` + `writes()`, following the identical precedent
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  already established for the depth `Texture`. `execute()`'s existing
  draw-pass recognition ([ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md))
  does not fire for this pass — no attachment-scoping bracketing is
  inserted around it.
- Readback is synchronous and blocking: the caller submits, calls the
  existing `Device::waitIdle()`, then reads the readback `Buffer`'s
  already-mapped pointer directly. No new synchronization primitive,
  signal type, or fence-adjacent concept is introduced.
- Every `VkResult` along copy recording, submission, and wait is checked;
  no `VkResult` is discarded. Buffer/image size or format mismatch between
  what the caller computed and what `OffscreenTarget` actually holds is a
  caller precondition violation, not a guaranteed-detectable error.

**RenderGraph execution generalization for non-presentable and chained
bindings**

- `execute()`'s existing frame-scoped external resource binding
  ([ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
  gains two additional, caller-supplied pieces of information per bound
  `RenderTarget` entry: an assumed incoming `ResourceState` (defaulting to
  `Undefined` — zero required change for every existing single-`execute()`-
  call windowed usage) and a **required** (no default) final
  `ResourceState`, expressed as `std::optional<ResourceState>`
  (`std::nullopt` meaning "no trailing transition beyond the last pass's
  own declared state"). Full contract in
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).
- Every existing windowed call site
  (`examples/frame_execution_demo`, `examples/minimal_renderer_demo`)
  must be mechanically updated to pass `ResourceState::PresentSource`
  explicitly as the final state where it previously received this
  behavior implicitly — a required, non-behavioral update this spec's
  future Plan must include; zero observable change to either demo's
  output or recorded Vulkan calls.
- `execute()`'s algorithm (walk compiled pass order, track
  most-recently-recorded state per bound resource, insert
  `transitionResource()` on a state change, insert one trailing call if a
  final state is specified and differs from the resource's ending state)
  is otherwise unchanged from
  [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)/[ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md).
  Guard 1 and Guard 2 are unchanged; the bound depth `Texture`'s binding
  (never trailing-transitioned) is unaffected.
- `Renderer::drawFrame()`'s own internal `execute()` call is entirely
  unaffected by this generalization — it continues to build, compile, and
  execute its own private graph with its own private, per-call state
  bookkeeping, exactly as Spec 0007 shipped it, using its existing
  hardcoded `ColorAttachmentOutput`/`DepthAttachmentReadWrite` usages
  internally. Only the caller's own, separate, second `execute()` call
  (for the readback copy pass) uses this spec's new incoming/final-state
  binding fields, supplying `ResourceState::ColorAttachmentOutput` as the
  documented, fixed incoming state a `RenderTarget` is left in immediately
  after `Renderer::drawFrame()` returns.

**Reuse of Renderer, RenderGraph, RHI, and Vulkan Backend — no fork**

- The headless verification composition constructs `Mesh`/`Material`/the
  camera uniform `Buffer` using the same fixed fixture Spec 0007's
  windowed verification composition already uses (exact code-sharing
  mechanism — shared source file vs. duplicated fixture — left to the
  Plan), and calls `Renderer::drawFrame()` with the exact same signature
  and borrowed-reference contract as the windowed composition, passing
  the `OffscreenTarget`-vended `RenderTarget` and a depth `Texture` in
  place of the windowed ones.
- No new `Renderer`, RenderGraph, RHI interface, or Vulkan Backend
  responsibility is introduced beyond what this spec's Requirements
  explicitly name above — `Renderer` itself gains zero new code path.

**Phase 1 single-threaded orchestration and thread-safety contracts**

- Every new public type this spec introduces (`OffscreenTarget`, the
  extended `execute()` binding, `CommandList::copyTextureToBuffer()`) documents
  its thread-safety contract at its public API — "not thread-safe;
  caller-thread-only," on the single Phase 1 logical thread, per
  [ADR-0004](../adr/0004-phase1-threading-baseline.md). No mutex, atomic,
  job/task system, or lock-free structure is introduced anywhere in this
  spec's scope. Unlike the windowed path, this thread is not required to
  also own a Platform message pump — a headless composition has no
  window and no Platform event loop to run — but it remains exactly one
  logical thread, not a new threading model.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" outside the deliberate `waitIdle()`-based readback stall,
  which is an explicit, accepted simplification (see
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)), not a
  performance claim.
- **Memory:** no general GPU memory suballocation strategy is introduced
  or assumed — see
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).
- **Portability (within the Vulkan-only Phase 1 constraint):**
  implemented and verified on Windows only, using no window and no
  Atlantis Platform code at all. RHI's and RenderGraph's public interface
  shapes must not preclude Android's future implementation, verified by
  inspection.
- **Other:** no new third-party dependency. Unit tests use the existing
  Catch2 v3 framework ([ADR-0007](../adr/0007-test-framework.md)).

## Proposed Design

### Module boundaries (realizing, not moving, existing ones)

Realizes exactly the headless path
[ADR-0002](../adr/0002-presentation-rendertarget-unification.md) and
[resource_lifetime.md](../docs/architecture/resource_lifetime.md) already
anticipated: RHI gains a second `RenderTarget`-vending type
(`OffscreenTarget`, alongside `Presentation`) and a narrow readback
extension; RenderGraph's existing RHI dependency
([ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
is generalized, not widened to a new dependency direction. `Renderer`
gains no new dependency and no new code path — it is depended on by this
spec's verification composition exactly as Spec 0007's already is.

```
Headless verification composition, once per render-and-readback cycle:
  Device::createOffscreenTarget(extent, colorFormat) -> OffscreenTarget
  Device::createTexture(depth, extent) -> depth Texture (unchanged,
    Spec 0007's existing mechanism)
  Device::createBuffer(readback purpose, size) -> readback Buffer
    (unchanged Device::createBuffer(), new purpose value)

  OffscreenTarget::acquireTarget() (exact name left to Plan)
    -> Ok(RenderTarget): continue below
    -> Err: propagate/log

  Write this cycle's camera view/projection into the camera uniform
    Buffer's mapped memory (unchanged from Spec 0007)

  Device::createCommandList() -> CommandList

  Renderer::drawFrame(commandList, *renderTarget, *depthTexture,
                       cameraBuffer, drawItems)
    -- entirely unchanged from Spec 0007: builds, compiles, and executes
       its own internal graph (ColorAttachmentOutput + Depth
       AttachmentReadWrite draw pass) --

  Caller builds a second, small RenderGraphBuilder description: one pass
    declaring a single writes() usage tagged TransferSource against the
    same logical resource the RenderTarget is bound to; execution
    callback calls CommandList::copyTextureToBuffer(*renderTarget,
    *readbackBuffer)
  Caller compiles that second graph -> CompiledGraph
  render_graph::execute(compiledGraph,
                         bindings{resource -> (RenderTarget,
                                  incoming=ColorAttachmentOutput,
                                  final=std::nullopt)},
                         commandList)
    -- ADR-0039's generalized execute(): seeds this resource's tracked
       state from the caller-supplied ColorAttachmentOutput (not
       Undefined), inserts one transitionResource() to TransferSource
       before the copy pass's callback runs, records the copy, no
       trailing transition (final = std::nullopt) --

  Device::submit(commandList, target's-acquire-complete-signal)
  Device::waitIdle()
    -- readback Buffer's writes are now host-visible --

  Read readbackBuffer's mapped pointer; run this spec's basic,
    reproducible content check (see Testing & Verification Plan)

  -- On every exit path: Device::waitIdle() before destroying
     OffscreenTarget/Device/depth Texture/readback Buffer/Mesh/Material --
```

### Offscreen `RenderTarget` construction and ownership

See
[ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
for the full decision: `OffscreenTarget`'s shape, its two-outcome
acquire contract, its allocation policy (extending
[ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)),
and why depth-`Texture` ownership needs no change.

### GPU-to-CPU readback

See
[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md) for the
full decision: `TransferSource`, the readback `Buffer` purpose,
`copyTextureToBuffer()`, the synchronous `waitIdle()`-based
synchronization model, and error semantics.

### RenderGraph execution generalization

See
[ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
for the full decision: the caller-specified incoming/final `ResourceState`
binding fields, why this resolves both the `PresentSource`-for-headless
defect and the cross-`execute()`-call state-continuity gap, and why
`Renderer`'s public API needs no change to support it.

### Threading

Single logical thread, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md) — unchanged in kind
from every prior spec, minus the windowed path's additional obligation to
also own a Platform message pump (headless has none).

### Error handling

- Recoverable runtime errors (`OffscreenTarget`/`Buffer`/`Texture`
  creation failure, submission failure) use `atlantis::Result<T, E>`,
  consistent with every prior spec's convention.
- Programmer errors — Guard 1/Guard 2 violations (unchanged scope,
  generalized mechanism), acquiring a second `RenderTarget` from an
  `OffscreenTarget` before consuming the first — use
  `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`, per
  [ADR-0009](../adr/0009-assertion.md)'s existing convention.
- `OffscreenTarget`/readback-`Buffer` misuse outside its valid lifetime
  window (destroying `Device` while either is still alive; reading a
  readback `Buffer` before `waitIdle()` has been called for the
  submission that wrote it) is a **lifetime precondition violation**, the
  same tier as every other borrowed/owned-handle misuse case already
  established in this codebase — not claimed to be guaranteed-detectable,
  not tested for detection.
- Every `VkResult` along `OffscreenTarget` construction, copy recording,
  submission, and readback is checked; no `VkResult` is discarded.
- Vulkan Validation Layers are enabled unconditionally in Debug builds and
  any GPU-touching CI job; a validation warning or error is a build/test
  failure.

## Architectural Impact

This spec introduces architecture across three distinct, independently-
reviewable decisions, filed as three new `Proposed` ADRs — none decided
by this spec's prose alone:

1. **Headless offscreen `RenderTarget` construction and ownership** —
   `OffscreenTarget`'s concrete shape, its two-outcome acquire contract,
   and its allocation policy (extending, not reopening,
   [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)).
   Filed as
   [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).
2. **RenderGraph execution — caller-specified incoming and final resource
   states** — resolves a real correctness gap between
   [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)'s
   origin-opacity requirement and
   [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
   hardcoded `PresentSource`/`Undefined` assumptions, and enables a
   second, chained `execute()` call within one frame without changing
   `Renderer`'s public API. Filed as
   [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).
3. **GPU-to-CPU readback RHI capability** — `TransferSource`, the
   readback `Buffer` purpose, `CommandList::copyTextureToBuffer()`, and
   the synchronous readback synchronization/error model. Filed as
   [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified by this spec or by the three new ADRs above — each new ADR
references and extends the existing ones (particularly ADR-0002,
ADR-0015, ADR-0019, ADR-0020, ADR-0021, ADR-0022, ADR-0023, ADR-0025,
ADR-0026) without altering their own Decision text. Architectural Impact
is not "None" — `OffscreenTarget`, the readback capability, and
RenderGraph's generalized binding are each new or changed public API
surface, exactly what [AGENTS.md](../AGENTS.md)'s "What counts as
significant" section requires the full Spec → Plan → Human Review path
for. **This spec's own approval is not itself an authorization to
implement** — a Plan may be drafted per [AGENTS.md](../AGENTS.md) only
once this spec's own PR has merged into `main`, and that future Plan must
still pass its own Human Review before any code, test, or build-
configuration file for this spec's scope is written.

## Alternatives Considered

- **Design and implement Image Regression Testing's golden-image
  comparison in this same spec, rather than deferring it to a separate,
  later spec.** Rejected: [AGENTS.md](../AGENTS.md)'s own sequencing
  treats headless rendering and image regression testing as two
  Candidate Backlog entries with a real dependency edge between them
  (Candidate 3 depends on Candidate 2), and this spec's own three ADRs
  are already independently substantial (offscreen construction, a
  RenderGraph execution-model generalization, and a new RHI capability) —
  bundling golden-image methodology in as well repeats exactly the
  over-scoping mistake Spec 0005's and Spec 0006's own Alternatives
  Considered already rejected once each, for the same reason.
- **Resolve the `PresentSource`/`Undefined` hardcoding gap by forking
  RenderGraph's execution entry point for headless, rather than
  generalizing the existing binding.** Rejected — see
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  Alternatives Considered: this is exactly the kind of fork
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md) exists
  to prevent.
- **Change `Renderer::drawFrame()`'s public signature to thread a shared
  execution-state object through both Renderer's internal `execute()`
  call and the caller's own second one, avoiding the caller needing to
  know `Renderer`'s fixed `ColorAttachmentOutput` contract.** Rejected —
  see
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  Alternatives Considered: reopens
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  already-`Accepted`, already-implemented contract for a capability only
  this spec's own second graph needs.
- **Decide a general GPU memory allocator (VMA or hand-rolled) now, since
  headless plus a future Image Regression Testing harness could
  plausibly create many offscreen resources over time.** Rejected — see
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  Alternatives Considered: this spec's own resource count does not create
  a concrete pooling/suballocation need; reusing
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  existing migration boundary is the correct place for that future
  decision, not here.
- **Silently amend [specs/README.md](README.md)'s Candidate Spec
  Backlog ordering (Android Platform vs. this spec) or
  [docs/project-blueprint.md](../docs/project-blueprint.md)'s milestone
  numbering as part of this spec's own PR.** Rejected: per AGENTS.md,
  governance/roadmap documents change only through their own review or
  explicit, minimal, status-driven registry maintenance (see
  [specs/README.md](README.md)'s own "Backlog maintenance rules"); this
  spec's PR makes only the single, status-driven registry edit that rule
  requires (promoting this spec's own row from Candidate Backlog to the
  Spec Registry) and records the human-directed reprioritization
  suggestion (Headless → Image Regression Testing → Android Platform) as
  an explicit, clearly-labeled, not-yet-approved suggestion — not as a
  rewrite of `docs/project-blueprint.md`'s milestone numbering, which
  remains reserved for a separate, later docs-sync PR per the same
  pattern Spec 0005/0006/0007 all followed.

## Testing & Verification Plan

- **Unit tests:** GPU-independent bookkeeping and validation logic,
  exercised against a fake/mock `CommandList` where a real device is not
  required, per
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  layer 1. At minimum, tests must cover:
  - `execute()`'s generalized binding correctly seeds a bound resource's
    tracked state from a caller-supplied incoming state (not
    `Undefined`) when one is provided, and continues to default to
    `Undefined` when one is not — confirming zero behavior change for
    every binding that omits the new field.
  - `execute()` inserts a trailing transition to a caller-supplied final
    state when one is provided and differs from the resource's ending
    state, inserts none when `std::nullopt` is supplied, and (existing,
    unchanged behavior) inserts none for a resource never used by any
    pass.
  - A pass declaring a single `writes()` usage tagged `TransferSource`
    does **not** trigger `execute()`'s draw-pass recognition (no
    attachment-scoping calls inserted around it) — confirming
    [ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)'s
    existing recognition rule is unaffected by the new state variant.
  - Guard 1 and Guard 2
    ([ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
    continue to hold exactly as before, exercised against bindings that
    do and do not specify the new incoming/final-state fields.
  - `Buffer` construction-parameter validation for the new readback
    purpose (e.g. purpose/usage-mismatch checks not requiring a real
    Vulkan device), where such logic exists independent of the Vulkan
    Backend's own device-dependent creation path.
- **GPU integration tests (Windows/Vulkan):** real `OffscreenTarget`/
  readback-`Buffer`/`CommandList` construction, execution, and
  destruction, Validation-Layers-enabled, mirroring the existing
  `atlantis_vulkan_backend_gpu_tests`/`atlantis_render_graph_tests`
  pattern. Must cover, at minimum: creating and destroying an
  `OffscreenTarget`; acquiring, using, and consuming its vended
  `RenderTarget` more than once across the same instance's lifetime;
  creating and destroying a readback `Buffer`; one full render-and-
  readback cycle (`Renderer::drawFrame()` into the offscreen target,
  followed by the copy pass, submission, and `waitIdle()`-gated read) with
  Validation Layers reporting zero warnings/errors; and the existing
  windowed GPU integration tests (`frame_execution_demo`/
  `minimal_renderer_demo`-backing tests) re-run unmodified in behavior
  after their mechanical binding-call-site update, confirming zero
  regression.
- **Headless integration tests:** this spec **is** what makes this test
  layer possible for the first time
  ([docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  layer 2) — its own GPU integration tests above are the first instance
  of it, though this spec does not yet formalize a distinct CI job
  category for it (see Risks & Open Questions and
  [ci-strategy.md](../docs/process/ci-strategy.md)'s own open questions).
- **Image regression tests:** not applicable — this spec's own basic
  content check (below) is explicitly not a golden-image comparison; that
  remains a future spec's scope, per Non-Goals.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of `OffscreenTarget` construction, draw,
  copy, submission, and readback — per [AGENTS.md](../AGENTS.md) and
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- **Manual/automated verification composition:** a minimal, non-shipping
  composition (mirroring `examples/minimal_renderer_demo`'s own structure
  and disclaimer, but creating **no window and no Atlantis Platform
  instance of any kind**) constructs a `Device`, an `OffscreenTarget`,
  this spec's fixed `Mesh`/`Material`/camera uniform `Buffer`/depth
  `Texture`/readback `Buffer`, and runs the full acquire → draw → copy →
  submit → `waitIdle()` → read cycle. It confirms:
  - No window, `HWND`, message pump, or `Presentation`/`VkSwapchainKHR`
    object is created anywhere in this composition — verifiable by
    inspection.
  - The readback `Buffer`'s contents, after the cycle completes, pass a
    **reproducible basic content check** — not a golden-image comparison
    (see Non-Goals) — sufficient to demonstrate the mesh actually drew
    (e.g. the buffer is not uniformly one color/all-zero, and a small,
    fixed set of known sample positions — e.g. image center vs. a
    background-only corner — differ from each other in the direction the
    fixed mesh/camera/material fixture predicts). The exact check's
    concrete shape is left to the Plan; this spec fixes only that it must
    be automated and reproducible (same input, same output, every run),
    not a human eyeballing a screenshot.
  - The same cycle repeated more than once against the same
    `OffscreenTarget` instance produces consistent results each time
    (confirming single-frame-in-flight discipline holds across repeated
    acquire/readback cycles, not just once).
  - The composition exits cleanly with no outstanding acquired
    `RenderTarget`, no leaked `CommandList`/`Buffer`/`Texture`/
    `OffscreenTarget`, and no Validation Layer warning or error at any
    point, including at shutdown.
  - Separately, `examples/frame_execution_demo` and
    `examples/minimal_renderer_demo`, after their mechanical binding
    update, are re-run interactively and confirmed to behave identically
    to their Spec 0006/0007-verified behavior (visible frame, correct
    resize/minimize/restore, Validation Layers clean) — the explicit
    regression check this spec's Non-Goals require.

## Risks & Open Questions

- **Exact concrete C++ shape of `execute()`'s extended binding entry**
  (a struct with named fields vs. a tuple vs. an overload set) is left to
  the Plan — this spec and
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  fix the conceptual contract (caller-supplied optional incoming state,
  required optional-typed final state), not its exact C++ representation.
- **Exact names** for `OffscreenTarget`'s acquire-equivalent method,
  `CommandList::copyTextureToBuffer()`, and the readback `BufferPurpose`
  enumerator are left to the Plan, consistent with every prior spec's own
  practice of fixing semantics and deferring exact spelling.
- **Whether a distinct CI/test-category label for headless GPU integration
  tests is needed**, separate from the existing `gpu`-labeled pattern —
  the same open question Spec 0006 and Spec 0007 already flagged for
  windowed GPU tests, now recurring for this spec's own new headless
  category; flagged, not resolved, left to the Plan or a future
  testing-strategy amendment.
- **Whether the readback `Buffer`'s tightly-packed byte layout needs any
  row-pitch/alignment padding handling** for a color format whose
  per-row byte count doesn't naturally align to a convenient boundary —
  a real Vulkan Backend implementation concern (`vkCmdCopyImageToBuffer`'s
  `bufferRowLength`/`bufferImageHeight` parameters), left entirely to the
  Plan as an implementation detail this ADR/spec does not need to fix,
  since it is private to the Vulkan Backend's own "how" responsibility per
  [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md).
- **Whether a future Image Regression Testing spec will need
  `OffscreenTarget` to support more than one simultaneously-live instance,
  or pooling/reuse across many test cases**, is explicitly left open — see
  Non-Goals; this spec's own verification composition needs only one.
- **Whether a future spec revisiting the single-frame-in-flight baseline
  will also need to revisit this spec's fully-synchronous `waitIdle()`-
  based readback model** is left open, mirroring
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  own equivalent, already-accepted open question for the windowed path.
- **Whether depth-buffer readback will be needed by a future spec** (e.g.
  for depth-aware image regression comparison) is left open; this spec's
  own Non-Goals explicitly exclude it, and
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)'s pattern
  would need extending, not reusing unchanged, to support it.

## Out of Scope / Future Work

Image Regression Testing (Candidate 3 in
[specs/README.md](README.md)'s backlog, depending on this spec) is the
direct, named next consumer of this spec's rendering-and-readback
foundation — golden-image storage, comparison/tolerance methodology, and
CI gating are entirely its own future scope, not advanced or pre-designed
here beyond this spec satisfying it as a prerequisite. Android Platform
and Vulkan presentation (a separate Candidate Backlog entry) is unaffected
by and does not depend on this spec in either direction; see this spec's
own accompanying registry update and PR description for the human-
directed, not-yet-approved suggestion to sequence it after both this spec
and Image Regression Testing. A future spec wanting `OffscreenTarget`
pooling, multiple simultaneous instances, depth-buffer readback,
asynchronous/non-blocking readback, or a distinct headless-GPU-test CI
category is expected to extend — not merely reuse unchanged — the
ADRs this spec introduces; none of that shape is predicted or
pre-designed here.
