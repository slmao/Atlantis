# ADR 0051: World-to-Renderer Extraction and Asset Resolution Boundary

- **Status:** Proposed
- **Date:** 2026-08-22
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md)
- **Revision (2026-08-22, pre-Human-Review evidence pass):** replaced the
  "view = inverse(world matrix)" description with a precise, precedent-
  reusing construction (extract eye position and a normalized basis from
  the camera entity's world matrix columns, feed them into the exact same
  `lookAt()`-shaped formula every existing demo already uses) that needs
  no general 4×4 matrix inverse and no unscaled-camera precondition — see
  Decision step 3. Softened the resource-table container type to
  illustrative-only and stated explicitly that it is Runtime-**private**,
  not a new public API this ADR fixes. Added an explicit statement that
  Runtime's existing windowed `RenderTarget` cannot be pixel-read-back or
  auto-compared against a golden — windowed verification stays smoke-test-
  and-manual-only, headless remains the only pixel-comparison path — see
  Consequences. No change to this ADR's `Proposed` status; still pending
  Human Review.

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
3. Runtime computes that camera entity's **view matrix by extracting an
   eye position and an orthonormal basis directly from its own world
   matrix's columns, then feeding them into the exact same
   `lookAt()`-shaped construction `examples/minimal_renderer_demo/main.cpp`
   already implements** — not a general 4×4 matrix inverse, and not a new
   view-matrix algorithm. Precisely: for a world matrix built as
   `T · R · S` (per
   [ADR-0050](0050-transform-hierarchy-composition-and-update-model.md)'s
   own Math contract), columns 0/1/2 are `R`'s own orthonormal basis
   columns each scaled by `S`'s corresponding factor — **mutually
   orthogonal regardless of scale**, since scaling each of three already-
   orthogonal columns by a (possibly different) non-zero factor preserves
   their pairwise zero dot products; normalizing each column individually
   therefore recovers a valid orthonormal basis even under non-uniform
   scale, with no "the camera must be unscaled" precondition needed.
   Concretely: `eye` = the world matrix's translation column;
   `right` = `normalize(column 0)`; `up` = `normalize(column 1)`;
   `forward` = `normalize(-column 2)` (this fixes the convention **a
   Camera entity looks down its own local −Z axis**, matching the
   existing `lookAt()` function's own already-verified result, whose
   view-space forward is conventionally −Z — confirmed by inspection of
   its own `result[2]/result[6]/result[10] = -fx/-fy/-fz` assignment).
   Runtime then calls the existing, **unmodified** `lookAt(eye.x, eye.y,
   eye.z, eye.x + forward.x, eye.y + forward.y, eye.z + forward.z)` — the
   same function signature every existing windowed demo already calls,
   now fed World-derived values instead of a hardcoded/orbiting eye
   position. No new view-matrix formula is introduced; this is new
   *plumbing* (World → eye/forward extraction → the existing function),
   not new *math*.
   The **projection matrix** comes from the `Camera` component's
   `fovYRadians`/`nearZ`/`farZ` fields plus the current swapchain aspect
   ratio (`Presentation::metadata()`, computed per-frame exactly as every
   existing windowed demo already does, unchanged) — fed into the
   existing, unmodified `perspective()` function. Both matrices are
   written into the existing camera uniform `Buffer`, through the exact
   same caller-owned, per-frame write path Spec 0007/0013 already fixed —
   **no RHI or Renderer API changes**. This math (basis extraction,
   normalization, the `lookAt()`/`perspective()` calls themselves) stays
   Runtime's own hand-rolled code — it is not, and this Spec does not
   claim it needs to be, promoted into World or into a shared Core
   library (see
   [ADR-0048](0048-world-scene-module-boundary-and-ownership.md)'s own
   Alternatives Considered).
