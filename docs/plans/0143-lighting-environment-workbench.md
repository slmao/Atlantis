# Plan 0143 — Lighting and Environment Workbench

- **Spec:** [Spec 0143](../specs/0143-lighting-environment-workbench.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending. S2–S4 require explicit authorization and maintainer merge of this design.
- **ADR:** None proposed
- **Design baseline:** `b27d93fe` on `main`

## Objective

Complete the first slice of 3D Scene and Render Effects Editor V1. Select
and edit the lights of a saved Bistro copy and verify the daylight pass in
the two existing reference views and on an independent cold start.

## Milestones / task breakdown

1. **S1 — Joint review.** Settle J1–J4, then record the reviewed revision
   and the single implementation authorization. The maintainer merges the
   design before implementation begins.
2. **S2 — Authoring-backed light selection.** Extend the current Hierarchy
   snapshot/filter model for the two existing Light kinds. Use the Worker's
   schema and synchronized document tree to build ordered matches. Verify
   completeness, current row incarnations and intent before consuming the
   selection. Use existing authoring and observational paths for tests.
3. **S3 — Editor affordance.** Add the kind filter, match count, explicit
   selection action and launch-owned environment note to the existing
   Hierarchy/Inspector workflow. Integrate the existing focus, draft, drag,
   selection, inspector and feedback paths. Leave the capture window,
   Saved Scenes and edit settlement code paths as they are.
4. **S4 — Verification and PR.** Follow the Windows sequence below, run the
   affected Editor tests and applicable gates, and submit a reviewable PR.
   Link each reused result to its revision, inputs and applicability.

## Files / modules touched (expected)

| Files | Scope |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/HierarchyFilter.cs`, `SceneMarkers.cs` | Light metadata in the existing snapshot and filtering, matching and stale-selection model. |
| `src/editor_app/Atlantis.Editor/EditorSession.cs`, `MainWindow.cs` | Worker snapshot/intent check and narrow Hierarchy/Inspector controls and feedback. |
| `src/editor_app/tests/Atlantis.Editor.Tests/HierarchyFilterTests.cs`, `Program.cs` | Targeted filtering, stale intent and existing editing regression coverage. |
| Disposable Windows acceptance scene and evidence outside the implementation diff | 0131 hand-authored fixture with pinned Bistro asset provenance, fresh day Save As, PNGs, raw reports and Windows screenshots. |

The staging sequence is S2 before S3, then S4 on the final candidate.
Reuse [0113](../specs/0113-hierarchy-search-and-filter.md),
[0106](../specs/0106-scene-multiselect-batch-edit.md),
[0131](../specs/0131-bistro-reference-views.md) and
[0137](../specs/0137-bistro-light-exposure-tuning.md). No Runtime, asset
pipeline, CMake, IPC or SDK implementation files are planned.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Scene preparation and preservation** | Reuse the pinned 0131 hand-authored fixture procedure, then use Editor Save As and explicit Saved Scenes Build/Open to make the daylight copy. Preserve both the built-in night and the editable baseline source. Track fixture setup as a prerequisite and exclude an imported-scene Save As feature. |
| **J2 — Workbench and environment boundary** | Add light-kind selection and the launch-environment explanation to the existing editor panels. Reuse the Inspector and leave environment editing for a later design that owns the scene data and Runtime resource lifecycle. Confirm no new ADR for this scope. |
| **J3 — State and recovery** | Keep existing guarded transactions, per-edit Undo/Redo, correlated Apply and independent Save. Selection-only work and reference view switching produce zero edits. Failed or unknown projection results cannot be used for the comparison. |
| **J4 — Evidence and authorization** | Require the Windows 100% controlled before/after test at both 0131 views and same-source independent Build/Open. Upon joint approval and maintainer merge, authorize S2–S4, scoped fixes, applicable verification and submission of the implementation PR in one decision. Reserve final merge for a maintainer. |

## Verification checklist

- [ ] **R2–R3 — Focused Editor verification.** Cover existing name/component
  filters and scroll/selection semantics, kind and name composition, actual
  hit count/exclusion of ancestor-only rows, unnamed nodes, no match,
  incomplete evidence, active draft and drag protection, a replaced node
  carrying the old GUID and a late response after filter/selection or document
  changes. Verify selection emits no transaction, Apply or history entry.
  Exercise the existing mixed-value batch, history/conflict and Apply
  settlement suites, including failure or unknown outcome followed by
  explicit Apply of a settled edit. Run affected Editor/model build and tests.
- [ ] **R1 — Windows 100%, fixture and protection.** Record main/candidate,
  driver, Bistro input pin, source IDs/hashes and effective launch
  configuration. Use the [0131 fixture procedure](0131-bistro-reference-views.md#verification-checklist)
  in a trusted checkout. Keep the night overlay values and the two calibrated
  views. Use Editor Save As, Build and Open in separate processes. Compare
  both sides of each experiment in the day copy's ordinary `saved_scene`
  configuration. Record the built-in night's no-IBL configuration separately.
  If the fixture or its references cannot be built and opened with normal
  Saved Scenes, report the failure rather than substituting an ad hoc launch.
- [ ] **R2–R4 — Windows 100%, lighting-only comparison.** With the day copy
  successfully applied, capture the before at Street Corner and Storefront
  Terrace. Preselect three 0131 landmarks per view and a lit surface without
  clipping for a sample of the observed lighting difference. Confirm initial
  `0 EV` from the fixture camera and actual reported EV on all corresponding
  frames. Select the directional light and adjust its aim, color and intensity
  in Inspector. Capture the directional-only state at both views. Then select
  multiple visible Point matches with mixed intensities, confirm the target
  count and set their intensity to zero in one batch to suppress the night
  lights. Capture the final state at both views. Check shared frame reports
  against the authored directional values and point-light world position,
  color, range and intensity. Verify the directional-only edit keeps point
  values constant and the point batch keeps directional values constant. Hold
  camera, fog/bloom, environment, geometry and materials fixed across stages.
  Keep captures at 960 × 540, view/projection matrices within `0.0001` per
  element and corresponding landmarks within one pixel. Confirm the sample
  shows a lighting response while using the reports as the parameter evidence.
  Retain raw PNGs, reports and a native Editor screenshot of the selection
  and Inspector. Bracket ordinary 0131 captures with existing document/Apply
  and Runtime status reads to record the exact settled revision, Apply ID,
  Runtime generation and frame. Explicitly Apply the unchanged opening
  document if an initial projection receipt is needed. A previous-projection
  capture, changed framing, missing EV or unverified result is excluded.
- [ ] **R3–R4 — Undo and persistence.** Undo the point batch once and confirm
  each original point intensity returns while the directional changes stay.
  Capture restoration in both views. Redo and verify again, Save from Editor,
  close the copy, then independently Build/Open its saved source using 0090.
  Verify scene GUID, effective camera/EV, unchanged bindings, persisted light
  values and both fixed views in the new Runtime. Recheck the original night
  inputs and editable baseline source byte-for-byte. Keep ORCA context labeled
  per 0131 and report image observations without a visual-parity threshold.
- [ ] **Gates.** Keep Vulkan validation enabled for the Windows run. Run
  the checked-in performance applicability rule on the final diff and carry
  out any selected gate under the existing protocol. Record each applicable
  image check and its result. Production rendering and committed goldens are
  expected to stay unchanged, with the authored daylight screenshots serving
  as acceptance evidence.

## Rollback and definition of done

An ordinary revert removes the Editor affordance. The night original and day
copy remain standard scene files with existing light data. See the repository
[definition of done](../process/definition-of-done.md). S4 records results and
remaining limitations against R1–R4 before asking for implementation review.
