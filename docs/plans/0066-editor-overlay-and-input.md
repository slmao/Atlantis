# Plan: Viewport Overlay and Editor Input over the Wire

- **Spec:** [Spec 0066: Viewport Overlay and Editor Input over the Wire](../specs/0066-editor-overlay-and-input.md)
  (`Approved` 2026-10-24) — [ADR-0132](../adr/0132-viewport-overlay-surface.md) (`Accepted` 2026-10-24).
  - Drafted together with the Spec at the maintainer's direction (2026-10-24). The owner ruled Q1 (a), Q2 P-a + L-a +
    G-a + S-a + O-a, Q3 I-1, Q4 H-a, Q5 as confirmed and Q6's budgets, exactly the options this Plan was drafted
    against.
  - Keeps ADR-0103, ADR-0123, ADR-0124, ADR-0125, ADR-0126 and ADR-0131 unchanged.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-24 — this Plan and [Spec 0066](../specs/0066-editor-overlay-and-input.md)
  reviewed together in [PR #286](https://github.com/slmao/Atlantis/pull/286) (revised after review, merged at `2e22e1a`); the rulings were given after
  that merge and are recorded here by their own docs PR.
  - **Spec:** Q1–Q6 ruled as recommended.
  - **Plan:** J1–J11 ruled as proposed (J3 as revised in review); no Spec Correction then. J12 and a Spec
    Correction came with the 2026-10-24 amendment below.
  - **ADR:** ADR-0132 `Accepted`; ADR-0133 `Accepted` with the amendment.
  - **Authorization:** implementation on `feature/0066-editor-overlay-input` once this rulings PR has merged, M1–M7
    in this Plan's order, with the owner's J8 checkpoint on M1's verdict before M5 (and before M4 if the verdict
    adds an input ADR, reviewed in its own docs PR).
  - **Constraints:** no merge by the agent, no push to `main`;
    `tests/performance/baselines/` and `gate_policy.json` untouched; every existing golden byte-identical, none
    re-captured; changes limited to this Plan's file list, deviations disclosed; the changed `runFrame()` lines
    enumerated; the performance gate applies (`src/runtime/**`, `src/renderer/**`) and its verdict is reported as
    measured; the C# SDK's zero-NuGet and the editor-app package checks unchanged and passing; M1's and M6's
    measurements reported against Spec 0066 Q6, not as test verdicts; the J8 checkpoint is the owner's.
  - **Amendment (2026-10-24, found at the start of M3, before any M3 code; accepted by slmao, 2026-10-24, in the
    review of its own docs PR, [PR #288](https://github.com/slmao/Atlantis/pull/288) — J8's pattern; J12 accepted
    as proposed):** the RHI clears every attachment it begins, so no pass after `drawFrame()` can draw over the
    frame or test against its depth (ADR-0025, ADR-0026). [ADR-0133](../adr/0133-attachment-load-after-the-scene.md)
    (`Accepted` 2026-10-24) lets a RenderGraph binding load an attachment's contents. P3 gains it, M3 splits into M3a
    (the load) and M3b (the overlay), the file list gains the RHI, the RenderGraph and the Vulkan Backend, and J4's
    mask becomes `Rgba8Unorm` (J12).
    - **Two review notes, folded in with the ruling:** the overlay's incoming states come from the end states the
      previous graph reports, never from literal states (so `execute()` and `drawFrame()` report them; ADR-0133 D3,
      D5); and M3a's synchronization-validation run is recorded with its exact layer settings.
    - **Spec 0066 Correction (2026-10-24):** R3 and R4 unchanged; its Architectural Impact list names ADR-0133 (the
      open question of PR #288, answered yes).
    - M3 starts only after PR #288 merges.
- **Implementation:** M1–M7 merged in [PR #290](https://github.com/slmao/Atlantis/pull/290) (`7cf06a2`).
  - **Where it was verified:** M2–M3b on the Intel Arc B370 laptop; M4 onward on the AMD RX 7900 XT desktop, which
    became the reference machine on 2026-10-25 (Spec 0011 and Spec 0060 Corrections, [PR #291](https://github.com/slmao/Atlantis/pull/291)).
    It was verified there after the goldens were re-captured ([PR #292](https://github.com/slmao/Atlantis/pull/292)) and
    its baseline recorded ([PR #293](https://github.com/slmao/Atlantis/pull/293)).
  - **Results:** every golden byte-identical; the performance gate `pass` (the changed `runFrame()` lines
    1989–1998 and 2019–2022); M1's and M6's measurements reported against Q6 in PR #290 (overlay latency
    2.0 frames, GPU cost 0.10–0.30 ms).
  - **M4's synchronization-validation run** needed the swapchain acquire fix of
    [PR #289](https://github.com/slmao/Atlantis/pull/289) first.
  - **Owner's visual check:** the default scene was right. On Bistro, far-away picks outlined an entity the scene
    does not draw (the pick geometry keeps the non-conformal entities the renderer skips). This is a pre-existing
    disagreement and a named follow-up, not this Plan's code.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Measure the editor's drag path under R-a first (Spec 0066 R8), then build the Viewport overlay end to end — the
value and `RuntimeControl::setViewportOverlay`, the wire, the CLI and the C# SDK, the Renderer's outline and primitive
passes, the Runtime's named place — and use it minimally in Atlantis Editor App: the picked entity's outline and a
translate Gizmo, no editing.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `277f3f9`.

1. **The in-process overlay:** `atlantis/renderer/ui_overlay.h` (`UiDrawList`, the pipeline's fixed shape: no camera
   uniform, no depth), `Renderer::drawOverlay()`, and `runFrame()`'s overlay places (`drawFrame()` into the Viewport
   target under `--editor`, `drawOverlayInto()` after it).
2. **The control surface:** `atlantis/connection/runtime_control.h`; `RuntimeControlHost`; Remote's
   `control.setViewportCamera` handling (`remote_server.cpp`: the camera's owner, cleared in `forget()` on
   disconnect); the C# `RemoteControl`; `remote_protocol.md` §5; the implementations of `RuntimeControl` that every
   new pure virtual reaches — `RuntimeControlHost`, Remote's `RemoteControl`, and the test fakes
   (`tests/cli/cli_tree_fixture.h`, `tests/editor/editor_fixture.h`, `tests/remote/remote_control_tests.cpp`, the C#
   fixture server).
3. **E1's client-side models:** `gizmo.h` (axes, modes, spaces), `viewport_picking.h` (pixels ↔ rays), and the ImGui
   view's screen-space Gizmo drawing (`editor_view.cpp`).
4. **The editor application:** `src/editor_app/Atlantis.Editor/` — `ViewportWindow` (R-a's receiver),
   `EditorSession` (one worker thread owns the session; the UI thread never waits on the Runtime), `EditorCamera`
   (E1's camera port), `MainWindow` (pick → highlight), and Phase 1's measurement logging (`FrameSampler`,
   `EditorLog`).
5. **Phase 1's findings:** Plan 0064's M6 report and J7 ruling (camera latency 1.5–2 Runtime frames; 100–600 ms
   UI-thread stalls at drag transitions; the UI tick-gap method with a 16 ms timer).

## Plan of changes

- **P1 — The value and the call** (ADR-0132 D1, D2; Spec R1, R2).
  - `atlantis/connection/runtime_control.h` (or a sibling `viewport_overlay.h` in Connection): `OverlayColor`,
    `OverlayDepth {Tested, OnTop}`, `OverlaySegment {a, b, color, depth, widthPixels}`, `OverlayTriangle {a, b, c,
    color, depth}`, `OverlayPoint {p, color, depth, sizePixels}`, `OverlayGroup {optional anchor + pixelScale,
    segments, triangles, points}` (world when no anchor, screen-constant with one), `ViewportOverlay {selection,
    groups}`; the limits (J6); `RuntimeControl::setViewportOverlay(std::optional<ViewportOverlay>)` returning
    `Result<std::monostate, ControlError>` (`InvalidRequest` over the limits).
  - `StepRequest` gains `withOverlay` (what a client asks; default `false`) and a transport-filled `captureOverlay`
    (the requester's own overlay, set by the transport holding per-client overlays and never carried on the wire;
    J3).
  - Every `RuntimeControl` implementation gains it: `RuntimeControlHost` (stores the union it is given), Remote's
    `RemoteControl`, and the test fakes.
- **P2 — Wire, CLI, C# SDK** (ADR-0132 D2; Spec R6).
  - Remote: the codec's overlay values; `control.setViewportOverlay` `{"overlay":<ViewportOverlay>|null}` →
    `{"ok":null}` or `{"err":"InvalidRequest"}`; the server keeps one overlay per client and hands the host their
    union on every change and on a client's disconnect (J2); `control.step`'s request gains `"withOverlay":bool`
    (absent read as `false`), which the server resolves to the requester's own overlay (J3); `remote_protocol.md` §5
    and §6.
  - CLI: `runtime overlay set <file|->` (an overlay as JSON, the wire's form), `runtime overlay select <guid>...`
    (a selection-only overlay), `runtime overlay clear`; `runtime step --capture [<path>] --with-overlay`
    (`--with-overlay` without `--capture` is a usage error); `cli/1` envelopes; help text.
  - C# SDK: the overlay records, codec, `RemoteControl.SetViewportOverlay(ViewportOverlay?)`, `StepRequest`'s
    `WithOverlay`; conformance vectors appended (the step-request vectors regenerated with the new member); the
    fixture server's control stores the union and reports which overlay a capture was given, for the C# tests.
- **P3 — Renderer** (ADR-0132 D3; Spec R3, R4; amended 2026-10-24, J12).
  - **Attachment load (ADR-0133; M3a).** The RHI's `AttachmentLoad { Clear, Load }` and a `beginRendering()`
    overload choosing it per attachment; the Vulkan Backend's `LOAD_OP_LOAD` and its two same-state transitions
    (colour and depth); the RenderGraph binding's `load` field, `incomingState` honoured for depth bindings, the
    same-state barrier before each pass on a `Load` binding, and the guard against loading an `Undefined` binding.
    `execute()` reports each binding's end state, and `drawFrame()` returns its colour target's and depth's end
    states (its parameters and passes unchanged). Every existing binding keeps `Clear`.
  - `atlantis/renderer/viewport_overlay.h`: `ViewportOverlayDrawList` (vertices of segments, triangles and points
    with colour, depth mode and, for a screen-constant group, its anchor and pixel scale; the selected `DrawItem`s),
    and `Renderer::drawViewportOverlay(commandList, target, depth, camera, list, resources)` recording RenderGraph
    passes after the scene's (no command outside the graph):
    - **outline:** the selected draw items into an `Rgba8Unorm` mask at the target's extent, in its own graph
      ending in `ShaderRead`, then an edge pass sampling it and compositing the outline colour over the target
      (J4, J12);
    - **primitives:** a screen-constant group's offsets scaled in the vertex shader from the frame's camera at the
      anchor's view depth; segments expanded to screen-space quads (J5), triangles and points (screen-space
      squares); depth-tested ones with the frame's depth read-only, then on-top ones.
  - Shaders: `shaders/viewport_overlay` (primitives), `shaders/selection_mask` and `shaders/selection_outline`,
    built by the Shader System for Windows and Android.
  - `drawFrame()`'s parameters and passes are unchanged; it returns its targets' end states. When an overlay follows,
    its caller passes `ColorAttachmentOutput` as `drawFrame()`'s final colour state. The overlay's graph binds the
    target and the frame's depth with **the end states `drawFrame()` reported** as their incoming states, never with
    literal ones (ADR-0133 D5), both `Load`. The target ends in the frame's real final state (present source,
    transfer source for a capture, or shader-read under `--editor`).
  - Depth-tested primitives are one pass with the depth attachment (a pipeline with `depthWriteEnabled = false`);
    the outline composite and the on-top primitives are passes without one.
- **P4 — Runtime** (ADR-0132 D4; Spec R3–R5).
  - `RuntimeControlHost::setViewportOverlay` validates the limits and passes the union to `RuntimeApplication`.
  - With a non-empty union, the frame's `drawFrame()` call is given `ColorAttachmentOutput` as its final colour state;
    its reported end states are handed to the overlay call, whose passes end in the state the frame used before (P3;
    ADR-0133 D5). Without one, the call is unchanged.
  - `RuntimeApplication`: the overlay's shaders loaded and pipelines built at the first non-empty union (J11); each
    frame with a non-empty union, the selection resolved through the bake's
    `EntityGuidMap` to `DrawItem`s (mesh, material-free, current `WorldMatrix`; an entity without a `Renderable`,
    unknown or destroyed skipped); the draw list built; **one named place in `runFrame()`** right after `drawFrame()`
    and before `drawOverlayInto()` / the present records `drawViewportOverlay()` when the union is non-empty; the
    capture path's re-render draws no overlay, or the step request's `captureOverlay` when it has one (J3). The
    place's lines are enumerated in the PR and in AGENTS.md's guard paragraph (M7).
- **P5 — Atlantis Editor App** (Spec R7).
  - After a pick: the picked entity's `WorldMatrix` read through `RuntimeConnection` (the worker thread), a
    translate Gizmo built as one screen-constant group anchored at its translation (three axis segments with cone
    tips as triangles, on top), sent with the selection via `SetViewportOverlay`; a camera change alone sends
    nothing; nothing picked → `SetViewportOverlay(null)`. No hit-testing, no editing.
  - Its headless smoke test (which today picks by `Raycast` and sets the camera) also sends the overlay and asserts
    through a capture (`control.step` with an image and `withOverlay`) that the picked entity's pixels differ from
    a capture without `withOverlay`.

## Milestones / Task Breakdown

Each milestone builds and runs the affected suites (Debug) before the next.

### M1 — The input investigation (Spec R8, Q3, Q6; no engine change)

- **Measurement build of Atlantis Editor App** (`--measure-drag`, measurement-only, not a feature): with an entity
  picked, a left drag with Shift held translates it along the camera's right axis by the pointer's world delta at
  the entity's depth, writing one `WorldMatrix` transaction per pointer-move batch on the worker thread (runtime-side
  writes on the Runtime World, recorded nowhere, Spec Q5), and a final one on release. Timestamps (`Stopwatch`,
  one clock) are logged for: pointer message received by the UI thread, write sent, write answered, and the first
  `control.status` frame after it; the UI thread's 16 ms timer ticks are logged throughout (Phase 1's method).
- **Protocol** (Release; the reference machine; mains power; the owner runs it, or the agent runs it and the owner
  reviews the log and a screen recording):
  1. Bistro (`--scene bistro`) and the default scene (`--scene integrated_showcase_demo`), embedded (V-a + R-a),
     editor maximized at 2374×1320 as in Phase 1.
  2. Per scene: pick an entity; ten drags of about 3 s each, each with a press, continuous movement, a pause of about
     0.5 s mid-drag, and a release; then 10 s idle.
  3. Per scene, the same ten drags with the Runtime unembedded (`atlantis_runtime --listen` in its own window,
     the editor driving it over the wire) as the no-attachment control.
- **Reported** (per scene, embedded and control): drag latency — pointer move → the frame that applied the write →
  that frame presented (median, p95, in ms and in Runtime frames); the UI thread's tick gaps (max, p99, the count over
  100 ms) during drags, at transitions, and idle; Runtime frame time over the same window.
- **Verdict table** (the owner rules, J8):

  | Verdict | When | Next |
  |---|---|---|
  | **R-a kept** | Q6's drag latency and stall ceiling met on both scenes | M2 onward as planned |
  | **Pump inside the frame (I-3)** | the stall ceiling missed embedded, met in the control | an ADR (amending ADR-0126 or new) and a Plan amendment in their own docs PR, merged before M4 (J8); the pump's `runFrame()` places enumerated |
  | **V-a2 (I-4)** | as above, and I-3 rejected or insufficient | inside ADR-0126 D5: a Plan amendment for the editor's owned-window path, in its own docs PR (J8) |
  | **Latency only** | stalls met, drag latency missed | reported; the owner decides whether the next Spec needs a faster path |
  | **Both fail in the control too** | the Runtime's frame time itself dominates | reported; not an input-route problem |

### M2 — The value, the call, the wire, the CLI and the C# SDK (P1, P2; R1, R2, R6)

- `src/connection/` (the value, the call), `src/remote/` (codec, server per-client union, client,
  `remote_protocol.md`), `src/cli/`, `src/runtime/` (`RuntimeControlHost` stores the union; nothing drawn yet),
  `src/csharp/Atlantis.Gameplay/`, `tests/csharp/` (vectors, fixture server, suites), the test fakes.
- Tests: codec round trips and vectors; per-client replacement, the union, removal on disconnect, `null` clearing,
  over-limit refusal with nothing changed (loopback, two clients); a step with an image and no `withOverlay` while
  another client's overlay is set hands the host no capture overlay, and with `withOverlay` hands it only the
  requester's (two clients); `--with-overlay`'s usage error; the CLI's envelopes and exit codes; the C# session
  against the fixture server.

### M3 — The Renderer's overlay (P3; R3, R4) — after the amendment's PR merges

**M3a — Attachment load (ADR-0133).**

- `src/rhi/include/atlantis/rhi/{types,command_list}.h`; `src/render_graph/include/atlantis/render_graph/execution.h`,
  `src/render_graph/src/execution.cpp`; `src/vulkan_backend/src/{vulkan_command_list.h,vulkan_command_list.cpp,
  resource_state_mapping.cpp}`; `src/renderer/include/atlantis/renderer/renderer.h` and `src/renderer/src/renderer.cpp`
  (`drawFrame()`'s returned end states); the RenderGraph tests' fake command list.
- Tests:
  - RenderGraph (fake command list): a `Load` binding begins its passes with `Load`, colour and depth; a `Clear`
    binding is recorded exactly as before; a depth binding's `incomingState` seeds its tracking; the same-state
    barrier is recorded before each pass on a `Load` binding and only there; a `Load` binding entering `Undefined`
    fails its guard; `execute()` reports each binding's end state (its `finalState`, its last usage's state, or its
    `incomingState` when unused); `drawFrame()` returns its target's and depth's end states.
  - Vulkan Backend (GPU, fatal VVL): a target cleared in one `execute()` keeps its pixels through a second that loads
    it and draws a small quad (pixels outside the quad unchanged); a depth written in one `execute()` hides a farther
    quad drawn by a second that loads it, and does not with `Clear`.
  - With every existing binding unchanged, every existing golden and the gate's counters are byte-identical.
  - **Acceptance includes the synchronization-validation run:** the M3 GPU tests run once with synchronization
    validation enabled (a manual check; ADR-0133 Consequences). The implementing PR records the result and the exact
    layer settings used (the environment variables or the `vk_layer_settings.txt` keys), so later Renderer passes
    can repeat it.

**M3b — The overlay.**

- `src/renderer/` (`viewport_overlay.{h,cpp}`), `shaders/viewport_overlay/`, `shaders/selection_mask/`,
  `shaders/selection_outline/`, their CMake.
- Tests (GPU, fatal VVL), on a small test scene rendered offscreen: a segment, a triangle and a point land at the
  pixels their world positions project to; a depth-tested primitive behind a mesh is hidden and an on-top one is not;
  a selected mesh's outline appears just outside its silhouette and nowhere else; a screen-constant group's segment
  has the same pixel length at two camera distances; an empty list records no pass.
  Pixel assertions, no new golden (J7).

### M4 — The Runtime's place (P4; R3–R5)

- `src/runtime/` (`runtime_application.{h,cpp}`, `runtime_control_host.cpp`).
- Tests (GPU, fatal VVL): with no overlay, frame data and every existing golden unchanged and the gate's counters
  exact; through `RuntimeControlHost`, an overlay and a selection drawn in the window's frame, and in a capture
  only when the step request carries it, under the control camera and under `--editor` (in the Viewport target,
  below the UI pass); a selection of an unknown GUID, an entity without a `Renderable`, and one destroyed while
  selected draws nothing and logs nothing per frame; over loopback (`atlantis_runtime --listen`), two clients'
  overlays both drawn, one disconnecting removes only its own, a capture without `withOverlay` shows neither and one
  with it shows only the requester's.
- The performance gate run with no overlay; the `runFrame()` lines enumerated.

### M5 — The editor's minimal use (P5; R7) — after J8's checkpoint

- `src/editor_app/Atlantis.Editor/` (an overlay builder, `MainWindow`, `EditorSession`), its tests.
- Tests: the Gizmo builder's primitives for a known matrix and camera (C#, against values from E1's C++ convention);
  the smoke test's capture with and without the overlay (P5).

### M6 — Overlay measurements (Q6)

- Reported on the reference machine, Release, Bistro: overlay latency (send → first presented frame showing it,
  median over 50 sends while orbiting); the GPU cost of a translate Gizmo plus one selected entity's outline (GPU
  timing build; the outline of a large mesh and of a small one); the owner's look at the outline and Gizmo on Bistro
  and the default scene.

### M7 — Normative docs and acceptance

- AGENTS.md and `docs/architecture/module_boundaries.md`: the Viewport overlay on the control surface; Renderer's
  second overlay input; the Runtime's named place and the `runFrame()` guard's lines; the CLI and C# additions.
- Acceptance: Debug and Release full suites, warning-free; every existing golden byte-identical; VVL clean;
  `assembleDebug`; the C# rules and editor-app package checks; the performance gate's verdict with the `runFrame()`
  lines enumerated; `git diff --check`.

## Files / Modules Touched (expected)

- **Connection:** `src/connection/include/atlantis/connection/runtime_control.h` (and a new `viewport_overlay.h` if
  the value is split out).
- **Remote:** `src/remote/src/{codec,remote_server,remote_client}.{h,cpp}`, `docs/architecture/remote_protocol.md`.
- **CLI:** `src/cli/` (the `runtime overlay` commands, help text).
- **RHI, RenderGraph, Vulkan Backend, and `drawFrame()`'s return** (M3a; ADR-0133, J12):
  `src/renderer/include/atlantis/renderer/renderer.h`, `src/renderer/src/renderer.cpp`,
  `src/rhi/include/atlantis/rhi/types.h`,
  `src/rhi/include/atlantis/rhi/command_list.h`, `src/render_graph/include/atlantis/render_graph/execution.h`,
  `src/render_graph/src/execution.cpp`, `src/vulkan_backend/src/vulkan_command_list.{h,cpp}`,
  `src/vulkan_backend/src/resource_state_mapping.cpp`.
- **Renderer:** `src/renderer/include/atlantis/renderer/viewport_overlay.h` (new), `src/renderer/src/viewport_overlay.cpp`
  (new), `src/renderer/CMakeLists.txt`; `shaders/viewport_overlay/`, `shaders/selection_mask/`,
  `shaders/selection_outline/` (new) and the shader build lists.
- **Runtime:** `src/runtime/include/atlantis/runtime/runtime_application.h`, `src/runtime/src/runtime_application.cpp`,
  `src/runtime/include/atlantis/runtime/runtime_control_host.h`, `src/runtime/src/runtime_control_host.cpp`,
  `src/runtime/CMakeLists.txt` (shader paths).
- **C# SDK:** `src/csharp/Atlantis.Gameplay/` (`Control.cs`, `Remote/Codec.cs`, `Remote/RemoteSession.cs`).
- **Editor App:** `src/editor_app/Atlantis.Editor/` (M1's measurement mode; M5's overlay builder) and its tests.
- **Tests:** `tests/connection/`, `tests/remote/`, `tests/cli/`, `tests/render_graph/` (the fake command list
  and the load tests), `tests/vulkan_backend/` (the load GPU tests), `tests/renderer/`, `tests/runtime/`,
  `tests/csharp/` (vectors, fixture server, suites), `tests/editor/editor_fixture.h` (the fake's new call).
- **Android:** none expected. `android/app/build.gradle` lists the shaders it packages explicitly; the overlay's
  pipelines are created at the first non-empty overlay (J11), which an Android Runtime never receives, so its list
  is unchanged. The Renderer's sources build there as today, and so do the RHI, RenderGraph and Vulkan Backend
  changes (the backend is shared).
- **Docs:** `remote_protocol.md` (M2); AGENTS.md, `module_boundaries.md` (M7); the registry row, README and
  blueprint after merge (J9).
- **Not touched:** World, Asset System, Authoring, `RuntimeConnection`, `AuthoringConnection`, the ImGui editor's
  sources (`src/editor/`), `src/tools/sdk_codegen/` and the generated bindings, existing goldens,
  `tests/performance/baselines/`, `gate_policy.json`.

## Sequencing & Dependencies

- M1 first: it changes no engine code and its verdict (J8) may add an input ADR before M4 touches `runFrame()`.
- M2 needs nothing else; M3 needs the amendment's docs PR merged (J12), then M3a before M3b; M4 needs M2
  and M3; M5 needs M4 and the J8 ruling; M6 needs M5; M7 is last.
- The next Spec (the E3 editor) needs M5's overlay builder and Q4's client-side hit-testing; it does not wait for M6.

## Risks & Mitigations

- **The stalls are not reproduced by a scripted measurement:** M1 drags by hand with a screen recording, as Phase 1
  did; the no-attachment control separates the attachment from the Runtime's frame time.
- **Outline cost on large meshes:** measured in M6 against Q6; a selection limit bounds the worst case (J6).
- **Depth-tested primitives on the `--editor` path:** the Viewport target's depth is the frame's; M4 tests both
  paths.
- **Line quality:** screen-space quads alias at a 1-pixel width; widths are given in pixels and tested at 2 px.
- **A missing barrier on a loaded attachment** would pass the fatal validation layers (no synchronization
  validation): M3a's RenderGraph tests assert the barrier, and M3's GPU tests run once with synchronization
  validation (J12).

## Joint Review decisions — ruled (slmao, 2026-10-24): J1–J11 as proposed; J12 with the amendment

- **J1 — Spec Corrections:** none proposed.
- **J2 — Where the union is formed:** in Remote's server, which already tracks per-client control state (the
  camera's owner) and calls `setViewportOverlay(union)` on every change; `RuntimeControl` stays one overlay per
  caller, and the in-process `--exec` client is one caller. Proposed: accept. The alternative, owner keys on
  `RuntimeControl`, changes every implementation's shape for one transport's need.
- **J3 — Captures exclude overlays unless asked, and then show only the requester's** (revised after review; Spec
  R5, ADR-0132 D2): a capture is the evaluation image of agents and CI, so another client's Gizmo or debug lines
  never reach it. The step request's `withOverlay` asks for the requester's own overlay; Remote's server, which holds
  per-client overlays (J2), fills `captureOverlay` with it; in process the CLI's `--exec` runner holds no
  `RuntimeControl`, so the server is the only resolver. Proposed: accept.
- **J4 — Outline technique** (the mask's format amended by J12): the selected meshes into an R8 mask at the
  target's extent, then a 2-pixel edge pass
  (a texel differs from a neighbour) in a fixed colour, drawn over everything (visible through occluders, the common
  editor convention). Proposed: accept; a depth-aware dimmed occluded part is future work.
- **J5 — Lines:** segments expanded to screen-space quads in the vertex shader from both endpoints' clip positions
  and the pixel width; no `wideLines` device feature (unavailable on many Android GPUs). Proposed: accept.
- **J6 — Limits:** per client at most 256 groups, 65,536 segments, 65,536 triangles, 16,384 points (counted
  across groups) and 256 selected entities;
  over a limit, `InvalidRequest` and nothing changes. Proposed: accept; a Gizmo needs about a hundred, the limits stop
  a runaway debug client.
- **J7 — Testing without new goldens:** the overlay is tested by pixel assertions on captured frames (positions,
  occlusion, outline presence), not by new golden images; every existing golden stays byte-identical. Proposed:
  accept; the overlay's look is reviewed by the owner in M6, not frozen as a golden.
- **J8 — The M1 checkpoint:** the owner rules on M1's verdict table before M5 starts; an I-3 verdict adds an ADR and
  a Plan amendment before M4 changes `runFrame()`. That ADR and amendment are reviewed in **their own docs PR** (as
  Spec 0065's Corrections were, PRs #281–#283), never folded into the implementation PR; the implementation PR
  cites that PR, and M4 does not start before it merges. Proposed: accept (as Plan 0064 J7).
  - **Checkpoint ruled (slmao, 2026-10-24): R-a kept**, M1's proposed verdict, so no input ADR and no amendment.
    M1's verdict table and measurements are in the implementation PR (DoD below).
- **J9 — Docs split:** normative sentences with the implementation PR, narratives after merge (Plans 0062–0065).
  Proposed: accept.
- **J10 — Budgets as reported targets:** Spec 0066 Q6's targets are reported against, as Plan 0065 J3/J9, never test
  verdicts. Proposed: accept.
- **J11 — Pipelines created on first use:** the overlay's shaders are loaded and its pipelines built at the first
  non-empty overlay, not at start, so startup, Android's packaged shader list and every frame without an overlay are
  unchanged. Proposed: accept; the first overlay's frame pays the creation once (reported in M6).

- **J12 — Attachment load (amendment, 2026-10-24; ruled by slmao, 2026-10-24, in
  [PR #288](https://github.com/slmao/Atlantis/pull/288): accepted as proposed, with the two review notes above and a Spec 0066 Correction naming ADR-0133, which supersedes
  the "No Spec Correction" sentence below):** accept
  [ADR-0133](../adr/0133-attachment-load-after-the-scene.md): a RenderGraph binding may load its attachment's
  contents (RHI `AttachmentLoad`, the Vulkan Backend's `LOAD_OP_LOAD` and same-state barriers, depth
  `incomingState`), with every existing binding unchanged. The mask is `Rgba8Unorm`, not R8: the RHI's `Format` has
  no single-channel value, and adding one for a transient target would touch every switch over formats. M3 is split
  into M3a and M3b and starts after this PR merges. No Spec Correction: Spec 0066 R3 and R4 already say the overlay
  is drawn after the scene by RenderGraph passes. Its Architectural Impact list does not name this RHI change; the
  ADR records it. Proposed: accept. The alternatives (inside `drawFrame()`, in the HDR pass, offscreen and
  composite) are in ADR-0133.

## Verification Checklist

- [ ] Unit tests: codec round trips and vectors (C++ and C#); the union and its lifetime; the CLI's envelopes; the C#
      Gizmo builder (R1, R2, R6, R7).
- [ ] Headless integration tests: loopback, two clients; the C# fixture-server suite (R2, R6).
- [ ] Image regression tests: every existing golden byte-identical; overlay pixel assertions (R3–R5; J7).
- [ ] Vulkan Validation Layers clean: fatal in every GPU test (M3, M4); M3's GPU tests once with synchronization
      validation, recorded with its layer settings (J12).
- [ ] Other: `assembleDebug`; the C# rules and editor-app package checks; the performance gate with the `runFrame()`
      lines enumerated; M1's and M6's measurements reported against Q6 (R8).

## Rollback Plan

Revert the implementation PR. The call is additive on the wire; with no client sending an overlay the frame is
unchanged; the Editor App's use is behind `ATLANTIS_BUILD_EDITOR_APP` (default OFF). No format, golden or baseline
depends on it.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:

- M1's measurement report with the verdict table, and the owner's J8 ruling, are in the PR.
- The changed `runFrame()` lines are enumerated, and the performance gate's verdict is reported as measured.
