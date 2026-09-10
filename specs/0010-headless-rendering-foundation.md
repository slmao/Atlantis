# Spec: Headless Rendering Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Created:** 2026-08-15
- **Human Review Approval (2026-08-16):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), following **three independent,
  read-only Human Review rounds** conducted against this spec's drafted
  text and the real shipped implementation (`src/renderer/`,
  `src/render_graph/`, `src/rhi/`, `src/vulkan_backend/`) — see
  [PR #42](https://github.com/slmao/Atlantis/pull/42) and
  [PR #43](https://github.com/slmao/Atlantis/pull/43) for the full
  findings/revisions record. The rounds established, against the shipped
  code: (a) `Renderer::drawFrame()` calls the same shared
  `render_graph::execute()` every other caller does, and that function
  (prior to this spec) unconditionally transitioned every bound, touched
  `RenderTarget` to `ResourceState::PresentSource` — an earlier draft's
  claim that `Renderer::drawFrame()` needs no change and leaves color in
  `ColorAttachmentOutput` was verified **false**; (b) ADR-0038's vended
  borrow is *not* consumed by any readback call —
  `copyRenderTargetToBuffer(RenderTarget&, Buffer&)` only borrows a
  reference to record a command; (c) the borrow wrapper's lifetime and
  `OffscreenTarget`'s own backing-resource lifetime are two distinct
  preconditions and must be stated separately (a caller may legally drop
  the borrow right after `Device::submit()` returns yet must **not**
  destroy `OffscreenTarget`/`Device` while GPU work referencing its
  resources is still in flight).

  **Eight design decisions were confirmed explicitly and are accepted
  as-is:**

  1. `Renderer::drawFrame()` gains a required, backend-agnostic
     `finalColorState` parameter — a windowed caller passes
     `ResourceState::PresentSource`, a headless caller passes
     `ResourceState::TransferSource` directly (no intermediate
     presentation-shaped layout ever recorded); `Renderer` never
     observes, validates, or branches on the value, preserving its
     origin-opacity contract completely. See
     [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
     `Accepted Amendment`.
  2. `OffscreenTarget` is introduced as a second `RenderTarget` source,
     with no `Presentation` involved; the Vulkan Backend implements it as
     an owning, private type vending non-owning, per-cycle borrows,
     directly mirroring the existing `Presentation`/`VulkanRenderTarget`
     pattern. See
     [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md).
  3. A vended borrow ends via ordinary RAII (destroying/resetting the
     `std::unique_ptr<RenderTarget>`) — no new public
     `release()`/`consume()` API.
  4. **Two explicitly separate lifetimes govern `OffscreenTarget`.** The
     borrow wrapper's minimum required lifetime extends only through the
     `Device::submit()` call that references it. `OffscreenTarget`'s own
     backing resources (the color `VkImage`/memory/view) must remain
     alive until the GPU work that referenced them has actually completed
     — established by the caller via `Device::waitIdle()` before
     destroying `OffscreenTarget`, a documented, caller-enforced
     precondition at the same tier as the existing `Presentation`/`Device`
     rule
     ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)),
     deliberately **not** guaranteed-detectable.
  5. Repeated acquisition without returning a prior borrow, destroying
     `OffscreenTarget`/`Device` while a borrow is still outstanding, and
     supplying a `finalColorState`/`incomingState`/`finalState` value
     unsupported by the Vulkan Backend's transition table are each
     guaranteed-detectable programmer errors via `ATLANTIS_CHECK`/
     `ATLANTIS_ASSERT` — never a compile-time restriction, never a
     `Result`-typed error.
  6. RenderGraph's `execute()` binding gains caller-declared
     `incomingState`/`finalState` `ResourceState` fields;
     `ColorAttachmentOutput → TransferSource` is the one and only new
     Vulkan Backend transition-table entry this spec's design requires.
     See
     [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md).
  7. `ResourceState::TransferSource`, a fourth `BufferPurpose` (readback),
     and `CommandList::copyRenderTargetToBuffer()` are introduced as the
     minimal, narrow GPU-to-CPU readback capability. See
     [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md).
  8. No general GPU memory allocator, asynchronous/non-blocking readback,
     multiple frames in flight, Linux support, Android support, or
     image-regression comparison/tolerance methodology/CI gating is
     introduced — all remain explicitly out of scope per Non-Goals.

  Following this approval, ADR-0038, ADR-0039, and ADR-0040 each move to
  `Accepted`, and ADR-0022's 2026-08-15 Proposed Amendment moves to
  `Accepted Amendment`; this spec moves to `Approved`. **This approval is
  not itself an authorization to implement** — a Plan may be drafted per
  [AGENTS.md](../AGENTS.md), and must still pass its own (or a joint
  Spec+Plan) Human Review. The following remain **open Plan-stage
  questions, not blockers** (full list in Risks & Open Questions): exact
  C++ names and parameter positions for `OffscreenTarget`'s acquire
  method, `Renderer::drawFrame()`'s `finalColorState` parameter, and the
  Vulkan Backend's owning/non-owning implementation classes; the exact
  `ResourceBinding` extension's C++ representation; the exact reproducible
  basic-content-check shape; whether a distinct CI/test-category label for
  headless GPU integration tests is needed; and row-pitch/alignment
  handling inside the Vulkan Backend's copy implementation.
- **Related Plan(s):**
  [plans/0010-headless-rendering-foundation.md](../plans/0010-headless-rendering-foundation.md)
  (`Approved`, Human Review 2026-08-16 following two independent Plan
  Review rounds —
  [PR #45](https://github.com/slmao/Atlantis/pull/45),
  [PR #46](https://github.com/slmao/Atlantis/pull/46)). With this spec and
  that plan both `Approved`, the joint Human Review gate is satisfied;
  Implementation may begin strictly against the approved plan.
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0009](../adr/0009-assertion.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  (all `Accepted`). Four new decisions filed alongside this spec, all
  `Accepted` with it:
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  (offscreen `RenderTarget` construction and ownership),
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  (RenderGraph execution — caller-specified incoming/final resource
  states),
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md) (GPU-to-CPU
  readback capability), and a **Proposed Amendment (2026-08-15)** to the
  already-`Accepted`
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)
  (`Renderer::drawFrame()` gains an explicit `finalColorState` parameter),
  now `Accepted Amendment`.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #150](https://github.com/slmao/Atlantis/pull/150) Batch 3. Original
  scope, obligations, and the corrected-design evidence trail retained;
  the round-by-round Human Review narration is preserved in PR #42/#43
  history.

## Summary

This spec introduces headless rendering: driving the exact same
`Renderer` → RenderGraph → RHI → Vulkan Backend stack Spec 0007 already
shipped, without any window, `Presentation`, or `VkSwapchainKHR`, and
reading the rendered pixels back to the CPU. It extends RHI with an
offscreen `RenderTarget`-vending type (`OffscreenTarget`, no
`Presentation`), a GPU-to-CPU readback capability (a new `ResourceState`,
a new `Buffer` purpose, a new `CommandList` copy operation), and
generalizes RenderGraph's `execute()` to support both a non-presentable
bound `RenderTarget` and a second, chained `execute()` call sharing one
`CommandList` with `Renderer::drawFrame()`'s own internal one.
**`Renderer::drawFrame()` gains one new, required, backend-agnostic
parameter** (`ResourceState finalColorState`) so it can tell its
caller-agnostic internal graph what state to leave the color target in —
without ever learning why. It does **not** design golden-image
comparison, tolerance methodology, or CI gating — it is the
rendering-and-readback foundation a future Image Regression Testing spec
depends on, not that spec itself.

## Motivation / Problem Statement

[AGENTS.md](../AGENTS.md)'s Phase 1 constraints: "Headless rendering and
image regression testing follow once the windowed/swapchain path works —
they are still Phase 1 scope... don't skip headless once windowed is
working." Windowed rendering is now genuinely working: Spec 0007 (Minimal
Renderer) is `Approved` and implemented. [specs/README.md](README.md)'s
Candidate Spec Backlog lists Headless Rendering (Candidate 2) as depending
only on Spec 0007 — satisfied.

Three genuine architectural gaps, none resolved by any existing `Accepted`
ADR:

- **RHI has no way to produce a `RenderTarget` without `Presentation`.**
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md) named
  this as the intended future shape ("an explicitly-requested offscreen
  target → `RenderTarget` backed by an offscreen image, no `Presentation`
  involved") but fixed no concrete construction API, ownership type, or
  lifecycle.
- **RHI has no GPU-to-CPU data path of any kind.** No `ResourceState`
  supports being copied out of, no `Buffer` purpose supports being a copy
  destination, and `CommandList` has no copy operation.
- **RenderGraph's `execute()` hardcodes two assumptions** true only
  because, until now, exactly one origin and exactly one call per frame
  ever existed — including the target `Renderer` itself draws into.
  Verified against `src/render_graph/src/execution.cpp` and
  `src/renderer/src/renderer.cpp`: `Renderer::drawFrame()` calls the same
  shared `render_graph::execute()` every other caller does, and that
  function unconditionally transitions every bound, touched `RenderTarget`
  to `ResourceState::PresentSource` before returning, and starts every
  `execute()` call's own state tracking from `ResourceState::Undefined`
  regardless of a prior `execute()` call on the same physical resource
  within the same `CommandList`. Both assumptions are wrong for a headless
  target read back via a second, chained `execute()` call.

A fourth question, procedural: whether this spec's implementation pressure
forces an early, general GPU-memory-allocator decision. It does not — see
Requirements' "GPU memory allocation" bullet and
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
  long-lived `OffscreenTarget` and the short-lived `RenderTarget` borrows
  it vends (ADR-0038).
- Extend RHI with a minimal, narrow GPU-to-CPU readback capability: one
  new `ResourceState` (`TransferSource`), one new `Buffer` purpose
  (readback), one new `CommandList` copy operation (ADR-0040).
- Generalize RenderGraph's `execute()` binding to accept a
  caller-specified incoming and final `ResourceState` per bound resource,
  fixing the hardcoded-`Undefined`/hardcoded-`PresentSource` assumptions —
  precisely enumerating every existing call site this requires updating,
  including `Renderer`'s own internal one (ADR-0039).
- **Give `Renderer::drawFrame()` an explicit, backend-agnostic, required
  `finalColorState` parameter**, as a narrow, reviewed Proposed Amendment
  to the already-`Accepted` ADR-0022 — the minimal change letting a
  headless caller request `ResourceState::TransferSource` directly, with
  no intermediate, never-observed `PresentSource` transition, while
  `Renderer` remains completely unaware of *why* a value was chosen.
- Demonstrate, end-to-end, that `Renderer::drawFrame()` — its dependency
  boundary and per-frame responsibilities unchanged, its public signature
  extended by exactly one caller-supplied parameter — draws the same
  mesh/material/camera Spec 0007 already verifies, into an offscreen
  `RenderTarget`, with no window, `Presentation`, or swapchain anywhere.
- Verify a full render → readback cycle produces a reproducible basic
  content check (not a golden-image comparison — see Non-Goals) on Windows
  with a real GPU, with Vulkan Validation Layers clean throughout.
- Resolve the architectural decisions this spec identifies via dedicated
  ADRs/amendment, so a future Image Regression Testing spec inherits a
  settled contract.
- Confirm explicitly that no general GPU memory allocator decision is
  forced — extending ADR-0023's existing direct-allocation policy is
  sufficient (ADR-0038).

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Golden-image storage, comparison, tolerance/diff methodology, or CI
  image-regression gating of any kind.** This spec proves rendering and
  readback work and produce a reproducible basic content result; it does
  not design the comparison harness a future Image Regression Testing spec
  (Candidate 3) will build on top of this spec's output.
- **Android Platform, iOS Platform, or a second graphics backend of any
  kind.** Windows/Vulkan only (AGENTS.md). This spec's headless path uses
  no Atlantis Platform code at all.
- **Linux as a target platform, or any Linux-specific
  build/CI/runtime content** (AGENTS.md).
- **A general, sampled/shader-readable `Texture` usage, or a general
  `Sampler` type.** ADR-0040's readback capability is scoped strictly to a
  copy-out-to-`Buffer` operation.
- **Widening the existing `Texture` type to support a color or
  transfer-source usage.** ADR-0038 explicitly rules this out — `Texture`
  remains exactly as depth-only as ADR-0023 fixed it; the offscreen color
  resource is a second, distinct RHI concept.
- **Depth-buffer readback.** Only the color `RenderTarget` gains a
  readback path this round; the depth `Texture`'s existing, unchanged
  ownership/lifecycle is reused verbatim.
- **Asynchronous, non-blocking, or multi-frame-latency-amortized
  readback.** Readback this round is synchronous and blocking
  (`Device::waitIdle()`-based).
- **Multiple frames in flight, multi-threaded command
  recording/resource creation/graph execution, or any job/task system.**
- **A general GPU memory suballocator (VMA or hand-rolled), or any change
  to
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s general
  deferral.**
- **Any change to `Renderer`'s dependency boundary, ownership model, or
  per-frame responsibilities beyond the one new `finalColorState`
  parameter.** `Renderer` still depends only on
  `Atlantis::RHI`/`RenderGraph`/`Core`; still never owns a `RenderTarget`,
  depth `Texture`, `Mesh`, or `Material`; still builds exactly one
  internal draw pass with unchanged
  `ColorAttachmentOutput`/`DepthAttachmentReadWrite` usages; still never
  learns whether it was called by a windowed or headless composition.
  This spec does **not** claim `Renderer`'s public API is unaffected — see
  Requirements and ADR-0022's Proposed Amendment for the one narrow change
  it does make.
- **A second rendering path, a second `RenderGraph`/RHI implementation, or
  any fork of `Renderer`.**
- **Multiple simultaneous `OffscreenTarget` instances, or any
  pooling/reuse-across-instances policy for them.**
- **Any regression to the windowed path.** `examples/frame_execution_demo`
  and `examples/minimal_renderer_demo`, once mechanically updated, must
  continue to build and behave identically to their Spec
  0006/0007-verified behavior.
- **Editing [specs/README.md](README.md)'s Section A entries for prior
  specs, [docs/project-blueprint.md](../docs/project-blueprint.md), or any
  other governance/roadmap document beyond this spec's own required
  backlog-registry update.**

## Requirements

### Functional

**Offscreen `RenderTarget` construction and ownership**

- A new RHI type, `OffscreenTarget`, constructed via `Device` with a fixed
  extent and color `atlantis::rhi::Format` (caller-specified, never
  changed for that instance's lifetime — no resize/recreation concept),
  owning exactly one color image (and its backing memory) for its whole
  lifetime. Full contract in ADR-0038.
- `OffscreenTarget` vends a `RenderTarget` via a two-outcome
  (`Err`/`Ok`, matching `Presentation`'s real
  `Result<std::unique_ptr<RenderTarget>, Err>` shape) acquire-equivalent
  call — no zero-extent case, unlike `Presentation`. `Err` is reserved for
  genuine, environmental failures; calling this method while a
  previously-vended borrow is still outstanding is a separate,
  guaranteed-detectable programmer error (assertion), not part of this
  `Result::Err` channel.
- The vended `RenderTarget` is the exact same abstract public type
  ADR-0019 already defines — no new field or method on its public
  interface. A **second concrete Vulkan Backend implementation** of it is
  introduced (non-owning, borrowing from the owning `OffscreenTarget`
  implementation, mirroring exactly how `VulkanRenderTarget` borrows from
  `VulkanPresentation`) — see ADR-0038 for the full ownership/lifetime
  split.
- **Two distinct, non-equivalent lifetimes govern this type — the borrow
  wrapper's, and `OffscreenTarget`'s own (backing-resource) lifetime.
  Conflating them is a real correctness risk, not a documentation
  nicety** — see ADR-0038's "Lifetime contract" section.
- **Borrow wrapper.** `OffscreenTarget` has no `present()` counterpart and
  no other explicit borrow-ending method — a borrow ends via ordinary
  RAII (destroying/resetting the vended `std::unique_ptr<RenderTarget>`),
  not by any call ADR-0040 defines. The borrow's **minimum required
  lifetime** extends only through the return of the `Device::submit()`
  call whose recorded commands reference it — it does **not** need to
  survive through `Device::waitIdle()`, reading the readback `Buffer`, or
  the completion of the GPU work, because Vulkan bakes handles, not C++
  object references, into a recorded `CommandList`. This claim is about
  the wrapper only; it says nothing about, and does not relax, when
  `OffscreenTarget` itself may be destroyed.
- The same `OffscreenTarget` instance may be acquired-and-borrowed more
  than once across its lifetime, each cycle independent. Acquiring a
  second `RenderTarget` while a previously-vended borrow is still
  outstanding, or destroying `OffscreenTarget` while a borrow it vended is
  still outstanding, are each a guaranteed-detectable programmer error — a
  deliberate, disclosed improvement over `Presentation`'s equivalent,
  currently-undetectable precondition for that case, made possible because
  the same outstanding-borrow tracking already exists for the
  double-acquire check. This check answers only "is a borrow wrapper
  currently alive" — it is not, and cannot be, a substitute for the
  separate GPU-completion precondition below.
- **`OffscreenTarget` and backing resources (the color `VkImage`, its
  memory, its view).** `OffscreenTarget` — and the `Device` it was
  constructed from — must not be destroyed while GPU work that referenced
  its backing resources is still in flight, **independently of whether the
  borrow wrapper that referenced them has already been destroyed.** This
  is a lifetime precondition violation at the same tier as the identical,
  existing `Presentation`/`Device` rule (ADR-0019) — **deliberately not
  guaranteed-detectable**: Phase 1's `Device` exposes only a single,
  coarse `Device::waitIdle()` completion signal, with no finer-grained
  per-resource tracking, and this spec does not introduce one. A caller
  satisfies this precondition by calling `Device::waitIdle()` after the
  last `Device::submit()` call that referenced this `OffscreenTarget`,
  before destroying it. **Destroying `OffscreenTarget`'s backing resources
  first and relying on `Device`'s own destructor to wait afterward is the
  wrong order and is not safe** — `Device`'s destructor only drains work
  related to its own teardown. Correct order: `Device::waitIdle()` →
  destroy `OffscreenTarget` → (whenever done) destroy `Device`.
  Re-acquiring against a still-alive `OffscreenTarget` needs no
  intervening `waitIdle()` (a separate claim, backed by `Device::submit()`'s
  own fence-wait) — full contract in ADR-0038.
- The depth `Texture` used alongside an `OffscreenTarget` is constructed,
  owned, and destroyed via the existing, unchanged `Device::createTexture()`
  path — `OffscreenTarget` itself never owns, creates, or references a
  depth resource, and `Texture`/`DepthFormat` are entirely untouched.
- **GPU memory allocation:** every allocation `OffscreenTarget`'s color
  image requires uses ADR-0023's existing direct, unpooled,
  Vulkan-Backend-private policy.

**`Renderer::drawFrame()`'s new `finalColorState` parameter**

- `Renderer::drawFrame()` gains one new, required parameter:
  `atlantis::rhi::ResourceState finalColorState` — a Proposed Amendment to
  ADR-0022, not a silent reinterpretation. Exact parameter position is a
  Plan-stage detail.
- `Renderer` passes this value through, unmodified and uninspected, as the
  `finalState` field of its own internal color `ResourceBinding` entry
  (ADR-0039). `Renderer`'s draw pass itself is unchanged — it still
  declares exactly one `writes()` usage tagged `ColorAttachmentOutput`.
- A windowed caller passes `ResourceState::PresentSource` — the exact
  value `execute()`'s old hardcoded behavior already produced, a
  zero-behavior-change update once the one existing call site
  (`minimal_renderer_demo`) supplies it explicitly.
- A headless caller passes `ResourceState::TransferSource` directly — no
  intermediate `PresentSource` transition is ever recorded for a target
  that will never be presented.
- `Renderer` does not interpret, validate, or branch on this value, and
  gains no knowledge of `Presentation`, `VkSwapchainKHR`, `OffscreenTarget`,
  or any other origin-specific concept — see ADR-0022's Proposed
  Amendment.

**GPU-to-CPU readback capability**

- `ResourceState` gains exactly one new variant: `TransferSource`. Full
  contract in ADR-0040.
- `Buffer` gains a fourth, fixed purpose: readback — host-visible,
  host-coherent, mapped once for its whole lifetime, created via the
  existing, unchanged `Device::createBuffer()`.
- `CommandList` gains exactly one new recordable operation:
  `copyRenderTargetToBuffer(RenderTarget&, Buffer&)` — named after its
  actual parameter type, not `Texture` (an earlier draft's
  `copyTextureToBuffer` name was corrected during Human Review). Copies
  the full, tightly-packed color image into a readback-purpose `Buffer`;
  no partial-region copy, no format conversion.
- The copy is recorded as a RenderGraph pass declaring **exactly one
  `writes()` usage tagged `ResourceState::TransferSource`** against the
  logical resource the color `RenderTarget` is bound to, with
  `incomingState = ResourceState::TransferSource` (matching what
  `Renderer::drawFrame()` was already told to leave the target in) and
  `finalState = std::nullopt`. Because the resource's tracked incoming
  state already equals this pass's own declared state, `execute()` inserts
  no `transitionResource()` call for this pass at all — its callback
  consists solely of the copy call. `execute()`'s existing draw-pass
  recognition does not fire for this pass.
- Readback is synchronous and blocking: the caller submits, calls the
  existing `Device::waitIdle()`, then reads the readback `Buffer`'s
  already-mapped pointer (`Buffer::mappedData()`) directly.
- Every `VkResult` along copy recording, submission, and wait is checked.

**RenderGraph execution generalization for non-presentable and chained
bindings**

- `execute()`'s existing frame-scoped external resource binding
  (`ResourceBinding`,
  `src/render_graph/include/atlantis/render_graph/execution.h`) gains two
  additional fields, meaningful only for `target`-shaped (`RenderTarget`)
  entries: `incomingState` (`ResourceState`, defaults to `Undefined` —
  **safe only for a resource being bound to its first `execute()` call
  within its current `CommandList`/frame; using the default for a resource
  already touched by an earlier `execute()` call sharing the same
  `CommandList` is a caller precondition violation that silently discards
  the resource's real prior contents, not a guaranteed-detectable
  error**) and `finalState` (`std::optional<ResourceState>`, **no default
  — every `target`-shaped binding entry must supply one explicitly**;
  `std::nullopt` means "no trailing transition beyond whatever the last
  pass leaves it in"). Full contract in ADR-0039.
- **Every existing call site that constructs a `target`-shaped
  `ResourceBinding` must be mechanically updated — three, not two, none
  optional:**
  1. `src/renderer/src/renderer.cpp` — supplies `finalState =
     finalColorState` (the new parameter); `incomingState` stays at its
     default.
  2. `examples/frame_execution_demo/main.cpp` — its own direct,
     non-`Renderer` `execute()` call must supply `finalState =
     ResourceState::PresentSource` explicitly.
  3. `examples/minimal_renderer_demo`'s verification composition — must
     pass `ResourceState::PresentSource` as `Renderer::drawFrame()`'s new
     `finalColorState` argument.
  All three are mechanical, non-behavioral updates for the windowed path —
  each supplies exactly the value the old hardcoded behavior already
  produced.
- **Vulkan Backend impact, named explicitly:**
  `resource_state_mapping.cpp`'s `planTransition()` is a closed,
  exhaustively-enumerated lookup table; any `(before, after)` pair not
  listed triggers an assertion failure, not a computed fallback (verified
  by inspection). This spec's design requires **exactly one new entry:
  `ColorAttachmentOutput → TransferSource`** — used by `Renderer`'s own
  internal trailing transition when a headless caller supplies
  `finalColorState = TransferSource`. No other new entry — the windowed
  path continues to use the already-existing `ColorAttachmentOutput →
  PresentSource` entry, and the readback copy pass never calls
  `transitionResource()` at all.
- `execute()`'s algorithm (walk compiled pass order, track
  most-recently-recorded state per bound resource, insert
  `transitionResource()` on a state change, insert one trailing call if a
  final state is specified and differs from the resource's ending state)
  is otherwise unchanged. Guard 1 and Guard 2 are unchanged; the bound
  depth `Texture`'s binding is unaffected.

**Reuse of Renderer, RenderGraph, RHI, and Vulkan Backend — no fork**

- The headless verification composition constructs `Mesh`/`Material`/the
  camera uniform `Buffer` using the same fixed fixture Spec 0007's
  windowed verification composition already uses (exact code-sharing
  mechanism left to the Plan), and calls `Renderer::drawFrame()` with the
  same borrowed-reference contract as the windowed composition plus the
  new `finalColorState` argument, passing the `OffscreenTarget`-vended
  `RenderTarget` and a depth `Texture` in place of the windowed ones.
- `Renderer`'s dependency set (`Atlantis::RHI`/`RenderGraph`/`Core` only),
  ownership model, and per-frame responsibilities are unaffected beyond
  the one new parameter.

**Phase 1 single-threaded orchestration and thread-safety contracts**

- Every new public type this spec introduces (`OffscreenTarget`, the
  extended `execute()` binding, `CommandList::copyRenderTargetToBuffer()`,
  `Renderer::drawFrame()`'s new parameter) documents its thread-safety
  contract at its public API — "not thread-safe; caller-thread-only," on
  the single Phase 1 logical thread, per
  [ADR-0004](../adr/0004-phase1-threading-baseline.md). No mutex, atomic,
  job/task system, or lock-free structure. Unlike the windowed path, this
  thread is not required to also own a Platform message pump — a headless
  composition has no window and no Platform event loop — but it remains
  exactly one logical thread.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily" outside the deliberate `waitIdle()`-based readback stall,
  an explicit, accepted simplification.
- **Memory:** no general GPU memory suballocation strategy is introduced
  or assumed.
- **Portability (within the Vulkan-only Phase 1 constraint):** implemented
  and verified on Windows only, using no window and no Atlantis Platform
  code at all.
- **Other:** no new third-party dependency. Unit tests use the existing
  Catch2 v3 framework ([ADR-0007](../adr/0007-test-framework.md)).

## Proposed Design

### Module boundaries (realizing, not moving, existing ones)

Realizes exactly the headless path ADR-0002 and
[resource_lifetime.md](../docs/architecture/resource_lifetime.md) already
anticipated: RHI gains a second `RenderTarget`-vending type
(`OffscreenTarget`, alongside `Presentation`) and a narrow readback
extension; RenderGraph's existing RHI dependency is generalized, not
widened to a new dependency direction. `Renderer`'s dependency set is
unchanged; its public signature gains one new parameter per ADR-0022's
Proposed Amendment.

**Headless verification composition, once per render-and-readback cycle
(concrete C++ names left to the Plan):**

1. `Device::createOffscreenTarget(extent, colorFormat)`;
   `Device::createTexture(depth, extent)` (unchanged Spec 0007
   mechanism); `Device::createBuffer(readback purpose, size)` (unchanged
   `Device::createBuffer()`, new purpose value).
2. `OffscreenTarget::acquireTarget()` (exact name left to Plan) →
   `Ok(RenderTarget)` or `Err` (propagate/log).
3. Write this cycle's camera view/projection into the camera uniform
   `Buffer`'s mapped memory (unchanged from Spec 0007).
4. `Device::createCommandList()`.
5. `Renderer::drawFrame(commandList, *renderTarget, *depthTexture,
   cameraBuffer, drawItems, ResourceState::TransferSource)` — internally
   builds its own graph exactly as Spec 0007 does
   (`ColorAttachmentOutput` + `DepthAttachmentReadWrite` draw pass),
   compiles it, and calls `render_graph::execute()` with its color-target
   binding carrying `finalState = TransferSource` (the new argument,
   passed through unmodified). `execute()` inserts the draw pass's own
   `Undefined → ColorAttachmentOutput` transition, then, because
   `finalState` differs from the pass's ending state, one trailing
   `ColorAttachmentOutput → TransferSource` transition (this spec's one
   new `planTransition()` entry). The color `RenderTarget` is now in
   `TransferSource`; no `PresentSource` transition ever occurs.
6. The caller builds a second, small `RenderGraphBuilder` description: one
   pass declaring a single `writes()` usage tagged `TransferSource`
   against the same logical resource the `RenderTarget` is bound to;
   execution callback calls
   `CommandList::copyRenderTargetToBuffer(*renderTarget, *readbackBuffer)`.
   The caller compiles that second graph and calls
   `render_graph::execute(compiledGraph, bindings{resource →
   (RenderTarget, incomingState = TransferSource, finalState =
   std::nullopt)}, commandList)` — the resource's tracked state (seeded
   from `incomingState = TransferSource`) already equals this pass's
   declared state, so `execute()` inserts no `transitionResource()` call
   at all; only the copy pass's callback runs.
7. `Device::submit(commandList, target's-acquire-complete-signal)` — the
   borrow **wrapper's** minimum required lifetime ends here (ADR-0038 Part
   1); this says nothing about `OffscreenTarget` itself (governed by Part
   2).
8. `Device::waitIdle()` — two independent things become true after this
   returns: (a) the readback `Buffer`'s writes are now host-visible, and
   (b) the GPU-completion precondition `OffscreenTarget`'s own destruction
   requires (ADR-0038 Part 2) is now satisfied for every submission up to
   and including this cycle's.
9. Read `readbackBuffer`'s mapped pointer; run this spec's basic,
   reproducible content check.
10. Drop/reset the `RenderTarget` borrow (ordinary RAII — no explicit
    method call; the outstanding-borrow tracking clears via the borrow's
    own destructor). Only now may the next `acquireTarget()` succeed;
    calling it earlier is a guaranteed-detectable programmer error.
    Re-acquiring against the SAME, still-alive `OffscreenTarget` needs no
    further `waitIdle()` here — the next cycle's own `submit()` serializes
    against this one's fence internally (ADR-0038 Part 2).
11. If this cycle is the last: `OffscreenTarget` may now be safely
    destroyed, because the `waitIdle()` above already established GPU
    completion for every submission that referenced it. Destroying its
    backing resources BEFORE that `waitIdle()` and relying on `Device`'s
    own destructor to "catch up" is the wrong order and is not safe. On
    every exit path, including an early/error exit: `Device::waitIdle()`
    before destroying `OffscreenTarget`/`Device`/depth `Texture`/readback
    `Buffer`/`Mesh`/`Material`.

### Deferred decisions (see the linked ADRs for full text)

- **Offscreen `RenderTarget` construction and ownership** —
  [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md):
  `OffscreenTarget`'s shape, the owning/non-owning Vulkan Backend
  implementation split, its two-outcome acquire contract, its allocation
  policy, and why `Texture` is not reused.
- **`Renderer::drawFrame()`'s `finalColorState` parameter** —
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment: the new parameter's exact contract, why it is
  required (not defaulted or boolean), and why it preserves `Renderer`'s
  origin-opacity completely.
- **GPU-to-CPU readback** —
  [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md):
  `TransferSource`, the readback `Buffer` purpose,
  `copyRenderTargetToBuffer()`, the synchronous `waitIdle()`-based model,
  and error semantics.
- **RenderGraph execution generalization** —
  [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md):
  the caller-specified incoming/final `ResourceState` binding fields,
  their exact default/misuse policy, the full list of mechanically-updated
  call sites, and the one new `planTransition()` entry.

### Threading

Single logical thread, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md) — unchanged in kind,
minus the windowed path's additional obligation to also own a Platform
message pump (headless has none).

### Error handling

- Recoverable runtime errors (`OffscreenTarget`/`Buffer`/`Texture`
  creation failure, submission failure) use `atlantis::Result<T, E>`.
  `OffscreenTarget`'s acquire-equivalent call's `Err` is reserved for
  genuine, environmental failures only.
- Programmer errors, all `ATLANTIS_CHECK`/`ATLANTIS_ASSERT` (ADR-0009),
  all guaranteed-detectable: Guard 1/Guard 2 violations (unchanged scope,
  generalized mechanism); acquiring a second `RenderTarget` from an
  `OffscreenTarget` before the first has been returned; destroying
  `OffscreenTarget` (or its `Device`) while a vended borrow is still
  outstanding (this check answers only "is a borrow wrapper currently
  alive" — a separate condition from, and not a substitute for, the
  GPU-completion precondition below); supplying a
  `finalColorState`/`incomingState`/`finalState` value for which
  `planTransition()` has no entry — the same, unchanged assertion
  mechanism `planTransition()` already applies to every other unlisted
  pair.
- **Not guaranteed-detectable — documented, caller-enforced lifetime
  preconditions:** supplying an `incomingState` override that does not
  match a resource's true prior state (ADR-0039); destroying
  `OffscreenTarget` (or its `Device`) while GPU work that referenced its
  backing resources is still in flight — independently of whether the
  borrow wrapper has already been reset — at the same tier as the
  identical existing `Presentation`/`Device` rule (ADR-0019), deliberately
  not made guaranteed-detectable (Phase 1's `Device` exposes only the
  single coarse `Device::waitIdle()` signal). A caller satisfies this by
  `Device::waitIdle()` after the last referencing `Device::submit()`,
  before destroying it. Other `OffscreenTarget`/readback-`Buffer` misuse
  outside its valid lifetime window is a lifetime precondition violation
  at the same tier as every other borrowed/owned-handle misuse case in
  this codebase.
- **A vended `RenderTarget` borrow ends via ordinary RAII** — never an
  explicit method call. Its minimum required lifetime (only through
  `Device::submit()`'s return) is a claim about the wrapper only; it does
  not relax, and is independent of, `OffscreenTarget`'s own destruction
  precondition.
- Every `VkResult` along `OffscreenTarget` construction, copy recording,
  submission, and readback is checked; no `VkResult` is discarded.
- Vulkan Validation Layers are enabled unconditionally in Debug builds and
  any GPU-touching CI job; a validation warning or error is a build/test
  failure.

## Architectural Impact

Architecture across four distinct, independently-reviewable decisions —
three new ADRs and one Proposed Amendment to an already-`Accepted` ADR —
none decided by this spec's prose alone; all four reached acceptance
alongside this spec's Human Review Approval:

1. **Headless offscreen `RenderTarget` construction and ownership** —
   `OffscreenTarget`'s concrete shape, its owning/non-owning Vulkan
   Backend implementation split, its two-outcome acquire contract, its
   allocation policy (extending, not reopening, ADR-0023), and the
   explicitly-separated borrow-wrapper vs. backing-resource lifetime
   contract.
   [ADR-0038](../adr/0038-headless-offscreen-rendertarget-construction-and-ownership.md)
   (`Accepted`).
2. **RenderGraph execution — caller-specified incoming and final resource
   states** — resolves a real, code-verified correctness gap between
   ADR-0002's origin-opacity requirement and
   [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
   hardcoded `PresentSource`/`Undefined` assumptions, enumerates every
   mechanically-updated call site (including `Renderer`'s own), and names
   the one new Vulkan Backend transition-table entry required.
   [ADR-0039](../adr/0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
   (`Accepted`).
3. **`Renderer::drawFrame()`'s new `finalColorState` parameter** — a
   narrow, reviewed change to `Renderer`'s already-`Accepted` public API,
   needed because `Renderer`'s own internal `execute()` call is subject to
   the same hardcoded-`PresentSource` defect as any other caller —
   verified against the shipped implementation, not assumed. Filed as an
   **Accepted Amendment** to
   [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
   *not* a new ADR number — the original Decision, Consequences, and
   Alternatives Considered remain unchanged and unsuperseded; a new,
   separately-dated section is appended and clearly marked as its own
   acceptance record.
4. **GPU-to-CPU readback RHI capability** — `TransferSource`, the readback
   `Buffer` purpose, `CommandList::copyRenderTargetToBuffer()`, and the
   synchronous readback synchronization/error model, with an explicit,
   stated dependency on decisions 2 and 3 for its own "no intermediate
   transition needed" claim.
   [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)
   (`Accepted`).

No existing `Accepted` ADR's Decision text was silently rewritten —
ADR-0022's amendment is appended as its own clearly-marked,
separately-dated section, exactly the pattern
[ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)'s own
amendment established. `OffscreenTarget`, the readback capability,
RenderGraph's generalized binding, and `Renderer`'s new parameter are each
new or changed public API surface — exactly what [AGENTS.md](../AGENTS.md)'s
"What counts as significant" section requires the full Spec → Plan → Human
Review path for. **This spec's approval is not itself an authorization to
implement** — a Plan may be drafted per AGENTS.md only once this approval
PR has merged into `main`, and must still pass its own Human Review before
any code, test, or build-configuration file is written.

## Alternatives Considered

- **Design and implement Image Regression Testing's golden-image
  comparison in this same spec.** Rejected: AGENTS.md's sequencing treats
  headless rendering and image regression testing as two Candidate Backlog
  entries with a real dependency edge; this spec's four architectural
  decisions are already independently substantial — bundling golden-image
  methodology repeats the over-scoping mistake Spec 0005's and Spec 0006's
  Alternatives Considered already rejected.
- **Resolve the `PresentSource`-for-headless defect entirely on
  RenderGraph's side, leaving `Renderer::drawFrame()`'s signature
  unchanged.** Rejected — see ADR-0022's Proposed Amendment: this does not
  eliminate the defect, only relocates it behind a confusing,
  never-actually-presented intermediate layout, and requires the same
  Vulkan Backend transition-table work regardless.
- **Fork RenderGraph's execution entry point for headless.** Rejected —
  see ADR-0039: exactly the kind of fork ADR-0002 exists to prevent.
- **Thread a shared execution-state object through both `Renderer`'s
  internal `execute()` call and the caller's second one.** Rejected — see
  ADR-0022's Proposed Amendment: requires a larger `Renderer` signature
  change than `finalColorState` alone, for no additional benefit.
- **Decide a general GPU memory allocator (VMA or hand-rolled) now.**
  Rejected — see ADR-0038: no concrete pooling/suballocation need exists
  at this spec's resource count.
- **Silently amend [specs/README.md](README.md)'s Candidate Spec Backlog
  ordering or
  [docs/project-blueprint.md](../docs/project-blueprint.md)'s milestone
  numbering as part of this spec's PR.** Rejected: governance/roadmap
  documents change only through their own review or explicit, minimal,
  status-driven registry maintenance; this spec's PR makes only the
  single status-driven registry edit and records the human-directed
  reprioritization suggestion as an explicit, clearly-labeled,
  not-yet-approved suggestion.

## Testing & Verification Plan

- **Unit tests:** GPU-independent bookkeeping and validation logic against
  a fake/mock `CommandList`, per
  [testing-strategy.md](../docs/process/testing-strategy.md) layer 1. At
  minimum:
  - `execute()`'s generalized binding correctly seeds a bound resource's
    tracked state from a caller-supplied `incomingState` when provided,
    and continues to default to `Undefined` when not — confirming zero
    behavior change for every binding that omits the new field.
  - `execute()` inserts a trailing transition to a caller-supplied
    `finalState` when provided and differing from the resource's ending
    state, inserts none when `std::nullopt` is supplied, and (existing,
    unchanged) inserts none for a resource never used by any pass.
  - `execute()` inserts **no** transition at all when a resource's
    `incomingState` already equals the state its one usage declares — the
    specific case the readback copy pass relies on.
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
    device-dependent creation path.
  - `resource_state_mapping.cpp`'s `planTransition()` (GPU-independent) is
    unit-tested for: the one new entry (`ColorAttachmentOutput →
    TransferSource`) produces a barrier plan without asserting; a
    `(before, after)` pair this spec does not add (e.g.
    `ColorAttachmentOutput → DepthAttachmentReadWrite`) continues to fire
    the existing assertion, confirming exactly one entry and no more.
- **GPU integration tests (Windows/Vulkan):** real
  `OffscreenTarget`/readback-`Buffer`/`CommandList` construction,
  execution, and destruction, Validation-Layers-enabled, mirroring the
  existing `atlantis_vulkan_backend_gpu_tests`/`atlantis_render_graph_tests`
  pattern. At minimum: creating and destroying an `OffscreenTarget`;
  acquiring, using, and returning (via RAII) its vended `RenderTarget`
  more than once across the same instance's lifetime, confirming a second
  `acquireTarget()` succeeds after the first borrow is destroyed/reset;
  that a second `acquireTarget()` called *before* returning the first
  fires the expected assertion (exercised under a non-terminating test
  handler); that dropping a borrow immediately after `Device::submit()`
  returns (before `waitIdle()`) is followed by a correct subsequent cycle
  with no Validation Layer warning/error, confirming the borrow's
  minimum-lifetime contract (ADR-0038) holds in practice; creating and
  destroying a readback `Buffer`; one full render-and-readback cycle
  (`Renderer::drawFrame()` with `finalColorState = TransferSource`,
  followed by the copy pass, submission, and `waitIdle()`-gated read) with
  Validation Layers reporting zero warnings/errors, **confirming no
  `VK_IMAGE_LAYOUT_PRESENT_SRC_KHR` transition is ever recorded for the
  headless target**; a full cycle that calls `Device::waitIdle()`
  immediately after `submit()` (before dropping the borrow, before
  destroying `OffscreenTarget`), confirming the documented correct-order
  flow (`waitIdle()` → destroy `OffscreenTarget` → destroy `Device`) is
  Validation-Layers-clean with no reliance on `Device`'s destructor-time
  drain; and the existing windowed GPU integration tests, re-run
  unmodified in behavior after the three mechanical call-site updates,
  confirming zero regression.
- **Headless integration tests:** this spec **is** what makes this test
  layer possible for the first time (testing-strategy.md layer 2); its own
  GPU integration tests above are the first instance, though this spec
  does not yet formalize a distinct CI job category for it (see Risks &
  Open Questions).
- **Image regression tests:** not applicable — this spec's basic content
  check is explicitly not a golden-image comparison.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of `OffscreenTarget` construction, draw,
  copy, submission, and readback.
- **Manual/automated verification composition:** a minimal, non-shipping
  composition (mirroring `examples/minimal_renderer_demo`'s structure and
  disclaimer, but creating **no window and no Atlantis Platform instance
  of any kind**) constructs a `Device`, an `OffscreenTarget`, this spec's
  fixed `Mesh`/`Material`/camera uniform `Buffer`/depth `Texture`/readback
  `Buffer`, and runs the full acquire → draw → copy → submit →
  `waitIdle()` → read cycle. It confirms:
  - No window, `HWND`, message pump, or `Presentation`/`VkSwapchainKHR`
    object is created anywhere.
  - `Renderer::drawFrame()` is called with `finalColorState =
    ResourceState::TransferSource`, and by inspection of the recorded
    command sequence, no transition to `ResourceState::PresentSource`
    occurs at any point.
  - The readback `Buffer`'s contents pass a **reproducible basic content
    check** — not a golden-image comparison — sufficient to demonstrate
    the mesh actually drew (e.g. the buffer is not uniformly one
    color/all-zero, and a small fixed set of known sample positions differ
    from each other in the direction the fixed mesh/camera/material
    fixture predicts). The exact check's shape is left to the Plan; this
    spec fixes only that it must be automated and reproducible, not a
    human eyeballing a screenshot.
  - The same cycle repeated more than once against the same
    `OffscreenTarget` instance produces consistent results each time.
  - The composition exits cleanly with no outstanding acquired
    `RenderTarget`, no leaked `CommandList`/`Buffer`/`Texture`/
    `OffscreenTarget`, and no Validation Layer warning or error.
  - `Device::waitIdle()` is called, and returns, before `OffscreenTarget`
    is destroyed on every exit path, including an early/error exit —
    verifiable by inspection, satisfying ADR-0038's documented (not
    guaranteed-detectable) destruction precondition.
  - Separately, `examples/frame_execution_demo` and
    `examples/minimal_renderer_demo`, after their mechanical updates, are
    re-run interactively and confirmed to behave identically to their Spec
    0006/0007-verified behavior.

## Risks & Open Questions

- **Exact concrete C++ shape of `execute()`'s extended binding entry** and
  `Renderer::drawFrame()`'s new parameter's exact position/name are left
  to the Plan — this spec and its ADRs fix the conceptual contracts, not
  their exact C++ representation.
- **Exact names** for `OffscreenTarget`'s acquire-equivalent method, the
  owning/non-owning Vulkan Backend implementation classes, and the
  readback `BufferPurpose` enumerator are left to the Plan.
- **Whether a distinct CI/test-category label for headless GPU integration
  tests is needed**, separate from the existing `gpu`-labeled pattern —
  the same open question Spec 0006 and Spec 0007 already flagged for
  windowed GPU tests; flagged, not resolved.
- **Whether the readback `Buffer`'s tightly-packed byte layout needs any
  row-pitch/alignment padding handling** for the Vulkan Backend's own
  `vkCmdCopyImageToBuffer` parameters — a private Vulkan Backend
  implementation concern, left entirely to the Plan.
- **Whether a future Image Regression Testing spec will need
  `OffscreenTarget` to support more than one simultaneously-live instance,
  or pooling/reuse across many test cases** — explicitly left open (see
  Non-Goals).
- **Whether a future spec revisiting the single-frame-in-flight baseline
  will also need to revisit this spec's fully-synchronous
  `waitIdle()`-based readback model** — left open.
- **Whether depth-buffer readback will be needed by a future spec** — left
  open; this spec's Non-Goals explicitly exclude it.
- **Whether a future spec chaining a third or fourth `execute()` call
  within one frame should replace the caller-supplied `incomingState`
  override with automated cross-call state tracking** — considered and
  rejected for this spec's own narrow, two-call scope (see ADR-0039's
  Negative/Trade-offs), but explicitly flagged as a design point a future
  spec with more chained calls should revisit.

## Out of Scope / Future Work

Image Regression Testing (Candidate 3 in
[specs/README.md](README.md)'s backlog, depending on this spec) is the
direct, named next consumer of this spec's rendering-and-readback
foundation — golden-image storage, comparison/tolerance methodology, and
CI gating are entirely its own future scope. Android Platform and Vulkan
presentation (a separate Candidate Backlog entry) is unaffected by and
does not depend on this spec in either direction; see this spec's
accompanying registry update and PR description for the human-directed,
not-yet-approved suggestion to sequence it after both this spec and Image
Regression Testing. A future spec wanting `OffscreenTarget` pooling,
multiple simultaneous instances, depth-buffer readback,
asynchronous/non-blocking readback, automated cross-`execute()`-call state
tracking, or a distinct headless-GPU-test CI category is expected to
extend — not merely reuse unchanged — the ADRs this spec introduces.
