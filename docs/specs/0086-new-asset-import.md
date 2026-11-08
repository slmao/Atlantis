# Spec: Managed Import and Registration of New Assets

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-08
- **Related Plan:** [Plan 0086](../plans/0086-new-asset-import.md)
- **Approval:** Pending joint human review
- **Related ADR:** [ADR-0147](../adr/0147-managed-import-registration-and-publication.md) (Proposed)

## Summary

A trusted local Windows project can import one self-contained glTF source
through the existing producer pipeline. One shared job registers the source
and publishes a complete generation. After an explicit Runtime restart,
clients discover its derived mesh and can reference its GUID in an existing
hand-authored scene.

## Motivation / Problem Statement

[Spec 0085](0085-shared-asset-rebuild.md) can force an existing producer, but
requires the staged identity set to match the active catalog. A new import
needs a durable source and root GUID, a producer declaration and an allowed
catalog addition. Writing into the active tree would violate the snapshot
and publication rules that make rebuild safe.

## Goals

- Admit one source under a bounded project-local operation shared by the
  Editor App, CLI, native and C# SDKs, and MCP.
- Keep identity stable across retries, clean builds and process restarts.
- Deliver an editable and persistent mesh reference through the current
  Inspector, authoring and Runtime contracts.

## Non-Goals

Bulk import, upload, arbitrary file selection by the Runtime, textures in the
source, GLB, external buffers, overlays, import settings, asset replacement,
rename, removal, cross-project transfer, version-control integration, new scene
creation or launch selection, and live generation switching.

## Requirements

### Functional

**R1 — Source and authority.** A local operator opts into a Windows import
workspace through the trusted Spec 0085 profile and places one file in
`inbox/<name>.gltf`. The request carries only the logical name and whether to
register or resume. Names match `[a-z][a-z0-9_]{0,47}` and exclude Windows
device names. Use a reserved `managed_<name>` content-root and producer
prefix with case-insensitive collision checks. A register request refuses an
existing name even if the bytes match. Identical bytes under different valid
names represent distinct imports.

The workspace is project-owned and disjoint from the checkout, seed build,
staging build and publication roots. Acquire the existing staging lock and
an OS-exclusive lock on the canonical project workspace at bootstrap
without waiting. Hold both through worker settlement. An import-enabled
host lacking either lock may serve reads, but import and rebuild starts
return `Busy`. Refuse overlapping roots, path aliases, reparse traversal
and destination overwrite. Copy an ordinary inbox file under the size
limit into private pending storage. Validate and build only that copy.
The operator keeps the inbox file unchanged during the copy. The job leaves
the inbox intact. The trusted profile authorizes authenticated loopback
clients equally. No request supplies a path, tool, option, URL or CMake
expression.

Accept one glTF 2.0 JSON file, at most 16 MiB, with one scene, one static
mesh node and one indexed triangle-list primitive with the importer's
required position, normal and UV attributes. Allow at most one untextured
material using supported factors. Require one embedded base64 buffer and
check its actual decoded size against the declared byte length before
passing it to the loader. Bound reads and allocations to the checked input.
Reject external buffers, images, textures, extensions, skins, animation,
morph targets, cameras and overlays. The existing importer checks the
restricted profile before it loads buffers or other resources. Its normal
geometry and accessor checks still apply. Ordinary imports keep their
current behavior.

**R2 — Registration and build.** The worker allocates one v4 root GUID
for an admitted register job through the Asset System mint. The build and
importer only read it. The pending unit lives under
`.pending/<jobId>/managed_<name>/` and contains `source.gltf` and a
one-entry catalog-source v1 fragment. The source key is
`content:managed_<name>/source.gltf` before and after registration.
The fragment is the managed root GUID's authoritative record. Copying
source bytes into pending storage creates no registration. Committing the
unit renames it without replacement to `registered/managed_<name>/` on
the same volume. Registered units are immutable in this slice. Back them
up alongside hand-authored scene sources.

An opt-in Windows configure reads registered units as data and, for an
import job, the one host-selected pending unit if present. The host owns
that temporary selection. A normal opt-in configure reads committed units
only. Generate the effective catalog-source v1 in deterministic catalog
order and feed the existing imported-scene declaration, importer,
cook-manifest and full assembly. Omit the overlay argument in this mode.
Keep Spec 0085's seed/staging match for
the base producer configuration. Freeze the managed producer set for
each seed build or generation. Compare job inputs with that selected set
and the allowed registration delta, including the normalized source keys.
Keep the checked-in source catalog, default build and output formats
intact. Regenerate the staging graph for each later job from committed
units and its own optional pending selection. A stale pending selection
cannot carry over to another job or a clean build.

