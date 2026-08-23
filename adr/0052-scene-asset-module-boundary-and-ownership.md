# ADR 0052: Scene Asset Module Boundary and Ownership

- **Status:** Accepted
- **Date:** 2026-08-23
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-23 as part of Spec 0015's Human Review Approval
- **Related Spec:** [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)
- **Acceptance Record (2026-08-23):** Accepted by Human Review as part
  of
  [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)'s
  own Human Review Approval (2026-08-23) — see that Spec's own approval
  note (items 1, 2, 6, 14) and its own Human Review Decision Table's
  "Where" column for the specific items this ADR corresponds to. This
  record does not change this ADR's own Decision, Consequences, or
  Alternatives Considered below.

## Context

Spec 0014 built `Atlantis::World` — an in-memory Entity/Transform/
Camera/Renderable store — and established two facts this ADR must
respect, not re-litigate:

1. **`Atlantis::World` depends on `Atlantis::Core` and, narrowly, on
   `Atlantis::AssetSystem` for `atlantis::asset_system::AssetId` only**
   ([ADR-0048](0048-world-scene-module-boundary-and-ownership.md)).
   `Atlantis::AssetSystem` itself depends on `Atlantis::Core` only —
   confirmed by its own `module_boundary_tests.cpp`. This repository's
   module-boundary tests (one per module, each scanning its own sources
   for forbidden includes) consistently enforce a one-directional,
   acyclic dependency graph; nothing in this codebase currently reverses
   an edge once drawn.
2. **`Atlantis Runtime` is the only module that depends on both
   `Atlantis::World` and `Atlantis::AssetSystem`**, and already hosts
   the translation logic between them:
   `src/runtime/include/atlantis/runtime/scene_extraction.h`/`.cpp`
   turns World geometry and an `AssetId` reference into Renderer-
   consumable output, Runtime-private, duplicated (not shared) with the
   image-regression fixture's own independent copy
   ([ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)).

