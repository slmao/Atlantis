# Spec: Authoring Scene → Runtime World Bake

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-06
- **Related Plan(s):** none yet. Drafting a Plan is authorized only after this
  Spec's Approval.
- **Approval:** pending. The maintainer fixed the principle, the pipeline and
  the boundaries before drafting (2026-10-06, chat):
  - the bake product is the ECS world;
  - Runtime switches to it within this Spec;
  - goldens stay byte-identical, with no re-capture;
  - the bake builds on Specs 0049 and 0050 and lives in the World module;
  - Prefab, Editor, Gameplay components and Agent Patch are named only.

  These are recorded under Goals / Non-Goals and are not open questions.
- **Related ADR(s):**
  [ADR-0102](../adr/0102-authoring-runtime-world-separation-and-scene-bake.md)
  (`Proposed`, drafted alongside this Spec). It records the
  authoring/runtime separation principle, the bake's entry, product, hierarchy
  treatment and order guarantee, world::World's role, and the Runtime switch.
  It supersedes, in part, ADR-0051 (extraction's data source) and ADR-0048
  (what Runtime owns).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

This Spec establishes **Authoring Scene ≠ Runtime World** as an engine
principle and implements it:

- a validated scene is **baked** into a Spec 0050 ECS world (archetypes,
  chunks, SoA), in Atlantis World;
- Runtime switches its scene storage from `world::World`'s slot map to that
  baked world;
- the per-frame extraction becomes ECS queries. This is the Runtime-migration
  decision Spec 0050 ruling Q1 deferred.

The bake reuses existing parts:

- Spec 0049's semantic layer is the input's meaning;
- Spec 0050's `createEntities`/`EntityGuidMap` binds GUIDs;
- `schema::TypeId` gives component identity.

Rendered output must not change by a single byte. No golden is re-captured, and
the verification design makes that a constructive proof rather than an
observation.

## Motivation / Problem Statement

Current state read at `origin/main` `7d6e244`.

### Runtime renders from the editable world

- **Loading.** `RuntimeApplication` loads the scene artifact through the
  catalog (`scene_load.cpp`) and instantiates it into one `world::World`
  (`instantiateScene()`, `scene_instantiation.h`). It keeps that `World` as
  its scene (`runtime_application.cpp:1093`).
- **The frame** (`runFrame()`):
  - calls `world_->updateTransforms()` (`:1380`);
  - reads `activeCamera()`, `getWorldMatrix()` and `getCamera()` for the
    camera uniform, fog and bloom (`:1382-1552`);
  - walks `lightEntities()` to build `FrameLightingData` (`:1443-1450`,
    ADR-0088);
  - walks `renderableEntities()` twice: once for referenced materials
    (`:1571`), once to build the `DrawItem`s (`:1686-1774`).
- **The same type does both jobs.** `world::World` is the slot map with a
  parent/child hierarchy, per-component quartets and by-value accessors
  (ADR-0048–0051). It is the type an editor would edit and the type the frame
  reads. Nothing separates authored structure from what the frame consumes.

### No production code moves an entity, but a tested contract allows it

In production code, `world_` is mutated only by its construction at load.
Every later use in `src/runtime/` is a read:

- `activeCamera`;
- `getCamera`;
- `getWorldMatrix`;
- `getLight`;
- `getRenderable`;
- `lightEntities`;
- `renderableEntities`.

Searching the Runtime and Android sources finds no `setLocalTransform`,
`setParent`, `createEntity` or `destroyEntity` after load. So
`updateTransforms()` recomputes identical matrices every frame from a
hierarchy that never changes. Its only consumers are the three
`getWorldMatrix` reads above:

- the camera's eye and forward;
- each light's direction or position;
- each renderable's `objectToWorld`, plus the conformal check
  (ADR-0051).

But [Spec 0022](0022-dynamic-frame-uniform-updates-foundation.md)
(`Approved`) makes live edits a contract. Its Goals state that a
`World::setLight()` change, and every `World` mutation affecting the lighting
payload, becomes visible at the next successful frame's extraction. That
includes:

- a Light `Transform`;
- a parent-hierarchy `Transform`;
- Light entity creation or removal.

`runtime_smoke_gpu_tests.cpp` exercises this through a test-access hook
(`RuntimeSmokeTestAccess::world()`). It calls `createEntity`, `setLight`,
`setLocalTransform` and `setParent` on the running app's live `World`
between frames, and the comment at `runtime_application.cpp:1427-1440`
restates it. So `updateTransforms()` per frame is load-bearing for that
contract, though for no production path (Q2).

### Order is observable in pixels

- **Light order.** `renderableEntities()` and `lightEntities()` return live
  entities in **slot order** (`world.cpp:339-378`). For a freshly
  instantiated `World`, slot order is scene node order.
  `extractFrameLightingData()` packs lights into `FrameLightingData` in that
  order. The shader sums them in buffer order, so a different light order is
  a different floating-point sum.
- **Draw order.** `Renderer::drawFrame()` draws opaque items in caller order,
  then `std::stable_sort`s blended items back-to-front
  (`draw_order.cpp:36`), where equal depths keep caller order.
- **Upload order.** The material realization order is first-occurrence order
  over the same walk (`:1566-1584`).

An ECS query visits archetype order, then chunk, then row (ADR-0101 D5). It
equals node order only by coincidence of archetype layout.

### The goldens do not exercise Runtime's frame

- **No golden runs `RuntimeApplication::runFrame()`.** The scene-based
  image-regression fixtures re-implement its camera, light, material and draw
  walks over `world::World` themselves ("duplicated, not shared").
  - 34 files under `tests/image_regression/` and 5 under `tests/runtime/` call
    `updateTransforms`, `renderableEntities`, `lightEntities` or
    `instantiateScene`.
  - Nine fixtures (lighting, material, PBR-material and its four variants,
    PBR showcase, integrated showcase) do load their scene through Runtime's
    own `loadAndInstantiateScene()` (`scene_load.h`) and its `SceneLoadOutcome`,
    and share Runtime's pure extraction functions (`scene_extraction.h`).
  - `WorldSceneLoadedFixture` duplicates even the load. `WorldSceneFixture`
    builds its `World` by hand.
- **Consequence.** Switching Runtime's frame alone would leave every golden
  byte-identical while proving nothing about the new path: "goldens
  unchanged" is necessary but not sufficient here. And changing
  `SceneLoadOutcome` forces the nine fixtures to change with it (Q7).

### Why now

- Spec 0050 delivered the ECS core and deliberately deferred Runtime's
  adoption to this Spec (its ruling Q1).
- Spec 0049 fixed what a scene means.
- [ADR-0035](../adr/0035-authoring-runtime-data-separation-as-a-long-term-principle.md)
  requires the World/ECS work to make the authoring/runtime representation
  choice explicitly rather than default into one. ADR-0102 records that
  choice: separate representations, joined by a bake.
- What remains is the separation itself: authored scenes are validated and
  baked, and the frame reads only the baked world. Future Prefab, Editor and
  Gameplay work has to sit on one side of that line, never on both.

## Goals

These are maintainer-fixed and are not to be relaxed in review:

- **The principle and the pipeline:**

  ```
        .scene
          │ Schema Validation
          ▼
        Authoring World
          │ Bake
          ▼
        Runtime World
          │
          ▼
        ECS / Chunk / SoA
  ```

- **The bake's product is an ECS world** (archetypes, chunks, SoA), not
  `world::World`'s slot map.
- **Runtime switches to the bake product as its scene storage within this
  Spec, and its extraction becomes ECS queries.** This is Spec 0050 Q1's
  deferred migration.
- **Rendered output is byte-identical.** No existing golden is re-captured.
  If implementation finds this impossible, it stops and reports. It never
  re-captures quietly.
- **The bake builds on existing parts:**
  - Spec 0049's semantic layer as the input's meaning;
  - Spec 0050's `createEntities`/`EntityGuidMap` for GUID binding;
  - `schema::TypeId` for component identity.
- **The bake lives in Atlantis World** (the `scene_instantiation` precedent).
  Asset System does not know the bake exists.
- **Restraint.** Prefab, Editor, Gameplay components and Agent Patch are named
  as future consumers only. They are not designed or scaffolded.

Goals of this Spec within those boundaries:

- **The stages, concretely** (Q1):
  - *Schema Validation* is cook plus decode, held to the Spec 0049 semantic
    schema by its conformance suite;
  - *Authoring World* is the validated scene with its hierarchy;
  - *Runtime World* is the baked `ecs::World`.
- **A bake output** that carries everything the frame needs, with no reference
  back to the authoring stage (Q5).
- **Order identity by construction.** The baked world's extraction yields the
  same light, draw and material sequences as today (Q3).

## Non-Goals

These are maintainer-fixed:

- No Prefab, Editor, Gameplay components or Agent Patch. They are named only:
  no types, no reserved fields, no hooks.
- No golden re-capture, and no rendered-output change.

Also out of scope:

- **Gameplay, animation or input-driven motion.** There is no runtime
  transform propagation and no ECS hierarchy relation (Q2 H1). Component and
  entity edits of the Runtime World stay live per R5.
- **No change to:**
  - scene source or artifact formats, the catalog, Asset System;
  - Spec 0049's semantic types or conformance suite;
  - Renderer, RenderGraph, RHI, shaders;
  - `FrameLightingData`'s layout (ADR-0088) or the extraction math functions
    (`extractCameraMatrices`, `extractFrameLightingData`, `extractFogData`,
    `checkConformalTransform`), which are pure and keep their signatures.
- **No bake from `.scene` source text in the Runtime path** (Q1 B1). Runtime
  keeps loading cooked artifacts.
- **No re-bake or hot reload,** no streaming, no multiple scenes per Runtime.
- **No ECS feature beyond Spec 0050's.** No scheduler, change tracking or
  reactive query.

## Requirements

### Functional

- **R1 — Principle.** Runtime's frame reads scene state only from the baked
  `ecs::World` (and the bake output's document-level fields). It never reads
  `world::World`. The authoring stage is gone from Runtime once the bake
  returns.
- **R2 — Bake entry** (Q1). Atlantis World provides a bake from the validated
  scene Runtime already loads (`ValidatedSceneData`). The bake is the only path
  from a validated scene into a Runtime world.
- **R3 — Bake product** (Q5). A `BakedScene` value holds:
  - the `ecs::World`;
  - the `EntityGuidMap` (Spec 0050, one entity per scene node, created in node
    order through `createEntities`);
  - the active camera's `EntityId` (a document-level field, Spec 0049 ruling
    Q4).

  Error semantics are per Q5.
- **R4 — Components** (Q2).
  - Each node's entity carries its World components as Spec 0050 components:
    `Transform` (authored local data), and `Camera`, `Light` or `Renderable`
    as authored.
  - It also carries the bake-resolved world matrix as a described component.
  - The world matrix is computed by the same arithmetic `updateTransforms()`
    uses today, so it is bit-identical.
- **R5 — Hierarchy** (Q2). Parent/child structure is resolved at bake time.
  The baked world carries no hierarchy and the frame runs no transform pass.
  Live component and entity edits of the Runtime World still reach the next
  frame (Spec 0022's surviving contract, Q2).
- **R6 — Order** (Q3). For every scene, the baked world's extraction yields,
  element for element and byte for byte, the same:
  - `DrawItem` sequence (mesh, material, `objectToWorld`);
  - `FrameLightingData` bytes;
  - referenced-material sequence;
  - camera uniform, fog, bloom and camera-position data;

  as today's `world::World` path. The bake's guarantee is constructive, not
  coincidental.
- **R7 — Runtime switch.**
  - `SceneLoadOutcome` carries the bake output instead of `world::World` and
    `SceneEntityMap`.
  - `runFrame()`'s camera, light, material and draw walks become ECS reads and
    queries, ordered per R6.
  - The EntityRef resolver resolves through `EntityGuidMap` and
    `ecs::World::isValid` (ADR-0097 D6 semantics unchanged).
  - Android uses the same code path.
- **R8 — `world::World`** (Q4) remains as decided by Q4. It is no longer on the
  frame path.
- **R9 — Data plane** (Q6). Camera and lighting inputs are re-extracted every
  frame from the baked world (today's per-frame semantics). The aspect ratio
  still comes from the current target per frame.

### Non-functional

- **Behaviour:** every golden byte-identical; no format, asset or shader
  change. Runtime's observable frame output is identical (R6, Q7).
- **Performance:** the frame drops `updateTransforms()` (O(nodes) per frame).
  Extraction becomes queries plus one order-restoring sort per sequence
  (O(n log n), n = renderables or lights). No per-frame allocation is added
  beyond today's vectors.
- **Dependencies:** none added. World already depends on Asset System for
  `ValidatedSceneData`/`EntityGuid` in `scene_instantiation.h` (Q5 places the
  bake there). Asset System gains nothing and knows nothing of the bake.
- **Portability:** Windows and Android, same code.
- **Threading:** single-threaded (ADR-0004).

## Proposed Design

### The pipeline in today's types

| Stage | Type | Produced by |
|---|---|---|
| `.scene` | source text | authors, the importer |
| Schema Validation | cook + decode (`cookScene`, `decodeScene`), held to `sceneSchema()` | Asset System (unchanged) |
| Authoring World | `ValidatedSceneData`, its hierarchy solved by `world::World` (`instantiateScene` + `updateTransforms`) | Atlantis World (unchanged code) |
| **Bake** | `bakeScene(const ValidatedSceneData&) → BakedScene` | **Atlantis World (new)** |
| Runtime World | `ecs::World` + `EntityGuidMap` + active camera | the bake |
| ECS / Chunk / SoA | Spec 0050 storage | `ecs::World` |

### Bake algorithm (sketch)

1. `instantiateScene(scene)` builds the authoring `world::World` and its
   `SceneEntityMap` (existing code), then `updateTransforms()` runs **once**.
   This is the exact arithmetic of today's frame.
2. `createEntities(ecsWorld, guids-in-node-order)` gives one entity per node.
   In a fresh world `EntityId::index()` equals node order (Spec 0050, P3).
3. For each node, in node order, the bake adds:
   - the node's `Transform`;
   - its `Camera`, `Light` or `Renderable`, as present;
   - its world matrix, read from `world::World::getWorldMatrix`.
4. The authoring `world::World` and `SceneEntityMap` are discarded. The
   result is `{ecsWorld, guidMap, activeCamera}`.

### Extraction under queries (Q3, Q6)

- **Lights:** `query<const Light, const WorldMatrix>` collects
  `(EntityId, LightExtractionInput)`, which are stable-sorted by
  `EntityId::index()` and passed to the unchanged `extractFrameLightingData()`.
- **Draws and materials:** `query<const Renderable, const WorldMatrix>`
  collects in the same way and sorts once. Both the material walk and the
  `DrawItem` walk use that one sorted sequence.
- **Camera:** `BakedScene.activeCamera`, then `get<Camera>` and
  `get<WorldMatrix>`, feeding the unchanged `extractCameraMatrices`,
  `extractFogData` and `extractCameraWorldPosition`.

The functions that turn components into GPU bytes are untouched. Only where
their inputs come from changes, and R6 pins those inputs.

## Architectural Impact

Yes. Recorded in
[ADR-0102](../adr/0102-authoring-runtime-world-separation-and-scene-bake.md),
drafted alongside:

- the separation principle;
- the bake's place, entry and product;
- the static-hierarchy decision and the new world-matrix component;
- the order guarantee;
- `world::World`'s role;
- Runtime's switch.

It supersedes, in part:

- [ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)'s
  per-frame steps 1–2, the extraction's data source. Runtime remains the
  composition-root adapter, and the extraction math is unchanged.
- [ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)'s
  statement that Runtime owns and drives one `world::World`.

Under the Q2 recommendation (H1), it also supersedes, in part,
[Spec 0022](0022-dynamic-frame-uniform-updates-foundation.md)'s Goal that
local-`Transform` and parent-hierarchy edits of the live `World` reach the
next frame. Component and entity edits stay live.

It fulfils [ADR-0101](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md)
D1's deferral and keeps ADR-0049, ADR-0097 D5 (no GUID stored in the ECS) and
ADR-0088 unchanged.

The new component adds a type to `worldSchema()`, so Spec 0048's uniqueness
pin and World's schema listing change. These are planned changes, named in
the Plan.

## Alternatives Considered

1. **Keep Runtime on `world::World` and only add a bake** (no switch). The
   maintainer requires the switch. It would also leave the ECS without a
   consumer.
2. **Bake from `.scene` source** (Q1 B1). Runtime would need source text and
   the parser, against the cooked-artifact model (ADR-0035, ADR-0098 closure
   catalogs on Android).
3. **Runtime hierarchy in the ECS now** (Q2 H2). No production path moves an
   entity, and it needs a relation design Spec 0050 excluded. Its one argument
   is Spec 0022's hierarchy clause, which Q2 weighs explicitly.
4. **Precompute `FrameLightingData` and camera data at bake** (Q6 C2). The
   aspect ratio is per frame, and freezing GPU bytes in the bake couples the
   bake to the renderer layout.
5. **Rely on single-archetype iteration for order** (Q3 D1). It is correct
   today only because all renderables happen to share one archetype; any
   future component breaks it silently.

## Testing & Verification Plan

The guarantee is constructive (Q7). GPU-independent tests come first; the
goldens are the end-to-end confirmation.

- **Frame-input equivalence (R6, the core proof).** A GPU-independent Runtime
  test, for every committed whitelist scene and the content-gated Bistro
  scene:
  - computes the frame inputs twice: through today's `world::World` path
    (kept verbatim in the test as the oracle) and through the bake and query
    extraction;
  - compares, byte for byte: the `DrawItem` sequence (mesh `AssetId`,
    material `AssetId`, `objectToWorld` bits), `FrameLightingData` bytes, the
    referenced-material sequence, and the camera uniform (view, projection,
    world position), fog and bloom data.
- **Bake unit tests (R2–R5):**
  - one entity per node, in node order, with the GUID map;
  - components as authored;
  - world matrices bit-equal to `updateTransforms()`;
  - the active camera;
  - multi-level hierarchies, including non-uniform scale.
- **Order robustness (R6):** a test adds an unrelated component to some
  renderables, so they span archetypes, and asserts extraction order is still
  node order.
- **Runtime (R7):** existing Runtime tests pass:
  - `scene_load`, `entity_ref_resolution` and `material_realization_gpu`,
    updated for the new outcome type as named in the Plan;
  - `runtime_smoke_gpu`, whose live-edit cases move to the ECS per Q2;
  - `runtime_ownership`, unchanged.
  - Spec 0022's surviving contract keeps its test: a Light component edit,
    a world-matrix edit, and Light entity creation and removal each reach
    the next frame's payload.
- **Goldens:**
  - the full Debug + Release suites with no re-capture;
  - the nine `loadAndInstantiateScene` fixtures move to the bake output and to
    Runtime's shared collection functions per Q7, so the goldens run
    Runtime's new extraction code. Their goldens stay byte-identical.

  A golden that moves stops the work and is reported.
- **Platforms and gates:**
  - Validation Layers clean on the GPU suites (Runtime's frame path is
    touched);
  - Android `assembleDebug`, plus an emulator run of the whitelist scene;
  - no change under `assets/`, the goldens, `shaders/`, `src/asset_system/` or
    `src/renderer/`.

## Risks & Open Questions

Risks:

- **A golden moves.** That would mean an order or arithmetic difference the
  equivalence test missed. The rule is stop and report (Goals); the
  equivalence test is designed to catch it first, on the CPU.
- **Two worlds briefly coexist at load.** The authoring `world::World` lives
  only inside the bake. Peak memory roughly doubles scene state at load and
  is freed before the first frame.
- **Fixture migration scope.** Nine fixture units change with
  `SceneLoadOutcome` (Q7); the hand-built and self-loading fixtures do not.

Open questions (to be ruled at review):

- **Q1 — Bake entry, and Runtime's load path.**
  - (B1) From Spec 0049's authoring semantic model (`AuthoringScene`).
    Runtime would have to parse `.scene` source, which it does not ship.
  - (B2) From `ValidatedSceneData`, the decoded artifact. Runtime's path
    becomes catalog → decode → `bakeScene` → `BakedScene`. Its meaning is
    Spec 0049's, held by that Spec's corpus-projection conformance.
  - (B3) Both, through a shared bake core: B2 for Runtime, an `AuthoringScene`
    overload for tools.
  - **Recommendation: (B2).** It is the input Runtime actually has, and it
    keeps cooked artifacts as Runtime's only scene input. B3's authoring
    overload waits for its consumer (the Editor), which is out of scope.
- **Q2 — Runtime hierarchy.**
  - **Evidence:**
    - No production code moves an entity after load, and
      `updateTransforms()` recomputes identical matrices every frame for
      three read sites.
    - Spec 0022's approved contract, exercised only by
      `runtime_smoke_gpu_tests` through a test hook, requires live Light,
      local-`Transform` and parent-hierarchy edits to reach the next frame
      (Motivation).
  - (H1) **Resolve at bake.** A static world-matrix component and no
    hierarchy in the baked world.
    - Component edits on the Runtime World stay live through per-frame
      queries (Q6): a `Light` or `Camera` value, a world matrix, entity
      creation and removal.
    - Local-`Transform` and parent edits no longer propagate: the Runtime
      World has no hierarchy to propagate through. This supersedes, in part,
      Spec 0022's Goal for the Runtime World. Hierarchy edits belong to the
      authoring stage and reach the frame by re-baking (a future Editor
      concern).
    - The smoke test's live-edit cases are rewritten against ECS component
      edits.
  - (H2) **ECS `Parent` component plus a per-frame transform pass** over the
    baked world.
    - It keeps Spec 0022's contract whole.
    - It needs a hierarchy relation design that Spec 0050 excluded from its
      core.
    - It keeps a per-frame O(n) pass that no production path needs.
    - Its bit-identical matrices require sharing `world.cpp`'s private TRS
      and multiply code with the ECS pass.
  - **Recommendation: (H1)**, with the partial supersession of Spec 0022
    stated explicitly in ADR-0102. It matches the separation the maintainer
    directed: authored structure is resolved at bake, and the Runtime World
    is flat data. H2 is the right answer the day gameplay moves things, and
    belongs to that Spec. If review rules that Spec 0022's hierarchy clause
    must stay live now, choose H2.
  - Sub-question, the component's shape: a new described World component
    `world::WorldMatrix`, four `std::array<float, 4>` columns (`Vec4Float32`,
    so no Core vocabulary change), added to `worldSchema()` and mapped for
    the ECS.
    - Alternatives: (a) a new `PrimitiveKind::Mat4Float32`, an additive Core
      vocabulary change under ADR-0099; (b) world matrices kept outside the
      ECS, against the principle.
  - Local `Transform` is kept in the baked world as authored data
    (recommended), not dropped.
- **Q3 — Draw and light order.**
  - (D1) Rely on archetype layout.
  - (D2) A described `BakeOrder` index component, with extraction
    stable-sorting by it.
  - (D3) The bake creates entities in node order in a fresh world, so
    `EntityId::index()` is node order, and extraction stable-sorts each
    collected sequence by `index()`.
  - **Recommendation: (D3).** It needs no new component and is constructive
    given Spec 0050's allocation rule and R1 (the baked world is never
    structurally changed after the bake). D2 is the fallback if runtime
    creation or destruction ever arrives. Equivalence and robustness tests pin
    it (Verification).
- **Q4 — `world::World` after it leaves the frame path.**
  - (W1) It remains a supported library type: the authoring-stage world and
    the bake's hierarchy solver. Image-regression fixtures that build scenes by
    hand keep using it.
  - (W2) Mark it deprecated.
  - (W3) Remove it.
  - **Recommendation: (W1).** The bake itself uses it, which gives
    bit-identical matrices by construction. It is also the natural home of
    authored hierarchy for a future Editor. W2/W3 would leave the bake without
    its solver.
- **Q5 — Bake output and error semantics.**
  - Output: `BakedScene { ecs::World world; ecs::EntityGuidMap entities;
    std::optional<ecs::EntityId> activeCamera; }`, declared in
    `scene_instantiation.h`, which is already allowed to name
    `ValidatedSceneData`/`EntityGuid`, so the boundary test does not change.
  - Errors:
    - (E1) Infallible: a `ValidatedSceneData` is fully validated, so the
      bake's internal steps cannot fail on valid input. Impossible states are
      `ATLANTIS_CHECK`s, like `instantiateScene()`.
    - (E2) A `Result` with transactional all-or-nothing semantics.
    - (E3) Partial results.
  - **Recommendation: E1.** There is no reachable error to report, and all or
    nothing holds trivially.
- **Q6 — Lighting and camera data plane.**
  - (C1) Re-extract every frame from the baked world by queries; the
    extraction functions are unchanged.
  - (C2) Precompute `FrameLightingData` and camera data at bake.
  - **Recommendation: (C1).** The aspect ratio is per frame, it keeps the bake
    free of renderer layouts, and it preserves today's per-frame semantics
    (ADR-0088).
- **Q7 — Verification topology, and what the fixtures do.** The goldens do not
  run Runtime's frame, while nine fixtures depend on `SceneLoadOutcome`
  (Motivation).
  - (V1) Goldens only, with the fixtures kept on a legacy
    `loadAndInstantiateScene()` beside Runtime's new bake load. This proves
    nothing about the switch.
  - (V2) A GPU-independent frame-input equivalence test, with today's
    `world::World` walks kept in the test as the oracle (Verification).
  - (V3) The collection walks move into shared, GPU-independent Runtime
    functions over the bake output (`scene_extraction.h`, beside today's pure
    extraction functions). `runFrame()` and the nine fixtures call the same
    functions, and the fixtures take the bake output from the changed load.
    The goldens then render through Runtime's own new extraction code and must
    stay byte-identical.
    - `WorldSceneLoadedFixture` (self-loading) and `WorldSceneFixture`
      (hand-built) stay on `world::World` (W1). Their goldens are unaffected.
  - **Recommendation: V2 + V3.** V2 proves on the CPU that Runtime's GPU
    inputs are identical; V3 makes the goldens exercise exactly that code.
    The shared functions end the fixtures' duplication of these walks, which
    were duplicated only because they lived inside `runFrame()`.

## Out of Scope / Future Work

- **Named consumers, not designed here:**
  - Prefab (bakes prefab instances);
  - Editor (edits the Authoring World and re-bakes; a possible B3 authoring
    overload);
  - Gameplay components (mutate the Runtime World, which raises Q2 H2 and Q3
    D2);
  - Agent Patch (addresses the Authoring World via Spec 0049's property
    address).
- **Further work:** runtime transform hierarchy, re-bake and hot reload,
  multi-scene, streaming.
