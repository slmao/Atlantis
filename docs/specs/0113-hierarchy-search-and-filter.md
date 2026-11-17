# Spec 0113 — Hierarchy Search and Filter

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-17
- **Related Plan:** [Plan 0113](../plans/0113-hierarchy-search-and-filter.md)
- **Approval:** Maintainer, 2026-11-17, [PR #391, `cecb5769`](https://github.com/slmao/Atlantis/pull/391#issuecomment-6950680552)
- **Related ADRs:** None — see Architectural Impact

## Summary and motivation

Find authoring nodes in a large Hierarchy by name and component type, see
their ancestor paths, and reveal the current selection without losing it.
The ordinary Editor already shows a fully expanded, depth-first list from
its local `DocumentTree`. The worker builds that tree from shared authoring
node, component and schema queries and updates it from authoring events.
The row label combines name, GUID prefix and component names; there is no
search field or persisted expansion state. The same selection drives the
Hierarchy, Inspector and Viewport. Several current structure commands also
use the displayed rows to resolve sibling order or a drop target, so simply
substituting a filtered list would change what those commands mean.

## Goals and non-goals

Provide one compact search row and a way to reveal selected nodes. Keep
existing authoring and selection semantics while making hidden targets
explicit and preventing accidental batch edits. This slice adds no bulk
rename, custom groups, property or GUID search, query language, manual
branch folding, saved filter presets or new Viewport rendering.

## Requirements and proposed design

### R1 — Match document names and component types, with ancestor context

Add a **Search by name** field, an **All components** selector with one
optional component type, and **Clear filters**. Match a trimmed, ordinal
case-insensitive substring of the explicit authoring node name. Whitespace
alone is empty. An unnamed node matches only when the name filter is empty;
the GUID prefix and combined display label are not searchable. The component
choices come from the current authoring component schema; display the schema
names and match `TypeId` against the node's actual component membership.
When both criteria are active, a node must satisfy both itself. Component
membership on an ancestor does not make a descendant match. A type known to
the schema can remain selected when there are no instances of it.

With either criterion active, show each match and its ancestor path exactly
once, in the same depth-first order and indentation as the unfiltered tree.
Mark rows shown only for context with secondary text or an equivalent cue
that does not depend on color. Unmatched descendants of a matching parent
are omitted unless needed to reach another match. Distinguish direct
match count, context and the no-match state. Without an active criterion,
show the ordinary full tree. Keep filtering and highlighting state local to
the Editor. Use existing [0093](0093-editor-visual-layout.md) controls, focus
styles and readable compact layout; the controls and status must be usable
with keyboard and pointer, and long labels must remain inspectable.

### R2 — Preserve selection and provide a predictable reveal

Changing the filter or refreshing results does not change the ordered
selection, its limit, Inspector subject or Viewport outlines. Sent edits
continue to settle; unsent edits use R3. Indicate the total selected and
the number hidden by the filter (including when there are no matches).
A context row is a real,
selectable node. Plain row click replaces the selection as today; Ctrl+click
or Ctrl+Space toggles that visible row in the **full** selection, preserving
hidden entries and the existing 64-node limit. The list highlights only
visible selected rows. Programmatic list refreshes must not replace the
full selection with the visible subset. Viewport picks retain their current
replace/clear behavior and do not automatically clear the filter.

Add **Locate selection** in the Hierarchy, distinct from Viewport **Focus
selection**. If all selected nodes are visible, scroll to the first in
document tree order without changing the filter. If any are hidden, clear
both criteria, reveal all of them and scroll to the first previously hidden
selected node in that order. With no live selection, the action is disabled.
Locate takes focus to the revealed list row for keyboard navigation without
changing the set, the active camera or the document.

### R3 — Guard structure and batch edits without retargeting them

While a filter is active, block Hierarchy node operations whose source or
destination comes from the selection or rows: duplicate (single or batch),
delete subtree, create child, pick up/move/reparent and sibling moves, by
drag or button. Also block Inspector Add/Remove Component while filtered.
Explain that clearing the filter restores structural editing. Never infer
sibling adjacency, affected descendants or a drop destination from the
filtered projection. An explicit root creation action or **Assets → Add to
Scene** keeps its existing rules because it does not target a Hierarchy row.

If any selected node is hidden, block Inspector batch **Set** (including
Enter) and group Gizmo editing for the whole selection. Give a reason next
to the affected controls and direct the user to Locate selection or clear
the filters. Do not silently edit just the visible subset. When all
selected nodes are visible, these operations use their existing entire
selection and guards. Single-node property and Gizmo editing, explicit
Undo/Redo of recorded edits, Save, Apply and Viewport camera actions keep
their existing contracts. The local filter is not an additional host or
shared client permission.

Use the same eligibility checks for control state and invocation. A change
to the effective filters cancels any unsent Hierarchy drag or pick-up and
clears its feedback; a group drag that becomes ineligible cancels and clears
its preview through the existing path. Before sending a queued edit subject
to these guards, check the current filter intent and document lifetime with
the existing row identity, selection and revision checks at the send
boundary. For eligible batch edits under an active filter, confirm against
the worker's latest observed tree that the
whole selection is still visible even if the UI has not refreshed. If a
queued intent is stale or ineligible before sending, do not send a mutation,
record history, trigger Apply or retry automatically. An already-sent edit
still settles under the existing contract. Filtering itself does not
submit, clear or rebase an Inspector draft: normal focus behavior remains
subject to [0112](0112-unsaved-document-protection.md), and a blocked batch
draft can only be submitted later if its existing guards still pass.

### R4 — Follow authoring updates and restore browsing position

Recompute results from accepted tree/metadata snapshots when node names,
component membership, lifetimes, parents or document order change, including
through external clients and Undo/Redo. Use the same underlying row identities
for selection and actions. Late work from an older session, document, tree or
filter must not restore obsolete rows, focus or an old scroll request. A
destroyed node is dropped from the selection by the existing lifetime rule;
a replacement with the same GUID does not inherit an old gesture or scroll
anchor.

On entering filtered mode, remember the top visible row by live identity.
Filtered mode always exposes the ancestor paths. On normal clear, restore
the existing fully expanded tree and the remembered row as the scroll anchor.
If the row is gone, use the nearest surviving predecessor from the original
tree order, or the top when none survives. A Locate action that clears the
filter uses its reveal target instead. Normal refreshes retain the current
visible anchor where possible and must not steal search focus. Clear action
returns focus to the search field. Filter text, type and scroll anchor are
window/document state only; clear them on confirmed document reopen or a new
session, never as a side effect of Save, Apply, cancelling a reopen, or an
unsettled reopen request. Preserve the existing document selection lifecycle.

Project from the existing snapshots with bounded per-node/edge work and
without per-keystroke RPCs, polling, renderer work or extra overlay calls.

## Architectural Impact

None. This is a view projection and an invocation guard in the existing
managed Editor (UI-free model, Avalonia view, existing Editor worker). It
reuses shared authoring queries/events and transaction, history and Apply
contracts. It changes no public API, wire behavior, persisted data, dependency,
threading ownership or module boundary. No new ADR is needed.

## Alternatives considered

A flat results list loses the parent path and makes similarly named nodes
harder to distinguish. Removing hidden nodes from selection makes merely
typing a search destructive to the selection and can silently shrink a batch.
Allowing structure edits from filtered results would require showing or
confirming affected hidden descendants and actual sibling slots. A shared
search endpoint adds a new contract even though the Editor already has all
required metadata.

## Testing and verification

Use [Plan 0113](../plans/0113-hierarchy-search-and-filter.md) to cover matching,
row lifetimes and selection, unsent edit refusal and existing settlement,
and the actual Windows 100% Hierarchy flow with Save and Reopen from Source.

## Risks and open questions

The main risks are accidentally using filtered adjacency, overwriting the
full selection from an Avalonia refresh, or sending a previously eligible
batch after an external change. The Plan's joint decisions cover the match,
selection, edit and acceptance rules; no expansion behavior is persisted.
