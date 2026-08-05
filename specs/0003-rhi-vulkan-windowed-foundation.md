# Spec: Atlantis RHI and Vulkan Windowed Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human direction;
  human authorship/ownership of this spec is pending confirmation at
  Human Review.
- **Created:** 2026-08-06
- **Related Plan(s):** None yet — a plan is written only after this spec
  reaches `Approved` (see [AGENTS.md](../AGENTS.md); explicitly not
  created as part of this task).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (amended),
  [ADR-0011](../adr/0011-native-window-handle-representation.md), and
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md) — all
  `Accepted`. See **Architectural Impact** below — three new decisions are
  identified and drafted alongside this spec as
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md), and
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
  — all now `Accepted`, confirmed alongside this spec on 2026-08-06.

## Summary

This spec establishes the minimal, backend-agnostic **RHI** (Render
Hardware Interface) boundary that a future RenderGraph and Renderer will
be built against, and the **Vulkan Backend's** Windows-windowed `Device`/
`Presentation` foundation that is Phase 1's sole implementation of that
boundary. It is explicitly **not** a Renderer, a rendering-pass design, or
a graphics-pipeline design. Its scope stops at `Presentation`'s
**non-frame lifecycle**: Vulkan instance/device/queue initialization,
Windows WSI surface creation, swapchain creation and safe destruction,
swapchain metadata queries, zero-extent handling, and resize-driven lazy
recreation. **`Presentation` never acquires, vends, or tracks a swapchain
image anywhere in this spec** — acquiring a frame, preparing it via
graph-recorded work, and presenting it are bundled together and left
entirely to a later, approved RenderGraph specification and plan. This
spec does not record, schedule, or submit any GPU command, and does not
incur any per-image synchronization obligation.

## Motivation / Problem Statement

`specs/0001-project-foundation.md` (Core) and
`specs/0002-platform-foundation.md` (Windows Platform) are implemented.
Nothing above them exists: no RHI, no Vulkan Backend, no RenderGraph, no
Renderer, no Shader System, no Runtime module. Per
[AGENTS.md](../AGENTS.md), Renderer cannot be specced or built directly —
it depends on RHI and RenderGraph, neither of which exist, and any attempt
to build a "renderer" without them would either invent RHI/RenderGraph
implicitly (the exact uncontrolled-architectural-decision failure mode the
Golden Rule exists to prevent) or bypass RenderGraph with an ad hoc direct
submission path (explicitly forbidden).

Several architectural questions are already flagged as open across the
accepted architecture-baseline ADRs and `docs/architecture/`, but none has
been resolved into a concrete, buildable interface:

- **How** does Runtime obtain a concrete `Device`/`Presentation` instance
  without RHI depending on Vulkan Backend, and without leaking `Vk*` types
  into RHI's public surface? ([ADR-0001](../adr/0001-rhi-backend-independence.md)'s
  own Consequences section flags this mechanism as undecided.)
