# ADR 0105: RuntimeConnection — a Transport-Independent Client Connection, and the CLI as an Ordinary Client

- **Status:** Accepted
- **Date:** 2026-10-07 (accepted 2026-10-07)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-07 (review of this branch's own PR,
  [PR #222](https://github.com/slmao/Atlantis/pull/222); accepted together with Spec 0054's Approval, its eight open
  questions ruled as recommended)
- **Related Spec:** [Spec 0054: RuntimeConnection and a Minimal CLI Client](../specs/0054-runtime-connection-minimal-cli.md) (`Approved`)
- **Related ADR(s):**
  - Applies [ADR-0033](0033-runtime-authority-and-client-boundary.md). It
    gives that ADR's in-process/out-of-process question its first, in-process
    answer, and leaves the transport open.
  - Extends [ADR-0103](0103-runtime-world-operation-boundary.md) D1's Query
    set by one Query (D1: "Additions are later Specs"), under Spec 0054
    ruling Q1. It supersedes nothing, and no ADR-0103 decision changes.
  - Keeps [ADR-0104](0104-runtime-world-transaction-atomicity.md) and
    [ADR-0004](0004-phase1-threading-baseline.md) unchanged; no thread is
    added.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **Where we are.** Specs 0052/0053 gave the Runtime World a Query /
  Command / Event boundary with transactions (`RuntimeWorldAccess`). Its
  only user is a test, through a friend accessor.
- **The maintainer's Phase 2B** begins by proving that a client holding no
  Runtime object can observe and modify a running World, end to end into
  the renderer.
- **The maintainer fixed** (2026-10-07):
  - the connection's semantics are exactly the four existing concepts;
  - the transport must not decide the Runtime API, with IPC as 0055, named
    only;
  - InProcess is the only implementation;
  - the CLI has a fixed six-command surface;
  - the CLI is a real external client.
- **Constraints found in the code:**
  - **The frame loop.** The window is pumped only inside `runFrame()`
    (`platform::processEvents()`, a non-blocking `PeekMessageW` drain on
    Windows), and presentation is FIFO.
  - **Threading.** ADR-0004 fixes one frame thread, and
    `RuntimeWorldAccess` is frame-thread-only.
  - **The executable's dependencies.** `atlantis_runtime` links only
    `RuntimeHost`. No other module may depend on `RuntimeHost`, and
    nothing may depend on Tools.
  - **The boundary's queues.** `RuntimeWorldAccess` has one event queue
    and one failure queue.
- **Inherited debts:**
  - entity listing (Spec 0052 Risks; ADR-0103 Consequences);
  - an address text form (Plan 0052 J7).

## Decision

1. **The connection is exactly the four concepts.**
   - **Shape.** `RuntimeConnection` exposes Query, Command (`submit`),
     Transaction (`submitTransaction`) and Event (subscriptions, and the
     connection's own failures).
   - **Types.** It uses Spec 0052/0053's value, command, event, ticket and
     failure types, unchanged.
   - **Semantics are those of `RuntimeWorldAccess`:**
     - application at the owner's next `applyPending()`;
     - validation;
     - atomicity;
     - event content.
   - It never returns a pointer or reference into Runtime-owned state.
   - A transport (0055) implements this interface; it does not shape it.
2. **InProcess first and only** (Spec 0054 ruling Q7, R-a).
   - **Ownership.** `RuntimeApplication` owns the InProcess endpoint over
     its `RuntimeWorldAccess`. It is declared after `worldAccess_` and
     destroyed first.
   - **Opening connections.** `openConnection()` hands out connections,
     which must not outlive it.
   - **Threads.** None is added.
   - **The frame.** `runFrame()` is unchanged; with no connection open,
     Runtime behaves exactly as before.
3. **Placement** (Spec 0054 rulings Q4, Q6: M-b, B-a).
   - **Atlantis Connection** (`src/connection/`, `atlantis::connection`):
     - **Contents:** the interface, the endpoint and the text forms;
     - **Depends on:** Core and World's public access headers;
     - **Depended on by:** Runtime, which owns the endpoint, and clients.
   - **Atlantis CLI** (`src/cli/`, `atlantis::cli`):
     - **Contents:** commands, formatting and the script runner;
     - **Depends on:** only Atlantis Connection;
     - **Must not include or link:** `RuntimeHost`, Runtime headers,
       `world/ecs/*`, Platform, RHI or the Renderer, enforced by a test.
   - **The host.** The executable `atlantis_runtime` is the only v1 host of
     the CLI library.
4. **The listing query** (Spec 0054 ruling Q1, L1).
   - **Shape.** `RuntimeWorldAccess::listEntities()` returns the GUID of
     every live entity in the boundary's index, sorted by GUID value.
   - **Scope.** Exactly what the boundary can address.
   - This is one Query added under ADR-0103 D1, and it is exposed through
     the connection.
5. **Text forms** (Spec 0054 ruling Q2, T2 + F1 + J-i):
   - an entity is its canonical RFC 9562 GUID text;
   - a property is `<Type>.<field>[.<field>…]`:
     - the type by its short World schema name, with the qualified name
       also accepted;
     - the field path down to the leaf, mapped to `(TypeId, leaf FieldId)`;
   - each field kind has one value text: decimals, 3 or 4 arguments for
     vectors, enum constant names, GUID text, and `none` for an absent
     Optional;
   - `get` output parses back to the same value.

   The text is client text, not a wire protocol. Its encoding over a
   transport is 0055's.
6. **Per-connection, pull-only events** (Spec 0054 ruling Q5, S-b).
   - **Subscribing.** `subscribe(EventFilter{kinds, optional entity,
     optional component})`, then `drainEvents(subscription)` and
     `unsubscribe`.
   - **Delivery.** Events arrive from subscription time on.
   - **Failures.** `drainFailures()` returns the connection's own, routed
     by ticket.
   - **Mechanism.** The endpoint is the boundary's only drainer and fans
     out on demand.
   - There are no callbacks; this is not reactive.
7. **The host** (Spec 0054 ruling Q3, H-a). `atlantis_runtime --exec <file|->`.
   - **Loading.** The script is read whole at startup.
   - **Pacing.** One line runs after each `runFrame()`, on the frame thread.
     A `set` applies at the next frame's start.
   - **No blocking.** Nothing blocks after startup.

   An interactive REPL, which needs a thread or non-blocking console I/O,
   is not chosen and is named for 0055.
8. **The schema through the connection** (Spec 0054 ruling Q8, K1). A Query
   returns Core's schema descriptors, valid for the connection's lifetime.
   The CLI never calls `worldSchema()` itself.

## Consequences

### Positive

- **One interface for every client** (CLI now; Editor 0056, SDK 0057,
  transport 0055 later), with ADR-0033's symmetry made concrete.
- **Several clients can coexist** without stealing each other's events or
  failures.
- **The CLI exercises every layer** of the north-star chain on the scene
  being rendered, deterministically and scriptably.
- **No thread, no frame change, no golden change.**

### Negative / Trade-offs

- **Two new top-level modules,** with their dependency rules and boundary
  tests.
- **Fan-out cost.** The endpoint copies each event once per matching
  subscription.
- **Not interactive in v1.** Scripts run one line per frame.
- **Windows only** for the CLI host. Android builds the libraries but does
  not host them.
- **0055 must implement this interface as is,** or change it through its
  own Spec and ADR.

## Alternatives Considered

- **The connection inside World** (Q4 M-a); **the CLI in Tools or in the
  executable's private layer** (Q6 B-b/B-c); **merging the CLI into
  Connection** (Q6 B-d, a lighter variant).
- **Listing through a filtered FindEntity** (Q1 L2, a later extension), or
  outside the World (Q1 L3, incorrect).
- **The qualified-only or leaf-only text forms** (Q2 T1, F2).
- **A blocking REPL** (freezes the window), **a non-blocking console poll**
  (OS-specific I/O outside Platform), **a reader thread** (a new threading
  model), **a standalone process** (needs IPC) (Q3).
- **A single shared event queue** (Q5 S-a) and **callbacks** (Q5 S-c).
- **`RuntimeApplication` exposing `RuntimeWorldAccess&`** (Q7 R-b).
- **The CLI reading `worldSchema()` directly** (Q8 K2).
