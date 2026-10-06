# Plan: Runtime World Query / Command / Event Foundation

- **Spec:** [Spec 0052: Runtime World Query / Command / Event Foundation](../specs/0052-runtime-world-query-command-event-foundation.md)
  (`Approved`, 2026-10-06, [PR #214](https://github.com/slmao/Atlantis/pull/214);
  rulings Q1–Q9 binding) —
  [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) (`Accepted`).
  The boundary applies [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md),
  supplies [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md)
  D5's accessor layer, and keeps
  [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md) D5/D6 and
  [ADR-0101](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md) D4.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-06 — reviewed this Plan and
  [Spec 0052](../specs/0052-runtime-world-query-command-event-foundation.md) together in
  [PR #215](https://github.com/slmao/Atlantis/pull/215) and explicitly authorized Implementation from Milestone 1.
  J1–J9 were ruled as recommended. J1–J3's Spec Corrections are recorded in
  Spec 0052's header (Correction 2026-10-06). See Joint Review decisions
  below.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0052 R1–R10 as ruled: `atlantis::world::access::RuntimeWorldAccess`.

- It is a Query / Command / Event boundary over the Runtime-owned
  `BakedScene`, addressed by `(EntityGuid, TypeId, FieldId)`.
- Its parts are:
  - an API-layer GUID index seeded through a new read-only
    `EntityGuidMap::entries()`;
  - a descriptor-driven accessor for all 24 World leaf fields;
  - a drained event queue;
  - ordered per-command application with crash guards.
- `RuntimeApplication` adopts it, applying pending commands at the start of
  `runFrame()`.
- The smoke test's live edits move onto it.

Every golden stays byte-identical; nothing is re-captured.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `4916036` (PR #214 merged).

1. **Schema.** `worldSchema()` describes 24 leaf fields:

   | Component | Leaf fields | Kinds |
   |---|---|---|
   | `Transform` | 3 | `Vec3Float32` |
   | `Camera` | 4 | `Float32` |
   | `CameraFog` (reached through `Camera.fog`) | 5 | `Vec3Float32` + `Float32` |
   | `CameraBloom` (reached through `Camera.bloom`) | 2 | `Float32` |
   | `Light` | 4 | `kind` is `Enum` `world::LightKind`; `Vec3Float32`; `Float32` |
   | `Renderable` | 2 | `meshAsset` `UInt64` `AssetReference`; `materialAsset` `UInt64` `AssetReference` + `Optional`, a `std::optional<AssetId>` |
   | `WorldMatrix` | 4 | `Vec4Float32` |

   - Every World field is flagged `Editable` (Plan 0048 J3, Plan 0051 J3).
   - `LightKind` is a plain `enum class` (`int`).
   - The Spec 0049 ruling J8 rule addresses a nested leaf under its component.
2. **ECS.**
   - `ecs::World::get`/`has` are `const`; `add`/`remove`/`set`/`destroyEntity`
     are direct. `query` is non-const.
   - `EntityGuidMap` holds a sorted `std::vector<std::pair<EntityGuid,
     EntityId>> entries_` and exposes only `find()`/`size()`
     (`ecs/entity_guid_map.h:28-35`).
   - `CommandBuffer::set<T>` copies a whole component at record time
     (`command_buffer.h`).
3. **Runtime.**
   - `runFrame()` begins `ATLANTIS_CHECK_MSG(shouldContinue())` and then
     processes platform events (`runtime_application.cpp:1138-1141`).
   - The scene is `std::optional<world::BakedScene> scene_`, published in
     `initializeSteps()`.
   - The smoke test edits through
     `RuntimeSmokeTestAccess::scene(app)` → `BakedScene&`
     (`runtime_smoke_gpu_tests.cpp:386-440`): create a Point light, add
     `Transform`/`WorldMatrix`, move it, destroy it.
4. **Light limits.**
   - The scene schema states `MaxDirectionalLights` = 1 and `MaxPointLights`
     = `asset_system::kMaxPointLightsPerScene` (64)
     (`scene_semantic_schema.cpp:91-92`; `scene_types.h:83`).
   - `tests/runtime/scene_extraction_tests.cpp:1063` already ties Runtime's
     `kMaxPointLights` to `kMaxPointLightsPerScene`.
   - `FrameLightingData::directionalLights` has extent 1.
5. **The boundary test and its sentence.**
   - `tests/world/module_boundary_tests.cpp:70-98` allows `EntityGuid` only
     in `scene_instantiation.h` and `ecs/entity_guid_map.h`.
   - `AGENTS.md:185-186` and `docs/architecture/module_boundaries.md:340`
     state the same.
6. **Crash paths a client could reach** (these drive J1–J3):
   - **Lights.** `extractFrameLightingData()` aborts on a second Directional
     light or a 65th Point light. It only sees entities with `Light` **and**
     `WorldMatrix` (`collectLights()`). `Light{}` defaults to `Directional`.
   - **Materials.** `realizePendingMaterials()` aborts
     (`ATLANTIS_CHECK_MSG`, `material_realization.cpp:680`) on a referenced
     material id the scene load did not put in `materialDataMap_`.
   - **Active camera.** `collectActiveCamera()` aborts if the active camera
     entity lacks `Camera` or `WorldMatrix`. If the entity is destroyed,
     `runFrame()` calls `markFailed()` and the Runtime ends.

## Plan-stage decisions

These are details Spec 0052 leaves to the Plan. None changes a Spec
requirement or an ADR-0103 decision, except as J1–J3 propose.

**P1 — Files.** All under the World module.

| File | Contents |
|---|---|
| `src/world/include/atlantis/world/access/access_error.h` | `AccessError` and `toString`; names no GUID |
| `src/world/include/atlantis/world/access/runtime_world_access.h` | `PropertyAddress`, `PropertyValue`, the five command and five event types, `CommandFailure`, `RuntimeWorldAccess`, the light-limit constants. **The one new World header naming `EntityGuid`** |
| `src/world/src/access/property_access.h` / `.cpp` | private: field resolution, the generic copy, the enum and optional accessor tables, the `static_assert`s (P3) |
| `src/world/src/access/runtime_world_access.cpp` | the boundary object |

**P2 — Public value types** (R2, R3; rulings Q2, Q7).

```cpp
struct PropertyAddress { EntityGuid entity; schema::TypeId component; schema::FieldId field; };  // world-schema ids
struct EnumValue { std::int64_t value; };
struct Absent {};
using PropertyValue = std::variant<std::uint64_t, float, std::array<float, 3>, std::array<float, 4>,
                                   AssetGuid, EntityGuid, EnumValue, Absent>;   // ==, by value
```

- The variant's first six alternatives map one-to-one onto the six
  `PrimitiveKind`s.
- No address text form in v1; a wire format is 0054's (J7).

**P3 — The accessor** (R7; ruling Q2). This is private.

- **Resolution.** `resolve(TypeId component, FieldId field)` walks the
  component's descriptor. For a `Struct`-kind field it recurses into the
  referenced descriptor. It returns `{kind, primitive, flags, enum TypeId,
  summed byteOffset}` for the unique leaf, or `UnknownField`.
- **Generic copy, 23 fields at their `byteOffset`** (ruling Q2): the 22
  primitive leaves and the enum. Every non-`Enum`, non-`Optional` leaf is
  copied with `std::memcpy` at the summed offset, `primitiveSize(kind)`
  bytes, into or out of a component value obtained by `get<T>` /
  written by `set<T>`.
- **Enum, 1 of the 23.** Copied at its offset as `std::int32_t`. The convention
  is held by `static_assert(std::is_same_v<std::underlying_type_t<LightKind>,
  std::int32_t>)` in `property_access.cpp`.
  - That file is compiled by both toolchains, MSVC and the Android NDK's
    Clang via `assembleDebug`, so both enforce the assertion.
  - An enum table lists every described enum `TypeId` it covers.
- **`Optional`, the 24th field.** A typed accessor table: one entry,
  `Renderable.materialAsset`, read and written through the
  `std::optional<AssetId>` member itself.
- **Dispatch.** `TypeId` → C++ type is a compile-time walk over
  `ecs::WorldComponentTypes`. No type-erased ECS entry point is added.
- **Coverage tests** (M1):
  - every `Enum`-kind field in `worldSchema()` is in the enum table, and
    every `Optional`-flagged field is in the optional table;
  - each table entry's offset equals its descriptor's.

  A new enum or optional field therefore fails a test until it is handled.

**P4 — `RuntimeWorldAccess`** (R3–R6, R9; rulings Q1, Q3, Q4).

```cpp
class RuntimeWorldAccess {
 public:
  explicit RuntimeWorldAccess(BakedScene& scene);      // borrows; seeds the index from scene.entities.entries()
  // Query -- by value
  [[nodiscard]] bool findEntity(const EntityGuid&) const;
  [[nodiscard]] Result<std::vector<schema::TypeId>, AccessError> listComponents(const EntityGuid&) const;
  [[nodiscard]] Result<PropertyValue, AccessError> getProperty(const PropertyAddress&) const;
  // Command -- recorded now, applied by the owner
  CommandTicket submit(Command);                        // Command = variant of the five
  // Owner
  ApplyReport applyPending();                           // in submission order (Q9)
  // Event and command outcome -- drained by value
  [[nodiscard]] std::vector<Event> drainEvents();       // Event = variant of the five
  [[nodiscard]] std::vector<CommandFailure> drainFailures();
};
```

- **The index** is a `std::map<EntityGuid, ecs::EntityId>`, O(log n).
  - It is seeded from `entries()`.
  - `CreateEntity` inserts, after rejecting nil and duplicate GUIDs.
  - `DestroyEntity` erases.
  - `findEntity` also checks `isValid`.
- **Same-batch addressing (ruling Q1 a1).** Commands are validated and
  executed one by one in submission order. A `CreateEntity(g)` therefore
  makes `g` addressable by every later command in the same batch.
- **Lowering (J5).** Each command is validated against the current world and
  then executed with direct `ecs::World` operations (`createEntity`,
  `add<T>`, `remove<T>`, `get<T>`/`set<T>`, `destroyEntity`). `CommandBuffer`
  is not used: a field write must read the component's state at apply time,
  not at record time.
- **Owner-side misuse.** `applyPending()` called while the ECS is iterating
  is a programmer error (`ATLANTIS_CHECK_MSG`). It cannot happen at Runtime's
  application point (P9).
- **Ownership.** Non-copyable and movable. It holds `BakedScene&`, and its
  lifetime must nest inside the scene's.

**P5 — Commands, validation and errors** (R4, R8; rulings Q6, Q8, Q9).

```cpp
enum class AccessError {
  UnknownEntity, NilGuid, DuplicateGuid, UnknownComponentType, ComponentMissing,
  ComponentAlreadyPresent, UnknownField, FieldNotEditable, KindMismatch,
  EnumValueOutOfRange, NonFiniteValue, LightLimitExceeded, ActiveCameraProtected /* J3 */,
};
```

| Command | Checks (in order) | ECS lowering | Event |
|---|---|---|---|
| `CreateEntity{g}` | non-nil; not in the index | `createEntity()`; index insert | `EntityCreated{g}` |
| `DestroyEntity{g}` | known; not the active camera (J3) | `destroyEntity`; index erase | `EntityDestroyed{g}` |
| `AddComponent{g, T}` | known; T is a World component; absent; light limits (J1) | `add<T>(T{})` | `ComponentAdded{g, T}` |
| `RemoveComponent{g, T}` | known; T known; present; not `Camera`/`WorldMatrix` of the active camera (J3) | `remove<T>` | `ComponentRemoved{g, T}` |
| `SetProperty{addr, v}` | the entity is known and has the component; the field resolves; `Editable`; v's alternative matches the field's kind (`Absent` only for `Optional`); enum in the declared constants; every float finite; light limits on `Light.kind` (J1) | `get<T>`, field write (P3), `set<T>` | `PropertyChanged{addr, v}` |

- **Per-command effect** (ruling Q9). A failed command has no effect. Every
  check runs before any ECS call.
- **`Transform` writes** (ruling Q8) are accepted, emit `PropertyChanged` and
  do not affect rendering. The class comment states this, and an M4 test
  pins it.
- **Asset ids** are not validated by World. They are a Runtime concern
  (J2).

**P6 — Events and failures** (R5; ruling Q4).

- **Events.** `Event` is a `std::variant` of the five structs. Each is
  appended when its command succeeds, in application order.
  `drainEvents()` moves the queue out.
- **Failures.** A failed command appends
  `CommandFailure{ticket, AccessError}` instead, where `CommandTicket` is the
  submission sequence number. `applyPending()` also returns the batch's
  failures to the owner (J4).
- **Non-reactive.** Nothing hooks the ECS: only `applyPending()` writes to
  either queue. A test pins this.

**P7 — `EntityGuidMap::entries()`** (ruling Q1). This is the one ECS-module
increment.

```cpp
[[nodiscard]] std::span<const std::pair<atlantis::asset_system::EntityGuid, EntityId>> entries() const noexcept;
```

It is sorted by GUID and read-only. The map stays an immutable snapshot, and
the ECS stores no GUID (ADR-0101 D4).

**P8 — Light limits and their `static_assert` chain** (ruling Q6).

- **The constants.** `runtime_world_access.h` defines
  `kMaxDirectionalLights = 1` and
  `kMaxPointLights = atlantis::asset_system::kMaxPointLightsPerScene`, from
  `scene_types.h`. World already depends narrowly on Asset System.
- **The chain:**
  - `tests/world/world_access_validation_tests.cpp` CHECKs both against the
    scene schema's `MaxDirectionalLights`/`MaxPointLights` constraint limits
    (`sceneSchema()` is a runtime accessor);
  - `tests/runtime/runtime_world_access_tests.cpp` `static_assert`s
    `world::access::kMaxPointLights == runtime::kMaxPointLights` and
    `std::size(FrameLightingData{}.directionalLights) ==
    world::access::kMaxDirectionalLights`.

  The guard thus provably matches what extraction can hold.
- **Counting** (J1). An entity counts toward a kind when it has `Light` and
  `WorldMatrix`, exactly the set `collectLights()` hands to extraction. The
  check runs on `AddComponent(Light)` and `AddComponent(WorldMatrix)` for an
  entity with a `Light`, and on `SetProperty(Light.kind)`.

**P9 — Runtime adoption** (R9; ruling Q5).

- **The member.** `RuntimeApplication` gains
  `std::optional<world::access::RuntimeWorldAccess> worldAccess_`, declared
  after `scene_` so that it is destroyed first. It is emplaced right after
  `scene_.emplace(...)` in `initializeSteps()`.
- **The application point.** `runFrame()`: the first statement after the
  `shouldContinue()` check is `worldAccess_->applyPending()`, before platform
  events, acquisition and collection (J6).
  - Failures are retained for the client's `drainFailures()`.
  - With nothing pending the call does nothing, so the frame is unchanged.
- **Spec 0051's collection code is unchanged.**
- **J2 changes `runFrame()`.** Under J2's recommendation, a referenced
  material id absent from `materialDataMap_` is excluded from
  `pendingMaterialIds` and its entity is skipped for that frame. That is
  Spec 0018 D4 case 3, the existing "present but unresolvable → skip"
  semantics.
- **The smoke hook.** `RuntimeSmokeTestAccess::scene()` (mutable
  `BakedScene&`) is replaced by `worldAccess(app)` →
  `RuntimeWorldAccess&`. The read-only `renderableEntityCount()` stays an
  owner-internal friend read.

**P10 — Smoke-test migration** (Spec 0022's surviving contract).

| Today (`BakedScene&`) | After (`RuntimeWorldAccess&`) |
|---|---|
| `createEntity()` + `add(Light)` + `add(Transform)` + `add(WorldMatrix)` | submit `CreateEntity{g}`, `AddComponent{g, Light}`, `SetProperty` (kind = Point, color, intensity, range), `AddComponent{g, Transform}`, `AddComponent{g, WorldMatrix}`, `SetProperty{WorldMatrix.column3 = (1,1,1,1)}` → `runFrame()` |
| `set(WorldMatrix)` | submit `SetProperty{WorldMatrix.column3 = (-2,3,0.5,1)}` → `runFrame()` |
| `destroyEntity()` | submit `DestroyEntity{g}` → `runFrame()` |

After each `runFrame()` the test checks:

- the same `FrameLightingData` bytes as today;
- `drainEvents()` in order: 1 + 1 + 4 + 1 + 1 + 1 events for the first batch, then 1, then 1;
- `drainFailures()` empty.

The scene's Directional light plus one Point stays within the limits, and the
J1 counting rule lets `AddComponent{g, Light}` (default Directional) precede
the `kind` change.

## Milestones / Task Breakdown

Five milestones. Commit prefixes: `feat:` for code, `test:` for test-only
steps, `docs:` for docs.

Every gate runs:

- a Debug + Release build;
- the full `ctest` in both configurations, with every golden compared exactly;
- the golden-directory guard;
- the path guard (Verification);
- Android `assembleDebug`.

**Stop rule:** a moved golden stops the work and is reported, never
re-captured.

### M1 — Values, accessor, `entries()` (R7; rulings Q1, Q2)

1. `access_error.h`, the value types in `runtime_world_access.h` (P2), and
   `property_access.h/.cpp` (P3). `EntityGuidMap::entries()` (P7). CMake
   sources.
2. `tests/world/world_access_property_tests.cpp`:
   - **The 24-field matrix.** For each leaf, read the baked default, write a
     distinct non-default value, read it back. The other fields of the
     component are unchanged, compared bytewise.
   - Nested leaves through `Camera`.
   - **The enum.** `EnumValue{0}`/`{1}` round-trip as `LightKind`.
   - **The optional.** `Absent` ↔ `std::nullopt`, and a value ↔ `AssetId`.
   - **Coverage tests** (P3).
   - Unknown or unreachable field → `UnknownField`.
3. `tests/world/ecs_entity_guid_tests.cpp`, a planned addition: `entries()`
   is sorted, complete, and equal to `find()`.

*Gate:* standard.

### M2 — Queries and the index; the planned allowlist change (R2, R3, R6)

1. `RuntimeWorldAccess` construction (index seeding), `findEntity`,
   `listComponents`, `getProperty` (P4).
2. `tests/world/world_access_query_tests.cpp`, over baked real cook+decode
   scenes (helper duplicated, the precedent):
   - every node GUID is found and an unknown one is not;
   - `listComponents` equals each entity's actual set, sorted;
   - `getProperty` equals `get<T>` for every field of every entity.
3. **Planned changes to existing files:**
   - the `module_boundary_tests.cpp` allowlist and its "not vacuous" check
     gain `access/runtime_world_access.h`;
   - the same one-entry wording in `AGENTS.md:185-186` and
     `module_boundaries.md:340` (J8).

*Gate:* standard.

### M3 — Commands, events, validation (R4, R5, R8)

1. `submit`, `applyPending`, `drainEvents`, `drainFailures`, validation,
   guards (P4–P6, P8).
2. `tests/world/world_access_command_tests.cpp`:
   - each command alone;
   - **same-batch addressing:** `CreateEntity(g)` → `AddComponent(g, …)` →
     `SetProperty(g, …)` in one batch;
   - two `SetProperty`s on one component in one batch, where both survive;
   - commands after a `DestroyEntity(g)` are `UnknownEntity`;
   - reusing a destroyed GUID;
   - **events:** exact sequence, type and payload, one per success, none
     per failure. A failure mid-batch leaves the later commands applied
     (ruling Q9), with failure tickets;
   - **non-reactive:** direct `ecs::World` edits produce no event.
3. `tests/world/world_access_validation_tests.cpp`:
   - nil or duplicate GUID, unknown type, missing or present component,
     kind mismatch, `Absent` on a non-optional field, enum out of range,
     NaN or ±∞ in each float kind;
   - **guard negatives:**
     - a second Directional via `SetProperty(Light.kind)` and via
       `AddComponent(WorldMatrix)` on a Directional light;
     - a 65th Point via `AddComponent(WorldMatrix)` and via
       `SetProperty(kind)`;

     each rejected with `LightLimitExceeded` and no effect;
   - **J1's enabling case:** `AddComponent(Light)` on an entity without a
     `WorldMatrix` is accepted while a Directional light exists;
   - **J3:** `DestroyEntity`/`RemoveComponent(Camera|WorldMatrix)` on the
     active camera are refused;
   - P8's limit CHECKs against the scene schema.

*Gate:* standard.

### M4 — Runtime adoption (R9, R10; ruling Q5; J2)

1. `runtime_application.h/.cpp` (P9), including J2's pending-material filter.
2. `runtime_smoke_gpu_tests.cpp` (P10). The `scene()` hook is replaced by
   `worldAccess()`.
3. `tests/runtime/runtime_world_access_tests.cpp`, GPU-independent, over
   baked catalog scenes and the shared `collect*()` functions:
   - **Q8 contract.** `SetProperty(Transform.localPosition)` is applied and
     emits `PropertyChanged`. `collectRenderables`/`collectLights` outputs
     are byte-identical before and after.
   - **Cross-frame visibility.** After `applyPending()`, a `Light` value
     edit, a `WorldMatrix` edit, and Light create/destroy show up in the
     next collection.
   - P8's `static_assert` chain.
   - **J2.** A `Renderable.materialAsset` set to an id the scene never
     loaded leaves `computePendingMaterialIds`'s input free of it under the
     new filter. This is CPU-level, with the filter factored as a
     Runtime-private helper.

*Gate:* standard. GPU suites run under fatal VVL; every golden is exact.

### M5 — Acceptance

1. Final path guard and diff review.
2. Windows run of the four whitelist scenes, and an Android emulator run of
   the default scene (J9).

*Gate:* standard, plus the runs.

## Files / Modules Touched (expected)

**New:**

- `src/world/include/atlantis/world/access/access_error.h`;
- `src/world/include/atlantis/world/access/runtime_world_access.h`;
- `src/world/src/access/property_access.h`;
- `src/world/src/access/property_access.cpp`;
- `src/world/src/access/runtime_world_access.cpp`;
- `tests/world/world_access_property_tests.cpp`;
- `tests/world/world_access_query_tests.cpp`;
- `tests/world/world_access_command_tests.cpp`;
- `tests/world/world_access_validation_tests.cpp`;
- `tests/runtime/runtime_world_access_tests.cpp`.

**Changed (planned, not deviations):**

- `src/world/include/atlantis/world/ecs/entity_guid_map.h`: `entries()`;
- `tests/world/ecs_entity_guid_tests.cpp`: one case;
- `src/world/CMakeLists.txt`, `tests/world/CMakeLists.txt`,
  `tests/runtime/CMakeLists.txt`;
- `tests/world/module_boundary_tests.cpp`: the allowlist;
- `AGENTS.md` (one sentence) and `docs/architecture/module_boundaries.md`
  (`:340`);
- `src/runtime/include/atlantis/runtime/runtime_application.h` and
  `src/runtime/src/runtime_application.cpp`: P9, plus J2's filter;
- `tests/runtime/runtime_smoke_gpu_tests.cpp`: P10.

**Not touched:**

- the ECS core other than `entries()`;
- `world.h`/`world.cpp`, `scene_instantiation.*`, `scene_bake.cpp`,
  `world_schema.*`, every component header;
- `scene_extraction.*` and `scene_load.*`;
- Asset System, Renderer, RHI, shaders, assets, goldens.

No dependency is added.

## Sequencing & Dependencies

- M1 → M2: queries read through the accessor.
- M2 → M3: commands need the index.
- M3 → M4: Runtime applies commands.
- M5 comes last.

## Verification Checklist

Maps to Spec 0052's Testing & Verification Plan.

- [ ] **R1/R2 (M2):** the public headers name no `ecs::`, `EntityId` or
  component C++ type in a signature (a scan test); the GUID allowlist holds.
- [ ] **R3 (M2):** find, list and get over every baked entity and field.
- [ ] **R4/R9 rulings (M3):** the five commands, same-batch addressing,
  per-command effect, failure tickets.
- [ ] **R5 (M3):** event order, one per success, none per failure,
  non-reactive.
- [ ] **R6 (M1–M3):** `entries()`, index seeding, nil/duplicate rejection,
  GUID reuse.
- [ ] **R7 (M1):** the 24-field matrix, the enum convention (both toolchains
  compile the `static_assert`), the optional accessor, coverage tests.
- [ ] **R8 (M3, M4):** type, finiteness and enum range; light guards
  (negatives plus J1's enabling case); J3 protection; Q8 contract;
  J2 filter.
- [ ] **R9/R10 (M4):** apply at the start of `runFrame()`; the smoke test's
  live edits through the boundary under fatal VVL; every golden exact in
  Debug and Release.
- [ ] **Path guard, every gate:** only the files above. Nothing under
  `assets/`, `tests/image_regression/`, `shaders/`, `src/asset_system/`,
  `src/renderer/` or the other GPU modules.
- [ ] **Android:** `assembleDebug` at every gate (it also compiles P3's
  `static_assert`); the emulator run at M5 (J9).

## Joint Review decisions — ruled (Joint Human Review, slmao, 2026-10-06, PR #215)

All nine were ruled as recommended. J1–J3 correct Spec 0052, recorded as one
dated Correction in its header with in-place markers. No ADR-0103 decision
text is changed.

- **J1 — Light-guard counting.** **Ruled (2026-10-06):** the light limits
  count only entities holding both `Light` and `WorldMatrix`, the set
  extraction actually sees. They are checked on `AddComponent(Light)`,
  `AddComponent(WorldMatrix)` and `SetProperty(Light.kind)` (P5, P8; M3).
  `AddComponent(Light)` with its default `Directional` is therefore accepted
  on an entity that has no `WorldMatrix` yet (P10).
- **J2 — An unloaded material id.** **Ruled (2026-10-06):** Runtime's frame
  skips an entity whose material id the scene load did not load, following
  the existing semantics of Spec 0018 D4 case 3, instead of aborting (P9;
  M4).
  - The id is excluded from `pendingMaterialIds`, and the entity is skipped
    for that frame.
  - Spec 0052's "0051 frame code unchanged" wording is corrected.
- **J3 — Active-camera protection.** **Ruled (2026-10-06):**
  `RemoveComponent(Camera|WorldMatrix)` on the active camera entity and
  `DestroyEntity` of it are refused with `ActiveCameraProtected` (P5; M3).
- **J4 — The failure channel.** **Ruled (2026-10-06):** `submit()` returns a
  `CommandTicket`, the submission sequence number. Failures are recorded as
  `{ticket, AccessError}` and taken by `drainFailures()`; `applyPending()`
  also returns them to the owner (P4, P6; M3).
- **J5 — Lowering.** **Ruled (2026-10-06):** each command is validated and
  then executed with direct `ecs::World` operations; `ecs::CommandBuffer` is
  not used (P4).
  - **Every intent of the maintainer's "ECS/CommandBuffer" boundary is
    kept:**
    - commands are deferred: recorded on `submit()`, applied later;
    - each is validated;
    - all apply together on the frame thread at one point
      (`applyPending()`);
    - no caller outside the boundary holds a pointer or reference.
  - Only that specific type is not used: it copies whole components at
    record time, and the GUID index must change in lockstep with each
    command.
- **J6 — The application point.** **Ruled (2026-10-06):** `applyPending()` is
  the first statement of `runFrame()` after the `shouldContinue()` check
  (P9; M4).
- **J7 — Address text form.** **Ruled (2026-10-06):** none in v1; it is left
  to 0054 (P2).
- **J8 — Docs in the implementation.** **Ruled (2026-10-06):** M2 changes
  only the allowlist sentences (`AGENTS.md:185-186`,
  `module_boundaries.md:340`). The `module_boundaries.md` and blueprint
  status narratives go to the post-merge docs PR.
- **J9 — Acceptance runs.** **Ruled (2026-10-06):** M5 runs the four Windows
  whitelist scenes and the Android emulator (default scene: install,
  screencap, logcat; the Plan 0047 M8 pattern).

## Rollback Plan

- **After merge:** revert the implementation PR as a whole. M1–M3 are
  additive. M4's Runtime change is the apply step, J2's filter and the smoke
  test. No format, asset or golden changes.
- **Before merge:** revert milestone by milestone in reverse order.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:

- [ ] Every golden exact in Debug and Release at the final gate. None
      re-captured.
- [ ] The path guard holds; the existing files changed are only those listed.
- [ ] Both toolchains compile P3's `static_assert` (`assembleDebug` at every
      gate).
- [ ] The post-merge docs items (J8) are queued.
