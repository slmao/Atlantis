# ADR 0177 — Editor-Owned Viewport Placement

- **Status:** Accepted
- **Date:** 2026-11-21
- **Deciders:** Maintainer
- **Acceptance:** 2026-11-21, [joint approval of `cfa48c79`](https://github.com/slmao/Atlantis/pull/420#issuecomment-7060032916). Maintainer merge `a7d0004b`.
- **Related Spec:** [Spec 0128](../specs/0128-viewport-mesh-placement.md)

## Context

[ADR-0126](0126-viewport-embedding-and-input-routing.md) makes the drawable a
foreign child HWND. Its ordinary pointer forwarding does not establish that
native drag-and-drop callbacks reach the parent while Windows runs the drag
loop. Avalonia also cannot paint a marker over that child. [0115](../specs/0115-asset-drag-drop.md)
already owns the drag source and OLE operation inside the Editor.
Windows routes those drag callbacks through registered drop targets.
[Win32 IDropTarget](https://learn.microsoft.com/windows/win32/api/oleidl/nn-oleidl-idroptarget)

[ADR-0128](0128-remote-viewport-camera-and-raycast.md) and
[ADR-0132](0132-viewport-overlay-surface.md) provide the geometry query and
transient overlay needed for placement.

## Decision

Use the existing Avalonia/Windows drag operation and observe its
enter/over/leave/drop events from the originating Editor's top-level
window. Within that route, recognize the drawable from the editor-owned
Viewport host and its current hosted child. Check the actual window under
the screen point and convert once to host client pixels. Leave the existing
Inspector and Hierarchy routing authoritative outside that drawable. Keep
the existing forwarded input route for ordinary Viewport input.

S2 first proves that this route delivers hover and release over the actual
hosted Runtime child. Delivering synthetic Avalonia events, or delivery over
an empty host HWND, provides no such evidence. If the top-level route does
not work, bring the native routing decision back to review. This decision
keeps OLE registration within the current Avalonia route and makes no
change to the Runtime HWND procedure, Platform forwarding or Remote.

The Worker owns the published candidate from the confirmed Viewport
projection, shared geometry raycast and observed scene/document identities.
Render its mark as a small addition to this Editor client's existing overlay
value. Placement submits the ordinary authoring transaction after release
revalidation. An atomic creation protects its commands. The read-to-send
interval across Runtime geometry and authoring retains the observed-change
boundary in the Spec. None of the overlay, ray or hit target is persisted.

## Consequences

Selection and authoring remain independent of hover. The same APIs continue
to serve all clients and captures keep their existing overlay isolation.
An asynchronously evaluated point can be pending on a large scene. Expiry
requires waiting for a fresh marker or starting another gesture. Windows
native routing and the Runtime's geometry retention are real prerequisites.

## Alternatives considered

- Add a native drop target or input shield on the child. This would add
  registration, positioning and lifetime behavior across the embedding
  boundary before proving the existing route.
- Use ordinary forwarded mouse-up plus the last hover. This cannot prove a
  valid native Drop and could consume the wrong gesture or a stale hit.
- Use a rendered mesh ghost or extend the wire with placement. Neither is
  necessary for a pivot preview and one verified creation.
