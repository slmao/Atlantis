# Spec: Runtime World Query / Command / Event Foundation

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-06
- **Related Plan(s):** none yet. Drafting a Plan is authorized only after this
  Spec's Approval.
- **Approval:** pending. The maintainer fixed the one-sentence goal, the
  concept inventory, the command and event sets, the addressing rule, the
  query coverage and the named-only list before drafting (2026-10-06, chat).
  They are recorded under Goals / Non-Goals and are not open questions.
- **Related ADR(s):**
  [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) (`Proposed`,
  drafted alongside this Spec). It records the operation boundary's
  categories, addressing, identity, value and accessor model, placement,
  event delivery and frame integration. It applies
  [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md) for the
  first time and supplies the accessor layer
  [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md) D5
  deferred.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

The maintainer's goal, verbatim: "建立与 ECS 内部布局解耦、以 Stable Identity +
Schema 寻址的 Runtime World 公开操作边界。" (Establish a public operation boundary
for the Runtime World that is decoupled from the ECS's internal layout and
addressed by Stable Identity + Schema.)

This Spec defines that boundary as exactly three concepts:

- **Query:** find an entity by its `EntityGuid`, list an entity's component
  types (`TypeId`s), and read a property by address. Results are values.
- **Command:** exactly five operations, `CreateEntity`, `DestroyEntity`,
  `AddComponent`, `RemoveComponent` and `SetProperty`, lowered onto the
  Spec 0050 ECS. No caller ever holds a component pointer or reference.
- **Event:** exactly five notifications, `EntityCreated`, `EntityDestroyed`,
  `ComponentAdded`, `ComponentRemoved` and `PropertyChanged`, for operations
  applied through this boundary only. This is not a reactive ECS.

Every property is addressed by `(EntityGuid, TypeId, FieldId)` against
`worldSchema()`. There is no per-component verb (`Get/Set<Transform>()`). The
Spec 0048 descriptor tables carry real load for the first time.

## Motivation / Problem Statement

### The Runtime World has no public operation surface

- Spec 0051 made the baked `ecs::World` Runtime's world. The only
  operations on it are the ECS's own: `get<T>`/`set<T>`/`add<T>`/`query<Ts...>`
  on an `ecs::EntityId`, a per-process handle.
- The only external caller today is a test. `runtime_smoke_gpu_tests.cpp`
  reaches `RuntimeApplication`'s `BakedScene&` through a friend hook
  (`RuntimeSmokeTestAccess::scene()`). It then edits components by C++ type
  and holds raw `EntityId`s.
- [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md), `Accepted`
  since Spec 0009, already bounds what the first real surface must look like:
  - Runtime is the sole authoritative owner of world state;
  - every observer or mutator, Editor included, is a Client;
  - access is query-, command- or event-shaped;
  - no public cross-module API returns or accepts a raw pointer or reference
    to a Runtime-owned object.

  ADR-0033 fixed the categories and left their concrete shape to "whichever
  future Spec designs the real World/Runtime API". This is that Spec.

### The identity and schema groundwork is complete but unused

- **Identity.** [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md)
  gives every scene node a persistent `EntityGuid` (D5) and a persisted
  reference form `EntityRef` (D6). Spec 0050 binds GUIDs at creation into an
  immutable `EntityGuidMap` snapshot. Its ruling Q4 chose G1 and recorded
  "persistent entity lookup by GUID (G2), if the Editor protocol needs it" as
  future work. That moment is this Spec (Q1).
- **Schema.** Spec 0048 described every World component: `TypeId`,
  `FieldId`, `PrimitiveKind`, `FieldFlags`, `byteOffset`.
  [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md) D5
  made v1 "descriptive only" and deferred the accessor layer to "the
  consuming spec". No code reads a value through a descriptor yet.
