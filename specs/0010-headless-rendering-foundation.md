# Spec: Headless Rendering Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved by human review — see Human Review Approval below.
- **Created:** 2026-08-15
- **Human Review Approval (2026-08-16):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-16, following **three
  independent, read-only Human Review rounds**, each conducted against
  this spec's actual drafted text and the real shipped implementation
  (`src/renderer/`, `src/render_graph/`, `src/rhi/`,
  `src/vulkan_backend/`) rather than against the drafter's own summaries
  — see [PR #42](https://github.com/slmao/Atlantis/pull/42) for the
  first two rounds' findings and revisions, and
  [PR #43](https://github.com/slmao/Atlantis/pull/43) for the third
  round's finding and fix:

  1. **Round 1** found that this spec's first draft incorrectly claimed
     `Renderer::drawFrame()` requires no change and leaves its color
     `RenderTarget` in `ResourceState::ColorAttachmentOutput` when it
     returns — verified false against `src/renderer/src/renderer.cpp`
     and `src/render_graph/src/execution.cpp`: `Renderer::drawFrame()`
     calls the same shared `render_graph::execute()` every other caller
     does, and that function (prior to this spec) unconditionally
     transitioned any bound, touched `RenderTarget` to
     `ResourceState::PresentSource`. Resolved by a Proposed Amendment to
     [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)
     (`Renderer::drawFrame()` gains a required, backend-agnostic
     `finalColorState` parameter, passed through uninspected) and a
     corrected
     [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).
  2. **Round 2** found that [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
     promised its vended `RenderTarget` borrow "is consumed by the
     readback operation ADR-0040 defines," but
     [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md) never
     defined any such call —
     `copyRenderTargetToBuffer(RenderTarget&, Buffer&)` only borrows a
     reference to record a command, it never takes ownership. Resolved
     by an explicit, RAII-based borrow-lifetime contract in ADR-0038 and
     a corresponding disclaimer in ADR-0040.
  3. **Round 3** found that ADR-0038's round-2 revision conflated the
     borrow wrapper's lifetime with `OffscreenTarget`'s own (backing-
     resource) lifetime, leaving no precondition preventing a caller
     from legally dropping the borrow right after `Device::submit()`
     returns and then destroying `OffscreenTarget`/`Device` while GPU
     work referencing its resources was still in flight — verified
     directly against `VulkanDevice::submit()`'s fence-wait,
     `VulkanDevice`'s own destructor, and
     `VulkanPresentation::~VulkanPresentation()`'s established "the
     caller must call `Device::waitIdle()` first; this destructor does
     not itself wait" precedent
     (`src/vulkan_backend/src/vulkan_device.cpp`,
     `src/vulkan_backend/src/vulkan_presentation.cpp`). Resolved by
     restructuring ADR-0038's lifetime contract into two explicit,
     independent parts (borrow wrapper vs. `OffscreenTarget`/backing
     resources), each stated separately, per
     [PR #43](https://github.com/slmao/Atlantis/pull/43).

  **Eight design decisions were confirmed explicitly as part of this
  approval, and are accepted as-is:**

  1. `Renderer::drawFrame()` gains a required, backend-agnostic
     `finalColorState` parameter — a windowed caller passes
     `ResourceState::PresentSource`, a headless caller passes
     `ResourceState::TransferSource` directly (no intermediate
     presentation-shaped layout ever recorded); `Renderer` itself never
     observes, validates, or branches on the value, preserving its
     origin-opacity contract completely. See
     [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
     Proposed Amendment (`Accepted Amendment` alongside this approval).
  2. `OffscreenTarget` is introduced as a second `RenderTarget` source,
     with no `Presentation` involved; the Vulkan Backend implements it
     as an owning, private type vending non-owning, per-cycle borrows,
     directly mirroring the existing `Presentation`/`VulkanRenderTarget`
     pattern rather than inventing a new ownership shape. See
     [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).
  3. A vended borrow ends via ordinary RAII (destroying/resetting the
     `std::unique_ptr<RenderTarget>`) — no new public `release()`/
     `consume()` API is introduced.
  4. **Two explicitly separate lifetimes govern `OffscreenTarget`.** The
     borrow wrapper's minimum required lifetime extends only through the
     `Device::submit()` call that references it. `OffscreenTarget`'s own
     backing resources (the color `VkImage`/memory/view) must remain
     alive until the GPU work that referenced them has actually
     completed — established by the caller via `Device::waitIdle()`
     before destroying `OffscreenTarget`, a documented, caller-enforced
     precondition at the same tier as the existing `Presentation`/
     `Device` rule ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)),
     deliberately **not** guaranteed-detectable (no new assertion,
     `Result` error, implicit destructor wait, or per-resource tracking
     system is introduced to make it so).
  5. Repeated acquisition without returning a prior borrow, destroying
     `OffscreenTarget`/`Device` while a borrow is still outstanding, and
     supplying a `finalColorState`/`incomingState`/`finalState` value
     unsupported by the Vulkan Backend's transition table are each
     guaranteed-detectable programmer errors via this codebase's
     existing `ATLANTIS_CHECK`/`ATLANTIS_ASSERT` mechanism — never a
     compile-time restriction, never a `Result`-typed error.
  6. RenderGraph's `execute()` binding gains caller-declared
     `incomingState`/`finalState` `ResourceState` fields;
     `ColorAttachmentOutput → TransferSource` is confirmed as the one and
     only new Vulkan Backend transition-table entry this spec's design
     requires. See
     [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).
  7. `ResourceState::TransferSource`, a fourth `BufferPurpose`
     (readback), and `CommandList::copyRenderTargetToBuffer()` are
     introduced as the minimal, narrow GPU-to-CPU readback capability.
     See [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md).
  8. No general GPU memory allocator, asynchronous/non-blocking
     readback, multiple frames in flight, Linux support, Android
     support, or image-regression comparison/tolerance methodology/CI
     gating is introduced — all remain explicitly out of this spec's
     scope, per its own Non-Goals; none of the above is authorized or
     promised as implemented by this approval.

  Following this approval:
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md),
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md),
  and [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md) each
  move to `Accepted`, and
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  2026-08-15 Proposed Amendment moves to `Accepted Amendment` (see each
  document's own header/section for the precise record) — and this spec
  moves to `Approved`. Per [AGENTS.md](../AGENTS.md), a Plan may now be
  drafted against this spec; **this approval is not itself an
  authorization to implement** — that future Plan must still pass its
  own (or a joint Spec+Plan) Human Review before any source, test,
  shader, or build-configuration file for this spec's scope is written.
  The following remain **open, Plan-stage questions, not blockers to
  this approval** (full list in this spec's own Risks & Open Questions
  section, unchanged by this approval): exact C++ names and parameter
  positions for `OffscreenTarget`'s acquire method,
  `Renderer::drawFrame()`'s `finalColorState` parameter, and the Vulkan
  Backend's owning/non-owning implementation classes; the exact
  `ResourceBinding` extension's concrete C++ representation; the exact
  reproducible basic-content-check shape; whether a distinct CI/test-
  category label for headless GPU integration tests is needed; and row-
  pitch/alignment handling inside the Vulkan Backend's copy
  implementation.
- **Related Plan(s):**
  [plans/0010-headless-rendering-foundation.md](../plans/0010-headless-rendering-foundation.md)
  (`Draft`) — drafted against this `Approved` spec, per
  [AGENTS.md](../AGENTS.md); not yet reviewed or approved. Implementation
  may not begin until this Plan (or a joint Spec+Plan review) passes its
  own Human Review.
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0009](../adr/0009-assertion.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  (all `Accepted`). See **Architectural Impact** below — four new
  decisions were identified and drafted alongside this spec:
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  (offscreen `RenderTarget` construction and ownership),
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  (RenderGraph execution — caller-specified incoming/final resource
  states), [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)
  (GPU-to-CPU readback capability) — all `Accepted` alongside this
  spec's own approval above — and a **Proposed Amendment (2026-08-15)**
  to the already-`Accepted`
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)
  (`Renderer::drawFrame()` gains an explicit `finalColorState`
  parameter), now `Accepted Amendment` alongside this same approval.

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
`CommandList` with `Renderer::drawFrame()`'s own internal one.
**`Renderer::drawFrame()` itself gains one new, required, backend-
agnostic parameter** (`ResourceState finalColorState`) so it can tell its
caller-agnostic internal graph what state to leave the color target in —
without ever learning why; this is the minimal, reviewed change to an
already-`Accepted` API needed to make headless correct, not an
unconstrained reopening of it. It does **not** design golden-image
comparison, tolerance methodology, or CI gating — this spec is the
rendering-and-readback foundation a future Image Regression Testing spec
depends on, not that spec itself.

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
  frame ever existed — and this includes the target `Renderer` itself
  draws into, not only a caller's own graph.** Verified against the
  actual implementation
  (`src/render_graph/src/execution.cpp`,
  `src/renderer/src/renderer.cpp`): `Renderer::drawFrame()` calls the
  same shared `render_graph::execute()` function every other caller
  does, and that function unconditionally transitions every bound,
  touched `RenderTarget` to `ResourceState::PresentSource` before
  returning, and starts every `execute()` call's own state tracking from
  `ResourceState::Undefined` regardless of any prior `execute()` call
  that already used the same physical resource within the same
  `CommandList`. Both assumptions are wrong for a headless target read
  back via a second, chained `execute()` call. **An earlier draft of this
  spec incorrectly claimed `Renderer::drawFrame()` requires no change and
  leaves its color target in `ResourceState::ColorAttachmentOutput`; both
  claims were checked against the shipped code during this spec's own
  Human Review and found false — the real, pre-this-spec behavior is
  `ResourceState::PresentSource`, unconditionally, for every caller
  including `Renderer`'s own internal one.** See **Architectural Impact**
  and
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment /
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  for the corrected design and the full evidence trail.

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
  already defines, with no `Presentation` object involved, and an
  explicitly-resolved ownership/lifetime relationship between the
  long-lived `OffscreenTarget` and the short-lived `RenderTarget`
  borrows it vends
  ([ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)).
- Extend RHI with a minimal, narrow GPU-to-CPU readback capability: one
  new `ResourceState` (`TransferSource`), one new `Buffer` purpose
  (readback), and one new `CommandList` copy operation
  ([ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)).
- Generalize RenderGraph's `execute()` binding to accept a caller-
  specified incoming and final `ResourceState` per bound resource, fixing
  the hardcoded-`Undefined`/hardcoded-`PresentSource` assumptions —
  precisely enumerating every existing call site this requires updating,
  including `Renderer`'s own internal one
  ([ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)).
- **Give `Renderer::drawFrame()` an explicit, backend-agnostic,
  required `finalColorState` parameter**, as a narrow, reviewed,
  Proposed Amendment to the already-`Accepted`
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md) —
  the minimal change that lets a headless caller request
  `ResourceState::TransferSource` directly, with no intermediate,
  never-observed `PresentSource` transition, while `Renderer` itself
  remains completely unaware of *why* a particular value was chosen.
