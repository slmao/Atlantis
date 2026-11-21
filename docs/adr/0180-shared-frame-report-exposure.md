# ADR 0180 — Report Effective Exposure in Shared Frame Data

- **Status:** Accepted
- **Date:** 2026-11-21
- **Deciders:** Atlantis maintainer
- **Acceptance:** Maintainer approved revision `637eaefd` in [PR #425](https://github.com/slmao/Atlantis/pull/425#issuecomment-7063983586), 2026-11-21.
- **Related Spec:** [Spec 0131](../specs/0131-bistro-reference-views.md)

## Context

The existing `control.step` report carries matrices, direct lights and an
optional PNG through [ADR-0106](0106-attachable-runtime-transport-and-control.md).
A camera override sets pose and projection under
[ADR-0128](0128-remote-viewport-camera-and-raycast.md). Runtime keeps the
underlying active scene camera's exposure even with the override set. The
authoring document can be ahead of the visible scene, so source values and
authoring queries cannot substitute for a report of the rendered exposure.

## Decision

Add optional `exposureCompensationEv` to the existing `FrameData` object
in `atlantis.remote/1`. It represents the finite EV value used by Runtime's
active camera output transform for the reported frame, including when an
override supplies pose and lens. Use the same frame-report sampling path
and effective camera selection as PNG capture. Make the value available to
native RuntimeControl clients, CLI and the C# SDK. The existing MCP
`control.step` tool returns the additive field in the same report.

When present, the member is a finite number representable as a float.
Its absence means unavailable. Producers omit the missing member. New native
and managed decoders accept omission and reject a present nonnumeric,
nonfinite or out-of-range value. Old clients continue to read the existing
members. Preserve the positional constructor of managed FrameData. Zero is
an explicit measured EV and must remain distinct from absence. No new
command, version handshake, setter, capture request member or MCP tool.

The new value is extracted only on the existing report path. Keep camera
override lifetime, active scene camera ownership, exposure computation and
capture extent unchanged. This extends the report decision in ADR-0106.
The camera override and capture behavior from ADR-0128 and ADR-0132 remain
in force.

## Consequences

CLI, SDK, Editor and MCP can distinguish the exposure used for an image
from a value in an unapplied document. On an older Runtime there is no
capture-time exposure reading. Direct lights are already in the frame
report. The additional value makes no claim about reference-image lighting,
display gamma or equivalence to another renderer.

## Alternatives considered

- Read the authoring camera or infer exposure from the visible image. Both
  can give a value other than the Runtime's effective compensation.
- Add a Bistro-only capture API or extend ViewportCamera with exposure
  writes. Both would expand the control boundary beyond reporting one
  existing frame value.
