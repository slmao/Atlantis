# ADR 0022: Minimal Renderer Public API, Module Boundary, and Resource Ownership

- **Status:** Accepted
- **Date:** 2026-08-11
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-11; see
  [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)'s
  Human Review Approval note for the full approval record this ADR's
  Decision is part of.
- **Related Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md) (`Approved`)

## Context

`docs/architecture/module_boundaries.md` and `resource_lifetime.md` have,
since the architecture-baseline documentation task, fixed *principles*
for the future Renderer module: it depends only on RHI, RenderGraph, and
Core ([ADR-0001](0001-rhi-backend-independence.md)); it never owns a
`RenderTarget`, only borrows one per frame
([ADR-0002](0002-presentation-rendertarget-unification.md),
[ADR-0003](0003-resource-rendertarget-ownership-model.md)); its own
public surface is explicitly left undesigned ("scene representation,
material system, and any higher-level submission API are not designed
yet"). No spec has ever instantiated these principles into a concrete
public API, because no spec before this one has had a real consumer
(a mesh, a camera, a material) to validate a shape against.

Spec 0007 is that consumer. It needs `Renderer` to be a real C++ type
with a real per-frame call contract before `src/renderer/` can exist at
all — this is exactly the kind of "settled implicitly by whichever code
gets written first" gap [AGENTS.md](../AGENTS.md)'s Golden Rule exists to
close.

A second, related gap: nothing has yet fixed who owns a `Mesh`, a
`Material`, or per-frame camera data, or how (if at all) they may be
shared across multiple draws. [ADR-0003](0003-resource-rendertarget-ownership-model.md)
already fixed the *principle* for RHI resources generally ("explicitly
owned by whoever creates them... no hidden global resource cache or
implicit refcounted pool inside RHI itself in Phase 1"), but Renderer-
level concepts like `Mesh`/`Material` are new types this spec introduces,
not raw RHI resources — that principle needs to be applied to them
explicitly, not assumed to carry over silently.

A third gap: a depth attachment is a GPU resource with no natural owner
yet. `Presentation` owns the swapchain-backed color image
([ADR-0016](0016-presentation-acquire-present-and-recreation-contract.md)),
but nothing owns an equivalent depth image, and nothing has decided who
is responsible for recreating it when the window resizes.

## Decision

**`Renderer` is a concrete class, not an RHI-style abstract interface.**
Unlike `Device`/`Presentation`/`RenderTarget` — which exist because a
second Vulkan Backend implementation is a real, if not-yet-built, Phase 1
extension point ([ADR-0014](0014-rhi-device-presentation-construction-boundary.md))
— `Renderer` has no backend variance of its own to abstract: it is
already fully backend-independent by construction, because it is built
entirely on top of RHI's and RenderGraph's already-abstract interfaces.
Introducing a second `Renderer` abstraction layer on top of that would be
speculative — there is no concrete second implementation this round, or
any known future one, that a `Renderer` interface would exist to swap in.

- New module `src/renderer/`, target `atlantis_renderer`, alias
  `Atlantis::Renderer`. Depends only on `Atlantis::RHI`,
  `Atlantis::RenderGraph`, `Atlantis::Core` — exactly the dependency set
  [module_boundaries.md](../docs/architecture/module_boundaries.md)
  already fixed, now realized rather than merely anticipated. No `Vk*`
  type, no Win32/Android NDK type, no Atlantis Platform type, and no
  Vulkan Backend reference anywhere in `src/renderer/`'s public or
  private surface — the same structural rule already enforced for
  RenderGraph, verified the same way (grep/inspection).
- **`Renderer` never owns a `RenderTarget`, a depth texture, a `Mesh`, or
  a `Material`.** Every per-frame call takes these as borrowed references
  supplied by its caller (this spec's own verification composition; a
  future Runtime). `Renderer` retains no GPU resource, and no
  frame-to-frame state, across calls — it is a stateless orchestrator over
  caller-supplied inputs, matching AGENTS.md's "Renderer does not
  fundamentally depend on Window, Platform, or Swapchain... consumes
  RHI + Render Graph + a RenderTarget handed to it by its caller, nothing
  more" principle exactly, extended to the depth/mesh/material inputs this
  spec adds.
- **`Mesh` and `Material` are explicitly-owned, caller-held types** —
  RAII, single-owner, non-copyable (movable), the same "explicit
  ownership, no hidden global cache" principle
  [ADR-0003](0003-resource-rendertarget-ownership-model.md) already fixed
  for RHI resources, applied here to these two new Renderer-level
  aggregates. `Mesh` owns the vertex and index `Buffer`s
  ([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)) it
  was constructed from; `Material` owns the `Pipeline`
  ([ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
  it was constructed from. Neither is created, looked up, deduplicated, or
  cached by `Renderer` itself — `Renderer` never has a factory method that
  returns a `Mesh`/`Material` by name or ID, and never keeps a
  name-to-resource map of any kind. A caller that wants to draw the same
  `Mesh` (or reuse the same `Material`) more than once in a frame does so
  by passing the same borrowed reference to `Renderer`'s per-frame call
  more than once — this is the *only* sharing mechanism this round
  provides, and it requires no new ownership machinery: it is ordinary
  reference reuse, not a cache. Genuine shared ownership across multiple
  independent owners (e.g. a future asset system holding a
  reference-counted `Mesh`) is out of this decision's scope entirely — see
  Alternatives Considered.
- **`Renderer`'s per-frame contract** takes: the caller-acquired
  `RenderTarget` (color), a caller-owned depth `Texture`
  ([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)), a
  reference to the caller-owned, caller-written camera uniform `Buffer`
  ([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)) —
  **not a raw camera-data value** — and a caller-owned collection of draw
  items (each: a `Mesh` reference, a `Material` reference, and an
  object-to-world transform). The camera `Buffer` reference, not a value,
  is deliberate: the caller writes that frame's view/projection matrices
  directly into the `Buffer`'s mapped memory *before* calling `Renderer`
  (see [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  for why this write is safe at that point), and `Renderer`'s internally-
  built RenderGraph pass callback binds that same `Buffer` during
  recording — `Renderer` never touches raw camera matrices itself, only
  binds a `Buffer` reference, consistent with every other input to this
  contract being a borrowed reference to something the caller already
  owns and already populated. Unlike the depth `Texture` (recreated on
  resize), the camera `Buffer`'s size never changes with window extent, so
  the caller creates it once and never needs to recreate it for a resize.
  The draw-item collection is an ordinary caller-supplied parameter
  (e.g. `std::span<const DrawItem>`), not a scene graph, an entity system,
  or any persistent registration API — `Renderer` iterates it once per
  call and retains nothing from it afterward. Internally, `Renderer`
  builds a `RenderGraphBuilder` description for the frame (one pass this
  round is sufficient — see Non-Goals in
  [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)),
  compiles it, and calls `render_graph::execute()` into the
  caller-provided `CommandList` — `Renderer` itself never calls
  `Device::submit()` or `Presentation::present()`, exactly mirroring
  RenderGraph's own "records, does not submit or present" boundary
  ([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)).
  Acquire, submit, and present remain the caller's explicit responsibility,
  unchanged from Spec 0006's contract.
- **Depth-texture ownership and resize responsibility belong to the
  caller, not to `Renderer` or to `Presentation`.** A depth attachment has
  no natural owner in the existing RHI/Vulkan Backend module boundary
  ([ADR-0016](0016-presentation-acquire-present-and-recreation-contract.md)'s
  scope is the color swapchain image only) — this decision fixes that gap
  by extending, not contradicting,
  [ADR-0003](0003-resource-rendertarget-ownership-model.md)'s existing
  rule ("RHI resources are explicitly owned by whoever creates them
  through RHI"): whoever creates the depth `Texture` through RHI (the
  caller) is responsible for recreating it whenever its extent no longer
  matches the newly-acquired `RenderTarget`'s extent, before calling
  `Renderer`'s per-frame draw. This mirrors exactly how the caller already
  owns `Presentation::notifyResized()` timing today, and keeps `Renderer`
  itself free of any resize-driven internal state or lifecycle logic. See
  [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md) for
  `Texture`'s own creation/destruction contract.
- **Attachment format change carries the same caller-owned, `Renderer`-
  free responsibility, extended to `Material`'s owned `Pipeline`.** Human
  Review (2026-08-11) confirmed this explicitly, as its own concrete
  contract rather than an open risk — see
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  for the full decision (how the caller observes a format change via
  `Presentation::metadata()`, why an extent-only change and a format
  change are handled differently, and the `Device::waitIdle()` sequencing
  required before recreating a format-dependent resource). This ADR's own
  contribution is only the ownership half already stated above: `Renderer`
  plays no role in detecting or acting on a format change, exactly as it
  plays none for the depth `Texture`'s extent.

## Consequences

### Positive

- Gives `src/renderer/` a real, reviewed shape before any implementation,
  closing the last "not designed yet" gap
  [module_boundaries.md](../docs/architecture/module_boundaries.md) left
  open for this module.
- Extending, rather than reinterpreting, every already-`Accepted`
  ownership/dependency principle (ADR-0001/0002/0003) means none of those
  decisions are reopened — this ADR only makes them concrete.
- Explicit, no-cache ownership for `Mesh`/`Material` keeps resource
  lifetime exactly as predictable and debuggable as
  [ADR-0003](0003-resource-rendertarget-ownership-model.md) already
  established for `RenderTarget` — a resource is alive exactly as long as
  its owning handle is.
- Pinning depth-texture resize responsibility on the caller (not
  `Renderer`) keeps `Renderer` stateless and trivially safe to construct/
  destroy at any point — there is no internal recreation bookkeeping to
  reason about at `Renderer`'s own destruction.

### Negative / Trade-offs

- The caller (this spec's own verification composition; a future Runtime)
  now carries real per-frame bookkeeping — checking the depth texture's
  extent against the newly-acquired `RenderTarget`'s extent every frame —
  that a `Renderer`-internal design would have hidden. Accepted as the
  cost of keeping `Renderer` itself free of resize-lifecycle state.
- No cross-owner resource sharing (e.g. two independent systems both
  wanting a reference to the same `Mesh`) is possible without inventing a
  new shared-ownership handle type — deliberately deferred, not designed
  here; a future spec must add one if a real need appears.
- A concrete `Renderer` class (not an interface) means a hypothetical
  future second Renderer implementation (unlikely, and not anticipated by
  any current spec) would require a new abstraction layer to be retrofitted
  — accepted because no such second implementation is a real Phase 1 (or
  currently-known future-phase) need.

## Alternatives Considered

- **Make `Renderer` an RHI-style abstract interface with a Vulkan-Backend-
  provided (or otherwise "concrete") implementation**, mirroring
  `Device`/`Presentation`. Rejected: `Renderer` has no second
  implementation to abstract for — it is already backend-independent
  because everything it touches (RHI, RenderGraph) already is. Adding a
  vtable interface here would be speculative abstraction with no concrete
  consumer, the exact failure mode AGENTS.md's "No speculative
  abstraction" principle warns against.
- **Give `Renderer` its own internal `Mesh`/`Material` registry (a
  create-by-name or create-and-cache API), so callers don't have to manage
  handles themselves.** Rejected: this is precisely the "hidden global
  resource cache" [ADR-0003](0003-resource-rendertarget-ownership-model.md)
  already ruled out for RHI resources generally; extending that rule to
  Renderer-level aggregates, rather than quietly reintroducing a cache one
  layer up, keeps the whole stack's ownership story consistent.
- **Have `Renderer` own and internally recreate the depth texture**, keyed
  off the `RenderTarget` extent it's handed each frame. Considered — it is
  more convenient for callers. Rejected for this round: it would make
  `Renderer` stateful and resize-aware in a way nothing else about it is,
  contradicting AGENTS.md's "Renderer does not fundamentally depend on...
  Swapchain" principle by proxy (depth-texture recreation is exactly the
  kind of presentation-adjacent lifecycle logic that principle exists to
  keep out of Renderer). The caller already owns equivalent
  resize-response logic for `notifyResized()`; extending that pattern to
  the depth texture is more consistent than inventing a second, different
  resize-response owner inside `Renderer`.
- **A scene-graph or entity/component submission API**, rather than a
  plain per-frame draw-item collection. Rejected outright — explicitly out
  of this spec's Non-Goals; no ECS or scene representation is chosen
  anywhere in this codebase yet.
- **A reference-counted/shared-ownership `Mesh`/`Material` handle type**
  (e.g. via a new Core-provided shared-pointer-like wrapper), to directly
  support cross-owner sharing. Rejected for this round: no concrete
  consumer needs it yet (this spec's own acceptance target needs at most a
  handful of draw items sharing borrowed references within one frame, not
  cross-owner sharing across independent systems); a future spec can add
  one once a real need — likely an asset system — motivates its exact
  shape.

## Accepted Amendment — 2026-08-15

**Status of this section:** drafted alongside
[specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)'s
own revision, following an independent Human Review of that spec's first
draft, and **formally accepted by Human Review on 2026-08-16** — see
"Human Review — Amendment Acceptance (2026-08-16)" immediately below for
the full approval record. **This section does not itself change this
ADR's top-level Status** (`Accepted` above is unchanged — it remains the
accurate record of what Spec 0007 shipped and how `Renderer::drawFrame()`
behaved prior to this amendment). The original Decision, Consequences,
and Alternatives Considered above are preserved verbatim and are not
superseded except where this section says so explicitly.

### Human Review — Amendment Acceptance (2026-08-16)

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-08-16; see
[specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)'s
own Human Review Approval note for the full, three-round approval record
this amendment's acceptance is part of (decision 1 of the eight
confirmed decisions listed there). This amendment (the `finalColorState`
parameter, its pass-through-only contract, and its windowed/headless
legal values) is accepted exactly as drafted in this section — no
further revision was required of this amendment specifically across any
of the three review rounds; the rounds' findings and fixes landed in
[ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md),
[ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md),
and [ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md) instead. No
further Human Review is pending for this amendment.

### What prompted this amendment

Spec 0010's first draft (and this amendment's own predecessor text)
incorrectly assumed `Renderer::drawFrame()` requires zero changes to
support headless rendering, and incorrectly asserted that
`Renderer::drawFrame()` leaves its bound color `RenderTarget` in
`ResourceState::ColorAttachmentOutput` when it returns. Independent
verification against the actual implementation
(`src/renderer/src/renderer.cpp`,
`src/render_graph/src/execution.cpp`) found both claims false:

- `Renderer::drawFrame()` builds and executes its own internal
  `RenderGraphBuilder` graph by calling the same shared
  `render_graph::execute()` free function
  [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  extends — it is not an independent, isolated code path.
- Prior to this amendment and [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md),
  `execute()`'s trailing-transition step unconditionally transitions
  **every** bound `RenderTarget` that was touched by any usage to
  `ResourceState::PresentSource`, with no way to distinguish origin
  (`src/render_graph/src/execution.cpp`, the trailing loop at the end of
  `execute()`) — this applied to Renderer's own internal call exactly as
  it applied to any other caller's, because it is the same function.
- Consequently, **`Renderer::drawFrame()`, prior to this amendment,
  actually leaves its bound color `RenderTarget` in
  `ResourceState::PresentSource`** when it returns — not
  `ColorAttachmentOutput`. A headless caller relying on the incorrect
  `ColorAttachmentOutput` assumption would record a barrier against the
  wrong actual layout — a genuine Vulkan-Validation-relevant correctness
  defect this amendment exists to prevent, not a documentation
  correction with no functional consequence.

Leaving `Renderer::drawFrame()`'s signature unchanged and having a
headless caller's own second `execute()` call simply treat
`PresentSource` as the "real" incoming state (transitioning
`PresentSource → TransferSource` for the copy) was considered and
rejected — see Alternatives Considered below.

### Amendment

**`Renderer::drawFrame()` gains one new, required parameter:
`atlantis::rhi::ResourceState finalColorState`.** Conceptually:

```
void drawFrame(atlantis::rhi::CommandList& commandList,
               atlantis::rhi::RenderTarget& colorTarget,
               atlantis::rhi::Texture& depthTarget,
               atlantis::rhi::Buffer& cameraUniformBuffer,
               std::span<const DrawItem> drawItems,
               atlantis::rhi::ResourceState finalColorState);
```

(Exact parameter position/name is a Plan-stage detail; the conceptual
requirement — a caller-supplied, backend-agnostic, required
`ResourceState` telling `Renderer` what state to leave the bound color
`RenderTarget` in when the call returns — is fixed here.)

- `Renderer` passes `finalColorState` through, unmodified, as the `final`
  field of its own internal color `ResourceBinding` entry
  ([ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)),
  replacing today's implicit reliance on `execute()`'s old hardcoded
  `PresentSource` default. `Renderer`'s internal draw pass itself is
  unchanged — it still declares exactly one `writes()` usage tagged
  `ColorAttachmentOutput`; only the *trailing* transition after that pass
  is now caller-directed rather than hardcoded.
- **`Renderer` does not interpret, validate, or branch on
  `finalColorState`'s value** — it is an entirely opaque, pass-through
  parameter as far as `Renderer`'s own logic is concerned. `Renderer`
  gains no knowledge of `Presentation`, `VkSwapchainKHR`,
  `OffscreenTarget`, or any other origin-specific concept — it does not
  learn, and does not need to learn, *why* its caller chose the value it
  did. This preserves
  [ADR-0002](0002-presentation-rendertarget-unification.md)'s origin-
  opacity requirement exactly: the discriminator lives entirely in the
  caller's own choice of argument value, never in anything `Renderer`
  inspects or stores.
- **A windowed caller passes `ResourceState::PresentSource`** — the exact
  value `execute()`'s old hardcoded behavior always produced, so this is
  a **zero-behavior-change** update for the windowed path once
  `minimal_renderer_demo` is updated to supply it explicitly (see
  [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  for the full, explicit list of mechanically-updated call sites,
  including this one).
- **A headless caller passes `ResourceState::TransferSource` directly** —
  `Renderer`'s internal trailing transition then goes straight from
  `ColorAttachmentOutput` to `TransferSource`, with no intermediate
  `PresentSource` state ever recorded for an image that will never be
  presented.
- **No boolean or other implicit/ambiguous parameter is introduced.**
  `finalColorState` is the same backend-agnostic `ResourceState` enum
  already used throughout RHI/RenderGraph's public surface
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)) —
  no new type, and no leak of any Vulkan-specific concept into
  `Renderer`'s public surface.
- **Legal values and error semantics.** `finalColorState` accepts any
  `ResourceState` value — the type itself imposes no compile-time
  restriction to a "valid for a color target" subset, consistent with
  how `ResourceState` is used everywhere else in this codebase. A value
  for which the Vulkan Backend's transition-mapping table
  (`resource_state_mapping.cpp`'s `planTransition()`) has no entry
  starting from `ColorAttachmentOutput` (this round: any value other
  than `PresentSource` or `TransferSource`) is a **programmer error**,
  surfaced as a guaranteed-detectable assertion failure
  (`ATLANTIS_CHECK_MSG`, per
  [ADR-0009](0009-assertion.md)) at the point `Renderer`'s internal
  `execute()` call would otherwise record the corresponding
  `transitionResource()` call — **not a compile-time restriction, and
  not a recoverable `Result`-typed error.** This is the same, unchanged
  assertion mechanism `planTransition()` already applies to every other
  unlisted `(before, after)` pair; this amendment does not introduce a
  general state-validation system, a new error enum, or any check
  `Renderer`/RenderGraph performs ahead of the Vulkan Backend's own
  existing (closed-table) mechanism — see
  [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  for the identical contract as it applies to `ResourceBinding`'s
  `incomingState`/`finalState` fields more generally.

**Everything else in this ADR's original Decision is unchanged**:
`Renderer` remains a concrete, stateless class depending only on
`Atlantis::RHI`/`Atlantis::RenderGraph`/`Atlantis::Core`; it still never
owns a `RenderTarget`, depth `Texture`, `Mesh`, or `Material`; `Mesh`/
`Material` ownership, the depth-texture/attachment-format caller-owned
resize contract, and the camera-`Buffer`-by-reference contract are all
unaffected.

### Consequences of this amendment

**Positive:**

- Fixes a real correctness defect (see "What prompted this amendment")
  rather than papering over it with a headless-side workaround that
  routes a never-presented image through `PresentSource` layout.
- `Renderer`'s dependency boundary and origin-opacity are fully
  preserved — the new parameter is exactly as backend-agnostic as
  everything else in its existing contract.
- The windowed path's behavior is bit-for-bit unchanged once its one call
  site is updated — no regression risk from the mechanism itself.

**Negative/Trade-offs:**

- This is a genuine, if narrow, breaking change to
  `Renderer::drawFrame()`'s already-`Accepted`, already-shipped public
  signature — every existing caller (today: `minimal_renderer_demo`)
  must be updated, and this ADR's original claim of a fully stable Spec
  0007 contract no longer holds without qualification; this amendment is
  the explicit, reviewed record of that change, not a silent one.
- A caller must now make an explicit decision every time it calls
  `Renderer::drawFrame()`, rather than relying on an implicit, always-
  correct-for-windowed default — a small, permanent increase in this
  call's own cognitive/API surface, accepted as the cost of not
  hardcoding a windowed-shaped assumption into `Renderer` itself.

### Alternatives considered (this amendment's own scope)

- **Leave `Renderer::drawFrame()`'s signature unchanged; have a headless
  caller's own second `execute()` call treat
  `ResourceState::PresentSource` as the real, documented incoming state
  and transition directly from it to `TransferSource`.** Considered —
  this is very likely Vulkan-Validation-safe (nothing in the Vulkan
  specification requires a `PRESENT_SRC_KHR`-layout image to actually be
  presented), and would avoid touching `Renderer`'s public API at all.
  **Rejected**: it does not actually eliminate the defect Spec 0010's own
  Motivation identifies — it relocates an offscreen, never-to-be-
  presented image's transition through a layout whose name and
  documented purpose
  ([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md))
  is specifically presentation, which is confusing to a future reader of
  the Vulkan Backend's transition-mapping table and is exactly the
  "meaningless presentation layout" the review that prompted this
  amendment flagged as worth avoiding. It also does not reduce the
  amount of new Vulkan Backend transition-table work required (a
  `PresentSource → TransferSource` entry would be needed instead of
  `ColorAttachmentOutput → TransferSource` — see
  [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)) —
  so it trades a small, explicit, reviewed `Renderer` API change for a
  larger, harder-to-explain semantic compromise with no net
  implementation savings.
- **Thread a shared, caller-owned execution-state object through both
  `Renderer`'s internal `execute()` call and the caller's own second
  one**, so cross-call state continuity happens automatically without
  either a `Renderer` signature change or a caller-supplied incoming-
  state override. Considered in the original (pre-this-amendment) draft
  of
  [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  and rejected there for the same reason it is rejected here: it requires
  `Renderer::drawFrame()`'s signature to change *regardless* (to accept
  and thread the shared object through), and does so via a larger, less
  self-contained change than a single `ResourceState` parameter — no
  additional benefit over this amendment's narrower approach.
- **Give `Renderer` an internal boolean, e.g. `bool isHeadless`, instead
  of an explicit `ResourceState`.** Rejected: a boolean's meaning is not
  self-evident at a call site, does not generalize if a future spec needs
  a third final state, and — more importantly — would require `Renderer`
  to itself map that boolean to a concrete `ResourceState` internally,
  reintroducing exactly the kind of origin-aware branching inside
  `Renderer` this ADR's original Decision (and
  [ADR-0002](0002-presentation-rendertarget-unification.md)) exists to
  keep out of it. An explicit `ResourceState` value keeps `Renderer`
  itself completely opaque to *why* a particular value was chosen.
