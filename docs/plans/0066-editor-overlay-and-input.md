# Plan: Viewport Overlay and Editor Input over the Wire

- **Spec:** [Spec 0066: Viewport Overlay and Editor Input over the Wire](../specs/0066-editor-overlay-and-input.md)
  (`Draft`) — [ADR-0132](../adr/0132-viewport-overlay-surface.md) (`Proposed`).
  - Drafted together with the Spec at the maintainer's direction (2026-10-24), against the Spec's recommended
    options: Q1 (a), Q2 P-a + L-a + G-a + O-a, Q3 I-1, Q4 H-a, Q5 as confirmed, Q6's budgets.
  - Keeps ADR-0103, ADR-0123, ADR-0124, ADR-0125, ADR-0126 and ADR-0131 unchanged.
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending. Once approved: reviewer, date, the PR naming this Plan and its Spec and authorizing
  implementation on `feature/0066-editor-overlay-input`, M1–M7 in this Plan's order, with M1's checkpoint (J8) before
  M5.
  - **Constraints (proposed, as Plans 0064–0065):** no merge by the agent, no push to `main`;
    `tests/performance/baselines/` and `gate_policy.json` untouched; every existing golden byte-identical, none
    re-captured; changes limited to this Plan's file list, deviations disclosed; the changed `runFrame()` lines
    enumerated; the performance gate applies (`src/runtime/**`, `src/renderer/**`) and its verdict is reported as
    measured; the C# SDK's zero-NuGet and the editor-app package checks unchanged and passing; M1's and M6's
    measurements reported against Spec 0066 Q6, not as test verdicts; the J8 checkpoint is the owner's.

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
    color, depth}`, `OverlayPoint {p, color, depth, sizePixels}`, `ViewportOverlay {selection, segments, triangles,
    points}`; the limits (J6); `RuntimeControl::setViewportOverlay(std::optional<ViewportOverlay>)` returning
    `Result<std::monostate, ControlError>` (`InvalidRequest` over the limits).
  - Every `RuntimeControl` implementation gains it: `RuntimeControlHost` (stores the union it is given), Remote's
    `RemoteControl`, and the test fakes.
- **P2 — Wire, CLI, C# SDK** (ADR-0132 D2; Spec R6).
  - Remote: the codec's overlay values; `control.setViewportOverlay` `{"overlay":<ViewportOverlay>|null}` →
    `{"ok":null}` or `{"err":"InvalidRequest"}`; the server keeps one overlay per client and hands the host their
    union on every change and on a client's disconnect (J2); `remote_protocol.md` §5 and §6.
  - CLI: `runtime overlay set <file|->` (an overlay as JSON, the wire's form), `runtime overlay select <guid>...`
    (a selection-only overlay), `runtime overlay clear`; `cli/1` envelopes; help text.
  - C# SDK: the overlay records, codec, `RemoteControl.SetViewportOverlay(ViewportOverlay?)`; conformance vectors
    appended; the fixture server's control stores and reports the union for the C# tests.
- **P3 — Renderer** (ADR-0132 D3; Spec R3, R4).
  - `atlantis/renderer/viewport_overlay.h`: `ViewportOverlayDrawList` (world-space vertices of segments, triangles
    and points with colour and depth mode; the selected `DrawItem`s), and `Renderer::drawViewportOverlay(commandList,
    target, depth, camera, list, resources)` recording, through the RenderGraph:
    - **outline:** the selected draw items into an R8 mask at the target's extent, then an edge pass compositing the
      outline colour over the target (J4);
    - **primitives:** segments expanded to screen-space quads in the vertex shader (J5), triangles and points
      (screen-space squares), depth-tested ones with the frame's depth read-only, then on-top ones.
  - Shaders: `shaders/viewport_overlay` (primitives), `shaders/selection_mask` and `shaders/selection_outline`,
    built by the Shader System for Windows and Android.
  - `drawFrame()` is unchanged; the RenderGraph carries the target from it to the overlay call, which ends in the
    target's final state (present source, or shader-read under `--editor`).
- **P4 — Runtime** (ADR-0132 D4; Spec R3–R5).
  - `RuntimeControlHost::setViewportOverlay` validates the limits and passes the union to `RuntimeApplication`.
  - `RuntimeApplication`: the overlay's shaders loaded and pipelines built at the first non-empty union (J11); each
    frame with a non-empty union, the selection resolved through the bake's
    `EntityGuidMap` to `DrawItem`s (mesh, material-free, current `WorldMatrix`; an entity without a `Renderable`,
    unknown or destroyed skipped); the draw list built; **one named place in `runFrame()`** right after `drawFrame()`
    and before `drawOverlayInto()` / the present records `drawViewportOverlay()` when the union is non-empty; the
    capture path's re-render records it too (J3). The place's lines are enumerated in the PR and in AGENTS.md's guard
    paragraph (M7).
- **P5 — Atlantis Editor App** (Spec R7).
  - After a pick and on every camera change: the picked entity's `WorldMatrix` read through `RuntimeConnection`
    (the worker thread), a translate Gizmo built as primitives (three axis segments with cone tips as triangles, on
    top, sized to a constant pixel length at the entity's depth), sent with the selection via
    `SetViewportOverlay`; nothing picked → `SetViewportOverlay(null)`. No hit-testing, no editing.
  - Its headless smoke test (which today picks by `Raycast` and sets the camera) also sends the overlay and asserts
    through the CLI-equivalent capture (`control.step` with an image) that the picked entity's pixels differ with the
    overlay and without it.

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
  | **Pump inside the frame (I-3)** | the stall ceiling missed embedded, met in the control | an ADR (amending ADR-0126 or new) and a Plan amendment before M4; the pump's `runFrame()` places enumerated |
  | **V-a2 (I-4)** | as above, and I-3 rejected or insufficient | inside ADR-0126 D5: a Plan amendment for the editor's owned-window path |
  | **Latency only** | stalls met, drag latency missed | reported; the owner decides whether the next Spec needs a faster path |
  | **Both fail in the control too** | the Runtime's frame time itself dominates | reported; not an input-route problem |

### M2 — The value, the call, the wire, the CLI and the C# SDK (P1, P2; R1, R2, R6)

- `src/connection/` (the value, the call), `src/remote/` (codec, server per-client union, client,
  `remote_protocol.md`), `src/cli/`, `src/runtime/` (`RuntimeControlHost` stores the union; nothing drawn yet),
  `src/csharp/Atlantis.Gameplay/`, `tests/csharp/` (vectors, fixture server, suites), the test fakes.
- Tests: codec round trips and vectors; per-client replacement, the union, removal on disconnect, `null` clearing,
  over-limit refusal with nothing changed (loopback, two clients); the CLI's envelopes and exit codes; the C# session
  against the fixture server.

### M3 — The Renderer's overlay (P3; R3, R4)

- `src/renderer/` (`viewport_overlay.{h,cpp}`), `shaders/viewport_overlay/`, `shaders/selection_mask/`,
  `shaders/selection_outline/`, their CMake.
- Tests (GPU, fatal VVL), on a small test scene rendered offscreen: a segment, a triangle and a point land at the
  pixels their world positions project to; a depth-tested primitive behind a mesh is hidden and an on-top one is not;
  a selected mesh's outline appears just outside its silhouette and nowhere else; an empty list records no pass.
  Pixel assertions, no new golden (J7).

### M4 — The Runtime's place (P4; R3–R5)

- `src/runtime/` (`runtime_application.{h,cpp}`, `runtime_control_host.cpp`).
- Tests (GPU, fatal VVL): with no overlay, frame data and every existing golden unchanged and the gate's counters
  exact; through `RuntimeControlHost`, an overlay and a selection appear in a captured frame under the control camera
  and under `--editor` (in the Viewport target, below the UI pass); a selection of an unknown GUID, an entity without
  a `Renderable`, and one destroyed while selected draws nothing and logs nothing per frame; two clients' overlays
  both appear, and one disconnecting removes only its own (loopback, `atlantis_runtime --listen`).
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
- **Renderer:** `src/renderer/include/atlantis/renderer/viewport_overlay.h` (new), `src/renderer/src/viewport_overlay.cpp`
  (new), `src/renderer/CMakeLists.txt`; `shaders/viewport_overlay/`, `shaders/selection_mask/`,
  `shaders/selection_outline/` (new) and the shader build lists.
- **Runtime:** `src/runtime/include/atlantis/runtime/runtime_application.h`, `src/runtime/src/runtime_application.cpp`,
  `src/runtime/include/atlantis/runtime/runtime_control_host.h`, `src/runtime/src/runtime_control_host.cpp`,
  `src/runtime/CMakeLists.txt` (shader paths).
- **C# SDK:** `src/csharp/Atlantis.Gameplay/` (`Control.cs`, `Remote/Codec.cs`, `Remote/RemoteSession.cs`).
- **Editor App:** `src/editor_app/Atlantis.Editor/` (M1's measurement mode; M5's overlay builder) and its tests.
- **Tests:** `tests/connection/`, `tests/remote/`, `tests/cli/`, `tests/renderer/`, `tests/runtime/`,
  `tests/csharp/` (vectors, fixture server, suites), `tests/editor/editor_fixture.h` (the fake's new call).
- **Android:** none expected. `android/app/build.gradle` lists the shaders it packages explicitly; the overlay's
  pipelines are created at the first non-empty overlay (J11), which an Android Runtime never receives, so its list
  is unchanged. The Renderer's sources build there as today.
- **Docs:** `remote_protocol.md` (M2); AGENTS.md, `module_boundaries.md` (M7); the registry row, README and
  blueprint after merge (J9).
- **Not touched:** World, Asset System, Authoring, `RuntimeConnection`, `AuthoringConnection`, the ImGui editor's
  sources (`src/editor/`), `src/tools/sdk_codegen/` and the generated bindings, existing goldens,
  `tests/performance/baselines/`, `gate_policy.json`.

## Sequencing & Dependencies

- M1 first: it changes no engine code and its verdict (J8) may add an input ADR before M4 touches `runFrame()`.
- M2 needs nothing else; M3 needs nothing else (it can run beside M2); M4 needs M2 and M3; M5 needs M4 and the J8
  ruling; M6 needs M5; M7 is last.
- The next Spec (the E3 editor) needs M5's overlay builder and Q4's client-side hit-testing; it does not wait for M6.

## Risks & Mitigations

- **The stalls are not reproduced by a scripted measurement:** M1 drags by hand with a screen recording, as Phase 1
  did; the no-attachment control separates the attachment from the Runtime's frame time.
- **Outline cost on large meshes:** measured in M6 against Q6; a selection limit bounds the worst case (J6).
- **Depth-tested primitives on the `--editor` path:** the Viewport target's depth is the frame's; M4 tests both
  paths.
- **Line quality:** screen-space quads alias at a 1-pixel width; widths are given in pixels and tested at 2 px.

## Joint Review decisions — proposed

- **J1 — Spec Corrections:** none proposed.
- **J2 — Where the union is formed:** in Remote's server, which already tracks per-client control state (the
  camera's owner) and calls `setViewportOverlay(union)` on every change; `RuntimeControl` stays one overlay per
  caller, and the in-process `--exec` client is one caller. Proposed: accept. The alternative, owner keys on
  `RuntimeControl`, changes every implementation's shape for one transport's need.
- **J3 — Captures include the overlay:** what `runtime step --capture` writes matches the window (Spec R5); with no
  overlay, captures are unchanged. Proposed: accept; agents then see what they highlighted.
- **J4 — Outline technique:** the selected meshes into an R8 mask at the target's extent, then a 2-pixel edge pass
  (a texel differs from a neighbour) in a fixed colour, drawn over everything (visible through occluders, the common
  editor convention). Proposed: accept; a depth-aware dimmed occluded part is future work.
- **J5 — Lines:** segments expanded to screen-space quads in the vertex shader from both endpoints' clip positions
  and the pixel width; no `wideLines` device feature (unavailable on many Android GPUs). Proposed: accept.
- **J6 — Limits:** per client at most 65,536 segments, 65,536 triangles, 16,384 points and 256 selected entities;
  over a limit, `InvalidRequest` and nothing changes. Proposed: accept; a Gizmo needs about a hundred, the limits stop
  a runaway debug client.
- **J7 — Testing without new goldens:** the overlay is tested by pixel assertions on captured frames (positions,
  occlusion, outline presence), not by new golden images; every existing golden stays byte-identical. Proposed:
  accept; the overlay's look is reviewed by the owner in M6, not frozen as a golden.
- **J8 — The M1 checkpoint:** the owner rules on M1's verdict table before M5 starts; an I-3 verdict adds an ADR and
  a Plan amendment before M4 changes `runFrame()`. Proposed: accept (as Plan 0064 J7).
- **J9 — Docs split:** normative sentences with the implementation PR, narratives after merge (Plans 0062–0065).
  Proposed: accept.
- **J10 — Budgets as reported targets:** Spec 0066 Q6's targets are reported against, as Plan 0065 J3/J9, never test
  verdicts. Proposed: accept.
- **J11 — Pipelines created on first use:** the overlay's shaders are loaded and its pipelines built at the first
  non-empty overlay, not at start, so startup, Android's packaged shader list and every frame without an overlay are
  unchanged. Proposed: accept; the first overlay's frame pays the creation once (reported in M6).

## Verification Checklist

- [ ] Unit tests: codec round trips and vectors (C++ and C#); the union and its lifetime; the CLI's envelopes; the C#
      Gizmo builder (R1, R2, R6, R7).
- [ ] Headless integration tests: loopback, two clients; the C# fixture-server suite (R2, R6).
- [ ] Image regression tests: every existing golden byte-identical; overlay pixel assertions (R3–R5; J7).
- [ ] Vulkan Validation Layers clean: fatal in every GPU test (M3, M4).
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
