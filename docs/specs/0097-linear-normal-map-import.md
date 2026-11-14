# Spec: Linear Normal Map Import

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-14
- **Related Plan:** [Plan 0097](../plans/0097-linear-normal-map-import.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0156](../adr/0156-managed-linear-normal-map-import.md) (Proposed)

## Summary

Register one standalone PNG as a linear tangent-space normal map in an
opted-in Windows project. Explicitly activate it, then bind, preview and
publish it through the existing material editor. Verify the persisted result
in an ordinary Runtime restart and in an independently reopened saved scene.
Editor, CLI, native SDK, C# SDK and MCP share the import contract.

## Motivation / Problem Statement

[0095](0095-texture-import-and-base-color-binding.md) always cooks managed
PNGs as sRGB. [0096](0096-pbr-texture-editing.md) accepts RGBA8 UNORM in the
normal slot and already previews it with the existing tangent-space shader.
Project operators can use checked-in normal maps, but currently have no
managed import that preserves the linear samples of an external PNG.

## Goals

- Persist the interpretation and stable identity across publication, Resume
  and independent builds.
- Use the imported GUID in 0096's Normal map slot and existing pair preview.
- Preserve 0095's closed color import API and 0090's saved-slot launch
  contract.

## Non-Goals

Other data slots or formats, green-channel flipping, normal generation from
height, normal-strength controls, mip generation, texture replacement and
reimport, uploads, file pickers, and node editing are deferred. Import and
material save remain separately authorized and recoverable operations.

## Requirements

### Functional

**R1. Input, identity and interpretation.** The operator places one
`inbox/<name>/source.png` in the trusted managed project and explicitly
chooses the normal import operation. Reuse 0095's logical name, fixed file,
canonical path and reparse checks, and bounded private copy. Accept its same
8-bit single-image RGB or RGBA PNG subset, 1–2048 pixels per axis and at
most 24 MiB encoded. Apply the same chunk, CRC, unsupported chunk and decoded
shape checks. Ignore PNG gamma, sRGB and ICC tags for interpretation.
Preserve RGB sample bytes and straight alpha, filling RGB input alpha with
255. Cook one mip of RGBA8 tagged `Unorm`. Sample RGB as tangent-space XYZ
mapped from [0, 1] to [-1, 1], with +Y in green and the existing cooked
tangent handedness on UV0. Alpha is unused by normal shading. The operator
supplies +Y content. The importer performs no gamma conversion, channel
flip, pixel heuristics or normal-vector preprocessing.

The worker mints one GUID and atomically registers one ordinary Texture unit
under `registered/texture_<name>`. Normal units contain `normal.png`,
`source.sha256` over that PNG, and one ordinary v1 fragment with source key
`content:texture_<name>/normal.png`. The committed key fixes the linear
interpretation for subsequent reads and builds. Color units retain
`source.png`, the existing digest seal and `content:texture_<name>/source.png`.
Each unit accepts exactly one of the two canonical PNG leaves and its
matching fragment. Reject mixed leaves, inconsistent keys, invalid seals
and unsupported forms in both the worker and the configured build. The two
forms share the texture name namespace. Name, source key, GUID and derived
key use the existing collision rules. Source bytes and interpretation remain
immutable after registration. Discovery uses the existing Texture type and
ordinary `assets.list/get` records.

**R2. Publication, compatibility and recovery.** Reuse the 0095 managed
producer and single worker slot, writer locks, dirty markers, exact catalog
delta and contained CMake build. The producer freezes the declared PNG leaf,
selects the fixed format from the registered source key, validates the
bounded cook result and records the existing PNG, artifact and metadata
digests. The frozen leaf and its declaration participate in build
dependencies so a stale sRGB artifact cannot satisfy a normal producer.
Include the interpretation in registered, staged, prepared and
active comparisons. A normal registration or Resume adds exactly one
dependency-free Texture and preserves every existing identity and edge.
Verify the prepared files and intended registry under the existing commit
guard before the registration rename. Validate a selected generation or
configured seed against its own frozen view, independently of the ahead
registry. Frozen key, format, GUID, PNG and digests must agree. A role or
digest mismatch refuses activation or writes according to the existing
settlement boundary.

Emit private manifest v5 only when at least one managed normal is present.
It retains v4's texture evidence and adds required `colorSpace` to every
`textureSources` entry, using `Srgb` or `Unorm` as determined by the source
key and checked against cooked artifact and metadata. Preserve the exact
existing v1–v4 layout and checks for compatible views. v4 admits only
0095 color units. Earlier views retain their existing texture-membership
rules. Reject missing, additional or contradictory role evidence, including
an attempted v4 normal or a v5 without a managed normal. Older build tools
must reject the normal unit's leaf or fragment before cooking. Older
Runtime executables can continue using compatible earlier views under the
existing read and repair rules and cannot activate a normal-bearing v5.

