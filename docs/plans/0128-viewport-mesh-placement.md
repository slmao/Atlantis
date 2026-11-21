# Plan 0128 — Place One Static Mesh on a Viewport Surface

- **Spec:** [Spec 0128](../specs/0128-viewport-mesh-placement.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** 2026-11-21, [Spec/Plan 0128, J1–J4 and S2–S4 approved](https://github.com/slmao/Atlantis/pull/420#issuecomment-7060032916) at `cfa48c79`. Maintainer merge `a7d0004b`. Windows native routing is the S2 prerequisite.
- **ADR:** [ADR-0177](../adr/0177-editor-viewport-placement-boundary.md) (Accepted)

## Objective and baseline

Land one Editor-local drag-to-surface gesture using the shared Runtime and
authoring contracts. The review baseline is `3e867e80`, the maintainer
merge of [PR #419](https://github.com/slmao/Atlantis/pull/419).

## Milestones and dependencies

1. **S1 — Joint review.** Resolve J1–J4 and the proposed ADR. Begin
   implementation from latest main after explicit approval and the
   maintainer's design merge.
2. **S2 — Native routing and preview.** Prove hover, leave and Drop over
   the real hosted child on Windows 100%, using the existing 0115 source
   and ADR-0177 destination. A route proof may record events without
   mutating a scene. Verify physical-coordinate mapping and retention of
   old targets, then wire bounded worker samples, candidate expiry and
   overlay composition. Raise native-route incompatibility as a concrete
   design decision if the proposed route fails.
3. **S3 — Create and settle.** Extend the existing mesh planning and
   structural verification to accept a position while retaining the two
   existing origin paths. Add guarded release and revalidation, preserve the
   existing receipt, history, Apply and recovery ordering, and run the
   focused regressions and Bistro acceptance below.
4. **S4 — Final verification and PR.** Complete applicable gates on the
   final candidate, verify the scoped diff and submit a review-ready
   implementation PR with evidence. Reuse matching evidence and keep final
   merge manual.

## Expected files and modules

| Files | Scope |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/{AssetDragDrop,HierarchyCommands}.cs`, `ViewportMeshPlacement.cs` (new in the same directory) | Reuse drag identity and phase. Add UI-free placement math and candidate checks, and teach existing creation/history about an explicit position. |
| `src/editor_app/Atlantis.Editor/{MainWindow,ViewportWindow,EditorSession}.cs` | Route native drag coordinates and UI feedback, coalesce placement queries, compose the existing overlay and settle the shared add path. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{ViewportMeshPlacementTests,ViewportMeshPlacementUiTests}.cs` (new), `{Program,InspectorTests}.cs` in the same directory | Register geometry, worker sequencing and native Windows checks using the existing hosted test entry. |
| Spec 0128, Plan 0128, ADR-0177 metadata and `docs/specs/README.md` | Approval and navigation. |

No checked-in asset, scripted platform hook, package, SDK, native engine,
build or CI change is expected. Use disposable local acceptance data.

## Joint review recommendations

| Decision | Recommendation |
| --- | --- |
| **J1 — Source and input** | Approve the existing 0115 window-local Mesh source and native drop delivery to the real Viewport drawable. Retain the existing root zone and Inspector destinations, with ADR-0177's Windows routing proof (R1). |
| **J2 — Placement and expiry** | Approve one pivot cross at the nearest mesh hit, local origin to hit, identity rotation and unit scale. Require a ready preview for the release pixel and a fresh matching raycast before sending. Accept the observed concurrency limit (R2–R3). |
| **J3 — Mutation and recovery** | Approve one ordinary atomic root creation and one structural history entry, keeping source material fallback and explicit Apply recovery for failed or uncertain projection. Preserve the edit and recovery behavior across UI changes after send (R4). |
| **J4 — Acceptance and authority** | Approve the Windows 100% Bistro chair fixture, focused negative cases and applicable gates. Authorize S2–S4 in one decision after the maintainer merges the design. |

Suggested authorization:

> Jointly approve this revision of Spec/Plan 0128 and J1–J4, and accept
> ADR-0177. After maintainer merge, run S2–S4 from latest main, including
> in-scope fixes, applicable verification and a review-ready PR. Keep
> Windows 100% native drag, the Bistro Save/reopen scenario and all
> applicable gates. Final merge remains with a maintainer.

## Verification checklist

- [ ] **R1–R3, deterministic.** Cover known pixel/extent and camera-to-ray
  cases, clip and finite-value guards, miss and `NoGeometry`, overlay
  rejection, one outstanding ray, bounded coalescing, reordered responses
  and clear while an answer is in flight. Test leave/return, Escape,
  capture loss, native completion after a queued drop, replay, and source
  token/catalog changes. Delay the final recheck and change the camera,
  extent, document revision/lifetime, generation, hit GUID or same-GUID
  incarnation/distance. Confirm refusal produces zero scene transactions,
  history and Apply. Rerun affected 0115 and normal click-pick/Gizmo checks.
- [ ] **R4, deterministic.** Verify all three commands in one ticket
  group, finite translation, exact root/mesh/transform in history and a
  single undo/redo, including a conflicting external edit. Cover refused
  transaction, lost transaction receipt, unknown/failed Apply, selection
  change after send and explicit Apply without duplicate creation. Keep
  origin placement and existing Inspector assignment semantics unchanged.
- [ ] **R1–R4, Windows 100% — Bistro.** In a disposable trusted checkout,
  use the content-gated Bistro inputs from [0046](../specs/0046-bistro-finale.md).
  Prepare a hand-authored Saved Scenes unit from the generated Bistro
  source with a fresh scene asset GUID/catalog entry. Retain the terrace
  geometry, transforms and asset references. Use a single static mesh
  representing a whole chair, upright in engine space with the origin
  at its feet. Reuse a suitable catalog mesh or prepare one whole-chair
  glTF and register it through [0086](../specs/0086-new-asset-import.md)
  locally. Some Bistro chairs are
  multi-node assemblies. Verify the fixture's silhouette and pivot before
  testing, without changing the feature's one-mesh rule or the upstream
  Bistro source/catalog. Record the scene, floor and chair GUIDs, asset
  generation if selected, and fixture provenance.
  1. Build and Open the authored copy through [0090](../specs/0090-saved-scene-build-open.md).
     In the embedded Runtime view, use a physical pointer drag from the
     visible Assets chair row to expose an unoccluded terrace floor point.
     Check the mark follows the point and the selected document/node count
     stays unchanged before release. Independently identify the floor hit
     and distance through the shared ray query. Keep a native screenshot
     of the visible mark.
  2. Release at that point. Confirm one new root GUID, the chair mesh
     binding and pivot equal to the shown hit, and a chair visibly on the
     terrace after Apply. Undo once and confirm removal, then Redo once
     and confirm the same GUID, binding and position. Save and reopen the
     same authored scene, explicitly Apply when needed, and verify the
     persisted scene and chair. Keep native post-Apply and reopen captures.
  3. Use native Escape and release over sky/empty space to confirm no
     edit and cleanup. During a held drag, resize or change the observed
     projection after a valid preview and release before a fresh marker.
     Confirm no edit, no history and no Apply. Use an existing 0115 target
     and normal Viewport picking afterward to verify input recovery.
- [ ] **Applicable gates.** Build and run affected Editor suites. Verify
  the hosted path with clean Vulkan validation and existing per-client
  capture isolation. The placement mark is transient and confined to the
  existing overlay. Assess headless/golden and platform coverage from
  final inputs and assess performance using `atlantis_runtime --gate --applies`.
  Record the applicability and retain every applicable gate unchanged.

## Rollback

Revert the Editor placement route. Scenes already saved through the feature
contain ordinary renderables readable by existing clients. Follow the
[Definition of Done](../process/definition-of-done.md).
