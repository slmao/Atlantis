# Spec: Gameplay SDK

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-08
- **Related Plan(s):** none yet. Plan drafting awaits this Spec's Approval.
- **Approval:** pending, in [PR #234](https://github.com/slmao/Atlantis/pull/234). Review rounds 1 and 2 (2026-10-09)
  corrections are folded into this text: Q4, Q7 and Q9, the
  component-operation contracts (read isolation, enum defaults) and an
  attribution. The maintainer set this Spec's goal before drafting
  (2026-10-08, chat):
  - a reflective interface and a generated typed interface, both from the
    one schema;
  - `RuntimeConnection`'s Query, Command, Event and Transaction semantics,
    reused;
  - no Runtime or ECS internal storage exposed;
  - one real client example proving the full operation loop;
  - a scope proposal from the drafter on whether custom components,
    game-logic execution and a time model are in v1, without growing into a
    complete scripting or scheduling system.

  These are recorded under Goals / Non-Goals. Everything else, including
  the answers to the three scope questions (Q5–Q7), is this draft's
  recommendation, open to review.
- **Related ADR(s)** (both `Proposed`, drafted alongside):
  - [ADR-0110](../adr/0110-gameplay-sdk-client-library-and-execution-model.md):
    the SDK's module, its two layers over `RuntimeConnection`, its optional
    query batch, and where game logic runs (Q1, Q2, Q5–Q9);
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

This draft recommends (Q5–Q7, open to review) that the SDK be a **client
library**: game logic runs in the client's own control flow, between the
Runtime's frames. v1 would have no callbacks inside the Runtime, no
scheduler, no clock (a client counts its own logic steps), and no custom
components.

**North star:** a client process holding no Runtime C++ object connects to
the default scene and checks its generated bindings against the Runtime's
schema. It then:

1. finds the Directional light through a typed query;
2. spawns a Point "beacon" light as one typed transaction;
3. pauses the Runtime, then for logic steps `k = 1…K` computes the
   beacon's position and intensity from `k` (its own counter, Q7), submits
   them as one transaction and steps one frame; each stepped frame's data
   shows exactly the values written for that `k`;
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
- **A transport-neutral query batch** (Plan 0055 P9): `cli::QueryBatch`
  (`src/cli/include/atlantis/cli/command_layer.h`) declares
  `listComponents` and `getProperties` over spans.
  - The CLI's default implementation asks one query at a time.
  - The `atlantis` executable supplies an adapter over `RemoteSession`'s
    pipelined queries (`RemoteBatch`, `src/cli/app/main.cpp`).
  - `RemoteSession` answers a batch in input order, equal to the
    one-at-a-time calls at that frame boundary.

  The interface belongs to the client; Connection and the protocol do not
  change.

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
  held while paused (Spec 0055). That application
  (`runtime_application.cpp`, `applyPending()`) is the only place the
  frame writes the World. Every call is single-threaded (ADR-0004).
- **The Runtime's frame number is not a logic clock.**
  `RuntimeControlHost::afterFrame()` increments it every frame, paused or
  not, because pause holds only command application
  (`src/runtime/src/runtime_control_host.cpp`). How many frames pass
  between two client calls depends on Remote waits, so a client cannot
  derive reproducible behaviour from it. It identifies a returned frame.
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
- **A scope proposal** for custom components, game-logic execution and the
  time model (Q5–Q7), without growing into a complete scripting or
  scheduling system.

Goals of this Spec within those boundaries:

- **Zero behaviour change in the Runtime:** no change to Runtime, World,
  Connection or Remote; every existing test and golden unchanged.

## Non-Goals

Maintainer-set (2026-10-08):

- **A complete scripting system.** No embedded language (Luau, C#,
  Python), hot-reload, script assets or visual scripting. C# (0058) and
  Python (0061) are named only.
- **A complete scheduling system.** No systems, job graph or execution
  ordering. ADR-0004 unchanged.

Proposed by this draft (open to review):

- **Runtime-hosted game logic** (Q6): no callbacks, behaviours or update
  phases inside the Runtime's frame.
- **Custom components** (Q5) and **a time model** (Q7).
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
  - failures routed by ticket;
  - batched queries through an optional, SDK-owned, transport-neutral
    `QueryBatch` (Q9): component-field reads and the component filter.
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
- **R5 — Schema compatibility** (Q4). Before the first typed operation
  that uses a type, the SDK compares the type's generated binding with the
  connection's schema.
  - **What is compared:**
    - the type's `TypeId`, kind and `schemaVersion`;
    - its field set exactly: ids, names, kinds, primitive kinds, the
      referenced `TypeId` of each struct or enum field, and the `Optional`
      and `Editable` flags;
    - an enum's constants, by name and value.
  - **Recursively:** every struct or enum type a field references is
    checked the same way (`Camera` → `CameraFog`, `CameraBloom`; `Light` →
    `LightKind`). A type is compatible only if it and every type it
    references are.
  - **On a mismatch:**
    - every typed operation on that type returns `SchemaMismatch` and
      submits nothing;
    - a transaction, typed or mixed, containing any operation on an
      incompatible type is not submitted at all: `submit` returns
      `SchemaMismatch` and takes no ticket;
    - typed event decoding of that type returns `SchemaMismatch`.

  The reflective layer alone is unaffected.
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
- **R10 — Component-operation contracts** (Q4).
  - **A component read is not a snapshot.** `read<C>()` and its reflective
    form issue one `getProperty` per leaf, or one `getProperties` batch
    when a `QueryBatch` is supplied.
    - Over Remote, one-at-a-time queries are answered at successive frame
      boundaries, so commands may apply between them.
    - The batch interface promises answers in input order, not one
      instant: the default asks one at a time, and an adapter may split.
    - The returned value holds each leaf as it was when that leaf was
      answered.
    - **Pausing alone does not isolate a read.** Any client of the same
      Runtime can step or resume it, and a step queued before the read can
      still be running. A multi-leaf read is consistent only if, for the
      whole read:
      - the Runtime stays paused;
      - no client (the reader or any other) calls `step` or `resume`;
      - no step is pending, i.e. requested but not yet completed.

      Command application is the frame's only World write, so under these
      conditions every leaf is answered against the same World state. The
      SDK adds no lock, lease or snapshot to enforce them; coordinating
      control between clients is the clients' business.
  - **`add(entity, C)`** appends `AddComponent` and then one `SetProperty`
    per leaf in descriptor order. It compiles only for a type whose every
    leaf is `Editable`. For a type with a read-only leaf, the client adds
    the bare component (World's defaults) and sets the editable leaves. No
    `worldSchema()` type has a read-only leaf today; a synthetic schema
    covers the rule.
  - **Defaults:** generated value structs are value-initialized, not set
    to World's member defaults; the schema carries no defaults (ADR-0099
    D5).
    - Numbers and vectors become zeros, optionals become empty.
    - An enum member becomes **underlying value 0**. That is not "the
      first enumerator": it equals whichever constant has the value 0, if
      any, and corresponds to no constant when none has it.
    - Today `LightKind::Directional` is 0, so `w::Light{}.kind` is
      `Directional`. A future enum without a 0 constant would make `C{}`
      carry an undeclared value, and `add(entity, C{})` would be refused
      by the boundary (`EnumValueOutOfRange`), aborting its transaction.
    - So `add(entity, C{})` writes zeros (and enum value 0), while a bare
      add gives World's defaults (for `Light`: colour 1, 1, 1 and
      intensity 1).

