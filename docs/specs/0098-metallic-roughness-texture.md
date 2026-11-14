# Spec: Metallic–Roughness Texture Import and Binding

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-14
- **Related Plan:** [Plan 0098](../plans/0098-metallic-roughness-texture.md)
- **Approval:** Atlantis maintainer, 2026-11-14, [PR #361](https://github.com/slmao/Atlantis/pull/361) at `8241daab`
- **Related ADR:** [ADR-0157](../adr/0157-metallic-roughness-texture.md) (Accepted)

## Summary

Import one managed linear PNG, bind it to an opaque direct-lit PBR
material, preview it, and publish it through the shared asset workflow.
Preserve the material reference in the saved scene through ordinary
generation activation and an independent Saved Scenes reopen.

## Motivation / Problem Statement

Atlantis already cooks linear RGBA8 textures and provides managed PNG
registration and recovery through [0097](0097-linear-normal-map-import.md).
The [direct-lit shader](../../shaders/pbr_direct_lit/pbr_direct_lit.slang)
and its [normal-mapped variant](../../shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.slang)
currently feed the BRDF from scalar factors. The
[glTF importer](../../src/tools/gltf_importer/material_import.cpp) retains
those factors and explicitly drops the packed map. Binding therefore
requires a material slot and a shader sample. The proposed channel order
follows [Khronos glTF 2.0 §5.22.5](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html#_material_pbrmetallicroughness_metallicroughnesstexture).

## Goals

- Preserve the packed samples, role and GUID across registration, explicit
  Resume, publication and independent builds.
- Add selection, replacement, clearing and preview to the existing shared
  material editing flow for opaque `PbrDirectLit`, including its existing
  normal and emissive slots.
- Keep earlier generation, source, cooked and wire formats usable for the
  recipes they represent.

## Non-Goals

Separate grayscale map imports, channel packing, occlusion, glTF import
changes, IBL and other PBR models, mask/blend, new sampler controls, UV1,
texture replacement in place, and node editing are outside this slice.

## Requirements

### Functional

**R1. Input, identity and frozen generation.** The caller chooses
Metallic–Roughness explicitly for `inbox/<name>/source.png`. Reuse 0097's
bounded RGB/RGBA 8-bit PNG parser, exact sample preservation, alpha handling,
path guards, single mip and RGBA8 UNORM cook. Ignore color tags. The shared
texture namespace uses the new canonical leaf
`registered/texture_<name>/metallic-roughness.png`, its matching source key
and the existing seal and ordinary Texture fragment. Reject multiple or
misnamed leaves, inconsistent keys and seals, name and identity collisions.
The canonical registered key is `content:texture_<name>/metallic-roughness.png`.
Share the registry's name, GUID and derived-key collision rules. Include
the complete logical key, GUID and PNG digest in registry comparisons.
The GUID and inputs stay stable on Resume and fresh builds. Targeted
rebuild of a managed texture remains unsupported. Catalog discovery
continues through `assets.list/get`.

Generate a private v6 manifest only when the frozen view contains this
role. Keep v5's color-space field and add required `role` to each texture
entry in v6, using exactly `BaseColor`, `Normal` or `MetallicRoughness`.
Derive the role from the canonical producer and catalog keys. Check
`Srgb` for BaseColor and `Unorm` for both linear roles against the
artifact and metadata. Require the exact frozen texture membership and
reject missing, extra or inconsistent evidence. Keep exact v1–v5 shapes
for compatible views. Validate the selected generation or configured seed
against its own frozen view and check registry readiness separately.
An older builder refuses the new registration shape, and an older Runtime refuses
its v6 generation. Older Runtime can keep a compatible earlier snapshot
readable and refuses unsafe workspace mutation. Registration uses the
current writer, pending unit, exact catalog delta and both renames.
Preserve #360's complete producer-header and original Register-fragment
checks at the commit boundary, the frozen material and texture checks and
the destination recheck when publication is uncertain.

**R2. Material and rendering semantics.** For the new slot sample UV0 with
the existing material filter and addressing. Use `G` as the linear
roughness multiplier and `B` as the linear metallic multiplier. `R`
and `A` have no effect. Keep existing factors unchanged on select, replace
and clear. With UNORM samples in [0, 1], compute
`metallic = clamp(metallicFactor × B, 0, 1)` and
`roughness = clamp(roughnessFactor × G, 0, 1)`.
Then compute the existing BRDF alpha from the effective roughness
using `max(roughness², kMinAlpha)`.
Clear returns to the existing scalar path, equivalent to G and B both 1.
A metallic factor of zero still yields zero after binding. Use the same
ordinary Texture GUID throughout. Require a dependency-free RGBA8 UNORM,
single-mip Texture whose authoritative cooked catalog key identifies a
managed MetallicRoughness registration. The existing shared bounded
loader validates artifact and sidecar. Refuse the new managed role in the
normal, base-color and emissive slots, including through older recipe
methods on the new Runtime. Keep previously supported normal sources
eligible for their existing slot. Show which role failed.

For a bound recipe emit material source v11 with a final
`metallic_roughness_map_asset` GUID, cooked artifact v10 with the
corresponding AssetId appended to the v9 payload, and material metadata
v10 with a final `metallic_roughness_map_asset_id` field. The new formats
require a non-nil GUID or a nonzero key, respectively. Emit the current
source v10, artifact v9 and metadata v9 byte formats with an empty slot.
The loader accepts the two complete artifact/metadata pairs and checks
the catalog's declared artifact version against the decoded header.
Reject mixed pairs, mismatched keys and extra payload. The cooker writes
the actual source and artifact schema versions into the catalog record.
Restrict use of the slot to the existing authoring subset of
`PbrDirectLit` with opaque alpha mode, base alpha 1 and cutoff 0.5.
Other recipes continue through their existing load and render paths.
Add precisely the unique dependency edge and preserve the
material GUID, other fields and unrelated edges. Clear removes that edge.
The cooker validates the slot's catalog identity and canonical role,
including the rejection of this role in older slots. Runtime scene
loading and preview independently require compatible cooked metadata and
a valid edge before realization. Check slot role even when the texture
is already in the scene cache. A new cooked record advertises the new
artifact schema. An older Runtime refuses scenes that require the new
schema. Keep the existing unmapped rendering paths, including layout
and push constants. Add one sampled binding only in the two opt-in
direct-lit shader permutations. Use set 0 binding 4 without a
normal map and binding 5 with one. Keep existing bindings, the shared
material sampler and shadow selection. Use the same RenderGraph upload
and lifetime rules. Release request-owned GPU resources at the existing
terminal boundary. No catalog, scene or texture format change.

**R3. Shared client and recovery rules.** Keep all 0095–0097 methods and
closed payloads. Add the following names to `atlantis.remote/1`, with
typed native/C# operations, matching CLI kebab-case commands and MCP
underscore tools. The existing status methods and failure shapes retain
their meanings.

| Method | Shape |
| --- | --- |
| `assets.metallicRoughnessImport` | 0097 texture admission shape `{name,mode}` and texture job receipt. The method fixes the role. |
| `assets.metallicRoughnessImportStatus` | 0097 texture status shape `{activeGeneration,job,workspaceNeedsRepair}`, filtered to this role. |
| `assets.materialReadWithMetallicRoughness` | 0096 read shape with the complete recipe. |
| `assets.materialSaveWithMetallicRoughness` | 0096 Create, Copy, Edit and Resume arms using the complete values and complete Edit `expected` where applicable. Use existing `assets.materialStatus`. |
| `assets.previewMaterialDraftWithMetallicRoughness` | 0096 bounded draft PNG shape with the complete recipe echoed. |

Complete values extend the exact 0096 values by required
`metallicRoughnessMap`, a canonical non-nil GUID or null. The cooked
`assets.previewMeshMaterial` pair keeps its existing wire shape. Exact
well-formed correlated outer `BadRequest` with message
`unknown or malformed asset query` is the only `Unsupported` mapping
for new calls. Require one matching response ID, exact error members
and no result arm. Use `Unavailable` for writes on a supporting Runtime
without the write profile. Retain transport/protocol categories and
known target/ID. The Editor may offer a separate legacy flow after the
exact refusal on the current connection. Writes require explicit
submission in that flow. A lost response never retries a write.

On the new Runtime, each older Read, Copy or Edit path refuses an active
recipe it cannot represent before projection or staging. A legacy Read
may still return an older recipe from the active snapshot while the
registry is ahead. The existing ahead guard refuses new material writes,
including an Edit using the old `expected`, until the caller settles or
activates the committed changes. Resume checks the ahead committed
source using the calling method's version. A legacy Resume that cannot represent it returns
`Source/InvalidRecipe` with committed state and GUID, preserves disk, and
points to a compatible explicit Resume. Existing source permissions,
complete expected-value concurrency checks, separate publication receipts
and workspace repair rules apply.

All texture kinds share the worker slot and 16 terminal texture receipts.
Latest status filters by role, wrong-role or evicted IDs yield
`UnknownJob`, and repair state is workspace-wide. After host, lock and
repair checks, Resume returns `NothingToResume` for a missing, active
or wrong-role target and `ActivationRequired` when a matching ahead
target is accompanied by other committed changes. Only the sole new
registration of the requested role can Resume, using its registered
PNG, fragment and seal. Compare the admitted role, key, GUID and digest
against disk before cook and again before commit. A prepublication
mismatch leaves the active view intact. Retain committed state and GUID
only after confirming the intended registration. Resume after a committed
material failure preserves raw committed source and the complete recipe.
Keep checks against altered prepared inputs, typed precommit, committed
and unknown outcomes, confirmed publication despite cleanup failure,
and explicit generation activation.

**R4. Editor, preview and reopen.** In the 0093 Assets layout add a
Metallic–Roughness import choice and a field in the existing material
dialog. Describe G/B and the retained factors. Discover textures in the
existing catalog and show the eligible role. Confirm eligibility in the
shared service. Retain status by import role and block other texture
writes during unresolved admission, running, committed failure or ahead
success. Keep receipt and restart guidance with its original role.
Reuse the fixed camera, lighting, PNG bounds and one shared preview
permit across mesh, cooked pair and every draft recipe version.
Cooked/draft preview and the scene use the same sampling semantics.
Resolve all inputs from the retained snapshot before acquiring GPU
resources. Reuse the existing decoded texture caps and release rules.
An asynchronous completion applies only to the current session,
retained snapshot, surface, selected mesh and material or draft revision.
Discard stale success and failure responses. Preview preserves catalog,
Runtime projection, scene document and undo/redo.

Save & Publish leaves the active Runtime view, document and undo/redo
unchanged. Use an ordinary opted-in fixture with the intended material
reference already in the scene source before configuring seed and staging.
After activating the texture generation, Save As a separate scene before
editing the material. Edit the material in place, publish, then restart
the original scene on the named material generation. Independently
Build/Open the saved copy via [0090](0090-saved-scene-build-open.md) from
the same committed sources. The second Editor uses its saved-slot build
without profile or generation override. Both legs use the same published
material edit and must preserve the original node/material reference and
show the packed-map effect without a scene write, rebinding or Apply.
Keep distinct scene identities and the saved-slot launch guards.

### Non-functional

Add bounded work for materials using the new slot and explicit previews.
Reuse RenderGraph and the existing no-map draw path on Windows and
Android Vulkan. Keep portable Asset System independent of RHI and
Renderer. Add no dependencies, frame polling, scheduling or renderer
backends. Preserve existing goldens and the applicable performance gate.

## Proposed Design

Keep import, material publication and scene startup as separate
operations. A durable source key carries the packed texture role. A new
complete material recipe and opt-in Vulkan sampling path consume that
role. Existing clients still reach the same asset discovery, job status
and recovery surfaces.

## Architectural Impact

[ADR-0157](../adr/0157-metallic-roughness-texture.md) proposes the packed
role and private manifest v6, dual material-format emission, additive
versioned requests and opt-in GPU binding. It extends ADR-0155/0156 and
the existing direct-lit descriptor contract for mapped variants.

## Alternatives Considered

See [ADR-0157](../adr/0157-metallic-roughness-texture.md#alternatives-considered).

## Testing & Verification Plan

Use [Plan 0098](../plans/0098-metallic-roughness-texture.md#verification-checklist)
for R1–R4, compatibility, Windows 100%, Android, Vulkan, golden and
performance gates.

## Risks & Open Questions

A valid linear PNG can use the wrong channel convention. Explicit role
and channel guidance are required. Mixed workspaces need a compatible
runtime and toolchain for new assets. Joint review decisions are listed
in Plan 0098.

## Out of Scope / Future Work

Occlusion may eventually reuse a packed source, subject to a separate
review of role, channel contracts and indirect-light rendering.