- **What** does `Presentation`'s non-frame lifecycle contract actually
  look like, and how exactly does it honor the already-`Accepted` rule
  that a zero-extent window must never trigger swapchain creation/
  recreation ([ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md),
  `specs/0002-platform-foundation.md`'s Ownership and Lifetime table) —
  **without** this spec having to invent a frame-level acquire/present
  contract it has no RenderGraph to validate against yet?
- **Whether** a GPU memory suballocation strategy (VMA vs. hand-rolled) is
  needed now, and if not, how that is prevented from being decided
  implicitly by whichever code happens to need an allocation first (per
  [AGENTS.md](../AGENTS.md) Vulkan-specific rules, which flag this
  explicitly as not yet decided).

This spec exists to answer exactly these questions — and no others — so
that a future RenderGraph/Renderer spec inherits a settled, non-frame RHI
boundary instead of having to invent one under pressure to also design
rendering passes at the same time.

## Goals

- Define the minimal RHI public interface surface needed for
  `Device`/`Presentation` construction and `Presentation`'s non-frame
  lifecycle (recreation, destruction, metadata queries) — implemented and
  verified by this spec.
- **Do not** define, sketch, or illustrate an acquire/present frame-level
  API in this spec. That API, `RenderTarget`'s frame-ownership shape, and
  every synchronization detail it implies are bundled together and left
  to the future RenderGraph specification — see
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  "Deferred as One Bundle" section.
- Implement `Presentation`'s non-frame lifecycle on Windows only, using
  Vulkan as Phase 1's sole backend, reusing the existing, implemented
  `Atlantis Platform` Windows path (`specs/0002-platform-foundation.md`)
  for window/surface handle production — no changes to Platform's public
  API.
- Make the zero-framebuffer-extent rule
  ([ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md))
  a structural property of swapchain (re)creation, applying uniformly to
  both the first swapchain and every later recreation — not a convention
  callers must remember to honor, and not something that only applies
  "after" construction.
- Resolve the three open questions in Motivation via dedicated ADRs
  ([ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)),
  so a future RenderGraph/Renderer spec does not have to.
- Verify `Presentation`'s non-frame lifecycle — construction, WSI surface
  creation, swapchain creation/recreation and its metadata, zero-extent
  bookkeeping, and safe destruction — end-to-end on a real Windows machine
  with a real GPU, with Vulkan Validation Layers running clean, using a
  verification story that never acquires, vends, or references a
  swapchain image, and therefore never risks image-pool exhaustion or an
  undefined-synchronization destruction.

## Non-Goals

Explicitly deferred to future specs, not designed, sketched, or
implemented here:

- **RenderGraph** — pass declaration, resource dependency tracking,
  barrier/lifetime resolution, execution ordering. This spec performs
  **no GPU work that would need a graph to orchestrate** — no image
  layout transition, no command buffer recording, no command submission
  of any kind. The first acquire → graph-recorded work/synchronization →
  present frame is explicitly left to a later, approved RenderGraph
  specification and plan, not attempted here in any reduced or bypassing
  form.
- **Renderer** — frame orchestration, scene/material submission, any
  concept of a "draw." No draw call, no clear-color command, no graphics
  pipeline (`VkPipeline`) object, no image layout transition, and no
  command buffer recording or submission of any kind is created anywhere
  by this spec.
- **`acquireNextTarget()` (or any acquire-shaped operation), `present()`,
  and everything they imply.** Not implemented. Not called. Not tested.
  **Not even declared with a concrete signature** — this spec does not
  pre-sketch a future acquire/present API, per
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  explicit deferral. Specifically excluded, all together, as one bundle:
  - Any frame-level acquire operation and the `RenderTarget` it would
    vend.
  - Acquire-complete and render-finished semaphores or fences.
  - Image layout transitions of any kind.
  - Command buffer recording or submission of any kind.
  - Any "empty frame," "acquire → present," or "no-render present"
    verification path — including a *single*, *repeated*, or
    *rate-limited* acquire-without-present pattern. Repeated acquiring
    without presenting exhausts the swapchain's image pool and can block
    or fail; destroying a `Presentation` with an outstanding acquired
    image is an undefined-synchronization shortcut, not a valid test
    technique — see [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
    Context for the full reasoning. This spec's verification therefore
    never acquires a swapchain image at all (see Proposed Design and
    Testing & Verification Plan).
- **Shader System** — no shader authoring, compilation, or reflection.
- **Atlantis Runtime (the module)** — this spec's own verification uses a
  minimal, non-shipping composition (mirroring how
  `examples/foundation_demo` and `examples/platform_demo` are not
  previews of Runtime) to exercise Platform + RHI + Vulkan Backend
  end-to-end. It is not, and must not be mistaken for, Runtime itself.
  Runtime's actual responsibilities — owning the Platform instance across
  the application's full lifetime, the general frame-loop policy, Android
  pause/resume handling — remain a separate future spec's scope.
- **Resource loading / general `Buffer`/`Texture` creation** — this spec's
  `Device`/`Presentation` foundation does not allocate any general-purpose
  GPU resource; see [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)
  for why that decision is deliberately deferred, not silently answered by
  omission.
- **Headless rendering, image regression testing** — Phase 1 sequencing
  ships windowed first (per [AGENTS.md](../AGENTS.md)); this spec is part
  of that windowed path and does not implement or design headless.
- **Android NDK build / Android Vulkan Backend implementation** — Android
  remains architecturally anticipated (this spec's interfaces must not
  preclude it) but not implemented; no Android build configuration is
  introduced.
- **iOS, MoltenVK, or a native Metal RHI backend.**
- **A second Vulkan-capable graphics backend of any kind**, and no
  abstraction knob added "for" one.
- **GPU-driven rendering, multi-threaded command recording/submission, or
  any job/task system** — Phase 1's single-logical-frame-thread baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is unchanged.
- **A general Vulkan memory suballocator (VMA or hand-rolled).** See
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md).

## Requirements

### Functional

- A `Device` RHI interface is constructible, on Windows, via Vulkan
  Backend's construction API: it selects a suitable physical device and
  creates a logical `VkDevice` with at least one queue capable of both
  graphics and presentation.
- A `Presentation` RHI interface is constructible from a `Device` and the
  `NativeWindowHandle` the existing Windows Platform implementation
  produces (via `SurfaceCreated`), creating a `VkSurfaceKHR` through the
  private WSI boundary [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md)
  (as amended) already establishes. **Construction creates the surface
  only, not a swapchain** — see
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md).
- `Presentation` exposes `notifyResized(WindowExtent)` (updates the
  tracked extent, makes no Vulkan call) and `recreateIfNeeded()` (the
  sole operation that creates, recreates, or destroys the
  `VkSwapchainKHR`, per [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  exact contract). **When the tracked framebuffer extent is `{0, 0}`,
  `recreateIfNeeded()` issues no Vulkan swapchain call whatsoever** —
  this must hold structurally (verifiable by code inspection), for both
  the first call after construction and any later call, not merely as
  documented caller discipline.
- `Presentation` exposes read-only swapchain metadata queries (image
  count, format, current extent) reflecting the most recently
  successfully (re)created swapchain. **These queries never hand out an
  image handle, a `RenderTarget`, or any per-image resource.**
- Observing a `WindowResize` `PlatformEvent` (already implemented, per
  `specs/0002-platform-foundation.md`), calling `notifyResized()`, and
  then calling `recreateIfNeeded()` results in the swapchain being
  recreated at the new extent, observable via the metadata queries above.
- `Presentation` can be destroyed safely at any point in this sequence —
  right after construction, after any number of `recreateIfNeeded()`
  calls, at zero or non-zero extent — because no swapchain image is ever
  acquired or outstanding under this spec's contract, so there is no
  synchronization precondition destruction must satisfy.
- Every `VkResult` returned along the `Device`/`Presentation`
  construction, recreation, and destruction paths is checked; recoverable
  swapchain-creation failures are surfaced through `atlantis::Result`,
  never silently discarded.
- Vulkan Validation Layers are enabled unconditionally in Debug builds and
  any GPU-touching CI job; a validation warning or error is a build/test
  failure, not advisory output — see
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- Programmer errors (e.g. calling any RHI method before successful
  `Device`/`Presentation` construction) use `ATLANTIS_CHECK`/
  `ATLANTIS_ASSERT`, per the existing convention from Spec 0001/0002 — not
  a new assertion mechanism.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily." No frame-pacing or performance target is introduced.
- **Memory:** no GPU memory suballocation strategy is introduced or
  assumed — see [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md).
  Whatever host-side allocation this spec's own narrow scope needs uses
  ordinary RAII, consistent with [AGENTS.md](../AGENTS.md)'s ownership
  rules.
- **Portability (within the Vulkan-only Phase 1 constraint):** implemented
  and verified on Windows only. The RHI public interface shape must not
  preclude Android's future implementation — verified by inspection (no
  Windows type in any RHI public header), not by building an Android
  target, which remains out of scope.
