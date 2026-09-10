# Spec: RHI / RenderGraph Frame Execution Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code at explicit human direction; the original
  author metadata left human authorship/ownership confirmation pending.
- **Created:** 2026-08-09
- **Human Review Approval:** slmao, 2026-08-09, following a joint architecture
  review of this Spec and ADR-0019–0021. Four points confirmed as-is: (1)
  **`Device::submit()` takes ownership of the `CommandList` and manages the
  single-frame-in-flight submission's lifetime internally** (waiting on, then
  releasing, any previously-retained submission before accepting a new one) —
  over leaving `CommandList`/fence lifetime as a caller obligation; (2) the
  three-way ADR split (0019/0020/0021) is retained as drafted, and the two
  `execute()`-time guard checks review added — a `ResourceState`-tagged usage
  must have a supplied binding; a bound `RenderTarget` must carry no declared
  read usage — are adopted exactly; (3) four Phase 1 simplifications are
  accepted: single frame-in-flight; a write-only `RenderTarget`
  (always-`ResourceState::Undefined` incoming layout); `clearColor()` as the
  sole recordable GPU operation; no same-frame retry of `acquireNextTarget()`
  on an out-of-date result; (4) updating `specs/README.md`'s backlog and
  `docs/project-blueprint.md`'s Milestone 3 entry is deferred to a separate
  docs PR, not this Spec's branch. Approval authorizes drafting a Plan; it does
  not authorize implementation.
- **Related Plan(s):** [Plan 0006](../plans/0006-rhi-render-graph-frame-execution-foundation.md)
  (`Approved`). Joint Spec + Plan Human Review completed 2026-08-09;
  implementation merged via [PR #23](https://github.com/slmao/Atlantis/pull/23),
  [PR #24](https://github.com/slmao/Atlantis/pull/24).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md)–[ADR-0004](../adr/0004-phase1-threading-baseline.md)
  and [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)–[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md).
  Three new decisions filed as
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md),
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
  [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md);
  all `Accepted` alongside this Spec.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #149](https://github.com/slmao/Atlantis/pull/149) Batch 2. Original scope
  and obligations retained.

## Summary

Closes the gap Specs 0003 and 0005 both left open: a real, GPU-visible frame.
Extends RHI with the minimal `RenderTarget` type, `Presentation` acquire/present
protocol, and `CommandList`/submission interface a frame needs, and extends
RenderGraph with an execution capability that turns a compiled graph (Spec
0005) into recorded, barrier-correct GPU commands against those RHI resources.
It does **not** design a Renderer, shader system, general resource system, or
resource-lifetime/aliasing — it is scoped to exactly the "acquire →
RenderGraph-recorded work → submit → present" bundle
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
deferred in full, plus the minimal `CommandList` surface RenderGraph needs to
record into it.

## Motivation / Problem Statement

Spec 0003 delivered `Presentation`'s non-frame lifecycle only and bundled
acquire/present/`RenderTarget`/command recording into "a later, approved
RenderGraph specification." Spec 0005 delivered RenderGraph's GPU-independent
compilation core only and bundled execution/RHI binding/Vulkan calls into "a
future spec." Both were right to stop where they did, but the result is that
**nothing in this repository can put a pixel on screen yet**. The Candidate
Spec Backlog lists Minimal Renderer as the next candidate depending only on
Spec 0005 — read literally, suggesting it could be drafted directly against
Spec 0005's GPU-independent core. It cannot: a Renderer drawing a mesh would
immediately need real GPU submission, barriers/layout transitions, and
`Presentation` acquire/present, all excluded by Spec 0005's Non-Goals. **This
Spec is that missing foundation.** It does not edit the backlog/roadmap
documents — it states the undeclared dependency for the reviewer's benefit (see
Out of Scope / Future Work).

## Goals

- Define `RenderTarget` as a concrete RHI public type: a non-owning,
  frame-scoped borrow representing one presentable color attachment, per
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)/[ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md).
- Define `Presentation::acquireNextTarget()` and `present(RenderTarget)`,
  resolving in full the bundle
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
  deferred: acquire's shape, `RenderTarget`'s frame ownership, acquire-complete
  synchronization, graph-to-present synchronization, image layout handoff, and
  present's own shape and out-of-date/suboptimal handling.
