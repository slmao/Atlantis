# Spec: Minimal Editor

- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-08
- **Related Plan(s):** none yet. Plan drafting awaits this Spec's Approval.
- **Approval:** pending. The maintainer fixed this Spec's position, the four
  v1 features, the layout, the schema-generated Inspector, the Gizmo chain,
  the "ordinary client" rule and the named-only list before drafting
  (2026-10-08, chat). They are recorded under Goals / Non-Goals and are not
  open to review; the open questions below are.
- **Related ADR(s)** (all `Proposed`, drafted alongside):
  - [ADR-0107](../adr/0107-editor-ui-library-selection.md): the editor's UI
    library (Q1);
  - [ADR-0108](../adr/0108-editor-host-and-viewport-composition.md): the
    editor's host, module boundary and Viewport composition (Q2, Q3, Q7);
  - [ADR-0109](../adr/0109-platform-input-events.md): input events from
    Atlantis Platform (Q9, surfaced while drafting).

  They keep [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md)
  and [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md)
  (the editor is a client of their interfaces), and
  [ADR-0103](../adr/0103-runtime-world-operation-boundary.md),
  [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) and
  [ADR-0004](../adr/0004-phase1-threading-baseline.md) unchanged.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

`RuntimeConnection` has one real external consumer, the Agent CLI (Spec
0055). This Spec adds the second: a **minimal editor** — Hierarchy,
Inspector, Viewport and a Transform Gizmo — that is, by construction, one
more ordinary client of `RuntimeConnection` and `RuntimeControl`. It reads
and writes the World only through them; it has no Renderer, ECS or Runtime
privilege. The Inspector is generated from the schema's descriptors, never
hand-written per component. A Gizmo drag becomes a transaction of
`SetProperty` commands. The Viewport shows the Runtime's own rendering.

The hard part is not the panels but where the editor lives, how its UI is
drawn without breaking the module rules (only the Vulkan Backend includes
Vulkan; only Platform owns OS windowing and input; Render Graph is the path
for GPU work), and how the Viewport shows Runtime output without a
privileged path. Those are this Spec's open questions.

**North star:** open the editor on the default scene → select the
Directional light in the Hierarchy → the Inspector shows its fields,
generated from the schema → change `intensity` → the next frame's Viewport
changes. Pause and step work from the editor.

## Motivation / Problem Statement

### What exists

- **The connection** (Spec 0054, ADR-0105): Query / Command / Event /
  Transaction over GUIDs and schema ids, with an InProcess endpoint owned by
  Runtime and pull-only subscriptions.
- **The transport and control** (Spec 0055, ADR-0106): Atlantis Remote
  (loopback JSON-lines) carrying `RuntimeConnection` and `RuntimeControl`;
  pause holding command application in one named line of `runFrame()`;
  step; diagnostics; capture (frame data, and an offscreen re-render read
  back as PNG).
- **The schema** (Spec 0048, ADR-0099): `TypeDescriptor` /
  `FieldDescriptor` / `EnumConstantDescriptor` — every World component's
  fields with their `PrimitiveKind`, nested struct and enum types, and
  `FieldFlags` (`Editable`, `Optional`, `AssetReference`,
  `EntityReference`). Spec 0054's text forms already walk them
  (`text::leavesOf`, `parseValue`, `formatValue`).

### What the editor needs that does not exist

Found in the code while drafting:

- **No input.** Atlantis Platform's events are `WindowResize`,
  `WindowCloseRequested`, `FocusGained/Lost`, `ApplicationPause/Resume`,
  `SurfaceCreated/Destroyed` and `Quit`
  (`src/platform/include/atlantis/platform/platform_event.h`). There is no
  mouse, keyboard or text input. No interactive UI is possible without a
  Platform change (Q9).
- **One window, one swapchain.** Platform creates one window; Runtime's
  `RuntimeApplication` owns the only `Presentation` and draws the scene to
  the whole swapchain image (`runtime_application.cpp`, `drawFrame(…,
  PresentSource, …)`).
