# Plan 0111 — Viewport Camera and Light Guides

- **Spec:** [Spec 0111](../specs/0111-viewport-camera-light-guides.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** The maintainer approved J1–J4 and Spec/Plan 0111 at [PR #387](https://github.com/slmao/Atlantis/pull/387), revision `a8222bf0`, on 2026-11-17. No new ADR is needed. S2–S4 were authorized after manual design merge `bd56e5bc`.
- **Approved scope revision:** On 2026-11-17, the maintainer approved the
  ComponentExclusivity correction for the `b0b99b86` implementation in [PR #388](https://github.com/slmao/Atlantis/pull/388).
  Real-scene guides use valid camera/light nodes, mesh picking uses a
  separate Renderable, and mixed-component checks use real host refusal and
  synthetic model inputs. The Windows 100%, capture, Vulkan and other
  applicable gates remain required.

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
| `src/editor_app/Atlantis.Editor.Model/{Camera,SceneMarkers,GizmoOverlay}.cs` | Reuse camera and transform math where applicable and compose extra segments with the ordinary overlay. The guide read must be strict. Keep the existing marker path and its fallback behavior. |
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

Use the shared membership read and pipelined property reads for the bounded
input in R4. At most 197 property values are needed: three Transform leaves
per node plus three lens and two light leaves. Invalidate on a required field,
component or ancestor change. Consume document events even when a guide read
is discarded. Recheck intent at send and feedback boundaries, including
fallback, and reuse the existing worker queue.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Entry and picking** | Approve the window-local **Guides** toggle, Off by default, for one selected node. Use depth-tested accent lines and keep group/single Gizmo, origin marker and mesh picking priority. |
| **J2 — Geometry and limits** | Approve the selected camera's perspective lens and drawable aspect, the authored point radius and the directional emission arrow, with the two-group/84-segment cap and per-shape omission for unusable geometry. |
| **J3 — Document, recovery and isolation** | Approve the strict 64-node chain, current-intent guards, preview suppression and committed Undo/Redo feedback throughout Apply suspension. Reuse per-client overlay and capture ownership and 0083 Retry. No new ADR is needed. |
| **J4 — Acceptance and authorization** | Approve the mapped Windows 100%, controlled Apply recovery, capture isolation, active Vulkan validation and applicable repository gates. Authorize S2–S4 together after manual design merge. |

Suggested one-time authorization for the reviewed revision:

> I approve J1–J4 and Spec/Plan 0111 jointly, including using the existing
> ADRs. After the design PR is manually merged, proceed from latest main
> through implementation, scope-preserving fixes, applicable verification
> and a review-ready implementation PR. Reuse valid evidence and retain
> applicable gates. Final merge is manual.

## Verification checklist

- [ ] **R1/R2 — Geometry and display.** Check camera near/far corners and
  aspect in wide and narrow views against the current Runtime convention,
  including parent rotation/non-uniform scale, roll and near-vertical
  degeneracy. Select a camera with a lens different from the camera used
  to view it. Check point range under parent scale and directional
  emission independent of range, including vertical emission and the
  projected shaft length when side-on. Use a synthetic dual-component
  fixture to cover composition within the cap and omission of one guide
  when its required fields are missing. Cover failed ancestor reads,
  64/65-node chains, zero extent, invalid lens/range, overflow and
  precision collapse. Verify ordinary overlays with Guides Off or an
  ineligible selection.
- [ ] **R1/R3 — Picking and shared surface.** In the real Editor, cover an
  existing camera/light origin marker, a guide line across a separate
  Renderable's mesh hit and a guide-only miss. Confirm that the real
  authoring host refuses mixed-component edits atomically with
  `ComponentExclusivity`, leaving the revision and Apply receipt intact.
  Keep Gizmo priority and the projection-settlement guard during a camera
  switch. Hold a late mesh reply across a newer selection. In a two-client fixture, give
  each connection distinguishable geometry with the existing overlay call.
  Verify each client's own-overlay capture, an overlay-free default capture
  and independent replace, clear and disconnect. For GPU captures, the
  session that sends the guide segments must also request its own-overlay
  capture. Use the candidate composition in the test client and the real
  Editor for visible-window checks. Verify definite refusal fallback and
  the lost overlay reply separately.
- [ ] **R4 — Document versus projection.** Hold a required property
  reply, then change selection, toggle Off, resize away and back, reopen,
  detach or replace the selected row with the same GUID. Verify stale reads
  send no guide. Hold an already-sent overlay reply across a newer intent
  and verify its feedback is ignored and the latest overlay wins. Check
  lens, range/kind, ancestor motion and component removal. A refused
  property edit, abandoned draft or cancelled Gizmo preview cannot become
  verified guide input. Check a committed drag and its history.
  Hold Apply during Pause, then cause a deterministic reload failure with
  a disposable missing Runtime dependency and a committed guide edit.
  Keep the failure or suspended state while exercising Undo/Redo.
  Verify the guide follows each document state, the last good image
  remains, and no automatic Apply is sent in recovery. Restore the
  dependency and explicitly Retry the latest revision. Check that toggling
  adds no history, document revision or Apply receipt.
- [ ] **Windows 100% desktop.** In a disposable hand-authored scene, use
  0110 to create separate Camera, Point Light and Directional Light nodes,
  and use an independent Renderable for the pick check. Select each guide
  subject with Guides enabled, edit a lens or light field, exercise
  Undo/Redo, Save and reopen the same source. Verify the same authored
  guide after reselecting and that fresh windows default Off. Check standard and
  compact layouts at actual Windows 100% scaling, a resized hosted
  Viewport, marker and mesh click alignment, and Inspector focus/draft
  retention when toggling. Inspect a guide with a visible part and a
  part occluded by scene depth. Retain native captures of the three guides
  and the failed Apply state with both the guide source and Apply visible.
- [ ] **Gates.** Build and run affected Editor model, worker/UI and Runtime
  overlay checks. Record Vulkan Validation Layers enabled with no warnings
  or errors on the real visible and capture paths. In a deterministic
  fixture, verify default captures with Guides Off and On have identical
  decoded pixels at the same scene, camera and extent. Check
  the functional own-overlay images separately. Reuse unaffected golden
  evidence only with matching inputs. Run the selector
  `atlantis_runtime --gate --applies` on the complete implementation diff
  and follow the existing protocol if selected. Evaluate native and Android
  build applicability from the final diff. S1 needs document and link
  checks only.

## Rollback and definition of done

Revert the Editor toggle and guide composition. Saved scenes contain the
ordinary light/camera fields and need no migration. Follow the existing
[definition of done](../process/definition-of-done.md).
