# Spec 0116 — Reimport a Registered Base Color Texture

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-19
- **Related Plan:** [Plan 0116](../plans/0116-registered-texture-reimport.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0167](../adr/0167-managed-texture-source-replacement.md) (Proposed)

## Summary

Replace the PNG of one already registered sRGB texture at the original
import input path. Request a rebuild from Assets, review its known material
dependents, and explicitly restart into the resulting named generation.
The texture GUID and all existing references stay the same. A bad input
leaves the last usable version intact and permits a fresh retry after repair.

## Motivation / problem statement

[0085](0085-shared-asset-rebuild.md) already admits one GUID, builds in an
exclusive workspace, and publishes a named generation without activating it.
Its host explicitly excludes managed texture producers. [0095](0095-texture-import-and-base-color-binding.md)
registers a sealed managed source, and both the rebuild path and the managed
CMake configuration reject an unsealed change. Register would allocate a new
identity; its Resume handles an unpublished *new* registration. Neither is
a replacement operation. The shared catalog exposes direct dependency
GUIDs through assets.get but has no reverse dependency query.

## Goals and non-goals

Support one 0095 registered base color PNG from the selected named generation,
in a Windows development Runtime with the existing trusted managed profile.
Preserve the registration identity, recipes, bindings and historical
generations. Leave batch reimport, watching, upload or an arbitrary path,
move/rename/delete, editor-side file browsing, live texture reload, material
rewrites, and linear-role reimport to later work. Existing sRGB consumers
(including [0100](0100-emissive-texture-import-and-binding.md)) can observe
the changed bytes when they already reference this GUID; no slot or renderer
behavior is added.

## Requirements

### R1 — Resolve one eligible asset and freeze the original input

On the existing shared rebuild request, resolve the GUID against the active
immutable catalog and its frozen producer view. The selected entry must be
one managed 0095 sRGB texture with no subkey, with matching type, GUID,
catalog source key, registered unit, and original catalog fragment. The host
derives the name from that verified producer. The editable input is the
original 0095 path `inbox/<name>/source.png` in the configured managed
project. The logical catalog key remains `content:texture_<name>/source.png`.
Do not read the registered file as an edited input, edit the sealed copy
in place, or accept a path, bytes, or options over the wire.

Require the registered source and seal to match the active frozen digest.
Verify the canonical registration fragment against the active identity and
capture its exact bytes for later rechecks. Require the rest of the managed
registry to match the active view as well.
An already-ahead registry, wrong profile, unsupported producer, wrong role,
or dirty or busy workspace uses existing rebuild refusal semantics.
Ordinary rebuild for other supported producers is unchanged. Admit only one
job under the existing project and staging locks. Freeze the bounded input
in job-owned staging; subsequent changes to the inbox cannot affect this
job. Leave the inbox file at its original path. Apply 0095's existing PNG
validation and format limits (8-bit RGB/RGBA,
single image, 1–2048 pixels per axis, at most 24 MiB, one-mip RGBA8 sRGB
output). Use exact canonical paths, ordinary files, and the existing guarded
copy and no-link rules. A missing, invalid or unsafe PNG is a diagnosable
clean failure when no uncertain change remains.

### R2 — Build, validate and publish a same-identity replacement

Use the existing managed configure, producer stamp invalidation, catalog
build and named-generation publication paths. A distinct private staging
selection substitutes the existing unit for this build only; it must match
one existing base color producer, never mask duplicate or missing Register
entries. Cook the frozen PNG with a fresh seal and a byte-identical
registration fragment. Do not mint a texture GUID or write material
recipes, binding fields, scene sources or the live generation.

Before source exchange and publication require the complete catalog
identity set and direct dependency sets to match the active catalog.
Verify the frozen producer inventory including its header, effective source
identity, the exact material recipes, other managed registrations and texture
seals, and the selected PNG's frozen bytes, stamp, artifact and sidecar
evidence. Recheck the prepared directory using activation validation and
confirm it contains this job's exact intended texture evidence. The existing
catalog build may run other graph work; reimport cannot publish changed
registrations, recipes or dependency edges. The shared catalog schema,
artifact layout, and generation manifest versions stay unchanged.

Use [ADR-0167](../adr/0167-managed-texture-source-replacement.md) for the
backup, replacement and publication sequence. Success names a validated,
selectable generation and the existing asset GUID from the job. It does
not change discovery, preview or rendering in the attached Runtime. Show
the difference between the published and active generation; the operator
selects the published ID via the existing profile and explicit restart.
Restarting into an older complete generation remains valid for use, but
managed writes require the active and committed registry to agree.