- **No sampleable render target.** An `OffscreenTarget`'s colour image is
  created `COLOR_ATTACHMENT | TRANSFER_SRC` (`vulkan_device.cpp:1364`):
  capture reads it back to the CPU. Nothing can sample it as a texture.
  `HdrColorTarget` is sampleable (`COLOR_ATTACHMENT | SAMPLED`,
  `vulkan_device.cpp:1557`) but is the HDR intermediate, before the output
  transform.
- **Runtime's host library is closed.** `atlantis_runtime_host` "exists
  solely for testability … and is not a dependency any other top-level
  module may take" (AGENTS.md, Atlantis Runtime). An editor executable that
  embeds the Runtime would need that rule changed (Q2).
- **No camera for the client.** `RuntimeConnection` exposes the camera
  entity's components but not which entity is the active camera (the bake's
  `BakedScene::activeCamera` is not a component). A Gizmo must project
  handles with the frame's view and projection (Q5).

### Facts the panels must respect

- **The Runtime World has no hierarchy.** Spec 0051 ruling Q2 (H1) resolves
  the hierarchy at bake time; the baked world is flat. A tree Hierarchy has
  nothing to show.
- **`Transform` does not move anything.** Spec 0052 ruling Q8 (T-a):
  `Transform` is readable and writable but does not affect rendering;
  `WorldMatrix` is the render-authoritative placement. Its schema is four
  `Vec4Float32` fields, `column0`–`column3`.
- **Entities have no name** (Spec 0055 ruling Q3, N-a): an entity is its
  GUID and its components.
- **Scale, measured on Bistro** (Release, merged `atlantis` and
  `atlantis_runtime --listen`, 2026-10-08):

  | Measure | Value |
  |---|---|
  | Entities / with `Renderable` / with `Light` | 5969 / 2909 / 60 |
  | `entity list` (every entity with its components) JSON result | 570 KB |
  | `listEntities` on the wire (5969 GUIDs) | about 235 KB, one line (limit 1 MiB) |
  | One-shot `atlantis` invocation, single query | about 0.9–1.3 s (process start, connect, schema) |
  | The same with all 5969 entities' components, pipelined | +0.25 s |
  | Bistro frame time (`runtime step --frames 60`) | about 130 ms (7.6 FPS) |
  | Default-scene frame time | about 16.4 ms (vsync, 60 Hz) |
  | Capture (offscreen render + readback + PNG) | +0.5–0.6 s per frame, both scenes |
  | One capture's PNG | 0.37 MB (default) / 2.6 MB (Bistro, 2374×1320) |

  Bistro's frame time includes its known `checkConformalTransform()` log
  flood (about 2700 error lines per frame), a separate issue.

## Goals

Maintainer-fixed (2026-10-08), not to be relaxed in review:

- **v1 is exactly four things:** Hierarchy, Inspector, Viewport, Transform
  Gizmo.
