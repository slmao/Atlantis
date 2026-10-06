# Plan: Runtime ECS Foundation

- **Spec:** [Spec 0050: Runtime ECS Foundation](../specs/0050-runtime-ecs-foundation.md)
  (`Approved`, 2026-10-06, [PR #206](https://github.com/slmao/Atlantis/pull/206);
  rulings Q1–Q8 binding) —
  [ADR-0101](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md)
  (`Accepted`). The ECS core adopts
  [ADR-0049](../adr/0049-entity-identity-and-handle-invalidation.md)'s handle
  rules and keeps [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md)
  D5.
- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending. Implementation is not authorized until a
  reviewer has read this Plan and Spec 0050 together, ruled J1–J8 below, and
  explicitly authorized it.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0050 R1–R10 exactly, as `atlantis::world::ecs` in the Atlantis
World module (rulings Q1 B, Q2 M1, Q8): an additive, single-threaded
archetype/chunk ECS core with `EntityId`, `EntityGuid` binding,
`ComponentTypeId`, archetypes, chunks, `query` and `CommandBuffer`, proven by
the north-star test on `world::Transform`/`world::Renderable`.

These do not change:

- `world::World` and its tests;
- Runtime;
- every asset, golden and shader.

The only edits to existing files are:

- the World boundary-test allowlist entry ruling Q4 (P-a) names;
- the CMake source lists;
- AGENTS.md's matching sentence.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `eeb0ffb` (PR #206 merged).

1. **`world::World`** (`world.h`, `world.cpp`) is a slot map:
   - `struct Slot` (`world.cpp:24-34`) holds `alive`, `generation`, `parent`,
     `Transform`, optional `Camera`/`Renderable`/`Light`, and a visit state;
   - a LIFO free list;
   - a heap `WorldIdentity` token (`world.cpp:18`) that every `EntityId`
     carries.

   Nothing in it changes.
2. **The handle precedent.** `world::EntityId` (`entity_id.h`) holds an index,
   a 64-bit generation and an identity pointer. Only `World` (a friend) may
   construct one. `kInvalidEntityId` is its sentinel.
3. **The retirement test precedent.** `World` declares
   `friend struct EntityLifecycleTestAccess` and a private
   `forceGenerationForTesting()` (`world.h:105-111`, `world.cpp:380-386`).
   The test drives a slot to the tombstone generation
   (`entity_lifecycle_tests.cpp:103-130`).
4. **The GUID allowlist.** `tests/world/module_boundary_tests.cpp:70-98` fails
   if any World header except `scene_instantiation.h` names `EntityGuid`.
   - AGENTS.md:185-188 states the same rule ("in `scene_instantiation.h`
     alone").
   - So does `docs/architecture/module_boundaries.md:340` and `:626`.
5. **Assertions.** `ATLANTIS_ASSERT` is Debug-only. A test can install a
   recording handler through `atlantis::assertions::setFailureHandler()`
   (`assert.h`; precedent `tests/core/assert_tests.cpp:58-69`).
6. **Schema.** `worldSchema()` lists `world::Transform`, `Camera`, `Light`,
   `Renderable` (and `CameraFog`, `CameraBloom`, `LightKind`), with TypeIds
   from `schema::typeId("world::…")` (Plan 0048). The World test target
   already builds with `/w14062`.

## Plan-stage decisions

These are details Spec 0050 leaves to the Plan. None changes a Spec
requirement or an ADR-0101 decision. The four the Spec names explicitly are
P4 (error set), P6 (snapshot map), P5 (chunk budget) and P7 (deferred-create
addressing).

**P1 — Files.** All under the World module. Public headers are in
`src/world/include/atlantis/world/ecs/`; private sources are in
`src/world/src/ecs/`.

| Public header | Contents |
|---|---|
| `entity_id.h` | `ecs::EntityId` (P3) |
| `ecs_error.h` | `EcsError` and `toString` (P4) |
| `component.h` | `ComponentType<T>` (primary template, undefined), the `Component` concept, `componentTypeId<T>()`, `ComponentInfo` (P2) |
| `world.h` | `ecs::World`: lifecycle, the templated component operations, `query` |
| `command_buffer.h` | `CommandBuffer`, `PendingEntity`, `ApplyReport` (P7) |
| `entity_guid_map.h` | `EntityGuidMap` and `createEntities()`. **The only ECS header naming `EntityGuid`** (ruling Q4 P-a) |
| `world_components.h` | `ComponentType<>` specializations for the four World components, plus `WorldComponentTypes` (P2) |

Private sources:

- `archetype.h`: `Archetype`, `Chunk`, the column layout;
- `world.cpp`;
- `command_buffer.cpp`;
- `entity_guid_map.cpp`.

The generic headers (`entity_id.h` to `command_buffer.h`) name no World
component type (ADR-0101 D2).

**P2 — Component vocabulary** (R3, rulings Q3, Q7).

```cpp
template <typename T> struct ComponentType;  // undefined: unmapped types do not compile
template <typename T>
concept Component = requires { { ComponentType<T>::kName } -> std::convertible_to<std::string_view>; }
                    && std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>
                    && std::is_standard_layout_v<T>;
template <Component T> constexpr schema::TypeId componentTypeId() { return schema::typeId(ComponentType<T>::kName); }
struct ComponentInfo { schema::TypeId id; std::uint32_t size; std::uint32_t alignment; };
```

- Every templated `ecs::World` and `CommandBuffer` operation is constrained on
  `Component`.
- `world_components.h` specializes `ComponentType` for `world::Transform`,
  `Camera`, `Light` and `Renderable`, with `kName` equal to each type's
  schema name (`"world::Transform"`, …). It also defines
  `using WorldComponentTypes = std::tuple<Transform, Camera, Light, Renderable>;`
  for the table check (J4).
- `CameraFog`, `CameraBloom` and `LightKind` are not components (they are
  fields of components) and get no specialization.

**P3 — `ecs::EntityId` and the entity table** (R1, ruling Q4 (a)).

- The handle has ADR-0049's layout: a `uint32` index, a `uint64` generation,
  and an opaque `const EcsWorldIdentity*`.
  - It has `index()`/`generation()`, `==`, and the sentinel
    `ecs::kInvalidEntityId`.
  - Only `ecs::World` (a friend) constructs a non-default handle.
- The entity table is a vector of `{generation, alive, archetype, chunk,
  row}` with a LIFO free list.
- At `destroyEntity()`, the generation is incremented. On reaching
  `std::numeric_limits<std::uint64_t>::max()` the index is retired
  permanently (ADR-0049 verbatim).
- `ecs::World` is non-copyable and move-constructible, with the identity token
  heap-allocated so handles survive a move (ADR-0049 Amendment).
- A private `forceGenerationForTesting()` with `friend struct EcsTestAccess`
  follows the precedent in reading item 3.

**P4 — Error set** (R1, R2, R6–R8).

```cpp
enum class EcsError {
  InvalidEntity,                // stale, retired, foreign-instance, or the sentinel
  ComponentMissing,             // get/set/remove of a component the entity lacks
  ComponentAlreadyPresent,      // add of a component the entity has
  StructuralChangeDuringQuery,  // a structural operation while a query runs (J1)
  NilEntityGuid,                // createEntities(): a nil GUID
  DuplicateEntityGuid,          // createEntities(): a GUID twice in one batch
};
```

- Results use `atlantis::Result<T, EcsError>`.
- Results with no value use `Result<std::monostate, EcsError>`, as World does.

**P5 — Archetypes and chunks** (R4, R5, ruling Q7).

- **Archetype identity.** An archetype is identified by its component
  `TypeId`s sorted by value. It is found through an ordered map from that
  vector to the archetype's index. Archetypes are kept in creation order, and
  the empty archetype is created first.
- **Chunk budget: 16 KiB (16384 bytes).** Each chunk is one allocation
  aligned to the archetype's maximum column alignment (at least
  `alignof(EntityId)`).
- **Column layout.** The `EntityId` column first, then one column per
  component in `TypeId` order. Each column starts at its alignment.
- **Capacity.** The largest `n ≥ 1` whose laid-out columns fit in 16384
  bytes. A row larger than the budget gets capacity 1, in a chunk sized to
  fit.
- **Packing.**
  - Rows in an archetype are dense across its chunks: every chunk but the
    last is full.
  - Removal moves the archetype's last row into the hole and fixes that
    entity's table entry.
  - A last chunk that becomes empty is freed.
- **Moves.** An `add`/`remove` copies the entity's shared columns to the
  target archetype by byte copy (components are trivially copyable), writes or
  drops the one changed column, then removes the source row.

**P6 — EntityGuid binding** (R2, ruling Q4 (G1) (P-a)).

- `entity_guid_map.h` declares the snapshot map and a free function, so that
  `world.h` never names a GUID:

  ```cpp
  class EntityGuidMap {
    std::optional<EntityId> find(const EntityGuid&) const;
    std::size_t size() const;
  };
  Result<EntityGuidMap, EcsError> createEntities(World&, std::span<const EntityGuid>);
  ```

- **All or nothing.** A nil or repeated GUID fails with `NilEntityGuid` or
  `DuplicateEntityGuid` before any entity is created.
- **On success** each GUID gets a fresh entity (no components) in span order.
- **Snapshot semantics.** The map is a caller-owned, immutable snapshot.
  Liveness stays the world's: a later destroy makes `find` return a handle
  that `isValid` rejects.

**P7 — `CommandBuffer`** (R8).

- **Recording.** `create()` returns a `PendingEntity`, a buffer-local ordinal
  tagged with its buffer.
- **Targets.** `destroy`, `add<T>(target[, value])`, `remove<T>(target)` and
  `set<T>(target, value)` take either an `EntityId` or a `PendingEntity`.
- **Storage.** Values are stored as bytes plus `ComponentInfo`, which is
  possible because components are trivially copyable.
- **`apply(World&)`:**
  - applies commands in recording order;
  - **continues past a failed command** (J3);
  - returns `ApplyReport { std::vector<EntityId> created; std::vector<CommandFailure> failures; }`.
    `created[i]` is pending entity *i*. A failure records the command index
    and its `EcsError`;
  - clears the buffer.
- **Misuse.**
  - A `PendingEntity` from another buffer is a programmer error
    (`ATLANTIS_ASSERT`) and is reported as `InvalidEntity`.
  - `apply` while the world is iterating fails as
    `StructuralChangeDuringQuery`, with no command applied (J1).

**P8 — Query** (R7, ruling Q6).

- `template <Component... Ts, typename Fn> void query(Fn&& fn)` invokes
  `fn(EntityId, Ts&...)` per matching row. A `const`-qualified `Ts`
  (`query<const Transform>`) passes `const T&`.
- **Matching** is an all-of subset test on the sorted `TypeId`s.
- **Order** is archetypes in creation order, then chunks in order, then rows
  in order.
- **Implementation.** A non-template core walks matching chunks and hands a
  `detail::ChunkView` (row count, entity column, column pointers) to the
  template, so there is no per-row type erasure.
- **Iteration counter.**
  - Nested queries are allowed (read-only; the counter nests).
  - While the counter is non-zero, every structural operation fails
    `ATLANTIS_ASSERT_MSG` and returns `StructuralChangeDuringQuery` (J1). The
    structural operations are `createEntity`, `destroyEntity`, `add`,
    `remove`, `createEntities` and `CommandBuffer::apply`.
  - `get`, `set` and `has` remain allowed: they do not move rows.

**P9 — Thread safety and docs.** Each public header states "not thread-safe"
(ADR-0004). There is no global state.

## Milestones / Task Breakdown

Six milestones. Commit prefixes: `feat:` for code, `test:` for test-only
steps, `docs:` for docs.

Every gate runs:

- a Debug + Release build;
- the full `ctest` in both configurations;
- the path guard (Verification).

Android `assembleDebug` runs at every gate that adds library sources (M2, M4,
M5) and at M6.

### M1 — Component vocabulary (R3)

1. `component.h`, `ecs_error.h` and `world_components.h` (P2, P4).
2. `tests/world/ecs_component_type_tests.cpp`. For every type in
   `WorldComponentTypes`:
   - `componentTypeId<T>()` equals `schema::typeId(ComponentType<T>::kName)`;
   - the `TypeId` names a `Struct` descriptor in `worldSchema()`;
   - `ComponentInfo` size and alignment equal `sizeof`/`alignof`.

   `static_assert`s that each satisfies `Component`, and that `CameraFog` has
   no mapping.
3. **Development demonstration** (not committed): a non-trivially-copyable
   type with a `ComponentType` specialization fails `Component`. The
   compiler diagnostic is recorded in the PR.

*Gate:* standard. These are headers only, so no Android run is needed.

### M2 — Entity lifecycle and storage (R1, R4, R5, R6, R9)

1. `entity_id.h`, `world.h`, `src/ecs/archetype.h` and `src/ecs/world.cpp`
   (P3, P5). Add `world.cpp` to `src/world/CMakeLists.txt`.
2. `tests/world/ecs_entity_tests.cpp`:
   - create, destroy and reuse; the generation bump;
   - retirement at the tombstone via `EcsTestAccess`;
   - a foreign-instance handle is `InvalidEntity`;
   - the sentinel is invalid;
   - handles survive a world move.
3. `tests/world/ecs_storage_tests.cpp`:
   - add-order independence (`{A,B}` and `{B,A}` give one archetype, observed
     through `EcsTestAccess`);
   - values preserved across add/remove moves;
   - swap-remove keeping every other entity's data;
   - overflow into a second chunk (capacity + 1 entities) and back, with the
     empty chunk freed;
   - every P4 error of `add`, `get`, `set`, `remove` and `has`.

*Gate:* standard, plus `assembleDebug`.

### M3 — Query (R7)

1. `query` in `world.h`, and the chunk walk in `world.cpp` (P8).
2. `tests/world/ecs_query_tests.cpp`:
   - all-of matching (subset archetypes included, others excluded);
   - **deterministic order**: an exact `EntityId` sequence asserted across
     three archetypes created in a known order and a two-chunk archetype;
   - mutation through `T&` observed by `get`;
   - `const T&` access;
   - a nested read-only query;
   - **structural change during a query.** A recording failure handler is
     installed and `add` is called inside the callback. In Debug the
     assertion is recorded once (`#if !defined(NDEBUG)`, precedent in reading
     item 5). In both configurations the call returns
     `StructuralChangeDuringQuery` and the world is unchanged.

*Gate:* standard.

### M4 — CommandBuffer (R8)

1. `command_buffer.h` and `src/ecs/command_buffer.cpp` (P7).
2. `tests/world/ecs_command_buffer_tests.cpp`:
   - every command kind applied in order;
   - a pending entity addressed by later commands, its `EntityId` from
     `ApplyReport::created`;
   - a stale target reported, while later commands still apply (J3);
   - the buffer is empty after `apply`;
   - `apply` during a query is refused;
   - a foreign `PendingEntity` is reported.

*Gate:* standard, plus `assembleDebug`.

### M5 — EntityGuid binding (R2) and the planned allowlist change (ruling Q4 P-a)

1. `entity_guid_map.h` and `src/ecs/entity_guid_map.cpp` (P6).
2. **Planned change to an existing test.**
   - `tests/world/module_boundary_tests.cpp`'s EntityGuid scan accepts
     `entity_guid_map.h` as a second allowed header.
   - Its "not vacuous" check now also requires that header to name the GUID.
   - The test name and comment are updated to say "scene_instantiation.h and
     ecs/entity_guid_map.h".
3. **AGENTS.md:185-188.** "in `scene_instantiation.h` alone" becomes "in
   `scene_instantiation.h` and `ecs/entity_guid_map.h` alone (the creation-time
   `EntityGuidMap`, Spec 0050)". The rest of the sentence is unchanged.
4. `tests/world/ecs_entity_guid_tests.cpp`:
   - the snapshot maps each GUID to a live entity in span order;
   - nil and duplicate GUIDs are rejected, with nothing created;
   - a later destroy leaves the map's handle invalid by `isValid`;
   - `createEntities` during a query is refused.

*Gate:* standard, plus `assembleDebug`.

### M6 — North star and acceptance (R10)

1. `tests/world/ecs_north_star_tests.cpp`, the Spec's north-star shape on
   `world::Transform` and `world::Renderable`:
   - `createEntity`;
   - `add<Transform>`;
   - `add<Renderable>(e, {meshKey, materialKey})`;
   - `query<Transform, Renderable>` mutating `localPosition`;
   - the mutation observed by `get<Transform>`.

   It also covers a `Light` entity excluded from the query, an archetype move
   by `remove<Renderable>`, and a `CommandBuffer` round.
2. Docs (J7): `docs/architecture/module_boundaries.md:340` and `:626` get the
   same one-entry wording as AGENTS.md.
3. The final path guard and diff review against this Plan.

*Gate:* standard, plus `assembleDebug`.

## Files / Modules Touched (expected)

**New:**

- `src/world/include/atlantis/world/ecs/`: `entity_id.h`, `ecs_error.h`,
  `component.h`, `world.h`, `command_buffer.h`, `entity_guid_map.h`,
  `world_components.h`;
- `src/world/src/ecs/`: `archetype.h`, `world.cpp`, `command_buffer.cpp`,
  `entity_guid_map.cpp`;
- `tests/world/`: `ecs_component_type_tests.cpp`, `ecs_entity_tests.cpp`,
  `ecs_storage_tests.cpp`, `ecs_query_tests.cpp`,
  `ecs_command_buffer_tests.cpp`, `ecs_entity_guid_tests.cpp`,
  `ecs_north_star_tests.cpp`.

**Changed:**

- `src/world/CMakeLists.txt`: three sources;
- `tests/world/CMakeLists.txt`: seven sources;
- `tests/world/module_boundary_tests.cpp`: the allowlist (M5);
- `AGENTS.md`: one sentence (M5);
- `docs/architecture/module_boundaries.md`: two sentences (M6, J7).

**Not touched:**

- `world.h`, `world.cpp`, `entity_id.h`, `scene_instantiation.*`,
  `world_schema.*`, every component header, and every other World test;
- Core, Asset System, Runtime, Tools, Renderer and all GPU modules;
- assets, goldens and shaders.

No dependency is added.

## Sequencing & Dependencies

- M1 → M2: storage needs the vocabulary.
- M2 → M3: query needs storage.
- M3 → M4: the CommandBuffer interacts with the query counter.
- M2 → M5: GUID binding needs lifecycle. M5 may run before M4.
- M6 comes last.

## Verification Checklist

Maps to Spec 0050's Testing & Verification Plan.

- [ ] **R1 (M2):** lifecycle, generation, retirement via `EcsTestAccess`,
  foreign instance, sentinel, move.
- [ ] **R2 (M5):** snapshot contents and order, nil and duplicate rejection
  (all or nothing), liveness after destroy.
- [ ] **R3 (M1):** every `WorldComponentTypes` member's `TypeId` is in
  `worldSchema()` as a `Struct`, with size and alignment equal. The
  `Component` constraint is demonstrated against a non-trivial type during
  development and recorded in the PR.
- [ ] **R4–R6 (M2):** archetype identity, moves, swap-remove, chunk overflow
  and release, every error.
- [ ] **R7 (M3):** matching, the exact deterministic order, mutation,
  `const`, nesting, and the structural-change assertion (Debug) plus refusal
  (both configurations).
- [ ] **R8 (M4):** order, pending addressing, stale reporting, continuation,
  clearing, refusal during a query.
- [ ] **R9:** "not thread-safe" stated in every public ECS header; no global
  state (reviewed in the diff).
- [ ] **R10 (M6):** the north-star test.
- [ ] **Path guard, every gate:** `git diff origin/main --name-only` contains
  only the files above. Nothing under `assets/`,
  `tests/image_regression/goldens/`, `shaders/`, `src/runtime/`, `src/tools/`,
  `src/core/`, `src/asset_system/`, or any existing `src/world` or
  `tests/world` file beyond the listed changes.
- [ ] **Full suites:** Debug + Release at every gate. `assembleDebug` at M2,
  M4, M5 and M6, with the NDK compiling every `src/world/src/ecs/*.cpp`.
- [ ] **GPU gates:** N/A. No GPU path is touched, so there is no new image
  regression or Validation-Layer obligation and no golden captured. The
  existing suites run as part of the full-suite gate.

## Joint Review decisions (to be ruled)

- **J1 — Structural change during a query.**
  - **Recommendation:** `ATLANTIS_ASSERT_MSG` (Debug) as R7 requires, and in
    every build the operation is also refused with
    `StructuralChangeDuringQuery`, leaving the world unchanged. Release
    therefore has defined behaviour instead of corrupting the iteration.
  - Alternative: `ATLANTIS_CHECK` (fatal in every build).
- **J2 — Archetype and Chunk are internal types in v1.** They are not public
  API. Tests observe archetype count, chunk count and capacity through the
  `EcsTestAccess` friend (precedent: `EntityLifecycleTestAccess`).
  - **Recommendation:** accept. The Spec fixes them as storage concepts
    (R4/R5), not as public types, and a public archetype API would invite
    the excluded features.
- **J3 — `CommandBuffer::apply` failure semantics.**
  - **Recommendation:** continue past a failed command and report every
    failure with its command index. The buffer is cleared after `apply`.
  - Alternatives: stop at the first failure; all-or-nothing (needs a
    rollback that v1 does not otherwise need).
- **J4 — How R3's table check finds every `ComponentType<T>`.** C++20 cannot
  enumerate specializations.
  - **Recommendation:** each module that maps components declares its list
    beside the specializations (`WorldComponentTypes`), and the test iterates
    the list. A specialization added outside the list is not caught
    mechanically; review and the 0048 Definition of Done item cover it.
- **J5 — Chunk budget and capacity rule** (P5).
  - **Recommendation:** 16 KiB, the largest fitting `n ≥ 1`, with an oversize
    row getting capacity 1.
- **J6 — `add` and `set` semantics** (P4).
  - **Recommendation:** `add` on a present component is
    `ComponentAlreadyPresent` (no overwrite), and `set` on a missing one is
    `ComponentMissing` (no implicit add).
  - Alternative: an "add-or-set" upsert.
- **J7 — Where the "ECS undecided" lines and the allowlist wording are
  updated.**
  - **Recommendation:** AGENTS.md's sentence goes with the test change in M5
    (ruled by Q4 P-a). The same wording in `module_boundaries.md:340/:626`
    goes in M6.
  - The two "ECS not implemented" lines (`module_boundaries.md:617`,
    `project-blueprint.md:766`) are left to the post-merge docs PR, as the
    maintainer directed when the Spec was drafted. The Definition of Done's
    "docs/architecture updated" item is thereby satisfied in two steps,
    stated in the implementation PR.
- **J8 — Read access during a query.**
  - **Recommendation:** `get`/`set`/`has` stay allowed inside a callback,
    including on other entities, because they never move rows. Only the
    structural operations in P8 are refused.

## Rollback Plan

The change is additive: no existing code path calls the ECS. Revert the
implementation PR as a whole. Before merge, revert milestone by milestone in
reverse order. M5's allowlist and AGENTS.md edits revert with M5.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:

- [ ] The path guard holds at the final gate. The only existing files
      changed are those listed above.
- [ ] The `Component`-constraint demonstration is recorded in the PR.
- [ ] `assembleDebug` passes at the final gate, with every ECS source
      compiled by the NDK.
- [ ] The two "ECS not implemented" lines are queued for the post-merge docs
      PR (J7).
