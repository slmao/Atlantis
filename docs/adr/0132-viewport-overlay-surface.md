# ADR 0132: The Viewport Overlay — Drawing into the Viewport from Any Client

- **Status:** Proposed
- **Date:** 2026-10-24
- **Deciders:** slmao
- **Acceptance:** pending (review of Spec 0066's branch PR)
- **Related Spec:** [Spec 0066: Viewport Overlay and Editor Input over the Wire](../specs/0066-editor-overlay-and-input.md)
  (`Draft`; its Q1, Q2, Q4)
- **Related ADR(s):**
  - **Extends** [ADR-0108](0108-editor-host-and-viewport-composition.md) D3: the Renderer gains a second overlay
    input beside the in-process `UiDrawList` — world-space, depth-aware primitives and a selection outline. The UI
    overlay pass, its draw list and the `--editor` path are unchanged.
  - **Extends** [ADR-0128](0128-remote-viewport-camera-and-raycast.md) and **supersedes in part**
    [ADR-0106](0106-attachable-runtime-transport-and-control.md) D3: `RuntimeControl` and `atlantis.remote/1` gain one
    more call, `setViewportOverlay`, with ADR-0128's per-client lifetime rule; and **extends** ADR-0106 D5 (capture):
    a capture's request gains `withOverlay`, and a capture draws no overlay unless asked. The protocol version is
    unchanged (additive).
  - **Applies** [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md) D2 (one interface for
    every client) and answers [ADR-0126](0126-viewport-embedding-and-input-routing.md)'s airspace consequence (an
    overlay must be drawn by the Runtime). ADR-0126's input decisions are unchanged.
  - **Keeps** [ADR-0103](0103-runtime-world-operation-boundary.md) (no World verb, event or component),
    [ADR-0123](0123-editor-viewport-camera-and-picking-host-services.md) D2 (picking unchanged) and
    [ADR-0124](0124-editor-transaction-and-client-side-undo.md) (the Gizmo's preview/release contract).
  - Specifies what [Spec 0062](../specs/0062-editor-viewport-navigation-picking.md) named as future work: a rendered
    selection outline.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The Renderer's only overlay is in-process and screen-space** (ADR-0108 D3, `ui_overlay.h`): a `UiDrawList` of
  textured, vertex-coloured, scissored triangles in target pixels, no camera, no depth, drawn under `--editor` only.
- **Atlantis Editor App runs out of process** (ADR-0125) and embeds the Runtime's window as a Win32 child (ADR-0126
  V-a). Avalonia cannot paint over it. Whatever the editor shows inside the Viewport — a Gizmo, a selection outline,
  debug shapes — must be drawn by the Runtime.
- **Every editor capability is a call of the shared surface** (ADR-0125 D2), as ADR-0128's camera and ray-cast are.
- **A client has no meshes.** Bounds and pick geometry are Runtime services (ADR-0123 D2); only `raycast` crosses the
  wire. A selection outline needs the selected meshes.
- **E1's Gizmo and picking are client-side models** (`gizmo.h`, `viewport_picking.h`): the client knows its handles'
  geometry and the camera it set, and hit-tests in its own process.
- **The frame path is guarded and gated:** `runFrame()` changes are enumerated; the performance gate applies to the
  Runtime and the Renderer; a frame with nothing to overlay must stay exactly as it is.

## Decision

1. **The value.** Atlantis Connection gains a Viewport overlay value: a selection set (`EntityGuid`s) and groups of
   primitives from a closed, versioned set —
   - segments (two points, a width in pixels),
   - filled triangles (three points),
   - points (one point, a size in pixels),

   each with a straight-alpha colour authored in sRGB and a depth mode: tested against the scene's depth, or drawn on
   top. A group is **world** (its points are world positions) or **screen-constant** (an anchor point in world space
   and a pixel scale: its points are offsets from the anchor, drawn so that one unit is that many pixels at the
   anchor's view depth, from the frame's own camera). No text, texture or screen-space-positioned primitive. Limits
   on groups, primitives and selection size are part of the value's contract (the Plan sets them); an overlay over
   them is refused.
