# Spec 0141 — Hierarchy Batch Rename

- **Status:** In Review
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan:** [Plan 0141](../plans/0141-hierarchy-batch-rename.md)
- **Approval:** Pending joint human review
- **Related ADR:** [ADR-0189](../adr/0189-document-guarded-node-name-batch.md) (Proposed)
- **Design baseline:** `4bf9791c`, maintainer merge of [PR #444](https://github.com/slmao/Atlantis/pull/444)

## Summary and motivation

Let a Bistro author use Hierarchy search and multi-selection to number a
street segment's storefronts or lamps. Preview the names in Hierarchy order,
confirm one atomic edit, and retain the names through Save and reopen.

[0069](0069-scene-node-names.md) provides persisted authoring names, shared
`SetNodeName` and `NodeNameChanged`, and single-name history.
[0106](0106-scene-multiselect-batch-edit.md) provides selection and atomic
batch edit settlement. [0113](0113-hierarchy-search-and-filter.md) preserves
hidden selections and can reveal them. The current revision guard admits
property writes under [ADR-0162](../adr/0162-revision-guarded-authoring-property-batch.md).
A plain transaction of existing name writes is atomic but cannot pin the
preview's revision at the host.

## Goals and non-goals

Deliver one numbered rename of an explicit selection in a hand-authored
scene. Keep virtual groups, cross-scene moves, streaming, imported-source
editing in this command, recursive selection, asset renaming, and
find/replace or other naming templates outside V1.

## Requirements and proposed design

### R1 — Selection and reviewed preview

Add **Batch Rename…** to Hierarchy Actions for 2–64 selected nodes.
Keep the existing single-node Name field. Capture the whole selection
by GUID, including any selected ancestors and descendants as separate
targets. Unselected descendants are outside the write set. Use the
current unfiltered depth-first Hierarchy order, including root and sibling
order, independent of click order and visible row indices.

When any selected node is hidden by a filter, show the hidden count and
require the existing **Locate selection** reveal before building a
confirmable preview. The reveal keeps the entire selection, and requires a
separate Confirm action. Protect an active Inspector draft or editing
gesture under the existing navigation and edit guards. Opening, revealing,
editing the prefix and canceling cause no document, history or Apply writes.

Generate a name from the exact user-entered prefix, one ASCII space and a
two-digit ASCII ordinal starting at `01` (for example, `Bistro Lamp 01`).
Reject empty or whitespace-only prefixes and any generated name that fails
0069's encoding, control-character or 255-byte rule. Validate the completed
name including its suffix. Preserve valid spaces, case and Unicode as typed.
There is no truncation, locale-sensitive numbering or automatic suffix
adjustment. Existing duplicate labels remain legal, including a collision
with an unselected sibling. Names do not define identity.

Show every target with its actual old name (including unnamed), proposed
name, and enough parent path and GUID detail to distinguish duplicate labels.
Mark unchanged rows and omit those writes, keeping their ordinal positions.
Disable Confirm when all rows are unchanged or any row is invalid. Support
keyboard navigation and review of all rows at the existing supported
Windows 100% window sizes. Use the Editor's existing visual resources.

### R2 — Stale refusal and atomic commit

Build the preview from a current authoring roster bracketed by an identical
document token, plus the Editor's current session, selection/filter intent
and observed node occurrences. Capture GUID, old name, position in
Hierarchy order and parent context. Changing the prefix regenerates proposed
names against that same roster. A stale preview requires a fresh read and
a separate explicit confirmation.

Before sending, recheck the exact document token, names, occurrences,
ordered selection and visibility. Recheck client intent at the actual send
boundary after any read or capability wait. Changed selection, filter,
session, draft/gesture state, target, parent/order or old name invalidates
the preview. Host admission compares the full document token as specified in
ADR-0189. Any intervening document revision, even an unrelated one, can
require a refresh. This also catches races after the client check, including
destroy and recreate with the same GUID and name. No implicit retry.

Send the changed rows in one guarded authoring transaction. A known refusal
writes no names, revision, success events or history and starts no new
Apply. The sole forward write is to the names. GUIDs, hierarchy slots,
parents, components, transforms, Prefab associations and asset bindings
stay the same. For a verified success, refresh labels via existing name
events and re-evaluate the current filter. If names become filtered out,
retain selection and expose the existing hidden-count/reveal workflow.

### R3 — History, projection and persistence

Create one history entry with exact before/after names and observed target
occurrences after verified success. Undo restores all changed names and
Redo reinstates them. Replay uses a fresh document token and the same
host guard. Compare exact recorded names on the same occurrences, without
depending on the current selection or rerunning numbering. A changed
name, removed node or replacement refuses the entire replay and reports
the conflict. A known stale-revision refusal alone keeps the original
history entry pending explicit retry after a fresh read. Unrelated
earlier edits may coexist when the replay's exact readback still agrees.

Use the existing ticket and failure draining contract and verify the
settled revision, ordered name events and readback before publishing
history. Publish verified edit/replay settlement and history before
fallible UI refresh. Distinguish a known refusal, known commit with
unverified history, and an unknown commit result. In the latter two
cases show affected GUIDs and direct the user to inspect current names
before retrying. Clear Redo after an uncertain forward edit. Drop an
unverified replay from its original stack without moving it to the
opposite one. Require explicit Apply after an uncertain edit and preserve
independent settlement of older owned Apply receipts.

Follow [0083](0083-authoring-apply-outcomes.md) for the separate Apply
intent after verified commit or replay. While recovery is suspended,
retain the document edit and history. Explicit Apply retries projection
of the current document without repeating the name transaction. A lost
Apply receipt leaves verified history intact. Save through ordinary
source Save. Reopen the same source and verify names by GUID, with the
ordinary clearing of local history on document reopen.

## Architectural Impact

[ADR-0189](../adr/0189-document-guarded-node-name-batch.md) defines one
additive closed guard, read-only capability and refusal categories across
the existing shared authoring interface. No new RPC, event kind, source
format, package, module boundary, Runtime name component or render path.

## Alternatives considered

A 64-step loop permits partial edits and creates multiple history steps.
A single unguarded transaction cannot exclude an external write between
preview and admission. Extending the property-only guard changes the
shape promised to existing clients. A name-specific leading guard keeps
that shape and reuses the existing transaction and events.

## Testing and verification

[Plan 0141](../plans/0141-hierarchy-batch-rename.md) maps R1–R3 to
shared-client, concurrency and Windows 100% Bistro verification.
This review contains the design and document checks.