- Define the minimal RHI `CommandList`/`ResourceState`/`Device::submit()`
  surface needed to record and submit exactly one resource-state transition and
  one drawable operation (`clearColor`) against a `RenderTarget` — no more.
- Extend RenderGraph (Spec 0005) with an execution capability: a per-pass
  execution callback, a `ResourceState` tag on resource usages, a frame-scoped
  external-resource-binding mechanism, and an `execute()` entry point that
  records — but does not submit or present — GPU work in compiled pass order.
- Fix, as a reviewed architectural boundary, the responsibility split between
  RenderGraph (decides *when* / *between what states* a transition is needed,
  from compiled dependency data) and RHI/Vulkan Backend (decides *how*).
- Handle window resize, zero-extent (minimize), and swapchain
  out-of-date/suboptimal conditions correctly across the whole
  acquire/execute/submit/present cycle.
- Verify end-to-end on Windows with a real GPU: acquire a frame target, execute
  at least one GPU pass through RenderGraph and RHI, submit, and present a
  visible frame, correct across resize and minimize/restore, Vulkan Validation
  Layers clean throughout.
- Resolve the three architectural decisions via
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md),
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
  [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md),
  so a future Minimal Renderer spec inherits a settled frame-execution
  contract.

## Non-Goals

- **Atlantis Renderer** — no scene/mesh/camera/material concept, no
  `src/renderer/`. This Spec's verification composition is not a preview of it.
- **Shader System** — no shader authoring, compilation, reflection, or SPIR-V.
  `CommandList::clearColor()` is the only drawable operation, deliberately not
  pipeline-bound or shader-driven
  ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)).
- **Graphics/compute pipeline objects, vertex/index buffers, or any general
  draw call** — left to a future Minimal Renderer / Shader System spec.
- **General RHI resources** — no `Buffer`, `Texture`, or `Sampler`.
  `RenderTarget` remains the only concrete resource type RHI exposes,
  unchanged in kind from
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md), just given
  a concrete shape.
- **A GPU memory allocator (VMA or hand-rolled)** —
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s deferral is
  unaffected; `RenderTarget`'s backing memory remains `Presentation`-owned
  swapchain memory.
- **Resource lifetime, aliasing, or a resource-versioning model** — Spec 0005's
  Non-Goals unchanged; the `ResourceState` tag is for transition bookkeeping
  only, never a lifetime interval or memory-reuse plan.
- **Any caller-authored pass-to-pass dependency edge, or any pass culling** —
  Spec 0005's dependency-derivation and pass-retention rules
  ([ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md))
  are unchanged and not reopened.
- **Multiple frames in flight / double- or triple-buffered command recording**
  — single frame in flight this round
  ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)).
- **Multi-threaded command recording, submission, or graph execution; any
  job/task system** — Phase 1's single-logical-frame-thread baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is unchanged.
- **Android, iOS, or a second graphics backend**, and no abstraction knob added
  "for" one. Windows/Vulkan only.
- **Headless rendering, image regression testing** — windowed ships first; this
  Spec is part of the windowed path.
- **GPU-driven rendering, neural rendering/shading, 3D Gaussian Splatting, or
  any world-model workload** — future phases.
- **Editing `specs/README.md`, `docs/project-blueprint.md`, or any other
  governance/roadmap document** — this Spec states the undeclared dependency in
  its own Motivation only.
- **A `RenderTarget` that supports being read from within the same graph** —
  write-only this round
  ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md));
  no read-back pattern is designed or implemented.
- **Depth attachments or multi-attachment `RenderTarget`s** — exactly one color
  attachment this round.

## Requirements

### Functional

**`RenderTarget`**

- A concrete RHI public type representing one presentable color attachment (the
  acquired swapchain image and its view), per
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md).
- Non-owning: `Presentation` continues to own every swapchain-backed resource
  behind it ([ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md)).
