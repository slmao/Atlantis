# ADR 0022: Minimal Renderer Public API, Module Boundary, and Resource Ownership

- **Status:** Proposed
- **Date:** 2026-08-11
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)

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
