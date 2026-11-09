# Spec: Save As for Hand-Authored Scenes

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-10
- **Related Plan:** [Plan 0088](../plans/0088-scene-save-as.md)
- **Approval:** Pending joint human review
- **Related ADR:** [ADR-0148](../adr/0148-scene-save-as-and-explicit-launch.md) (Proposed)

## Summary

Save As copies the current hand-authored document into a new, named scene
unit with its own scene GUID. It leaves the active document and Runtime
bound to the original scene. The operator can select one saved unit in a
separate development build, start that scene by name and open it through
ordinary authoring.

## Motivation / Problem Statement

[Spec 0065](0065-authoring-data-layer.md) saves a hand-authored document back
to its bound source. The Runtime starts from the cooked catalog and a
compiled scene whitelist. Its authoring host opens only the source of the
scene it loaded. Copying a source file alone supplies neither a catalog
identity nor an entry for that startup path. Users need a shared way to
save a variant without changing the running scene or the original file.

## Goals

- Capture the accepted document, including unsaved edits, as one independent
  hand-authored scene with stable identity and existing asset references.
- Make the copy buildable, launchable and editable through the existing
  development and authoring paths.
- Give all clients the same authority, outcome and recovery rules.

## Non-Goals

Imported-document conversion, blank-scene creation, arbitrary host paths,
overwrite, rename or delete, automatic build or publication, activation of
the copy in the current Runtime, multiple open documents, a scene browser,
and Android authoring. The Windows development build selects one saved unit
at a time. The same workspace can store multiple distinct units.

## Requirements

### Functional

- **R1 — Destination and authority.** A trusted local Windows launch opts
  in with proposed flag --allow-scene-save-as. Its authority is limited to the
  existing development checkout's compiled assets root, under the
  operator-created directory assets/scenes/saved. Clients provide a logical
  name of one to 48 ASCII characters matching [a-z][a-z0-9_]*. It identifies
  assets/scenes/saved/save_<name> with the fixed leaves scene.scene.txt and
  catalog-source.txt. Requests cannot supply a path, suffix or root. The host
  verifies containment through preparation and commit using the trusted root
  and rejects unsafe roots, reparse points, alternate paths and name aliases.
  The opt-in authorizes the existing in-process and authenticated loopback
  authoring clients on that host. Installed or unconfigured
  hosts refuse the operation. A source is eligible only when the active
  session has an open hand-authored document.
- **R2 — Snapshot and identity.** Capture the current accepted document and
  its document revision when the host services the request. The capture
  includes settled edits and excludes client drafts and Runtime-only changes.
  Serialize through the ordinary lossless scene path and emit a one-entry
  catalog-source v1 fragment of type scene, rooted at assets, for the new
  source. Mint a fresh scene AssetGuid through the existing Asset System
  authority. Preserve every EntityGuid, node order, name, parent, active
  camera, property and asset reference. The existing scene-scoped identity
  pair distinguishes corresponding nodes in the two scenes. No asset is
  copied, remapped or added to the active catalog. Copy completion attests
  source settlement. Build and Runtime still validate dependencies and
  loadability.
- **R3 — Commit, conflicts and recovery.** Prepare the scene and catalog
  fragment in a private sibling directory and publish the entire unit with a
  same-volume, atomic no-replace directory operation. A name held by a file,
  directory, incomplete unit or case alias conflicts. Simultaneous same-name
  requests have one winner and the others refuse. A collision in scene GUID
  or derived catalog key must never overwrite an identity. Validate uniqueness
  against known registrations and recheck it at configure against the complete build
  input set, using the existing catalog rules. Failed preparation or commit
  leaves no final unit from that request. Clean up owned temporary state on
  ordinary failure. A crash may leave a private temporary unit, which builds
  ignore. A lost response leaves the client outcome unknown. Inspect the
  final unit locally. An existing complete unit keeps its GUID, and repeating
  the name refuses. After verifying there is no final unit, an operator can
  explicitly retry. No automatic replay or overwrite.
- **R4 — State and shared contract.** Success reports the new scene GUID,
  asset-root-relative source path and captured document revision. The original
  remains the current document on success and failure, with the same source,
  document revision, selection, history and ordinary Save target. Save As
  neither marks the original saved nor emits DocumentReopened. It requests
  no Runtime apply, cancels no pending apply and changes no Runtime
  projection, generation selection or catalog. Existing pending work can
  settle independently. The Editor action settles normal focused edits
  through the existing workflow before making the request and reports the copy
  and explicit build/launch instructions. Guard stale session callbacks and
  duplicate UI activation while a request is pending.
