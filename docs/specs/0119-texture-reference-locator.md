# Spec 0119: View and Locate Direct Texture References

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-20
- **Related Plan:** [Plan 0119](../plans/0119-texture-reference-locator.md)
- **Approval:** Atlantis maintainer, 2026-11-20, [PR #403](https://github.com/slmao/Atlantis/pull/403) at `d25897ec`.
- **Related ADRs:** None proposed. See Architectural Impact.

## Summary and motivation

Assets can already use the reimport workflow to list known dependent materials.
A catalog dependency contains a GUID once, even if a material uses it for both
base color and emissive. The list does not identify or navigate to a slot.
Add one read-only **View References** action for the selected texture. A verified
result identifies a material and a slot and can reveal both in Assets.

## Goals and non-goals

Cover one texture at a time, one hop to material users, in the connected
Runtime's accepted catalog snapshot. Include imported and managed textures
present in that snapshot and every catalog Material, even those hidden by
Assets search or type filters. Exclude uncommitted drafts, source-only and
unpublished changes, other generations, transitive dependencies, scene usage,
reverse indexing, batch replacement and asset writes.

## Requirements and proposed design

### R1 — Enumerate the snapshot and resolve each slot

Extend the explicit, cancellable [0116](0116-registered-texture-reimport.md)
dependent-material scan. Capture the owning worker/session, accepted Assets
list epoch and visible texture GUID. Use `assets.list` for the Material roster
and `assets.get` to check each Material's direct dependency set, identity and
type. Reject ambiguous identities as incomplete. Only read a recipe for a
Material whose valid detail includes the texture GUID. Use the existing
`assets.materialReadWithCombinedMaps` projection, which reads cooked material
values against the same catalog. Require the returned material GUID and
generation, when reported and known for the attached snapshot, to agree with
the request. Compare the deduplicated
nonempty recipe texture GUIDs with that Material's dependency set before
claiming slots. Expand matching fields to **Base Color**, **Normal**,
**Metallic/Roughness**, **Occlusion** and **Emissive** using the slot vocabulary
of the [0118 removal check](0118-registered-texture-removal.md). Each verified
`(material GUID, slot)` gets one row. Two matching slots in one material give
two rows, including a legal shared Base Color and Emissive texture. Display the
material name and slot prominently, with source and GUID available in secondary
details. Sort by ordinal material source, GUID and the slot order above.

### R2 — Distinguish empty, incomplete and unavailable

Label the scope **Current Runtime catalog · direct material references**. Show
**No direct material references in this catalog** only when the accepted full
Material roster and every relevant lookup were checked and no match was found.
Failures, incomplete enumeration, or disagreement between details and recipe
produce **Check incomplete**, with any verified rows retained. If a catalog edge
is proven but its recipe or slot cannot be resolved, show the material as
**Slot unavailable** with a reason and no Locate action. Examples include an
unsupported cooked recipe, unavailable artifact and a host lacking the full
material read. A legacy host can still prove a negative with a full list and
successful negative detail lookups. Older material projections may omit slots
and are never used to certify a slot. If the catalog itself is unavailable,
report that state separately. The result confers no removal authority. The
[ADR-0169](../adr/0169-prove-unused-before-removing-managed-textures.md) source
proof, fresh-producer checks, receipts, and write guards retain their own
semantics. Share the existing slot vocabulary and distinction between known
edges and incomplete evidence. Avoid running the removal job as a browser query.

### R3 — Locate only a current, revalidated row

Activating a verified result rechecks the material detail and full recipe by
GUID on the owning worker. Check identity, scope and exact slot-to-texture
binding again. Use the newest request intent for this action and keep the
origin texture selected until verification succeeds. A failed check leaves the
current selection, filters and scroll untouched and shows **Reference changed
or could not be verified. Refresh references.** A missing or mismatched slot
never falls back to a different slot or to a path/name match.

Both scan and locate replies require the original worker/session, accepted list
and intent to remain current at completion. Supersede on selection change
(including away and back), source selection becoming hidden, Reload view,
reconnect, shutdown, explicit refresh or a newer Locate. A late response from a
retired intent cannot move focus or publish results. Recheck again on the UI
thread before navigation. Publishing a new named generation leaves the attached Runtime on its own
catalog until an explicit
restart or reconnect to a different snapshot. A new snapshot always requires a
new query. There is no claim about newer generations or mutable source files.

On success, reuse the Assets dependency-reveal path to make the material visible
and scroll to its exact GUID, clearing search/type only as needed. Preserve
sort and grouping. Reveal and focus the verified slot in a read-only Material
slot section in Assets details. Populate that section from the verified recipe
and give its contents to the material selection under the same navigation
intent. Subsequent detail replies may update unrelated detail content but may
neither retarget the focused slot nor resurrect it after a selection change.
This works for verifiable materials regardless of write permission. It opens no
editable material dialog and changes no material draft, scene selection,
authoring document, history, Apply state or Runtime data. Keep existing preview,
Edit Material and asset operations available through their own controls.

### R4 — Responsiveness and accessibility

Reuse the 0116 worker slices and 0114 Assets visual conventions. Start work only
on explicit action, with one current scan, one outstanding lookup at a time,
and yields between calls. Keep progress, cancellation and transport failure
visible without blocking the UI. Use virtualized result rows and reuse the
existing display-name and details treatment. Acknowledge cancel without changing
the user's selection. Provide keyboard activation, an accessible status label
and visible slot focus at Windows 100%.

## Architectural Impact

None. The client composes existing read-only catalog and complete material
projection calls using 0116's scan ownership and 0118's slot vocabulary. The
CLI, native and C# SDKs and MCP can compose the same calls with their existing
result shapes. The locator is Editor navigation state. No change to a module
boundary, protocol, shared API, capability negotiation, on-disk format, worker
ownership, rendering path or dependency. Existing ADRs remain in force.

## Alternatives considered

- Use the catalog GUID set alone. It finds candidate materials but collapses
  two slots into one and cannot certify which slot to focus.
- Use the 0118 removal scan on every click. It answers whether the project
  sources allow a destructive operation and can describe unpublished state
  that the Assets viewer cannot reveal consistently. Its write-gated producer
  checks and receipt are unnecessary for this snapshot query.
- Open the existing Edit Material dialog. Write permission and an edit draft
  would be coupled to a read-only navigation action.

## Verification and open decisions

[Plan 0119](../plans/0119-texture-reference-locator.md) maps the four requirements
and joint decisions to tests and Windows 100% acceptance. The review should
confirm the published-snapshot scope, incomplete-coverage presentation, and
read-only slot landing before implementation.
