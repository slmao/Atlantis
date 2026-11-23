# Spec 0142 — Static Mesh Thumbnails in Assets

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-23
- **Related Plan:** [Plan 0142](../plans/0142-assets-mesh-thumbnails.md)
- **Approval:** Maintainer, 2026-11-23, [joint approval of `fc5c7974`](https://github.com/slmao/Atlantis/pull/447#issuecomment-7105301892)
- **Related ADR:** [ADR-0190](../adr/0190-editor-mesh-thumbnail-admission-and-cache.md) (Accepted)
- **Design baseline:** `0372c4a3`, maintainer merge of [PR #446](https://github.com/slmao/Atlantis/pull/446)

## Summary and motivation

Show small geometry stills beside static mesh names in the Editor's Assets
catalog so a Bistro author can distinguish props while browsing.
[0114](0114-assets-search-and-browse.md) already virtualizes and projects the
catalog. [0091](0091-static-mesh-preview.md) already frames and renders one
cooked mesh through a shared Runtime call. Today the list uses a type icon,
and a full preview requires an explicit action for a selected row.

## Goals and non-goals

Load geometry for visible static mesh rows on demand, reuse current-session
results, and retain the catalog's search, keyboard/mouse selection and asset
drag behavior. Scope is the Editor's Assets catalog list. Source browsing,
reference pickers, other asset types, material spheres, texture channel
previews and a persistent or shared thumbnail store are outside this slice.

## Requirements and proposed design

### R1 — Row presentation and visible demand

Use the existing geometry appearance from `assets.previewMesh`. Scale
the returned square to a 64 × 64 physical-pixel thumbnail displayed in a
fixed square slot of up to 40 DIPs. Use the existing mesh icon as the
placeholder before load and after refusal. Distinguish a settled refusal
with a short accessible hint on the row. Keep names, type text and full
source/GUID access. The image has no independent focus, hit target or drag
identity. Keep row height and width allocation stable across image states.

Keep the virtualized list and use the ordered set of eligible row bounds
intersecting its clipped scroll viewport after layout. Realized offscreen
containers, group headers and non-mesh rows create no demand. Publish that
set on scroll, size, realization, projection and surface changes. Retain the
existing scroll anchor and selection policy. Painting an image does not
reset the items source, alter the layout anchor or select the row.

### R2 — Admission and late results

Use the single Editor preview slot and request ordering in ADR-0190. Collect
demands from visible rows and derive the next miss from the latest set.
Wait 200 ms after the latest event that changes effective visible demand.
Space automatic sends at least 500 ms apart per Editor, across Runtime
changes. Both conditions apply. Deferred
scheduling is event-driven, with a one-shot wakeup when pending visible
work first becomes eligible. An unchanged completed or empty view has no
automatic polling. Pause new automatic work while dragging an asset or
while the full-preview picker or material editor is open.

On each pending request, pin a Runtime/cache epoch, visible-demand epoch
and asset GUID. Serialize the admission check with demand invalidation
after connection setup and immediately before the SDK send. Change to an effective visible set, new filter or catalog projection,
leaving the visible list, disconnect or close retires old demand. On
completion recheck those identities before accepting the result. When
publishing to the UI, verify the actual current row binding and visibility
again. Clear image binding when a container recycles. An already-sent call
may settle, and stale bytes are discarded. Older results never populate a new
epoch's row or cache even if the same GUID is visible again.

### R3 — Bounded session cache and invalidation

The Editor owns at most 128 thumbnail records for the current Runtime and
catalog view epoch, keyed by that epoch and mesh GUID. Count decoded images
and failure markers together. Keep only scaled CPU-owned images. Bound
retained pixel storage to 2 MiB at four bytes per pixel. The existing SDK
reply and decoder scratch for the single active request are transient, with
the existing 256 × 256 geometry result and 512 KiB PNG limits.

Reuse a valid hit immediately, including after sorting, search, scrolling
or switching away from and back to Assets. Evict the least recently used
unpinned record. Detach dependent controls before disposing an evicted or
invalidated image. Pin entries currently displayed and defer a new miss when
every record is pinned. Offscreen or recycled controls must release their
image references. Cache correctness does not depend on list index, asset
name, source path or socket port.

Clear the cache and retire pending demand at Reload view, when replacing
the catalog snapshot shown to the Editor, on Runtime disconnect or replacement
and on Editor close. Reload view renews thumbnail eligibility using the
serving Runtime's existing catalog; activation of a published generation uses
the existing Runtime restart flow. A publish receipt for an inactive
generation alone leaves the running cache eligible. This is session reuse,
with explicit refresh for artifacts changed in place.

### R4 — Failure and ordinary browsing

Keep catalog interactions available while a thumbnail loads or the isolated
preview connection fails. Preserve the explicit full preview's own status
and retry behavior. Display a row-local fallback for recoverable errors.
A stable failure such as InvalidMesh or TooLarge remains a marker until
eviction or view reload. For Busy, unavailable rendering, render/decode
failure or transport failure, permit a fresh attempt only on later re-entry
to the visible demand set after a 2 s cooldown, or after Reload view.
A stationary failed row does not trigger a retry timer. Unsupported and
CatalogUnavailable suspend automatic preview for the current view epoch.
A preview transport failure never disconnects the catalog connection.
Connection-wide Runtime outages still use the existing disconnect flow.

## Architectural Impact

[ADR-0190](../adr/0190-editor-mesh-thumbnail-admission-and-cache.md) records
the Editor admission and ownership rules. It extends the previously
explicit-only Editor behavior in 0091 with one automatic consumer, keeping
ADR-0151's shared operation, Runtime admission, ownership and image contract.
The implementation stays within the Editor and model projects.

## Alternatives considered

See [ADR-0190](../adr/0190-editor-mesh-thumbnail-admission-and-cache.md#alternatives-considered).

## Verification

[Plan 0142](../plans/0142-assets-mesh-thumbnails.md) maps the rules to
directed model/UI checks and Windows 100% acceptance with Bistro props.