4. Runtime iterates `world.renderableEntities()`, in the ascending-slot-
   index order [ADR-0049](0049-entity-identity-and-handle-invalidation.md)
   fixes (every entity with both a `Transform` and a `Renderable`). For
   each: Runtime resolves `Renderable.meshAsset` (an
   `atlantis::asset_system::AssetId`) through its own **resolution
   mechanism** — a plain, Runtime-**private** lookup from `AssetId` to an
   already-constructed `Mesh`/`Material` pair, populated once at startup
   from the same already-cooked assets Spec 0013 already loads (for this
   Spec's own validation scene: exactly one entry, `minimal_cube`'s
   `AssetId` mapped to the one `Mesh` and one fixed `Material` Runtime
   already constructs today — looked up per entity instead of hardcoded,
   not a new construction per entity or per frame). **This ADR fixes only
   the existence and the input/output shape of this resolution step
   (`AssetId` in, an already-constructed `Mesh`/`Material` pair or a
   not-found outcome, out) — not its concrete container type or data
   structure** (a `std::unordered_map`, a `std::vector` linear-scanned at
   this Spec's own tiny scale, or any equivalent are all conforming
   choices; the exact one is a Plan-stage implementation detail, not fixed
   here, so this ADR does not lock in a public interface no real second
   consumer has asked for yet). It is **entirely internal to
   `Atlantis::RuntimeHost`'s own composition object** — not a new public
   API, not a type any other module (including World) names or depends
   on, and specifically **not a global mutable Asset database**: it is
   owned, constructed, and destroyed exactly once per `Atlantis::RuntimeHost`
   instance, alongside every other resource Spec 0013 already owns this
   way, never a process-wide singleton or static — matching
   [AGENTS.md](../AGENTS.md)'s existing no-global-mutable-engine-state
   rule exactly as Spec 0013 already satisfies it for its own `Mesh`/
   `Material`. An `AssetId` with no matching entry is a recoverable,
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
- **Runtime's AssetId→Mesh/Material resolution mechanism is new state
  this Spec introduces to Runtime's own composition object** (not
  existing before this Spec, since Spec 0013's own bootstrap held exactly
  one hardcoded `Mesh`/`Material` pair with no lookup at all), entirely
  **private** to that composition object — scoped, for this Spec's own
  validation scene, to the same single already-cooked asset Runtime
  already loads; a general multi-asset resource-table implementation
  (eviction, hot-reload, lazy loading), and any fixed public interface for
  it, are explicitly not designed here — see the related Spec's Non-Goals.
- **Runtime's existing windowed `RenderTarget` still cannot be
  pixel-read-back or auto-compared against a golden — unchanged from
  Spec 0013, re-confirmed by inspection during this ADR's own evidence
  pass.** `src/vulkan_backend/src/vulkan_presentation.cpp` still
  constructs a swapchain-backed `RenderTarget` with `imageUsage =
  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`
  only — no `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` — so
  `CommandList::copyRenderTargetToBuffer()` (the only existing GPU-to-CPU
  readback path, ADR-0040) cannot legally be issued against it. This Spec
  does not add `TRANSFER_SRC_BIT` to the swapchain or any other windowed-
  readback capability (matching Spec 0013's own explicit, unchanged
  decision on this exact point) — **Runtime's own windowed verification
  therefore stays a GPU smoke test (the real acquire/draw/submit/present
  pipeline runs Validation-Layers-clean, with no pixel assertion) plus
  manual, by-eye comparison against a golden PNG; the only automated
  pixel-level comparison this Spec's own multi-entity scene gets is
  through the existing, unmodified headless `OffscreenTarget`/image-
  regression path** (`tests/image_regression/`, Spec 0010/0011) — see the
  related Spec's Testing & Verification Plan and Decisions Requiring
  Human Review, item 9.

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
- The camera view-matrix construction (basis extraction + the existing,
  unmodified `lookAt()`) needs no general 4×4 matrix inverse and carries
  no "the camera must be unscaled" precondition (see Decision step 3's
  own orthogonality-under-scale reasoning) — a more robust, less-new-code
  result than the "invert the world matrix" phrasing this ADR's own first
  draft used.

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
- **Compute the camera view matrix via a general 4×4 matrix inverse of the
  camera entity's world matrix**, as this ADR's own first draft described.
  Rejected on reconsideration during this ADR's own evidence pass:
  correct, but strictly more code (a general cofactor/adjugate-based 4×4
  inverse, unused for anything else in this Spec's own scope) than
  extracting and normalizing the world matrix's own basis columns and
  reusing the existing `lookAt()` function — the chosen approach is both
  smaller and, per Decision step 3's own orthogonality-under-scale
  argument, no less correct.
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
