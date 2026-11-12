# Spec 0093: Editor Visual Layout and Controls

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-12
- **Related Plan(s):** [Plan 0093](../plans/0093-editor-visual-layout.md)
- **Approval:** Pending joint human review, including visual approval
- **Related ADR(s):** None. See Architectural Impact.

## Summary

Refine the Windows Atlantis Editor App's three-pane layout in neutral dark gray. Put scene
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

**R1. Window and panes.** Retain the native Windows
title bar, three resizable Hierarchy / Viewport / right workspace
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

**R2. Type, surfaces and states.** Use the existing Fluent controls and the
Windows-installed Segoe UI family with system fallback. Start with 13 DIP
for controls and panel or component labels, 12 DIP for secondary text,
17 DIP for the selected object name and 22 DIP for the chooser title.
Separate toolbar and panel headers, panel bodies, and inset input and list
surfaces by lightness in a neutral dark gray palette. Give property groups
a compact header and boundary so they can be scanned as units. Use the
4 / 8 / 12 / 16 DIP spacing scale and a 30 DIP one-line rhythm. Grow
controls for long text or constrained layouts as needed.

Use project-authored line icons with a common 16 DIP grid, stroke and
optical weight. A smaller icon may accompany tab labels or disclosure
controls. Keep visible command labels and accessible names, including
Undo/Redo descriptions. Use subdued boundaries at rest, strengthen them
on hover and show a distinct focus ring. Reserve blue for selection, focus
and primary actions. Keep text and status icons for errors and warnings.
Require 4.5:1 contrast for enabled text and 3:1 for focus and essential
control boundaries, including search when at rest.

Use one set of X/Y/Z column headings for the Transform group. Align
Position, Rotation and Scale against those headings and use tabular,
right-aligned values. Keep each field's property and axis available to
assistive technology. When columns cannot fit, repeat the axis labels in
the reflow and preserve value precision, the draft and keyboard order.

**R3. Information hierarchy and chooser.** Give the Inspector a distinct
selected-object header, then group editable fields by component. Keep
asset names primary in references and list rows, with type and technical
details secondary. Derive a short label from data already available in
the session and fall back to a labeled reference or short identifier.
Keep long values discoverable in full. Use a keyboard-reachable disclosure
for raw GUID editing where the Inspector already permits it. Keep Choose
and Clear and their existing guards near the reference value. At compact
widths, wrap those actions below the value so the name retains usable
width. Reflow and
disclosure preserve the existing field binding, draft and focus rules.

In Assets, pin search, type filter and **Reload view** above the scrollable
results and selected asset area. Group existing geometry and combination
preview, material selection and **Add to Scene** around that selected
asset. Place Copy GUID, complete identifiers, paths and dependency
actions in **Asset details**. Group existing Rebuild and Import controls
in labeled sections within the same pane. Keep active and failed outcomes
and their next actions discoverable when a section is collapsed.
Keep the catalog snapshot guidance reachable, including when a Runtime
restart is needed.

In Saved Scenes, group the local unit list, optional managed import root,
Build/Open and current outcome. Keep Refresh and build configuration
with the panel header. Show whether the current result permits Open and
put complete checkout, build directory, executable and log text in
selectable details. Preserve local authorization, build inputs, result
invalidation and separate Editor process ownership.

In the chooser, give search and the Confirm/Cancel footer stable positions.
Use roughly equal result and preview columns at the standard size, with
slightly more room for the material preview. Show the opening mesh and
explicit **Preview with material** action above a contained image. Give the
selected candidate and Confirm a clear relationship through the footer.
Keep identifiers, the full opening mesh identity, schemas and dependency
actions in secondary details. Keep normal compatibility guidance as
secondary text. Below 720 DIP stack preview below results in the scrollable
body. The generic chooser uses the same structure without the preview.
Confirm continues to use its existing guard independently of preview.

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

The revised design uses layout A. It keeps the full-height
Hierarchy and right workspace and makes the Viewport the largest surface.
The compact layout narrows the side panes and lets the active workspace
scroll while the panel header and relevant filters stay in place.

| View | Before at the same size | Revised at 100% | Revised at 150% |
| --- | --- | --- | --- |
| Main window and Inspector | [1400 × 860](../images/0093-editor-visual-layout/main-window-before.png) | [1400 × 860](../images/0093-editor-visual-layout/main-window.png) | [2100 × 1290](../images/0093-editor-visual-layout/main-window-150.png) · [1440 × 900, compact](../images/0093-editor-visual-layout/main-window-compact-150.png) |
| Assets | — | [1400 × 860](../images/0093-editor-visual-layout/assets.png) | [1440 × 900, compact](../images/0093-editor-visual-layout/assets-compact-150.png) |
| Saved Scenes | — | [1400 × 860, build ready](../images/0093-editor-visual-layout/saved-scenes.png) | [1440 × 900, compact and stale](../images/0093-editor-visual-layout/saved-scenes-compact-150.png) |
| Material chooser | [900 × 630](../images/0093-editor-visual-layout/reference-chooser-before.png) | [900 × 630](../images/0093-editor-visual-layout/reference-chooser.png) | [1350 × 945](../images/0093-editor-visual-layout/reference-chooser-150.png) · [900 × 900, compact](../images/0093-editor-visual-layout/reference-chooser-compact-150.png) |

The matched comparisons re-render the preceding A and chooser layouts
using the same scene, sample values, candidate list and actual preview as
the revised designs. The 2044 × 1411 Bistro scene capture is shared across
all main window images. Geometry and combination images come from the
existing Runtime preview calls for Bistro mesh `5dcf249d-1d02-8cb1-b6c8-e1006682c68b`.
The combination uses the catalog's Showcase wood reddish stain material
`057d2d26-8bb6-4a8a-a05a-39ec65a84857`. Both are the existing 256 × 256
PNG output. Scene capture and preview retain their colors. UI states and
Saved Scenes examples are staged for design review. The mockups render
each display scale independently on Windows with Segoe UI.

Use `#181819` for the canvas, `#202022` for panes, `#262628` for headers,
`#2D2D30` for raised controls, `#1B1B1D` for insets and `#39393D`
for separators. Use `#EFF0F2` for primary and `#AFB0B5` for secondary text.
Use `#283B54` with a `#9CC7FF` marker for selection, `#B0D4FF` for
focus, and `#A5CAFF` with `#142032` text for a primary action. Use
`#74747B` for essential outlines. Use `#A2D3BB` for success,
`#F1C990` for warnings and `#FFC0BF` for failure.

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

The earlier wide Viewport study stacked Hierarchy over Inspector and
reduced the height available to each. The three-pane layout better
supports sustained tree and property work at compact sizes. Colors and
spacing alone would leave property scanning, action placement and result
hierarchy unresolved. Keep the present window and Fluent architecture.

## Testing & Verification Plan

[Plan 0093](../plans/0093-editor-visual-layout.md#verification-checklist)
requires actual Windows Editor windows with the embedded Runtime, a size
and DPI matrix, long content and representative interaction states. The
mockups support design review; they are neither implementation evidence
nor engine image baselines.

## Risks & Open Questions

Review the refined three-pane direction, typography, panel grouping and
compact behavior before implementation. The main risks are keeping the native child HWND aligned
during resize and preserving field focus and draft settlement through
disclosures and reflow. The Plan tests both explicitly.

## Out of Scope / Future Work

Theme switching, custom window chrome, dockable panels, new font and icon
packages, asset pipeline changes and restyling other applications.
