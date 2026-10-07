# ADR 0108: Editor Host, Module Boundary and Viewport Composition

- **Status:** Accepted
- **Date:** 2026-10-08 (accepted 2026-10-08)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-08 (review of this branch's own PR,
  [PR #230](https://github.com/slmao/Atlantis/pull/230); accepted together with Spec 0056's Approval, its nine open
  questions ruled as recommended; ADR-0105, ADR-0106, ADR-0103, ADR-0104 and
  ADR-0004 unchanged)
- **Related Spec:** [Spec 0056: Minimal Editor](../specs/0056-minimal-editor.md) (`Approved`)
- **Related ADR(s):**
  - The editor is a client of [ADR-0105](0105-runtime-connection-and-cli-client.md)'s
    `RuntimeConnection` and [ADR-0106](0106-attachable-runtime-transport-and-control.md)'s
    `RuntimeControl`; neither interface changes.
  - Hosted as ADR-0105 D7 hosts Atlantis CLI (`--exec`).
  - Extends RHI's targets ([ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md))
    and Renderer's passes; keeps [ADR-0002](0002-presentation-rendertarget-unification.md)
    (Presentation owns the swapchain) and [ADR-0004](0004-phase1-threading-baseline.md).
  - Pairs with [ADR-0107](0107-editor-ui-library-selection.md) (the UI
    library) and [ADR-0109](0109-platform-input-events.md) (input).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The editor must be an ordinary client** (maintainer, 2026-10-08): World
  access only through `RuntimeConnection`, lifecycle only through
  `RuntimeControl`, no Renderer/ECS/Runtime privilege.
- **The Viewport must show the Runtime's own rendering**, in real time.
- **Runtime's host library is closed:** `atlantis_runtime_host` is not a
  dependency any other top-level module may take (AGENTS.md).
- **The frame:** one frame thread (ADR-0004); `runFrame()` applies commands,
  pumps Platform, draws the scene to the whole swapchain image, presents
  (FIFO). Spec 0055 held `runFrame()` to one named changed line.
- **What the GPU side lacks** (code evidence, Spec 0056 Motivation):
  `OffscreenTarget` images are `COLOR_ATTACHMENT | TRANSFER_SRC` (not
  sampleable); there is no UI pass; Platform has no input events.
- **Measured costs** (Spec 0056): capture-based frames +0.5–0.6 s each;
  Bistro's full listing 570 KB over the transport.

## Decision

1. **Host: `atlantis_runtime --editor`** (Spec 0056 ruling Q2, (a)). The Runtime
   executable hosts Atlantis Editor as it hosts Atlantis CLI: it opens an
   InProcess connection for it and creates the `RuntimeControlHost` (Spec
   0055) for it. No other module depends on `atlantis_runtime_host`; that
   rule is unchanged. A remote editor (attached through Spec 0055's
   transport) is a later mode, once frames can cross processes.
2. **Module: Atlantis Editor, `src/editor/`** (Spec 0056 ruling Q7, P-a). A client library
   that links Atlantis Connection and the UI library (ADR-0107) only, and
   includes no Runtime, ECS, RHI, Renderer, Vulkan, Platform or OS header
   (boundary-scanned). Its interface with the host is plain values:
   - in: the connection, the control, input values, the Viewport's opaque
     texture handle, size, and the frame's view/projection;
   - out: the UI draw list (engine-neutral) and the Viewport size it wants.

   `atlantis_runtime` links it; `atlantis_runtime_host` does not.
3. **Viewport: offscreen, then sampled** (Spec 0056 ruling Q3, V-a). With the editor
   attached, Runtime draws the scene with the existing `drawFrame()` into an
   offscreen colour target at the Viewport's size (its depth, HDR and bloom
   targets follow that size), then draws the editor's UI draw list into the
   swapchain image, sampling that target where the Viewport panel is.
   - **RHI:** an offscreen colour target can be created sampleable and bound
     as a sampled texture (a creation option and a sampled view; the Vulkan
     Backend adds `SAMPLED` usage). Existing targets are unchanged.
   - **Renderer:** a UI overlay pass — textured, vertex-coloured, scissored
     triangles from a Renderer-owned draw list type — through the Render
     Graph. Renderer knows neither ImGui nor the editor.
   - **Ownership:** the window, swapchain and Viewport target are Runtime's;
     the editor holds an opaque handle only.
4. **The frame with an editor attached.** Runtime's frame gains an optional
   editor path at named places: after the Platform pump (input values to
   the editor, its UI update), the scene's target (the Viewport target
   instead of the swapchain), and after the scene (the UI pass). Without an
   editor, `runFrame()` behaves as today, guarded line by line as Spec 0055
   P7 was (the exact mechanism and named lines are the Plan's). The camera
   is the scene's active camera; the editor gets the frame's view and
   projection for its Gizmo.
5. **Control and cadence** (Spec 0056 ruling Q8). The editor's play, pause and step call
   `RuntimeControl`; its UI frame is the Runtime's frame (FIFO vsync).

## Consequences

### Positive

- **A real-time Viewport** with no frame copy to the CPU and no second
  process.
- **The editor stays an ordinary client**: its only World and lifecycle
  surfaces are the two interfaces; everything GPU- or OS-side is the host's.
- **No change to who depends on Runtime.**

### Negative / Trade-offs

- **Runtime's frame grows an editor path** (named places, guarded).
- **Three lower-layer additions**: a sampleable offscreen target (RHI,
  Vulkan Backend), a UI overlay pass (Renderer), input events (Platform,
  ADR-0109).
- **The editor runs inside the Runtime's frame**: on a slow scene (Bistro,
  about 130 ms per frame with its log flood) the UI is equally slow.
- **No remote editor in v1.**

## Alternatives Considered

- **A standalone `atlantis_editor` embedding the Runtime** (Q2 (b)): needs
  `atlantis_runtime_host` opened to another module, and makes the editor a
  second composition root.
- **Remote attach** (Q2 (c)): the Viewport would need capture-based frames
  (about 2 FPS measured) or cross-process GPU memory sharing.
- **A sub-rectangle of the swapchain** (Q3 V-b): every Renderer pass learns a
  sub-rectangle, and the UI must not overdraw it.
- **The editor inside Atlantis CLI** (Q7 P-b): a UI dependency in a library
  whose rule is "links Connection only".