Spec 0015 needs a scene **authored once, cooked into a versioned
runtime artifact, and loaded into a real, fully-populated `World`** at
Runtime startup — and the same artifact loaded again by a GPU-required
headless test, to prove zero pixel difference against the existing
`world_scene` golden. This
raises a genuine module-boundary question Spec 0014 explicitly deferred
([docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md):
"a rename/save-durable identity scheme, scene serialization... are each
named, explicitly out-of-scope future work in Spec 0014 — not designed
or scaffolded here").

**The hard constraint:** whatever decodes a scene artifact into a real
`World` must, transitively, depend on both `Atlantis::AssetSystem`
(mesh `AssetId` references) and `Atlantis::World` (the type it
constructs). `Atlantis::AssetSystem` cannot be that place — `World`
already depends on it, so `AssetSystem` depending back on `World` would
close a cycle. This is not a stylistic preference; it is the same
acyclic-dependency invariant every existing module-boundary test in this
repository already enforces.

## Decision

**No new top-level module.** Scene asset cook/decode/instantiate is
split across two already-`Accepted` modules, each extended narrowly
within its own existing role, matching this repository's own
"extend before inventing" bias
([ADR-0048](0048-world-scene-module-boundary-and-ownership.md)'s own
reasoning for why `World` itself became a new module only because
nothing existing could plausibly own it):

1. **`Atlantis::AssetSystem` gains scene artifact encode (cook) and
   decode**, producing/consuming a new, neutral, in-memory
   `ValidatedSceneData` value type — a flat array of per-node data, each
   entry carrying its own `Atlantis::AssetSystem`-owned
   `DecodedTransform`/optional `DecodedCamera`/optional
   `DecodedRenderable` (an `atlantis::asset_system::AssetId`), a parent
   reference, and an active-camera reference — that names **no
   `Atlantis::World` type anywhere, by construction, not merely by
   convention** (see
   [ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)'s
   own Decision for exactly why this matters and the concrete field
   shapes). This is the same shape `loadStaticMeshAsset()` already
   established for meshes: "reads the runtime artifact... returns
   CPU-side [data]... A composition root outside Asset System is
   responsible for" turning it into something else
   (`src/asset_system/include/atlantis/asset_system/load.h`). Encoding
   reuses the mesh cooker's own established conventions: a hand-bumped,
   independently-versioned behavior constant (mirroring
   `kImporterVersion` in `cook.h`, not the same constant — a scene
   cooker and a mesh cooker have independent behavior-change histories),
   write-to-temp-then-rename atomic writes, and a metadata sidecar
   cross-checked against the artifact at load time
   (`AssetLoadError::MetadataArtifactMismatch`'s own precedent).
2. **`Atlantis::World` gains one new, small, additive public entry
   point — `World fromValidatedSceneData(const ValidatedSceneData&)` (exact name a
   Plan-level detail) — that consumes an already-fully-validated
   `ValidatedSceneData` and returns a fully-populated `World` by value —
   infallibly, not `Result`-wrapped** (Spec 0015's own transactional-
   instantiation requirement —
   [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
   explains why every precondition is already guaranteed by the time
   this function runs). **This function performs a real, concrete
   conversion from `Atlantis::AssetSystem`'s own DTOs
   (`DecodedTransform`/`DecodedCamera`/`DecodedRenderable`) into real
   `world::Transform`/`Camera`/`Renderable` values** — this is
   genuine translation work, not eliminated by this Decision, only
   located in the one place permitted to know both shapes, so it never
   needs its own separate name or file the way `scene_extraction.h`
   does for the World→Renderer direction. Placing this conversion on
   `World` itself does not add a dependency edge: `World` already
   depends on `Atlantis::AssetSystem` for `AssetId`; consuming one more
   neutral value type from the same, already-permitted module is a
   broader use of an existing edge, not a new one. Placing instantiation
   on `World` itself — not in Runtime — keeps it reusable without
   duplication: the headless image-regression test needs the identical
   "already-decoded scene → real `World`" capability Runtime's own
   bootstrap needs, and unlike the small, easily-duplicated
   camera-math/scene-construction helpers ADR-0051 accepted duplicating,
   a strict, validating scene-instantiation routine is exactly the kind
   of logic where a second, independently-maintained copy risks
   silently drifting from the first.
3. **`Atlantis Runtime`'s own role is composition and mesh-dependency
   resolution, not `ValidatedSceneData`→`World` translation** — that
   translation is `World`'s own job, per item 2 above. Runtime still
   does real work of its own: call `Atlantis::AssetSystem`'s decode,
   resolve and eagerly load every `Renderable`'s mesh dependency against
   a local resolver
   ([ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)),
   call `Atlantis::World`'s new instantiation entry point, publish the
   result on success — the same "call other modules, wire results
   together" shape Runtime's `initializeSteps()` already has for every
   other resource (Device, Mesh, shader load). No new Runtime-private
   *translation* file is required for the `ValidatedSceneData`→`World`
   direction specifically (unlike `scene_extraction.h`, which exists
   because the World→Renderer direction genuinely has no other natural
   owner) — mesh-dependency resolution is orchestration, not a second
   translation layer duplicating what `World`'s own entry point already
   does.
4. **The scene cooker is a new mode of the existing `atlantis_asset_cooker`
   Tools executable**, not a second standalone binary — reusing its CLI
   argument conventions, its `--validate-set`-style pattern, and its
   already-established atomic-write helper, matching
   `src/tools/asset_cooker/`'s own existing shape.

## Consequences

### Positive

- No new top-level module, no new CMake target category, no new entry
  in `docs/architecture/module_boundaries.md`'s own module list beyond
  extending two already-documented sections — the smallest boundary
  change that solves the actual problem.
- `ValidatedSceneData` living in `Atlantis::AssetSystem` keeps that module's
  own already-`Accepted` role intact ("cooked artifact ↔ CPU data,
  never a downstream GPU or engine-object type") and requires no
  loosening of its own module-boundary test.
- Instantiation living on `World` itself means the headless
  image-regression test, Runtime, and any future consumer (a
  research/simulation harness, a future Editor) all call the exact same
  code path — one implementation to keep correct, not two independently
  duplicated ones for a binary-format-adjacent concern.
- Matches ADR-0035's own procedural requirement explicitly: authoring
  representation (human-editable scene source) and runtime
  representation (versioned binary artifact, then `ValidatedSceneData`, then
  a real `World`) are named as three distinct shapes with two
  transformation steps (cook, instantiate), not defaulted into "same
  structure."

### Negative / Trade-offs

- `Atlantis::World` gaining a public entry point that names
  `atlantis::asset_system`'s `ValidatedSceneData` type is a small increase in
  `World`'s own public surface area beyond pure entity/component
  manipulation — a deliberate, disclosed trade-off, not an oversight;
  the alternative (Runtime doing the translation) was rejected above
  specifically to avoid duplicating strict binary-format-adjacent
  validation logic.
- Two modules (`AssetSystem` for cook/decode, `World` for instantiate)
  now jointly own "loading a scene," rather than one new module owning
  the whole pipeline — a reader has to look in two places, not one.
  Accepted because both places already exist and are already the
  natural owner of their own half (`AssetSystem` already owns every
  other cooked-artifact format; `World` already owns every other way an
  `EntityId` comes into existence).

## Alternatives Considered

- **A new, independent `Atlantis::Serialization` (or `Atlantis::SceneAsset`)
  top-level module, depending on `Atlantis::Core` +
  `Atlantis::AssetSystem` + `Atlantis::World`, owning cook, decode, and
  instantiate together.** Structurally valid — nothing currently depends
  on such a module, so it would not close a cycle. Rejected as this
  ADR's own recommendation (not forbidden, see Spec 0015's own Human
  Review Decision Table item 1) because it would be this repository's
  first module whose entire reason to exist is gluing two other modules
  together, when both of those modules already have an established,
  narrower place to host their own half of that glue — matching this
  repository's own consistent preference (Asset System absorbing
  cooking rather than a separate "Importer" module; Runtime absorbing
  `scene_extraction.h` rather than a separate "Extraction" module,
  [ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
  own explicit rejection) not to add a module purely to sit between two
  others.
- **Runtime owns decode-to-`World` instantiation itself (Runtime-private,
  like `scene_extraction.h`).** Rejected: the headless image-regression
  test does not, and should not, link Runtime's own private composition
  library (`Atlantis::RuntimeHost` is a composition root's own object
  model and frame lifecycle, not a general-purpose library other
  binaries are meant to consume) — it would either need to duplicate a
  strict, validating binary decoder (a heavier, more error-prone
  duplication than the small math/construction helpers ADR-0051 already
  accepted duplicating) or reach into Runtime-private internals,
  neither acceptable.
- **`Atlantis::World` owns cook/decode too (the whole pipeline), not
  just instantiate.** Rejected: `World` would then need to parse a
  human-editable authoring format and a versioned binary artifact —
  exactly the kind of cooked-artifact-format ownership
  [ADR-0048](0048-world-scene-module-boundary-and-ownership.md) already
  drew a hard line around ("World never depends on... the cooker or
  validation surfaces"). Keeping artifact bytes entirely inside
  `AssetSystem`, and handing `World` only an already-decoded, neutral
  value type, preserves that line exactly.
