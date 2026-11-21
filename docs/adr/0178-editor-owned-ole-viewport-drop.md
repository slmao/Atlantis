# ADR 0178 — Editor-Owned OLE Target Above the Runtime Drawable

- **Status:** Accepted
- **Date:** 2026-11-21
- **Deciders:** Maintainer
- **Acceptance:** 2026-11-21, [joint approval of `e08987ea`](https://github.com/slmao/Atlantis/pull/421#issuecomment-7060287245). Maintainer merge `a36518d4`.
- **Related Spec:** [Spec 0129](../specs/0129-viewport-native-ole-drop.md)
- **Supersedes in part:** [ADR-0177](0177-editor-viewport-placement-boundary.md), only its initial top-level drag route and proof

## Context

The approved top-level route missed native OLE callbacks over the actual
cross-process child on Windows 100%. A normal Avalonia release in the same
window supplied a positive control. Existing Win32 forwarding delivers
ordinary input after the gesture. [PR #420 route results](https://github.com/slmao/Atlantis/pull/420#issuecomment-7060085719)

## Decision

- Create one dedicated child HWND under the Editor's existing
  `ViewportWindow`, a sibling above the Runtime-owned drawable rather than
  a child of that drawable. The Editor UI thread owns the HWND and registers
  its `IDropTarget`. Clip the surface to the drawable client area. Use a
  nonactivating layered child with minimal **nonzero** opacity and no
  hit-test bypass, keeping the underlying scene and existing placement
  marker visible. Zero alpha and transparent hit-testing pass pointer
  input through. Validate child composition, clipping and hit-testing
  over the actual Runtime in the route proof. [Win32 layered windows](https://learn.microsoft.com/en-us/windows/win32/winmsg/window-features#layered-windows)
- Preflight the geometry, create the window hidden, register then show it
  without activation just before the eligible Assets OLE drag. Use the
  existing message-pumping Editor STA. Balance only OLE initialization
  acquired by the adapter. The same owner hides, revokes and destroys the
  window through Spec 0129's teardown rules. Keep the COM wrapper alive
  through revocation and any callback in flight. A native-handle failure
  detaches the route and preserves ordinary input. [RegisterDragDrop](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-registerdragdrop), [RevokeDragDrop](https://learn.microsoft.com/en-us/windows/win32/api/ole2/nf-ole2-revokedragdrop)
- Use a named Windows platform byte format via the pinned Avalonia
  source and existing managed targets so the native target reads
  the exact same one-use token. Validate the one expected fixed-size
  token and its OLE medium. Use .NET source-generated COM wrappers for
  the narrowly required interfaces. Keep built-in COM interop disabled
  and allow generated unsafe stubs only in the Windows Editor App
  project. Keep native marshalling private to its adapter. The proposed
  project change is this single compiler setting. [Avalonia DataFormat](https://docs.avaloniaui.net/api/avalonia/input/dataformat), [.NET source-generated COM](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/comwrappers-source-generation)
- Keep the existing Avalonia drag loop, parent/child embedding and
  forwarded pointer route. The Editor creates and registers only its own
  temporary HWND. The Runtime HWND is observed for identity and bounds.
  Its procedure, OLE registration, Z-order API calls and ownership stay
  with the existing boundary. The Editor makes Z-order changes to its own
  temporary HWND only. The Worker uses the original shared projection,
  overlay and authoring contracts.

## Consequences

The Windows Editor owns one extra hit-testable window while an eligible
drag is active. Composition and teardown must be established on a real
desktop before shipping. No Renderer, Platform, Runtime, wire,
dependency or shared-client changes are authorized. If proving this
requires subclassing, registering or reparenting the foreign HWND, changing
Runtime input forwarding, adding a hook or changing the COM/build scope
again, stop for a new decision.

## Alternatives considered

- Top-level Avalonia dispatch, even with host AllowDrop. The existing
  Windows evidence rules it out for the current embedded layout.
- Register or subclass the Runtime HWND. This crosses the accepted
  ownership boundary and would require a separate ruling.
- Treat a forwarded mouse-up as Drop. This cannot establish OLE source
  and effect, or distinguish cancellation from an admitted drop.
- Use a permanent overlay window. Temporary ownership limits input
  interception to the gesture and gives a definite restoration check.
