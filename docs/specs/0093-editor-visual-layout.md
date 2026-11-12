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
a common command row, with document/history actions on the left and grouped
transport controls in the center. Keep Create child beside the hierarchy
and the other existing structural actions in an inline **Actions**
disclosure. Group transform mode, coordinate space, snap, and scene camera
in the Viewport header. Keep New Scene / Save As in an inline **Scene files**
section with their existing permission and result states. Put measurement
controls and detailed diagnostics behind a labeled **Diagnostics** section
below the workspace, open by default for explicit diagnostic modes. Keep
ordinary connection, document and Runtime state visible in a compact strip.
Use reflow within the same panes at smaller widths, without moving existing
focused or selected values into a different view. Keep menus, status,
and diagnostics outside the embedded child viewport's bounds.

**R2. Visual language.** Use a neutral graphite Fluent theme, the
Windows-installed Segoe UI family with framework/system fallback, and
existing Avalonia controls and templates. Use 14 DIP for default controls,
12 DIP for secondary text, 16 DIP semibold for panel headings, and
22–24 DIP for the chooser title. Use 4 / 8 / 12 / 16 DIP spacing, 30 DIP
minimum command height, and 28 DIP minimum one-line list row height.
Use project-authored 16 DIP line icons in the existing `PathIcon` control
for familiar commands, a few object types, and section affordances. Keep
visible command labels and accessible names, including the existing edit
description for Undo and Redo. Group toolbar buttons on flat surfaces.
Reserve stronger outlines for input and secondary action controls where
needed, and reserve solid blue for primary confirmation. Use a tinted
row with a leading marker for selection, an underline for active tabs,
and a visible focus ring distinct from either. Do not use color alone
to communicate status.
Measure at least 4.5:1 contrast for enabled text, including state messages,
and 3:1 for focus and essential control boundaries against adjacent colors.

**R3. Lists, forms and dialog.** Keep pane headers, search/filter affordances
and the chooser's Confirm/Cancel available while content scrolls. Use one
primary line per asset row and a short type label. Keep full identifiers and
paths in an adjacent detail or result region with selectable or copyable text.
Truncate only labels whose full value remains available, and wrap guidance
and errors without hiding the next action. The Inspector shows the
selection name above component groups separated by headings and dividers.
Align editable values within each group, and reflow labels and controls
when their usable width is limited. Asset references keep their
existing editable text, Choose and Clear actions. Use one chooser layout for
all references. Show the opening mesh and preview column only for the
existing material path. Use a distinct preview area with the preview
action above the image. Below 720 DIP, stack the preview below the
list/details within the scrolling body, with search and the action footer
outside it. Keep preview state independent of Confirm. New Scene and Save
As stay inline.

**R4. State and interaction.** Use the existing session and operation results
to show nearby text for empty, loading or busy, success, disabled and failed
states. Give failures their existing next action or recovery guidance. A
save receipt or accepted edit and a failed or pending Runtime reload can
coexist in the display. Report the latest known operation result without
inferring a document-wide dirty or saved flag. Apply, retries, unknown
results, focus restore, text drafts, keyboard shortcuts, Undo/Redo, pane
sizing and modal dismissal
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
| [Main window, standard](../images/0093-editor-visual-layout/main-window.png) | A compact command row and viewport toolbar leave the scene as the focal point. The selected Hierarchy row and component groups identify the edited object. |
| [Main window, compact](../images/0093-editor-visual-layout/compact-assets.png) | Short rows and a dedicated Assets detail area at reduced width. A busy preview and a separate Runtime reload failure each show their own recovery. |
| [Material reference chooser](../images/0093-editor-visual-layout/reference-chooser.png) | The list and current field stay on the left. The labeled opening mesh and read-only preview stay together on the right, with a fixed action footer. |
| [Narrow chooser](../images/0093-editor-visual-layout/compact-chooser.png) | The preview stacks under the results while the search and action footer remain reachable. |

Use window `#15171B`, pane `#1D2025`, raised surface `#252930`, text
`#F0F2F5` and secondary text `#B3BBC7`. Use `#75ADF0` for the primary
action fill with `#111B29` text, `#263B53` for selection, `#A6CDFF` for
focus, and `#677585` for essential control outlines. Suggested semantic
foregrounds are `#7DB6F5` for information, `#89C8AA` for success,
`#E6BC75` for warning, and `#F3A2A7` for failure.

The [Unreal Editor interface](https://dev.epicgames.com/documentation/unreal-engine/unreal-editor-interface)
informs the distinction between the main and Viewport toolbars. The
[Unity toolbar](https://docs.unity.com/en-us/engine/6000.0/manual/unity-editor/editor-navigating-managing/toolbar)
and [component Inspector](https://docs.unity.com/en-us/engine/6000.0/manual/working-with-gameobjects/unity-components/inspector-manage-components)
inform the grouped playback controls and property layout. Atlantis keeps
the three existing panes and places all managed controls outside the native
Viewport. The mockups use original UI styling and existing Atlantis scene
and preview captures.

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
