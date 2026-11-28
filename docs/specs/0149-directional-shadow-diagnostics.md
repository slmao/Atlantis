# Spec 0149 — Directional Shadow Diagnostics V1

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-28
- **Design baseline:** `35c3f7dc` on `main`, maintainer merge of [PR #460](https://github.com/slmao/Atlantis/pull/460)
- **Related Plan:** [Plan 0149](../plans/0149-directional-shadow-diagnostics.md)
- **Related ADR:** [ADR-0195](../adr/0195-isolated-directional-shadow-diagnostic.md) (Proposed)
- **Approval:** Pending joint review.

## Summary and motivation

At the same saved camera position, compare a normally shaded scene with one
rendered without directional shadows. An artist can check whether the existing
directional shadow map accounts for darkness around a chair leg or below an
awning. [0148](0148-material-lighting-diagnostics.md) separates one surface's
base color from its lighting. This comparison isolates one lighting contribution
across the whole frame.

Runtime currently extracts at most one directional light. Its RenderGraph
shadow pass clears a depth map to 1.0 and draws eligible casters in the
existing light projection. PBR receivers with shadow sampling use this map
to attenuate their directional-light term. The camera's appearance and the
normal frame's other light contributions are independent inputs.
See [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md).

## Requirements and proposed design

### R1 — Compare at a settled bookmark

Add **Compare Directional Shadows** to the [0147](0147-scene-view-bookmarks.md)
bookmark window for a hosted Windows Editor with an open hand-authored scene.
Reuse its confirmed pose, lens and physical extent. Require the default
temporary viewport visibility and an exact document revision represented by
the current settled Apply. Refuse a pending or failed Apply, a replaced
projection, or incomplete live inputs. Ask for a settled Apply and a fresh
return to the bookmark as appropriate. An unapplied document cannot be used
as the basis for a diagnostic of the previous projection.

Protect unsubmitted drafts and active gestures before focus transfer. Verify
the Worker/session, document occurrence and revision, Apply and projection,
bookmark, camera intent, drawable size and presented visibility before send
and when consuming the reply. Compare the reply with the latest
Worker-observed state. Changes retire the request and its results. Repeating
the action requires a current, settled bookmark. Captures create no
authoring history and preserve the document, saved bytes, selection,
active scene camera, drafts and Apply state.

### R2 — Change only directional shadow visibility

Use the [ADR-0195](../adr/0195-isolated-directional-shadow-diagnostic.md)
shared pair. Both overlay-free PNGs use the same applied projection, draw
items and order, camera matrices, extent, realized resources and active
scene camera appearance. Require one extracted directional light with
positive intensity and at least one positive color channel. A missing or
zero-radiance directional light produces an actionable refusal.

**Normal shading** uses the existing shadow pass and caster set.
**Without directional shadows** uses the same pass, freshly clears the
private depth map to 1.0, and submits no caster draws to that pass. Both
images retain directional direct lighting, material and environment
inputs, point lights, exposure, fog, Bloom, and the normal output transform.
The same light-space matrices, depth comparison, bias and receiver shaders
run in both images. With the cleared map the existing receivers see full
directional visibility. Screen-space post processing can carry the resulting
change across the geometric shadow edge, so the pair is an appearance
comparison rather than a pixelwise shadow mask.

Show the two images side by side at equal scale and aspect, with those
labels, the bookmark and capture provenance. Include the applied projection,
extent, directional-light settings and the recorded active-camera exposure
when present. An unchanged region means the current directional shadow map
did not measurably affect that rendered region under these conditions.

### R3 — Keep comparison local to the request

Use a capability-gated operation with the same contract for Editor, CLI,
both SDKs and MCP. Reuse current output, receipt-loss and group lifetime
conventions. A known refusal returns no successful pair. A sent request
with a lost or malformed receipt preserves the transport error category and
reports an unknown result with instructions to check both paths before an
explicit retry. The Editor decodes and displays only a verified complete pair
and releases its temporary images when the comparison is retired.

Share capture admission with Render Looks and Material Diagnostics. Record
the two images sequentially with private resources. The main viewport and
other clients retain their ordinary render inputs and descriptor bindings.
The operation runs only on request and writes only its two requested PNGs.

## Architectural impact

[ADR-0195](../adr/0195-isolated-directional-shadow-diagnostic.md) records the
additive client contract and where capture selects the empty caster set.
Runtime owns scene settlement and temporary resources. Renderer continues to
consume ordinary draw data through its RenderGraph, with the existing RHI
and shaders. The comparison reuses the existing bookmark and scene formats.

## Alternatives and limits

Turning off the directional light also removes its direct illumination.
A shader override would change the receiver programs and their bindings.
The existing depth-only clear provides a control case that uses the same
lighting calculation, receiver shaders and output transform. V1 covers
the current directional shadow map only. Shadow algorithm changes, indirect
lighting changes, per-object controls, new lighting algorithms, automatic
attribution and live viewport toggles are outside this proposal.

## Verification

[Plan 0149](../plans/0149-directional-shadow-diagnostics.md) maps these
requirements to controlled receiver-pixel checks, a Windows 100% Bistro
terrace comparison, shared-client and isolation checks, and the applicable
Vulkan, image and performance gates.
