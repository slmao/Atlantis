# ADR 0107: Editor UI Library Selection

- **Status:** Proposed
- **Date:** 2026-10-08
- **Deciders:** slmao
- **Acceptance:** pending. To be `Accepted` before or during the review of
  [Spec 0056](../specs/0056-minimal-editor.md), as ruled there (Spec Q1).
- **Related Spec:** [Spec 0056: Minimal Editor](../specs/0056-minimal-editor.md)
- **Related ADR(s):**
  - Follows [ADR-0082](0082-gltf-parser-dependency-selection.md)'s
    selection precedent (a candidate matrix for one new dependency) and
    [ADR-0006](0006-dependency-management.md) (pinned source fetch).
  - Constrained by [ADR-0001](0001-rhi-backend-independence.md) (only the
    Vulkan Backend includes Vulkan) and
    [ADR-0005](0005-platform-module-multi-os-windowing.md) (Platform owns
    OS windowing and input).
  - Pairs with [ADR-0108](0108-editor-host-and-viewport-composition.md)
    (where the UI's draw data is rendered) and
    [ADR-0109](0109-platform-input-events.md) (where its input comes from).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

Spec 0056's editor needs four panels (Hierarchy, Inspector, Viewport,
Gizmo) in one window, drawn every frame. Atlantis has no UI toolkit: no
widgets, no layout, no text rendering.

Constraints from the repository:

- **Only the Vulkan Backend includes Vulkan headers** (AGENTS.md); any UI
  library's own Vulkan renderer is out.
- **Only Platform owns OS windowing and input**; any UI library's own Win32
  or Android integration is out.
- **Render Graph is the path for GPU work**; the UI's drawing must be a
  Renderer pass through RHI.
- **A new dependency is pinned** (ADR-0006), small, permissively licensed.

## Decision

**Adopt Dear ImGui** (MIT) for Atlantis Editor's UI, fetched pinned to a
release tag (the exact tag and hash are the Plan's), compiled into the
editor library only, **core only**:

- **No stock backend is used.** Not `imgui_impl_vulkan` (Vulkan outside the
  Vulkan Backend), not `imgui_impl_win32` / `imgui_impl_android` (OS code
  outside Platform).
- **Input** comes in as plain values the host derives from Platform's input
  events (ADR-0109).
- **Output** is ImGui's `ImDrawData`, converted inside the editor library to
  an engine-neutral UI draw list (vertices, indices, clipped commands,
  opaque texture handles). The host passes it to a Renderer pass
  (ADR-0108). No ImGui type crosses the editor library's boundary.
- **The editor's models** (Hierarchy, Inspector, Gizmo) hold no ImGui type,
  so the view layer is replaceable.
- **Not adopted:** ImGui's docking branch (the maintainer excludes complex
  docking); the layout is a fixed four-quadrant split.

**Candidate matrix:**

| | Dear ImGui (U-a) | Self-drawn on RHI (U-b) | Web UI (U-c) |
|---|---|---|---|
| New dependency | One, MIT, a few C++ sources | None (stb_truetype already vetted, for text) | HTTP/WebSocket server and a front-end toolchain |
| Engine integration | Core only: input values in, draw lists out; a Renderer pass draws them | Everything engine-side: widgets, layout, text, input | World access via Spec 0055's transport; Viewport needs frame streaming |
| Vulkan / OS boundary | Kept (no stock backend) | Kept | Kept, but a server in Runtime |
| Viewport | A sampled texture in the UI (ADR-0108) | Same | Capture is +0.5–0.6 s/frame (about 2 FPS), 0.37–2.6 MB PNG each (measured); a stream needs a GPU encoder or shared memory |
| Effort before the first panel | Small | Months (a toolkit) | Large (server, client, stream) |
| Exit risk | View layer only (models are UI-free) | None external | Front-end rewrite |

## Consequences

### Positive

- **Four panels without a toolkit project.**
- **The module rules hold:** no Vulkan outside the backend, no OS code
  outside Platform, GPU work through a Renderer pass.

### Negative / Trade-offs

- **A third-party dependency** in the editor library, and a Renderer pass
  to draw its output.
- **Immediate mode** couples the view code to ImGui's idioms.
- **Text rendering** is ImGui's font atlas (a texture the host uploads), not
  an engine text system.

## Alternatives Considered

- **Self-drawn UI (U-b):** no dependency, but the editor would wait on a UI
  toolkit nobody has specified.
- **Web UI (U-c):** a local server and a browser front end; rejected for v1
  by the Viewport's cost (capture-based frames at about 2 FPS) and the new
  subsystems a stream would need.
- **Other C++ immediate/retained toolkits** (for example Nuklear, RmlUi):
  not evaluated in depth; Dear ImGui's core-without-backends shape matches
  the boundary rules directly.
