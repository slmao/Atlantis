# Spec: New Hand-Authored Scene

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-10
- **Related Plan:** [Plan 0089](../plans/0089-new-hand-authored-scene.md)
- **Approval:** Pending joint human review
- **Related ADR:** [ADR-0149](../adr/0149-create-hand-authored-scene-through-shared-authoring.md) (Proposed)

## Summary

Create one named, buildable hand-authored scene with a single active camera.
The shared authoring operation commits a source unit using the Windows
development authority from [Save As](0088-scene-save-as.md). The user then
builds, starts and opens that unit explicitly, leaving the original session
and its unsaved work in place.

## Motivation / Problem Statement

Save As needs an open hand-authored document to copy. It cannot start a scene
without inheriting that document's nodes and asset references. Authoring
opens the source of the scene already loaded by the Runtime. The scene cooker
rejects zero nodes, and a frame needs an active camera. A single camera can
give a new scene a valid source and a usable viewport without copying any
project asset references or introducing live scene switching.

## Goals

- Create a deterministic starter scene with fresh, durable identities.
- Keep existing work intact and use the same operation from Editor, CLI,
  native and C# SDKs, and MCP.
- Reuse the saved-scene build and launch path, ordinary authoring, mesh
  addition, Save, and apply recovery.

## Non-Goals

Untitled in-memory documents, source templates or cloning, arbitrary paths,
overwriting or deleting units, live scene switching, automatic configure/build
or process launch, changes to asset generation publication, and Android
authoring. There is no new dirty-document prompt, editor-only transport, or
scene browser.

## Requirements

### Functional

- **R1 — Authority and destination.** A Windows development Runtime that
  serves authoring and explicitly opts in with the existing
  `--allow-scene-save-as` flag may create a starter unit. Document that the
  flag also authorizes New Scene under the same root. A caller need not open a
  document or have a hand-authored source open; an imported document, if
  present, supplies no data. A host that cannot serve authoring still returns
  `NoAuthoring`; a host that serves authoring but has not opted in returns
  `Unavailable`. Reuse 0088's logical name validation, pre-existing pinned
  local NTFS root, path safeguards and two fixed leaves in
  `assets/scenes/saved/save_<name>`. New Scene and Save As share this
  namespace. A caller cannot specify a path, template or overwrite policy.
- **R2 — Starter content and identity.** Create exactly one root node named
  `Camera`, and make it the active camera. Its transform is position
  `(0, 0, 5)`, zero Euler rotation (looking along −Z toward the origin),
  and unit scale. Use vertical FOV 60°, near 0.1 and far 100 in scene units,
  with existing default exposure, fog and bloom. Include no lights, meshes,
  materials or other source asset references. Mint a fresh scene AssetGuid
  and camera EntityGuid through Asset System; neither is copied from the
  current scene, supplied by a client or derived from the requested name.
  Persist both identities in the existing scene source and catalog fragment,
  with the camera GUID distinct across creations. Produce ordinary lossless
  scene source and validate it using the existing in-memory cook before
  publication. The starter source requires no managed inputs; the saved-slot
  launch retains its ordinary Runtime defaults.
- **R3 — Commit and recovery.** Use 0088's private staging, shared
  workspace commit exclusion, scene GUID and derived AssetId checks,
  atomic no-replace publication and request-owned cleanup. A racing Create
  Scene and Save As for the same name have at most one winner. Typed refusal
  leaves no final unit from that attempt; interrupted private staging remains
  outside build selection. A successful rename commits even if the reply is
  lost. Apply 0088's inspection and retry rules: a complete unit may be used
  after validating both files and intended contents, but its existence alone
  does not identify the winning caller. For an absent unit, first establish
  that the previous request can no longer commit. Never replay automatically.
