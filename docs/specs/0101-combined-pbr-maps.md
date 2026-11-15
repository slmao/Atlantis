# Spec: Combined Occlusion, Normal and Metallic–Roughness Maps

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0101](../plans/0101-combined-pbr-maps.md)
- **Approval:** Pending
- **Related ADR:** [ADR-0159](../adr/0159-combined-pbr-maps.md) (Proposed)

## Summary

Allow an opaque `PbrDirectLit` material to bind its occlusion, normal
and metallic–roughness textures independently. Preview, save and
publish one complete recipe through the shared asset interface. Reopen
a scene using the published material through both the ordinary Runtime
and the independent Saved Scenes entry point.

## Motivation / Problem Statement

[Spec 0099](0099-occlusion-texture.md) uses a sixth sampler for AO, while
normal-mapped IBL already uses six. [Spec 0098](0098-metallic-roughness-texture.md)
routes packed metallic–roughness through direct lighting. Runtime
realization, the Renderer and the material artifact also use a shared
surface-map slot for AO or metallic–roughness. The mutual exclusion is
enforced in the material formats, shared clients and Editor.

## Goals and Non-Goals

Cover AO plus normal, AO plus metallic–roughness, and all three maps in
one opaque material. Preserve the existing project-owned material
workflow and previously supported recipes. Keep separate texture GUIDs
and existing import roles. Combined-channel ORM assets, new texture
import formats, glTF importer changes, other material kinds, alpha mask
and blend, new material parameters and live generation activation are
out of scope.

## Requirements

### R1. Bindings and persisted material

Bind, replace and clear each slot independently. Keep the existing
UV0, material sampler, RGBA8 UNORM compatibility, tangent-space +Y
normal convention and canonical role checks for managed AO and
metallic–roughness. The base and emissive slots keep their existing
sRGB rules and factors. The same color GUID can still serve both
slots. Persist each slot separately and keep exactly one edge per
referenced GUID in the cooked catalog. Clear removes the edge only
when no slot still uses its GUID. Edit retains the material GUID
and all unrelated recipe fields.

