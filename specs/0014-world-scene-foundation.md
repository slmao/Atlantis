# Spec: World / Scene Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction.
- **Created:** 2026-08-22
- **Historical scope:** Three self-review rounds preceded formal Human
  Review (2026-08-22) — see
  [PR #67](https://github.com/slmao/Atlantis/pull/67) for the full
  revision history. Round 1 confirmed no existing public API must change
  (verified directly against `renderer/{draw_item,mesh,material,renderer}.h`,
  `core/{assert,log,result}.h`, `asset_system/{asset_id,load,static_mesh_asset_data}.h`,
  ADR-0033/0035/0043, and Spec 0013's own Runtime source) and flagged one
  real fork for Human Review: whether `Renderable` should reuse
  `atlantis::asset_system::AssetId` directly or a World-owned opaque
  handle. Round 2 re-verified ten points directly against real code or the
  ADRs' own current text rather than from memory, fixing: the correct
  ADR-0042 golden-update-reason category ("Initial baseline bootstrap,"
  not "Approved rebaseline," since no prior golden exists);
  re-confirmation that a windowed `RenderTarget` still cannot be
  pixel-read-back (`vulkan_presentation.cpp`'s `imageUsage` unchanged
  since Spec 0013); re-grounding stale-handle detection as a `Result`
  (not an assertion) directly against AGENTS.md's Programmer-error/`Result`
  model; widening `EntityId::generation` to `uint64_t`; fixing the LIFO
  free-list and ascending-index enumeration order explicitly; pinning down
  every hierarchy semantic (`setParent()` preserves local, not world,
  transform; cascading destroy clears the active camera; every mutation
  is atomic); fully specifying the Transform/Camera math contract
  (column-major, `parentWorld · local`, right-handed Y-up, `T·R·S`,
  `R = Ry·Rx·Rz`); clarifying `Renderable`'s `AssetId`→`Mesh` resolution
  as private to `Atlantis::RuntimeHost`; and replacing the original
  "Decisions Requiring Human Review" prose list with the numbered Human
  Review Decision Table below. Round 3 found two of Round 2's own
  conclusions did not hold up under further, targeted checking and
  superseded them: (1) generation-counter overflow — a 64-bit width alone
  is a probabilistic mitigation, not a formal closure; corrected to an
  unconditional slot-retirement rule (below); (2) camera view-matrix
  construction under a scaled hierarchy — a concrete counter-example (a
  non-uniformly-scaled parent composed with a rotated child) proved the
  original "normalize each basis column independently" extraction wrong
  (the columns are not generally orthogonal under composed shear);
  corrected to an `eye`/`forward`-only extraction feeding the existing
  `lookAt()` (below), with two genuine degenerate-input cases given
  explicit, recoverable error semantics.
- **Human Review Approval (2026-08-22):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`), accepting the merged document's own
  Human Review Decision Table in full, with no amendment. This approval
  explicitly accepts:
  1. **A new, independent top-level module, `Atlantis::World`** (not a
     private submodule of `src/runtime/`), depending on `Atlantis::Core`
     and, narrowly, `Atlantis::AssetSystem` for `AssetId` only — no RHI,
     Vulkan Backend, RenderGraph, Renderer, Shader System, Platform,
     Runtime, or Tools dependency, in either direction (ADR-0048).
  2. **`EntityId` as an index (`uint32_t`) + generation (`uint64_t`)
     handle, with unconditional, formal overflow closure**: a slot's
     index is permanently retired — never reused — the moment its
     generation reaches `uint64_t`'s maximum representable value,
     guaranteeing no historical handle can ever revalidate regardless of
     cycle count; the 64-bit width is a complementary practical
     mitigation, not the thing that alone closes the risk (ADR-0049).
  3. **Stale/invalid `EntityId` use is a `Result::Err(WorldError::InvalidEntity)`**,
     classified per AGENTS.md's Programmer-error/`Result` model: a stale
     handle produced by a legitimate (direct or cascading) destruction is
     a normal, observable state `EntityId`'s own non-owning contract
     already defines, not a violated precondition — while a genuine
     internal generation/slot bookkeeping inconsistency remains squarely
     an `ATLANTIS_CHECK` matter, never folded into `WorldError` (ADR-0049).
  4. **Every public World accessor returns by-value** (`Result<T,
     WorldError>`), never a reference or pointer into World's own internal
     storage (ADR-0049).
  5. **Fixed-type component storage** — a mandatory `Transform` plus two
     optional components (`Camera`, `Renderable`) directly on each
     entity's own record — not a generic, type-erased ECS registry (see
     "Why this stays a minimal World, not a general ECS").
  6. **Deterministic slot reuse and multi-entity enumeration order**: the
     free list is a LIFO stack, and any World API enumerating more than
     one entity iterates in ascending slot-index order — a fully
     specified, reproducible function of World's own mutation history
     (ADR-0049's Decision).
  7. **Explicit, single-threaded, once-per-frame `updateTransforms()`**,
     with cycle prevention at `setParent()` mutation time (an
     ancestor-chain walk, `Result::Err` before any state change) and a
     defensive, traversal-time `ATLANTIS_CHECK` as a last-resort invariant
     guard only (ADR-0050).
  8. **Parent destruction cascades** to every transitive descendant in one
     atomic call, automatically clearing the active camera if implicated
     (ADR-0050).
  9. **`setParent()` preserves the child's own *local* transform, not its
     *world* transform** — reparenting therefore generally changes world
     position/orientation as a disclosed side effect, with no automatic
     world-preserving reparent operation in this round (ADR-0050).
  10. **The fully specified Math contract**: column-major matrix layout;
      column-vector composition with parent on the left
      (`worldMatrix = parentWorldMatrix · localMatrix`); right-handed,
      Y-up coordinates; `localMatrix = T · R · S`; Euler-angle composition
      `R = Ry(yaw) · Rx(pitch) · Rz(roll)`; the disclosed fact that a
      composed, multi-level world matrix may contain shear; and the
      `Camera` `fovYRadians`/`nearZ`/`farZ`-only ownership boundary
      (aspect computed by Runtime per-frame, never stored on `Camera`).
  11. **Camera ownership, the active-camera rule, and view-matrix
      construction under a scaled hierarchy**: `Camera` is an optional
      component on an ordinary entity, carrying only
      `fovYRadians`/`nearZ`/`farZ`; exactly one active camera at a time;
      the view matrix is built by extracting only `eye` and `forward`
      from the camera's own world matrix — never `right`/`up` columns,
      which are not reliably orthogonal under a sheared hierarchy —
      feeding them into the existing, unmodified `lookAt()`; a near-zero
      forward direction or a forward direction parallel to the canonical
      world-up axis are explicit, recoverable, Runtime-classified
      extraction errors, never a silent `NaN` (ADR-0051).
  12. **`Renderable` reuses `atlantis::asset_system::AssetId` directly**
      (not a World-owned opaque handle), and World depends on nothing else
      from Asset System (ADR-0048).
  13. **The World→Renderer `DrawItem` translation is Runtime's own
      composition-root adapter** — never inside World, never a Renderer
      change, and not a new, separate "Extraction" module (ADR-0051) —
      and **Runtime's `AssetId`→`Mesh`/`Material` resolution mechanism is
      a private implementation detail of `Atlantis::RuntimeHost`'s own
      composition object**, never a fixed public interface and never a
      global mutable Asset database (ADR-0051).
  14. **Every existing public rendering API — `Renderer`, RHI, Vulkan
      Backend, Platform, Shader System, and Asset System — remains exactly
      as `Accepted` today, with zero modification to any public header,
      type, or function signature.**
  15. **Runtime's own windowed `RenderTarget` cannot be pixel-read-back**
      (no `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`, unchanged from Spec 0013);
      windowed verification for this Spec's multi-entity scene stays a
      GPU smoke test plus manual, by-eye comparison, while the existing,
      unmodified headless `OffscreenTarget`/image-regression path is the
      only automated pixel-comparison route this Spec's scene gets
      (ADR-0051's Consequences).
  16. **The recommended new headless image-regression golden for this
      Spec's multi-entity scene cites ADR-0042's own `Accepted` Amendment,
      "Initial baseline bootstrap" category — not "Approved rebaseline"**
      — per that Amendment's applicability constraint 1 (no prior golden
      exists at that path) and constraint 5's substitute-evidence
      requirement (visual inspection; zero-diff self-consistency; a real
      Validation-Layers-clean GPU run; citing ADR-0042's own existing
      calibration) in place of an inapplicable old-vs-new diff.
  17. **Every Non-Goal this Spec states explicitly** — a
      general/data-driven/multi-threaded ECS framework; any scene file
      format, cooker, or serialization; textures/samplers; PBR materials,
      lighting, shadows; animation/rotation interpolation;
      post-processing; Android/iOS/Linux; any new third-party dependency
      or general-purpose `Atlantis::Math` module; a Client/Editor API;
      any change to an existing module's public API; multiple
      simultaneous cameras; and a general multi-asset resource
      cache/hot-reload/async streaming in Runtime's own resolution
      mechanism.

  Two stale internal cross-references (each still naming this document's
  prior "Decisions Requiring Human Review" section name, and one citing a
  stale item number) were corrected on this same branch immediately before
  this approval — neither correction changes any Decision, Consequence, or
  Alternative Considered in either ADR.

  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  all move to `Accepted` alongside this approval. **This approval
  authorizes drafting Plan 0014; it does not itself authorize
  Implementation.**
- **Related Plan(s):**
  [plans/0014-world-scene-foundation.md](../plans/0014-world-scene-foundation.md)
  (`Approved / Ready for Implementation` — Human Review Approval recorded
  2026-08-22; see that Plan's own note).
- **Related ADR(s):**
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)
  (module boundary and ownership),
  [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)
  (entity identity and handle invalidation, including its own Accepted
  Amendment — stable `World` identity token),
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)
  (Transform hierarchy, composition, and update model),
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  (World-to-Renderer extraction and asset resolution boundary) — all four
  `Accepted`.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #151](https://github.com/slmao/Atlantis/pull/151) Batch 4. Original scope, obligations, the Human Review
  Decision Table, and the full Accepted Amendment (stable `World`
  identity, and its two subsequent corrections) retained; the round-by-round
  self-review narration is preserved in PR #67 history.

## Summary

This Spec introduces `Atlantis World` — a new, eleventh top-level module,
`Atlantis::World` — as Atlantis's first in-memory, multi-entity scene
representation: a minimal, CPU-only, backend-independent World owning
Entity lifecycle, local/world Transform with parent/child hierarchy, an
optional Camera component, an optional Renderable component (referencing
a stable Asset System `AssetId`), and a read-only, multi-entity traversal
surface. It replaces Runtime's current single hardcoded `DrawItem`
composition (Spec 0013) with a World-driven scene of several `minimal_cube`
instances at distinct transforms plus one camera, extracted each frame by
a new Runtime-owned adapter into the exact same, **unmodified**
`atlantis::renderer::DrawItem`/`Renderer::drawFrame()` inputs every
existing composition root already uses — confirmed, by direct inspection,
to already support a multi-item span. This is deliberately not a general,
data-driven, or multi-threaded ECS framework: fixed-type component
storage, explicit single-threaded mutation, and a minimal, dependency-free
Transform-composition math live entirely inside this one new module. Scene
file formats, a scene-asset cooker, textures/sampling, PBR materials,
lighting, shadows, animation, and post-processing are all explicitly
excluded — see Non-Goals; Scene Asset/Serialization is registered as the
next Candidate Backlog item this Spec's own boundary hands off to.

## Motivation / Problem Statement

Every rendering milestone through Spec 0013 (Runtime Host Foundation,
`Approved`, implemented) has drawn **exactly one** hardcoded mesh: a fixed
`DrawItem` built once, in composition-root C++, from the already-cooked
`minimal_cube` asset and the already-compiled `minimal_mesh` shader. Spec
0013's Non-Goals state this explicitly, and its Out of Scope names
"World/ECS Foundation" as the very next Candidate Backlog item depending
on it, once `Approved`/implemented — which it now is (merged via
[PR #63](https://github.com/slmao/Atlantis/pull/63), post-merge closeout
via [PR #64](https://github.com/slmao/Atlantis/pull/64)).

Nothing in this codebase today can own, update, or traverse **more than
one** positioned object. There is no Entity concept, no Transform
hierarchy, no Camera-as-data, and no notion of "the current scene" distinct
from "the one `DrawItem` this frame's code happens to build."
[specs/README.md](README.md)'s Candidate Spec Backlog has named this gap
since its own creation ("World/ECS Foundation," depending on Spec 0013)
and [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)
(`Accepted`, Spec 0009) already commits Atlantis to a long-term principle
— "Runtime, once it exists as a real module, is the sole authoritative
owner of engine world state" — while explicitly deferring every concrete
representation decision (entity/handle shape, storage layout, hierarchy
model) to this Spec by name.

This Spec is the first to give Atlantis an actual, ownable, updatable,
traversable in-memory scene, and the boundary a future Scene
Asset/Serialization Spec (the next Candidate Backlog item, per the
registry update below) will bake authoring data into.

### Why this stays a minimal World, not a general ECS

`docs/project-blueprint.md` itself already states "World/ECS foundation —
no ECS implementation, library, or in-house design is chosen" as an open
item, and nothing downstream of this Spec (per its own Non-Goals) needs
more than two optional component kinds (`Camera`, `Renderable`) or more
than a few entities to validate. A generic, type-erased component
registry — the shape a "real ECS" implies — would be exactly the
speculative, data-driven abstraction [AGENTS.md](../AGENTS.md)'s Golden
Rule and "No speculative abstraction" principle warn against, built before
a second real component type or a second real consumer exists to validate
its shape against. This Spec instead fixes a small, closed set of
component types directly on each entity's own record — see the Human
Review Decision Table, item 5, for the explicit trade-off this accepts.

## Goals

- Introduce **`Atlantis World`** as a new top-level module
  (`Atlantis::World`, namespace `atlantis::world`, directory `src/world/`)
  — CPU-only, backend-independent, depending on Atlantis Core and (for
  `AssetId` only) Atlantis Asset System, and nothing else.
- Entity lifecycle: create, destroy (cascading to descendants), and detect
  a stale/invalidated handle safely (ADR-0049).
- Local and world Transform per entity (position, rotation, scale),
  composed through an explicit parent/child hierarchy with cycle
  prevention at mutation time and an explicit, single-threaded,
  once-per-frame update pass (ADR-0050).
- An optional `Camera` component (field-of-view, near/far planes) and a
  single World-level "active camera" reference, with a defined rule for
  what happens when none is set or the active camera is destroyed.
- An optional `Renderable` component referencing a stable Asset System
  `AssetId` — never an RHI/Renderer type, never a raw GPU handle.
- A read-only, multi-entity traversal surface sufficient for a
  composition root to enumerate every Renderable entity and resolve the
  active camera, once per frame.
- A Runtime-owned extraction/adapter path from World data to Renderer's
  existing, **unmodified** `DrawItem`/`drawFrame()` inputs (ADR-0051).
- A real, verifiable Runtime validation scene: several `minimal_cube`
  instances at distinct World-driven transforms, plus one World-driven
  camera, displayed through the existing windowed Runtime.
- Establish the boundary a future Scene Asset/Serialization Spec hands
  authoring-baked data into, without this Spec designing that format
  itself.

## Non-Goals

Explicitly excluded from this Spec's design:

- **A general, data-driven, or multi-threaded ECS framework.** No
  type-erased component registry, no generic runtime component-type
  registration, no archetype/chunk storage, no job-system-driven parallel
  iteration. Fixed-type component storage only.
- **Scene file format or serialization of any kind.** World's own data
  exists only in memory. Scene Asset/Serialization is the next Candidate
  Backlog item this Spec's own module boundary is deliberately shaped to
  hand off to.
- **A scene-asset cooker, importer, or any Tools-hosted CLI for World
  data.**
- **Textures, samplers, or any RHI sampled-image capability.**
- **PBR materials, lighting, shadows, or any new rendering capability.**
  Every Renderable entity uses the same single, fixed `minimal_mesh`
  `Material` every existing windowed demo already uses; `Renderable`
  carries no material reference in this round.
- **Animation, skeletal or otherwise, and rotation interpolation.**
  `Transform` is a static per-frame value set directly by a caller — no
  keyframe, blend, or time-driven mutation exists in World itself. This
  also motivates ADR-0050's Euler-angle rotation choice over a
  quaternion.
- **Post-processing, of any kind.**
- **Android, iOS, or Linux.** Windows-only, matching every prior spec;
  Linux is not a target platform for Atlantis at all.
- **Any new third-party dependency.** World's own minimal math primitives
  (ADR-0050) are hand-rolled, matching every existing composition root's
  precedent — no math library (e.g. GLM) is added.
- **A general-purpose `Atlantis::Math` module, or any change to Atlantis
  Core.** World's math primitives are scoped to the `atlantis::world`
  namespace only (ADR-0048).
- **A Client/Editor API, IPC, or any second-process consumer of World
  state.** Matching Spec 0013's precedent exactly: ADR-0033's principle
  is acknowledged and trivially satisfied.
- **Any change to Renderer's, RHI's, Vulkan Backend's, Platform's, Asset
  System's, or Shader System's existing public API.**
- **Multiple simultaneous cameras, viewports, or a camera stack.** Exactly
  one active camera at a time.
- **A general multi-asset resource cache, hot-reload, or async asset
  streaming** in Runtime's new `AssetId`→`Mesh`/`Material` resource table.
  Scoped, for this Spec, to the single already-cooked `minimal_cube`
  asset (ADR-0051).
- **A Plan or an Implementation.** This Spec, alongside ADR-0048–0051, is
  the entire scope of this round of work.

## Requirements

### Functional

**Module boundary** (ADR-0048)

- New top-level module `src/world/`, CMake target/alias `Atlantis::World`,
  namespace `atlantis::world`.
- Depends on `Atlantis::Core` and, narrowly, `Atlantis::AssetSystem` (for
  `atlantis::asset_system::AssetId` only — no other Asset System header).
  No dependency on RHI, Vulkan Backend, RenderGraph, Renderer, Shader
  System, Platform, Runtime, or Tools.
- Depended on by `Atlantis Runtime` only, for now.

**Entity lifecycle and identity** (ADR-0049; the shape below is superseded
by this Spec's own "Accepted Amendment" section further down, which adds a
third, private `World`-identity field)

- `EntityId` is an index+generation value type (`{ std::uint32_t index;
  std::uint64_t generation; }`, 16 bytes), value-comparable, with a fixed
  invalid sentinel. **Overflow is formally closed, not merely made
  unlikely:** `std::numeric_limits<std::uint64_t>::max()` is a reserved
  tombstone value, never assigned to any live entity; when
  `destroyEntity()` increments a slot's generation to that value, the slot
  is permanently retired (its index is never returned to the free list,
  never reused by any future `createEntity()` call). The 64-bit width is a
  complementary, practical mitigation.
- `World::createEntity()` returns a new, always-valid `EntityId`
  (non-fallible) — unaffected by retirement: if every existing index has
  ever been retired, `createEntity()` still succeeds by growing storage
  and allocating a new index. Slot reuse is a **LIFO free list** — the
  most recently destroyed, non-retired slot's index is the first one a
  following `createEntity()` reuses.
- `World::destroyEntity(EntityId)` returns `Result<void, WorldError>`;
  cascades to every transitive descendant in the same call; clears the
  active-camera reference automatically if it or any destroyed descendant
  was the active camera.
- `World::isValid(EntityId) const` reports whether a handle's index is in
  range and its generation matches the slot's current generation.
- Every public World API accepting an `EntityId` validates it and returns
  `Result<T, WorldError>` with `WorldError::InvalidEntity` on a stale or
  out-of-range handle — never undefined behavior. **Classification,
  aligned with AGENTS.md's own Programmer-error/`Result` split:** a stale
  `EntityId` — one that named an entity since destroyed by a *legitimate*
  call — is a normal, observable state `EntityId`'s own explicitly
  non-owning contract already defines, not a violated precondition; the
  same category of outcome as `std::weak_ptr::lock()` returning `nullptr`.
  An **internal** generation/slot bookkeeping inconsistency remains a
  categorically different case, handled by `ATLANTIS_CHECK` (never folded
  into `WorldError`).
- No public World accessor returns a reference or pointer into World's
  own internal storage; every getter returns a by-value copy.
- **Every mutating World operation is atomic:** it either fully succeeds,
  or returns `Result::Err` having changed nothing at all — every
  precondition is validated before any state change.
- **Multi-entity enumeration order is deterministic and specified:** any
  World API enumerating more than one entity (`renderableEntities()`)
  iterates in **ascending slot-index order** — combined with the LIFO
  free-list rule, this makes the order a pure, reproducible function of
  the exact sequence of `createEntity()`/`destroyEntity()` calls a caller
  makes. Required so a multi-entity image-regression golden never depends
  on undefined ordering. (`updateTransforms()`'s own internal traversal
  order is not a public contract.)

**Transform and hierarchy** (ADR-0050)

- Every entity has exactly one `Transform` (mandatory, not optional):
  `Vec3 localPosition`, `Vec3 localEulerAnglesRadians`, `Vec3 localScale`
  (default `{1,1,1}`).
- `World::setLocalTransform(EntityId, Transform) -> Result<void,
  WorldError>`, `World::getLocalTransform(EntityId) const -> Result<
  Transform, WorldError>`.
- `World::setParent(EntityId child, EntityId parent) -> Result<void,
  WorldError>` — `parent == kInvalidEntityId` clears to root. Returns
  `WorldError::WouldCreateCycle` (checked before any mutation) if `parent`
  is `child` itself or a descendant of `child`; `WorldError::InvalidEntity`
  if either handle is stale. **Preserves the child's own *local*
  transform; does not preserve its *world* transform** — `setParent()`
  never reads or writes `Transform` fields, so reparenting generally
  changes the child's world position/orientation as a disclosed side
  effect. A caller wanting the child's world transform to stay fixed
  across a reparent must compute and set the appropriate new local
  transform itself — no automatic "preserve world transform" reparent
  operation exists in this round.
- `World::getParent(EntityId) const -> Result<EntityId, WorldError>`
  (returns the invalid sentinel for a root entity, not an error).
- `World::updateTransforms()` recomputes every entity's world matrix in
  one traversal, visiting each entity strictly after its own parent.
  `World::getWorldMatrix(EntityId) const -> Result<std::array<float, 16>,
  WorldError>` reflects state as of the most recent `updateTransforms()`
  call.
- **Math contract, fully specified:** column-major matrix layout (matching
  `DrawItem::objectToWorld`); column-vector composition with parent on the
  left (`worldMatrix = parentWorldMatrix · localMatrix`, matching
  `minimal_mesh.slang`'s vertex-stage `mul()` chain); right-handed, Y-up
  coordinates; `localMatrix = T · R · S`; and Euler-angle composition
  `R = Ry(yaw) · Rx(pitch) · Rz(roll)` — the one piece of this contract
  with no prior precedent in this codebase, fixed here arbitrarily but
  precisely. This is a fully-specified internal contract of
  `atlantis::world`, not a new general-purpose `Atlantis::Math` module.
  **A composed, multi-level world matrix may contain shear** (its linear
  part is not guaranteed to decompose back into a pure rotation times a
  uniform scale) whenever a non-uniform or negative scale at one level is
  combined with a differently-oriented rotation at a descendant level — a
  disclosed, accepted property of this composition model, not a defect (a
  `Renderable` entity renders correctly under an arbitrary, even sheared,
  linear transform), but a real constraint for any consumer needing to
  recover an *orthonormal* basis from a world matrix.

**Camera**

- Optional per-entity `Camera` component: `float fovYRadians`, `float
  nearZ`, `float farZ`. **No aspect-ratio field, and no
  position/orientation fields of its own** — aspect is Runtime's own
  per-frame responsibility (computed from the current swapchain extent),
  and a Camera entity's position/orientation come entirely from its own
  `Transform` (so a camera can be parented, e.g. attached to a moving
  rig).
- `World::setCamera(EntityId, Camera) -> Result<void, WorldError>`,
  `World::removeCamera(EntityId) -> Result<void, WorldError>`,
  `World::getCamera(EntityId) const -> Result<Camera, WorldError>`.
- Exactly one **active camera** at a time: `World::setActiveCamera(
  EntityId) -> Result<void, WorldError>` (fails with
  `WorldError::NoCameraComponent` if the target entity has no `Camera`),
  `World::clearActiveCamera() noexcept`, `World::activeCamera() const
  noexcept -> std::optional<EntityId>`.

**Renderable**

- Optional per-entity `Renderable` component: `atlantis::asset_system
  ::AssetId meshAsset` only — no material reference.
- `World::setRenderable(EntityId, Renderable) -> Result<void,
  WorldError>`, `World::removeRenderable(EntityId) -> Result<void,
  WorldError>`, `World::getRenderable(EntityId) const -> Result<
  Renderable, WorldError>`.

**Multi-entity traversal**

- `World::renderableEntities() const` returns an enumerable, read-only
  view (exact container/return type a Plan-stage detail) of every live
  entity carrying a `Renderable` component, for a composition root to
  iterate once per frame.

**Extraction / Runtime adapter** (ADR-0051)

- Runtime, not World, performs: `world.updateTransforms()`; resolving the
  active camera to a view/projection matrix pair — the **view** matrix
  built by extracting **only** an eye position (the world matrix's
  translation column) and a forward direction (`normalize(-column 2)`,
  fixing the convention **a Camera looks down its own local −Z axis**)
  from the camera entity's own world matrix — **never** a `right`/`up`
  column — then feeding `eye`/`eye+forward` into the existing, unmodified
  `lookAt()` function every windowed demo already uses, which itself
  derives an orthonormal `right`/`up` basis via cross products against a
  fixed world-up reference. This is deliberately **not** "extract and
  normalize all three basis columns": a composed, multi-level world
  matrix may contain shear, under which independently-normalized columns
  are not generally orthogonal — extracting only a single direction
  sidesteps that problem entirely. No general 4×4 matrix inverse is
  needed either way. Two genuine degenerate-input cases — a
  near-zero-length forward direction, and a forward direction parallel to
  the canonical world-up axis (the camera looking straight up/down) — are
  detected explicitly and treated as recoverable, Runtime-classified
  extraction conditions, never silently computed into a `NaN`-poisoned
  matrix. The **projection** matrix comes from `Camera`'s
  `fovYRadians`/`nearZ`/`farZ` and the current swapchain aspect ratio —
  and writing both into the existing camera uniform `Buffer`; resolving
  each Renderable entity's `AssetId` through a resolution mechanism
  entirely **private** to Runtime's own composition object (never a
  global mutable Asset database, never a type World or any other module
  depends on; this Spec fixes only its `AssetId`-in/`Mesh`+`Material`-or-
  not-found-out shape, not a concrete container type); building one
  `renderer::DrawItem` per Renderable entity, in `renderableEntities()`'s
  own ascending-slot-index order; and calling the existing, unmodified
  `Renderer::drawFrame()` once per frame with the full multi-item span.
- No RHI, Renderer, RenderGraph, Vulkan Backend, or Platform type is ever
  named, included, or constructed inside `src/world/`.
- **Runtime's existing windowed `RenderTarget` cannot be pixel-read-back
  or automatically compared against a golden** — confirmed by direct
  inspection of `vulkan_presentation.cpp` (a swapchain-backed
  `RenderTarget`'s `imageUsage` carries only
  `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`,
  never `TRANSFER_SRC_BIT`, unchanged since Spec 0013), and this Spec does
  not add that capability. Runtime's own windowed verification is
  therefore a GPU smoke test plus manual, by-eye comparison — the
  headless `OffscreenTarget`/image-regression path is the **only**
  automated pixel-comparison path this Spec's scene gets.

### Non-functional

- **Performance:** not a goal beyond "a single, full-traversal
  `updateTransforms()` and a single extraction pass per frame do not
  stall or busy-spin" at this Spec's own validation scale — no dirty-flag
  optimization, no parallel traversal.
- **Memory:** World owns all entity/component data as plain value types
  in its own internal storage (a slot map, ADR-0049) — no shared
  ownership, no `shared_ptr` aliasing, no global/static World instance
  anywhere (AGENTS.md's no-singleton rule).
- **Portability (within the Vulkan-only Phase 1 constraint):** World
  itself has no platform or graphics-API dependency and is portable by
  construction; verified only via Windows Runtime.
- **Threading:** single-threaded throughout (ADR-0004). World is not
  internally thread-safe and documents this at its own public API; no
  concurrent mutation during `updateTransforms()` traversal is possible or
  supported.
- **Ownership:** RAII throughout; World is the sole owner of every entity
  and component it holds; `EntityId` is a non-owning value handle; no
  public accessor exposes a reference/pointer into World's own storage.
- **Determinism and ordering:** `renderableEntities()` iterates in
  ascending slot-index order, with a LIFO free list governing slot reuse.
  `updateTransforms()`'s own **internal** traversal order is deliberately
  *not* a public contract: its only fixed requirement is topological
  (every entity visited strictly after its own parent), and any traversal
  satisfying that requirement produces byte-identical world-matrix
  values.
- **Atomicity:** every mutating World operation either fully succeeds or
  returns `Result::Err` having changed nothing, including on a
  `WorldError::WouldCreateCycle` or `WorldError::InvalidEntity` failure.
- **Error handling:** every recoverable World operation returns
  `atlantis::Result<T, WorldError>`, including stale/invalid `EntityId`
  use, classified as a normal, observable runtime state — not a
  programmer-error assertion. This is categorically distinct from a
  genuine **internal** invariant violation — the update traversal's own
  defense-in-depth "never revisit an already-visited entity" check
  (ADR-0050) is exactly such an internal-invariant guard, and uses
  `ATLANTIS_CHECK` specifically (not `ATLANTIS_ASSERT`, which compiles to
  a no-op whenever `NDEBUG` is defined) so it stays active in Release
  builds too.

## Proposed Design

### Module boundary diagram

```
atlantis_runtime / Atlantis::RuntimeHost (composition root; extended, not
redesigned, by this Spec)
  -> Atlantis::World          (creates/owns one World instance; calls
                                updateTransforms(); reads Transform/
                                Camera/Renderable data each frame)
  -> Atlantis::Renderer       (drawFrame() -- UNCHANGED; now called with
                                a multi-item DrawItem span built from
                                World data instead of one hardcoded item)
  -> Atlantis::AssetSystem    (loadStaticMeshAsset() -- UNCHANGED; the
                                same minimal_cube artifact, still loaded
                                once at startup)
  -> ... (Platform, RHI, Vulkan Backend, Shader System -- all UNCHANGED,
          exactly as Spec 0013 already composes them)

Atlantis::World (new; this Spec)
  -> Atlantis::Core           (Result<T,E>, logging, assertions)
  -> Atlantis::AssetSystem    (AssetId type only)

No dependency from Atlantis::World on Atlantis::Renderer, RHI, Vulkan
Backend, RenderGraph, Shader System, or Platform. No dependency from
Atlantis::Renderer, RHI, or any other existing module on Atlantis::World.
```

### Validation scene (illustrative, exact values a Plan-stage detail)

Runtime constructs one `World` at startup, alongside its existing
already-loaded `minimal_cube` `Mesh` and fixed `Material`: several
entities (e.g. five), each given a `Transform` at a distinct
`localPosition` (and, optionally, a distinct `localEulerAnglesRadians`, to
visibly demonstrate rotation) and a `Renderable` referencing
`minimal_cube`'s `AssetId` — at least one of them parented to another, to
exercise the hierarchy — plus one additional entity carrying only a
`Camera` (no `Renderable`), set as the active camera via
`setActiveCamera()`, positioned via its own `Transform` to frame the other
entities. Every frame, Runtime calls `updateTransforms()`, then the
extraction path fixed by ADR-0051, then the existing, unmodified
`Renderer::drawFrame()` once with the full multi-item span.

## Architectural Impact

This Spec introduces a new top-level module and four architectural
decisions, filed as four ADRs — all `Accepted` alongside this Spec's own
Human Review Approval:

1. **Module boundary and ownership** — ADR-0048.
2. **Entity identity and handle invalidation** — ADR-0049.
3. **Transform hierarchy, composition, and update model** — ADR-0050.
4. **World-to-Renderer extraction and asset resolution boundary** —
   ADR-0051.

**No existing `Accepted` ADR's conclusions are reopened or modified.**
Each new ADR references and builds on ADR-0001–0005, ADR-0022, ADR-0032,
ADR-0033, ADR-0035, ADR-0043–0045, and ADR-0046/0047, without altering
any of them.

**No new public API in any existing module.** `Atlantis::Renderer`,
`Atlantis::RHI`, `Atlantis::VulkanBackend`, `Atlantis::AssetSystem`,
`Atlantis::Platform`, and `Atlantis::ShaderSystem` are consumed exactly as
they exist today.

**ADR-0032 five-layer placement.** `Atlantis::World` sits in the
**Authoritative Runtime** conceptual layer, alongside Atlantis Runtime
itself — a non-binding, illustrative placement only; the authoritative
eleven-module source-ownership view (ADR-0048) is what a build/dependency
check actually enforces.

**ADR-0033 compliance.** Runtime owns the one real `World` instance;
nothing outside Runtime observes or mutates it in this round's scope — the
same trivial satisfaction Spec 0013 established for its own bootstrap
state, now applied to genuine "engine world state" for the first time.

**ADR-0035 compliance — addressed explicitly, per that ADR's own
procedural requirement.** This Spec's `World`/`Transform`/`Camera`/
`Renderable` representation **is** the runtime-execution representation;
no distinct authoring-facing representation or bake/compile step exists in
this round, because no authoring tool or Editor consumes World data yet.
This is a considered, explicit choice — Scene Asset/Serialization, the
next Candidate Backlog item, is where an authoring-facing representation
and a bake step feeding this same runtime `World` structure are expected.

**`docs/architecture/module_boundaries.md`, deferred.** That document
predates this Spec and does not describe World at all yet. Reconciling it
is deferred to a future Plan/docs-sync, not a blocker to this Spec's own
approval.

**Registry update (specs/README.md).** "World/ECS Foundation," formerly
Candidate Order 2, is promoted to Section A as Spec 0014. Its "Depends On"
was Spec 0013 (`Approved`, implemented) — satisfied. Every remaining
candidate's Candidate Order number, and every cross-reference naming
"World/ECS" or "Candidate 2," is renumbered/corrected as a mechanical
index update. Android Platform and Vulkan Presentation (Candidate Order 1)
is explicitly **not** reordered, reprioritized, or reinterpreted.

## Human Review Decision Table

Fourteen decisions this Spec asks Human Review to confirm, reject, or
amend — none silently locked as "just an implementation detail." Each row
states this Spec's own recommendation and the trade-off; full reasoning
and Alternatives Considered live in the ADR each row links to (or in this
Spec's own sections, for the two rows with no dedicated ADR).

| # | Decision | Recommendation | Key trade-off / why this needs sign-off | Source |
|---|---|---|---|---|
| 1 | New, independent top-level module (`Atlantis::World`), or a private submodule of `src/runtime/`? | New top-level module, matching Asset System's own precedent (ADR-0043). | A new top-level module is a permanent structural commitment; folding into Runtime's private `RuntimeHost` library would foreclose independent unit testing and a future non-Runtime consumer. | [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md) |
| 2 | `EntityId` shape, generation width, and overflow behavior. | Index (`uint32_t`) + generation (`uint64_t`), 16 bytes; a slot's index is **permanently retired** the moment its generation reaches `uint64_t`'s maximum value. | The retirement rule, not the 64-bit width alone, formally guarantees no historical handle ever revalidates; costs one doubled handle size and, in the practically-unreachable retirement case, one permanently smaller reusable-index pool. | [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md) |
| 3 | Stale/invalid `EntityId` detection: `Result::Err`, or an assertion? | `Result<T, WorldError>` — `InvalidEntity` for a stale external handle; `ATLANTIS_CHECK` reserved for a genuine **internal** bookkeeping bug. | A stale handle from a *legitimate* destruction is a normal, observable state (like `std::weak_ptr::lock()` returning `nullptr`), not a violated precondition. | ADR-0049 |
| 4 | Do public World accessors return by-value copies, or references/pointers? | By-value only — every getter returns `Result<T, WorldError>` by value; every setter takes its argument by value. | Stricter than ADR-0033 strictly requires — adopted because World's own internal slot array can reallocate on growth, which would otherwise dangle any previously-returned reference. | ADR-0049 |
| 5 | Fixed-type component storage, or a generic ECS registry? | Fixed-type storage — a mandatory `Transform` plus two optional components on each entity's own record; no type-erased pool, no runtime component-type registration. | A future component type requires extending the fixed entity record, not registering a new type generically. | This Spec's own "Why this stays a minimal World" |
| 6 | Transform hierarchy update strategy, and when cycle detection runs. | Explicit, single-threaded, once-per-frame `updateTransforms()`; cycle prevention at `setParent()` mutation time, with a defensive traversal-time `ATLANTIS_CHECK` as a last-resort guard only. | A world matrix read without a following `updateTransforms()` call silently reflects stale data — a documented contract, not an automatic dirty check. | [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md) |
| 7 | Does destroying a parent cascade to descendants, reparent them, or leave them orphaned? | Cascades — `id` and every transitive descendant are destroyed together, in one atomic call; the active camera is cleared automatically if implicated. | Simplest semantics to reason about, but no "detach children first" escape hatch exists. | ADR-0050 |
| 8 | Does `setParent()` preserve the child's *local* or *world* transform? | Preserves *local*; world transform generally changes as a disclosed side effect. | The alternative requires a general 4×4 matrix inverse plus a TRS decomposition — real machinery this Spec's minimal scope does not need. | ADR-0050 |
| 9 | World↔Asset System reference boundary: `Renderable` reuses `AssetId` directly, or a World-owned opaque handle? | Reuse `AssetId` directly — one source of truth, no conversion layer; World depends on nothing else from Asset System. | Couples `Renderable` to Asset System's current, path-derived (not rename-durable) identity scheme — a future Serialization/Stable-Identity Spec changing that scheme changes `Renderable` directly. | [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md) |
| 10 | Where does the World→Renderer `DrawItem` translation live? | Runtime's own composition-root adapter — never inside World, never a Renderer change, and not a new "Extraction" module. | Confirmed, by direct inspection, that `Renderer::drawFrame()` already accepts and iterates a multi-item `DrawItem` span. | [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md) |
| 11 | Camera ownership, the active-camera rule, and view-matrix construction under a scaled hierarchy. | `Camera` is an optional component on an ordinary entity, carrying only `fovYRadians`/`nearZ`/`farZ`; exactly one active camera at a time. View matrix: extract only `eye`+`forward` (never `right`/`up`), feeding them into the existing, unmodified `lookAt()`; near-zero forward or forward-parallel-to-world-up are explicit, recoverable extraction errors. | View/projection matrix computation stays entirely Runtime's own hand-rolled code, never moved into World. A correction to this ADR's own first-drafted column-normalization approach, found incorrect by a concrete counter-example. | ADR-0051 |
| 12 | Should Runtime's `AssetId`→`Mesh`/`Material` resolution mechanism be a fixed public interface, or a private implementation detail? | Private to `Atlantis::RuntimeHost`'s own composition object — this Spec fixes only its input/output shape, not a concrete container type or public API. | Locking a public resolver interface now, with only one real consumer and one real asset, would be premature, unnecessary abstraction. | ADR-0051 |
| 13 | Does this Spec preserve every existing public rendering API unchanged? | Yes — confirmed by direct inspection, twice: `Renderer`, RHI, Vulkan Backend, Platform, Shader System, and Asset System all remain exactly as `Accepted` today. | Not a judgment call — a factual finding this table records for direct confirmation, since it is this Spec's own central architectural claim. | Independent Review above |
| 14 | The first multi-entity Runtime validation scene, and its image-regression golden-update-reason category. | Extend Runtime's bootstrap with several `minimal_cube` instances at distinct World-driven transforms (one hierarchy relationship exercised) plus one World-driven camera; verify via (a) a **new** headless image-regression fixture/golden, citing ADR-0042's own **Accepted Amendment** "Initial baseline bootstrap" category (not "Approved rebaseline," which needs old-vs-new diff evidence that cannot exist here) — the Amendment's constraint-5 substitute evidence (visual inspection; zero-diff self-consistency; a real Validation-Layers-clean GPU run; citing ADR-0042's existing calibration); (b) a windowed Runtime GPU smoke test extension; (c) manual, by-eye windowed verification against that same golden. | Runtime's own windowed swapchain still cannot be pixel-read-back — the headless path is the only automated pixel-comparison route available. Exact fixture/golden naming and PR sequencing remain Plan-stage details. | This Spec's own Testing & Verification Plan; [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md) |

**Approval readiness — superseded by the Human Review Approval recorded at
the top of this document.** All 14 items were accepted in full, as
recommended, with no amendment, and all four ADRs moved from `Proposed` to
`Accepted` as part of that same approval.

## Alternatives Considered

- **Defer World/Scene entirely and let a future Scene Asset/Serialization
  Spec introduce both an authoring format and a runtime representation
  together.** Rejected: there is no runtime representation to bake into
  yet, and the Candidate Backlog already orders Serialization and Stable
  Identity as depending on World/ECS, not the reverse.
- **Adopt a real, general-purpose ECS library or design now.** Rejected
  for this round: no second component type, no data-driven authoring
  tool, and no measured performance need exists yet to validate a general
  ECS's added complexity against.
- **Give World a Renderer dependency so it can vend `DrawItem`s (or even
  own GPU `Mesh`/`Material`) directly.** Rejected: violates this Spec's
  own CPU-only/backend-independent requirement, and would make World's
  own unit tests require a real `Device`.
- **Promote a shared math library into Atlantis Core now.** Rejected for
  this round: no second genuine consumer exists yet to validate a shared
  library's shape against.

## Testing & Verification Plan

- **Unit tests (GPU-independent), `tests/world/`, linking
  `Atlantis::World` and `Atlantis::Core`/`Atlantis::AssetSystem` (for
  `AssetId`) only — no `Device`, no GPU, no real window required:**
  - Entity lifecycle: `createEntity()` always succeeds and returns a valid
    handle; `destroyEntity()` invalidates the entity and (recursive case)
    every transitive descendant in one call; every subsequent operation
    against any of those handles returns `WorldError::InvalidEntity`; a
    destroyed and reused slot's new `EntityId` (different generation) does
    not alias the old, stale one.
  - Hierarchy: `setParent()` succeeds for a valid, non-cycle-forming
    request; rejects (`WouldCreateCycle`) a direct self-parent, a two-hop
    cycle, and a longer transitive cycle, leaving the hierarchy
    unchanged; `setParent()` leaves the child's own `getLocalTransform()`
    byte-identical while its `getWorldMatrix()` (after
    `updateTransforms()`) changes when the new parent's world matrix
    differs — the local-vs-world preservation contract;
    `updateTransforms()` produces the expected world matrix for a
    multi-level chain against a hand-computed expected result, verifying
    the full math contract; destroying a mid-chain entity cascades to its
    own descendants, leaving unrelated siblings/ancestors untouched.
  - **Shear under a scaled hierarchy — the `World`-owned half.** A parent
    with non-uniform local scale composed with a child rotated about an
    axis not aligned with the scale produces a world matrix whose
    linear-part columns are verifiably **not** mutually orthogonal
    (reproducing ADR-0050's own counter-example) — confirming
    `updateTransforms()` does not attempt to "correct" or reject shear; a
    `Renderable` entity in this configuration still produces a valid
    `DrawItem::objectToWorld`.
  - Camera: `setActiveCamera()` fails (`NoCameraComponent`) against an
    entity with no `Camera`; destroying the active camera entity clears
    `activeCamera()` to `std::nullopt` automatically; `activeCamera()`
    starts `std::nullopt` on a freshly-constructed `World`.
  - Renderable and traversal: `renderableEntities()` returns exactly the
    set of live entities currently carrying a `Renderable`, correctly
    excluding entities with only a `Transform`, a destroyed entity, and
    (after `removeRenderable()`) a previously-renderable entity.
  - **Determinism:** a fixed sequence of `createEntity()`/`destroyEntity()`
    calls that frees and reuses slot indices (exercising the LIFO free
    list) produces the exact same `renderableEntities()` ascending-slot-
    index ordering across repeated, independent runs of the same test.
  - **Atomicity:** a `setParent()` call that fails with
    `WouldCreateCycle`, and a `destroyEntity()`/`setLocalTransform()`/etc.
    call that fails with `InvalidEntity`, each leave every observable
    World state byte-identical to immediately before the call.
  - `EntityId` value semantics: equality, the invalid sentinel's own
    `isValid()` result, and ordinary copyable-value usage in
    `std::vector`/`std::unordered_map`.
  - **Generation retirement, directly tested at the boundary.** A
    test-only construction path sets a slot's generation to
    `std::numeric_limits<std::uint64_t>::max() - 1` before calling
    `destroyEntity()` on it, then asserts (a) the slot's generation is now
    the tombstone value; (b) a following `createEntity()` never reuses
    that specific index; (c) an `EntityId` carrying that index at its old,
    pre-retirement generation still correctly returns
    `WorldError::InvalidEntity`.
- **Unit tests (GPU-independent), `tests/runtime/`, exercising Runtime's
  own camera view-matrix extraction directly against hand-constructed
  world matrices — no `Device`, no GPU, no `World` instance required:**
  - The same shear-producing parent/child configuration as the `World`-
    owned test above, with the child instead representing a `Camera`'s
    world matrix: the `eye`+`forward`-only extraction still produces a
    well-formed, orthonormal view basis.
  - A negatively-scaled ancestor (a mirror) feeding into a camera's world
    matrix: the resulting view matrix remains a proper, right-handed
    orthonormal basis (determinant `+1`, not a reflection).
  - A degenerate world matrix that collapses the camera's own forward axis
    to (near-)zero length: extraction returns a recoverable,
    Runtime-classified error, never a `NaN`-containing matrix.
  - A world matrix whose forward direction is (near-)parallel to the
    canonical world-up axis `(0,1,0)`: extraction returns the same
    category of recoverable error.
- **GPU-required tests (Windows/Vulkan, `gpu`-labeled), extending
  `tests/runtime/` and/or `tests/vulkan_backend/`:**
  - A Runtime GPU smoke test constructing the full validation-scene
    composition (World with several Renderable entities plus one active
    camera), confirming `Renderer::drawFrame()` succeeds against the
    resulting multi-item `DrawItem` span with Vulkan Validation Layers
    reporting zero warnings/errors.
- **Image regression (headless), `tests/image_regression/`:** a new
  fixture and golden for this Spec's own multi-entity validation scene —
  see the Human Review Decision Table item 14 for the "Initial baseline
  bootstrap" category and the three-layer verification split. Exact
  fixture composition a Plan-stage detail, fixed to be deterministic and
  visually distinguishable from the existing single-cube golden.
- **Manual verification (Windows, real window, real GPU):** a visible
  window shows several distinct cube instances at their expected relative
  positions (including the exercised parent/child relationship — moving/
  rotating a parent visibly moves its child too) and the expected camera
  framing, matching the new golden by eye; no crash, no Vulkan Validation
  Layer warning or error across a full run including resize/minimize/
  restore/close.
- **Regression, unchanged:** every existing GPU-independent test suite,
  every existing `gpu`-labeled test suite, and the existing `minimal_cube`
  headless golden/regression test continue to pass — this Spec adds a new
  module, a new test directory, and a new golden; it does not modify any
  existing test, asset, shader, or golden file.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of this Spec's implementation.

## Risks & Open Questions

- **Whether the recommended new image-regression golden lands in the same
  PR as the rest of this Spec's implementation, or as a follow-up** — a
  Plan-stage sequencing detail (Human Review Decision Table item 14).
- **The exact `EntityId` invalid-sentinel bit pattern, and the exact
  per-axis rotation-matrix element layout implementing the fixed
  `R = Ry(yaw) · Rx(pitch) · Rz(roll)` composition** are left to the
  Plan — this Spec fixes the handle's shape and the full math contract,
  not the literal sentinel value or the mechanical per-element formula.
- **Whether `renderableEntities()`'s exact return type is a
  `std::vector<EntityId>` snapshot, a lazy view, or a callback-based
  `forEach`** is left to the Plan.
- **Whether reusing `atlantis::asset_system::AssetId` directly proves
  awkward once a real Serialization and Stable Identity Spec is drafted**
  is a named, honest, deferred risk.
- **Whether the camera basis-extraction convention (a Camera entity looks
  down its own local −Z axis) reads intuitively** versus a possible
  alternative (+Z-forward) convention — this Spec fixes one, documented,
  internally-consistent convention because it has to pick one.
- **Runtime's own new AssetId→Mesh/Material resource table's exact
  container/lookup-failure policy** (skip-and-log vs. fail-the-frame) is
  left to the Plan.
- **The exact epsilon threshold(s) for the camera view-matrix extraction's
  two degenerate-input checks**, and **the exact Runtime-side error
  type/enumerator naming these two cases and the unresolved-`AssetId` case
  share**, are left to the Plan.

## Out of Scope / Future Work

**Scene Asset/Serialization** is registered as the next Candidate Backlog
item this Spec's own module boundary is deliberately shaped to hand off to
— an authoring-facing representation, a bake/compile step, stable
cross-session entity/asset identity, and a scene file format all remain
that future Spec's own scope. Also remaining out of scope, unaffected by
this Spec: Android Platform and Vulkan Presentation; Tool/Editor
Connection Protocol (the first real exercise of ADR-0033's Client model,
now that real "engine world state" exists to protect); a Gameplay SDK; a
general, data-driven, or multi-threaded ECS; textures/sampling; PBR
materials, lighting, and shadows; animation; post-processing — all remain
later, separately-specced work.

## Accepted Amendment (2026-08-22) — Stable `World` identity in `EntityId`

**Status: Accepted.** Recorded following Human Review direction, then
formal approval, responding to Plan 0014's Independent Review Round 2
finding (cross-`World`-instance `EntityId` use — see
[plans/0014-world-scene-foundation.md](../plans/0014-world-scene-foundation.md)'s
own Deviations). Does not alter the Human Review Approval recorded above;
that approval covered the document as it stood on 2026-08-22, before this
finding existed.

**What changed:** Human Review rejected treating cross-`World`-instance
`EntityId` use as an undetectable documented precondition violation, and
separately rejected resolving it with a global, incrementing per-process
`World`-instance counter. Instead, Human Review directed, then accepted:
each `World` instance exclusively owns one heap-allocated, address-stable,
opaque identity token for its own exclusive lifetime; `EntityId` carries a
non-owning, **private** reference to that token alongside its existing
public `index`/`generation`; every `World` API validates identity
**before** slot/generation; a handle used against a **different,
currently live** `World` instance is rejected with a new, distinct
`WorldError::WrongWorld` — never silently misapplied to an unrelated
entity. Full design rationale, exact validation ordering, and rejected
alternatives are recorded in ADR-0049's own amendment; this section states
the requirements-level consequence.

- **`EntityId` gains a third, `private` field** — a non-owning identity
  reference to an opaque `WorldIdentity` token is added — and **all three
  of `EntityId`'s own fields (index, generation, and identity) are
  `private`, not only the identity field** (corrected 2026-08-23; leaving
  index/generation public would have left a same-`World` forgery case
  this amendment's own "no forgery" intent was meant to close).
  `EntityId` publicly exposes only what is necessary: default construction
  (the invalid sentinel), equality comparison (which still includes
  identity, since a defaulted comparison operator has access to private
  members), and — only where a real call site needs it — read-only
  `index()`/`generation()` accessors. **Never a caller-writable raw
  pointer, and never any mutator for any of the three fields.** No caller
  can forge or overwrite any of them directly. `EntityId` remains a plain,
  trivially-copyable value type owning nothing. **This Spec's prior "16
  bytes" claim is superseded and not replaced with a new fixed number** —
  the exact resulting size is pointer-width- and alignment-dependent, an
  Implementation-time detail.
- **`WorldError` gains a fourth enumerator, `WrongWorld`**, returned when
  a `Result`-returning API's `EntityId` argument carries a non-null
  identity that does not match the receiving `World` instance's own token
  — checked before any index/generation check, and before a moved-from-
  `World` check, since a mismatched identity makes those fields
  meaningless relative to this instance. The existing sentinel
  `kInvalidEntityId` (no claimed identity) is unaffected: a null identity
  is never treated as "wrong," only as ordinarily invalid
  (`InvalidEntity`).
- **`EntityId` must never be serialized, persisted, or used across a
  process boundary.** Its identity component is a heap address,
  meaningful only within the process and `World` instance that produced
  it — stable, cross-session identity remains the separately deferred
  Serialization/Stable-Identity Spec's own future scope.
- **Lifetime remains a separate concern from cross-instance confusion.**
  `EntityId` is still a strictly borrowed, non-owning handle: using it
  after the `World` instance that issued it has been destroyed remains a
  lifetime precondition violation (undefined behavior), not a condition
  this design detects. `WrongWorld` covers use against a different,
  currently **live** `World` instance only — not a destroyed one.
- **`World`'s own copy/move semantics are now load-bearing, not merely a
  Plan-stage convenience choice.** `World` must be move-constructible,
  with its identity token and all state moving together (a handle valid
  before a move remains valid after it), and must be neither copyable nor
  move-assignable — both would let two live `World` "identities" apply to
  overlapping state, defeating the very check this amendment adds.
- **A moved-from `World` guarantees only that it remains destructible or
  may be move-constructed from again.** Any other call on a moved-from
  `World` is a programmer error, caught by an explicit assertion-based
  check, not silently tolerated.

### Human Review Amendment Approval (2026-08-22)

Reviewed and approved by slmao (`slmao <slmaosjtu@gmail.com>`) on
2026-08-22, accepting this section's design in full, matching ADR-0049's
own "Human Review Amendment Approval" note item-for-item. This approval
does not reopen or modify the Human Review Approval recorded above — only
this amendment section is newly `Accepted`.
[Plan 0014](../plans/0014-world-scene-foundation.md), synced to this
design, is separately approved. Implementation itself still waits on
[PR #67](https://github.com/slmao/Atlantis/pull/67) being merged.

**Correction (2026-08-23), mechanical, no new review round:** the
original text above left `index`/`generation` as plain public `EntityId`
fields, moving only the identity reference behind `private` —
inconsistent with this same approval's own "no forgery" intent, since a
plain public `index` field lets a caller copy a legitimate handle and
overwrite its index directly, coincidentally forging a different entity
within the same `World` when the mutated index carries a matching
generation. Fixed: all three fields are now private, exposed only via
read-only `index()`/`generation()` accessors (never an identity accessor,
never a mutator). No other part of this approval changes.

**Human Review Correction (2026-08-23), additive, no new review round:**
Implementation of [Plan 0014](../plans/0014-world-scene-foundation.md)
disclosed that `World::getRenderable()` had no enumerator of its own for a
valid `EntityId` legitimately carrying no `Renderable` component, and
reused `NoCameraComponent` for that case as an interim choice. Human
Review **rejected** that reuse as insufficiently precise. Corrected:
**`WorldError` gains a fifth enumerator, `NoRenderableComponent`**,
returned by `getRenderable()` for a valid entity with no `Renderable`;
`NoCameraComponent` remains scoped exactly as before — a missing `Camera`
component only. The `InvalidEntity`/`WrongWorld`/stale-generation
validation already required before any component-absence check is
unchanged and still takes priority. No other `World` API, module
boundary, or error semantics changes. See
[Plan 0014](../plans/0014-world-scene-foundation.md)'s own Verification
Checklist (V28) for the corresponding new verification requirement.