### Non-functional

- **Latency:** an SDK call costs what its `RuntimeConnection` calls cost.
  In process that is a function call. Over Remote it is one frame boundary
  per synchronous call (about 33 ms on the default scene, measured above).
  A component read of N leaves is N calls, or one pipelined batch when the
  client supplies a `QueryBatch` (Q9). The component filter is one
  `listEntities` plus one `listComponents` per entity, or one batch.
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
                     recursive per-type schema compatibility
        │ calls                       │ batched reads (optional)
RuntimeConnection                gameplay::QueryBatch  (SDK-owned; default: one at a time)
 (unchanged; InProcess or Remote)

examples/gameplay_demo  (atlantis_gameplay_demo)
   links the SDK + atlantis_remote_client; attaches to atlantis_runtime --listen;
   supplies a QueryBatch adapter over RemoteSession (as `atlantis` does for the CLI);
   runs the North star; RuntimeControl for pause, step and capture
```

Typed client code, illustratively (Q4 fixes the shape; the Plan fixes the
exact spellings):

```cpp
namespace gp = atlantis::gameplay;
namespace w = atlantis::gameplay::world;          // generated

RemoteBatch batch(session);                       // the example's adapter
gp::World world(session.connection(), &batch);    // borrows both; batch optional
gp::Transaction tx;
tx.create(beacon);
tx.add(beacon, w::Light{.kind = w::LightKind::Point, .color = {1, .6f, .2f},
                        .intensity = 4.0f, .range = 6.0f});
tx.add(beacon, w::WorldMatrix{/* column3 = position */});
const auto ticket = world.submit(tx);             // one TransactionTicket (or SchemaMismatch)
control.pause();
for (int k = 1; k <= K; ++k) {                    // the client's own logic step (Q7)
  gp::Transaction move;
  move.set(beacon, w::fields::WorldMatrix.column3, positionAt(k));
  move.set(beacon, w::fields::Light.intensity, intensityAt(k));
  world.submit(move);
  // step(1) -> the FrameReport's point light equals positionAt(k), intensityAt(k)
}
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
  - its two layers over `RuntimeConnection` (Q2), and its optional,
    SDK-owned, transport-neutral `QueryBatch` (Q9);
  - the execution model: client-driven logic, no Runtime-hosted callbacks
    or scheduler (Q6);
  - the time model: no clock; the client counts its own logic steps, and
    the Runtime's frame number only identifies frames (Q7);
  - custom components out of scope (Q5);
  - the example client attaching through Atlantis Remote's client half
    (Q8).