- Demonstrate, end-to-end, that `Renderer::drawFrame()` — its dependency
  boundary and per-frame responsibilities unchanged, its public
  signature extended by exactly one caller-supplied parameter — draws
  the same mesh/material/camera Spec 0007 already verifies, into an
  offscreen `RenderTarget`, with no window, `Presentation`, or swapchain
  anywhere in the composition.
- Verify a full render → readback cycle produces a reproducible, basic
  content check (not a golden-image comparison — see Non-Goals) on
  Windows with a real GPU, with Vulkan Validation Layers clean throughout.
- Resolve the architectural decisions this spec identifies via dedicated
  ADRs/amendment, so a future Image Regression Testing spec inherits a
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
  spec's own output.
- **Android Platform, iOS Platform, or a second graphics backend of any
  kind.** Windows/Vulkan only, per [AGENTS.md](../AGENTS.md). This spec's
  headless path uses no Atlantis Platform code at all (no window is
  created).
- **Linux as a target platform, or any Linux-specific build/CI/runtime
  content.** Per [AGENTS.md](../AGENTS.md), Linux is not a target platform
  for Atlantis.
- **A general, sampled/shader-readable `Texture` usage, or a general
  `Sampler` type.** [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)'s
  readback capability is scoped strictly to a copy-out-to-`Buffer`
  operation, not to making the offscreen color image readable by a
  shader.
