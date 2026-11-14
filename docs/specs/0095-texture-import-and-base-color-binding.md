# Spec: Texture Import and Base Color Binding

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-14
- **Related Plan:** [Plan 0095](../plans/0095-texture-import-and-base-color-binding.md)
- **Approval:** Atlantis maintainer, 2026-11-14, [PR #355](https://github.com/slmao/Atlantis/pull/355) at `95e0186b`
- **Related ADR:** [ADR-0154](../adr/0154-managed-base-color-texture-import.md) (Accepted)

## Summary

Import one sRGB PNG into an opted-in Windows project's asset catalog. After
explicit activation, select the texture in an existing basic PBR material,
preview it on a mesh, save and publish the material, and reopen a scene that
uses it. Editor, CLI, both SDKs and MCP use the same import and material
contracts.

## Motivation / Problem Statement

[Spec 0094](0094-basic-material-authoring.md) already stores a base texture
GUID in the ordinary material recipe, validates the selected catalog
dependency and renders drafts with the existing mesh/material preview.
Checked-in textures can already be bound. The managed import in
[Spec 0086](0086-new-asset-import.md) only registers a restricted glTF unit.
The missing step is getting a standalone image into a trusted project's
managed catalog with an identity a material can keep across builds.

## Goals

- Register one PNG with an identity stable across recovery, generation
  activation and fresh opt-in builds.
- Use it as the single base color texture in Spec 0094 materials and
  demonstrate the saved result after reopening a scene.
- Expose the same typed outcomes and recovery rules on every shared client.

## Non-Goals

No image upload, file picker/copy workflow, batch import, reimport or
replacement, delete, rename, content deduplication, or external registry
editing workflow. No DDS, JPEG, HDR, compression or mip generation changes,
color management, data or normal maps, new material slots, or node graphs.
There is no combined transaction for importing a texture and saving a
material, no auto-activation and no live asset swap.

## Requirements

### Functional

**R1. Managed input and identity.** The local operator places
`inbox/<name>/source.png` in the existing opted-in project. Use 0086's
logical name grammar; API parameters never carry a path or image bytes.
Resolve only the fixed, ordinary inbox file in the project. Reject unsafe
paths, reparse points and noncanonical unit or file names. Copy bounded
bytes to the job's private staging unit and cook that copy. The operator
provides sRGB-authored color values. The managed cooker accepts actual
8-bit single-image PNG with IHDR color type 2 (RGB) or 6 (RGBA), 1–2048
pixels on each axis and at most 24 MiB encoded. Validate signature, header,
dimension and bounded chunk framing on the copied bytes before decoding.
Reject APNG, CgBI and `tRNS` inputs. Use the existing decoder for pixels and
reject decode failure or a decoded shape different from the checked header.
ICC, gamma and other color description chunks are ignored for color
interpretation. Preserve straight RGBA alpha and supply 255 for RGB.
The existing opaque 0094 slot does not acquire transparency. Preserve
encoded RGB samples in the one-mip RGBA8 artifact tagged `Srgb`.
Vulkan samples RGB through the existing sRGB format. The 0094 base color
factor stays linear.

Commit an ordinary `registered/texture_<name>` unit containing
`source.png`, a one-entry v1 catalog-source fragment and a private
`source.sha256` seal containing the 64 lowercase hexadecimal digits of
the PNG digest followed by a newline. Prepare all three before the
directory rename. Its stable source
key is `content:texture_<name>/source.png`. Mint one non-nil GUID at the
host for Register and persist it in the fragment. Reject an occupied
texture name without overwriting; same bytes under different names are
distinct. Texture, material and glTF units have separate name namespaces.
Check source key, GUID and derived asset key uniqueness against the
effective source, cooked catalog and generated identities. Use the
existing opt-in catalog assembly and stamp/producer inventory. The
configured seed, staging build and Saved Scenes build use the same
logical identity; only the worker build may select a private candidate.

**R2. Publication and recovery.** Reuse the 0085–0086 Windows worker,
workspace and project writer locks, one shared job slot, containment,
deadline and dirty markers across texture import, glTF import, material
save and rebuild. While the job owns the workspace, keep project inputs
stable. Prepare and validate a complete generation before committing
registration. The catalog must retain existing identities and dependency
edges and add exactly one Texture with no dependencies on Register or
Resume against a snapshot missing that unit. Verify the candidate's
logical source key, GUID, producer, stamp, metadata, `Srgb`, RGBA8 layout,
one mip and preview bounds, and tie the cooked result to the exact bounded
PNG snapshot used by the producer. Compare existing dependency sets as
well as identity. The current identity-only check is insufficient.

The opt-in build freezes the PNG it actually uses. Private manifest v4
adds the frozen texture units alongside the existing glTF and material
information. Each texture entry binds its logical key, GUID, safe
generation-relative PNG path and SHA-256 to the completed producer and
its cooked artifact and metadata digests. The producer records these
digests after a successful cook and completes its stamp last. Missing or
stale producer evidence fails validation. Use the frozen copy for cooking
and verify the prepared generation's files before either rename. Emit v4
when managed textures are present. Earlier manifests and seeds without
texture metadata describe an empty managed texture set only when their
producer view contains no managed textures. Preserve their prior
validation. At activation, verify the selected generation or seed's
membership, safe paths, digests and catalog using its own files,
including when the current registry is ahead or requires repair. The
configured seed build emits the same per-texture producer evidence
without a publication manifest.

Compare registered source contents, keys and GUIDs with the selected
snapshot and verify the registered seal before mutations and at commit.
The worker performs the streaming reads after admission and before build
or disk mutation. Existing inexpensive admission checks may still refuse
early; an ahead change discovered on the worker settles with the
corresponding existing failure. A digest identifies checked
content and never replaces the catalog GUID. An edited source under an
unchanged GUID is an ahead or invalid registry. Registered sources are
immutable in this feature. For a current, writable view, targeted rebuild
of a managed Texture returns `UnsupportedProducer`. A fresh opt-in build cooks
committed units only, including texture dependencies of managed materials.

Publish via the existing no-replace registration and generation renames.
If the first rename begins, settle its outcome before responding; after
known registration commit, attempt publication even on orderly stop or
deadline, under the existing commit guard. Use the existing offline repair
procedure for uncertain settlement. Keep markers until the child settles
and the private candidate/selection is retired. Incomplete generations
are never selectable. A failed rename result must be reconciled under
the locks against the expected registered unit or prepared generation.
Return a generation ID only once the intended complete generation has
been confirmed.

| Outcome | Recovery |
| --- | --- |
| Clean refusal before registration | Retain the inbox input. After correction allow explicit Register retry. No source registration or selectable generation was added. |
| Registered, publication failed | Return the original GUID. After clean settlement or offline repair, Resume reads the sole new committed texture, verifies its registered seal, keeps its GUID and leaves the inbox and registration untouched. |
| Publication confirmed | Return the selectable generation and original GUID. Cleanup failure retains success and blocks future writes until repair. |
| Settlement unknown or process lost | Keep dirty markers when settlement is unverified. Use the retained job ID when available and inspect the registered unit and completed generations offline before any retry. |

An ahead registry blocks ordinary texture, glTF, material and rebuild
writes. Existing glTF and material Resume operations also reject a texture
delta. Texture Resume requires precisely one newly registered texture and no
other texture, material or glTF delta. Changed bytes on an already active
texture are not resumable. Select an existing complete generation when
known or regenerate an opt-in seed offline from validated committed units.
The running Runtime, including its first catalog query after publishing,
retains its snapshot. Activate through an explicit restart with the
existing profile and generation arguments.

**R3. Shared import contract.** Add `assets.textureImport {name,mode}`
with exact `Register` and `Resume` arms, and
`assets.textureImportStatus {}` or `{jobId}` on `atlantis.remote/1`.
Reuse 0086's import job payload with a separately typed texture job ID
and receipt history. A request uses `jobId`; the job payload uses `id`
in all states, as in the existing glTF codec. Start returns `Running`
with canonical non-nil `id`, name and mode or a typed refusal. Texture
status returns nullable `activeGeneration` and `job` plus required
`workspaceNeedsRepair`. The flag can accompany `Succeeded` when
publication is confirmed but cleanup fails. Keep one running and 16
terminal texture receipts in memory. An explicit missing ID returns
`UnknownJob`, including after eviction or host restart. `Succeeded` adds `rootGuid` (the
Texture GUID) and `generation`. `Failed` adds the existing import
failure fields with the registered Texture GUID only for `Committed`.
Keep 0086's error vocabulary, stage/code pairs, retry behavior and
4096-byte diagnostic limit. Use `Source/InvalidSource` for unsafe or
missing input, `Pipeline/BuildFailed` for decoder rejection, and
`Validation/InvalidOutput` or `Validation/IdentityConflict` for an
invalid producer or catalog delta. Name/mode and explicit job ID
correlation are mandatory. A known ID remains the lookup key after a
status transport failure. An invalid or lost admission reply leaves
the result unknown and retains the texture name and mode for
reconciliation. Latest can guide inspection and cannot establish
ownership. Disconnect does not cancel an admitted job and no client
retries automatically.
After lock and repair checks, Register of an occupied texture name returns
`NameConflict` before the ahead-registry check. Resume with no matching
new registration returns `NothingToResume`. An otherwise eligible
registration with additional ahead changes returns `ActivationRequired`.

Expose typed operations in both SDKs, `assets texture-import <name>
[--resume]` and `assets texture-import-status [<JobId>]` in CLI, and
`assets.texture_import` and `assets.texture_import_status` in MCP
through the C# SDK. Preserve the terminal failure payload and use a
nonzero CLI exit code or MCP tool error. Reject missing, extra or repeated
members and incorrect reply IDs or outcome arms for the new operations.
Only the exact existing legacy `BadRequest` /
`unknown or malformed asset query` in a valid correlated envelope for
either well-formed new call maps to `Unsupported` and keeps the connection
usable. Other invalid or lost replies keep the existing transport or
protocol error category. Old clients continue to use unchanged
methods and discover activated textures as ordinary Texture records.
This client compatibility does not make v4 generations readable by
pre-0095 executables. A host without the opt-in returns `Unavailable`.
There is no implicit conversion to glTF import or fallback to another
mutating call.

**R4. Bind, preview and reopen.** Add an Import Texture action in Assets
using logical inbox name, explicit Register/Resume, correlated status and
restart guidance. Follow 0093's layout and status styles. Explain the fixed
sRGB interpretation. Retain the known texture job ID or unknown target
when leaving the Assets tab. Ignore replaced-session or unrelated job
callbacks. A known failed registration may be retried explicitly.
Committed failure offers Resume only when the status allows writes.
Unknown results direct the user to durable inspection and a repair
flag takes precedence over mutation. Keep the material chooser on
`assets.list/get`.
A Texture row is a candidate. The existing shared material preview and
save checks determine base color eligibility from the cooked dependency.
No bulk decoding, new metadata query or Editor-only eligibility rule is
required. Reject an incompatible or missing texture with the existing
typed diagnostic and keep the material draft. Successful texture import
does not bind it or modify the scene, Runtime or history.

After activating the texture generation, choose its GUID in the existing
0094 Base texture field on a managed material. Use the existing draft preview
with one compatible mesh. The Save & Publish action uses the existing
material write with its original recipe/generation guard. Edit preserves
the material GUID, sampler and other recipe values when only the texture
is changed. It replaces the old direct dependency with the chosen GUID.
Verify the saved recipe and catalog edge in the published generation.
Existing preview admission, late reply guards, cancel and
failure behavior apply. Explicitly restart into the material generation.
Reopen a saved scene referencing that material and verify its unchanged
reference and changed rendered appearance. Independently build and open
the saved scene using the committed root and its own outputs, with no
generation override.

### Non-functional

Windows opt-in publishing only. Keep source I/O and cook/build/publication
off the frame thread. Reuse existing preview GPU work, isolation and
terminal drain. Use the existing CMake SHA-256 operation in the managed
producer and the Windows SDK's CNG SHA-256 in the executable-private
verifier. Keep the cooker and Asset System free of Windows APIs.
Add no idle work, third-party dependency, texture/material/scene artifact
format, shader changes or RenderGraph/RHI interface. The private
manifest is versioned separately.

## Proposed Design

Extend managed registration with a standalone Texture kind and reuse the
existing texture cooker and generation publisher. Keep texture delivery
and material editing as two separately authorized, recoverable writes.
The material and scene keep their ordinary GUID references and do not
depend on a profile when opened from a fresh opt-in Saved Scenes build.

## Architectural Impact

[ADR-0154](../adr/0154-managed-base-color-texture-import.md) records the
additive shared import surface, managed producer identity and frozen
generation metadata. It extends ADR-0147 and ADR-0153.

## Alternatives Considered

See [ADR-0154](../adr/0154-managed-base-color-texture-import.md#alternatives-considered).

## Testing & Verification Plan

[Plan 0095](../plans/0095-texture-import-and-base-color-binding.md#verification-checklist)
maps R1–R4 to cooker, publication, compatibility, GPU and Windows
end-to-end evidence. Windows UI acceptance uses actual 100% scaling.

## Risks & Open Questions

The closed loop requires activation after texture import and after material
save. Frozen inputs increase generation storage. The joint review settles
the PNG subset, shared import API, recovery rules and acceptance scope.

## Out of Scope / Future Work

Additional image formats, texture replacement, thumbnail browsing, linear
data maps, multi-slot material authoring and node graphs require separate
design decisions.
