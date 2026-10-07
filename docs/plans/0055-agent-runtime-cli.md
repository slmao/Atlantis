# Plan: Agent / Runtime CLI

- **Spec:** [Spec 0055: Agent / Runtime CLI](../specs/0055-agent-runtime-cli.md)
  (`Approved`, 2026-10-07, [PR #226](https://github.com/slmao/Atlantis/pull/226);
  rulings Q1–Q8 binding) —
  [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md)
  (`Accepted`, D1–D8).
  - It fills [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md)
    D1's reserved transport slot.
  - It keeps [ADR-0103](../adr/0103-runtime-world-operation-boundary.md),
    [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) and
    [ADR-0004](../adr/0004-phase1-threading-baseline.md).
  - It uses [ADR-0040](../adr/0040-gpu-to-cpu-readback-rhi-capability.md)'s
    readback.
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending. Implementation needs the Joint Human Review
  of this Plan together with Spec 0055, explicitly authorizing it (J1–J10
  below).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0055 R1–R10 as ruled:

- **A new Atlantis Remote module:** a loopback-TCP JSON-lines RPC whose
  payload is `RuntimeConnection` and `RuntimeControl` calls. It has a client
  half (`RemoteConnection`, `RemoteControl`) and a server half drained after
  each `runFrame()`.
- **A `RuntimeControl`** (status, pause/resume, step with capture,
  diagnostics) implemented by Runtime, with pause holding command
  application in `runFrame()`'s first statement.
- **Capture:** exact frame data always, and an offscreen-rendered PNG on
  request.
- **The CLI:** the maintainer's command tree with `--json`,
  `--diagnostics=json`, `--with`/`--where`, `atlantis tx` and a REPL, in
  the 0054 command layer (old names as aliases).
- **The `atlantis` executable.**
- **The north star:** on Bistro (content-gated) and on the default scene,
  in-process and across two processes.

Every golden stays byte-identical; nothing is re-captured.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `c97c6e2` (PR #226 merged).

1. **The 0054 code being extended:**
   - `RuntimeConnection` (`src/connection/include/atlantis/connection/runtime_connection.h`);
   - `InProcessEndpoint` (`in_process_endpoint.h/.cpp`: the boundary's only
     drainer, per-connection subscriptions, ticket-routed failures);
   - `text` (`text.h/.cpp`);
   - `atlantis::cli::Commands` / `ScriptRunner` (`src/cli/`);
   - `RuntimeApplication::openConnection()` with its `EndpointSlot` move
     guard;
   - `--exec` in `src/runtime/main.cpp:322-324`.
2. **`runFrame()`'s first statement** is
   `if (worldAccess_.has_value()) (void)worldAccess_->applyPending();`
   (`runtime_application.cpp:1182`), followed by the
   `platform::processEvents()` pump (`:1184`).
3. **Drawing.** `Renderer::drawFrame(CommandList&, RenderTarget& colorTarget,
   Texture& depth, Buffer& camera, drawItems, ResourceState finalColorState,
   HdrColorTarget&, …, bloom)` (`renderer.h:139`) draws into any
   `RenderTarget`. Runtime passes the swapchain target with `PresentSource`
   (`runtime_application.cpp:1811`).
   - **Output transform.** Runtime creates only the output-transform
     pipeline matching the swapchain format (`outputTransformUnorm/Srgb`,
     `:1400-1405`, `:1852-1859`).
   - **Window-sized targets.** The depth, HDR and bloom targets are
     window-sized.
4. **Offscreen and readback:**
   - `Device::createOffscreenTarget({extent, format})` (`device.h:80`,
     `types.h:225`) and `OffscreenTarget::acquireTarget()`;
   - `CommandList::copyRenderTargetToBuffer(RenderTarget&, Buffer&)`
     (`command_list.h:89`) into a `BufferPurpose::Readback` buffer
     (ADR-0040, the Spec 0010/0011 precedent).
5. **WSI precedent** (ADR-0106 D7):
   - `src/vulkan_backend/src/wsi/win32_surface.cpp` and `android_surface.cpp`
     are private to Vulkan Backend, selected per platform, with no Platform
     dependency and no OS type in a public header (module_boundaries.md,
     Atlantis Vulkan Backend).
6. **Stb:**
   - `cmake/AtlantisStb.cmake` declares the header-only INTERFACE
     `Stb::Stb`, included unconditionally at top level. In production code
     only the asset cooker links it.
   - `STB_IMAGE_WRITE_IMPLEMENTATION` is defined in
     `tests/image_regression/support/png_codec.cpp` and in the asset
     cooker's own TU.
7. **Logging.** Core's `log::initialize(std::shared_ptr<LogSink>)` replaces
   the sink, and `LogSink::write(level, message)` is the interface
   (`log.h:27-43`).
8. **The north-star light.** `assets/bistro/bistro_overlay.scene.txt:6`:
   `6b63b12c-9cde-4ae2-8391-c0b4cefadb7d`, `parent=none`, position
   (-39.615, 3.255, -5.032), `light=point`, intensity 12, range 6. The
   default scene's light is `0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea`, a
   Directional light with intensity 3.
9. **Bistro gating:** `ATLANTIS_RUNTIME_BISTRO_IMPORT_GUID`
   (`src/runtime/CMakeLists.txt:255`) and the test `SKIP`
   (`frame_input_equivalence_tests.cpp:200`).
10. **No frame counter.** `RuntimeApplication` has none.

## Plan-stage decisions

These are details Spec 0055 leaves to the Plan. None changes a Spec
requirement or an ADR-0106 decision, except as J1 proposes.

**P1 — Files and modules.**

| Where | Contents |
|---|---|
| `src/connection/include/atlantis/connection/runtime_control.h` (new) | the `RuntimeControl` interface and its value types (`RuntimeStatus`, `StepRequest`, `FrameReport`, `Diagnostic`); no Runtime type (P4) |
| `src/connection/include/atlantis/connection/json.h` + `src/connection/src/json.cpp` (new) | a minimal JSON value, writer and parser shared by the CLI's `--json` and Remote's wire codec (J2) |
| `src/remote/` (new module, **Atlantis Remote**) | `include/atlantis/remote/remote_client.h` (`connectRemote()` → `RemoteConnection` + `RemoteControl`); `include/atlantis/remote/remote_server.h` (`RemoteServer`); `include/atlantis/remote/session_file.h`; private `src/codec.*` (wire ↔ calls), `src/os/socket.h` with `socket_win32.cpp` / `socket_posix.cpp` (the WSI analogy: one private header, one TU per OS, selected in CMake; no OS type above it) |
| `src/remote/CMakeLists.txt` | `atlantis_remote_client` (`Atlantis::RemoteClient`) and `atlantis_remote_server` (`Atlantis::RemoteServer`), both PUBLIC `Atlantis::Connection`; `ws2_32` PRIVATE on Windows |
| `src/runtime/include/atlantis/runtime/runtime_control_host.h` + `src/runtime/src/runtime_control_host.cpp` (new) | `RuntimeControlHost`: implements `RuntimeControl` over a `RuntimeApplication&`, counts frames, runs step state and the diagnostics ring (P5, P6) |
| `src/runtime/src/frame_capture.h/.cpp` (new, private) | frame-data assembly and offscreen render + readback + PNG (P8) |
| `src/runtime/include/atlantis/runtime/runtime_application.h`, `src/runtime/src/runtime_application.cpp` | `setCommandsHeld(bool)`, `captureFrame(CaptureOptions)`, `sceneGuid()`; the one `runFrame()` statement (P7) |
| `src/runtime/main.cpp`, `cli.h/.cpp`, `CMakeLists.txt` | `--listen <port>` and `--session-file <path>`; the main loop; the links |
| `src/cli/` | the new tree, `--json`, filters, `tx`, `repl`, aliases (P9); `atlantis_cli_app` (output name `atlantis`) linking `Atlantis::Cli` + `Atlantis::RemoteClient` (P10) |

**Dependency graph changes** (written out, per the brief):

- **Runtime host** links `Stb::Stb` PRIVATE, an existing target with no new
  third-party dependency. The TU defines `STB_IMAGE_WRITE_STATIC` with
  `STB_IMAGE_WRITE_IMPLEMENTATION`, so no symbol can collide with
  `png_codec.cpp` or the cooker in any binary.
- **`atlantis_runtime`** links `Atlantis::RemoteServer`.
- **`atlantis_runtime_host`** does not link Remote. `RuntimeControlHost`
  needs only Connection's interface.
- **The CLI library `atlantis_cli`** still links only `Atlantis::Connection`,
  so the 0054 R8 scan is unchanged. `json.h` is in Connection, which is
  why J2 recommends it.

**P2 — Wire protocol** (ADR-0106 D1, D2; ruling Q8 E-a). One JSON object
per line, UTF-8, with a maximum line of 1 MiB.

```
→ {"id":0,"method":"hello","params":{"protocol":"atlantis.remote/1","token":"<32 hex>"}}
← {"id":0,"result":{"protocol":"atlantis.remote/1","scene":"<asset guid>"}}         | error + close
→ {"id":n,"method":"connection.getProperty","params":{"address":{...}}}
← {"id":n,"result":{"ok":<PropertyValue>}}  |  {"id":n,"result":{"err":"ComponentMissing"}}
← {"id":n,"error":{"code":"BadRequest","message":"..."}}                              (protocol errors only)
```

- **Methods** map one-to-one to the interfaces:
  - `connection.{findEntity, listEntities, listComponents, getProperty,
    schema, submit, submitTransaction, subscribe, unsubscribe, drainEvents,
    drainFailures}`;
  - `control.{status, pause, resume, step, diagnostics}`.

  No other method exists. The command engine stays client-side.
- **Encoding on the wire** (tagged, so variant alternatives survive):
  - `PropertyValue` is one of `{"u64":"…"}`, `{"f32":x}`, `{"vec3":[…]}`,
    `{"vec4":[…]}`, `{"assetGuid":"…"}`, `{"entityGuid":"…"}`,
    `{"enum":n}` or `{"absent":true}`;
  - floats are JSON numbers (`%.9g`), and non-finite floats are the strings
    `"nan"` / `"inf"` / `"-inf"`, so `NonFiniteValue` remains reachable;
  - `TypeId`/`FieldId` are `"0x"` plus 16 hex digits;
  - commands and events are `{"kind":"SetProperty", …}`;
  - an in-band `Result<T, AccessError|ConnectionError>` is `{"ok":…}` or
    `{"err":"<Name>"}`.
- **`schema()`** is fetched once at connect. `RemoteConnection` owns the
  decoded copy (strings and spans into its own storage), valid for its
  lifetime, per Spec 0054 ruling Q8 K1.
- **Token.** It is checked on `hello` with a constant-time compare. A wrong
  token, a wrong protocol or a missing hello closes the connection after
  the error.

**P3 — The server** (ADR-0106 D1; R2, non-functional robustness).

- **Constructor.** `RemoteServer(std::function<std::unique_ptr<RuntimeConnection>()>
  openConnection, RuntimeControl& control)`: one InProcess connection per
  accepted client.
- **`poll()`**, called by the host after each `runFrame()` (non-blocking,
  zero-timeout `select`):
  1. accept;
  2. read into each client's buffer;
  3. dispatch every complete line in order;
  4. flush write buffers.
- **Parking.** A frame-dependent request (`control.step`) becomes a
  *parked* entry `{client, id, kind, deadline frame, payload}` in one FIFO
  vector. `RuntimeControlHost` resolves it after the frame it waits for.
  Everything else answers in the same poll.
- **Limits:**
  - read buffer: 1 MiB per line;
  - write buffer: 64 MiB per client (a Bistro `schema` or `listEntities`
    fits);
  - exceeding either disconnects the client and drops its parked entries.
- **Disconnect.** It drops the client's InProcess connection, so its later
  failures are dropped (0054 J4) and its subscriptions end.
- **No thread, no blocking call.** A poll with no input does no I/O wait.
- **`--listen <port>`** (0 means ephemeral) binds `127.0.0.1` only.
  Without the flag no socket is created.

**P4 — Session file** (ADR-0106 D1).

- **Location.** `--session-file <path>`, defaulting to
  `<working dir>/.atlantis/runtime.session.json`.
- **Content:**

  ```
  {"protocol":"atlantis.remote/1","port":<n>,"token":"<32 hex>","pid":<n>,"scene":"<guid>"}
  ```

- **Writing.** It is written after the listen socket is bound and the
  scene is loaded, by a write-then-rename so a reader never sees a partial
  file. It is removed on clean shutdown.
- **The token** is 128 bits from `std::random_device`.
- **Client resolution order:**
  1. `atlantis --session <path>`;
  2. `ATLANTIS_SESSION`;
  3. `./.atlantis/runtime.session.json`.

  No file gives connection error, exit code 3.

**P5 — `RuntimeControl`** (ADR-0106 D3; R3).

```cpp
struct RuntimeStatus { bool paused; std::uint64_t frame; asset_system::AssetGuid scene; };
struct StepRequest { std::uint32_t frames = 1; std::optional<std::string> imagePath; };  // frame data always
struct FrameReport { std::uint64_t frame; bool applied; FrameData data; std::optional<CapturedImage> image; };
struct Diagnostic { std::uint64_t sequence; Severity severity; std::string message; };
class RuntimeControl {
  virtual RuntimeStatus status() = 0;
  virtual void pause() = 0;  virtual void resume() = 0;
  virtual void step(StepRequest, std::function<void(Result<FrameReport, ControlError>)> done) = 0;  // completes after the frames
  virtual std::vector<Diagnostic> diagnostics(std::uint64_t afterSequence, std::size_t max) = 0;
};
```

- **`FrameData`:**
  - directional and point lights as `FrameLightingData` holds them
    (direction or position, colour, intensity, range);
  - the camera's view and projection (16 floats each);
  - the draw-item count.

  These are Connection value types, converted by Runtime.
- **Step semantics:**
  - **Paused:** `step(n)` releases exactly n frames of command
    application.
  - **Running:** it waits n frames.
  - **Either way,** it completes after the n-th frame with that frame's
    report.
- **Pause and resume** are idempotent.
- **`RemoteControl`** implements the same interface over the wire.
  `step`'s callback runs when the parked response arrives.

**P6 — Diagnostics ring** (R3, R6).

- **Installation.** `RuntimeControlHost` installs a tee `LogSink`: it
  forwards to the console sink unchanged, and records Warn and above into
  a ring of 4096 entries with monotonically increasing sequence numbers,
  plus a dropped count.
- **Who installs it.** Only the host does, when `--listen` is given.
  Without it the sink is untouched.
- **Reading.** `diagnostics(after, max)` returns the entries after a
  sequence.
- **Use by the CLI.** It records the newest sequence at connect, and
  attaches entries newer than that to the envelope's `diagnostics`.
  `runtime step` attaches those produced during its frames.
- **Bistro's flood.** Bistro logs about 109,000 `checkConformalTransform()`
  error lines in a 40 s Release run (Spec 0054 M6 record). The ring keeps
  the newest 4096 and reports `dropped`.

**P7 — `runFrame()`'s first statement: the hold guard** (ADR-0106 D4; R3,
R10). The **one named exception** to "`runFrame()` unchanged".

```cpp
if (worldAccess_.has_value() && !commandsHeld_) (void)worldAccess_->applyPending();
```

- **The flag.** `commandsHeld_` is a new `bool` member, default `false`,
  set only by `setCommandsHeld()`.
- **Who sets it.** `RuntimeControlHost::beforeFrame()` sets it from the
  pause and step state. `main.cpp` calls `beforeFrame()` /
  `afterFrame()` only when `--listen` is given.
- **Proof that the default path is unchanged:**
  1. **Diff guard.** Every gate extracts `runFrame()`'s body from base and
     from HEAD. They must differ in exactly that one line, and the new
     line must equal the text above.
  2. **Reachability.** No code path sets `commandsHeld_` without
     `--listen` (code review, and a CPU test that `setCommandsHeld` is the
     only writer).
  3. **Behaviour.** The full Debug + Release suites run, including the GPU
     smoke tests and the 0054 north star, which assert exact
     `FrameLightingData` after an apply.
  - **What goldens do not prove.** The image goldens come from the
    headless fixture, which does not use `RuntimeApplication`, so they
    cannot see this change. They stay byte-identical regardless, and
    proofs 1–3 carry this guard (J3).

**P8 — Capture** (ADR-0106 D5; ruling Q2 C1 + C2; R3, R8).

- **`RuntimeApplication::captureFrame(CaptureOptions)`** is called by
  `RuntimeControlHost::afterFrame()` when a step with capture completes,
  between frames.
  - **Frame data.** It recomputes the frame's inputs from the baked world
    with the same shared functions `runFrame()` uses (`collect*()`, the
    lighting and camera extraction). The world has not changed since that
    frame applied, so the data equals the frame's own.
  - **Image (optional):**
    1. `device_->waitIdle()`;
    2. an `OffscreenTarget` of the presentation extent in the swapchain's
       format, so the existing output-transform pipeline and the
       window-sized depth, HDR and bloom targets are reused;
    3. one `drawFrame(…, finalColorState = TransferSource, …)` into it;
    4. `copyRenderTargetToBuffer()` into a Readback buffer;
    5. submit and wait;
    6. BGRA→RGBA swizzle if the format is BGRA;
    7. `stbi_write_png` to the requested path.
  - **Duplication.** The draw-argument assembly duplicates the part of
    `runFrame()` that builds `drawFrame`'s arguments, in a private helper
    in `frame_capture.cpp`. `runFrame()` itself is not refactored (P7).
    An equivalence test pins the duplication (Verification).
- **Cost:** one wait-idle and one extra render, on capture frames only.

**P9 — The CLI** (rulings Q3–Q6; R4–R7, R10).

- **The tree.** As Spec 0055's Goals list it.
- **Aliases.** `world entities` → `entity list`, `entity get` → `property
  get`, `entity set` → `property set`. The aliases' and the 0054 commands'
  human output is byte-identical to 0054's, so the 0054 CLI tests and
  `north_star.txt` pass unmodified.
- **Filters (J5):**
  - `--with <Type>` (repeatable; component presence, AND);
  - `--where <Type>.<field>=<value>` (repeatable, AND). Vector values are
    comma-separated (`Light.color=1,0.8,0.55`). Equality is exact on the
    parsed `PropertyValue`, and float equality is bitwise after parse.

  Filters are evaluated client-side with pipelined `listComponents` /
  `getProperty`.
- **`entity create [<guid>] [--component <Type>]…`:**
  - without a GUID it generates an RFC 9562 v4 GUID client-side
    (`std::random_device`);
  - with components it submits one transaction (create plus adds).
- **`atlantis tx <file|->` (J6):**
  - **Input.** Write commands only, one per line, with `#` comments and
    blank lines skipped.
  - **Validation first.** Every line is parsed before anything is
    submitted, and any error exits 2.
  - **Submission.** One `submitTransaction()`.
  - **Result.** `{ticket:{first,count}, committed:true, events:[…]}` or
    `{committed:false, at:<n>, error:"<AccessError>"}`.
- **`atlantis repl [--json]` (J7):**
  - one connection and control session;
  - the prompt `atlantis> ` goes to **stderr**, so stdout stays pure
    output;
  - `exit`, `quit` or EOF ends it;
  - a command's error does not end the session.
- **Write outcomes.** A write command submits, then polls
  `drainFailures`/`drainEvents` on a subscription opened for it. Each poll
  is answered at a frame boundary, until its ticket resolves, with a 10 s
  timeout (exit 4).
- **`--json` and `--diagnostics=json`.** Exactly Spec 0055 ruling Q3's
  envelope, error shape, exit codes, value encoding and per-command
  fields.
  - **Formatting.** Keys are emitted in a fixed order, and floats with
    `%.9g`, so the same command on the same world gives the same bytes.
  - **Errors.** Human mode prints `error: …` on stderr.

**P10 — The `atlantis` executable** (ruling Q6 X-a).

- **Build.** `atlantis_cli_app` in `src/cli/CMakeLists.txt` (`OUTPUT_NAME
  atlantis`), Windows-only like `atlantis_runtime`. It links `Atlantis::Cli`
  and `Atlantis::RemoteClient`.
- **Startup.** `main` resolves the session (P4), connects, and runs one
  invocation or `repl`.
- **The library's links are unchanged.** The executable's link list is
  separate from `atlantis_cli`'s, so the R8 link scan still reads exactly
  `Atlantis::Connection` for the library.

**P11 — The host loop** (`main.cpp`).

```
while (app.shouldContinue()) {
  if (control) control->beforeFrame();      // hold/step decision (P7)
  app.runFrame();
  if (control) control->afterFrame();       // frame count, step completion, capture (P8)
  if (server) server->poll();               // P3
  if (runner) runner->step();               // 0054 --exec, unchanged
}
```

- **`--listen` and `--exec`** may be combined.
- **`android_main.cpp`** is untouched.

## Milestones / Task Breakdown

Eight milestones. Commit prefixes: `feat:` for code, `test:` for test-only
steps, `docs:` for docs.

Every gate runs:

- a Debug + Release build;
- the full `ctest` in both configurations, with every golden compared
  exactly;
- the golden-directory guard;
- the path guard: the Plan's file list, with `src/runtime/` limited to the
  files above;
- the **`runFrame()` diff guard** (P7): byte-identical before M4, and
  exactly the one named line from M4 on;
- Android `assembleDebug`, whose targets add `atlantis_remote_client` and
  `atlantis_remote_server` from M1 (J8).

GPU suites run under fatal VVL.

**Stop rule:** a moved golden stops the work and is reported, never
re-captured.

### M1 — JSON, codec, socket layer, module skeleton (R2)

1. `json.h/.cpp` in Connection (P1, J2).
2. Atlantis Remote: the private `os/socket.*` (WSI-style per-OS TUs) and
   `codec.*` (P2). CMake for both targets; top-level `add_subdirectory`.
3. Tests (`tests/remote/`, new target):
   - JSON round-trip;
   - codec round-trip of every `PropertyValue` alternative, every command
     and event kind, `Result` errors, tickets and schema descriptors;
   - non-finite floats.
4. **Boundary scan** (`tests/remote/remote_boundary_tests.cpp`):
   - `src/remote/include/**` names no OS header or type (`winsock2.h`,
     `sys/socket.h`, `SOCKET`, …);
   - OS headers appear only under `src/remote/src/os/`;
   - Remote includes no Runtime, ECS, Platform, RHI or Renderer header.

*Gate:* standard.

### M2 — Client, server, `--listen`, session file (R1, R2)

1. `RemoteServer` (P3), the `RemoteConnection` client (P2), the session
   file (P4).
2. `--listen <port>` and `--session-file <path>` in `src/runtime/cli.h/.cpp`;
   `main.cpp` P11 (server only); `atlantis_runtime` links
   `Atlantis::RemoteServer`.
3. Tests, CPU-only, over a baked fixture scene with a real loopback
   socket:
   - **Equivalence.** The Spec 0054 connection suite's cases run against
     a `RemoteConnection` and give results equal to InProcess:
     - queries, commands and transactions;
     - subscriptions and filters;
     - failure routing across two remote clients;
     - `UnknownSubscription`.
   - **Protocol behaviour:**
     - pipelining;
     - partial and oversize lines;
     - bad token, bad protocol, missing hello;
     - disconnect mid-request.
   - **No blocking.** `poll()` with no client returns without waiting.
4. **The in-process test harness (J4).** `RemoteConnection` takes an
   optional `whileWaiting` callback. A test passes `[&]{ runFrame-or-apply();
   server.poll(); }`, so client and server run in one thread.
5. **`tests/runtime/cli_tests.cpp`** gains `--listen` and `--session-file`
   parse cases.

*Gate:* standard. `runFrame()` stays byte-identical.

### M3 — `RuntimeControl` and diagnostics (R3)

1. `runtime_control.h` (P5); `RuntimeControlHost` (P5, P6) with frame
   counting and the diagnostics tee; `RemoteControl` over the wire;
   `main.cpp` wires `beforeFrame`/`afterFrame` under `--listen`.
2. Tests:
   - status, idempotent pause/resume, step-while-running completes after
     n frames (no hold yet);
   - the diagnostics ring: sequences, cursor, the 4096 bound and the
     dropped count;
   - the sink is untouched without `--listen`;
   - control over the wire.

*Gate:* standard. `runFrame()` stays byte-identical.

### M4 — The hold guard (R3, R10)

1. P7: `commandsHeld_`, `setCommandsHeld()`, the one `runFrame()`
   statement.
2. Tests:
   - paused: commands stay pending across frames, with no events;
   - `step(n)` applies exactly n times;
   - `resume` returns to per-frame application;
   - the not-held path: every 0054 and smoke test passes unmodified;
   - `setCommandsHeld` is the flag's only writer.
3. **The gate's `runFrame()` diff guard** switches to "exactly the named
   line".

*Gate:* standard, plus the named-line diff guard.

### M5 — Capture (R3, R8)

1. `frame_capture.*`, `RuntimeApplication::captureFrame()`, `sceneGuid()`
   (P8). `atlantis_runtime_host` links `Stb::Stb` PRIVATE.
2. GPU tests, under fatal VVL:
   - **frame data equals the frame.** The capture's `FrameData` equals the
     frame's `FrameLightingData` and camera bytes, read through the
     existing smoke friend;
   - **image determinism.** Two captures of an unchanged world are
     byte-identical;
   - **image sensitivity.** The image changes after an intensity edit;
   - the PNG decodes at the presentation extent.

*Gate:* standard.

### M6 — The CLI tree, `--json`, filters, `tx`, REPL (R4–R7, R10)

1. P9 in `src/cli/`.
2. Tests (`tests/cli/`):
   - **Expected human text and expected JSON** (J-a) for every command on
     the fixture scene, through InProcess:
     - `schema list/inspect`;
     - `world list/inspect`;
     - `entity list/inspect/create/destroy`;
     - `component add/remove`;
     - `property get/set`;
     - `runtime pause/resume/step`, through a fake `RuntimeControl`.
   - **Error paths:**
     - every error category's JSON;
     - `--diagnostics=json` stderr lines;
     - exit codes 0–4.
   - **Filters:** `--with`, `--where` and combined.
   - **`tx`:** commit, and abort at n.
   - **Determinism:** the same command gives identical bytes twice.
   - **Unchanged.** The 0054 CLI tests and `north_star.txt` pass
     unmodified (aliases).

*Gate:* standard.

### M7 — The `atlantis` executable, the north star, the completion loop (R1, R8, R9)

1. P10, plus the REPL in the executable.
2. **In-process north star** (A-a), using the J4 harness, under fatal VVL,
   as a new TEST_CASE in `runtime_smoke_gpu_tests.cpp`:
   - **Default-scene twin** (always runs):
     1. `entity list --with Light --where Light.kind=Directional --json`
        finds `0b2c1db2-…`;
     2. `property get` gives 3;
     3. `property set … 6`;
     4. `runtime pause` then `runtime step --capture <tmp.png>`;
     5. `frameData.directionalLights[0].intensity == 6` exactly, and the
        image differs from the pre-edit capture.
   - **Bistro** (`SKIP` without content): the same with
     `--where Light.kind=Point` finding `6b63b12c-…`. Intensity goes 12 →
     24. The point light whose position is (-39.615, 3.255, -5.032) has
     intensity 24 exactly, and the image differs.
3. **Two-process ctest** (A-b, L-a), `tests/cli/cli_e2e_tests.cpp`:
   - **Launch:** `atlantis_runtime --listen 0 --session-file <tmp>
     --scene integrated_showcase_demo` through a test-only Windows process
     helper (J9).
   - **Wait:** for the session file, up to 60 s.
   - **The eight completion steps, one assertion each:**
     1. `schema list --json`;
     2. `world inspect --json`;
     3. `entity list --with Light --json`;
     4. `property set …`;
     5. `runtime pause`;
     6. `runtime step --capture`;
     7. the envelope's `diagnostics`;
     8. verify `frameData`.
   - **REPL smoke:** pipe three lines into `atlantis repl --json` and check
     three envelopes.
   - **Graceful close:** `WM_CLOSE` to the runtime's window (found by PID),
     then a wait for exit code 0. `TerminateProcess` after 30 s is a test
     failure, not a pass.
4. **Docs (J6, normative).**
   - AGENTS.md's top-level module list gains Atlantis Remote, with its
     dependency rules and the WSI-style private socket layer.
   - `module_boundaries.md` gains an Atlantis Remote section's dependency
     lines and Runtime's "Depends on" (`atlantis_runtime` links
     RemoteServer; the host links `Stb::Stb`).