- **Other:** no new third-party dependency beyond the already-categorized
  Vulkan SDK/loader (per [ADR-0006](../adr/0006-dependency-management.md)'s
  "external system/toolchain dependency" category).

## Proposed Design

### Module boundaries (unchanged, this spec's slice within them)

This spec does not move or reinterpret any existing module boundary — it
fills in the previously-undecided mechanism inside boundaries
[ADR-0001](../adr/0001-rhi-backend-independence.md),
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (amended),
and [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
already fixed:

```
Atlantis Platform (existing, Windows-implemented)
  -- SurfaceCreated{ NativeWindowHandle } -->
Runtime-equivalent composition (this spec's minimal verification demo;
NOT the future Runtime module — see Non-Goals)
  -- NativeWindowHandle, by value, uninterpreted -->
Vulkan Backend's construction API (ADR-0014)
  -- consumes NativeWindowHandle only here, inside its private WSI
     boundary (ADR-0005 amended) -->
  VkSurfaceKHR (private to Vulkan Backend)
  -->
RHI's public Device / Presentation interfaces
  (backend-agnostic; zero Vk*, zero OS types, zero NativeWindowHandle;
  no RenderTarget vended anywhere in this spec — see Non-Goals)
```

`NativeWindowHandle` crosses exactly one boundary beyond Platform/Runtime:
into Vulkan Backend's construction API (ADR-0014). It never reaches
generic RHI's public `Presentation` interface itself, per
[ADR-0011](../adr/0011-native-window-handle-representation.md)'s existing
constraint — this spec does not relax that constraint, it satisfies it.

### Object model

- **`Device`** (RHI public interface, Vulkan Backend concrete
  implementation) — represents a logical GPU device and its queues.
  Constructed once via Vulkan Backend's factory API
  ([ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)).
  Has no window/surface knowledge of its own. Owned by whoever constructs
  it (this spec's verification demo; Runtime, once that module exists).
- **`Presentation`** (RHI public interface, Vulkan Backend concrete
  implementation, `VkSwapchainKHR`-backed) — the swapchain-backed
  drawable-surface abstraction [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)
  already established at the ownership-model level, scoped in this spec
  to its **non-frame** lifecycle only. Constructed from a `Device` and a
  `NativeWindowHandle` via Vulkan Backend's factory API (surface only, no
  swapchain yet). Owns its swapchain once one exists. Implements and this
  spec verifies `notifyResized()`, `recreateIfNeeded()`, and the
  swapchain metadata queries — never touches Vulkan at zero extent, at
  construction or any later recreation attempt. **Does not implement,
  declare, or expose any acquire or present operation, and vends no
  `RenderTarget` and no image handle of any kind** — see
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
  for the exact contract and the full bundle of what is deferred.
- **`RenderTarget`** — **not defined, declared, or referenced anywhere in
  this spec's implementation.** `RenderTarget` only has meaning as "the
  thing a frame-level acquire vends," and acquire is deferred in full (see
  Non-Goals); defining `RenderTarget`'s shape now, with no acquire to vend
  it and no Renderer to borrow it, would be exactly the kind of premature
  future-API definition this spec avoids. `RenderTarget`'s concrete shape
  is left entirely to the future RenderGraph specification, consistent
  with [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)'s
  existing ownership-level statement that it does not fix the type's
  concrete C++ shape.
