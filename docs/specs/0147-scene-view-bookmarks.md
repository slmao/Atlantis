# Spec 0147 — Scene View Bookmarks and Reference Comparison V1

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-27
- **Design baseline:** `9f353a36` on `main`, maintainer merge of [PR #455](https://github.com/slmao/Atlantis/pull/455)
- **Related Plan:** [Plan 0147](../plans/0147-scene-view-bookmarks.md)
- **Related ADR:** [ADR-0193](../adr/0193-bounded-render-look-capture-extent.md) (Proposed)
- **Approval:** Pending joint review.

## Summary and motivation

Save a named view of an open hand-authored scene, return to the same
composition, and compare a new screenshot against a locally chosen reference
or two of the scene's saved render looks. [0131](0131-bistro-reference-views.md)
already settles a fixed physical drawable and checks the observed frame
matrices. Its two calibrated viewpoints and ORCA image identities are built in.
[0145](0145-render-look-presets.md) and [0146](0146-render-look-capture-descriptors.md)
reuse the shared camera shape for isolated A/B captures but fix their output
at 960 × 540. Custom composition needs a scene-local pose and extent, and a
bounded way for A/B to use that same extent.

## Requirements and proposed design

### R1 — Save and scope a view

Add **View Bookmarks** alongside the existing viewport navigation actions
in the Windows Editor. On an explicit **Save Current View**, use the hosted
child's confirmed physical drawable and effective perspective camera. Use the
Worker's last acknowledged override or the active scene camera from the
existing navigation query. Check these inputs against a newer frame's view
and projection matrices using the existing frame-report path before writing
the bookmark. Require a settled camera, a presented drawable and a stable
hand-authored document occurrence. Reject guessed poses, unsupported lenses,
pending navigation or changes observed during sampling. A save needs no PNG.

Store a stable local bookmark ID, trimmed user name, finite world-space eye
and forward, vertical FOV, near/far clip planes and the two physical pixel
dimensions. Reconstruct the pose with the existing +Y camera convention.
Extent must be an integer in **[128, 1920] per axis**, with area at most
**2,073,600 pixels**. Use the exact observed dimensions. Show the limit and
offer instructions to resize the viewport when it is outside the envelope.
Store up to 16 bookmarks per scope, with names up to 64 UTF-8 bytes and
case-insensitive uniqueness. Explicit Replace keeps the bookmark ID and
changes its view. Explicit Remove deletes only that bookmark.

Write a versioned, bounded local file per Windows user at
`%LOCALAPPDATA%/Atlantis/ViewBookmarks/v1/<scope-hash>.json`. Use scene Asset
GUID and the verified canonical source path, as in the Render Looks store.
Resolve the current source from the live hand-authored document and reuse the
ordinary-file/path and atomic replacement rules. The scope grants no checkout,
source-write or launch access. Save As, a moved source or a different scene
GUID uses a separate scope. Reopening the same source with the same scene GUID
loads its bookmarks. A successful normal Save or Apply, lighting changes and
geometry edits leave the saved observation usable. Bookmarks carry no node
bindings, transient document tokens, Runtime IDs or appearance values.

Bound the file at 64 KiB. Verify its version, scope, IDs, names, poses and
extents on load, and recheck the read revision before any replacement. An
unavailable source or unreadable, unsupported or invalid store yields a
diagnostic and prevents writes to that scope. Preserve bytes on stale writer
or replacement failure and allow an explicit refresh and retry. No automatic
migration, transfer, deletion or cross-scene discovery is needed.

### R2 — Return to a view without editing

Select a bookmark to apply a navigation-only override and center an embedded
drawable of its exact saved size. Reuse 0131's size/pose settle, capture
admission and pick/Gizmo gates. Hold picking, Gizmo hits and capture until
the current child extent and a newer frame confirm the pose and projection.
If the available area cannot fit, report the required size and keep the view.
After a resize or DPI change, revalidate and re-settle at the saved physical
extent, or leave fixed-size mode with a diagnostic. Never substitute a
different aspect or resolution. Ordinary navigation leaves fixed-size mode
from the last confirmed pose. **Scene Camera** uses the existing return path.
Opening a scene restores its list of bookmarks and starts in normal navigation.

Before a gesture or focus transfer that would submit, discard or replace an
unsubmitted draft, refuse the bookmark action and leave the existing draft
and focus available. Respect pending gestures and preview, visibility and
projection protections. View actions preserve document bytes/revision,
selection, authored camera identity and values, Apply state and Undo/Redo.
They issue no authoring commands. Use the existing last-writer camera policy.
Check Worker/session, document occurrence, bookmark identity/revision,
camera intent, viewport stamp/extent and frame freshness at the existing
send and consume boundaries. A changed document occurrence or Runtime
connection retires in-flight work and the active mode. A changed projection
generation requires a fresh settle before capture. Changes to the camera,
viewport or bookmark retire affected in-flight work. Late results cannot
reactivate a view or attach an image to another bookmark.

### R3 — Capture and compare a local reference

Capture an explicit overlay-free PNG of the settled view through the existing
shared one-frame capture call. Ask the user to restore the default viewport
visibility before capturing or comparing. Verify the echoed output path,
exact PNG dimensions, frame freshness and view/projection matrices within
`0.0001` per element. Retain frame and projection provenance and show observed
exposure when available. While Apply is pending or unknown, refuse capture.
If a prior projection remains visible following a failed or deferred Apply,
label it **Last visible projection**. Require a still-verifiable settled view.
Reuse 0131's refused or unknown-outcome handling and temporary file ownership,
including no automatic retry.

Accept an explicitly selected local PNG with at most 8 MiB encoded and at
most 4096 pixels per axis. Validate the byte budget and header dimensions
before decoding and check decoded dimensions against the header.
Hold one accepted reference snapshot in memory for the current comparison.
Show the capture and reference side by side at their original aspect ratios,
with actual dimensions and local reference name. Treat the reference capture
conditions and provenance as unknown where the user supplied no evidence.
A canceled picker, bad PNG or stale decode leaves the current pair available.
Release comparison images when changing bookmark, closing the comparison or
retiring the document; select the reference again after reopen. Local reference
files are opened read-only and the selection is never copied into the project.

### R4 — Use the same composition for A/B

Let Render Looks select a saved bookmark alongside the fixed 0131 views.
A/B uses the stored eye, forward, lens and exact extent in the existing 0145
comparison. It retains the settled projection, camera/light roster and
placement guard. Both variants render against the same frozen basis. Switching
A/B selects between the two completed images and leaves the live viewpoint
and authoring state alone. Explicit Adopt continues to use 0145's contract.
Refuse stale or partial pairs and require both images to match the bookmark's
matrices and physical dimensions before display. Never fall back to a pair
rendered at a different extent. Preserve legacy 960 × 540 A/B for older hosts
with the original capture capability. Show a capability diagnostic for other
extents on such hosts; navigation and ordinary capture can still be available.

## Architectural Impact and alternatives

[ADR-0193](../adr/0193-bounded-render-look-capture-extent.md) supplements the
shared A/B wire contract and bounds the offscreen render extent. Reuse the
existing RenderGraph path, Vulkan descriptor pool and ownership settlement
from 0146, and the existing Renderer, camera override and capture methods.
Do work on explicit actions and the fixed-view settle path. Scene format,
renderer algorithms and project asset pipelines need no changes.

A fixed 960 × 540 for every custom bookmark would lose compositions from
drawables with other aspect ratios. Storing an authoring camera would mix
navigation with edits and history. Resizing or cropping PNGs after capture
would not restore the saved projection. Persistent reference attachments,
multi-image comparisons, orthographic projections, automated alignment and
cross-scene bookmark sharing are outside V1.

## Testing and verification

[Plan 0147](../plans/0147-scene-view-bookmarks.md) maps R1–R4 to a Windows
100% workflow on a non-built-in manual scene, shared-client conformance,
bounded capture checks and applicable Vulkan, image and performance gates.