*Gate:* standard.

### M8 — Acceptance

1. Final path guard, the `runFrame()` named-line guard, and diff review.
2. **Human north star:**
   - on Bistro (content present): `atlantis_runtime --scene bistro
     --listen 0`;
   - in a second terminal, the `atlantis` commands of the north star,
     with before and after captures;
   - the transcript (commands plus JSON) is recorded.
3. **The Plan 0052 J9 runs repeated** (J10):
   - Windows whitelist scenes without `--listen`, Debug and Release, VVL
     on;
   - the Android emulator default scene.

*Gate:* standard, plus the runs.

## Files / Modules Touched (expected)

**New:**

- `src/remote/**`;
- `src/connection/include/atlantis/connection/{runtime_control.h,json.h}`,
  `src/connection/src/json.cpp`;
- `src/runtime/include/atlantis/runtime/runtime_control_host.h`,
  `src/runtime/src/runtime_control_host.cpp`,
  `src/runtime/src/frame_capture.{h,cpp}`;
- `src/cli/src/` new command files and the executable's `main`;
- `tests/remote/**`;
- `tests/cli/` new test files (commands JSON, filters, `tx`, e2e);
- `tests/runtime/` control and capture tests (new file).

**Changed (planned, not deviations):**

- `CMakeLists.txt` (top level: `src/remote`, `tests/remote`);
- `src/connection/CMakeLists.txt` (`json.cpp`);
- `src/cli/CMakeLists.txt` (new sources; `atlantis_cli_app` — the
  `atlantis_cli` link list unchanged);