- **`VkSurfaceKHR`, `VkSwapchainKHR`, and every other `Vk*` type** — fully
  private to Vulkan Backend's `Presentation` implementation and its WSI
  boundary. None crosses into any RHI public header, ever.

### Windows resize vs. future Android surface destruction

- **Windows resize:** Runtime-equivalent code observes `WindowResize` and
  calls `Presentation::notifyResized()` (see
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
  for the exact call shape), then calls `recreateIfNeeded()` — the same
  `Presentation` object recreates its swapchain in place. The
  `VkSurfaceKHR` itself is not necessarily recreated (the native window is
  unchanged). This recreation path is implemented and verified by this
  spec entirely through `notifyResized()`/`recreateIfNeeded()` and the
  metadata queries — no acquire, no present, no command buffer, anywhere.
- **Android surface destroyed/recreated (architecturally anticipated,
  not implemented or tested by this spec):** per
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md) and
  `docs/architecture/resource_lifetime.md`, a new `SurfaceCreated` handle
  is never guaranteed to reference the same underlying object. This spec's
  `Presentation` object-lifetime model therefore treats this case as full
  object teardown and reconstruction, not an in-place resize: on
  `SurfaceDestroyed`, the owning code destroys its `Presentation` instance
  entirely (safely — no acquired image can ever be outstanding, per this
  spec's contract); on a later `SurfaceCreated`, it constructs a brand-new
  `Presentation` instance via the same factory API. `Presentation` itself
  has no "recover from a destroyed surface" method — this is deliberately
  a caller-level object-lifetime decision, not a `Presentation` API
  surface question, so that Android's future implementation needs no
  change to `Presentation`'s shape.
- **Zero framebuffer extent (minimize, either platform, or a zero initial
  extent at construction):** never triggers a Vulkan swapchain call, on
  any path — see
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md).

