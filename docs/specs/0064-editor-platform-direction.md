# Spec: Editor Platform Direction — an Out-of-Process C#/Avalonia Editor

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-20
- **Related Plan(s):** [Plan 0064](../plans/0064-editor-platform-direction.md) (`Approved`; Phase 1 only; Joint
  Human Review 2026-10-20 recorded in its header). As with Specs 0062 and 0063, the Plan was drafted together with
  this Spec at the maintainer's direction.
- **Approval:** slmao, 2026-10-20 — owner rulings Q1–Q6, each as its recommendation (Q1 P-a; Q2 V-a + R-a, with
  V-a2 as the fallback inside ADR-0126; Q3 W-a; Q4 D-a; Q5 T-a; Q6 G-a), recorded once in
  [Plan 0064](../plans/0064-editor-platform-direction.md); review of this Spec's own branch PR, [PR #275](https://github.com/slmao/Atlantis/pull/275).
  The open-question text below keeps the options as the record of what was weighed.
- **Related ADR(s)** (all `Accepted` 2026-10-20, alongside this Spec's own Approval):
  - [ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md): the platform
    direction — an out-of-process C#/Avalonia editor as one more ordinary client, the one structured interface
    shared by GUI, scripts, CI and agents, and the ImGui editor's place (Q1, Q5).
  - [ADR-0126](../adr/0126-viewport-embedding-and-input-routing.md): Viewport embedding through a
    Platform-owned child window inside a foreign parent, and input routing between it and the editor (Q2).
  - [ADR-0127](../adr/0127-editor-application-managed-dependency-policy.md): the editor application's managed
    dependency policy — pinned by version and hash, offline-reproducible (Q4).
  - [ADR-0128](../adr/0128-remote-viewport-camera-and-raycast.md): the Viewport camera and ray-cast on the
    control surface and the wire, superseding in part ADR-0106 D3 and ADR-0123 D2/D4 (Q3).
  - Cited, unchanged: [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) /
    [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) (the closed World surface),
    [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md) /
    [ADR-0100](../adr/0100-scene-semantic-schema-layering-model-and-versioning.md) /
    [ADR-0102](../adr/0102-authoring-runtime-world-separation-and-scene-bake.md) (identity and layering),
    [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md) /
    [ADR-0111](../adr/0111-schema-generated-typed-bindings.md) /
    [ADR-0112](../adr/0112-csharp-gameplay-client-transport-and-bindings.md) (the schema as the one truth and
    its generated bindings), [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md),
    [ADR-0110](../adr/0110-gameplay-sdk-client-library-and-execution-model.md),
    [ADR-0113](../adr/0113-dotnet-toolchain-and-build-integration.md),
    [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md) /
    [ADR-0011](../adr/0011-native-window-handle-representation.md) /
    [ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md) (Platform, WSI and window ownership),
    [ADR-0107](../adr/0107-editor-ui-library-selection.md) / [ADR-0108](../adr/0108-editor-host-and-viewport-composition.md)
    (the ImGui editor), [ADR-0124](../adr/0124-editor-transaction-and-client-side-undo.md).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

The maintainer set the editor's platform direction on 2026-10-20: an **out-of-process C#/Avalonia editor**, one
more ordinary client of `RuntimeConnection` / `atlantis.remote/1` with no Runtime privilege; **one structured
interface** shared by the GUI, scripts, CI and AI agents; and the in-process **ImGui editor kept as a debug tool**.

This Spec records that direction and the decisions it forces, and commits to **Phase 1 only**: an investigative
prototype in the mould of Spec 0061's "investigation before technique", whose job is to answer two unknowns —
how the Runtime's Viewport is embedded in a foreign window, and how input is routed between that window and
Avalonia — with Bistro running in the embedded Viewport as the acceptance. Phases 2–4 (an editor games are
developed in, a tool ecosystem, AI-native) are written as background; nothing of them is designed here.

Everything an editor needs that the client surface lacks today is added to the **shared** surface, not to the
editor: a Viewport camera and a ray-cast on the control interface and the wire (reopening what Spec 0062 deferred
by name), reachable by the CLI, the SDKs and agents alike. The World's five commands and five events stay closed.

**North star (Phase 1):** `Atlantis.Editor` opens a window, launches `atlantis_runtime --listen`, the Runtime's
Viewport appears inside the editor's window as a child surface rendering Bistro at the Runtime's frame rate; a
right-drag over it orbits the camera (the editor sets the camera over the wire), a click selects the entity under
the pointer (the editor ray-casts over the wire) and the editor's list shows its GUID; the `atlantis` CLI does
the same ray-cast from a script. No Avalonia dependency exists in the repository before this Spec is approved.

## Motivation / Problem Statement

Read at `origin/main` `77197fb` (E2 merged, PR #274).

### What exists, and what this Spec builds on without reopening

| Fact | Where it is decided | This Spec's use of it |
|---|---|---|
| The World's verb surface is **five commands, five events, transactions**; additions are later Specs. | Spec 0052/0053, ADR-0103 D1, ADR-0104 | Kept closed. Nothing here is a World command, event or component (R6). |
| **Authoring scene ≠ Runtime World**; a node and its baked entity share one `EntityGuid`; properties are addressed by (GUID, `TypeId`, `FieldId`). | Spec 0049 R7, Spec 0051, ADR-0097 D5, ADR-0100 D7, ADR-0102 | The editor addresses everything by those ids; E3's authoring editing stays a later Spec (Non-Goals). |
| The **schema is the one truth**: hand-authored descriptor tables (`worldSchema()`, `assetSystemSchema()`), name-derived ids, bindings **generated from** the tables and committed with staleness checks. | Spec 0048, ADR-0099 D2–D4, ADR-0111 D1, ADR-0112 D3 | The editor consumes the schema through the C# SDK's generated bindings and the wire's `connection.schema`. No C# attribute, class or file may become a second source of truth (R7). |
| **One client interface** — `RuntimeConnection` (query, command, event, transaction) and `RuntimeControl` (status, pause, resume, step, diagnostics) — carried verbatim by `atlantis.remote/1`; the CLI, the C++ SDK and the C# SDK are its clients. | Spec 0054/0055/0057/0058, ADR-0105, ADR-0106 D2–D3, ADR-0110, ADR-0112 | The editor is one more client of exactly this (R1, R2). |
| The **C# SDK** is managed-only, .NET 10, **zero NuGet**, speaking the wire itself; `check_csharp_rules.cmake` scans `src/csharp/**`, `examples/csharp/**`, `tests/csharp/**`. | Spec 0058, ADR-0112 D1, ADR-0113 D2 | The SDK's rule is untouched; the editor application is a separate module with its own policy (Q4, ADR-0127). |
| **Platform owns the window**; `NativeWindowHandle` is a borrowed POD; Vulkan WSI consumes it to make a `VkSurfaceKHR` and never destroys the window. | ADR-0005 (amended), ADR-0011, ADR-0013 | Embedding keeps every one of these: the child window is Platform's (Q2, ADR-0126). |
| The **ImGui editor**: Dear ImGui core only, hosted by `atlantis_runtime --editor` through a frame overlay; its models are UI-free; E1 added camera/picking as host services, E2 an undo history. | Spec 0056/0062/0063, ADR-0107/0108/0123/0124 | Kept as the in-process debug tool, frozen after E2 (Q5). |
| **No managed runtime inside the Runtime process**; no custom components; game logic is client-driven. | Spec 0057 rulings Q5/Q6, ADR-0110 D3/D5 | Honoured: the editor is a separate process; C# components stay named-only (Q6). |
| **Remote attach** was the editor's deferred mode "once frames can cross processes"; capture-based frames measured at about 2 FPS; GPU sharing needs platform external memory. | Spec 0056 Q2 (c) | The reason Q2 prefers a child surface over streaming. |
| **Picking for remote clients and agents** and a **protocol-level camera** were deferred by name: "a separate Spec that would add a verb under its own ADR. This ADR does not pre-authorize it." | Spec 0062 Q2 K-d, ADR-0123 D2/D4 | Reopened here (Q3, ADR-0128). |
| The Runtime's `runFrame()` draws from the overlay's camera at one named place; captures follow the drawn camera. | Spec 0062 R3, ADR-0123 D1 | The wire camera uses the same place (ADR-0128 D1). |
| Dependencies are pinned fetches: ImGui and Tracy by URL + SHA-256, Catch2 by tag; host toolchains (Vulkan SDK, NDK, .NET) are installed, never fetched. | ADR-0006, ADR-0107, ADR-0113 D1, ADR-0116 D1 | The precedent ADR-0127's pinning follows. |
| The **editor process model** was explicitly undecided. | blueprint, Candidate 2 ("Editor process model … Not decided") | Decided here (Q1, ADR-0125). |

### Facts found while drafting

- **Windows Platform** creates one `WS_OVERLAPPEDWINDOW` top-level `HWND`
  (`src/platform/src/windows/windows_platform.cpp:355`), is per-monitor-DPI-aware v2 (`:318`), and emits input
  as plain values (ADR-0109). `WindowOptions` has one field, `requestedClientSize`. There is no way to create the
  window inside a foreign parent.
- **The session file** (`atlantis.remote/1` §1) is how a client finds a Runtime: `{protocol, port, token, pid,
  scene}`. It carries no window handle.
- **The C# SDK** has `RuntimeStatus`, `StepRequest`, `StepResult` (`Control.cs`): the control surface is already
  usable from C#.
- **The wire** has 16 methods (`remote_protocol.md` §5); none is a camera or a ray-cast.
- **Avalonia** (MIT) hosts a foreign native control through `NativeControlHost`, whose override returns a platform
  handle (on Windows, an `HWND`) that Avalonia positions and sizes within its visual tree. The hosted window is a
  Win32 child: it receives its own mouse messages when under the cursor and keyboard messages when focused —
  Avalonia does not see them. Whether Avalonia processes messages a child forwards to the top-level window, and
  how resize and DPI changes reach the child, are the unknowns Phase 1 exists to answer (Q2).

### Why a platform decision now

The ImGui editor has reached what Spec 0056 asked of it and two iterations beyond. Everything the maintainer
wants next — an editor games are developed in, tools, agents driving the same surface — asks whether to keep
growing a C++/ImGui application inside the Runtime process or to build the editor where its ecosystem is. The
maintainer chose the latter. Deciding it now, before E3, saves E3 being built twice.

## Goals

Maintainer-fixed (2026-10-20), not to be relaxed in review:

- **An out-of-process C#/Avalonia editor**, one more ordinary client of `RuntimeConnection` and
  `atlantis.remote/1`, with **no Runtime privilege**.
- **One structured interface** shared by the GUI, scripts, CI and AI agents.
- **The ImGui editor stays** as a debug tool.
- **Four phases** as background; **Phase 1 is an investigative prototype** answering the Viewport-embedding and
  input-routing unknowns, with Bistro as its acceptance.
- **No Avalonia dependency before this Spec is approved.**
- **Existing rulings are cited, not reopened:** the closed verb surface, authoring/runtime layering and GUID
  identity, and the schema as the one source of truth.

Goals of this Spec within those boundaries:

- Decide the platform shape, the embedding path, the input routing to investigate, the dependency policy, the
  wire additions and the dual-editor transition — each as an open question with a recommendation, ruled at review.
- Commit Phase 1's scope exactly, and nothing of Phases 2–4.

## Non-Goals

- **Phases 2–4** (editor features, authoring editing E3, tools, asset pipeline UI, agents' workflows): background
  only; no design, scaffold or placeholder.
- **A managed runtime inside the Runtime process**, C++/CLI, P/Invoke of engine code: excluded by Spec 0057 /
  ADR-0112 D1 and not reopened.
- **Any World command, event, query or component**; any change to `RuntimeConnection`.
- **Custom (C#) gameplay components**: named only (Q6).
- **Cross-platform editor hosting**: Phase 1 is Windows; Android hosts no editor (Spec 0056), and no macOS/Linux
  target exists (AGENTS.md).
- **Replacing or removing the ImGui editor**; **porting its C++ models to C#** in Phase 1.
- **A second serializer, scene format or protocol version.**

## Requirements

### Functional

- **R1 — An ordinary client.** The editor application (`Atlantis.Editor`, managed) reaches the Runtime only
  through `atlantis.remote/1` via the Atlantis C# SDK: `RuntimeConnection` for the World, `RuntimeControl` for
  lifecycle and the frame-side services this Spec adds. It links no native engine code and loads nothing into the
  Runtime process. Nothing it can do is unavailable to another client of the same wire.
- **R2 — One structured interface.** Every capability the editor needs is a method of the shared surface, with
  the CLI, the C++ client interface (`connection::RuntimeControl`, in process or through Remote's `RemoteControl`)
  and the C# SDK exposing it the same way. Phase 1 adds exactly two: a Viewport camera
  (`control.setViewportCamera`) and a ray-cast (`control.raycast`), each with CLI commands and conformance
  vectors. They are hand-written protocol methods, like capture, not schema-generated.
- **R3 — Embedded Viewport (Phase 1 unknown U1).** The Runtime renders into a surface that appears inside the
  editor's window: a Platform-owned child window created under a parent handle the Runtime is given at start
  (`--parent-window`). The Runtime still owns and destroys the child (ADR-0013); Vulkan WSI consumes it as today
  (ADR-0005). The child's handle is published in the session file so the editor can host it. Resize and DPI
  changes of the parent reach the child; Bistro renders in it at the Runtime's own frame rate.
- **R4 — Input routing (Phase 1 unknown U2).** Pointer and keyboard input over the embedded Viewport reaches the
  editor application, which owns navigation and picking (as E1 made the camera client state). Phase 1 measures the
  routes in Q2 and reports which works; the Spec recommends one and names the fallback.
- **R5 — Remote camera and picking (Q3).** A client may set the Viewport camera the Runtime draws from (pose and
  lens, the ADR-0123 D1 values) and clear it; a client may ask for the nearest `Renderable` hit by a world-space
  ray (the ADR-0123 D2 service). Both are `RuntimeControl` calls, carried by the wire; both are available to every
  client; neither touches `RuntimeConnection`.
- **R6 — The World surface is unchanged.** No command, event, query, component or `RuntimeConnection` method is
  added. The verb additions of R5 live on the control surface, like capture (ADR-0106 D3, D5).
- **R7 — The schema stays the one truth.** The editor reads types and fields from the generated C# bindings and
  from `connection.schema`. If a later Spec lets C# declare components (Q6), any attribute or class is an
  *input to the generator* that lands in a descriptor table; the table remains the truth and the bindings are
  generated from it (ADR-0099 D2/D4, ADR-0111 D1).
- **R8 — Dependency policy (Q4).** The editor application may take managed packages, each pinned to an exact
  version and content hash in a committed lock file, restored in locked mode, reproducible offline from a
  committed or mirrored feed; a test enforces the policy. The C# SDK's zero-NuGet rule is untouched.
- **R9 — Dual-editor transition (Q5).** The ImGui editor is frozen at E2 (fixes only). The Avalonia editor is
  the target of E3 and later, subject to Phase 1's verdict.
- **R10 — Phase 1 is investigative.** It ships the minimum that answers U1 and U2 on Bistro and reports its
  findings; the owner checkpoints the report before Phase 2 is specified (the Spec 0061 R1 shape).

### Non-functional

- **Frame rate:** the embedded Viewport presents at the Runtime's rate; the child-window path adds no copy.
- **Latency:** camera and ray-cast requests are answered at frame boundaries, like every wire call (ADR-0106 D1).
- **Threading:** the Runtime's frame thread is unchanged (ADR-0004); the editor is its own process.
- **Security:** the wire stays loopback-only with the session token (ADR-0106 D1). The parent-window option
  accepts a handle only from the command line of the process that started the Runtime.
- **Performance contract:** Phase 1 touches `src/runtime/**` (the control host, one camera line in `runFrame()`),
  the root `CMakeLists.txt` and `cmake/**`, all on the gate's applicability list; the implementation PR runs the
  gate and reports the verdict.

## Proposed Design

Under the recommended options:

```
Atlantis.Editor (managed; Avalonia; src/editor_app/)            ── Phase 1: a shell
  ├─ launches: atlantis_runtime --listen 0 --parent-window <hwnd> --scene bistro
  ├─ reads the session file: port, token, pid, scene, window   (ADR-0126 D3: the child handle)
  ├─ NativeControlHost  ─ hosts the Runtime's child HWND        (U1: embedding)
  ├─ input over the host  ─ routed per Q2 (R-a: forwarded by the child; R-b fallback: wire events)
  ├─ camera model (client state, E1's rules) → control.setViewportCamera   (ADR-0128)
  └─ click → control.raycast → entity GUID → list                          (ADR-0128)
         ▲ all through Atlantis.Gameplay (C# SDK) over atlantis.remote/1; no engine code loaded

atlantis_runtime
  ├─ Platform (Windows): child-window mode — WS_CHILD under the parent, Platform-owned (ADR-0126 D1)
  ├─ RuntimeControlHost: setViewportCamera / raycast (over the existing pick services)  (ADR-0128 D1–D2)
  ├─ runFrame(): the camera place takes the overlay's camera, else the control's      (ADR-0128 D1)
  └─ Remote: the two methods on the wire; CLI: `runtime camera`, `runtime raycast`
```

- **Uniform interface in practice.** The CLI gains the same two commands in Phase 1, so a script, CI job or agent
  can orbit and pick exactly as the editor does — the one-interface goal is a test, not a slogan.
- **The ImGui editor** is untouched by Phase 1 except that the camera place in `runFrame()` learns a second source
  (overlay first, control second).
- **What Phase 1 does not build:** Hierarchy, Inspector, Gizmo, undo in C#; authoring editing; any Phase 2 UI.

## Architectural Impact

Yes, in four decisions, each its own ADR (all `Accepted` 2026-10-20):

- **[ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md)** — the platform
  direction: a new managed module **Atlantis Editor App** (`src/editor_app/`), an ordinary client with no
  privilege; one interface for GUI, scripts, CI and agents; the ImGui editor frozen as the debug tool (Q1, Q5).
- **[ADR-0126](../adr/0126-viewport-embedding-and-input-routing.md)** — embedding: Platform's child-window mode
  under a foreign parent (ownership rules unchanged), the session file's window handle, and the input route Phase 1
  investigates (Q2).
- **[ADR-0127](../adr/0127-editor-application-managed-dependency-policy.md)** — the editor application's managed
  dependency policy (Q4).
- **[ADR-0128](../adr/0128-remote-viewport-camera-and-raycast.md)** — `setViewportCamera` and `raycast` on
  `RuntimeControl` and the wire, superseding ADR-0106 D3 in part (two more control calls) and ADR-0123 D2/D4 in
  part (the services reach the wire) (Q3).

**Expected code impact of Phase 1** (the Plan's file list is binding; nothing lands before approval):

- **New:** `src/editor_app/` (the Avalonia application), its tests and checks.
- **Changed:** Platform (Windows child-window mode, `WindowOptions`), Runtime (`--parent-window`, two control
  calls, the camera place, the session file field), Remote (two methods, conformance vectors, `remote_protocol.md`),
  CLI (two commands), the C# SDK (two control methods), AGENTS.md and `module_boundaries.md`.
- **Unchanged:** World, Connection's `RuntimeConnection`, RHI, Vulkan Backend's WSI contract, Renderer, Render
  Graph, shaders, formats, goldens, the ImGui editor library.

## Alternatives Considered

The open questions compare the options. Rejected across them:

- **An in-process managed editor** (the Runtime hosting .NET, or the editor loading the engine as a library):
  Spec 0057 excluded a managed runtime inside the Runtime, and an editor that links the engine is the privileged
  "big object" Spec 0056 excluded.
- **A web UI** (Spec 0056 Q1 U-c): the Viewport cost that rejected it is unchanged.
- **Growing the ImGui editor into the product editor:** the maintainer's direction excludes it.

## Testing & Verification Plan

Phase 1 verifies the unknowns and the additions; the rest is background.

- **Platform (GPU-free where possible):** child-window mode creates a `WS_CHILD` window under a given parent and
  destroys it on shutdown; without `--parent-window` nothing changes (every existing test and golden unchanged).
- **Wire and SDKs:** the two methods round-trip (conformance vectors regenerated and C# decoding them byte for
  byte, as ADR-0112 D2); the CLI's `runtime camera` and `runtime raycast` JSON envelopes; a bad camera is refused
  with the ADR-0123 D1 rules; a ray-cast on a Runtime without pick geometry answers "no geometry".
- **Runtime (fatal VVL):** with no overlay, a control-set camera draws the frame and `captureFrameData()` reports
  it; clearing it restores the scene camera byte for byte; the overlay's camera wins when both exist.
- **Prototype north star (manual, on the owner's machine, reported with numbers):** Bistro in the embedded
  Viewport: frame time versus `atlantis_runtime` alone; resize and DPI change; right-drag orbit latency; click
  picks the entity the CLI's `runtime raycast` returns for the same ray.
- **Regression:** Debug and Release full suites; goldens byte-identical; `assembleDebug`; the performance gate
  run and its verdict reported (the camera place and the control host are on the applicability list); the C#
  SDK's zero-NuGet check unchanged and passing; the editor application's policy check passing.

## Risks & Open Questions

Risks:

- **U1 — embedding may not hold up:** a Win32 child inside Avalonia's composition (airspace: nothing of Avalonia
  can draw over the child; occlusion and z-order follow Win32 rules). A cross-process parent/child pair also
  **attaches the two threads' input queues** (Win32 does this implicitly), so a Runtime frame that stalls (Bistro
  runs at about 130 ms per frame with its known log flood, Spec 0056) can stall the editor's input processing.
  Phase 1 measures both; the fallback is the Runtime's own top-level window positioned by the editor (Q2 V-a2),
  which shares no input queue.
- **U2 — input routing** is a Win32 workaround either way; Phase 1 decides between forwarding and the wire.
- **Two editors for a while:** the ImGui editor frozen, the Avalonia editor young. Q5 bounds it.
- **Managed packages enter the repository** for the first time. ADR-0127's policy and check bound it.
- **A C# re-implementation of the editor models** (Phase 2): E1/E2's C++ models are the reference, and their
  ADRs (0123, 0124) are contracts the C# side must keep. Not Phase 1 work.

Open questions — all six ruled by the owner (slmao, 2026-10-20, review of [PR #275](https://github.com/slmao/Atlantis/pull/275)), each as its
recommendation; rulings recorded in Plan 0064:

- **Q1 — The platform shape** (ADR-0125).
  - **(P-a) A separate managed process, Avalonia UI, the C# SDK as its only engine access** — recommended. The
    maintainer's direction; it keeps the editor an ordinary client by construction (no engine code is even
    loadable from it) and puts the editor where the tooling ecosystem is.
  - **(P-b) A separate native (C++) process** with a C++ UI toolkit: an ordinary client too, but a second large UI
    dependency and none of the C# ecosystem the direction wants.
  - **(P-c) The ImGui editor grown in place:** excluded.
  - **Why P-a:** it is the direction, and the ordinary-client rule needs no boundary scan to hold — a managed
    process cannot reach the ECS.

- **Q2 — Viewport embedding and input routing** (ADR-0126; Phase 1's two unknowns).
  - **Embedding, (V-a) a Platform-owned child window inside the editor's window** — recommended. The editor
    creates a placeholder host window inside its layout (Avalonia's `NativeControlHost` returns it) and starts the
    Runtime with that handle as `--parent-window`; the Runtime creates its window `WS_CHILD` under it. No frame
    leaves the GPU, no copy, no new RHI or WSI code: the Vulkan surface is created on the child exactly as on a
    top-level window. The Runtime owns and destroys the child (ADR-0013); only Platform includes Win32
    (ADR-0005). The child's handle goes into the session file, and the editor sizes the child to the host
    (`SetWindowPos` works across processes); the Runtime sees the resulting `WindowResize` as today. **(V-a2)** the
    fallback within the same ADR: the Runtime's own top-level window, owned by and positioned under the editor's
    window — the same wire, no `NativeControlHost`, for a Phase 1 in which (V-a) fails.
    - **Requirements on Platform/WSI:** one new `WindowOptions` field (`parent`), Windows only; the child follows
      the parent's DPI awareness (the process stays per-monitor-DPI-aware v2); `WindowResize` arrives as today
      when the host resizes the child; `WindowCloseRequested` on the parent's destruction. Vulkan WSI: nothing
      changes — `vkCreateWin32SurfaceKHR` on a child `HWND` is ordinary.
  - **Embedding, (V-b) cross-process streaming:** the Runtime renders offscreen and the editor shows the frames —
    by capture (measured about 2 FPS, Spec 0056) or by GPU memory sharing (`VK_KHR_external_memory_win32` on the
    Runtime side, a D3D/Vulkan-aware compositor on the Avalonia side): a new RHI capability, new Vulkan Backend
    code and an Avalonia rendering integration, for a result the child window gives for free. Rejected for Phase
    1; recorded as the route if (V-a) and (V-a2) both fail.
  - **Input, (R-a) the child forwards** — recommended for investigation. Platform's child-window mode does not
    take focus (`WM_MOUSEACTIVATE` → no activation) and re-posts its mouse and keyboard messages to the parent
    with translated coordinates, so Avalonia's top-level window handles them as its own and the `NativeControlHost`
    region gets hit-tested like any control. Platform's own input events (ADR-0109) are still emitted — they are
    simply not consumed by anyone in the Runtime process without `--editor`.
  - **Input, (R-b) the wire:** the Runtime forwards its input events to attached clients as a control-side event
    stream (not a World event; a control method `control.drainInput`). Portable and toolkit-independent; costs one
    frame-boundary poll of latency and a protocol addition. Phase 1's fallback, measured if (R-a) fails or lags.
  - **(R-c) a transparent Avalonia overlay** above the child: impossible with a Win32 child (airspace).
  - **Why (V-a) + (R-a):** both reuse what exists; the unknowns are confined to Win32 behaviour Phase 1 can
    measure in days, not to new engine subsystems.

- **Q3 — Remote camera and ray-cast** (ADR-0128; reopening Spec 0062 Q2 K-d and ADR-0123 D2/D4 by name).
  - **(W-a) Two `RuntimeControl` calls, carried by the wire** — recommended. `setViewportCamera(pose, lens) /
    clear` and `raycast(ray) → {entity, distance}?`. They sit beside capture: frame-side services, not World data.
    - **Supersedes:** ADR-0106 D3's call list (two more calls); ADR-0123 D2's "nothing reaches them over Atlantis
      Remote" and D4's "does not pre-authorize" (this Spec authorizes exactly these). ADR-0123 D1's camera rules
      (eye and forward from the matrix; exposure, fog and bloom stay the scene's; an unusable camera is ignored)
      and D2's picking rules (CPU geometry, both faces, alpha ignored, retained only when asked) are reused, not
      changed: the control host calls the same `RuntimeApplication` services the in-process adapter calls.
    - **Not on `RuntimeConnection`:** the World's query surface stays the five-command/five-event boundary's
      (R6); a ray-cast answers from render-side geometry, which ADR-0123 D2 already placed outside World data.
    - **Retention:** `retainPickGeometry` is set by `--listen` as well as `--editor` (a remote client may pick).
      Cost: about 42 MB on Bistro for every listening Runtime (Spec 0062 Q4), accepted; a `--no-pick-geometry`
      opt-out is the Plan's.
  - **(W-b) `raycast` on `RuntimeConnection`:** a sixth World verb and a change to ADR-0103/0105; rejected, as
    Spec 0062 K-d was.
  - **(W-c) No wire additions; the editor picks client-side** from bounds it cannot obtain (Spec 0062 K-c):
    rejected for the reasons given there.
  - **Why W-a:** the editor cannot work without them, every client gets them, and the World stays closed.

- **Q4 — The editor application's managed dependency policy** (ADR-0127).
  - **(D-a) Pinned by exact version and content hash, locked-mode restore, offline-reproducible, a check** —
    recommended. The editor application gets its own `NuGet.config` (nuget.org, under `src/editor_app/` only; the
    repository root's `<clear/>` still governs the SDK), a committed `packages.lock.json` (every package's exact
    version and SHA-512 content hash), `RestorePackagesWithLockFile` + `RestoreLockedMode`, an optional
    `ATLANTIS_NUGET_OFFLINE_FEED` directory that `dotnet restore` is pointed at for an offline build, and a check
    (the `check_csharp_rules.cmake` shape) that every `PackageReference` has a lock entry and that the allow-list
    (Avalonia's own packages only in Phase 1) is not exceeded. This is ADR-0107/0116's "URL + SHA-256" rule in
    NuGet's native form.
  - **(D-b) Vendor Avalonia's binaries into the repository:** reproducible, but tens of MB of binaries in git and
    no upgrade path through the package graph.
  - **(D-c) Extend zero-NuGet to the editor:** an Avalonia editor cannot exist without Avalonia.
  - **Why D-a:** the same guarantee ADR-0006's pinning gives native fetches — exact bits, verifiable, offline.

- **Q5 — The dual-editor transition** (ADR-0125 D4).
  - **(T-a) Freeze the ImGui editor at E2; E3 and later target the Avalonia editor once Phase 1 passes** —
    recommended. The ImGui editor keeps `--editor` as the in-process debug tool: bug fixes and changes forced by
    shared-surface Specs (as Phase 1's camera-place change) only, no new panels or iterations. E3 (authoring
    editing), E5, E4 and E6 (the maintainer's reference order; not yet specified) land on the Avalonia side,
    starting after Phase 1's checkpoint. If Phase 1 fails both (V-a) and (V-a2), the owner re-rules; until then
    E3 is not started on either side.
  - **(T-b) Keep both editors moving:** every feature twice.
  - **(T-c) Remove the ImGui editor once Phase 2 reaches parity:** not now; it stays the debug tool by direction.
  - **Why T-a:** one line of development, with the existing editor's C++ models and ADRs as the C# port's
    reference.

- **Q6 — C# gameplay components** (Spec 0057 Q5 C-a, ADR-0110 D5).
  - **(G-a) Named only, not reopened here** — recommended. If a later Spec reopens it, the schema rule binds:
    a C# attribute or class is an input to the generator and lands in a descriptor table; the table stays the
    truth, the bindings are generated from it, and the Runtime learns the type through the schema, never through
    loaded managed code (Spec 0057 Q6).
  - **(G-b) Reopen it in this Spec:** it is a Spec with several ADRs of its own (Spec 0057 Q5 C-b lists them)
    and has nothing to do with Phase 1.

## Phases (background; only Phase 1 is this Spec's scope)

1. **Technical prototype (this Spec).** Investigative: U1 embedding, U2 input routing, the two wire additions,
   Bistro in the embedded Viewport; a findings report the owner checkpoints.
2. **An editor games are developed in.** Hierarchy, Inspector, Gizmo, undo (the E1/E2 contracts in C#), E3's
   authoring editing, saving and re-bake, play/pause/step, content browsing. Its own Spec(s).
3. **A tool ecosystem.** Extension points, asset pipeline UI, profiling views; the CLI and SDKs grow with the
   editor since they share the surface. Its own Specs.
4. **AI-native.** Agents as first-class clients of the same surface — already the case by construction since Spec
   0055; what Phase 4 adds (workflows, policies, sandboxes) is its own Specs.

## Out of Scope / Future Work

- Phases 2–4 as above; E3–E6.
- Cross-process GPU frame sharing (Q2 V-b) and a transport-level input stream (Q2 R-b), if Phase 1 needs them.
- C# gameplay components (Q6).
- Removing the ImGui editor (Q5 T-c).