- **Widening the existing `Texture` type to support a color or
  transfer-source usage.** [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  explicitly rules this out — `Texture` remains exactly as depth-only as
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  already fixed it; the offscreen color resource is a second, distinct
  RHI concept.
- **Depth-buffer readback.** Only the color `RenderTarget` gains a
  readback path this round; the depth `Texture`'s existing, unchanged
  ownership/lifecycle is reused verbatim.
- **Asynchronous, non-blocking, or multi-frame-latency-amortized
  readback.** Readback this round is synchronous and blocking
  (`Device::waitIdle()`-based).
- **Multiple frames in flight, multi-threaded command recording/resource
  creation/graph execution, or any job/task system.**
- **A general GPU memory suballocator (VMA or hand-rolled), or any change
  to [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s
  general deferral.**
- **Any change to `Renderer`'s dependency boundary, ownership model, or
  per-frame responsibilities beyond the one new `finalColorState`
  parameter.** `Renderer` still depends only on `Atlantis::RHI`/
  `Atlantis::RenderGraph`/`Atlantis::Core`; still never owns a
  `RenderTarget`, depth `Texture`, `Mesh`, or `Material`; still builds
  exactly one internal draw pass with unchanged
  `ColorAttachmentOutput`/`DepthAttachmentReadWrite` usages; still never
  learns whether it was called by a windowed or headless composition.
  **This spec does not claim `Renderer`'s public API is unaffected** —
  see Requirements and
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment for the one, narrow, explicitly-reviewed change it
  does make.
- **A second rendering path, a second `RenderGraph`/RHI implementation,
  or any fork of `Renderer`.**
- **Multiple simultaneous `OffscreenTarget` instances, or any pooling/
  reuse-across-instances policy for them.**
- **Any regression to the windowed path.** `examples/frame_execution_demo`
  and `examples/minimal_renderer_demo`, once mechanically updated (see
  Requirements), must continue to build and behave identically to their
  Spec 0006/0007-verified behavior — no windowed output or Vulkan call
  sequence changes.
- **Editing [specs/README.md](README.md)'s Section A entries for prior
  specs, [docs/project-blueprint.md](../docs/project-blueprint.md), or any
  other governance/roadmap document beyond this spec's own required
  backlog-registry update.**

## Requirements

### Functional

**Offscreen `RenderTarget` construction and ownership**

- A new RHI type, `OffscreenTarget`, constructed via `Device` with a
  fixed extent and color `atlantis::rhi::Format` (caller-specified,
  never changed for that instance's lifetime — no resize/recreation
  concept), owning exactly one color image (and its backing memory) for
  its whole lifetime. Full contract in
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).
- `OffscreenTarget` vends a `RenderTarget` via a two-outcome
  (`Err`/`Ok`, matching `Presentation`'s real
  `Result<std::unique_ptr<RenderTarget>, Err>` shape) acquire-equivalent
  call — no zero-extent case, unlike `Presentation`. `Err` is reserved
  for genuine, environmental failures; calling this method while a
  previously-vended borrow is still outstanding is a separate,
  guaranteed-detectable programmer error (assertion), not part of this
  `Result::Err` channel.
- The vended `RenderTarget` is the exact same abstract public type
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
  already defines — no new field or method on its public interface. A
  **second concrete Vulkan Backend implementation** of it is introduced
  (non-owning, borrowing from the owning `OffscreenTarget` implementation,
  mirroring exactly how the existing `VulkanRenderTarget` borrows from
  `VulkanPresentation`) — see
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  for the full ownership/lifetime split.
- **Two distinct, non-equivalent lifetimes govern this type — the borrow
  wrapper's, and `OffscreenTarget`'s own (backing-resource) lifetime.
  Conflating them is a real correctness risk, not a documentation
  nicety** — see
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)'s
  own "Lifetime contract" section, which a third-round Human Review
  revised specifically to keep the two separate after an earlier
  revision conflated them.
- **Borrow wrapper.** `OffscreenTarget` has no `present()` counterpart
  and no other explicit borrow-ending method — a borrow ends via
  ordinary RAII (destroying/resetting the vended
  `std::unique_ptr<RenderTarget>`), not by any call
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md) defines.
  The borrow's **minimum required lifetime** extends only through the
  return of the `Device::submit()` call whose recorded commands
  reference it — it does **not** need to survive through
  `Device::waitIdle()`, reading the readback `Buffer`, or the completion
  of the GPU work that referenced it, because Vulkan bakes handles, not
  C++ object references, into a recorded `CommandList`. **This claim is
  about the wrapper only; it says nothing about, and does not relax,
  when `OffscreenTarget` itself may be destroyed** — see below.
- The same `OffscreenTarget` instance may be acquired-and-borrowed more
  than once across its lifetime, each cycle independent.
  Acquiring a second `RenderTarget` while a previously-vended borrow is
  still outstanding, or destroying `OffscreenTarget` while a borrow it
  vended is still outstanding, are each a guaranteed-detectable
  programmer error — a deliberate, disclosed improvement over
  `Presentation`'s equivalent, currently-undetectable precondition for
  that specific case, made possible because the same outstanding-borrow
  tracking already exists for the double-acquire check. **This check
  answers only "is a borrow wrapper currently alive" — it is not, and
  cannot be, a substitute for the separate GPU-completion precondition
  below.**
- **`OffscreenTarget` and backing resources (the color `VkImage`, its
  memory, its view).** `OffscreenTarget` — and the `Device` it was
  constructed from — must not be destroyed while GPU work that
  referenced its backing resources is still in flight, **independently
  of whether the borrow wrapper that referenced them has already been
  destroyed.** This is a lifetime precondition violation, at the same
  tier as the identical, existing `Presentation`/`Device` rule
  ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)) —
  **deliberately not guaranteed-detectable**: Phase 1's `Device` exposes
  only a single, coarse `Device::waitIdle()` completion signal, with no
  finer-grained per-resource tracking to check against, and this spec
  does not introduce one. A caller satisfies this precondition by
  calling `Device::waitIdle()` after the last `Device::submit()` call
  that referenced this `OffscreenTarget`, before destroying it — the
  same, unchanged mechanism already required before destroying
  `Presentation`/`Device`. **Destroying `OffscreenTarget`'s backing
  resources first and relying on `Device`'s own destructor to wait
  afterward is the wrong order and is not safe** — `Device`'s destructor
  only drains work related to its own teardown; by the time it runs,
  already-destroyed `OffscreenTarget` resources cannot be protected by
  it. Correct order: `Device::waitIdle()` → destroy `OffscreenTarget` →
  (whenever done) destroy `Device`.
  Full contract, including why re-acquiring against a still-alive
  `OffscreenTarget` needs no intervening `waitIdle()` (this is a
  separate claim from the destruction precondition above, backed by
  `Device::submit()`'s own fence-wait), in
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).
- The depth `Texture` used alongside an `OffscreenTarget` is constructed,
  owned, and destroyed via the existing, unchanged
  `Device::createTexture()` path — `OffscreenTarget` itself never owns,
  creates, or references a depth resource, and `Texture`/`DepthFormat`
  are entirely untouched by this spec.
