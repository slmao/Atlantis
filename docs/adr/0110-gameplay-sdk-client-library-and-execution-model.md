# ADR 0110: Gameplay SDK — Client Library, Layers and Execution Model

- **Status:** Accepted
- **Date:** 2026-10-08 (accepted 2026-10-09)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-09 (review of this branch's own PR,
  [PR #234](https://github.com/slmao/Atlantis/pull/234); accepted together with Spec 0057's Approval, its ten open
  questions ruled as recommended after review rounds 1 and 2; ADR-0099 D4,
  ADR-0105, ADR-0106, ADR-0103, ADR-0104 and ADR-0004 unchanged)
- **Related Spec:** [Spec 0057: Gameplay SDK](../specs/0057-gameplay-sdk.md) (`Approved`)
- **Related ADR(s):**
  - The SDK is a client of [ADR-0105](0105-runtime-connection-and-cli-client.md)'s
    `RuntimeConnection`; over Remote it attaches through
    [ADR-0106](0106-attachable-runtime-transport-and-control.md). Neither
    interface changes.
  - It applies [ADR-0033](0033-runtime-authority-and-client-boundary.md)
    (the Runtime is authoritative; clients hold values) and
    [ADR-0034](0034-stable-public-boundary-versus-internal-cpp-layout.md)
    (the stable boundary is schema, identity and protocol).
  - It pairs with [ADR-0111](0111-schema-generated-typed-bindings.md) (how
    its typed layer is generated).
  - It keeps [ADR-0103](0103-runtime-world-operation-boundary.md),
    [ADR-0104](0104-runtime-world-transaction-atomicity.md) and
    [ADR-0004](0004-phase1-threading-baseline.md) unchanged.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The maintainer's goal** (2026-10-08):
  - a Gameplay SDK with a reflective and a generated typed interface over
    one schema;
  - `RuntimeConnection`'s Query, Command, Event and Transaction semantics,
    reused;
  - no Runtime or ECS storage exposed;
  - a real client example;
  - a scope proposal (from the drafter) on custom components, logic
    execution and time, without growing into a complete scripting or
    scheduling system.

  D3–D5 below were proposed by the drafter and ruled with Spec 0057's
  Approval (rulings Q5–Q7).
- **What exists:**
  - `RuntimeConnection` (values only; InProcess and Remote);
  - `RuntimeControl` (pause, resume, step with exact frame data and
    capture);
  - `worldSchema()` (eight types; every component field `Editable`);
  - Connection's client text grammar (`text::parsePath`, `leavesOf`);
  - a transport-neutral query batch in the CLI (`cli::QueryBatch`, Plan
    0055 P9). Its default asks one query at a time; the `atlantis`
    executable adapts `RemoteSession`'s pipelined queries to it. Neither
    Connection nor the protocol changed for it.
- **Three clients already speak the connection directly**: the CLI, the
  editor and the tests. Each rebuilds names → ids, component-level
  operations and multi-command builds by hand. The smoke test's point light
  is nine ordered commands.
- **The Runtime has no logic-hosting point.** `runFrame()` has been held to
  named-line diff guards since Spec 0055. There is no delta time and no
  simulation clock.
- **The frame number is not a logic clock.**
  `RuntimeControlHost::afterFrame()` increments it every frame, paused or
  not, because pause holds only command application. The number of frames
  between two Remote calls varies with the waits.
- **Component types are C++ types in World** (the ECS's component ids are
  per C++ type). The boundary accepts only `worldSchema()`'s components.
- **Remote calls are answered at frame boundaries** (D1 of ADR-0106).
  Measured for Spec 0057: about 33 ms per synchronous call on the default
  scene.

## Decision

1. **A new top-level module, Atlantis Gameplay SDK** (Spec 0057 ruling Q1) (`src/gameplay_sdk/`,
   namespace `atlantis::gameplay`, target `Atlantis::GameplaySdk`).
   - It links **Atlantis Connection only**. It includes no Runtime, ECS,
     World component, Platform, RHI, Renderer, Vulkan, Remote or OS header.
     A boundary test checks this, as for the CLI and the editor.
   - Nothing in the engine depends on it. Runtime, Remote, the CLI and the
     editor do not link it.
2. **Two layers, one implementation** (Spec 0057 rulings Q2, Q9).
   - **Reflective:** `gameplay::World` borrows a `RuntimeConnection` and
     addresses types and fields by name (Connection's `text` grammar) or
     id. It provides:
     - component-level read, add (with values) and remove;
     - "entities with these components";
     - a `Transaction` builder;
     - RAII `Subscription`s;
     - failures per ticket.
   - **Optional query batch:** `gameplay::QueryBatch`, an SDK-owned,
     transport-neutral interface after `cli::QueryBatch`.
     - It declares `listComponents` and `getProperties` over spans, with
       results in input order.
     - `World` takes it optionally. Without one, a default asks one query
       at a time over the connection.
     - The SDK uses it for component-field reads and for the component
       filter.
     - A Remote client supplies an adapter over `RemoteSession`; the SDK
       never names Remote.
     - It promises input order, not a snapshot (ADR-0111 D3).
   - **Typed:** generated per ADR-0111, and implemented on the reflective
     layer only.
   - Every SDK operation is a sequence of `RuntimeConnection` calls a client
     could make by hand, in the order the client wrote it. The SDK never
     reorders, merges or splits a submission.
   - The SDK adds no rule the boundary owns (finiteness, limits,
     editability): those remain the boundary's refusals. It resolves names
     and reports names it cannot resolve.
   - Values only: every result is a copy, and no SDK type names a World
     component C++ type, ECS handle or byte offset.
3. **Game logic is client-driven** (Spec 0057 ruling Q6). The SDK is a library: logic runs in the
   client's own control flow.
   - Over Remote, that is the client's own process.
   - In process, it is wherever a host calls it between frames.

   There is no callback, behaviour, update phase or scheduler inside the
   Runtime, and no loading of client code into the Runtime process.
4. **No clock; logic time is the client's own step** (Spec 0057 ruling Q7). The SDK defines no
   clock, delta time, fixed step or time scale.
   - A client counts its own logic steps and computes state from them.
   - Ordering comes from the rule that commands apply at the next frame's
     start, and that while paused a `step` releases exactly one frame's
     application.
   - The Runtime's frame number (`status().frame`, `FrameReport::frame`)
     identifies which frame a report describes. It is not a logic clock.
5. **No custom components** (Spec 0057 ruling Q5). The SDK covers the types the connection's
   schema serves, which today is `worldSchema()`. Client-defined component
   types are a later Spec. The reflective layer and the generator take any
   descriptor table, so they carry over unchanged.
6. **The real client is an out-of-process example** (Spec 0057 ruling Q8)
   (`examples/gameplay_demo/` → `atlantis_gameplay_demo`).
   - It links the SDK and Atlantis Remote's client half
     (`atlantis_remote_client`), the second executable to do so after
     `atlantis`.
   - It attaches to `atlantis_runtime --listen`, supplies a `QueryBatch`
     adapter over `RemoteSession`, and runs Spec 0057's North star: paused,
     one `step` per logic step `k`.
   - The Runtime executable does not link the SDK.
7. **Threading:** not thread-safe. Calls are made on one thread: between
   frames in process, or the client's thread over Remote (ADR-0004).

## Consequences

### Positive

- Gameplay code gets names, component values and compile-time types without
  any new Runtime capability. The Runtime, its frame and its goldens are
  untouched.
- One reflective implementation backs both layers, so typed and reflective
  calls cannot disagree. Parity is testable as equal `Command` sequences.
- The SDK works the same over InProcess and Remote because it only speaks
  `RuntimeConnection`.
- The systems this ADR leaves out — scheduler, time model, custom
  components, hosted code — stay open for their own Specs. No SDK shape presumes their
  answers.

### Negative / Trade-offs

- **Over Remote, logic runs at frame-boundary latency.** A synchronous call
  costs about two vsync frames on the default scene. A `QueryBatch` brings
  a component read or the component filter down to one pipelined boundary,
  but writes and steps remain one call each.
- **No delta time.** Motion is expressed per logic step. Frame-rate-
  independent logic waits for a time model.
- **Two identical small batch interfaces** (the CLI's and the SDK's). They
  can be unified later; doing it now would change Connection.
- **Order is the client's responsibility.** A point light must get its
  `Light` and `kind` before its `WorldMatrix` (Plan 0052 J1). The SDK
  documents the order and does not fix it silently.
- One more module, and Atlantis Remote's client half has a second linker.

## Alternatives Considered

- **Inside Atlantis Connection.** Rejected: Connection is the
  transport-neutral interface that Remote and Runtime link. A convenience
  layer and generated code would grow what they all carry.
- **No reflective layer (the typed layer over `RuntimeConnection`
  directly).** Rejected: every client would rebuild names, component
  operations and transactions, and the typed layer would be a second
  implementation of each.
- **Runtime-hosted behaviours** (update callbacks in `runFrame()`).
  Rejected for v1 (Spec 0057 ruling Q6): it is the start of a scheduler, a
  privileged position in the frame, and a frame-path change.
- **Loading client code into the Runtime** (a plugin flag). Rejected here:
  it is the Package/Plugin work (0059).
- **A frame clock query (delta time).** Rejected for v1: the Runtime has no
  such concept to expose; adding one is a Runtime and `RuntimeControl`
  change.
- **Deriving logic from the Runtime's frame number.** Rejected: it advances
  while paused and between Remote calls by varying amounts, so behaviour
  would not be reproducible.
- **No batching.** Rejected: component reads and the component filter
  would cost one frame boundary per query over Remote (5969
  `listComponents` calls for Bistro's filter).
- **A batch query on `RuntimeConnection`.** Rejected for v1: it is a new
  Query and a Connection change, where a client-side seam suffices.
- **Client-defined components in the World.** Rejected for v1: this needs
  run-time ECS registration, boundary and transport schema extension, and
  bake and persistence support.
- **SDK-side "components" keyed by GUID.** Rejected: it is invisible to
  other clients and is not World data.
- **The example hosted by `atlantis_runtime`.** Rejected: it needs a Runtime
  change for a demo and is hosted code in miniature. **Tests only** is not
  a real client.
