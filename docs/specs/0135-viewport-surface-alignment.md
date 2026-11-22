# Spec 0135 — Precise Viewport Surface Alignment

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-22
- **Related Plan:** [Plan 0135](../plans/0135-viewport-surface-alignment.md)
- **Approval:** Maintainer joint approval of Spec/Plan and J1–J4 at `d9ac83c1`, including the J3 race. [Approval record](https://github.com/slmao/Atlantis/pull/433#issuecomment-7074129488). Design merged by the maintainer as `e374506b`.
- **Related ADR:** [ADR-0184](../adr/0184-shared-alignment-face-query.md) (Accepted)

## Summary

Mate the end faces of two storefront wall sections in a hand-authored
Bistro scene. The user picks the moving face and the fixed target face,
sets a gap in world units, inspects the proposed position, and confirms or
cancels one translation. The tool uses the faces to position the node even
when its pivot is offset from the seam.

## Motivation and existing capabilities

The single and group Gizmos quantize translation to **0.5 world units**,
rotation to **15°**, and scale to **0.1 of a factor**. These are drag
increments, with no surface geometry or mate relationship.
[Spec 0128](0128-viewport-mesh-placement.md) uses the existing retained CPU
triangle pick to place an asset pivot. Its shared `control.raycast` returns
an entity and distance. A hit point or a world AABB cannot identify a
face's normal, extent, or center.
[0109](0109-viewport-navigation.md) supplies the confirmed perspective
projection and pixel route. [0111](0111-viewport-camera-light-guides.md)
and [ADR-0132](../adr/0132-viewport-overlay-surface.md) supply client-owned
overlay primitives. The one-node transform guard, history settlement and
Apply path already exist in [0107](0107-multi-object-gizmo.md) and
[0083](0083-authoring-apply-outcomes.md).

## Goals and scope

Support one selected static renderable and one fixed renderable in the
same writable hand-authored document. Mate congruent rectangular end faces
that already oppose each other and admit translation. Existing Gizmos can
orient a wall before alignment. This operation keeps the moving node's
rotation, scale, hierarchy and asset bindings. Descendants follow their
parent normally. The target must be outside the moving subtree.

## Requirements

### R1 — Pick, preview and confirm

Expose **Align surfaces** beside the selection's Viewport tools. Require
one current, visible, editable selection and a settled authoring projection
as defined in R4. Preserve a draft and show a reason if a draft, a staged
edit, a drag, a leave operation or a conflicting action prevents entry.
Follow 0093 for status, focus and keyboard behavior.

Pick the moving face on the selected node, then pick the fixed target face
on a different node. Use the existing forwarded Windows Viewport clicks,
drawable client pixels and confirmed camera. Route these clicks through
the geometric pick only. Camera and light markers, Gizmo hits and overlay
lines provide no surfaces. Use the view-depth clip check with each click's
camera and lens. Keep the saved rays in world space and allow ordinary
navigation between clicks so the user can view opposing sides. A change to
camera or extent expires an in-flight click sampled under the old view.
Stable world faces may be inspected from a new view after their reads
settle. Navigation during queued confirmation expires that unsent attempt.

Provide a numeric **Gap** in world units, initially zero. Show which two
objects and faces are used, the current result or refusal, and explicit
**Confirm** and **Cancel**. Preview the fixed face and the translated
outline of the moving face with an optional connector showing the gap.
The current scene geometry stays where it is until Apply. Use at most two
four-edge outlines and one connector in the existing per-client overlay.
Only a current, admitted overlay of a valid candidate enables Confirm.
An invalid change or a failed pick clears the old candidate, with the
reason shown. Use one read in flight, with work triggered by explicit
clicks and confirmation. Editing Gap solves against the accepted faces.
Escape, Cancel, deactivation, selection or document changes, or disconnect
retire unsent work and remove the alignment contribution. Restore normal
Viewport input and the current ordinary overlay. Late replies may not
restore retired guides or controls. Overlay replacement failure before send
cancels the attempt. Follow ADR-0132 cleanup and requester-only capture.

### R2 — Eligible faces

Base eligibility on the **first static triangle hit** under the existing
CPU pick rules. For this tool the hit must be an opaque, recognized
renderable with retained mesh data, a finite affine, nonsingular,
right-handed, unsheared world basis and a front-facing hit. An unsupported
nearest hit or an ambiguous hit refuses the pick. A farther candidate may
never substitute for it. This is CPU geometry selection. Pixel alpha,
shading normals and material normal maps do not define the contact plane.

A face is one rectangular end cap proven by exactly two consistently
wound, nondegenerate triangles of the hit mesh. Their shared edge is the
diagonal. Match local endpoint coordinates exactly, normalizing signed
zero, so identical positions at an imported vertex split are supported.
The union must have four distinct corners and a convex rectangular
perimeter in world space. Require the pair to be the only nondegenerate
triangles of that mesh in the supporting plane within the geometric
tolerance. Refuse additional coplanar triangles, overlapping or ambiguous
pairs, invalid data, unsupported material or transform, and a hit mesh
over 65,536 triangles. A tie between the two triangles of the same proven
cap is valid on its internal diagonal. A tie between distinct faces or
entities is ambiguous. A ray at or within the tolerance of the exterior
edge is refused. A single triangle, a curved face or a surface with an
arbitrary triangulation cannot establish a supported cap.

Use widened precision for geometric checks. For one cap let `L` be its
longest perimeter edge and `ε = max(1e-5, 1e-5 × L)` world units.
Require edges of length at least `max(1e-3, 100 × ε)`. Coplanarity and
opposite-edge agreement are within `ε`. Adjacent edges are orthogonal
within **0.1°**. Use `ε` for exterior-edge clearance and an equally near
hit's distance along the unit ray. Compute the unit normal from the first
triangle in ordinal order after world transformation and retain its winding.
Require its dot product with the unit ray direction to be less than
`-1e-6`. Reject grazing and back-facing rays. Negative world
determinant (reflection), singularity and shear are outside the supported
basis. Nonuniform scale is permitted when the resulting world cap passes
these checks. Refuse geometry whose floating-point representation cannot
support the same tests after encoding.

### R3 — Exact translation from face evidence

Let `s` and `t` be the means of the four moving and fixed face corners,
`n` the fixed face's unit normal and `g` the requested gap. Require finite
`g ≥ 0` and opposing geometric normals within **0.1°** of antiparallel.
Evaluate `d = t + g n − s`. Match the four moving corners translated by
`d` with the fixed corners offset by `g n`, under a cyclic
winding-reversing correspondence. With `L` the longer of the two cap
extents from R2 and the same `ε` rule, require each matched corner's
three-dimensional residual to be ≤ `ε`. Refuse unequal extents or an
in-plane orientation mismatch that cannot meet that criterion. Visual
success in the reference scene is a geometric seam, independent of
materials, shadows, and any other part of the meshes.

Apply `d` to the moving node's current pivot. A root adds `d` directly
to local position. For a parented node transform `d` by the inverse of
the parent's world **linear** basis and add the result to local position.
Keep the exact authored local Euler and scale leaves. Reject an unstable
or non-invertible parent, overflow, or a serialized Float32 local position
whose recomposed world corners fail the same residual bound. Evaluate
the preview from that representable position. The gap is measured along
the fixed face normal. Positive gap moves the face outward and the
general Gizmo Snap switch has no effect. An exactly unchanged stored local
position settles without a write or history.

### R4 — Staleness, transaction and recovery

At arm, require a current document token and the last successful tracked
Apply receipt for that same document and revision, with a matching
Runtime scene generation. A pending, failed, unknown or unproven
projection prevents alignment; on a fresh open with no positive receipt,
ask the user to Apply the current document explicitly. At first pick,
capture worker session, document ID/lifetime/revision, selection identity,
each participating node, its relevant Transform and Renderable
incarnations, mesh binding and ancestry as it is observed. Require exact
mesh GUID agreement. Compare each live matrix with the composed authoring
matrix using 0107's basis-column tolerance. Invert the queried matrix to
recover the four local corners and require the composed authoring matrix
to reproduce the live corners within the R2 position tolerance. No
incomplete read establishes equivalence. Keep the
document revision fixed through confirmation. A document edit, reparent,
same-GUID replacement, projection change or expired UI intent cancels the
unsent attempt.

When picking the target, and again before Confirm sends, recast both
original world rays in one shared query. Require the same first-hit
evidence for the source and, on confirmation, the target. Recheck the
document, parents, renderable bindings, projection and UI intent. Compare
the structural context and the exact canonical face evidence. If changed,
clear preview and explain that the user must pick again. Recheck after
capability discovery at the serialized Editor send boundary. Submit
`RequireTransformBatchRevision` plus one `SetProperty(localPosition)` via
the existing authoring transaction. Never fall back to unguarded editing.
The host fences concurrent authoring edits at admission. The read and
transaction are separate, so a Runtime-only write racing after the last
geometry sample or an unobservable change-and-restore lies outside the
guarantee. There is no new Runtime-to-authoring lock or mutation verb.

Settle the original transaction even if UI intent changes after send.
Use 0107 ticket/failure and value read-back to record one history entry
only after verified commit, publish history before fallible refresh and
settle correlated Apply independently. On unknown submission show the
intended node and position and ask the user to inspect before retrying.
Retain older Apply settlement. Known refusal or pre-send expiry writes
nothing, adds no history and requests no Apply. Undo/Redo replay the exact
before/after local positions under the source, ancestry, leaf and fresh
revision conflict guards. They never re-pick or follow a changed target.
Keep verified history on failed or unknown Apply. Explicit Apply
snapshots the current document without repeating alignment. Save and
reopen through the same scene source and explicit Apply path. The exact
local position, unchanged rotation and scale, asset bindings and the
world seam persist. Alignment intent, previews and face evidence are
transient.

## Architectural impact

[ADR-0184](../adr/0184-shared-alignment-face-query.md) defines the additive
shared face read, where Runtime proves the geometry, and the boundary
between that read, Editor math and existing guarded authoring. There is
no Renderer, RenderGraph, GPU input, source format, package or scheduler
change. The operation does no per-frame sampling when idle.

## Alternatives considered and later work

Surface depth and an AABB alone cannot prove a rectangular seam. A
general coplanar traversal or an Editor-side asset reader would expand
the geometry and compatibility boundary. Automatic rotation would
introduce a roll choice and could produce shear under a parent. Keep
manual preparation with Gizmo and a narrow positional tool. Batch
alignment, vertex snapping, repeated arrays and arbitrary surface mates
are outside V1.

## Verification and decisions

[Plan 0135](../plans/0135-viewport-surface-alignment.md) maps R1–R4 to
deterministic geometry checks, shared-client compatibility, Windows 100%
Bistro evidence and applicable gates. J1–J4 are pending human decision.
