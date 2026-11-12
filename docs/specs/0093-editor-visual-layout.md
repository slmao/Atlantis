# Spec 0093: Editor Visual Layout and Controls

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-12
- **Related Plan(s):** [Plan 0093](../plans/0093-editor-visual-layout.md)
- **Approval:** Pending joint human review, including visual approval
- **Related ADR(s):** None. See Architectural Impact.

## Summary

Give the Windows Atlantis Editor App a consistent, compact layout. Put scene
commands and Runtime state where they are easy to find, give the Viewport more
room, and make the Inspector, asset lists, and asset reference chooser readable
at smaller window sizes and higher display scaling.

## Motivation / Problem Statement

On `3195bb98` the [main window](../../src/editor_app/Atlantis.Editor/MainWindow.cs)
gives both side panes a 320 DIP starting width. Hierarchy shares its space with
document, Runtime, gizmo and measurement controls. The 96 DIP status field
mixes connection, input and measurement diagnostics without distinguishing the
state of the saved document from the state of Runtime. The Inspector uses fixed
label columns, and Assets and Saved Scenes stack commands, long paths and
results in the same scroll area. The [asset reference chooser](../../src/editor_app/Atlantis.Editor/AssetReferencePickerWindow.cs)
has a fixed search width and adds a 276 DIP preview column for material fields.
GUIDs compete with source names in list rows, and some long context text can
push filters or details out of view. The app currently relies on stock Fluent
defaults and local spacing choices.

## Goals

- Establish a common visual hierarchy for the main window, Inspector, Assets,
  Saved Scenes when available, and both forms of the existing asset chooser.
- Keep frequently used commands visible and let users distinguish selection,
  keyboard focus, progress, refusals, and recovery at a glance.
- Retain the same authoring and Runtime behavior while improving usable space.

## Non-Goals

New workflows, new modal dialogs for New Scene or Save As, dockable or
persistent layouts, theme settings, localization, new preview features,
Runtime rendering changes, and changes to the native debug Editor.

## Requirements

### Functional

**R1. Window and panes.** Retain the native Windows title bar, resizable
Hierarchy / Viewport / right workspace, and existing Inspector, Assets and
conditional Saved Scenes tabs. Place Save, Undo/Redo and Play/Pause/Step in
a common command row. Put hierarchy actions with the tree, gizmo controls
above the Viewport, and New Scene / Save As in an inline **Scene files**
section with their existing permission and result states. Put measurement
controls and detailed diagnostics behind a labeled **Diagnostics** section
below the workspace, open by default for explicit diagnostic modes. Keep
ordinary connection, document and Runtime state visible in a compact strip.
Use reflow within the same panes at smaller widths, without moving existing
focused or selected values into a different view. Keep menus, status,
and diagnostics outside the embedded child viewport's bounds.

**R2. Visual language.** Use a restrained graphite Fluent theme, the
Windows-installed Segoe UI family with framework/system fallback, and
existing Avalonia controls and templates. Use 14 DIP for default controls,
12 DIP for secondary text, and 16 DIP semibold for section headings.
Use 4 / 8 / 12 / 16 DIP spacing, 30 DIP minimum command height, and 28 DIP
minimum one-line list row height. Use project-authored 16 DIP line icons
through the existing `PathIcon` control for a small set of familiar actions
(Save, Undo, Redo, Play, Pause, Step). Keep visible labels and accessible
names, including the existing edit description for Undo and Redo. Use the same treatment for text boxes, lists, tabs, primary and
secondary actions, and keyboard focus. Provide a visible focus ring distinct
from selection and hover. Do not use color alone to communicate status.
Measure at least 4.5:1 contrast for enabled text, including state messages,
and 3:1 for focus and essential control boundaries against adjacent colors.

**R3. Lists, forms and dialog.** Keep pane headers, search/filter affordances
and the chooser's Confirm/Cancel available while content scrolls. Use one
primary line per asset row and a short type label. Keep full identifiers and
paths in an adjacent detail or result region with selectable or copyable text.
Truncate only labels whose full value remains available, and wrap guidance
and errors without hiding the next action. Inspector labels and controls
reflow when their usable width is limited. Asset references keep their
existing editable text, Choose and Clear actions. Use one chooser layout for
all references. Show the opening mesh and preview column only for the
existing material path. Below 720 DIP, stack the preview below the list/details, within the
scrolling body and with the action footer outside it.
Keep preview state independent of Confirm. New Scene and Save As stay inline.

