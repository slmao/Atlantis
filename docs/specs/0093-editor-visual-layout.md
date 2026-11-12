# Spec 0093: Editor Visual Layout and Controls

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-12
- **Related Plan(s):** [Plan 0093](../plans/0093-editor-visual-layout.md)
- **Approval:** Pending joint human review, including visual approval
- **Related ADR(s):** None. See Architectural Impact.

## Summary

Give the Windows Atlantis Editor App a compact graphite layout. Put scene
commands next to the work they affect, make selection and keyboard focus
easy to distinguish, and put object names, asset names and previews ahead
of identifiers and diagnostics.

## Motivation / Problem Statement

On `3195bb98` the [main window](../../src/editor_app/Atlantis.Editor/MainWindow.cs)
gives both side panes a 320 DIP starting width. The Hierarchy mixes document,
Runtime, gizmo and measurement controls. The 96 DIP status area blends
connection and diagnostic text with operation outcomes. The Inspector's fixed
label columns, Assets' stacked controls and the
[reference chooser](../../src/editor_app/Atlantis.Editor/AssetReferencePickerWindow.cs)'s
long identifiers and fixed search width compete for space at smaller sizes.

The previous mockups carried this competition into the design: headings
were heavy, borders around nine adjacent Transform values had similar
weight, and paths, GUIDs and always-visible guidance competed with asset
names and the preview. Panel headings, rows and grouped forms need a shared
rhythm; active, focused and failed states should carry the stronger accents.

## Goals

- Establish one visual system for the main window, Inspector, Assets,
  conditional Saved Scenes, and both forms of the existing asset chooser.
- Keep commands easy to find and make normal, selected, focused, busy,
  disabled and failed states distinct.
- Improve density and long-value handling while preserving authoring and
  Runtime behavior.

## Non-Goals

New workflows or dialogs for New Scene or Save As, dockable or persistent
layouts, theme settings, localization, new asset metadata or preview
capabilities, Runtime rendering changes, and changes to the native debug Editor.

## Requirements

### Functional

**R1. Window and panes.** For the recommended layout A, retain the native
Windows title bar, three resizable Hierarchy / Viewport / right workspace
panes, and the existing Inspector, Assets and conditional Saved Scenes tabs.
Put Save and Undo/Redo at the left of a common command row, and group
Play/Pause/Step at its center. Keep Create child near the tree and put the
other existing structural actions in an inline **Actions** disclosure. Group
transform mode, coordinate space, snap and scene camera at the Viewport.
Place New Scene / Save As in an inline **Scene files** section with their
existing permission and result states. Give measurement and detailed
diagnostics a labeled **Diagnostics** section below the workspace, open
by default for explicit diagnostic modes. Keep ordinary connection,
document and Runtime results visible in a compact strip. Reflow within the
same panes at small widths. Keep managed controls and feedback outside the
native child Viewport's bounds.

**R2. Type, density and states.** Use neutral graphite Fluent resources and
the Windows-installed Segoe UI family with system fallback. Start with 13 DIP
for controls and panel headings, 12 DIP for secondary text, 15 DIP for the
selected object name and 18 DIP for the chooser title. Use moderate heading
weights and distinguish component groups by spacing and dividers. Use a
4 / 8 / 12 / 16 DIP spacing scale and a common 28 DIP one-line rhythm for
commands, list rows and properties. Grow controls where needed for text,
focus rings or constrained layouts. Use project-authored line icons in
16 DIP boxes with consistent stroke and optical alignment; keep command
labels and accessible names, including Undo/Redo descriptions.

Use slight surface differences for ordinary fields and flat actions;
strengthen outlines on hover and show a distinct keyboard focus ring.
Use a tinted row with a leading marker for selection and an underline for
active tabs. Reserve blue for selection, focus and primary actions. Align
Position, Rotation and Scale on the same three numeric columns. Put small
text labels X/Y/Z before tabular, right-aligned values and keep each
field's property and axis available to assistive technology. When the
three columns no longer fit, reflow without clipping digits or changing
the keyboard sequence. Require 4.5:1 contrast for enabled text and 3:1
for focus and essential control boundaries, including search when at
rest. Use text and, when helpful, an icon to communicate status.

**R3. Information hierarchy and chooser.** Show the selected object name
above component groups. Show asset names before type labels and identifiers
in list and reference rows. Use names from asset data already available in
the current session; fall back to a labeled reference or short identifier
if unavailable. Add no lookups, prefetch or snapshot changes for labels.
Put full paths, GUIDs and existing metadata in secondary details: allow
selection and copying of complete text, and retain raw GUID editing
where the Inspector already permits it. Keep Choose and existing Clear
guards on the primary reference row. Give raw editing a labeled,
keyboard-reachable disclosure; preserve the same field bindings, validation
and draft settlement on ordinary blur, Enter, Escape and chooser opening.
Keep long names discoverable in full. A disclosure or reflow must not
silently discard a draft or trigger a second commit.

Use one chooser layout for search, candidate selection, optional preview
and Confirm/Cancel. Keep search and the action footer outside the scrolling
body. Show the selected asset name next to its type and keep **Asset
details** collapsed by default; retain existing identifiers, schemas and
dependency actions inside the disclosure, with their existing guards. Put
normal guidance on a line of secondary text. Give the existing material
combination path a dedicated preview area (about 296 DIP square at the
standard size), with the opening mesh label and explicit **Preview with
material** action above the image. Keep the full opening mesh identity
available in secondary details. Below 720 DIP, stack the preview below
results/details in the scrollable body. Other references keep the same
layout without the preview column. Keep Confirm independent of preview
admission, success or failure.