Before committing registration, run the existing whole-catalog checks.
Every old cooked record must retain its `(GUID, type, structured source)`.
As in Spec 0085, whole-graph cooking may change old artifact bytes and
dependencies subject to ordinary validation. The new record set must
equal the admitted producer's complete validated fragment. Check each
addition against the expected root source and recompute its v8 GUID from
the canonical importer subkey. Require the mesh and scene, plus any
material and fallback produced by that import. The root itself is a
source entry. Reject missing or unrelated records, rekeys, duplicate
logical sources, GUID or AssetId collisions, broken dependencies and
unsafe output locations. A clean opt-in build reproduces the same
identities. An explicit register retry after a clean pre-registration
failure may mint again. A committed root keeps its GUID across resume,
rebuild and clean build.

**R3 — Settlement, recovery and activation.** Reuse Spec 0085's one worker
and shared job slot, contained fixed CMake invocation, 30-minute deadline
and generation copy/rename. Record the staging dirty marker before any
staging mutation and the project dirty marker before any project mutation.
Record enough local ownership data to identify the staging and publication
trees to repair. Either marker for the selected roots blocks starts. The
project marker also blocks a later host using that project through a
different staging tree. The markers survive process loss.
They are repair evidence, separate from the in-memory job receipt.

Prepare and validate the complete generation before the registration rename.
If interruption wins before the first rename, publish nothing. Once the
first rename starts, settle its result before returning. A successful
registration rename proceeds to the generation rename under the same
commit guard even if orderly shutdown or the deadline arrives. A refused
registration rename bars publication. On resume, the generation rename is
the only commit point.

| Observed boundary | Durable state and next action |
|---|---|
| Failure before registration | No new registered unit or selectable generation. Discard the private candidate after a verified clean settlement and allow an explicit register retry. |
| Registration committed, publication failed | Retain source and GUID. Return the committed root in the failure. With validated staging and clean settlement, an explicit resume can publish it. |
| Publication committed | The generation is selectable even if the process exits before returning a receipt or clearing the markers. A known successful rename remains success. |
| Interruption, invalid staging, or uncertain settlement | Keep the applicable markers and refuse mutation until offline repair. A lost receipt remains unknown. |

Clear markers only after settling the child tree, checking the durable
outcome, and retiring the temporary selection. If cleanup fails, keep
admission blocked. Offline repair verifies child exit, inspects the
registered unit and completed generations, and validates or recreates
staging before clearing both relevant markers. Incomplete pending
directories are never selectable. Process-loss recovery covers the
registered/published states above, with no new power-loss guarantee.

Resume reads the committed source with its original GUID and never consults
the inbox. It is allowed only for the sole committed root ahead of the
selected snapshot. After the lock and repair checks, a `Register` for
an already committed name returns `NameConflict` before checking whether
activation is required. If the complete generation is known, select it
explicitly.
Otherwise resume may publish an equivalent new generation. With committed
roots ahead of the selected snapshot, fresh registrations return
`ActivationRequired` and rebuild returns `Unavailable` through the
existing contract. If multiple roots are ahead, select a compatible newer
generation or regenerate an opt-in seed offline from the committed units.

Bind an import-enabled publication root to the stable project, checkout and
build configuration. The new private manifest version describes the
generation's effective catalog source, managed membership, producer mapping
and generated source locations. Check that these agree with the selected
catalog and safe generation-relative files. Generation metadata uses logical
source keys and relative output locations. Determine active managed
membership from this frozen view, or the matching configured seed's frozen
view, never by scanning the current registry into the active snapshot.
Older complete generations remain selectable after later registrations or
while repair is required. Spec 0085 profiles and generations keep their
existing manifest rules.

Publication leaves the old Runtime, its first discovery query, running scene,
document and undo history on their existing snapshot. An explicit restart
with the existing profile and generation arguments selects the new catalog
for load, apply and discovery. Imported generated sources come from that
generation. The launch scene for this workflow is an existing supported
hand-authored scene. The Inspector finds the mesh through existing target
type metadata and `assets.list/get`. Edit, apply/reload outcomes, recovery
and undo/redo keep Specs 0082–0084 semantics. Save persists the derived
GUID in the hand-authored source. Reopen that source and apply it again under
the selected generation. Save leaves the cooked scene and generation intact.
Unsaved state and session history end at restart.