### Threading

Single logical frame thread, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md): `Device`/
`Presentation` construction, recreation, and destruction all happen on
the same thread that owns the Windows Platform's message pump. This
spec's RHI interfaces are not required to be internally thread-safe, and
no thread, lock, or job/task system is introduced.

### Error handling

- Recoverable runtime errors (construction failure, unexpected `VkResult`
  during swapchain (re)creation) use `atlantis::Result<T, E>`, consistent
  with `specs/0001` and `specs/0002`'s existing convention — no exception
  is introduced anywhere in RHI or Vulkan Backend's public or private
  surface.
- Programmer errors (calling any RHI method before successful
  construction) use `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`.
- Every `VkResult` is checked; see Functional Requirements above and
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
  for exactly which paths recover vs. surface as an error.

### Frame completion belongs to a future RenderGraph spec

RenderGraph does not exist yet, and per [AGENTS.md](../AGENTS.md) no
subsystem may submit ad hoc, hand-scheduled GPU work outside it once it
does — not a full rendering pass, not a single pipeline barrier, and, as
this spec's own revision history shows, not even a "harmless" repeated
acquire used only to exercise recreation. Presenting a swapchain image
validation-cleanly requires that image to already be in a presentable
layout, which requires GPU work (at minimum an image layout transition)
recorded into and submitted via a command buffer — exactly the kind of
GPU work RenderGraph exists to orchestrate. Acquiring an image at all,
even without presenting it, takes on a synchronization obligation this
spec has nowhere safe to discharge without either presenting (needing the
transition above) or inventing an ad hoc "just drop it" convention that
would itself need to be reconciled with RenderGraph's real design later.

This spec therefore does not touch a swapchain image at all. It builds
and verifies `Presentation`'s non-frame lifecycle — Vulkan instance/
device/queue initialization, Windows WSI surface creation, swapchain
creation and safe destruction, swapchain metadata queries, zero-extent
handling, and resize-driven recreation via `notifyResized()`/
`recreateIfNeeded()` — and stops there. **The first acquire →
graph-recorded work/synchronization → present frame is bundled together
and left entirely to a later, approved RenderGraph specification and
plan** — see
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
"Deferred as One Bundle" section for exactly what that includes.
`Presentation` exposes only the non-frame lifecycle contract future work
will need; it does not define, and this spec does not build, any
alternate direct-submission path — nor any acquire-without-present
pattern — to reach a presented frame sooner.

## Architectural Impact

This spec introduces architecture and requires three new ADRs before it
can move from `In Review` to `Approved`, per [AGENTS.md](../AGENTS.md).
None of the following is decided by this spec's prose alone — each is
filed as its own ADR:

1. **RHI interface mechanism and Vulkan Backend's construction API** —
   how Runtime-equivalent code obtains a concrete `Device`/`Presentation`
   without RHI depending on Vulkan Backend or leaking `Vk*`/
   `NativeWindowHandle` into RHI's public surface. Resolves the mechanism
   [ADR-0001](../adr/0001-rhi-backend-independence.md) left open. Filed as
   [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)
   (`Accepted`).
2. **Vulkan GPU memory allocation strategy** — whether VMA or a
   hand-rolled suballocator is adopted now. Filed as
   [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)
   (`Accepted`) — resolved as an explicit, non-silent deferral with a
   stated future blocker, not a default pick.
