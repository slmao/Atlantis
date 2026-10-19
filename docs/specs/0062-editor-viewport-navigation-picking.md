# Spec: Editor Viewport Navigation and Picking (E1)

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-19
- **Implementation:** merged — M1–M7 in [PR #271](https://github.com/slmao/Atlantis/pull/271).
- **Related Plan(s):** [Plan 0062](../plans/0062-editor-viewport-navigation-picking.md) (`Approved`; Joint
  Human Review 2026-10-19 recorded in its header; implemented in PR #271). Drafted together with this Spec at the
  maintainer's direction.
- **Approval:** slmao, 2026-10-19 — owner rulings Q1–Q6, each as its recommendation (Q1 C-a, Q2 K-a, Q3 F-a,
  Q4 G-a, Q5 B-a, Q6 D-a), recorded once in [Plan 0062](../plans/0062-editor-viewport-navigation-picking.md);
  review of this Spec's own branch PR, [PR #270](https://github.com/slmao/Atlantis/pull/270). The open-question
  text below keeps the options as the record of what was weighed.
- **Related ADR(s):**
  - [ADR-0123](../adr/0123-editor-viewport-camera-and-picking-host-services.md) (`Accepted` 2026-10-19): the editor
    camera's ownership and its hand-off to the host, and picking as a host service. It supersedes
    [ADR-0108](../adr/0108-editor-host-and-viewport-composition.md) D4's camera sentence ("The camera is the
    scene's active camera") and extends D2's list of host-interface values; ADR-0108 D1, D3 and D5 are
    unchanged.
  - Unchanged: [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) and
    [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) (the World's five commands, five events and
    transactions), [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md) (`RuntimeConnection`),
    [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md) (`RuntimeControl`, the wire protocol),
    [ADR-0107](../adr/0107-editor-ui-library-selection.md) (Dear ImGui, core only),
    [ADR-0109](../adr/0109-platform-input-events.md) (Platform input events) and
    [ADR-0004](../adr/0004-phase1-threading-baseline.md).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

The minimal editor (Spec 0056) shows the Runtime's frame from the scene's own camera and selects entities only
from the Hierarchy. This Spec is the first editor iteration, **E1**: a free **editor camera** with orbit, pan,
dolly and fly navigation; **picking** entities by clicking in the Viewport; **selection sync** between the
Viewport, Hierarchy, Inspector and Gizmo; and **Gizmo modes** (world/local space and snapping beside the
existing translate/rotate/scale).

All four stay inside the World's closed verb surface: **no command, event or query is added**, and
`RuntimeConnection`, `RuntimeControl` and the wire protocol are unchanged. The editor camera is client state,
never written to the World. What the client genuinely cannot do alone — render from a camera other than the
scene's, and intersect a ray with geometry it has no access to — becomes two **host services** on the editor's
existing plain-value interface with its host (ADR-0108 D2), not World verbs. That choice, and the verb-based
alternatives it rejects, are this Spec's open questions Q1 and Q2.

**North star:** open the editor on the default scene → the Viewport looks exactly as in Spec 0056 → orbit
around the scene with Alt+drag → click a cube → its row is selected and scrolled to in the Hierarchy, the
Inspector shows it, the Gizmo sits on it and a bounds box outlines it → switch the Gizmo to Local and rotate
with Ctrl held (15° steps) → press F to frame it. Nothing about the World changed except the rotation.

## Motivation / Problem Statement

### What exists (Spec 0056, merged in PR #232)

- **Selection** lives in `HierarchyModel::selection()`; `Editor::frame()` hands it to the Inspector and the
  Gizmo as their subject each frame (`src/editor/src/editor.cpp`). A Hierarchy click is the only way to set it.
- **The Viewport** is the scene drawn by `drawFrame()` into an offscreen target at the panel's size and sampled
  by the overlay pass (ADR-0108 D3). Its camera is the scene's active camera (ADR-0108 D4).
- **The Gizmo** draws world-axis handles projected with `FrameContext::camera`, which the host fills from
  `captureFrameData()` only while a Gizmo is shown (Plan 0056 J7). Modes Translate / Rotate / Scale, switched by
  the toolbar or W / E / R while the Viewport is hovered. Translate moves along a world axis, Rotate turns about
  a world axis, Scale scales the entity's own axis. A drag writes `WorldMatrix` as one transaction per frame
  (Spec 0056 ruling Q5, G-a).

### Why the gaps were left (Spec 0056 ruling Q3)

> A free editor camera would either write the camera entity's `WorldMatrix` (an edit to the World) or need a
> Renderer override (a privileged path); neither is v1. … Picking … needs an ID buffer or mesh data the client
> cannot reach.

Both observations still hold, and E1 has to answer them.

### Facts found while drafting

- **One place reads the camera.** In `runFrame()`, `cameraWorldMatrix` and `cameraComponent`
  (`runtime_application.cpp:1474–1475`) are the only reads of the active camera. The world matrix feeds the
  view/projection extraction and the camera world position. The component feeds fov/near/far, fog, bloom and
  exposure. `frame_capture.cpp` repeats the same two reads for `captureFrameData()` / `captureImage()`. The
  directional shadow's light-space matrices depend on the light direction only
  (`computeShadowLightSpaceMatrices(direction)`), not on the camera.
- **The camera convention:** `extractCameraMatrices()` takes the eye and forward from the world matrix and
  builds a look-at with world-up +Y. Roll is therefore ignored, and a forward parallel to +Y is an error
  (`DegenerateCameraBasis`).
- **The client cannot reach geometry.** `RuntimeConnection` carries World data only. `Renderable` holds a mesh
  `AssetId` and nothing else. Runtime discards each mesh's CPU data once it is uploaded (`scene_load.cpp`, step
  (e)); a `renderer::Mesh` keeps only its local bounds centre.
- **The connection has no active-camera identity** (Spec 0056 Motivation): the bake's
  `BakedScene::activeCamera` is not a component.
- **Bistro geometry** (`content/bistro/bistro.gltf`, counted 2026-10-19): 551 meshes, 2909 mesh instances (295
  meshes instanced more than once), every mesh node with its own translation; 1,738,262 vertices, 1,753,630
  triangles. A CPU copy of positions (12 B/vertex) and 32-bit indices is about 20.9 MB + 21.0 MB ≈ 42 MB.
- **Materials can be alpha-masked** (`alpha_mode` / `alpha_cutoff`, material artifact v8): what is drawn is not
  always the whole triangle.
- **The World's verb surface** (Spec 0052, closed): commands `CreateEntity`, `DestroyEntity`, `AddComponent`,
  `RemoveComponent`, `SetProperty`; events `EntityCreated`, `EntityDestroyed`, `ComponentAdded`,
  `ComponentRemoved`, `PropertyChanged`; plus Spec 0053's transactions.

## Goals

- **E1 is exactly four things:** viewport navigation (an editor camera), entity picking, selection sync and
  Gizmo modes — the maintainer's scope of 2026-10-19.
- **The World's verb surface stays closed** (maintainer constraint): no new command, event, query or component.
  A need for one would be argued here as an open question; none is proposed (see Q1, Q2).
- **Client-first:** everything that can be computed from what the editor already receives is computed in the
  editor library: camera state and navigation, ray construction, marker picking, selection, Gizmo math. Only
  rendering from the editor camera and intersecting geometry are host services.
- **The editor stays an ordinary client** (Spec 0056 R1): it links Atlantis Connection and the UI library only,
  and includes no Runtime, ECS, RHI, Renderer, Vulkan, Platform or OS header.
- **Zero behaviour change without the editor**, and with the editor in Scene camera mode the frame is Spec
  0056's.

## Non-Goals

- The later editor iterations (E2, E3, E5, E4, E6 in the maintainer's reference order) — not designed or
  scaffolded here.
- Undo/redo, multi-selection, box selection, copy/paste, saving (Spec 0056 Non-Goals, unchanged).
- Rendered selection highlighting (outline, tint): the selection is shown by overlay drawing only (R6).
- Alpha-accurate or pixel-exact picking (Q2), a picking acceleration structure beyond what the latency budget
  needs, picking through the Remote transport or the CLI.
- Editor-camera persistence across runs, camera bookmarks, orthographic views, multiple Viewports.
- Gizmo plane handles, a combined "universal" Gizmo, pivot modes other than the entity's origin, configurable
  snap increments.
- Android hosting (Spec 0056: Android builds the editor library and hosts none).

## Requirements

### Functional

- **R1 — Editor camera (client state).** The editor library holds a UI-free camera model: eye position, yaw,
  pitch (clamped to ±89°, no roll — the look-at convention above), a pivot distance, a fly speed, and a lens
  (vertical fov, near, far). There are two Viewport camera modes:
  - **Scene:** the scene's active camera, as Spec 0056.
  - **Editor:** this model.

  The mode is switched from the Viewport toolbar. The editor camera is initialized once from the scene camera's
  pose and lens, which the host provides (R3). "Align to scene camera" re-initializes it. Its default at attach
  is Q6. It is never written to the World.
- **R2 — Navigation.** While the Viewport is hovered, or a navigation drag that began inside it is still held:

  | Input | Editor camera |
  |---|---|
  | Right button drag | Look: yaw/pitch about the eye. |
  | Right button held + W / A / S / D, Q / E | Fly forward / left / back / right, down / up (world Y). Shift ×4. |
  | Right button held + wheel | Fly speed ×1.25 per notch up, ÷1.25 down. |
  | Alt + left button drag | Orbit about the pivot. |
  | Middle button drag | Pan: the point under the pointer at the pivot's depth follows the pointer. |
  | Wheel | Dolly toward the pivot (distance ×0.85 per notch, with a floor). |
  | Alt + right button drag | Continuous dolly. |
  | F | Frame the selection (R6). |

  Motion is frame-rate independent (`FrameContext::deltaSeconds`). Orbit, pan and dolly scale with the pivot
  distance. Navigation input is ignored while a text field has keyboard focus. Navigating in Scene mode
  switches to Editor mode, starting from the scene camera's pose. Navigation input is never interpreted as a
  pick (R4) or a Gizmo drag.
- **R3 — The Viewport renders from the editor camera** (Q1). In Editor mode the host draws the Viewport with the
  editor camera's world matrix and lens in place of the active camera's. Exposure, fog and bloom remain the
  active camera's `Camera` component values, and the aspect remains the Viewport's. The World is untouched.
  The host gives the editor the scene camera's pose and lens each frame. In Scene mode the frame equals Spec
  0056's. What `captureFrameData()` / `captureImage()` report under an editor camera is Q3.
- **R4 — Picking.** A left click in the Viewport selects: a press and release with less than 4 px of movement,
  no Alt, not on a Gizmo handle. Candidates are considered in this order:
  1. a Gizmo handle (unchanged; it starts a drag, never a pick);
  2. a marker (R5) within 10 px of the pointer, the nearest to the camera first;
  3. geometry: the host's nearest ray hit (Q2) against every entity with `Renderable` and `WorldMatrix`, using
     the current `WorldMatrix` (edits since load are honoured), both triangle faces, alpha masks ignored.

  A hit selects that entity. A click that hits nothing clears the selection. The editor builds the ray from the
  camera that drew the image the user clicked on. The host answers a geometry pick by the next editor frame. An
  answer that arrives after a newer selection change, or after a newer pick request, is discarded.
- **R5 — Markers for entities without geometry.** Entities with a `Light` or a `Camera` component, a
  `WorldMatrix` and no `Renderable` are drawn as small screen-space markers at their world translation, with a
  per-kind glyph. They can be picked (R4) and hidden from the toolbar. The editor reads their positions through
  the connection; this is client-only.
- **R6 — Selection sync.** `HierarchyModel::selection()` remains the one selection. A Viewport pick, a
  Hierarchy click, Esc (clear) and the destruction of the selected entity all change it there. The Inspector
  and Gizmo follow it as in Spec 0056. Beyond that:
  - **Hierarchy:** a selection made outside the Hierarchy scrolls its row into view. If the text filter hides
    the row, the panel says so and offers to clear the filter.
  - **Viewport:** the selection is shown by its Gizmo, its marker highlighted (R5) and, for an entity with
    `Renderable`, a wire bounds box: the mesh's local bounds from the host (Q5), transformed by the entity's
    `WorldMatrix`, drawn by the editor.
  - **F (frame):** moves the pivot to the bounds' centre (or the `WorldMatrix` translation) and sets the
    distance so the bounds' sphere fits the vertical fov.
- **R7 — Gizmo modes.** Translate / Rotate / Scale stay as they are (toolbar, W / E / R when the right button is
  not held). Added:
  - **Space**, World or Local (toolbar; the X key). In Local, translate and rotate use the entity's own axes.
    Scale is always local, as in Spec 0056: a world-axis scale of a rotated entity would shear it.
  - **Snapping** while Ctrl is held during a drag: translate in 0.5-unit steps, rotate in 15° steps, scale in
    0.1 steps of the factor, each measured from the drag's start.

  Writes are unchanged: at most one transaction of `column0`–`column3` per editor frame, plus the final value on
  release (G-a).
- **R8 — The World surface is unchanged.** No command, event, query or component is added. `RuntimeConnection`,
  `RuntimeControl`, `atlantis.remote/1` and their C# counterpart are untouched. The editor's link list and
  include rules are those of Spec 0056 R1 (boundary-scanned).
- **R9 — No behaviour change without the editor** (Spec 0056 R7 extended). Without `--editor`, the frame, every
  test, every golden and the memory Runtime holds are unchanged. With the editor in Scene mode, the frame is
  Spec 0056's.

### Non-functional

- **Latency:** navigation and Gizmo feedback are visible on the next frame. A geometry pick is answered by the
  next editor frame. On Bistro, in Release, one pick's host time is at most **50 ms** worst case, measured and
  reported. Like every performance number it is not a test verdict (Spec 0059). Exceeding it reopens the
  acceleration question (Plan J3).
- **Memory:** the host's pick geometry exists only while an editor is hosted (Q4); on Bistro about 42 MB plus
  per-mesh bounds.
- **Threading:** one frame thread (ADR-0004); every new host service is called between frames.
- **Dependencies:** none new.
- **Portability:** Windows hosts the editor; the editor library still builds for Android (`assembleDebug`).
- **Performance contract:** `runtime_application.cpp` and `frame_capture.cpp` are on the gate's applicability
  list. The implementation PR runs `atlantis_runtime --gate --json` and reports the verdict (AGENTS.md).

## Proposed Design

Under the recommended options:

```
Editor::frame(input, FrameContext)                         (editor library, client)
  in : sceneCamera {worldMatrix, fov, near, far}            ← host, from the active camera
       pickAnswer  {requestId, hit?: {guid, distance}}      ← host, answers last frame's request
       selectionBounds {guid, localMin, localMax}?          ← host, mesh bounds of the last asked entity
  ViewportCameraModel  ← input (R2)                         client: pose, lens, navigation, matrices
  SelectionModel/view  ← click → marker test (client) | ray → pick request
  Gizmo                ← space, snap (R7)
  out: drawList, viewportSize                               (as Spec 0056)
       viewportCamera {worldMatrix, fov, near, far}?        → host renders the Viewport with it (R3)
       pickRequest {requestId, ray origin, direction}?      → host intersects it after this frame (R4)
       boundsSubject guid?                                  → host looks up its mesh bounds (R6)

Host (atlantis_runtime --editor; EditorAttachment + RuntimeApplication)
  EditorAttachment::update(): fills FrameContext, runs Editor::frame(), then answers the
      pick request and the bounds lookup through RuntimeApplication (between frames)
  runFrame(): camera = overlay's viewportCamera() if any, else the active camera   (one named place)
  pick geometry: positions, indices and local bounds per mesh, retained at scene load under --editor (Q4);
      the ray is tested against every Renderable instance's current WorldMatrix (world-space bounds reject,
      then triangles)
```

- **The editor's matrices.** For its own overlay (Gizmo, markers, the bounds box) and for pick rays, the editor
  computes view and projection from a pose and lens with the host's convention: look-at with world-up +Y, the
  same projection. That keeps the overlay in step with the image drawn in the same frame. Copying a convention
  is a maintenance hazard, so a cross-check test pins the editor's matrices to `extractCameraMatrices()`'s for
  the same inputs (Plan M1). The host stays the authority: it receives a pose and lens, never matrices.
- **The host interface stays plain values, request/response, one frame deferred.** No callback from the editor
  into the host, no host type in an editor header, no editor type in a Runtime header (ADR-0108 D2). The
  editor's public header changes: `FrameContext` gains the inputs above. `FrameContext::camera`, which only the
  Gizmo used, is replaced by `sceneCamera` (the Plan lists the touched call sites).
- **`FrameOverlay`** (Runtime's public header) gains one query, the Viewport camera override, defaulting to
  none. The picking and bounds services are public `RuntimeApplication` methods called by the executable-private
  `EditorAttachment` between frames, as `captureFrameData()` is today.

## Architectural Impact

Yes. One ADR, drafted alongside:

- **[ADR-0123](../adr/0123-editor-viewport-camera-and-picking-host-services.md)** (`Accepted` 2026-10-19):
  - D1: the editor camera is client state; the host renders the Viewport from a pose and lens the editor
    supplies. This supersedes ADR-0108 D4's "the camera is the scene's active camera".
  - D2: picking is a host service over Runtime's own CPU copy of mesh geometry, reached through the editor–host
    interface. It is not a World verb and not a connection call.
  - D3: the host interface additions are plain values, request/response, deferred by one frame.
  - D4: no World-surface change.

**Expected code impact** under the recommendations (the Plan's file list is binding):

- **Editor** (`src/editor/`): a camera model, picking/marker and selection-sync logic, the Gizmo's space and
  snapping, view changes (navigation input, toolbar, Hierarchy scroll-to, overlay drawing), `editor.h`'s
  `FrameContext` and the editor's new outputs.
- **Runtime:**
  - `frame_overlay.h`: the camera override query;
  - `runtime_application.{h,cpp}`: one named place in `runFrame()`, the scene-camera, pick and bounds services,
    the pick geometry's ownership;
  - `frame_capture.cpp`: as Q3 rules;
  - `scene_load.{h,cpp}`: retain the pick geometry when asked (Q4);
  - a new Runtime-private picking unit (ray–triangle, bounds);
  - `bootstrap_config.h`: the retention switch;
  - `editor_attachment.{h,cpp}` and `main.cpp` (executable-private): the wiring.
- **Docs:** AGENTS.md (the Editor paragraph's host-interface sentence, the `runFrame()` guard sentence) and
  `module_boundaries.md`, with the implementation PR (the Plan 0056 J10 split).
- **Unchanged:** World, ECS, `RuntimeWorldAccess`, Connection, Remote, CLI, Gameplay SDK, C# SDK, Platform, RHI,
  Vulkan Backend, Renderer, Render Graph, shaders, the scene and asset formats, goldens.

## Alternatives Considered

The open questions below compare the options. Rejected across them:

- **Anything that adds a World verb** (a camera-selection command, a raycast query): it widens a closed surface
  for one client's convenience, and the needs are met without it (Q1 C-c, Q2 K-d).
- **The editor reading asset files for geometry:** a second data path beside the connection, which Spec 0056
  ruling Q6 already declined for the catalog.

## Testing & Verification Plan

Mapped to requirements; the Plan makes each concrete.

- **Editor model tests** (GPU-free, over an InProcess connection on the editor fixture scene):
  - camera: init from a pose round-trips; look, orbit (pivot distance preserved), pan (the point under the
    pointer stays under it), dolly (floor respected), fly (distance = speed × delta), pitch clamp, F framing a
    sphere into the fov (R1, R2, R6);
  - the ray from a pixel passes through the point that pixel's projection came from (R4);
  - marker picking order and radius (R5);
  - selection sync: a pick answer selects; a stale answer is dropped; a miss clears; destroying the selection
    clears it; Inspector and Gizmo subjects follow (R4, R6);
  - Gizmo: Local translate/rotate along the entity's axes, World unchanged from Spec 0056, snapping steps, still
    one transaction per frame (R7).
- **Convention cross-check:** the editor's view/projection equal `extractCameraMatrices()`'s within 1e-6 for a
  sweep of poses, lenses and aspects (R3).
- **UI tests** (ImGui headless, scripted input): right-drag looks, Alt+left-drag orbits, a click issues a pick
  request while a drag of 4 px or more does not, a handle press drags instead of picking, Esc clears, the
  Hierarchy scrolls to an external selection, X toggles space, Ctrl snaps (R2, R4, R6, R7).
- **Runtime host tests** (GPU-free): ray–triangle nearest hit, misses, back faces, instanced meshes, a
  `WorldMatrix` edited after load picked at its new place, bounds of a selected entity, no pick geometry without
  the retention switch (R4, R6, R9).
- **GPU tests** (fatal VVL):
  - with an overlay supplying a camera, the frame's camera block equals the override's extraction and the
    Viewport image differs from the scene camera's;
  - with the override equal to the scene camera, or absent, the image is byte-identical to Spec 0056's;
  - captures behave as Q3 rules (R3, R9).
- **North star** (automated, in process, fatal VVL): the default scene, scripted as in Summary. The click lands
  on a known cube's projected centre, and the selected GUID, the Inspector subject, the Gizmo and the
  transaction's columns are exact. **Bistro, content-gated:** a click at a fixed pixel of the initial view picks
  an entity (it varies only if the content does), and the worst-case pick time is reported (Non-functional).
- **Regression (R8, R9):** Debug + Release full suites; every golden byte-identical, none re-captured; the editor
  boundary scan unchanged; the `runFrame()` diff guard (only the enumerated named lines); `assembleDebug`; the
  performance gate's verdict reported.
- **Interaction feel:** a human acceptance run on the default scene and Bistro (Plan M7).

## Risks & Open Questions

Risks:

- **The convention copy** (Proposed Design): mitigated by the cross-check test, which fails when either side
  changes.
- **CPU picking disagrees with the image where alpha masks cut geometry** (Bistro foliage): a click on a cut-out
  hole selects the leaf card. This is accepted for E1 and stated in R4; Q2 K-b is the remedy if review wants
  pixel-exactness.
- **Editor-hosted memory** (+42 MB on Bistro), only under `--editor`.
- **A second camera source in `runFrame()`.** Kept to one named place, guarded like Spec 0056's four.

Open questions — all six ruled by the owner (slmao, 2026-10-19, review of
[PR #270](https://github.com/slmao/Atlantis/pull/270)), each as its recommendation; rulings recorded in Plan 0062:

- **Q1 — How does the editor camera reach the frame?** (ADR-0123 D1)
  - **(C-a) Client state, a host render override** — recommended. The editor owns the pose and lens and hands
    them to its host as a plain value. The host substitutes them for the active camera at the one named place.
    - No World change, and no verb.
    - The game camera keeps working for every other client: a gameplay client that moves the camera entity
      still sees its own writes.
    - Cost: one more host-interface value, and `runFrame()` gains one named place.
  - **(C-b) Write the active camera entity's `WorldMatrix` through `SetProperty`** — pure existing verbs.
    - Navigation becomes World edits: every client sees the game camera fly, and gameplay that drives the
      camera fights the editor.
    - Restoring the game view on exit is the editor's burden.
    - The client also does not know which entity is the active camera; guessing "the one entity with a
      `Camera`" fails with two cameras.
  - **(C-c) A dedicated editor-camera entity** (`CreateEntity` + `AddComponent` with existing verbs). Making it
    the one the frame draws needs a new command that sets `BakedScene::activeCamera`, a sixth verb. It would
    also put an editor artefact into the World every client lists.
  - **Why C-a:** the camera is a view of the World, not part of it. C-a keeps the World and its verbs exactly
    as they are, and its one host-side change is a value the host already computes for itself.

- **Q2 — How is geometry picked?** (ADR-0123 D2)
  - **(K-a) Host CPU ray-cast** — recommended. Runtime keeps a CPU copy of each mesh's positions, indices and
    local bounds (Q4). The editor sends a world-space ray. The host tests every `Renderable` instance:
    world-space bounds first, then its triangles. It returns the nearest hit's entity GUID and distance.
    - No Renderer, RHI, shader or frame-path change.
    - Deterministic and testable without a GPU; works while paused; honours live `WorldMatrix` edits.
    - Cost: memory, and alpha masks ignored.
  - **(K-b) A GPU ID pass.** Render entity indices into an offscreen target on demand and read one texel back
    (the capture path, ADR-0040). It is pixel-exact, and alpha masks too if its shader tests them.
    - Cost: a new Renderer pass and shader pair, a pipeline per vertex layout, and an index→entity table in
      Runtime; a GPU stall per click; GPU-only tests.
    - The larger lower-layer change for an editor convenience.
  - **(K-c) Client-only:** pick by entities' projected origins, or by bounds the client cannot obtain. On Bistro
    every mesh has its own translation, so origins are spread out, but large meshes overlap small ones and the
    nearest origin is often not what was clicked. Markers (R5) use this rule where it is exact enough: lights
    and cameras.
  - **(K-d) A new `RuntimeConnection` query (`raycast`).** Remote clients and agents could pick too. But it is
    a new verb on a closed surface (Spec 0052), a protocol change (an ADR superseding ADR-0106) and a C# SDK
    change. It also answers from render-side geometry, which is not World data.
  - **Why K-a:** it meets R4 with no change below Runtime and no verb. K-d remains the route if a remote editor
    or an agent workflow needs picking; that would be its own Spec.

- **Q3 — What do `captureFrameData()` / `captureImage()` report under an editor camera?** This matters only
  with `--editor --listen` together, where a remote client can ask.
  - **(F-a) The drawn camera** — recommended. Spec 0055 C1 defines frame data as the frame's own extraction
    output, and captures re-render "the world as the last frame drew it"; both keep that meaning.
  - **(F-b) Always the scene camera.** Agents would always see the game view. But frame data would then
    describe a camera the frame did not draw, and a capture would not match the Viewport.

- **Q4 — When does the host hold pick geometry?**
  - **(G-a) Retained at scene load when the executable will host an editor** (a bootstrap switch that only
    `--editor` sets) — recommended. Zero cost otherwise (R9), and the first click is as fast as any.
  - **(G-b) Reloaded lazily from the catalog on the first pick.** No switch, but the first click on Bistro
    stalls on reading and decoding every mesh again, and Runtime must keep the catalog resolution around.
  - **(G-c) Always retained.** Simplest, but it costs every non-editor run about 42 MB on Bistro, against R9.

- **Q5 — The selection's bounds box** (R6).
  - **(B-a) Included:** a host lookup of the selected entity's mesh local bounds, transformed by the editor —
    recommended. It is the only Viewport indication of a selection whose origin lies far from its geometry, and
    it gives F a size to frame.
  - **(B-b) Deferred:** the selection is shown by the Gizmo and markers only, and F keeps the current distance.

- **Q6 — The Viewport camera mode at attach.**
  - **(D-a) Editor, initialized from the scene camera** — recommended. The first image is Spec 0056's, and
    navigation works at once.
  - **(D-b) Scene**, as Spec 0056. The user switches before navigating, though R2's "navigating switches to
    Editor" makes the difference small.

**No new verb is proposed.** The verb-based options (Q1 C-c, Q2 K-d) are recorded above as rejected
alternatives, not as proposals.

## Out of Scope / Future Work

- Everything under Non-Goals; the later iterations E2–E6 per the maintainer's reference order.
- Picking for remote clients and agents (Q2 K-d), and with it a remote editor's camera (Spec 0056 Q2 (c)).
- Alpha-accurate or pixel-exact picking (Q2 K-b) if the CPU result proves inadequate.
- A rendered selection outline (a Renderer feature).
