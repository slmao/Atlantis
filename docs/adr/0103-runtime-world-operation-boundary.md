# ADR 0103: Runtime World Operation Boundary — Query, Command, Event

- **Status:** Accepted
- **Date:** 2026-10-06 (accepted 2026-10-06)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-06 (review of this branch's own PR,
  [PR #214](https://github.com/slmao/Atlantis/pull/214); accepted together with Spec 0052's Approval, its nine open
  questions ruled as recommended)
- **Related Spec:** [Spec 0052: Runtime World Query / Command / Event Foundation](../specs/0052-runtime-world-query-command-event-foundation.md) (`Approved`)
- **Related ADR(s):**
  - Applies [ADR-0033](0033-runtime-authority-and-client-boundary.md): the
    first concrete Query/Command/Event shape under its authority rule.
  - Supplies the accessor layer
    [ADR-0099](0099-engine-schema-core-and-descriptor-vocabulary.md) D5
    deferred.
  - Keeps unchanged, under Spec 0052 ruling Q1 (a):
    - [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md) D5/D6;
    - [ADR-0101](0101-runtime-ecs-core-storage-identity-and-placement.md) D4.
  - Builds on [ADR-0102](0102-authoring-runtime-world-separation-and-scene-bake.md)
    (the Runtime World it addresses) and
    [ADR-0004](0004-phase1-threading-baseline.md) (single thread).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **No public surface on the Runtime World.** Spec 0051 made the baked
  `ecs::World` the Runtime World, but its only operations are the ECS's own,
  on per-process `EntityId`s and C++ component types. The one external
  caller is a test that reaches Runtime's `BakedScene&` through a friend
  hook.
- **ADR-0033 already fixed the rules** that the first surface must satisfy:
  - Runtime is the sole authoritative owner of world state;
  - every observer or mutator is a Client;
  - access is query-, command- or event-shaped;
  - no public API passes a raw pointer or reference to Runtime-owned state.
- **The identity and schema groundwork is in place:**
  - persistent `EntityGuid`s (ADR-0097);
  - creation-time GUID binding with no GUID in the ECS (ADR-0101 D4);
  - descriptor tables with `TypeId`/`FieldId`/`PrimitiveKind`/`byteOffset`,
    whose accessor layer ADR-0099 D5 deferred.
- **The maintainer fixed the boundary's goal and limits** (2026-10-06):
  - a public operation boundary, decoupled from ECS layout, addressed by
    Stable Identity + Schema;
  - exactly Query, Command and Event;
  - five commands and five events;
  - `Get/SetProperty(EntityGuid, TypeId, FieldId[, Value])` with no
    per-component verbs;
  - three queries returning values;
  - transaction, transport, Agent CLI, Editor, Gameplay SDK, C#, Python and
    RenderWorld named only.
- **Drafting surfaced three limits of the existing pieces:**
  - a pure `byteOffset` copy cannot read a `std::optional` field or an enum
    of unknown width;
  - extraction aborts on a second Directional light or a 65th Point light;
  - `Transform` is not render-authoritative in the baked world
    (ADR-0102 D4).

## Decision

1. **Three concepts, fixed sets.**
   - The Runtime World's public operation boundary consists of Query,
     Command and Event only.
   - **Commands** are exactly `CreateEntity`, `DestroyEntity`,
     `AddComponent`, `RemoveComponent` and `SetProperty`. They are lowered
     onto the Spec 0050 ECS at application; no component pointer or
     reference is ever handed out.
   - **Events** are exactly `EntityCreated`, `EntityDestroyed`,
     `ComponentAdded`, `ComponentRemoved` and `PropertyChanged`, emitted only
     for commands this boundary applies. This is not a reactive ECS.
   - **Queries** are `FindEntity`, `ListComponents` and `GetProperty`, and
     return values.
   - Additions are later Specs.
2. **Stable Identity + Schema addressing.**
   - An entity is addressed by its `EntityGuid`. A property is addressed by
     `(EntityGuid, TypeId, FieldId)` resolved against `worldSchema()`, with a
     nested leaf addressed under its component (the Spec 0049 J8 rule).
   - No `ecs::EntityId`, archetype, chunk, C++ component type, pointer or
     reference crosses the boundary.
   - Per-component verbs are forbidden.
   - The address space is the Runtime World's schema, not the authoring
     scene's (Spec 0052 ruling Q7).
3. **Runtime identity** (Spec 0052 ruling Q1):
   - the boundary owns a mutable `EntityGuid → EntityId` index, seeded from
     the bake's `EntityGuidMap` through a read-only enumeration added to that
     snapshot, the one additive ECS-module change;
   - `CreateEntity` takes a caller-supplied, non-nil, unique GUID;
   - the ECS core stores no GUID;
   - `EntityRef` resolution (ADR-0097 D6) stays on the bake snapshot.
4. **Values and the accessor** (Spec 0052 ruling Q2):
   - a `PropertyValue` variant over the six `PrimitiveKind`s, plus an enum's
     `int64`, plus `Absent` for an `Optional` field;
   - fields are resolved through the descriptors and read or written by
     their summed `byteOffset`;
   - the one described enum (`Light.kind`) has `std::int32_t` storage, a
     convention held by `static_assert`s compiled by both toolchains (MSVC
     and the Android NDK's Clang);
   - the one `Optional` field (`Renderable.materialAsset`) uses a typed
     accessor tested against its descriptor;
   - a schema-vocabulary increment (an enum's storage width, an `Optional`
     shape) is recorded as a future candidate only.
5. **Placement and ownership** (Spec 0052 ruling Q3):
   - Atlantis World, namespace `atlantis::world::access`, object
     `RuntimeWorldAccess`;
   - the object borrows the Runtime-owned `BakedScene` and owns the GUID
     index and event queue;
   - Runtime stays the owner and reads its `BakedScene` directly for its own
     frame.
6. **Event delivery** (Spec 0052 ruling Q4): an ordered queue,
   drained by value. One event per successful command, in application order;
   none for failures; none for direct ECS edits.
7. **Frame integration and authority** (Spec 0052 ruling Q5):
   - clients submit between frames on the frame thread;
   - Runtime applies the pending list once, at the start of `runFrame()`,
     before collection;
   - `RuntimeApplication` adopts the boundary in v1, and the smoke test's
     live edits move onto it.

   ADR-0033 applies as stated: clients use only the boundary, and the
   owner's frame code is internal.
8. **Validation** (Spec 0052 rulings Q6, Q8):
   - type correctness (entity, type, field, kind, `Editable`);
   - finite floats;
   - the two light-count limits whose violation would abort extraction,
     checked where the command is applied. These are crash guards, not
     value domains.

   Scene value domains are not enforced in v1. `Transform` writes are
   accepted but inert for rendering (`WorldMatrix` is authoritative,
   ADR-0102 D4), and this is documented.
9. **Batch semantics** (Spec 0052 ruling Q9): ordered, per-command
   apply-or-fail. Each failure is reported with its index; a failed command
   has no effect. Atomicity is Transaction's (0053).

## Consequences

### Positive

- Editors, tools and Agents get one layout-independent surface, the shape
  ADR-0033 required, before any of them exists to invent a privileged path.
- The schema tables become load-bearing, and drift is caught by tests at the
  point of use.
- The ECS layout and `EntityId` stay private and free to change.
- The fixed sets and the pull-based event queue serialize directly for a
  future transport (0054).

### Negative / Trade-offs

- **A second GUID → EntityId structure** beside the bake snapshot, and two
  views of "which entities are addressable" until EntityRef resolution moves
  onto the live index.
- **Partial validation.** Type-correct but semantically odd values (a
  negative intensity) reach the renderer, as they would through
  `world::World` today.
- **An inert `Transform` write is a trap,** documented rather than prevented
  (Spec 0052 Q8).
- **No entity listing, no batch atomicity** in v1.
- **Planned changes outside the new code:** the boundary test's EntityGuid
  allowlist and AGENTS.md's sentence gain the new header; `EntityGuidMap`
  gains a read-only enumeration.

## Alternatives Considered

- **Per-component verbs.** Forbidden, and they cannot be enumerated by a
  protocol client.
- **Exposing `ecs::World`/`EntityId` to clients.** This violates ADR-0033 and
  freezes the ECS layout.
- **GUID as an ECS component.** It puts persistence identity into simulation
  data, supersedes ADR-0097 D5 and ADR-0101 D4, and still needs an index for
  lookup.
- **A snapshot map only.** Runtime-created entities would be unaddressable.
- **Synchronous event callbacks.** They allow re-entrant commands during
  apply.
- **Immediate application.** The frame's view of the world would shift
  mid-collection; one application point per frame is simpler.
- **Scene-schema addressing.** It blurs ADR-0102's authoring/runtime line.
- **All-or-nothing batches now.** That is the Transaction Spec's (0053).
