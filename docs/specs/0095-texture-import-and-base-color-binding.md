# Spec: Texture Import and Base Color Binding

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-14
- **Related Plan:** [Plan 0095](../plans/0095-texture-import-and-base-color-binding.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0154](../adr/0154-managed-base-color-texture-import.md) (Proposed)

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
8-bit RGB or RGBA single-image PNG, 1–2048 pixels on each axis, at most
24 MiB encoded.
Check the signature and header bounds before decoding, verify the decoded
shape and reject malformed or unsupported inputs, including APNG. PNG profile and gamma
chunks do not select a different output or trigger conversion. Preserve
RGB sample values and RGBA alpha, supplying 255 for RGB. The existing
opaque 0094 slot does not acquire transparency. Use the existing PNG
decoder/cooker and its one-mip RGBA8 artifact with explicit `Srgb`.

Commit an ordinary `registered/texture_<name>` unit containing
`source.png` and a one-entry v1 catalog-source fragment. Its stable source
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
PNG snapshot used by the producer. Reject unexpected catalog deltas.

The opt-in build freezes the PNG it actually uses. Private manifest v4
adds the frozen texture units, their producer membership and
generation-relative input locations alongside the existing glTF and
material information. Emit v4 when managed textures are present. Older manifests and
seeds without texture metadata describe an empty managed texture set and
retain their prior validation. Verify membership, path and cooked catalog
agreement at activation. Use the same frozen PNG for cooking and
publication and verify producer completion against that input. Keep
frozen files generation-relative and compare contents in bounded chunks
on the worker. Compare registered source bytes, keys and GUIDs against
the selected snapshot before mutations and recheck at commit. A source change
under an unchanged GUID is still an ahead or invalid registry. Committed
texture sources are immutable in this feature. Managed Texture GUIDs
refuse targeted rebuild with `UnsupportedProducer`; a clean build still
cooks them from the committed source. A fresh opt-in build reads
committed units only, including texture dependencies of managed materials.

Publish via the existing no-replace registration and generation renames.
If the first rename begins, settle its outcome before responding; after
known registration commit, attempt publication even on orderly stop or
deadline, under the existing commit guard. Use the existing offline repair
procedure for uncertain settlement. Keep markers until the child settles
and the private candidate/selection is retired. Incomplete generations
are never selectable.

| Outcome | Recovery |
| --- | --- |
| Clean refusal before registration | Retain the inbox input. After correction allow explicit Register retry. No source registration or selectable generation was added. |
| Registered, publication failed | Return the original GUID. Resume reads the sole new committed texture ahead of the selected snapshot, never the inbox. It keeps the GUID, verifies the original registered unit and does not rewrite it. |
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
Reuse 0086's typed import values and envelopes with a separate texture
operation and receipt history. Start returns `Running` with canonical
non-nil `jobId`, name and mode or a typed refusal. Status returns
`activeGeneration` and `job`, both nullable. Keep one running and 16
terminal texture receipts in memory. `Succeeded` adds `rootGuid` (the
Texture GUID) and `generation`. `Failed` adds the existing import
failure fields with the registered Texture GUID only for `Committed`.
Keep 0086's error vocabulary, stage/code pairs, retry behavior and
4096-byte diagnostic limit. Use `Source/InvalidSource` for unsafe or
missing input, `Pipeline/BuildFailed` for decoder rejection, and
`Validation/InvalidOutput` or `Validation/IdentityConflict` for an
invalid producer or catalog delta. Name/mode and explicit job ID
correlation are mandatory. Latest is a hint, not a receipt. Disconnect
does not cancel an admitted job and no client retries automatically.
After lock and repair checks, Register of an occupied texture name returns
`NameConflict` before the ahead-registry check. Resume with no matching
new registration returns `NothingToResume`. An otherwise eligible
registration with additional ahead changes returns `ActivationRequired`.

Expose typed operations in both SDKs, `assets texture-import <name>
[--resume]` and `assets texture-import-status [<JobId>]` in CLI, and
`assets.texture_import` and `assets.texture_import_status` in MCP
through the C# SDK. Preserve the terminal failure payload while returning
a nonzero CLI or MCP tool error. Only the exact existing legacy
`BadRequest` / `unknown or malformed asset query` for either well-formed
new call maps to `Unsupported`. Old clients continue to use unchanged
methods and discover activated textures as ordinary Texture records.
A host without the opt-in returns `Unavailable`; there is no implicit
conversion to glTF import or fallback to another mutating call.

**R4. Bind, preview and reopen.** Add an Import Texture action in Assets
using logical inbox name, explicit Register/Resume, correlated status and
restart guidance. Follow 0093's layout and status styles. Explain the fixed
sRGB interpretation. Reuse the disposal and replaced-session guards from
the existing import UI. Keep the material chooser on `assets.list/get`.
A Texture row is a candidate. The existing shared material preview and
save checks determine base color eligibility from the cooked dependency.
No bulk decoding, new metadata query or Editor-only eligibility rule is
required. Reject an incompatible or missing texture with the existing
typed diagnostic and keep the material draft. Successful texture import
does not bind it or modify the scene, Runtime or history.

After activating the texture generation, choose its GUID in the existing
0094 Base texture field on a managed material. Use the existing draft preview
with one compatible mesh. The Save & Publish action uses the existing
material write with its original recipe/generation guard; Edit preserves
the material GUID and replaces its sole direct texture dependency with the
chosen GUID. Verify both the saved recipe and catalog edge in the published
generation. Existing preview admission, late reply guards, cancel and
failure behavior apply. Explicitly restart into the material generation.
Reopen a saved scene referencing that material and verify its unchanged
reference and changed rendered appearance. Independently build and open
the saved scene using the committed root and its own outputs, with no
generation override.

### Non-functional

Windows opt-in publishing only. Keep source I/O and cook/build/publication
off the frame thread. Reuse existing preview GPU work, isolation and
terminal drain. Add no idle work, external dependency, texture/material/scene
artifact format, shader changes or RenderGraph/RHI interface. The changed
private manifest is versioned separately.

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
