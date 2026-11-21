# Spec 0134 — Project Assets Explorer V1

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan:** [Plan 0134](../plans/0134-project-assets-explorer.md)
- **Approval:** Maintainer approval on `a7268957`, 2026-11-22. [Joint approval](https://github.com/slmao/Atlantis/pull/431#issuecomment-7067495383)
- **Related ADR:** [ADR-0183](../adr/0183-project-source-inventory-and-catalog-links.md) (Accepted)

## Summary

Extend Assets so a Bistro author can find a storefront glTF and a base-color
PNG by directory, see their import state, open the existing import controls,
and find the usable results after activation. Keep the physical project
source tree and the current Runtime's logical asset tree distinct.

## Existing capabilities

[0081](0081-asset-browser-shared-interface.md) supplies a process-lifetime
catalog and GUID-based details. [0114](0114-assets-search-and-browse.md)
already supplies search, stable ordering, virtualized rows and guards for
hidden selections and late replies. [0086](0086-new-asset-import.md),
[0095](0095-texture-import-and-base-color-binding.md) and
[0130](0130-static-gltf-group-instantiation.md) provide checked glTF and PNG
import, job status, Resume and generation publication.
[0121](0121-editor-tasks-and-issues.md) associates receipts with this
Editor session. [0120](0120-registered-texture-move.md) makes a logical path
independent of the original inbox slot. [0132](0132-scene-prefabs.md) and
[0133](0133-prefab-source-sync.md) register a Prefab in the checkout and
activate its frozen definition through [0090](0090-saved-scene-build-open.md).

## Requirements

### R1 — Browse two scopes within Assets

Keep **Runtime** as the default view, with the existing loaded catalog,
details and controls. Add a virtual directory tree. Use the catalog's
effective logical path when supplied and the canonical source root and
path otherwise. Treat glTF subkeys as asset identities and search terms.
Unknown source formats remain available under **Other**. A directory
scope limits the rows under it without changing its catalog.

Add a **Project** view with a read-only tree of explicitly enabled local
roots. The first release covers the managed project's `inbox/` and the
checkout's canonical `assets/prefabs/` registrations. These roots can be
available independently. Use only the checked managed profile and the
trusted development checkout authorized under ADR-0183. Show scope names
and relative paths. When no safe root is available, explain why and keep
the Runtime view usable. Surface recognizable directories and their safe
file entries. Mark files outside the supported import layouts as browse
only. Keep registration fragments, generated output and private work
directories outside the source tree.

Reuse 0114 sorting, search focus and virtualized lists and follow
[0093](0093-editor-visual-layout.md). Search the selected directory subtree.
Project searches file names and relative paths and filters by source kind
(glTF, PNG, Prefab source or Other). Runtime retains name, path and GUID
search and asset type filters. Keep filter context and scroll per view.
Distinguish no matching rows, pending scan and unavailable or incomplete
data. A hidden target cannot provide an action or drag source.

### R2 — Show the evidence for import state and association

A complete scan can distinguish an ordinary eligible inbox slot with no
registration from a verified registration. Mark eligibility by the
existing path grammar, without claiming that the importer will accept the
file. Display matching in-flight, failed, unknown, committed and published
states from this Editor's correlated 0121 receipts and existing status reads.
Treat **Published · restart required** as a receipt-backed state for a
confirmed generation that has yet to be activated. A verified registration
with no current matching asset reports **Registered · absent from this
Runtime** when the relevant catalog check is complete. Show **Status
unavailable** when evidence is missing or incomplete. A name or path
coincidence cannot supply a job, registration, or activation result.

Only show a cross-view association for a checked source slot and matching
GUIDs in the current catalog, as defined by ADR-0183. A glTF can lead to
several outputs. Present the known output types and names separately.
Keep the existing `assets.staticGroup` check for an actionable group.
Use the original managed inbox slot for PNG and glTF navigation, even
when a display name or logical path has changed.
A checkout Prefab link identifies its checked registration
and active frozen definition. If the checked checkout revision differs,
explain that a fresh Build/Open is required to use that source version.
For managed inbox links, explain that the preview and scene operations
use the active build. The association establishes the producer's source
slot, without verifying the on-disk input's current bytes.

### R3 — Recheck and locate in either direction

**Show in Runtime** lists the verified output choices for the source row.
**Show Project Source** takes an active asset back to its verified file.
An explicit jump may clear the query or type filter that hides the target
and reveal its directory, applying the existing GUID selection behavior.
For multiple outputs, let the user pick an exact GUID. An absent, unsafe,
ambiguous or changed target leaves the current selection intact and offers
Refresh or the existing activation step as appropriate.

Use a current completed scan plus explicit recheck before following a
link. Reuse the same scan path for the recheck. Compare the source role,
slot, evidence stamp and GUIDs relevant to the chosen link. Fence the
request by local selection intent, worker session, scan ID and active
catalog lifetime. Replies for an older selection, scan, scope or Runtime
cannot navigate or restore actions. A failed recheck leaves the stale link
unavailable until refreshed. Search and folder selection operate on
accepted data without starting scans, import, detail reads for hidden
assets or automatic previews.

### R4 — Continue through the existing workflows

For the supported layouts `inbox/<name>.gltf` and
`inbox/<name>/source.png`, offer **Open Import** with the checked logical
name and exact existing importer kind. The user still confirms Register
or a supported Resume in the existing controls. Keep the existing
permission, exclusive work admission, source validation, receipt
settlement, recovery and explicit activation rules. Where the state is
unknown, keep the existing inspect-before-retry guidance. A browse reply
does not grant write authority.

Continue to use existing catalog-backed preview, Add to Scene,
Instantiate Prefab and [0115 drag/drop](0115-asset-drag-drop.md) routes.
Only eligible Runtime asset rows supply a drag payload. A mesh dragged
from Runtime still goes through the existing target and transaction guards.
A Prefab uses its established Assets instantiate action. Project delegates
scene, build and publication operations to their existing owners.

## Architectural impact

[ADR-0183](../adr/0183-project-source-inventory-and-catalog-links.md) defines
the read opt-in, shared bounded scan and authoritative cross-view evidence.
Keep catalog and source formats, import/publication contracts, Renderer
and scene editing interfaces unchanged. Reuse the current Assets panel
without adding a persistent index, dependency or scheduler.

## Verification and scope limits

[Plan 0134](../plans/0134-project-assets-explorer.md) covers the Windows 100%
Bistro workflow, the 0090 Prefab boundary, and stale and unsafe inputs.
V1 excludes arbitrary filesystem roots, editing or moving source files,
deletion, batch operations, folder imports, file watchers, external drag
and drop, a thumbnail cache and automatic activation.
