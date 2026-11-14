# Spec: Occlusion Texture Import and Binding

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0099](../plans/0099-occlusion-texture.md)
- **Approval:** Pending joint human review
- **Related ADR:** [ADR-0158](../adr/0158-occlusion-texture.md) (Proposed)

## Summary

Import a standalone linear PNG as an occlusion texture, bind it to an
eligible material, preview the result and save and publish through the
shared asset workflow. Verify activation and two independent reopens
without editing the saved material reference.

## Motivation / Problem Statement

The [glTF importer](../../src/tools/gltf_importer/material_import.cpp)
reports occlusion textures among the dropped inputs. Atlantis has no
occlusion material slot today. The
[IBL shader](../../shaders/pbr_ibl/pbr_ibl.slang) already computes diffuse
irradiance and specular environment lighting separately from direct
lighting and emission. That path can consume a linear red-channel
occlusion multiplier with the semantics of
[glTF 2.0 Material Occlusion](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#additional-textures).
The direct-lit path has no indirect contribution to attenuate.
[Plan 0098](../plans/0098-metallic-roughness-texture.md) and its
[implementation](https://github.com/slmao/Atlantis/pull/362) provide the
linear import, shared material edit and publication boundaries.

## Goals

- Import and discover a managed Occlusion texture with stable identity.
- Select, replace, clear and preview it in the existing material editor.
- Publish and reopen the same material reference through existing Runtime
  and Saved Scenes launch flows.
- Preserve the earlier registered roles and compatible client, artifact
  and launch paths.

## Non-Goals

Packed ORM, occlusion strength controls, glTF material import changes,
channel extraction, UV sets beyond UV0, coexistence with normal or packed
metallic–roughness maps in an occluded material, masked or blended
materials, SSAO, probe changes and node editing are outside this slice.

## Requirements

### R1. Linear registration and frozen views

Use the bounded 0097/0098 RGB/RGBA 8-bit PNG path. Preserve RGB samples and
source alpha, fill omitted alpha with 255, ignore color tags and cook one
mip of RGBA8 UNORM. Use `inbox/<name>/source.png` and reserve the
canonical leaf `registered/texture_<name>/occlusion.png` and catalog key
`content:texture_<name>/occlusion.png`. Derive the role from the authoritative
producer and catalog key. Share the ordinary Texture GUID, derived-key and
name collision checks across all managed roles. The full key, role, GUID
and source digest remain stable across Resume and fresh builds. Accept
one canonical PNG leaf with the existing seal and registration fragment.
Discover
published textures through `assets.list/get`. Targeted managed texture
rebuild remains unsupported.

Emit private generation manifest v7 only for a frozen view containing
Occlusion. Keep v6's entry shape and extend the required `role` enum to
`BaseColor`, `Normal`, `MetallicRoughness` and `Occlusion`. Derive role from
the exact canonical key. Require `Srgb` for BaseColor and `Unorm` for all
three linear roles. Recompute the PNG, artifact and sidecar digests and
check exact membership and agreement with the producer and catalog.
Preserve v1–v6 byte formats for compatible views. An older builder
rejects the new registered leaf and an older Runtime rejects v7. The
supporting Runtime can still read a compatible prior generation against
its own frozen view. Check workspace write readiness separately.

Reuse the shared worker, workspace lock, exact catalog delta, original
Register-fragment and producer-header checks, and the two rename
boundaries. Under the existing commit lock, compare the complete producer
header and frozen view, catalog delta, texture evidence and material bytes,
cooked recipe and dependency edges with the intended build. Re-read the
private Register fragment byte for byte before its rename. For either
texture or material Resume, check the previously committed source and the
prepared output before publishing. Resume performs no source rename.
After an uncertain publication rename, accept a generation receipt only
if the prepared path is gone and the destination passes the same check.
A precommit mismatch leaves registered source, catalog and publication
unchanged. A destination mismatch after source commit yields publication
failure with the verified committed identity, no generation receipt and
the existing repair disposition.

### R2. Material and lighting

Require the existing opaque authoring subset of `PbrDirectLit` with
base alpha 1 and cutoff 0.5. AO may coexist with base-color, emissive
and existing scalar fields. When AO is bound, require both the normal and
metallic–roughness slots to be empty. Select the same UV0 and material
sampling as other maps. Multiply `diffuseIBL + specularIBL` by linear R
before adding it to the direct light. Keep emission, fog and output
conversion in their existing order. Strength is fixed at 1. R=0
suppresses the IBL terms and R=1 leaves them unchanged. G/B/A are ignored.
Direct lights, direct shadows, alpha and sky keep their existing behavior.
Clearing removes the slot and restores the existing unoccluded path.
With no configured IBL, keep the existing direct-lit path and validate
the dependency without binding or sampling an AO image.

A bound slot requires a non-nil GUID resolving to a canonical managed
Occlusion Texture with no dependencies, RGBA8 UNORM and a single mip.
Refuse this role in the base-color, emissive, normal and metallic–roughness
slots, including older recipe methods on the new Runtime. Enforce recipe and
role eligibility in shared authoring, cooker, scene load and preview, including
texture-cache hits. Preserve material GUID, other values and unrelated
edges. Replace or remove the old AO edge as appropriate and emit one unique
edge for a bound slot. Carry that reference through the existing texture
lifetime accounting when an applied scene reload reuses a live material.

A bound slot emits material source v12 with final `occlusion_map_asset`
GUID, a 132-byte artifact v11 with one little-endian AO AssetId appended
at offset 124 to the v9 payload layout, and metadata v11 with final
`occlusion_map_asset_id`. Require a matching artifact/metadata pair and
exact byte length. Resolve the nonzero key to the intended non-nil GUID
and catalog edge. Record the actual source and artifact schema versions
at cook time and check the declared artifact version at load time. Keep
the exact 0098 v11/v10/v10 triple when only the packed map is bound, and
the v10/v9/v9 triple without either slot. Reject mixed
versions and unsupported schema through existing loader errors. Keep
scene, catalog and texture artifact formats.

Use one opt-in IBL fragment permutation with a sampled texture at set 0
binding 6 and the existing material sampler. Normal-mapped IBL would
need a seventh sampled binding for AO. The packed path would first need
IBL support in addition to another permutation. The base IBL case fits
the existing six sampled bindings. Keep existing IBL permutations,
push constants and RHI binding limits. Use the existing RenderGraph
resource and upload path. Do not add checks to the frame loop for
textures or tasks that are absent.

### R3. Shared contracts and recovery

Keep all previous closed payloads. Add matching Connection, Remote,
CLI, native/C# SDK and MCP surfaces. CLI commands use kebab case and
MCP tools use underscore names. Proposed wire additions follow.

| Method | Shape |
| --- | --- |
| `assets.occlusionImport` | Existing bounded texture `{name,mode}` admission and job receipt. The method fixes the role. |
| `assets.occlusionImportStatus` | Existing texture `{activeGeneration,job,workspaceNeedsRepair}`, with Occlusion job filtering. |
| `assets.materialReadWithOcclusion` | Existing material read shape with a complete recipe. |
| `assets.materialSaveWithOcclusion` | Existing Create, Copy, Edit and Resume arms. Use complete Edit `expected` and the existing `assets.materialStatus`. |
| `assets.previewMaterialDraftWithOcclusion` | Existing bounded draft PNG shape, echoing the complete recipe. |

The complete recipe is the exact 0098 recipe plus a required
`occlusionMap`, a canonical non-nil GUID or null. Use it in all complete
value arms and the Edit expectation. Retain the Edit active-generation
check and existing project-ownership rules. Copy validates its source
using the caller's recipe version. A new draft reply must match the requested mesh
and complete recipe. Keep the cooked `assets.previewMeshMaterial` wire
shape. For new methods, map only the correlated outer `BadRequest` with
message `unknown or malformed asset query` to `Unsupported`. Require the
exact envelope with one matching ID, the error arm and the exact error
members `code` and `message`. Supporting peers without write permission
reply `Unavailable`. Malformed responses and transport failures retain
existing categories.
An unknown write result retains known target and receipt information and
offers same-role status or material status. Without an ID, a latest job is
only a lead until its target and receipt can be reconciled. A missing or
evicted receipt leaves the result unknown. Require explicit reconciliation
before retrying a write.

On the new Runtime, every older Read, Copy and Edit refuses a recipe with
unsupported fields before projection or staging. Read returns
`UnsupportedMaterial` and accepted Copy/Edit jobs fail at
`Source/InvalidRecipe`. An older Read can still show its compatible
active view while the registry is ahead. The existing ahead guard blocks
writes based on that view. Legacy Resume encountering
an unrepresentable committed source returns `Source/InvalidRecipe` with
the committed state and GUID, and preserves bytes. A compatible Resume
can publish without another source edit. An explicit older workflow
remains available on a connection with the precise capability refusal.
Re-evaluate support on reconnect. An original or draft with AO bound
must use the complete write shape, even when the proposed edit clears AO.

Occlusion joins the shared worker and the same 16 terminal receipts.
Latest is role-scoped, unknown/wrong-role/evicted IDs return
`UnknownJob` and repair state is workspace-wide. After permission,
lock and repair checks, missing, already-active or wrong-role targets
return `NothingToResume`. A sole ahead Occlusion registration can
Resume from its original registered PNG, seal and fragment even if the
inbox file is absent. Require all other active registrations to match the
committed registry. Another ahead texture, mesh import or material change
returns `ActivationRequired`.
Distinguish precommit refusal, committed source with publish failure,
confirmed publication and unknown outcome. Preserve the committed GUID
and bytes after confirmed source commit, and use the current explicit
activation and workspace repair guidance.

### R4. Editor, preview and reopen

Add the Occlusion choice to the existing 0093 Assets texture import
dialog and a slot in the existing material dialog. The new choice uses
current Texture entries with exact canonical Occlusion keys. Exclude such
entries from the other slots. The service validates compatibility again.
Explain linear R, IBL-only effect and conflicting slots. Keep Replace,
Clear, existing factors and a failed draft. A conflicting normal or
packed map must be explicitly cleared before AO can be bound. With AO
bound, require explicit AO clear before assigning either of those maps.
Track texture receipts by role across UI switches and keep existing shared
write blocking, recovery and restart guidance.

Keep the captured mesh, fixed camera, existing direct light and PNG
bounds. AO-bound cooked pairs and complete AO-compatible draft requests,
including null AO for a matched comparison, use the existing baked
`ibl_studio` resource and its irradiance/specular/DFG inputs as fixed
preview lighting. A complete draft with normal or packed maps and null
AO uses its existing lighting. Legacy draft methods and unbound cooked
pairs keep their prior lighting. The dialog identifies the fixed studio
lighting. The server keeps the calling recipe version when sharing draft
preview code, so legacy calls keep their prior mode. Studio preview keeps
the existing background, fog, exposure and direct light. It draws no sky.
Read only the build-configured `ibl_studio` artifact and sidecar, even when
the live scene has no IBL. Reuse the current studio bake size (256-pixel
faces, nine mips and 128 × 128 DFG). Cap the artifact read at the exact
size implied by the current layout, including header and SH, and the
sidecar at 64 KiB before decoding. Validate their paired identity and
layout. Report missing or oversized inputs or pre-submit realization
failure via the existing preview error path. Preview inputs remain the
mesh and material identity or the complete draft recipe.

Keep 0092's one permit per Remote poll plus following `--exec` step. The
first well-formed request to a usable provider consumes it before catalog
or file work, even if it fails or disconnects. Malformed requests and
unavailable providers take no permit. Later eligible requests return
`Busy` for that interval. Resolve mesh, material and maps
from one retained catalog. Reuse the existing mesh and map byte limits.
Use request-owned IBL inputs and mutable descriptor resources. Record
uploads, draw and readback on existing RenderGraph paths in the same
between-frame submission. Release scratch on recoverable refusal and
release submitted resources only after confirmed completion. On fatal
submit or completion failure or device loss return `Unavailable` and
retain the full bundle, including studio staging, through the existing
terminal drain. Preview preserves live lighting, scene resource caches
and viewport state. Accept a completion only for the current connection,
snapshot, surface, mesh and selected material or draft revision. Invalidate
the old image when any input changes.

Save & Publish leaves the active Runtime catalog, projection, current
document and undo history intact. In an isolated checkout, use the
`ibl_material_demo` scene with the managed material reference established
before seed and staging configuration. Enable the existing Save As
permission and local 0090 launch settings. After restarting on the texture
generation, Save As a copy before editing the material. Publish one
material edit. Reopen the original scene on the named generation using
its ordinary scene selection and build profile.
From the ordinary Editor, explicitly select the saved copy and the same
committed managed import root in [0090](0090-saved-scene-build-open.md).
Build/Open it in a second Editor. The saved slot consumes committed
sources without a build profile or generation override. Both legs retain
the node/material reference and show the same committed AO effect without
a scene write, a rebind or Apply. Keep the inputs fixed between publication
and the second build.

## Proposed Design

Keep asset admission, source commit, generation publication and Runtime
activation at their existing boundaries. The registered key supplies the
new role. The material recipe and catalog edge carry it into the existing
lighting path. The Editor, CLI, SDKs and MCP use the same operations and
outcomes.

## Architectural Impact

[ADR-0158](../adr/0158-occlusion-texture.md) records the new role, compatible
format emission, shared versioned recipes and the IBL descriptor and
preview scope. Asset System keeps its dependency on Core. Renderer and
RenderGraph ownership remain unchanged.

## Alternatives Considered

See [ADR-0158](../adr/0158-occlusion-texture.md#alternatives-considered).

## Testing & Verification Plan

Follow the [Plan 0099 checks](../plans/0099-occlusion-texture.md#verification-checklist).
Retain Vulkan validation, existing goldens and the applicable performance
protocol. Verify Windows Editor at 100% DPI.

## Risks & Open Questions

A correctly encoded PNG may still use another channel convention. The
shared role and UI instructions identify R explicitly. Preview and
Runtime may use different environments, so comparison tests control the
lighting in each path. J1–J4 in Plan 0099 are pending.

## Out of Scope / Future Work

A follow-up can evaluate packed ORM, occlusion strength and coexistence
with normal and metallic–roughness maps. Coexistence requires a separate
review of descriptor and shader permutations.
