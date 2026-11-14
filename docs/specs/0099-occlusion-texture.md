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
and source digest remain stable across Resume and fresh builds. Discover
published textures through `assets.list/get`. Targeted managed texture
rebuild remains unsupported.

Emit frozen producer manifest v7 only for a view containing Occlusion.
Require the existing digest and membership checks, declared color space
and exactly one of `BaseColor`, `Normal`, `MetallicRoughness` or
`Occlusion` per entry. Validate the whole frozen view against the producer,
catalog, cooked artifact and sidecar. Occlusion requires `Unorm`. Preserve
v1–v6 byte formats for compatible views. An older builder rejects the new
registered leaf and an older Runtime rejects v7. A compatible prior
generation can still be read against its own frozen view, with workspace
write readiness checked separately.

Reuse the shared worker, workspace lock, exact catalog delta, original
Register-fragment and producer-header checks, and the two rename
boundaries. Recheck admitted role, key, GUID, bytes and frozen texture
evidence before commit. Material Save and Resume recheck the frozen
material and texture inputs at this boundary. When a publication receipt
is uncertain, validate the destination against the same frozen evidence
before confirming it. Inconsistency before source commit leaves durable
source/catalog and active publication intact. An invalid destination
cannot be confirmed as published.

### R2. Material and lighting

Require the existing opaque authoring subset of `PbrDirectLit` with
base alpha 1 and cutoff 0.5. AO may coexist with base-color, emissive
and existing scalar fields. When AO is bound, require both the normal and
metallic–roughness slots to be empty. Select the same UV0 and material
sampling as other maps. Multiply the sum of IBL diffuse and specular by
linear R at fixed strength 1. R=0 suppresses that contribution and R=1
leaves it unchanged. G/B/A are ignored. Direct lights, direct shadows
and emission keep their existing terms. Clearing removes the slot and
restores the existing unoccluded path. With no configured IBL, keep the
existing direct-lit path and validate the dependency without binding or
sampling an AO image.

A bound slot requires a non-nil GUID resolving to a canonical managed
Occlusion Texture with no dependencies, RGBA8 UNORM and a single mip.
Refuse this role in the base-color, emissive, normal and metallic–roughness
slots, including older recipe methods on the new Runtime. Enforce recipe and
role eligibility in shared authoring, cooker, scene load and preview, including
texture-cache hits. Preserve material GUID, other values and unrelated
edges. Replace or remove the old AO edge as appropriate and emit one unique
edge for a bound slot.

A bound slot emits material source v12 with final `occlusion_map_asset`
GUID, artifact v11 extending the v9 common payload with one AO AssetId,
and metadata v11 with final `occlusion_map_asset_id`. Require a complete
matching version, byte length, GUID/key, format and sidecar. Check the
actual source and artifact schema versions in catalog producer/cook
records. Keep the exact 0098 v11/v10/v10 triple when only the packed map
is bound, and the v10/v9/v9 triple without either slot. Reject mixed
versions and unsupported schema through existing loader errors. Keep
scene, catalog and texture artifact formats.

Use one opt-in IBL shader permutation, with a sampled texture at set 0
binding 6 and the existing material sampler. The six-slot Vulkan limit
can already accommodate this case. Keep existing IBL permutations,
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
`occlusionMap`, a canonical non-nil GUID or null. Keep the cooked
`assets.previewMeshMaterial` wire shape. A new client classifies only
a response with the matching ID, no result, and the exact outer
`BadRequest` error with message `unknown or malformed asset query`
as `Unsupported`. Supporting peers without write permission reply `Unavailable`.
Malformed responses and transport failures retain existing categories.
An unknown write result retains known target and receipt information and
offers status/reconciliation. The client never replays an uncertain
write automatically.

On the new Runtime, every older Read, Copy and Edit refuses a recipe with
unsupported fields before projection or staging. An older Read can still
show its compatible active view while the registry is ahead. The existing
ahead guard blocks writes based on that view. Legacy Resume encountering
an unrepresentable committed source returns `Source/InvalidRecipe` with
the committed state and GUID, and preserves bytes. A compatible Resume
can publish without another source edit. An explicit older workflow
remains available on a connection with the precise capability refusal.

Occlusion joins the shared worker and the same 16 terminal receipts.
Latest is role-scoped, unknown/wrong-role/evicted IDs return
`UnknownJob` and repair state is workspace-wide. After permission,
lock and repair checks, missing, already-active or wrong-role targets
return `NothingToResume`. A sole ahead Occlusion registration can
Resume from its original PNG, seal and fragment. Multiple ahead changes
return `ActivationRequired` where the existing contract does.
Distinguish precommit refusal, committed source with publish failure,
confirmed publication and unknown outcome. Preserve the committed GUID
and bytes after confirmed source commit, and use the current explicit
activation and workspace repair guidance.

### R4. Editor, preview and reopen

Add the Occlusion choice to the existing 0093 Assets texture import
dialog and a slot in the existing material dialog. Filter by the current
catalog and canonical role, retaining final validation in the service.
Explain linear R, IBL-only effect and conflicting slots. Keep Replace,
Clear, existing factors and a failed draft. A conflicting normal or
packed map must be explicitly cleared before AO can be bound. Track
texture receipts by role across UI switches and keep existing shared
write blocking, recovery and restart guidance.

Keep the captured mesh, fixed camera, existing direct light and PNG
bounds. AO-bound cooked pairs and complete AO-compatible draft requests,
including null AO for a matched comparison, use the existing baked
`ibl_studio` resource and its irradiance/specular/DFG inputs as fixed
preview lighting. A complete draft with normal or packed maps and null
AO uses its existing lighting. Legacy draft methods and unbound cooked
pairs keep their prior lighting. The dialog identifies the fixed studio
lighting. Load only the configured built-in studio artifact and sidecar for this
preview mode, even if the live scene has no IBL. A request cannot select
an environment. If the built-in input cannot be loaded or realized,
return the existing typed preview refusal or render failure.

Resolve and validate mesh/material/texture inputs against one retained
catalog before GPU realization. Reuse one preview permit across all mesh,
pair and draft versions, acquired before resource realization. Reuse the
environment realization and upload helpers with request-owned resources
and the existing RenderGraph, settlement and cleanup rules. No live
lighting or scene resource is changed. Reject late success or failure
unless connection/session, snapshot, surface, mesh and selected material
or draft revision still match.

Save & Publish preserves the active Runtime catalog, projection, current
document and undo history until explicit activation. Use an opted-in
IBL fixture with the existing material reference in its source before
staging. Save As a copy after activating the texture and before editing
the material. Publish one material edit. Reopen the original saved scene
on the named generation. Independently Build/Open the copy in a second
Editor through [0090](0090-saved-scene-build-open.md) using the ordinary
saved-slot build without a profile or generation override. Both legs
retain the node/material reference and exhibit the same committed AO
effect without a scene write, a rebind or Apply.

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