**R4 — Shared operations.** Add `assets.import { name, mode }` and
`assets.importStatus { jobId? }` on `atlantis.remote/1`. Mode is the closed
set `Register` or `Resume`. Start returns an admitted `Running` receipt
with a job ID, name and mode, or refuses without admitting work. Use the
existing typed `ok`/`err` envelopes. A job has `id`, `name`, `mode` and
`state`. Status returns `activeGeneration` and `job`, each nullable. An
omitted job ID asks for the latest import job. Each admitted job reaches one
terminal state while the host remains available. Only `Succeeded` carries
`rootGuid` and `generation`. Only `Failed` carries `failure` with
`stage`, `code`, `diagnostic`, optional `exitCode` and `registration`
(`NotCommitted`, `Committed` or `Unknown`). A `registeredRootGuid` is
present only for `Committed`, including a failed resume. An unknown
registration state requires repair. Keep the running job and 16 terminal
import receipts in memory independently of rebuild retention. IDs are
canonical non-nil GUIDs in their existing asset, job or generation domains.
A start reply must echo name and mode and be `Running`. An explicit status
reply must match the requested job ID. A disconnect or lost start response
does not cancel work. Latest status can aid investigation but cannot prove
that an unacknowledged request belongs to the caller. After eviction or
process loss the outcome is unknown. Retry is explicit.

Start refusals are `Unavailable`, `Busy`, `WorkspaceNeedsRepair`,
`CatalogUnavailable`, `NameConflict`, `ActivationRequired` and
`NothingToResume`. Status refusals are `Unavailable` and `UnknownJob`.
Malformed parameters use `BadRequest`. The failure stage/code pairs are
`Source/InvalidSource` (missing, unreadable, oversized or unsafe inbox input),
`Pipeline/LaunchFailed` (including failed process containment),
`Pipeline/BuildFailed` (including the importer's subset rejection),
`Validation/InvalidOutput`, `Validation/IdentityConflict`,
`Registration/RegisterFailed` and `Publication/PublishFailed`. Report
`TimedOut` in the interrupted stage. Keep shared diagnostics at most 4 KiB
of UTF-8. Detailed importer/cooker output stays in the local log. The host
uses the child exit result and does not classify failures by parsing logs.

Expose the typed values in both SDKs, CLI `assets import <name> [--resume]`
and `assets import-status [<JobId>]`, and closed MCP tools `assets.import`
and `assets.import_status` through the C# SDK. CLI and MCP keep the payload
on a queried terminal failure and signal a nonzero exit/tool error. Each
MCP tool makes one RPC. Keep existing methods, wire shapes and connection
setup unchanged. On these two new methods only, a well-formed call to the
pinned Spec 0085 host can map the exact top-level `BadRequest` /
`unknown or malformed asset query` error to client-local `Unsupported`
and keep the session. Other malformed replies and disconnects use existing
transport semantics. A new host without the opt-in returns in-band
`Unavailable`. In-process providers default to that refusal after
recompilation.

The Assets panel accepts an inbox name, offers explicit register/resume and
displays the receipt, registration state, unknown outcomes and restart
guidance. Use the existing worker and relevant-job polling, discard callbacks
from replaced sessions and never resubmit automatically.

### Non-functional

**R5 — Isolation.** Keep copying, parsing, configure/build, validation,
registration and publication off the frame thread. Bound the source read,
diagnostics and receipts. No new dependency, Android build host, RuntimeHost
pipeline dependency, frame-loop change or asset artifact schema change.
The workspace is for trusted local development. Direct builds, project
source edits and registry edits during a managed job are outside the
supported workflow.

## Proposed Design

Use one project registration unit per import and one isolated whole-catalog
build. Separate durable registration from immutable publication and keep
the existing explicit Runtime activation step.

## Architectural Impact

[ADR-0147](../adr/0147-managed-import-registration-and-publication.md)
records the additional catalog-source authority, shared surface, two
commit points and generation-local validation. It extends ADR-0097,
ADR-0098, ADR-0143 and ADR-0146 within the opt-in workflow.

## Alternatives Considered

ADR-0147 compares editing the checked-in catalog and build scripts, treating
the cooked generation as source authority, and client-side tool execution.

## Testing & Verification Plan

[Plan 0086](../plans/0086-new-asset-import.md#verification-checklist)
maps R1–R5 to fixture, shared client, failure, restart and scene persistence
checks and the existing implementation gates.

## Risks & Open Questions

Whole-catalog assembly and copying retain their time and disk costs. Durable
registration can lead publication after a failure. The project registry
must accompany saved scenes that reference it. Confirm these trade-offs,
the source subset and the shared surface in Plan 0086 J1–J4.

## Out of Scope / Future Work

Additional source shapes, authoring of imported scenes, preview,
project sharing, asset replacement and activation automation.