- **Addressing.** Spec 0049 already fixed the *authoring* address shape
  `PropertyAddress { EntityGuid, TypeId, FieldId }`
  (`scene_property_address.h`). It is a value type that nothing resolves yet,
  and it is keyed to the scene semantic schema (Q7).

### Why now

- Specs 0047–0051 completed the trunk: identity, schema, scene semantics,
  the ECS, and the bake.
- The maintainer judged that, at this point, strengthening the Runtime as a
  platform has more leverage than adding rendering features. This boundary
  is the waist that the Editor, human tools and Coding Agents will later all
  operate the Runtime World through.
- Building it before any of those consumers exists keeps them from each
  inventing a privileged path (ADR-0033's stated risk). It also keeps the ECS
  layout (archetypes, chunks, `EntityId`) private to the World module.

### Hazards found while drafting (they shape Q6 and Q8)

- **A runtime edit can abort the process.** `extractFrameLightingData()`
  treats a second Directional light or a 65th Point light as a programmer
  error (`ATLANTIS_CHECK_MSG`, fatal in every build). Only cook and decode
  enforce those limits (Spec 0049 constraints `MaxDirectionalLights`,
  `MaxPointLights`). A public `SetProperty(Light.kind)` or `AddComponent(Light)`
  could cross either limit.
- **`Transform` is not render-authoritative in the baked world.**
  ADR-0102 D4: the frame reads `WorldMatrix`, and `Transform` is kept as
  authored data only. A `SetProperty` on `Transform.localPosition` is
  therefore stored but has no visual effect.
- **Two field shapes defeat a pure `byteOffset` copy:**
  - `Renderable.materialAsset` is a `std::optional<AssetId>`, whose layout the
    standard does not fix;
  - `Light.kind` is an enum whose storage width no descriptor records.

## Goals

These are maintainer-fixed and are not to be relaxed in review:

- **Exactly three concepts:** Query, Command, Event. Nothing else.
- **Command v1 is exactly five:** `CreateEntity`, `DestroyEntity`,
  `AddComponent`, `RemoveComponent`, `SetProperty`.
  - They end in the Spec 0050 ECS / `CommandBuffer`.
  - No caller obtains a component pointer or reference.
- **Event v1 is exactly five:** `EntityCreated`, `EntityDestroyed`,
  `ComponentAdded`, `ComponentRemoved`, `PropertyChanged`.
  - This is not a reactive ECS.
  - Events cover only operations performed through this boundary.
- **Addressing is Stable Identity + Schema throughout:**
  `GetProperty(EntityGuid, TypeId, FieldId)` and
  `SetProperty(EntityGuid, TypeId, FieldId, Value)`. Per-component verbs
  (`Get/Set<Transform>()` and the like) are explicitly forbidden.
- **Query covers exactly three reads:**
  - find an entity by `EntityGuid`;
  - enumerate an entity's components (its `TypeId` set);
  - read a property by address.

  Results are returned by value, never by reference.
- **Named only — not designed, not scaffolded:**
  - Transaction (0053);
  - RuntimeConnection / transport (0054);
  - Agent CLI;
  - Editor;
  - Gameplay SDK;
  - C# and Python bindings;
  - RenderWorld / RenderSnapshot. These wait until a real need for
    multithreaded extraction, frame snapshots or a GPU scene appears, and get
    their own Spec then.

Goals of this Spec within those boundaries:

- **Layout independence.** No `ecs::EntityId`, archetype, chunk, row or C++
  component type crosses the boundary. A caller holds only GUIDs, schema
  ids and values.
- **Descriptor-driven access.** Every read and write is resolved through
  `worldSchema()`: field membership, kind and editability come from the
  descriptor, not from a hand-written per-component path (Q2).
- **No behavioural change.** The frame, the bake and every golden are
  unchanged.

## Non-Goals

These are maintainer-fixed:

- Anything in the named-only list above. That means:
  - no types, no reserved fields, no hooks;
  - no transaction or undo;
  - no transport, serialization of commands, or out-of-process client.
- A reactive ECS: no change tracking, no events for direct ECS operations.
- Per-component verbs.

Also out of scope:

- **More commands.** No reparenting, no batch or bulk command, no
  "duplicate entity".
- **More queries.** No query listing all entities or matching component
  sets; the maintainer's Query list is exactly the three reads (Risks).
- **Scene re-bake or authoring edits.** The boundary edits the Runtime World
  only (ADR-0102 D1).
- **ECS changes.** No change to the ECS's storage, identity or query
  semantics. Q1 and Q3 name the one additive accessor this Spec may need, on
  `EntityGuidMap`.
- **Format and rendering changes.** No asset, format, shader, Renderer or
  golden change.

## Requirements

### Functional

- **R1 — Concept inventory.** The boundary exposes Query, Command and Event
  types and functions only (Goals).
- **R2 — Addressing** (Q7).
  - Entities are addressed by `EntityGuid`. Properties are addressed by
    `(EntityGuid, TypeId, FieldId)`, resolved against `worldSchema()`.
  - A nested field (`Camera.fog.density`) is addressed under its component by
    its leaf `FieldId`, the Spec 0049 ruling J8 rule.
  - No `ecs::EntityId`, pointer or reference crosses the boundary in either
    direction (ADR-0033).
- **R3 — Queries.** Each returns by value or as an explicit error:
  - `FindEntity(guid)`: whether the Runtime World holds a live entity with
    that GUID;
  - `ListComponents(guid)`: the entity's component `TypeId`s, sorted by
    value;
  - `GetProperty(address)`: the field's current value (Q2).
- **R4 — Commands.** Exactly the five, each lowered onto ECS operations (Q5,
  Q9).
  - `CreateEntity(guid)` makes an entity with no components (Q1 decides
    where its GUID comes from).
  - `DestroyEntity(guid)`.
  - `AddComponent(guid, TypeId)` adds the component with its C++ default
    member values (`WorldMatrix` identity, `Light{}` and so on).
  - `RemoveComponent(guid, TypeId)`.
  - `SetProperty(address, value)`.
- **R5 — Events.** Exactly the five, emitted only for commands this boundary
  successfully applies, in application order (Q4).
  - One event per successful command. `DestroyEntity` emits
    `EntityDestroyed` only; its components' removal is implied.
  - A failed command emits nothing.
- **R6 — Runtime identity** (Q1).
  - Every entity the boundary can address has exactly one non-nil
    `EntityGuid`, unique within the Runtime World.
  - Baked entities keep their authored GUIDs. Entities created by
    `CreateEntity` obtain theirs per Q1.
  - The ECS core stores no GUID unless Q1 rules (b).
- **R7 — Values and access** (Q2).
  - Values are tagged by the field's `PrimitiveKind`, or are an enum's
    `int64`, and represent an absent `Optional` field.
  - Field access resolves through the descriptor; the descriptor tables are
    the authority for which fields exist and of which kind.
- **R8 — Validation** (Q6, Q8). `SetProperty` and `AddComponent` reject, with
  explicit errors and no partial effect on that command:
  - an unknown entity, component type or field;
  - a field not reachable from the addressed component;
  - a value of the wrong kind;
  - a field not flagged `Editable`;
  - plus the further checks Q6 rules.
- **R9 — Placement, ownership and frame integration** (Q3, Q5).
  - The boundary lives in Atlantis World.
  - Runtime remains the sole owner of the Runtime World (ADR-0033) and
    decides when commands apply.
  - Clients reach the world only through this boundary.
- **R10 — No behaviour change.** Runtime's frame output is unchanged and
  every golden stays byte-identical. Scene, artifact and catalog formats are
  unchanged.

### Non-functional

- **Threading:** single-threaded (ADR-0004). Every call is made on the frame
  thread, between frames, per Q5.
- **Performance:**
  - Queries and command application are O(1) or O(log n) per command in the
    entity count, plus the ECS's own operation cost.
  - Nothing is added to the per-frame path when no command is pending.
- **Memory:** the GUID index is O(entities) (Q1). The event queue is bounded
  by the commands applied since the last drain.
- **Dependencies:** none added. World already names `EntityGuid`/`AssetGuid`
  (Asset System) and `schema` (Core).
- **Portability:** Windows and Android, same code.

## Proposed Design

### Layering

```
  Clients (tests now; Editor / tools / Agents later, via 0054)
        │   Query (by value) · Command (submitted) · Event (drained)
        ▼
  Runtime World operation boundary   (Atlantis World, Q3)
        │   GUID index (Q1) · descriptor accessor (Q2) · validation (Q6)
        ▼
  ecs::World of the BakedScene        (Spec 0050/0051; layout private)
```

- **Runtime owns the `BakedScene` and the boundary object over it** (Q3, Q5).
  Its own frame keeps reading the `BakedScene` through the Spec 0051
  `collect*()` functions. That is the owner's internal access, not a
  client's.
- **The boundary keeps its own ordered list of address-level commands**
  (Q9). It lowers each one onto ECS operations when Runtime applies the list.
  - An `ecs::CommandBuffer` cannot hold them directly: its `set<T>` copies a
    whole component at record time, which loses earlier field edits in the
    same batch.
  - A `CreateEntity` and later commands addressing the new GUID resolve at
    apply time.

### Addressing and access (R2, R7)

- **Schema.** `TypeId` names one of the World component types in
  `ecs::WorldComponentTypes` (Transform, Camera, Light, Renderable,
  WorldMatrix). The boundary dispatches at compile time over that list, so
  no type-erased ECS entry point is added.
- **Field resolution.** A field resolves by walking the component's
  descriptor to the leaf, nested structs included. This yields the leaf's
  kind, its flags and the summed byte offset.
- **Reads.** `get<T>` by value, then the field is extracted per Q2.
- **Writes** (at apply). `get<T>`, overwrite the field per Q2, then `set<T>`.

### Errors

The error set is fixed by the Plan from R3–R8. The error kinds needed are:

- unknown entity;
- nil or duplicate GUID;
- unknown component type;
- component missing;
- component already present;
- unknown field;
- field not editable;
- kind mismatch;
- non-finite value;
- constraint violation (Q6);
- structural change refused during a query (the ECS's J1 refusal, surfaced).

## Architectural Impact

Yes. Recorded in
[ADR-0103](../adr/0103-runtime-world-operation-boundary.md), drafted
alongside:

- the three-concept boundary and its fixed command and event sets;
- GUID + schema addressing;
- runtime GUID identity (Q1);
- the value and accessor model (Q2);
- placement and ownership (Q3);
- event delivery (Q4);
- frame integration and authority (Q5);
- validation (Q6, Q8).

It applies:

- [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md): the first
  concrete Query/Command/Event shape;
- [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md) D5:
  the first accessor layer.

It keeps:

- [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md) D5;
- [ADR-0101](../adr/0101-runtime-ecs-core-storage-identity-and-placement.md) D4
  (no GUID in the ECS core);
- [ADR-0102](../adr/0102-authoring-runtime-world-separation-and-scene-bake.md).

If Q1 is ruled (b), it would instead supersede ADR-0097 D5 and ADR-0101 D4 in
part.

Planned changes to existing files, named here for the Plan:

- **The World boundary test's EntityGuid allowlist and AGENTS.md's matching
  sentence** gain the new header that names `EntityGuid` (the Spec 0050
  ruling Q4 P-a precedent).
- **`ecs::EntityGuidMap`** may need a read-only enumeration (Q1, Q3). It has
  only `find()`/`size()`, so the boundary cannot seed its index from it
  otherwise.
- **Runtime** (Q5):
  - `RuntimeApplication` gains the boundary object beside its `BakedScene`;
  - `runtime_smoke_gpu_tests` moves its live edits onto the boundary.

## Alternatives Considered

1. **Per-component verbs** (`GetTransform`, `SetLight` …), the shape
   `world::World` has. Forbidden by the maintainer. It also multiplies with
   every component, and a protocol client cannot enumerate it.
2. **Expose `ecs::World` and `EntityId` to clients.** This breaks ADR-0033
   (internal handles across the boundary) and freezes the ECS layout as
   public API.
3. **Return references into chunks for reads.** Forbidden by ADR-0033, and
   the ECS's own callback-scoped references would dangle.
4. **A reactive ECS with change tracking for all writes.** It is excluded,
   and it is a Spec 0050 exclusion (change tracking).
5. **Address by the scene schema (`asset_system::scene::*`) ids.** That is the
   authoring document's schema, not the Runtime World's (ADR-0102 D1; Q7).

## Testing & Verification Plan

- **Queries (R3):**
  - every World component and field of a baked committed scene read back
    through `GetProperty`, equal to `ecs::World::get<T>`;
  - `ListComponents` against each entity's actual set;
  - `FindEntity` true for every node GUID and false for an unknown one.
- **Commands (R4):** each of the five, alone and in an ordered batch.
  - A `CreateEntity` followed in the same batch by `AddComponent` /
    `SetProperty` on the new GUID.
  - Two `SetProperty` on the same component in one batch, where both edits
    survive.
  - `DestroyEntity` then commands on that GUID, which are rejected.
  - Each failure leaves the world and the other commands' effects as Q9
    rules.
- **Events (R5):**
  - exactly one per successful command, in application order, with the
    applied value;
  - none for failures;
  - none for direct ECS edits (the non-reactive check).
- **Identity (R6):** per Q1.
  - Nil and duplicate GUIDs are rejected.
  - A destroyed GUID is unknown afterwards. Its reuse is per Q1.
- **Values and access (R7):** for every field of every World component:
  - a round trip of a non-default value;
  - kind mismatch rejected;
  - `Optional` absent/present;
  - an enum by its `int64`;
  - nested leaf fields (`Camera.fog.density`).

  A test ties the accessor to the descriptors, so a schema/struct drift
  fails it (the `ATLANTIS_SCHEMA_PROBE` precedent).
- **Validation (R8, Q6):** non-editable, non-finite and constraint-violating
  writes are rejected. In particular, a second Directional light through
  `SetProperty` or `AddComponent` never reaches `extractFrameLightingData()`'s
  fatal check.
- **Layout independence (R2):** a header scan finds no `ecs::` type or
  component C++ type in the boundary's public signatures.
- **Runtime (R9, R10; per Q5):**
  - `runtime_smoke_gpu`'s live edits go through the boundary and keep
    Spec 0022's surviving contract (Light value, world matrix, Light create
    and destroy reach the next frame);
  - the full Debug + Release suites with every golden byte-identical;
  - Validation Layers clean;
  - Android `assembleDebug`.

## Risks & Open Questions

Risks:

- **Surface creep.** Each future consumer will want one more verb. The fixed
  sets (Goals) and the named-only list are the guard; additions are later
  Specs.
- **No entity listing.** The Query list cannot enumerate entities. An Editor
  needs that, and it is a candidate for 0054 or a later Spec.
- **Validation is necessarily partial in v1** (Q6). Semantically odd values
  that are type-correct (a negative intensity) reach the renderer as they
  would through `world::World` today.
- **Non-atomic batches** (Q9) until Transaction (0053).

Open questions (to be ruled at review):

- **Q1 — Runtime EntityGuid** (the Spec 0050 Q4-G2 moment).
  - **(a) The boundary owns a mutable `EntityGuid → EntityId` index.**
    - The ECS core still stores no GUID: ADR-0097 D5 and ADR-0101 D4 are
      kept as written.
    - The bake's `EntityGuidMap` seeds the index. This needs a read-only
      enumeration of that snapshot (`EntityGuidMap` has only
      `find()`/`size()` today), a small additive ECS-module accessor.
    - `CreateEntity` sub-options:
      - (a1) the caller supplies the GUID: required, non-nil, unique;
      - (a2) caller-optional, generated if absent (needs a GUID source, which
        is non-deterministic unless seeded);
      - (a3) always generated.
    - A destroyed GUID is removed from the index. Reusing it for a new
      `CreateEntity` is allowed: it then names the new entity.
  - **(b) `EntityGuid` becomes an ECS component** (a `world::EntityGuidTag`
    with the GUID).
    - Lookup needs either a query scan or an index anyway.
    - It supersedes ADR-0097 D5 and ADR-0101 D4 in part, and needs a new ADR.
    - It puts persistence identity into simulation data.
  - **(c) Snapshot map only.**
    - Entities created at runtime have no GUID and cannot be addressed.
    - The Editor story does not hold, and `CreateEntity` could not be
      followed by any addressed command.
  - **Recommendation: (a) with (a1).**
    - GUIDs stay asset-layer identity, the ECS stays GUID-free, and the one
      structure that needs a mutable map owns it.
    - Caller-supplied GUIDs make a batch self-addressing: `CreateEntity(g)`
      then `AddComponent(g, …)` before apply. They also keep runs
      deterministic, and a future Editor or Agent already mints GUIDs as
      authors do.
    - **Consequence to rule with it:** the Runtime's `resolveEntityRef()`
      (ADR-0097 D6) keeps resolving through the bake snapshot. Persisted
      `EntityRef`s name authored entities only. Moving it onto the live
      index is left to the first Spec that persists references to
      runtime-created entities.
- **Q2 — Value representation and the accessor.**
  - **Value:**
    - (V-a) `PropertyValue`, a variant over the six `PrimitiveKind`s
      (`UInt64`, `Float32`, `Vec3Float32`, `Vec4Float32`, `AssetGuid`,
      `EntityGuid`), plus `EnumValue { int64 }`, plus `Absent` for an
      `Optional` field;
    - (V-b) raw bytes plus `PrimitiveKind`;
    - (V-c) typed templates, forbidden as verb sprawl.
  - **Accessor**, the first use of `FieldDescriptor.byteOffset`
    (ADR-0099 D5):
    - (A1) **Generic `byteOffset` copy for every non-`Optional` field**, for
      a primitive at its `primitiveSize`.
      - Enum fields assume a fixed storage width, `std::int32_t`. This is a
        convention the World schema test checks per described enum, since
        `LightKind` is a plain `enum class` (`int`).
      - `Optional` fields go through a small typed accessor list, tested
        against their descriptors. Today that is the single field
        `Renderable.materialAsset`, because `std::optional`'s layout is not
        standard.
    - (A2) Extend the Core vocabulary: enum storage width in
      `TypeDescriptor`, plus a fixed `Optional` layout convention (or
      replacing `std::optional` in components). These are additive ADR-0099
      vocabulary changes.
    - (A3) A hand-written typed accessor per component. Every field is
      written by hand, so the descriptors carry no load.
  - **Recommendation: V-a + A1.**
    - The values are self-describing and match the descriptor kinds, and an
      Editor/Agent protocol (0054) can serialize them.
    - The descriptors carry the load exactly as ADR-0099 D5 intended, for 23
      of the World's 24 leaf fields (Transform 3, Camera 4 plus fog 5 and
      bloom 2, Light 4, Renderable 2, WorldMatrix 4). The two shapes a pure
      offset copy cannot handle are closed by a tested convention (the one
      enum) and a one-entry typed list (the one `Optional`), not a vocabulary
      change.