- `src/cli/include/atlantis/cli/command.h`, `src/cli/src/command.cpp` (tree
  growth, aliases);
- `src/runtime/include/atlantis/runtime/runtime_application.h`,
  `src/runtime/src/runtime_application.cpp`:
  - `setCommandsHeld`, `captureFrame`, `sceneGuid`, `commandsHeld_`;
  - **one `runFrame()` line** (P7);
- `src/runtime/main.cpp`, `src/runtime/cli.h`, `src/runtime/cli.cpp`,
  `src/runtime/CMakeLists.txt`;
- `tests/runtime/cli_tests.cpp`, `tests/runtime/runtime_smoke_gpu_tests.cpp`
  (new TEST_CASEs only), `tests/runtime/CMakeLists.txt`,
  `tests/cli/CMakeLists.txt`;
- `android/app/build.gradle` (two targets, J8);
- `AGENTS.md`, `docs/architecture/module_boundaries.md` (J6, normative
  only).

**Not touched:**

- `runFrame()` other than P7's line;
- `RuntimeWorldAccess`, the ECS, the Connection endpoint's semantics;
- the existing 0052–0054 tests other than the additive TEST_CASEs above;
- render extraction, the Renderer, RHI, the Vulkan Backend, Platform,
  `android_main.cpp`;
