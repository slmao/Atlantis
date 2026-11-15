# Spec: Duplicate a Scene Node and Its Subtree

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0102](../plans/0102-duplicate-scene-subtree.md)
- **Approval:** Pending joint review
- **Related ADRs:** None new. See Architectural Impact.

## Summary

Add **Duplicate subtree** to Hierarchy Actions. Copy the selected node
and all its descendants in the current hand-authored document as one
structural edit. The copy can be selected, undone, redone, applied, and
saved using the established authoring workflow.

## Motivation / Problem Statement

[0068](0068-authoring-editor.md) provides child creation, reparenting,
and subtree destruction. Structural redo already reconstructs nodes
from their recorded values. A user who wants a second copy of an edited
group currently has to recreate the group and re-enter its properties
and asset bindings.

## Goals

- Duplicate one selected node, including a leaf or a nested subtree.
- Preserve authored components, local transforms and asset references.
- Reuse the shared authoring contract and the Editor's structural history,
  apply recovery and hand-authored save paths.

## Non-Goals

Multi-selection, partial subtree copy, cross-scene or clipboard copy,
paste, prefabs, changes to imported documents through this action,
automatic offset or camera activation, and copy-time asset production.

## Requirements

### R1. Eligibility and feedback

Show **Duplicate subtree** in the existing Hierarchy Actions group
using [0093](0093-editor-visual-layout.md). Enable it for one live
selected node in an open hand-authored document. Disable it while a
Gizmo gesture is active or a duplicate request is settling. Report why
the action is unavailable or refused, including for an imported or
oversize subtree. The first release copies up to 64 nodes including
the root. Reject larger subtrees as a whole. This action limit keeps
schema reads, the command batch and structural history bounded. The
existing document and transport limits still apply.

An Inspector draft is handled by ordinary focus/commit rules. A
settled Inspector edit is its own history entry and can be part of
the observed source. The copy reads committed authoring values.

### R2. What the copy contains

- Give each copied node a distinct, fresh, explicit EntityGuid. Build
  the source-to-copy map before emitting commands. Retain the document's
  SceneGuid, source path and each referenced AssetGuid, including absent
  optional bindings.
- Place the new root under the original root's parent, or at scene root
  when the original has no parent. Append the copied nodes as one block in
  the relative document order of the originals. Remap each descendant's
  parent to its counterpart in the copy. Copy local TRS directly, including
  nonuniform or negative scale. Both subtrees initially occupy the same
  world placement.
- Copy the component set and every serializable leaf of Transform,
  Camera, Renderable and Light. An unreadable, unknown or unwritable
  leaf makes the whole copy ineligible. The current node-to-node
  reference stored per node is its parent. Retain the document-level
  active camera setting even if its node is copied. Leave the original
  subtree and all external references untouched. Refuse an unhandled
  node-reference field if a later schema exposes one.
- Name the copied root `<name> Copy`, or `Copy` for an unnamed
  source. Choose the first available name among its observed siblings,
  using `Copy 2`, `Copy 3`, and so on as the suffix or unnamed label.
  Use ordinal name equality and trim the source prefix at a Unicode
  scalar boundary to fit the existing 255-byte UTF-8 limit. Preserve
  descendant names exactly. Names are display labels. Concurrent
  label collisions are permitted by the existing schema.

### R3. Transaction and settlement

Capture the source GUID, worker, observed document-open lifetime and
request identity. Use the latest committed subtree observed on that
worker. Refresh document events and compare the existing document
revision before and after the source reads, including before submit.
Refuse if the observation fails, the source is missing, the reads
span a revision change, or the worker/lifetime is stale. A successful
same-source reopen invalidates the request when observed. A refused
reopen preserves the lifetime. A later selection cannot retarget it.

Build one existing authoring transaction from explicit `CreateNode`,
schema-addressed `SetProperty`, `SetNodeName` and `SetParent`.
Validation of the final candidate happens at the host. Limits such as
the directional-light, point-light and scene node caps can refuse the
entire transaction. Source observation and transaction admission are
separate calls. An external edit in their final gap can leave a still
valid copy of the observed snapshot. Existing authoring has no atomic
reopen or revision precondition on submit.

Settle the ticket and drain its failure range before history, selection
or automatic apply. A refusal leaves the document, history and selection
untouched by the copy and issues no apply. If settlement is unknown,
show the attempted root GUID and direct the user to inspect the document
before explicitly retrying. Keep the known document commit distinct
from a later lost apply reply. Neither path auto-replays duplication.

### R4. History, recovery and persistence

After confirmed settlement, verify the intended full copied state in
the same document-open lifetime. Record one structural entry for the
whole copy using created `NodeLife` records. Select its root in
Hierarchy/Inspector once it appears, unless a later selection or
document change has taken precedence. If the result cannot be verified
against the intended state, report the known commit and unavailable
history. Skip automatic apply when the post-commit observation fails,
the lifetime changed, or a different revision has already overtaken
the copy. Use the ordinary explicit Apply path for the current document.

Pass a verified commit to the existing
[0083 apply controller](0083-authoring-apply-outcomes.md). Preserve its
pending, suspended, superseded and unknown-result rules. A failed reload
leaves the copy, its recorded history and the last successful Runtime
projection intact. Explicit Retry/Apply targets the current document
and creates no additional subtree.

One Undo removes the entire verified copy. One Redo recreates the same
GUIDs, values, names, recorded order positions and parent links with the
existing position and external-change guards. Changes to a copy's fields
or children, a missing parent, or a GUID conflict can refuse a history
step. Use ordinary selection cleanup and redo behavior. Save explicitly
using the existing authoring save path. Reopen the same scene and asset
generation to verify persistent GUIDs, properties, links and active
camera. Reopen clears local history under the existing policy.

## Architectural Impact

None. Compose the same shared queries, transaction, failure drain, apply
and save operations in Editor, CLI (`scene tx`), SDK and MCP. The
Hierarchy action adds no new RPC, command, event, schema, file format or
Runtime/World operation. CLI, SDK and MCP clients can read the same
authoring state and submit the same command batch. History and selection
remain Editor-local. Existing decisions cover document ownership and the
command set, structural replay, and correlated apply.
[ADR-0130](../adr/0130-authoring-access-interface-and-module-boundary.md) ·
[ADR-0135](../adr/0135-two-document-edit-and-apply.md) ·
[ADR-0136](../adr/0136-structural-edit-records.md) ·
[ADR-0144](../adr/0144-correlated-authoring-apply-outcomes.md)

## Alternatives Considered

- A host-owned duplicate command could capture the source and mutate it
  at one revision. It requires a shared protocol and history extension.
  Reconsider if users need atomic source capture or larger subtrees.
- Creating each node in its own transaction can expose a partial copy
  and makes a single rollback unreliable. A bounded batch keeps the
  existing all-or-nothing validation.
- Offsetting the copy or switching cameras would add a placement policy
  and could affect the user's view. The copied root's Hierarchy
  selection distinguishes initially overlapping copies.

## Testing & Verification Plan

[Plan 0102](../plans/0102-duplicate-scene-subtree.md#verification-checklist)
maps R1–R4 to model, shared-client and real Windows 100% verification,
including save/reopen and a correlated reload failure. Joint review
settles the choices in the Plan before implementation.
