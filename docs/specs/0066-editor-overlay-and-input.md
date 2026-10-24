# Spec: Viewport Overlay and Editor Input over the Wire (Spec 0064 Phase 2, step 1)

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-24
- **Related Plan(s):** [Plan 0066](../plans/0066-editor-overlay-and-input.md) (`Approved`; Joint Human Review
  2026-10-24 recorded in its header). As with Specs 0062–0065, the Plan was drafted together with this Spec at the
  maintainer's direction.
- **Approval:** slmao, 2026-10-24 — owner rulings Q1–Q6, each as its recommendation (Q1 (a); Q2 P-a + L-a + G-a + S-a + O-a; Q3 I-1; Q4 H-a; Q5 as confirmed; Q6's budgets), recorded once in
  [Plan 0066](../plans/0066-editor-overlay-and-input.md). The text was reviewed in this Spec's own branch PR, [PR #286](https://github.com/slmao/Atlantis/pull/286)
  (revised after review, merged at `2e22e1a`); the rulings were given after that merge and are recorded by their own
  docs PR. The open-question text below keeps the options as the record of what was weighed.
- **Related ADR(s):**
  - [ADR-0132](../adr/0132-viewport-overlay-surface.md) (`Accepted` 2026-10-24): how a client draws into the Viewport — a
    per-client Viewport overlay (a closed set of world-space primitives and a selection set) carried by a new
    `RuntimeControl` call and drawn by the Renderer after the scene, with a selection outline the Renderer draws
    from the selected entities' meshes (Q1, Q2, Q4).
  - Cited, unchanged: [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) (no World verb is added),
    [ADR-0124](../adr/0124-editor-transaction-and-client-side-undo.md) (the Gizmo's preview and release contract,
    Q5), [ADR-0125](../adr/0125-out-of-process-editor-platform-and-uniform-client-interface.md) (D2: one interface
    for every client), [ADR-0126](../adr/0126-viewport-embedding-and-input-routing.md) (V-a + R-a, adopted; D4/D5
    are where Q3's outcome would land), [ADR-0131](../adr/0131-authoring-apply-and-runtime-scene-reload.md) (D7:
    runtime-side edits are overwritten by an apply).
  - Extended or superseded in part, each named in ADR-0132:
    [ADR-0108](../adr/0108-editor-host-and-viewport-composition.md) D3 (the Renderer's overlay input),
    [ADR-0128](../adr/0128-remote-viewport-camera-and-raycast.md) and
    [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md) D3 (the control surface's method list).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Spec 0064's Phase 2 is "an editor games are developed in". Two things it needs are on the engine side, not in the
editor: a way for an **out-of-process editor to draw into the Viewport** — a Gizmo, a selection outline, debug
shapes — and an **input path whose stalls do not ruin a drag**. Avalonia cannot paint over the Runtime's child
window, and nothing reaches the Renderer's overlay pass from another process. This Spec recommends a **Viewport
overlay**: each client sends a small, closed set of world-space primitives and a selection set by `EntityGuid`
through one more `RuntimeControl` call; the Renderer draws them after the scene, and draws a **selection outline**
from the selected entities' meshes. Like `setViewportCamera`, it is on the shared surface — the wire, the CLI and
the C# SDK — so the CLI and agents can highlight an entity too. A frame capture, the agents' and CI's evaluation
image, shows no overlay unless its request asks, and then only the requesting client's own. For input, it recommends **measuring first**: an
investigative first milestone measures a drag end to end on Bistro against budgets this Spec states, and names the
remedies the owner rules between if the budgets fail. Atlantis Editor App uses the overlay minimally (an outline
and a Gizmo for the picked entity; no editing). No World verb is added.

## Motivation / Problem Statement

### Phase 2's split (background)

Spec 0064 Phase 1 adopted the platform (V-a + R-a, J7 checkpoint, 2026-10-21); Spec 0065 landed E3's data layer.
Phase 2 is split into Specs, each on the shared surface:

1. **This Spec:** drawing into the Viewport from any client, and the input path's stalls.
2. **The E3 authoring editor in Atlantis Editor App:** E1/E2's models ported to C#, editing on two documents (the
   authoring document of Spec 0065 and the Runtime World), structural undo, apply / save / play UI.
3. **Node names in the scene grammar:** a small Asset System Spec that can run in parallel.
4. **Later:** content browsing (needs a catalog query on the wire), incremental validation (Plan 0065 J9), live
   projection of document edits (Spec 0065 Out of Scope).

As Spec 0065 preceded the editor UI that uses it, this Spec precedes the Gizmo and selection UI that need it.

### What exists, and what this Spec builds on without reopening

- **The in-process overlay** (Spec 0056, ADR-0108 D3): Renderer's overlay pass draws a Renderer-owned `UiDrawList`
  — screen-space, textured, vertex-coloured, scissored triangles, no depth — from the in-process host
  (`ui_overlay.h`). It runs only with `--editor`, where the scene is drawn into an offscreen Viewport target that the
  pass samples. The ImGui editor draws its Gizmo, markers and selection bounds into that list in screen space.
- **The control surface** (Spec 0055, ADR-0106; Spec 0064, ADR-0128): `RuntimeControl` carries status,
  pause/resume/step, diagnostics, `setViewportCamera` (one camera per Runtime, cleared when its client disconnects)
  and `raycast`. ADR-0125 D2: every editor capability is a call of the shared surface, added for the wire, the CLI
  and the SDKs in the same Spec; there is no editor-only channel.
- **Embedding** (ADR-0126): the Runtime's window is a Win32 child of a window in the editor (V-a), whose pointer,
  wheel and key messages it re-posts to the parent (R-a). Phase 1 measured camera latency at 1.5–2 Runtime frames and
  sporadic 100–600 ms editor-UI stalls at drag transitions, attributed to the input-queue attachment Spec 0064
  named. ADR-0126 D4 keeps R-b — `control.drainInput`, the Runtime's own input events drained by a client over the
  wire — as the fallback that "needs its own Spec"; D5 keeps V-a2, the Runtime's own top-level window owned by the
  editor's, as the embedding fallback.
- **The Gizmo's contract** (Spec 0056, Spec 0062, Spec 0063, ADR-0124): E1's camera, picking and Gizmo models are
  client-side (`viewport_picking.h`, `gizmo.h`); a drag previews by writing `WorldMatrix` through
  `RuntimeConnection` once per editor frame and commits one transaction on release, which alone is recorded.
- **Two documents** (Spec 0051, Spec 0065): the Runtime World carries no hierarchy and is a bake product; the
  authoring document carries the hierarchy, and an apply replaces runtime-side edits (ADR-0131 D7).

### Facts found while drafting

1. **Nothing reaches the Renderer's overlay from another process,** and the overlay is screen-space: its pipeline
   has no camera uniform and no depth attachment. A world-space overlay drawn by the Runtime is new Renderer work.
2. **Avalonia cannot draw over the Viewport.** The Runtime's child window sits in a `NativeControlHost`; Avalonia's
   own drawing is clipped around it (ADR-0126's "airspace" consequence). An editor overlay must be drawn by the
   Runtime, or by a separate transparent window kept above the child.
3. **A client cannot outline a selection itself.** It has no mesh: ADR-0123 D2's bounds service
   (`renderableLocalBounds()`) is in process only, and the wire carries only `raycast`. A selection outline needs
   the Runtime.
4. **R-b would not remove the stalls.** The input queues are attached because the Runtime's window is a child of a
   window in another process (Win32 attaches the two threads' queues implicitly), whoever then reads the input.
   R-b changes who reads the input, not the attachment; it adds a frame-boundary poll of latency. What removes the
   coupling is either the Runtime keeping its own message queue serviced while a long frame runs, or no
   cross-process parent at all (V-a2).
5. **The frame path is guarded.** `runFrame()` changes are enumerated line by line in AGENTS.md, and the performance
   gate applies to `src/runtime/**` and `src/renderer/**`; an empty overlay must leave the frame — its passes, its
   counters, every golden — exactly as it is.

### Why now

The editor UI Spec cannot specify a Gizmo, a selection highlight or drag behaviour while it is unknown how they
reach the Viewport and whether the drag path holds up. Both are engine-side questions with a shared-surface answer;
deciding them first keeps the next Spec about the editor.

## Goals

- **Any client draws into the Viewport:** a Gizmo, a selection outline and debug shapes, from the editor, the CLI,
  scripts and agents alike (ADR-0125 D2).
- **The Renderer draws them,** after the scene, depth-aware where asked, without the Renderer learning of the wire
  or of an editor.
- **A drag is measured end to end** on Bistro, and its stalls are either within a stated budget or have a named,
  ruled remedy.
- **Atlantis Editor App shows** the picked entity's outline and a Gizmo through the overlay.
- **Nothing changes for a frame with no overlay:** the same passes, counters and pixels.

## Non-Goals

- **Editing in Atlantis Editor App:** no Gizmo drag that edits, no Inspector, Hierarchy, undo or apply/save UI. The
  E3 editor is the next Spec; M1's measurement drag (Plan 0066) is measurement-only.
- **The two-document editing model** (which document a Gizmo edits, and how runtime-side previews and authoring
  commits combine): the next Spec's (Q5).
- **Changes to the ImGui editor** (frozen at E2, ADR-0125 D4): it keeps its own screen-space overlay; it may be
  given the Viewport overlay later as a shared-surface change, not here.
- **Any World verb, event, component or error** (ADR-0103); any change to `RuntimeConnection` or
  `AuthoringConnection`.
- **Text, icons or textures in the overlay;** screen-space UI drawn by the Runtime for a remote client.
- **Frame streaming** (Spec 0064 V-b) and non-Windows embedding.

## Requirements

### Functional

- **R1 — The Viewport overlay value.** A client's overlay is a value: a selection set (`EntityGuid`s) and groups of
  primitives from a closed set — line segments (a pixel width), filled triangles, and points (a pixel size) — each
  with a straight-alpha sRGB colour and a depth mode (depth-tested against the scene, or drawn on top). A group is
  either **world** (its primitives' points are world positions) or **screen-constant** (an anchor point in world
  space and a pixel scale: its points are offsets from the anchor, one unit drawn as that many pixels at the anchor's
  depth, from the frame's own camera — Q2 S-a). The set is versioned with the wire; it carries no text, texture or
  screen-space-positioned primitive.
- **R2 — The control call.** `RuntimeControl::setViewportOverlay(optional<overlay>)` replaces the calling client's
  overlay whole from the next frame on; `none` clears it. Each client has its own overlay; the frame draws the
  union of all clients'; a client's overlay is removed when its connection closes (ADR-0128's lifetime rule). An
  overlay over the stated limits is refused (`InvalidRequest`) and changes nothing. It is not World state: no event
  reports it, and it does not survive the Runtime.
- **R3 — Drawing.** After the scene, in the frame's scene target (the swapchain image, or the Viewport target under
  `--editor`), from the camera the frame drew (ADR-0123 D1, ADR-0128 D1): first the selection outline, then the
  primitives, depth-tested ones against the frame's depth. With no overlay from any client, the frame records no
  overlay pass and is unchanged.
- **R4 — The selection outline.** Each selected entity that has a `Renderable` is outlined from its mesh at its
  current `WorldMatrix`, in one fixed colour, visible through occluders; an entity without a `Renderable`, unknown or
  destroyed, is skipped without error (selections go stale between frames).
- **R5 — Captures.** A captured frame (`runtime step --capture`) shows **no overlay** unless its request asks for
  one (`withOverlay`); it then shows **only the requesting client's own overlay** (its selection outline and
  primitives as they were when the step's last frame ran), never another client's. The window still shows every
  client's overlay; frame data (`FrameData`) is unchanged either way. A capture is the evaluation image of agents and
  CI (Spec 0055): an agent capturing while the editor is attached must not get the editor's Gizmo, and an agent's
  debug lines must not land in a CI comparison.
- **R6 — Every client.** The call is carried by `atlantis.remote/1` (`control.setViewportOverlay`, with
  `remote_protocol.md`), the CLI (`runtime overlay set <file|-> | select <guid>... | clear`), Remote's C++ client and
  the C# SDK, with conformance vectors for every new value. The capture request's `withOverlay` (R5) is carried by
  `control.step`'s request (`{"frames":…,"image":…,"withOverlay":bool}`, absent read as `false`), the CLI
  (`runtime step --capture [<path>] --with-overlay`) and the C# `StepRequest`. The C++ Gameplay SDK exposes no control calls and is
  unchanged (as for ADR-0128).
- **R7 — The editor's minimal use.** Atlantis Editor App outlines the entity a click picked and draws a translate
  Gizmo (three axis segments with cone tips, on top) at its `WorldMatrix`, and clears both when nothing is picked.
  It hit-tests nothing yet and edits nothing (Q4, Q5).
- **R8 — The input investigation.** Before the overlay is built, a measurement on Bistro and on the default scene
  reports a drag's end-to-end latency and the editor UI's stalls under R-a against the budgets of Q6, with a verdict
  table the owner rules on (Plan 0066 M1).

### Non-functional

- **Performance:** an empty overlay adds no pass and no counter change (the gate's counters stay exact). A Gizmo plus
  one outline on Bistro is a reported cost (Q6), not a test verdict.
- **Memory:** per-client overlays are bounded (a primitive and a selection limit, Plan 0066 J6); per-frame overlay
  vertex data is the Runtime's, sized to the union.
- **Portability:** the Renderer pass and its shaders build on Android; nothing feeds them there (Android hosts no
  Remote server). Lines are drawn as screen-space-expanded triangles, not with a wide-lines device feature.
- **Other:** the `runFrame()` guard gains one named place, enumerated in the implementing PR.

## Proposed Design

```
editor / CLI / agent ──control.setViewportOverlay──► RemoteServer (per-client overlays; removed on disconnect)
                                                        │ union
                                                        ▼
                                RuntimeControlHost ─► RuntimeApplication: GUIDs → selected draw items
                                                        │ (named place after drawFrame())
                                                        ▼
                         Renderer: outline pass (selected meshes → mask → edge) → primitive pass (world space,
                         depth-tested or on top) into the scene target, then present / the UI pass under --editor
```

- **Value and call** (R1, R2): Atlantis Connection gains the overlay value types beside `ViewportCamera` and
  `setViewportOverlay` on `RuntimeControl`; Remote carries it as `control.setViewportOverlay` and keeps per-client
  overlays, as it keeps the camera's owner; the CLI and the C# SDK mirror it. A step request with `withOverlay` is
  resolved by the transport that holds the per-client overlays (Remote's server) to the requester's own overlay,
  which the capture alone draws (R5).
- **Renderer** (R3, R4): a Renderer-owned `ViewportOverlayDrawList` — vertices of segments, triangles and points
  with colour, depth mode and, for a screen-constant group, its anchor and pixel scale (applied in the vertex shader
  from the frame's camera), and the selected entities' draw items — drawn by new RenderGraph passes after
  `drawFrame()`. The Renderer knows neither the wire nor an editor: the Runtime translates the control value into
  this list, as the `--editor` host translates ImGui's (ADR-0108 D3).
- **Runtime** (R3–R5): one named place in `runFrame()` after the scene: with a non-empty union, resolve the selection
  through the bake's GUID map to draw items, build the list, record the passes; a capture re-renders with no
  overlay, or with the requester's own when asked (R5).
- **Editor App** (R7): builds the Gizmo as one screen-constant group anchored at the picked entity's
  `WorldMatrix` translation (read through `RuntimeConnection`) and sends it with the selection after each pick; a
  camera change alone needs no resend (Q2 S-a).
- **Input** (R8): no engine change in this Spec unless the M1 verdict calls for one; the remedies are named in Q3.

## Architectural Impact

Yes:

- **A new `RuntimeControl` call and wire method** — the shared surface's method list
  ([ADR-0132](../adr/0132-viewport-overlay-surface.md), superseding ADR-0106 D3's list in part, as ADR-0128 did).
- **The Renderer gains a second overlay input** beside `UiDrawList`: world-space, depth-aware primitives and a
  selection outline (ADR-0132, extending ADR-0108 D3); new shaders. **The outline and primitive passes are
  RenderGraph passes** (AGENTS.md: the Render Graph is the mandatory path for GPU work), recorded by the Renderer
  after the scene's passes; no command is recorded outside the graph.
- **`runFrame()` gains one named place** (the performance gate applies).
- **Conditionally, input:** if M1's verdict calls for keeping the Runtime's queue serviced inside a long frame, that
  is a frame-loop change needing an ADR (amending ADR-0126 or a new one) ruled at M1's checkpoint. That ADR and the
  Plan amendment are reviewed in their own docs PR (as Spec 0065's Corrections were, PRs #281–#283), never folded
  into the implementation PR, which cites it (Plan 0066 J8). V-a2 is already inside ADR-0126 D5; R-b would supersede
  ADR-0126 D4 and needs its own Spec. Nothing about input is decided by ADR-0132.

No module boundary changes; no new dependency; no World, Asset System or Authoring change.

## Alternatives Considered

The options are weighed in Q1–Q6 below; the main rejected ones are:

- **A transparent top-level window kept by the editor above the Viewport** (Q1 (b)): editor-only (the CLI and
  agents get nothing), tracks the camera one frame behind, and needs its own z-order, DPI and focus handling.
- **Frame streaming** (Q1 (c)): rejected in Phase 1 (Spec 0064 V-b) for cost and GPU-sharing complexity.
- **A Runtime-side Gizmo** (`showGizmo{entity, mode, space}`, Q2): puts the Gizmo's geometry and hit-testing in
  the Runtime, duplicating E1's client-side model.
- **Specifying R-b now** (Q3): it moves input reading, not the queue attachment that stalls the editor.

## Testing & Verification Plan

- **Codec and vectors** (R1, R6): every new value round-trips in C++ and C#; conformance vectors appended.
- **Control and wire** (R2, R5, R6): per-client replacement, the union, removal on disconnect, `none` clearing, the
  limits refused with nothing changed; a capture requested without `withOverlay` while another client's overlay is
  set shows no overlay, and with it shows only the requester's; the CLI's envelopes; the C# session against the
  fixture server.
- **Rendering (GPU, fatal VVL)** (R3–R5): with no overlay, frame data, counters and every existing golden unchanged;
  a segment, a triangle and a point land at the pixels their world positions project to, depth-tested ones hidden
  behind a mesh and on-top ones not; a selected mesh's outline appears around its silhouette and an unknown or
  non-`Renderable` GUID draws nothing; a screen-constant group keeps its pixel size at two camera distances; a
  capture with `withOverlay` shows the requester's overlay under the control camera and under `--editor`, and one
  without shows none.
  Assertions on captured pixels, not new goldens (Plan 0066 J7).
- **Performance gate:** run on the implementing branch with no overlay; verdict reported as measured.
- **Editor App** (R7): its smoke test picks, outlines and shows the Gizmo on the default scene; the owner's run on
  Bistro (Plan 0066 M6).
- **Measurements** (R8, Q6): M1's input investigation and M6's overlay latency and cost, reported against Q6's
  targets, not test verdicts.

## Risks & Open Questions

Risks:

- **The stalls may persist under every cheap remedy.** Then V-a2 (no cross-process parent) is the fallback ADR-0126
  already keeps, at the cost of the editor positioning a top-level window.
- **Outline cost on dense scenes:** a mask pass draws each selected mesh again; selecting many large meshes on Bistro
  costs GPU time (reported in M6).
- **Overlay data per frame:** a careless client could send large debug geometry every frame; the limits bound it,
  and screen-constant groups remove the camera-driven resend (Q2 S-a).
- **A capture that should show the overlay and does not:** captures exclude overlays unless asked (R5); a client that
  wants its own overlay in an image passes `withOverlay`.
- **The ImGui editor and the overlay coexist** under `--editor`: both draw, in different passes (R3's order).

Open questions — all six ruled by the owner (slmao, 2026-10-24), each as its recommendation; rulings recorded in
Plan 0066:

- **Q1 — How does an out-of-process client draw into the Viewport?** (ADR-0132)
  - **(a) A Runtime-drawn Viewport overlay fed by a `RuntimeControl` call** carrying a closed set of world-space
    primitives and a selection set — **recommended**. One more call on the shared surface, so the CLI and agents can
    highlight an entity too; drawn in the frame's own target with its depth and camera, so it never lags the image;
    new Renderer work (a pass and shaders) and one `runFrame()` place.
  - **(b) A transparent, layered top-level window** the editor keeps above the child and draws itself, synchronised
    to the camera it set. No engine change; but editor-only, one frame behind the image (it draws from the camera it
    sent, the image from the frame that used it), with its own z-order, DPI, focus and minimize handling, and no
    depth against the scene.
  - **(c) Frame streaming** (Spec 0064 V-b): the editor composites everything itself. Rejected in Phase 1 for its cost
    and cross-process GPU sharing.
  - **Why (a):** it is the only option every client gets, and the only one that draws in the image's own frame with
    its depth.

- **Q2 — The primitive set and its ownership.** (ADR-0132)
  - **Set:** **(P-a) a closed set — segments with a pixel width, filled triangles, points with a pixel size; colour
    and a depth mode (tested / on top) each; plus a selection set by `EntityGuid`** — **recommended**. Enough for a
    translate/rotate/scale Gizmo (axes, rings, cones, plane quads), boxes, frusta, paths and markers; versioned with
    the wire. **(P-b)** an open mesh upload (vertex buffers, textures): far more surface, little need now.
  - **Lifetime:** **(L-a) per client, replaced whole by each call, removed on disconnect** — **recommended**, as
    ADR-0128's camera; the frame draws the union. Consequence: a one-shot `atlantis runtime overlay …` lasts only
    while its process is connected, so the CLI's call is useful in `repl`, `--exec` and scripts holding a session —
    the same as `runtime camera set` today. **(L-b)** persistent until cleared: survives the client, but a crashed
    editor leaves a stale Gizmo in the Viewport.
  - **Gizmo geometry:** **(G-a) built client-side from primitives** — **recommended**: E1's Gizmo model is
    client-side, so the client already knows every handle's geometry and hit-tests it (Q4). **(G-b)** a Runtime-side
    `showGizmo{entity, mode, space}`: a second Gizmo implementation in the Runtime, and its handles' geometry known
    only to the Runtime.
  - **Screen-constant size:** E1's Gizmo keeps a constant size on screen, so with world-space primitives alone it
    must be rebuilt whenever the camera moves.
    - **(S-a) Screen-constant groups** — **recommended**: a group may carry an anchor point and a pixel scale; its
      points are offsets from the anchor, and the Runtime scales them from the frame's own camera (one unit drawn as
      that many pixels at the anchor's depth). An orbit or dolly then needs no resend — only a change of the
      previewed matrix does, which a drag sends anyway — and the Gizmo is sized from the very camera the frame
      draws, so it never shows one frame at the previous camera's size. The client hit-tests with the same formula
      from the camera it set (Q4). Cost: one more group kind in the value and a scale in the primitive shader.
    - **(S-b) World-space only, resent on every camera change:** no new value; a second message per camera change
      (the editor already sends `setViewportCamera` per pointer move), and the camera and the overlay can land in
      different frames, so the Gizmo's size pops for a frame during an orbit.
    - **Why (S-a):** it removes the resend and the size pop with a small, closed addition.
  - **Selection outline:** **(O-a) Runtime-side, by GUID** — **recommended**: the outline needs the mesh, which only
    the Runtime has (fact 3). **(O-b)** a client-built bounds box: needs the bounds on the wire, and is not an
    outline.
  - **Depth:** each primitive chooses (a Gizmo on top, debug shapes depth-tested); the outline is always visible.

- **Q3 — Input: keep R-a, or change the route now?** (no ADR unless the M1 verdict requires one)
  - **(I-1) Keep R-a and measure a drag end to end first** — **recommended**: an investigative first milestone
    (Plan 0066 M1, as Plan 0064 M6) measures pointer-down → World write applied → frame presented, and the editor
    UI's tick gaps during drags, on Bistro and the default scene, against Q6's budgets; the owner rules on a verdict
    table.
  - **(I-2) R-b, `control.drainInput`** (ADR-0126 D4): the Runtime's own input events drained by the editor over the
    wire. Portable and toolkit-independent, but it keeps the child window and so the queue attachment (fact 4), and
    adds a frame-boundary poll of latency. Worth its own Spec only for a non-Windows editor or if R-a's forwarding
    itself fails.
  - **(I-3) Keep the Runtime's message queue serviced during a long frame:** Platform pumps (peeks and dispatches)
    at named points inside a frame, or on a window thread of its own. Attacks the attachment's symptom directly; the
    first is a frame-loop change, the second changes ADR-0004's threading model; either needs an ADR, ruled at M1's
    checkpoint.
  - **(I-4) V-a2:** the Runtime's own top-level window, owned by the editor's and positioned over the Viewport pane
    (ADR-0126 D5), with input forwarded to the owner — no cross-process parent, so no attachment.
  - **Why (I-1):** Phase 1's stalls were sporadic and at drag transitions; whether they matter for a real drag, and
    which remedy fits, is measurable before anything is changed. If M1 fails the stall ceiling, the remedy order this
    Spec proposes is I-3 (pumping inside the frame) then I-4; I-2 is not a remedy for the stalls.

- **Q4 — Hit-testing the overlay** (grabbing a Gizmo axis).
  - **(H-a) Client-side, against the primitives it sent,** with E1's pixel/ray math — **recommended**: the client
    built them, knows the camera it set, and E1's model does exactly this (`viewport_picking.h`, `gizmo.h`). A
    screen-constant group is hit-tested with the same anchor-depth scale the Runtime applies (S-a).
  - **(H-b) Runtime-side,** a `raycast` against overlay primitives: a round trip per pointer move, and overlay
    geometry interpreted by the Runtime.
  - This Spec decides H-a; the next Spec implements the Gizmo's hit-testing (R7 draws only).

- **Q5 — Where the Gizmo's preview writes.**
  - **Confirmed, unchanged:** across processes the E2 contract holds — a drag's previews are `RuntimeConnection`
    writes of `WorldMatrix`, at most one per editor frame, runtime-side edits that an authoring apply overwrites
    (ADR-0131 D7); the release is the one recorded transaction (ADR-0124). The overlay follows the previews (the
    client re-sends the Gizmo at the preview's matrix).
  - **In this Spec,** only M1's measurement drag writes, to the Runtime World, and records nothing.
  - **Not decided here:** whether the release is recorded on the authoring document, the World, or both, and how a
    preview on the World relates to a node's authoring transform under a parent — the next Spec's two-document model.

- **Q6 — Budgets** (reported targets, measured on the reference machine, Release; like Plan 0065 J3/J9):
  - **Overlay latency** (`setViewportOverlay` sent → first presented frame showing it): median ≤ 2 Runtime frames,
    the camera's latency in Phase 1.
  - **Drag latency** (pointer move → presented frame showing the moved entity): median ≤ 3 Runtime frames, p95 ≤ 4.
  - **Stall ceiling** (the editor UI thread's longest gap between ticks of a 16 ms timer, over ten 3-second drags with
    press and release transitions): ≤ 100 ms on Bistro and on the default scene. Phase 1 observed 100–600 ms.
  - **Overlay cost** (a translate Gizmo and one selected entity's outline on Bistro): GPU ≤ 0.5 ms; with no overlay,
    zero (gate counters exact).

## Out of Scope / Future Work

- The E3 authoring editor (Hierarchy, Inspector, Gizmo editing with hit-testing, two-document undo, apply / save /
  play UI) — the next Spec, using this one's overlay.
- Node names in the scene grammar (a parallel Asset System Spec); content browsing and a catalog query on the wire;
  incremental validation (Plan 0065 J9); live projection (Spec 0065).
- Text, icons and textured primitives in the overlay; Runtime-drawn screen-space UI for remote clients.
- R-b (`control.drainInput`) and non-Windows embedding; frame streaming.
- The ImGui editor adopting the Viewport overlay.
