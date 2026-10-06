# Spec: Runtime World Query / Command / Event Foundation

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-06
- **Related Plan(s):** none yet — Plan 0052 drafting is authorized by the
  Approval below. **Implementation still awaits its own, separate Joint Human
  Review** of Spec + Plan together, per AGENTS.md's own workflow.
- **Approval:** slmao, 2026-10-06 (review of this Spec's own branch PR,
  [PR #214](https://github.com/slmao/Atlantis/pull/214)) — authorizes drafting Plan 0052; Implementation itself
  still awaits its own, separate Joint Human Review of Spec + Plan together.
  The maintainer fixed the one-sentence goal, the concept inventory, the
  command and event sets, the addressing rule, the query coverage and the
  named-only list before drafting (2026-10-06, chat). They are recorded under
  Goals / Non-Goals. The same review ruled all nine open questions, each as
  its recommendation. See Risks & Open Questions below.
  **Correction (2026-10-06, post-Approval, Plan 0052 Joint Human Review,
  [PR #215](https://github.com/slmao/Atlantis/pull/215), rulings J1–J3):** three crash paths a client could reach,
  found while drafting Plan 0052.
  - **J1 — light-guard counting.** The light limits count entities holding
    both `Light` and `WorldMatrix`, exactly the set `collectLights()` hands
    to extraction. They are checked on `AddComponent(Light)`,
    `AddComponent(WorldMatrix)` and `SetProperty(Light.kind)`.
    - The ruled text counted every entity holding a `Light`.
    - Because `Light{}` defaults to `Directional`, that rule would have
      refused every `AddComponent(Light)` in a scene with a Directional
      light.
  - **J2 — an unloaded material id.**
    - Runtime's frame excludes a referenced material id that the scene load
      did not load from material realization, and skips the entity for that
      frame. This is Spec 0018 D4 case 3, the existing "present but
      unresolvable → skip" semantics.
    - Before this, `realizePendingMaterials()`'s fatal check aborted on such
      an id.
    - Spec 0051's frame code therefore changes in two places: the apply
      step, and this skip. Behaviour is unchanged for every loaded scene.
  - **J3 — active-camera protection.** `DestroyEntity` of the active camera
    entity, and `RemoveComponent` of its `Camera` or `WorldMatrix`, are
    refused with `ActiveCameraProtected`.
    - Without this, `collectActiveCamera()`'s check aborts, or `runFrame()`
      fails the Runtime.

  No other requirement or ruling changes.
- **Related ADR(s):**
  [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) (`Accepted`
  2026-10-06, alongside this Spec's own Approval). It records the operation
  boundary's categories, addressing, identity, value and accessor model,
  placement, event delivery, frame integration, validation and batch
  semantics. It applies
  [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md) for the
  first time and supplies the accessor layer
  [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md) D5
  deferred. ADR-0097 D5/D6 and ADR-0101 D4 are unchanged (ruling Q1 (a)).

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
  unchanged. Runtime's frame code changes only by the apply step and J2's
  unloaded-material skip, with no effect on any loaded scene (Correction 2026-10-06, Plan 0052 ruling J2).

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
  semantics. The one additive change is a read-only enumeration on
  `EntityGuidMap` (ruling Q1).
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

  `DestroyEntity` and `RemoveComponent` likewise refuse to remove the active
  camera or its `Camera`/`WorldMatrix` (Correction 2026-10-06, Plan 0052 ruling J3).
- **R9 — Placement, ownership and frame integration** (Q3, Q5).
  - The boundary lives in Atlantis World.
  - Runtime remains the sole owner of the Runtime World (ADR-0033) and
    decides when commands apply.
  - Clients reach the world only through this boundary.
- **R10 — No behaviour change.** Runtime's frame output is unchanged and
  every golden stays byte-identical. The frame's only code changes are the
  apply step and J2's skip of a material id the scene did not load
  (Correction 2026-10-06, Plan 0052 ruling J2). Scene, artifact and catalog formats are
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
- active camera protected (Correction 2026-10-06, Plan 0052 ruling J3);
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

Ruling Q1 (a) keeps ADR-0097 D5/D6 and ADR-0101 D4 unchanged; the
rejected option (b) would have superseded them in part.

Planned changes to existing files, named here for the Plan:

- **The World boundary test's EntityGuid allowlist and AGENTS.md's matching
  sentence** gain the new header that names `EntityGuid` (the Spec 0050
  ruling Q4 P-a precedent).
- **`ecs::EntityGuidMap`** gains a read-only enumeration (ruling Q1). It has
  only `find()`/`size()`, so the boundary cannot seed its index from it
  otherwise. The map stays an immutable snapshot, and the ECS stores no GUID.
- **Runtime** (ruling Q5):
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

Open questions — all nine ruled by Human Review (slmao, 2026-10-06, review
of [PR #214](https://github.com/slmao/Atlantis/pull/214)), each as its
recommendation. Q1–Q6 were posed by the maintainer; Q7–Q9 surfaced while
drafting.

- **Q1 — Runtime EntityGuid** (the Spec 0050 Q4-G2 moment). **Ruled
  (2026-10-06): (a) with (a1)** (R4, R6).
  - **The index.** The boundary owns a mutable `EntityGuid → EntityId`
    index.
    - The ECS core stores no GUID, so ADR-0097 D5 and ADR-0101 D4 are kept as
      written.
    - The bake's `EntityGuidMap` seeds the index through a read-only
      enumeration added to that snapshot, an additive ECS-module accessor.
  - **New entities.** `CreateEntity` takes a caller-supplied GUID, which must
    be non-nil and unique in the Runtime World, so a batch addresses its own
    new entities.
  - **Destruction.** A destroyed GUID leaves the index. Reusing it later
    names the new entity.
  - **EntityRef.** Resolution (ADR-0097 D6, `resolveEntityRef()`) stays on
    the bake snapshot, so persisted `EntityRef`s name authored entities only.
  - **Rejected:**
    - (a2)/(a3), generated GUIDs: they need a non-deterministic source;
    - (b), GUID as an ECS component: it supersedes D5/D4 and still needs an
      index;
    - (c), snapshot only: runtime-created entities would be unaddressable.
- **Q2 — Value representation and the accessor.** **Ruled (2026-10-06): V-a +
  A1** (R7).
  - **Value.** `PropertyValue` is a variant over the six `PrimitiveKind`s
    (`UInt64`, `Float32`, `Vec3Float32`, `Vec4Float32`, `AssetGuid`,
    `EntityGuid`), plus `EnumValue { int64 }`, plus `Absent` for an
    `Optional` field.
  - **Access** is hybrid, with a generic copy at the descriptor's
    `byteOffset` as the main path: 23 of the World's 24 leaf fields
    (Transform 3, Camera 4 plus fog 5 and bloom 2, Light 4, Renderable 2,
    WorldMatrix 4).
    - **The one enum** (`Light.kind`) uses an `int32` storage convention,
      held by `static_assert`s compiled by both toolchains (MSVC and the
      Android NDK's Clang).
    - **The one `std::optional`** (`Renderable.materialAsset`) goes through
      a typed accessor, tested against its descriptor.
  - **Recorded only, not done here:** extending the schema vocabulary with an
    enum's storage width and an `Optional` shape is registered as a future
    incremental candidate (Out of Scope).
  - **Rejected:**
    - V-b, raw bytes;
    - V-c, typed templates (verb sprawl);
    - A2, vocabulary change now;
    - A3, hand-written accessors per component.
- **Q3 — Module placement, naming, and relation to `BakedScene`.** **Ruled
  (2026-10-06): N1 + W-b** (R9).
  - Atlantis World, namespace `atlantis::world::access`, object
    `RuntimeWorldAccess`.
  - It borrows the Runtime-owned `BakedScene` and owns the GUID index and
    the event queue.
  - Runtime remains the owner, and Spec 0051's frame code is unchanged
    apart from the apply step and J2's unloaded-material skip (Correction 2026-10-06, Plan 0052 ruling J2).
  - Rejected: N2/N3 names; W-a (owning) and W-c (free functions).
- **Q4 — Event delivery.** **Ruled (2026-10-06): E-b** (R5).
  - An ordered event queue that the client drains by value.
  - One event per successful command, in application order; none for a
    failed one.
  - **Stated as a requirement:** only operations applied through this
    boundary produce events. This is not a reactive ECS: direct edits on the
    `ecs::World` produce none.
  - Rejected: E-a, synchronous callbacks; E-c, both.
- **Q5 — Frame integration and authority.** **Ruled (2026-10-06): F-a +
  H-a** (R9).
  - **When.** Clients submit between frames, on the frame thread.
    `RuntimeApplication` applies the pending commands once, at the start of
    `runFrame()`, before collection.
  - **Adoption.** Runtime adopts the boundary in v1, and
    `runtime_smoke_gpu_tests` moves its live edits onto it.
  - **ADR-0033** applies as written:
    - Runtime is the owner;
    - every client, including tests standing in for future Editor and
      Agent clients, uses only the boundary;
    - the owner's frame code is internal.
  - Rejected: F-b, immediate application; H-b, World-only adoption.
- **Q6 — `SetProperty` / `AddComponent` validation scope.** **Ruled
  (2026-10-06): D-c** (R8). The checks are:
  - type correctness (entity, component type, field, kind, `Editable`);
  - finite `Float32`/`Vec*Float32` values;
  - the two light-count limits: at most one Directional, at most 64 Point.

  The light limits are checked where the command is applied, on
  `AddComponent(Light)`, `AddComponent(WorldMatrix)` and
  `SetProperty(Light.kind)`. They count every entity that holds both `Light`
  and `WorldMatrix`, the set extraction sees (Correction 2026-10-06, Plan 0052 ruling J1; the ruled text
  counted every `Light` holder). They guard against `scene_extraction`'s
  fatal assertion and are not value domains. The limit is the scene schema's
  `MaxPointLights`, equal to Asset System's `kMaxPointLightsPerScene`, to
  which Runtime's `kMaxPointLights` is tied by `static_assert`. World cannot
  name the Runtime header.

  A third crash guard of the same kind refuses removing the active camera
  or its `Camera`/`WorldMatrix` (Correction 2026-10-06, Plan 0052 ruling J3). An unloaded material id is
  handled on the Runtime side (Correction 2026-10-06, Plan 0052 ruling J2).

  Spec 0049's value domains are named, not enforced.
- **Q7 — Address space and the address type.** **Ruled (2026-10-06): S-a**
  (R2).
  - Addresses use `worldSchema()` ids, in the boundary's own address type.
  - Spec 0049's scene `PropertyAddress`, which is authoring-schema-keyed, is
    not reused.
  - A scene↔world address projection is future work.
- **Q8 — `Transform` in the baked world.** **Ruled (2026-10-06): T-a** (R8).
  - `Transform` fields are readable and writable, and a write emits
    `PropertyChanged`.
  - The boundary's contract states that such a write does not affect
    rendering, and a test pins that.
  - Placement is edited through `WorldMatrix` (ADR-0102 D4).
  - Rejected: T-b, a second writability vocabulary; T-c, recompute
    (correct only for unparented nodes).
- **Q9 — Command failure and batch semantics.** **Ruled (2026-10-06): B-a**
  (R4).
  - Ordered, per-command apply-or-fail.
  - Every failure is reported with its command index and error; a failed
    command has no effect.
  - Atomicity and undo are Transaction's (0053).

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
  - a runtime-writability rule for `Transform` (if T-b is wanted later;
    ruling Q8);
  - a `const` ECS query (Candidate 9);
  - a schema-vocabulary increment recording an enum's storage width and an
    `Optional` shape, so the accessor could become fully generic (ruling Q2;
    recorded only).
