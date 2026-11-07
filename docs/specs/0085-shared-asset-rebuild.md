# Spec: Shared Rebuild of Existing Assets

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-07
- **Related Plan:** [Plan 0085](../plans/0085-shared-asset-rebuild.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0146](../adr/0146-managed-asset-rebuild-and-generation-publication.md) (Proposed)

## Summary

Let a local Windows development Runtime accept a rebuild request for a GUID
already in its cooked catalog. The existing Tools pipeline rebuilds that
asset's registered producer in an isolated build tree. A complete validated
output becomes available as a named generation. An explicit restart selects
that generation for use by Runtime and its clients.

## Motivation / Problem Statement

The Assets panel and shared queries can discover existing outputs. Rebuilding
still requires a separate manual build. The normal CMake graph runs the cooker,
glTF import and cook-manifest steps, and whole-build catalog assembly. Its
outputs are mutable, including the imported tree that CMake removes and
recreates. Running that graph against the active Runtime tree can therefore
change files underneath the fixed discovery snapshot or a scene reload.

## Goals

- Request and observe one existing producer rebuild using the same contract
  from Editor App, CLI, native and C# SDKs, and MCP.
- Keep stable asset identities and the existing producer configuration.
- Give the operator separate, observable build and activation outcomes.

## Non-Goals

New asset registration, removal or rename, GUID minting, import option editing,
topology changes that add/remove/re-key outputs, asset/source editing in the UI,
automatic watching, distributed jobs, cancel, queueing, live asset replacement,
remote restart, deployment and automatic generation cleanup.

## Requirements

### Functional

**R1 — Existing asset and producer.** A request names one non-nil cooked
AssetGuid from the active ADR-0143 snapshot. Resolve its existing source
catalog root and configured producer through data generated from the same
CMake declarations as the build. A derived asset selects its registered glTF
import as a unit, including generated scene source and sibling outputs.
No wire input names a filesystem path, tool, build target, option or command.
Unknown GUIDs and records without a supported configured producer are explicit
refusals. A cooked catalog that failed its initial load keeps the existing
restart requirement and cannot authorize a rebuild. A first start uses the
same one-time snapshot initialization as discovery. The worker owns its
immutable inputs and does not borrow session objects or mutable catalog state.

Execute the selected producer even when its normal incremental target is
current. Invalidate only its declared completion stamp in the isolated build
tree, then invoke the existing `atlantis_asset_catalog` target. Its existing
dependency rules decide other work and its assembly checks the full output.
A job can include other graph changes picked up by that build. It has no
single-file isolation promise. Compare the staged catalog's full
`(GUID, type, source)` identity set with the active snapshot and refuse
publication on any difference. Dependencies and artifact bytes may change
subject to the existing build validation. Changes to an import that add or
remove derived outputs require a later workflow.

**R2 — Ownership and job outcome.** Opt in locally with a trusted Windows
development launch profile. It selects a preconfigured, dedicated staging
build tree and its configuration, and a separate publication directory. The
host verifies the canonical checkout, generator configuration and content
roots against the Runtime's launch build. It launches the pinned CMake
executable with fixed arguments and no shell. The ordinary launch keeps
discovery and serves rebuild management as unavailable. Android exposes
no build host.

A single executable-private worker owns one in-flight job. All clients of
that Runtime share it. The host claims the workspace writer lock at bootstrap
and releases it after orderly shutdown and child settlement. A second host
can read its selected generation, but its rebuild starts return `Busy` until
it is restarted and can claim the workspace. A start request returns a unique,
non-reused job ID and `Running` immediately after admission, or refuses
without creating a job. A competing start, including a duplicate request,
returns `Busy`. No queue or implicit retry. An admitted job reaches exactly
one of `Succeeded { generation }` or
`Failed { stage, code, diagnostic }` while the host remains available.
Failure stages distinguish launch, pipeline, validation and publication.
Use host-generated diagnostic text (at most 4 KiB of UTF-8) and the child
exit code when available. Raw tool output stays in a local log. A 30-minute
elapsed deadline covers the child and publication
preparation. Report timeout only after stopping the child tree. Publication
commit settles success once the directory is renamed. The host owns child
and descendant lifetime, including termination on host loss.

A status request with an ID returns that job or `UnknownJob`. Without an ID
it returns the latest admitted job or none. A successful status also reports
the active generation, or no generation for the ordinary build tree.
Retain the running job and the most recent 16 terminal records in memory
for this Runtime process. Disconnect does not cancel a job. After a lost
start response, query latest status before any explicit retry. Without an ID,
another client's matching request cannot establish ownership of the lost
receipt. After host loss or eviction an unavailable result is unknown,
with no inferred success or failure. Shutdown stops the child tree and
never promotes unfinished output. An abandoned workspace lock after host
loss requires local recovery after verifying the owner and children have
exited. The next attempt reruns the producer and whole catalog validation.

**R3 — Publication and activation.** Build only in the dedicated tree,
which is neither the active catalog's tree nor a published generation.
After successful assembly, use the existing catalog reader to validate
identities and enumerate its records. Copy the catalog, every referenced
artifact and sidecar, and the configured imported authoring scene sources
into a fresh staging directory under the publication root. Preserve their
catalog-relative locations and copy files rather than sharing mutable
file storage. Refuse missing files, path escapes and link traversal.
Write a private completion manifest in staging with the profile identity,
generation ID and relative imported scene source locations. Publish the
complete directory by a same-volume rename to the generation ID. Failed or
interrupted preparation leaves all previously published generations and
active files intact. Incomplete directories are never
selectable. After a crash following publication, a complete generation can
exist without a retained job result. Local operator recovery may inspect
complete generations and select one explicitly. Filesystem failure, including
space exhaustion, is reported. Retention is operator-managed offline after
all consumers of the affected generations have stopped.