- assets, shaders, goldens.

No third-party dependency is added. `Stb::Stb` is existing.

## Sequencing & Dependencies

- **M1 → M2:** the client and server need the codec.
- **M2 → M3:** control rides the transport.
- **M3 → M4:** step needs the hold guard to be exact.
- **M4 → M5:** a capture follows a step.
- **M5 → M6:** `runtime step --capture` reports a capture.
- **M6 → M7:** the executable and the end-to-end tests use the full tree.
- **M8** comes last.

## Verification Checklist

This maps to Spec 0055's Testing & Verification Plan.

- [ ] **R1 (M2, M7):** `--listen`; the session file; one-shot and REPL
  attach.
- [ ] **R2 (M1, M2):**
  - codec round-trips;
  - `RemoteConnection` ≡ InProcess on the 0054 suite;
  - pipelining, limits and disconnects;
  - no blocking.
- [ ] **R3 (M3–M5):**
  - status, pause/resume and step semantics;
  - hold exactness;
  - the diagnostics ring;
  - capture frame-data equality, image determinism and sensitivity.
- [ ] **R4 (M6):** the full tree, filters and `entity create` GUID
  generation.
- [ ] **R5, R6 (M6):**
  - expected JSON per command and byte determinism;
  - error JSON, `--diagnostics=json` and exit codes.
