# Spec: Scene Asset & Serialization Foundation

- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path.
- **Created:** 2026-08-23
- **Related Plan(s):** None yet — Plan drafting is explicitly out of
  scope for this document; see AGENTS.md's own Spec → Plan → Human
  Review sequencing.
- **Related ADR(s):**
  [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)
  (module boundary and ownership),
  [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)
  (artifact format, versioning, scene-local node identity),
  [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)
  (transactional instantiation contract) — all `Proposed`, pending this
  Spec's own Human Review Approval.

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
- A Runtime load path that decodes the artifact, resolves every
  `Renderable`'s mesh `AssetId` against assets Runtime has actually
  loaded, and instantiates a fully-populated `Atlantis::World` —
  transactionally: full success before anything is published, no
  partial state on any failure.
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
  kind.** Every reference inside an artifact or `DecodedScene` is a
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
  Plan-level detail) reads the authoring source, validates it
  (`DuplicateNodeId`, `UndeclaredParentReference`, `ParentCycle`,
  `InvalidActiveCameraReference`, `NonFiniteValue` — exact enumerator
  names a Plan-level detail, the *conditions* are fixed here), resolves
  every `Renderable`'s logical path to an `AssetId` via the existing
  `computeAssetId()`, remaps every `node_id` to a dense, zero-based
  array index in declaration order, and writes a versioned, unconditionally
  little-endian binary artifact plus a text metadata sidecar, atomically
  (write-to-temp-then-rename, the existing pattern
  `cookStaticMesh()`/`cook.h` already established). Exposed via a new
  mode of the existing `atlantis_asset_cooker` Tools executable, not a
  second binary.
- **Loader**: `atlantis::asset_system::decodeScene(...)` (exact name a
  Plan-level detail) reads the artifact and its metadata sidecar,
  independently re-validates internal consistency (magic, schema
  version, every offset/count consistent, every parent/active-camera
  index in range, no cycle — never assuming a well-formed cooker
  output), cross-checks the sidecar against the artifact, and returns a
  `DecodedScene` — a flat, array-indexed, `Atlantis::World`-independent
  value type naming no `EntityId`, `WorldIdentity`, pointer, or logical
  path.
- **Instantiation**: a new `Atlantis::World` entry point consumes a
  `DecodedScene` and returns `atlantis::Result<World, WorldError>` — a
  fully-populated, freshly-constructed `World` on success, or
  `Err(WorldError::NoCameraComponent)` if the decoded active-camera node
  has no `Camera` (the one genuinely reachable failure mode; every other
  internal call is guaranteed to succeed by the loader's own prior
  validation — see
  [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)).
  Two-pass, deterministic, ascending `DecodedScene` array order: every
  node instantiated first, every parent link set second.
- **Runtime asset resolution**: before calling `World`'s instantiation
  entry point, Runtime confirms every decoded `Renderable`'s `AssetId`
  matches an asset Runtime has actually loaded — extending
  `resolveMeshAsset()`'s existing per-`AssetId` check to a whole-scene
  pass; an unresolved reference fails the load before any `World` is
  constructed, classified via `RuntimeInitError` (a new enumerator,
  exact name a Plan-level detail), matching `initializeSteps()`'s own
  existing failure-classification convention.
- **Runtime bootstrap**: `BootstrapConfig` gains the scene artifact/
  metadata paths as new fields, populated the same
  CMake-compile-definition way every existing `BootstrapConfig` field
  already is — no CLI parsing, no environment variable, no config file.
  `initializeSteps()` replaces its call to `buildValidationScene()`
  with: load asset → decode scene → resolve assets → instantiate
  `World` → on any failure, `markFailed()` and propagate, exactly
  matching its own existing per-step error-handling shape; on success,
  the returned `World` becomes `RuntimeApplication`'s own `world_`
  member, used by `runFrame()` exactly as today (Plan 0014 Section D9,
  unchanged).
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
- **Memory:** `DecodedScene` is a flat, bounded-size, single-owner value
  — freed once instantiation (success or failure) completes; no
  persistent cache, no derived-data store beyond the artifact/metadata
  sidecar CMake's own build tree already keeps for every other cooked
  asset.
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

Three stages, two transformation steps, per
[ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md):