Keep the 0095 durable outcomes. A clean precommit failure leaves the inbox
and registry intact. Register returns `NameConflict` for an occupied texture
name of either kind after the existing lock and repair checks. A known
committed publication failure retains GUID, bytes and interpretation for
explicit Resume. Resume can publish the sole new committed unit of the
requested kind using its registered PNG and seal, even after inbox removal.
It cooks using the original interpretation and leaves the registered bytes
and GUID intact. A wrong-kind Resume returns
`NothingToResume` before admitting a job. If the matching unit exists alongside
other ahead changes, use `ActivationRequired`. Repair and unknown outcomes
follow 0095. Confirmed publication remains successful when cleanup requires
repair. Targeted rebuild remains unsupported for managed textures. Keep
earlier catalog views stable until explicit restart.

**R3. Shared client contract.** Add the following closed operations to
`atlantis.remote/1`. The import kind is fixed by the method. The legacy
methods and payloads stay byte-compatible.

| Method | Parameters | Success |
| --- | --- | --- |
| `assets.normalMapImport` | `{name,mode}`, with `Register` or `Resume` | Existing Texture import job shape in `Running` state. |
| `assets.normalMapImportStatus` | `{}` or `{jobId}` | Existing texture status shape `{activeGeneration,job,workspaceNeedsRepair}` with the normal job view. |

Both kinds share the existing typed texture job ID, one running job and
16 terminal receipts in total. Status without an ID returns the running job
of that kind, otherwise the newest terminal job of that kind or no job.
An explicit wrong-kind, missing, evicted or prior-host ID returns
`UnknownJob`. Generation and repair status are workspace facts visible
through either method, even with no matching job. Use the 0095 refusal,
failure and receipt rules, including `Unavailable` on a supporting host
without write opt-in. Check closed member sets, canonical IDs, correlated
name/mode and explicit IDs. Only a well-formed call with the exact
correlated legacy `BadRequest` / `unknown or malformed asset query`
envelope maps to `Unsupported`. Preserve transport or protocol error
categories, the known ID or unknown target, and durable inspection guidance.
Latest status is a hint. Neither status nor a lost reply causes replay.

Expose typed normal import and status methods in both SDKs, CLI
`assets normal-map-import <name> [--resume]` and
`assets normal-map-import-status [<JobId>]`, and MCP
`assets.normal_map_import` and `assets.normal_map_import_status` through
the C# SDK. Preserve terminal receipts and the existing CLI nonzero and
MCP tool-error conventions. Old clients can use the existing color import
contract and discover newly activated normal textures through catalog calls.

**R4. Editor, binding and reopen.** Extend the existing 0093-style Assets
import controls with an explicit Base color / Normal map choice. Show the
fixed format and +Y expectation for the normal choice. Correlate admission,
settlement, diagnostics, Resume and restart guidance with the chosen kind,
logical name, job ID and session. Changing kind or leaving the tab retains
known receipts and unknown targets. A delayed callback must not update the
other kind's presentation. Resume uses the original kind and explicit
user action. An older host's refusal leaves color import usable and never
causes fallback to a color mutation. Successful import never changes the
running World, document, history or material binding.

Activate the texture generation, select its GUID in 0096's Normal map field,
preview on a compatible mesh, then Save & Publish. Continue to determine
slot eligibility from cooked metadata in the shared service. Refuse the
UNORM texture in sRGB slots and an sRGB texture in the normal slot with the
existing feedback. Preserve the material GUID, other slots, sampler and
scene reference, and add exactly the unique normal dependency. Explicitly
restart an ordinary opted-in Runtime into the material generation and
verify the same scene reference without a rebind, the persisted recipe and
the rendered normal effect. Independently use 0090 Saved Scenes Build/Open
with the committed root and its own outputs. The saved-slot Runtime uses
its existing launcher rules and accepts no profile or generation override.

### Non-functional

Keep all PNG copying, hashing, validation, cooking and publication on the
existing worker/build path. Reuse portable CPU decoding and the Windows
host's existing seal verification. The existing Vulkan representation,
shader variants, RenderGraph, preview admission and resource ownership
apply. Add no frame work, GPU feature, third-party dependency, or public
catalog, material, texture or scene format changes.

## Proposed Design

Distinguish managed color and normal textures at the registration and
shared import boundaries. One worker and producer turn both forms into
ordinary Texture records. The material editor continues consuming GUIDs.

## Architectural Impact

[ADR-0156](../adr/0156-managed-linear-normal-map-import.md) records the
additional closed shared operations, durable interpretation and private
generation compatibility. It extends ADR-0154 and uses ADR-0155 as-is.

## Alternatives Considered

See [ADR-0156](../adr/0156-managed-linear-normal-map-import.md#alternatives-considered).

## Testing & Verification Plan

[Plan 0097](../plans/0097-linear-normal-map-import.md#verification-checklist)
maps R1–R4 to producer, cross-version, recovery, Vulkan and Windows 100%
evidence. It distinguishes named generation restart from independent
Saved Scenes Build/Open.

## Risks & Open Questions

The review settles durable role representation, status partitioning and the
launch evidence for each restart path. A valid PNG can still contain the
wrong tangent convention. Settings and help identify the expected content.

## Out of Scope / Future Work

Changing normal convention, adding additional linear map roles or changing
an already registered texture requires a separate design.