- **R5 — Build and reopen.** A separate Windows development build explicitly
  selects one completed unit with the proposed CMake input
  ATLANTIS_SAVED_SCENE=<name>. Configure validates the fixed unit and exact
  catalog entry and composes it with the normal catalog sources before
  declaring producers. Use the existing scene cook and catalog assembly.
  Include the same managed-import inputs when the source references 0086
  assets. Missing dependencies follow the existing build or startup failure
  path. In that build, the existing --scene command accepts the reserved
  name saved_scene. Its selection is bound to the saved unit's catalog GUID.
  This build's authoring host uses the effective catalog source, which
  includes the saved slot, in place of its usual checked-in source catalog.
  Build the normal atlantis_runtime target in the separate tree. Start a new
  process from this build with --scene saved_scene and the
  ordinary authoring options, then call the existing authoring.open. The
  bound source is the saved unit, so ordinary Save now updates that source.
  The fresh session starts with its own history. The saved-slot build
  rejects --asset-build-profile and --asset-generation in this slice. The
  source host can still save a copy from a profile-backed session.

### Shared client surface

Add proposed authoring.saveAs { name } to the existing AuthoringConnection
and atlantis.remote/1 authoring surface. A successful result carries
{ scene, source, documentRevision }. Typed refusal categories are
Unavailable, InvalidName, NoDocument, ImportedSceneNotSavable, UnsafeTarget,
NameConflict, IdentityConflict and WriteFailed. Client-side Unsupported
represents the legacy BadRequest with message "unknown method authoring.saveAs".
Other malformed responses use ordinary protocol failure. Existing NoAuthoring
behavior applies when the host serves no authoring session. Transport loss is
distinct from a typed refusal. Expose the same operation in both SDKs,
CLI scene save-as, MCP authoring.save_as and Editor Save As. Preserve existing Save/Open and
old-client behavior. No transport or session file version changes.

### Non-functional

Preserve the existing serializer, catalog, artifact and wire framing
formats. Keep platform filesystem operations in the Windows executable
host. Use ordinary authoring request order and no additional scheduler.
Do not change default startup or default build inputs. The Plan retains
applicable regression, validation and performance gates.

## Proposed Design

Authoring owns snapshot selection and serialization. A narrow host sink
takes the owned source bytes and logical name. The executable owns
destination validation, GUID allocation through Asset System, fragment
writing and atomic publication. Return the copy identity and the captured
revision without changing the authoring binding. CMake consumes only the
explicitly selected complete unit. The existing executable whitelist binds
the opt-in startup alias to its GUID, so both Runtime load and authoring
lookup use the same catalog entry.

## Architectural Impact

[ADR-0148](../adr/0148-scene-save-as-and-explicit-launch.md) records the new
shared authoring operation, constrained host write authority and offline
catalog/launch boundary. Existing RuntimeHost, Asset System, scene grammar
and authoring ownership continue to govern the copy.

## Alternatives Considered

- Switch the active document to the copy. This would separate the document
  from the scene identity used by the current Runtime and its apply history.
- Let each client copy source files. This would lose shared validation and
  conflate client paths with server paths.
- Make the operator hand-edit catalog entries, CMake and the compiled
  whitelist for each copy. A single explicit build slot reuses those paths
  while making saved copies usable without per-scene code edits.
- Automatically rebuild and publish through the managed worker. This would
  couple Save As settlement to build, publication and activation.

## Testing & Verification Plan

[Plan 0088](../plans/0088-scene-save-as.md) maps the snapshot, filesystem,
compatibility and build contracts to focused checks and an end-to-end
Windows acceptance. A fixture deliberately exercises distinct original and
copy states, failure and restart, then reopens and edits the cooked copy.

## Risks & Open Questions

The name is a project-local handle. A saved unit depends on the same
registered asset inputs and must travel with them when moving a project.
Atomic visibility across process interruption is required. Power-loss
durability and reconciliation across project copies remain out of scope.
Joint review decides J1–J4 in the Plan, including the single build slot.

## Out of Scope / Future Work

Scene lists, choosing arbitrary saved scenes from one executable, integrated
build or activation, and additional source types require separate review.
