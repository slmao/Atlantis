# Spec 0151 — Normal and Roughness Diagnostics V1

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-28
- **Design baseline:** `225317bf` on `main`, maintainer merge of [PR #464](https://github.com/slmao/Atlantis/pull/464)
- **Related Plan:** [Plan 0151](../plans/0151-normal-roughness-diagnostics.md)
- **Related ADR:** [ADR-0197](../adr/0197-isolated-normal-roughness-diagnostic-capture.md) (Proposed)
- **Approval:** Pending joint Spec/Plan review

## Summary and motivation

Compare the normal scene image, the normal vector used for lighting, and the
final material roughness at one saved camera position. On Bistro paving or a
tabletop this helps distinguish variation in the shading normal and roughness
from the appearance produced by lighting and post processing. The existing
base-color comparison exposes neither input. A texture preview omits the
scene's world transform, tangent basis, material factor and actual mip sample.

## Requirements and proposed design

### R1 — Locate one supported surface at a saved viewpoint

Add **Compare Normal & Roughness** beside the existing base-color action for
a unique [0138 Locate Material](0138-viewport-material-slot.md) result at a
settled [0147 bookmark](0147-scene-view-bookmarks.md). Reuse 0148's scope:
a Windows hosted Editor, a hand-authored scene, default temporary viewport
visibility, and one opaque `PbrDirectLit` Renderable with an explicit
`Renderable.materialAsset` binding. Use its realized DirectLit or IBL variant,
including supported normal-map, packed metallic/roughness and AO combinations.
Require one exact settled Apply for the current document. A valid hit must
resolve the reviewed entity, mesh and material GUIDs. Ambiguity, an
unsupported material, unresolved inputs or a stale binding produces a
diagnostic and no comparison. Never locate a farther object as a substitute.

Reuse the existing draft/gesture, bookmark, drawable, viewport-policy and
request retirement guards. Reject outdated UI results using the latest
Worker-observed projection and intent before showing any image. A change
during the request leaves the current selection and draft in place and asks
the user to return to the bookmark and pick again.

### R2 — Define the three images

| Image | Display |
| --- | --- |
| **Normal shading** | Entire scene at the bookmark's pose, lens and physical extent. Keep the settled scene's direct lighting, shadows, environment, and active camera exposure, fog and Bloom. |
| **Shading normal · world XYZ** | Show the normalized fragment normal consumed by ordinary PBR lighting. Map world X, Y and Z to R, G and B respectively with `q = clamp(0.5 × N + 0.5, 0, 1)` componentwise. Diagnostic PNG bytes are the UNORM quantization of `q`; approximately 128 represents a zero component. Supply an axis legend and keep colors invariant to view orientation for the same world direction. |
| **Roughness · linear 0–1** | Show the PBR roughness scalar as grayscale. With a bound packed metallic/roughness map, compute `r = clamp(roughnessFactor, 0, 1) × sampledLinearG`. With no map, G's multiplier is 1 and the factor alone controls the result. An omitted factor defaults to 1.0. Write `r` to all three UNORM channels. Black is 0 and white is 1. This value precedes squaring and the BRDF alpha floor. |

Use the normalized interpolated world normal when no normal map is bound,
with the existing cofactor transform for nonuniform scale. With a map, use
the actual filtered linear RGB sample, remapped by `2 × sample − 1`, and
the same interpolated world tangent, fragment orthogonalization and cooked
tangent handedness as the production shader. Follow the existing +Y
convention with no channel flip. Use the realized UVs, samplers, color
space and mip selection for both maps. Alpha, packed R/A, the metallic
factor and packed B do not drive roughness. A declared but unresolved map
is an error. The unbound-map rules apply only to absent bindings.

Both data views isolate the located Renderable at its actual transform and
use the same camera, raster/depth rules and self-occlusion as the regular
draw. Other objects can occlude it in the normal image, so compare interior
pixels visible in all three views. Diagnostic coverage is opaque and
uncovered pixels are transparent, shown over a neutral background in the
Editor. Data PNGs have no sRGB conversion, lighting, exposure, fog, Bloom
or tone mapping. Display all three images at equal scale with aspect ratio
preserved. Keep the slot, entity/mesh/material identities, optional bound
normal/packed map GUIDs, roughness factor, applied revision/generation and
capture extent visible or accessible beside the images.

### R3 — Read one projection through a shared, isolated capture

Use [ADR-0197](../adr/0197-isolated-normal-roughness-diagnostic-capture.md)
to deliver the three images as one bounded, opt-in capture for Editor, CLI,
native and C# SDKs, and MCP. All images share one verified settled projection,
binding and camera. Failure yields no complete comparison. A lost receipt
preserves the transport error and reports an unknown result with instructions
to check the requested outputs before an explicit retry.

Diagnostic actions issue no edit, Apply, save, asset publication, camera
override or history entry. They leave ordinary rendering and other clients'
captures at their existing settings. PNG output is the only requested write.
Work runs on explicit demand with the existing capture admission and GPU
completion rules.

## Architectural impact

ADR-0197 records the additive shared capture contract, the diagnostic shader
inputs and the resource bounds across three images. Reuse the 0138 binding
and 0148 capture isolation, 0147 extent, and 0146 descriptor ownership.
Runtime resolves frozen resources and Renderer records through RenderGraph
with RHI. There are no scene or asset schema changes, new dependencies or
new render algorithms.

## Alternatives and limits

An object- or tangent-space display can help inspect a texture but omits the
world direction submitted to lighting. Using BRDF alpha would obscure the
material roughness scalar under the square and floor. A full-scene or live
debug view needs broader material support and puts work on the normal frame
path. V1 limits the comparison to one opaque slot. Base color retains its
existing action. Masked and blended geometry, other material kinds,
standalone map views, and new lighting or post-processing effects are
outside this proposal.

## Testing and verification

[Plan 0151](../plans/0151-normal-roughness-diagnostics.md) maps R1–R3 to
analytic pixel checks, Windows 100% controlled and Bistro captures,
compatibility and resource isolation, and the applicable final gates.
