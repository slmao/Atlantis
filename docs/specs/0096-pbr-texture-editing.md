# Spec: PBR Normal and Emissive Texture Editing

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-14
- **Related Plan:** [Plan 0096](../plans/0096-pbr-texture-editing.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0155](../adr/0155-pbr-material-map-authoring.md) (Proposed)

## Summary

Extend the existing material editor to select, replace and clear a normal
map and an emissive texture on an opaque direct-lit PBR material. Preview
the complete draft on the chosen mesh, save and publish it, and verify the
same material GUID and appearance after restarting and reopening a saved
scene. Editor, CLI, native SDK, C# SDK and MCP share the operation.

## Motivation / Problem Statement

[Spec 0095](0095-texture-import-and-base-color-binding.md) delivered managed
sRGB PNG registration and base-color binding. The material source and
Runtime already support tangent-space normals and emissive sampling. The
0094 authoring recipe and 0092 pair preview currently refuse those maps.
Metallic and roughness are scalar factors in the checked-in shaders and
material format. The glTF importer drops metallic-roughness and occlusion
textures. Editing those two texture roles would first require cooking and
rendering support. See the [material source](../../src/asset_system/include/atlantis/asset_system/material_source.h),
[Runtime realization](../../src/runtime/src/material_realization.cpp)
and [glTF mapping](../../src/tools/gltf_importer/material_import.cpp).

## Goals

- Expose the two existing optional slots, and the emissive factor needed
  to see the emissive texture, through the 0094 authoring workflow.
- Use the retained cooked catalog for identity, dependency validation and
  both draft and published preview.
- Keep material identity, scene state, and the managed publication and
  recovery semantics intact.

## Non-Goals

Metallic/roughness textures, occlusion textures, packed channel conventions,
normal-map strength or channel conversion, new import modes, alpha and PBR
variants outside the scope below, and a node-based editor are deferred.
The existing metallic and roughness sliders remain scalar. This design uses
normal assets already cooked in the catalog, such as the checked-in UNORM
fixtures. Managed PNG registration keeps its 0095 sRGB contract.

## Requirements

### Functional

**R1. Recipe and slot interpretation.** Use the existing
`PbrDirectLit` opaque authoring subset, with base alpha 1 and cutoff 0.5.
Retain the existing general sampler, color factor and scalar surface
controls. The selected Runtime catalog is authoritative. For every named
slot, resolve a non-nil Texture GUID, require a dependency-free record,
and validate the cooked artifact, metadata, GUID/key and texture format
through the bounded CPU loader. Texture names and source paths are labels.

| Slot | Cooked format and sampling | Empty value and defaults |
| --- | --- | --- |
| Base color | RGBA8 sRGB, RGB sampled on UV0 and multiplied by the linear base color factor | Required. Existing 0094 defaults. |
| Normal map | RGBA8 UNORM carrying linear data. RGB is tangent-space XYZ mapped from [0, 1] to [-1, 1] on UV0. Uses the existing +Y convention and cooked tangent handedness. | Optional. Clear selects the existing geometric-normal path. |
| Emissive texture | RGBA8 sRGB, RGB sampled on UV0 and multiplied by the linear emissive RGB factor. | Optional. Clear retains the factor and uses the existing white fallback. The factor defaults to (0, 0, 0) and accepts finite components in [0, 65504], the existing cooker range. Binding a texture leaves the factor as authored. A zero factor gives zero emission. |

Opaque shading gives these slots no alpha control. Limit emissive textures to
sRGB RGBA8 for authoring and preview. The scene renderer also accepts other
cooked emissive formats. Use the material's existing sampler for all slots. Keep per-file and image bounds from 0092
and allow at most three unique textures per preview, with at most 32 MiB of
encoded artifacts per texture and 96 MiB across the set. Metadata retains
the present per-file bound. Base color and emissive may share a GUID.
Resolve, load and stage that texture once per request and keep one unique
catalog dependency edge. The authored slots remain independent.

**R2. Shared authoring and compatibility.** Keep the closed 0094 wire shapes
and add the following methods to `atlantis.remote/1`. Use the current request
ID and outcome validation, including exact keys and canonical GUIDs. Native
and managed clients expose typed equivalents. CLI and MCP surface the same
requests, refusals and job status using the existing output conventions. CLI uses the existing material command shapes with a `-with-maps` suffix. MCP uses the corresponding `_with_maps` tool suffix.

| Method | Contract |
| --- | --- |
| `assets.materialReadWithMaps` | Takes the existing `{material}`. Returns the existing read shape, using the complete recipe below. |
| `assets.materialSaveWithMaps` | Takes the existing Create, Copy, Edit and Resume arms, with complete `values` and, for Edit, complete `expected`. Resume keeps its existing `{mode, material}` shape. Returns the existing correlated material job receipt. |
| `assets.previewMaterialDraftWithMaps` | Takes `{mesh, recipe}` and returns the existing bounded PNG shape with the full recipe echoed. |

Complete values have the four existing keys `texture`, `color`,
`metallic`, `roughness` plus required `normalMap`, `emissiveTexture`
and `emissiveFactor`. Each optional slot is a GUID or JSON null. The
factor is three floats. The recipe keeps the existing `values`, `filter`
and `address` keys. Missing, duplicate or extra fields are malformed.
Create starts with 0094 defaults and empty slots unless set by the caller.
Copy starts from the complete eligible source recipe in Editor, retains
the source sampler and writes the explicitly supplied values to its own
new GUID. Edit compares the selected generation and all original values
and preserves the existing GUID and sampler. Only project-owned materials
can be edited. Read and preview remain available on supported catalogs
without write opt-in.