- **Q3 — Module placement, naming, and relation to `BakedScene`.**
  - **Placement.** Atlantis World. Runtime is the composition root, and its
    `atlantis_runtime_host` library may not be depended on by other modules
    (AGENTS.md).
  - **Naming:**
    - (N1) namespace `atlantis::world::access`, object `RuntimeWorldAccess`;
    - (N2) `atlantis::world::ops` / `WorldOps`;
    - (N3) `atlantis::world::api` / `WorldApi`.
  - **Relation:**
    - (W-a) the boundary object **owns** the `BakedScene` (moved in), and
      Runtime reaches the ECS for its frame through it;
    - (W-b) the boundary object **borrows** the Runtime-owned `BakedScene`.
      Runtime holds both, and the object's lifetime is nested inside the
      scene's;
    - (W-c) free functions over `BakedScene&` plus a separate index object.
  - **Recommendation: N1 + W-b.**
    - W-b keeps Runtime the owner of the world it renders (ADR-0033) without
      handing a `BakedScene&` back out of the boundary to its own owner.
    - It also keeps Spec 0051's frame code unchanged.
    - The index and event queue have one home with a clear lifetime.
    - The name says what it is (access to the Runtime World) without
      colliding with `atlantis::runtime`.
- **Q4 — Event delivery.**
  - (E-a) Synchronous callbacks during apply. This invites re-entrant
    commands mid-apply, and callbacks during an ECS operation.
  - (E-b) **An ordered queue that the client drains.** Events are appended
    as each command applies and are taken by value via `drainEvents()`.
  - (E-c) Both.
  - **Order:** application order. One event per successful command; none for
    failures.
  - **Scope, stated as a requirement:** only commands applied through this
    boundary produce events (not reactive). Edits made directly on the
    `ecs::World` by its owner (Runtime-internal code, tests) produce none.
  - **Recommendation: E-b.** It is deterministic and re-entrancy-free under
    ADR-0004, and it is the shape a transport (0054) will serialize.
