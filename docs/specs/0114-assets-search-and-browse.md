# Spec 0114 — Assets Search and Browse

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-17
- **Related Plan:** [Plan 0114](../plans/0114-assets-search-and-browse.md)
- **Approval:** Maintainer, 2026-11-17, [PR #393, `c593b445`](https://github.com/slmao/Atlantis/pull/393#issuecomment-6952336127)
- **Related ADRs:** None (see Architectural Impact)

## Summary and motivation

Help users find an asset by the name they see, its logical source path or
its GUID. The Assets panel currently filters by source text or GUID and
type, then displays the host's GUID order. Friendly names, such as the
registered name of a texture whose file is `source.png`, are derived for
display separately. The panel rebinds its result list on detail replies,
and a filtered-away selection can still own a details reply. The shared
[0081 catalog](0081-asset-browser-shared-interface.md) already supplies
the identity, type and canonical source ID needed for browsing.

## Goals and non-goals

Make the ordinary Assets panel usable with large catalogs while preserving
its details, mesh previews and authoring actions. Give the reference
chooser the same search language and default sort with its existing target
rules. Asset move, deletion, drag and drop, thumbnail caching, saved
browser preferences and recursive folder views are outside this slice.

## Requirements and proposed design

### R1 — Search the displayed name, source and GUID

Provide a single **Search name, path or GUID** field and keep the existing
type filter. Trim surrounding whitespace. A blank query matches every row
eligible for the current type. A nonblank query matches an ordinal,
case-insensitive substring in any of the displayed name, the complete
catalog source ID or the canonical hyphenated GUID. For path matching alone,
treat a typed backslash like a forward slash. Also accept a compact GUID
substring when the query consists only of ASCII hex digits, with optional
hyphens and at least one hex digit. This accommodates copying a GUID with
or without hyphens and retains partial GUID search.

Use one Editor naming helper for both display and matching. Preserve the
current labels for managed textures and imported sub-assets, including
their sub-key. Retain the exact source string, GUID, type, tool, source and
artifact schemas, and dependencies in **Asset details**. Make the exact
path and GUID accessible by keyboard, selectable and copyable when they
are too long to show inline. The active row's source, GUID and type are
available from its summary even while its detail request loads or fails.
Search and browsing use the existing full catalog from `assets.list`;
`assets.get` fills in tool, schemas and dependencies for the active
eligible asset.

### R2 — Stable sorting and a bounded result list

Default to **Name A–Z** and offer **Name Z–A**. Compare names ordinally
without case; use the original complete source ID and canonical GUID
string as ascending ordinal tie breakers in either direction. Grouping
defaults to **None** with exclusive **By type** and **By source** choices.
Within every group use the chosen name order. Keep group order fixed:
Mesh, Texture, Material, Scene, Environment for type, and `assets:`,
`content:`, then Other for source. Source grouping uses the visible
root token of the shared source ID. It does not claim which records are
managed, imported, editable or backed by a current file. A source with an
unrecognised or absent prefix remains searchable and appears under Other.
Only show groups with matches. Headers display their result counts,
remain outside the selection and cannot launch asset actions.

Follow [0093](0093-editor-visual-layout.md). Pin the controls and status
above a bounded and virtualized result list. The list stays useful at both
standard and compact widths, with separate access to the selected asset
and operation controls. Keyboard navigation skips headers, and a focused
search field keeps focus through list and detail updates. Show the visible
count against the catalog total and distinguish loading, an empty catalog,
no matches, catalog unavailable and disconnection. **Clear filters** clears
the query and optional type only. Keep sort and grouping local to this
window. Prepare sort and search keys from each accepted list and avoid
rebuilding the result surface on detail or operation callbacks. Filtering,
ordering and grouping cause no additional list requests or automatic
preview renders.

### R3 — Preserve selection by GUID and expose eligible actions

Keep the selected GUID through sorting, grouping and filter changes while
it remains visible. Keep accepted details, in-flight detail work and the
preview for an unchanged visible target. Keep the row in view on a
reorder when it was in view before; otherwise anchor by the top visible
asset GUID where possible, falling back to the first matching asset.
A filter that hides the selection leaves a remembered GUID and a
**Selected asset is hidden** indication with **Clear filters** available.
It clears the active detail and preview presentation and disables actions
that require the selected asset, such as Preview, Copy GUID, rebuild,
material edit or copy, and **Add to Scene**. When that GUID is visible
again, restore it and read its current details once. A new selection,
explicit deselection or refreshed catalog without that GUID clears the
old preference. A newly bound Editor connection starts without the old
selection or scroll anchor. Returning to the Assets tab keeps the current
query, sort and group for the window.

An available dependency in Asset details can be followed by GUID. If it
is hidden in Assets, clear the filters, then select and reveal it with the
current sort and grouping. Keep an unlisted dependency visible as copyable
text. Existing commands still capture their normal target and settle
against the existing receipt and document guards after admission, even
if the browser changes. This applies to [Add to Scene](0087-add-mesh-to-scene.md)
and existing asset jobs. Import, material creation and other actions
without an asset target retain their own availability.

In the reference chooser, reuse only the match rules, naming and default
Name A–Z order. Keep the required type filter, explicit deselection,
the visible-candidate guard for Confirm, and the existing target,
focus, Apply and Undo/Redo contracts. A remembered hidden candidate can
be highlighted again only once it is visible and eligible. Confirm
does not wait for a successful details or preview result. Keep the
opening mesh and candidate bindings for combination previews.

### R4 — Bind asynchronous results to the visible target and snapshot

Keep the first-query Runtime catalog lifecycle from
[0081](0081-asset-browser-shared-interface.md). **Reload view** queries
the same snapshot. While loading, a previous list must not authorize
actions. A successful reload restores the saved GUID when still listed and
reads fresh details exactly once if visible. A refusal displays its own
state, with the existing recovery instructions for unavailable catalogs
or broken connections. A newer Runtime process after an explicit restart
can show a newly published generation.

Bind each list and detail result to the current connection, panel or
chooser instance, accepted list and selection request. A detail can update
the active asset only when its GUID and request are current. This holds
for rapid A → B → A selection, hide and reveal, reload while loading,
chooser closure and reconnect. Allow a dispatched detail to settle and
skip superseded queued intents before sending, with at most the current
queued detail per surface. Use existing worker and UI ownership. Do not
couple a detail callback to list selection or fire selection events while
restoring the list.

Reuse [0091 mesh](0091-static-mesh-preview.md) and
[0092 mesh/material](0092-mesh-material-preview.md) preview intent
and admission rules. Losing the visible target, Reload view, leaving
Assets, disconnecting or closing the chooser invalidates the corresponding
preview. Late replies cannot replace another asset's UI. Pure sorting or
grouping of the same visible asset leaves its preview usable. An explicit
preview request retains its existing failure and retry behavior. A detail
lookup failure reports the current asset's diagnostic without turning
the catalog into an empty result.

## Architectural Impact

None. Search, ordering, grouping and selection are managed Editor view
state over the existing catalog DTOs and the existing Editor worker.
CLI, SDK and MCP continue to share the existing operations and canonical
order of `assets.list`. [ADR-0143](../adr/0143-shared-cooked-asset-catalog-queries.md)
continues to govern catalog ownership, and [ADR-0151](../adr/0151-shared-offscreen-mesh-preview.md)
and [ADR-0152](../adr/0152-shared-mesh-material-preview.md) govern previews.
The design requires no new ADR.

## Alternatives considered

Server-side search or paging would require new shared semantics for
ordering, query and snapshot lifetimes. A flat list alone makes type
and source browsing harder. Classifying by producer, folder ownership
or editability would require facts outside the summary. Source-root
grouping gives a clear browsing distinction from the existing data.

## Testing and verification

[Plan 0114](../plans/0114-assets-search-and-browse.md) maps search, stable
order, hidden targets, virtualized browsing and delayed replies to model,
live integration and actual Windows 100% standard and compact checks.

## Risks and open questions

Several assets can show the same friendly name. Distinguish them through
the stable order and accessible details without relying on row position.
The joint review covers the match and grouping rules, hidden selection,
chooser reuse and acceptance.
