# Spec: Gameplay SDK

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-08
- **Related Plan(s):** none yet. Plan drafting awaits this Spec's Approval.
- **Approval:** pending, in [PR #234](https://github.com/slmao/Atlantis/pull/234). The maintainer set this Spec's goal before drafting
  (2026-10-08, chat):
  - a reflective interface and a generated typed interface, both from the
    one schema;
  - `RuntimeConnection`'s Query, Command, Event and Transaction semantics,
    reused;
  - no Runtime or ECS internal storage exposed;
  - one real client example proving the full operation loop;
  - an explicit ruling on whether custom components, game-logic execution
    and a time model are in scope, without growing into a scripting or
    scheduling system.

  These are recorded under Goals / Non-Goals; the open questions below are
  open to review.
- **Related ADR(s)** (both `Proposed`, drafted alongside):
  - [ADR-0110](../adr/0110-gameplay-sdk-client-library-and-execution-model.md):
    the SDK's module, its two layers over `RuntimeConnection`, and where
    game logic runs (Q1, Q2, Q5–Q8);
  - [ADR-0111](../adr/0111-schema-generated-typed-bindings.md): how the
    typed interface is generated from the schema, and checked against a
    running Runtime (Q3, Q4).

  They keep [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md)
  and [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md)
  (the SDK is a client of their interfaces, which do not change), keep
  [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md) D4
  (descriptor tables stay hand-authored; the generator only reads them), and
  keep [ADR-0103](../adr/0103-runtime-world-operation-boundary.md),
  [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) and
  [ADR-0004](../adr/0004-phase1-threading-baseline.md) unchanged. ADR-0111
  settles, for C++, the item
  [ADR-0034](../adr/0034-stable-public-boundary-versus-internal-cpp-layout.md)
  left open ("whether/how a future SDK is generated from schema versus
  hand-written").

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Three clients reach a running World today: the CLI (Spec 0054/0055), the
editor (Spec 0056) and the tests. Each speaks `RuntimeConnection` directly
in schema ids and `PropertyValue` variants. A gameplay programmer would
have to do the same: hash `"world::Light.intensity"` by hand, build a
`std::variant`, and remember that a point light is nine commands in a
certain order. This Spec adds the **Gameplay SDK**, a C++ client library
with two layers over the same connection and the same schema:

- a **reflective** layer, which addresses types and fields by name or id at
  run time;
- a **generated typed** layer: value structs, enums and field handles
  produced from the schema by a generator, so `Light.intensity` is a
  compile-time `float` and a misspelt field does not compile.

Both layers reuse `RuntimeConnection`'s Query, Command, Event and
Transaction semantics. They add no verb and expose no Runtime or ECS
storage. One real client, an example executable attached to
`atlantis_runtime --listen`, runs the full loop.

The SDK is a **client library**. Game logic runs in the client's own
control flow, between the Runtime's frames. The SDK has no callbacks inside
the Runtime, no scheduler, no time model beyond the frame boundary, and no
custom components (Q5–Q7).

**North star:** a client process holding no Runtime C++ object connects to
the default scene and checks its generated bindings against the Runtime's
schema. It then:

1. finds the Directional light through a typed query;
2. spawns a Point "beacon" light as one typed transaction;
3. for several stepped frames, moves the beacon and pulses its intensity
   from the frame number, and each frame's data shows exactly the written
   values;
4. sends a deliberately invalid transaction, which changes nothing and
   returns one failure by ticket;
5. captures an image that shows the beacon;
6. destroys the beacon, and the frame data returns to the original lights.

## Motivation / Problem Statement

### What exists

- **The connection** (Spec 0054, ADR-0105): `RuntimeConnection` — Query
  (`findEntity`, `listEntities`, `listComponents`, `getProperty`, `schema`),
  Command (`submit` of the five commands), Transaction
  (`submitTransaction`), Event (per-connection pull subscriptions,
  `drainEvents`) and ticket-routed `drainFailures`. Values only. Two
  implementations: InProcess (Runtime-owned) and Remote (Spec 0055).
- **Control** (Spec 0055, ADR-0106): `RuntimeControl` — `status`
  (paused, frame, scene), `pause`, `resume`, `step` (returns the
  `FrameReport`: exact frame data, optional captured PNG), `diagnostics`.
- **The schema** (Spec 0048, ADR-0099): `worldSchema()`, eight hand-authored
  `constexpr` descriptors:
  - five components: `Transform`, `Camera`, `Light`, `Renderable`,
    `WorldMatrix`;
  - two nested structs: `CameraFog`, `CameraBloom`;
  - one enum: `LightKind`.

  Every component field is `Editable`. One field is `Optional`
  (`Renderable.materialAsset`) and two are `AssetReference`. `TypeId` and
  `FieldId` are FNV-1a-64 of the qualified names, so a client can compute
  them at compile time (`schema::fieldId` is `constexpr`). Each type has a
  `schemaVersion`.
- **Client text** (Spec 0054, `connection::text`): one grammar for paths
  (`Light.intensity`, `Camera.fog.density`), `leavesOf`, `parseValue` and
  `formatValue`, shared by the CLI and the editor.

### What a gameplay client lacks

Found in the code while drafting:

- **No typed access.** A read is
  `getProperty({guid, typeId("world::Light"), fieldId("world::Light",
  "intensity")})`, returning a `Result<std::variant<…8 alternatives…>>`. A
  wrong kind is a run-time `KindMismatch` refusal, one frame later. The
  smoke test builds a point light as nine hand-assembled commands
  (`tests/runtime/runtime_smoke_gpu_tests.cpp`, the Plan 0053 M5
  transaction).
- **Order matters and is easy to get wrong.** `Light{}` defaults to
  `Directional`, and a `Light` + `WorldMatrix` holder counts toward the
  one-Directional limit (Plan 0052 Correction J1). So a new point light must
  add `Light`, set `kind = Point`, and only then add `WorldMatrix`.
- **No component-level operations.** Reading a whole `Light` is four
  `getProperty` calls. Adding one with values is an `AddComponent` plus one
  `SetProperty` per leaf.
- **No check that a client's idea of the schema is the Runtime's.** The
  Remote client receives the Runtime's schema at connect, with ids, kinds,
  flags and `schemaVersion` (`src/remote/src/codec.cpp`), but nothing
  compares it with what a client was compiled against.
- **The generation question is open.** ADR-0099 D4 rejected codegen *for
  the descriptor tables* (they stay hand-authored, with drift tests).
  ADR-0034 explicitly left "whether/how a future SDK is generated from
  schema versus hand-written" to a future Spec. This is that Spec.

### Facts the SDK must respect

- **`WorldMatrix` is render-authoritative; `Transform` moves nothing**
  (Spec 0052 ruling Q8, T-a). The baked world is flat (Spec 0051 H1).
- **Entities have no name** (Spec 0055 N-a): an entity is its GUID and its
  components.
- **Commands apply at the start of the next frame** (Spec 0052), and are
  held while paused (Spec 0055). Every call is single-threaded (ADR-0004).
- **Remote calls are answered at frame boundaries** (ADR-0106 D1). Measured
  while drafting (Release, default scene at vsync, `atlantis repl` over
  `atlantis_runtime --listen`, 59 sequential `property get`): **about
  33 ms per call** (about two 60 Hz frames). Bistro's frame time is about
  130 ms (Spec 0056 measurements).

## Goals

Maintainer-set (2026-10-08):

- **One schema, two interfaces:** a reflective interface and a generated
  typed interface, both derived from the World schema the Runtime serves.
- **`RuntimeConnection`'s semantics reused:** Query, Command, Event and
  Transaction exactly as Specs 0052–0054 define them. No new verb, event,
  query or apply rule.
- **No internal storage exposed:** values only. No ECS handle, archetype,
  component C++ type, byte offset, pointer or reference into the Runtime.
- **A real client example** proving the full loop (North star).
- **Scope stated explicitly** for custom components, game-logic execution
  and the time model (Q5–Q7); no scripting system, no scheduling system.

Goals of this Spec within those boundaries:

- **Zero behaviour change in the Runtime:** no change to Runtime, World,
  Connection or Remote; every existing test and golden unchanged.

## Non-Goals

- **A scripting system.** No embedded language (Luau, C#, Python), no
  hot-reload, no script assets, no visual scripting. C# (0058) and Python
  (0061) are named only.
- **A scheduling system.** No update phases, systems, job graph or ordering
  inside the Runtime's frame. ADR-0004 unchanged.
- **Package / plugin loading** (0059): no loading of client code into the
  Runtime process.
- **Headless simulation** (0060), and running the Runtime without a window.
- **New World capabilities.** No new commands, events or queries; no
  change to `RuntimeConnection`, `RuntimeControl` or the wire protocol.
- **A math library.** The SDK carries the schema's value kinds
  (`std::array<float, 3/4>`, …), not vector/quaternion/TRS math.
- **Android hosting.** The SDK library builds for Android (Q10); no Android
  client runs, as Spec 0055 left attaching to an Android Runtime out of
  scope.

## Requirements

### Functional

- **R1 — An ordinary client** (Q1). The SDK reaches the World only through
  a `RuntimeConnection`. It links Atlantis Connection only and includes no
  Runtime, ECS, World component, Platform, RHI, Renderer, Vulkan, Remote or
  OS header (a boundary scan, as Specs 0054–0056 did).
- **R2 — Reflective interface** (Q2). By name or id, against the
  connection's schema:
  - entity queries, including "every entity with these components";
  - property get and set;
  - component-level read, add (with values) and remove;
  - create and destroy;
  - a transaction builder;
  - event subscriptions;
  - failures routed by ticket.
- **R3 — Generated typed interface** (Q3, Q4), covering every type in
  `worldSchema()`:
  - a value struct per struct type (components and nested structs) and a
    C++ enum per enum type;
  - a typed handle for every leaf field, including nested leaves
    (`Camera.fog.density`);
  - typed get/set, component read/add, typed transactions and typed event
    decoding.

  A wrong field kind, or a set on a non-`Editable` field, does not compile.
- **R4 — One semantics** (Q2, Q4). Every SDK operation is a sequence of
  `RuntimeConnection` calls a client could make by hand. The typed layer is
  implemented on the reflective layer. A typed operation and its reflective
  equivalent submit equal `Command` sequences, in the order the client
  wrote them; the SDK never reorders, merges or splits a submission.
  Refusals stay the boundary's: the SDK does not pre-check finiteness,
  light limits or editability (the Spec 0054 "one rule source" principle).
  It resolves names to ids and reports names it cannot resolve.
- **R5 — Schema compatibility** (Q4). Before its first typed operation on a
  type, the SDK compares that type's generated binding with the
  connection's schema: ids, kinds, primitive kinds, `Optional` and
  `Editable` flags, enum constants and `schemaVersion`. On a mismatch,
  every typed operation on that type returns `SchemaMismatch` and submits
  nothing. The reflective layer is unaffected.
- **R6 — No storage exposure.** Generated code encodes names, ids, kinds,
  flags and versions, never `FieldDescriptor::byteOffset` or a World C++
  type. Every value the SDK returns is a copy.
- **R7 — Reproducible, current generation** (Q3). The generator is
  deterministic: the same schema gives byte-identical output. A test fails
  when the committed output differs from what the current `worldSchema()`
  generates. The generator is a host tool and never runs on Android.
- **R8 — The example client** (Q8). An executable that attaches through
  Atlantis Remote's client half and runs the North star's steps. It exits
  0 only if every step's check holds, and prints one line per step.
- **R9 — No Runtime change.** Runtime, World, Connection and Remote sources
  are unchanged. `runFrame()` is byte-identical at every milestone, and
  every golden is unchanged.

### Non-functional

- **Latency:** an SDK call costs what its `RuntimeConnection` calls cost.
  In process that is a function call. Over Remote it is one frame boundary
  per synchronous call (about 33 ms on the default scene, measured above).
  A typed component read of N leaves is N calls (Q9).
- **Threading:** not thread-safe. One thread, between the owner's frames in
  process, or the client's own thread over Remote (ADR-0004, ADR-0106).
- **Dependencies:** none new.
- **Portability:** Windows runs the example and every test. The SDK library
  also builds for Android (`assembleDebug`) if Q10 rules so.
- **Size:** the generated header grows linearly with the schema. Today that
  is eight types and 24 component leaves.

## Proposed Design

Under the recommendations below:

```
worldSchema()  (World, hand-authored, ADR-0099)
   │ read by
   ▼
atlantis_sdk_codegen  (host tool, src/tools/)  ──► committed header
                                                    src/gameplay_sdk/include/atlantis/gameplay/generated/world.h
                                                    (value structs, enums, field handles, bindings)
Atlantis Gameplay SDK  (src/gameplay_sdk/, links Atlantis Connection only)
   typed layer       world::Light, world::fields::Light.intensity, read<Light>, add(Light{…})
        │ implemented on
   reflective layer  gameplay::World over RuntimeConnection&: names/ids, components,
                     queries, Transaction builder, Subscription, failures by ticket,
                     per-type schema compatibility
        │ calls
RuntimeConnection  (unchanged; InProcess or Remote)

examples/gameplay_demo  (atlantis_gameplay_demo)
   links the SDK + atlantis_remote_client; attaches to atlantis_runtime --listen;
   runs the North star; RuntimeControl for step and capture
```

Typed client code, illustratively (Q4 fixes the shape; the Plan fixes the
exact spellings):

```cpp
namespace gp = atlantis::gameplay;
namespace w = atlantis::gameplay::world;          // generated

gp::World world(session.connection());            // borrows the connection
gp::Transaction tx;
tx.create(beacon);
tx.add(beacon, w::Light{.kind = w::LightKind::Point, .color = {1, .6f, .2f},
                        .intensity = 4.0f, .range = 6.0f});
tx.add(beacon, w::WorldMatrix{/* column3 = position */});
const auto ticket = world.submit(tx);             // one TransactionTicket
// … step …
const float i = world.get(beacon, w::fields::Light.intensity).value();
world.set(beacon, w::fields::WorldMatrix.column3, {x, y, z, 1.0f});
```

Without the generated header, the same operations work by name:
`world.get(beacon, "Light.intensity")` and `tx.add(beacon, "Light",
{{"kind", …}, …})`.

## Architectural Impact

Yes. Recorded in two ADRs drafted alongside (both `Proposed`):

- **[ADR-0110](../adr/0110-gameplay-sdk-client-library-and-execution-model.md)**
  covers:
  - a new top-level module, Atlantis Gameplay SDK, a client of Atlantis
    Connection only (Q1);
  - its two layers over `RuntimeConnection` (Q2);
  - the execution model: client-driven logic, no Runtime-hosted callbacks
    or scheduler (Q6);
  - the time model: the frame boundary only (Q7);
  - custom components out of scope (Q5);
  - the example client attaching through Atlantis Remote's client half
    (Q8).
- **[ADR-0111](../adr/0111-schema-generated-typed-bindings.md)** covers:
  - typed bindings generated from the schema by a host tool, with the
    output committed and checked for staleness (Q3);
  - the binding shape and the per-type compatibility check against the
    connected Runtime's schema (Q4).

  It leaves ADR-0099 D4 in force: the descriptor tables are not generated.

**Expected code impact** under the recommendations:

- **New:**
  - `src/gameplay_sdk/` (library, committed generated header) and
    `tests/gameplay_sdk/`;
  - `src/tools/sdk_codegen/` (host tool);
  - `examples/gameplay_demo/`;
  - an in-process GPU test (fatal VVL) and a two-process end-to-end test.
- **Changed:** top-level `CMakeLists.txt` (the new subdirectories);
  `android/app/build.gradle` (`targets` += the SDK library, if Q10).
- **Docs:** AGENTS.md (module list; Atlantis Remote's client-half sentence
  names the example) and `module_boundaries.md` (a Gameplay SDK section),
  with the implementation. Narratives after merge.
- **Unchanged:**
  - Runtime, World (schema tables included), Connection, Remote, the CLI
    and the editor;
  - the wire protocol, `RuntimeConnection` and `RuntimeControl`;
  - goldens.

## Alternatives Considered

The questions below compare their options. Rejected across them:

- **A Runtime-embedded gameplay layer** (behaviours with per-frame update
  callbacks inside `runFrame()`): it is a scheduling system, and it gives
  client code a privileged position in the frame. The maintainer excluded
  it (Q6).
- **Generating the descriptor tables from annotated C++** (macros or a
  parser): it reverses ADR-0099 D4 and is a reflection system the
  repository rejected. The SDK generates *client* code *from* the tables.

## Testing & Verification Plan

- **Generator tests** (no GPU):
  - running the generator on `worldSchema()` reproduces the committed
    header byte for byte (R7);
  - the output is identical across two runs;
  - a synthetic schema table exercises every `PrimitiveKind`, nesting, an
    enum, `Optional`, a non-`Editable` field and a long name.
- **Typed-binding compile checks:**
  - the generated header `static_assert`s every `TypeId` and `FieldId`
    against `schema::typeId` / `schema::fieldId` of its names, using Core
    only;
  - every leaf of every `worldSchema()` type has exactly one handle;
  - negative compile probes (a set with the wrong kind, a set on a
    read-only handle) fail to compile. They run as ctest cases expected to
    fail compilation, an automated form of Plan 0023's C4062 probes.
- **SDK tests** (no GPU, over an InProcess connection on a baked fixture
  scene, as Specs 0054–0056):
  - reflective and typed get/set, component read/add/remove,
    create/destroy and queries;
  - parity: every typed operation submits the same `Command` sequence as
    its reflective equivalent and a hand-written `RuntimeConnection` call
    (R4);
  - transactions are all-or-nothing, with one failure by ticket;
  - subscriptions: typed decoding and RAII unsubscribe;
  - name errors are reported client-side; boundary refusals pass through
    unchanged.
- **Compatibility tests** (R5), with a modified schema span served by a
  test `RuntimeConnection`: a changed `schemaVersion`, a changed kind, a
  missing field or a renamed enum constant each give `SchemaMismatch` for
  that type only, and nothing is submitted.
- **Boundary scan** (R1, R6): the SDK's includes and link list. The
  generated header includes Core's `schema.h` and SDK headers only.
- **The North star, in process** (fatal VVL; `RuntimeApplication` +
  `RuntimeControlHost`, as the 0055/0056 north stars):
  - exact frame data at every step;
  - the image changes when the beacon appears;
  - frame data returns to the original light set after the destroy.

  **Bistro, content-gated:** a typed `Light` query equals the reflective
  one (60 entities), and `6b63b12c-…` goes 12 → 24 exactly through the
  typed layer.
- **The North star, two processes:** `atlantis_runtime --listen 0` plus
  `atlantis_gameplay_demo` in a ctest (the Spec 0055 e2e harness). Exit 0,
  one expected line per step, and a graceful Runtime close.
- **Regression (R9):** Debug and Release full suites at every milestone,
  every golden byte-identical, `runFrame()` byte-identical, zero changes
  under `src/runtime/`, `src/world/`, `src/connection/` and `src/remote/`.
  `assembleDebug` at every milestone.

## Risks & Open Questions

Risks:

- **A committed generated file.** It can go stale when someone edits
  `worldSchema()`. The staleness test (R7) fails the suite with the command
  that regenerates it. Editing the generated header by hand also fails it.
- **Operation order is the client's.** The SDK preserves order (R4), so
  a typed `add(Light{.kind = Point})` *after* `add(WorldMatrix{})` on a scene
  that already has a Directional light is refused (Plan 0052 J1). The SDK
  documents the order and the example follows it. It does not reorder
  silently.
- **Remote latency.** A typed component read over Remote is N frame
  boundaries (Q9). Gameplay loops over Remote should write in transactions
  and observe through events rather than poll.
- **The first generated code in the repository.** Its banner, determinism
  and review rules are set once here (ADR-0111) and reused by later
  bindings (C#, Python).

Open questions — each lists options and a recommendation. Q5–Q7 answer the
maintainer's three explicit scope questions.

- **Q1 — Module placement** (ADR-0110).
  - **(M-a) A new module, Atlantis Gameplay SDK** (`src/gameplay_sdk/`,
    namespace `atlantis::gameplay`, target `Atlantis::GameplaySdk`). It
    links Atlantis Connection only and is boundary-scanned like the CLI and
    the editor. It neither depends on Remote nor is depended on by Runtime.
  - **(M-b) Inside Atlantis Connection.** Connection is the
    transport-neutral interface every client and transport shares. Adding
    a client-side convenience layer and generated code to it grows what
    Remote and Runtime link.
  - **(M-c) A header-only add-on to the CLI or editor.** It ties gameplay
    clients to a tool's library.
  - **Recommendation: M-a.**

- **Q2 — The reflective layer** (ADR-0110).
  - **(R-a) A layer over `RuntimeConnection&`** (borrowed):
    - `gameplay::World` resolves names through Connection's `text` grammar
      (one path syntax for CLI, editor and SDK);
    - it offers component-level read, add and remove, and "entities with
      components" (client-side `listEntities` + `listComponents`, like the
      CLI's `--with`);
    - `Transaction` builds a `std::vector<Command>`;
    - `Subscription` unsubscribes on destruction;
    - failures can be checked per ticket.

    It adds no rule the boundary owns (R4).
  - **(R-b) No reflective layer:** `RuntimeConnection` itself is the
    reflective interface, and the typed layer calls it directly. This is
    smaller. But names, components and transactions would then be rebuilt
    by every client, and the typed layer would have its own second
    implementation of each operation.
  - **Recommendation: R-a.** The typed layer is implemented on it, so the
    two cannot drift (R4).

- **Q3 — How the typed interface is generated** (ADR-0111).
  - **(G-a) A host generator plus a committed output plus a staleness
    test.**
    - `atlantis_sdk_codegen` (`src/tools/sdk_codegen/`, host-only like the
      other tools) links World for `worldSchema()` and writes one header.
    - The header is committed, so the SDK library has no build-time
      generation step and builds on Android unchanged.
    - A test regenerates in memory and compares.
  - **(G-b) Build-time generation** (`add_custom_command`). Android
    cross-builds cannot run a host tool: the SDK would need the ADR-0080
    "cooked on Windows, packaged for Android" path, which exists for assets,
    not code. The output would also no longer be reviewable in a PR.
  - **(G-c) No generator, compile-time lookup** (`get<"Light.intensity">()`
    via a C++20 string template parameter, resolved against a `constexpr`
    table).
    - World's table would have to move into a public header.
    - There would be no named value structs (`read<Light>()` needs member
      names), and error messages would be template diagnostics.
  - **(G-d) Hand-written typed wrappers.** They are not generated, and they
    drift silently: what the maintainer asked to avoid.
  - **Recommendation: G-a.** Generated code is reviewed like source. It is
    proven current by test, and it builds everywhere the SDK does.

- **Q4 — The typed shape and compatibility** (ADR-0111).
  - **Values:**
    - one value struct per schema struct (`world::Light`, with nested
      `world::Camera::fog` of type `world::CameraFog`);
    - one `enum class` per schema enum, with the constants' values;
    - field types are exactly the `PropertyValue` alternatives (`float`,
      `std::array<float, 3/4>`, `std::uint64_t`, GUIDs);
    - `Optional` becomes `std::optional<T>`.
  - **Handles:** one `constexpr` object per component, in a `fields`
    namespace, whose members mirror the canonical path:
    `world::fields::Light.intensity`, `world::fields::Camera.fog.density`.
    - Each handle carries the component's `TypeId`, the leaf's `FieldId`
      and the C++ type.
    - A non-`Editable` leaf gets a read-only handle type.
  - **Component operations:**
    - `read<C>` reads every leaf (N gets) into a `C`;
    - `add(entity, C)` is `AddComponent` then one `SetProperty` per leaf,
      in descriptor order, appended to the caller's transaction. It writes
      every leaf, so a value-initialized `C{}` writes zeros, not World's
      defaults (the schema carries no defaults, ADR-0099 D5). A bare
      `AddComponent` (World's defaults) stays available by type.
  - **Compatibility (R5):**
    - each generated type carries its binding: ids, kinds, flags,
      constants and `schemaVersion`;
    - `World` compares it with `connection.schema()` once per type, on
      first use;
    - on a mismatch that type returns `SchemaMismatch`;
    - in process the check passes by construction. Over Remote it catches
      a client built against another Runtime.
  - **Alternative (S-b):** check every type at construction and refuse the
    whole connection. One unrelated renamed type would then block a client
    that never uses it.
  - **Recommendation:** as above, per-type lazily (S-a).

- **Q5 — Custom components** (maintainer-posed; ADR-0110).
  - **(C-a) Out of scope.** The SDK covers `worldSchema()`'s types only.
    The generator and the reflective layer take any descriptor table, so a
    later Spec that adds component tables reuses both unchanged.
  - **(C-b) In scope:** client-defined component types stored in the
    Runtime World. This needs:
    - run-time component registration in the ECS (component ids are C++
      types today);
    - an operation boundary that accepts types beyond `worldSchema()`;
    - a way to carry those types' schema to the Runtime;
    - scene-format and bake support;
    - persistence.

    That is a Spec with several ADRs of its own.
  - **(C-c) Client-side "components":** SDK-side data keyed by
    `EntityGuid`. It is not in the World, no other client sees it, and it
    is just client state under a misleading name.
  - **Recommendation: C-a**, with C-b named as the follow-up it would be.

- **Q6 — Game-logic execution** (maintainer-posed; ADR-0110).
  - **(X-a) Client-driven.** The SDK is a library; logic runs in the
    client's own control flow:
    - over Remote, in its own process, each call answered at a frame
      boundary;
    - in process, wherever a host calls it between frames (as
      `--exec`/`--editor` do).

    The SDK has no Runtime-side hook, callback or update phase.
  - **(X-b) Runtime-hosted behaviours:** registered callbacks run inside
    `runFrame()`. This is a scheduler, and it changes the frame path.
  - **(X-c) Loading client code into the Runtime** (a DLL/plugin flag): the
    Package/Plugin work (0059).
  - **Recommendation: X-a.** The example's logic is a plain loop in its own
    process.

- **Q7 — The time model** (maintainer-posed; ADR-0110).
  - **(T-a) The frame boundary only.** The SDK defines no clock, no delta
    time and no fixed timestep. A client observes time as frames:
    - `RuntimeStatus::frame`;
    - `FrameReport::frame` after `step`;
    - commands applying at the next frame start.

    The example derives motion from the frame number, which is
    deterministic under `step`.
  - **(T-b) A frame clock query** (frame number plus delta time): the
    Runtime has no delta time or simulation time today. This is a new
    Runtime concept and a `RuntimeControl` change.
  - **(T-c) A full time model** (simulation time, fixed step, time scale,
    pause semantics for logic). It belongs with a scheduler.
  - **Recommendation: T-a.**

- **Q8 — The example client and the loop** (ADR-0110).
  - **(E-a) An out-of-process example**, `examples/gameplay_demo/` →
    `atlantis_gameplay_demo`:
    - it links the SDK and `atlantis_remote_client`, attaches to
      `atlantis_runtime --listen` by session file, and runs the North star
      with `RuntimeControl::step` (capture on the image step);
    - the same loop runs in process in a GPU test, for exact frame data
      under fatal VVL;
    - a two-process ctest runs the executable;
    - Atlantis Remote's client half gains this second linker (an AGENTS.md
      sentence).
  - **(E-b) Hosted in process by a new `atlantis_runtime` flag.** It needs
    a Runtime change for a demo, and it is X-c in miniature.
  - **(E-c) Tests only.** That is not the "real client" the maintainer asked
    for.
  - **Recommendation: E-a.**

- **Q9 — Read batching over Remote.**
  - **(B-a) None in v1.** Typed reads cost N calls (about 33 ms each on the
    default scene). The example writes in transactions and observes
    through events and frame reports.
  - **(B-b) A batch `getProperties` on `RuntimeConnection`.** Remote
    already pipelines it on `RemoteSession` (Spec 0055). It is a new Query
    (an ADR-0105 D1 extension), which the maintainer's "reuse the
    semantics" goal argues against adding here.
  - **(B-c) The SDK takes an optional batch reader** (a `RemoteSession`
    adapter). This couples the SDK's API to a transport.
  - **Recommendation: B-a.** Measure in the example and decide B-b in a
    later Spec if a real client needs it.

- **Q10 — Android.**
  - **(A-a)** `assembleDebug` builds the SDK library (Gradle `targets`), as
    Spec 0056 J11 did for the editor. No Android client runs.
  - **(A-b)** Windows only.
  - **Recommendation: A-a.** The library is portable C++ over Connection;
    building it keeps it honest.

## Out of Scope / Future Work

- Custom components (Q5 C-b): run-time component registration, schema
  extension and bake support.
- A time model or scheduler (Q6 X-b, Q7 T-b/T-c), and Runtime-hosted
  logic.
- A batch query on `RuntimeConnection` (Q9 B-b).
- Generated bindings for other languages (C# 0058, Python 0061) from the
  same generator design.
- Math helpers (TRS composition), an SDK-side entity cache, and Android
  clients.
