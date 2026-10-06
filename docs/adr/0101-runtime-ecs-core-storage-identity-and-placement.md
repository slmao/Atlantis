# ADR 0101: Runtime ECS Core — Relationship to World, Placement, Identity, Storage and Access

- **Status:** Accepted
- **Date:** 2026-10-06 (accepted 2026-10-06)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-06 (review of this branch's own PR,
  [PR #206](https://github.com/slmao/Atlantis/pull/206); accepted together with Spec 0050's Approval, its eight open
  questions ruled as recommended)
- **Related Spec:** [Spec 0050: Runtime ECS Foundation](../specs/0050-runtime-ecs-foundation.md) (`Approved`)
- **Related ADR(s):**
  - Extends [ADR-0048](0048-world-scene-module-boundary-and-ownership.md):
    World gains an ECS core and its boundary is otherwise unchanged.
  - Adopts [ADR-0049](0049-entity-identity-and-handle-invalidation.md)'s
    handle rules for a new handle type, leaving `world::EntityId` untouched.
  - Leaves [ADR-0050](0050-transform-hierarchy-composition-and-update-model.md)
    and [ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
    unchanged.
  - Keeps [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md) D5.
  - Builds on [ADR-0099](0099-engine-schema-core-and-descriptor-vocabulary.md)
    (`schema::TypeId`).
  - Stays inside [ADR-0004](0004-phase1-threading-baseline.md).
  - Respects [ADR-0033](0033-runtime-authority-and-client-boundary.md)'s
    Client boundary.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **No ECS exists.** Atlantis has no generic entity-component store.
  `world::World` is a slot map with four hard-coded component kinds
  (ADR-0048/0049), explicitly "not a generic, type-erased ECS registry".
  ADR-0033 deferred handle representation and ECS storage layout to a
  World/ECS Spec, and Spec 0050 is that Spec.
- **The maintainer fixed Spec 0050's boundaries** (2026-10-06):
  - v1 is exactly `EntityId`, `EntityGuid`, `ComponentTypeId`, `Archetype`,
    `Chunk`, `Query` and `CommandBuffer`;
  - scheduler, jobs, change tracking, replication, reactive queries, chunk
    streaming, prefab, networking and C# are named only;
  - single-threaded;
  - a north-star API (`createEntity`/`add<T>`/`query<Ts...>(callback)`);
  - additive, with no change to existing frame-path behaviour.
- **Binding constraints:**
  - ADR-0049: `EntityId` is never persisted; handles are validated; World's
    accessors are by value.
  - ADR-0097 D5: World, `EntityId` and components store no GUID. A
    GUID↔EntityId side table inside World was explicitly rejected.
  - Spec 0048: component types can be named by a stable `schema::TypeId`.
  - Spec 0049 ruling Q4: components are keyed by `TypeId`, at most one per
    type per node.
  - AGENTS.md's module list is a reviewed decision.

## Decision

1. **Additive parallel core.** A new archetype-based ECS world exists beside
   `world::World`; neither replaces nor wraps the other in v1.
   - `world::World`, its slot map, hierarchy, scene instantiation and
     Runtime extraction are unchanged.
   - Choosing between evolving World in place and re-implementing it over
     the core is the Runtime-migration Spec's decision (Spec 0050 ruling Q1).
2. **Placement.** The core lives in Atlantis World, namespace
   `atlantis::world::ecs`, with headers under `atlantis/world/ecs/`.
   - AGENTS.md's module list is unchanged.
   - The generic core names no World component type.
   - Promotion to a top-level module needs a non-World consumer and a
     superseding ADR (Spec 0050 ruling Q2).
3. **Component identity.** `ComponentTypeId` is `schema::TypeId`, mapped from
   a C++ type by an explicit, non-intrusive `ComponentType<T>` specialization
   naming the type's schema name.
   - A component must be described in its owning module's Spec 0048 table
     under that `TypeId`, checked by test.
   - It must be trivially copyable, trivially destructible and
     standard-layout, checked at compile time.
   - The ECS never reads descriptors at runtime (Spec 0050 rulings Q3, Q7).
4. **Entity identity.** The ECS has its own `ecs::EntityId`, which adopts
   ADR-0049's rules unchanged:
   - index plus 64-bit generation;
   - slot retirement;
   - a per-instance identity token;
   - an invalid sentinel;
   - never persisted;
   - explicit errors for stale or foreign handles.

   EntityGuid is bound only at creation: a batch creation taking EntityGuids
   returns a caller-owned, immutable `EntityGuid → EntityId` snapshot. The
   ECS stores no GUID, so ADR-0097 D5 is unchanged (Spec 0050 ruling Q4).
5. **Storage.**
   - An archetype is the sorted set of an entity's `ComponentTypeId`s, with at
     most one component per type.
   - Each archetype stores rows in fixed-byte-budget chunks: one column per
     component plus an `EntityId` column, densely packed with swap-remove.
   - An entity table maps an index to {generation, archetype, chunk, row}.
   - Iteration order is deterministic: archetype creation order, then chunk,
     then row.
6. **Access.**
   - Queries are all-of callbacks receiving `EntityId` and `T&`/`const T&`,
     valid only for one invocation and never returned or retained.
   - A structural change to the queried world during a query is a programmer
     error (assertion). Deferred structural change goes through a
     `CommandBuffer` applied explicitly, in recording order.
   - Point access (`get`/`set`) is by value.
   - ADR-0049's by-value rule continues to govern `world::World`. ADR-0033's
     Client-boundary rule is not engaged, because the core is not
     Runtime-owned in v1 and offers no Client surface (Spec 0050 ruling Q6).
7. **Single-threaded, not adopted.**
   - Every type is documented not thread-safe (ADR-0004), with no global
     state.
   - Runtime neither owns nor reads an ECS world in v1 (Spec 0050 ruling Q5).
   - The maintainer-excluded features are not designed or scaffolded.

## Consequences

### Positive

- Atlantis gets a generic, data-oriented component store with stable
  identities shared by schema, scene and future protocol layers. A new
  component no longer implies new store methods.
- No existing behaviour, ADR, format or frame-path code changes. The core can
  be reviewed, measured and evolved before anything depends on it.
- Handle safety and persistence rules match World's exactly, so the eventual
  migration changes storage, not semantics.

### Negative / Trade-offs

- **Two entity stores** coexist until the migration Spec, with two
  `EntityId` types in two namespaces.
- **Generality not yet proven by a real consumer.** The v1 shape is proven
  only by tests, and the migration may still find gaps.
- **Restricted component types.** Trivially copyable components exclude
  owning types (strings, vectors) until a reviewed extension adds move and
  destroy hooks.
- **A second World header names `EntityGuid`** (the creation-time binding).
  The World boundary test's allowlist and AGENTS.md's sentence each gain one
  entry, unless the binding is declared in `scene_instantiation.h`
  (Spec 0050 ruling Q4).
- **Callback-scoped references** need care: a reference must not be kept past
  its invocation. The API makes retention impossible only by convention and
  documentation, plus the structural-change assertion.

## Alternatives Considered

- **Evolve `world::World` in place, or re-implement it over the core.** Both
  supersede ADR-0049's slot-map decision in part and touch the frame path,
  against the maintainer's additive boundary. They are deferred to the
  migration Spec.
- **A new top-level ECS module.** It changes the reviewed module list for a
  core with no non-World consumer. Revisit when one appears.
- **A third-party ECS.** That is a new dependency, with identity and
  scheduling models of its own.
- **Runtime-dense or compiler-derived component ids.** These are unstable
  across builds or processes, or non-portable, and they diverge from schema
  keys.
- **An EntityGuid component or side table.** This contradicts ADR-0097 D5. If
  a future Editor protocol needs lookup by GUID, it must supersede D5 in part
  with its own ADR.
- **Sparse-set storage.** The maintainer fixed archetype and chunk storage.