- Frame-scoped: valid from the `acquireNextTarget()` call that vended it until
  the matching `present()` call in the same frame consumes it. Using it outside
  that window is a lifetime precondition violation, not a guaranteed-detectable
  error (consistent with Spec 0005's Error Model tiering).
- **Move-only: movable, not copyable** — fixed explicitly
  ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)).
- Exposes read-only extent/format queries; no resize/mutation API. Carries an
  opaque acquire-complete signal internally for `Device::submit()` to consume —
  never exposed to the caller as a raw handle.
- **Destruction precondition:** `Presentation`/`Device` must not be destroyed
  while a `RenderTarget` they vended has been acquired but not yet consumed by a
  matching `present()`, nor while a submission that `RenderTarget` participated
  in has not yet completed on the GPU. A lifetime precondition violation, not a
  guaranteed-detectable error. Unlike Spec 0003 (no image was ever acquired),
  this is a genuinely new caller obligation this Spec introduces.

**`Presentation` acquire/present**

- `acquireNextTarget()` returns a tri-state outcome: `Err(AcquireError)` for an
  unrecoverable failure; `Ok(std::nullopt)` when the tracked framebuffer extent
  is `{0, 0}` (nothing to draw — not an error); `Ok(RenderTarget{...})`
  otherwise. It internally performs `recreateIfNeeded()`'s existing recreation
  logic
  ([ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md))
  as its first step, every call.
- `VK_ERROR_OUT_OF_DATE_KHR` from the underlying acquire call → `Ok(std::nullopt)`
  for that call and recreation marked needed for the next — no immediate
  in-call retry. `VK_SUBOPTIMAL_KHR` → returns the acquired target normally but
  marks recreation needed for the next call.
- `present(RenderTarget, SubmissionSignal)` consumes the target by value, waits
  on the opaque `SubmissionSignal`
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  `Device::submit()` returned for that target's submission before calling
  `vkQueuePresentKHR`, and treats `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR`
  from present itself as routine (marks recreation needed, not `Result::Err`);
  any other Vulkan error from present is a genuine `Result::Err`.
- `recreateIfNeeded()` (Spec 0003) is unchanged and remains independently
  callable; its own acceptance criteria continue to hold.
- Every `RenderTarget`'s image is treated as entering its first transition of
  the frame from `ResourceState::Undefined`, regardless of true prior layout —
  valid because `RenderTarget` is write-only this round.

**Minimal RHI GPU resource, command recording, and submission**

- `ResourceState` enum: `Undefined`, `ColorAttachmentWrite`, `PresentSource` —
  sufficient for this round's one resource kind
  ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)).
- `CommandList` (RHI interface): `transitionResource(RenderTarget&,
  ResourceState before, ResourceState after)` and `clearColor(RenderTarget&,
  ClearColorValue)` — the only two recordable operations this Spec introduces.
  Not thread-safe, not copyable; caller-owned only while being recorded into —
  **ownership transfers to `Device` when passed to `submit()`**; a caller never
  destroys a `CommandList` it has submitted.
- `Device::createCommandList()` vends a `CommandList`.
- `Device::submit(CommandList, WaitOn)` **takes ownership** of the `CommandList`
  (moved in) and submits it to the graphics/present-capable queue. `WaitOn` is
  the opaque acquire-complete signal carried inside the `RenderTarget` the
  recorded work targets — never a raw semaphore the caller constructs. On
  success, returns an opaque `SubmissionSignal` for `present()`. `Device` owns
  and internally manages whatever semaphore(s)/fence this requires; **no
  general, publicly constructible `Semaphore`/`Fence` RHI type is introduced.**
  Every `VkResult` is checked; failures surface through `atlantis::Result`.
- Single frame-in-flight baseline, enforced by `Device` internally: `Device`
  retains at most one previously-submitted `CommandList` and its fence at a
  time; each `submit()` waits on that prior fence (if any — the first call has
  none) and releases the prior `CommandList` before accepting the new
  submission. A caller never sees or manages a fence.