```
authoring source (.scene.txt, human-edited)
        │  cookScene()  — Atlantis::AssetSystem, build time
        ▼
runtime artifact (.ascene) + metadata sidecar (.ascene.meta.txt)
        │  decodeScene()  — Atlantis::AssetSystem, load time
        ▼
DecodedScene (flat, array-indexed, World-independent, in memory)
        │  Runtime's own asset-resolution pre-check (RuntimeInitError)
        │  World's own instantiate-from-DecodedScene entry point
        ▼
Result<World, WorldError>  — full success, or no World at all
```

Module ownership follows
[ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md):
`Atlantis::AssetSystem` owns both transformation arrows (cook, decode);
`Atlantis::World` owns the final arrow (instantiate); `Atlantis Runtime`
composes the three calls in sequence, gaining no new Runtime-private
translation file the way `scene_extraction.h` was needed for the
World→Renderer direction. See each ADR's own Decision for the full
reasoning; this section states the resulting shape, not a second copy
of the architecture.

## Architectural Impact

This Spec introduces a new subsystem boundary (does the scene
cook/decode/instantiate pipeline live in an existing module or a new
one), a new file format (authoring source, runtime artifact, in-memory
`DecodedScene`), a new cross-module public entry point (`World` gaining
a `DecodedScene`-consuming constructor-like function), and a new
transactional-loading contract — each recorded in its own
single-responsibility ADR, per AGENTS.md's Golden Rule:

- [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)
  — module boundary and ownership: no new top-level module; cook/decode
  extends `Atlantis::AssetSystem`; instantiate extends
  `Atlantis::World`; Runtime composes, translates nothing new.
- [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md)
  — artifact format, versioning, and scene-local node identity:
  authoring/runtime/in-memory shapes, explicit author-assigned node IDs
  remapped to dense array indices at cook time, both cook-time and
  load-time validation.
- [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)
  — transactional instantiation: asset resolution as a Runtime pre-check,
  `World`'s own new entry point reusing `WorldError` with no new
  enumerator, two-pass deterministic instantiation order, RAII-based
  all-or-nothing semantics with no explicit rollback code.

**Per [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md)'s
own procedural requirement**, this Spec states explicitly: authoring
representation (human-editable scene source) and runtime representation
(versioned binary artifact, then `DecodedScene`, then a real `World`)
are **three distinct shapes connected by two named transformation
steps** (cook, instantiate) — not the same structure, and not left
unaddressed.

## Human Review Decision Table

