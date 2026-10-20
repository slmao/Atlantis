# Plan: Editor Platform Direction — Phase 1, the Investigative Prototype

- **Spec:** [Spec 0064: Editor Platform Direction](../specs/0064-editor-platform-direction.md) (`Approved`) —
  [ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md),
  [ADR-0126](../adr/0126-viewport-embedding-and-input-routing.md),
  [ADR-0127](../adr/0127-editor-application-managed-dependency-policy.md),
  [ADR-0128](../adr/0128-remote-viewport-camera-and-raycast.md) (all `Accepted` 2026-10-20); the Spec is
  `Approved` 2026-10-20.
  - Drafted together with the Spec at the maintainer's direction (2026-10-20). The owner ruled Q1 P-a, Q2 V-a + R-a
    (V-a2 the fallback inside ADR-0126), Q3 W-a, Q4 D-a, Q5 T-a and Q6 G-a, exactly the options this Plan was
    drafted against.
  - Scope: **Phase 1 only.** Phases 2–4 are the Spec's background and get their own Specs.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-20 — reviewed this Plan and
  [Spec 0064](../specs/0064-editor-platform-direction.md) together in [PR #275](https://github.com/slmao/Atlantis/pull/275).
  - **Spec:** Q1–Q6 ruled as recommended (Q2: V-a + R-a, with V-a2 the fallback inside ADR-0126).
  - **Plan:** J1–J7 ruled as recommended; no Spec Correction.
  - **ADRs:** ADR-0125, ADR-0126, ADR-0127 and ADR-0128 `Accepted`.
  - **Ruling note (recorded here, the Spec text unchanged):** under Q5 T-a, if Phase 1 is delayed, splitting E3's
    data layer — the authoring document, its access and the bake, without UI — off the editor side and letting it
    go first may be considered. It would need its own Spec.
  - **Authorization:** implementation on `feature/0064-editor-platform-phase1` once PR #275 has merged, M1–M5 first.
    The Avalonia dependency is authorized from M5 on: the newest stable release at that time, its version, lock
    files and MIT licence recorded in the PR. After M5 the agent stops and hands over the M6 investigation
    protocol; the owner runs M6 and holds the J7 checkpoint before the report is folded in and M7 completes.
  - **Constraints:** no merge by the agent, no push to `main`; `tests/performance/baselines/` and
    `gate_policy.json` untouched; goldens byte-identical; changes limited to this Plan's file list; the changed
    `runFrame()` line enumerated in the PR; the performance gate applies (`src/runtime/**`, the root
    `CMakeLists.txt`, `cmake/**`) and its verdict is reported as measured; the C# SDK's zero-NuGet check unchanged
    and passing; a default configure unchanged (`ATLANTIS_BUILD_EDITOR_APP` defaults OFF).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Answer Spec 0064's two unknowns — **U1** (the Runtime's Viewport embedded in a foreign window) and **U2** (input
routing between that window and Avalonia) — with the smallest real system that can: Platform's child-window
mode, the two control-surface additions every client gets (`setViewportCamera`, `raycast`), and an Avalonia
shell that hosts Bistro, orbits it and picks in it. Report the findings; the owner checkpoints the report before
Phase 2 is specified (Spec 0064 R10, the Spec 0061 R1 shape).

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `77197fb`.

1. **Platform (Windows):** one top-level window (`windows_platform.cpp:355`, `WS_OVERLAPPEDWINDOW`),
   per-monitor-DPI-aware v2 (`:318`), `WindowOptions{requestedClientSize}` (`platform.h`), input events as plain
   values (ADR-0109).
2. **Runtime CLI** (`src/runtime/cli.cpp`): `--scene`, `--listen`, `--session-file`, `--editor`; `bistro` is a
   selectable scene (`main.cpp:96`).
3. **`RuntimeControl`** (`src/connection/include/atlantis/connection/runtime_control.h:122`): `status`, `pause`,
   `resume`, `step`, `diagnostics`; implemented by `RuntimeControlHost` (Runtime) and `RemoteControl` (Remote);
   the editor tests' `FakeRuntime` implements it too.
4. **Remote:** the server dispatches `control.*` in `remote_server.cpp`; the client calls them in
   `remote_client.cpp`; `remote_protocol.md` §5 lists 16 methods; conformance vectors under
   `tests/csharp/conformance/`.
