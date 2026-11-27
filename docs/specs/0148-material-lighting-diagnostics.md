# Spec 0148 — Material and Lighting Diagnostics V1

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-28
- **Design baseline:** `4a6dedde` on `main`, maintainer merge of [PR #458](https://github.com/slmao/Atlantis/pull/458)
- **Related Plan:** [Plan 0148](../plans/0148-material-lighting-diagnostics.md)
- **Related ADR:** [ADR-0194](../adr/0194-isolated-base-color-diagnostic-capture.md) (Proposed)
- **Approval:** Pending joint review.

## Summary and motivation

At one saved camera position, compare the normal scene with the effective
base color of a clicked surface. On Bistro paving or a wooden tabletop,
this shows whether the sampled material already contains the seam or grain
visible in the normally shaded image. An ordinary capture includes lighting
and post processing. The mesh/material preview uses a separate studio setup.
Neither supplies the base color of the located scene surface in its original
composition.

## Requirements and proposed design

### R1 — Locate one supported binding at a bookmark

Reuse the exact camera, lens and physical extent of a settled [0147
bookmark](0147-scene-view-bookmarks.md) in a Windows hosted Editor with a
hand-authored scene. After [Locate Material](0138-viewport-material-slot.md)
finds a unique surface, offer **Compare Base Color** in the existing material
binding view. The owner is one Renderable and its explicit
`Renderable.materialAsset` binding. Display the node, slot and effective
material identity. Limit V1 to opaque `PbrDirectLit`, including its existing
texture variants. Ambiguous or unsupported hits use 0138's refusal and do
not choose a farther binding. The Editor requires default temporary viewport
visibility and an exact document revision on a settled Apply projection.
Ask the user to finish a draft or gesture before a focus transfer could
submit or discard it. Starting the comparison does not change selection.

Recheck the located occurrence, mesh and material, camera intent, drawable,
bookmark, document and projection before send. On invalidation of the
reviewed target or an in-flight result, keep the current Editor state and
prompt the user to return and pick again. At consumption, compare with the
latest Worker-observed projection in addition to the captured request
identity. Late replies never display in another comparison. Captures are
explicit and temporary. An unknown result provides a check-and-retry
diagnostic and no automatic retry.

### R2 — Compare normal shading with effective base color

Capture two overlay-free images at the exact bookmark camera, perspective
and physical extent. Both use one reviewed Runtime projection and the same
target geometry, transform and resource binding. Display the images side by
side with equal scale and aspect preservation. Keep slot, material, texture
identity, RGB factor, projection generation, extent and render conditions
available beside the pair.

| Image | What it displays |
| --- | --- |
| **Normal shading** | Entire scene, through ordinary rendering and output transform. Use current applied scene lighting, shadows, environment and the active scene camera's actual exposure, fog and Bloom. Use the bookmark for observation pose and lens. |
| **Base color · isolated surface** | Draw only the located Renderable at its scene transform with its existing UVs, sampler and mip sampling. Show `clamp(linear texture RGB × linear baseColorFactor RGB, 0, 1)`, then apply only the sRGB transfer once for display. Covered pixels are opaque. Outside the mesh coverage the PNG is transparent and the comparison shows a neutral background. Keep geometric self-occlusion and the existing face-culling convention. Other objects do not occlude this image. |

Use the cooked base-color sample actually bound to the material, including
the declared sRGB decode. The Runtime requires a resolved texture. A glTF
material with no source color map already binds the importer's generated
white texture. That binding displays the factor and retains its texture
GUID. Missing or unresolved required inputs produce a refusal.
The diagnostic applies no lighting, IBL, AO, normal map, metal/roughness,
emissive, shadow contribution, exposure, fog, Bloom or tone mapping. It
shows effective base color at this scale and mip level. It does not assess
the raw source image, and the diagnostic silhouette can extend behind other
objects visible in the normal image.

### R3 — Shared capture and read-only operation

Use the shared [ADR-0194](../adr/0194-isolated-base-color-diagnostic-capture.md)
capture for Editor, CLI, C++ and C# SDKs, and MCP. Resolve the explicit
binding against both the applied document and the Runtime's live projection
and catalog. Refuse stale or unsupported input and publish a pair only after
both images succeed. No diagnostic action writes material or scene content,
changes the active camera or viewport overrides, issues Apply, publishes
assets, or records Undo/Redo. The operation writes only its requested PNG
outputs. Ordinary viewport rendering and other clients' images retain their
own settings. The diagnostic adds no per-frame pass or polling.

## Architectural impact

ADR-0194 records the additive shared request and the capture-only RenderGraph
path. Reuse the [0146](0146-render-look-capture-descriptors.md) private
descriptor and completion rules and the 0147 extent envelope. The Editor
owns the comparison and the existing bookmark and binding intent. Runtime
owns projection and resolved resources. Renderer receives draw data and
targets through RHI without a scene, catalog, Platform or Editor dependency.

## Alternatives and limits

Turning lights off still leaves output processing and non-lighting material
contributions. Reimporting a texture preview loses the scene UVs and factor.
A full-scene diagnostic or a live debug mode would add broader material and
coverage rules. V1 uses an isolated opaque binding and a normal scene image
from the same view. Normal, roughness and shadow diagnostics, masked and
transparent targets, full-scene false color, material authoring changes and
new lighting or post-processing algorithms remain outside V1.

## Testing and verification

[Plan 0148](../plans/0148-material-lighting-diagnostics.md) maps R1–R3 to
Windows 100% Bistro acceptance, controlled color and coverage tests, shared
client conformance, and the applicable Vulkan, golden and performance gates.