**R4. State and interaction.** Use the existing session and operation results
to show nearby text for empty, loading or busy, success, disabled and failed
states. Give failures their existing next action or recovery guidance. A
save receipt or accepted edit and a failed or pending Runtime reload can
coexist in the display. Report the latest known operation result without
inferring a document-wide dirty or saved flag. Apply, retries, unknown
results, focus restore,
text drafts, keyboard shortcuts, Undo/Redo, pane sizing and modal dismissal
keep their existing behavior. Long messages stay readable and do not resize
the command row or hide the owning action. Keep keyboard order predictable
and avoid stealing focus on asynchronous refresh.

### Non-functional

Use device-independent dimensions and existing per-monitor DPI support.
Target an ordinary 1400 × 860 DIP client area and a compact range down to
960 × 600 DIP when the work area permits. Use the same persistent controls
and event bindings across sizes. No new packages, font files, third-party
icon assets, asynchronous work, polling, or work on the Runtime frame path.

## Proposed Design

| View | Proposed arrangement |
| --- | --- |
| [Main window, standard](../images/0093-editor-visual-layout/main-window.png) | Command row above three panes. The selected node and Inspector are easy to associate, and the Viewport retains the largest share. |
| [Main window, compact](../images/0093-editor-visual-layout/compact-assets.png) | Short rows and a dedicated Assets detail area at reduced width. A busy preview and a separate Runtime reload failure each show their own recovery. |
| [Material reference chooser](../images/0093-editor-visual-layout/reference-chooser.png) | The list and current field stay on the left. The labeled opening mesh and read-only preview stay together on the right, with a fixed action footer. |
| [Narrow chooser](../images/0093-editor-visual-layout/compact-chooser.png) | The preview stacks under the results while the search and action footer remain reachable. |

Use window `#101419`, pane `#161C24`, raised surface `#1B222C`, text
`#ECF1F8` and secondary text `#AAB7C8`. Use `#A6C6FF` for the primary
action fill with `#122039` text, `#273A56` for selection, `#B8D0FF` for
focus, and `#627488` for essential control outlines. Suggested semantic
foregrounds are `#A6C6FF` for information, `#7FDDB5` for success,
`#F4C477` for warning, and `#FFA5A6` for failure. The mockups show sample
content and states. Their embedded scene and preview images use existing
repository captures, separate from the proposed chrome.

## Architectural Impact

None. Style resources and layout stay private to Atlantis Editor App. The
existing Fluent package is MIT-licensed and stays pinned under
[ADR-0127](../adr/0127-editor-application-managed-dependency-policy.md).
Refer to Segoe UI by system family name and ship no font file. Author the
small icon paths under the repository's [Apache 2.0 license](../../LICENSE),
without importing an icon set. Viewport hosting and DPI ownership remain under
[ADR-0126](../adr/0126-viewport-embedding-and-input-routing.md).
The Editor model, SDK, shared contracts, and Runtime remain untouched.

## Alternatives Considered

Styling the current layout alone would leave the Hierarchy crowded and keep
long status blocks in competition with the Viewport. Replacing Fluent or
introducing docking would add dependencies or interaction rules before the
existing flows have a consistent presentation.

## Testing & Verification Plan

[Plan 0093](../plans/0093-editor-visual-layout.md#verification-checklist)
requires actual Windows Editor windows with the embedded Runtime, a size and
DPI matrix, long content, and representative interaction states. Mockups
are the visual review reference. They do not serve as implementation
evidence or engine image baselines.

## Risks & Open Questions

Review the dark direction, command placement, compact layout and chooser
hierarchy before implementation. The main implementation risks are
resizing the native child HWND without clipping and preserving keyboard
focus when a row reflows or refreshes.

## Out of Scope / Future Work

Theme switching, custom window chrome, dockable panels, shared icon or font
packages, and restyling other applications can be considered separately.
