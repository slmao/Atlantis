# Spec: Scene Asset & Serialization Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path. Reviewed and approved by a human — see Human
  Review Approval immediately below.
- **Created:** 2026-08-23
- **Human Review Approval (2026-08-23):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer) on 2026-08-23, accepting this document's own Human Review
  Decision Table in full — all 16 items, as recommended, with no
  amendment. This approval explicitly accepts:

  1. Scene authoring/cook/decode belongs to `Atlantis::AssetSystem`; no
     new, independent Serialization module (item 1; [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)).
  2. `Atlantis::AssetSystem` uses its own DTOs
     (`DecodedTransform`/`DecodedCamera`/`DecodedRenderable`), never
     depending on `Atlantis::World`; `Atlantis::World` owns the
     `ValidatedSceneData → World` conversion (item 6; ADR-0052,
     [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)).
  3. **`ValidatedSceneData` is constructible only by a successful
     decoder/validator** (`decodeScene()`, via a `private` constructor
     and `friend` relationship, matching `EntityId`'s own established
     pattern) — private internal state, read-only access only, no
     public default/arbitrary construction, copy/move preserving
     validity, and no path by which a caller can produce or mutate an
     invalid hierarchy, index, active-camera reference, or component
     value (item 15; ADR-0053).
  4. `World` accepts only `ValidatedSceneData` and returns a fresh
     `World` value — infallibly, no `SceneInstantiationError` (item 6;
     ADR-0052, [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)).
  5. Scene-local node ID, deterministic ordering, dense artifact-index
     remapping at cook time, and the explicit prohibition on
     persisting `EntityId`/`WorldIdentity` (items 3, 5; ADR-0053).
  6. Authoring/runtime artifact separation, a versioned, unconditionally
     little-endian binary format, and strict, independent decode-time
     validation (item 2; ADR-0053).
  7. A per-scene, build-tree-private dependency manifest — each entry a
     (normalized logical path, `AssetId`, artifact locator) triple
     (item 16; ADR-0054).
  8. Distinct, named manifest error semantics: duplicate entry,
     `AssetId` collision, metadata/`AssetId` mismatch, an unresolved
     scene reference, and a malformed/corrupted manifest line (item 16;
     ADR-0054).
  9. A manifest entry the scene never actually references is explicitly
     permitted, not an error (item 16; ADR-0054).
  10. Runtime resolves and loads mesh dependencies in the scene's own
      deterministic, ascending first-reference order — never relying on
      `std::unordered_map`'s own unspecified iteration order (item 16;
      ADR-0054).
  11. Every mesh dependency resolves and loads before any `Entity` is
      created (items 7, 13; ADR-0054).
  12. `ValidatedSceneData`, the mesh resource map, and the fresh
      `World` publish together only after full success; any failure is
      discarded by RAII, with no partial state (item 7; ADR-0054).
  13. **The dependency manifest exists to serve the current build tree
      only — it is never part of, or shipped alongside, the portable
      scene artifact.** This Spec does not claim to solve distributable-
      asset packaging or a general-purpose Asset Catalog; both remain
      explicitly future, unscoped work (item 13; ADR-0054; see Non-Goals
      and `specs/README.md`'s own Candidate Order 7).
  14. Reuses the existing Asset System CMake re-import-triggering
      pattern, atomic cooker writes, and introduces no new derived-data
      cache (item 5; ADR-0052).
  15. The first scene asset's headless load reuses the existing
      `world_scene` golden with **zero** difference — no new or updated
      equivalent golden — and Runtime's windowed path remains smoke-only
      plus genuine human visual confirmation, never an automated pixel-
      comparison claim (item 11; this Spec's own Requirements/Goals).
  16. Every Non-Goal named in this document, including no new
      third-party dependency and no change to `Renderer`/`RHI`/
      `RenderGraph`/Vulkan Backend's own public API (item 12; this
      Spec's own Non-Goals).

  [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)–[ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)
  all move to `Accepted` alongside this approval — see each ADR's own
  new Acceptance Record. **This approval authorizes drafting Plan 0015
  against this Spec, per [AGENTS.md](../AGENTS.md); it does not itself
  authorize Implementation** — that future Plan must still pass its own
  Human Review, per the same Spec → Plan → Human Review → Implementation
  → Verification → PR → Merge path every prior spec in this line has
  followed.
- **Related Plan(s):** [plans/0015-scene-asset-serialization-foundation.md](../plans/0015-scene-asset-serialization-foundation.md)
  (`In Review`) — drafted following this Spec's own Human Review
  Approval (2026-08-23), which authorizes drafting a Plan but does not
  itself authorize Implementation; see that Plan's own Human Review
  Approval note once recorded.
- **Related ADR(s):**
  [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)
  (module boundary and ownership),
  [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)
  (artifact format, versioning, scene-local node identity),
  [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)
  (transactional instantiation contract) — all `Accepted`, alongside
  this Spec's own Human Review Approval recorded 2026-08-23.

## Summary

Replace Runtime's current hard-coded scene construction
(`buildValidationScene()` in `src/runtime/src/runtime_application.cpp`,
Plan 0014 Section D9) with a real, minimal load loop: a human-editable
scene is authored once, cooked at build time into a versioned runtime
artifact (reusing `Atlantis::AssetSystem`'s existing cook/artifact/load
conventions), and loaded by Runtime into a fully-populated
`Atlantis::World` before the first frame — the same five-cube,
one-hierarchy-relationship, one-camera scene Spec 0014 already ships,
now data-driven instead of hand-written C++, rendered through the
existing, unmodified `Renderer`/`RHI` path, and proved pixel-identical
to the existing checked-in `world_scene` golden.

## Motivation / Problem Statement

Spec 0014 gave Atlantis its first in-memory scene representation
(`Atlantis::World`) but explicitly deferred how a scene comes to exist
at all — today, the only way to populate a `World` is
`buildValidationScene()`'s own six hard-coded
`createEntity()`/`setLocalTransform()`/`setCamera()`/`setRenderable()`/
`setParent()` calls, compiled directly into `Atlantis::RuntimeHost`.
Every future consumer of `World` — a research/simulation harness
loading a different scene, an eventual Editor round-tripping content, a
second validation scene for a future rendering-feature Spec — needs a
real load path, not a second copy of hand-written C++ construction
code. [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md)
already named this exact gap as Candidate Backlog work when Spec 0014
adopted its own module boundary; this Spec is that work, narrowed to
the smallest slice that proves the whole pipeline end to end: author →
cook → load → instantiate → render, using the scene Atlantis already
has, not a new one.

## Goals

- A human-editable, deterministic scene authoring source format,
  extending `Atlantis::AssetSystem`'s existing mesh-source grammar
  style — no general parser library.
- A build-time scene cooker, producing a strict, versioned,
  little-endian runtime artifact plus a metadata sidecar, atomically
  written, reusing the existing `atlantis_asset_cooker` Tools
  executable and CMake re-import-triggering pattern.
- A Runtime load path that decodes the artifact, resolves and eagerly
  loads every `Renderable`'s mesh dependency against a small,
  build-generated, locally-held resolver (never a global or persistent
  asset database), and instantiates a fully-populated `Atlantis::World`
  — transactionally: every mesh dependency resolved and loaded, and
  `World` instantiation complete, before anything is published; no
  partial state, and no `Entity` created at all, on any failure.
- Strict validation at both cook time and load time: duplicate
  scene-local node ID, a parent referencing an undeclared node, a
  parent cycle, an invalid or missing active-camera reference, a
  non-finite authored float, a corrupted/truncated/version-mismatched
  artifact, and an unresolved mesh reference are all explicit,
  distinct, recoverable error conditions — never a crash, never a
  silently-wrong scene.
- Proof that the loaded scene is pixel-identical to the scene Spec 0014
  already ships: a new, GPU-required headless test loads the cooked
  scene asset through the new loader, renders one frame through the
  existing, unmodified extraction/`Renderer` path, and matches the
  existing checked-in `world_scene` golden with **zero** difference —
  no new golden captured, no existing golden modified.
- Runtime's windowed path gains only a smoke check (the scene loads,
  the window shows something) and a manual visual re-confirmation,
  matching Spec 0014's own already-established V17/V20 split — never a
  claim of automated swapchain pixel comparison, which does not exist
  and is not introduced here.

## Non-Goals

Explicitly excluded from this Spec's design:

- **Texture/Sampler, PBR Material, Light, Shadow, Animation, or
  Post-processing data of any kind.** A scene's own `Renderable` still
  names exactly one mesh `AssetId`, unchanged from Spec 0014's own
  fixed-type component shape — this Spec adds no new component type,
  only a way to populate the ones that already exist.
- **An Editor, a Prefab system, or a general-purpose reflection/
  serialization framework.** The authoring grammar is a strict,
  fixed-shape parser for exactly this Spec's own scene schema — the
  same "hand-written strict parser, not a generic library" choice
  `mesh_source.h` already made, not a reusable (de)serialization
  mechanism any other module is meant to consume.
- **Saving a live Runtime `World` back out to an artifact.** This Spec
  is load-only — authoring happens by hand-editing the source text file
  and re-cooking, never by exporting live `World` state. No
  `World`→artifact direction exists anywhere in this Spec.
- **Persisting `EntityId`, `WorldIdentity`, or any pointer of any
  kind.** Every reference inside an artifact or `ValidatedSceneData` is a
  scene-local, cook-time-assigned identifier — never a live handle.
  Real `EntityId`s are minted fresh, in-process, only at instantiation
  ([ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)).
- **Network replication of any kind.**
- **Runtime scene replacement, hot-reload, or streaming.** Runtime
  loads exactly one scene, once, before its first frame — the same
  single-scene shape Spec 0014's own validation scene already has.
  Nothing in this Spec's contract forbids a future Spec from adding
  reload; nothing here builds toward it either.
- **Android, iOS, or Linux.** Windows remains this Spec's own verified
  target, matching every prior Spec in this repository.
- **Any new third-party dependency, including a general-purpose JSON
  (or other structured-data) parser.** The authoring format is a
  strict, hand-rolled grammar; the runtime artifact is a hand-rolled
  binary layout — both follow `mesh_source.h`/`mesh_artifact.h`'s own
  already-`Accepted` precedent exactly.
- **Any change to `Renderer`, `RHI`, `RenderGraph`, or Vulkan Backend's
  own public API.** This Spec is entirely upstream of `DrawItem`
  construction — the exact same `Renderer::drawFrame()`/`DrawItem`
  contract Spec 0014 already uses, unmodified.
- **An independent Asset Catalog or Registry** — a persistent,
  cross-scene, cross-session store mapping `AssetId`s to file locations.
  Mesh-dependency location for this Spec's own scope is a small,
  build-generated, per-scene manifest consumed into a local, immutable,
  per-`RuntimeApplication` resolver — never a global, mutable, or
  persistent database
  ([ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)).
  A real Asset Catalog remains a named, future Candidate Backlog item;
  see Out of Scope / Future Work.
- **The broader "stable GUID/handle schemes, schema versioning and
  migration" scope originally named for Candidate Backlog item 2**
  (`specs/README.md`'s own prior description) **— disclosed narrowing,
  not silent scope drift.** This Spec's own scene-local node identity
  ([ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md))
  is meaningful only within one artifact, resolved away entirely by the
  time a real `World` exists, and is explicitly not a durable,
  cross-session, cross-artifact, or Editor-stable identity scheme. A
  general stable-identity/migration system — needed for save games,
  networking, or an Editor referencing objects across sessions — remains
  future, unscoped work; see Out of Scope / Future Work.

## Requirements

### Functional

- **Authoring source**: a strict, versioned, plain-text grammar (one
  scene per file) declaring, per node: an explicit `node_id` (unsigned
  integer, author-assigned, unique within the file), an optional parent
  `node_id`, a local `Transform` (position, Euler angles in radians,
  scale — the same fields `atlantis::world::Transform` already has), an
  optional `Camera` (`fovYRadians`/`nearZ`/`farZ`), an optional
  `Renderable` naming a mesh by its **logical path** (never an
  `AssetId`). Exactly one node may be designated the scene's own active
  camera. See
  [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md).
- **Cooker**: `atlantis::asset_system::cookScene(...)` (exact name a
  Plan-level detail) reads the authoring source, validates it against
  its own dedicated `SceneCookError`-style error enum (exact enumerator
  names a Plan-level detail; the *conditions* are fixed here and are
  distinct from every other error domain below): `DuplicateNodeId`, a
  parent `node_id` naming no declared node, `ParentCycle`, more than one
  node (or zero, if one is required) claiming the active-camera role, an
  active-camera node with no declared `Camera`, and any non-finite
  authored float. It resolves every `Renderable`'s logical path to an
  `AssetId` via the existing `computeAssetId()`, remaps every `node_id`
  to a dense, zero-based array index in declaration order, and writes a
  versioned, unconditionally little-endian binary artifact plus a text
  metadata sidecar, atomically (write-to-temp-then-rename, the existing
  pattern `cookStaticMesh()`/`cook.h` already established). Exposed via
  a new mode of the existing `atlantis_asset_cooker` Tools executable,
  not a second binary.
- **Loader**: `atlantis::asset_system::decodeScene(...)` (exact name a
  Plan-level detail) reads the artifact and its metadata sidecar against
  its own dedicated `SceneArtifactDecodeError`-style enum — distinct
  from `SceneCookError` and from the mesh pipeline's own
  `ArtifactDecodeError` — independently re-validating every one of the
  cooker's own conditions against the artifact's actual bytes (magic,
  schema version, every offset/count consistent, every parent/active-
  camera index in range and non-cyclic, the active-camera node's own
  decoded record provably has `Camera` fields present — never assuming
  a well-formed cooker output), cross-checks the sidecar against the
  artifact, and returns a `ValidatedSceneData` — a flat, array-indexed value
  type owned entirely by `Atlantis::AssetSystem`, naming no
  `Atlantis::World` type (`Transform`/`Camera`/`Renderable`), no
  `EntityId`, no `WorldIdentity`, no pointer, and no logical path
  anywhere in its own fields (see
  [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)
  for the exact `DecodedTransform`/`DecodedCamera`/`DecodedRenderable`
  DTO shapes and why they must not alias `Atlantis::World`'s own
  types). **By the time `decodeScene()` returns `Ok`, `ValidatedSceneData` is
  exhaustively proven valid — structurally and semantically** — nothing
  downstream re-derives or re-checks any of these conditions.
  **`ValidatedSceneData` is encapsulated so this is a type-level
  guarantee, not a caller convention**: its own node array and every
  parent/active-camera index are `private`; its only non-default
  constructor is `private`, callable only by `decodeScene()` itself (a
  `friend` relationship, matching `atlantis::world::EntityId`'s own
  established `friend class World;` pattern); its public surface is
  read-only accessors only — no setter, no mutable reference, no way
  for any caller to construct or mutate a `ValidatedSceneData` naming
  arbitrary node data. See
  [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)
  for the full encapsulation contract.
- **Build-time mesh-dependency declaration**: a scene asset's own CMake
  declaration (`atlantis_add_scene_asset(...)`, exact name a Plan-level
  detail) names, explicitly, which already-declared mesh assets it
  depends on (`MESH_DEPENDENCIES`) — never auto-discovered from cooked
  artifact content, since CMake itself cannot run `computeAssetId()` or
  parse a binary artifact. This wires real `add_dependencies()` build
  ordering against each named mesh's own existing target, and emits a
  small, per-scene, build-tree-local text manifest — each declared
  dependency's own authoring-time logical path plus its own build-tree
  artifact/metadata paths — via `file(GENERATE ...)`, the same mechanism
  `atlantis_finalize_asset_validation()`'s `declared_assets.txt` already
  uses. Rebuild scoping is exact: editing the scene source, the scene
  cooker, or one of this scene's own declared mesh dependencies
  re-triggers exactly this scene's own cook step, via ordinary CMake
  `DEPENDS` staleness checking; an unrelated scene or mesh never appears
  in this command's own dependency list and never triggers it — not a
  new caching layer. A scene whose authoring source references a mesh
  logical path never declared as a `MESH_DEPENDENCIES` entry is not a
  build error — it surfaces at Runtime load time as an unresolved
  `AssetId` (below).
  See
  [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md).
- **Runtime mesh-dependency resolution and load**: Runtime reads its own
  manifest (named by a new `BootstrapConfig` field, populated the same
  CMake-compile-definition way every existing field already is) once at
  startup, using the same strict, fixed-grammar text parsing every other
  format in this pipeline uses — a malformed line is a hard failure.
  For each well-formed entry, Runtime computes its own `AssetId` via
  `computeAssetId()` over that entry's own logical path, forming a
  (logical path, `AssetId`, artifact/metadata locator) triple, and
  validates the manifest as a whole before trusting any entry: a
  duplicate logical path, an `AssetId` collision between two distinct
  paths, and a mismatch between an entry's own computed `AssetId` and
  its own metadata sidecar's recorded `asset_id` are each a distinct,
  named failure (mirroring `AssetSetError::AssetIdCollision`'s and
  `AssetLoadError::MetadataArtifactMismatch`'s own established
  precedent, applied per-scene). A declared dependency the scene never
  actually references is **not** an error. The result is a local,
  immutable, per-`RuntimeApplication` `AssetId → (artifactPath,
  metadataPath)` resolver map — never global, never persistent, never
  mutated after construction. After decoding the scene, Runtime
  collects the complete, deterministically-ordered (ascending
  first-reference in `ValidatedSceneData`'s own array order) set of
  distinct `AssetId`s the scene actually references, then proceeds in
  two explicit phases: **(1) resolve** every one against the resolver
  map — if any is missing, the load fails immediately, before any file
  I/O for this phase and before any `Entity` is created; **(2) load**
  each resolved entry via the existing `loadStaticMeshAsset()` +
  `createMesh()`, in that same deterministic order (never relying on
  `std::unordered_map`'s own unspecified iteration order), accumulating
  a mesh resource map (`AssetId → Mesh`). **Every resolution and every
  load must succeed before any `Entity` is created** — any failure in
  either phase fails the entire scene load immediately, classified via
  `RuntimeInitError` (a new enumerator per distinct condition, exact
  names a Plan-level detail) — never `WorldError`, `SceneCookError`,
  `SceneArtifactDecodeError`, or the mesh pipeline's own `AssetLoadError`
  directly (Runtime's own enumerator may wrap one for diagnostic detail,
  but the *classification* callers observe is Runtime's own). The
  manifest itself is build-tree-private, exactly like every other CMake
  output already in this repository — never embedded in, or shipped
  alongside, the scene artifact, which remains fully portable content
  containing only `AssetId`s.
- **Instantiation**: a new, **infallible** `Atlantis::World` entry point
  — `World fromValidatedSceneData(const ValidatedSceneData&)`, returning a
  fully-populated `World` by value, never `Result`-wrapped — because
  `decodeScene()`'s own exhaustive validation already guarantees every
  precondition its internal calls depend on (see
  [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)
  for exactly which conditions and why this makes a `Result` return
  type unnecessary rather than merely convenient). Two-pass,
  deterministic, ascending `ValidatedSceneData` array order: every node
  instantiated first (converting each `DecodedTransform`/
  `DecodedCamera`/`DecodedRenderable` into a real
  `world::Transform`/`Camera`/`Renderable`), every parent link set
  second, using a decoded-index→`EntityId` mapping that exists only for
  the duration of this one function call — never part of `World`'s own
  public state.
- **Runtime bootstrap**: `BootstrapConfig` gains the scene artifact/
  metadata paths and the mesh-dependency manifest path as new fields,
  populated the same CMake-compile-definition way every existing
  `BootstrapConfig` field already is — no CLI parsing, no environment
  variable, no config file. `initializeSteps()` replaces its call to
  `buildValidationScene()` with: decode scene → resolve and load every
  mesh dependency into a local mesh resource map → call `World`'s
  infallible instantiation entry point → **only once both the mesh
  resource map and the `World` value are fully constructed**, move both
  into `RuntimeApplication`'s own members, replacing whatever it held
  before. Any failure at the decode or dependency-resolution stage
  `markFailed()`s and propagates, exactly matching `initializeSteps()`'s
  own existing per-step error-handling shape — no explicit rollback
  code; an unpublished `World` or mesh resource map is simply destroyed
  by ordinary C++ scope exit. `runFrame()` uses the published `World`
  and mesh resource map exactly as today (Plan 0014 Section D9,
  `resolveMeshAsset()` now querying a small map instead of one
  hard-coded `AssetId`).
- **First scene asset**: the exact five-cube, one-hierarchy,
  one-camera scene `buildValidationScene()` currently hand-builds is
  authored as this Spec's own first, and only required, scene asset —
  same entity positions/rotations, same parent relationship (D
  attached to C), same camera transform and lens parameters, so the
  rendered result is bit-for-bit identical to what Spec 0014's own
  `world_scene` golden already captured.
- **Golden reuse, not a new golden**: a new GPU-required headless test
  loads the first scene asset through the real cook→decode→resolve→
  instantiate path, renders one frame through the existing extraction/
  `Renderer` pipeline, and compares against the **existing, already
  checked-in** `world_scene` golden
  (`tests/image_regression/goldens/world_scene/`) — zero channel
  difference required, matching `minimal_cube`'s own already-shipped
  precedent of validating two independent construction paths (a
  hand-authored fixture and an Asset-System-sourced one) against one
  shared golden. No new golden is captured; the existing one is not
  modified.
- **Windowed smoke, not pixel comparison**: the existing windowed GPU
  smoke test (`runtime_smoke_gpu_tests.cpp`) is extended (or a sibling
  case added) to confirm the scene-asset load path succeeds in the real
  `atlantis_runtime` executable and frames render — matching Spec
  0014's own V17 shape exactly. A manual, genuine-human visual
  re-confirmation (matching Spec 0014's own V20 requirement and its own
  recorded PASS precedent) is this Spec's own equivalent verification
  step — never described as, or substituted by, an automated
  swapchain pixel comparison, which this Spec does not introduce.

### Non-functional

- **Performance:** cooking and loading a single-digit-node scene is not
  a hot path — no performance budget beyond "does not noticeably delay
  Runtime startup," matching Spec 0014's own similarly unbudgeted
  scene-construction cost.
- **Memory:** `ValidatedSceneData`, the mesh-dependency resolver map, and the
  mesh resource map are all flat, bounded-size values scoped to one
  scene load — the resolver and `ValidatedSceneData` are freed once
  instantiation completes; no persistent cache, and no derived-data
  store beyond what CMake's own build tree already keeps for every
  other cooked asset (the runtime artifact, its metadata sidecar, and
  the new per-scene dependency manifest are all ordinary,
  regenerated-every-build CMake outputs, not a new cache category).
- **Portability (within the Vulkan-only Phase 1 constraint):** the
  artifact's unconditional little-endian encoding (matching
  `mesh_artifact.h`'s own established discipline) makes the format
  itself host-endianness-independent, though this Spec's own verified
  target remains Windows, matching every prior Spec.
- **Other:** every new error condition is a distinct enumerator, never
  collapsed into a generic "load failed" — matching this repository's
  own consistent error-taxonomy discipline
  (`src/asset_system/include/atlantis/asset_system/errors.h`'s own
  existing shape).

## Proposed Design

Build time (once per scene, CMake-triggered) and load time (once per
Runtime startup), per
[ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)
and
[ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md):

```
Build time:
  authoring source (.scene.txt) ─┐
                                  ├─ cookScene() [AssetSystem] ─▶ runtime artifact (.ascene) + metadata sidecar
  MESH_DEPENDENCIES (CMake) ─────┴─ file(GENERATE) ─────────────▶ per-scene dependency manifest (text, build-tree)

Load time (Runtime::initializeSteps(), once, before the first frame):
  dependency manifest ─▶ parse + validate (dup path / AssetId collision / metadata mismatch)
                              │
                     computeAssetId() per entry ─▶ local AssetId→(artifact,metadata) resolver map
                                                              │
  runtime artifact + sidecar ─▶ decodeScene() [AssetSystem] ─┼─▶ ValidatedSceneData (encapsulated: private
                                                              │    fields, decodeScene()-only construction,
                                                              │    read-only accessors -- fully validated
                                                              │    by construction, not by convention)
                                                              │         │
                              collect distinct AssetIds, ascending first-reference order
                                                              │         │
                        phase 1: resolve ALL against the map (fail fast, no I/O, no Entity yet)
                                                              │         │
                        phase 2: loadStaticMeshAsset()+createMesh(), same deterministic order
                                                              │         │
                                                    mesh resource map   │
                                                    (AssetId → Mesh)    │
                                                              │         ▼
                                                              │   World::fromValidatedSceneData()  [World, infallible --
                                                              │    ValidatedSceneData's own encapsulation is what
                                                              │    makes this a real guarantee, not an assumption]
                                                              │         │
                                                              ▼         ▼
                                    only on full success: publish (mesh resource map, World) together into RuntimeApplication
                                    any failure at any stage: nothing published, no partial state, no explicit rollback code
```

Module ownership follows
[ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md):
`Atlantis::AssetSystem` owns both artifact-side transformation arrows
(cook, decode) and the `DecodedTransform`/`DecodedCamera`/
`DecodedRenderable` DTO shapes; `Atlantis::World` owns the one arrow
that produces a real `World` (`fromValidatedSceneData()`, infallible, doing
the genuine DTO→domain-type conversion internally); `Atlantis Runtime`
owns mesh-dependency resolution and loading (a real capability, not
mere composition) and publishes the final result transactionally. See
each ADR's own Decision for the full reasoning; this section states the
resulting shape, not a second copy of the architecture.

## Architectural Impact

This Spec introduces a new subsystem boundary (does the scene
cook/decode/instantiate pipeline live in an existing module or a new
one), a new file format (authoring source, runtime artifact, in-memory
`ValidatedSceneData`), a new cross-module public entry point (`World` gaining
a `ValidatedSceneData`-consuming constructor-like function), and a new
transactional-loading contract — each recorded in its own
single-responsibility ADR, per AGENTS.md's Golden Rule:

- [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)
  — module boundary and ownership: no new top-level module; cook/decode
  and the `ValidatedSceneData` DTO shapes extend `Atlantis::AssetSystem`
  (never naming an `Atlantis::World` type, avoiding a dependency cycle);
  the real `ValidatedSceneData`→`World` conversion extends `Atlantis::World`
  itself; Runtime's own new work is mesh-dependency resolution/loading,
  not a duplicate translation layer.
- [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)
  — artifact format, versioning, and scene-local node identity:
  authoring/runtime/in-memory shapes, explicit author-assigned node IDs
  remapped to dense array indices at cook time, cook-time and load-time
  validation exhaustive enough (including the active-camera-has-`Camera`
  semantic check) to make `ValidatedSceneData` a fully-proven-valid type
  — and **encapsulated** (private fields, `decodeScene()`-only
  construction, read-only accessors) so that proof is a type-level
  guarantee, not a caller convention.
- [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)
  — transactional instantiation: a concrete, buildable mesh-dependency
  location mechanism (build-generated manifest, local Runtime-side
  resolver, no global catalog); mesh dependencies resolved and loaded
  before any `Entity` exists; `World`'s own new entry point is
  infallible, not a `WorldError`-reusing `Result`; two-pass deterministic
  instantiation order; RAII-based all-or-nothing semantics spanning both
  the mesh resource map and the `World` value, with no explicit
  rollback code.

**Per [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md)'s
own procedural requirement**, this Spec states explicitly: authoring
representation (human-editable scene source) and runtime representation
(versioned binary artifact, then `ValidatedSceneData`, then a real `World`)
are **three distinct shapes connected by two named transformation
steps** (cook, instantiate) — not the same structure, and not left
unaddressed.

## Human Review Decision Table

| # | Question | Recommendation | Rejected Alternative(s) | Where |
|---|---|---|---|---|
| 1 | Does the scene cooker/loader belong to `Atlantis::AssetSystem`, or does it need an independent Serialization module? | Split: cook/decode extends `Atlantis::AssetSystem` (no new dependency, matches `loadStaticMeshAsset()`'s own "CPU data only" role); instantiate extends `Atlantis::World` itself (avoids `AssetSystem`→`World`, which would close a cycle, since `World` already depends on `AssetSystem`). No new top-level module. | A new `Atlantis::Serialization`/`Atlantis::SceneAsset` module owning all three steps — structurally valid, not chosen because both existing modules already have a narrower, established place for their own half. | [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md) |
| 2 | What format, version, and byte order do the authoring source, metadata sidecar, and runtime artifact each use? | Authoring: strict versioned plain text, `mesh_source.h`'s own grammar style. Artifact: fixed binary header + per-node records, unconditionally little-endian, explicit shift/mask (never `memcpy`), matching `mesh_artifact.h` exactly. Metadata sidecar: text, cross-checked against the artifact at load, matching the mesh pipeline's own convention. | A structured-text format (JSON/YAML) for authoring or metadata — rejected per this Spec's own Non-Goals (no new dependency, no general parser). | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md) |
| 3 | What type is a scene-local node ID, how is ordering handled, and what are the parent/active-camera reference rules? | Explicit, author-assigned unsigned integer `node_id`, unique per file; the cooker remaps it to a dense, zero-based array index (declaration order) in the artifact and `ValidatedSceneData`; parent/active-camera references are plain array indices at runtime. | Pure array-position identity (no explicit ID) — rejected because it makes the required `DuplicateNodeId` error case structurally unreachable and couples authoring convenience to declaration order. Reusing `EntityId` itself — rejected outright, meaningless before a `World` exists or across a process boundary. | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md) |
| 4 | When is a `Renderable`'s logical-path mesh reference converted to an `AssetId`, and when is its existence/location validated? | Conversion (path syntax check + `computeAssetId()`) happens at **cook time**, unconditionally — the artifact never contains a path. Existence/location is resolved at **Runtime load time**, against a local resolver map built from a build-generated, per-scene dependency manifest (Decision 13 below) — not at cook time, since a scene need not be cooked in mesh-availability build order. | Cook-time cross-validation against the global declared-asset list (reusing `atlantis_finalize_asset_validation()`'s own mechanism) — rejected as this Spec's own recommendation because it would couple scene cooking to mesh-cooking build order and to a *global* list, for a check this Spec's own explicit, per-scene `MESH_DEPENDENCIES` declaration (Decision 13) already covers more precisely; Human Review may still prefer it as additional defense-in-depth. | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md); [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 5 | How does the cooker guarantee determinism and atomic writes? | Reuses `cookStaticMesh()`'s own exact pattern: a hand-bumped, independently-versioned behavior constant; write-to-temp-then-rename in the output directory; no filesystem timestamp or other non-deterministic input reflected in output bytes. | A new, scene-specific atomic-write mechanism — rejected, no reason exists to diverge from an already-`Accepted`, already-shipped pattern. | [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md) |
| 6 | Where is the boundary between the artifact loader and `World` instantiation, and what does `ValidatedSceneData` contain? | Loader (`Atlantis::AssetSystem`) decodes bytes into `ValidatedSceneData` — an `AssetSystem`-owned value type using its own `DecodedTransform`/`DecodedCamera`/`DecodedRenderable` DTOs, never `atlantis::world`'s own types (naming one would give `AssetSystem` a real dependency on `World`, closing a cycle). Instantiation (`Atlantis::World`'s own new entry point, `World fromValidatedSceneData(const ValidatedSceneData&)`) performs the real DTO→domain conversion and returns `World` **by value, infallibly** — `ValidatedSceneData`'s own exhaustive prior validation (Decision 9 below) removes every reachable failure case. Runtime composes decode → dependency resolution → instantiation; no Runtime-private *translation* file the way `scene_extraction.h` is needed for World→Renderer, though Runtime does gain real dependency-resolution logic of its own (Decision 13). | Runtime itself owning decode-to-`World` translation (like `scene_extraction.h`) — rejected because the headless image-regression test would then have to duplicate a strict binary decoder rather than reuse one. `ValidatedSceneData` reusing `atlantis::world::Transform`/`Camera`/`Renderable` directly — rejected outright, would give `AssetSystem` a compile-time dependency on `World`. | [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md); [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md) |
| 7 | What is the transactional failure/rollback contract, and what exactly must succeed before any `Entity` exists? | Sequence: decode scene (fails → nothing published) → resolve **every** mesh dependency and load it (fails → nothing published, no `Entity` created yet) → call `World::fromValidatedSceneData()` (infallible) → **only now**, with both the mesh resource map and the `World` value fully built, publish both together into `RuntimeApplication`. Any failure anywhere in this sequence leaves Runtime's own pre-existing state (for this Spec's scope: none, since this runs once at startup) completely untouched; an unpublished `World`/mesh map is destroyed by ordinary RAII — no explicit rollback code. | Publishing a partially-instantiated `World`, or a partial mesh resource map, and letting the caller decide — rejected outright, directly contradicts this Spec's own explicit no-partial-state requirement. Deferring mesh-dependency loading to be lazy/per-frame (today's single-mesh shape) — rejected, would let a scene "load" with cubes silently missing. | [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 8 | What is Runtime's minimal configuration entry point for its own startup scene and its own mesh dependencies? | `BootstrapConfig` gains scene artifact/metadata path fields plus the dependency-manifest path, populated the same CMake-compile-definition way every existing field already is — no CLI flag, no environment variable, no config file. | A CLI flag or environment variable naming the scene (or its dependencies) at process-launch time — rejected, `BootstrapConfig`'s own existing doc comment already states this is "the whole of Runtime's own configuration surface"; no reason to add a second configuration channel. | This Spec's own Requirements ("Runtime bootstrap") |
| 9 | What are the semantics for Camera aspect ratio, the active-camera reference, and a decoded active-camera node with no `Camera`? | Aspect ratio: unaffected, still computed per-frame from the live swapchain extent. Active-camera node lacking a `Camera`: **validated at cook time and independently re-validated at decode time** — a structural/semantic precondition `ValidatedSceneData` itself guarantees, *not* a `World`-runtime-checked condition — specifically so `World`'s own instantiation entry point never needs to report this as a `Result::Err` reusing `WorldError`. | Relying on `World::setActiveCamera()`'s own existing `Err(WorldError::NoCameraComponent)` runtime check instead — this was this Spec's own original design and was corrected during self-review: it conflates a scene-authoring mistake with `WorldError`'s own established "caller-supplied-handle" domain and would make instantiation fallible for a condition decode-time validation can catch instead. | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md); [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 10 | What is the deterministic multi-node instantiation/traversal order? | Two-pass, ascending `ValidatedSceneData` array-index order: every node instantiated (entity + Transform + Camera + Renderable) in pass one; every parent link set in pass two, using a decoded-index→`EntityId` mapping that exists only for the duration of the instantiation call. Generalizes `buildValidationScene()`'s own existing shape exactly. | Single-pass, deferring only forward-referenced parents — rejected, more complex for no benefit at this Spec's own scale. | [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 11 | How is the existing `world_scene` golden reused for verification? | A new GPU-required headless test loads the first scene asset through the real load path, renders one frame through the existing, unmodified extraction/`Renderer` pipeline, and compares against the existing checked-in golden — zero difference required, no new golden captured, matching `minimal_cube`'s own already-shipped "two construction paths, one golden" precedent. | Capturing a new, second golden for the artifact-loaded path — rejected, the whole point is proving the loaded scene is the *same* scene, not a plausibly-similar one; a second golden would only prove the new path renders *something*, not that it matches. | This Spec's own Requirements/Goals |
| 12 | Does this Spec require any change to `Renderer`/`RHI`/`RenderGraph`/Vulkan Backend's public API, or any new third-party dependency? | No to both — confirmed by this Spec's own Non-Goals and by every design decision above staying entirely upstream of `DrawItem` construction, using only hand-rolled parsing/encoding matching already-`Accepted` precedent. | N/A — this is a confirmation, not an open design choice. | This Spec's own Non-Goals |
| 13 | **(Must Fix, resolved.)** Given the scene artifact only stores `AssetId`s, and this repository has no Asset Catalog, how does Runtime actually locate the corresponding mesh artifact/metadata files on disk? | A scene's own CMake declaration explicitly names its mesh dependencies (`MESH_DEPENDENCIES`, referencing already-declared assets by name) — never auto-discovered, since CMake cannot run `computeAssetId()` or parse cooked binary content. This wires real `add_dependencies()` build ordering and emits a small, per-scene, build-tree text manifest (`file(GENERATE)`, matching `declared_assets.txt`'s own precedent) of (logical path, artifact path, metadata path) triples. Runtime reads this manifest once at startup, computes each `AssetId` itself via the existing `computeAssetId()`, validates it (item 16 below), and holds the result as a **local, immutable, per-`RuntimeApplication` resolver map** — never global, never persisted, never mutated after construction. The scene artifact itself never contains a path, only `AssetId`s. | (a) A build-generated *global* catalog/registry spanning every declared asset — rejected as broader than this Spec's own scope needs; remains a named future Candidate. (b) The scene artifact stores a portable logical path instead of an `AssetId` — rejected, already closed by [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md). (c) The scene artifact embeds each dependency's own mesh bytes inline — rejected, duplicates data across every referencing scene, breaks Asset System's one-canonical-location-per-asset model. (d) Auto-discover dependencies from cooked scene content at CMake-configure time — not achievable at all, CMake cannot compute an `AssetId` or parse a binary artifact. | [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 14 | **(Must Fix, resolved.)** How are cook-time, decode-time, dependency-resolution, and instantiation errors kept in four separate, correctly-scoped domains rather than one collapsing into another? | Four domains, each owned by the one stage that can actually detect the condition: `SceneCookError` (authoring/cook-time mistakes, `Atlantis::AssetSystem`); `SceneArtifactDecodeError` (artifact-bytes-level corruption/inconsistency, `Atlantis::AssetSystem`, independently re-checking every `SceneCookError` condition); a new `RuntimeInitError` enumerator (mesh-dependency resolution/load failure, `Atlantis Runtime`, before any `Entity` exists); and **no fifth domain for `World` instantiation**, because it is infallible by construction once `ValidatedSceneData` is exhaustively validated. `WorldError` is never reused for any scene-loading condition — its own domain (caller-supplied-handle operations against a live `World`) is unaffected and unextended by this Spec. | Reusing `WorldError` for scene-node conditions (duplicate ID, missing parent, cycle, invalid active camera) — rejected outright, this Spec's own self-review corrected exactly this mistake; see this table's own item 9. A single, generic "scene load failed" error collapsing all four stages — rejected, matches none of this repository's own existing error-taxonomy precedent (`errors.h`'s own five distinct enums for the mesh pipeline alone). | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md); [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 15 | **(Must Fix, resolved.)** Is `World` instantiation *really* infallible, or does that depend on callers not misusing `ValidatedSceneData` between decode and instantiation? | `ValidatedSceneData` is **encapsulated**, not merely documented as valid: its own node array and every parent/active-camera index are `private`; its only non-default constructor is `private`, callable only by `decodeScene()` (a `friend` relationship, matching `atlantis::world::EntityId`'s own established `friend class World;` pattern); its public surface is read-only accessors only — no setter, no mutable reference, no way to reach a node's own fields except through those accessors. No caller, anywhere, can construct or mutate an instance naming an out-of-range parent, a cycle, or an invalid active-camera reference — infallibility is therefore a type-system guarantee, not a documented convention. | A plain, publicly-mutable struct (public node array, public fields), relying on documentation ("do not mutate this after `decodeScene()` returns") to preserve validity — rejected during self-review; this was the prior draft's own design and does not actually guarantee what "infallible instantiation" claims. If a real API were found where closing every mutation path is not achievable, the correct fallback is an explicit `Result<World, SceneInstantiationError>` — not asserting infallibility on trust. | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md) |
| 16 | **(Must Fix, resolved.)** What exactly must the per-scene dependency manifest guarantee, and in what order are resolved dependencies loaded? | Each manifest entry is fully associated as a (normalized logical path, `AssetId`, real build-tree artifact/metadata locator) triple before it is trusted. Runtime validates, before building the resolver: no duplicate logical path; no `AssetId` collision between two distinct paths (mirroring `AssetSetError::AssetIdCollision`'s own precedent); each entry's own computed `AssetId` matches its own metadata sidecar's recorded `asset_id` (mirroring `MetadataArtifactMismatch`'s own precedent). A declared-but-unreferenced dependency is not an error. Resolution (phase one, no I/O) and loading (phase two) are separate steps, both walking the scene's own distinct-`AssetId` set in **ascending first-reference order within `ValidatedSceneData`'s own array** — never relying on `std::unordered_map`'s own unspecified iteration order. The manifest itself is build-tree-private, never embedded in or shipped with the scene artifact. | Trusting the manifest as-is, with no duplicate/collision/metadata cross-check — rejected during self-review, would let a stale or hand-edited manifest silently resolve to the wrong file. Rejecting unused `MESH_DEPENDENCIES` entries as an error — rejected as this Spec's own recommendation (authoring hygiene, not a mistake); Human Review may prefer strict rejection. Loading in hash-map iteration order — rejected, unspecified and non-deterministic. | [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |

## Alternatives Considered

- **Defer this Spec entirely; let a future Spec design scene loading
  once a second, genuinely different scene is needed.** Rejected: Spec
  0014 already named this exact gap as the next Candidate Backlog item
  when it drew `World`'s own module boundary specifically to hand off
  to it; deferring further leaves `buildValidationScene()`'s hard-coded
  construction as the only precedent anyone building on `World` has to
  copy from, and the existing `world_scene` golden as the perfect,
  already-verified target to prove a real loader against — waiting
  loses that target, it does not preserve optionality.
- **Design the full "stable GUID/handle schemes, schema versioning and
  migration" scope specs/README.md's own prior Candidate 2 description
  named**, not just a scene-asset load loop. Rejected as this Spec's
  own scope, disclosed explicitly in Non-Goals: nothing in loading one
  fixed scene once, at startup, into a single-process `World` needs a
  identity scheme durable across sessions, saves, or an Editor —
  designing one now would be exactly the speculative
  over-engineering AGENTS.md warns against, ahead of any real consumer
  (a save system, networking, an Editor) that would actually need it.
- **Skip the metadata sidecar; embed everything in one binary file.**
  Rejected: diverges from the mesh pipeline's own already-`Accepted`
  two-file (artifact + sidecar) convention for no stated benefit, and
  loses the sidecar's own existing cross-check value
  (`AssetLoadError::MetadataArtifactMismatch`'s own precedent catches a
  mismatched pair a single file cannot).
- **Have Runtime construct the `World` directly from `ValidatedSceneData`
  itself, inline in `initializeSteps()`, rather than giving `World` a
  new public entry point.** Rejected: identical reasoning to Human
  Review Decision 6 above — the headless test needs the same
  capability outside Runtime's own private composition code.

## Testing & Verification Plan

GPU-independent (unit-level, matching every prior module's own
three-layer verification model):

- Authoring-source parser: strict grammar acceptance/rejection,
  round-trip (parse → serialize → parse, matching `mesh_source.h`'s own
  established test shape), and every named `SceneCookError` condition
  (`DuplicateNodeId`, an undeclared parent reference, `ParentCycle`, an
  invalid/missing active-camera reference, an active-camera node with
  no `Camera`, a non-finite value) individually triggered and correctly
  reported.
- Artifact encode/decode round-trip: cooking a known scene then
  decoding it reproduces the exact same node data; every
  `SceneArtifactDecodeError` condition (bad magic, unknown schema
  version, truncated, an out-of-range parent/active-camera index, a
  decode-time-detected cycle, a decode-time-detected active-camera node
  with no `Camera`) individually triggered and correctly reported,
  matching `mesh_artifact.h`'s own already-shipped test discipline.
- `ValidatedSceneData`'s own unforgeability: a compile-fail negative
  test (documented as an intentionally-uncompilable example the test
  file comments but does not build, matching `EntityId`'s own V27
  precedent) confirming no external code can name a non-default
  constructor, assign to any node/parent/active-camera field, or obtain
  a mutable reference to any of them; `static_assert`s confirming its
  own public accessors return plain values/`const` references, never a
  mutable one; a round-trip confirming copy/move preserve every
  accessor's own observed value unchanged.
- `World`'s new instantiation entry point: a well-formed `ValidatedSceneData`
  produces a `World` whose entities/hierarchy/components match exactly;
  a `static_assert`-confirmed compile-time check that the entry point's
  own return type is `World`, not a `Result` — the type system itself
  proving infallibility, not merely an absence of currently-observed
  failures; deterministic instantiation order confirmed via repeated
  runs producing identical `EntityId` sequences (matching Spec 0014's
  own V14 determinism-test shape).
- Runtime's own mesh-dependency resolver: a resolver map built from a
  known manifest correctly maps each entry's `AssetId` (independently
  computed via `computeAssetId()` over the same logical path) to its
  own artifact/metadata paths; a duplicate logical path, an `AssetId`
  collision between two distinct paths, and a computed-`AssetId`-vs-
  metadata-`asset_id` mismatch are each individually triggered and
  correctly, distinctly reported; a manifest entry the scene never
  references does **not** fail the load; a scene referencing an
  `AssetId` with no resolver entry fails via `RuntimeInitError`
  **before any `Entity` is created** (confirmed directly, not inferred
  from exit status); a resolver entry whose own
  `loadStaticMeshAsset()`/`createMesh()` call fails likewise fails the
  whole load before any `Entity` exists; a scene with two `Renderable`s
  sharing one `AssetId` loads that mesh exactly once (mesh resource map
  keyed by `AssetId`, not by node); repeated runs against the same
  scene produce an identical mesh-load order (confirming the
  ascending-first-reference ordering is genuinely deterministic, not
  incidentally stable).
- Transactional failure, at every stage: a scene that fails cook-time
  validation never produces an artifact; an artifact that fails
  decode-time validation never produces a `ValidatedSceneData`; a scene whose
  own mesh dependency fails to resolve or load never reaches
  `World::fromValidatedSceneData()` and leaves `RuntimeApplication` with
  neither a `World` nor a mesh resource map — confirmed directly, by
  observing that no partial entity/component state and no partial mesh
  resource map exist to observe, the same way Spec 0014's own atomicity
  requirements are confirmed.

GPU-required (real hardware, matching Spec 0014's own two-tier
windowed/headless split):

- **Headless, golden reuse (this Spec's own central verification
  claim):** cook the first scene asset, load it through the real path,
  render one frame through the existing extraction/`Renderer` pipeline,
  compare against the existing checked-in `world_scene` golden — zero
  channel difference. The existing `world_scene` golden's own test
  (Spec 0014's own hand-authored-fixture path) is re-run unmodified,
  proving this Spec's own new path did not disturb it — the same
  cross-check `minimal_cube`'s own two-construction-path precedent
  already established.
- **Windowed smoke:** the real `atlantis_runtime` executable, launched
  with this Spec's own real scene artifact path, completes real
  windowed acquire/draw/submit/present frames — extending
  `runtime_smoke_gpu_tests.cpp`'s own existing shape, or a sibling case,
  Plan-level detail.
- **Manual, genuine-human visual verification** — matching Spec 0014's
  own V20 requirement and its own recorded 2026-08-23 PASS precedent
  exactly: an actual person, using a real graphical session, confirms
  the windowed scene renders correctly. **Never claimed as, or
  substituted by, an automated swapchain pixel comparison** — this Spec
  introduces no such capability.

## Risks & Open Questions

- **The exact authoring grammar's own field names/order, the exact
  artifact/manifest header layouts, and the exact CMake function/error-
  enumerator names**, are Plan-level details this Spec does not fix
  beyond the *conditions* and *shapes* named in Requirements — matching
  Spec 0014's own precedent of leaving concrete C++/file-format shapes
  to the Plan where no architectural content is at stake.
- **Whether cook-time cross-validation against the *global* declared-
  asset list is worth adding as additional defense-in-depth, on top of
  this Spec's own per-scene `MESH_DEPENDENCIES` mechanism** (Human
  Review Decision 4) is a named, disclosed open question — either
  answer is compatible with every ADR this Spec introduces.
- **This Spec's own scene-local node ID space is per-file, not
  repository-wide** — two different scene files may freely reuse the
  same `node_id` values; nothing in this Spec requires or provides a
  global node-ID registry, matching the deliberately narrow scope
  disclosed in Non-Goals.
- **The per-scene dependency manifest is regenerated on every CMake
  configure/build**, never a persistent, cross-build cache — a
  `MESH_DEPENDENCIES` declaration that goes stale relative to the
  scene's own authoring source is caught at Runtime load time
  (unresolved `AssetId`), not silently — but this Spec does not add a
  CMake-time check that the two stay consistent; Human Review may
  consider this an acceptable gap for this Spec's own scale, or note it
  as a candidate future Plan-level refinement.

## Out of Scope / Future Work

A durable, cross-session, cross-artifact stable identity/schema-
migration system, **and** an independent, general-purpose Asset
Catalog/Registry (a persistent, cross-scene, cross-session `AssetId`→
location store) — the broader scope originally implied by
`specs/README.md`'s prior Candidate Backlog description of this item,
plus the location mechanism this Spec's own scope needed a real answer
for but deliberately scoped as narrowly as possible
([ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)'s
own build-generated, per-scene, local-resolver design) — both remain
explicitly future, unscoped work, needed only once a real consumer (a
save system, networking, an Editor referencing objects across sessions,
or a second, independent scene needing to share dependency location
logic across scenes) exists to design against; see `specs/README.md`'s
own Candidate Backlog for the renamed entry tracking this. This Spec's
own scene-local node identity and per-scene dependency manifest are not,
and do not evolve into, either of those systems without a future Spec
explicitly designing it. Also remaining out of scope,
unaffected by this Spec: Texture/Sampler, PBR Material, Light, Shadow,
Animation, Post-processing; an Editor or Tool/Editor Connection
Protocol; a Gameplay SDK; Android/iOS/Linux — all remain later,
separately-specced work per
[docs/project-blueprint.md](../docs/project-blueprint.md).
