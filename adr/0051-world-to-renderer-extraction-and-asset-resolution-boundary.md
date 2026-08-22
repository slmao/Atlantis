# ADR 0051: World-to-Renderer Extraction and Asset Resolution Boundary

- **Status:** Proposed
- **Date:** 2026-08-22
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)

## Context

`World` (per [ADR-0048](0048-world-scene-module-boundary-and-ownership.md))
is required to stay CPU-only and backend-independent — no RHI, Vulkan
Backend, Renderer, or Platform type anywhere in its own public surface or
implementation. `Atlantis::Renderer`'s public API, unchanged and
unmodified by this Spec (confirmed by direct inspection of
`src/renderer/include/atlantis/renderer/{draw_item,renderer}.h` and
`src/renderer/src/renderer.cpp`), already accepts everything this Spec's
own multi-entity validation scene needs:

- `atlantis::renderer::DrawItem` already carries `const Mesh*`,
  `const Material*`, and a raw `std::array<float, 16> objectToWorld` —
  nothing here is new.
- `Renderer::drawFrame(..., std::span<const DrawItem> drawItems, ...)`
  already accepts a **span** of `DrawItem`, and
  `src/renderer/src/renderer.cpp` already iterates the full span
  (`for (const DrawItem& item : drawItems)`) — confirmed by direct
  inspection, not assumed. No existing composition root has ever passed
  more than one `DrawItem` in that span, but the capability itself is
  already implemented, `Accepted`, and untouched by this Spec.
- `Renderer::drawFrame()` "never touches raw camera math, only binds the
  [caller-written] Buffer it is handed" (`renderer.h`'s own doc comment) —
  view/projection matrix computation has always been, and remains, the
  composition root's own responsibility. Every existing windowed demo
  already hand-rolls its own `lookAt()`/`perspective()` helpers for
  exactly this reason.

Something, therefore, must sit between World's CPU-only entity/component
data and Renderer's existing `DrawItem`/camera-buffer inputs, without
World itself depending on Renderer (which would transitively pull RHI —
`Mesh`/`Material` own RHI `Buffer`/`Pipeline` handles — into a module
this Spec requires to stay backend-independent) and without Renderer
changing at all. `atlantis::renderer::Mesh`/`Material` are also, by
[ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
own already-`Accepted` design, move-only, single-owner, GPU-backed types
with **no built-in caching or deduplication** — `createMesh()`/
`createMaterial()` each produce one new, independent instance per call —
so something must also own the mapping from a Renderable's asset
reference to an already-constructed GPU `Mesh`/`Material` pair, without
constructing a new one per entity per frame.

## Decision

**The World→Renderer translation is Runtime's own responsibility, as a
composition-root adapter — never World's, and never a Renderer change.**
Concretely, once per frame:

1. Runtime calls `world.updateTransforms()`
   ([ADR-0050](0050-transform-hierarchy-composition-and-update-model.md)).
2. Runtime resolves `world.activeCamera()`. If unset (`std::nullopt` —
   the only reachable "no camera" state, since `destroyEntity()` already
   clears an active camera reference automatically per ADR-0050, and
   `setActiveCamera()` itself only accepts an entity that currently has a
   `Camera` component), this is a recoverable, Runtime-classified
   extraction failure — this Spec's own validation scene always sets one
   (see the related Spec's Requirements), so this path is exercised only
   by a deliberately-misconfigured test, matching this codebase's existing
   convention of testing an error path explicitly rather than only its
   success path.
3. Runtime computes that camera entity's **view matrix as the inverse of
   its own world matrix** (`World::getWorldMatrix()` on the active camera
   entity) — a standard technique that needs no separate `lookAt()`-style
   function, and a **projection matrix** from the `Camera` component's
   `fovYRadians`/`nearZ`/`farZ` fields plus the current swapchain aspect
   ratio (`Presentation::metadata()`, computed per-frame exactly as every
   existing windowed demo already does, unchanged). Both matrices are
   written into the existing camera uniform `Buffer`, through the exact
   same caller-owned, per-frame write path Spec 0007/0013 already fixed —
   **no RHI or Renderer API changes**. This is genuinely new *code*
   (matrix inversion, reading `Camera`/`Transform` instead of hardcoded
   eye coordinates) but reuses the exact existing capability contract
   every prior spec in this line already established; it does not, and
   this Spec does not claim it needs to, promote this math into World or
   into a shared Core library (see
   [ADR-0048](0048-world-scene-module-boundary-and-ownership.md)'s own
   Alternatives Considered).
4. Runtime iterates `world.renderableEntities()` (every entity with both a
   `Transform` and a `Renderable`). For each: Runtime resolves
   `Renderable.meshAsset` (an `atlantis::asset_system::AssetId`) through
   its own resource table — a plain, Runtime-owned
   `std::unordered_map<AssetId, /* already-constructed Mesh+Material */>`
   populated once at startup from the same already-cooked assets Spec
   0013 already loads (for this Spec's own validation scene: exactly one
   entry, `minimal_cube`'s `AssetId` mapped to the one `Mesh` and one
   fixed `Material` Runtime already constructs today — looked up per
   entity instead of hardcoded, not a new construction per entity or per
   frame). An `AssetId` with no resource-table entry is a recoverable,
   Runtime-classified extraction error (exact handling — skip that entity
   and log, or fail the frame — a Plan-stage detail; the category is
   fixed here as recoverable, matching every other per-frame condition
   Spec 0013 already classified this way).
5. For each resolved Renderable entity, Runtime builds one
   `renderer::DrawItem{ mesh, material,
   world.getWorldMatrix(entity) }` and appends it to one
   `std::vector<DrawItem>` for the frame.
6. Runtime calls the existing, unmodified
   `Renderer::drawFrame(commandList, colorTarget, depthTarget,
   cameraUniformBuffer, drawItems, finalColorState)` **exactly once**,
   passing the full multi-item span built in step 5 — the same call every
   existing windowed composition root already makes, now with more than
   one `DrawItem`.

- **World exposes only a read-only, CPU-side traversal surface for this
  purpose** — `renderableEntities()` (or an equivalent enumerable view;
  exact return shape a Plan-stage detail) and per-entity, by-value
  accessors (`getWorldMatrix()`, `getRenderable()`, `getCamera()`,
  `activeCamera()`) — no `DrawItem`, `Mesh`, `Material`, or any Renderer/
  RHI type is named, returned, or constructed anywhere inside World.
- **Camera view/projection computation stays Runtime's own hand-rolled
  code**, exactly the same pattern every existing composition root
  already uses, now reading its inputs from `World`/`Camera` instead of
  hardcoded literals — not a new shared capability, not moved into World
  or Renderer.
- **Runtime's AssetId→Mesh/Material resource table is new state this Spec
  introduces to Runtime's own composition object** (not existing before
  this Spec, since Spec 0013's own bootstrap held exactly one hardcoded
  `Mesh`/`Material` pair with no lookup at all) — scoped, for this Spec's
  own validation scene, to the same single already-cooked asset Runtime
  already loads; a general multi-asset resource-table implementation
  (eviction, hot-reload, lazy loading) is explicitly not designed here —
  see the related Spec's Non-Goals.

## Consequences

### Positive

- Confirmed, by direct inspection rather than assumption, that **zero**
  change to Renderer's, RHI's, or Vulkan Backend's public API is required
  to display multiple World-driven mesh instances plus a World-driven
  camera — directly satisfying this Spec's own "reuse existing Renderer
  API; raise an explicit architectural objection if the real code proves
  it impossible" instruction, with a definite, verified "it is possible"
  answer.
- Keeps World genuinely backend-independent — it can be constructed,
  populated, traversed, and unit-tested with no RHI, no Renderer, no
  Device, and no window linked in at all, the same practical benefit
  Asset System's own Core-only shape already provides.
- The extraction/adapter logic lives in exactly one place (Runtime), the
  same place every existing composition root's own scene-to-`DrawItem`
  logic (currently one hardcoded item) already lives — this Spec extends
  an existing pattern rather than introducing a new architectural layer
  (no "Scene Renderer" or "Extraction System" module is created).

### Negative / Trade-offs

- Runtime's own composition code grows a real, new responsibility (the
  AssetId→Mesh/Material resource table, the World-driven camera math, the
  per-frame extraction loop) that Spec 0013 did not need — a genuine,
  disclosed increase in Runtime's own scope, not free.
