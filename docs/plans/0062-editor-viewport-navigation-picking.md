# Plan: Editor Viewport Navigation and Picking (E1)

- **Spec:** [Spec 0062: Editor Viewport Navigation and Picking (E1)](../specs/0062-editor-viewport-navigation-picking.md)
  (`Approved` 2026-10-19) — [ADR-0123](../adr/0123-editor-viewport-camera-and-picking-host-services.md)
  (`Accepted` 2026-10-19; supersedes [ADR-0108](../adr/0108-editor-host-and-viewport-composition.md) D4's camera
  sentence in part).
  - Drafted together with the Spec at the maintainer's direction (2026-10-19). The owner ruled the Spec's
    Q1 C-a, Q2 K-a, Q3 F-a, Q4 G-a, Q5 B-a and Q6 D-a, exactly the options this Plan was drafted against.
  - Keeps ADR-0103/0104 (World verbs), ADR-0105 (`RuntimeConnection`), ADR-0106 (`RuntimeControl`, protocol),
    ADR-0107 (Dear ImGui), ADR-0109 (input) and ADR-0004.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-19 — reviewed this Plan and
  [Spec 0062](../specs/0062-editor-viewport-navigation-picking.md) together in
  [PR #270](https://github.com/slmao/Atlantis/pull/270): Spec Q1–Q6 ruled as recommended, J1–J7 ruled as
  recommended, ADR-0123 `Accepted`. Implementation explicitly authorized for M1–M7 once PR #270 has merged, on
  `feature/0062-editor-viewport-navigation-picking`, with no merge by the agent, no push to `main`, no change to
  `tests/performance/baselines/` or `gate_policy.json`, every golden byte-identical, the `runFrame()` lines
  enumerated in the implementation PR, and the gate verdict and Bistro pick time reported as measured. J6's
  interaction run is the owner's.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0062 R1–R9 as recommended:

- an editor camera with navigation, owned by Atlantis Editor and rendered by the host (ADR-0123 D1);
- Viewport picking: client-side markers, plus a host CPU ray-cast over geometry the Runtime retains only under
  `--editor` (D2);
- selection sync across Viewport, Hierarchy, Inspector and Gizmo;
- Gizmo world/local space and Ctrl snapping.

No World verb, connection, control or protocol change. Without `--editor`, nothing changes.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `1811835`.

1. **Editor** (`src/editor/`):
   - `Editor::frame()` updates the Hierarchy, then the Inspector and Gizmo subjects from
     `HierarchyModel::selection()`, runs the view and flushes the Gizmo.
   - `FrameContext::camera` is filled by the host from `captureFrameData()` only while `wantsCamera()` (Plan
     0056 J7).
   - The view (`editor_view.cpp`) projects the Gizmo with that camera; W / E / R switch modes while the Viewport
     is hovered; `widgetRect()` keys serve scripted input.
2. **Host adapter** (`src/runtime/editor_attachment.{h,cpp}`, executable-private, also compiled into
   `atlantis_runtime_gpu_tests`): `update()` builds the `FrameContext`, runs `Editor::frame()` and converts the
   draw list. `main.cpp` calls `update()` after each `runFrame()`.
3. **Frame:**
   - The active camera is read once, at `runtime_application.cpp:1474–1475`, and feeds extraction (1480), the
     camera world position (1561), fog (1615), bloom (1865) and exposure (1878).
   - The four Plan 0056 overlay places are commented `Plan 0056 P7, place N`.
   - `frame_capture.cpp` repeats the camera read in `captureFrameData()` and `captureImage()`.
   - An `extractCameraMatrices()` failure marks the lifecycle failed (1480–1491).
4. **Scene load** (`scene_load.cpp` step (e)): each distinct mesh's `StaticMeshAssetData` is uploaded by
   `createMesh()` and then dropped. Positions are read from the vertex layout as `computeLocalBoundsCentre()`
   does (`src/renderer/src/mesh_bounds.h`). Both index widths occur.
5. **Extraction inputs:** `collectRenderables()` gives `Renderable` + world matrix per entity in node order,
   without the entity's identity. GUIDs come from the baked scene's entity–GUID map (Spec 0050/0052).
6. **Gate applicability** (`tests/performance/applicability.txt`): `src/runtime/**` is included and
   `src/editor/**` is excluded, so the gate applies to this change.

## Plan-stage decisions

- **P1 — Editor public API.**
  - New UI-free model headers: `model/viewport_camera.h` (`ViewportCameraModel`, the pose/lens types and the
    matrix functions) and `model/viewport_picking.h` (rays, the marker test, the pick-request state).
  - `editor.h`:
    - `FrameContext::camera` (matrices) is replaced by `sceneCamera` (a pose and lens);
    - `FrameContext` gains `pickAnswer` and `selectionBounds`;
    - `Editor` gains `viewportCamera()`, `pickRequest()`, `boundsSubject()` (outputs after `frame()`) and
      `camera()` (the model, for hosts and tests).
  - `wantsCamera()` is removed: the host now always provides the scene pose, which is cheap (P3).
- **P2 — `FrameOverlay`.** One virtual, `viewportCamera()`, returning `std::optional<ViewportCameraOverride>` (a
  Runtime-side struct: a `Mat4` world matrix, fov, near, far). It is non-pure with a `nullopt` default, so
  existing overlays and test doubles compile unchanged.
- **P3 — New `RuntimeApplication` methods**, frame thread only and between frames:
  - `activeCameraPose()`: the active camera's world matrix and `Camera` component, from `collectActiveCamera()`,
    with no draw-item walk;
  - `pickRenderable(ray)`: `std::optional<{EntityGuid, distance}>`;
  - `renderableLocalBounds(EntityGuid)`.

  The last two are empty when no pick geometry was retained.
- **P4 — `runFrame()`'s fifth named place** (Plan 0062, place 5) replaces lines 1474–1475 with a camera chosen
  by one helper:

  ```
  const ActiveCameraInput frameCamera = overlay_ ? overlayFrameCamera(*activeCamera) : *activeCamera;  // place 5
  const Mat4& cameraWorldMatrix = frameCamera.worldMatrix;
  const atlantis::world::Camera cameraComponent = frameCamera.camera;
  ```

  - `overlayFrameCamera()` returns the active camera unless the overlay supplies one.
  - A supplied camera keeps the active `Camera` component except fov/near/far.
  - It is used only if it passes the same degeneracy checks `extractCameraMatrices()` makes, with finite values
    and `0 < near < far`. Otherwise the frame logs once and uses the active camera. Editor input never fails
    the lifecycle.
  - `frame_capture.cpp` calls the same helper (Spec Q3 F-a).
  - The PR enumerates the changed `runFrame()` lines under the AGENTS.md diff guard.
- **P5 — Pick geometry** (Spec Q4 G-a).
  - `BootstrapConfig::retainPickGeometry` defaults to `false`; only `main.cpp` sets it, under `--editor`.
  - When it is set, `loadAndBakeScene()` additionally returns, per distinct mesh `AssetId`: positions (float3),
    indices widened to `uint32` and local min/max. These come from the same `StaticMeshAssetData` before it is
    dropped, so no second read.
  - `RuntimeApplication` owns them beside `meshResourceMap` and releases them with the scene.
- **P6 — Picking unit** (`src/runtime/src/scene_picking.{h,cpp}`, private to `atlantis_runtime_host`).
  - A new collection walk gives each `Renderable` entity's GUID, mesh id and current world matrix.
  - Ray–world AABB reject per instance, then Möller–Trumbore against its triangles in world space. It is
    two-sided, and the nearest positive `t` wins; ties go to the earlier entity in node order.
  - No acceleration structure (J3).
- **P7 — Editor camera model.**
  - State: eye, yaw, pitch (±89°), pivot distance (≥ 0.05), fly speed (0.05–500 units/s, default 5), lens.
  - `fromPose()` derives yaw/pitch from the forward axis exactly as `extractCameraMatrices()` reads it. The
    convention test (M1) pins which column and sign.
  - The matrix functions mirror the host's look-at (world-up +Y) and projection.
  - Navigation functions take pixel deltas, the Viewport size and `deltaSeconds`; constants as Spec R2.
- **P8 — Picking and selection logic** (editor).
  - A click: press and release within 4 px, no Alt, not consumed by a Gizmo handle or a navigation drag.
  - Markers are tested first (10 px, nearest by view depth).
  - Otherwise one pick request carries an increasing id and a ray built from the camera matrices of the frame
    that drew the image. The view keeps the previous frame's matrices for this purpose.
  - An answer applies only if its id is the latest and no selection change happened since the request.
  - Marker positions come from `getProperty(WorldMatrix.column3)` for the Hierarchy rows with `Light` or
    `Camera` and no `Renderable`, read once per frame. Bistro has 60 lights, 60 reads in process.
- **P9 — Gizmo.**
  - `GizmoModel` gains `space()` / `setSpace()` and `setSnap(bool)`.
  - Local translate moves along the normalized rotation column. Local rotate composes R·R_axis instead of
    R_axis·R.
  - Snapping rounds the drag amount from the drag's start.
  - `flush()` and its one-transaction-per-frame contract are unchanged.
- **P10 — View.**
  - Toolbar: a `Scene | Editor` camera radio, `Align`, a `World | Local` radio, a `Markers` toggle.
  - New `widgetRect()` keys: `toolbar:SceneCam`, `toolbar:EditorCam`, `toolbar:Align`, `toolbar:World`,
    `toolbar:Local`, `toolbar:Markers`, `hierarchy:filter-hidden`.
  - Navigation captures the pointer from press to release.
  - The Hierarchy calls `SetScrollHereY` for a selection made elsewhere (once per change).
  - The bounds box is twelve projected edges, clipped to the Viewport. Markers are glyphs (a sun/bulb shape for
    `Light`, a frustum shape for `Camera`) in the overlay draw list.

## Milestones / Task Breakdown

Each milestone builds and runs the affected suites (Debug) before the next (AGENTS.md).

### M1 — Editor camera model and the convention cross-check (R1, R2, R3)

- `model/viewport_camera.{h,cpp}` (P7) with GPU-free tests: initialization, look, orbit, pan, dolly, fly, clamps,
  and framing a sphere.
- `tests/runtime/editor_camera_convention_tests.cpp`: the editor's view/projection equal
  `extractCameraMatrices()`'s within 1e-6 over a sweep of poses (including pitch ±89°), fovs, near/far and
  aspects. `fromPose(worldMatrix)` re-composes to the same eye and forward. `atlantis_runtime_tests` links
  `Atlantis::Editor` (test-only).

### M2 — The host camera override (R3, R9; ADR-0123 D1, D3)

- `frame_overlay.h` (P2), `RuntimeApplication::activeCameraPose()` (P3), place 5 and `overlayFrameCamera()`
  (P4), and the capture path (P4).
- GPU tests in `runtime_smoke_gpu_tests.cpp` (fatal VVL), with a test overlay:
  - the camera block equals the extraction of the supplied pose;
  - the Viewport differs from the scene camera's;
  - a supplied pose equal to the scene camera's gives a byte-identical Viewport;
  - an invalid pose falls back without failing;
  - `captureFrameData()` reports the drawn camera.

### M3 — Pick geometry and the host services (R4, R6, R9; ADR-0123 D2)

- `bootstrap_config.h` (P5); `scene_load.{h,cpp}` returns the retained geometry; `RuntimeApplication` owns it and
  exposes `pickRenderable()` / `renderableLocalBounds()` (P3); `scene_picking.{h,cpp}` (P6).
- GPU-free tests (`tests/runtime/scene_picking_tests.cpp`, on hand-built geometry and the catalog test scenes):
  - nearest hit, miss, back face, instanced mesh;
  - 16- and 32-bit source indices;
  - a `WorldMatrix` edited through the access layer is picked at its new place;
  - local bounds;
  - nothing is retained without the switch.

### M4 — Editor picking, markers, selection sync, Gizmo modes (R4–R7)

- `model/viewport_picking.{h,cpp}` (P8), `gizmo.{h,cpp}` (P9), `editor.{h,cpp}` (P1: `FrameContext`, the outputs,
  answer and staleness handling).
- Model tests in `editor_model_tests.cpp`:
  - ray through a projected point;
  - marker order and radius;
  - a pick answer selects, a stale answer is dropped, a miss clears;
  - destroying the selection clears it, and the subjects follow;
  - Local translate/rotate, snapping steps, one transaction per frame.

### M5 — The view (R1, R2, R4–R7)

- `editor_view.{h,cpp}` (P10): navigation input, click/drag discrimination, toolbar, Hierarchy scroll-to and the
  filter note, the bounds box, markers.
- Headless UI tests in `editor_view_tests.cpp`, scripted through `widgetRect()`:
  - right-drag looks; Alt+left-drag orbits; the wheel dollies;
  - a click issues one pick request, while a 4 px drag issues none;
  - a handle press drags without picking;
  - Esc clears; an external selection scrolls the Hierarchy; X toggles space; Ctrl snaps.

### M6 — Wiring and the north star (all R)

- `editor_attachment.{h,cpp}`: fill `sceneCamera` from `activeCameraPose()` every frame. After
  `Editor::frame()`, answer `pickRequest()` and `boundsSubject()` through P3 and keep the answers for the next
  frame. Implement `viewportCamera()`.
- `main.cpp`: set `retainPickGeometry` under `--editor`.
- **North star** (`runtime_smoke_gpu_tests.cpp`, fatal VVL, default scene): the first Viewport is byte-identical
  to the scene camera's; then the Spec Summary sequence is scripted. Assert:
  - the selected GUID, the Inspector subject and the Gizmo subject;
  - the bounds box was drawn;
  - the transaction's rotation equals 15°·k exactly in Local;
  - F leaves the selection's centre at the Viewport centre.
- **Bistro, content-gated:** the same click-to-select at a fixed pixel. The worst-case `pickRenderable()` time
  over a grid of rays is logged (Release), not asserted.

### M7 — Normative docs and acceptance (J5, J6)

- AGENTS.md:
  - in the Editor paragraph, the host-interface values (ADR-0123 D3) and the Runtime methods;
  - in the Runtime paragraph, "five named places" and the `runFrame()` guard sentence (place 5).
- `docs/architecture/module_boundaries.md`, the Editor and Runtime sections.
- Acceptance:
  - Debug and Release full suites, every golden byte-identical, VVL clean;
  - `assembleDebug`;
  - the editor boundary scan;
  - `atlantis_runtime --gate --json`, with the verdict reported;
  - the human interaction run (J6).

## Files / Modules Touched (expected)

- **Editor:**
  - `src/editor/include/atlantis/editor/editor.h`, `src/editor/src/editor.cpp`;
  - `src/editor/include/atlantis/editor/model/viewport_camera.h`, `src/editor/src/model/viewport_camera.cpp`
    (new);
  - `src/editor/include/atlantis/editor/model/viewport_picking.h`, `src/editor/src/model/viewport_picking.cpp`
    (new);
  - `src/editor/include/atlantis/editor/model/gizmo.h`, `src/editor/src/model/gizmo.cpp`;
  - `src/editor/src/view/editor_view.h`, `src/editor/src/view/editor_view.cpp`;
  - `src/editor/CMakeLists.txt`.
- **Runtime (host library):**
  - `src/runtime/include/atlantis/runtime/frame_overlay.h`;
  - `src/runtime/include/atlantis/runtime/runtime_application.h`, `src/runtime/src/runtime_application.cpp`;
  - `src/runtime/src/frame_capture.cpp` (and `frame_capture.h` if the helper is declared there);
  - `src/runtime/include/atlantis/runtime/scene_load.h`, `src/runtime/src/scene_load.cpp`;
  - `src/runtime/include/atlantis/runtime/bootstrap_config.h`;
  - `src/runtime/src/scene_picking.h`, `src/runtime/src/scene_picking.cpp` (new);
  - `src/runtime/CMakeLists.txt`.
- **Runtime (executable-private):** `src/runtime/editor_attachment.h`, `src/runtime/editor_attachment.cpp`,
  `src/runtime/main.cpp`.
- **Tests:**
  - `tests/editor/editor_model_tests.cpp`, `tests/editor/editor_view_tests.cpp`, `tests/editor/editor_fixture.h`
    (if fixture helpers are needed), `tests/editor/CMakeLists.txt`;
  - `tests/runtime/editor_camera_convention_tests.cpp` (new), `tests/runtime/scene_picking_tests.cpp` (new),
    `tests/runtime/runtime_smoke_gpu_tests.cpp`, `tests/runtime/CMakeLists.txt`.
- **Docs:** `AGENTS.md`, `docs/architecture/module_boundaries.md` (M7); `docs/specs/README.md`; Spec/Plan/ADR
  status lines post-merge.
- **Not touched:** `src/world/`, `src/connection/`, `src/remote/`, `src/cli/`, `src/gameplay_sdk/`, `src/csharp/`,
  `src/platform/`, `src/rhi/`, `src/vulkan_backend/`, `src/renderer/`, `src/render_graph/`, `shaders/`, goldens,
  `tests/performance/baselines/`, `gate_policy.json`.

## Sequencing & Dependencies

- M1 first: the convention test pins the math that M2's tests and M4's rays rely on.
- M2 and M3 are independent host changes; either order. Both precede M6.
- M4 depends on M1. M5 depends on M4.
- M6 needs M2–M5. M7 is last.
- One branch, `feature/0062-editor-viewport-navigation-picking`, one PR.

## Verification Checklist

- [ ] Unit (GPU-free):
  - camera model (M1);
  - convention cross-check, 1e-6 (M1);
  - picking unit and retention (M3);
  - editor models: picking, markers, selection sync, Gizmo space and snapping (M4);
  - headless UI scripts (M5).
- [ ] GPU (fatal VVL):
  - camera override, fallback, byte-identity when equal, capture follows (M2);
  - north star (M6);
  - Bistro click, content-gated (M6).
- [ ] Image regression: all goldens byte-identical, none re-captured (R9).
- [ ] Vulkan Validation Layers clean on every GPU suite.
- [ ] Other:
  - editor boundary scan unchanged (R8);
  - `git diff origin/main --stat -- src/world src/connection src/remote src/cli src/gameplay_sdk src/csharp
    src/platform src/rhi src/vulkan_backend src/renderer src/render_graph shaders` is empty (R8);
  - `runFrame()` diff limited to place 5's lines, enumerated in the PR;
  - Debug + Release full suites;
  - `assembleDebug`;
  - performance gate verdict reported, no baseline touched;
  - Bistro pick time reported against the 50 ms budget;
  - human interaction run (J6).

## Risks

- **Convention drift** between the editor's matrices and the host's: caught by M1's test.
- **Pick-time outliers on Bistro** (a ray grazing many large-bounds instances): measured in M6. Over budget →
  J3.
- **Pointer capture in ImGui:** navigation drags must not leak into the Hierarchy or Inspector. Covered by M5's
  scripted tests.
- **Gate noise:** the frame change is one branch on an already-attached overlay. A non-`pass` verdict is
  reported as it is, never rebaselined by the agent.

## Joint Review decisions — ruled (slmao, 2026-10-19, PR #270): J1–J7 as proposed

- **J1 — Spec Corrections:** none proposed.
- **J2 — Bounds/pick answers one frame late** (P8). Proposed: accept; a synchronous path is rejected by ADR-0123
  D3.
- **J3 — No pick acceleration structure in E1** (P6). Proposed: brute force with per-instance bounds reject; a
  BVH only if M6 measures more than 50 ms.
- **J4 — `FrameContext::camera` removal** (P1). Proposed: replace with `sceneCamera`. Its two users (the view,
  the adapter) change in this PR.
- **J5 — Docs split.** Proposed: as Plan 0056 J10. Normative sentences land with the implementation PR;
  registry, blueprint and status lines land after merge.
- **J6 — Acceptance runs.** Proposed: the maintainer runs `atlantis_runtime --editor` on the default scene and
  Bistro and judges navigation feel and picking; tuning constants stay within Spec R2/R7's stated values unless
  the maintainer rules otherwise.
- **J7 — Shortcut collisions.**
  - Proposed: W / E / R switch Gizmo modes only while the right button is up. While it is held, W / A / S / D /
    Q / E fly.
  - X toggles space. F frames. Esc clears the selection.

## Rollback Plan

Revert the implementation PR. The change is confined to the editor library, the executable-private adapter and
additive Runtime methods, plus one named `runFrame()` place. No format, golden, baseline or protocol depends on
it.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:

- The performance gate's verdict is reported in the PR, and the `runFrame()` lines are enumerated.
- The human interaction run (J6) is recorded in the PR.
