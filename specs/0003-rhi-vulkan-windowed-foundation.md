# Spec: Atlantis RHI and Vulkan Windowed Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code at explicit human direction; the original
  author metadata left human authorship/ownership confirmation pending.
- **Created:** 2026-08-06
- **Related Plan(s):** [Plan 0003](../plans/0003-rhi-vulkan-windowed-foundation.md)
  (`Approved`). Joint Spec + Plan Human Review completed 2026-08-08;
  implementation merged via [PR #14](https://github.com/slmao/Atlantis/pull/14).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md),
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
  [ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
  [ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (amended),
  [ADR-0011](../adr/0011-native-window-handle-representation.md),
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md). Three new
  decisions in **Architectural Impact** were filed as
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md);
  all `Accepted`, confirmed alongside this Spec on 2026-08-06.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #147](https://github.com/slmao/Atlantis/pull/147) Batch 1. Original scope
  and obligations retained.

## Summary

Establishes the minimal, backend-agnostic **RHI** (Render Hardware Interface)
boundary a future RenderGraph and Renderer are built against, and the **Vulkan
Backend's** Windows-windowed `Device`/`Presentation` foundation that is Phase
1's sole implementation of that boundary. It is **not** a Renderer, a
rendering-pass design, or a graphics-pipeline design. Scope stops at
`Presentation`'s **non-frame lifecycle**: Vulkan instance/device/queue
initialization, Windows WSI surface creation, swapchain creation and safe
destruction, swapchain metadata queries, zero-extent handling, and
resize-driven lazy recreation. **`Presentation` never acquires, vends, or
tracks a swapchain image anywhere in this Spec** — acquiring a frame, preparing
it via graph-recorded work, and presenting it are bundled together and left
entirely to a later, approved RenderGraph specification and plan. This Spec
records, schedules, and submits no GPU command and incurs no per-image
synchronization obligation.

## Motivation / Problem Statement

`Atlantis Core` (Spec 0001) and Windows Platform (Spec 0002) are implemented.
Nothing above them exists. Renderer cannot be specced or built directly — it
depends on RHI and RenderGraph, neither of which exists, and building a
"renderer" without them would either invent RHI/RenderGraph implicitly or
bypass RenderGraph with an ad hoc direct submission path (forbidden). Three
architectural questions are flagged open across the accepted architecture-
baseline ADRs and `docs/architecture/` but not yet resolved into a buildable
interface:

- **How** does Runtime obtain a concrete `Device`/`Presentation` without RHI
  depending on Vulkan Backend, and without leaking `Vk*` types into RHI's
  public surface? ([ADR-0001](../adr/0001-rhi-backend-independence.md) flags
  this mechanism as undecided.)
- **What** does `Presentation`'s non-frame lifecycle contract look like, and
  how does it honor the `Accepted` rule that a zero-extent window must never
  trigger swapchain creation/recreation
  ([ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md),
  Spec 0002's Ownership and Lifetime table) — without inventing a frame-level
  acquire/present contract there is no RenderGraph to validate against yet?
- **Whether** a GPU memory suballocation strategy (VMA vs. hand-rolled) is
  needed now, and if not, how that is prevented from being decided implicitly
  by whichever code needs an allocation first.

This Spec answers exactly these questions — and no others — so a future
RenderGraph/Renderer Spec inherits a settled, non-frame RHI boundary.

## Goals

- Define the minimal RHI public interface for `Device`/`Presentation`
  construction and `Presentation`'s non-frame lifecycle (recreation,
  destruction, metadata queries) — implemented and verified here.
- **Do not** define, sketch, or illustrate an acquire/present frame-level API.
  That API, `RenderTarget`'s frame-ownership shape, and every synchronization
  detail it implies are bundled and left to the future RenderGraph
  specification — see
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  "Deferred as One Bundle".
- Implement `Presentation`'s non-frame lifecycle on Windows only, using Vulkan
  as Phase 1's sole backend, reusing the implemented `Atlantis Platform`
  Windows path for window/surface handle production — no changes to Platform's
  public API.
- Make the zero-framebuffer-extent rule
  ([ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md)) a
  structural property of swapchain (re)creation, applying uniformly to the
  first swapchain and every later recreation — not caller discipline.
- Resolve the three Motivation questions via
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md).
- Verify `Presentation`'s non-frame lifecycle end-to-end on a real Windows
  machine with a real GPU, Vulkan Validation Layers clean, using a verification
  story that never acquires, vends, or references a swapchain image (so it
  never risks image-pool exhaustion or an undefined-synchronization
  destruction).

