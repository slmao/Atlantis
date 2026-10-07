# Spec: Agent / Runtime CLI

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-07
- **Related Plan(s):** none yet. Drafting a Plan is authorized only after this
  Spec's Approval.
- **Approval:** pending. The maintainer fixed this Spec's position, command
  tree, output contract, north star, completion definition and named-only
  list before drafting (2026-10-07, chat). They are recorded under Goals /
  Non-Goals and are not open questions.
- **Related ADR(s):**
  [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md)
  (`Proposed`, drafted alongside this Spec). It records the decisions this
  Spec's open questions settle:
  - the transport, with its RPC shape and frame-boundary drain;
  - the Runtime control interface and the pause semantics it needs;
  - capture;
  - the machine output contract.

  The ADR implements [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md)'s
  "a transport (0055) implements this interface; it does not shape it",
  applies [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md),
  and keeps [ADR-0103](../adr/0103-runtime-world-operation-boundary.md),
  [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) and
  [ADR-0004](../adr/0004-phase1-threading-baseline.md).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Specs 0047–0054 laid eight layers of foundation:

- stable GUIDs;
- the schema;
- the authoring scene;
- the ECS;
- the bake;
- the Query / Command / Event boundary;
- transactions;
- the RuntimeConnection with its in-process CLI.

This Spec puts them under their first real consumer: a **machine client that
knows nothing of Atlantis's C++**. It drives a running Runtime through a
standalone `atlantis` command, reads and writes the World, steps frames,
captures what was rendered, reads diagnostics, and verifies the result.
Every command speaks JSON.

**No new lower-level abstraction is added.** The World is reached only
through Spec 0054's `RuntimeConnection`. A transport implements that
interface, as ADR-0105 D1 reserved. The lifecycle verbs Runtime needs
(pause, resume, step, capture, diagnostics) go through a small control
interface beside it, not into the World's four concepts.

