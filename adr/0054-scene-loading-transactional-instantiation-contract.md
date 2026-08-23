# ADR 0054: Scene Loading Transactional Instantiation Contract

- **Status:** Proposed
- **Date:** 2026-08-23
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)

## Context

[ADR-0052](0052-scene-asset-module-boundary-and-ownership.md) put
`DecodedScene → World` translation on `Atlantis::World` itself.
[ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)
made `DecodedScene` a fully-proven-valid type by the time anything
downstream consumes it — every structural precondition (no cycle, every
index in range) and, deliberately, the one semantic precondition that
matters (the active-camera node has a `Camera`) are already guaranteed
by decode time. What remains for this ADR to define: how Runtime turns
a `Renderable`'s `AssetId` into an actual, loadable mesh file on disk
(this repository has no Asset Catalog or registry of any kind today —
Runtime currently hard-codes exactly one mesh's own artifact/metadata
paths as CMake compile definitions), what "all-or-nothing" means
concretely across both mesh-dependency loading and `World`
construction together, and what error domain covers each distinct kind
of failure this pipeline can produce.

**This repository's current state, confirmed directly, not assumed:**
`src/runtime/main.cpp` populates `BootstrapConfig::assetArtifactPath`/
`assetMetadataPath` from exactly two CMake compile definitions
(`ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH`/`_METADATA_PATH`), themselves
sourced from `atlantis_add_static_mesh_asset()`'s own exported
`ATLANTIS_<name>_ARTIFACT_PATH`/`_METADATA_PATH`/`_TARGET` CMake
variables (`src/asset_system/CMakeLists.txt`). There is no code path
anywhere in this repository that looks up a build-tree artifact path
*from* an `AssetId` at runtime — the one existing mesh's own path is
wired in by name, at CMake configure time, by a human editing
`assets/CMakeLists.txt`. A scene referencing one or more meshes by
`AssetId` needs a real answer for how Runtime finds their files; "the
artifact contains the `AssetId`" is not, by itself, a location
mechanism.

## Decision

**1. Mesh dependencies are declared explicitly, in CMake, by the same
human who declares the scene asset itself — never auto-discovered from
cooked artifact content, and never a global catalog.** A new
`atlantis_add_scene_asset(NAME ... SOURCE ... MESH_DEPENDENCIES name1
name2 ...)` CMake function (exact name a Plan-level detail) extends
`atlantis_add_static_mesh_asset()`'s own established shape: each named
`MESH_DEPENDENCIES` entry must already be a declared asset (its own
`ATLANTIS_<name>_ARTIFACT_PATH`/`_METADATA_PATH`/`_TARGET` variables
already exist, from that mesh's own prior
`atlantis_add_static_mesh_asset()` call), so this function can
`add_dependencies()` on each one's own target (build-ordering
correctness — the mesh's own artifact exists before Runtime ever tries
to load it). **Rebuild scoping is exact, not approximate:** this
custom command's own `DEPENDS` names only the scene's own source file,
the scene cooker binary, and this specific scene's own declared
`MESH_DEPENDENCIES` targets — editing the scene source, the scene
cooker, or one of *its own* referenced meshes re-triggers exactly this
scene's own cook step (CMake's own ordinary staleness check, the same
`atlantis_add_static_mesh_asset()`'s own stamp/`DEPENDS` pattern
already relies on); an unrelated mesh or scene, declared elsewhere with
no `MESH_DEPENDENCIES` edge to this one, never appears in this
command's own `DEPENDS` list and so never triggers it. This is ordinary
CMake incremental-build behavior, not a new caching layer. It also emits
a small, per-scene, build-tree-local **text manifest** — one line per declared dependency, each recording that
mesh's own **authoring-time logical path** (already known to CMake, the
same `SOURCE` string that mesh's own `atlantis_add_static_mesh_asset()`
call used) plus its own build-tree artifact/metadata paths — via
`file(GENERATE ...)`, the identical mechanism
`atlantis_finalize_asset_validation()` already uses for
`declared_assets.txt`. **This manifest never contains an `AssetId`
directly** — CMake itself cannot compute one (`computeAssetId()` is a
C++ function); Runtime computes each entry's `AssetId` itself, at
startup, over the manifest's own logical-path string, via the already-
public, already-deterministic `computeAssetId()`/`LogicalPath`
normalization — the exact same computation the scene cooker already
performed over the same string when it resolved the `Renderable`'s
own reference at cook time
([ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)).
A mismatch between what a scene's authoring source references and what
its own CMake `MESH_DEPENDENCIES` declares is not a build-time error —
it surfaces at Runtime load time as the already-designed "unresolved
`AssetId`" condition (below), the same layered-validation shape this
Spec already uses elsewhere (a structural/build-time check plus an
independent, never-assumed-consistent runtime check).