- [ ] **R7 (M6):** write outcomes and `tx` commit/abort.
- [ ] **R8 (M7, M8):**
  - the in-process north star on the default scene (always) and on Bistro
    (gated), with exact intensities;
  - the human Bistro run.
- [ ] **R9 (M7):** the two-process completion loop, eight assertions.
- [ ] **R10 (every gate):**
  - the `runFrame()` guard;
  - full suites and goldens;
  - 0054 tests and `north_star.txt` unmodified;
  - no socket without `--listen`.
- [ ] **Path guard, every gate:** the files above.
- [ ] **Android:** `assembleDebug`, which compiles both Remote halves and
  the CLI.

## Risks

- **The first `runFrame()` change since 0052.** P7's three-part proof
  carries it; goldens alone cannot (J3).
- **Duplicated draw-argument assembly in capture** (P8). It is pinned by
  the frame-data equality and image-determinism tests. A future
  `runFrame()` change must update both.
- **A listening socket.** It is opt-in, loopback-only and token-gated.
  The session file is written to the working directory (`.atlantis/`).
- **Two-process tests on CI-less machines.** They need a GPU and a window,
  like the existing GPU suites. Bistro variants skip without content.
- **Bistro's log flood** saturates the diagnostics ring every frame. The
  ring reports `dropped` (P6).

