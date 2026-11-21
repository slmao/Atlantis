# Spec 0131 — Bistro Reference Views and Image Comparison

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0131](../plans/0131-bistro-reference-views.md)
- **Approval:** Maintainer approved revision `637eaefd` in [PR #425](https://github.com/slmao/Atlantis/pull/425#issuecomment-7063983586), 2026-11-21.
- **Related ADR:** [ADR-0180](../adr/0180-shared-frame-report-exposure.md) (Accepted)

## Summary

Give the Windows Editor two repeatable Bistro viewpoints. An artist can return
to the street corner or storefront terrace, preview and capture the current
Runtime projection, and compare it with a credited NVIDIA ORCA image. The
views belong to Editor navigation. The current scene and its active camera
remain available for normal editing.

## Motivation and existing capabilities

[0109](0109-viewport-navigation.md) already settles perspective navigation
against the embedded viewport's physical size. Its scene-camera read supplies
the underlying pose and lens. The camera override replaces the pose and lens
used to render. Exposure, fog and bloom still come from the active scene camera.
The shared [step and capture contract](../architecture/remote_protocol.md#5-methods)
returns the drawn view and projection matrices, direct lights, and an optional
overlay-free PNG. Runtime captures at the current drawable extent. The request
cannot select an output size and the report currently omits exposure.

The [Bistro build](0046-bistro-finale.md) uses pinned NVIDIA RTXDI assets and an
Atlantis-authored camera and lighting overlay. The ORCA gallery gives visual
references for the same place. It does not associate its gallery PNGs with
reproducible camera or lighting settings.

## Requirements

### R1 — Two fixed viewpoints

Offer **Street Corner** and **Storefront Terrace** in the existing Viewport
toolbar. Scope them to the Windows Editor's own embedded Runtime. Use the
built-in Bistro or explicitly opt in for the current document when working
with a hand-authored Bistro copy in the same world coordinates. Opt-in applies
to the current document occurrence. The Editor makes no inference from a
scene name.

Freeze one versioned Editor descriptor per view, with a world-space eye and
forward vector, vertical FOV, near/far planes, physical output extent and ORCA
reference identity. Use Runtime's +Y, roll-free camera convention. Start with the
existing [Bistro overlay](../../assets/bistro/bistro_overlay.scene.txt) lens
(vertical FOV 1.0472 radians, near 0.1, far 200) for both. For Street Corner,
start from the authored wide shot and frame the corner facade, curb and street
foreground. For Storefront Terrace, select an eye under the awning at seated
height, looking along the shopfront so a foreground table, awning and brighter
street opening remain visible. Calibrate the two poses once on the pinned
Bistro scene in S2 and record the final values and three stable landmarks per
shot for review. Normal use applies the frozen descriptors directly, independent
of selection, pivot, prior camera, scene-camera lens or edited objects.

### R2 — Repeatable live preview and capture

While a reference view is active, center the existing embedded draw surface
at **960 × 540 physical pixels** with padding as needed. Its projection and
the capture target share that extent. A window that cannot fit the area keeps
normal navigation available and explains what size is required. Resize, DPI
and drawable transitions invalidate unsettled captures. Re-enable capture
after the child reports the fixed physical extent and the existing settle
path observes a newer frame. A capture at another size is a refusal, with its
image excluded from comparison.

Use the existing 0109 worker, camera override and settle rules. Gate pick and
Gizmo hit tests while the pose or size changes. Normal picking and editing
remain available in the settled view. Explicitly leaving the reference view,
or initiating normal camera navigation, restores the flexible viewport and
continues from the settled reference pose. **Scene Camera** follows its
existing clear and resample action. These camera and layout actions preserve
selection, active camera identity and authored camera values, document
revision, dirty state, Undo/Redo and Apply state. They neither commit nor
discard an Inspector draft. Use existing guards during an active edit gesture.
The reference mode, opt-in and comparison are session-local and do not change
the workspace layout preference.

### R3 — Capture the visible Runtime state

Capture on explicit request using the existing overlay-free one-frame PNG
path from the running Runtime. Disable capture while paused or while an Apply
is unsettled, a preview gesture is active, or the projection is unavailable.
Label a capture as the last visible projection if a document edit awaits
Apply or Apply has failed. The capture action submits no authoring operation
and never invokes Apply or Save. The existing step can service a frame.
Admission and the post-capture check depend on the observed session state
under the existing last-writer camera policy.

Before presenting a new pair, match the preset by the reported view and
projection matrices, verify the receipt and decoded PNG extent, and verify
the same connection, document occurrence, Runtime scene generation, viewport
size and latest reference intent. Use Editor-owned, unique local output paths.
On refusal, missing file or lost reply, keep any earlier successful pair
labelled with its captured view and frame until the session ends. Ignore stale
completion after a new intent or session change. A lost reply leaves the
requested file's outcome unknown. Show that uncertainty and require an
explicit new capture. Retain one comparison pair. Release temporary images
on replacement or session end without touching user-owned reference files.

Record the captured frame, Runtime scene and build/content identity when
available, reported direct-light values, and effective exposure compensation
in EV through [ADR-0180](../adr/0180-shared-frame-report-exposure.md). Record
environment/lighting or build provenance in acceptance evidence when known.
Show absent exposure as **Unavailable on this Runtime**. Zero EV is a valid
measurement. Older hosts retain ordinary navigation and capture.

### R4 — Credited, descriptive comparison

The official [Amazon Lumberyard Bistro ORCA gallery](https://developer.nvidia.com/orca/amazon-lumberyard-bistro)
is the source of both reference images. The two original PNGs are 1920 × 1061.

| View | Official reference | SHA-256 of original PNG |
| --- | --- | --- |
| Street Corner | [Bistro Exterior 1](https://d29g4g2dyqv443.cloudfront.net/sites/default/files/akamai/Bistro_Exterior_1.png) | `35c3c824fe342e92549a525d11e7b80ea4e1b39fe3d4ef1323e4a688253579dc` |
| Storefront Terrace | [Bistro Exterior 2](https://d29g4g2dyqv443.cloudfront.net/sites/default/files/akamai/Bistro_Exterior_2.png) | `a5f18071df20c4068dcff6d82c22a2c784148a52e2ed2af679f789e2c0cc0cd0` |

Let the user open the source link and provide the matching official PNG
locally. Keep gallery artwork out of the repository and installer. Verify the
selected file's original identity before decoding it, with a 5 MiB input cap.
For missing or changed bytes show how to supply the correct file and keep the Atlantis capture
usable. Show the two images side by side using aspect-preserving fit with
the original dimensions, attribution and source link. Label the gallery
image **Visual reference**. Its capture pose, FOV, exposure, direct/ambient
lighting and color pipeline are **Unknown** unless documented for that
exact still. The presence of Falcor files in the dataset does not establish
which settings produced a gallery still.

Credit Amazon Lumberyard and NVIDIA ORCA for the official still source.
The [upstream Bistro asset provenance](../../tools/content/bistro_source.provenance.txt)
also supplies both attribution lines for Atlantis-derived captures. Existing
Atlantis screenshots and image goldens have their own provenance.

## Architectural Impact and alternatives

Extend the existing shared frame report with optional exposure on the
explicit report path, as proposed by ADR-0180. Editor owns preset selection,
window layout, source files and comparison. Reuse RuntimeControl for CLI,
native clients, C# SDK and MCP. Preserve existing camera ownership, frame
extraction, RenderGraph, input routing and scene serialization. This design
adds no recurring capture or per-frame report work, dependency, rendering
algorithm or alternate offscreen resolution API.

Storing the views as authored cameras would change scene state and bind the
workflow to authoring history. A render-to-size API would require new capture
resource and extent behavior. Reading exposure from the source cannot
identify the value used by the currently visible projection after edits.

## Testing and verification

[Plan 0131](../plans/0131-bistro-reference-views.md) maps checks to R1–R4.
Acceptance proves repeatability between two Atlantis visits to each frozen
view, including a navigation excursion between visits. ORCA is the labelled
comparison column. Visual parity, pixel equality with ORCA, alignment scoring,
image registration, lighting matching and changes to the rendering algorithms
are outside this increment. Custom bookmarks, additional shots, persistent
comparisons and automated capture are deferred.