**North star** (the maintainer's words): start Bistro → the CLI finds a
point light → reads its intensity → changes it → the Runtime World changes →
render extraction → the next frame's image changes, with no privileged
Renderer or ECS API anywhere in the chain.

**Done** (the maintainer's words): discover the schema → query the world →
find the entity → modify a component → step the Runtime → capture the
frame → read diagnostics → verify the result.

## Motivation / Problem Statement

### What Spec 0054 left, by design

- **Hosted only.** The only host is `atlantis_runtime --exec`, which reads
  a whole script at startup and runs one line per frame
  (`src/runtime/main.cpp:322-324`). A client cannot read a result and then
  decide its next command: there is no read-act loop. Spec 0054 ruling Q3
  named interactive and attached use as 0055's.
- **Human text only.** CLI output is line text; nothing is JSON.
- **Six commands.** There is no create, destroy, add or remove; no
  component filter; no Runtime control.
- **ADR-0105's open slot.** "A transport (0055) implements this interface;
  it does not shape it." No transport exists.

### Code evidence that constrains the transport

- **The message pump.** The window's messages are pumped only inside
  `runFrame()`, by `platform::processEvents()`
  (`runtime_application.cpp:1184`), a non-blocking `PeekMessageW` drain on
  Windows (`windows_platform.cpp:229`).
- **Frame pacing.** Presentation is FIFO (`vulkan_presentation.cpp:429`),
  so frames are vsync-paced.
- **The application point.** Commands apply at the first statement of
  `runFrame()` (`:1182`, Spec 0052 J6).
- **One frame thread.** ADR-0004 fixes one frame thread, and
  `RuntimeConnection` and `RuntimeWorldAccess` are frame-thread-only.
- **The consequence.** External input can enter the World only at a frame
  boundary, drained without blocking. `--exec` already works this way
  (Spec 0054 ruling Q3): a transport must be the same shape, drained once
  per frame by the host.
- **Platform and OS code.** Only Runtime may depend on Atlantis Platform
  (module_boundaries.md, Atlantis Runtime). OS-specific code outside
  Platform has a precedent in Tools (`src/tools/shader_compiler/process_launch.cpp`,
  `CreateProcessW`).

### Code evidence that constrains capture

- **The swapchain.** It is created with `COLOR_ATTACHMENT | TRANSFER_DST`
  usage only (`vulkan_presentation.cpp:417`). The presented image cannot
  be read back without an RHI and Vulkan-backend change.
- **The readback precedent.** Spec 0010/0011 and ADR-0040/0042 render into
  an `OffscreenTarget` and copy it with
  `CommandList::copyRenderTargetToBuffer()` (`command_list.h:89`) into a
  `BufferPurpose::Readback` buffer.
- **PNG encoding** exists only test-side (`tests/image_regression/support/png_codec.h`,
  over stb). In production code only the asset cooker links `Stb::Stb`.
- **Exact frame data.** Spec 0054's north star asserted exact
  `FrameLightingData` values through a test friend. A client needs the same
  facts through a public surface.

### Facts the command tree must respect

- **Entities have no name.** The World schema is `Transform`, `Camera`,
  `Light`, `Renderable` and `WorldMatrix`; none has a name field, and the
  scene grammar carries only `node_id` and `guid`. An entity is its GUID
  plus its components.
- **Point lights are a kind, not a type.** "A PointLight" is an entity with
  a `Light` whose `kind` is `Point`.
- **Bistro is content-gated.** It exists only when the fetched content's
  build step was declared: `ATLANTIS_bistro_scene_TARGET` and
  `ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID` (`src/runtime/CMakeLists.txt:255`,
  `main.cpp:75`). Tests skip without it
  (`frame_input_equivalence_tests.cpp:200`).
- **Bistro's lights.** Its point lights come from the committed overlay
  `assets/bistro/bistro_overlay.scene.txt`: for example
  `6b63b12c-9cde-4ae2-8391-c0b4cefadb7d`, a Point light with intensity 12
  and range 6. Spec 0046 states 64 point lights.
- **The blueprint** has no "Tooling Foundation" section. This Spec's
  position comes from the maintainer's direction (2026-10-07). The
  blueprint's Milestone 11 note records Phase 2B's start (Spec 0054).

## Goals

These are maintainer-fixed and are not to be relaxed in review:

- **The command tree**, in Atlantis's real shapes:

  ```
  atlantis schema list | schema inspect <Type>
  atlantis world list | world inspect
  atlantis entity list | entity inspect <guid> | entity create [<guid>] | entity destroy <guid>
  atlantis component add <guid> <Type> | component remove <guid> <Type>
  atlantis property get <guid> <Type>.<field> | property set <guid> <Type>.<field> <value...>
  atlantis runtime pause | runtime resume | runtime step
  ```

  Components are real types (`Light` with `kind`, not `PointLight`), and
  entities have no name.
- **Machine-operable output.** Every command supports `--json`. Errors go
  out with `--diagnostics=json`. The output is machine-discoverable,
  actionable and verifiable alike.
- **The north star** above, with no privileged Renderer or ECS API in the
  chain.
- **The completion definition** above.
- **No new lower-level abstraction.** The World is reached only through
  `RuntimeConnection` (ADR-0105).
- **Named only, not designed or scaffolded:**
  - a Minimal Editor (0056): Hierarchy, Inspector, Viewport and Gizmo, also
    an ordinary client;
  - the Gameplay SDK (0057): a reflective surface and a generated typed
    surface, from the same schema but called differently;
  - C# (0058);
  - Package/Plugin (0059);
  - Headless Simulation (0060);
  - Python (0061);
  - Advanced Runtime: jobs, physics, animation, navigation, audio,
    networking;
  - Renderer v2: Render World, GPU Scene, HiZ, indirect draw, Forward+,
    temporal, RT, GI, Neural.

Goals of this Spec within those boundaries:

- **A real read-act loop.** A client issues a command, reads its JSON
  result and decides the next, against a Runtime that keeps rendering.
- **Determinism where it matters.** A paused Runtime applies nothing until
  stepped, so "step" makes "this frame shows that edit" exact.
- **Zero behaviour change** when no client attaches: every existing test
  and golden is unchanged.

## Non-Goals

These are maintainer-fixed: everything in the named-only list above.

Also out of scope:

- **Any new World verb, event or concept.** Filters and summaries are
  client-side compositions of existing Queries.
- **Remote (cross-machine) access, authentication beyond a local session
  token, and encryption.** The transport is loopback-local (Q1).
- **Android attach.** Android builds the libraries; attaching to an Android
  Runtime (for example over `adb forward`) is later work.
- **Multi-threading.** No new thread; ADR-0004 is unchanged.
- **Time-driven simulation.** Nothing in the World animates today, so
  pause/step gate only command application (Q2); 0060 owns simulation time.
- **Renderer changes.** Capture must not change what is rendered (Q2).

## Requirements

### Functional

- **R1 — Attach** (Q1). A running `atlantis_runtime` started with a listen
  option accepts local client connections. The standalone `atlantis`
  executable attaches to it and runs one command per invocation. A REPL
  is also offered (Q5).
- **R2 — Transport implements `RuntimeConnection`** (Q1; ADR-0105 D1).
  - **Client side:** a remote connection implementing `RuntimeConnection`.
  - **Server side:** in the Runtime host, it drives an InProcess
    connection per client.
  - **Draining:** requests are drained without blocking at frame
    boundaries, on the frame thread.
  - **Semantics:** Spec 0052/0053/0054 semantics are unchanged.
- **R3 — Runtime control** (Q2). Through a control interface beside
  `RuntimeConnection`:
  - pause, resume, and step N frames;
  - the status (paused, frame index, scene GUID);
  - capture of a stepped frame (frame data, and an image on request);
  - diagnostics since a cursor.
- **R4 — The command tree** (Goals), with the shapes and outputs of Q3.
  - **`entity list`** accepts client-side filters by component presence
    and by field value, so "find a point light" is
    `entity list --with Light --where Light.kind=Point`.
  - **`entity create`** without a GUID generates one client-side: the
    Runtime still receives a caller-supplied GUID, per Spec 0052 ruling Q1
    (a1).
- **R5 — JSON** (Q3). `--json` gives one JSON document per invocation, in a
  versioned envelope.
  - **Values:** typed per field kind.
  - **Identity:** TypeIds are given with their names.
  - **Stability:** the same command on the same world yields the same bytes.
- **R6 — Diagnostics** (Q3). `--diagnostics=json` writes every error and
  warning to stderr as JSON objects with a stable code, a category and the
  facts to act on. Exit codes distinguish success, refusal, usage/text
  error, connection error and runtime error.
- **R7 — Writes** (Q4). Each write command is one ticketed submission, and
  reports its outcome (the event, or the refusal) once applied. Several
  writes can be grouped into one transaction.
- **R8 — The north star.** Everything in the chain runs through R1–R7 and
  public surfaces only:
  1. on Bistro, find a point light;
  2. read its intensity;
  3. set it;
  4. step;
  5. capture;
  6. the captured frame data carries the new intensity, and the image
     differs from the pre-edit capture.
- **R9 — The completion loop.** Discover → query → find → modify → step →
  capture → diagnostics → verify, as one automated end-to-end case (Q7).
- **R10 — No behaviour change without a client.**
  - Without the listen option, no socket is opened.
  - Every existing test and golden is unchanged.
  - Spec 0054's `--exec` keeps working (Q6).

### Non-functional

- **Latency:** a request is served at the next frame boundary: at most one
  frame, about 16.7 ms at 60 Hz FIFO. Requests a client sends together
  (pipelined) are served in the same boundary.
- **Robustness:** a slow or vanished client never stalls a frame. I/O is
  non-blocking with bounded buffers, and overruns disconnect.
- **Threading:** ADR-0004; no new thread.
- **Portability:** the client and server libraries build on Windows and
  Android (`assembleDebug`). Attach is exercised on Windows.
- **Dependencies:** none new for the transport (OS sockets). Image
  encoding is decided in Q2.

## Proposed Design

Under the recommendations below:

```
agent ── atlantis <cmd> --json ── atlantis::cli Commands ── RemoteConnection + RemoteControl ─┐
                                                                     (RuntimeConnection impl)  │ loopback TCP,
                                                                                               │ JSON-lines RPC
atlantis_runtime --listen ── main loop: runFrame(); server.poll();  ◄─────────────────────────┘
                              server: per client → InProcess connection (0054) + RuntimeControl
                              runFrame(): if !held → applyPending(); pump; collect; render  (Q2: hold)
```

- **The CLI** runs in the client process (`atlantis`), over a
  `RemoteConnection` that implements `RuntimeConnection`. Text forms,
  filters and JSON rendering are client-side. The wire carries
  `RuntimeConnection` and control calls, never command lines (Q1, Q3).
- **The server** is polled once per frame after `runFrame()`. It accepts
  clients, reads every complete request line, answers what it can now,
  and parks what needs a frame (a set's outcome, a step, a capture) until
  that frame completes.
- **Pause** holds command application, and frames keep being pumped and
  presented. A step releases exactly N applications (Q2).

## Architectural Impact

Yes. Recorded in
[ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md), drafted
alongside:

- the transport (loopback TCP, JSON-lines RPC of `RuntimeConnection` plus
  control), its frame-boundary drain, and its module placement (Q1);
- the `RuntimeControl` interface beside `RuntimeConnection`, which leaves
  ADR-0103's and ADR-0105's four concepts intact (Q2);
- the hold/step semantics, a `runFrame()` change to its first statement,
  with the default (not held) byte-identical (Q2);
- capture by offscreen re-render and readback, not swapchain readback (Q2);
- the JSON and diagnostics contract (Q3).

**Expected code impact**, for the Plan, under the recommendations:

- **New:**
  - transport code (client and server) with its tests;
  - the `atlantis` executable;
  - control and capture in Runtime;
  - JSON rendering and the new commands in Atlantis CLI;
  - end-to-end tests.
- **Changed:**
  - `runtime_application.h/.cpp` (hold/step, control, capture);
  - `main.cpp` and `cli.h/.cpp` (`--listen`);
  - Atlantis CLI's command layer (new tree, `--json`).
- **Docs:** `module_boundaries.md` and AGENTS.md (the transport's
  placement and its OS-code rule).
- **No change:**
  - the ECS core;
  - `RuntimeWorldAccess` semantics;
  - render extraction and the Renderer's output;
  - RHI and the Vulkan backend (capture uses existing readback);
  - formats, shaders, goldens.

## Alternatives Considered

The seven questions below compare their options. Rejected across them:

- **A privileged in-engine agent API.** It contradicts ADR-0033 and the
  "no privileged API" north star.
- **New World verbs for lifecycle control.** They would break ADR-0103's
  concept set (Q2).

## Testing & Verification Plan

- **Transport:**
  - a `RemoteConnection` equals an InProcess connection for every Query,
    command, transaction, subscription and failure route. This is the
    0054 connection test suite run against both;
  - pipelining;
  - partial lines;
  - oversize lines and disconnects;
  - no blocking (a frame with no client input does no I/O wait).
- **Control:**
  - pause holds application: commands stay pending across frames;
  - step N applies exactly N times, and its response carries the frame
    index;
  - resume restores per-frame application;
  - the not-held frame is byte-identical (full suites and goldens).
- **Capture:**
  - frame data equals the frame's extraction output, exactly;
  - the image equals an offscreen render of the same world.
- **Commands and JSON:**
  - every command against expected human text and expected JSON on a
    fixture scene;
  - JSON envelope and field stability;
  - diagnostics codes and exit codes;
  - filters.
- **The north star (R8):**
  - **Bistro, content-gated** (skipped without content, the
    `frame_input_equivalence_tests.cpp:200` precedent):
    1. attach;
    2. `entity list --with Light --where Light.kind=Point --json`;
    3. read `6b63b12c-…`'s intensity, expecting 12;
    4. set it to 24;
    5. step with capture;
    6. the frame data's point light at that light's position has intensity
       24 exactly, and the image differs from the pre-edit capture.
  - **A non-gated twin** runs the same on the default scene's Directional
    light, so every machine runs the chain.
- **The completion loop (R9):** one end-to-end case that runs the
  `atlantis` executable against `atlantis_runtime --listen`, through the
  completion definition's eight steps, asserting each step's JSON.
- **Regression (R10):**
  - full Debug + Release suites, every golden byte-identical;
  - fatal VVL;
  - `assembleDebug`;
  - the 0054 tests and the `--exec` north star unchanged in behaviour (Q6).

## Risks & Open Questions

Risks:

- **The transport is new surface:**
  - a listening socket (loopback-only, opt-in, with a session token);
  - a wire format;
  - two-process tests.
- **The hold mode changes `runFrame()`'s first statement.** The default
  path must stay byte-identical; full suites and goldens gate it.
- **A capture costs a second render** on capture frames only.
- **Client-side filters cost one Query per entity.** They are pipelined, so
  they cost one frame of latency but O(entities) messages.
- **The JSON contract is long-lived.** It is versioned from day one.

Open questions (to be ruled at review). Q1–Q7 were posed by the maintainer;
Q8 surfaced while drafting.

- **Q1 — Host and transport** (the heaviest).
  - **The evidence** (Motivation):
    - external input can enter only at a frame boundary, drained without
      blocking on the frame thread, exactly as `--exec` is;
    - ADR-0105 D1: a transport implements `RuntimeConnection`.

    So a transport is a frame-boundary-drained request queue whose payload
    is `RuntimeConnection` calls.
  - **Options:**
    - **(a) IPC in this Spec, with a standalone `atlantis` executable.**
      - **Transport choice:**
        - **(a1)** loopback TCP: portable sockets on Windows and Android
          (Winsock and BSD), non-blocking, `select` with a zero timeout per
          frame; a later Android attach via `adb forward`; binding to
          127.0.0.1 avoids firewall prompts;
        - **(a2)** a Windows named pipe: Windows-only, so Android needs a
          second transport;
        - **(a3)** `AF_UNIX`: Windows 10 1803+ and Android, file-system
          addressing, little Windows tooling.
      - **Framing:** JSON-lines (one request or response object per line,
        with an id). Length-prefixing is not needed for text payloads.
      - **Coexisting with the pump and vsync:** the host polls once after
        each `runFrame()`. It reads all complete lines and answers them, so
        latency is at most one frame. Writes are non-blocking and buffered.
        Nothing waits, so the window keeps pumping and presenting.
      - **Cost:**
        - an RPC codec for every `RuntimeConnection` and control call;
        - a server that parks frame-dependent requests;
        - session discovery (a session file holding the port and a token);
        - socket code outside Platform (the Tools precedent) or a Platform
          API change, to be ruled in ADR-0106;
        - two-process tests.
    - **(b) Hosted first.** Grow `--exec` with the new tree and JSON. IPC and
      attach become a second phase or a separate Spec.
      - **Cost:**
        - no read-act loop, so the north star's "find a point light, then
          change it" must be scripted with its GUID known in advance, and
          the completion definition is not met by a machine client;
        - the JSON contract is fixed before its real consumer exists;
        - phase two reopens the CLI layer anyway.
      - **Benefit:** no transport risk now.
    - **(c) Stdio session.** `atlantis_runtime --serve-stdio` speaks
      JSON-lines on its own stdin/stdout to the process that spawned it.
      - **Gains:** interactive, no ports, no firewall, no token.
      - **Costs:**
        - only the spawning process can talk to it, so one-shot `atlantis
          <cmd>` invocations cannot attach;
        - Windows needs a non-blocking pipe peek (`PeekNamedPipe`) on
          stdin, the Spec 0054 Q3 H-c concern;
        - not usable with a console stdin.
  - **Recommendation: (a) with (a1),** loopback TCP and JSON-lines RPC,
    drained after each `runFrame()`.
    - **Why (a).** It is the only option that meets the completion
      definition with a real read-act loop from a separate process, and
      the one ADR-0105 reserved this Spec for.
    - **Why (a1).** It is the one transport both target platforms share.
    - **How cost is contained:**
      - opt-in `--listen [port]`, with 0 meaning ephemeral;
      - 127.0.0.1 only;
      - a per-run token in a session file;
      - no thread;
      - the server reuses 0054's InProcess endpoint per client;
      - the payload is the existing interface, not new semantics.
    - **(c)** remains a cheap later addition; (b) defers the very
      pressure test this Spec is for.
- **Q2 — The Runtime control plane.** Pause, resume, step, capture and
  diagnostics are lifecycle verbs, not World operations.
  - **Shape:**
    - **(K-a)** a parallel `RuntimeControl` interface beside
      `RuntimeConnection`:
      - `status()`;
      - `pause()` / `resume()`;
      - `step(n, captureRequest)`;
      - `diagnostics(cursor)`.

      It is implemented by Runtime and carried by the same transport, with
      a new ADR (0106). ADR-0103's and ADR-0105's four concepts are
      untouched.
    - **(K-b)** controls modelled as World data (a "runtime" entity with a
      `paused` property). It abuses the schema and makes lifecycle look
      like world state, against ADR-0103.
    - **(K-c)** a v1 subset (pause/resume/step only), with capture and
      diagnostics postponed. That fails the completion definition.
  - **Verbs vs the fixed tree.** The maintainer's tree has `runtime pause
    | resume | step` and no capture or diagnostics verb, yet the completion
    definition needs both. Options:
    - **(V-a)** fold them in without new verbs:
      - `runtime step [--frames N] [--capture <dir>]` returns the stepped
        frame's report;
      - Runtime diagnostics come back in every JSON envelope's
        `diagnostics` array, and on stderr under `--diagnostics=json`;
    - **(V-b)** add `runtime capture` and `runtime diagnostics` verbs,
      which extends the fixed tree.
  - **Pause semantics.** Nothing animates, so pause means *hold command
    application*.
    - **(P-a)** `runFrame()`'s first statement becomes "apply pending
      unless held". Pumping, collection and presentation continue, so the
      window stays responsive and the same world is presented. A step
      releases N applications. A changed line of `runFrame()`; the default
      not-held path is byte-identical.
    - **(P-b)** the host stops calling `runFrame()` while paused. The window
      stops pumping (`processEvents()` is the only pump), so it hangs.
    - **(P-c)** the endpoint withholds submissions. That breaks ticket
      assignment at submit (Spec 0052 J4).
  - **Capture:**
    - **(C1)** frame data: the stepped frame's extraction output
      (`FrameLightingData`, camera matrices, draw count) as JSON. It is
      exact and cheap, with no GPU readback.
    - **(C2)** an image by offscreen re-render: on request, the stepped
      frame's world is rendered once more into an `OffscreenTarget` at the
      window extent and read back with `copyRenderTargetToBuffer()` (the
      Spec 0010/0011, ADR-0040/0042 precedent). It is written as PNG
      (Runtime would link `Stb::Stb`, the asset cooker's dependency) or as
      raw RGBA plus a sidecar (no dependency). It costs a second render on
      capture frames only.
    - **(C3)** swapchain readback. It needs `TRANSFER_SRC` swapchain usage
      (`vulkan_presentation.cpp:417` has none), a Presentation readback API
      and BGRA/sRGB handling, and Android support of that usage is not
      guaranteed.
  - **Bistro gating.** Bistro exists only with its content (Motivation).
    Its automated north star skips without it, and a non-gated twin runs
    on the default scene.
  - **FIFO and vsync.** A step completes after N presented frames, so its
    response latency is at least N frames. Holding costs nothing extra:
    frames keep presenting at the vsync rate.
  - **Recommendation: K-a + V-a + P-a + C1 always, with C2 on request**
    (PNG via `Stb::Stb`, already vetted under ADR-0006). C3 is rejected for
    v1.
- **Q3 — The `--json` output schema.**
  - **Envelope** (one per invocation, on stdout):

    ```
    { "atlantis": "cli/1", "command": "<tree path>", "ok": true,
      "result": <command-specific>, "diagnostics": [<runtime diagnostic>…] }
    ```

  - **Error shape** (`ok: false`; and with `--diagnostics=json`, each error
    or warning also goes to stderr as one JSON object per line):

    ```
    { "code": "UnknownField", "category": "text|usage|refused|connection|runtime",
      "message": "...", "subject": { ... the facts to act on: guid, path, ticket, typeId } }
    ```

    - **Codes** are the existing `TextError`, `AccessError` and
      `ConnectionError` names, plus transport and usage codes.
    - **Exit codes:**

      | Code | Meaning |
      |---|---|
      | 0 | ok |
      | 1 | refused by the boundary |
      | 2 | usage or text error |
      | 3 | connection error |
      | 4 | runtime error |

  - **Values:**

    | Kind | JSON |
    |---|---|
    | `Float32` | a number (shortest round-trip) |
    | vectors | arrays |
    | enum | `{ "name": "Point", "value": 1 }` |
    | `UInt64` | a decimal string (asset ids exceed 2^53) |
    | GUID | a string |
    | absent Optional | `null` |

  - **Identity:** a type is `{ "name": "Light", "qualified": "world::Light",
    "typeId": "0x…" }` (16 hex digits).
  - **Per command** (fields):
    - `schema list`: `[type]`;
    - `schema inspect`: `type` with `fields[{name, kind, type?, flags[]}]`
      or `constants[]`;
    - `world list`: `[{scene, entities}]`, one world in v1;
    - `world inspect`: `{scene, entities, components{name: count},
      paused, frame}`;
    - `entity list`: `[{guid, components[]}]`;
    - `entity inspect`: `{guid, components{name: {path: value}}}`;
    - writes: `{ticket, outcome: {event…} | {refused…}}`;
    - `property get`: `{guid, path, value}`;
    - `runtime step`: `{frame, applied, capture?: {frameData, image?}}`.
  - **No name field:**
    - **(N-a)** show exactly the GUID and the components;
    - **(N-b)** also a derived, read-only `summary`. For example a Light
      gives `"Light(Point, intensity 12)"`, and an entity with a Renderable
      gives its mesh asset id.
  - **Recommendation: the envelope, error shape, values and fields above,
    with N-a.** A summary invents a naming convention the schema does not
    have; `entity list --with/--where` filters make finding things
    possible without names. Human text output is derived from the same
    data.
- **Q4 — Making writes transactional.**
  - **Options:**
    - **(T-a)** each write invocation is one submission (one ticket), as
      Spec 0052's single path;
    - **(T-b)** every invocation is a transaction:
      `entity create <guid> --component Light --set Light.kind=Point …`
      builds one transaction from one invocation;
    - **(T-c)** an explicit `atlantis tx <file|->`: a list of write
      commands submitted as one `submitTransaction()`, with the result
      `{applied | refused-at <n>}`.
  - **Recommendation: T-a by default, plus T-c.**
    - A single write is already all-or-nothing.
    - Grouping is explicit, so its atomicity is visible.
    - T-b's compound flags multiply the syntax. `entity create` keeps only
      an optional `--component <Type>…` (still one transaction, of the
      create plus adds).
- **Q5 — A REPL in v1.**
  - **The 0054 objection is gone under attach:** the CLI process blocks on
    its own stdin while the Runtime keeps rendering in its own process.
  - **Options:**
    - **(R-a)** v1 includes `atlantis repl`: one connection and one
      control session, each line a command, `--json` per line;
    - **(R-b)** one-shot invocations only.
  - **Recommendation: R-a.**
    - It is a small loop over the same command layer.
    - It keeps subscriptions across commands, and an agent harness with a
      persistent shell can use it.
    - One-shot stays primary and fully specified.
- **Q6 — How `atlantis::cli` grows, and the executable.**
  - **The command layer:**
    - **(G-a)** grow the Spec 0054 `Commands` into the new tree. The 0054
      command names (`world entities`, `entity get/set`) stay as aliases
      for `--exec` scripts and `north_star.txt`;
    - **(G-b)** replace them outright, migrating the script and its tests.
  - **The executable:**
    - **(X-a)** `atlantis` built in `src/cli/` (target `atlantis_cli_app`,
      output name `atlantis`), linking Atlantis CLI and the transport
      client;
    - **(X-b)** in Tools: possible now, since the executable is a client
      and nothing depends on it.
  - **Transport placement:**
    - **(M-a)** a new module **Atlantis Remote** (`src/remote/`): client
      `RemoteConnection` and `RemoteControl`, the server, and the codec,
      with its own minimal OS socket layer under the Tools precedent;
    - **(M-b)** inside Atlantis Connection, which would then carry OS code;
    - **(M-c)** in Platform, which only Runtime may use, so the client
      could not use it.
  - **Recommendation: G-a + X-a + M-a.**
    - Aliases keep 0054's verified script working.
    - Keeping the executable next to the library it is a shell for keeps
      the R8 boundary scan in one place.
    - A Remote module keeps OS code out of Connection and CLI, and lets
      Runtime link the server half without Platform.
- **Q7 — The verification surface.**
  - **North star, automated:**
    - **(A-a)** an in-process test: a windowed `RuntimeApplication`, the
      server, and a `RemoteConnection` over real loopback in one process,
      the server polled between frames. Exact frame-data assertions, as
      the Spec 0054 precedent did. Bistro-gated, plus the default-scene
      twin;
    - **(A-b)** a two-process ctest that launches `atlantis_runtime
      --listen` and drives the `atlantis` executable;
    - **(A-c)** both.
  - **JSON stability:**
    - **(J-a)** expected-JSON text per command on a fixture scene, plus an
      envelope-version test;
    - **(J-b)** a JSON Schema document validated in tests (a new
      dependency).
  - **Agent loop:**
    - **(L-a)** one end-to-end case per completion-definition step, in the
      two-process form;
    - **(L-b)** a human-run script only.
  - **Recommendation: A-c + J-a + L-a.**
    - A-a gives the exact, debuggable assertions.
    - A-b and L-a prove the real external shape.
    - J-a needs no new dependency.
- **Q8 — Where the command engine runs** (surfaced).
  - **Options:**
    - **(E-a)** in the client process, over a `RemoteConnection`. The wire
      carries `RuntimeConnection` and control calls.
    - **(E-b)** in the Runtime process. The wire carries command lines,
      and the server runs `atlantis::cli` against an InProcess connection.
  - **Trade-off.** E-b makes filters cheap (in-process queries) but makes
      the wire a CLI protocol: the transport would then shape the API,
      against ADR-0105 D1, and every future client (Editor, SDK) would
      speak CLI text.
  - **Recommendation: E-a.** Filters stay client-side compositions and are
    pipelined, so their cost is one frame plus O(entities) messages.

## Out of Scope / Future Work

- **Named only (maintainer list):** see Goals.
- **Further work:**
  - attaching to an Android Runtime;
  - remote (non-loopback) access and real authentication;
  - server-side filtered listing (0056's L2);
  - a stdio session transport (Q1 (c));
  - swapchain readback (Q2 C3);
  - time-driven pause semantics (0060).