- **R4 — Shared result and current session.** Add
  `authoring.createScene { name }` to the existing AuthoringConnection and
  `atlantis.remote/1` surface. Return the ordinary in-band
  `{"ok":{"scene":...,"source":...}}` or `{"err":"<refusal>"}` form.
  Success carries a canonical non-nil scene GUID and the exact
  asset-root-relative `scenes/saved/save_<name>/scene.scene.txt` for that
  request. There is no captured revision. Refusals are `Unavailable`,
  `InvalidName`, `UnsafeTarget`, `Busy`, `NameConflict`,
  `IdentityConflict` and `WriteFailed` (including preparation failure).
  Client-side `Unsupported` maps only the legacy BadRequest with exact
  unknown-method message `unknown method authoring.createScene`.
  Malformed replies follow the existing protocol failure path; transport
  loss is not a typed refusal. Expose the operation as Editor **New Scene**,
  CLI `scene create <name>`, native/C# SDK methods, and MCP
  `authoring.create_scene`. Existing clients, Save, Save As and Open
  retain their behavior; there is no wire or session-file version bump.

  Success, refusal and unknown outcome leave the original document binding,
  saved source, accepted edits, selection, undo/redo, apply receipts and
  Runtime projection under their existing owners. Creation does not read,
  save, flush or discard the current document or client drafts; it emits no
  document reopen, adds no undo item and initiates no Runtime reload. Normal
  focus-loss edits and queued authoring/apply work can settle independently
  on their existing paths. The Editor prevents duplicate submission from
  one action, refuses to send a queued request on a replaced session, and
  discards stale callbacks for detached sessions. A request already sent
  can still commit. The Editor explains before submission that creation
  writes a new unit and does not open it; feedback gives the created
  identity or refusal/unknown outcome and the explicit next steps. It does
  not close the current document or Runtime.
- **R5 — Open, edit and restart.** The new source unit enters only the
  explicit 0088 saved-slot build: set `ATLANTIS_SAVED_SCENE=<name>`, build
  and start a separate `atlantis_runtime --scene saved_scene`. Until then
  the current Runtime and its catalog snapshot do not discover or activate
  the new scene. Existing saved-slot catalog, conflict checks and managed
  input rules apply. Ordinary `authoring.open` binds to the created source
  in the new process; the Editor starts with no selection and empty history.
  The new source is already on disk before any edit. Use 0087 Assets mesh
  addition, existing transactions and apply receipts/recovery, ordinary
  undo/redo and Save. Saving edits updates that new source; ordinary reopen
  and independent rebuild/restart retain the identities and saved content.
  Runtime reload failure leaves the new authoring document and history under
  the existing 0083 rules, with the previous projection intact until
  recovery. A build/startup failure cannot switch or close the old session;
  fix the input and retry the same unit.

### Non-functional

Keep the existing scene, catalog and artifact formats, dependency graph,
thread ownership and default startup. No per-frame creation work, new
scheduler, default build input or new publishing service. The operation
does not require any rendering changes.

## Proposed Design

Authoring owns the fixed starter document and uses the existing Asset
System minting and scene codec. Its opt-in host sink submits the owned
source bytes to the same executable-private publisher as Save As.
The caller receives a durable source identity rather than a new active
document. The existing saved-slot build consumes the new unit exactly as
it consumes a copy.

## Architectural Impact

[ADR-0149](../adr/0149-create-hand-authored-scene-through-shared-authoring.md)
records the additive shared operation and its authority, ownership and
activation boundary. It extends ADR-0148's publication boundary without
changing Save As semantics or the Runtime's current-scene ownership.

## Alternatives Considered

See ADR-0149 for the tradeoffs against an untitled document, automatic
activation and client-side source generation.

## Testing & Verification Plan

[Plan 0089](../plans/0089-new-hand-authored-scene.md#verification-checklist)
maps contract, filesystem and state checks to R1–R5. The live Windows
acceptance starts with unsaved edits in the old session and follows a newly
created unit through its first launch, mesh addition, edit and recovery,
ordinary Save/reopen and independent rebuild/restart.

## Risks & Open Questions

Approve the one-camera defaults, reuse of the existing development opt-in,
and deliberate separation of creation from activation. The separate build
costs a manual step; the UI must state that step without implying a new
document has opened. A lost result follows 0088's bounded recovery rather
than claiming exactly-once attribution.

## Out of Scope / Future Work

A scene switcher or in-memory new document would require a separate design
for unsaved work, authoring binding and Runtime projection changes.