- **Q5 — Frame integration and authority.**
  - **When:**
    - (F-a) clients submit at any time between frames, on the frame thread,
      and the owner applies the pending list at one fixed point: the start
      of `runFrame()`, before collection;
    - (F-b) commands apply immediately on submission.
  - **Runtime adoption in v1:**
    - (H-a) `RuntimeApplication` creates the boundary object over its scene
      and applies pending commands at that point. `runtime_smoke_gpu_tests`
      moves its live edits from the raw `BakedScene&` hook onto the boundary
      (submit, `runFrame`, drain), which retires the hook's mutable scene
      access. Nothing is exposed outside the process.
    - (H-b) The boundary exists in World with its own tests. Runtime is
      unchanged.
  - **ADR-0033 as applied here:**
    - Runtime is the owner;
    - every client, including tests standing in for future Editor and
      Agent clients, reaches the world only through the boundary;
    - Runtime's own frame code is owner-internal and is not a client.
  - **Recommendation: F-a + H-a.**
    - One application point per frame keeps the frame's view consistent and
      matches the ECS's "structural changes between queries" rule.
    - Adopting it in Runtime proves the boundary on the real world under the
      real frame, without adding a transport.
- **Q6 — `SetProperty` / `AddComponent` validation scope.**
  - (D-a) Type correctness only: entity, type, field, kind, `Editable`.
  - (D-b) D-a, plus finite floats on every `Float32`/`Vec*Float32` write.
  - (D-c) D-b, plus the two document constraints whose violation is a crash
    in Runtime: at most one Directional and at most 64 Point lights. The
    limit is the scene schema's `MaxPointLights` constraint, equal to Asset
    System's `kMaxPointLightsPerScene`, which Runtime's `kMaxPointLights` is
    tied to by `static_assert`. World cannot name the Runtime header. These are checked on `SetProperty(Light.kind)` and
    `AddComponent(Light)`, counting every entity that holds a `Light`
    (whether or not it has a `WorldMatrix` yet).
  - (D-d) Full reuse of Spec 0049's value domains and constraints.
    - Those are keyed to scene-schema `FieldId`s, so this needs a
      world↔scene projection (Q7).
    - Exclusivity and cardinality rules would also forbid legitimate runtime
      shapes.
  - **Recommendation: D-c.**
    - Type correctness is the v1 floor.
    - Finiteness is cheap and universal.
    - The two light limits are crash guards, not value domains: a client
      must never be able to abort the Runtime.
    - Value domains (color in [0,1], intensity ≥ 0) and the rest of 0049's
      constraints are referenced as future work, not enforced. A
      type-correct but semantically odd value renders as it would through
      `world::World` today.
