# Spec: Reparent a Scene Subtree by Drag and Drop

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-15
- **Related Plan:** [Plan 0103](../plans/0103-hierarchy-reparent.md)
- **Approval:** Pending joint review.
- **Related ADRs:** None new. See Architectural Impact.

## Summary

Drag one selected node onto another node in Hierarchy to move its
subtree under that parent while keeping its world placement. Use the
existing authoring transaction, structural history and apply recovery.

## Motivation / Problem Statement

[0068](0068-authoring-editor.md) defines world-preserving reparenting.
The Editor currently asks users to pick up the source, select the
destination, then choose **Move under selection**. The pickup stores a
GUID without its document lifetime. After [0102](0102-duplicate-scene-subtree.md)
made copying a subtree a single action, users need a direct way to place
that subtree under another parent. Both entry points need the same
settlement and stale-target rules.

## Goals

- Move one selected subtree inside the current hand-authored scene by
  dropping it onto a node row.
- Preserve authored world placement, identities and descendant data.
- Keep reparenting a single edit across settlement, Undo/Redo and recovery.

## Non-Goals

Same-parent reordering, insertion above or below a row, a blank-area drop
target for moving to root, multi-selection, clipboard and cross-scene
movement, prefabs and live Runtime transform preview during drag.

## Requirements

### R1. Gesture and feedback

Start a drag from the selected node row in a live hand-authored document.
The whole row is a drop-on-parent target. Indicate the valid target using
the existing [0093](0093-editor-visual-layout.md) selection and focus
style. Invalid targets cannot accept a drop. Cancel or drop outside a node
to end the gesture without submitting. Suppress another reparent while
one is settling and prevent reparenting during an active Gizmo gesture.
Hovering reads the displayed tree and updates only the local indicator.

Keep **Pick up**, **Move under selection** and **Move to root** available
under their existing document eligibility. They use the same guarded
submission as drag and drop. **Move to root** supplies a null parent.
A successful drag keeps or restores selection to the moved GUID if a
later selection has not taken precedence. The buttons keep their
existing selection behavior. Show refusal, settling and recovery
feedback in Hierarchy.

### R2. Parent and world transform

Keep the source GUID, document order, components, asset references and
active camera. Descendant parent links and local transforms stay as
authored. Read the committed local translation, rotation and scale of
the source and both relevant ancestor paths using the current document.
Missing, unreadable or non-finite transform inputs refuse the edit.

Resolve the new local matrix as the inverse of the destination parent's
world matrix times the source's current world matrix. Moving to the root
uses the identity parent matrix. Use the existing engine TRS convention
and reparent tolerance from 0068. Refuse a singular or non-invertible
destination, or a local matrix that cannot round-trip as TRS within the
existing tolerance. The source and descendants then keep their world
placement within the existing tolerance under an unchanged scene context.

Build one transaction containing `SetParent` and all three
schema-addressed local Transform writes. Self, descendants, missing
rows and the current parent are invalid targets. Keep the host's
validation of the entire candidate authoritative.

### R3. Stale requests and settlement

Capture the worker, observed document-open lifetime, request identity,
source GUID and expected source parent at drag start or pickup. Resolve
the destination by GUID at drop or button activation. Reject a deleted
source or destination or a changed source parent. A successful reopen,
including reopening the same source, invalidates a captured gesture
and any stale completion. A refused reopen retains the lifetime. A
later selection never retargets a queued request. Use committed values
under the ordinary Inspector focus and draft rules.

On the worker, bracket the topology and transform reads with successful
document revision observations and event drains as in
[0102](0102-duplicate-scene-subtree.md#r3-transaction-and-settlement).
Immediately before submit, confirm the same observed lifetime and
revision and recheck gesture eligibility. The final observation and
submit are separate shared calls. A cross-client write in that gap can
race the edit; the host continues to validate the whole transaction.

Require a matching submit ticket and a successful failure drain before
recording history or requesting apply. Correlate errors to the ticket
range. A refusal from this action leaves the document, history and
selection as they were and issues no apply. If submission may have
reached the host and settlement is unavailable, show the source and
requested parent, report the result as unknown, and direct the user to
inspect the document before retrying. Guard the completion by worker,
lifetime and request. Never automatically resubmit a reparent.

### R4. History, apply and persistence

After confirmed settlement, verify the new parent and written local
values between successful reads of the same revision and document-open
lifetime. Record one structural history entry with the before and after
parent and local values. An unverified result gets a known-commit notice
with Undo unavailable for that edit and a prompt to Apply explicitly.
A confirmed edit in the same document invalidates Redo even when a
safe record cannot be made. Only a verified commit at the pre-submit
revision plus one may enqueue automatic apply.

Undo and Redo replay the recorded parent and local values under the
existing structural guards. They use the current ancestor transforms
and refuse conflicts covered by those guards. Pass settled edits into
the existing [0083](0083-authoring-apply-outcomes.md) controller. Pending,
coalesced and suspended applies retain their rules. A lost apply reply
leaves the known document commit distinct from the unknown Runtime result.
Reload failure keeps the committed subtree, history and last successful
Runtime projection. Explicit Retry/Apply targets the current document.

Save uses the ordinary hand-authored source. Reopen that source under
the same active generation, with a fresh local history, and Apply
explicitly to project the reopened document.

## Proposed Design

Translate the Avalonia gesture into the same reparent request used by
the existing buttons. The worker reads and validates the source and
target through the UI-free Editor Model, then sequences the existing
submit, drain, history and apply operations. Only the Editor owns the
gesture, feedback and selection.

## Architectural Impact

None. The Editor composes existing authoring and apply calls. CLI, SDK
and MCP clients can submit and settle the same closed command batch.
The design uses existing module boundaries, schema and source format.
[ADR-0130](../adr/0130-authoring-access-interface-and-module-boundary.md) ·
[ADR-0135](../adr/0135-two-document-edit-and-apply.md) ·
[ADR-0136](../adr/0136-structural-edit-records.md) ·
[ADR-0144](../adr/0144-correlated-authoring-apply-outcomes.md)

## Alternatives Considered

Keeping only the button sequence leaves the direct Hierarchy gesture
unavailable. A dedicated host command could make source observation and
submission atomic, but requires a new shared contract. The accepted
client-side pattern already limits the race to the final call boundary.

## Testing & Verification Plan

[Plan 0103](../plans/0103-hierarchy-reparent.md#verification-checklist)
maps R1–R4 to model, shared-client and real Windows 100% checks, including
a reload failure and save/reopen on a named generation.