- **GPU memory allocation:** every allocation `OffscreenTarget`'s color
  image requires uses
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)'s
  existing direct, unpooled, Vulkan-Backend-private policy.

**`Renderer::drawFrame()`'s new `finalColorState` parameter**

- `Renderer::drawFrame()` gains one new, required parameter:
  `atlantis::rhi::ResourceState finalColorState` — a Proposed Amendment
  to [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
  not a silent reinterpretation of it. Exact parameter position is a
  Plan-stage detail.
- `Renderer` passes this value through, unmodified and uninspected, as
  the `finalState` field of its own internal color `ResourceBinding`
  entry ([ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)).
  `Renderer`'s draw pass itself is unchanged — it still declares exactly
  one `writes()` usage tagged `ColorAttachmentOutput`.
- A windowed caller passes `ResourceState::PresentSource` — the exact
  value `execute()`'s old hardcoded behavior already produced, a
  zero-behavior-change update once the one existing call site
  (`minimal_renderer_demo`) supplies it explicitly.
- A headless caller passes `ResourceState::TransferSource` directly — no
  intermediate `PresentSource` transition is ever recorded for a target
  that will never be presented.
- `Renderer` does not interpret, validate, or branch on this value in
  any way, and gains no knowledge of `Presentation`, `VkSwapchainKHR`,
  `OffscreenTarget`, or any other origin-specific concept — see
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment for the full contract and rationale.

**GPU-to-CPU readback capability**

- `ResourceState` gains exactly one new variant: `TransferSource`. Full
  contract in
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md).
- `Buffer` gains a fourth, fixed purpose: readback — host-visible,
  host-coherent, mapped once for its whole lifetime, created via the
  existing, unchanged `Device::createBuffer()`.
- `CommandList` gains exactly one new recordable operation:
  `copyRenderTargetToBuffer(RenderTarget&, Buffer&)` — named after its
  actual parameter type, not `Texture` (an earlier draft's
  `copyTextureToBuffer` name was corrected during this spec's own Human
  Review, since the parameter is `RenderTarget&`). Copies the full,
  tightly-packed color image into a readback-purpose `Buffer`; no
  partial-region copy, no format conversion.
- The copy is recorded as a RenderGraph pass declaring **exactly one
  `writes()` usage tagged `ResourceState::TransferSource`** against the
  logical resource the color `RenderTarget` is bound to, with
  `incomingState = ResourceState::TransferSource` (matching exactly what
  `Renderer::drawFrame()` was already told to leave the target in) and
  `finalState = std::nullopt`. **Because the resource's tracked incoming
  state already equals this pass's own declared state, `execute()`
  inserts no `transitionResource()` call for this pass at all** — its
  callback consists solely of the copy call. `execute()`'s existing
  draw-pass recognition does not fire for this pass.
- Readback is synchronous and blocking: the caller submits, calls the
  existing `Device::waitIdle()`, then reads the readback `Buffer`'s
  already-mapped pointer (`Buffer::mappedData()`) directly.
- Every `VkResult` along copy recording, submission, and wait is checked.

**RenderGraph execution generalization for non-presentable and chained
bindings**

- `execute()`'s existing frame-scoped external resource binding
  (`ResourceBinding`,
  `src/render_graph/include/atlantis/render_graph/execution.h`) gains
  two additional fields, meaningful only for `target`-shaped
  (`RenderTarget`) entries: `incomingState` (`ResourceState`, defaults to
  `Undefined` — **safe only for a resource being bound to its first
  `execute()` call within its current `CommandList`/frame; using the
  default for a resource already touched by an earlier `execute()` call
  sharing the same `CommandList` is a caller precondition violation that
  silently discards the resource's real prior contents, not a
  guaranteed-detectable error**) and `finalState`
  (`std::optional<ResourceState>`, **no default — every `target`-shaped
  binding entry must supply one explicitly**; `std::nullopt` means "no
  trailing transition beyond whatever the last pass leaves it in"). Full
  contract, including the exact default/misuse policy, in
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).
- **Every existing call site that constructs a `target`-shaped
  `ResourceBinding` must be mechanically updated — three, not two, named
  explicitly, none optional:**
  1. `src/renderer/src/renderer.cpp` — supplies `finalState =
     finalColorState` (the new parameter above); `incomingState` stays
     at its default.
  2. `examples/frame_execution_demo/main.cpp` — its own, direct,
     non-`Renderer` `execute()` call must supply
     `finalState = ResourceState::PresentSource` explicitly.
  3. `examples/minimal_renderer_demo`'s verification composition — must
     pass `ResourceState::PresentSource` as `Renderer::drawFrame()`'s
     new `finalColorState` argument.
  All three are mechanical, non-behavioral updates for the windowed
  path — each supplies exactly the value the old hardcoded behavior
  already produced.
- **Vulkan Backend impact, named explicitly:** `resource_state_mapping.cpp`'s
  `planTransition()` function is a closed, exhaustively-enumerated
  lookup table; any `(before, after)` pair not listed triggers an
  assertion failure, not a computed fallback (verified by inspection).
  This spec's design requires **exactly one new entry:
  `ColorAttachmentOutput → TransferSource`** — used by `Renderer`'s own
  internal trailing transition when a headless caller supplies
  `finalColorState = TransferSource`. No other new entry is required —
  the windowed path continues to use the already-existing
  `ColorAttachmentOutput → PresentSource` entry, and the readback copy
  pass itself (see above) never calls `transitionResource()` at all.
- `execute()`'s algorithm (walk compiled pass order, track
  most-recently-recorded state per bound resource, insert
  `transitionResource()` on a state change, insert one trailing call if
  a final state is specified and differs from the resource's ending
  state) is otherwise unchanged. Guard 1 and Guard 2 are unchanged; the
  bound depth `Texture`'s binding (never trailing-transitioned, and
  unaffected by either new field) is unaffected.

