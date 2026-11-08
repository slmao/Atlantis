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
staging build and publication roots. Use an OS-exclusive writer lock on the
canonical import workspace as well as the existing staging lock. Acquire
both at bootstrap without waiting and hold them through worker settlement.
An import-enabled host lacking either lock may serve reads, but import and
rebuild starts return Busy. Validate local
roots and inputs before reading or mutating them. Refuse aliases across
roots, reparse points, unsafe paths and destination overwrite. Copy from the
inbox into private pending storage and validate the copy. Never move, delete
or edit the inbox file. The approved profile authorizes the existing
authenticated loopback clients equally. No request supplies an absolute
path, tool, build argument, external URL or CMake expression.

Accept one glTF 2.0 JSON file, at most 16 MiB, containing one scene with
one static mesh node and one supported triangle primitive. Buffers use
embedded base64 data URIs. Allow untextured material factors supported by
the existing importer. Refuse external buffers, images, textures, extensions,
skins, animation, cameras and overlays in this slice. Enforce the restricted
profile in the existing importer before it can load external resources.
The importer retains authority over supported accessors and geometry.

**R2 — Registration and build.** The worker allocates one v4
root GUID for an admitted register job using the Asset System mint. The build
and importer only read it.
Use the existing content source key
`content:managed_<name>/source.gltf` and the existing v8 derivation for all
outputs. Keep the source and a one-entry catalog-source v1 fragment together
in the pending directory. The fragment is the sole editable home of the
managed root GUID. Committing the directory registers both by one
same-volume no-overwrite rename into the workspace's registered directory.
Committed inputs stay immutable in this slice and must be backed up with
hand-authored scene sources. Pending directories are never registrations.

An opt-in Windows configure takes the trusted project workspace root,
consumes registered directories as data and feeds their declarations into
the existing imported-scene CMake helper.
For an import job it includes exactly the one privately selected pending
directory, if any. Materialize the effective catalog-source v1 from the
checked-in source and the selected fragments in deterministic catalog order.
Use that view for producer lookup, importer, cooker and assembly. Keep
Spec 0085's seed/staging check for the base producer configuration. Compare
managed producers separately against the registered set and selected
generation. The pending source is eligible only in the isolated job build.
Keep the checked-in source, default build and catalog formats intact. A clean opt-in
build from the same registrations must reproduce the identities. The build
uses the existing importer, cook-manifest and full catalog assembly. Keep
all outputs of the import, including its derived scene and generated source.

Validate the complete staged catalog before committing registration. It must
preserve the active catalog's full `(GUID, type, source)` identity set and
add exactly the derived records of the admitted root, including its mesh
and scene. The root is a source entry. Discovery exposes its cooked outputs.
Reject unrelated additions, removals, rekeys, duplicate source keys,
GUID or AssetId collisions, broken dependencies and unsafe output locations.
A rejected pending root is never visible. A later explicit register attempt
may mint again after a pre-registration failure. A committed root retains
its GUID even if publication fails.

**R3 — Settlement, recovery and activation.** Reuse the Spec 0085 worker,
contained fixed build command, 30-minute deadline, staging dirty marker and
generation copy/rename. Rebuild and import share one job slot without a queue.
Mark the workspace before the first mutation. Prepare and validate the
complete generation before the registration rename. Once that rename starts,
settle the short commit sequence through the generation rename even if
orderly shutdown or the deadline arrives. On resume the generation rename is
the commit point. Preserve both rename outcomes in a terminal result when
known.

A failure before registration leaves no new committed entry. Once registration
commits, a later failure retains the source and GUID and reports that
registration separately from publication. A new register for that name
returns a conflict. An explicit resume uses the registered copy and GUID,
with no inbox access, when it is the only registered root absent from the
active snapshot. It may publish an equivalent new generation if a prior
publication cannot be located. First try explicit selection of a known
complete generation. A known publication failure with validated staging
permits resume after clean settlement. Any interrupted, uncertain or invalid
staging state requires the existing offline repair before retry. Offline repair verifies
child exit, examines the committed directory and completed generations, and
repairs or recreates staging before clearing its marker. A process loss can
erase the receipt, including after either rename. Never infer success from
a lost receipt or mint over a committed name. Keep the existing clean
settlement and marker-cleanup behavior for a successful generation.

A profile with imports binds publication to the stable project, checkout and
build configuration. Each generation records its own effective catalog source
and producer/source mapping so later registrations cannot invalidate an old
generation. Validate its owned manifest, catalog, output locations and
generation-local metadata without consulting current registration membership. Use a new
private manifest version for these profiles, in a separately bound publication
root. Existing Spec 0085 profiles and their generation validation remain
usable. While the selected catalog lacks a committed registration, new
register requests return `ActivationRequired` and rebuild admission returns
`Unavailable`. Only the restricted resume above can publish in that state.
An older generation remains readable. If several registrations are ahead of
it, explicitly choose an appropriate newer generation or regenerate an
opt-in seed offline from the complete registry.

Success returns a generation, then the operator explicitly restarts with
the existing profile and generation arguments. The old Runtime, including
its first discovery query, retains its old catalog, scene, document and
undo history. A new process opens the selected catalog for Runtime load,
apply and shared discovery. Imported generated sources are bound to that
generation. The launch scene remains an existing supported hand-authored
scene for this workflow. The Inspector uses the existing target-type
metadata and `assets.list/get` to select the derived mesh. Edit, apply,
reload outcome and recovery, undo/redo and Save follow Specs 0082–0084.
Save persists the derived GUID in the hand-authored source. A new session
can reopen the saved reference under the selected generation. An ordinary
restart loses unsaved state and history under the existing rules.

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
present only for `Committed`. An unknown registration state requires repair. Keep the running job and 16 terminal import receipts in memory,
independent of existing rebuild retention. Preserve existing unknown-ID,
eviction and disconnect behavior and require explicit retry.

Start refusals are `Unavailable`, `Busy`, `WorkspaceNeedsRepair`,
`CatalogUnavailable`, `NameConflict`, `ActivationRequired` and
`NothingToResume`. Status refusals are `Unavailable` and `UnknownJob`.
Malformed parameters use `BadRequest`. The failure stage/code pairs are
`Source/InvalidSource` (including a missing inbox file or rejected input
subset), `Pipeline/BuildFailed`, `Validation/InvalidOutput`,
`Validation/IdentityConflict`, `Registration/RegisterFailed` and
`Publication/PublishFailed`. A `TimedOut` code uses the interrupted stage.
Keep diagnostics bounded to 4 KiB UTF-8 and raw tool logs local. Distinguish a failed
job from transport failure and an unknown receipt.

Expose the typed values in both SDKs, CLI `assets import <name> [--resume]`
and `assets import-status [<JobId>]`, and closed MCP tools `assets.import`
and `assets.import_status` through the C# SDK. CLI and MCP keep the payload
on a queried terminal failure and signal a failure exit/tool error. Reuse
the pinned legacy host's exact unknown asset query fallback on the new
calls only. Old clients keep their existing methods and wire shapes.
The Assets panel accepts an inbox name, offers explicit register/resume and
displays status, unknown outcomes and restart guidance. It uses the
existing worker and relevant-job polling, ignores callbacks from replaced
sessions and never resubmits automatically.

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