## Joint Review decisions (recommendation first)

- **J1 — None proposed against the Spec.** The Plan stays within Spec
  0055's rulings. The first `runFrame()` change is the one ADR-0106 D4
  decided, and it is held to P7's guard.
- **J2 — Where JSON lives.**
  - **Recommended:** a minimal JSON value, writer and parser in Atlantis
    Connection, shared by the CLI's `--json` and Remote's codec. The CLI
    library then keeps linking only Connection (0054 R8), and no
    third-party JSON dependency is added.
  - **Alternatives:**
    - private copies in CLI and Remote (duplicated code);
    - a third-party JSON library (a new dependency, against the brief).
- **J3 — The proof that the default path is unchanged** (P7):
  - the named-line diff guard at every gate;
  - the flag's single writer;
  - the full suites with the exact-`FrameLightingData` GPU tests.

  This states explicitly that the image goldens (from the headless
  fixture) cannot observe `runFrame()`.
- **J4 — The in-process harness.** `RemoteConnection` takes an optional
  `whileWaiting` hook, so a single-threaded test drives frames and
  `server.poll()` while the client waits. Production passes none and
  waits on the socket.
- **J5 — Filter syntax.**
  - `--with <Type>` and `--where <Type>.<field>=<value>`, repeatable and
    ANDed;
  - comma-separated vectors;
  - exact equality after parse;
  - client-side and pipelined.
