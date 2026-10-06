# ADR 0102: Authoring/Runtime World Separation and the Scene Bake

- **Status:** Accepted
- **Date:** 2026-10-06 (accepted 2026-10-06)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-06 (review of this branch's own PR,
  [PR #210](https://github.com/slmao/Atlantis/pull/210); accepted together with Spec 0051's Approval, its seven open
  questions ruled as recommended)
- **Related Spec:** [Spec 0051: Authoring Scene → Runtime World Bake](../specs/0051-authoring-scene-runtime-world-bake.md) (`Approved`)
- **Related ADR(s):**
  - Records the representation choice
    [ADR-0035](0035-authoring-runtime-data-separation-as-a-long-term-principle.md)
    requires.
  - Supersedes, in part:
    - [ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md):
      its per-frame steps 1–2, the extraction's data source;
    - [ADR-0048](0048-world-scene-module-boundary-and-ownership.md): Runtime
      owning and driving one `world::World`;
    - [Spec 0022](../specs/0022-dynamic-frame-uniform-updates-foundation.md)'s
      Goal clause on live local-`Transform` and parent-hierarchy edits
      (Spec 0051 ruling Q2, H1). Spec 0022's header records this as its
      Correction 2026-10-06. Its component-value and entity-lifetime
      live-edit guarantees remain.
  - Fulfils [ADR-0101](0101-runtime-ecs-core-storage-identity-and-placement.md)
    D1's deferred Runtime-migration decision.
  - Keeps unchanged:
    - [ADR-0049](0049-entity-identity-and-handle-invalidation.md);
    - [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md) D5 (no GUID
      stored in the ECS);
    - [ADR-0088](0088-frame-lighting-data-successor-structure-and-binding-strategy.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **Runtime reads the editable world.** Runtime keeps the loaded scene as one
  `world::World`, the editable slot map with a hierarchy and by-value
  accessors. Each frame it recomputes world matrices and walks lights and
  renderables in slot order (Spec 0051 Motivation).
- **No production code mutates it after load.**
  [Spec 0022](../specs/0022-dynamic-frame-uniform-updates-foundation.md)'s
  approved contract nonetheless makes live Light, local-`Transform` and
  parent edits visible at the next frame. A test-access hook exercises it.
- **Order is observable.** Light order sets the `FrameLightingData` packing
  and the shader's summation order. Draw order sets opaque submission and
  ties in the blended back-to-front stable sort.
- **The goldens do not run Runtime's frame.**
  - The fixtures duplicate Runtime's extraction walks over `world::World`.
  - Nine of them load scenes through Runtime's own `loadAndInstantiateScene()`
    and so depend on its `SceneLoadOutcome`.
- **ADR-0035** requires this choice to be made explicitly. Spec 0050 left the
  Runtime migration to this decision.
- **The maintainer fixed the boundaries** (2026-10-06):
  - the pipeline `.scene → Schema Validation → Authoring World → Bake →
    Runtime World → ECS/Chunk/SoA`;
  - the bake's product is the ECS;
  - Runtime switches in this step;
  - goldens stay byte-identical with no re-capture (stop and report
    otherwise);
  - the bake lives in the World module and builds on Specs 0049 and 0050;
  - Prefab, Editor, Gameplay components and Agent Patch are named only.

## Decision

1. **Authoring World ≠ Runtime World.** Authored scenes are validated (cook,
   decode, Spec 0049's schema) and **baked**. Runtime's frame reads scene
   state only from the baked `ecs::World` and the bake output's
   document-level fields, never from the authoring representation. This is
   the representation choice ADR-0035 asked for.
2. **The bake lives in Atlantis World and takes `ValidatedSceneData`.**
   - Runtime's load path is catalog → decode → bake.
   - The authoring stage is realized by the existing `instantiateScene()` and
     one `updateTransforms()`, so world matrices are bit-identical to today's
     by construction.
   - It is declared beside `instantiateScene()` in `scene_instantiation.h`
     (already allowed to name `ValidatedSceneData`/`EntityGuid`).
   - Asset System is unaware of it.
   - There is no source-text bake in the Runtime path (Spec 0051 ruling Q1).
3. **The product is `BakedScene`**: an `ecs::World`, an `EntityGuidMap` (one
   entity per node, created in node order through `createEntities()`), and
   the active camera's `EntityId`.
   - The bake is infallible on validated input: impossible states are
     `ATLANTIS_CHECK`s, as in `instantiateScene()`.
   - The authoring `world::World` is discarded before the bake returns
     (Spec 0051 ruling Q5).
4. **Hierarchy is resolved at bake.**
   - Each entity carries its authored components and a described
     `world::WorldMatrix` (four `Vec4Float32` columns), added to
     `worldSchema()` and mapped for the ECS.
   - The baked world has no hierarchy and the frame runs no transform pass.
   - Live component and entity edits of the Runtime World still reach the
     next frame, and their test coverage is kept as ECS edits.
     Local-`Transform` and parent edits no longer do: this supersedes, in
     part, Spec 0022's Goal for the Runtime World (its Correction
     2026-10-06). Hierarchy edits belong to the authoring stage and re-bake.
   - The local `Transform` stays in the baked world as authored data.
   - A runtime hierarchy is a future gameplay Spec's decision (Spec 0051 ruling Q2).
5. **Order is preserved by construction.** In the fresh baked world,
   `EntityId::index()` equals node order. Extraction stable-sorts every
   collected sequence (lights, renderables, referenced materials) by it, so
   the sequences equal today's slot-order walks regardless of archetype
   layout. When runtime entity creation or destruction actually arrives, the
   order source falls back to a described `BakeOrder` index component; that
   Spec owns the switch (Spec 0051 ruling Q3).
6. **Extraction becomes shared, per-frame queries.**
   - The camera, light, material and draw collection moves into
     GPU-independent Runtime functions over the bake output
     (`scene_extraction.h`). `runFrame()` and the scene-loading
     image-regression fixtures call them.
   - The pure extraction math and `FrameLightingData`'s layout are unchanged.
   - Data is re-extracted every frame (Spec 0051 rulings Q6, Q7).
7. **`world::World` stays a supported library type,** off the frame path and
   not deprecated: the authoring-stage world, the bake's hierarchy solver,
   and the hand-built fixtures' scene type (Spec 0051 ruling Q4).
8. **Verification is constructive.**
   - A GPU-independent test compares, byte for byte and for every committed
     scene (and Bistro when present), the frame inputs from today's
     `world::World` walks (kept as the oracle) against the bake-and-query
     path.
   - The goldens, rendering through the shared functions, must stay
     byte-identical.
   - A golden that moves stops the work.

## Consequences

### Positive

- The engine has one explicit line between authored and runtime data. Future
  Prefab, Editor and Gameplay work lands on a known side of it.
- Runtime's frame stops recomputing a static hierarchy. It reads flat SoA
  data through the Spec 0050 core, the ECS's first real consumer.
- Identity, ordering and arithmetic of everything the GPU sees are pinned by
  construction and by a CPU-side equivalence test. The goldens, for the first
  time, exercise Runtime's own collection code.

### Negative / Trade-offs

- **Hierarchy edits stop being live.** Local-`Transform` and parent edits of
  the Runtime World no longer reach the next frame (the Spec 0022 partial
  supersession). Until a re-bake path or a runtime-hierarchy Spec exists,
  only component and entity edits are live.
- **A sixth World component** (`WorldMatrix`) and the schema pins that move
  with it. `Transform` and `WorldMatrix` coexist, and only `WorldMatrix` is
  authoritative for rendering.
- **The order guarantee relies on a fresh world's index allocation.** Runtime
  creation or destruction of entities would need an explicit order component
  (Spec 0051 ruling Q3's stated D2 fallback).
- **Load-time memory peaks** while the authoring `world::World` and the baked
  ECS coexist inside the bake.
- **Nine fixtures and three Runtime test files change** with the load
  outcome. That churn is the price of goldens that actually cover the switch.

## Alternatives Considered

- **Keep Runtime on `world::World`; bake only for tests or tools.** This
  contradicts the maintainer's direction and leaves the ECS without a
  consumer.
- **Bake from `.scene` source or `AuthoringScene`.** Runtime would need source
  and the parser, against the cooked-artifact model and the Android closure
  catalogs.
- **An ECS `Parent` component with a per-frame transform pass.** This keeps
  Spec 0022 whole. But it needs a relation design Spec 0050 excluded, keeps a
  per-frame pass no production path needs, and must share World's private
  math to stay bit-identical. It is deferred to the Spec that first moves
  things at runtime.
- **Order from archetype layout,** or a `BakeOrder` component. The first is
  correct only by coincidence. The second is the fallback once runtime
  creation or destruction exists.
- **Precompute GPU payloads at bake.** The aspect ratio is per frame, and it
  would couple the bake to renderer layouts.
- **Goldens alone, with fixtures kept on a legacy load.** That proves nothing
  about the switch.