Successful publication never selects an active generation. A local operator
restarts the Runtime with the same trusted profile (`--asset-build-profile`)
and explicit generation ID (`--asset-generation`). A profile without a
generation uses the existing compiled-in catalog to seed the first rebuild.
This initial tree is kept separate from the staging build and must not be
rebuilt in place while the Runtime is using it. Runtime rejects an unknown,
incomplete or foreign generation before serving its session and uses
that one catalog path for initial scene load, authoring reload resolution
and discovery. Imported authoring scene sources come from that same
generation. Hand-authored source still opens and saves through its existing
source path. A failed activation uses existing startup errors, leaves
published files intact and permits an explicit restart with the previous
generation. There is no fallback to another generation during startup.
The existing process keeps its loaded assets and its original catalog
snapshot, including first query after a successful build. Apply, Save,
reconnect and Reload view never select a generation. Publish does not
affect the document, running projection, pending apply or history. On
restart, unsaved changes and session-local history do not carry over.
A save after a build does not revise the published cooked scene.

**R4 — Shared clients.** Add `assets.rebuild { asset }` and
`assets.rebuildStatus { jobId? }` on
`atlantis.remote/1`, with owned typed values in Connection and both SDKs,
CLI `assets rebuild <GUID>` / `assets rebuild-status [<JobId>]`, and two
closed MCP tools (`assets.rebuild`, `assets.rebuild_status`) through the C#
SDK. Keep old `assets.list/get` and existing
wire shapes unchanged. A new operation on an old host follows the existing
unsupported-method path. Distinguish host refusals and failed job outcomes
from protocol/disconnect failures. CLI start success means admission.
A queried terminal failure returns nonzero even though status was obtained.
MCP reports the same job facts without tool-side execution or polling.

The new typed replies follow existing `ok`/`err` envelopes. Job and
generation IDs are canonical non-nil GUID strings in their own domains.
Start returns a job value with `id`, requested `asset` and `state`. Status
returns `activeGeneration` and `job`, each nullable. A job carries `generation`
only on success and `failure` only on failure. Start refusals are `Unavailable`,
`Busy`, `CatalogUnavailable`, `UnknownAsset` and `UnsupportedProducer`.
Status refusals are `Unavailable` and `UnknownJob`. Malformed parameters
use existing `BadRequest`. Failure carries `stage`, `code`, `diagnostic`
and optional `exitCode`. The closed failure pairs are launch/`LaunchFailed`,
pipeline/`BuildFailed`, validation/`InvalidOutput` or `IdentityChanged`,
publication/`PublishFailed`, and `TimedOut` in the interrupted stage. Stage
tokens are `Launch`, `Pipeline`, `Validation` and `Publication`.

The Assets panel starts rebuilds for its selection, displays job/stage or
terminal diagnostics and the restart-required generation, and tracks a known
job by explicit shared status polling while relevant. It uses the existing
worker and discards late results after session replacement. Busy, unsupported
and transport failures are visible. A trusted local Editor launch can pass
the same Runtime profile and generation options when opening a new process.
There is no editor-only build or activation endpoint.

### Non-functional

**R5 — Isolation and delivery.** Process launch, output draining, build,
copying and assembly stay off the Runtime frame thread. Only the existing
first-query catalog load, bounded job admission and status work are on that
thread. Both SDKs remain ordinary clients. All operations use the existing
authenticated loopback boundary.
Opt-in grants the token holder access to the same fixed build operation as
every other client. The staging tree has one managed writer. Direct builds in
that tree and edits to source, content or build configuration during a job are
outside the supported workflow. Save and other editing commands keep their
existing rules. The host does not promise a source snapshot when inputs are
changed externally during a build. Keep existing asset and catalog formats,
Android packaging, World, renderer and frame scheduling.

## Proposed Design

Use CMake's existing producer declarations to expose a private lookup for
the Windows build host. The host controls the dedicated build workspace and
publishes self-contained outputs. A new Runtime process chooses one generation
at bootstrap, giving its loader and discovery the same immutable files.

## Architectural Impact

[ADR-0146](../adr/0146-managed-asset-rebuild-and-generation-publication.md)
records the shared API, executable-private worker, workspace ownership,
publication and startup boundaries. It extends the build and client decisions
in ADR-0098 and ADR-0143 while preserving the read-only discovery snapshot.

## Alternatives Considered

ADR-0146 compares in-place builds, a per-client build path and live replacement.

## Testing & Verification Plan

[Plan 0085](../plans/0085-shared-asset-rebuild.md#verification-checklist) maps
R1–R5 to real builds, shared clients, failure and activation checks.

## Risks & Open Questions

Whole-catalog validation and copying cost disk and time, especially for
Bistro. Joint review must accept the scope, fixed timeout, publication and
offline retention rules. A build can succeed while Runtime startup fails
because scene compatibility is only decided by the loader.

## Out of Scope / Future Work

New asset registration, topology changes, import settings, preview, automatic
activation and automatic cleanup can be considered independently.
