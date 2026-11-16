# Plan 0111 — Viewport Camera and Light Guides

- **Spec:** [Spec 0111](../specs/0111-viewport-camera-light-guides.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending approval of both documents and authorization for S2–S4.

## Objective

Draw the selected Camera or Light's authoring guide through the existing
Viewport overlay, with an explicit toggle and clear separation between the
committed document and the Runtime image.

## Milestones

1. **S1 — Joint review.** Resolve J1–J4 and obtain explicit joint approval
   before manual design merge.
2. **S2 — Implement.** Add a UI-free guide builder and tests. Integrate
   selection, toggle and coalesced document refresh in the current worker,
   including property/ancestor invalidation and stale read guards. Compose
   with the existing overlay, including selection, clear and refusal paths.
   Wire a Viewport toggle and feedback through the existing toolbar and
   focus rules. Keep the existing Apply and pick routes.
3. **S3 — Verify.** Run the mapped checks against the final candidate.
   Record affected GPU and desktop evidence and the final gate selection.
4. **S4 — Submit.** Open a review-ready implementation PR with scope,
   evidence provenance, any deviations and final human merge conditions.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/ViewportGuides.cs` | Guide geometry, validation and bounded inputs; in-process managed model only. Reuse Camera convention, schema addressing and ancestor world-matrix math. |
| `src/editor_app/Atlantis.Editor.Model/{Camera,SceneMarkers,GizmoOverlay}.cs` | Reuse the camera basis and compose extra segments with the ordinary overlay. Keep marker picking and positions unaffected by the new toggle. |
| `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` | Worker intent and authoring observations; overlay submission, fallback and status; toolbar, layout and focus integration. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{ViewportGuideTests,Program,InspectorTests}.cs` | Focused model, worker/overlay and Windows UI acceptance with fixtures as needed. |
| `README.md`, `docs/specs/README.md` | Usage and delivery navigation. |

Keep Runtime, World, Renderer, RenderGraph, RHI, shader, scene serialization,
shared transport/client packages, build and CI source outside this diff.
Fixtures are disposable. Record deviations under the existing repository
rules.

## Sequencing and dependencies

Design baseline is `4a099c83`, the manual merge of [PR #386](https://github.com/slmao/Atlantis/pull/386).
Start implementation from latest main after S1 and manual design merge.
Reuse [0066](../specs/0066-editor-overlay-and-input.md) for drawn primitives,
[0068](../specs/0068-authoring-editor.md) for the document and selection,
[0083](../specs/0083-authoring-apply-outcomes.md) for Runtime outcomes,
[0093](../specs/0093-editor-visual-layout.md) for visual and focus rules,
and [0109](../specs/0109-viewport-navigation.md) for projection and picking.

The worker should read only the selected input and its ancestor chain.
Use shared batched property reads with the existing limits, and invalidate
against the observed component/ancestor data. A result that races with
another edit or intent is discarded. Refresh through the existing worker
queue on a new relevant observation, without a separate polling loop.
If a newer intent arrives after an overlay send, the next worker update
replaces the older client overlay at the normal frame boundary.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Entry and visibility** | One window-local **Guides** toggle, Off by default, for one selected node. Use depth-tested accent lines, retain existing marker/Gizmo/mesh picking and make strokes read-only. |
| **J2 — Shapes and limits** | Use Runtime-compatible perspective basis and drawable aspect, three fixed point-range circles and a short directional arrow. Approve the two-group/84-segment cap and per-shape refusal for unusable geometry. |
| **J3 — Source and recovery** | Show committed authoring separately from Runtime projection. Suppress guides during local Gizmo preview, use verified document reads after edits and history replay, and retain 0083 Apply recovery. Reuse the existing shared overlay and capture boundaries with no new ADR. |
| **J4 — Acceptance** | Approve the mapped Windows 100%, capture, Vulkan and applicable repository gates. Authorize S2–S4 together after manual design merge. |

Suggested one-time authorization for the reviewed revision:

> I approve J1–J4 and Spec/Plan 0111 jointly, including using the existing
> ADRs. After the design PR is manually merged, proceed from latest main
> through implementation, scope-preserving fixes, applicable verification
> and a review-ready implementation PR. Reuse valid evidence and retain
> applicable gates. Final merge is manual.

## Verification checklist

- [ ] **R1/R2 — Geometry and display.** Check camera near/far corners and
  aspect in wide and narrow views against the current Runtime convention,
  including parent rotation/scale, roll and near-vertical degeneracy.
  Check point range under parent scale and directional emission orientation
  independent of range. Cover a node with both components, missing fields,
  zero extent, nonfinite values, overflow and precision collapse. The
  combined result stays within the segment/group cap. Confirm Off and
  ineligible selections use the prior overlay.
- [ ] **R1/R3 — Picking and shared surface.** In the real Editor, cover an
  existing origin marker, a renderable camera/light via mesh and Hierarchy,
  a line in front of geometry and a guide-only miss. Keep Gizmo priority and
  the existing projection-settlement guard during a camera switch. Run a
  separate shared client over the ordinary overlay call. Verify independent
  replace/clear and disconnect, refusal fallback, an overlay-free capture,
  and an explicit own-overlay capture isolated from other clients. Reuse
  corresponding codec and client capability checks only with matching inputs.
- [ ] **R4 — Document versus projection.** With a held guide read, change
  selection, resize, toggle Off, reopen or replace a row with the same GUID.
  Verify only the current result can send a guide. Check committed lens,
  light range/kind, ancestor motion and component removal. Confirm staged,
  refused and abandoned edits do not appear as committed input. Check a
  cancelled Gizmo preview and committed motion, Undo and Redo. Hold an Apply
  while paused. For reload failure, use a disposable scene dependency that
  is available to authoring but unavailable to Runtime, as in 0110, then
  restore it and explicitly Retry. Verify the committed guide remains attributable to the
  document while the image retains the last good projection and existing
  Apply text distinguishes the states. Check that toggling adds no history,
  document revision or Apply receipt.
- [ ] **Windows 100% desktop.** In a disposable hand-authored scene, use
  0110 to create a Camera, Point Light and Directional Light. Select each
  with Guides enabled, edit a lens or light field, exercise Undo/Redo, Save
  and reopen the same source. Verify the same authored guide after
  reselecting and that fresh windows default Off. Check standard and
  compact layouts, a resized hosted Viewport, marker and mesh click
  alignment, and Inspector focus/draft retention when toggling. Retain
  native evidence of the three guides and the held or failed Apply state.
- [ ] **Gates.** Build and run affected Editor model, worker/UI and Runtime
  overlay checks. Run the visible and capture paths with Vulkan Validation
  Layers active, using a deterministic functional overlay capture. Check
  the base image with default capture semantics and reuse unaffected golden
  evidence only with matching inputs. Run the selector
  `atlantis_runtime --gate --applies` on the complete implementation diff
  and follow the existing protocol if selected. Evaluate native and Android
  build applicability from the final diff. S1 needs document and link
  checks only.

## Rollback and definition of done

Revert the Editor toggle and guide composition. Saved scenes contain the
ordinary light/camera fields and need no migration. Follow the existing
[definition of done](../process/definition-of-done.md).