| # | Question | Recommendation | Rejected Alternative(s) | Where |
|---|---|---|---|---|
| 1 | Does the scene cooker/loader belong to `Atlantis::AssetSystem`, or does it need an independent Serialization module? | Split: cook/decode extends `Atlantis::AssetSystem` (no new dependency, matches `loadStaticMeshAsset()`'s own "CPU data only" role); instantiate extends `Atlantis::World` itself (avoids `AssetSystem`→`World`, which would close a cycle, since `World` already depends on `AssetSystem`). No new top-level module. | A new `Atlantis::Serialization`/`Atlantis::SceneAsset` module owning all three steps — structurally valid, not chosen because both existing modules already have a narrower, established place for their own half. | [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md) |
| 2 | What format, version, and byte order do the authoring source, metadata sidecar, and runtime artifact each use? | Authoring: strict versioned plain text, `mesh_source.h`'s own grammar style. Artifact: fixed binary header + per-node records, unconditionally little-endian, explicit shift/mask (never `memcpy`), matching `mesh_artifact.h` exactly. Metadata sidecar: text, cross-checked against the artifact at load, matching the mesh pipeline's own convention. | A structured-text format (JSON/YAML) for authoring or metadata — rejected per this Spec's own Non-Goals (no new dependency, no general parser). | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md) |
| 3 | What type is a scene-local node ID, how is ordering handled, and what are the parent/active-camera reference rules? | Explicit, author-assigned unsigned integer `node_id`, unique per file; the cooker remaps it to a dense, zero-based array index (declaration order) in the artifact and `DecodedScene`; parent/active-camera references are plain array indices at runtime. | Pure array-position identity (no explicit ID) — rejected because it makes the required `DuplicateNodeId` error case structurally unreachable and couples authoring convenience to declaration order. Reusing `EntityId` itself — rejected outright, meaningless before a `World` exists or across a process boundary. | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md) |
| 4 | When is a `Renderable`'s logical-path mesh reference converted to an `AssetId`, and when is its existence validated? | Conversion (path syntax check + `computeAssetId()`) happens at **cook time**, unconditionally — the artifact never contains a path. Existence (does that `AssetId` correspond to an asset Runtime actually has loaded) is validated at **Runtime load time**, as a pre-check before instantiation — not at cook time, since a scene need not be cooked in mesh-availability build order. | Cook-time cross-validation against a declared-asset list (reusing `atlantis_finalize_asset_validation()`'s own mechanism) — rejected as this Spec's own recommendation because it would couple scene cooking to mesh-cooking build order for a check Runtime already needs to make anyway once real GPU asset loading is involved; Human Review may still prefer it. | [ADR-0053](../adr/0053-scene-artifact-format-versioning-and-node-identity.md); [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 5 | How does the cooker guarantee determinism and atomic writes? | Reuses `cookStaticMesh()`'s own exact pattern: a hand-bumped, independently-versioned behavior constant; write-to-temp-then-rename in the output directory; no filesystem timestamp or other non-deterministic input reflected in output bytes. | A new, scene-specific atomic-write mechanism — rejected, no reason exists to diverge from an already-`Accepted`, already-shipped pattern. | [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md) |
| 6 | Where is the boundary between the artifact loader and `World` instantiation? | Loader (`Atlantis::AssetSystem`) decodes bytes into `DecodedScene` only — no `Atlantis::World` type named anywhere in it. Instantiation (`Atlantis::World`'s own new entry point) consumes `DecodedScene`, returns `Result<World, WorldError>`. Runtime composes the two calls; no new Runtime-private translation file. | Runtime itself owning decode-to-`World` translation (like `scene_extraction.h`) — rejected because the headless image-regression test would then have to duplicate a strict binary decoder rather than reuse one, a heavier duplication than this codebase's existing small-helper "duplicated, not shared" precedent. | [ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md) |
| 7 | What is the transactional failure/rollback contract? | Instantiation builds directly into a freshly-constructed, not-yet-published `World`; any failure returns `Err` before that value is ever handed anywhere; its own RAII destructor tears down whatever partial state existed — no explicit rollback code. Runtime's own asset-resolution pre-check fails before even that `World` is constructed. | Publishing a partially-instantiated `World` and letting the caller decide — rejected outright, directly contradicts this Spec's own explicit no-partial-state requirement. | [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 8 | What is Runtime's minimal configuration entry point for its own startup scene? | `BootstrapConfig` gains scene artifact/metadata path fields, populated the same CMake-compile-definition way every existing field already is — no CLI flag, no environment variable, no config file. | A CLI flag or environment variable naming the scene at process-launch time — rejected, `BootstrapConfig`'s own existing doc comment already states this is "the whole of Runtime's own configuration surface"; no reason to add a second configuration channel for one more path. | This Spec's own Requirements ("Runtime bootstrap") |
| 9 | What are the semantics for Camera aspect ratio, the active-camera reference, and a decoded active-camera node with no `Camera`? | Aspect ratio: unaffected, still computed per-frame from the live swapchain extent — this Spec authors/cooks nothing aspect-related. Active-camera node lacking a `Camera`: reuses `World::setActiveCamera()`'s own already-`Accepted` `Err(WorldError::NoCameraComponent)` path as the sole enforcement point — not independently re-validated at cook time. | Cook-time validation that the active-camera node also declares a `Camera` — rejected as this Spec's own recommendation to avoid duplicating a check `World` already performs correctly; Human Review may still prefer defense-in-depth at cook time. | [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 10 | What is the deterministic multi-node instantiation/traversal order? | Two-pass, ascending `DecodedScene` array-index order: every node instantiated (entity + Transform + Camera + Renderable) in pass one; every parent link set in pass two, using the pass-one index→`EntityId` mapping. Generalizes `buildValidationScene()`'s own existing shape exactly. | Single-pass, deferring only forward-referenced parents — rejected, more complex for no benefit at this Spec's own scale. | [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md) |
| 11 | How is the existing `world_scene` golden reused for verification? | A new GPU-required headless test loads the first scene asset through the real load path, renders one frame through the existing, unmodified extraction/`Renderer` pipeline, and compares against the existing checked-in golden — zero difference required, no new golden captured, matching `minimal_cube`'s own already-shipped "two construction paths, one golden" precedent. | Capturing a new, second golden for the artifact-loaded path — rejected, the whole point is proving the loaded scene is the *same* scene, not a plausibly-similar one; a second golden would only prove the new path renders *something*, not that it matches. | This Spec's own Requirements/Goals |
| 12 | Does this Spec require any change to `Renderer`/`RHI`/`RenderGraph`/Vulkan Backend's public API, or any new third-party dependency? | No to both — confirmed by this Spec's own Non-Goals and by every design decision above staying entirely upstream of `DrawItem` construction, using only hand-rolled parsing/encoding matching already-`Accepted` precedent. | N/A — this is a confirmation, not an open design choice. | This Spec's own Non-Goals |

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
- **Have Runtime construct the `World` directly from `DecodedScene`
  itself, inline in `initializeSteps()`, rather than giving `World` a
  new public entry point.** Rejected: identical reasoning to Human
  Review Decision 6 above — the headless test needs the same
  capability outside Runtime's own private composition code.

## Testing & Verification Plan

GPU-independent (unit-level, matching every prior module's own
three-layer verification model):

- Authoring-source parser: strict grammar acceptance/rejection,
  round-trip (parse → serialize → parse, matching `mesh_source.h`'s own
  established test shape), and every named validation failure
  (`DuplicateNodeId`, `UndeclaredParentReference`, `ParentCycle`,
  `InvalidActiveCameraReference`, `NonFiniteValue`) individually
  triggered and correctly reported.
- Artifact encode/decode round-trip: cooking a known scene then
  decoding it reproduces the exact same node data; every artifact-level
  corruption case (`ArtifactDecodeError`-style: bad magic, unknown
  schema version, truncated, an out-of-range parent/active-camera
  index, a decode-time-detected cycle) individually triggered and
  correctly reported, matching `mesh_artifact.h`'s own already-shipped
  test discipline.
- `World`'s new instantiation entry point: a well-formed `DecodedScene`
  produces a `World` whose entities/hierarchy/components match exactly;
  an active-camera node with no `Camera` produces
  `Err(WorldError::NoCameraComponent)`; deterministic instantiation
  order confirmed via repeated runs producing identical `EntityId`
  sequences (matching Spec 0014's own V14 determinism-test shape).
- Runtime's own asset-resolution pre-check: a resolvable scene passes;
  an unresolvable `Renderable` reference fails before any `World`
  exists, classified via `RuntimeInitError`.
- Transactional failure: an artifact that fails validation at any stage
  (cook-time-equivalent corruption injected directly, since a
  cook-time-rejecting source never reaches an artifact) never leaves a
  `World` behind, confirmed the same way Spec 0014's own atomicity
  requirements are confirmed — by directly observing that no partial
  entity/component state exists to observe.

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

- **The exact authoring grammar's own field names/order, and the exact
  artifact header layout**, are Plan-level details this Spec does not
  fix beyond the *conditions* named in Requirements — matching Spec
  0014's own precedent of leaving concrete C++/file-format shapes to
  the Plan where no architectural content is at stake.
- **Whether cook-time cross-validation against a declared-asset list is
  worth its build-ordering cost** (Human Review Decision 4) is a named,
  disclosed open question, not a claim this Spec has settled — either
  answer is compatible with every ADR this Spec introduces.
- **Whether the active-camera-node-lacks-a-Camera check deserves
  cook-time defense-in-depth in addition to `World`'s own existing
  runtime check** (Human Review Decision 9) is similarly named, not
  settled.
- **This Spec's own scene-local node ID space is per-file, not
  repository-wide** — two different scene files may freely reuse the
  same `node_id` values; nothing in this Spec requires or provides a
  global node-ID registry, matching the deliberately narrow scope
  disclosed in Non-Goals.

## Out of Scope / Future Work

A durable, cross-session, cross-artifact stable identity/schema-
migration system — the broader scope originally implied by
`specs/README.md`'s prior Candidate Backlog description of this item —
remains explicitly future, unscoped work, needed only once a real
consumer (a save system, networking, or an Editor referencing objects
across sessions) exists to design against; this Spec's own scene-local
node identity is not, and does not evolve into, that system without a
future Spec explicitly designing it. Also remaining out of scope,
unaffected by this Spec: Texture/Sampler, PBR Material, Light, Shadow,
Animation, Post-processing; an Editor or Tool/Editor Connection
Protocol; a Gameplay SDK; Android/iOS/Linux — all remain later,
separately-specced work per
[docs/project-blueprint.md](../docs/project-blueprint.md).
