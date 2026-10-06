# Plan: Authoring Scene → Runtime World Bake

- **Spec:** [Spec 0051: Authoring Scene → Runtime World Bake](../specs/0051-authoring-scene-runtime-world-bake.md)
  (`Approved`, 2026-10-06, [PR #210](https://github.com/slmao/Atlantis/pull/210);
  rulings Q1–Q7 binding) —
  [ADR-0102](../adr/0102-authoring-runtime-world-separation-and-scene-bake.md)
  (`Accepted`). The bake builds on
  [ADR-0101](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md)'s
  ECS core and keeps
  [ADR-0088](../adr/0088-frame-lighting-data-successor-structure-and-binding-strategy.md)'s
  `FrameLightingData` unchanged. Spec 0022's surviving live-edit contract is
  the one its Correction 2026-10-06 states.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-06 — reviewed this Plan and
  [Spec 0051](../specs/0051-authoring-scene-runtime-world-bake.md) together in
  [PR #211](https://github.com/slmao/Atlantis/pull/211) and explicitly authorized Implementation from Milestone 1.
  J1–J7 were ruled as recommended. J6's two Spec Corrections are recorded in
  Spec 0051's and Spec 0022's headers. See Joint Review decisions below.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0051 R1–R9 as ruled (Q1 B2, Q2 H1, Q3 D3, Q4 W1, Q5 E1,
Q6 C1, Q7 V2 + V3):

- a described `world::WorldMatrix` component;
- `world::bakeScene()` → `world::BakedScene`;
- Runtime's switch to the bake output, with its camera, light, renderable and
  material collection moved into shared functions in `scene_extraction.h`;
- the nine `loadAndInstantiateScene` fixtures moved onto those same functions.

These must not change:

- a single golden byte (no re-capture; stop and report otherwise);
- assets, shaders, Asset System and Renderer;
- `FrameLightingData`'s layout and the pure extraction math.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `754b563` (PR #210 merged).

1. **Runtime's frame** (`runtime_application.cpp`) does, in order:
   - `world_->updateTransforms()` (`:1380`);
   - the active camera: `activeCamera()`, `getWorldMatrix`, `getCamera`
     (`:1382-1404`);
   - lights: `lightEntities()` → `LightExtractionInput` (`:1442-1449`);
   - referenced materials: a first-reference walk of `renderableEntities()`
     (`:1570-1584`);
   - a **second** `renderableEntities()` walk building `DrawItem`s
     (`:1685-1774`). This walk resolves meshes and materials against GPU maps,
     runs the conformal check, and skips an entity on failure.

   The publish is `world_.emplace(...)` plus the map moves (`:1093-1099`). It
   is guarded by nothrow-move `static_assert`s (`:101-125`). The init-time
   bloom check reads the active camera (`:1104-1107`). `world_`,
   `sceneGuid_`, `sceneEntities_` and `activeCameraEntity_` are members
   (`runtime_application.h:271-277`).
2. **The load** (`scene_load.h/.cpp`). `SceneLoadOutcome` holds `world`, three
   maps, `sceneGuid` and `entities`. Step (f) is `instantiateScene(scene)`.
   Nothing between decode and (f) reads the World.
3. **The extraction header** `scene_extraction.h` holds the pure functions
   (`extractCameraMatrices`, `extractFrameLightingData`, `extractFogData`,
   `extractCameraWorldPosition`, `checkConformalTransform`, the resolvers) and
   `LightExtractionInput`. It already names `world::Light`/`world::Camera`.
4. **The EntityRef resolver** `entity_ref_resolution.h`.
   - `LoadedSceneView {sceneGuid, const SceneEntityMap&, const world::World&}`
     returns a `world::EntityId`.
   - No production code calls it. Its tests build the view from
     `instantiateScene`.
5. **The ECS** (`ecs/world.h`).
   - `get`/`has` are `const`, but `query<Ts...>` is **non-const**: it
     maintains the iteration counter.
   - `ecs::World` is move-constructible `noexcept`, and not move-assignable.
   - `createEntities()` returns `Result<EntityGuidMap, EcsError>`.
   - The decoder already rejects nil and duplicate EntityGuids
     (`scene_artifact.cpp:306-321`). The bake's `createEntities` therefore
     cannot fail.
6. **The schema.**
   - `PrimitiveKind::Vec4Float32` exists (size 16, alignment 4). Asset System
     uses it for `float[4]`.
   - `worldSchema()` lists seven types.
   - These pin it:
     - `tests/world/world_schema_tests.cpp` (the listing of 7;
       `primitiveKindOf` maps no 4-float type yet);
     - `schema_uniqueness_tests.cpp` (combined 20);
     - `ecs_component_type_tests.cpp` (`WorldComponentTypes` size 4).
   - `scene_world_alignment_tests.cpp` checks an explicit list of pairs, so a
     World-only type is unaffected. `sceneSchema()`'s fingerprint is Asset
     System's and is unaffected.
7. **The fixtures.**
   - Nine fixture units hold `std::optional<world::World> world` and
     duplicate Runtime's walks: integrated_showcase, lighting, material,
     pbr_anisotropic, pbr_clearcoat, pbr_material, pbr_materials_showcase,
     pbr_normal_map, pbr_sheen. The emissive, transparency, ibl_material and
     fog fixtures are aliases of the nine.
   - **These files also read or edit `fixture.world`:**
     - **13 GPU test files:** bistro_scene, bloom_on, fog_demo,
       ibl_material_demo, integrated_showcase_demo, lighting_demo,
       multi_light_demo, pbr_anisotropic_demo, pbr_clearcoat_demo,
       pbr_materials_showcase, pbr_normal_map_demo, pbr_sheen_demo,
       transparency_demo;
     - **2 support headers:** `support/emissive_differential.h` and
       `support/fog_differential.h`.
   - `world_scene_fixture` (hand-built) and `world_scene_loaded_fixture`
     (self-loading) use only `world::World`. No golden generator reads
     `fixture.world`.
8. **Live-edit coverage of Spec 0022 exists in three places, not one:**
   - `runtime_smoke_gpu_tests.cpp:386-430`: `createEntity`, `setLight`,
     `setLocalTransform`;
   - `lighting_demo_gpu_tests.cpp:314-1078`: `setLight`, `setLocalTransform`,
     `createEntity`, `destroyEntity`, and **`setParent`** at `:886-945`;
   - `multi_light_demo_gpu_tests.cpp:229-330`: `destroyEntity`.

   See J6.
9. **Golden comparison is exact.** `support/pixel_diff.h:27-28` has
   `kChannelTolerance = 0` and `kFailingPixelBudget = 0`, so a passing golden
   test is a byte-identical frame.
10. **Validation Layers.** The GPU suites already construct their Devices with
    `enableValidationLayers = true` and fatal-severity handling
    (`runtime_smoke_gpu_tests.cpp:277`, every fixture's `setUp`).
11. **The whitelist** is Runtime's CLI scene list (`src/runtime/CMakeLists.txt`):
    integrated_showcase_demo (default, and Android's packaged scene),
    ibl_material_demo, pbr_normal_map_demo and pbr_materials_showcase, plus
    Bistro when its content was fetched. `tests/runtime` already receives
    `ATLANTIS_ASSET_CATALOG_PATH`.

## Plan-stage decisions

These are details Spec 0051 leaves to the Plan. None changes a Spec
requirement or an ADR-0102 decision. Items needing a reviewer's call are in
**Joint Review decisions** below.

**P1 — `world::WorldMatrix`** (R4; ruling Q2 sub-question).

New header `src/world/include/atlantis/world/world_matrix.h`:

```cpp
struct WorldMatrix {                 // column-major, matching getWorldMatrix()
  std::array<float, 4> column0{1, 0, 0, 0};
  std::array<float, 4> column1{0, 1, 0, 0};
  std::array<float, 4> column2{0, 0, 1, 0};
  std::array<float, 4> column3{0, 0, 0, 1};
};
[[nodiscard]] WorldMatrix toWorldMatrix(const std::array<float, 16>& columnMajor) noexcept;
[[nodiscard]] std::array<float, 16> toColumnMajor(const WorldMatrix& m) noexcept;
```

- The conversions are `std::memcpy`, so they are bit-exact.
- `static_assert`s cover `sizeof == 64`, no padding, standard layout and
  trivially copyable.
- **Schema.** A `Struct` descriptor `"world::WorldMatrix"` with four
  `Vec4Float32` fields, `column0`–`column3`, **appended** to `worldSchema()`'s
  listing (8 types; the existing order is unchanged). Flags are per J3.
- **ECS.** A `ComponentType<WorldMatrix>` specialization; `WorldComponentTypes`
  gains it (5 types).
- **Planned pin changes,** and only these:

  | Pin | Change |
  |---|---|
  | `schema_uniqueness_tests.cpp` combined count | 20 → 21 |
  | `world_schema_tests.cpp` listing | 7 → 8, `"world::WorldMatrix"` last |
  | `world_schema_tests.cpp` field cases | a new `WorldMatrix` case; `primitiveKindOf` maps `std::array<float, 4>` → `Vec4Float32` |
  | `ecs_component_type_tests.cpp` | `tuple_size` 4 → 5, `static_assert(Component<WorldMatrix>)` |
  | `world_schema.h` comment | the listing gains WorldMatrix |

**P2 — `bakeScene` and `BakedScene`** (R2, R3, R5; rulings Q1, Q5).

Declared in `scene_instantiation.h`; implemented in a new
`src/world/src/scene_bake.cpp`. `scene_instantiation.cpp` is unchanged.

```cpp
struct BakedScene {
  ecs::World world;
  ecs::EntityGuidMap entities;               // node i -> its entity
  std::optional<ecs::EntityId> activeCamera;
};
[[nodiscard]] BakedScene bakeScene(const atlantis::asset_system::ValidatedSceneData& scene);
```

The algorithm (ADR-0102 D2–D3, Spec "Bake algorithm"):

1. `SceneInstance authoring = instantiateScene(scene)`, then
   `authoring.world.updateTransforms()` once.
2. `createEntities(baked.world, guids-in-node-order)`, where the GUIDs are
   `scene.entityGuid(0..n-1)`. The result is `ATLANTIS_CHECK`ed Ok (reading
   item 5).
3. For node `i`, in node order:
   - `w = authoring.entities.find(guid_i)` and `e = baked.entities.find(guid_i)`,
     both `ATLANTIS_CHECK`ed;
   - `add<Transform>(e, getLocalTransform(w))`;
   - `add<WorldMatrix>(e, toWorldMatrix(getWorldMatrix(w)))`;
   - `add<Camera|Light|Renderable>` for each component `w` has. The World
     getters' `Ok`/`ComponentMissing` decides presence, so the baked
     component set is the World's.
4. `activeCamera` is the baked entity of `scene.activeCameraIndex()`'s node.
5. Return. `authoring` is destroyed at scope exit (R1, R3).

Every impossible state is `ATLANTIS_CHECK_MSG` (E1). No `Result`, no new
error enumerator. `BakedScene` is move-constructible `noexcept` and not
move-assignable, since `ecs::World` is not. Holders use
`std::optional::emplace`, as `world_` does today.

**P3 — Shared collection functions** (R6, R9; rulings Q3, Q6, Q7 V3). These
are added to `scene_extraction.h`/`.cpp`; the pure functions are untouched.

```cpp
struct ActiveCameraInput { atlantis::world::Camera camera; Mat4 worldMatrix; };
struct RenderableExtractionInput { atlantis::world::Renderable renderable; Mat4 worldMatrix; };

[[nodiscard]] std::optional<ActiveCameraInput> collectActiveCamera(const atlantis::world::BakedScene&);
[[nodiscard]] std::vector<LightExtractionInput> collectLights(atlantis::world::BakedScene&);
[[nodiscard]] std::vector<RenderableExtractionInput> collectRenderables(atlantis::world::BakedScene&);
[[nodiscard]] std::vector<atlantis::asset_system::AssetId> collectReferencedMaterialIds(
    std::span<const RenderableExtractionInput>);   // first-reference order
```

- **Lights and renderables.** `query<const Light, const WorldMatrix>`
  (respectively `<const Renderable, const WorldMatrix>`) collects
  `(EntityId, input)` pairs. These are `std::stable_sort`ed by
  `EntityId::index()`, and the inputs returned (D3).
- **The two take a mutable scene** because `query` is non-const (J1).
- **The camera** uses `const` `get`s.
- **One renderable sequence per frame.** Runtime and the fixtures collect
  renderables once per frame. They derive both the referenced-material list
  and the `DrawItem` walk from that one sequence (Spec "Extraction under
  queries").
- **What stays at each call site:** `DrawItem` resolution against GPU maps,
  the conformal check and the per-entity skip. Their code is unchanged; only
  the source of the sequence changes.
- The header's "no World instance required" comment is narrowed to the pure
  functions.

**P4 — Runtime ownership** (R1, R7, R9).

- **Members.**
  - `std::optional<world::World> world_` and `SceneEntityMap sceneEntities_`
    become `std::optional<world::BakedScene> scene_`.
  - `activeCameraEntity_` becomes `std::optional<ecs::EntityId>`, still for
    logging only.
  - `sceneGuid_` stays.
- **The publish** is `scene_.emplace(std::move(outcome.scene))`. The World and
  `SceneEntityMap` nothrow `static_assert`s are replaced by one:
  `std::is_nothrow_move_constructible_v<world::BakedScene>`.
- **The init bloom check** reads `collectActiveCamera(*scene_)`.
- **`runFrame()`.**
  - The `updateTransforms()` call is removed.
  - Camera, lights, referenced materials and renderables come from P3. The
    material walk and the draw walk share one `collectRenderables()` result.
  - Every downstream line is unchanged: buffer writes, offsets, shadow
    matrices, fog, bloom, realization, resolution and skips.
  - A missing active camera still logs and `markFailed()`.

**P5 — The load function** (R7; ruling Q1; J2).

- `loadAndInstantiateScene()` is renamed `loadAndBakeScene()`.
  `SceneLoadOutcome` becomes
  `{BakedScene scene; meshResourceMap; materialDataMap; textureDataMap; sceneGuid;}`.
  `entities` lives in `scene.entities`.
- Steps (a)–(e) are unchanged. Step (f) is `bakeScene(scene)`.
- **Transition (M4 → M5).** Steps (a)–(e) move into a file-local helper.
  `loadAndInstantiateScene()` stays, unchanged in behaviour and marked
  transitional, for the nine fixtures only, until M5 migrates them and
  deletes it. The final tree has only `loadAndBakeScene()`.

**P6 — The EntityRef resolver** (R7).

- `LoadedSceneView` becomes `{AssetGuid sceneGuid; const world::BakedScene& scene;}`.
- `resolveEntityRef` returns `Result<ecs::EntityId, EntityRefError>`. It finds
  through `scene.entities` and checks liveness with `scene.world.isValid`.
- The error set and ADR-0097 D6 semantics are unchanged.

**P7 — V2 frame-input equivalence test** (R6; ruling Q7; J4). A new file,
`tests/runtime/frame_input_equivalence_tests.cpp`, in the GPU-independent
`atlantis_runtime_tests`.

- **The oracle.** A test-local `legacy` namespace holds today's walks
  verbatim, taken from `runtime_application.cpp` at `754b563` over
  `world::World`:
  - `instantiateScene` plus `updateTransforms`;
  - active camera;
  - `lightEntities()`;
  - first-reference materials;
  - `renderableEntities()`.

  It is kept in the test after the switch and never in `src/`.
- **Scenes.**
  - Every `Scene` record of the assembled build catalog
    (`ATLANTIS_ASSET_CATALOG_PATH`), decoded with no Device.
  - A check that the four whitelist GUIDs are among them.
  - **Bistro.** A separate case, compiled only when the content build step
    defined its import GUID. The define reaches `tests/runtime/CMakeLists.txt`
    under `if(DEFINED ATLANTIS_bistro_scene_TARGET)`, as `atlantis_runtime`
    does. The case `SKIP`s otherwise, or when the record is absent.
- **Compared byte for byte** (`std::memcmp` or `std::bit_cast`):
  - the renderable sequence: `meshAsset`, `materialAsset`, and the 64 matrix
    bytes;
  - `extractFrameLightingData()` output (2096 bytes), plus the raw
    `LightExtractionInput` sequence;
  - the referenced-material sequence;
  - `extractCameraMatrices()` at aspects 1 and 16/9;
  - `extractCameraWorldPosition()` and `extractFogData()`;
  - the `Camera` component field by field, which covers bloom strength and
    threshold and `exposureCompensationEv`;
  - the camera world matrix.

**P8 — Image-regression edit helper** (M5). A new
`tests/image_regression/support/baked_scene_edits.h`, test-only:

- `lightEntities(BakedScene&)` and `renderableEntities(BakedScene&)`, returning
  `EntityId`s sorted by `index()`;
- `findLightByKind`;
- **`solveWorldMatrix(const Transform& local, optional parent world)`.** It
  builds a scratch `world::World`, sets the Transform, and runs
  `updateTransforms()`, so an edit's matrix is bit-identical to what the
  authoring stage would compute (W1).

Tests then `set<WorldMatrix>`/`set<Light>`/`set<Camera>`, or `createEntity` +
`add<…>` / `destroyEntity`, on `fixture.scene->world`.

**P9 — Live-edit test mapping** (Spec 0022 surviving contract; J5).

| Today | After |
|---|---|
| smoke: `createEntity` + `setLight` + `setLocalTransform` | `createEntity` + `add<Light>` + `add<WorldMatrix>` (+ `add<Transform>`) |
| smoke: `setLocalTransform` on that light | `set<WorldMatrix>` |
| *(new)* smoke | `destroyEntity` of the added light → `pointLightCount` back to 0 |
| lighting: `setLight` cases (color, intensity, two-calls-one-frame) | `set<Light>` |
| lighting: `setLocalTransform` cases (reposition, rotate Directional, move Point far, non-conformal renderable) | `set<WorldMatrix>(solveWorldMatrix(...))` |
| lighting: create a new Point light / destroy the scene's Point light | ECS `createEntity` + `add` / `destroyEntity` |
| lighting `:886` reparent under a new parent, move the parent | **removed** (the superseded clause; J5) |
| multi_light: destroy slots 5–8 | ECS `destroyEntity` on `lightEntities(scene)[4..7]` |
| bistro, bloom_on, fog_differential: `setCamera`/`setLight` | `set<Camera>`/`set<Light>` |

Each case keeps its assertions: the before/after pixel relation and the
CPU-visible `FrameLightingData` fields.

**P10 — Android emulator method** (M6). The Plan 0047 M8 pattern:

1. `assembleDebug`, `installDebug` on the configured emulator.
2. Launch. The packaged scene is the whitelist default,
   integrated_showcase_demo.
3. Run ≥ 10 s, then `adb exec-out screencap` and `adb logcat -d`.
4. **Gate:** the scene renders (visual match to the Windows run, no golden on
   Android), and logcat has no load or extraction error and no `markFailed`.

The emulator Validation-Layer gap is inherited from Plan 0034/0047 and
recorded, not closed.

**P11 — Windows whitelist run** (M6). `atlantis_runtime --scene <name>` for
each of the four whitelist entries, plus Bistro when present.

- Each runs for ≥ 5 s with Validation Layers enabled (the executable's
  default).
- One screenshot per scene and the log are recorded in the PR.
- The automated equivalent is `runtime_smoke_gpu_tests` (two scenes, fatal
  VVL), which runs at every gate.

## Milestones / Task Breakdown

Six milestones. Commit prefixes: `feat:` for code, `refactor:` for M5's
fixture migration, `test:` for test-only steps, `docs:` for docs.

Every gate runs:

- a Debug + Release build;
- the full `ctest` in both configurations: unit, GPU and image regression,
  i.e. **every golden, exact**;
- the path guard (Verification);
- Android `assembleDebug`, since every milestone touches library sources.

**Stop rule (Spec Goals):** if any golden comparison fails at any gate, the
work stops and is reported with the failure artifacts. Nothing is
re-captured, and no generator is run.

### M1 — `world::WorldMatrix` (R4)

1. `world_matrix.h` (P1); its descriptor in `world_schema.cpp`; its
   specialization and `WorldComponentTypes` entry in `ecs/world_components.h`.
2. The planned pin changes in P1's table, and only those.
3. A `world_matrix` conversion round trip (bit-exact) in
   `world_schema_tests.cpp`'s new case, or a small `world_matrix_tests.cpp`
   (the Plan prefers the former: no new test file).

*Gate:* standard.

### M2 — `bakeScene` (R2, R3, R4, R5)

1. `BakedScene` and `bakeScene` in `scene_instantiation.h`;
   `src/scene_bake.cpp`; `src/world/CMakeLists.txt` gains one source (P2).
2. A new `tests/world/scene_bake_tests.cpp`, built from real cook + decode
   (the `scene_instantiation_tests.cpp` precedent, helper duplicated):
   - one entity per node, `index() == i`, `entities.find(guid_i)` is entity
     `i`;
   - components equal those of an independently instantiated `world::World`;
   - **`WorldMatrix` bit-equal (`memcmp`) to `getWorldMatrix` after
     `updateTransforms()`** on that independent World;
   - a ≥ 3-level hierarchy with rotation and **non-uniform scale** at more
     than one level, including a child whose parent comes later in node order
     if the grammar allows it;
   - the active camera maps to `activeCameraIndex()`'s node; a scene without
     one gives `nullopt`;
   - no hierarchy in the product: each entity's set is exactly `{Transform,
     WorldMatrix}` plus its authored component, checked with `has<>`.

*Gate:* standard.

### M3 — Shared collection functions and the V2 proof, before the switch (R6, V2, V3)

1. P3's functions in `scene_extraction.h`/`.cpp`. Runtime does not call them
   yet: this milestone is additive.
2. `tests/runtime/frame_input_equivalence_tests.cpp` (P7). It passes against
   the still-live `world::World` frame code, which is the strongest moment for
   the proof.
3. `tests/runtime/scene_collection_tests.cpp`:
   - **Order robustness (R6).** A test-local component (`OrderProbe`, its own
     `ComponentType` specialization) is added to every other renderable and
     light, so they span archetypes.
     - It asserts that the raw `query` order **differs** from node order, so
       the test is not vacuous.
     - It asserts that `collectRenderables`/`collectLights` still equal the
       node order and the oracle.
   - `collectActiveCamera` on a camera-less scene gives `nullopt`.
   - **Surviving contract at CPU level.** `set<Light>`, `set<WorldMatrix>`,
     and entity creation and destruction are each reflected by the next
     `collect…` call. A new entity sorts after the scene's.
4. `tests/runtime/CMakeLists.txt`: two sources, plus the conditional Bistro
   define (P7).

*Gate:* standard.

### M4 — Runtime switch (R1, R7, R9)

1. `scene_load.h/.cpp`: P5, with `loadAndBakeScene` plus the transitional
   `loadAndInstantiateScene`.
2. `entity_ref_resolution.h/.cpp`: P6.
3. `runtime_application.h/.cpp`: P4.
4. Runtime tests:
   - `scene_load_tests.cpp`: the success-path cases move to `loadAndBakeScene`
     and `outcome.scene` (`:363-412`, V19). The failure-path cases rename
     their target function; their expected errors are unchanged.
   - `entity_ref_resolution_tests.cpp`: built from `bakeScene`; DeadEntity
     uses `ecs::World::destroyEntity`.
   - `material_realization_gpu_tests.cpp`: the `loadAndInstantiateScene`
     cases (`:816-909`) move to `loadAndBakeScene`; `:906` counts through
     `collectRenderables`.
   - `runtime_smoke_gpu_tests.cpp`:
     - `RuntimeSmokeTestAccess` returns `world::BakedScene&` and counts
       renderables through `collectRenderables`;
     - the live-edit block follows P9 (create + add, set `WorldMatrix`, plus
       the new destroy step).
5. **R1 check:**
   `grep -rn "world::World\|updateTransforms\|SceneEntityMap" src/runtime`
   matches only the transitional `loadAndInstantiateScene` (until M5) and
   comments, which the diff review checks.

*Gate:* standard. The Runtime GPU suites are now on the new path under fatal
VVL.

### M5 — Fixture migration (V3)

1. **The nine fixture units** (`.h` + `.cpp`):
   - `std::optional<world::World> world` becomes
     `std::optional<world::BakedScene> scene`;
   - setUp calls `loadAndBakeScene`;
   - render uses `collectActiveCamera`/`collectLights`/
     `collectRenderables`/`collectReferencedMaterialIds`.

   Their DrawItem resolution, skip logic and every GPU call are unchanged.
2. **The 13 consumer GPU test files and the 2 support headers** (reading item
   7) follow P8/P9. Add `support/baked_scene_edits.h` (P8).
3. Delete the transitional `loadAndInstantiateScene` and its file-local
   wrapper. The final tree has no `world::World` in `src/runtime/` (R1).

*Gate:* standard. **Every golden passes exactly, now through Runtime's own
collection code.** A failure stops the work (stop rule).

### M6 — Acceptance and docs

1. **Docs** (J7):
   - `AGENTS.md:201`: "Runtime owns the one real `World` instance" becomes
     "Runtime owns the loaded scene's bake output (`world::BakedScene`, Spec
     0051)". The rest of the sentence is unchanged.
   - `docs/architecture/module_boundaries.md`:
     - the ECS "Not yet done" bullet (`:641`): Runtime now reads a baked ECS
       world;
     - the Runtime paragraph's per-frame description (`:735-745`):
       `bakeScene` at load, and collection per frame with no
       `updateTransforms()`.
2. P11's Windows whitelist run and P10's emulator run, recorded in the PR.
3. The final path guard and diff review against this Plan.

*Gate:* standard, plus the emulator run.

## Files / Modules Touched (expected)

**New:**

- `src/world/include/atlantis/world/world_matrix.h`;
- `src/world/src/scene_bake.cpp`;
- `tests/world/scene_bake_tests.cpp`;
- `tests/runtime/frame_input_equivalence_tests.cpp`;
- `tests/runtime/scene_collection_tests.cpp`;
- `tests/image_regression/support/baked_scene_edits.h`.

**Changed:**

- **World:**
  - `world_schema.h` (comment), `world_schema.cpp`,
    `ecs/world_components.h`, `scene_instantiation.h`, `CMakeLists.txt`;
  - tests: `world_schema_tests.cpp`, `schema_uniqueness_tests.cpp`,
    `ecs_component_type_tests.cpp`, `CMakeLists.txt`.
- **Runtime:**
  - `scene_load.h/.cpp`, `scene_extraction.h/.cpp`,
    `entity_ref_resolution.h/.cpp`, `runtime_application.h/.cpp`;
  - tests: `scene_load_tests.cpp`, `entity_ref_resolution_tests.cpp`,
    `material_realization_gpu_tests.cpp`, `runtime_smoke_gpu_tests.cpp`,
    `CMakeLists.txt`.
- **Image regression:**
  - the nine fixture units' `.h`/`.cpp` (18 files);
  - the 13 GPU test files and 2 support headers of reading item 7.
- **Docs:** `AGENTS.md` (one sentence), `docs/architecture/module_boundaries.md`
  (two passages).

**Not touched:**

- `world.h`, `world.cpp`, `scene_instantiation.cpp`, the ECS core under
  `ecs/` (except `world_components.h`), and the other World tests;
- the hand-built and self-loading fixtures, the golden generators,
  `tests/image_regression/CMakeLists.txt`;
- `scene_extraction_tests.cpp` and `runtime_ownership_tests.cpp`;
- Core, Asset System, Renderer, RenderGraph, RHI, Vulkan Backend, Platform,
  Shader System, Tools;
- `assets/`, `shaders/`, `tests/image_regression/goldens/`.

No dependency is added.

## Sequencing & Dependencies

- M1 → M2: the bake adds `WorldMatrix`.
- M2 → M3: collection reads `BakedScene`.
- M3 → M4: the switch calls the collection functions, and V2 is green
  **before** Runtime changes.
- M4 → M5: the fixtures need `loadAndBakeScene`. The transitional function
  keeps M4 compiling without them.
- M6 comes last.

## Verification Checklist

Maps to Spec 0051's Testing & Verification Plan.

- [ ] **R1 (M4, M5):** no `world::World`, `SceneEntityMap` or
  `updateTransforms` in `src/runtime/` code at the final gate (grep plus diff
  review).
- [ ] **R2, R3, R5 (M2):** bake unit tests; product shape; `nullopt` camera;
  no hierarchy in the product.
- [ ] **R4 (M1, M2):** the WorldMatrix descriptor and mapping; matrices
  bit-equal to `updateTransforms()`, including the multi-level non-uniform-scale
  hierarchy.
- [ ] **R6 (M3):** V2 byte equivalence over every committed catalog scene,
  including the four whitelist scenes, plus Bistro (gated; `SKIP` recorded
  when absent); the non-vacuous order-robustness test.
- [ ] **R7 (M4):** the Runtime tests as listed; the EntityRef resolver over
  `EntityGuidMap`/`isValid`.
- [ ] **R8:** `world::World`, its tests and the two `world::World` fixtures
  are unchanged and green.
- [ ] **R9 (M4):** per-frame collection; aspect from the current extent
  (diff review).
- [ ] **Spec 0022 surviving contract:** the CPU-level (M3), Runtime smoke (M4)
  and fixture (M5) live-edit cases per P9.
- [ ] **Goldens:** Debug + Release, every gate, exact (reading item 9).
  `git status --porcelain tests/image_regression/goldens` empty and no
  generator run. A golden failure is reported, never re-captured.
- [ ] **Vulkan Validation Layers:** every GPU suite (fatal VVL), every gate;
  P11's Windows run.
- [ ] **Android:** `assembleDebug` at every gate; P10's emulator run at M6;
  the VVL gap recorded.
- [ ] **Path guard, every gate:** `git diff origin/main --name-only` holds
  only the files above. Nothing under `assets/`,
  `tests/image_regression/goldens/`, `shaders/`, `src/asset_system/`,
  `src/renderer/`, `src/render_graph/`, `src/rhi/`, `src/vulkan_backend/`,
  `src/platform/`, `src/shader_system/`, `src/core/` or `src/tools/`.

## Joint Review decisions — ruled (Joint Human Review, slmao, 2026-10-06, PR #211)

All seven were ruled as recommended. None changes a Spec 0051 requirement or
an ADR-0102 decision. J6 corrects Spec text only.

- **J1 — Collection functions take a mutable `BakedScene&`.** **Ruled
  (2026-10-06).**
  - `collectLights`/`collectRenderables` take `world::BakedScene&`, because
    `ecs::World::query` is non-const. `collectActiveCamera` takes `const&`
    (P3; M3).
  - A `const` query overload is not added: it would change Spec 0050's ECS
    API, outside Spec 0051's Non-Goals. It is recorded only, as a candidate
    for a future ECS refinement.
- **J2 — Rename and transition.** **Ruled (2026-10-06).**
  `loadAndInstantiateScene` becomes `loadAndBakeScene`. The old function is
  kept, transitional, from M4 to M5 only, and M5 deletes it (P5; M4, M5).
- **J3 — `WorldMatrix` field flags.** **Ruled (2026-10-06):** `Editable`
  only, not `Serializable`, because no committed format or codec carries it
  (P1; M1).
- **J4 — V2 scope.** **Ruled (2026-10-06):** every committed scene of the
  assembled build catalog, a superset of the four whitelist scenes. Bistro
  stays content-gated (P7; M3).
- **J5 — The reparenting test.** **Ruled (2026-10-06):**
  `lighting_demo_gpu_tests.cpp:886-945` is deleted. It tests the clause Spec
  0022's Correction 2026-10-06 superseded. The "move the Point light far
  away" case keeps covering a world-matrix change (P9; M5).
- **J6 — Spec text corrections.** **Ruled (2026-10-06):** two dated
  Corrections were recorded with this review (the Plan 0048 J1 / Plan 0049 J3
  precedent). No requirement, ruling or ADR decision changes.
  1. **Spec 0051.** Spec 0022's live-edit contract is exercised by
     `runtime_smoke_gpu_tests`, `lighting_demo_gpu_tests` (including the
     `setParent` case J5 deletes) and `multi_light_demo_gpu_tests`.
     - Besides the nine fixtures, 13 GPU test files and 2 support headers
       change with `SceneLoadOutcome` (reading items 7–8).
     - Motivation, Q2's evidence, Testing and Risks are corrected in place.
  2. **Spec 0022.** Its Correction 2026-10-06's "Test coverage" item now names
     all three files: `multi_light_demo` is rewritten as ECS edits, and the
     `lighting_demo` reparent case is deleted.
- **J7 — Where the docs are updated.** **Ruled (2026-10-06).**
  - M6 updates `AGENTS.md:201` and `docs/architecture/module_boundaries.md`'s
    ECS "Not yet done" bullet and Runtime per-frame passage.
  - `project-blueprint.md` and the historical Spec 0019/0022 narrative
    paragraphs are left to the post-merge docs PR (the Plan 0050 J7
    precedent).

## Rollback Plan

- **After merge:** revert the implementation PR as a whole. M1–M3 are
  additive. M4/M5 change only Runtime and test code, with no format, asset or
  golden change, so the revert restores `world::World` extraction exactly.
- **Before merge:** revert milestone by milestone in reverse order. M5 restores
  the transitional load, so reverting it alone leaves a consistent M4 tree.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:

- [ ] Every golden passes exactly in Debug and Release at the final gate. None
      re-captured; the goldens directory is untouched.
- [ ] V2 equivalence is green over every committed scene; the Bistro run or
      its `SKIP` is recorded.
- [ ] The path guard holds at the final gate. The only existing files changed
      are those listed above.
- [ ] Android `assembleDebug` passes at the final gate; the emulator run is
      recorded (screencap and logcat); the VVL gap is noted.
- [ ] The Windows whitelist run is recorded.
- [ ] The post-merge docs items (J7) are queued.
