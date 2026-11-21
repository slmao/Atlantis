# Spec 0133 — Update a Prefab Source and Synchronize One Instance

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan:** [Plan 0133](../plans/0133-prefab-source-sync.md)
- **Approval:** Maintainer approval of `e9235765` on 2026-11-22. [Joint approval](https://github.com/slmao/Atlantis/pull/429#issuecomment-7066200488)
- **Related ADR:** [ADR-0182](../adr/0182-prefab-source-updates-and-sync-baselines.md) (Accepted)

## Summary

Update an existing static Prefab from one authored subtree, then explicitly
synchronize one scene instance to the updated definition. Keep the selected
instance's root identity and placement. Refuse synchronization when managed
parts of the instance have local edits or their state cannot be established.

## Motivation and existing capabilities

[0132](0132-scene-prefabs.md) registers a bounded subtree, exposes a validated
frozen Prefab through Assets and instantiates expanded nodes in one guarded
authoring transaction. Its persisted source GUID identifies the Prefab, but
does not establish whether the instance has been edited. It also has no
operation that replaces a registered definition or an existing linked subtree.
Reuse its capture limits, source layout and catalog carrier, the shared
document revision guard and verified history, and the
[0090](0090-saved-scene-build-open.md) Build/Open workflow.

## Requirements

### R1 — Update one registered static Prefab

From a visible existing Prefab in Assets, offer **Update from Selected Subtree…**
with a confirmation that names the Prefab and the single visible Hierarchy
donor. The donor must be an independently authored static subtree eligible for
0132 capture. Apply the same 64-node and 64 KiB limits, type restrictions,
lossless validation and typed mesh/material dependency checks. Keep edits
already committed to Authoring eligible even if Apply has failed. Block
active drafts or gestures without committing or discarding them. Capture
against the named document occurrence and revision. Recheck the intended
donor, asset and session before sending. Later edits cannot retarget a capture
that was already admitted.

Require the explicit 0132 trusted Windows development Runtime permission for
Prefab writes. The update uses the existing asset GUID, logical name, unit
path and exact catalog fragment. It atomically replaces the canonical
`source.scene.txt` in the owned unit. Keep the donor and all open document,
history and Runtime state unchanged. Use the frozen source revision from the
current build as the expected revision. Refuse missing, ambiguous or changed
registrations and invalid candidates before replacement. Identical source
bytes settle as unchanged. Use the existing task feedback to report committed,
unchanged, refused or unknown outcomes. A success supplies the asset GUID,
prior and new source revisions and captured document revision. An unknown
outcome supplies the GUID and target path, plus revision information when
known, and asks the user to verify checkout state after the request ends.
Neither the client nor host retries an uncertain write automatically.

### R2 — Load the updated definition explicitly

Tell the user to run an explicit **Build** and **Open** of the saved scene
through 0090. The old Runtime continues serving its original definition and
revision even if the checkout changes. [0123](0123-saved-scene-build-reuse.md)
must reject reuse of a receipt whose Prefab input changed. A failed
Build or Open leaves the updated checkout source available for a corrected
Build/Open and keeps any still-running projection usable. After activation,
the same GUID resolves to the new validated source and cooked carrier. A
second distinct source update requires a build that has activated the first.

Keep the exact existing `assets.prefab` reply. Provide an additive read of the
source revision from the same frozen snapshot used for the definition and
synchronization admission. Use shared operations for source update, revision
read, guarded synchronization and replay across Editor, CLI, native and C#
SDKs and MCP. Updated clients check support before mutation. Keep existing
operations and their replies usable on the updated Runtime. Notify users of
the authoring-source compatibility boundary in ADR-0182.

### R3 — Synchronize one instance or explain the refusal

Offer **Synchronize Instance…** from the Prefab source section of Inspector
for one visible linked root. Keep its GUID, parent, exact local TRS, display
name and order relative to retained nodes. With the same parent transform
this preserves its world placement. Apply the definition's root mesh/material
and complete ordered descendant content. Allocate fresh descendant GUIDs
for a changed definition. Put those descendants immediately after the root
in template order and preserve the relative order of all nodes outside the
replaced descendants. This operation leaves all other instances unchanged.
Reject unsupported static data, an invalid tree, unresolved typed assets,
external references to descendants scheduled for replacement, and documents
or definitions that changed after the request was prepared.

Persist a checked baseline of the instance's managed content at updated
instantiation and after successful synchronization. Tracked instantiation
Undo/Redo carries the same baseline with its existing verified GUID block.
The managed content is the root mesh/material plus the descendants' GUIDs,
relative order,
membership, parents, names, exact local TRS and mesh/material values.
Selection, root name and placement, and unrelated document data are outside
this comparison. A mismatch with the baseline is a local-edit conflict.
Show the conflict before writing and keep the visible instance intact. An
equal managed payload and active definition yields **Already current** and
leaves identities, history and Apply unchanged.

A provenance-only 0132 instance can establish its baseline from
**Synchronize Instance…** only while its managed payload matches the active
definition after ignoring template-vs-instance node identities. Explain the
matching requirement before confirmation. Record this as one undoable
metadata-only edit. When the payload differs, refuse and explain that this
instance lacks a verifiable baseline and can be prepared against a matching
build. There is no force or merge option.

### R4 — Settle, undo, recover and persist as one edit

A synchronization prepares both bounded subtree states and commits one
shared transaction against the same document occurrence/revision, root GUID
and frozen source revision. The Editor repeats selection, row incarnation
and interaction guards after any asynchronous reads. A stale, conflicted or
invalid request changes zero nodes and leaves existing selection, history
and Apply untouched. Late replies cannot retarget a new document or root.

A confirmed synchronization contributes one verified Undo entry before
refreshing the UI. Undo/Redo restore exact before/after GUIDs, topology,
slots, component values and baseline as a unit. Replay requires the expected
affected state and document ordering, resolvable dependencies and a current
revision. Refuse a conflict without moving the history cursor. Unrelated
value edits are kept. Replay restores the captured scene state even if a
newer definition is available. After a possible submission with an unknown
outcome, identify the instance root and requested source revision, retain
existing history, and direct the user to inspect before retrying. A
confirmed commit with insufficient read-back is reported separately with
Undo unavailable.

Use [0083](0083-authoring-apply-outcomes.md) to settle a confirmed structural
edit's Apply independently. Keep the edit and verified history if Apply
fails or is unknown. Explicit Apply recovers the projection without
replacing nodes again. Baseline-only preparation changes the source document
and its history without scheduling Apply. Save, Save As and
[recovery](0122-hand-authored-scene-recovery.md) preserve the baseline.
After save and reopen, retain both instances, their placements, identities,
bindings and source association, and use the same conflict rule on a later
synchronization.

## Architectural impact

[ADR-0182](../adr/0182-prefab-source-updates-and-sync-baselines.md) records the
checked source replacement, snapshot revision, shared transaction modes and
authoring-only source v10. The Renderer, RenderGraph, World and cooked Scene
projection keep their existing contracts.

## Testing and verification

[Plan 0133](../plans/0133-prefab-source-sync.md) maps R1–R4 to shared and
Windows 100% checks. Use two independently placed instances of one table
and four chairs to distinguish explicitly synchronized content from the
untouched instance. Verify actual saved and reopened values, including
local-edit conflict after reopen.

## Out of scope

Automatic propagation, stored instance overrides, conflict merging,
nested Prefabs, variants, bulk synchronization, cross-checkout links,
publishing to a named generation and a source-history editor.