5. **E1's services** (`RuntimeApplication::pickRenderable()`, `renderableLocalBounds()`, `FrameOverlay::viewportCamera()`,
   `overlayFrameCamera()` at `runFrame()` place 5) and `BootstrapConfig::retainPickGeometry`.
6. **C# SDK:** `Control.cs` (`RuntimeStatus`, `StepRequest`, `StepResult`), the remote client under `Remote/`;
   `check_csharp_rules.cmake` scans `src/csharp/**`, `examples/csharp/**`, `tests/csharp/**`.
7. **Gate applicability** (`tests/performance/applicability.txt`): `src/runtime/**`, the root `CMakeLists.txt`
   and `cmake/**` are included, so Phase 1 runs the gate; `src/remote/**`, `src/cli/**`, `src/connection/**` and
   `src/csharp/**` are excluded; `src/platform/**` and the new `src/editor_app/**` are not listed (M7 confirms with
   `--gate --applies`).

## Plan-stage decisions

- **P1 — Child-window mode** (ADR-0126 D1–D3).
  - `platform::WindowOptions` gains `std::optional<NativeWindowHandle> parent` (Windows only).
  - Windows Platform: with a parent, `CreateWindowExW(0, …, WS_CHILD | WS_CLIPSIBLINGS | WS_VISIBLE, 0, 0, w, h,
    parent, …)` at the parent's client size; `WM_MOUSEACTIVATE` → `MA_NOACTIVATE`; parent destruction →
    `WindowCloseRequested`.
  - R-a forwarding (ADR-0126 D4): pointer, wheel and key messages are re-posted to the parent with coordinates
    mapped into its client space (`MapWindowPoints`), after Platform has emitted its own ADR-0109 events.
  - `atlantis_runtime --parent-window <decimal handle>` (refused with `--editor`); the session file's `window`
    field (decimal), written only in child-window mode.
- **P2 — Control additions** (ADR-0128 D1–D3).
  - `connection::RuntimeControl` gains pure virtual `setViewportCamera(std::optional<ViewportCamera>)` and
    `raycast(Ray) → Result<std::optional<RayHit>, ControlError>` (new `ControlError::NoGeometry`). Value types in
    `runtime_control.h`: `ViewportCamera{worldMatrix[16], fovYRadians, nearZ, farZ}`, `Ray{origin, direction}`,
    `RayHit{entity, distance}`.
  - `RuntimeControlHost`: holds the camera (cleared when the setting Remote connection closes — the host is told by
    the server's disconnect path); `raycast` calls `RuntimeApplication::pickRenderable()`.
  - `RuntimeApplication`: the camera place's helper takes the overlay's camera first, else the control's (one
    accessor `setControlViewportCamera()`; the `runFrame()` line itself is unchanged — the helper decides).
    `overlayFrameCamera()` is renamed `viewportFrameCamera()` and is called when either source is set; the one
    `runFrame()` line that calls it changes its condition. The PR enumerates the changed `runFrame()` line.
  - Retention: `main.cpp` sets `retainPickGeometry` for `--listen` too; `--no-pick-geometry` turns it off.
  - Implementers updated: `RuntimeControlHost`, `RemoteControl`, the editor tests' `FakeRuntime` and any other
    test double (all are in the file list).
- **P3 — Wire, CLI, C# SDK.**
  - Remote: `control.setViewportCamera {camera: <Camera>|null}` → `null`; `control.raycast {ray: <Ray>}` →
    `{"ok": {"entity": "<E>", "distance": <f>} | null}` or `{"err": "NoGeometry"}`. Floats as the codec's existing
    float form. `remote_protocol.md` §5/§6.6 gain them.
  - Conformance vectors regenerated by the C++ codec and committed; the C# tests decode and re-encode them.
  - CLI: `runtime camera set --matrix <16 floats> --fov <f> --near <f> --far <f>`, `runtime camera clear`,
    `runtime raycast --origin x y z --direction x y z`, each with its `cli/1` JSON result.
  - C# SDK: `Control.cs` gains `SetViewportCamera(ViewportCamera?)` and `Raycast(Ray)`, the records and the error.
