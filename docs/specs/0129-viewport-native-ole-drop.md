# Spec 0129 — Native OLE Target for Viewport Mesh Placement

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0129](../plans/0129-viewport-native-ole-drop.md)
- **Approval:** 2026-11-21, [joint approval of `e08987ea`](https://github.com/slmao/Atlantis/pull/421#issuecomment-7060287245). Maintainer merge `a36518d4`.
- **Related ADR:** [ADR-0178](../adr/0178-editor-owned-ole-viewport-drop.md) (Accepted)

## Summary

Supplement [Spec 0128](0128-viewport-mesh-placement.md) with a Windows OLE
destination for the existing same-window Assets drag. Amend only how drag
events enter the Editor. Keep the approved ray, preview, expiry,
transaction, history, Apply and Bistro acceptance requirements.

## Motivation

At baseline `a7d0004b`, three real 96-DPI drags over the embedded Runtime
child delivered zero Viewport hover or drop callbacks to the top-level
Avalonia route. Enabling Avalonia AllowDrop on the host did not help. In the
same setup an ordinary Avalonia control received an actual Drop and the
Hierarchy destination received DragOver. [PR #420 native evidence](https://github.com/slmao/Atlantis/pull/420#issuecomment-7060085719)

## Goals and non-goals

Prove one native route within the existing Editor/Runtime boundary, with
ordinary input usable immediately after teardown. Keep the 0115 Assets drag
source, Inspector and Hierarchy destinations. File import, cross-window
transfer, Runtime-side drop handling and changes to placement semantics are
outside the supplement.

## Requirements

### R1 — Admission and callbacks

Use the Editor-owned window specified by ADR-0178 for the drawable
rectangle. Arm it only after [0115](0115-asset-drag-drop.md) admits an
eligible Mesh drag from Assets in that window and just before the existing
Avalonia OLE drag starts. Bind the target to the current drag, owner
window, native host and Runtime child identity. If setup fails, give
local feedback when possible and leave the existing 0115 targets usable.

Accept only the existing one-use drag token for that exact active source.
The Avalonia and OLE destinations use the same token and share its
consume-once path. The native adapter accepts one bounded private platform
format, with validated storage and balanced release. It reads no file,
text or other fallback format. Unknown, foreign, retired or replayed
payloads confer no authority. Report Copy only for a current Spec 0128
candidate, and at Drop only when it is queued. An OLE effect reports local
admission while transaction and Apply settle independently in the Worker.

Use OLE screen coordinates converted once to current host client pixels.
Require the hit-test to name the armed target above the confirmed drawable,
within its bounds. Treat callbacks as input notifications. Preview queries
and submission follow the existing asynchronous Editor/Worker split.
DragLeave clears the candidate and mark while allowing the same gesture to
return. A leave or normal OLE completion after a verified Drop does not
discard a queued release. Spec 0128's send guards still apply.

### R2 — Disarm and restore input

Disarm after Drop, Escape or other native cancellation, loss of eligibility,
unexpected loss of capture or foreground, drawable invalidation, host
destruction, Runtime disconnect, or Editor close. A change of drawable
identity or client geometry, including a DPI change, retires the Viewport
route for that gesture. The normal OLE capture handoff retains the gesture.
OLE completion and the source's exception/finally path share the same
teardown. An expired callback cannot re-arm the target, restore its marker
or consume another drop.

Clear the placement feedback, hide the target immediately and revoke
registration and destroy the window on the owning UI thread. During an
OLE callback, disarm immediately and complete native teardown after the
callback returns. Successful queueing retains the release intent only.
Failed setup or revoke still leaves no hit-testable destination. Restore
the pre-existing Runtime input route by removing the temporary window.
Do not force focus, inject Viewport input or manually release unrelated
capture. On abnormal Editor process exit the OS removes the Editor-owned
window. A subsequent launch has no persisted registration or input state.

### R3 — Feasibility gate

After approval and maintainer merge, run the bounded Windows 100% route
check in Plan 0129 before resuming Spec/Plan 0128. The check uses physical
mouse gestures over a functioning, visible embedded Runtime, delivers
native enter/over/leave/drop, and demonstrates input restoration after
cancel and teardown. Preserve failed evidence and stop if the route
cannot be proven within the approved boundary. Simulated drag events are
supplementary evidence only.

## Proposed design and architectural impact

[ADR-0178](../adr/0178-editor-owned-ole-viewport-drop.md) partially supersedes
ADR-0177's top-level event route and its initial feasibility check. It
introduces a private Windows OLE adapter in Editor App. Ordinary forwarded
input stays with ADR-0126. A requirement to change the Runtime-owned HWND,
Platform forwarding, process ownership, public contracts or OLE policy
beyond the scoped adapter returns to human review.

## Alternatives and verification

ADR-0178 records the rejected input routes. [Plan 0129](../plans/0129-viewport-native-ole-drop.md)
defines physical-input proof and links the unchanged [Plan 0128](../plans/0128-viewport-mesh-placement.md)
acceptance.