**2. Runtime builds one local, immutable, per-instance resolver from
that manifest at startup — never a global or persistent one.**
`BootstrapConfig` gains a new field naming this manifest's own
build-tree path (populated the same CMake-compile-definition way every
existing `BootstrapConfig` field already is — no CLI flag, no
environment variable). `initializeSteps()` reads it once, computes each
entry's `AssetId`, and holds the resulting `AssetId → (artifactPath,
metadataPath)` map as a local, `RuntimeApplication`-scoped value —
built fresh every process run, never written back to disk, never
shared across `RuntimeApplication` instances, and never mutated after
construction. **The scene artifact itself still contains only
`AssetId`s** ([ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)'s
own Decision, unchanged) — every path involved lives entirely in this
ephemeral, machine-local, regenerated-every-build resolver, exactly how
the single existing mesh path already works today, generalized from one
entry to a small, explicitly-declared set.

**3. Every mesh dependency must resolve and successfully load before
any `Entity` is created.** After decoding the scene artifact
(`Atlantis::AssetSystem`, already fully validated per ADR-0053),
Runtime walks every `DecodedRenderable` in the `DecodedScene`, looks up
its `AssetId` in its own local resolver map, and — for every distinct
`AssetId` found, at most once — calls the already-`Accepted`
`loadStaticMeshAsset()` and `atlantis::renderer::createMesh()` exactly
as `initializeSteps()` already does for its own single existing mesh
today, accumulating the results into a **mesh resource map**
(`AssetId → real, GPU-backed `Mesh``). **If any `AssetId` has no
resolver entry, or any resolver-entry-backed load fails, the entire
scene load fails immediately — before `World`'s own instantiation entry
point is ever called, and therefore before any `Entity` exists.** This
is a distinct, Runtime-owned failure surface — classified via
`RuntimeInitError` (a new enumerator, exact name a Plan-level detail),
matching `initializeSteps()`'s own existing per-step classification
convention (`AssetLoadFailed`, `SceneConstructionFailed`) — **never**
`WorldError`, `AssetLoadError`, or `ArtifactDecodeError`, each of which
already means something else.

**4. `World`'s own new instantiation entry point is infallible —
`World fromDecodedScene(const DecodedScene&)`, returning by value, not
`Result`-wrapped.** [ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)'s
own decode-time validation already guarantees every precondition this
function's internal calls (`createEntity()`, `setLocalTransform()`,
`setCamera()`, `setRenderable()`, `setParent()`) depend on — no cycle,
every parent/active-camera index in range, the active-camera node
provably has a `Camera`. Each internal call is therefore
`ATLANTIS_CHECK_MSG`-guarded ("should never happen in correct
operation," matching `RuntimeApplication::runFrame()`'s own existing
convention for identically-reasoned invariants), not a `Result::Err`
path — there is no genuinely reachable failure left inside this
function to report, so it reports none, rather than inventing a
`SceneInstantiationError` (or reusing `WorldError`) for a condition
that structurally cannot occur. Internally, this function performs the
real `DecodedTransform`/`DecodedCamera`/`DecodedRenderable` →
`world::Transform`/`Camera`/`Renderable` conversion
([ADR-0052](0052-scene-asset-module-boundary-and-ownership.md)'s own
Decision item 2) using the mesh resource map's own already-loaded
`Mesh` objects is **not** its concern — a `Renderable`'s own
`AssetId` is copied into the component as-is; resolving it to a real
`Mesh` for drawing remains, unchanged, `resolveMeshAsset()`'s own
per-frame job ([ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)),
now querying a small map instead of one hard-coded `AssetId`.

**5. Nodes are instantiated in two passes, in the `DecodedScene`'s own
array order.** Pass one: `createEntity()` + `setLocalTransform()` +
(if present) `setCamera()` + (if present) `setRenderable()`, for every
node in ascending array-index order, recording a **decoded index →
`EntityId` mapping that exists only for the duration of this one
function call** — a local variable, never returned, never exposed on
`World`'s own public surface, discarded the moment the function
returns. Pass two: `setParent()` for every node that has one, using
that same transient mapping — a parent's own `EntityId` is guaranteed
to already exist regardless of authoring order, because pass one
already created every node before pass two links any of them. This
exactly generalizes `buildValidationScene()`'s own existing shape in
`src/runtime/src/runtime_application.cpp` (every cube entity created
first via its own `makeCubeEntity()` closure; `world.setParent(*d, *c)`
called only afterward). Instantiation order is deterministic by
construction: the same `DecodedScene` always produces the same sequence
of `createEntity()` calls against a freshly-constructed `World`, and
`World`'s own already-`Accepted` slot-assignment rule
([ADR-0049](0049-entity-identity-and-handle-invalidation.md)) makes the
resulting `index()`/`generation()` values deterministic in turn.

**6. All-or-nothing spans both the mesh resource map and the `World`
value together — published as one unit, or neither is.** Runtime's own
sequence is: decode scene (fails → nothing published) → resolve and
load every mesh dependency into a local mesh resource map (fails →
that map, and everything built so far, is simply dropped — RAII;
nothing published) → call `World::fromDecodedScene()` (infallible, per
item 4) → **only now**, with both the mesh resource map and the `World`
value fully and successfully constructed, does Runtime move them into
`RuntimeApplication`'s own members, replacing whatever it held before
(for this Spec's own scope: nothing, since this happens once, during
`initializeSteps()`, before `runFrame()` ever runs). **No explicit
rollback code exists anywhere in this sequence** — a `World` or a mesh
resource map that never gets published is simply destroyed by ordinary
C++ scope exit, exactly as any other local value would be; this is a
property of every type involved already being RAII/move-only, not a
new mechanism this ADR invents. **This Spec introduces no runtime scene
replacement, hot-reload, or streaming** — the contract is stated
generally enough to remain correct if a future Spec adds one, but this
Spec's own scope exercises it exactly once, at Runtime startup, and
never mutates a `RuntimeApplication` that has already reached
`Running`.

**7. Camera aspect ratio is unaffected — still computed per-frame from
the live swapchain extent**, exactly as
`extractCameraMatrices()`/`runFrame()` already do
([ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)).
Nothing about scene loading authors, cooks, or instantiates an aspect
ratio.

## Consequences

### Positive

- The mesh-dependency resolver mechanism is concrete and buildable
  entirely from primitives this repository already has —
  `computeAssetId()`, `file(GENERATE)`, explicit CMake `NAME`-based
  asset declaration, per-asset exported path/target variables,
  `add_dependencies()` — no new persistent store, no absolute or
  platform-specific path baked into any *shipped* artifact (only into
  an ephemeral, regenerated-every-build manifest, exactly like the
  single existing mesh path already is today).
- `World`'s own new entry point being genuinely infallible is not an
  aspiration bolted on afterward — it falls directly out of
  [ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)'s
  own decode-time validation being exhaustive; no new error type is
  invented, and no existing one (`WorldError`) is stretched to cover a
  condition outside its own established domain.
- Four distinct, correctly-scoped error domains now exist, each owned
  by the stage that can actually detect the condition: authoring/cook
  errors (a scene-cook-specific enum, `Atlantis::AssetSystem`, cook
  time), artifact decode/validation errors (a scene-decode-specific
  enum, `Atlantis::AssetSystem`, load time), mesh-dependency-resolution
  errors (`RuntimeInitError`, `Atlantis Runtime`, before any `Entity`
  exists), and — because instantiation is infallible — no fifth domain
  is needed for `World` construction itself.
- Requiring every mesh dependency to resolve and load before any
  `Entity` exists means a scene either renders completely correctly or
  Runtime never reaches `Running` with it — no half-drawn scene, no
  silently-missing cube, matching this Spec's own explicit
  no-partial-state requirement at the mesh-resource layer, not only the
  `World`-construction layer.

### Negative / Trade-offs

- Loading every referenced mesh eagerly, before instantiating `World`,
  is strictly more upfront work than today's single-mesh path (which
  loads its one mesh during `initializeSteps()` regardless) — a real
  but small cost at this Spec's own scale (a handful of distinct
  meshes), not budgeted further.
- The build-time manifest mechanism adds one more generated file per
  scene asset and one more `MESH_DEPENDENCIES`-style CMake declaration
  a human must keep honest — accepted because the alternative (auto-
  discovering dependencies from cooked scene content at CMake-configure
  time) is not achievable at all: CMake cannot run `computeAssetId()`
  or parse a cooked binary artifact itself.
- Runtime now owns two related but distinct concerns during scene load
  (mesh-dependency resolution, and swapping in the final published
  result) rather than one — accepted as the natural cost of Runtime
  being the only module with access to both "what's declared" (the
  manifest) and "what's actually GPU-loaded" (the mesh resource map),
  neither of which `Atlantis::AssetSystem` or `Atlantis::World` has any
  reason to know.

## Alternatives Considered

**For mesh location (the "Must Fix" question this ADR exists to
answer):**

- **A build-generated, scene-scoped dependency manifest, consumed by a
  local Runtime-side resolver — this ADR's own Decision.** Chosen: uses
  only existing mechanisms, keeps the resolver local and immutable, and
  requires no change to what the scene artifact itself contains.
- **The scene artifact stores a portable logical path instead of an
  `AssetId`, resolved fresh on every load.** Rejected —
  [ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)
  already rejected this for the identical reason (reintroduces a
  source-path dependency into the runtime artifact, redoes work a cook-
  time resolution already finished once) and this ADR does not reopen
  that decision.
- **The scene artifact embeds each referenced mesh's own bytes inline**
  (a "fat" artifact with no external reference at all). Rejected: every
  scene referencing the same mesh would duplicate its full vertex/index
  data on disk, breaks the one-canonical-location-per-asset model
  `Atlantis::AssetSystem` already established, and makes the mesh
  cooker's own already-`Accepted` artifact format irrelevant to scenes
  for no benefit.
- **A new, independent, general-purpose Asset Catalog/Registry Spec,
  drafted now, ahead of this one.** Rejected as this Spec's own
  prerequisite — genuinely useful once a second, independent consumer
  needs cross-scene or cross-session asset lookup, but not needed to
  prove the single-scene, startup-only load loop this Spec's own scope
  requires; a real Asset Catalog remains a named, future Candidate
  Backlog item (`specs/README.md`), not designed, scaffolded, or
  blocked on here.

**For the transactional/instantiation contract:**

- **A new, scene-loading-specific `SceneInstantiationError` enum on
  `World`'s own new entry point**, covering conditions ADR-0053's own
  decode-time validation already forecloses. Rejected: once
  `DecodedScene` is exhaustively pre-validated, there is nothing left
  for this enum to ever actually report — an error type with no
  reachable case is worse than no error type.
- **Reuse `WorldError::NoCameraComponent`** for the active-camera-node-
  lacks-a-`Camera` condition. Rejected — see
  [ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)'s
  own Alternatives Considered for the full reasoning; validating this
  at decode time instead is what makes instantiation infallible in the
  first place.
- **Resolve mesh dependencies lazily, per-frame, exactly as today's
  single-mesh path does** (skip unresolved `Renderable`s at draw time
  rather than failing the whole load). Rejected: contradicts this
  Spec's own explicit requirement that a scene load either fully
  succeeds or is entirely discarded — a scene that "loads" with some
  cubes silently missing is exactly the partial-state outcome this
  Spec exists to prevent.
- **Single-pass instantiation, deferring only forward-referenced
  parents.** Rejected: more complex than "create everything, then link
  everything" for no benefit at this Spec's own scale, and it would
  diverge from `buildValidationScene()`'s own already-shipped,
  already-correct two-pass shape for no reason.
- **Publish a partially-instantiated `World` (or a partial mesh
  resource map) and let the caller decide.** Rejected outright —
  directly contradicts this Spec's own explicit no-partial-state
  requirement, and would reintroduce exactly the "silent partial state"
  hazard [ADR-0049](0049-entity-identity-and-handle-invalidation.md)'s
  own atomicity guarantees were written to close for every other
  `World` mutation.
