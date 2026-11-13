# Spec: Basic Material Creation and Editing

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-13
- **Related Plan:** [Plan 0094](../plans/0094-basic-material-authoring.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0153](../adr/0153-managed-material-authoring-and-draft-preview.md) (Proposed)

## Summary

Create a basic material or copy an eligible one from Assets, edit its values,
preview the draft on a mesh, and explicitly save and publish it. A newly
started Runtime selects the published generation. Its clients can then
discover the material, use it in a scene and reopen the saved reference.

## Motivation / Problem Statement

The [material source parser and serializer](../../src/asset_system/include/atlantis/asset_system/material_source.h)
and [material cooker](../../src/asset_system/include/atlantis/asset_system/cook_material.h)
already support linear base color, metallic, roughness, texture references and
sampler settings. The current asset worker can rebuild or register assets.
[0092](0092-mesh-material-preview.md) previews a cooked mesh and material from
the Runtime's retained catalog. Assets has no way to author a material source
or preview unsaved factors.

## Goals

- Author one project-owned basic material per operation through a shared
  contract. Keep the original asset untouched when copying.
- Preview the draft with the existing fixed pair recipe. Reuse existing
  source, cooker, generation publication, startup and scene reference paths.

## Non-Goals

- Node graphs, custom shaders, new texture import, new texture slots, alpha
  modes, advanced PBR kinds, batch operations, rename or delete.
- In-place editing of checkout or importer-generated material sources, live
  asset replacement, automatic restart, or coupling material edits to scene
  undo/redo.

## Requirements

### Functional

**R1. Editable subset and authority.** Use v10 material source and the current
cooked format. First release supports opaque `PbrDirectLit`, one existing
catalog base-color texture meeting 0092's sRGB RGBA8, dependency and size
checks, alpha factor 1, default alpha cutoff, zero emissive RGB and no normal
or emissive map. The editable recipe is the base-color texture GUID, linear
RGB, metallic and roughness. Each numeric component is a finite float32 in
[0, 1], inclusive. Reuse the cooker rules and round-trip serializer. Create
starts with RGB (1, 1, 1), metallic 0, roughness 0.5 and linear/repeat sampling,
with an explicit existing texture selection required. Copy takes an eligible
cooked Material GUID from the selected catalog, preserving its supported
factors, texture and sampler initially. Edit preserves the original sampler.
Expose the sampler as read-only context. An ineligible source is refused
without stripping fields. A checked-in or imported material can be copied
when eligible. Only a managed material in the opted-in project can be edited.

The project registry owns the source and a single catalog-source v1
registration. Use `registered/material_<name>/source.material.txt` in the
existing managed workspace, with the logical source key
`content:material_<name>/source.material.txt` and a matching producer.
Names follow 0086's lowercase logical-name and
Windows device-name rules. Create and Copy mint a v4 GUID once per committed
registration. They refuse a conflicting destination, even for equal values.
Edit preserves the GUID, logical source and producer. No client supplies a
filesystem path, source text, shader, build option or tool. Use the existing
Windows trusted profile and project/staging ownership from 0085/0086.

**R2. Shared operations.** Add `assets.materialRead { material }`,
`assets.materialSave`, `assets.materialStatus { id? }`, and
`assets.previewMaterialDraft { mesh, recipe }` to the existing asset surface.
Define a closed Save request with these arms:

| Mode | Input |
| --- | --- |
| Create | `name` and editable recipe |
| Copy | `name`, source `material` GUID and editable recipe |
| Edit | `material` GUID, expected active generation (nullable for a seed), the editable values returned by Read before editing, and replacement editable recipe |
| Resume | Committed `material` GUID only |

For an eligible material, Read returns the requested GUID, cooked recipe,
sampler, edit eligibility and active generation. Determine edit ownership
from the frozen producer view and configured write authority. A missing,
wrong-type, malformed or ineligible material has a typed refusal. Other
materials remain discoverable with list/get. The draft
preview request adds a sampler value to the editable recipe, using the two
existing filter and two address modes. Copy and Edit derive the saved sampler
from the authoritative material. Check the Edit precondition and the registry
checks in R3 before writes. Refuse a no-op edit with `Unchanged` after those
checks. Echo request identity in Read and in
the preview result, including the exact normalized recipe for preview. Use
the 0092 PNG limits and metadata. An admission receipt contains a job ID.
Status by ID distinguishes an unknown or evicted job from a pending job.
Status without ID reports the latest job for inspection. A job reports mode,
target identity where known and bounded diagnostics. A settled job is either
`Succeeded { material, generation }` or `Failed { stage, code, sourceCommitted,
material? }`. A known committed source includes its GUID. Keep the existing
bounded retention policy and classify transport/protocol errors separately from
operation refusals. Reuse the existing meaning of `Unsupported`,
`Unavailable`, `Busy` and `CatalogUnavailable`. Distinguish a destination
conflict, stale edit, activation required, repair required and invalid recipe
from build, validation, source-commit and publication failures. A lost or
malformed receipt leaves its outcome unknown. Clients may inspect status and
durable results. A latest job belonging to another request is not evidence
of settlement. If a lost admission cannot be correlated to a job ID, check
the intended target and durable results after the writer has settled before
retrying. No automatic resubmit.

Expose the same operations through CLI, native and C# SDKs, and MCP. MCP uses
the C# SDK and a closed tool schema. Writes require the opted-in profile;
draft preview uses the existing preview capability and is read-only. Use
additive `atlantis.remote/1` methods and the established unknown-method fallback.
Keep list/get, import, rebuild and the two existing previews callable on mixed
client and Runtime versions.

