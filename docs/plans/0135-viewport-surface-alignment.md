# Plan 0135 — Precise Viewport Surface Alignment

- **Spec:** [Spec 0135](../specs/0135-viewport-surface-alignment.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Maintainer joint approval of Spec/Plan and J1–J4 at `d9ac83c1`, including the J3 race. [Approval record](https://github.com/slmao/Atlantis/pull/433#issuecomment-7074129488). Design merged by the maintainer as `e374506b`.
- **ADR:** [ADR-0184](../adr/0184-shared-alignment-face-query.md) (Accepted)
- **Design baseline:** `931efd5e`, maintainer merge of [#432](https://github.com/slmao/Atlantis/pull/432)

## Objective

Use two verified rectangular end caps to move one wall into a repeatable
seam or gap, through one guarded position edit and ordinary Undo and Save.

## Milestones and sequencing

1. **S1 — Joint review.** Decide J1–J4 on the exact Spec, Plan and ADR
   revision. Begin implementation from current `main` after maintainer
   merge of the approved design.
2. **S2 — Geometry and shared read.** Prove eligible rectangular caps
   from retained geometry. Keep the ordinary pick result and tie behavior.
   Add the bounded shared face query and verify native and managed codecs,
   CLI and MCP before using it in the Editor.
3. **S3 — Editor operation.** Add the ordered face picks, Gap and overlay
   preview through the existing Viewport input and Worker. Implement the
   widened solve and check the actual Float32 result. Reuse guarded
   settlement, one history entry, correlated Apply and explicit recovery.
4. **S4 — Verification and PR.** Verify final-candidate Windows 100%
   Bistro use, refusal cases, save/reopen, compatibility and applicable
   gates. Publish the evidence and open a review-ready implementation PR.

## Expected files and modules

| Area | Changes |
| --- | --- |
| `src/runtime/{include/atlantis/runtime,src}` — `runtime_application`, `runtime_control_host`, `scene_picking`, `scene_load` and focused Runtime tests | Obtain first hits and cap geometry in the existing control dispatch. Carry the proven mesh identity with retained data. Verify no change to ordinary picking. |
| `src/connection/include/atlantis/connection/runtime_control.h`, `src/remote/src`, shared native RuntimeControl callers, `src/cli/src`, `src/csharp/Atlantis.Gameplay`, `src/mcp/Atlantis.Mcp` and their contract suites | Add the shared query and closed errors, validate bounds and older hosts. Update Remote protocol and applicable as-built architecture notes in the implementation PR. |
| `src/editor_app/Atlantis.Editor/{MainWindow*,EditorSession.cs}`, `src/editor_app/Atlantis.Editor.Model` and corresponding Editor tests | Alignment intent and solver, ordinary forwarded input, bounded overlay, existing guarded transform history and Apply integration. |
| Existing saved-scene and glTF asset fixture paths used for acceptance | Disposable Bistro fixture with two opaque, seam-capable static wall sections. No shipping scene or renderer change. |

Keep the existing IDs and encodings of all authoring verbs. Reuse the
existing Windows forwarding without adding native input code. Record the
concrete implementation and test file list at S2 before edits.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Geometry and transform** | Support two-triangle rectangular caps whose world corners agree after translation. Preserve authored rotation and scale, allow an offset pivot or a supported parent, reject nonopposed, ambiguous and unrepresentable results. Use a non-negative gap along the target normal. |
| **J2 — Shared face evidence** | Accept ADR-0184's one- or two-ray read with checked opaque first-hit evidence and a bounded reply. Keep ordinary `raycast`, source formats and existing client behavior. |
| **J3 — Preview, concurrency and recovery** | Require a confirmed current projection, allow navigation between picks, use a client overlay, compare both faces again and send one revision-guarded position write. Accept the explicit Runtime-only race boundary. Undo/Redo replay poses and Apply settles independently. |
| **J4 — One implementation authorization** | After joint approval and maintainer merge of the design, authorize S2–S4 once, including in-scope fixes, affected checks, Windows 100% acceptance, applicable gates and submission of a review-ready PR. Final merge remains with the maintainer. |

## Verification checklist

- [ ] **Face evidence (R1–R2).** Use tilted or rotated rectangular
  fixtures, split vertex indices at identical positions, both diagonals,
  and a parented case. Test internal-diagonal ties, exterior-edge picks,
  first hit on another or unsupported object, nonopaque meshes, backface,
  degenerate and extra coplanar geometry, reflected or sheared transforms,
  missing retained data, the exact and exceeded triangle bound, and
  insufficient numeric precision. Ordinary `raycast` and normal
  pick/Gizmo behavior retain their prior results.
- [ ] **Shared clients (R2, ADR-0184).** Verify exact shapes for one and
  two rays, ordered success and no partial or oversized reply, first error
  index, bad rays and types, finite values, canonical corners and identities,
  and paired sampling across scene swaps. Compare native, CLI, C# SDK and
  MCP results. Exercise a new client on an old host and an old client on
  the new host. Keep refusal, unknown method, transport loss and malformed
  data distinct. A known unsupported read leaves ordinary picking usable.
- [ ] **Solver and settlement (R1, R3–R4).** Check zero and positive gap,
  differing tessellation diagonal, non-origin pivot, parent-space solve,
  bitwise unchanged Euler and scale leaves, matched-corner residual,
  Float32 round-trip and no-op. Refuse noncongruent or misoriented caps,
  negative or nonfinite gaps and unrepresentable results. Delay replies
  across navigation, cancel, resize and document/selection changes. Swap
  the target for the same GUID, move or rebind it from a peer, change the
  observed Runtime generation, and inject an authoring race at admission.
  Verify no write/history/Apply from each pre-send refusal. Cover lost
  transaction and Apply replies, one verified history entry, Undo/Redo
  conflict, and explicit Apply of a verified edit after a failed reload.
  Verify overlay rejection and cleanup. A second client's independent
  capture and overlay remain isolated as defined by ADR-0132.
- [ ] **Windows 100% — Bistro storefront (R1–R4).** Use a disposable
  hand-authored Bistro scene and two opaque static wall sections with
  equal end caps, different pivots, initially separated laterally and
  along the seam normal. Use actual forwarded Viewport clicks to pick
  each cap, including reorienting the camera between picks. Capture the
  two outlines, gap readout, and the actual frame after Apply.
  1. At **0.000** world units, confirm once. Read back both world poses
     and the moved node's local Transform through existing shared
     queries. Identify the four test corners from fixture geometry
     independent of the alignment result. At each matched corner, require
     3D residual to the fixed cap ≤ `ε` from R3.
  2. Undo once to the exact original local position. Redo restores the
     recorded aligned position. Undo again, then explicitly align with
     a **0.250** world-unit gap. Require signed distance along the fixed
     outward normal of `0.250 ± ε`, tangential residual ≤ `ε` and 3D
     residual to each offset target corner ≤ `ε`. Verify unchanged Euler,
     scale, parent and mesh/material references in both successful cases.
  3. Cancel a valid preview. In a separate attempt have a peer move
     or replace the target after the preview and try to confirm. Record
     the refusal and unchanged moving-node position, history and Apply
     activity relative to the state before the use of Confirm. Attribute
     any peer edit separately. An expired preview follows no new hit.
  4. Save the spaced placement, reopen the same source in the same
     asset generation, and explicitly Apply. Verify exact saved local
     position and unchanged fields by GUID, then recheck the same world
     corner criteria. Preview data is absent.
- [ ] **Applicable gates.** Run the affected Windows Runtime, Remote,
  SDK, CLI, Editor and MCP checks. Use Vulkan validation on the real
  overlay and placement path and run rendering-adjacent headless checks.
  Evaluate image regression, golden review and Android applicability
  against the final diff. Query `--gate --applies` and retain the formal
  result when applicable under the current repository protocol. Record
  exact revision, configuration and permitted evidence reuse.

## Rollback and definition of done

Retire the additive read and alignment UI. A committed result is an
ordinary Transform position already readable by previous clients and
persisted in the same scene format. Apply the repository's
[Definition of Done](../process/definition-of-done.md) to the
implementation, including traceable raw Windows evidence.