## Non-Goals

Explicitly deferred to future Specs — not designed, sketched, or implemented
here:

- **RenderGraph** — pass declaration, resource dependency tracking,
  barrier/lifetime resolution, execution ordering. This Spec performs no GPU
  work that would need a graph to orchestrate: no image layout transition, no
  command buffer recording, no command submission of any kind.
- **Renderer** — frame orchestration, scene/material submission, any concept of
  a "draw". No draw call, clear-color command, graphics pipeline (`VkPipeline`)
  object, layout transition, or command buffer.
- **Any acquire-shaped operation, `present()`, and everything they imply** —
  not implemented, called, tested, or even declared with a concrete signature
  ([ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)).
  Excluded together as one bundle: any frame-level acquire operation and the
  `RenderTarget` it would vend; acquire-complete / render-finished semaphores
  or fences; image layout transitions; command buffer recording or submission;
  any "empty frame", "acquire → present", or "no-render present" verification
  path, including a single, repeated, or rate-limited acquire-without-present
  pattern (it exhausts the swapchain image pool and can block or fail;
  destroying a `Presentation` with an outstanding acquired image is an
  undefined-synchronization shortcut). This Spec's verification never acquires
  a swapchain image at all.
- **Shader System** — no shader authoring, compilation, or reflection.
- **Atlantis Runtime (the module)** — verification uses a minimal, non-shipping
  composition (like `examples/foundation_demo`/`platform_demo`) and is not
  Runtime. Runtime's own responsibilities remain a future Spec's scope.
- **Resource loading / general `Buffer`/`Texture` creation** — no
  general-purpose GPU resource is allocated; see
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md).
- **Headless rendering, image regression testing** — Phase 1 ships windowed
  first; this Spec is part of that path.
- **Android NDK build / Android Vulkan Backend** — architecturally anticipated
  (interfaces must not preclude it) but not implemented; no Android build
  configuration.
- **iOS, MoltenVK, or a native Metal RHI backend.**
- **A second Vulkan-capable graphics backend**, and no abstraction knob added
  "for" one.
- **GPU-driven rendering, multi-threaded command recording/submission, or any
  job/task system** — Phase 1's single-logical-frame-thread baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is unchanged.
- **A general Vulkan memory suballocator (VMA or hand-rolled)** — see
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md).

## Requirements

### Functional

- A `Device` RHI interface is constructible, on Windows, via Vulkan Backend's
  construction API: it selects a suitable physical device and creates a logical
  `VkDevice` with at least one queue capable of both graphics and presentation.
- A `Presentation` RHI interface is constructible from a `Device` and the
  `NativeWindowHandle` the Windows Platform implementation produces (via
  `SurfaceCreated`), creating a `VkSurfaceKHR` through the private WSI boundary
  [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (as amended)
  establishes. **Construction creates the surface only, not a swapchain.**
- `Presentation` exposes `notifyResized(WindowExtent)` (updates the tracked
  extent, makes no Vulkan call) and `recreateIfNeeded()` (the sole operation
  that creates, recreates, or destroys the `VkSwapchainKHR`, per
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)).
  **When the tracked framebuffer extent is `{0, 0}`, `recreateIfNeeded()`
  issues no Vulkan swapchain call whatsoever** — structurally (verifiable by
  code inspection), on the first call after construction and any later call.
- `Presentation` exposes read-only swapchain metadata queries (image count,
  format, current extent) reflecting the most recently (re)created swapchain.
  **These queries never hand out an image handle, a `RenderTarget`, or any
  per-image resource.**
- Observing a `WindowResize` `PlatformEvent`, calling `notifyResized()`, then
  `recreateIfNeeded()` recreates the swapchain at the new extent, observable
  via the metadata queries.
- `Presentation` can be destroyed safely at any point in that sequence — no
  swapchain image is ever acquired or outstanding, so destruction has no
  synchronization precondition.
- Every `VkResult` along the construction, recreation, and destruction paths is
  checked; recoverable swapchain-creation failures are surfaced through
  `atlantis::Result`, never silently discarded.
- Vulkan Validation Layers are enabled unconditionally in Debug builds and any
  GPU-touching CI job; a validation warning or error is a build/test failure,
  not advisory output — see
  [definition-of-done.md](../docs/process/definition-of-done.md).