- **J6 — Docs.**
  - **Normative sentences in the implementation PR** (the Plan 0054 J6
    split): AGENTS.md's module list and Remote's dependency rule, and
    `module_boundaries.md`'s Remote section and Runtime "Depends on".
  - **Narratives post-merge:** the blueprint, the registry's
    Implementation column, Spec 0055's Related Plan.
- **J7 — The REPL.** The prompt goes to stderr; `exit`, `quit` or EOF
  ends it; errors do not end the session.
- **J8 — Android build targets.** Gradle `targets` gain
  `atlantis_remote_client` and `atlantis_remote_server`, so both halves
  compile on the NDK. Android attach itself remains out of scope.
- **J9 — The two-process test's process control.**
  - **Start:** a test-only Windows helper (`CreateProcessW` with pipes)
    starts and reads the processes.
  - **Close:** `WM_CLOSE` to the runtime's window closes it gracefully.
  - **Exit code:** a 0 exit is asserted, and a forced kill is a failure.
  - **Alternative:** a `control.shutdown` method. That is a new verb, so it
    is not chosen.
- **J10 — Acceptance runs.**
  - **Recommended:**
    - the human north star on Bistro;
    - the Plan 0052 J9 runs repeated (Windows whitelist without
      `--listen`; the Android emulator default scene).
  - **Why:** `runFrame()`, `RuntimeApplication` and `main.cpp` change.

## Rollback Plan

- **After merge:** revert the implementation PR as a whole.
  - Remote, control and capture are additive.
  - `runFrame()`'s single line reverts with it.
  - No format, asset or golden changes.
- **Before merge:** revert milestone by milestone, in reverse order.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:

- [ ] Every golden exact in Debug and Release at the final gate. None
      re-captured.
- [ ] `runFrame()` differs from base in exactly P7's line; the path guard
      holds.
- [ ] The 0054 tests and `north_star.txt` pass unmodified.
- [ ] Boundary scans green: CLI (0054 R8), Remote (no OS types in public
      headers; OS code only in `src/os/`).
- [ ] The post-merge docs items (J6) are queued.
