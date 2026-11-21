# Spec 0124 — Recent Saved Scenes

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0124](../plans/0124-saved-scenes-recent.md)
- **Approval:** Jointly approved at `6767ef6f` in [PR #413](https://github.com/slmao/Atlantis/pull/413). See Plan 0124.
- **Related ADR:** [ADR-0173](../adr/0173-local-saved-scenes-recent-history.md) (Accepted)

## Summary

Add a Recent list inside Saved Scenes. After restarting the launcher, the user
can select an earlier scene and use the existing Build and Open controls.
Recent is available for the current Windows user and the checkout explicitly
authorized for this launcher.

## Motivation

[Spec 0090](0090-saved-scene-build-open.md) discovers local saved units and
starts a separate Editor on explicit Open. [Spec 0123](0123-saved-scene-build-reuse.md)
can recover and verify one previous build. The user still has to find the
saved unit again in the ordinary list after restarting.

## Goals and non-goals

Give users a short list of recently launched scenes and a way to remove stale
shortcuts. Keep the ordinary Saved Scenes list and its existing source,
configuration and launch checks. Project-wide history, cross-checkout browsing,
automatic relocation, automatic build or launch, and additional cached build
results are outside the first release.

## Requirements

### R1 — Record successful explicit Open

Add or promote a record when an explicit Saved Scenes Open successfully starts
the independent Editor process. Use the canonical saved unit name and non-nil
scene GUID from that attempt's checked registration. Process start is the
recording point. Runtime readiness is reported in the child through the
existing flow. Selection, Refresh, Build, New Scene and Save As receipts,
refused or failed starts, and an Editor launched outside Saved Scenes leave
the history order unchanged.

Order the list newest first by settled recording order. Keep up to eight
distinct scene GUIDs per user and checkout. Reopening a GUID promotes it and
updates its recorded unit name if the user explicitly opens that GUID from
a different valid location. Until then, the existing entry keeps its recorded
location. Eviction affects the oldest shortcut only. If the launched scene's
identity cannot be established from the checked inputs, or the history write
fails, preserve the launch result and show a history warning.

### R2 — Select the same saved scene

Show Recent beside the ordinary saved-unit list using existing Editor
controls and styles. A valid Recent selection activates the same target in
Saved Scenes, shows its name and runs the ordinary eligibility check. Leave
the managed import setting as entered by the user. Reading or selecting a
record, including double-click or keyboard selection, starts neither Build
nor Open. Use the explicit action buttons.

Resolve the stored name within the current checkout through 0090's canonical
saved-unit checks. Confirm the scene GUID against the single scene entry in
that unit's guarded `catalog-source.txt`. Validate its declared source path
as the existing saved source leaf. This lookup establishes identity for
navigation. CMake remains responsible for build and asset validation. A recent
record supplies no claim about current source contents or artifact freshness.

Keep the selection bound to the checked name and GUID while it originates
from Recent. Recheck this identity before admitting Build and before starting
Open. For Open, bind the identity to the source evidence already checked for
that attempt. An identity refusal starts no process and does not promote the
record. Preserve 0123's full launch-time checks for recorded builds and its
existing rule for an eligible fresh build in the same launcher. An explicit
selection in the ordinary list can target the current unit on its own terms,
even when a stale Recent row shows the same name.

### R3 — Unavailable records and removal

Leave stale shortcuts visible with a reason. A complete check that finds the
expected location absent or incomplete shows that the scene may have moved
or is missing. A different GUID shows that the location now contains a
different scene. An unsafe, unreadable or inconclusive check shows that the
location could not be verified. Unavailable shortcuts offer Refresh and
Remove from Recent. They cannot activate another scene. The first release
does not search other paths or infer a move from matching names or GUIDs.

Remove the exact observed entry from history only, with no source, build,
process or receipt effects. If the entry was updated elsewhere, refresh
the row and require another explicit removal. Removing a shortcut never
drops the identity guard on an active target. Availability checks cannot
evict records. An unsuccessful Recent selection leaves the current target
unchanged. If an already active Recent target becomes unavailable, clear its
action eligibility. Older checks cannot restore it or replace a later user
selection. Pin admission and feedback to the current intent across Refresh,
normal selection, Recent selection and changes to the managed input.

### R4 — Local scope and independent persistence

Read and write the bounded private history only after ordinary Saved Scenes
has been enabled with a trusted checkout. Keep the list available without a
connected Runtime, as with ordinary discovery. Use the checkout and user scope
in [ADR-0173](../adr/0173-local-saved-scenes-recent-history.md). Persist settled
updates atomically and serialize them among launchers in that scope. If
history is missing, show an empty list. If it cannot be read or its version
is unsupported, explain the history failure and keep ordinary Saved Scenes
usable. A failed update preserves the last complete history if present.
Place file work off the UI thread.

History data carries no permission, checkout selection, managed import root,
CMake setting, artifact path or launch argument. Recent use and removal leave
0123's single build receipt and its lifecycle independent.

## Architectural impact

[ADR-0173](../adr/0173-local-saved-scenes-recent-history.md) records ownership
of the new private local history format and its separation from the build
receipt. Shared clients, native modules, scene formats and dependencies keep
their existing boundaries.

## Verification

[Plan 0124](../plans/0124-saved-scenes-recent.md#verification-checklist) maps
the requirements to a Windows 100% launcher restart, exact identity and
removal checks, and the existing 0123 refusal when a build becomes stale.