Continue using `assets.materialStatus`, the same worker slot and receipt
history. Read, Copy, Edit and Resume through 0094 must retain the existing
subset guard. A material with an optional map or a nonzero emissive factor
cannot be projected into a writable 0094 recipe. When 0094 Resume sees an
extended committed recipe, it refuses with the existing `Source/InvalidRecipe`
failure and preserves the source. An existing 0094 job status remains
readable regardless of which method admitted the job.

A new client can expose basic editing on a 0094 Runtime only after a
well-formed new read receives the exact correlated legacy
`BadRequest` / `unknown or malformed asset query`. Use a separate, explicit
basic workflow. Do not replay an extended Save, Resume, or preview as a
legacy operation. A lost or malformed mutation reply keeps its existing
transport/protocol error and settlement guidance. The existing cooked
pair uses the same mesh/material GUID request, response and refusal set.
An older client can therefore request a supported cooked pair without
understanding the extended authoring recipe.

**R3. Preview and Editor.** Extend the existing offscreen pair path for
cooked and draft inputs, selecting the current direct-lit shader variant
with or without normal mapping. Use existing fixed camera, light, exposure,
PNG limits and tangent-space mesh layout. A normal map requires a valid
cooked tangent basis on the preview mesh. Use the shared between-frame
admission permit, RenderGraph upload/readback and terminal drain. Keep
textures, uploads, pipelines and descriptors local to the request until
safe release. Report incompatible pairs and typed dependency or size
failures without returning an image. The exact recipe, mesh, request and
surface lifetime guard late results, including change or Clear while a
request is running. Cooked pair preview remains read-only and its success
does not certify scene apply compatibility.

In the 0093-style material modal, group base texture, Normal map and
Emissive texture with their selectors. Offer explicit Clear for optional
slots and label the linear emissive RGB factor and zero-emission state.
Continue using `assets.list/get` to populate choices. Runtime decides role
eligibility from cooked metadata. Preserve the draft and focus context when
the user corrects a failed dependency or switches the preview mesh.
Failing preview clears an obsolete image and leaves a diagnostic. It does
not block Save solely because the chosen preview mesh was incompatible.
The save validates the recipe and its dependencies separately. A missing,
corrupt or wrongly formatted texture must fail before source commit. Keep
the existing refusal categories and identify the invalid slot in the
save job's bounded diagnostic.

**R4. Publication, isolation and recovery.** Use the existing 0094 managed
material producer, ordinary material source, exact frozen source and
catalog generation. Before source commit and publication, verify that
cooking preserved every role and factor, the material dependency set is
the unique set of named GUIDs, and unrelated identities and edges are
unchanged. Clear removes an edge when no other slot still uses it.
Source, generation and workspace lock remain under the existing worker.
Keep 0094's known-uncommitted, committed, confirmed publication and
unknown settlement rules, including workspace repair and explicit Resume
for the same material. Resolve the committed recipe for Resume. Retain the
job ID when known and the operation target when the admission is unknown.
A loss of transport, generation conflict, or preview failure never causes
an automatic mutation replay. After a stale edit the user rereads the
active recipe before retrying. Repair or known committed source takes
precedence over another Save.

A saved material does not change the active catalog, World, current
authoring document or scene undo history. Explicit restart selects the
published generation. Independent Saved Scenes build and open must read
the same recipe and edges from committed sources without a generation
override. Existing asset files and generation metadata versions are reused.
An older executable can use compatible earlier selections. Client
compatibility does not promise that an older executable can activate
a generation whose managed recipe is outside its authoring subset.

### Non-functional

The portable asset loaders stay CPU-only. Persistent authoring uses the
Windows opt-in worker. Reuse the current Vulkan shader variants, descriptor
shape and RenderGraph path. Additional texture load and upload work occurs
only on explicit preview or asset work. Use the existing frame loop,
artifact formats, dependencies and Editor visual resources.

## Proposed Design

Expand the existing material recipe internally and route both wire shapes
through the same worker and preview path. The existing texture catalog
supplies sRGB and UNORM assets, and the scene loader continues using the
same material format. Keep material controls in the existing Assets modal
and retain the Inspector material reference chooser's confirmation semantics
in place.

## Architectural Impact

[ADR-0155](../adr/0155-pbr-material-map-authoring.md) records the parallel
closed wire shape and the extension of the existing offscreen material
recipe. It partially supersedes the one-texture preview restriction of
ADR-0152 and extends ADR-0153.

## Alternatives Considered

See [ADR-0155](../adr/0155-pbr-material-map-authoring.md#alternatives-considered).

## Testing & Verification Plan

[Plan 0096](../plans/0096-pbr-texture-editing.md#verification-checklist)
covers recipe validation, version compatibility, offscreen rendering,
publication recovery and the actual Windows 100% Editor workflow.

## Risks & Open Questions

J1–J4 in the Plan cover the supported slots and source workflow, the shared
contract, preview resource bounds and acceptance. The basic 0095 PNG importer
supplies color textures. Authoring a new external normal image requires a
separate decision on linear-data registration and remains outside this slice.

## Out of Scope / Future Work

If metallic/roughness and occlusion texture support is prioritized, review
the data format, packed-channel defaults, importer, renderer and rendering
acceptance together before adding Editor controls.
