# Spec: Runtime ECS Foundation

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-06
- **Related Plan(s):** none yet. Drafting a Plan is authorized only after this
  Spec's Approval.
- **Approval:** pending. The maintainer fixed this Spec's positioning and
  boundaries before drafting (2026-10-06, chat): the v1 inventory, the
  excluded list, single-threading, the acceptance north star, and
  additive-only. They are recorded under Goals / Non-Goals and are not open
  questions.
- **Related ADR(s):**
  [ADR-0101](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md)
  (`Proposed`, drafted alongside this Spec). It records the ECS core's
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
- **No Runtime adoption** (ruling Q5 expected): Runtime neither constructs nor
  reads an ECS world in v1.
- **No serialization, persistence or cross-process use** of ECS state or
  handles.
- **No Client/Editor access surface.** ADR-0033's query/command/event
  categories for Clients belong to Candidate 2.
- **No exclusion or optional terms in queries, no query caching, no sorting.**
- **No new dependency.**

## Requirements

### Functional

- **R1 — `EntityId`** (Q4).
  - An ECS handle with ADR-0049's rules: index plus 64-bit generation,
    permanent slot retirement at the maximum generation, an invalid sentinel,
    and a per-instance identity token (so a handle from another ECS world
    instance is detected).
  - It is never serialized, persisted or used across a process boundary.
  - A stale or foreign handle yields an explicit error result, never
    undefined behaviour (ADR-0049's runtime-state classification).
- **R2 — `EntityGuid`** (Q4).
  - The ECS stores no GUID (ADR-0097 D5 unchanged).
  - EntityGuids bind to EntityIds only at creation time: a batch creation
    that takes EntityGuids returns a caller-owned, immutable snapshot map
    `EntityGuid → EntityId`, in the manner of `SceneEntityMap`.
  - Nil or duplicate GUIDs in a batch are an explicit error. Liveness stays
    the ECS world's.
- **R3 — `ComponentTypeId`** (Q3, Q7).
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
    (Q6).
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

### Relationship to `world::World` (Q1)

| Option | What it is | Slot map / ADR-0049 | Hierarchy (ADR-0050) | Scene instantiation (ADR-0097 D5) | Runtime extraction (ADR-0051) | Frame-path risk |
|---|---|---|---|---|---|---|
| **A — evolve in place** | `world::World` gains `add<T>`/`query` and archetype storage replaces its slots | Replaced; ADR-0049's "World is a slot map" superseded in part | Must become a component or side structure; `updateTransforms()` rewritten | Rewritten | API kept, implementation changed | High: every frame touches the new storage |
| **B — additive parallel core** (recommended) | A new ECS world type beside `world::World`; nothing existing changes | Untouched; the ECS adopts ADR-0049's rules for its own handle | Untouched (not in ECS v1) | Untouched; ECS binds GUIDs by snapshot (R2) | Untouched | None |
| **C — World over archetype storage** | `world::World`'s public API kept and re-implemented on the ECS core | Internal storage replaced; ADR-0049 superseded in part | Re-implemented over components | Re-implemented | API kept | Medium–high: same API, new internals; traversal-order and determinism tests are at stake |

**Recommendation: B.**
- It is the only option compatible with "additive, no frame-path change".
- It keeps ADR-0048–0051 exactly as accepted.
- It lets Runtime migration, where A or C is decided against real
  usage, be its own Spec.
- Its cost, two entity stores in one codebase for a while, is explicit and
  temporary.

### Placement and names (Q2, Q8)

- **Recommended:** inside Atlantis World, namespace `atlantis::world::ecs`
  (`ecs::World`, `ecs::EntityId`, `ecs::CommandBuffer`), in its own headers
  under `src/world/include/atlantis/world/ecs/`.
- This needs no module-list change (AGENTS.md's eleven modules stand). The
  generic core names no World component, so a later option C can layer World
  over it inside the same module.

### Component identity (Q3)

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

### Access rules (Q6)

- A query hands the callback references into chunk columns. They are valid
  only during that invocation and never returned or stored by the API.
- ADR-0049's by-value rule governs `world::World` and is unchanged.
- ADR-0033's rule against returning or accepting references to Runtime-owned
  objects across a Client boundary is not engaged:
  - v1 is not Runtime-owned (Q5);
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
- **Module list:** unchanged, under the recommended Q2.

Two consequences need explicit review:

- **The GUID binding header (R2).** It would be a second World header naming
  `EntityGuid`. Today `tests/world/module_boundary_tests.cpp` and AGENTS.md
  allow it in `scene_instantiation.h` alone. That is a placement rule from
  Plan 0047, not ADR-0097 D5's substance (D5 forbids *storing* a GUID).
- **The allowlist update.** That test's allowlist and AGENTS.md's sentence
  would change, unless the binding lives in `scene_instantiation.h` itself
  (Q4).

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
   Rejected (Q3).
4. **Compiler-derived type names** (`__PRETTY_FUNCTION__` parsing). This is
   non-portable and compiler-specific (AGENTS.md). Rejected (Q3).
5. **World options A and C.** See Q1.

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
  allowlist line if Q4's placement requires it;
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

Open questions (to be ruled at review):

- **Q1 — Relationship to `world::World`.**
  - (A) Evolve in place.
  - (B) An additive parallel core.
  - (C) World re-implemented over archetype storage.

  See the table under Proposed Design. **Recommendation: (B)**, with A or C
  decided by the Runtime-migration Spec against real usage.
- **Q2 — Module placement.** AGENTS.md's module list is a reviewed decision.
  - (M1) Inside Atlantis World, namespace `atlantis::world::ecs`. The module
    list is unchanged.
  - (M2) A new top-level module "Atlantis ECS" below World. The module list
    changes, AGENTS.md and module_boundaries.md change, and it gets its own
    boundary test. It would not need World's GUID allowlist change.
  - (M3) Atlantis Core. Rejected: Core holds utilities and vocabulary, not
    runtime world state (ADR-0099 D1's scope).

  **Recommendation: (M1).** It is the smallest reviewed change, and the
  module already owns "Atlantis's in-memory multi-entity scene". Promote to
  M2 by a superseding ADR if a non-World consumer of the generic core
  appears.
- **Q3 — `ComponentTypeId` source and mapping.**
  - Identity:
    - (I-a) `schema::TypeId` (recommended);
    - (I-b) runtime-dense ids.
  - T → TypeId mapping:
    - (T-a) an explicit, non-intrusive `ComponentType<T>` specialization
      beside the component's module (recommended);
    - (T-b) an intrusive static member in each component struct, which edits
      existing World headers;
    - (T-c) compiler-derived names (rejected, non-portable).
  - Meaning of "must be described":
    - (D-a) strict: the mapped `TypeId` is in the owning module's schema
      table, checked by test (recommended);
    - (D-b) the name only, with no table check;
    - (D-c) a runtime descriptor lookup at registration, which would make
      the ECS read schema at runtime against Spec 0048's descriptive scope.
- **Q4 — EntityId and EntityGuid without silently changing ADR-0049 or
  ADR-0097 D5.**
  - EntityId:
    - (a) a new `ecs::EntityId` adopting ADR-0049's rules by reference
      (recommended);
    - (b) reuse `world::EntityId`, which changes its friend/constructor
      rules in an existing header.
  - EntityGuid:
    - (G1) bind at creation and return a snapshot map, storing nothing
      (recommended, D5 unchanged);
    - (G2) an `EntityGuid` component, which supersedes D5 in part via a new
      ADR (it is the likely ask of a future Editor protocol that looks
      entities up by GUID, and is not decided here);
    - (G3) no EntityGuid in v1, which contradicts the maintainer's
      inventory.
  - Placement of G1's header:
    - (P-a) a dedicated `ecs/` header, extending the World boundary test's
      allowlist and AGENTS.md's "scene_instantiation.h alone" sentence by one
      entry (recommended; a planned change named here, not a deviation);
    - (P-b) declared in `scene_instantiation.h`, which needs no allowlist
      change but mixes concerns.
- **Q5 — Runtime adoption in v1.**
  - (R-a) No: the core plus tests prove the API (recommended).
  - (R-b) Runtime dual-writes World and ECS, which changes the frame path.
  - (R-c) Runtime switches to the ECS.

  **Recommendation: (R-a).** Migration is a later Spec.
- **Q6 — Callback-scoped references.** Does handing `T&` into a query
  callback conflict with ADR-0049's by-value rule or ADR-0033's
  no-reference rule?
  - **Recommendation:** no. References are scoped to one invocation and
    never returned or retained. Structural change during iteration is
    asserted against. ADR-0049's rule stays `world::World`'s, and ADR-0033
    governs Client access, which v1 does not offer.
- **Q7 — Component requirements.** Should v1 require trivially copyable,
  trivially destructible and standard-layout components?
  - **Recommendation:** yes. Row moves become plain copies, no destructor
    bookkeeping exists, and the requirement matches Spec 0048's description
    constraint. All current World components qualify.
  - Alternative: allow non-trivial types with per-type move and destroy
    hooks (more machinery, no v1 consumer).
- **Q8 — Public names.**
  - **Recommendation:** `atlantis::world::ecs::World`, `EntityId`,
    `CommandBuffer`, `ComponentType<T>` (and the snapshot map's name, which
    is the Plan's).
  - Alternatives: `ecs::Registry`/`ecs::EntityWorld` for the store, to keep
    the bare word "World" for `world::World`.

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