- **[ADR-0111](../adr/0111-schema-generated-typed-bindings.md)** covers:
  - typed bindings generated from the schema by a host tool, with the
    output committed and checked for staleness (Q3);
  - the binding shape, the component-operation contracts, and the
    recursive per-type compatibility check against the connected Runtime's
    schema (Q4).

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
  callbacks inside `runFrame()`): it is the start of a scheduling system,
  and it gives client code a privileged position in the frame. This draft
  recommends not including it in v1 (Q6, open to review).
- **Generating the descriptor tables from annotated C++** (macros or a
  parser): it reverses ADR-0099 D4 and is a reflection system the
  repository rejected. The SDK generates *client* code *from* the tables.

## Testing & Verification Plan

- **Generator tests** (no GPU):
  - running the generator on `worldSchema()` reproduces the committed
    header byte for byte (R7);
  - the output is identical across two runs;
  - a synthetic schema table exercises every `PrimitiveKind`, nesting, an
    enum, `Optional`, a non-`Editable` field and a long name;
  - **enum values:** synthetic enums cover non-contiguous values, a
    negative value, a first constant that is not 0, and no 0 constant at
    all.
    - Each generated enumerator carries exactly its descriptor's value,
      never its ordinal.
    - Each enum's binding records whether 0 is a declared constant, and
      the test checks that flag against the descriptors.
    - A compile-time check shows that a value-initialized member of each
      enum has underlying value 0.
    - For `worldSchema()`, `w::Light{}.kind == w::LightKind::Directional`
      holds because `Directional` is 0; the test fails if that stops being
      true.
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
- **Batch tests** (R2, Q9):
  - the default `QueryBatch` returns exactly the one-at-a-time results;
  - a recording `QueryBatch` shows that a component read is one
    `getProperties` call and the component filter one `listComponents`
    call;
  - over Remote loopback (the Spec 0055 `whileWaiting` harness), the
    example's `RemoteSession` adapter returns the same values as the
    default.
- **Component-contract tests** (R10):
  - `add(e, w::Light{})` writes zeros, while a bare add gives World's
    defaults;
  - read isolation, over Remote loopback with two clients (the Spec 0055
    `whileWaiting` harness):
    - with the Runtime paused, client B queues a command and **steps once
      in the middle of** client A's one-at-a-time `read<C>`. A's result
      mixes leaves from before and after the applied command, so pausing
      alone does not isolate a read;
    - the same holds when B calls `resume` mid-read, and when a step
      requested before A's read is still pending as it starts;
    - with the Runtime paused, no `step` or `resume` from any client, and
      no pending step, A's read returns one consistent component;
  - `add(entity, C)` for a synthetic type with a read-only leaf fails to
    compile (a probe).
- **Compatibility tests** (R5), with a modified schema span served by a
  test `RuntimeConnection`:
  - a changed `schemaVersion`, kind, referenced `TypeId` or `Editable`
    flag, a missing or extra field, or a renamed enum constant each give
    `SchemaMismatch`;
  - a change only to a referenced type (`CameraFog`'s version,
    `LightKind`'s constants) makes the referencing component (`Camera`,
    `Light`) incompatible;
  - unrelated types stay usable;
  - a transaction mixing a compatible and an incompatible type is not
    submitted: no ticket, no command reaches the connection.
- **Boundary scan** (R1, R6): the SDK's includes and link list. The
  generated header includes Core's `schema.h` and SDK headers only.
- **The North star, in process** (fatal VVL; `RuntimeApplication` +
  `RuntimeControlHost`, as the 0055/0056 north stars):
  - exact frame data at every logic step `k` (paused, one step per `k`);
  - the same values on a repeated run, whatever the frame numbers;
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
- **Remote latency.** Without a `QueryBatch`, a component read over Remote
  is N frame boundaries (Q9). With one it is one pipelined boundary.
  Gameplay loops still write in transactions and observe through events
  and step reports rather than poll.
- **A component read is not a snapshot** (R10). A multi-leaf read can mix
  frames unless the Runtime stays paused with no `step` or `resume` from any
  client and no pending step throughout. The contract says so and adds no
  locking.
  - The example reads only in that state: it is the only client in its
    tests, and it pauses before reading.
  - With other clients attached, isolation is theirs to coordinate.
