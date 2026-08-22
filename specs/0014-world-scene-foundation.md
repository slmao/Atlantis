# Spec: World / Scene Foundation

- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path. Not yet reviewed by a human — see Independent
  Review below for the self-review performed during drafting; Human
  Review Approval is not recorded on this document yet.
- **Created:** 2026-08-22
- **Related Plan(s):** None. Per this round's explicit scope, only a Spec
  and its required ADRs are drafted — no Plan, no Implementation. A Plan
  is authorized only once this Spec and
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  have all passed Human Review, per [AGENTS.md](../AGENTS.md).
- **Related ADR(s):**
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)
  (module boundary and ownership),
  [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)
  (entity identity and handle invalidation),
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)
  (Transform hierarchy, composition, and update model), and
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  (World-to-Renderer extraction and asset resolution boundary) — all four
  `Proposed`, not yet `Accepted`. This Spec cannot reach `Approved` until
  Human Review accepts all four and moves them to `Accepted`, per
  [AGENTS.md](../AGENTS.md).
- **Independent Review (2026-08-22):** Self-review performed during
  drafting, against `main`'s actual, current public headers and
  implementation (not historical summaries or the human-provided design
  suggestions alone) for every module this Spec touches or reasons about:
  - `src/renderer/include/atlantis/renderer/{draw_item,mesh,material,renderer}.h`
    and `src/renderer/src/renderer.cpp` — confirmed `DrawItem` already
    carries a raw `std::array<float, 16> objectToWorld` (its own header
    comment: "Atlantis Core has no public math type yet"), confirmed
    `Renderer::drawFrame()` already accepts `std::span<const DrawItem>`
    and its implementation already iterates the full span
    (`for (const DrawItem& item : drawItems)`), and confirmed `Mesh`/
    `Material` are move-only, single-owner, GPU-backed types with no
    caching or deduplication (`createMesh()`/`createMaterial()` each
    produce a new instance per call). This grounds this Spec's central
    claim — multiple `DrawItem`s per frame need zero Renderer change —
    in real code, not assumption.
  - `src/core/include/atlantis/{assert,log,result}.h` — confirmed Atlantis
    Core has no `Vec3`, `Mat4`, quaternion, or any other math type today,
    despite `docs/architecture/module_boundaries.md`'s own (`PROPOSED`,
    not `Accepted`) Core section already anticipating one. Confirmed, by
    grepping `examples/minimal_renderer_demo/main.cpp`, that every
    existing windowed composition root hand-rolls its own private
    `Mat4`/`identityMatrix()`/`multiply()`/`lookAt()`/`perspective()`
    helpers, never shared.
  - `src/asset_system/include/atlantis/asset_system/{asset_id,load,static_mesh_asset_data}.h` —
    confirmed `AssetId` is a dependency-free `uint64_t` alias
    (`asset_id.h` includes nothing from Asset System's own loader/cooker
    surface), suitable for World to reuse without pulling in any loading,
    cooking, or validation logic.
  - [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md) and
    [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md)
    (both `Accepted`) — confirmed this Spec's own design satisfies
    ADR-0033's binding constraint (no raw pointer/reference to a
    Runtime-owned entity/component crosses World's public API — see
    [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md))
    and explicitly addresses ADR-0035's own procedural requirement (this
    Spec's Architectural Impact states, explicitly, that authoring and
    runtime representation are the same structure in this round — see
    below).
  - [ADR-0043](../adr/0043-asset-system-module-boundary.md) (`Accepted`) —
    used as the direct structural precedent for
    [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)'s
    own module-boundary decision (a new, narrow, Core-adjacent top-level
    module, not folded into an existing leaf).
  - [specs/0013-runtime-host-foundation.md](0013-runtime-host-foundation.md)
    (`Approved`, implemented) and `src/runtime/` — confirmed Runtime's own
    current bootstrap composition holds exactly one hardcoded `Mesh`/
    `Material`/`DrawItem`, with no resource lookup of any kind, and that
    extending it to a World-driven, multi-entity scene is additive to
    Runtime's own composition object, not a change to its already-fixed
    initialization/per-frame/shutdown ordering.
  - [adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
    own Accepted Amendment — confirmed the "Initial baseline bootstrap"
    golden-update-reason category exists specifically for a scene's
    first-ever golden, directly applicable to this Spec's own recommended
    new multi-entity validation golden (see Testing & Verification Plan
    and the Human Review Decision Table, item 14).

  This review found no case where an existing public API must change to
  support this Spec's own minimum scope, and one real, disclosed
  architectural fork worth flagging explicitly rather than deciding
  silently: whether Renderable should reuse
  `atlantis::asset_system::AssetId` directly (this Spec's recommendation)
  or a World-owned opaque handle type — see the Human Review Decision
  Table, item 9.
- **Independent Review — Round 2 (2026-08-22, pre-Human-Review evidence
  pass), centralized on the ten points a targeted, evidence-driven review
  raised before this document could go to formal Human Review.** Each
  point below was checked directly against real code or the actual,
  current text of the ADR it concerns — not restated from the Round 1
  summary above — and every finding was corrected on this same branch
  before this Spec's own status changed. Full detail lives in each
  affected ADR's own "Revision (2026-08-22...)" note; this is the
  consolidated index:
  1. **Golden-update-reason category — re-verified against
     [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
     own full text, not from memory.** Confirmed: "Initial baseline
     bootstrap" is a real, `Accepted` category (its own "Accepted
     Amendment — 2026-08-17" section, formally accepted by Human Review
     2026-08-17) — not a fourth category this Spec invented. Confirmed
     further that "Approved rebaseline" (category 3) is the **wrong**
     category for this Spec's own new golden: ADR-0042's own Alternatives
     Considered explicitly rejects using category 3 as "the permanent
     answer" for a first golden, since it requires "the same explicit
     reasoning as (2)" (old-vs-new provenance/diff evidence) that cannot
     exist when there is no prior golden. The Human Review Decision
     Table, item 14, below now cites the Amendment's own constraint
     numbers directly (applicability constraint 1; the four-part
     substitute-evidence constraint 5) instead of a general "matches Spec
     0011's precedent" gesture.
  2. **Windowed pixel comparison — reconfirmed impossible with today's
     public API, stated explicitly rather than left implicit.**
     Re-inspected `src/vulkan_backend/src/vulkan_presentation.cpp`
     directly (not merely cited from Spec 0013's own record): a
     swapchain-backed `RenderTarget` is still constructed with
     `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`
     only — no `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` — unchanged since Spec
     0013. Runtime's own windowed verification therefore remains a GPU
     smoke test plus manual, by-eye comparison; the headless
     `OffscreenTarget`/image-regression path is the **only** automated
     pixel-comparison path this Spec's own scene gets — made explicit in
     Requirements' "Extraction / Runtime adapter" and Testing &
     Verification Plan below, and in
     [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
     own Consequences.
  3. **Stale-handle `Result` vs. assertion — re-grounded in
     `src/core/include/atlantis/assert.h`'s actual Release-build
     semantics**, not merely in ADR-0033's cross-module framing (which
     this Spec's own Non-Goals concede does not yet have a real second
     Client to apply to). `ATLANTIS_ASSERT` compiles to a no-op — the
     condition unevaluated — whenever `NDEBUG` is defined, i.e. every
     Release build; using it for stale-handle detection would silently
     disable the entire safety net in exactly the configuration a real
     build ships. `ATLANTIS_CHECK` is evaluated in both configurations
     but aborts the whole process on failure — too severe for what is
     often ordinary, single-threaded caller bookkeeping. See
     [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s
     own Decision and Alternatives Considered, and the Human Review
     Decision Table, item 3, below — flagged for explicit confirmation,
     not silently locked.
  4. **Generation-counter overflow — closed, not merely disclosed.**
     `EntityId::generation` widened from `std::uint32_t` to
     `std::uint64_t` (`EntityId` grows from 8 to 16 bytes) — see
     [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s
     own quantitative reasoning (`2^64` cycles on one slot is
     impractical at any plausible process lifetime) and its Alternatives
     Considered for why a permanently-retired-slot scheme was rejected in
     favor of simply widening the field.
  5. **Deterministic multi-entity iteration order — fixed explicitly.**
     The free list is a LIFO stack (most-recently-freed index reused
     first); any World API enumerating more than one entity
     (`renderableEntities()`) iterates in ascending slot-index order —
     both fixed in
     [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s
     own Decision, so the order `DrawItem`s are built and submitted in is
     a specified, reproducible function of World's own mutation history,
     never an accident of unspecified container iteration a multi-entity
     image-regression golden could silently depend on.
  6. **Hierarchy semantics, each pinned down explicitly:**
     `setParent()` preserves the child's own **local** transform, not its
     world transform (reparenting therefore generally changes world
     position/orientation as a disclosed side effect — now its own Human
     Review Decision Table row, item 8 — see
     [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md));
     parent destruction cascades to every descendant, clearing the active
     camera automatically if implicated (unchanged from Round 1, now also
     its own Human Review Decision Table row, item 7); cycle
     detection failure and every other mutating World operation are
     atomic — full success or `Result::Err` with zero mutation, fixed as
     a blanket contract in
     [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md).
  7. **The minimal Transform/Camera math contract, fully specified and
     evidence-grounded rather than gestured at.** Verified directly
     against `examples/minimal_renderer_demo/main.cpp`'s own `multiply()`/
     `lookAt()`/`perspective()` and
     `shaders/minimal_renderer/minimal_mesh.slang`'s own vertex-stage
     `mul()` chain: column-major layout (matching `DrawItem`), column-
     vector composition with parent on the left
     (`worldMatrix = parentWorldMatrix · localMatrix`), right-handed Y-up,
     and `localMatrix = T · R · S`, all **reused** from already-
     established precedent — plus one genuinely **new** convention this
     Spec introduces (no prior code builds a rotation from Euler angles):
     `R = Ry(yaw) · Rx(pitch) · Rz(roll)`, fixed arbitrarily but
     precisely. Camera's `fovYRadians`/`nearZ`/`farZ`-only ownership
     (aspect computed by Runtime per-frame, never stored on `Camera`) is
     stated as an explicit responsibility boundary. See
     [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)'s
     own "Math contract" subsection for the full, precise statement. No
     general-purpose `Atlantis::Math` module is introduced.
  8. **`Renderable`'s `AssetId` → loaded `Mesh` resolution — boundary
     clarified, no public interface prematurely locked.** Confirmed this
     resolution mechanism is entirely private to
     `Atlantis::RuntimeHost`'s own composition object (never a process-
     wide singleton or global mutable Asset database, never a type World
     or any other module names or depends on) and that this ADR fixes
     only its input/output shape (`AssetId` in, an already-constructed
     `Mesh`/`Material` pair or a not-found outcome, out) — not a concrete
     container type, which remains an explicit Plan-stage detail. See
     [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md).
     Also replaced this ADR's own original "view = inverse(world matrix)"
     camera construction with a basis-extraction-plus-existing-`lookAt()`
     construction that needs no general 4×4 inverse and no unscaled-
     camera precondition (mutual orthogonality of a TRS matrix's basis
     columns is preserved under arbitrary per-axis scale) — see that
     ADR's own Decision step 3.
  9. **Every item this round confirmed must be a Human Review decision,
     not a silently-fixed implementation detail, is now its own row in
     the Human Review Decision Table below** — the new top-level module,
     fixed-type component storage, by-value access, cascading destroy,
     and the stale-handle `Result` policy are each individually visible
     and individually confirmable, not folded invisibly into surrounding
     prose. See "Human Review Decision Table," replacing the prior
     "Decisions Requiring Human Review" prose list.
  10. **Re-confirmed, not merely carried forward: no existing public
      rendering API needs to change.** Re-inspected
      `src/renderer/include/atlantis/renderer/{draw_item,renderer}.h` and
      `src/renderer/src/renderer.cpp` during this same evidence pass — no
      change since Round 1 (this branch has added no implementation code
      to any existing module); the multi-item `DrawItem` span capability
      and the "Renderer never touches raw camera math" contract both
      still hold exactly as Round 1 found them. No blocking finding.

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
`minimal_cube` asset and the already-compiled `minimal_mesh` shader.
Spec 0013's own Non-Goals state this explicitly: "this spec's own
bootstrap scene is exactly one hardcoded `DrawItem`; no entity, component,
or scene-description format of any kind is introduced," and its own Out
of Scope names "World/ECS Foundation" as the very next Candidate Backlog
item depending on it, once `Approved`/implemented — which it now is
(merged via [PR #63](https://github.com/slmao/Atlantis/pull/63), with its
own post-merge closeout landed via
[PR #64](https://github.com/slmao/Atlantis/pull/64)).

Nothing in this codebase today can own, update, or traverse **more than
one** positioned object. There is no Entity concept, no Transform
hierarchy, no Camera-as-data (every existing demo hardcodes eye
coordinates directly into its own `lookAt()` call), and no notion of
"the current scene" distinct from "the one `DrawItem` this frame's code
happens to build." `specs/README.md`'s own Candidate Spec Backlog has
named this gap since the backlog's own creation ("World/ECS Foundation,"
Candidate Order 2, depending on Spec 0013) and
[ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)
(`Accepted`, Spec 0009) already commits Atlantis to a long-term principle
— "Runtime, once it exists as a real module, is the sole authoritative
owner of engine world state" — while explicitly deferring every concrete
representation decision (entity/handle shape, storage layout, hierarchy
model) to this Spec by name.

This Spec is the first to give Atlantis an actual, ownable, updatable,
traversable in-memory scene — the minimum needed to move from "one
hardcoded mesh" to "a real, if still tiny, scene" — and the boundary a
future Scene Asset/Serialization Spec (the next Candidate Backlog item
after this one, per the registry update below) will bake authoring data
into.

### Why this stays a minimal World, not a general ECS

The human direction driving this Spec is explicit, and this Spec's own
research confirms it is the right call at this codebase's current scale:
`docs/project-blueprint.md` itself already states "World/ECS foundation —
no ECS implementation, library, or in-house design is chosen" as an
open item, and nothing downstream of this Spec (per its own Non-Goals)
needs more than two optional component kinds (`Camera`, `Renderable`) or
more than a few entities to validate. A generic, type-erased component
registry — the shape a "real ECS" implies — would be exactly the
speculative, data-driven abstraction [AGENTS.md](../AGENTS.md)'s Golden
Rule and "No speculative abstraction" principle warn against, built
before a second real component type or a second real consumer exists to
validate its shape against. This Spec instead fixes a small, closed set
of component types directly on each entity's own record — see the Human
Review Decision Table, item 5, for the explicit trade-off this
accepts.

## Goals

- Introduce **`Atlantis World`** as a new top-level module
  (`Atlantis::World`, namespace `atlantis::world`, directory `src/world/`)
  — CPU-only, backend-independent, depending on Atlantis Core and (for
  `AssetId` only) Atlantis Asset System, and nothing else.
- Entity lifecycle: create, destroy (cascading to descendants), and
  detect a stale/invalidated handle safely — see
  [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md).
- Local and world Transform per entity (position, rotation, scale),
  composed through an explicit parent/child hierarchy with cycle
  prevention at mutation time and an explicit, single-threaded,
  once-per-frame update pass — see
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md).
- An optional `Camera` component (field-of-view, near/far planes) and a
  single World-level "active camera" reference, with a defined rule for
  what happens when none is set or the active camera is destroyed.
- An optional `Renderable` component referencing a stable Asset System
  `AssetId` — never an RHI/Renderer type, never a raw GPU handle.
- A read-only, multi-entity traversal surface sufficient for a
  composition root to enumerate every Renderable entity and resolve the
  active camera, once per frame.
- A Runtime-owned extraction/adapter path from World data to Renderer's
  existing, **unmodified** `DrawItem`/`drawFrame()` inputs — see
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md).
- A real, verifiable Runtime validation scene: several `minimal_cube`
  instances at distinct World-driven transforms, plus one World-driven
  camera, displayed through the existing windowed Runtime — see Testing &
  Verification Plan.
- Establish the boundary a future Scene Asset/Serialization Spec (the
  next Candidate Backlog item, registered below) hands authoring-baked
  data into, without this Spec designing that format itself.

## Non-Goals

Explicitly excluded from this Spec's design:

- **A general, data-driven, or multi-threaded ECS framework.** No
  type-erased component registry, no generic "register a new component
  type at runtime" mechanism, no archetype/chunk storage, no job-system-
  driven parallel iteration. Fixed-type component storage only — see
  the Human Review Decision Table, item 5.
- **Scene file format or serialization of any kind.** World's own data
  exists only in memory, constructed and torn down within a single
  process run. No load/save, no versioning, no schema. Scene Asset/
  Serialization is the next Candidate Backlog item this Spec's own module
  boundary is deliberately shaped to hand off to — not designed,
  scaffolded, or previewed here.
- **A scene-asset cooker, importer, or any Tools-hosted CLI for World
  data.** Atlantis Tools gains no new content from this Spec.
- **Textures, samplers, or any RHI sampled-image capability.** Unchanged
  from every prior spec's own scope; not touched here.
- **PBR materials, lighting, shadows, or any new rendering capability.**
  Every Renderable entity in this Spec's own validation scene uses the
  same single, fixed `minimal_mesh` `Material` every existing windowed
  demo already uses. `Renderable` carries no material reference at all in
  this round — see Requirements.
- **Animation, skeletal or otherwise, and rotation interpolation.**
  `Transform` is a static per-frame value set directly by a caller; no
  keyframe, blend, or time-driven mutation exists in World itself. This
  also motivates
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)'s
  own Euler-angle rotation choice over a quaternion.
- **Post-processing, of any kind.** Unchanged from every prior spec.
- **Android, iOS, or Linux.** This Spec's own validation scene is
  verified on Windows only, matching every prior spec in this line;
  Linux is not a target platform for Atlantis at all, per
  [AGENTS.md](../AGENTS.md).
- **Any new third-party dependency.** World's own minimal math primitives
  ([ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md))
  are hand-rolled, matching every existing composition root's own
  precedent — no math library (e.g. GLM) is added.
- **A general-purpose `Atlantis::Math` module, or any change to Atlantis
  Core.** World's math primitives are scoped to the `atlantis::world`
  namespace only — see
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md).
- **A Client/Editor API, IPC, or any second-process consumer of World
  state.** Matching Spec 0013's own precedent exactly:
  [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s
  principle is acknowledged and trivially satisfied (Runtime is the only
  owner/consumer; nothing external observes or mutates World yet), not
  exercised in earnest.
- **Any change to Renderer's, RHI's, Vulkan Backend's, Atlantis Platform's,
  Atlantis Asset System's, or Atlantis Shader System's existing public
  API.** Confirmed unnecessary by this Spec's own Independent Review
  above; if Plan/Implementation later finds a genuine gap, that is raised
  as its own explicit architectural question, not patched around
  silently.
- **Multiple simultaneous cameras, viewports, or a camera stack.** Exactly
  one active camera at a time — see Requirements.
- **A general multi-asset resource cache, hot-reload, or async asset
  streaming** in Runtime's own new AssetId→Mesh/Material resource table.
  Scoped, for this Spec, to the single already-cooked `minimal_cube` asset
  — see
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md).
- **A Plan or an Implementation.** This Spec, alongside
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md),
  is the entire scope of this round of work — see AGENTS.md's Spec → Plan
  → Human Review → Implementation path.

## Requirements

### Functional

**Module boundary** (see
[ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md))

- New top-level module `src/world/`, CMake target/alias `Atlantis::World`,
  namespace `atlantis::world`.
- Depends on `Atlantis::Core` and, narrowly, `Atlantis::AssetSystem` (for
  `atlantis::asset_system::AssetId` only — no other Asset System header).
  No dependency on RHI, Vulkan Backend, RenderGraph, Renderer, Shader
  System, Platform, Runtime, or Tools.
- Depended on by `Atlantis Runtime` only, for now.

**Entity lifecycle and identity** (see
[ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md))

- `EntityId` is an index+generation value type (`{ std::uint32_t index;
  std::uint64_t generation; }`, 16 bytes — the 64-bit generation field
  closes the counter-wraparound risk a narrower field would leave open;
  see ADR-0049's own quantitative reasoning), value-comparable, with a
  fixed invalid sentinel.
- `World::createEntity()` returns a new, always-valid `EntityId`
  (non-fallible). Slot reuse is a **LIFO free list** — the most recently
  destroyed slot's index is the first one a following `createEntity()`
  reuses, a fixed, deterministic rule, never an accident of container
  choice.
- `World::destroyEntity(EntityId)` returns `Result<void, WorldError>`;
  cascades to every transitive descendant in the same call; clears the
  active-camera reference automatically if it or any destroyed descendant
  was the active camera.
- `World::isValid(EntityId) const` reports whether a handle's index is in
  range and its generation matches the slot's current generation.
- Every public World API accepting an `EntityId` validates it and returns
  `Result<T, WorldError>` with `WorldError::InvalidEntity` on a stale or
  out-of-range handle — never undefined behavior. This is a deliberate
  choice, not the only reasonable one: `WorldError::InvalidEntity` is used
  instead of `ATLANTIS_ASSERT`/`ATLANTIS_CHECK` specifically because
  `ATLANTIS_ASSERT` compiles to a no-op whenever `NDEBUG` is defined
  (verified against `src/core/include/atlantis/assert.h`), which would
  silently remove stale-handle detection in every Release build, and
  `ATLANTIS_CHECK` aborts the whole process — too severe for what is,
  in this Spec's own single-threaded model, often ordinary caller
  bookkeeping rather than a violated memory-safety invariant. See
  ADR-0049's own Decision and Alternatives Considered, and the Human
  Review Decision Table below (this is flagged for explicit confirmation,
  not silently locked).
- No public World accessor returns a reference or pointer into World's own
  internal storage; every getter returns a by-value copy.
- **Every mutating World operation (`destroyEntity()`, `setParent()`,
  `setLocalTransform()`, `setCamera()`/`removeCamera()`/
  `setActiveCamera()`, `setRenderable()`/`removeRenderable()`) is atomic:**
  it either fully succeeds, or returns `Result::Err` having changed
  nothing at all — every precondition (handle validity; for `setParent()`,
  the cycle check) is validated before any state change.
- **Multi-entity enumeration order is deterministic and specified:** any
  World API enumerating more than one entity (`renderableEntities()`, see
  below) iterates in **ascending slot-index order** — combined with the
  LIFO free-list rule above, this makes the order a pure, reproducible
  function of the exact sequence of `createEntity()`/`destroyEntity()`
  calls a caller makes, not an unspecified property of an internal
  container. Required so a multi-entity image-regression golden (the
  Human Review Decision Table, item 14) never depends on undefined
  ordering for its own reproducibility. (`updateTransforms()`'s own internal traversal
  order is not a public contract — see Non-functional below for why it
  does not need to be.)

**Transform and hierarchy** (see
[ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md))

- Every entity has exactly one `Transform` (mandatory, not optional):
  `Vec3 localPosition`, `Vec3 localEulerAnglesRadians`, `Vec3 localScale`
  (default `{1,1,1}`).
- `World::setLocalTransform(EntityId, Transform) -> Result<void,
  WorldError>`, `World::getLocalTransform(EntityId) const -> Result<
  Transform, WorldError>`.
- `World::setParent(EntityId child, EntityId parent) -> Result<void,
  WorldError>` — `parent == kInvalidEntityId` clears to root. Returns
  `WorldError::WouldCreateCycle` (checked before any mutation) if `parent`
  is `child` itself or a descendant of `child`; returns
  `WorldError::InvalidEntity` if either handle is stale. **Preserves the
  child's own *local* transform; does not preserve its *world*
  transform** — `setParent()` never reads or writes `Transform` fields,
  so reparenting generally changes the child's world position/orientation
  as a disclosed side effect (unless the old and new parent share the
  same world matrix). A caller wanting the child's world transform to stay
  fixed across a reparent must compute and set the appropriate new local
  transform itself via `setLocalTransform()` — no automatic "preserve
  world transform" reparent operation exists in this round (would require
  a general 4×4 matrix inverse plus a TRS decomposition; see
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)).
- `World::getParent(EntityId) const -> Result<EntityId, WorldError>`
  (returns the invalid sentinel for a root entity, not an error).
- `World::updateTransforms()` recomputes every entity's world matrix in
  one traversal, visiting each entity strictly after its own parent.
  `World::getWorldMatrix(EntityId) const -> Result<std::array<float, 16>,
  WorldError>` reflects state as of the most recent `updateTransforms()`
  call — an explicit, documented contract, not an implicit assumption.
- **Math contract, fully specified** (see
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)'s
  own "Math contract" subsection for the full statement and its evidence):
  column-major matrix layout (matching `DrawItem::objectToWorld`'s own
  existing contract); column-vector composition with parent on the left
  (`worldMatrix = parentWorldMatrix · localMatrix`, matching
  `minimal_mesh.slang`'s own vertex-stage `mul()` chain); right-handed,
  Y-up coordinates (matching `lookAt()`'s own established convention);
  `localMatrix = T · R · S`; and Euler-angle composition
  `R = Ry(yaw) · Rx(pitch) · Rz(roll)` — the one piece of this contract
  with no prior precedent in this codebase, fixed here arbitrarily but
  precisely. This is a fully-specified internal contract of
  `atlantis::world`, not a new general-purpose `Atlantis::Math` module.

**Camera**

- Optional per-entity `Camera` component: `float fovYRadians`, `float
  nearZ`, `float farZ`. **No aspect-ratio field, and no position/
  orientation fields of its own** — aspect is Runtime's own per-frame
  responsibility (computed from the current swapchain extent, exactly
  matching every existing windowed demo's own established pattern), and a
  Camera entity's position/orientation come entirely from its own
  `Transform` (so a camera can be parented, e.g. attached to a moving
  rig, using the same hierarchy every other entity uses). This is a fixed
  responsibility boundary, not left ambiguous.
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
  ::AssetId meshAsset` only — no material reference (see Non-Goals).
- `World::setRenderable(EntityId, Renderable) -> Result<void,
  WorldError>`, `World::removeRenderable(EntityId) -> Result<void,
  WorldError>`, `World::getRenderable(EntityId) const -> Result<
  Renderable, WorldError>`.

**Multi-entity traversal**

- `World::renderableEntities() const` returns an enumerable, read-only
  view (exact container/return type a Plan-stage detail) of every live
  entity carrying a `Renderable` component, for a composition root to
  iterate once per frame.

**Extraction / Runtime adapter** (see
[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md))

- Runtime, not World, performs: `world.updateTransforms()`; resolving the
  active camera to a view/projection matrix pair — the **view** matrix
  built by extracting an eye position and a normalized right/up/forward
  basis directly from the camera entity's own world matrix columns (a
  Camera looks down its own local −Z axis) and feeding them into the
  existing, unmodified `lookAt()`-shaped construction every windowed demo
  already uses (no general 4×4 matrix inverse, and no "camera must be
  unscaled" precondition — a TRS matrix's basis columns stay mutually
  orthogonal under arbitrary per-axis scale; see
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
  own Decision step 3); the **projection** matrix from `Camera`'s
  `fovYRadians`/`nearZ`/`farZ` and the current swapchain aspect ratio —
  and writing both into the existing camera uniform `Buffer`; resolving
  each Renderable entity's `AssetId` through a resolution mechanism
  entirely **private** to Runtime's own composition object (never a
  global mutable Asset database, never a type World or any other module
  depends on; this Spec fixes only its `AssetId`-in/`Mesh`+`Material`-or-
  not-found-out shape, not a concrete container type — a Plan-stage
  detail); building one `renderer::DrawItem` per Renderable entity, in
  `renderableEntities()`'s own ascending-slot-index order; and calling the
  existing, unmodified `Renderer::drawFrame()` once per frame with the
  full multi-item span.
- No RHI, Renderer, RenderGraph, Vulkan Backend, or Platform type is ever
  named, included, or constructed inside `src/world/`.
- **Runtime's existing windowed `RenderTarget` cannot be pixel-read-back
  or automatically compared against a golden** — confirmed by direct
  inspection of `src/vulkan_backend/src/vulkan_presentation.cpp` (a
  swapchain-backed `RenderTarget`'s `imageUsage` carries
  `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`
  only, never `TRANSFER_SRC_BIT`, unchanged since Spec 0013), and this
  Spec does not add that capability (see Non-Goals). Runtime's own
  windowed verification for this Spec's multi-entity scene is therefore a
  GPU smoke test plus manual, by-eye comparison, exactly as Spec 0013
  already established — the headless `OffscreenTarget`/image-regression
  path (below) is the **only** automated pixel-comparison path this
  Spec's own scene gets.

### Non-functional

- **Performance:** not a goal beyond "a single, full-traversal
  `updateTransforms()` and a single extraction pass per frame do not
  stall or busy-spin" at this Spec's own validation scale (a handful of
  entities) — the same bar every prior spec in this line has set. No
  dirty-flag optimization, no parallel traversal.
- **Memory:** World owns all entity/component data as plain value types
  in its own internal storage (a slot map, per
  [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)) —
  no shared ownership, no `shared_ptr` aliasing of component data, no
  global/static World instance anywhere (per [AGENTS.md](../AGENTS.md)'s
  no-singleton rule).
- **Portability (within the Vulkan-only Phase 1 constraint):** World
  itself has no platform or graphics-API dependency of any kind and is
  portable by construction; verified only via Windows Runtime, matching
  every prior spec's own verified-platform scope.
- **Threading:** single-threaded throughout, matching
  [ADR-0004](../adr/0004-phase1-threading-baseline.md)'s existing Phase 1
  baseline exactly. World is not internally thread-safe and documents
  this at its own public API, per [AGENTS.md](../AGENTS.md)'s existing
  rule; no concurrent mutation during `updateTransforms()` traversal is
  possible or supported.
- **Ownership:** RAII throughout; World is the sole owner of every entity
  and component it holds; `EntityId` is a non-owning value handle; no
  public accessor exposes a reference/pointer into World's own storage
  (see [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)).
- **Determinism and ordering:** `renderableEntities()` (and any future
  multi-entity enumeration API) iterates in ascending slot-index order,
  with a LIFO free list governing slot reuse — a fully specified,
  reproducible function of World's own mutation history (see Requirements
  above and
  [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)).
  `updateTransforms()`'s own **internal** traversal order is deliberately
  *not* a public contract: its only fixed requirement is topological
  (every entity visited strictly after its own parent), and any traversal
  satisfying that requirement produces byte-identical world-matrix values,
  since each entity's computation reads only its own immediate parent's
  already-finalized world matrix — this internal order is never externally
  observable through any World API, unlike enumeration order, and
  therefore needs no equivalent guarantee (see
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)).
- **Atomicity:** every mutating World operation either fully succeeds or
  returns `Result::Err` having changed nothing — no partially-applied
  mutation is ever observable, including on a `WorldError::WouldCreateCycle`
  or `WorldError::InvalidEntity` failure (see Requirements above).
- **Error handling:** every recoverable World operation returns
  `atlantis::Result<T, WorldError>`, matching every existing module's own
  convention, extended here to World for the first time — including
  stale/invalid `EntityId` use, which this Spec treats as recoverable
  rather than a programmer-error assertion (see Requirements' "Entity
  lifecycle and identity" above for the concrete, `assert.h`-grounded
  reason, and the Human Review Decision Table below for this as an
  explicit, confirmable choice). Genuine internal invariant violations
  (e.g. the update traversal's own defense-in-depth "never revisit an
  already-visited entity" check —
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md))
  use `ATLANTIS_CHECK` specifically (not `ATLANTIS_ASSERT`, which compiles
  to a no-op whenever `NDEBUG` is defined) so a defense-in-depth invariant
  guard stays active in Release builds too, never a `Result`.

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
  -> Atlantis::AssetSystem    (AssetId type only -- asset_id.h, nothing
                                else)

No dependency from Atlantis::World on Atlantis::Renderer, RHI, Vulkan
Backend, RenderGraph, Shader System, or Platform. No dependency from
Atlantis::Renderer, RHI, or any other existing module on Atlantis::World.
```

### Validation scene (illustrative, exact values a Plan-stage detail)

Runtime constructs one `World` at startup, alongside its existing
already-loaded `minimal_cube` `Mesh` and fixed `Material`: several
entities (e.g. five), each given a `Transform` at a distinct
`localPosition` (and, optionally, a distinct `localEulerAnglesRadians`,
to visibly demonstrate rotation) and a `Renderable` referencing
`minimal_cube`'s `AssetId` — at least one of them parented to another, to
exercise the hierarchy — plus one additional entity carrying only a
`Camera` (no `Renderable`), set as the active camera via
`setActiveCamera()`, positioned via its own `Transform` to frame the
other entities. Every frame, Runtime calls `updateTransforms()`, then the
extraction path fixed by
[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md),
then the existing, unmodified `Renderer::drawFrame()` once with the full
multi-item span.

## Architectural Impact

This Spec introduces a new top-level module and four architectural
decisions, filed as four `Proposed` ADRs (all requiring Human Review to
reach `Accepted` before this Spec can reach `Approved`, per
[AGENTS.md](../AGENTS.md)):

1. **Module boundary and ownership** —
   [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md).
2. **Entity identity and handle invalidation** —
   [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md).
3. **Transform hierarchy, composition, and update model** —
   [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md).
4. **World-to-Renderer extraction and asset resolution boundary** —
   [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md).

**No existing `Accepted` ADR's conclusions are reopened or modified.**
Each new ADR references and builds on ADR-0001–0005, ADR-0022, ADR-0032,
ADR-0033, ADR-0035, ADR-0043–0045, and ADR-0046/0047, without altering
any of them.

**No new public API in any existing module.** Confirmed by this Spec's
own Independent Review above: `Atlantis::Renderer`, `Atlantis::RHI`,
`Atlantis::VulkanBackend`, `Atlantis::AssetSystem`, `Atlantis::Platform`,
and `Atlantis::ShaderSystem` are consumed by this Spec's design exactly as
they exist today.

**ADR-0032 five-layer placement.** `Atlantis::World` sits in the
**Authoritative Runtime** conceptual layer, alongside Atlantis Runtime
itself — a non-binding, illustrative placement only, per ADR-0032's own
terms; the authoritative eleven-module source-ownership view (see
[ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)) is
what a build/dependency check actually enforces.

**ADR-0033 compliance.** Runtime owns the one real `World` instance;
nothing outside Runtime observes or mutates it in this round's scope — the
same trivial satisfaction Spec 0013 already established for its own
bootstrap state, now applied to genuine "engine world state" for the
first time. World's own public API (by-value access, `Result`-returning,
index+generation handles — see
[ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md))
is already shaped compatibly with ADR-0033's eventual Client-boundary
principle, without this Spec claiming to build that boundary itself.

**ADR-0035 compliance — addressed explicitly, per that ADR's own
procedural requirement.** This Spec's `World`/`Transform`/`Camera`/
`Renderable` representation **is** the runtime-execution representation;
no distinct authoring-facing representation or bake/compile step exists
in this round, because no authoring tool or Editor consumes World data yet
(see Non-Goals). This is a considered, explicit choice, not a silent
default: the next Candidate Backlog item this Spec's own registry update
below names, Scene Asset/Serialization, is where an authoring-facing
representation and a bake step feeding this same runtime `World`
structure are expected to be introduced.

**`docs/architecture/module_boundaries.md`, deferred.** That document
predates this Spec and does not describe World at all yet. Per this
repository's own established pattern (Spec 0012/0013's identical
treatment), reconciling it is deferred to a future Plan/docs-sync, not
performed by this Spec itself, and is not a blocker to this Spec's own
approval.

**Registry update (specs/README.md).** "World/ECS Foundation," formerly
Candidate Order 2 in the Candidate Spec Backlog, is promoted to Section A
as Spec 0014 alongside this Spec's own drafting, per that registry's own
backlog-maintenance rule. Its "Depends On" was Spec 0013 (`Approved`,
implemented) — satisfied. Every remaining candidate's own Candidate Order
number, and every cross-reference naming "World/ECS" or "Candidate 2," is
renumbered/corrected as a mechanical index update, per the registry's own
maintenance rules — no candidate's own scope or real dependencies change.
Android Platform and Vulkan Presentation (Candidate Order 1) is
explicitly **not** reordered, reprioritized, or reinterpreted by this
Spec — it remains `Candidate`, unimplemented, at its own unchanged
position; drafting this Spec ahead of it is, as with Spec 0012 and Spec
0013 before it, an explicit human-directed continuation, not a finding
that Android's own scope or dependencies changed.

## Human Review Decision Table

Fourteen decisions this Spec asks Human Review to confirm, reject, or
amend — none is silently locked as "just an implementation detail."
Every row states this Spec's own recommendation and the trade-off a
reviewer is actually being asked to weigh; full reasoning and
Alternatives Considered live in the ADR each row links to (or in this
Spec's own sections, for the two rows with no dedicated ADR). Items 2–4
were previously folded into a single "Entity ID/handle representation"
bullet; items 7–8 and 12 were previously implied rather than stated as
their own confirmable rows — this table makes each one individually
visible, per this round's own review finding (Independent Review Round 2,
item 9).

| # | Decision | Recommendation | Key trade-off / why this needs sign-off | Source |
|---|---|---|---|---|
| 1 | New, independent top-level module (`Atlantis::World`), or a private submodule of `src/runtime/`? | New top-level module, matching Asset System's own precedent (ADR-0043). | A new top-level module is a permanent structural commitment; folding into Runtime's private `RuntimeHost` library would forecloses independent unit testing and a future non-Runtime consumer. | [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md) |
| 2 | `EntityId` shape and generation width. | Index (`uint32_t`) + generation (`uint64_t`), 16 bytes. | A 64-bit generation closes the counter-wraparound risk a 32-bit field would leave open, at the cost of doubling the handle's own size. | [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md) |
| 3 | Stale/invalid `EntityId` detection: `Result::Err`, or an assertion (`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`)? | `Result<T, WorldError>` — `WorldError::InvalidEntity`. | `ATLANTIS_ASSERT` compiles to a no-op whenever `NDEBUG` is defined (verified against `assert.h`), silently disabling detection in every Release build; `ATLANTIS_CHECK` aborts the whole process for what is often ordinary single-threaded caller bookkeeping, not a memory-safety invariant. Re-grounded in this evidence, not merely in ADR-0033 (which this Spec has no second Client to exercise against yet). | [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md) |
| 4 | Do public World accessors return by-value copies, or references/pointers into World's own storage? | By-value only — every getter returns `Result<T, WorldError>` by value; every setter takes its argument by value. | Stricter than ADR-0033 strictly requires (that rule is about cross-module Client access) — adopted because World's own internal slot array can reallocate on growth, which would otherwise dangle any previously-returned reference. | [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md) |
| 5 | Fixed-type component storage, or a generic ECS registry? | Fixed-type storage — a mandatory `Transform` plus two optional components (`Camera`, `Renderable`) directly on each entity's own record; no type-erased component pool, no runtime component-type registration. | A future component type (e.g. a Light) requires extending the fixed entity record, not registering a new type generically — accepted because this Spec names exactly two optional component kinds and no generic-registration consumer exists yet. | This Spec's own "Why this stays a minimal World, not a general ECS" above |
| 6 | Transform hierarchy update strategy, and when cycle detection runs. | Explicit, single-threaded, once-per-frame `updateTransforms()` (no eager per-setter propagation, no dirty-flag scheme); cycle prevention at `setParent()` mutation time (ancestor-chain walk, `Result::Err` before any state change), with a defensive traversal-time `ATLANTIS_CHECK` as a last-resort invariant guard only. | A world matrix read without a following `updateTransforms()` call silently reflects stale data — a documented contract, not an automatic dirty check; accepted for this Spec's own explicit, single-threaded model. | [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md) |
| 7 | Does destroying a parent entity cascade to its descendants, reparent them, or leave them orphaned? | Cascades — `id` and every transitive descendant are destroyed together, in one atomic call; the active camera is cleared automatically if implicated. | Simplest semantics to reason about, but no "detach children first" escape hatch exists — a real, disclosed constraint a future Plan/caller must design around if it ever needs a subtree to outlive its root. | [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md) |
| 8 | Does `setParent()` preserve the child's *local* transform, or its *world* transform? | Preserves *local*; world transform generally changes as a disclosed side effect. | The alternative (auto-preserving world transform) requires a general 4×4 matrix inverse plus a TRS decomposition — real machinery no other part of this Spec's minimal scope needs, for a capability this Spec's own validation scene does not exercise. | [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md) |
| 9 | World↔Asset System reference boundary: does `Renderable` reuse `atlantis::asset_system::AssetId` directly, or a World-owned opaque handle? | Reuse `AssetId` directly — one source of truth, no conversion layer; World depends on nothing else from Asset System. | Couples `Renderable` to Asset System's current, path-derived (not rename-durable) identity scheme (ADR-0044) — a future Serialization/Stable-Identity Spec changing that scheme changes `Renderable` directly, with no insulating indirection. | [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md) |
| 10 | Where does the World→Renderer `DrawItem` translation live? | Runtime's own composition-root adapter — never inside World, never a Renderer change, and not a new, separate "Extraction" module ahead of a second real consumer. | Confirmed, by direct inspection, that `Renderer::drawFrame()` already accepts and already iterates a multi-item `DrawItem` span — zero Renderer change needed. | [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md) |
| 11 | Camera ownership and the active-camera rule. | `Camera` is an optional component on an ordinary entity (participates in the Transform hierarchy, e.g. a camera rig), carrying only `fovYRadians`/`nearZ`/`farZ`; exactly one active camera at a time, a single nullable `EntityId` on World. | View/projection matrix computation stays entirely Runtime's own hand-rolled code (basis extraction from the camera's world matrix, fed into the existing `lookAt()`), never moved into World — keeps World free of any graphics-facing convention. | [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md) |
| 12 | Should Runtime's `AssetId`→`Mesh`/`Material` resolution mechanism be a fixed public interface, or a private implementation detail? | Private to `Atlantis::RuntimeHost`'s own composition object — this Spec fixes only its input/output shape (`AssetId` in, a resolved pair or not-found, out), not a concrete container type or a public API. | Locking a public resolver interface now, with only one real consumer and one real asset, would be exactly the premature, unnecessary abstraction AGENTS.md's Golden Rule warns against. | [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md) |
| 13 | Does this Spec preserve every existing public rendering API unchanged? | Yes — confirmed by direct inspection, twice (Independent Review Rounds 1 and 2): `Renderer`, RHI, Vulkan Backend, Platform, Shader System, and Asset System all remain exactly as `Accepted` today, zero modification to any public header, type, or function signature. | Not a judgment call — a factual finding this table records for the reviewer's own direct confirmation, since it is this Spec's own central architectural claim. | Independent Review above |
| 14 | The first multi-entity Runtime validation scene, and its image-regression golden-update-reason category. | Extend Runtime's bootstrap (Spec 0013) with several `minimal_cube` instances at distinct World-driven transforms (one hierarchy relationship exercised) plus one World-driven camera; verify via (a) a **new** headless image-regression fixture/golden under `tests/image_regression/`, citing ADR-0042's own **Accepted Amendment** "Initial baseline bootstrap" category (not "Approved rebaseline" — ADR-0042's own Alternatives Considered explicitly rejects that category as the permanent answer for a first golden, since it needs old-vs-new diff evidence that cannot exist here) — the Amendment's own constraint 5 substitute evidence (visual inspection; zero-diff self-consistency; a real Validation-Layers-clean GPU run; citing ADR-0042's existing calibration) is what a future Plan/Implementation must produce; (b) a windowed Runtime GPU smoke test extension; (c) manual, by-eye windowed verification against that same golden. | Runtime's own windowed swapchain still cannot be pixel-read-back (confirmed: no `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` on it) — the headless path is the only automated pixel-comparison route available. Exact fixture/golden naming and PR sequencing remain Plan-stage details. | This Spec's own Testing & Verification Plan; [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md) |

### Approval readiness

**This Spec is not yet ready to move to `Approved`.** Per
[AGENTS.md](../AGENTS.md) and [specs/README.md](README.md), that requires:
(1) a human reading this Spec and
[ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
together and recording explicit Human Review Approval — the table above is
the complete, one-time set of items that approval needs to either accept
as recommended or direct a change to, with no item left for a later,
separate round; and (2) all four ADRs moving from `Proposed` to
`Accepted` as part of that same approval, per AGENTS.md's ADR workflow.
Neither has happened yet. This round's own work (Independent Review Round
2 above) closed every internal contradiction, omission, and overclaim a
targeted, evidence-driven review found — including one item (golden-
update-reason category) that was re-verified against ADR-0042's actual
text and confirmed correct as originally drafted, not changed — but a
self-review is not Human Review and does not substitute for it. No Plan
may be drafted, and no Implementation may begin, until that approval is
recorded.

## Alternatives Considered

- **Defer World/Scene entirely and let a future Scene Asset/Serialization
  Spec introduce both an authoring format and a runtime representation
  together.** Rejected: there is no runtime representation to bake into
  yet, and `specs/README.md`'s own Candidate Backlog already orders
  Serialization and Stable Identity as depending on World/ECS, not the
  reverse — building the runtime side first is the dependency-correct
  order, and gives the eventual serialization Spec a real, concrete
  structure to bake into and test against rather than designing both at
  once.
- **Adopt a real, general-purpose ECS library or design now**, since
  `docs/project-blueprint.md`'s own external draft names ECS as a
  long-term direction. Rejected for this round: no second component type,
  no data-driven authoring tool, and no measured performance need exists
  yet to validate a general ECS's own added complexity against — see
  "Why this stays a minimal World, not a general ECS" above, and
  the Human Review Decision Table, item 5.
- **Give World a Renderer dependency so it can vend `DrawItem`s (or even
  own GPU `Mesh`/`Material`) directly, simplifying Runtime's own
  composition code.** Rejected: violates this Spec's own CPU-only/
  backend-independent requirement for World, and would make World's own
  unit tests require a real `Device` — see
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
  own Alternatives Considered.
- **Promote a shared math library into Atlantis Core now**, since
  `module_boundaries.md`'s own `PROPOSED` text already anticipates one.
  Rejected for this round: no second genuine consumer exists yet to
  validate a shared library's shape against — see
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)'s
  own Alternatives Considered.

## Testing & Verification Plan

- **Unit tests (GPU-independent), `tests/world/`, linking
  `Atlantis::World` and `Atlantis::Core`/`Atlantis::AssetSystem` (for
  `AssetId`) only — no `Device`, no GPU, and no real window required:**
  - Entity lifecycle: `createEntity()` always succeeds and returns a
    valid handle; `destroyEntity()` invalidates the entity and (recursive
    case) every transitive descendant in one call; every subsequent
    operation against any of those handles returns
    `WorldError::InvalidEntity`; a destroyed and reused slot's new
    `EntityId` (different generation) does not alias the old, stale one.
  - Hierarchy: `setParent()` succeeds for a valid, non-cycle-forming
    request; rejects (`WorldError::WouldCreateCycle`) a direct self-parent,
    a two-hop cycle, and a longer transitive cycle, in each case leaving
    the hierarchy unchanged (verified by re-reading `getParent()`
    afterward — the atomicity contract); `setParent()` leaves the child's
    own `getLocalTransform()` value byte-identical while its
    `getWorldMatrix()` (after `updateTransforms()`) changes when the new
    parent's world matrix differs from the old one — the local-vs-world
    preservation contract; `updateTransforms()` produces the expected
    world matrix for a multi-level chain (root → child → grandchild)
    against a hand-computed expected result, verifying the full math
    contract (column-major layout, `parentWorld · local` composition,
    `T · R · S` order, and the fixed `Ry · Rx · Rz` Euler order) against
    independently hand-computed matrices, not merely "does it run";
    destroying a mid-chain entity cascades to its own descendants, leaving
    unrelated siblings/ancestors untouched.
  - Camera: `setActiveCamera()` fails (`WorldError::NoCameraComponent`)
    against an entity with no `Camera`; destroying the active camera
    entity clears `activeCamera()` to `std::nullopt` automatically;
    `activeCamera()` starts `std::nullopt` on a freshly-constructed
    `World`.
  - Renderable and traversal: `renderableEntities()` returns exactly the
    set of live entities currently carrying a `Renderable`, correctly
    excluding entities with only a `Transform`, a destroyed entity, and
    (after `removeRenderable()`) a previously-renderable entity.
  - **Determinism:** a fixed sequence of `createEntity()`/`destroyEntity()`
    calls that frees and reuses slot indices (exercising the LIFO free
    list) produces the exact same `renderableEntities()` ascending-slot-
    index ordering across repeated, independent runs of the same test —
    the concrete guarantee a multi-entity image-regression golden's own
    reproducibility depends on (Human Review Decision Table, item 14).
  - **Atomicity:** a `setParent()` call that fails with
    `WorldError::WouldCreateCycle`, and a `destroyEntity()`/
    `setLocalTransform()`/etc. call that fails with
    `WorldError::InvalidEntity`, each leave every observable World state
    (parent links, transforms, component presence, entity validity)
    byte-identical to immediately before the call.
  - `EntityId` value semantics: equality, the invalid sentinel's own
    `isValid()` result, and that a plain `std::vector<EntityId>`/`std::
    unordered_map<EntityId, ...>` usage compiles and behaves as expected
    (exercising the 16-byte handle as an ordinary copyable value).
  - **Honest scope limit, stated explicitly, not silently overclaimed:**
    the 64-bit generation counter's own overflow-safety margin
    ([ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s
    `2^64`-cycle reasoning) is a design-time, quantitative argument, not
    something any test exercises — actually driving one slot through
    anywhere near `2^64` destroy/reuse cycles is not a real, runnable test
    at any practical timescale. Only the ordinary, small-scale generation-
    mismatch case (one destroy, one reuse, one stale-handle check) is
    unit-tested above.
- **GPU-required tests (Windows/Vulkan, `gpu`-labeled), extending
  `tests/runtime/` and/or `tests/vulkan_backend/`:**
  - A Runtime GPU smoke test constructing the full validation-scene
    composition (World with several Renderable entities plus one active
    camera), confirming `Renderer::drawFrame()` succeeds against the
    resulting multi-item `DrawItem` span with Vulkan Validation Layers
    reporting zero warnings/errors — mechanical correctness, matching
    Spec 0013's own GPU smoke test precedent, extended to a multi-item
    span for the first time.
- **Image regression (headless), `tests/image_regression/`:** a new
  fixture and golden for this Spec's own multi-entity validation scene —
  see the Human Review Decision Table, item 14, for the recommended
  "Initial baseline bootstrap" category and the three-layer verification
  split. Exact fixture composition (which entities, which transforms) a
  Plan-stage detail, fixed to be deterministic and visually distinguishable
  from the existing single-cube golden.
- **Manual verification (Windows, real window, real GPU):** a visible
  window shows several distinct cube instances at their expected relative
  positions (including the exercised parent/child relationship — moving/
  rotating a parent visibly moves its child too) and the expected camera
  framing, matching the new golden by eye; no crash, no Vulkan Validation
  Layer warning or error across a full run including resize/minimize/
  restore/close, matching every prior spec's own established manual
  verification bar.
- **Regression, unchanged:** every existing GPU-independent test suite,
  every existing `gpu`-labeled test suite, and the existing
  `minimal_cube` headless golden/regression test continue to pass — this
  Spec adds a new module, a new test directory, and a new golden; it does
  not modify any existing test, asset, shader, or golden file.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of this Spec's implementation, per
  [AGENTS.md](../AGENTS.md).

## Risks & Open Questions

- **Whether the recommended new image-regression golden lands in the same
  PR as the rest of this Spec's implementation, or as a follow-up** — a
  Plan-stage sequencing detail, not fixed here (see the Human Review
  Decision Table, item 14).
- **The exact `EntityId` invalid-sentinel bit pattern, and the exact
  per-axis rotation-matrix element layout implementing the fixed
  `R = Ry(yaw) · Rx(pitch) · Rz(roll)` composition** are left to the
  Plan — this Spec fixes the handle's shape (index + 64-bit generation)
  and the full math contract (layout, multiplication order, handedness,
  TRS order, and the Euler axis order itself — see
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)'s
  own "Math contract"), not the literal sentinel value or the mechanical
  per-element formula implementing an already-fixed rotation matrix.
- **Whether `renderableEntities()`'s exact return type is a
  `std::vector<EntityId>` snapshot, a lazy view, or a callback-based
  `forEach`** is left to the Plan — this Spec fixes the capability (a
  read-only, complete enumeration of live Renderable entities) and its
  contract (valid as of the call, not a live/invalidating iterator held
  across a subsequent World mutation), not the concrete C++ shape.
- **Whether reusing `atlantis::asset_system::AssetId` directly (this
  Spec's own recommendation, the Human Review Decision Table item 9)
  proves awkward once a real Serialization and Stable Identity Spec is
  drafted** is a named, honest, deferred risk — not something this Spec
  claims to have preempted.
- **Whether the camera basis-extraction convention (a Camera entity looks
  down its own local −Z axis) reads intuitively to a future author hand-
  placing a camera entity**, versus a possible alternative (+Z-forward)
  convention some other engines use — this Spec fixes one, documented,
  internally-consistent convention (matching `lookAt()`'s own existing
  result) because it has to pick one, not because a strong argument
  favors it over the alternative; a future Spec is free to revisit if this
  proves confusing in practice.
- **Runtime's own new AssetId→Mesh/Material resource table's exact
  container/lookup-failure policy** (skip-and-log vs. fail-the-frame) is
  left to the Plan — this Spec fixes only that the condition is
  recoverable and Runtime-classified, not its exact handling (see
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)).

## Out of Scope / Future Work

**Scene Asset/Serialization** is registered as the next Candidate Backlog
item this Spec's own module boundary is deliberately shaped to hand off
to (see the registry update in
[specs/README.md](README.md) and Architectural Impact above) — an
authoring-facing representation, a bake/compile step (per
[ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md)),
stable cross-session entity/asset identity, and a scene file format all
remain that future Spec's own scope, not designed, previewed, or
scaffolded here. Also remaining out of scope, unaffected by this Spec:
Android Platform and Vulkan Presentation; Tool/Editor Connection Protocol
(the first real exercise of
[ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)'s Client
model, now that real "engine world state" exists to protect); a Gameplay
SDK; a general, data-driven, or multi-threaded ECS; textures/sampling;
PBR materials, lighting, and shadows; animation; post-processing — all
remain later, separately-specced work, per
[docs/project-blueprint.md](../docs/project-blueprint.md) and this
document's own Non-Goals above.
