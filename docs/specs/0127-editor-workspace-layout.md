# Spec 0127 — Save and Reset the Editor Workspace Layout

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0127](../plans/0127-editor-workspace-layout.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0176](../adr/0176-editor-workspace-layout-preferences.md) (Proposed)

## Summary

Remember the ordinary Windows Editor's three-pane layout and selected right
workspace tab across restarts. Add a single **Reset Workspace Layout** action.

## Motivation

[0093](0093-editor-visual-layout.md) already arranges the Hierarchy, Viewport
and right workspace with two splitters and standard and compact defaults. The
Viewport fills the remaining width. The right pane hosts Inspector, Assets and
Saved Scenes when the launcher has that capability. Splitter customization
currently lasts only for the window lifetime.

## Requirements

- **R1 — Save layout intent.** Persist the two preferred side widths in
  device-independent units, which determine all three widths with the existing
  fill Viewport. A completed pointer resize or keyboard splitter adjustment
  establishes the displayed pair as the new preference. Direct tab selection by
  pointer or keyboard saves a stable `inspector`, `assets` or `saved-scenes`
  identifier, including an explicit choice of the tab already shown as a
  fallback. Startup, resize, DPI, visibility fallback and programmatic tab
  changes do not write preferences. Use the local user scope in ADR-0176.
- **R2 — Restore within the current window.** Resolve the tab against the
  launcher's actual tabs. Use Inspector when the requested tab is unavailable;
  preserve the stored preference until the user chooses or resets it. Default
  widths use 0093's current standard or compact rules. Given a saved pair and a
  real layout pass, reserve both splitters and the three pane minima before
  allocating each side its desired width. If the side requests exceed the
  remaining budget, reduce their space above minimum proportionally. The
  Viewport takes the rest. Recompute on client size or rendering scale change
  using measured logical space without converting saved DIPs to physical pixels.
  Keep the uncompressed preferences so growing or
  returning to the previous scale restores the requested widths. Retain 0093's
  supported minimum window size; defer applying preferences when a transient
  layout pass cannot fit the existing pane minima. Use the existing Viewport
  host and input layout paths.
- **R3 — Recover defaults explicitly.** Missing, invalid, unsupported or
  unreadable settings use 0093 defaults and Inspector. A single reset action
  applies those defaults immediately and, if possible, persists the default
  state so it survives restart. Report a storage failure through local Editor
  feedback, with the session layout usable. Restoration and reset leave the
  document, selection, undo/redo, scene input and Runtime state alone. Keep
  the reset action unavailable during an active scene draft, pending input
  settlement, active drag, modal edit, inline asset edit that would lose its
  draft on tab change, or document transition. Check again on invocation. The
  reset control must preserve the draft and focus when the guard refuses it.
  Ordinary resize/reflow retains the live controls, focus and in-memory draft
  through the existing rules.

## Proposed Design and Architectural Impact

Use the current three-pane grid, splitter and tab controls with a bounded
local preference file. [ADR-0176](../adr/0176-editor-workspace-layout-preferences.md)
records the new preference scope, persistence and concurrent writer semantics.
Show Reset Workspace Layout in the existing main-window utility area using
0093's keyboard and accessible-control conventions. Saved Scenes availability
and its existing checkout and Open checks remain authoritative.

## Verification

[Plan 0127](../plans/0127-editor-workspace-layout.md#verification-checklist)
covers round trips, constrained layout and DPI transitions, availability,
damaged settings and a document-safe reset. Windows 100% remains the native
acceptance setting. Deterministic tests cover the logical layout path across
DPI changes.

## Out of Scope

Window bounds or maximization state, document and object selection, asset or
scene selection, filters, history, unsubmitted inputs, Runtime or camera state,
floating/dockable panels, new tabs, roaming settings and live synchronization
between open windows.
