# ADR 0106: Attachable Runtime — a Frame-Drained Transport for RuntimeConnection, and a Runtime Control Interface

- **Status:** Proposed
- **Date:** 2026-10-07
- **Deciders:** slmao
- **Acceptance:** pending. To be `Accepted` before or during the review of
  [Spec 0055](../specs/0055-agent-runtime-cli.md), with D1–D8 as ruled there
  (Spec Q1–Q8).
- **Related Spec:** [Spec 0055: Agent / Runtime CLI](../specs/0055-agent-runtime-cli.md)
- **Related ADR(s):**
  - Implements [ADR-0105](0105-runtime-connection-and-cli-client.md) D1's
    reservation: "a transport (0055) implements this interface; it does not
    shape it". No ADR-0105 decision changes.
  - Applies [ADR-0033](0033-runtime-authority-and-client-boundary.md): it
    answers the out-of-process question for a local client.
  - Keeps [ADR-0103](0103-runtime-world-operation-boundary.md) (concepts,
    commands, events, application point),
    [ADR-0104](0104-runtime-world-transaction-atomicity.md) and
    [ADR-0004](0004-phase1-threading-baseline.md) (no thread).
  - Uses the readback capability of
    [ADR-0040](0040-gpu-to-cpu-readback-rhi-capability.md) and the
    precedent of
    [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **What 0054 delivered and left open.** Spec 0054 delivered
  `RuntimeConnection` with an InProcess implementation and a CLI hosted by
  `atlantis_runtime --exec`. A client in another process could not reach a
  running Runtime, could not read a result and then act, and had no JSON
  and no lifecycle control.
- **The maintainer** (2026-10-07) fixed the next step: a machine client
  that knows nothing of Atlantis's C++, with no new lower-level
  abstraction. The maintainer also fixed the command tree, `--json`
  everywhere, `--diagnostics=json`, the north star, and the completion
  definition (discover → query → find → modify → step → capture → read
  diagnostics → verify).
- **Constraints from the code:**
  - **The pump.** The window is pumped only inside `runFrame()`
    (`platform::processEvents()`, a non-blocking `PeekMessageW` drain).
  - **Pacing.** Presentation is FIFO.
  - **The application point.** Commands apply at `runFrame()`'s first
    statement.
  - **Threading.** ADR-0004 fixes one frame thread, and the connection and
    boundary are frame-thread-only.
  - **Platform.** Only Runtime may depend on Platform. OS code outside
    Platform has a Tools precedent (`process_launch.cpp`).
  - **Capture.** The swapchain has no `TRANSFER_SRC` usage, and offscreen
    readback exists (ADR-0040).
  - **Data shape.** Entities have no name field.
  - **Content.** Bistro is content-gated.

## Decision

1. **Transport: loopback TCP, JSON-lines RPC, drained at frame boundaries**
   (Spec 0055 Q1, recommended (a)+(a1)).
   - **Opt-in.** `atlantis_runtime --listen [port]` (0 means ephemeral)
     binds 127.0.0.1 only. It writes a session file holding the port and a
     per-run token, and a client presents the token on connect.
   - **Framing.** Each line is one request or response object, carrying an
     id.
   - **Draining.** The host polls the server once after each `runFrame()`.
     Each poll accepts, reads every complete line, answers what is answerable
     now, and parks frame-dependent requests (a write's outcome, a step, a
     capture) until their frame completes.
   - **No blocking and no thread.** Sockets are non-blocking, buffers are
     bounded, and an overrun disconnects.
   - **Without `--listen`** no socket exists.
2. **The payload is the existing interface** (Q8, recommended E-a).
   - **The wire.** It carries `RuntimeConnection` calls (and D3's control
     calls), never CLI text.
   - **Client side.** A `RemoteConnection` implements `RuntimeConnection`.
   - **Server side.** Each client gets its own 0054 InProcess connection.
   - **Semantics.** They are unchanged: tickets, subscriptions, failure
     routing, transactions.
   - **The command engine** (text forms, filters, JSON) runs in the client
     process.
3. **A `RuntimeControl` interface beside `RuntimeConnection`** (Q2,
   recommended K-a + V-a).
   - **Calls:**
     - `status()` (paused, frame index, scene GUID);
     - `pause()` / `resume()`;
     - `step(n, capture)`;
     - `diagnostics(cursor)`.
   - **Implementation.** Runtime implements it, and the same transport
     carries it.
   - **The four concepts are untouched.** Lifecycle is not World data.
   - **The command tree gains no capture or diagnostics verb.**
     `runtime step --capture` returns the frame report, and Runtime
     diagnostics ride in every JSON envelope and on stderr.
4. **Pause holds command application** (Q2, recommended P-a).
   - **The change.** `runFrame()`'s first statement becomes "apply pending
     unless held".
   - **What keeps running:** pumping, collection and presentation, so the
     window stays responsive.
   - **Steps.** `step(n)` releases exactly n applications.
   - **The default path** (never held) is byte-identical, guarded by the
     full suites and goldens.
   - Nothing in the World is time-driven today. Simulation time is 0060's.
5. **Capture** (Q2, recommended C1 + C2).
   - **Always:** the stepped frame's data, its extraction output
     (`FrameLightingData`, camera matrices, draw count), exact.
   - **On request:** an image. The stepped world is rendered once more into
     an `OffscreenTarget` at the window extent, read back with
     `copyRenderTargetToBuffer()` (ADR-0040), and written as PNG. Runtime
     links `Stb::Stb`, already vetted under ADR-0006 for the asset cooker.
   - **Not chosen:** swapchain readback. It needs `TRANSFER_SRC` swapchain
     usage, a Presentation API and format handling, and Android support of
     that usage is not guaranteed.
6. **The machine output contract** (Q3, recommended).
   - **Envelope.** One versioned envelope per invocation:
     `{"atlantis":"cli/1", command, ok, result, diagnostics}`.
   - **Errors.** Each error is `{code, category, message, subject}`, with
     codes from `TextError`, `AccessError` and `ConnectionError` plus
     transport and usage codes. Under `--diagnostics=json`, errors are
     also written to stderr as JSON lines.
   - **Exit codes:** 0 ok, 1 refused, 2 usage/text, 3 connection,
     4 runtime.
   - **Values** are typed:
     - `UInt64` as decimal strings;
     - enums as `{name, value}`;
     - GUIDs as strings;
     - an absent value as `null`.
   - **Types** come with their TypeId.
   - **Entities** are shown as GUID plus components, with no invented names
     (N-a).
7. **Placement** (Q6, recommended G-a + X-a + M-a).
   - **Atlantis Remote** (`src/remote/`), a new module, holds:
     - the client `RemoteConnection` and `RemoteControl`;
     - the server;
     - the codec;
     - its own minimal OS socket layer, under the Tools precedent.
   - **Dependencies.** It depends on Atlantis Connection. Runtime links the
     server half, and Platform is untouched.
   - **The executable.** `atlantis` is built in `src/cli/`, links Atlantis
     CLI and the client half, and grows the 0054 command layer. The 0054
     names stay as aliases.
8. **Writes and sessions** (Q4, Q5, recommended T-a + T-c, R-a).
   - **One-shot writes.** Each write invocation is one ticketed submission,
     reporting its outcome once applied.
   - **Grouping.** `atlantis tx` submits a list of writes as one
     transaction.
   - **`atlantis repl`** keeps one connection and control session open.

## Consequences

### Positive

- **A real read-act loop** for any local process, over the interface every
  future client (Editor 0056, SDK 0057) will use. The transport adds no
  semantics.
- **Exact, deterministic verification.** Hold, step and frame data make
  "this frame shows that edit" exact.
- **No thread and no Renderer, RHI or Platform change.** The not-held frame
  is byte-identical.

### Negative / Trade-offs

- **A listening socket and a wire format to maintain.** Both are versioned
  and loopback-only, with a session token, but they are new surface.
- **One changed statement in `runFrame()`.**
- **A second render on capture frames.**
- **Runtime gains a PNG dependency.**
- **Client-side filters** cost O(entities) pipelined messages.
- **OS socket code** lives in a non-Platform module, under the Tools
  precedent rather than Platform's charter.

## Alternatives Considered

- **Transport alternatives** (Spec 0055 Q1):
  - hosted-only growth of `--exec` (b);
  - a stdio session (c);
  - Windows named pipes (a2);
  - `AF_UNIX` (a3).
- **Running the command engine in the Runtime process** (Q8 E-b). The wire
  would become a CLI protocol, which is the transport shaping the API.
- **Lifecycle as World data** (Q2 K-b), and a v1 without capture or
  diagnostics (K-c).
- **Pausing by not calling `runFrame()`** (Q2 P-b: the window hangs), or by
  withholding submissions (P-c: breaks ticket assignment).
- **Swapchain readback** (Q2 C3).
- **Derived entity names** (Q3 N-b).
- **Transport placement in Connection** (Q6 M-b) or in Platform (M-c).
