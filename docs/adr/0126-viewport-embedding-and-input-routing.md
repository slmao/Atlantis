# ADR 0126: Viewport Embedding in a Foreign Window, and Input Routing

- **Status:** Proposed
- **Date:** 2026-10-20
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0064: Editor Platform Direction](../specs/0064-editor-platform-direction.md) (`Draft`; its Q2)
- **Related ADR(s):**
  - Extends [ADR-0005](0005-platform-module-multi-os-windowing.md) (as amended) with a child-window mode; keeps
    [ADR-0011](0011-native-window-handle-representation.md) and
    [ADR-0013](0013-platform-window-ownership-and-lifetime.md) unchanged — the child is Platform's window.
  - Keeps [ADR-0109](0109-platform-input-events.md) (Platform's input events) and
    [ADR-0106](0106-attachable-runtime-transport-and-control.md) D1 (the session file); D3 adds one session-file
    field.
  - Pairs with [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The editor is another process** (ADR-0125 D1) and must show the Runtime's own rendering in real time inside
  its layout, as the ImGui editor's Viewport does in process (ADR-0108 D3).
- **Platform** creates one top-level `WS_OVERLAPPEDWINDOW` on Windows, owns it, and is the only module that
  includes Win32 (ADR-0005, ADR-0013); Vulkan WSI makes a `VkSurfaceKHR` from the borrowed handle (ADR-0011).
- **Frames do not cross processes today:** capture is about 2 FPS (Spec 0056); GPU sharing needs external-memory
  extensions and a compositor integration on the other side.
- **Avalonia** can host a foreign native window inside its layout (`NativeControlHost`), under Win32's airspace
  rule: the child is opaque to Avalonia's drawing, and its mouse and keyboard messages go to the child's own
  window procedure, not to Avalonia.
- **A cross-process parent/child pair attaches the threads' input queues** (implicit `AttachThreadInput`): a
  stalled Runtime frame can stall the editor's input processing.
- **Unknowns:** whether the embedded child holds up under resize, DPI change and Bistro's frame times, and which
  input route gives the editor the pointer and keys over the Viewport. Spec 0064 Phase 1 investigates them.

## Decision

1. **Child-window mode in Windows Platform.** `WindowOptions` gains `parent` (a borrowed `NativeWindowHandle`,
   Windows only; Android ignores it as it ignores `requestedClientSize`). With it, Platform creates its window
   `WS_CHILD | WS_CLIPSIBLINGS` under the parent instead of a top-level window, at the parent's client size.
   - **Ownership is unchanged:** Platform creates, owns and destroys the child (ADR-0013); it never destroys the
     parent; Vulkan WSI creates its surface on the child as on any window and never destroys the window.
   - **Lifecycle events:** `WindowResize` when the child is resized (by its host); `WindowCloseRequested` when
     the parent is destroyed; focus events as today.
   - **DPI:** the process stays per-monitor-DPI-aware v2; the child's size is in framebuffer pixels as today.
   - Only Platform's Windows translation unit names Win32 types; nothing else learns of the parent.
2. **`atlantis_runtime --parent-window <handle>`** passes the parent to Platform. It is accepted only on the
   command line (never over the wire) and is combinable with `--listen`, not with `--editor`.
3. **The session file** (ADR-0106 D1) gains `window`: the child's handle as a decimal integer, present only in
   child-window mode, so the host can size and position it (`SetWindowPos` is valid across processes). Clients that
   do not know the field ignore it; the protocol version is unchanged.
4. **Input routing — to be settled by Phase 1, in this order:**
   - **(R-a) forwarding, investigated first:** in child-window mode the child does not activate on click
     (`MA_NOACTIVATE`) and re-posts pointer, wheel and key messages to the parent with coordinates in the
     parent's client space, so the host's toolkit handles them as its own. Platform still emits its ADR-0109 input
     events (unused without `--editor`).
   - **(R-b) the wire, the fallback:** if R-a fails or lags, a later Spec adds a control-side input stream
     (`control.drainInput`) carrying ADR-0109 values; it is not a World event.

   The Phase 1 report records which route is adopted; adopting R-b needs that Spec, and this ADR's D4 is then
   superseded.
5. **Fallback embedding (V-a2), within this ADR:** if the child-window mode fails Phase 1's acceptance, the Runtime
   keeps its top-level window, made owned by the editor's window and positioned over the host region by the editor
   — same wire, no input-queue attachment, no `NativeControlHost`. Cross-process frame streaming (Spec 0064 Q2 V-b)
   is not adopted; it would need its own Spec and ADR.

## Consequences

### Positive

- **No frame copy and no new GPU code:** the Vulkan surface is on the child as on any window.
- **Platform keeps every ownership and include rule.**
- **The editor needs nothing from the Runtime but a handle and the wire.**

### Negative / Trade-offs

- **Airspace:** nothing of the editor's UI can draw over the Viewport (overlays such as a Gizmo must be drawn by
  the Runtime or beside the Viewport) — a constraint on Phase 2's design.
- **Attached input queues** couple the editor's responsiveness to the Runtime's frame time in V-a.
- **Windows only.** Another OS needs its own child-surface mechanism or the R-b/V-b routes.
- **A Win32 forwarding workaround** in Platform for R-a.

## Alternatives Considered

- **Cross-process streaming** (Spec 0064 Q2 V-b): capture at about 2 FPS, or GPU memory sharing with new RHI and
  compositor work; rejected for Phase 1.
- **A transparent toolkit overlay over the child** (R-c): impossible under airspace.
- **The editor reading Win32 input itself through a hook on the Runtime's window:** OS input handling outside
  Platform in a second process, with global-hook semantics; rejected.