- **Q7 — Address space and the address type** (surfaced while drafting).
  - The boundary addresses the **Runtime World's schema** (`worldSchema()`:
    `world::Camera`, `world::CameraFog.density` …). Spec 0049's
    `PropertyAddress` addresses the **authoring schema**
    (`asset_system::scene::*`): the same names, but different `TypeId` and
    `FieldId` values.
  - Options:
    - (S-a) world-schema ids, with the boundary's own
      `world::access::PropertyAddress` of the same shape (and the same text
      form, if the Plan wants one);
    - (S-b) reuse `asset_system::scene::PropertyAddress` and accept
      world-schema ids in it;
    - (S-c) accept scene ids and project them by name alignment.
  - **Recommendation: S-a.**
    - The Runtime World is a different representation (ADR-0102), and its
      schema is `worldSchema()`.
    - Reusing the scene type would blur exactly the line ADR-0102 drew.
    - A scene↔world address projection is future work, for the Editor's
      "edit authoring, see runtime" story.
- **Q8 — `Transform` in the baked world** (surfaced while drafting;
  ADR-0102 D4).
  - (T-a) `Transform` fields are readable and writable, but writes are inert
    for rendering, because `WorldMatrix` is authoritative. This is
    documented, and `PropertyChanged` is still emitted.
  - (T-b) `SetProperty` on `world::Transform` is refused as "not
    runtime-authoritative". This needs a per-type runtime writability rule
    beside the schema's `Editable` flag.
  - (T-c) Writing `Transform` also recomputes `WorldMatrix`. That is correct
    only for unparented baked nodes, because the bake drops parents.
  - **Recommendation: T-a.**
    - The schema's `Editable` flag stays the single writability authority.
    - Placement is edited through `WorldMatrix`, which is `Editable`
      (Plan 0051 J3).
    - The trap is documented in the boundary's contract and tested.
    - T-b is the right shape if review prefers it, but it introduces a
      second writability vocabulary, which deserves its own decision.
- **Q9 — Command failure and batch semantics** (surfaced while drafting).
  - (B-a) **Ordered, per-command.**
    - Each command applies or fails independently, and the batch continues.
    - Every failure is reported with its index and error (the Plan 0050 J3
      precedent).
    - A failed command has no effect.
  - (B-b) All-or-nothing per batch. This is a transaction, which is
    Spec 0053's.
  - **Recommendation: B-a.** Atomicity and undo are 0053's named scope. B-a
    is the simplest semantics a transaction can later be layered on.

## Out of Scope / Future Work

- **Named consumers and layers** (maintainer list):
  - Transaction (0053);
  - RuntimeConnection / transport (0054);
  - Agent CLI;
  - Editor;
  - Gameplay SDK;
  - C# and Python bindings;
  - RenderWorld / RenderSnapshot.
- **Roadmap context.** The maintainer's 0053–0056 roadmap is context only;
  this Spec makes no commitment to it.
- **Further work:**
  - an entity-listing query;
  - value-domain validation via a world↔scene projection (Q6, Q7);
  - re-bake / authoring edits;
  - moving EntityRef resolution onto the live index (Q1);
  - a runtime-writability rule for `Transform` (Q8, if T-b is wanted later);
  - a `const` ECS query (Candidate 9).
