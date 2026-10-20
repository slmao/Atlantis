# ADR 0128: The Viewport Camera and Ray-Cast on the Control Surface and the Wire

- **Status:** Proposed
- **Date:** 2026-10-20
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0064: Editor Platform Direction](../specs/0064-editor-platform-direction.md) (`Draft`; its Q3)
- **Related ADR(s):**
  - **Supersedes in part** [ADR-0106](0106-attachable-runtime-transport-and-control.md) D3: `RuntimeControl` gains
    two calls beyond status, pause, resume, step and diagnostics. ADR-0106's transport, payload rule, pause, capture
    and output contract are unchanged; the protocol's version string stays `atlantis.remote/1` (additive methods).
  - **Supersedes in part** [ADR-0123](0123-editor-viewport-camera-and-picking-host-services.md) D2's "They are not
    `RuntimeConnection` or `RuntimeControl` calls, and nothing reaches them over Atlantis Remote" and D4's "does not
    pre-authorize it". ADR-0123 D1's camera rules and D2's picking rules are reused unchanged.
  - Keeps [ADR-0103](0103-runtime-world-operation-boundary.md), [ADR-0104](0104-runtime-world-transaction-atomicity.md)
    and [ADR-0105](0105-runtime-connection-and-cli-client.md): `RuntimeConnection` does not change.
  - Keeps [ADR-0112](0112-csharp-gameplay-client-transport-and-bindings.md) D2: conformance vectors cover the new
    methods.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **An out-of-process editor needs a camera and picking** (Spec 0064 R5). E1 built both as in-process host
  services (ADR-0123 D1, D2) and deferred their wire form by name (Spec 0062 Q2 K-d).
- **The World surface is closed** (Spec 0052/0053); a ray-cast answers from render-side geometry, which ADR-0123
  D2 placed outside World data.
- **`RuntimeControl` already carries frame-side services** that are not World data: step with capture (ADR-0106
  D3, D5).
- **One interface for all clients** (ADR-0125 D2): what the editor gets, the CLI and SDKs get.

## Decision

1. **`RuntimeControl::setViewportCamera(optional<camera>)`**, wire method `control.setViewportCamera`.
   - **Value:** ADR-0123 D1's `{worldMatrix[16], fovYRadians, nearZ, farZ}`; `null` clears it.
   - **Effect:** the frame draws from it at `runFrame()`'s camera place (Spec 0062 place 5), with ADR-0123 D1's
     rules: exposure, fog and bloom stay the active camera's; an unusable camera is ignored (logged once); captures
     and frame data report the drawn camera. When an in-process overlay also supplies a camera, the overlay's wins.
   - **Scope:** one camera per Runtime, last writer wins, cleared when the setting connection closes. It is not
     World state: no event, no `RuntimeConnection` call sees it.
2. **`RuntimeControl::raycast(ray)`**, wire method `control.raycast`.
   - **Value:** `{origin[3], direction[3]}` (world space, unit direction) → `{entity, distance}` or `null`, or the
     error `NoGeometry` when pick geometry was not retained.
   - **Semantics:** ADR-0123 D2's service unchanged (CPU geometry, nearest `Renderable` at its current
     `WorldMatrix`, both faces, alpha masks ignored), answered between frames.
   - **Retention:** `BootstrapConfig::retainPickGeometry` is set by `atlantis_runtime --listen` as well as
     `--editor`; `--no-pick-geometry` opts out.
3. **Every client gets both.** The CLI gains `runtime camera set|clear` and `runtime raycast`;
   `connection::RuntimeControl` gains the two calls (implemented by `RuntimeControlHost` in process and by Remote's
   `RemoteControl`), and the C# SDK's control surface mirrors them; `remote_protocol.md` documents them;
   conformance vectors cover them. The C++ Gameplay SDK, which exposes no control calls today, is unchanged.
4. **Nothing else changes:** no `RuntimeConnection` method, World command, event or component; the protocol
   version string is unchanged because the methods are additive and an older client never calls them.

## Consequences

### Positive

- **The out-of-process editor can navigate and pick**, and so can every script, CI job and agent.
- **The World stays closed;** the services stay where ADR-0123 put them, in the Runtime, now reachable.
- **E1's rules are reused,** not redefined.

### Negative / Trade-offs

- **About 42 MB more memory on Bistro** for every listening Runtime unless opted out (Spec 0062 Q4).
- **One camera per Runtime:** two clients setting it contend (last writer wins).
- **A protocol addition** that every implementation (C++ codec, C# SDK) must carry.

## Alternatives Considered

- **`raycast` on `RuntimeConnection`** (Spec 0064 Q3 W-b): a sixth World verb; rejected, as Spec 0062 K-d was.
- **No wire additions** (W-c): the editor could not navigate or pick.
- **A camera stored in the World** (writing the active camera entity): rejected by Spec 0062 Q1 C-b for the same
  reasons — every client sees the game camera move.