- **P4 — The editor application** (`src/editor_app/`; ADR-0125 D1, ADR-0127).
  - `Atlantis.Editor.csproj` (WinExe, `net10.0-windows`), `Directory.Packages.props` (exact versions),
    `packages.lock.json`, `NuGet.config` (clear + nuget.org, or `ATLANTIS_NUGET_OFFLINE_FEED`),
    `allowed_packages.txt` (Avalonia's own packages), `cmake/check_editor_app_packages.cmake`.
  - The Avalonia version is chosen in M5 as the newest stable release at that time and recorded with its lock
    file; the PR names its licence (MIT).
  - It references the C# SDK as a project reference (no NuGet).
  - CMake: `ATLANTIS_BUILD_EDITOR_APP` (default OFF), gated on the .NET SDK as ADR-0113 D4; Android never.
  - **The shell:** a window with two panes — a list (entities from `listEntities`, the picked one highlighted) and
    a `NativeControlHost` whose placeholder `HWND` is passed to `atlantis_runtime --listen 0 --parent-window <h>
    --scene <name> --session-file <tmp>`; the session file gives the port, token and child handle; the host's
    size changes are applied to the child with `SetWindowPos` (via `user32` P/Invoke inside the editor application
    only — editor-side OS calls on its own windows, no engine code; ADR-0125 D1's "no P/Invoke of engine code" is
    kept).
  - **Navigation and picking:** E1's camera model ported minimally (orbit about a pivot, dolly, the
    `extractCameraMatrices` convention, with a C# convention test against vectors produced by the C++ test) →
    `SetViewportCamera`; a click → ray from the shown camera → `Raycast` → highlight in the list.
- **P5 — The investigation** (Spec 0064 R10). Measured on the owner's machine, Release, Bistro, and reported in a
  findings section of the Phase 1 PR:
  - **U1:** frame time embedded vs. standalone `atlantis_runtime --scene bistro` (median and p95 over 600 frames);
    resize (drag the splitter: the child follows, no validation error, no device loss); DPI change (move between
    monitors of different scale, or change scale); minimize/restore; editor close tearing down the Runtime.
  - **U2:** route R-a — orbit and click work; input latency (pointer-down to the next frame's camera change, from
    timestamps on both sides); keyboard focus (typing in an editor text box while the Viewport is visible); the
    input-queue coupling — editor UI responsiveness while the Runtime renders Bistro (frame time about 130 ms).
  - **Verdict table:** V-a + R-a adopted / V-a with R-b needed / V-a2 needed / both fail (owner re-rules).

## Milestones / Task Breakdown

Each milestone builds and runs the affected suites (Debug) before the next.

### M1 — Platform child-window mode and `--parent-window` (U1 groundwork; ADR-0126 D1–D3)

- `platform.h` (`WindowOptions::parent`), `windows_platform.cpp` (P1), Android ignores the field.
- `cli.{h,cpp}` and `main.cpp` (`--parent-window`, refused with `--editor`); `session_file` writer (`window`).
- Tests: Platform test creates a top-level test window, starts Platform with it as parent, checks `WS_CHILD`, the
  parent relation and `WindowResize` on resize, and that shutdown destroys the child and not the parent;
  without `parent` the existing tests are unchanged. A GPU smoke test renders a frame into a child window (fatal
  VVL). CLI tests for the option and its refusal.

### M2 — `setViewportCamera` and `raycast` on `RuntimeControl` (ADR-0128 D1, D2)

- `runtime_control.h`, `runtime_control_host.{h,cpp}`, `runtime_application.{h,cpp}` (the helper and accessor),
  `frame_capture.cpp` (same helper), `main.cpp` (retention, `--no-pick-geometry`), test doubles.
- Tests (GPU, fatal VVL): a control camera draws the frame and frame data reports it; clearing restores the scene
  camera byte for byte; overlay camera wins over the control's; an unusable camera is ignored; `raycast` equals
  `pickRenderable()` for the same ray; `NoGeometry` without retention. GPU-free: the host's camera lifetime
  (cleared on the owner's disconnect).

### M3 — The wire, the CLI (ADR-0128 D3)

- `src/remote/src/{codec,remote_server,remote_client}.cpp`, `remote_protocol.md`, conformance vectors.
- `src/cli/` (`runtime camera`, `runtime raycast`).
- Tests: codec round-trips; server/client end to end over loopback; vectors regenerated and checked; CLI JSON
  envelopes; the CLI's `runtime raycast` equals an in-process `pickRenderable()` on the default scene.

### M4 — The C# SDK

- `src/csharp/Atlantis.Gameplay/Control.cs` and the remote client; C# tests decoding the new vectors and calling
  both methods against `atlantis_runtime --listen` (the SDK's existing end-to-end fixture). Zero NuGet unchanged.

### M5 — The editor application shell (ADR-0125 D1, ADR-0127)

- `src/editor_app/**` (P4), `cmake/check_editor_app_packages.cmake`, CMake wiring.
- Tests: the package-policy check (and a negative case: an unpinned reference fails it); the C# camera-convention
  test; a headless smoke test that launches the shell against `--listen` without embedding (process start, connect,
  list entities, set a camera, ray-cast) — embedding itself is verified manually in M6.
- An offline restore from a feed populated from the lock file, recorded.

### M6 — The investigation and its report (Spec 0064 R10; P5)

- The owner's machine, Release, Bistro: every P5 measurement, the verdict table, and the route adopted.
- If R-a fails: stop and report (R-b needs its own Spec, ADR-0126 D4). If V-a fails: try V-a2 inside this
  milestone (ADR-0126 D5) and report both.

### M7 — Normative docs, acceptance

- AGENTS.md: the module list gains Atlantis Editor App (managed, not in the native graph); Platform's child-window
  mode; `RuntimeControl`'s two calls; the editor-app dependency rule (ADR-0127).
  `docs/architecture/module_boundaries.md`: the same sections. `remote_protocol.md` already updated in M3.
- Acceptance: Debug and Release full suites; goldens byte-identical; VVL clean; `assembleDebug`; the C# SDK
  rules check passing unchanged; the editor-app policy check passing; `atlantis_runtime --gate --json` run and its
  verdict reported (`src/runtime/**`, the root `CMakeLists.txt` and `cmake/**` are on the applicability list); the
  owner's checkpoint of the M6 report.

## Files / Modules Touched (expected)

- **Platform:** `src/platform/include/atlantis/platform/platform.h`, `src/platform/src/windows/windows_platform.cpp`,
  `src/platform/src/android/android_platform.cpp` (ignores the field, if a change is needed at all).
- **Connection:** `src/connection/include/atlantis/connection/runtime_control.h` (and its `.cpp` for `toString`).
- **Runtime:** `src/runtime/cli.{h,cpp}`, `src/runtime/main.cpp`,
  `src/runtime/include/atlantis/runtime/runtime_control_host.h`, `src/runtime/src/runtime_control_host.cpp`,
  `src/runtime/include/atlantis/runtime/runtime_application.h`, `src/runtime/src/runtime_application.cpp`,
  `src/runtime/src/frame_capture.cpp`, `src/runtime/editor_attachment.cpp` (rename of the helper only, if touched).
- **Remote:** `src/remote/src/codec.{h,cpp}`, `src/remote/src/remote_server.cpp`, `src/remote/src/remote_client.cpp`,
  `src/remote/src/session_file.cpp` and its header (the `window` field),
  `src/remote/include/atlantis/remote/*.h` as needed.
- **CLI:** `src/cli/` (the two commands).
- **C# SDK:** `src/csharp/Atlantis.Gameplay/Control.cs`, `src/csharp/Atlantis.Gameplay/Remote/*`.
- **Editor application (new):** `src/editor_app/**`, `cmake/check_editor_app_packages.cmake`, the root
  `CMakeLists.txt` (one `add_subdirectory` behind `ATLANTIS_BUILD_EDITOR_APP`), `cmake/AtlantisDotnet.cmake` if the
  gate needs it.
- **Tests:** `tests/platform/`, `tests/runtime/` (CLI, control host, GPU smoke), `tests/remote/`, `tests/cli/`,
  `tests/csharp/` (vectors, SDK tests), `tests/csharp/conformance/` (regenerated), `tests/editor/editor_fixture.h`
  (the `FakeRuntime` gains the two calls), `src/editor_app/tests/`.
- **Docs:** `docs/architecture/remote_protocol.md` (M3), `AGENTS.md`, `docs/architecture/module_boundaries.md`
  (M7); registry, README, blueprint and status lines after merge.
- **Not touched:** `src/world/`, `src/asset_system/`, `src/rhi/`, `src/vulkan_backend/`, `src/renderer/`,
  `src/render_graph/`, `shaders/`, `src/editor/` (the ImGui editor library, frozen), `src/gameplay_sdk/`, goldens,
  `tests/performance/baselines/`, `gate_policy.json`.

## Sequencing & Dependencies

- M1 and M2 are independent (Platform vs. control); M3 needs M2; M4 needs M3; M5 needs M1 and M4; M6 needs M5;
  M7 is last.
- One branch, `feature/0064-editor-platform-phase1`, one PR; M6's report is part of its description.

## Verification Checklist

- [ ] Unit / headless: Platform child mode; CLI options; control host camera lifetime; codec round-trips;
  conformance vectors; CLI envelopes; C# SDK calls; editor-app policy check (positive and negative); C# camera
  convention; editor-app headless smoke.
- [ ] GPU (fatal VVL): a frame into a child window; the control camera (draw, clear byte-identical, overlay wins,
  unusable ignored); `raycast` = `pickRenderable()`.
- [ ] Image regression: all goldens byte-identical.
- [ ] Vulkan Validation Layers clean.
- [ ] Other: Debug + Release full suites; `assembleDebug`; the SDK's zero-NuGet check unchanged; an offline restore
  of the editor app; the performance gate's verdict; the `runFrame()` line enumerated; the M6 report and the
  owner's checkpoint.

## Risks

- **U1/U2 fail:** the report says so with numbers; ADR-0126 D5's fallback is tried in M6; beyond it the owner
  re-rules (Spec 0064 Q5 then holds E3 back on both sides).
- **Input-queue coupling** under Bistro's 130 ms frames: measured in M6; V-a2 avoids it.
- **The first managed packages in the build:** contained by ADR-0127's scope, lock files and check; default
  configure unchanged (`ATLANTIS_BUILD_EDITOR_APP` OFF).
- **A pure-virtual addition to `RuntimeControl`** breaks every implementer at compile time — intended; all are in
  the file list.

## Joint Review decisions — ruled (slmao, 2026-10-20, PR #275): J1–J7 as proposed

- **J1 — Spec Corrections:** none proposed.
- **J2 — The control camera's lifetime** (P2): cleared when the connection that set it closes. Proposed: accept;
  a stale camera after an editor crash would otherwise persist.
- **J3 — Retention under `--listen`** (P2): on by default with `--no-pick-geometry`. Proposed: accept (about 42 MB
  on Bistro).
- **J4 — The Avalonia version** (P4): the newest stable release at M5, recorded with its lock file. Proposed:
  accept; pinning a version in the Plan would date it before the Spec is approved.
- **J5 — Editor-side P/Invoke of `user32`** (P4): the editor sizing its own hosted child window. Proposed: accept;
  it is OS UI work in the editor's own process, not engine code (ADR-0112 D1's rule is about engine code).
- **J6 — Docs split:** normative sentences with the implementation PR, narratives after merge (Plans 0056/0062).
- **J7 — The checkpoint:** the M6 report is reviewed by the owner before the PR leaves draft; Phase 2's Spec
  starts only after.

**J7 checkpoint — ruled (slmao, 2026-10-21, on the M6 run):** V-a + R-a adopted, with two caveats recorded in the
M6 findings: camera latency is 1.5–2 Runtime frames by construction (frame-boundary answers), and sporadic
100–600 ms editor-UI stalls appear at drag transitions (the input-queue attachment Spec 0064 named). Phase 2's
Spec may address the stalls; V-a2 was not needed. M6 also found and fixed one M1 defect (a child window drew
nothing until its host first resized it: Platform now reports the child's extent right after `SurfaceCreated`).
The owner's keyboard-focus observation (U2-3) was not recorded; the measured items are in the PR's M6 report.

## Rollback Plan

Revert the implementation PR. The editor application is behind `ATLANTIS_BUILD_EDITOR_APP` (default OFF); the
control calls are additive; Platform's child mode is unused without `--parent-window`. No format, golden or
baseline depends on Phase 1.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:

- The M6 findings report with the verdict table, and the owner's checkpoint, are in the PR.
- The performance gate's verdict is reported and the changed `runFrame()` line enumerated.