**R3. Save, build, publication and recovery.** Use the existing worker, job
slot, deadline, staging and project locks, dirty markers, and contained build.
Compare the committed producer set and managed material source bytes with the
frozen view of the selected generation or seed. Configure the managed material
producer as data, with one host-selected private candidate for the job. Use
that candidate in place of the old physical input for Edit while keeping
the logical source key. Capture the exact managed source bytes used by the
build for the frozen comparison. Bound each managed source read to 64 KiB.
The operator must keep managed source and registration files stable while a
managed job holds the workspace. Detect any observed change before commit.
Existing managed imports and the Saved Scenes opt-in build consume committed
units. A clean opt-in build produces the same identities. Keep the default
build, checked-in catalog, and public source/cooked formats intact.

Build and validate the complete candidate generation before committing source.
Check the texture eligibility against the active snapshot on admission and
again against the staged output before publication.
Require the intended identity delta (one Material added for Create/Copy, none
for Edit) and the target's exact expected dependency, values and sampler in its
cooked output. Check existing identity, collision, dependency and path rules.
Commit new source and its registration as one no-replace directory rename.
Edit atomically replaces only the registered material source under the locks.
The existing same-volume directory rename publishes the complete generation.
Use 0086's commit guard: after the source rename begins, settle it, and after
known source commit attempt publication despite orderly stop/deadline. No
generation is published after a failed source commit.

| Durable outcome | Client action |
| --- | --- |
| Failure before source commit, settled cleanly | Keep the draft and retry explicitly. No registered material or selectable generation was created or changed. |
| Source committed, publication failed | Retain source and GUID. If settlement is clean, explicitly resume the sole committed change ahead of the selected view. Resume reads the committed source, without rewriting it or minting another GUID. |
| Publication committed | Select the known complete generation at restart. A known completed rename stays successful if receipt delivery or cleanup fails. |
| Unknown or interrupted settlement | Inspect the job if retained. If uncertain, stop writers and use 0086's offline inspection of markers, registered source and published generations before any retry. |

An ahead registry blocks other new writes, rebuild and import; a known matching
published generation can be selected explicitly, or the compatible committed
workspace can be rebuilt offline. Resume requires exactly one ahead material
and no other registry delta. A dirty marker blocks mutation until repair.
Old generations remain startable. A generation freezes material source bytes
for comparison alongside its cooked catalog and producer mapping. The active
Runtime continues to use its selected catalog and artifacts even after a
successful publication. Apply, Reload view and reconnect do not activate the
result. Launch with the existing profile and explicit generation ID. The
standard Asset selector, scene apply/recovery, save and reopen use the new
GUID after activation.

**R4. Draft preview and Editor workflow.** Offer **New material** in Assets,
**Duplicate** for an eligible selection and **Edit material** for a managed
selection. Use one modal with the 0093 dark neutral surfaces, typography,
field grouping, focus and compact layout. Present material name, grouped
properties, the existing-texture chooser, and an explicit mesh selection and
**Preview draft** action. Show complete identifiers in details. Use one
**Save & Publish** action and distinguish editing, building, failure,
source saved with publication pending, unknown outcome and published with
restart required. Cancel discards a local draft before admission. Closing
after admission leaves the worker running and status retrievable. Preserve
the draft on validation failure or clean precommit refusal. When outcome is
unknown, keep the draft visible and disable repeated submission until settled.
Success shows the material GUID and generation ID. No scene edit, apply,
selection or history change occurs merely by editing, previewing or saving
a material.

Resolve mesh and texture from the retained catalog. Normalize and validate
the recipe on the host and realize it in memory using the same pair rendering
path, fixed camera and lighting, bounds, admission slot and lifetime as
0091/0092. A draft preview writes no asset or job and never resolves a draft
material GUID. Check request shape before admission and do catalog and asset
validation only after an available provider takes the shared permit. The
Editor uses its ordinary SDK preview connection and
latest-intent guard. Changing the draft, mesh, modal or session invalidates
the prior image and reply. Release stale/replaced image resources. Preview
failure is separate from save eligibility. A good preview does not certify
full scene compatibility.

### Non-functional

Keep source preparation, build and publication off the frame thread. Draft
preview inherits 0092's between-frame execution and bounded reads, uploads,
PNG and timeout behavior. Add no idle GPU work, permanent preview cache or new
third-party dependencies. First release targets the Windows host and existing
Vulkan renderer.

## Proposed Design

The Runtime executable owns the shared asset host and extends its managed
worker for persistence. The Asset System supplies the existing material
grammar, validation and cooked CPU data. The pair preview consumes a
validated in-memory material for drafts. Editor uses shared operations for
both paths and keeps its draft local until explicit submission.

## Architectural Impact

[ADR-0153](../adr/0153-managed-material-authoring-and-draft-preview.md) records
the shared API, material source ownership and private generation extension.
It builds on ADR-0146, ADR-0147 and ADR-0152. There is no Renderer, RenderGraph
or RHI contract change.

## Alternatives Considered

See [ADR-0153](../adr/0153-managed-material-authoring-and-draft-preview.md#alternatives-considered).

## Testing & Verification Plan

[Plan 0094](../plans/0094-basic-material-authoring.md#verification-checklist)
maps the shared clients, persistence and recovery, real Windows workflow and
applicable GPU and performance checks to R1–R4.

## Risks & Open Questions

Joint review settles the editable subset, registry and recovery semantics,
shared draft preview and the evidence required for delivery. The first
release requires a configured local workspace and an explicit restart.

## Out of Scope / Future Work

Editing other material families, authoring texture assets and richer material
tools can build on this workflow through separate designs.
