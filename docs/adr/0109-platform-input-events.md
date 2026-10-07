# ADR 0109: Input Events from Atlantis Platform

- **Status:** Accepted
- **Date:** 2026-10-08 (accepted 2026-10-08)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-08 (review of this branch's own PR,
  [PR #230](https://github.com/slmao/Atlantis/pull/230); accepted together with Spec 0056's Approval, its nine open
  questions ruled as recommended; ADR-0105, ADR-0106, ADR-0103, ADR-0104 and
  ADR-0004 unchanged)
- **Related Spec:** [Spec 0056: Minimal Editor](../specs/0056-minimal-editor.md) (`Approved`)
- **Related ADR(s):**
  - Extends the event model of
    [ADR-0005](0005-platform-module-multi-os-windowing.md) (Platform owns OS
    windowing) and [ADR-0012](0012-application-lifecycle-and-event-model.md) (the event
    span `processEvents()` returns).
  - Used by [ADR-0108](0108-editor-host-and-viewport-composition.md) (the
    host translates these events for the editor).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

`platform::processEvents()` returns window and lifecycle events only —
`WindowResize`, `WindowCloseRequested`, focus, application pause/resume,
surface created/destroyed, `Quit` (`platform_event.h`). Nothing delivers
mouse, keyboard or text input. An editor (Spec 0056) cannot work without it,
and AGENTS.md keeps OS input handling in Platform: neither the editor, nor
Runtime, nor a UI library's OS backend may read Win32 messages.

## Decision

**`PlatformEvent` gains input events** (Spec 0056 ruling Q9, I-a), emitted by Windows Platform from its
existing message pump, in the order received:

- pointer moved (window-relative position in framebuffer pixels);
- pointer button down/up (left, right, middle);
- wheel (vertical and horizontal deltas);
- key down/up (a Platform-defined key code set — the exact set is the
  Plan's — and modifier state);
- text input (UTF-8, for text fields).

They carry plain values, no OS type. Android Platform emits none of them in
v1 (the editor is not hosted on Android); touch input is a later decision.
Consumers that ignore them (today's Runtime frame) are unaffected: the frame
already ignores events it does not handle.

## Consequences

### Positive

- **Interactive clients become possible** without OS code outside Platform.
- **One input path** for any later interactive feature (camera control,
  picking).

### Negative / Trade-offs

- **Platform's public event set grows**, and its key-code set is a new
  long-lived vocabulary.
- **Windows-only in v1.**

## Alternatives Considered

- **The editor or Runtime reading Win32 messages itself** (Spec 0056 Q9
  I-b): OS code outside Platform.
- **The UI library's own Win32 backend** (I-c): the same violation, inside a
  third-party file.