A coexistence recipe is one with AO and at least one of the normal or
metallic–roughness slots bound. Emit the distinct material formats in
[ADR-0159](../adr/0159-combined-pbr-maps.md#decision) for coexistence only.
Emit the canonical existing format for every other recipe, including
after a clear that leaves the coexistence subset. The cooker, source,
artifact, sidecar and catalog schema declarations must agree.

### R2. Rendering and clear behavior

Under IBL, use the existing normal reconstruction and the packed G
roughness and B metallic channels multiplied by the corresponding
scalar factors. The combined IBL path uses those material values for
both direct and indirect shading. Within the same path, AO linear R at
strength 1 attenuates only diffuse plus specular IBL. It leaves direct
lighting, shadows and emission independent of R. When the scene has
no configured IBL, use the existing direct path for the recipe's
normal and metallic–roughness inputs and keep the AO reference.
Invalid configured IBL follows the existing loading error path.

Preserve the routing of old recipes. Clearing AO while metallic–roughness
is bound returns to its existing direct-lit path, even in an IBL scene.
Explain this transition in the Editor. Compare R=255 with the same
combined IBL path to test neutral occlusion. For AO plus normal with no
metallic–roughness map, Clear restores the ordinary normal-mapped IBL
path when the scene supplies IBL.

Resolve and bind both data textures independently on the existing
RenderGraph path. Use at most eight combined image samplers for the full
IBL combination, following the layout and lifetime decision in ADR-0159.

### R3. Shared compatibility and publication

Editor, CLI, both SDK surfaces and MCP use the additive exact Read,
Save and Draft Preview operations in ADR-0159. Reuse catalog discovery,
texture import/status, material status and the mesh/material GUID pair
preview request. Validate both proposed values and the full original
recipe for Edit and Copy. Resume validates the committed recipe. Older
operations on a supporting Runtime keep their current representable
subsets and refuse coexistence before mutation. Clearing a coexistence
binding still requires the new Save method if the original recipe uses
coexistence. A compatible active read may succeed while the registry is
ahead. Preserve the existing stale generation, expected-value and
ahead write guards. Discover support with Read and allow a representable
legacy workflow only after the exact correlated unknown-method refusal
for a well-formed request. Re-evaluate capability on reconnect. Writes
remain explicit and never downgrade or replay automatically.

Retain the existing writer, job and activation boundaries. Before
committing or accepting an uncertain publication destination, validate
the frozen producer inventory, source bytes, complete recipe and all
texture evidence and dependency edges. A precommit mismatch leaves the
registry unchanged. After confirmed source commit, Resume uses the
committed recipe, bytes and GUID. Retain the target and any known job ID
on an unknown outcome. A latest receipt is only a lead until correlated
to that attempt. A lost, mismatched or evicted receipt leaves the
outcome unknown until settlement and durable state are reconciled.
Publication leaves the current catalog, scene projection, document and
undo history intact until explicit restart. Verify an older Runtime
refuses activation of the coexistence generation while retaining the
ability to use a compatible older snapshot.

### R4. Editor and combination preview

On a supporting session, expose the three slots together in the
existing 0093 material dialog. Keep filters, source details, GUID-based
selection, draft state and controls for select, replace and clear.
Retain slot-specific validation, focus and the existing failure and
activation guidance. On a legacy session the existing workflow and
restrictions continue.

For an AO-bound cooked pair or combined draft, use the bounded fixed
0099 studio IBL, independently of the live scene. For AO-free input,
keep the existing cooked routing and use the WithOcclusion draft
lighting rules on the new method. Keep earlier methods' lighting.
Reuse the 0099 permit shared with geometry and cooked pair previews.
The first eligible request consumes it before catalog or file work even
if that work fails. Resolve every input from one retained catalog.
Keep the existing per-input byte bounds and PNG bounds. Use
request-owned mutable textures, descriptors and staging, releasing
all pre-submit scratch on refusal and retaining the submitted bundle
through confirmed completion or terminal drain. A completion must
match the current connection, snapshot, surface, mesh, and material or
full draft revision and method version. A changed input invalidates the
old image immediately. Preview leaves the live scene, caches and history
isolated.

### R5. Named generation and independent reopen

Use one opted-in Windows project and a saved IBL fixture that already
references a managed material. Prepare both the ordinary scene and a
Save As copy before editing the material. Activate the imported textures,
then publish one edit that adds AO to an existing normal and
metallic–roughness binding. Keep the committed inputs fixed between
publication and the independent build.

Reopen the ordinary scene with its build profile and named generation.
From that Editor choose the saved copy and the same committed managed
root in [0090 Saved Scenes](0090-saved-scene-build-open.md). Explicitly
Build and Open in a second Editor. The child launches with
`--scene saved_scene` and cooks the committed inputs through the saved
slot without generation or profile override. Both sessions show the
same material effect with matched camera, IBL and output size. Confirm
both actually use the configured environment. Keep distinct scene
identities, unchanged scene source hashes, stable node and material
references and the same texture GUIDs and complete material recipe.
The ordinary Editor stays attached to its named-generation Runtime
while the second Editor runs. Neither reopening needs a rebind or Apply.

## Architectural Impact

[ADR-0159](../adr/0159-combined-pbr-maps.md) supplements ADR-0157 and
ADR-0158 for coexistence. It records the material format and shared
contract revision, descriptor budget and preview routing. The Asset
System retains its module boundary. Runtime owns realization and
RenderGraph submission. Editor and other clients use the shared surface.

## Alternatives Considered

See [ADR-0159](../adr/0159-combined-pbr-maps.md#alternatives-considered)
for the packing, routing and compatibility trade-offs.

## Testing & Verification Plan

[Plan 0101](../plans/0101-combined-pbr-maps.md#verification-checklist)
maps R1–R5 to codec and client tests, fixed-input image checks,
Windows 100% UI and both reopen paths. Keep Vulkan validation, impacted
goldens, applicable packaging and the existing performance protocol.