2. **The call.** `RuntimeControl::setViewportOverlay(optional<overlay>)`, wire method `control.setViewportOverlay`.
   - It replaces the calling client's overlay whole from the next frame on; `null` clears it.
   - **Per client:** every client has its own overlay; the frame draws the union of all clients'. A client's overlay
     is removed when its connection closes (ADR-0128 D1's lifetime). An over-limit overlay is refused
     (`InvalidRequest`) and changes nothing.
   - It is not World state: no event reports it and no `RuntimeConnection` call sees it.
   - **Captures:** a frame capture (`control.step` with an image) draws **no overlay** unless the request carries
     `withOverlay: true`, and then **only the requesting client's own overlay** — never another client's. The
     transport that holds the per-client overlays (Remote's server) resolves the request to that overlay. Frame data
     is unchanged.
   - **Every client gets it:** the CLI (`runtime overlay …`), Remote's C++ client, the C# SDK's control surface;
     `remote_protocol.md` documents it; conformance vectors cover it. The C++ Gameplay SDK, which exposes no control
     calls, is unchanged.
3. **The Renderer draws it** through a Renderer-owned draw list (`ViewportOverlayDrawList`: the primitives'
   vertices with colour, depth mode and, for a screen-constant group, its anchor and pixel scale; and the selected
   entities' draw items) and one new call that records **RenderGraph passes** (the mandatory path for GPU work) after
   the scene's passes, into the frame's scene target, from the camera the frame drew:
   - **the selection outline first:** the selected meshes at their current `WorldMatrix` into a mask, then an edge
     pass composing a fixed-colour outline over the target, visible through occluders;
   - **then the primitives:** a screen-constant group's offsets scaled from the frame's camera at its anchor's
     depth; lines as screen-space-expanded triangles (no wide-lines device feature); depth-tested ones against the
     frame's depth (read only), on-top ones without depth;
   - new shaders for both, through the Shader System like every other pass.

   The Renderer knows neither the wire nor an editor: the Runtime translates the control value into the draw list, as
   the `--editor` host translates ImGui's into `UiDrawList`.
4. **The Runtime** resolves the union's selection through the bake's GUID map to draw items each frame (an entity
   without a `Renderable`, unknown or destroyed, is skipped), and records the overlay at **one named place in
   `runFrame()`** after the scene and before the UI pass or the present. With no overlay from any client the place
   records nothing: the frame's passes, counters and pixels are unchanged. A capture re-renders with no overlay,
   or with the requesting client's own overlay when its request asks (D2); `FrameData` is unchanged.
5. **The Gizmo stays client-side** (Spec 0066 Q2 G-a, Q4 H-a): its geometry is sent as primitives and hit-tested by
   the client against what it sent, with E1's models. There is no Runtime-side Gizmo.

## Consequences

### Positive

- **Every client can draw into the Viewport,** the CLI and agents included (highlight an entity, draw a path), with
  no editor-only channel.
- **The overlay is drawn in the image's own frame,** from its camera and against its depth, so it never lags or
  floats over the scene.
- **The outline uses the Runtime's meshes,** which no client has; the Gizmo uses the client's model, which the
  Runtime need not have.
- **Zero cost when unused,** and nothing else in the frame changes.
- **Captures stay clean for agents and CI:** another client's Gizmo or debug lines never reach an evaluation image;
  a client that wants its own overlay in an image asks for it.
- **A camera move needs no resend** for a screen-constant Gizmo: the Runtime sizes it from the frame's own camera,
  so it never shows one frame at the previous camera's size.

### Negative / Trade-offs

- **New Renderer passes and shaders,** and one more guarded `runFrame()` place; the gate applies to the
  implementation.
- **A resend when the previewed matrix changes:** during a drag the client re-sends its overlay with each preview
  (small for a Gizmo; bounded by the limits). World-space groups that must follow the camera are re-sent as well.
- **The capture request grows a field** (`withOverlay`) and the transport resolves "own" per client.
- **Overlay latency is the frame boundary's:** an overlay appears one or two Runtime frames after it is sent, like the
  camera.
- **A one-shot CLI call's overlay ends with its process,** as `runtime camera set` does; the CLI's call is useful in
  `repl`, `--exec` and scripts that hold a session.

## Alternatives Considered

- **A transparent top-level window kept by the editor above the Viewport** (Spec 0066 Q1 (b)): editor-only, one
  frame behind the image, no depth against the scene, and its own z-order, DPI and focus handling.
- **Frame streaming** (Q1 (c)): rejected in Spec 0064 Phase 1 (V-b).
- **Reusing the UI overlay pass** (`UiDrawList`) for remote clients: screen-space only, no depth, and drawn only with
  an offscreen Viewport target; a client would have to project every primitive with the camera of a frame it has not
  seen yet.
- **An open mesh/texture upload** (Q2 (P-b)): far more surface than a Gizmo, outline and debug shapes need.
- **Persistent overlays until cleared** (Q2 (L-b)): a crashed editor would leave a stale Gizmo.
- **Captures showing the window's union:** an agent's or CI's image would carry other clients' Gizmos and debug
  lines.
- **World-space primitives only, resent on every camera change** (Q2 (S-b)): a message per camera change, and a
  one-frame size pop when the camera and the overlay land in different frames.
- **A Runtime-side Gizmo** (`showGizmo`, Q2 (G-b)) and **Runtime-side overlay hit-testing** (Q4 (H-b)): a second
  Gizmo implementation, and a round trip per pointer move.
- **A client-drawn bounds box instead of an outline** (Q2 (O-b)): needs bounds on the wire, and is not an outline.