3. **`Presentation` construction, recreation, and destruction lifecycle
   contract** — the concrete, non-frame interface shape operationalizing
   [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
   [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md), and
   [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md) into
   a buildable contract, implemented and verified in full by this spec.
   All frame-level API and synchronization (acquire, `RenderTarget`,
   `present()`, and the synchronization between them) is bundled and
   deferred to a future RenderGraph specification — see that ADR's
   "Deferred as One Bundle" section. Filed as
   [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
   (`Accepted`).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified by this spec or by the three new ADRs above — each new ADR
references and builds on the existing ones without altering them.

## Alternatives Considered

- **Build Renderer directly against Vulkan, skip a separate RHI
  abstraction.** Rejected: this is exactly the coupling
  [ADR-0001](../adr/0001-rhi-backend-independence.md) exists to prevent,
  and would force a rewrite the moment headless rendering or a second
  backend is ever specced.
- **Design RenderGraph in this same spec, alongside RHI.** Rejected:
  RenderGraph is Renderer's central abstraction and deserves its own
  focused spec and review; conflating the two risks smuggling Renderer/
  rendering-pass decisions into what should stay a narrow RHI/Presentation
  foundation.
- **Verify this spec with a minimal clear-screen or triangle demo, a bare
  acquire/transition/present loop, or a repeated acquire-without-present
  loop.** Rejected — see Proposed Design's "Frame completion belongs to a
  future RenderGraph spec" note above and
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  Alternatives Considered for the full reasoning on each; all three either
  submit GPU work outside RenderGraph or incur a synchronization
  obligation this spec has no safe way to discharge.
- **Skip windowed verification and go straight to a headless
  Device-creation test.** Rejected: contradicts
  [AGENTS.md](../AGENTS.md)'s explicit windowed-rendering-ships-first
  sequencing, and this spec's own goal is specifically the windowed
  `Presentation` path.
- **Decide the Vulkan memory allocator now, since VMA is the ecosystem
  standard.** Rejected here — see
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s own
  Alternatives Considered for the full reasoning.

## Testing & Verification Plan

- **Unit tests:** GPU-independent bookkeeping and validation logic (e.g.
  the zero-extent-skip decision in `recreateIfNeeded()`'s dispatch,
  `VkResult`-to-`Result::Err` mapping logic where it can be exercised
  without a real device) — per
  [testing-strategy.md](../docs/process/testing-strategy.md) layer 1, no
  Vulkan device required to run.
- **Headless integration tests:** not applicable in
  [testing-strategy.md](../docs/process/testing-strategy.md)'s current
  sense (layer 2 is specifically headless *rendering*, which does not
  exist yet). Whether `Device`/`Presentation` construction itself needs a
  new, GPU-required-but-no-window test category not currently named in
  that document is flagged under Risks & Open Questions, not resolved
  here.
- **Image regression tests:** not applicable — nothing is rendered.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of `Device`/`Presentation` construction,
  `notifyResized()`/`recreateIfNeeded()`, metadata queries, and
  destruction — per [AGENTS.md](../AGENTS.md) and
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- **Manual verification:** a minimal, non-shipping composition (see
  Non-Goals) creates a Windows Platform window (reusing the existing,
  implemented Windows Platform), constructs a `Device` and `Presentation`,
  and — driven by the existing non-blocking Platform event loop —
  exercises `notifyResized()`/`recreateIfNeeded()` and the metadata
  queries across interactive resize, minimize, and restore. **No
  `acquireNextTarget()`-shaped call, no `present()` call, and no command
  buffer of any kind is created anywhere in this verification.** It
  confirms: zero extent (including an initial zero extent, if the window
  starts minimized) correctly skips any Vulkan call; a resize correctly
  triggers recreation at the new extent, observable via the metadata
  queries; and `Presentation` is destroyed cleanly at any point in the
  sequence — construction, mid-resize, or after any number of
  `recreateIfNeeded()` calls — with **no acquired `RenderTarget` or
  swapchain image outstanding at any destruction point**, because none is
  ever acquired. Validation layers must remain clean throughout,
  consistent with the manual-verification bar
  `specs/0002-platform-foundation.md`'s Plan already established for
  Windows Platform itself.

## Acceptance Criteria

- [ ] RHI's public headers contain no `Vk*` type and no
      `#include <vulkan/...>` — verifiable by inspection/grep.
- [ ] `NativeWindowHandle` appears as a parameter only in Vulkan Backend's
      construction-API header(s) (per
      [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)),
      never in any RHI public header.
- [ ] `Presentation` does not declare, implement, or call an acquire
      operation, a `present()` operation, or any synchronization primitive
      (semaphore/fence) anywhere in this spec's implementation —
      verifiable by inspection.
- [ ] `RenderTarget` is not declared or referenced anywhere in this spec's
      implementation — verifiable by inspection.
- [ ] `recreateIfNeeded()` issues zero Vulkan swapchain-creation,
      -recreation, or -destruction calls when the tracked framebuffer
      extent is `{0, 0}` — true both for the first call after
      construction and for any later call — verifiable by code inspection
      of that path, not merely by a passing manual test.
- [ ] A Windows resize results in the swapchain being recreated at the new
      extent, observable via the metadata queries, with no Renderer-level
      code involved (none exists yet — verified by inspection that
      nothing in RHI/Vulkan Backend requires a Renderer to function).
- [ ] Every `VkResult` along the `Device`/`Presentation` construction,
      recreation, and destruction paths is checked; no `VkResult` is
      discarded, including ones "expected" to always succeed.
- [ ] Debug builds and any GPU-touching CI job run with Vulkan Validation
      Layers enabled; a validation warning or error fails the run.
- [ ] No draw call, clear-color command, graphics pipeline object
      (`VkPipeline`), image layout transition, or command buffer of any
      kind is created, recorded, or submitted anywhere by this spec's
      implementation or verification.
- [ ] No swapchain image, `RenderTarget`, or per-image resource is ever
      acquired, vended, or referenced anywhere in this spec's
      implementation or verification — and consequently, every
      `Presentation` destruction and recreation exercised by this spec's
      tests occurs with no acquired `RenderTarget` outstanding, because
      none is ever acquired at all.
- [ ] No `src/render_graph/`, `src/renderer/`, or Shader System source is
      created by this spec's implementation.
- [ ] No Android NDK build configuration, no second graphics backend, and
      no thread/job system is introduced anywhere this spec touches.
- [ ] All three ADRs listed in Architectural Impact
      ([ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
      [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
      [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md))
      reach `Accepted` before this spec is marked `Approved`.

## Risks & Open Questions

- Whether `Device`/`Presentation` construction needs its own new
  test-harness category — a real Vulkan device is required, but there is
  no window/swapchain in a pure Device-creation test, and no rendering
  occurs either way, so it fits neither
  [testing-strategy.md](../docs/process/testing-strategy.md)'s layer 1
  (must not require a device) nor layer 2 (headless *rendering*) as
  currently named. Flagged, not resolved — related to, but not solved by,
  that document's own already-open "test framework choice" and
  "real GPU vs. software Vulkan implementation" questions.
- Queue selection policy (a single combined graphics+present queue vs.
  separate queue families, and how a device with no suitable combined
  queue is handled) is not fixed by this spec's Goals; left to the Plan
  stage or a future `Device`-focused ADR amendment if it turns out to
  carry architectural weight beyond an implementation detail.
- The entire frame-level acquire/present/synchronization bundle — the
  acquire API, `RenderTarget`'s frame ownership, acquire-complete and
  graph-to-present synchronization, and image layout handoff — is
  deferred as a whole to the future RenderGraph specification, per
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  "Deferred as One Bundle" section. Not invented piecemeal here.
- Whether a `Presentation` whose swapchain was created at a non-zero
  extent should eagerly release that swapchain the moment extent becomes
  zero (rather than leaving it untouched until the next non-zero resize)
  is left open — not needed while nothing ever acquires from it, per
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  Alternatives Considered.
- Whether `Device` construction should support enumerating/choosing among
  multiple physical devices, or always select the first suitable one, is
  not decided here — Phase 1's own milestone does not need multi-adapter
  selection, but this spec does not want to silently foreclose it either.
  Left to the Plan stage or a future amendment to
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)
  if it turns out to matter before then.
- Whether validation-layer configuration (which layers/extensions,
  message severity filtering) needs its own small decision record or is
  purely an implementation detail is left to the Plan stage.

## Out of Scope / Future Work

RenderGraph, Renderer, Shader System, the Atlantis Runtime module's own
spec, headless rendering, image regression testing, Android/iOS Vulkan
implementation, a second graphics backend, GPU-driven rendering, and
multi-threaded command recording are all future spec scope, per
[AGENTS.md](../AGENTS.md) Phase 1 constraints and this document's
Non-Goals above. This spec's `Presentation` non-frame lifecycle
foundation is what those future specs will build on; it does not itself
unblock drawing anything. In particular, the entire frame-level
acquire/present/synchronization bundle — the first acquire →
graph-recorded work/synchronization → present cycle — is the future
RenderGraph specification and plan's work to design and build, not this
one's, and this spec deliberately does not pre-sketch any part of it.
