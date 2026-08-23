# Spec: World / Scene Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path. Reviewed and approved by a human — see Human
  Review Approval immediately below; the Independent Review entries
  further below are the self-review record that preceded and fed that
  approval, not a substitute for it.
- **Created:** 2026-08-22
- **Human Review Approval (2026-08-22):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer) on 2026-08-22, accepting the merged document's own Human
  Review Decision Table in full, as recommended, with no amendment. This
  approval explicitly accepts:

  1. **A new, independent top-level module, `Atlantis::World`** (not a
     private submodule of `src/runtime/`), depending on `Atlantis::Core`
     and, narrowly, `Atlantis::AssetSystem` for `AssetId` only — no RHI,
     Vulkan Backend, RenderGraph, Renderer, Shader System, Platform,
     Runtime, or Tools dependency, in either direction (Human Review
     Decision Table item 1; [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)).
  2. **`EntityId` as an index (`uint32_t`) + generation (`uint64_t`)
     handle, with unconditional, formal overflow closure**: a slot's
     index is permanently retired — never reused — the moment its
     generation reaches `uint64_t`'s maximum representable value,
     guaranteeing no historical handle can ever revalidate regardless of
     cycle count; the 64-bit width itself is accepted as a complementary
     practical mitigation, not the thing that alone closes the risk
     (item 2; [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)).
  3. **Stale/invalid `EntityId` use is a `Result::Err(WorldError::InvalidEntity)`,
     classified per AGENTS.md's own Programmer-error/`Result` model**: a
     stale handle produced by a legitimate (direct or cascading)
     destruction is a normal, observable state `EntityId`'s own non-
     owning contract already defines, not a violated precondition — while
     a genuine internal generation/slot bookkeeping inconsistency (a bug
     in World's own implementation) remains squarely an `ATLANTIS_CHECK`
     matter, never folded into `WorldError` (item 3; ADR-0049).
  4. **Every public World accessor returns by-value** (`Result<T,
     WorldError>`), never a reference or pointer into World's own
     internal storage (item 4; ADR-0049).
  5. **Fixed-type component storage** — a mandatory `Transform` plus two
     optional components (`Camera`, `Renderable`) directly on each
     entity's own record — not a generic, type-erased ECS registry (item
     5; this Spec's own "Why this stays a minimal World, not a general
     ECS").
  6. **Deterministic slot reuse and multi-entity enumeration order**: the
     free list is a LIFO stack, and any World API enumerating more than
     one entity iterates in ascending slot-index order — a fully
     specified, reproducible function of World's own mutation history,
     never dependent on unspecified container iteration (ADR-0049's own
     Decision; not a separately numbered table row, accepted here
     explicitly as load-bearing for item 14's own image-regression
     reproducibility).
  7. **Explicit, single-threaded, once-per-frame `updateTransforms()`**,
     with cycle prevention at `setParent()` mutation time (an ancestor-
     chain walk, `Result::Err` before any state change) and a defensive,
     traversal-time `ATLANTIS_CHECK` as a last-resort invariant guard
     only (item 6; [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)).
  8. **Parent destruction cascades** to every transitive descendant in one
     atomic call, automatically clearing the active camera if implicated
     (item 7; ADR-0050).
  9. **`setParent()` preserves the child's own *local* transform, not its
     *world* transform** — reparenting therefore generally changes world
     position/orientation as a disclosed side effect, with no automatic
     world-preserving reparent operation in this round (item 8;
     ADR-0050).
  10. **The fully specified Math contract**: column-major matrix layout;
      column-vector composition with parent on the left
      (`worldMatrix = parentWorldMatrix · localMatrix`); right-handed,
      Y-up coordinates; `localMatrix = T · R · S`; Euler-angle composition
      `R = Ry(yaw) · Rx(pitch) · Rz(roll)`; the disclosed fact that a
      composed, multi-level world matrix may contain shear; and the
      `Camera` `fovYRadians`/`nearZ`/`farZ`-only ownership boundary
      (aspect computed by Runtime per-frame, never stored on `Camera`) —
      all as stated in ADR-0050's own "Math contract" subsection (not a
      separately numbered table row, accepted here explicitly as the
      final, evidence-grounded contract this Spec and ADR-0050 converged
      on).
  11. **Camera ownership, the active-camera rule, and view-matrix
      construction under a scaled hierarchy**: `Camera` is an optional
      component on an ordinary entity, carrying only `fovYRadians`/
      `nearZ`/`farZ`; exactly one active camera at a time; the view
      matrix is built by extracting only `eye` and `forward` from the
      camera's own world matrix — never `right`/`up` columns, which are
      not reliably orthogonal under a sheared hierarchy — feeding them
      into the existing, unmodified `lookAt()`; a near-zero forward
      direction or a forward direction parallel to the canonical world-up
      axis are explicit, recoverable, Runtime-classified extraction
      errors, never a silent `NaN` (item 11; [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)).
  12. **`Renderable` reuses `atlantis::asset_system::AssetId` directly**
      (not a World-owned opaque handle), and World depends on nothing
      else from Asset System (item 9; ADR-0048).
  13. **The World→Renderer `DrawItem` translation is Runtime's own
      composition-root adapter** — never inside World, never a Renderer
      change, and not a new, separate "Extraction" module (item 10;
      ADR-0051) — and **Runtime's `AssetId`→`Mesh`/`Material` resolution
      mechanism is a private implementation detail of
      `Atlantis::RuntimeHost`'s own composition object**, never a fixed
      public interface and never a global mutable Asset database (item
      12; ADR-0051).
  14. **Every existing public rendering API — `Renderer`, RHI, Vulkan
      Backend, Platform, Shader System, and Asset System — remains
      exactly as `Accepted` today, with zero modification to any public
      header, type, or function signature** (item 13; confirmed twice by
      direct inspection, Independent Review Rounds 1 and 2).
  15. **Runtime's own windowed `RenderTarget` cannot be pixel-read-back**
      (no `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`, unchanged from Spec 0013);
      windowed verification for this Spec's own multi-entity scene stays
      a GPU smoke test plus manual, by-eye comparison, while the existing,
      unmodified headless `OffscreenTarget`/image-regression path is the
      only automated pixel-comparison route this Spec's scene gets (part
      of item 14; ADR-0051's own Consequences).
  16. **The recommended new headless image-regression golden for this
      Spec's own multi-entity scene cites [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
      own `Accepted` Amendment, "Initial baseline bootstrap" category —
      not "Approved rebaseline"** — per that Amendment's own applicability
      constraint 1 (no prior golden exists at that path) and its own
      constraint 5 substitute-evidence requirement (visual inspection;
      zero-diff self-consistency; a real Validation-Layers-clean GPU run;
      citing ADR-0042's own existing calibration) in place of an
      inapplicable old-vs-new diff (item 14; this Spec's own Testing &
      Verification Plan).
  17. **Every Non-Goal this Spec states explicitly** (see Non-Goals) — a
      general/data-driven/multi-threaded ECS framework; any scene file
      format, cooker, or serialization; textures/samplers; PBR materials,
      lighting, shadows; animation/rotation interpolation; post-
      processing; Android/iOS/Linux; any new third-party dependency or
      general-purpose `Atlantis::Math` module; a Client/Editor API; any
      change to an existing module's public API; multiple simultaneous
      cameras; and a general multi-asset resource cache/hot-reload/async
      streaming in Runtime's own resolution mechanism.

  Two stale internal cross-references — found during this Human Review's
  own final consistency check, not a design change — were corrected on
  this same branch immediately before this approval was recorded:
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)'s
  own `setParent()` bullet and
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
  own windowed-`RenderTarget` bullet each still named this document's
  prior "Decisions Requiring Human Review" section (renamed to "Human
  Review Decision Table" during Independent Review Round 2) and, in
  ADR-0051's case, cited a stale item number left over from before the
  table's own final 14-row numbering — both are now updated to point at
  the correct, current table rows. Neither correction changes any
  Decision, Consequence, or Alternative Considered in either ADR.

  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  all move to `Accepted` alongside this approval — see each ADR's own new
  Acceptance Record. **This approval authorizes drafting Plan 0014
  against this Spec, per [AGENTS.md](../AGENTS.md); it does not itself
  authorize Implementation** — that future Plan must still pass its own
  Human Review, per the same Spec → Plan → Human Review → Implementation
  → Verification → PR → Merge path every prior spec in this line has
  followed.
- **Related Plan(s):**
  [plans/0014-world-scene-foundation.md](../plans/0014-world-scene-foundation.md)
  (`In Review` — self-reviewed, not yet Human-Review-approved; drafted
  under the authorization this Spec's own Human Review Approval above
  grants, per [AGENTS.md](../AGENTS.md)). Implementation is not
  authorized until that Plan passes its own Human Review.
- **Related ADR(s):**
  [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)
  (module boundary and ownership),
  [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)
  (entity identity and handle invalidation),
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)
  (Transform hierarchy, composition, and update model), and
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  (World-to-Renderer extraction and asset resolution boundary) — all four
  `Accepted` alongside this Spec's own Human Review Approval above.
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
- **Independent Review — Round 3 (2026-08-22, final targeted revision
  before formal Human Review) — three specific points only, not a fresh
  broad review.** Round 2's own points 3, 4, and 7–8 above are the
  historical record of what Round 2 found and concluded; two of those
  conclusions did not hold up under a further, targeted check and are
  **superseded** by this round, precisely as follows — no other scope,
  Golden classification, module boundary, or Renderer-API conclusion is
  reopened:
  1. **Generation-counter overflow (supersedes Round 2, point 4): a
     64-bit generation width is a probabilistic mitigation, not a formal
     closure, on its own.** Corrected: `EntityId` now has an explicit,
     unconditional rule for the counter's maximum value —
     `destroyEntity()` permanently retires a slot's index (never returns
     it to the free list) the moment incrementing its generation would
     reach `std::numeric_limits<std::uint64_t>::max()`, which guarantees,
     by construction and for any generation width, that no historical
     handle can ever revalidate — not merely "is astronomically unlikely
     to." The 64-bit width is retained as a complementary, practical
     mitigation (makes actually reaching that tombstone value
     unreachable at this Spec's own process scale), not as the thing that
     itself closes the risk. See
     [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s
     own reworked "Generation width and overflow behavior, formally
     closed."
  2. **Stale-handle `Result` vs. assertion (supersedes Round 2, point 3):
     re-classified against AGENTS.md's own Programmer-error/`Result`
     model directly, not primarily against `assert.h`'s Release-mode
     behavior.** Round 2's own framing ("assertion would be too severe")
     was not the right basis for the conclusion, even though the
     conclusion (use `Result`) was already correct. Corrected: a stale
     `EntityId`, produced by a *legitimate* destruction (direct or
     cascading) of an entity some other, unrelated code still holds a
     handle to, is a normal, observable state `EntityId`'s own non-owning
     contract already defines — not a violated precondition — which is
     why `AGENTS.md`'s "recoverable runtime errors use explicit result/
     error types" rule applies directly. A genuine internal generation/
     slot bookkeeping inconsistency (a bug in World's own implementation)
     remains squarely an `ATLANTIS_CHECK` matter, a categorically
     different failure source. The `assert.h` Release-mode-compile-away
     fact is retained only as secondary, reinforcing evidence. See
     [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s
     own reworked classification.
  3. **Camera view-matrix construction under a scaled hierarchy
     (supersedes Round 2, points 7 and 8): the "matrix columns stay
     mutually orthogonal under arbitrary per-axis scale" claim was wrong
     for a composed, multi-level hierarchy.** Found by direct
     counter-example, not merely reconsidered in the abstract: a parent
     with non-uniform local scale (`diag(2,1,1)`, no rotation) composed
     with a child rotated 45° about `Z` (no scale of its own) produces a
     child world matrix whose columns 0 and 1 have a dot product of
     `−1.5`, not `0` — real shear, invalidating the original "normalize
     each column independently" extraction. Corrected: view-matrix
     construction now extracts **only** `eye` and `forward` from the
     camera's own world matrix (never `right`/`up` columns), delegating
     orthonormal-basis construction entirely to the existing `lookAt()`
     function's own cross-product method against a fixed world-up
     reference — robust to any non-degenerate scale/shear composed
     anywhere in the camera's own ancestor chain, including negative
     scale (which cannot mirror the camera's own basis, since `right`/
     `up` are never inherited from the world matrix at all). Two genuine
     degenerate-input cases are now given explicit, recoverable
     (Runtime-classified) error semantics instead of silently producing a
     `NaN`-poisoned matrix: a near-zero-length forward direction (an
     ancestor scale genuinely collapsing that axis), and a forward
     direction parallel to the canonical world-up axis (the camera
     "looks straight up/down" — a rotational singularity latent in
     `lookAt()` itself, now genuinely reachable since `Camera`'s own
     Transform is fully author-controllable). See
     [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)'s
     own new Math-contract bullet (the general shear fact, as a property
     of the composition model) and
     [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
     own reworked Decision step 3 and Revision 2 note (the corrected
     construction and its two degenerate-input cases).

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
[ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md); the
shape below is superseded by this Spec's own "Accepted Amendment"
section further down, which adds a third, private `World`-identity
field)

- `EntityId` is an index+generation value type (`{ std::uint32_t index;
  std::uint64_t generation; }`, 16 bytes), value-comparable, with a fixed
  invalid sentinel. **Overflow is formally closed, not merely made
  unlikely:** `std::numeric_limits<std::uint64_t>::max()` is a reserved
  tombstone value, never assigned to any live entity; when
  `destroyEntity()` increments a slot's generation to that value, the
  slot is permanently retired (its index is never returned to the free
  list, never reused by any future `createEntity()` call) — guaranteeing,
  unconditionally, that no historical `EntityId` for that index can ever
  revalidate. The 64-bit width is a complementary, practical mitigation
  (makes actually reaching the tombstone value unreachable at this Spec's
  own process scale) — see ADR-0049's own "Generation width and overflow
  behavior, formally closed."
- `World::createEntity()` returns a new, always-valid `EntityId`
  (non-fallible) — unaffected by retirement: if every existing index has
  ever been retired, `createEntity()` still succeeds by growing storage
  and allocating a new index, exactly as it already does whenever the
  free list is empty for any other reason. Slot reuse is a **LIFO free
  list** — the most recently destroyed, non-retired slot's index is the
  first one a following `createEntity()` reuses, a fixed, deterministic
  rule, never an accident of container choice.
- `World::destroyEntity(EntityId)` returns `Result<void, WorldError>`;
  cascades to every transitive descendant in the same call; clears the
  active-camera reference automatically if it or any destroyed descendant
  was the active camera.
- `World::isValid(EntityId) const` reports whether a handle's index is in
  range and its generation matches the slot's current generation.
- Every public World API accepting an `EntityId` validates it and returns
  `Result<T, WorldError>` with `WorldError::InvalidEntity` on a stale or
  out-of-range handle — never undefined behavior. **Classification,
  aligned with AGENTS.md's own Programmer-error/`Result` split, not an
  arbitrary choice:** a stale `EntityId` — one that named an entity since
  destroyed by a *legitimate* call (direct or, per cascading destruction,
  transitive) — is a normal, observable state `EntityId`'s own explicitly
  non-owning contract already defines, not a violated precondition; the
  caller holding it did nothing wrong, and the code that destroyed the
  entity did nothing wrong either. This is the same category of outcome
  as `std::weak_ptr::lock()` returning `nullptr`, which is why it is a
  `Result`, per AGENTS.md's own "recoverable runtime errors use explicit
  result/error types" rule. An **internal** generation/slot bookkeeping
  inconsistency — a bug in World's own implementation, not a caller
  mistake — remains a categorically different case, handled by
  `ATLANTIS_CHECK` (never folded into `WorldError`), matching AGENTS.md's
  "violated precondition/invariant fails fast" rule for World's own
  internal correctness. See ADR-0049's own Decision, Consequences, and
  Alternatives Considered, and the Human Review Decision Table below
  (this classification is flagged for explicit confirmation, not silently
  locked).
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
  **A composed, multi-level world matrix may contain shear** (its linear
  part is not guaranteed to decompose back into a pure rotation times a
  uniform scale) whenever a non-uniform or negative scale at one level is
  combined with a differently-oriented rotation at a descendant level —
  a disclosed, accepted property of this composition model, not a defect
  (a `Renderable` entity renders correctly under an arbitrary, even
  sheared, linear transform), but a real constraint for any consumer
  needing to recover an *orthonormal* basis from a world matrix — see
  Camera and Extraction below, and
  [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)'s
  own Math contract for the concrete counter-example.

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
  built by extracting **only** an eye position (the world matrix's
  translation column) and a forward direction (`normalize(-column 2)`,
  fixing the convention **a Camera looks down its own local −Z axis**)
  from the camera entity's own world matrix — **never** a `right`/`up`
  column — then feeding `eye`/`eye+forward` into the existing, unmodified
  `lookAt()` function every windowed demo already uses, which itself
  derives an orthonormal `right`/`up` basis via cross products against a
  fixed world-up reference. This is deliberately **not** "extract and
  normalize all three basis columns": a composed, multi-level world
  matrix may contain shear (see the Math contract bullet above), under
  which independently-normalized columns are not generally orthogonal —
  extracting only a single direction (`forward`) sidesteps that problem
  entirely, since a lone direction has no orthogonality property to
  violate. No general 4×4 matrix inverse is needed either way. Two
  genuine degenerate-input cases — a near-zero-length forward direction,
  and a forward direction parallel to the canonical world-up axis (the
  camera looking straight up/down) — are detected explicitly and treated
  as recoverable, Runtime-classified extraction conditions, never
  silently computed into a `NaN`-poisoned matrix; see
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
  own Decision step 3 for the full construction, its counter-example-
  grounded correction, and both degenerate cases. The **projection**
  matrix comes from `Camera`'s `fovYRadians`/`nearZ`/`farZ` and the
  current swapchain aspect ratio — and writing both into the existing
  camera uniform `Buffer`; resolving
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
  stale/invalid `EntityId` use, which this Spec classifies as a normal,
  observable runtime state (a legitimately-destroyed entity's non-owning
  handle behaving exactly as its own contract defines), not a programmer-
  error assertion (see Requirements' "Entity lifecycle and identity"
  above for the AGENTS.md-aligned classification, and the Human Review
  Decision Table below for this as an explicit, confirmable choice). This
  is categorically distinct from a genuine **internal** invariant
  violation — a bug in World's own bookkeeping, not a caller mistake. The
  update traversal's own defense-in-depth "never revisit an already-
  visited entity" check
  ([ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md))
  is exactly such an internal-invariant guard, and uses `ATLANTIS_CHECK`
  specifically (not `ATLANTIS_ASSERT`, which compiles to a no-op whenever
  `NDEBUG` is defined) so it stays active in Release builds too, never
  folded into a `Result`.

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
decisions, filed as four ADRs — all `Accepted` alongside this Spec's own
Human Review Approval, per [AGENTS.md](../AGENTS.md):

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
| 2 | `EntityId` shape, generation width, and overflow behavior. | Index (`uint32_t`) + generation (`uint64_t`), 16 bytes; a slot's index is **permanently retired** (never reused) the moment its generation reaches `uint64_t`'s maximum value. | The retirement rule, not the 64-bit width alone, is what formally guarantees no historical handle ever revalidates (the width only makes reaching that value practically unreachable); costs one doubled handle size and, in the practically-unreachable case retirement ever triggers, one permanently smaller reusable-index pool. | [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md) |
| 3 | Stale/invalid `EntityId` detection: `Result::Err`, or an assertion (`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`)? | `Result<T, WorldError>` — `WorldError::InvalidEntity` for a stale external handle; `ATLANTIS_CHECK` remains reserved for a genuine **internal** generation/slot bookkeeping bug in World's own implementation. | A stale handle from a *legitimate* destruction is a normal, observable state `EntityId`'s own non-owning contract already defines (like `std::weak_ptr::lock()` returning `nullptr`), not a violated precondition — the classification, not `assert.h`'s Release-mode behavior alone, is why `Result` is correct here. | [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md) |
| 4 | Do public World accessors return by-value copies, or references/pointers into World's own storage? | By-value only — every getter returns `Result<T, WorldError>` by value; every setter takes its argument by value. | Stricter than ADR-0033 strictly requires (that rule is about cross-module Client access) — adopted because World's own internal slot array can reallocate on growth, which would otherwise dangle any previously-returned reference. | [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md) |
| 5 | Fixed-type component storage, or a generic ECS registry? | Fixed-type storage — a mandatory `Transform` plus two optional components (`Camera`, `Renderable`) directly on each entity's own record; no type-erased component pool, no runtime component-type registration. | A future component type (e.g. a Light) requires extending the fixed entity record, not registering a new type generically — accepted because this Spec names exactly two optional component kinds and no generic-registration consumer exists yet. | This Spec's own "Why this stays a minimal World, not a general ECS" above |
| 6 | Transform hierarchy update strategy, and when cycle detection runs. | Explicit, single-threaded, once-per-frame `updateTransforms()` (no eager per-setter propagation, no dirty-flag scheme); cycle prevention at `setParent()` mutation time (ancestor-chain walk, `Result::Err` before any state change), with a defensive traversal-time `ATLANTIS_CHECK` as a last-resort invariant guard only. | A world matrix read without a following `updateTransforms()` call silently reflects stale data — a documented contract, not an automatic dirty check; accepted for this Spec's own explicit, single-threaded model. | [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md) |
| 7 | Does destroying a parent entity cascade to its descendants, reparent them, or leave them orphaned? | Cascades — `id` and every transitive descendant are destroyed together, in one atomic call; the active camera is cleared automatically if implicated. | Simplest semantics to reason about, but no "detach children first" escape hatch exists — a real, disclosed constraint a future Plan/caller must design around if it ever needs a subtree to outlive its root. | [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md) |
| 8 | Does `setParent()` preserve the child's *local* transform, or its *world* transform? | Preserves *local*; world transform generally changes as a disclosed side effect. | The alternative (auto-preserving world transform) requires a general 4×4 matrix inverse plus a TRS decomposition — real machinery no other part of this Spec's minimal scope needs, for a capability this Spec's own validation scene does not exercise. | [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md) |
| 9 | World↔Asset System reference boundary: does `Renderable` reuse `atlantis::asset_system::AssetId` directly, or a World-owned opaque handle? | Reuse `AssetId` directly — one source of truth, no conversion layer; World depends on nothing else from Asset System. | Couples `Renderable` to Asset System's current, path-derived (not rename-durable) identity scheme (ADR-0044) — a future Serialization/Stable-Identity Spec changing that scheme changes `Renderable` directly, with no insulating indirection. | [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md) |
| 10 | Where does the World→Renderer `DrawItem` translation live? | Runtime's own composition-root adapter — never inside World, never a Renderer change, and not a new, separate "Extraction" module ahead of a second real consumer. | Confirmed, by direct inspection, that `Renderer::drawFrame()` already accepts and already iterates a multi-item `DrawItem` span — zero Renderer change needed. | [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md) |
| 11 | Camera ownership, the active-camera rule, and view-matrix construction under a scaled hierarchy. | `Camera` is an optional component on an ordinary entity (participates in the Transform hierarchy, e.g. a camera rig), carrying only `fovYRadians`/`nearZ`/`farZ`; exactly one active camera at a time. View matrix: extract only `eye`+`forward` from the camera's world matrix (never `right`/`up` columns, which are not reliably orthogonal under a sheared hierarchy — see Requirements' Math contract), feeding them into the existing, unmodified `lookAt()`; near-zero forward or forward-parallel-to-world-up are explicit, recoverable extraction errors, never a silent `NaN`. | View/projection matrix computation stays entirely Runtime's own hand-rolled code, never moved into World. The `eye`/`forward`-only extraction is a correction to this ADR's own first-drafted, column-normalization approach, found incorrect by a concrete counter-example (a scaled parent composed with a rotated child) during this Spec's own review — not a hypothetical concern. | [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md) |
| 12 | Should Runtime's `AssetId`→`Mesh`/`Material` resolution mechanism be a fixed public interface, or a private implementation detail? | Private to `Atlantis::RuntimeHost`'s own composition object — this Spec fixes only its input/output shape (`AssetId` in, a resolved pair or not-found, out), not a concrete container type or a public API. | Locking a public resolver interface now, with only one real consumer and one real asset, would be exactly the premature, unnecessary abstraction AGENTS.md's Golden Rule warns against. | [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md) |
| 13 | Does this Spec preserve every existing public rendering API unchanged? | Yes — confirmed by direct inspection, twice (Independent Review Rounds 1 and 2): `Renderer`, RHI, Vulkan Backend, Platform, Shader System, and Asset System all remain exactly as `Accepted` today, zero modification to any public header, type, or function signature. | Not a judgment call — a factual finding this table records for the reviewer's own direct confirmation, since it is this Spec's own central architectural claim. | Independent Review above |
| 14 | The first multi-entity Runtime validation scene, and its image-regression golden-update-reason category. | Extend Runtime's bootstrap (Spec 0013) with several `minimal_cube` instances at distinct World-driven transforms (one hierarchy relationship exercised) plus one World-driven camera; verify via (a) a **new** headless image-regression fixture/golden under `tests/image_regression/`, citing ADR-0042's own **Accepted Amendment** "Initial baseline bootstrap" category (not "Approved rebaseline" — ADR-0042's own Alternatives Considered explicitly rejects that category as the permanent answer for a first golden, since it needs old-vs-new diff evidence that cannot exist here) — the Amendment's own constraint 5 substitute evidence (visual inspection; zero-diff self-consistency; a real Validation-Layers-clean GPU run; citing ADR-0042's existing calibration) is what a future Plan/Implementation must produce; (b) a windowed Runtime GPU smoke test extension; (c) manual, by-eye windowed verification against that same golden. | Runtime's own windowed swapchain still cannot be pixel-read-back (confirmed: no `VK_IMAGE_USAGE_TRANSFER_SRC_BIT` on it) — the headless path is the only automated pixel-comparison route available. Exact fixture/golden naming and PR sequencing remain Plan-stage details. | This Spec's own Testing & Verification Plan; [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md) |

### Approval readiness

**Superseded by the Human Review Approval (2026-08-22) recorded at the top
of this document.** This subsection originally stated, correctly at the
time, that the Spec was not yet ready to move to `Approved` — retained
here as the honest historical record of this document's own pre-approval
state, not silently deleted now that it no longer applies. Per
[AGENTS.md](../AGENTS.md) and [specs/README.md](README.md), reaching
`Approved` required (1) a human reading this Spec and
[ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
together and recording explicit Human Review Approval — the table above
was the complete, one-time set of items that approval needed to either
accept as recommended or direct a change to, with no item left for a
later, separate round — and (2) all four ADRs moving from `Proposed` to
`Accepted` as part of that same approval, per AGENTS.md's ADR workflow.
Both have now happened, exactly as this section anticipated: the table's
own 14 items were accepted in full, as recommended, with no amendment —
see the Human Review Approval note at the top of this document for the
complete record. This Spec's own self-review work (Independent Review
Rounds 2 and 3 above) closed every internal contradiction, omission,
overclaim, and — in Round 3 — one genuine mathematical error (the camera
view-matrix construction's own original orthogonality claim) that
targeted, evidence-driven review passes found, before Human Review itself
ran; that self-review was not, and did not substitute for, the Human
Review Approval now recorded. **This approval authorizes drafting Plan
0014 against this Spec; it does not authorize Implementation** — a future
Plan must still pass its own Human Review before Implementation may
begin.

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
  - **Shear under a scaled hierarchy — the `World`-owned half.** A parent
    with non-uniform local scale (e.g. `diag(2,1,1)`) composed with a
    child rotated about an axis not aligned with the scale produces a
    world matrix whose linear-part columns are verifiably **not**
    mutually orthogonal (checked directly against a hand-computed
    expected matrix, reproducing
    [ADR-0050](../adr/0050-transform-hierarchy-composition-and-update-model.md)'s
    own counter-example) — confirming `updateTransforms()` itself does
    not attempt to "correct" or reject shear; a `Renderable` entity in
    this exact configuration still produces a valid
    `DrawItem::objectToWorld` (shear is a legitimate mesh transform, not
    an error). This is the extent of what `tests/world/` itself covers —
    the camera-specific view-matrix construction below is Runtime-owned
    logic, tested separately.
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
  - **Generation retirement, directly tested at the boundary — not merely
    a design-time argument.** Unlike naturally cycling a slot through
    anywhere near `2^64` destroy/reuse cycles (not a real, runnable test
    at any practical timescale — the 64-bit width's own safety margin
    stays a quantitative, not a tested, argument), the **retirement rule
    itself** is directly testable by injecting the boundary condition:
    a test-only construction path (or an internal test hook — exact
    mechanism a Plan-stage detail) sets a slot's generation to
    `std::numeric_limits<std::uint64_t>::max() - 1` before calling
    `destroyEntity()` on it, then asserts (a) the slot's generation is
    now the tombstone value; (b) a following `createEntity()` call (or
    sequence of calls exhausting every other free slot) never reuses that
    specific index; (c) an `EntityId` carrying that index at its old,
    pre-retirement generation still correctly returns
    `WorldError::InvalidEntity` (unchanged from the ordinary stale-handle
    case — no special-cased validation logic exists for a retired slot).
    This exercises the actual mechanism the safety property depends on,
    without needing to run anywhere near `2^64` real cycles.
- **Unit tests (GPU-independent), `tests/runtime/`, exercising Runtime's
  own camera view-matrix extraction (per
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
  own Decision step 3) directly against hand-constructed world matrices —
  no `Device`, no GPU, no `World` instance required, since this logic
  only ever consumes an already-computed world matrix:**
  - The same shear-producing parent/child configuration as the `World`-
    owned test above, with the child instead representing a `Camera`'s
    world matrix: the `eye`+`forward`-only extraction still produces a
    well-formed, orthonormal view basis — verified by checking the
    resulting view matrix's own row/column orthonormality directly
    (dot products between distinct basis rows/columns are zero, each has
    unit length), not merely that it runs without crashing.
  - A negatively-scaled ancestor (an odd number of negative-scale axes,
    i.e. a mirror) feeding into a camera's world matrix: the resulting
    view matrix remains a proper, right-handed orthonormal basis
    (determinant `+1`, not a reflection) — confirming `right`/`up` are
    never inherited from the (potentially mirrored) world matrix.
  - A degenerate world matrix that collapses the camera's own forward
    axis to (near-)zero length: extraction returns a recoverable,
    Runtime-classified error, never a `NaN`-containing matrix.
  - A world matrix whose forward direction is (near-)parallel to the
    canonical world-up axis `(0,1,0)` (the camera looking straight up or
    down): extraction returns the same category of recoverable error,
    never a `NaN`-containing matrix or a divide-by-near-zero inside
    `lookAt()`'s own cross-product construction.
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
- **The exact epsilon threshold(s) for the camera view-matrix
  extraction's two degenerate-input checks** (near-zero forward length;
  near-zero `cross(forward, worldUp)` length) and **the exact Runtime-
  side error type/enumerator naming these two cases and the unresolved-
  `AssetId` case share** are left to the Plan — this Spec fixes only that
  both are detected explicitly and treated as recoverable, Runtime-
  classified extraction conditions, never silently computed into
  `NaN`/undefined output (see
  [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
  own Decision step 3).

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

## Accepted Amendment (2026-08-22) — Stable `World` identity in `EntityId`

**Status: Accepted.** Recorded following Human Review direction, then
formal approval, responding to Plan 0014's Independent Review Round 2
finding (cross-`World`-instance `EntityId` use — see
[plans/0014-world-scene-foundation.md](../plans/0014-world-scene-foundation.md)'s
own Deviations). Does not alter the Human Review Approval recorded above;
that approval covered the document as it stood on 2026-08-22, before this
finding existed — see "Human Review Amendment Approval" below for the
formal record of this section's own approval.

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
alternatives are recorded in
[ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s own
amendment; this section states the requirements-level consequence, not a
second copy of the architecture.

- **`EntityId` gains a third, `private` field** — `index`/`generation`
  stay public, unchanged in form from before this amendment; a
  non-owning identity reference to an opaque `WorldIdentity` token is
  added — and **all three of `EntityId`'s own fields (index, generation,
  and identity) are `private`, not only the identity field** (corrected
  2026-08-23; see this section's own "Human Review Amendment Approval"
  note below for why leaving index/generation public would have left the
  same-`World` forgery case this amendment's own "no forgery" intent was
  meant to close). `EntityId` publicly exposes only what is necessary:
  default construction (the invalid sentinel), equality comparison (which
  still includes identity, since a defaulted comparison operator has
  access to private members), and — only where a real call site needs it
  — read-only `index()`/`generation()` accessors. **Never a caller-writable
  raw pointer, and never any mutator for any of the three fields.** No
  caller can forge or overwrite any of them directly. `EntityId` remains a
  plain, trivially-copyable value type owning nothing. **This Spec's
  prior "16 bytes" claim (Requirements, "Entity lifecycle and identity")
  is superseded and not replaced with a new fixed number** — the exact
  resulting size is pointer-width- and alignment-dependent, an
  Implementation-time detail, not a claim this Spec fixes.
- **`WorldError` gains a fourth enumerator, `WrongWorld`**, returned when
  a `Result`-returning API's `EntityId` argument carries a non-null
  identity that does not match the receiving `World` instance's own
  token — checked before any index/generation check, and before a
  moved-from-`World` check (below), since a mismatched identity makes
  those fields meaningless relative to this instance. The existing
  sentinel `kInvalidEntityId` (no claimed identity) is unaffected: a null
  identity is never treated as "wrong," only as ordinarily invalid
  (`InvalidEntity`), preserving its existing behavior exactly.
- **`EntityId` must never be serialized, persisted, or used across a
  process boundary.** Its identity component is a heap address,
  meaningful only within the process and `World` instance that produced
  it. This states explicitly, for `EntityId` specifically, an exclusion
  this Spec's own Non-Goals already establish for `World` generally (no
  serialization, no scene file format, no cross-process/Client consumer
  — see Non-Goals above); stable, cross-session identity remains the
  separately deferred Serialization/Stable-Identity Spec's own future
  scope, not repurposed from this token.
- **Lifetime remains a separate concern from cross-instance confusion.**
  `EntityId` is still a strictly borrowed, non-owning handle: using it
  after the `World` instance that issued it has been destroyed remains a
  lifetime precondition violation (undefined behavior), not a condition
  this design detects or is required to detect. `WrongWorld` covers use
  against a different, currently **live** `World` instance only — not a
  destroyed one — unchanged from every other borrowed-handle contract
  this codebase already establishes (AGENTS.md's own ownership/lifetime
  rules).
- **`World`'s own copy/move semantics are now load-bearing, not merely a
  Plan-stage convenience choice.** `World` must be move-constructible,
  with its identity token and all state moving together (a handle valid
  before a move remains valid after it, against the moved-to instance),
  and must be neither copyable nor move-assignable — both would let two
  live `World` "identities" apply to overlapping state, defeating the
  very check this amendment adds.
- **A moved-from `World` guarantees only that it remains destructible or
  may be move-constructed from again.** Any other call on a moved-from
  `World` (e.g. `createEntity()`, any `EntityId`-accepting method) is a
  programmer error, caught by an explicit assertion-based check, not
  silently tolerated or left to produce unspecified behavior.

### Human Review Amendment Approval (2026-08-22)

Reviewed and approved by slmao (`slmao <slmaosjtu@gmail.com>`, this
repository's git-identified maintainer) on 2026-08-22, accepting this
section's design in full, matching
[ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s own
"Human Review Amendment Approval" note item-for-item. This approval does
not reopen or modify the Human Review Approval recorded above (the
pre-amendment document) — only this amendment section is newly
`Accepted`. [Plan 0014](../plans/0014-world-scene-foundation.md), synced
to this design, is separately approved — see that Plan's own Human
Review Approval note. Implementation itself still waits on
[PR #67](https://github.com/slmao/Atlantis/pull/67) being merged — see
[specs/README.md](README.md).

**Correction (2026-08-23), mechanical, no new review round:** the
original text above left `index`/`generation` as plain public `EntityId`
fields, moving only the identity reference behind `private` — inconsistent
with this same approval's own "no forgery" intent, since a plain public
`index` field lets a caller copy a legitimate handle and overwrite its
index directly, coincidentally forging a different entity within the
same `World` when the mutated index carries a matching generation. Fixed:
all three fields are now private, exposed only via read-only
`index()`/`generation()` accessors (never an identity accessor, never a
mutator) — see the bullet above and
[ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s own
matching correction for the full mechanism. No other part of this
approval changes.

**Human Review Correction (2026-08-23), additive, no new review round:**
the bullet above ("`WorldError` gains a fourth enumerator, `WrongWorld`")
stated `WorldError`'s resulting size as four. Implementation of
[Plan 0014](../plans/0014-world-scene-foundation.md) disclosed that
`World::getRenderable()` had no enumerator of its own for a valid
`EntityId` legitimately carrying no `Renderable` component, and reused
`NoCameraComponent` for that case as an interim choice. Human Review
**rejected** that reuse as insufficiently precise. Corrected:
**`WorldError` gains a fifth enumerator, `NoRenderableComponent`**,
returned by `getRenderable()` for a valid entity with no `Renderable`;
`NoCameraComponent` remains scoped exactly as before — a missing
`Camera` component only. The `InvalidEntity` / `WrongWorld` /
stale-generation validation this Spec and its amendment already require
before any component-absence check is unchanged and still takes
priority — a component-absence error is only reachable once the handle
itself has already validated. No other `World` API, module boundary, or
error semantics changes. See
[ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s own
matching correction for the full mechanism, and
[Plan 0014](../plans/0014-world-scene-foundation.md)'s own Verification
Checklist (V28) for the corresponding new verification requirement.
