# Spec: Runtime ECS Foundation

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-06
- **Related Plan(s):** [Plan 0050](../plans/0050-runtime-ecs-foundation.md)
  (`Approved`; Joint Human Review 2026-10-06,
  [PR #207](https://github.com/slmao/Atlantis/pull/207)) — implemented, merged
  [PR #208](https://github.com/slmao/Atlantis/pull/208).
- **Approval:** slmao, 2026-10-06 (review of this Spec's own branch PR,
  [PR #206](https://github.com/slmao/Atlantis/pull/206)) — authorizes drafting Plan 0050; Implementation itself
  still awaits its own, separate Joint Human Review of Spec + Plan together.
  The maintainer fixed this Spec's positioning and boundaries before drafting
  (2026-10-06, chat): the v1 inventory, the excluded list, single-threading,
  the acceptance north star, and additive-only. They are recorded under
  Goals / Non-Goals. The same review ruled all eight open questions, each as
  its recommendation. See Risks & Open Questions below.
- **Related ADR(s):**
  [ADR-0101](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md)
  (`Accepted` 2026-10-06, alongside this Spec's own Approval). It records the ECS core's
  relationship to `world::World`, its module placement, component identity,
  entity identity, the storage model and the access rules.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

This Spec gives Atlantis its first generic, archetype-based entity-component
store: the step at which it becomes a game engine rather than a renderer with
a scene. v1 contains exactly seven concepts, and nothing else:

- `EntityId`;
- `EntityGuid`;
- `ComponentTypeId`;
- `Archetype`;
- `Chunk`;
- `Query`;
- `CommandBuffer`.

Component identity is Spec 0048's `schema::TypeId`, so the schema core becomes
the ECS's identity layer. v1 is single-threaded and purely additive:
`world::World`, the Runtime frame path, scene instantiation and extraction are
untouched. The core is proven by tests using the engine's real component
types. Runtime migration is a later Spec.

## Motivation / Problem Statement

Current state, read at `origin/main` `cc96163`.

### The repository records the ECS as not implemented and not chosen

- `docs/architecture/module_boundaries.md:617` describes World's components as
  "not a generic, type-erased ECS registry".
- `docs/project-blueprint.md:766` lists, under World's "Not implemented", "a
  general/data-driven/multi-threaded ECS".
- Spec 0014 ("Why this stays a minimal World, not a general ECS") deferred a
  generic registry until "a second real component type or a second real
  consumer" existed to validate its shape.
- [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md) deferred
  "entity/handle representation …, ECS storage layout" to a future World/ECS
  Spec.

Per the maintainer's direction, both lines of evidence above are updated by
this Spec's post-merge documentation sync, not before.

### Spec 0014's condition has been met

- **Component kinds.** World now hard-codes four component kinds on each
  slot: a mandatory `Transform` and optional `Camera`, `Renderable` and
  `Light` (`src/world/src/world.cpp:24-34`). Each kind is a hand-written
  quartet of `setX`/`removeX`/`getX`/`xEntities()` methods
  (`world.h`, the `Light` group since Spec 0019).
- **Cost of a new component.** Adding a component today means:
  - a new `Slot` field;
  - four new `World` methods;
  - a new `WorldError` case;
  - new extraction code in Runtime.

  That is the per-feature growth Spec 0048 identified for schemas, repeated
  for runtime state.
- **The identity layer exists.**
  - Spec 0048 gave every engine data type a stable `TypeId`
    (`schema::typeId("world::Camera")`, FNV-1a-64, identical across builds
    and processes).
  - Spec 0049 keyed scene components by `TypeId`, at most one per type per
    node (its ruling Q4), and fixed the property address (node, component
    `TypeId`, `FieldId`).
- **The consumers are queued.** The Tool/Editor Connection Protocol
  (Candidate 2) and the Gameplay SDK (Candidate 3) both need generic
  component access. Built on today's World, they would grow one verb per
  component kind.

### Why archetypes, and why now

The maintainer fixed the shape: archetype and chunk storage, behind a
callback query and a deferred command buffer. Fixing it now, before any
Runtime migration or protocol depends on World's per-type API, means that
API never spreads further.

## Goals

These are maintainer-fixed boundaries and are not to be relaxed in review:

- **v1 contains exactly** `EntityId`, `EntityGuid`, `ComponentTypeId`,
  `Archetype`, `Chunk`, `Query` and `CommandBuffer`, and nothing else.
- **Single-threaded.** The [ADR-0004](../adr/0004-phase1-threading-baseline.md)
  baseline is unchanged. No job system, no locks, no lock-free structures.
- **Acceptance north star.** This API shape must work with the engine's real
  component types. Atlantis has no `MeshRenderer`; the proof uses
  `world::Transform` and `world::Renderable` (and `world::Light`):

  ```cpp
  EntityId e = world.createEntity();
  world.add<Transform>(e);
  world.add<Renderable>(e);          // the north star's MeshRenderer
  world.query<Transform, Renderable>(
      [](EntityId e, Transform& t, Renderable& r) { ... });
  ```

- **Extreme restraint.** The change is additive, and v1 changes no existing
  frame-path behaviour.

Goals of this Spec within those boundaries:

- An archetype-and-chunk store whose component identity is
  `schema::TypeId` (Spec 0048), so that runtime component keys equal schema,
  scene (Spec 0049) and future protocol keys.
- Entity identity that keeps every existing ruling:
  - ADR-0049's handle rules (`EntityId` is never persisted);
  - ADR-0097 D5 (EntityGuid is asset-layer identity; World and components
    store no GUID).
- Deterministic iteration order, so that tests and every later consumer see
  the same order.

## Non-Goals

These are maintainer-fixed. Each is named only, not designed and not
scaffolded; there are no placeholder types, reserved enum values or empty
interfaces:

- System Scheduler;
- Job System Integration;
- Change Tracking;
- Replication;
- Reactive Query;
- Chunk Streaming;
- Prefab;
- Networking;
- C# Binding.

Also out of scope, because "nothing else" is in v1:

- **No parent/child hierarchy.** `Transform` in the ECS is plain data, and
  there is no `updateTransforms()` over the ECS. The hierarchy stays
  World's (ADR-0050).
- **No scene instantiation into the ECS**, and no extraction from it.
  `instantiateScene()` and Runtime's `scene_extraction` are unchanged.
- **No change to `world::World`, its API, its slot map, or its tests.**
- **No Runtime adoption** (ruling Q5): Runtime neither constructs nor
  reads an ECS world in v1.
- **No serialization, persistence or cross-process use** of ECS state or
  handles.
- **No Client/Editor access surface.** ADR-0033's query/command/event
  categories for Clients belong to Candidate 2.
- **No exclusion or optional terms in queries, no query caching, no sorting.**
- **No new dependency.**

## Requirements

### Functional

- **R1 — `EntityId`** (ruling Q4: a new `ecs::EntityId`).
  - An ECS handle with ADR-0049's rules: index plus 64-bit generation,
    permanent slot retirement at the maximum generation, an invalid sentinel,
    and a per-instance identity token (so a handle from another ECS world
    instance is detected).
  - It is never serialized, persisted or used across a process boundary.
  - A stale or foreign handle yields an explicit error result, never
    undefined behaviour (ADR-0049's runtime-state classification).
- **R2 — `EntityGuid`** (ruling Q4: G1, header placement P-a).
  - The ECS stores no GUID (ADR-0097 D5 unchanged).
  - EntityGuids bind to EntityIds only at creation time: a batch creation
    that takes EntityGuids returns a caller-owned, immutable snapshot map
    `EntityGuid → EntityId`, in the manner of `SceneEntityMap`.
  - Nil or duplicate GUIDs in a batch are an explicit error. Liveness stays
    the ECS world's.
- **R3 — `ComponentTypeId`** (rulings Q3: I-a, T-a, D-a; Q7).
  - It is `schema::TypeId`, obtained from a C++ component type through an
    explicit, non-intrusive mapping.
  - A component type must be **described**: its owning module's Spec 0048
    table lists it under the same `TypeId`.
  - It must be trivially copyable, trivially destructible and
    standard-layout.
  - All four existing World component types (`Transform`, `Camera`,
    `Renderable`, `Light`) qualify unchanged.
  - Violations are compile errors where C++ can detect them, and test
    failures where only the schema tables can.
- **R4 — `Archetype`.**
  - The canonical set of `ComponentTypeId`s an entity carries, sorted by
    `TypeId` value. Entities with equal sets share one archetype.
  - At most one component per type per entity, matching Spec 0049 ruling
    Q4's per-node rule. An entity with no components belongs to the empty
    archetype.
- **R5 — `Chunk`.**
  - A fixed-byte-budget block of one archetype's rows. Each component is a
    column (structure of arrays), plus an `EntityId` column.
  - Rows are packed: removal moves the last row into the hole.
  - An archetype owns as many chunks as it needs. The byte budget and the
    capacity rule are the Plan's, within this requirement.
- **R6 — Structural operations.**
  - `createEntity()`, `destroyEntity(e)`, `add<T>(e[, value])`, `remove<T>(e)`,
    `has<T>(e)`, by-value `get<T>(e)` and `set<T>(e, value)`.
  - Adding or removing a component moves the entity to the target archetype
    and preserves every other component's value.
  - Each operation on a stale handle, a missing component, or an
    already-present component (for `add`) returns an explicit error.
  - The Plan names the error set.
- **R7 — `Query`.**
  - `query<Ts...>(fn)` invokes `fn(EntityId, Ts&...)` once per entity whose
    archetype contains every `Ts` (all-of matching). `const T&` is accepted
    for read-only access.
  - Iteration order is deterministic: archetype creation order, then chunk
    order, then row order.
  - References are valid only for the duration of one callback invocation
    (ruling Q6).
  - A structural change to the same world during a query is a programmer
    error and fails an assertion. Deferred changes go through R8.
- **R8 — `CommandBuffer`.**
  - Records create, destroy, add, remove and set, to be applied later by an
    explicit `apply(world)` call, in recording order.
  - A deferred create is addressable within the buffer, and its `EntityId` is
    available after `apply`.
  - A command whose target is stale at apply time yields an explicit,
    reported result, never undefined behaviour.
  - There are no automatic sync points: there is no scheduler.
- **R9 — Ownership and threading.**
  - Each ECS world instance owns its archetypes and chunks (RAII). There is
    no global registry and no singleton; instances are independent.
  - Every public type documents that it is not thread-safe (ADR-0004,
    AGENTS.md threading rules).
- **R10 — Acceptance.** A test in the north-star shape, using
  `world::Transform` and `world::Renderable`:
  - creates entities;
  - adds components;
  - queries and mutates through the callback;
  - observes the mutation through `get<T>`;
  - exercises an archetype move and a `CommandBuffer` round.

### Non-functional

- **Behaviour:** byte-identical. No existing source, test expectation, asset,
  golden, shader or Runtime-visible behaviour changes. The full suites stay
  green.
- **Performance:**
  - Amortized O(1) `createEntity`/`add`/`remove`/`destroy`, apart from a
    one-time archetype creation.
  - Query iteration over contiguous columns.
  - No per-entity heap allocation.
  - No benchmark gate in v1. The Plan records a smoke-scale test, not a
    performance claim.
- **Memory:** O(entities × component bytes) plus chunk slack, bounded by the
  chunk budget per archetype.
- **Portability:** pure C++20 with no compiler-specific type-name tricks.
  Builds on Windows and Android, like World today.
- **Dependencies:** none added. The ECS core depends on Core
  (`schema::TypeId`). The GUID binding (R2) names Asset System's `EntityGuid`,
  as `scene_instantiation.h` already does.

## Proposed Design

### Relationship to `world::World` (ruling Q1: B)

| Option | What it is | Slot map / ADR-0049 | Hierarchy (ADR-0050) | Scene instantiation (ADR-0097 D5) | Runtime extraction (ADR-0051) | Frame-path risk |
|---|---|---|---|---|---|---|
| **A — evolve in place** | `world::World` gains `add<T>`/`query` and archetype storage replaces its slots | Replaced; ADR-0049's "World is a slot map" superseded in part | Must become a component or side structure; `updateTransforms()` rewritten | Rewritten | API kept, implementation changed | High: every frame touches the new storage |
| **B — additive parallel core** (recommended) | A new ECS world type beside `world::World`; nothing existing changes | Untouched; the ECS adopts ADR-0049's rules for its own handle | Untouched (not in ECS v1) | Untouched; ECS binds GUIDs by snapshot (R2) | Untouched | None |
| **C — World over archetype storage** | `world::World`'s public API kept and re-implemented on the ECS core | Internal storage replaced; ADR-0049 superseded in part | Re-implemented over components | Re-implemented | API kept | Medium–high: same API, new internals; traversal-order and determinism tests are at stake |

**Ruled (2026-10-06): B.**
- It is the only option compatible with "additive, no frame-path change".
- It keeps ADR-0048–0051 exactly as accepted.
- It lets Runtime migration, where A or C is decided against real
  usage, be its own Spec.
- Its cost, two entity stores in one codebase for a while, is explicit and
  temporary.

### Placement and names (rulings Q2: M1, Q8)

- **Ruled:** inside Atlantis World, namespace `atlantis::world::ecs`
  (`ecs::World`, `ecs::EntityId`, `ecs::CommandBuffer`), in its own headers
  under `src/world/include/atlantis/world/ecs/`.
- This needs no module-list change (AGENTS.md's eleven modules stand). The
  generic core names no World component, so a later option C can layer World
  over it inside the same module.

### Component identity (ruling Q3)

```cpp
// Explicit, non-intrusive: specialized beside the component's module.
template <> struct ComponentType<world::Transform> {
  static constexpr std::string_view kName = "world::Transform";  // == its schema name
};
// ComponentTypeId == schema::typeId(ComponentType<T>::kName)
```

- Compile-time requirements are checked where the mapping is used:
  - trivially copyable;
  - trivially destructible;
  - standard-layout.
- Per-module tests check that every mapped type's `TypeId` is in its module's
  schema table.
- The ECS stores size and alignment from the C++ type. It never reads
  descriptors at runtime, so Spec 0048 stays descriptive.

### Storage (R4, R5)

```
ecs::World ── entity table: index → {generation, archetype, chunk, row}
          └── archetypes (creation order): {sorted TypeIds, column layout,
                                            chunks: [ EntityId[] | T0[] | T1[] … ]}
```

- An `add` or `remove` copies the row's columns to the target archetype
  (trivially copyable, R3) and swap-removes the source row. The moved entity's
  table entry is updated.

### Access rules (ruling Q6)

- A query hands the callback references into chunk columns. They are valid
  only during that invocation and never returned or stored by the API.
- ADR-0049's by-value rule governs `world::World` and is unchanged.
- ADR-0033's rule against returning or accepting references to Runtime-owned
  objects across a Client boundary is not engaged:
  - v1 is not Runtime-owned (ruling Q5);
  - Client access stays query/command-shaped through Candidate 2.

## Architectural Impact

Yes. This Spec adds a new public API family to Atlantis World and fixes four
things: a storage model, a component-identity rule, an entity-identity rule
and an access rule. They are recorded in
[ADR-0101](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md):

- the parallel-core relationship to `world::World`;
- placement in the World module;
- `ComponentTypeId = schema::TypeId` with described, trivially-copyable
  components;
- ECS `EntityId` adopting ADR-0049 and EntityGuid binding by snapshot under
  ADR-0097 D5;
- archetype and fixed-budget chunk storage;
- callback-scoped references and deferred structural change;
- no Runtime adoption.

What does not change:

- **Dependencies:** none added.
- **Threading:** unchanged (ADR-0004).
- **Module list:** unchanged (ruling Q2: M1).

Two consequences need explicit review:

- **The GUID binding header (R2).** It is a second World header naming
  `EntityGuid`. Today `tests/world/module_boundary_tests.cpp` and AGENTS.md
  allow it in `scene_instantiation.h` alone. That is a placement rule from
  Plan 0047, not ADR-0097 D5's substance (D5 forbids *storing* a GUID).
- **The allowlist update.** By ruling Q4 (P-a) the binding lives in its own
  `ecs/` header, so the implementation adds one entry each to that test's
  allowlist and to AGENTS.md's "`scene_instantiation.h` alone" sentence. This
  is a planned change named by this Spec, not a deviation.

## Alternatives Considered

1. **A third-party ECS (EnTT, flecs).** This would be a new dependency
   against the Golden Rule. Their component identity is their own, not
   `schema::TypeId`, and their scheduling and reflection would exceed v1.
   Rejected.
2. **Sparse-set storage.** It gives cheaper add/remove and worse multi-
   component iteration. The maintainer fixed archetype and chunk storage.
   Rejected.
3. **Runtime-assigned dense component ids.** These are not stable across
   processes or builds, and they diverge from schema, scene and protocol keys.
   Rejected (ruling Q3).
4. **Compiler-derived type names** (`__PRETTY_FUNCTION__` parsing). This is
   non-portable and compiler-specific (AGENTS.md). Rejected (ruling Q3).
5. **World options A and C.** Rejected by ruling Q1.

## Testing & Verification Plan

All tests are GPU-independent Catch2 tests. No GPU path is touched, so image
regression and Validation Layers are N/A as new gates. The existing suites run
unchanged as the behaviour guard.

| Requirement | Tests |
|---|---|
| **R1** | Create, destroy and reuse; generation bump; retirement at the maximum generation (via a test-access hook, as ADR-0049's own tests do); a foreign-instance handle rejected; default sentinel invalid |
| **R2** | Batch creation returns the snapshot map; nil and duplicate GUIDs rejected; the map is unaffected by later destroys (liveness is the world's) |
| **R3** | `ComponentTypeId` of each World component equals `schema::typeId(name)` and is present in `worldSchema()`; the compile-time requirements are demonstrated against a non-trivial type during development (not committed) |
| **R4–R6** | Archetype identity under different add orders; values preserved across add/remove moves; swap-remove keeps every other entity's data; chunk overflow into a second chunk and back; every error |
| **R7** | All-of matching; deterministic order; mutation through references; read-only `const T&`; the structural-change assertion (Debug-only, the `ATLANTIS_ASSERT` test precedent) |
| **R8** | Every recorded command applied in order; deferred-create addressing; stale-at-apply results |
| **R10** | The north-star test with `world::Transform`/`world::Renderable` |

Byte guard:

- the full Debug + Release suites;
- `git diff origin/main` shows no change under `assets/`, the goldens,
  `shaders/`, `src/runtime/` or `src/tools/`;
- existing World sources and tests unchanged, except the one boundary-test
  allowlist entry ruling Q4 (P-a) requires;
- Android `assembleDebug` succeeds.

## Risks & Open Questions

Risks:

- **Two entity stores for a while.** `world::World` and `ecs::World` coexist
  until the migration Spec. Mitigations:
  - separate namespaces;
  - the North-star test names the ECS explicitly;
  - no Runtime path touches the ECS in v1.
- **Under-validated shape.** v1 has no consumer beyond tests. This is
  mitigated by using the real component types and by the north star. The
  migration Spec is the shape's real validation.
- **Identity drift.** A component's ECS name could disagree with its schema
  name. This is mitigated by R3's test against the schema tables.

Open questions — all eight ruled by Human Review (slmao, 2026-10-06, review
of [PR #206](https://github.com/slmao/Atlantis/pull/206)), each as its recommendation:

- **Q1 — Relationship to `world::World`.** **Ruled (2026-10-06): (B)**, an
  additive parallel core beside `world::World` (Proposed Design; Non-Goals).
  Evolving World in place (A) or re-implementing it over the core (C) is the
  Runtime-migration Spec's decision.
- **Q2 — Module placement.** **Ruled (2026-10-06): (M1)**, inside Atlantis
  World, namespace `atlantis::world::ecs`. AGENTS.md's module list is
  unchanged. Promotion to a top-level module needs a non-World consumer and a
  superseding ADR.
- **Q3 — `ComponentTypeId` source and mapping.** **Ruled (2026-10-06): I-a,
  T-a, D-a** (R3).
  - `ComponentTypeId` is `schema::TypeId`.
  - A C++ type maps to it through an explicit, non-intrusive
    `ComponentType<T>` specialization.
  - The mapped `TypeId` must be present in the owning module's schema table,
    checked by test.
- **Q4 — EntityId and EntityGuid.** **Ruled (2026-10-06): (a), (G1), (P-a)**
  (R1, R2). ADR-0049 and ADR-0097 D5 are unchanged.
  - A new `ecs::EntityId` adopts ADR-0049's rules.
  - EntityGuid binds only at creation and returns a caller-owned snapshot
    map. The ECS stores no GUID.
  - The binding lives in its own `ecs/` header. The World boundary test's
    allowlist and AGENTS.md's "`scene_instantiation.h` alone" sentence each
    gain one entry in the implementation. That is a planned change named by
    this Spec.
- **Q5 — Runtime adoption in v1.** **Ruled (2026-10-06): (R-a)**, not adopted
  (Non-Goals). The core plus tests prove the API; migration is a later Spec.
- **Q6 — Callback-scoped references.** **Ruled (2026-10-06): no conflict**
  (R7). References are valid only within one callback invocation, and a
  structural change during iteration fails an assertion. ADR-0049's by-value
  rule stays `world::World`'s; ADR-0033 governs Client access, which v1 does
  not offer.
- **Q7 — Component requirements.** **Ruled (2026-10-06): yes** (R3).
  Components must be trivially copyable, trivially destructible and
  standard-layout.
- **Q8 — Public names.** **Ruled (2026-10-06):**
  `atlantis::world::ecs::World`, `EntityId`, `CommandBuffer` and
  `ComponentType<T>`. The snapshot map's name is the Plan's.

## Out of Scope / Future Work

- **The Runtime-migration Spec:** World options A or C, extraction from the
  ECS, and scene instantiation into the ECS.
- **The hierarchy over ECS components:** relations and transform update.
- **Every maintainer-excluded item, as a separate future Spec each:**
  - System Scheduler;
  - Job System Integration;
  - Change Tracking;
  - Replication;
  - Reactive Query;
  - Chunk Streaming;
  - Prefab;
  - Networking;
  - C# Binding.
- **Query features:** exclusion and optional terms, and cached queries.
- **Persistent entity lookup by GUID (G2),** if the Editor protocol needs it.
