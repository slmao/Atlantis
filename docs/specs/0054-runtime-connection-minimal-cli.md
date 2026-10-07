# Spec: RuntimeConnection and a Minimal CLI Client

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-07
- **Related Plan(s):** none yet. Drafting a Plan is authorized only after this
  Spec's Approval.
- **Approval:** pending. The maintainer fixed this Spec's position, scope and
  boundaries before drafting (2026-10-07, chat). They are recorded under
  Goals / Non-Goals and are not open questions:
  - the first step of Phase 2B, "Machine-operable Runtime";
  - the connection exposes exactly Query / Command / Event / Transaction;
  - InProcess is the only implementation;
  - a minimal CLI with a fixed command list;
  - IPC, the Editor and the rest are named only.
- **Related ADR(s):**
  [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md) (`Proposed`,
  drafted alongside this Spec). It records:
  - the connection's shape and placement;
  - the CLI as an ordinary client and how it is hosted;
  - the address text form;
  - the subscription model;
  - the enumeration query added to ADR-0103 D1's Query set (D1 says
    "Additions are later Specs").

  It applies [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md)
  and keeps [ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) and
  [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

A **RuntimeConnection** is the one interface through which a client reaches
a running Runtime World. It exposes exactly the four existing concepts:

- Query;
- Command;
- Event;
- Transaction.

It adds nothing a transport would need to invent. Its first and only
implementation in this Spec is **InProcess**, owned by Runtime and
forwarding to Spec 0052/0053's `RuntimeWorldAccess`.

On top of it sits a **minimal CLI client** with the maintainer's six
commands. The CLI includes and links nothing of Runtime's internals; it
knows only the connection.

The north star is a chain that holds on a running, rendering scene:

> CLI → Connection → World API → ECS → render extraction → Renderer

The acceptance image: `entity set <light> Light.intensity 6` on the default
scene, and the scene being rendered visibly brightens on the next frame.

## Motivation / Problem Statement

### Why now: the start of Phase 2B

Specs 0047–0053 built every piece a machine needs to operate the engine:

| Spec | Piece |
|---|---|
| 0047 | stable GUIDs |
| 0048 | the schema |
| 0050 | the ECS |
| 0051 | the bake into a Runtime World |
| 0052 | a Query / Command / Event boundary |
| 0053 | transactions |

None of it has been driven by anything outside Runtime's own C++. The only
client is the smoke test, through a friend accessor
(`RuntimeSmokeTestAccess::worldAccess`, `runtime_smoke_gpu_tests.cpp:121`).

This Spec proves the design end to end. A client that holds no Runtime
object observes and modifies the running World, and the change reaches the
renderer.

### Debts this Spec inherits

- **No entity listing.** Spec 0052's Risks:
  > The Query list cannot enumerate entities. An Editor needs that, and it is
  > a candidate for 0054 or a later Spec.

  ADR-0103's Consequences repeat it, and the blueprint queues it.
  `world entities` cannot exist without it (Q1).
- **No address text form.** Plan 0052 J7: "none in v1; it is left to 0054".
  The CLI is its first consumer (Q2).
- **One event queue, one failure queue.** `RuntimeWorldAccess` has a single
  event queue and a single failure queue (`drainEvents()` / `drainFailures()`
  move them out). A second client would steal the first's events. ADR-0033
  expects several symmetric clients, including a future Editor, so the
  connection must give each client its own view (Q5).

### What constrains the host (code evidence)

- **The frame loop.** `atlantis_runtime`'s loop is
  `while (app.shouldContinue()) app.runFrame();` (`src/runtime/main.cpp:287-289`).
  The window's messages are pumped only inside `runFrame()`, by
  `platform::processEvents()` (`runtime_application.cpp:1170`). On Windows
  that is a non-blocking `PeekMessageW` drain (`windows_platform.cpp:229`).
  Presentation uses `VK_PRESENT_MODE_FIFO_KHR` (`vulkan_presentation.cpp:429`),
  so frames are vsync-paced. Anything that blocks the frame thread between
  frames freezes the window.
- **Threading.** ADR-0004 fixes a single logical frame thread, and
  `RuntimeWorldAccess` is "not thread-safe: all calls on the frame thread,
  between frames" (`runtime_world_access.h`, class comment).
- **When commands apply.** Commands apply at the first statement of
  `runFrame()` (`runtime_application.cpp:1168`, Spec 0052 J6).
- **Dependency rules.**
  - The executable `atlantis_runtime` links only `Atlantis::RuntimeHost`
    (`src/runtime/CMakeLists.txt`), and no other top-level module may depend
    on `RuntimeHost` (module_boundaries.md, Atlantis Runtime).
  - Nothing may depend on Tools ("no runtime module ever depends on Tools",
    module_boundaries.md, Atlantis Tools).
- **Goldens.** The image-regression goldens are produced by the headless
  fixture under `tests/image_regression/fixture/`, which does not use
  `RuntimeApplication`. So the Runtime-side change in this Spec cannot reach
  them.

## Goals

These are maintainer-fixed and are not to be relaxed in review:

- **Interface first, and only the four concepts.**
  - The connection's semantics are exactly Query, Command, Event and
    Transaction, as Specs 0052/0053 define them.
  - It invents no new Runtime API. Q1's listing query and Q8's schema query
    are Queries, the first already queued by Spec 0052.
  - **The transport must not decide the Runtime API.** IPC is 0055, named
    only, neither designed nor scaffolded.
- **One implementation: InProcess.** It validates the semantic boundary
  first.
- **A minimal CLI in this Spec, with exactly the maintainer's commands:**

  ```
  schema list
  schema inspect <Type>
  world entities
  entity inspect <guid>
  entity get <guid> <Type>.<field>
  entity set <guid> <Type>.<field> <value...>
  ```

  Types are real World schema types: `Light` with its `kind` field, not a
  `PointLight` type.
- **The north star.** CLI → Connection → World API → ECS → render
  extraction → Renderer holds on a running scene: setting a light's
  intensity visibly changes the rendered scene.
- **The CLI is a real external client.** It neither includes nor links
  Runtime's internal objects.
- **Named only, not designed or scaffolded:**
  - IPC (0055);
  - a Minimal Editor (0056): Hierarchy, Inspector, Viewport and Gizmo. The
    Editor is also an unprivileged ordinary client;
  - the Gameplay SDK and C# (0057/0058);
  - Package/Plugin (0059);
  - future Agent capabilities: step runtime, capture frame, read
    diagnostics, compare regression.

Goals of this Spec within those boundaries:

- **Every Query, Command, Event and Transaction is reachable through the
  connection,** with Spec 0052/0053 semantics unchanged: timing, validation,
  events, atomicity.
- **Deterministic, scriptable CLI.** The same script on the same scene
  produces the same output.
- **Zero behaviour change** for every existing Runtime path when no client is
  connected. Goldens are byte-identical.

## Non-Goals

These are maintainer-fixed:

- IPC, sockets, pipes, serialization or a wire protocol (0055);
- a standalone CLI process that attaches to a running Runtime (0055);
- the Editor, the Gameplay SDK, C#, Package/Plugin;
- Agent capabilities: stepping, frame capture, diagnostics, regression
  comparison;
- any new Runtime World verb, event or concept beyond Q1/Q8's Queries.

Also out of scope:

- **Reactive or callback event delivery.** Delivery stays pull-only, as in
  ruling Q4 of Spec 0052.
- **Threads.** No new thread, and no change to ADR-0004 (Q3).
- **An interactive REPL** (Q3: it needs either a thread or non-blocking
  console I/O; named for 0055).
- **Android hosting of the CLI.** `atlantis_runtime` is Windows-only
  (`src/runtime/CMakeLists.txt`, `if(WIN32)`). Android builds the connection
  but has no CLI host.
- **Undo/Redo, nested transactions, transactions across frames** (Spec 0053,
  named only).
- **Authoring-scene editing or saving.** The CLI edits the Runtime World only.

## Requirements

### Functional

- **R1 — The connection** (Q4, Q5, Q8). `RuntimeConnection` exposes:
  - **Query:**
    - `findEntity`, `listComponents`, `getProperty`, as in Spec 0052;
    - the listing query (Q1);
    - the World schema (Q8).
  - **Command:** `submit(Command)` → `CommandTicket`.
  - **Transaction:** `submitTransaction(std::vector<Command>)` →
    `TransactionTicket`.
  - **Event:** subscriptions and their drained events (Q5), and this
    connection's own failures.

  Values, commands, events, tickets and failures are Spec 0052/0053's types,
  unchanged. Nothing returned is a pointer or reference into Runtime-owned
  state (ADR-0033).
- **R2 — Unchanged semantics.**
  - **When commands apply.** A command or transaction submitted through the
    connection applies at the owner's next `applyPending()`, in submission
    order across all connections.
  - **Same outcomes.** It has exactly the validation, events, failures and
    atomicity it would have if submitted to `RuntimeWorldAccess` directly.
- **R3 — InProcess.**
  - **Ownership.** Runtime owns the InProcess endpoint over its
    `RuntimeWorldAccess` (Q7). Connections are opened from it, on the frame
    thread, and must not outlive it.
  - **No thread** is added.
  - **It is the only implementation.**
- **R4 — Per-connection events and failures** (Q5).
  - **Events.** A connection receives the events matching its
    subscriptions, from the moment each subscription is made.
  - **Failures.** It receives the failures of its own submissions only.
  - **No stealing.** Draining on one connection never removes another's
    events.
- **R5 — Entity listing** (Q1). A Query lists every live entity addressable
  through the boundary. The order is deterministic, independent of ECS
  storage order.
- **R6 — Text forms** (Q2).
  - **Canonical text** for an entity GUID, a property path (`<Type>.<field>`
    with nested fields), and a value of each field kind.
  - **Round-trip.** Parsing what `entity get` prints yields the same value.
  - **Unparsable text** is a client-side error, before anything is
    submitted.
- **R7 — The CLI commands.** Exactly the six commands in Goals:
  - **`schema list`:** every World schema type, with its kind.
  - **`schema inspect <Type>`:** its fields (name, kind, flags, nested type)
    or enum constants.
  - **`world entities`:** the R5 listing, one GUID per line, each with its
    component types.
  - **`entity inspect <guid>`:** each component, and every leaf field's value
    in R6 text.
  - **`entity get <guid> <Type>.<field>`:** one value in R6 text.
  - **`entity set <guid> <Type>.<field> <value...>`:** one `SetProperty`.
    Its outcome is reported once applied: the `PropertyChanged` event, or
    the failure with its `AccessError` name.

  Output is line-oriented and deterministic.
- **R8 — The CLI is an external client** (Q4, Q6). The CLI's sources and
  target reach the World only through `RuntimeConnection` and the public
  value types it carries. Specifically, they do not include or link:
  - `Atlantis::RuntimeHost`;
  - any Runtime header;
  - `atlantis/world/ecs/*`;
  - Platform, RHI, the Renderer or the Vulkan backend.

  A test enforces this.
- **R9 — Hosting** (Q3). A CLI script runs against the scene `atlantis_runtime`
  is rendering. Lines execute between frames, on the frame thread, and
  nothing blocks the frame loop once the script is loaded.
- **R10 — No behaviour change when no client is connected** (Q7).
  - `runFrame()` is unchanged.
  - Without the host option, nothing opens a connection, and every existing
    test and golden is unchanged.
- **R11 — The north star.**
  - **The case:** `entity set <the scene's light> Light.intensity <v>` through
    the CLI, on a running windowed `RuntimeApplication`.
  - **Next frame:** the frame's `FrameLightingData` carries the new
    intensity, under fatal Validation Layers.
  - **Human acceptance:** the default scene visibly brightens.

### Non-functional

- **Performance:**
  - the connection forwards; one listing is O(*n*);
  - the fan-out (Q5) is O(events × subscriptions);
  - one CLI line per frame.
- **Memory:** per-connection queues are bounded by what the client leaves
  undrained, as Spec 0052's queue is.
- **Threading:** ADR-0004 single frame thread; no new thread.
- **Portability:**
  - the connection and CLI libraries build on Windows and Android
    (`assembleDebug`);
  - only the Windows executable hosts the CLI.
- **Dependencies:** none external.

## Proposed Design

```
atlantis_runtime --exec script.txt                      (Q3; Windows host)
  main(): app = createRuntimeApplication(...)
          conn = app.openConnection()                   (Q7)  -> RuntimeConnection (Q4)
          cli  = cli::ScriptRunner(conn, lines)         (Q6)  links Atlantis::Connection only (R8)
          while app.shouldContinue():
            app.runFrame()      -- applyPending() first: last line's commands apply, frame renders
            cli.step()          -- report last line's outcomes; run next line (queries read this frame's world)

RuntimeConnection (InProcess)    endpoint owned by RuntimeApplication, beside worldAccess_
  Query        findEntity / listEntities (Q1) / listComponents / getProperty / schema (Q8)
  Command      submit(Command) -> CommandTicket
  Transaction  submitTransaction(vector<Command>) -> TransactionTicket
  Event        subscribe(EventFilter) -> SubscriptionId; drainEvents(id); drainFailures()   (Q5)
```

- **Fan-out (Q5).** The endpoint is the only drainer of `RuntimeWorldAccess`'s
  queues.
  - **When it drains.** On any connection's drain call, it moves the
    boundary's events and failures out and distributes them.
  - **Events** go to every subscription whose filter matches.
  - **Failures** go to the connection whose ticket range contains them,
    recorded at submit.
  - Runtime's frame is unchanged.
- **Text forms (Q2).** They live with the connection, so the CLI and a future
  transport share one definition. CLI syntax is their first consumer.

## Architectural Impact

Yes. Recorded in
[ADR-0105](../adr/0105-runtime-connection-and-cli-client.md), drafted
alongside:

- the connection interface (exactly the four concepts) and its InProcess
  implementation;
- module placement (Q4, Q6), with two new top-level modules under the
  recommendations;
- the address text form (Q2);
- the subscription and fan-out model (Q5);
- the hosting decision, with no thread and no ADR-0004 change (Q3);
- `RuntimeApplication` owning the endpoint (Q7);
- the listing query, extending ADR-0103 D1's Query set ("Additions are later
  Specs"; no supersession).

**Expected code impact**, for the Plan, under the recommendations:

- **New:**
  - `src/connection/` (interface, InProcess endpoint, text forms);
  - `src/cli/` (command layer and script runner);
  - their tests;
  - a boundary-scan test (R8).
- **World:** `RuntimeWorldAccess` gains the listing query (Q1). Its header
  is already the allowed GUID header.
- **Runtime:**
  - `runtime_application.h/.cpp`: the endpoint member and
    `openConnection()`;
  - `main.cpp` and `cli.h/.cpp`: `--exec`;
  - `src/runtime/CMakeLists.txt`: link Connection and CLI.

  `runFrame()` is unchanged.
- **Docs:**
  - the module_boundaries.md sections for the new modules, and the
    Runtime/World dependency lines;
  - AGENTS.md's module list, if the new modules are approved.
- **No change** to:
  - the ECS core;
  - render extraction, the Renderer, RHI, the Vulkan backend, Platform;
  - formats, shaders, assets, goldens.

## Alternatives Considered

The seven questions below compare the options. Rejected across all of them:

- **Clients using `RuntimeWorldAccess` directly, with no connection.** There
  would be no place for a transport, no per-client event view, and no
  boundary to hold the CLI to (R4, R8).
- **Designing IPC now.** Forbidden. It would let the transport shape the API.
- **A privileged in-engine console.** It would sit inside Runtime with
  internal access, which contradicts ADR-0033 and the maintainer's "real
  external client".

## Testing & Verification Plan

- **Connection, CPU-only** (over a baked catalog scene, no GPU):
  - **Equivalence:** every Query matches `RuntimeWorldAccess`; commands and
    transactions submitted through it equal direct submission (events,
    failures, world state) (R1, R2).
  - **Two connections:**
    - each sees only its own failures;
    - events reach every matching subscription, and only from subscription
      time on;
    - one connection's drain does not affect the other (R4).
  - **Listing:** complete, deterministic, and it follows create and destroy
    (R5).
- **Text forms:**
  - parse/format of each field kind;
  - round-trip of every leaf of every World component;
  - nested paths, short and qualified type names;
  - errors for an unknown type, field or enum constant and for the wrong
    number of values (R6).
- **CLI, CPU-only:** each of the six commands, over InProcess on a baked
  scene, against expected output text. A refused `set` reports the
  `AccessError` name (R7).
- **Boundary (R8):** a source scan of `src/cli/` (includes) and a CMake check
  of its link list.
- **North star (R11):** a GPU test in the smoke-test style, under fatal VVL:
  - **Setup:** a windowed `RuntimeApplication`, a connection, and the CLI
    `entity set` on the scene's light;
  - **Check:** after `runFrame()`, `FrameLightingData` carries the new
    intensity;
  - **Human acceptance:** `atlantis_runtime --exec` on the default scene,
    with before/after screenshots.
- **Regression (R10):**
  - full Debug + Release suites, every golden byte-identical;
  - Android `assembleDebug`;
  - the existing smoke tests unchanged.

## Risks & Open Questions

Risks:

- **Two new modules for a small feature,** under the Q4/Q6 recommendations.
  They hold the boundary the maintainer asked for; a lighter merge is noted
  in Q6.
- **Text forms becoming a de-facto wire format.** They are defined as client
  text, and 0055 decides its own encoding (Q2).
- **Script pacing is one line per frame.** A long script takes many frames.
  That is acceptable for v1, and deterministic.
- **The CLI can make inert edits.** It can set `Transform`, which does not
  render (Spec 0052 ruling Q8); the CLI prints values, not their effect.

Open questions (to be ruled at review). Q1–Q7 were posed by the maintainer;
Q8 surfaced while drafting.

- **Q1 — Listing entities.** `world entities` needs it. Spec 0052's Risks
  named it, and the blueprint queues it.
  - **Options:**
    - **(L1)** A new Query on `RuntimeWorldAccess`, `listEntities()` →
      `std::vector<EntityGuid>`. It returns every live entity in the GUID
      index, sorted by GUID value, in O(*n*).
    - **(L2)** An enumeration form of FindEntity: `findEntities(EntityFilter
      { required component TypeIds })`, with the empty filter meaning all.
      It is more general (an Editor's "all lights").
    - **(L3)** The CLI lists entities without the World:
      - by tracking `EntityCreated`/`EntityDestroyed` from a seed it cannot
        obtain;
      - or by reading the scene source, which misses runtime-created
        entities and is authoring data, not the Runtime World.
  - **Recommendation: L1.**
    - It is the smallest Query that closes the queued debt. It changes no
      existing semantics and enters through ADR-0103 D1's "Additions are
      later Specs".
    - **Listing scope.** It lists exactly what the boundary can address. An
      ECS entity without a GUID is not listed, by the same rule that makes
      it unaddressable.
    - L2's filter is a later extension when a consumer (0056) needs it. L3
      cannot be correct.
- **Q2 — The address text form** (Plan 0052 J7's debt).
  - **Entity.** The existing canonical GUID text: lowercase RFC 9562
    8-4-4-4-12 (`parseEntityGuid` / `toString`, `asset_guid.h:43-47`). No
    choice is needed.
  - **Type** — options:
    - **(T1)** the qualified schema name, `world::Light`;
    - **(T2)** the short name, `Light`, resolved among the World schema's
      component types by unqualified name. Today all are under `world::`
      and unique. A uniqueness test pins that, and the qualified name is
      also accepted.
  - **Field** — options:
    - **(F1)** the path of field names from the component to the leaf:
      `Light.intensity`, `Camera.fog.density`. It maps to `(TypeId, leaf
      FieldId)` per Spec 0049 J8;
    - **(F2)** the leaf name alone: `Camera.density`. It is ambiguous as
      structs grow.
  - **Values** (one shape per kind):

    | Kind | Text |
    |---|---|
    | `Float32` | a decimal |
    | `Vec3Float32` / `Vec4Float32` | 3 / 4 decimal arguments |
    | `UInt64` | a decimal |
    | enum | its constant name (`Point`) |
    | `AssetGuid` / `EntityGuid` | GUID text |
    | an absent `Optional` | `none` |

    Finiteness, range and editability stay the boundary's to refuse, so
    there is one rule source.
  - **Joined form** — options:
    - **(J-i)** two tokens, `<guid> <Type>.<field>`, as the CLI uses;
    - **(J-ii)** also a single-token joined form for a future wire.
  - **Recommendation: T2 (qualified also accepted) + F1 + the value table +
    J-i.**
    - The text lives in the connection module (Q4), so 0055 can reuse it.
    - The wire encoding remains 0055's, and the text is not a protocol. So
      the transport does not decide the API.
- **Q3 — How the CLI operates on a running Runtime without IPC.** It must
  coexist with the window on one thread (ADR-0004).
  - **Options:**
    - **(H-a) `atlantis_runtime --exec <file|->`.**
      - **Loading.** The script is read whole at startup, from the file or
        from stdin until EOF.
      - **Pacing.** Each line runs between frames, after `runFrame()`
        returns. Queries read the world the frame just rendered. A `set`
        submits, and the next `runFrame()` applies it first (J6), then
        renders it. Its outcome prints after that frame.
      - **No blocking.** Nothing blocks after startup, so the window stays
        responsive.
      - **After the last line,** the Runtime keeps rendering until the
        window is closed, so the result can be seen.
    - **(H-b) A blocking REPL on the frame thread.** While it waits for
      input, `processEvents()` is not called, and that is the only message
      pump (non-blocking `PeekMessageW`, `windows_platform.cpp:229`;
      `runtime_application.cpp:1170`). The window stops responding and
      presentation stalls.
    - **(H-c) A REPL polling stdin without blocking, once per frame.** It
      needs console/pipe peeking (Windows `PeekConsoleInput` /
      `PeekNamedPipe`), new OS-specific code that belongs in Platform, a
      module only Runtime may use.
    - **(H-d) A REPL on a stdin reader thread** handing lines to the frame
      thread. That is a second thread and a cross-thread queue. It changes
      ADR-0004's model and `RuntimeWorldAccess`'s "frame thread only"
      contract, so it needs its own ADR.
    - **(H-e) A standalone CLI process.** It cannot reach a running Runtime
      without IPC (0055).
  - **Recommendation: H-a.**
    - **Why it fits.** It runs against the scene being rendered (the north
      star), adds no thread and no OS-specific I/O, and is deterministic
      (scripts double as acceptance tests). It still uses Spec 0052's
      application point exactly.
    - **What it leaves out.** Interactive use comes with 0055, where a
      separate CLI process over IPC is naturally interactive. H-c/H-d are
      named there.
- **Q4 — Where the connection lives, and the client's dependency surface.**
  - **Options:**
    - **(M-a)** In Atlantis World, beside `access`
      (`atlantis::world::access::RuntimeConnection`). It is the smallest
      option: the InProcess implementation only forwards to
      `RuntimeWorldAccess`. But it puts a client/transport contract in the
      module that owns the world, and 0055's transport code would have to
      implement a World interface from outside.
    - **(M-b)** A new top-level module **Atlantis Connection**
      (`src/connection/`, `atlantis::connection`):
      - **Contents:** the interface, the InProcess endpoint, the text forms
        (Q2);
      - **Depends on:** Core and World's public access headers;
      - **Depended on by:** Runtime (it owns the endpoint) and clients.
    - **(M-c)** Tools. Rejected: Runtime must own the endpoint, and nothing
      may depend on Tools.
  - **Client surface (any option).** The CLI links Atlantis::Connection,
    reaching World, Core and Asset System only transitively, for value
    types. It must not link `Atlantis::RuntimeHost` (already forbidden to
    other modules) or include Runtime or `world/ecs` headers (R8).
  - **Recommendation: M-b.** The client contract gets one home that is
    neither the world's owner nor its host. 0055 adds a transport there,
    and the World module stays free of connection concerns.
- **Q5 — The v1 shape of Subscription / EventFilter** (pull-only, not
  reactive).
  - **Options:**
    - **(S-a)** No subscription: the connection exposes `drainEvents()` /
      `drainFailures()` as the boundary does. It has a single consumer, so
      a second connection steals events (the inherited debt).
    - **(S-b) Per-connection subscriptions, pull-only.**
      - **Subscribing:** `subscribe(EventFilter)` → `SubscriptionId`, then
        `drainEvents(SubscriptionId)` and `unsubscribe`.
      - **Filter:**

        ```
        EventFilter { kinds: subset of the five event kinds (default all);
                      entity: optional EntityGuid;
                      component: optional TypeId }
        ```

        It matches on the event's own fields.
      - **Delivery:** events arrive from subscription time on, and there is
        no history.
      - **Failures:** `drainFailures()` returns this connection's own,
        routed by ticket.
      - **Mechanism:** the endpoint is the boundary's only drainer and fans
        out on demand.
    - **(S-c) Callbacks.** Reactive, which is forbidden.
  - **Recommendation: S-b.** It is the smallest shape that makes several
    symmetric clients correct (ADR-0033; the Editor 0056 is an ordinary
    client). It needs no frame change and keeps Spec 0052's non-reactive
    pull. The CLI subscribes to `PropertyChanged` on the entity it sets.
- **Q6 — Where the CLI is built.**
  - **Options:**
    - **(B-a)** A new top-level module **Atlantis CLI** (`src/cli/`,
      `atlantis::cli`; distinct from `atlantis::runtime::cli`, Runtime's
      startup flags):
      - **Contents:** a library holding command parsing, execution over a
        `RuntimeConnection&`, output formatting and the script runner;
      - **Dependencies:** it links only Atlantis::Connection;
      - **Host:** it is hosted by `atlantis_runtime` (Q3);
      - **Standalone binary:** a standalone `atlantis_cli` executable is
        named for 0055.
    - **(B-b)** Tools (`src/tools/cli/`). Runtime cannot host it, because
      nothing depends on Tools. It fits the standalone binary of 0055, not
      v1.
    - **(B-c)** Private sources of `atlantis_runtime`, like `cli.cpp`. Then
      it is not an external client, because it shares the executable's
      private layer.
    - **(B-d)** A second target inside the Connection module. That is one
      module fewer, but it mixes client presentation with the connection
      contract.
  - **Recommendation: B-a.** The boundary scan (R8) then guards one
    directory, and `atlantis_runtime` (the executable, not `RuntimeHost`)
    is its only v1 host. B-d is an acceptable lighter variant if the
    reviewer prefers fewer modules.
- **Q7 — The Runtime side: where the InProcess endpoint hangs, who owns it,
  and how "no change" is guaranteed.**
  - **Options:**
    - **(R-a) `RuntimeApplication` owns the endpoint.**
      - **Member:** `std::optional<connection::InProcessEndpoint>`, declared
        after `worldAccess_` so it is destroyed first; it borrows it.
      - **Creation:** emplaced with it in `initializeSteps()`.
      - **API:** a public `openConnection()` →
        `std::unique_ptr<connection::RuntimeConnection>`. A connection must
        not outlive the application; the endpoint CHECKs this on
        destruction.
    - **(R-b)** `RuntimeApplication` exposes `RuntimeWorldAccess&`
      publicly, and `main.cpp` builds the endpoint. That widens the
      application's API with the owner-side boundary object and moves the
      access point out of the authority.
  - **Recommendation: R-a.** Runtime, the authority, owns every client
    access point (ADR-0033).
  - **How "no change" is guaranteed (R10):**
    - **The frame:** `runFrame()` is untouched. The endpoint drains only
      when a connection asks, and with no connection nothing is submitted.
    - **The executable:** `main.cpp` opens a connection only under
      `--exec`.
    - **Tests:** the smoke tests keep their friend access.
    - **Goldens:** produced by the headless fixture, which does not use
      `RuntimeApplication`.
    - **Android:** `android_main.cpp` is untouched, and `assembleDebug`
      covers `RuntimeHost`'s new dependency.
- **Q8 — The schema through the connection** (surfaced). `schema list` /
  `schema inspect` need the World schema.
  - **Options:**
    - **(K1)** A connection Query returns it: Core's descriptor types, whose
      views stay valid for the connection's lifetime. InProcess returns
      `worldSchema()`'s static data; a 0055 transport would own a decoded
      copy.
    - **(K2)** The CLI calls `worldSchema()` directly.
  - **Recommendation: K1.** K2 ties the client to being built from the same
    source as the Runtime, which is exactly what a remote client in 0055
    cannot assume. Through the connection, the CLI stays
    transport-independent, and nothing about the World changes.

## Out of Scope / Future Work

- **Named only (maintainer list):**
  - IPC and a standalone attachable CLI (0055), which brings the
    interactive REPL (Q3 H-c/H-d);
  - the Minimal Editor (0056): Hierarchy / Inspector / Viewport / Gizmo, as
    an ordinary client;
  - the Gameplay SDK and C# (0057/0058);
  - Package/Plugin (0059);
  - Agent capabilities: step runtime, capture frame, read diagnostics,
    compare regression.
- **Roadmap context.** The 0055–0059 roadmap is context only; this Spec
  makes no commitment to it.
- **Further work:**
  - a filtered listing (Q1 L2);
  - a joined single-token address (Q2 J-ii);
  - CLI commands for transactions and for create/destroy/add/remove;
  - Android hosting.