- Programmer errors (e.g. calling any RHI method before successful
  construction) use `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin". No
  frame-pacing or performance target.
- **Memory:** no GPU memory suballocation strategy is introduced or assumed
  ([ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)); host-side
  allocation uses ordinary RAII.
- **Portability:** implemented and verified on Windows only. The RHI public
  interface must not preclude Android's future implementation — verified by
  inspection (no Windows type in any RHI public header), not by building an
  Android target.
- **Other:** no new third-party dependency beyond the already-categorized
  Vulkan SDK/loader ([ADR-0006](../adr/0006-dependency-management.md)).

## Proposed Design

### Module boundaries (this Spec's slice within them)

This Spec moves no existing module boundary — it fills in the previously
undecided mechanism inside boundaries
[ADR-0001](../adr/0001-rhi-backend-independence.md),
[ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) (amended), and
[module_boundaries.md](../docs/architecture/module_boundaries.md) already fixed:

```
Atlantis Platform (existing, Windows-implemented)
  --SurfaceCreated{ NativeWindowHandle }-->
Runtime-equivalent composition (this Spec's minimal verification demo; NOT Runtime)
  --NativeWindowHandle, by value, uninterpreted-->
Vulkan Backend's construction API (ADR-0014)
  --consumes NativeWindowHandle only inside its private WSI boundary (ADR-0005 amended)-->
  VkSurfaceKHR (private to Vulkan Backend)
  -->
RHI's public Device / Presentation interfaces
  (backend-agnostic; zero Vk*, zero OS types, zero NativeWindowHandle;
  no RenderTarget vended anywhere in this Spec)
```

`NativeWindowHandle` crosses exactly one boundary beyond Platform/Runtime: into
Vulkan Backend's construction API. It never reaches generic RHI's public
`Presentation` interface
([ADR-0011](../adr/0011-native-window-handle-representation.md)).

### Object model

- **`Device`** (RHI public interface, Vulkan Backend concrete implementation) —
  a logical GPU device and its queues. Constructed once via Vulkan Backend's
  factory API
  ([ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)).
  No window/surface knowledge. Owned by whoever constructs it.
- **`Presentation`** (RHI public interface, Vulkan Backend `VkSwapchainKHR`-backed
  implementation) — the swapchain-backed drawable-surface abstraction
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md) established,
  scoped here to its **non-frame** lifecycle only. Constructed from a `Device`
  and a `NativeWindowHandle` (surface only, no swapchain yet). Owns its
  swapchain once one exists. Implements and this Spec verifies
  `notifyResized()`, `recreateIfNeeded()`, and the metadata queries; never
  touches Vulkan at zero extent. **Declares, implements, and exposes no acquire
  or present operation, and vends no `RenderTarget` or image handle** — see
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md).
- **`RenderTarget`** — **not defined, declared, or referenced anywhere in this
  Spec's implementation.** It only has meaning as "the thing a frame-level
  acquire vends", and acquire is deferred in full; its concrete shape is left
  to the future RenderGraph specification, consistent with
  [ADR-0002](../adr/0002-presentation-rendertarget-unification.md).
- **`VkSurfaceKHR`, `VkSwapchainKHR`, and every other `Vk*` type** — fully
  private to Vulkan Backend's implementation and its WSI boundary. None crosses
  into any RHI public header.

### Windows resize vs. future Android surface destruction

- **Windows resize:** Runtime-equivalent code observes `WindowResize`, calls
  `Presentation::notifyResized()` then `recreateIfNeeded()` — the same
  `Presentation` object recreates its swapchain in place; the `VkSurfaceKHR` is
  not necessarily recreated. Verified here entirely through those calls and the
  metadata queries — no acquire, present, or command buffer.
- **Android surface destroyed/recreated (anticipated, not implemented or tested
  here):** per
  [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md) and
  `docs/architecture/resource_lifetime.md`, a new `SurfaceCreated` handle is
  never guaranteed to reference the same object. `Presentation`'s
  object-lifetime model treats this as full teardown and reconstruction, not an
  in-place resize: on `SurfaceDestroyed` the owning code destroys its
  `Presentation` instance; on a later `SurfaceCreated` it constructs a new one
  via the same factory API. `Presentation` has no "recover from a destroyed
  surface" method — a deliberate caller-level object-lifetime decision, so
  Android's future implementation needs no change to `Presentation`'s shape.
- **Zero framebuffer extent** (minimize, either platform, or a zero initial
  extent at construction): never triggers a Vulkan swapchain call, on any path
  ([ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)).

### Threading

Single logical frame thread
([ADR-0004](../adr/0004-phase1-threading-baseline.md)): `Device`/`Presentation`
construction, recreation, and destruction all happen on the thread that owns
the Windows Platform message pump. The RHI interfaces are not required to be
internally thread-safe; no thread, lock, or job/task system is introduced.

### Error handling

- Recoverable runtime errors (construction failure, unexpected `VkResult`
  during swapchain (re)creation) use `atlantis::Result<T, E>` — no exception
  anywhere in RHI or Vulkan Backend's public or private surface.
- Programmer errors use `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`.
- Every `VkResult` is checked; see
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)
  for which paths recover vs. surface as an error.

### Frame completion belongs to a future RenderGraph Spec

RenderGraph does not exist yet, and per [AGENTS.md](../AGENTS.md) no subsystem
may submit ad hoc, hand-scheduled GPU work outside it once it does — not a full
pass, not a single pipeline barrier, and not a "harmless" repeated acquire.
Presenting a swapchain image validation-cleanly requires that image to be in a
presentable layout, which requires GPU work recorded into and submitted via a
command buffer. Acquiring an image at all takes on a synchronization obligation
this Spec has nowhere safe to discharge. This Spec therefore does not touch a
swapchain image: it builds and verifies `Presentation`'s non-frame lifecycle
and stops there. **The first acquire → graph-recorded work/synchronization →
present frame is bundled and left entirely to a later, approved RenderGraph
specification and plan** — see
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
"Deferred as One Bundle". This Spec builds no alternate direct-submission path
and no acquire-without-present pattern to reach a presented frame sooner.

## Architectural Impact

Introduces architecture; three new ADRs were required to be `Accepted` before
`Approved`. None is decided by this Spec's prose. All are `Accepted`.

| Decision | ADR |
|---|---|
| RHI interface mechanism and Vulkan Backend's construction API — how Runtime-equivalent code obtains a concrete `Device`/`Presentation` without RHI depending on Vulkan Backend or leaking `Vk*`/`NativeWindowHandle`; resolves the mechanism [ADR-0001](../adr/0001-rhi-backend-independence.md) left open | [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md) |
| Vulkan GPU memory allocation strategy — resolved as an explicit, non-silent deferral with a stated future blocker, not a default pick | [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md) |
| `Presentation` construction, recreation, and destruction lifecycle contract — the concrete non-frame interface operationalizing [ADR-0002](../adr/0002-presentation-rendertarget-unification.md)/[ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md)/[ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md); all frame-level API and synchronization bundled and deferred to a future RenderGraph specification | [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md) |

No existing `Accepted` ADR's conclusions are restated, reopened, or modified by
this Spec or the three new ADRs.

## Alternatives Considered

- **Build Renderer directly against Vulkan, skip a separate RHI abstraction.**
  Rejected: the coupling [ADR-0001](../adr/0001-rhi-backend-independence.md)
  exists to prevent; would force a rewrite the moment headless rendering or a
  second backend is specced.
- **Design RenderGraph in this same Spec.** Rejected: RenderGraph deserves its
  own focused Spec; conflating risks smuggling Renderer/pass decisions into a
  narrow RHI/Presentation foundation.
- **Verify with a minimal clear-screen or triangle demo, a bare
  acquire/transition/present loop, or a repeated acquire-without-present loop.**
  Rejected — see "Frame completion belongs to a future RenderGraph Spec" and
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  Alternatives: all either submit GPU work outside RenderGraph or incur a
  synchronization obligation with no safe discharge.
- **Skip windowed verification, go straight to a headless Device-creation
  test.** Rejected: contradicts [AGENTS.md](../AGENTS.md)'s
  windowed-rendering-ships-first sequencing, and the goal is specifically the
  windowed `Presentation` path.
- **Decide the Vulkan memory allocator now.** Rejected — see
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s Alternatives.

## Testing & Verification Plan

- **Unit tests:** GPU-independent bookkeeping and validation logic (the
  zero-extent-skip decision, `VkResult`-to-`Result::Err` mapping) — no Vulkan
  device required (testing-strategy.md layer 1).
- **Headless integration tests:** not applicable in
  [testing-strategy.md](../docs/process/testing-strategy.md)'s current sense
  (layer 2 is headless *rendering*). Whether `Device`/`Presentation`
  construction needs a new GPU-required-but-no-window test category is flagged
  under Risks, not resolved here.
- **Image regression tests:** not applicable — nothing rendered.
- **Vulkan Validation Layers:** mandatory and must run clean for every manual
  and automated exercise of construction, `notifyResized()`/
  `recreateIfNeeded()`, metadata queries, and destruction — per
  [AGENTS.md](../AGENTS.md) and
  [definition-of-done.md](../docs/process/definition-of-done.md).
- **Manual verification:** a minimal, non-shipping composition creates a
  Windows Platform window, constructs a `Device` and `Presentation`, and —
  driven by the existing non-blocking Platform event loop — exercises
  `notifyResized()`/`recreateIfNeeded()` and the metadata queries across
  interactive resize, minimize, and restore. **No acquire-shaped call, no
  `present()`, and no command buffer anywhere.** It confirms: zero extent
  (including an initial zero extent) skips any Vulkan call; a resize triggers
  recreation at the new extent, observable via the metadata queries;
  `Presentation` is destroyed cleanly at any point with no acquired
  `RenderTarget` or swapchain image outstanding, because none is ever acquired.
  Validation layers must stay clean throughout.

## Acceptance Criteria

Original obligations remain unmarked; this editorial revision does not certify
historical execution.

- [ ] RHI's public headers contain no `Vk*` type and no
      `#include <vulkan/...>`.
- [ ] `NativeWindowHandle` appears as a parameter only in Vulkan Backend's
      construction-API header(s), never in any RHI public header.
- [ ] `Presentation` does not declare, implement, or call an acquire operation,
      a `present()` operation, or any synchronization primitive
      (semaphore/fence) anywhere in this Spec's implementation.
- [ ] `RenderTarget` is not declared or referenced anywhere in this Spec's
      implementation.
- [ ] `recreateIfNeeded()` issues zero Vulkan swapchain-creation, -recreation,
      or -destruction calls when the tracked framebuffer extent is `{0, 0}` —
      first call and any later call — verifiable by code inspection of that
      path.
- [ ] A Windows resize recreates the swapchain at the new extent, observable
      via the metadata queries, with no Renderer-level code involved.
- [ ] Every `VkResult` along the construction, recreation, and destruction
      paths is checked; none discarded, including ones "expected" to succeed.
- [ ] Debug builds and any GPU-touching CI job run with Vulkan Validation
      Layers enabled; a validation warning or error fails the run.
- [ ] No draw call, clear-color command, `VkPipeline`, image layout transition,
      or command buffer of any kind is created, recorded, or submitted anywhere
      by this Spec's implementation or verification.
- [ ] No swapchain image, `RenderTarget`, or per-image resource is ever
      acquired, vended, or referenced — so every `Presentation` destruction and
      recreation the tests exercise occurs with none outstanding.
- [ ] No `src/render_graph/`, `src/renderer/`, or Shader System source is
      created.
- [ ] No Android NDK build configuration, second graphics backend, or
      thread/job system is introduced.
- [ ] All three ADRs
      ([ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
      [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
      [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md))
      reach `Accepted` before this Spec is marked `Approved`.

## Risks & Open Questions

- Whether `Device`/`Presentation` construction needs its own new test-harness
  category — a real Vulkan device is required, but there is no window/swapchain
  and no rendering, so it fits neither
  [testing-strategy.md](../docs/process/testing-strategy.md)'s layer 1 (must
  not require a device) nor layer 2 (headless *rendering*). Flagged, not
  resolved.
- Queue selection policy (single combined graphics+present queue vs. separate
  families, and handling a device with no suitable combined queue) — left to
  the Plan or a future `Device`-focused ADR amendment.
- The entire frame-level acquire/present/synchronization bundle is deferred as
  a whole to the future RenderGraph specification
  ([ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md)'s
  "Deferred as One Bundle"). Not invented piecemeal here.
- Whether a `Presentation` whose swapchain was created at non-zero extent
  should eagerly release it when extent becomes zero — left open; not needed
  while nothing ever acquires from it.
- Whether `Device` construction should support choosing among multiple physical
  devices, or always select the first suitable one — not decided here; left to
  the Plan or a future amendment to
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md).
- Whether validation-layer configuration (which layers/extensions, severity
  filtering) needs its own decision record or is purely an implementation
  detail — left to the Plan.

## Out of Scope / Future Work

RenderGraph, Renderer, Shader System, the Atlantis Runtime module's own Spec,
headless rendering, image regression testing, Android/iOS Vulkan
implementation, a second graphics backend, GPU-driven rendering, and
multi-threaded command recording are all future Spec scope, per
[AGENTS.md](../AGENTS.md) Phase 1 constraints and the Non-Goals above. This
Spec's `Presentation` non-frame lifecycle foundation is what those future Specs
build on; it does not itself unblock drawing anything. In particular, the
entire frame-level acquire/present/synchronization bundle is the future
RenderGraph specification and plan's work to design and build, and this Spec
deliberately does not pre-sketch any part of it.
