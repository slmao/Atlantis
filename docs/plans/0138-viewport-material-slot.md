# Plan 0138 — Locate and Edit a Material Binding from the Viewport

- **Spec:** [Spec 0138](../specs/0138-viewport-material-slot.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending
- **ADR:** [ADR-0186](../adr/0186-shared-material-binding-pick.md) (Proposed)
- **Design baseline:** `66bfd0ba`, maintainer merge of [PR #438](https://github.com/slmao/Atlantis/pull/438)

## Objective

Connect an unambiguous Bistro surface to the existing authoritative
material slot and make the choice between shared asset and single binding
visible at every write boundary.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Decide J1–J4 on the exact Spec/Plan/ADR revision.
   Begin S2 from current `main` after maintainer merge.
2. **S2 — Shared read.** Implement the Runtime query and bounded geometry
   resolution in ADR-0186. Connect C++, Remote, CLI, C# and MCP typed paths.
   Verify historic clients and the unchanged ordinary pick path.
3. **S3 — Editor.** Wire one-shot locate to the Inspector. Build the bounded
   known-use read, stale observation guard and guided handoff to Edit,
   Duplicate and the typed replacement chooser. Use the existing
   publication, guarded authoring transaction, history and Apply settlement.
4. **S4 — Verify and submit.** Complete targeted checks, Windows 100% Bistro
   acceptance, applicable gates and a review-ready implementation PR.

## Files / Modules Touched (expected)

| Area | Scope |
| --- | --- |
| `src/connection/include/atlantis/connection/runtime_control.h`, `src/runtime/src/scene_picking.{h,cpp}`, `src/runtime/src/runtime_application.cpp`, Runtime application/control-host headers and adapter, `src/runtime/main.cpp` and `tests/runtime` | Typed read, dispatch and bounded resolution against existing CPU geometry and active catalog. Test ownership, result attribution, ambiguity and nonparticipation in ordinary paths. |
| `src/remote`, `src/cli`, `src/csharp/Atlantis.Gameplay`, `src/mcp` and their corresponding tests and conformance inputs, `docs/architecture/remote_protocol.md` | Additive request and exact result/refusal shape. MCP forwards through the C# SDK. Legacy rays, queries and mutations stay compatible. |
| `src/editor_app/Atlantis.Editor/MainWindow.cs`, `EditorSession.cs`, `MaterialEditorWindow.cs`, Inspector and reference-picker code under `src/editor_app/Atlantis.Editor.Model`, corresponding Editor tests | Review, launch intent, known uses, clipboard handoff, exact binding guard, existing Edit/Copy, replacement, history and recovery integration. No renderer changes. |
| Disposable Bistro fixture and retained evidence | Two actual awning fabric nodes share an eligible material. Keep pinned Bistro geometry and 0131 view. Do not change shipped asset sources. |

## Sequencing & Dependencies

Use [0135](../specs/0135-viewport-surface-alignment.md) geometry traversal and
[0136](../specs/0136-viewport-visibility-picking.md) hosted revision/presentation
evidence before enabling Editor actions. Reuse
[0094](../specs/0094-basic-material-authoring.md) asset jobs and
[0090](../specs/0090-saved-scene-build-open.md) for the explicit independent
saved-scene Build/Open proof. Use existing
[0106](../specs/0106-scene-multiselect-batch-edit.md) guarded property
assignment and [0083](../specs/0083-authoring-apply-outcomes.md) Apply
settlement. Link the exact implementation file list and any narrow
adjustments in the implementation PR.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Surface and slot** | Accept ADR-0186's opt-in one-ray query with clipped distance and a bounded near-tie check. V1 resolves an explicit opaque PbrDirectLit binding in the default hosted view. The slot is the owning Renderable's existing material field. |
| **J2 — Evidence and sharing** | Bind panel validity to the current Worker observations, the settled projection, actual node occurrence and explicit authoring reference. Show bounded direct uses in the open document with completeness. Require an explicit shared edit or duplicate choice. |
| **J3 — Publication and replacement** | Keep asset publication independent. After activation, require a fresh hit and review note identity checks before an explicit revision-guarded replacement. Refuse changed expectations, preserve prior work on partial failure, and keep one history entry for only the slot commit. |
| **J4 — One implementation authorization** | After joint approval and maintainer merge, authorize S2–S4 once, covering implementation, in-scope repairs, applicable verification and a review-ready PR. Final merge remains with the maintainer. |

## Verification Checklist

- [ ] **R1 — Query and compatibility.** Check unique hits including a fabric
  edge inside one Renderable, fabric versus metal child, no geometry/miss,
  separate near-equal entities even with the same material, clipped hits,
  fallback, masked/blended and other unsupported front blockers, deterministic
  bound and malformed request. Verify old callers see the prior response
  and newer callers report an unsupported older host. Confirm hosted
  policy/revision changes fail rather than pick through a blocker. Verify
  CLI, SDK and MCP expose the same meaning.
- [ ] **R2–R4 — Read and mutation guards.** Check complete and partial
  sharing, same labels/different GUIDs, pending or failed projection,
  same-GUID replacements, external rebind, selection/view/extent changes,
  and a Worker-observed change before a delayed result reaches the UI.
  Keep the current selection and draft on refusal. Check model cases for
  loss of material authority, copy publication failure and Resume, lost
  asset or authoring receipt, and no automatic or duplicate mutation.
  Verify note validation across restart, including changed source slot,
  incorrect scene or mesh and missing/unactivated destination. Refusal
  before a write produces zero document edits, history and Apply. A
  previously published copy remains available. Test failed Apply and
  explicit recovery, and Undo/Redo conflict when the field was changed by
  another client. Capture the preflight-to-commit revision conflict on the
  same authoring transaction path.
- [ ] **Windows 100% — Hosted Bistro storefront.** Record physical DPI,
  candidate, pinned asset inputs, scene/mesh/material GUIDs, fixture and
  fixed click pixels. Use [0131 Storefront Terrace](../specs/0131-bistro-reference-views.md)
  at 960 × 540 physical pixels with lighting and EV held fixed. Use a
  disposable copy of the scene and only its approved workspace authority.
  1. Establish two visible, disjoint fabric Renderables with the same
     eligible managed material, using real Bistro meshes. Confirm that
     clicking the awning fabric reveals the exact child, mesh, field and
     shared GUID. Verify the second node appears as a known direct use.
     Capture baseline PNG and raw frame report. Show the shared edit
     confirmation in the real window and Cancel. Verify the source,
     binding, history and revision are unchanged by locate and cancel.
     Use a separate disposable asset case to exercise the existing shared
     edit path and confirm that it edits the shared asset without rebinding
     either node.
  2. Choose Duplicate for this binding and change one existing base-color
     parameter to a distinguishable value. Confirm publication produces
     one new GUID and changes neither binding in the old session. Retain
     the review note and activate the published generation explicitly.
     Freshly locate the original awning and confirm the one replacement.
     Check both GUIDs before and after Apply and record the confirmed
     Apply ID, document revision and Runtime generation.
  3. Take the after PNG and report at the same fixed view. Check both
     matrices within 0.0001 per element, the three reference landmarks
     within one pixel, a predeclared nonsaturated awning patch visibly
     changes through a measurable luminance/color difference above the
     repeated baseline variation, and a disjoint control patch stays
     within that variation. Confirm the control node retains the original
     GUID and the source material retains its recipe. Save native
     screenshots of the verified slot and both operation choices.
  4. Use one Undo and Redo to restore and reapply the one reference and
     verify the copy remains registered. Save and reopen the scene, then
     explicitly Build/Open the same source in an independent 0090 Editor.
     Verify both bindings and the preview again with the same committed
     managed inputs. A deliberately stale note or invalidated binding must
     neither rebind nor create an Undo entry.
- [ ] **Applicable gates.** Run affected Editor, C++/Remote, CLI, C# SDK,
  MCP and Runtime checks. Keep Vulkan validation clean in native hosted
  and capture runs. Evaluate headless/golden and Android build/link/package
  applicability for the final shared Runtime change. Run the repository
  `--gate --applies` selector and the existing performance protocol if
  selected. Preserve raw evidence and existing goldens and baseline.

## Rollback Plan

Revert the additive read and Editor entry point. Existing material
publications and ordinary authored bindings retain their existing formats
and can be opened independently.

## Definition of Done

Use the [repository definition of done](../process/definition-of-done.md).
Record approval and the accepted ADR, both binding identities, before/after
and Undo evidence, and the actual gate results in the implementation PR.