**Reuse of Renderer, RenderGraph, RHI, and Vulkan Backend — no fork**

- The headless verification composition constructs `Mesh`/`Material`/the
  camera uniform `Buffer` using the same fixed fixture Spec 0007's
  windowed verification composition already uses (exact code-sharing
  mechanism left to the Plan), and calls `Renderer::drawFrame()` with the
  same borrowed-reference contract as the windowed composition, plus the
  new `finalColorState` argument, passing the `OffscreenTarget`-vended
  `RenderTarget` and a depth `Texture` in place of the windowed ones.
- `Renderer`'s dependency set (`Atlantis::RHI`/`Atlantis::RenderGraph`/
  `Atlantis::Core` only), ownership model, and per-frame responsibilities
  are unaffected beyond the one new parameter above.

**Phase 1 single-threaded orchestration and thread-safety contracts**

- Every new public type this spec introduces (`OffscreenTarget`, the
  extended `execute()` binding, `CommandList::copyRenderTargetToBuffer()`,
  `Renderer::drawFrame()`'s new parameter) documents its thread-safety
  contract at its public API — "not thread-safe; caller-thread-only," on
  the single Phase 1 logical thread, per
  [ADR-0004](../adr/0004-phase1-threading-baseline.md). No mutex, atomic,
  job/task system, or lock-free structure is introduced. Unlike the
  windowed path, this thread is not required to also own a Platform
  message pump — a headless composition has no window and no Platform
  event loop to run — but it remains exactly one logical thread.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" outside the deliberate `waitIdle()`-based readback
  stall, an explicit, accepted simplification, not a performance claim.
- **Memory:** no general GPU memory suballocation strategy is introduced
  or assumed.
- **Portability (within the Vulkan-only Phase 1 constraint):**
  implemented and verified on Windows only, using no window and no
  Atlantis Platform code at all.
- **Other:** no new third-party dependency. Unit tests use the existing
  Catch2 v3 framework ([ADR-0007](../adr/0007-test-framework.md)).

## Proposed Design

### Module boundaries (realizing, not moving, existing ones)

Realizes exactly the headless path
[ADR-0002](../adr/0002-presentation-rendertarget-unification.md) and
[resource_lifetime.md](../docs/architecture/resource_lifetime.md) already
anticipated: RHI gains a second `RenderTarget`-vending type
(`OffscreenTarget`, alongside `Presentation`) and a narrow readback
extension; RenderGraph's existing RHI dependency is generalized, not
widened to a new dependency direction. `Renderer`'s dependency set is
unchanged; its public signature gains one new parameter, per
[ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
Proposed Amendment.

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
                       cameraBuffer, drawItems,
                       ResourceState::TransferSource)
    -- internally: builds its own graph exactly as Spec 0007 does
       (ColorAttachmentOutput + DepthAttachmentReadWrite draw pass),
       compiles it, and calls render_graph::execute() with its own
       binding for the color target carrying
       finalState = TransferSource (the new argument, passed through
       unmodified) -- execute() inserts the draw pass's own
       Undefined -> ColorAttachmentOutput transition, then, because
       finalState differs from the pass's own ending state, one
       trailing ColorAttachmentOutput -> TransferSource transition
       (this spec's one new planTransition() entry) --
       -- the color RenderTarget is now in TransferSource state; no
       PresentSource transition ever occurs for this target --

  Caller builds a second, small RenderGraphBuilder description: one pass
    declaring a single writes() usage tagged TransferSource against the
    same logical resource the RenderTarget is bound to; execution
    callback calls CommandList::copyRenderTargetToBuffer(*renderTarget,
    *readbackBuffer)
  Caller compiles that second graph -> CompiledGraph
  render_graph::execute(compiledGraph,
                         bindings{resource -> (RenderTarget,
                                  incomingState = TransferSource,
                                  finalState = std::nullopt)},
                         commandList)
    -- the resource's tracked state (seeded from incomingState =
       TransferSource) already equals this pass's own declared state,
       so execute() inserts no transitionResource() call at all; only
       the copy pass's callback runs --

  Device::submit(commandList, target's-acquire-complete-signal)
    -- the borrow WRAPPER's minimum required lifetime ends here (ADR-0038
       Part 1); this says nothing about OffscreenTarget itself, which
       remains governed entirely by Part 2 below --
  Device::waitIdle()
    -- two independent things become true after this call returns:
       (a) the readback Buffer's writes are now host-visible, and
       (b) the GPU-completion precondition OffscreenTarget's own
       destruction requires (ADR-0038 Part 2) is now satisfied for every
       submission up to and including this cycle's --

  Read readbackBuffer's mapped pointer; run this spec's basic,
    reproducible content check (see Testing & Verification Plan)

  Drop/reset the RenderTarget borrow (ordinary RAII -- no explicit
    method call; OffscreenTarget's outstanding-borrow tracking clears
    itself via the borrow's own destructor)
    -- only now may the next OffscreenTarget::acquireTarget() call
       succeed; calling it earlier, while this borrow is still alive,
       is a guaranteed-detectable programmer error. Re-acquiring against
       the SAME, still-alive OffscreenTarget needs no further waitIdle()
       here -- the next cycle's own submit() call serializes against
       this one's fence internally (ADR-0038 Part 2's repeated-cycle
       claim) --

  -- If this cycle is the last one: OffscreenTarget may now be safely
     destroyed or reused for another purpose, because the waitIdle()
     call above already established GPU completion for every
     submission that referenced it (ADR-0038 Part 2) -- destroying
     OffscreenTarget's backing resources BEFORE that waitIdle() call,
     and relying on Device's own destructor to "catch up" afterward,
     would be the wrong order and is not safe (Device's destructor only
     drains its own teardown, not an already-destroyed OffscreenTarget's
     resources). On every exit path, including an early/error exit:
     Device::waitIdle() before destroying OffscreenTarget/Device/depth
     Texture/readback Buffer/Mesh/Material --
```

### Offscreen `RenderTarget` construction and ownership

See
[ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
for the full decision: `OffscreenTarget`'s shape, the owning/non-owning
Vulkan Backend implementation split, its two-outcome acquire contract,
its allocation policy, and why `Texture` is not reused.

### `Renderer::drawFrame()`'s `finalColorState` parameter

See
[ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
Proposed Amendment for the full decision: the new parameter's exact
contract, why it is required (not defaulted or boolean), and why it
preserves `Renderer`'s origin-opacity completely.

### GPU-to-CPU readback

See
[ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md) for the
full decision: `TransferSource`, the readback `Buffer` purpose,
`copyRenderTargetToBuffer()`, the synchronous `waitIdle()`-based
synchronization model, and error semantics.

### RenderGraph execution generalization

See
[ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
for the full decision: the caller-specified incoming/final `ResourceState`
binding fields, their exact default/misuse policy, the full list of
mechanically-updated call sites, and the one new `planTransition()` entry.

### Threading

Single logical thread, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md) — unchanged in kind
from every prior spec, minus the windowed path's additional obligation to
also own a Platform message pump (headless has none).

### Error handling

- Recoverable runtime errors (`OffscreenTarget`/`Buffer`/`Texture`
  creation failure, submission failure) use `atlantis::Result<T, E>`,
  consistent with every prior spec's convention. `OffscreenTarget`'s
  acquire-equivalent call's `Err` is reserved for genuine, environmental
  failures only.
- Programmer errors, all `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`
  (`ADR-0009`), all guaranteed-detectable:
  - Guard 1/Guard 2 violations (unchanged scope, generalized mechanism).
  - Acquiring a second `RenderTarget` from an `OffscreenTarget` before
    the first has been returned (destroyed/reset — RAII, see below).
  - Destroying `OffscreenTarget` (or its `Device`) while a vended borrow
    is still outstanding — a deliberate, disclosed improvement over
    `Presentation`'s equivalent, currently-undetectable precondition for
    this specific case
    ([ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)).
    **This check answers only "is a borrow wrapper currently alive" — it
    is a separate condition from, and not a substitute for, the
    GPU-completion precondition below.**
  - Supplying `finalColorState`/`incomingState`/`finalState` a value for
    which the Vulkan Backend's `planTransition()` table has no
    corresponding entry — not a compile-time restriction (`ResourceState`
    is an unconstrained enum), not a `Result`-typed error; the same,
    unchanged assertion mechanism `planTransition()` already applies to
    every other unlisted `(before, after)` pair
    ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
    Proposed Amendment,
    [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)).
- **Not guaranteed-detectable — documented, caller-enforced lifetime
  preconditions, matching this codebase's existing handle-misuse
  tiering:**
  - Supplying an `incomingState` override that does not match a
    resource's true prior state (e.g. from an earlier `execute()` call
    within the same `CommandList`) — see
    [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).
  - **Destroying `OffscreenTarget` (or its `Device`) while GPU work that
    referenced its backing resources (the color `VkImage`, its memory,
    its view) is still in flight — independently of whether the borrow
    wrapper that referenced them has already been reset.** At the same
    tier as the identical, existing `Presentation`/`Device` rule
    ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)),
    deliberately not made guaranteed-detectable — Phase 1's `Device`
    exposes only the single, coarse `Device::waitIdle()` completion
    signal, with no finer-grained per-resource tracking to check
    against. A caller satisfies this by calling `Device::waitIdle()`
    after the last `Device::submit()` call that referenced this
    `OffscreenTarget`, before destroying it. Full contract, and why
    relying on `Device`'s own destructor-time drain to protect an
    already-destroyed `OffscreenTarget` is the wrong order, in
    [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).
  - Other `OffscreenTarget`/readback-`Buffer` misuse outside its valid
    lifetime window is a lifetime precondition violation, the same tier
    as every other borrowed/owned-handle misuse case already established
    in this codebase.
- **A vended `RenderTarget` borrow ends via ordinary RAII** — destroying
  or resetting the `std::unique_ptr<RenderTarget>` — never an explicit
  method call. Its minimum required lifetime (only through
  `Device::submit()`'s return) is a claim about the wrapper only; it does
  not relax, and is independent of, `OffscreenTarget`'s own destruction
  precondition above.
- Every `VkResult` along `OffscreenTarget` construction, copy recording,
  submission, and readback is checked; no `VkResult` is discarded.
- Vulkan Validation Layers are enabled unconditionally in Debug builds and
  any GPU-touching CI job; a validation warning or error is a build/test
  failure.

## Architectural Impact

This spec introduced architecture across four distinct, independently-
reviewable decisions — three new ADRs and one Proposed Amendment to an
already-`Accepted` ADR — none decided by this spec's prose alone. All
four reached acceptance alongside this spec's own Human Review Approval
(above):

1. **Headless offscreen `RenderTarget` construction and ownership** —
   `OffscreenTarget`'s concrete shape, its owning/non-owning Vulkan
   Backend implementation split, its two-outcome acquire contract, its
   allocation policy (extending, not reopening,
   [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)),
   and the explicitly-separated borrow-wrapper vs. backing-resource
   lifetime contract. Filed as
   [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
   (`Accepted`).
2. **RenderGraph execution — caller-specified incoming and final resource
   states** — resolves a real, code-verified correctness gap between
   [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)'s
   origin-opacity requirement and
   [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
   hardcoded `PresentSource`/`Undefined` assumptions, enumerates every
   mechanically-updated call site (including `Renderer`'s own), and
   names the one new Vulkan Backend transition-table entry required.
   Filed as
   [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
   (`Accepted`).
3. **`Renderer::drawFrame()`'s new `finalColorState` parameter** — a
   narrow, reviewed change to `Renderer`'s already-`Accepted` public
   API, needed because `Renderer`'s own internal `execute()` call is
   subject to the same hardcoded-`PresentSource` defect as any other
   caller — verified against the shipped implementation, not assumed.
   Filed as an **Accepted Amendment** to
   [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
   *not* a new ADR number — the original Decision, Consequences, and
   Alternatives Considered in ADR-0022 remain unchanged and unsuperseded;
   a new, separately-dated section is appended and clearly marked as its
   own acceptance record.
4. **GPU-to-CPU readback RHI capability** — `TransferSource`, the
   readback `Buffer` purpose, `CommandList::copyRenderTargetToBuffer()`,
   and the synchronous readback synchronization/error model, with an
   explicit, stated dependency on decisions 2 and 3 above for its own
   "no intermediate transition needed" claim. Filed as
   [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)
   (`Accepted`).

No existing `Accepted` ADR's Decision text was silently rewritten by this
spec or by the four decisions above — ADR-0022's amendment is appended as
its own clearly-marked, separately-dated section, exactly the pattern
[ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)'s own
amendment established. Architectural Impact was not "None" —
`OffscreenTarget`, the readback capability, RenderGraph's generalized
binding, and `Renderer`'s new parameter are each new or changed public
API surface, exactly what [AGENTS.md](../AGENTS.md)'s "What counts as
significant" section requires the full Spec → Plan → Human Review path
for. **This spec's approval is not itself an authorization to
implement** — see the Human Review Approval note above: a Plan may now
be drafted per [AGENTS.md](../AGENTS.md), but only once this approval PR
has merged into `main`, and that future Plan must still pass its own
Human Review before any code, test, or build-configuration file for this
spec's scope is written.

## Alternatives Considered

- **Design and implement Image Regression Testing's golden-image
  comparison in this same spec, rather than deferring it to a separate,
  later spec.** Rejected: [AGENTS.md](../AGENTS.md)'s own sequencing
  treats headless rendering and image regression testing as two
  Candidate Backlog entries with a real dependency edge between them, and
  this spec's own four architectural decisions are already independently
  substantial — bundling golden-image methodology in as well repeats
  exactly the over-scoping mistake Spec 0005's and Spec 0006's own
  Alternatives Considered already rejected once each, for the same
  reason.
- **Resolve the `PresentSource`-for-headless defect entirely on
  RenderGraph's side, leaving `Renderer::drawFrame()`'s signature
  unchanged and having the headless caller's own second `execute()` call
  treat `PresentSource` as the real incoming state.** Considered and
  rejected — see
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment, Alternatives Considered: this does not eliminate
  the defect, only relocates it behind a confusing, never-actually-
  presented intermediate layout, and requires the same amount of new
  Vulkan Backend transition-table work regardless.
- **Fork RenderGraph's execution entry point for headless, rather than
  generalizing the existing binding.** Rejected — see
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  Alternatives Considered: this is exactly the kind of fork
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md) exists
  to prevent.
- **Thread a shared execution-state object through both `Renderer`'s
  internal `execute()` call and the caller's own second one, avoiding any
  caller-supplied incoming-state override.** Rejected — see
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment, Alternatives Considered: requires a larger
  `Renderer` signature change than `finalColorState` alone, for no
  additional benefit.
- **Decide a general GPU memory allocator (VMA or hand-rolled) now.**
  Rejected — see
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  Alternatives Considered: no concrete pooling/suballocation need exists
  at this spec's resource count.
- **Silently amend [specs/README.md](README.md)'s Candidate Spec
  Backlog ordering (Android Platform vs. this spec) or
  [docs/project-blueprint.md](../docs/project-blueprint.md)'s milestone
  numbering as part of this spec's own PR.** Rejected: per AGENTS.md,
  governance/roadmap documents change only through their own review or
  explicit, minimal, status-driven registry maintenance; this spec's PR
  makes only the single, status-driven registry edit that rule requires
  and records the human-directed reprioritization suggestion as an
  explicit, clearly-labeled, not-yet-approved suggestion.

## Testing & Verification Plan

- **Unit tests:** GPU-independent bookkeeping and validation logic,
  exercised against a fake/mock `CommandList` where a real device is not
  required, per
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  layer 1. At minimum, tests must cover:
  - `execute()`'s generalized binding correctly seeds a bound resource's
    tracked state from a caller-supplied `incomingState` when one is
    provided, and continues to default to `Undefined` when one is not —
    confirming zero behavior change for every binding that omits the new
    field.
  - `execute()` inserts a trailing transition to a caller-supplied
    `finalState` when one is provided and differs from the resource's
    ending state, inserts none when `std::nullopt` is supplied, and
    (existing, unchanged behavior) inserts none for a resource never
    used by any pass.
  - `execute()` inserts **no** transition at all when a resource's
    `incomingState` already equals the state its one usage declares —
    the specific case this spec's own readback copy pass relies on.
  - A pass declaring a single `writes()` usage tagged `TransferSource`
    does **not** trigger `execute()`'s draw-pass recognition.
  - Guard 1 and Guard 2 continue to hold exactly as before, exercised
    against bindings that do and do not specify the new fields.
  - `Renderer::drawFrame()`'s internal `ResourceBinding` construction
    (`renderer.cpp`) correctly threads its new `finalColorState`
    parameter through as `finalState`, for both a `PresentSource`- and a
    `TransferSource`-valued argument, without inspecting or branching on
    the value.
  - `Buffer` construction-parameter validation for the new readback
    purpose, where such logic exists independent of the Vulkan Backend's
    own device-dependent creation path.
  - `resource_state_mapping.cpp`'s `planTransition()` is itself
    GPU-independent (it takes/returns plain enum-derived values, no
    `VkDevice`) and must be unit-tested for: the one new entry this spec
    adds (`ColorAttachmentOutput → TransferSource`) produces a barrier
    plan without asserting; a `(before, after)` pair this spec does not
    add (e.g. `ColorAttachmentOutput → DepthAttachmentReadWrite`)
    continues to fire the existing assertion, confirming this spec adds
    exactly one entry and no more.
- **GPU integration tests (Windows/Vulkan):** real `OffscreenTarget`/
  readback-`Buffer`/`CommandList` construction, execution, and
  destruction, Validation-Layers-enabled, mirroring the existing
  `atlantis_vulkan_backend_gpu_tests`/`atlantis_render_graph_tests`
  pattern. Must cover, at minimum: creating and destroying an
  `OffscreenTarget`; acquiring, using, and returning (via RAII) its
  vended `RenderTarget` more than once across the same instance's
  lifetime, confirming a second `acquireTarget()` succeeds after the
  first borrow is destroyed/reset; that a second `acquireTarget()`
  called *before* returning the first fires the expected assertion
  (exercised under a non-terminating test handler, per this codebase's
  existing assertion-testing pattern); that dropping a borrow
  immediately after `Device::submit()` returns (before `waitIdle()`) is
  followed by a correct subsequent cycle with no Validation Layer
  warning/error, confirming the borrow's minimum-lifetime contract
  ([ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md))
  holds in practice, not only in documentation; creating and destroying
  a readback `Buffer`; one full render-and-readback cycle
  (`Renderer::drawFrame()` with `finalColorState = TransferSource`,
  followed by the copy pass, submission, and `waitIdle()`-gated read)
  with Validation Layers reporting zero warnings/errors, **confirming no
  `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` transition is ever recorded for the
  headless target**; a full cycle that calls `Device::waitIdle()`
  immediately after `submit()` (before dropping the borrow, before
  destroying `OffscreenTarget`), confirming the documented,
  correct-order flow (`waitIdle()` → destroy `OffscreenTarget` → destroy
  `Device`) is Validation-Layers-clean, with no reliance on `Device`'s
  own destructor-time drain to protect `OffscreenTarget`'s already-
  destroyed resources; and the existing windowed GPU integration tests,
  re-run unmodified in behavior after the three mechanical call-site
  updates, confirming zero regression.
- **Headless integration tests:** this spec **is** what makes this test
  layer possible for the first time
  ([testing-strategy.md](../docs/process/testing-strategy.md) layer 2);
  its own GPU integration tests above are the first instance of it,
  though this spec does not yet formalize a distinct CI job category for
  it (see Risks & Open Questions).
- **Image regression tests:** not applicable — this spec's own basic
  content check (below) is explicitly not a golden-image comparison.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of `OffscreenTarget` construction, draw,
  copy, submission, and readback.
- **Manual/automated verification composition:** a minimal, non-shipping
  composition (mirroring `examples/minimal_renderer_demo`'s own structure
  and disclaimer, but creating **no window and no Atlantis Platform
  instance of any kind**) constructs a `Device`, an `OffscreenTarget`,
  this spec's fixed `Mesh`/`Material`/camera uniform `Buffer`/depth
  `Texture`/readback `Buffer`, and runs the full acquire → draw → copy →
  submit → `waitIdle()` → read cycle. It confirms:
  - No window, `HWND`, message pump, or `Presentation`/`VkSwapchainKHR`
    object is created anywhere in this composition.
  - `Renderer::drawFrame()` is called with
    `finalColorState = ResourceState::TransferSource`, and by inspection
    of the recorded command sequence, no transition to
    `ResourceState::PresentSource` occurs at any point in this
    composition's execution.
  - The readback `Buffer`'s contents, after the cycle completes, pass a
    **reproducible basic content check** — not a golden-image comparison
    — sufficient to demonstrate the mesh actually drew (e.g. the buffer
    is not uniformly one color/all-zero, and a small, fixed set of known
    sample positions differ from each other in the direction the fixed
    mesh/camera/material fixture predicts). The exact check's concrete
    shape is left to the Plan; this spec fixes only that it must be
    automated and reproducible, not a human eyeballing a screenshot.
  - The same cycle repeated more than once against the same
    `OffscreenTarget` instance produces consistent results each time.
  - The composition exits cleanly with no outstanding acquired
    `RenderTarget`, no leaked `CommandList`/`Buffer`/`Texture`/
    `OffscreenTarget`, and no Validation Layer warning or error at any
    point.
  - `Device::waitIdle()` is called, and returns, before `OffscreenTarget`
    is destroyed on every exit path this composition exercises, including
    an early/error exit — verifiable by inspection that no code path
    destroys `OffscreenTarget` without an intervening `waitIdle()` call
    for the submission(s) that referenced it, satisfying
    [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)'s
    documented (not guaranteed-detectable) destruction precondition.
  - Separately, `examples/frame_execution_demo` and
    `examples/minimal_renderer_demo`, after their mechanical updates
    (see Requirements), are re-run interactively and confirmed to
    behave identically to their Spec 0006/0007-verified behavior.

## Risks & Open Questions

- **Exact concrete C++ shape of `execute()`'s extended binding entry**
  and `Renderer::drawFrame()`'s new parameter's exact position/name are
  left to the Plan — this spec and its ADRs fix the conceptual contracts,
  not their exact C++ representation.
- **Exact names** for `OffscreenTarget`'s acquire-equivalent method,
  the owning/non-owning Vulkan Backend implementation classes, and the
  readback `BufferPurpose` enumerator are left to the Plan.
- **Whether a distinct CI/test-category label for headless GPU
  integration tests is needed**, separate from the existing
  `gpu`-labeled pattern — the same open question Spec 0006 and Spec 0007
  already flagged for windowed GPU tests, now recurring here; flagged,
  not resolved.
- **Whether the readback `Buffer`'s tightly-packed byte layout needs any
  row-pitch/alignment padding handling** for the Vulkan Backend's own
  `vkCmdCopyImageToBuffer` parameters — a private Vulkan Backend
  implementation concern, left entirely to the Plan.
- **Whether a future Image Regression Testing spec will need
  `OffscreenTarget` to support more than one simultaneously-live
  instance, or pooling/reuse across many test cases**, is explicitly left
  open — see Non-Goals.
- **Whether a future spec revisiting the single-frame-in-flight baseline
  will also need to revisit this spec's fully-synchronous `waitIdle()`-
  based readback model** is left open.
- **Whether depth-buffer readback will be needed by a future spec** is
  left open; this spec's own Non-Goals explicitly exclude it.
- **Whether a future spec chaining a third or fourth `execute()` call
  within one frame should replace the caller-supplied `incomingState`
  override with automated cross-call state tracking** — considered and
  rejected for this spec's own narrow, two-call scope (see
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  Negative/Trade-offs), but explicitly flagged as a design point a future
  spec with more chained calls should revisit, not silently assumed to
  scale indefinitely.

## Out of Scope / Future Work

Image Regression Testing (Candidate 3 in
[specs/README.md](README.md)'s backlog, depending on this spec) is the
direct, named next consumer of this spec's rendering-and-readback
foundation — golden-image storage, comparison/tolerance methodology, and
CI gating are entirely its own future scope. Android Platform and Vulkan
presentation (a separate Candidate Backlog entry) is unaffected by and
does not depend on this spec in either direction; see this spec's own
accompanying registry update and PR description for the human-directed,
not-yet-approved suggestion to sequence it after both this spec and
Image Regression Testing. A future spec wanting `OffscreenTarget`
pooling, multiple simultaneous instances, depth-buffer readback,
asynchronous/non-blocking readback, automated cross-`execute()`-call
state tracking, or a distinct headless-GPU-test CI category is expected
to extend — not merely reuse unchanged — the ADRs this spec introduces;
none of that shape is predicted or pre-designed here.