**R4. Results and interaction.** Use existing session and operation results
to show text for empty, loading or busy, success, disabled and failed states.
Put a stronger status region beside the affected control when an operation
fails. Include the existing next action or recovery guidance. A successful
save or accepted edit and a failed or pending Runtime reload can coexist.
Report the latest known results without inferring a document-wide dirty
or saved flag. Preserve existing apply and retry semantics, unknown
results, candidate eligibility, preview intent invalidation and stale
response guards. Preserve selection, text drafts, F4, Escape, Confirm
and its focus restoration, Undo/Redo and modal dismissal. Do not move
focus on asynchronous refresh. Wrap long errors without covering the
owning action or growing the command row.

### Non-functional

Use device-independent dimensions and existing per-monitor DPI support.
Target a 1400 × 860 DIP client area and compact sizes down to 960 × 600 DIP
where the work area permits. Keep the same controls and event bindings
through reflow. No new packages, font files, third-party icon assets,
background requests, polling, or Runtime frame work.

## Proposed Design

Both main window studies use the same Bistro capture, selected object,
field values and status. They show a normal field, a selected tree row,
an independently focused X field, and a Runtime reload error alongside
an accepted edit. Open the PNG at original size to review type and lines.

| Study | 100% | 150% | Tradeoff |
| --- | --- | --- | --- |
| **A. Three panes (recommended)** | [1400 × 860](../images/0093-editor-visual-layout/main-window.png) | [2100 × 1290](../images/0093-editor-visual-layout/main-window-150.png) | Full-height tree and Inspector support scanning and long edit sessions. At this size the Viewport gets 834 DIP, without changing the existing pane order. |
| **B. Wide Viewport with stacked workspace (comparison)** | [1400 × 860](../images/0093-editor-visual-layout/main-window-alt.png) | [2100 × 1290](../images/0093-editor-visual-layout/main-window-alt-150.png) | Puts the tree beside the Inspector on one side and widens the Viewport to 1021 DIP. The stack splits vertical space, so the tree and property groups scroll sooner. |
| **Material chooser (shared direction)** | [900 × 630](../images/0093-editor-visual-layout/reference-chooser.png) | [1350 × 945](../images/0093-editor-visual-layout/reference-chooser-150.png) | Search leads to a selected candidate, a large labeled preview and a fixed confirmation footer. Metadata and normal guidance stay secondary. |

Recommend A for the first delivery: it preserves useful height in both
side panes at small window sizes and requires less change to pane order,
resizing and keyboard traversal. B is for review only. If preferred,
revise and approve the plan for that layout before implementation.
Only one layout ships.

The mockups render each scale independently on Windows with Segoe UI.
Both main views use the same 2044 × 1411 Atlantis Runtime capture, cropped
to fit each Viewport. The chooser uses the existing 256 × 256 Atlantis
combination-preview golden as an illustrative sample in its larger display
area; this changes neither capture resolution nor golden files.

Use graphite `#17191D`, pane `#202226`, raised surface `#23262B`,
field `#191B20`, text `#ECEEF1` and secondary text `#ADB3BE`. Use
`#2A3B51` with a `#86B9F6` marker for selection, `#A6CEFF` for focus,
and `#84B8F3` with `#121C29` text for the primary action.
Use `#6B737F` for essential outlines; semantic foregrounds include
`#91C7AE` success, `#E4BE81` warning and `#F3A4A7` failure.

| Official reference | Rule adopted for Atlantis |
| --- | --- |
| [Unreal Editor interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface) | Separate scene-wide actions, Viewport tools and content browsing; put transform, snap and camera beside the Viewport. |
| [Unity Inspector](https://docs.unity3d.com/6000.0/Documentation/Manual/UsingTheInspector.html) | Establish the selected object first, then group and align its component properties for repeated scanning. |
| [Flax Editor interface](https://docs.flaxengine.com/manual/editor/interface.html) | Compare Scene, Properties, Content and Viewport as work areas; give the Viewport visual weight and leave enough height for tree and property work. |
| [CRYENGINE Sandbox](https://www.cryengine.com/features/view/sandbox) | Give content and material preview a clear place within the same tool family. Use this for preview emphasis and visual continuity; no new material editor. |

All UI and icons in the studies are original to this design.

## Architectural Impact

None. Style and layout stay private to Atlantis Editor App. The existing
MIT-licensed Fluent package stays pinned under
[ADR-0127](../adr/0127-editor-application-managed-dependency-policy.md).
Reference Segoe UI by system family name and distribute no font.
Author icon paths for the existing `PathIcon` under the repository's
[Apache 2.0 license](../../LICENSE). The existing
[Viewport embedding and input routing](../adr/0126-viewport-embedding-and-input-routing.md)
decision continues to apply. The Editor model, SDK, shared contracts,
Runtime, native rendering and build inputs remain unchanged.

## Alternatives Considered

The two layouts above compare different panel placement at the same density.
Changing only colors and spacing would leave command placement and metadata
hierarchy unresolved. A new theme dependency or docking system would add
integration and behavior beyond this design.

## Testing & Verification Plan

[Plan 0093](../plans/0093-editor-visual-layout.md#verification-checklist)
requires actual Windows Editor windows with the embedded Runtime, a size
and DPI matrix, long content and representative interaction states. The
mockups support design review; they are neither implementation evidence
nor engine image baselines.

## Risks & Open Questions

Choose A or B and approve the visual and information hierarchy before
implementation. The main risks are keeping the native child HWND aligned
during resize and preserving field focus and draft settlement through
disclosures and reflow. The Plan tests both explicitly.

## Out of Scope / Future Work

Theme switching, custom window chrome, dockable panels, new font and icon
packages, asset pipeline changes and restyling other applications.