- **The layout** (the maintainer's sketch):

  ```
  Hierarchy | Viewport
  Content   | Inspector
  ```

- **The Inspector is generated from the schema**: `TypeDescriptor` /
  `FieldDescriptor` → a generic property inspector. No per-component
  hand-written inspector (no `DrawTransformInspector()`). Custom inspectors
  for special components are a later registration point, not v1.
- **The Gizmo chain:** Gizmo → Transaction → `SetProperty`.
- **The Viewport shows the Runtime's rendering** directly.
- **The editor is a first-class ordinary client.** Every World access goes
  through `RuntimeConnection` (and lifecycle through `RuntimeControl`).
  There is no Renderer, ECS or Runtime privileged path; the editor never
  grows into a "big object" with Runtime privileges.
- **Named only, not designed or scaffolded:** see Non-Goals.

Goals of this Spec within those boundaries:

- **The north star** above, automated, plus a Bistro-gated version (Q8).
- **Zero behaviour change without the editor:** every existing test and
  golden unchanged; the frame without an editor attached is unchanged.

## Non-Goals

Maintainer-fixed:

- Terrain Editor, Animation Editor, Material Graph, Visual Scripting,
  Timeline, complex docking.
- Gameplay SDK (0057), C# (0058), Package/Plugin (0059), Headless
  Simulation (0060), Python (0061) — named only; this Spec makes no
  commitment to them.
- Authoring-scene editing and re-bake workflows; Prefab.

Also out of scope here:

- **A tree Hierarchy.** The baked world is flat (0051 H1); a tree belongs to
  a future authoring editing mode.
- **Undo/redo, multi-selection, copy/paste, save.** Nothing the World does
  today persists edits; saving is the authoring workflow's.
- **New World verbs, events or concepts.** The editor uses Spec 0052/0053's
  five commands, five events and transactions.
- **Android editing.** The editor runs where `atlantis_runtime` runs on
  Windows; Android may compile the editor library (Q7) but does not host it.
- **Multi-threading.** ADR-0004 unchanged.

## Requirements

### Functional

- **R1 — An ordinary client.** The editor reaches the World only through a
  `RuntimeConnection` and the Runtime's lifecycle only through a
  `RuntimeControl`. It includes no Runtime, ECS, RHI, Renderer, Vulkan or
  OS header (checked by a boundary scan, as Spec 0054/0055 did).
- **R2 — Hierarchy** (Q4). A flat list of every addressable entity, each
  shown as its GUID and its components, kept current by events; selecting
  one makes it the Inspector's and the Gizmo's subject.
- **R3 — Inspector** (Q5's Transform note). For the selected entity, every
  component's leaf fields, generated from the schema: each `PrimitiveKind`,
  enums from their constants, nested structs as groups, `Optional` fields
  with an absent state, `Editable == false` read-only. An edit is one
  `SetProperty`, its outcome (event or refusal) shown.
- **R4 — Viewport** (Q3). The Runtime's rendered frame, from its active
  camera, inside the editor's layout, updating every frame.
- **R5 — Transform Gizmo** (Q5). Translate / rotate / scale handles on the
  selected entity, writing its `WorldMatrix` as one transaction per update.
- **R6 — Runtime control** (Q8). Play (resume), pause and step map to
  `RuntimeControl`; the status (paused, frame) is shown.
- **R7 — No behaviour change without the editor.** Without the editor
  option, the Runtime's frame, its tests and goldens are unchanged.

### Non-functional

- **Latency:** an edit shows in the Viewport on the next frame (one frame
  of command application, Spec 0052).
- **Scale:** usable on Bistro (5969 entities): no per-frame full listing;
  the Hierarchy updates incrementally.
- **Threading:** one frame thread (ADR-0004).
- **Dependencies:** at most the one UI library Q1 selects, pinned
  (ADR-0006).
- **Portability:** Windows hosts the editor; the editor library builds on
  Android (`assembleDebug`) if Q7 rules so.

## Proposed Design

Under the recommendations below:

```
atlantis_runtime --editor
  ├─ Platform: window, events (+ input events, Q9)
  ├─ RuntimeApplication: world, frame, Presentation
  │    frame (editor attached): apply pending (unless held) → pump → editor UI update
  │      → scene drawn into the Viewport target (offscreen, sampleable)
  │      → editor UI drawn into the swapchain, sampling the Viewport target
  └─ Atlantis Editor (client library): RuntimeConnection + RuntimeControl only
       Hierarchy model ← listEntities + event subscription
       Inspector model ← schema descriptors + getProperty / SetProperty
       Gizmo model     → WorldMatrix decomposition/composition → transaction
       UI (Dear ImGui) → an engine-neutral UI draw list the host renders
```

The editor library never touches a GPU object or an OS handle: it receives
input as plain values and an opaque Viewport texture handle, and returns a
UI draw list. The host (Runtime, the composition root) turns input events
into those values and draws the list through the Renderer.

## Architectural Impact

Yes. Recorded in three ADRs drafted alongside (all `Proposed`):

- **[ADR-0107](../adr/0107-editor-ui-library-selection.md)** — a new
  third-party dependency, the UI library (Q1), with the integration rule that
  keeps it out of the Vulkan Backend's and Platform's territory.
- **[ADR-0108](../adr/0108-editor-host-and-viewport-composition.md)** — the
  editor's host (Q2), its module boundary (Q7), and the Viewport
  composition (Q3): an optional editor frame path in Runtime, a sampleable
  offscreen target in RHI, and a UI overlay pass in Renderer.
- **[ADR-0109](../adr/0109-platform-input-events.md)** — Platform's public
  event set gains input events (Q9).

**Expected code impact** under the recommendations:

- **New:** `src/editor/` (Atlantis Editor, a client library), its tests, the
  UI library's pinned fetch.
- **Changed:**
  - Platform: input events (Windows; Android emits none in v1);
  - RHI + Vulkan Backend: an offscreen colour target that can be sampled;
  - Renderer: a UI overlay pass taking a Renderer-owned draw list type;
  - Runtime: `--editor`, the editor frame path (named places in
    `runFrame()`, the non-editor path guarded as Spec 0055 P7 was);
  - the `atlantis_runtime` executable links Atlantis Editor.
- **Docs:** AGENTS.md module list and `module_boundaries.md` (Editor's
  section; Platform, RHI, Renderer, Runtime lines).
- **Unchanged:** `RuntimeConnection`, `RuntimeControl`, the wire protocol,
  `RuntimeWorldAccess`, the ECS, extraction, the scene formats, goldens.

## Alternatives Considered

The questions below compare their options. Rejected across them:

- **An editor with direct ECS / Renderer access** — the "big object" the
  maintainer excluded; it would make the editor a second Runtime.
- **Hand-written inspectors per component** — maintainer-excluded.

## Testing & Verification Plan

- **Model tests** (no GPU, over an InProcess connection on a baked fixture
  scene, as Spec 0054/0055):
  - the Inspector's generated field set equals the schema's leaves for every
    World component, with kinds, enum constants, nesting, `Optional` and
    read-only flags;
  - an Inspector edit submits exactly one `SetProperty` and shows its event
    or refusal;
  - the Hierarchy's first listing equals `listEntities()`; after creates,
    destroys, adds and removes, its incremental state equals a fresh
    listing;
  - Gizmo math: decomposing then recomposing a `WorldMatrix` is exact for
    translation and within a stated tolerance for rotation/scale; a
    translate/rotate/scale update writes `column0`–`column3` as one
    transaction; drag updates are coalesced to at most one per frame.
- **UI tests** (the UI library run headless, no GPU): scripted input
  selects a Hierarchy row, edits a field, presses pause/step; the expected
  commands and control calls happen.
- **Boundary scan:** Atlantis Editor includes nothing of Runtime, ECS, RHI,
  Renderer, Vulkan, Platform or OS; its link list is exactly what Q7 rules.
- **GPU tests** (fatal VVL): the editor-attached frame renders the Viewport
  and the UI; the RHI sampleable target and the Renderer overlay pass
  have their own tests.
- **The north star** (automated, in process, fatal VVL): default scene,
  select the Directional light `0b2c1db2-…`, the generated Inspector shows
  `Light.intensity = 3`, set 6, the next frame's frame data has 6 exactly
  and the Viewport image differs; pause holds, step applies. **Bistro,
  content-gated:** `6b63b12c-…` 12 → 24 exact in frame data; the in-view
  café light `425c3b17-…` (4.5 → 9) changes the Viewport image (the Spec
  0055 Correction 2026-10-08 precedent).
- **Regression (R7):** Debug + Release full suites, every golden
  byte-identical; the non-editor `runFrame()` guarded line by line as in
  Spec 0055; `assembleDebug`.

## Risks & Open Questions

Risks:

- **The first changes to Platform's event set, RHI's target types and
  Renderer's passes since their foundations** — each is small, but each is
  a public surface (ADR-0108, ADR-0109).
- **An editor frame path in Runtime.** It must stay optional and leave the
  non-editor frame unchanged (the 0055 named-line discipline).
- **A UI dependency.** Immediate-mode UI couples the view layer to the
  library; the editor's models are kept UI-free so the view can be
  replaced.
- **Bistro's frame time** (about 130 ms with its log flood) makes the
  editor's UI on Bistro run at that rate when hosted in the Runtime's frame.

Open questions — Q1–Q8 posed by the maintainer, Q9 surfaced while
drafting. Each lists options and a recommendation.

- **Q1 — UI technology** (a new dependency; ADR-0107, after ADR-0082's
  selection precedent).
  - **(U-a) Dear ImGui** (MIT, C++, a pinned source fetch, ADR-0006).
    - **Dependency surface:** a handful of source files compiled into the
      editor library; no transitive dependencies.
    - **Integration:** ImGui's core produces `ImDrawData` (2D textured,
      clipped triangle lists) from input values; it needs no backend. Its
      stock Vulkan and Win32 backends are **not** used: the first would put
      Vulkan calls outside the Vulkan Backend, the second Win32 outside
      Platform. Instead, input comes from Platform events (Q9) and the draw
      data is converted to a Renderer-owned draw list and drawn by a
      Renderer pass through RHI (Q3) — no ImGui type reaches the Renderer.
    - **Exit risk:** the editor's models (Hierarchy, Inspector, Gizmo) hold
      no ImGui type; replacing the UI rewrites the view layer only.
  - **(U-b) Self-drawn on RHI/RenderGraph.** No dependency, but a widget
    toolkit, layout and text rendering (font rasterization — `stb_truetype`
    is in the already-vetted stb) built from scratch: months of work that is
    not the editor, before the first panel.
  - **(U-c) Web UI** (a local browser page, reusing Spec 0055's JSON
    transport for World access).
    - **Dependency surface:** an HTTP/WebSocket server in Runtime (new code
      or a new dependency) and a front-end toolchain.
    - **The Viewport cost:** the only way a frame leaves the Runtime today is
      capture: measured +0.5–0.6 s per frame (about 2 FPS), 0.37–2.6 MB of
      PNG each. A usable stream needs a new GPU-side encode path (a video
      encoder dependency) or shared memory — a large new subsystem.
  - **Recommendation: U-a**, with the integration rule above. It is the
    smallest dependency that delivers four panels now, and the rule keeps
    the Vulkan-only-in-the-backend and OS-only-in-Platform boundaries
    intact.

- **Q2 — The host model** (load-bearing; ADR-0108).
  - **(a) `atlantis_runtime --editor`.** The editor library is a client
    hosted by the Runtime executable, as Atlantis CLI is hosted by
    `--exec` (Spec 0054). It gets an InProcess connection and the
    in-process `RuntimeControlHost`; Runtime, the composition root, owns the
    window, the GPU and the frame, and draws the Viewport and the UI.
    - **Boundary change:** none to who depends on whom (the executable links
      one more client library); the cost is Runtime's editor frame path
      (Q3) and the Platform/RHI/Renderer additions.
    - **Scale:** in process, World access has no wire: Bistro's 5969-entity
      listing is a function call, not 570 KB of JSON.
  - **(b) A standalone `atlantis_editor` executable** embedding the
    Runtime. It must link `atlantis_runtime_host`, which AGENTS.md forbids
    any other top-level module to depend on: the rule would have to change,
    and the editor would become a second composition root — the "big
    object" in all but name.
  - **(c) Remote attach** (an editor process attached through Spec 0055's
    transport). World access works today (0055 measured it), but the
    Viewport needs frames across processes: capture is about 2 FPS (Q1
    U-c's numbers). Sharing a GPU image between processes needs
    platform-specific external-memory APIs in the Vulkan Backend — a new
    design.
  - **Recommendation: (a).** It is the only option with a real-time
    Viewport and no change to who may depend on Runtime. It keeps the editor
    an ordinary client: the host gives it exactly what `--exec` gives the
    CLI — a connection — plus a control and its inputs and Viewport. (c)
    remains a later mode once a frame-sharing path exists.

- **Q3 — The Viewport rendering path** (ADR-0108).
  - **(V-a) Offscreen, then sampled by the UI.** The scene is drawn by the
    existing `drawFrame()` into an offscreen target at the Viewport panel's
    size (the Spec 0055 capture machinery, without readback); the UI pass
    samples it as a texture inside the layout. Needs: an RHI offscreen
    colour target that can also be sampled (today it is
    `COLOR_ATTACHMENT | TRANSFER_SRC` only); the window-sized depth/HDR/
    bloom targets follow the Viewport's size instead of the window's.
  - **(V-b) A sub-rectangle of the swapchain.** The scene is drawn straight
    into the Viewport's rectangle of the swapchain image; the UI around it.
    No sampleable target, but `drawFrame()`'s passes (HDR, bloom, output
    transform) all assume the full target: a Renderer API change to render
    into a rectangle, and the UI must avoid overdrawing it.
  - **Ownership and threading:** in both, the window and swapchain stay
    Runtime's (`Presentation`), and the Viewport target is Runtime's; the
    editor holds an opaque handle. One frame thread: the frame builds the UI,
    draws the scene, then the UI, then presents (ADR-0004; vsync FIFO
    unchanged).
  - **The camera:** v1's Viewport shows the scene's active camera. A free
    editor camera would either write the camera entity's `WorldMatrix`
    (an edit to the World) or need a Renderer override (a privileged
    path); neither is v1. The host gives the editor the frame's view and
    projection (the Spec 0055 frame data) for the Gizmo.
  - **Picking** (click in the Viewport to select): not v1 — it needs an ID
    buffer or mesh data the client cannot reach; selection is from the
    Hierarchy.
  - **Recommendation: V-a.** It reuses 0055's offscreen path, decouples the
    Viewport's size from the window's, and its RHI change (one usage bit and
    a sampled view) is smaller and more contained than teaching every
    Renderer pass a sub-rectangle.

- **Q4 — The Hierarchy's data** (the baked world is flat).
  - **Data:** a flat list. First, one `listEntities()` and every entity's
    `listComponents()`; then one subscription (`EntityCreated`,
    `EntityDestroyed`, `ComponentAdded`, `ComponentRemoved`) drained once
    per frame for incremental updates. Rows show the GUID and component
    names (no names exist, 0055 N-a); a text filter on GUID prefix and
    component is client-side; the list is virtualized (only visible rows
    drawn).
  - **Scale:** in process (Q2 a), the first listing is 5969 calls in one
    frame — no transport, no 1 MiB line; per frame afterwards only the
    events. Over the transport (Q2 c, later) the measured 570 KB listing
    costs one pipelined frame boundary, within the limits.
  - **(L-a) No server-side filtered listing** (Spec 0054's queued L2) in v1.
  - **(L-b) Add L2** now: a filtered `findEntities` on `RuntimeConnection`
    — a change to ADR-0105's interface with no v1 consumer that needs it in
    process.
  - **Recommendation: L-a.** A tree Hierarchy is explicitly the future
    authoring editing mode's.

- **Q5 — Gizmo semantics.**
  - **What it writes:** the selected entity's `WorldMatrix` (0052 T-a), not
    `Transform`.
  - **The math:** client-side, by the schema: read `column0`–`column3`,
    decompose into translation, rotation and scale, apply the drag, compose,
    and write the four columns. An entity without `WorldMatrix` gets no
    Gizmo. Shear is not representable: a sheared matrix is shown
    read-only to the Gizmo (its columns stay editable in the Inspector).
  - **Writes during a drag:**
    - **(G-a)** at most one transaction (the four columns) per frame,
      carrying the latest drag state; a final one on release;
    - **(G-b)** one transaction on release only (no live feedback);
    - **(G-c)** a transaction per input event (many per frame, all but the
      last superseded before they apply).
  - **The Inspector's `Transform`:** shown and editable like any component,
    with a note that it does not affect rendering (0052 T-a).
  - **Recommendation: G-a** — live feedback at the frame rate, every frame's
    matrix whole (one transaction), nothing written that a later write in
    the same frame supersedes.

- **Q6 — The Content panel** (in the layout, not in the four features).
  - **(C-a) Not built in v1.** The layout reserves the quadrant; it shows
    nothing yet.
  - **(C-b) A read-only catalog list.** The catalog is Asset System data,
    not World data: reading it means either a new Connection query (the
    connection carries the World only) or the editor reading the catalog
    file directly (a second data path beside the connection).
  - **Recommendation: C-a.** The maintainer's four-feature list is binding;
    C-b opens a data path that needs its own decision.

- **Q7 — Module placement** (ADR-0108).
  - **(P-a) Atlantis Editor, `src/editor/`**, a client library: links
    Atlantis Connection and the UI library only; includes no Runtime, ECS,
    RHI, Renderer, Vulkan, Platform or OS header; boundary-scanned like the
    CLI (Spec 0054) and Remote (Spec 0055). Its inputs are plain values; its
    outputs are commands, control calls and a UI draw list. The Runtime
    executable links it; Runtime's host library does not.
  - **(P-b)** The editor inside Atlantis CLI — mixes a UI dependency into
    the CLI library, whose rule is "links Connection only".
  - **Docs:** the normative sentences (AGENTS.md, `module_boundaries.md`)
    land with the implementation PR, narratives after merge (the Plan
    0054/0055 J6 split).
  - **Recommendation: P-a.**

- **Q8 — Control and cadence.**
  - **Control:** play, pause and step buttons call `RuntimeControl`
    (resume, pause, step) — the in-process `RuntimeControlHost` of Spec 0055,
    which the host creates under `--editor` as it does under `--listen`.
  - **Cadence:** the editor's UI frame is the Runtime's frame (vsync FIFO;
    about 60 Hz on the default scene, about 7.6 FPS on Bistro with its log
    flood). Paused, frames keep running (0055 P-a), so the editor stays
    responsive while the World is held.
  - **The north star** as in Summary, automated: the editor's models and UI
    driven by scripted input in process; frame data exact and the Viewport
    image changed. Bistro-gated: frame data exact on `6b63b12c-…`, the image
    change on the in-view café light `425c3b17-…`.
  - **Recommendation:** as above.

- **Q9 — Input** (surfaced; ADR-0109).
  - **(I-a)** Platform's event set gains input events — pointer move,
    pointer button, wheel, key down/up, text — emitted by Windows Platform
    from its message pump; Android Platform emits none in v1.
  - **(I-b)** The editor or Runtime reads Win32 messages itself — OS code
    outside Platform, against AGENTS.md's Platform rule.
  - **(I-c)** The UI library's own Win32 backend — the same violation.
  - **Recommendation: I-a.** Runtime translates them into the editor's
    plain input values; the editor sees no Platform type.

## Out of Scope / Future Work

- Everything in the maintainer's named-only list (Non-Goals).
- A remote editor (Q2 c) once frames can cross processes.
- A tree Hierarchy, picking, a free editor camera, Custom Inspectors,
  undo/redo, saving — all with the authoring editing mode or later Specs.
- The server-side filtered listing (L2) when a remote consumer needs it.