- **`Device` exposes a way to drain any outstanding submission and block until
  the GPU is idle** (e.g. a `waitIdle()`-shaped call, or a guarantee built into
  `Device`'s destructor) — required so a caller can satisfy `RenderTarget`'s
  destruction precondition on every exit path, including a mid-frame exit. The
  exact method name and whether it is caller-invoked or implicit is left to the
  Plan; that a mechanism exists and is exercised by manual verification is
  fixed here.
- No `Vk*` type, and no Vulkan header, appears in any RHI public header — same
  structural rule as Spec 0003, verified the same way.
- No direct `vkCmd*` call exists anywhere outside the Vulkan Backend's
  `CommandList` implementation.

**RenderGraph execution integration**

- `RenderGraphBuilder::reads()`/`writes()` (Spec 0005) are extended to
  additionally accept a `ResourceState`, used only for transition bookkeeping —
  Spec 0005's single-producer model, dependency derivation, cycle detection,
  and deterministic ordering
  ([ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md))
  are unchanged in every other respect.
- A pass declaration gains an execution callback
  (`std::function<void(CommandList&)>` or equivalent), recorded at declaration
  time.
- A new `execute(CompiledGraph, bindings, CommandList&)` entry point walks the
  compiled pass order and, for each pass: inserts a `transitionResource()` call
  whenever a resource usage's declared state differs from that resource's
  most-recently-recorded state, then invokes the pass's execution callback
  ([ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)).
- A frame-scoped binding mechanism associates each producer-less logical
  resource used by the graph with a concrete RHI `RenderTarget`, valid only for
  that one `execute()` call.
- **`execute()` validates two binding-related preconditions as
  guaranteed-detectable programmer errors** (`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`):
  every resource that participates in a `ResourceState`-tagged usage must have a
  binding supplied; and a bound `RenderTarget` must have no declared read usage
  anywhere in the compiled graph (protecting
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
  write-only/always-`Undefined` premise structurally). Spec 0005's plain,
  untagged logical resources remain legal and require no binding.
- `execute()` inserts one trailing `transitionResource()` call to
  `ResourceState::PresentSource` for any bound `RenderTarget`, after the last
  pass in compiled order that uses it.
- `execute()` only records into the caller-provided `CommandList`; it never
  calls `Device::submit()` or `Presentation::present()` — those remain
  explicit, separate calls made by the caller after `execute()` returns.
- RenderGraph's public headers still contain no `Vk*` type, no Vulkan header,
  and no Atlantis Platform type — this Spec's new RHI dependency does not relax
  that rule.

**Windows resize / zero-extent / out-of-date handling across the whole frame
cycle**

- Observing a `WindowResize` event and calling `notifyResized()` (Spec 0003,
  unchanged) followed by the next frame's `acquireNextTarget()` recreates the
  swapchain at the new extent before acquisition, transparently to the caller.
- A zero-extent window (minimized) → every frame's `acquireNextTarget()`
  returns `Ok(std::nullopt)`; the caller skips RenderGraph execution,
  submission, and present entirely for that frame — no Vulkan call on this
  path, structurally, mirroring `recreateIfNeeded()`'s existing zero-extent
  guarantee.
- Restoring from minimized → the very next `acquireNextTarget()` recreates and
  then successfully acquires, with no special-cased "recovery" call.
- An out-of-date or suboptimal swapchain at either acquire or present never
  crashes, hangs, or produces a Validation Layer warning/error — it is absorbed
  into the recreation-needed bookkeeping.
- Every exit path from the verification composition, including one that exits
  mid-frame (after `acquireNextTarget()` returns a target but before `present()`
  is called), waits for any outstanding GPU work to complete before destroying
  `Presentation`/`Device`, satisfying the destruction precondition.

**Phase 1 single-threaded orchestration and thread-safety contracts**

- Every new public type (`RenderTarget`, `CommandList`, the `execute()` entry
  point, the extended `Presentation`/`RenderGraphBuilder` methods) documents its
  thread-safety contract at its public API — in every case "not thread-safe;
  caller-thread-only," on the single Phase 1 logical frame thread that also
  owns the Windows Platform message pump
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)).
- No mutex, atomic, job/task system, or lock-free structure is introduced
  anywhere in this Spec's scope.