- The resource-table's own scope (one entry, populated once at startup)
  is a deliberate simplification that a future Spec adding a second real
  authored asset will need to generalize — not a general asset-streaming
  or hot-reload design, and not claimed to be one.
- Camera view/projection math remains duplicated, hand-rolled, per-
  composition-root code (now reading World data instead of literals) —
  this ADR does not consolidate it into a shared library, matching
  [ADR-0048](0048-world-scene-module-boundary-and-ownership.md)'s own
  explicit deferral of a general math module.

## Alternatives Considered

- **Give World a `Renderer`/RHI dependency so it can build `DrawItem`
  values (or even own `Mesh`/`Material`) directly.** Rejected: directly
  violates this Spec's own CPU-only/backend-independent requirement for
  World, and would make World's own unit tests require a real `Device` —
  the same reasoning Asset System's own ADR-0043 already used to reject
  an equivalent shape for itself ("Have Asset System depend on RHI/
  Renderer and return a ready-made `atlantis::renderer::Mesh` directly").
- **Introduce a new, separate "Scene Renderer" or "Extraction" module**
  between World and Runtime, owning the AssetId→Mesh/Material table and
  the DrawItem-building logic. Rejected for this round: no second
  consumer of an extraction API exists yet (only Runtime), and Runtime
  already owns the equivalent, smaller-scope logic today (one hardcoded
  `DrawItem`) — generalizing it into its own module ahead of a second real
  consumer would be speculative structure this Spec's own minimal-scope
  instruction argues against.
- **Have World compute camera view/projection matrices itself**, given
  `Camera`'s fields and its own Transform math. Rejected: view/projection
  matrices are meaningful only relative to a graphics convention (right-
  handed vs. left-handed, clip-space Z range) that has, by design, always
  been the composition root's own concern (`renderer.h`'s own doc comment:
  "Renderer never touches raw camera math") — folding it into World would
  make World responsible for a graphics-facing convention despite staying
  otherwise backend-independent in every other respect, a real
  inconsistency this ADR avoids by leaving 100% of camera-matrix math with
  Runtime, matching precedent exactly.
- **Cache/deduplicate `Mesh`/`Material` inside Asset System or Renderer**,
  instead of a Runtime-owned resource table. Rejected: both modules'
  own `Accepted` design explicitly forecloses this —
  `atlantis::renderer::createMesh()`/`createMaterial()` are documented,
  `Accepted` "no cache, no deduplication" free functions
  ([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)),
  and Asset System never constructs a GPU resource at all
  ([ADR-0043](0043-asset-system-module-boundary.md)) — a resource cache,
  if one is needed, belongs to whichever composition root actually
  constructs GPU resources, exactly where this ADR places it.