- **The first generated code in the repository.** Its banner, determinism
  and review rules are set once here (ADR-0111) and reused by later
  bindings (C#, Python).

Open questions — each lists options and a recommendation. Q5–Q7 are the
maintainer's three scope questions; their answers are recommendations, not
rulings.

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
  - **(G-d) Hand-written typed wrappers.** They drift silently from the
    schema, and the maintainer asked for a generated interface.
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
  - **Component operations** (R10):
    - `read<C>` reads every leaf into a `C`: N gets or one batch. It is not
      a snapshot, and pausing alone does not isolate it (R10);
    - `add(entity, C)` is `AddComponent` then one `SetProperty` per leaf,
      in descriptor order, appended to the caller's transaction. It is
      available only when every leaf is `Editable`. A value-initialized
      `C{}` writes zeros, and enum value 0, which matches a declared
      constant only if one has the value 0;
    - a bare `AddComponent` (World's defaults) stays available by type.
  - **Compatibility (R5):**
    - each generated type carries its binding: ids, kinds, flags,
      referenced `TypeId`s, constants and `schemaVersion`;
    - `World` compares it, and recursively every type it references, with
      `connection.schema()` once per type, on first use;
    - on a mismatch that type's operations return `SchemaMismatch`, and so
      does any transaction containing one, which is then not submitted;
    - in process the check passes by construction. Over Remote it catches
      a client built against another Runtime.
  - **Alternative (S-b):** check every type at construction and refuse the
    whole connection. One unrelated renamed type would then block a client
    that never uses it.
  - **Recommendation:** as above, per-type lazily (S-a).

- **Q5 — Custom components** (maintainer-posed question; the answer is this draft's recommendation; ADR-0110).
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

- **Q6 — Game-logic execution** (maintainer-posed question; the answer is this draft's recommendation; ADR-0110).
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

- **Q7 — The time model** (maintainer-posed question; the answer is this draft's recommendation; ADR-0110).
  - **(T-a) No clock; the client's own logic step.** The SDK defines no
    clock, no delta time and no fixed timestep.
    - **Logic time:** a client advances logic in its own steps `k` and
      computes state from `k`.
    - **Ordering:** commands apply at the next frame start, and a step
      releases exactly one frame's application while paused.
    - **Frame numbers:** the Runtime's frame number
      (`RuntimeStatus::frame`, `FrameReport::frame`) identifies the frame a
      report describes. It is not a logic clock: it advances while paused,
      and how many frames pass between client calls depends on Remote
      waits (Motivation, Facts).
    - **The example:** it pauses, then for each `k` submits the
      transaction computed from `k`, steps one frame, and checks that
      frame's report. The run is reproducible whatever frame numbers it
      sees, provided no other client resumes or steps the Runtime during
      it (R10).
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
      `atlantis_runtime --listen` by session file, supplies a `QueryBatch`
      adapter over `RemoteSession`, and runs the North star with
      `RuntimeControl` pause and `step` (capture on the image step);
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

- **Q9 — Query batching** (ADR-0110).
  - **(B-a) None in v1.** Component reads cost N calls (about 33 ms each on
    the default scene), and the component filter costs one call per entity
    (5969 on Bistro).
  - **(B-b) A batch query on `RuntimeConnection`.** Remote already
    pipelines it on `RemoteSession` (Spec 0055). It is a new Query (an
    ADR-0105 D1 extension) and a Connection change, which this Spec's
    "reuse the semantics" goal argues against.
  - **(B-c) An SDK-owned, transport-neutral `QueryBatch`**, after the CLI's
    `cli::QueryBatch` precedent (Plan 0055 P9):
    - **Interface:** `gameplay::QueryBatch`, belonging to the SDK and
      naming only Connection's value types. `listComponents` and
      `getProperties` take spans and return results in input order.
    - **Default:** one query at a time over the `RuntimeConnection`.
    - **Remote:** the example supplies a ~15-line adapter over
      `RemoteSession`, as `atlantis` does in `src/cli/app/main.cpp`.
    - **Scope:** the SDK uses it for component-field reads (one
      `getProperties` per component read) and for the component filter
      (one `listComponents` for all listed entities).
    - **No new contract:** Connection and the protocol are unchanged, the
      SDK does not depend on Remote, and the batch makes no snapshot or
      isolation promise (R10).
  - **(B-d) Share `cli::QueryBatch`** by moving it into Connection. This
    changes Connection for a client-side seam. Two identical small
    interfaces in two client libraries can be unified later if a third
    client needs one.
  - **Recommendation: B-c.**

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
- A batch query on `RuntimeConnection` (Q9 B-b), and unifying the CLI's
  and the SDK's `QueryBatch` (Q9 B-d).
- Generated bindings for other languages (C# 0058, Python 0061) from the
  same generator design.
- Math helpers (TRS composition), an SDK-side entity cache, and Android
  clients.