### R3 — Explain failure and retry without losing the usable version

Keep the current Runtime and every previously published generation read-only
throughout. For bad input and other verified failures before the source
exchange, retire only job-owned staging, leave the old registration intact,
and allow an explicit rebuild after correction. If the exchange started and
publication definitively did not happen, restore the verified old unit under
the locks and permit retry only after proving restoration and cleanup.
If state cannot be verified, keep the recovery evidence and dirty markers.
Block managed writes with the existing `WorkspaceNeedsRepair` refusal; the
operator may continue using or explicitly start the old complete generation
read-only. No automatic retry, Resume or inferred activation.

Keep the existing rebuild job/status wire shape, bounded terminal history,
and result correlation by job ID. Use existing failure stages and codes with
a diagnostic distinguishing correction and retry, activation required,
or offline inspection and repair. A lost start/status response is not
failure or success: query the job ID when known; a latest matching asset
alone does not prove ownership of a lost start. If a publish call reports
an uncertain result, success requires proof that the intended final directory
and committed registry match; otherwise keep ambiguity explicit. See
ADR-0167 for offline settlement. Cleanup failure after verified publication
does not retract the generation; it blocks further writes until repaired.

### R4 — Shared access and known affected materials

Reuse `assets.rebuild` / `assets.rebuildStatus` in Editor, CLI, native/C#
SDKs and MCP, with no added fields or error enums. Older clients can use the
same call and result; a host without this support refuses the producer
without changing files. No fallback to Register/Resume. Editor labels
the existing rebuild action Reimport PNG for a selected visible 0095 source
identified by its structured catalog key, and displays the expected inbox
path, progress, and retry or explicit restart guidance. Its eligibility is provisional until
the host verifies the producer. The UI follows 0093 and uses the existing
rebuild admission and status owner. A new selection must not retarget an
already admitted job; late replies must not update a replaced connection.

Offer a separate read-only action to view known affected materials in the
current snapshot. Capture the selected texture GUID, accepted catalog list,
and connection. For each Material in that snapshot use its existing
`assets.get` dependency set to find a direct edge to the texture GUID; the
reply must match the candidate material GUID and type. List unique matching
materials with name, path and GUID, even if the
texture is referenced by more than one slot. This is catalog evidence of a
direct material dependency, without a slot claim, transitive scene impact,
or uncommitted draft coverage. CLI, SDKs and MCP can compose the same
read-only queries. No new reverse index, remote method, or material writes.

Run the scan only on explicit request; keep one pending scan, use bounded
worker slices with at most one detail lookup outstanding, and allow newer
work between slices. Publish a deterministically ordered result for the
same GUID/snapshot only. Show no matches only after a complete scan; if a
detail fails show matches found so far as incomplete. Cancel on hide,
selection change, reload or disconnect and drop late results. Do not clear
the Assets filter/selection or change the current preview when showing this
result. No per-frame work when idle.

## Proposed design and architectural impact

Reuse the shared rebuild job and immutable Runtime activation model from
[ADR-0146](../adr/0146-managed-asset-rebuild-and-generation-publication.md)
and the PNG validation and evidence of
[ADR-0154](../adr/0154-managed-base-color-texture-import.md). Replacement of
an existing sealed PNG and seal is a new source ownership and recovery
decision, recorded by proposed [ADR-0167](../adr/0167-managed-texture-source-replacement.md).
No new Runtime endpoint, catalog schema, dependency package, rendering feature
or background listener is needed.

## Alternatives and open decisions

In-place overwrite loses the committed seal as an independent check and can
leave the older build unreproducible. Registering under a new name changes
the binding identity. A new reimport API or reverse dependency index would
duplicate capabilities available for this first slice. Plan 0116 J1–J4 asks
for approval of the editable input and named-generation precondition, guarded
source exchange/recovery, shared API and scan tradeoffs, and verification.

## Verification

[Plan 0116](../plans/0116-registered-texture-reimport.md) defines the two
Windows 100% workflows (valid PNG and explicit restart; invalid PNG, old
version still usable, corrected retry) plus independent identity, dependency,
material recipe and deterministic fault checks. Compare visible preview
regions using a known input pair with the same mesh, material and settings.
No new renderer golden or broader import feature is proposed.