### Non-functional

- **Performance:** "does not stall, leak, or busy-spin unnecessarily" — the
  same bar Spec 0003 set. Single-frame-in-flight is an explicit simplification,
  not a performance claim.
- **Memory:** no GPU memory suballocation strategy is introduced or assumed
  ([ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)); host-side
  allocation (e.g. `execute()`'s resource-state bookkeeping map) uses ordinary
  RAII and standard containers.
- **Portability:** implemented and verified on Windows only; RHI's and
  RenderGraph's public interface shapes must not preclude Android's future
  implementation — verified by inspection.
- **Other:** no new third-party dependency. Unit tests use Catch2 v3.

## Proposed Design

### Module boundaries

This Spec realizes the RenderGraph → RHI dependency Spec 0005's own Out of
Scope / Future Work section anticipated ("a future RenderGraph-execution
spec... consuming both that new RHI surface and this spec's compiled pass
order"), which
[module_boundaries.md](../docs/architecture/module_boundaries.md) also lists.
The per-frame flow: Windows Platform → verification composition (not Runtime) →
`Presentation::acquireNextTarget()` (skip on `Ok(std::nullopt)`; propagate on
`Err`; continue on `Ok(RenderTarget)`) → `Device::createCommandList()` →
`RenderGraphBuilder` (Spec 0005, extended with `ResourceState`-tagged
usages and `setExecute`) → `compile()` → `render_graph::execute(CompiledGraph,
bindings, CommandList&)` (records transitions + callbacks + the trailing
`PresentSource` transition) → `Device::submit(CommandList, target's signal)`
(takes ownership; internally waits on its own prior submission's fence; returns
`SubmissionSignal`) → `Presentation::present(RenderTarget, SubmissionSignal)`
(waits, `vkQueuePresentKHR`, absorbs out-of-date/suboptimal). On every exit
path, including a mid-frame exit after acquire but before present: drain
`Device`'s outstanding submission before destroying `Presentation`/`Device`.

RHI, RenderGraph, and Vulkan Backend keep exactly the dependency directions
[module_boundaries.md](../docs/architecture/module_boundaries.md) states:
RenderGraph → RHI, Core; Vulkan Backend → RHI, Core, Vulkan SDK; RenderGraph
never depends on Vulkan Backend, Atlantis Platform, or Runtime. Renderer still
does not exist and is not depended on by anything this Spec touches.

### RenderTarget, acquire/present, minimal RHI resource/command/submission, RenderGraph execution

See
[ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)
(`RenderTarget`'s non-owning, frame-scoped, write-only borrow;
`acquireNextTarget()`'s tri-state outcome and its folding-in of
`recreateIfNeeded()`; `present()`'s out-of-date/suboptimal absorption; the
always-`Undefined`-incoming-layout simplification),
[ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
(the `ResourceState` enum, `CommandList`'s two operations,
`createCommandList()`/`submit()`, single-frame-in-flight), and
[ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
(the `ResourceState`-tagged usage declarations, the per-pass execution
callback, the frame-scoped external binding mechanism, `execute()`'s
transition-insertion algorithm including the trailing `PresentSource`
transition, and why RenderGraph records but never submits or presents) for
each full decision and rationale.

### Threading and error handling

Single logical frame thread
([ADR-0004](../adr/0004-phase1-threading-baseline.md)): every new call happens
on the thread that owns the Windows Platform message pump; no type claims a
stronger guarantee. Recoverable runtime errors (acquire failure, submission
failure, a genuine present failure) use `atlantis::Result<T, E>` — no exception
anywhere in RHI, Vulkan Backend, or RenderGraph's surface. Programmer errors —
calling `execute()` with a `ResourceState`-tagged usage missing a binding;
binding a `RenderTarget` to a resource with a declared read usage — use
`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`
([ADR-0009](../adr/0009-assertion.md)). `RenderTarget`/`CommandList` misuse
outside their valid frame/lifetime window is a **lifetime precondition
violation**, the same tier as Spec 0005's builder-handle-after-destruction case
— not claimed guaranteed-detectable, not tested for detection; the obligation
is fixed and this Spec's verification composition satisfies it. Every
`VkResult` along acquire, command recording, submission, and present is
checked. Vulkan Validation Layers are enabled unconditionally in Debug builds
and any GPU-touching CI job; a validation warning or error is a build/test
failure.

## Architectural Impact

Introduces architecture; required three new ADRs before `Approved`, none
decided by this Spec's prose. All reached `Accepted` alongside this Spec:

| Decision | ADR |
|---|---|
| Presentation acquire/present protocol and `RenderTarget` frame-borrow contract — resolves in full the bundle [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md) deferred | [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md) |
| Minimal RHI GPU resource, command recording, and submission interface — `ResourceState`, `CommandList`, `createCommandList()`/`submit()`, and the single-frame-in-flight baseline | [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md) |
| RenderGraph/RHI execution integration and dependency-to-barrier responsibility — RenderGraph's new RHI dependency, the `ResourceState`-tagged usage model, the per-pass execution callback, the frame-scoped binding mechanism, and the exact split between RenderGraph (when/between what states) and RHI/Vulkan Backend (how) | [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md) |

No existing `Accepted` ADR's conclusions are restated, reopened, or modified.
Architectural Impact is not "None" — `RenderTarget`, `CommandList`, and
RenderGraph's execution capability are each a new public API surface. This
checkbox-level approval is not itself an authorization to implement.

## Alternatives Considered

- **Scope this Spec to RHI's frame-execution surface only, leave RenderGraph
  execution to a later spec.** Rejected: RHI's `CommandList`/transition surface
  has no real consumer without RenderGraph's execution phase to record into it;
  better to review both halves of one coherent frame cycle together.
- **Extend further to also add a minimal graphics pipeline/shader-binding
  surface, so the demonstrated pass is a real draw call.** Rejected
  ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  Alternatives) — pulls Shader System's and Minimal Renderer's scope in,
  contradicting the sequencing discipline Spec 0005 established.
- **Have Runtime-equivalent code manually insert barriers between passes.**
  Rejected
  ([ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  Alternatives) — reintroduces caller-authored, hand-scheduled GPU work,
  forbidden by AGENTS.md's mandatory-RenderGraph-path rule.
- **Silently amend `docs/project-blueprint.md` / `specs/README.md` to insert
  this Spec as an explicit prerequisite.** Rejected — governance/roadmap
  documents change only through their own review; this Spec states the
  dependency in its Motivation.
- **Support multiple frames in flight from the start.** Rejected this round
  ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  Alternatives) — no current performance requirement justifies the added
  synchronization-object complexity.

## Testing & Verification Plan

- **Unit tests** (testing-strategy.md layer 1, no Vulkan device): the
  tri-state acquire-outcome mapping logic where exercisable without a real
  device; RenderGraph's extended `ResourceState`-aware usage declaration and
  `execute()`'s transition-insertion decision logic, exercised against a
  fake/mock `CommandList` that records the calls it received. At minimum:
  `execute()` inserts no transition when consecutive same-resource usages
  declare the same state; exactly one transition when a resource's declared
  state changes between two adjacent usages; the trailing `PresentSource`
  transition exactly once, after the last pass that uses a bound `RenderTarget`,
  for one- and multi-pass graphs; no transition on a bound `RenderTarget` never
  used by any pass (no spurious trailing transition); every pass's execution
  callback invoked in exactly the compiled pass order; calling `execute()`
  without a binding for a resource in a `ResourceState`-tagged usage triggers
  the programmer-error/assertion policy; binding a `RenderTarget` to a resource
  with any declared read usage triggers it; a plain untagged Spec 0005 resource
  (no state, no binding) compiles and executes with no transition and no
  assertion.
- **Headless integration tests:** not applicable in
  [testing-strategy.md](../docs/process/testing-strategy.md)'s current sense
  (layer 2 is headless *rendering*); a real-device, no-window
  `Device`/`CommandList` submission test is possible but not required — flagged
  under Risks, consistent with Spec 0003's equivalent flag.
- **Image regression tests:** not applicable — manual verification checks for a
  visible, expected-color frame by direct observation.
- **Vulkan Validation Layers:** mandatory and must run clean for every manual
  and automated exercise of acquire, command recording, transition, submission,
  and present.
- **Manual verification:** a minimal, non-shipping composition creates a
  Windows Platform window, constructs a `Device` and `Presentation` (reusing
  Spec 0003's construction path), declares a one-pass RenderGraph that clears
  the acquired `RenderTarget` to a known, visually-distinct color, and — driven
  by the existing non-blocking Platform event loop — runs the full acquire →
  execute → submit → present cycle every frame. It confirms: a visible window
  shows the expected clear color across repeated frames; interactive resize
  continues to show the correct color at the new size, with no visible
  corruption, tearing attributable to a missing transition, or validation
  warning during/after the resize; minimizing produces no crash, no busy-spin,
  and no Vulkan call while minimized (verifiable by inspection of the
  zero-extent skip path); restoring resumes correct rendering with no special
  recovery step; the application exits cleanly at any point — at startup,
  mid-resize, minimized, or after any number of frames — with no outstanding
  acquired `RenderTarget`, no leaked `CommandList`, and no Validation Layer
  warning or error at any point, including at shutdown; and **a deliberate
  mid-frame exit** (shutdown immediately after `acquireNextTarget()` returns a
  `RenderTarget` but before `Device::submit()`/`present()`) is exercised
  explicitly, at least once, and completes with no Validation Layer warning or
  error, satisfying the destruction precondition even on this path.

## Acceptance Criteria

- [ ] RHI's and RenderGraph's public headers contain no `Vk*` type and no
      `#include <vulkan/...>`.
- [ ] No direct `vkCmd*` call, and no `VkImageMemoryBarrier`/
      `vkCmdPipelineBarrier` construction, exists anywhere outside the Vulkan
      Backend's `CommandList` implementation.
- [ ] `RenderTarget` is non-owning, frame-scoped, and write-only in every code
      path this Spec implements — no read-back-from-`RenderTarget` capability
      exists anywhere.
- [ ] `RenderTarget` is move-only (movable, non-copyable) — a compile-time
      property.
- [ ] Binding a `RenderTarget` to a logical resource with any declared read
      usage in the compiled graph is rejected as a programmer error at
      `execute()` time, in every tested case.
- [ ] A `ResourceState`-tagged usage against a resource with no supplied
      binding is rejected as a programmer error at `execute()` time.
- [ ] `Device::submit()` takes ownership of the `CommandList`; no code path
      destroys a submitted `CommandList`, and no code path submits a
      `CommandList` without first waiting (via `Device`'s own internal
      bookkeeping) on any prior submission's fence — verifiable by inspection
      that `Device` alone owns this sequencing.
- [ ] `Presentation`/`Device` are never destroyed anywhere in this Spec's
      implementation or manual verification while a `RenderTarget` they vended
      has been acquired but not yet presented, or while a submission has not yet
      completed on the GPU — including on the deliberate mid-frame exit path.
- [ ] `acquireNextTarget()` returns `Ok(std::nullopt)` (not an error) at zero
      framebuffer extent, both for a freshly-minimized window and an
      initially-zero-extent window at startup — verifiable by code inspection.
- [ ] A Windows resize results in the next frame's `acquireNextTarget()`
      transparently recreating the swapchain and successfully acquiring at the
      new extent, with the resulting frame rendered and presented correctly.
- [ ] `VK_ERROR_OUT_OF_DATE_KHR`/`VK_SUBOPTIMAL_KHR` at either acquire or
      present time never crashes, hangs, or produces a validation warning/error
      — absorbed into recreation-needed bookkeeping.
- [ ] `execute()` never calls `Device::submit()` or `Presentation::present()` —
      verifiable by inspection that RenderGraph's implementation has no
      reference to either.
- [ ] No GPU command is recorded into any `CommandList` anywhere in this Spec's
      implementation outside a RenderGraph pass execution callback.
- [ ] Every `VkResult` along acquire, command recording, submission, and
      present is checked; no `VkResult` is discarded.
- [ ] Debug builds and any GPU-touching CI job run with Vulkan Validation
      Layers enabled; a validation warning or error fails the run.
- [ ] The manual verification demo shows a visible, correctly-colored frame;
      continues across interactive resize; makes zero Vulkan calls while
      minimized; and resumes correctly on restore.
- [ ] No pipeline object, shader, vertex/index buffer, general draw call,
      `Buffer`/`Texture`/`Sampler` type, or GPU memory allocator is created
      anywhere by this Spec's implementation.
- [ ] No caller-authored pass-to-pass dependency edge, and no pass culling, is
      implemented anywhere this Spec touches — Spec 0005's existing rules on
      both are unchanged.
- [ ] No `src/renderer/` or Shader System source is created.
- [ ] No Android NDK build configuration, no second graphics backend, and no
      thread/job system is introduced anywhere.
- [ ] No multiple-frames-in-flight machinery is implemented — `Device` retains
      at most one previously-submitted `CommandList` at a time, and the
      single-frame-in-flight baseline is a structural property of the
      implementation, not merely a documented intention.
- [x] All three ADRs
      ([ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md),
      [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
      [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
      reach `Accepted` before this Spec is marked `Approved` — satisfied
      2026-08-09; this checkbox gates Spec approval, not implementation.

## Risks & Open Questions

- Whether `Device::createCommandList()`/`submit()` needs its own new
  test-harness category distinct from Spec 0003's already-flagged question about
  `Device`/`Presentation` construction testing — both need a real Vulkan device
  but neither fits
  [testing-strategy.md](../docs/process/testing-strategy.md)'s layer 1 or 2 as
  named. Flagged, not resolved.
- The exact `ClearColorValue` representation (a plain RGBA struct vs. a variant
  covering future non-float formats) — left to the Plan.
- Whether `CommandList` should be reset-and-reused across frames (a pool)
  rather than destroyed/recreated each frame — left to the Plan, provided it
  does not introduce multi-frame-in-flight semantics.
- The concrete representation of `execute()`'s frame-scoped resource binding —
  left to the Plan.
- Whether `AcquireError`/submission failure types reuse and extend the existing
  `PresentationError` enum or introduce new sibling error types — left to the
  Plan. This Spec fixes error *routing* and the tri-state acquire-outcome
  shape, not the concrete enum(s)' spelling.
- The exact method name and shape for `Device`'s "drain outstanding submission"
  capability (caller-invoked `waitIdle()`-shaped vs. implicit in the
  destructor) — left to the Plan; this Spec fixes only that it must exist and
  be exercised on every exit path, including a deliberate mid-frame exit.
- Whether a future Minimal Renderer spec will need to widen
  `transitionResource()`'s parameter from `RenderTarget&` to a general resource
  reference, and whether `ResourceState` will need meaningfully more variants
  once real `Buffer`/`Texture` types exist — explicitly left open; this Spec
  does not pre-widen either, per AGENTS.md's "no speculative abstraction"
  principle.
- Whether the single-frame-in-flight baseline will need revisiting once a real
  frame-time/performance signal exists — left open, per
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  own Trade-offs.

## Out of Scope / Future Work

Atlantis Renderer ("Minimal Renderer" in the backlog), Shader System, Android
Platform and Vulkan presentation, headless rendering, and image regression
testing all remain later, separately-specced work per
[docs/project-blueprint.md](../docs/project-blueprint.md), not advanced by this
Spec beyond satisfying this frame-execution foundation as their shared,
previously-undeclared dependency (see Motivation). A future Minimal Renderer
spec is expected to be the first consumer that needs `CommandList` to grow
beyond `transitionResource()`/`clearColor()` into a real pipeline-bound
draw-call surface, and the first to need general `Buffer`/`Texture` resources
and a wider `ResourceState` set — none of that shape is predicted or
pre-designed here. A future performance-motivated spec may revisit the
single-frame-in-flight baseline. A future resource-lifetime/versioning spec
(anticipated by Spec 0005) remains entirely separate from this Spec's
`ResourceState` transition-bookkeeping tag, which is not a lifetime or aliasing
mechanism.
