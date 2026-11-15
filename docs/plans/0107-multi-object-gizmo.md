# Plan 0107 — Multi-Object Gizmo Transforms

- **Spec:** [Spec 0107](../specs/0107-multi-object-gizmo.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending approval of Spec/Plan 0107, acceptance of ADR-0163 and implementation authorization after manual design merge
- **Related ADR:** [ADR-0163](../adr/0163-guarded-transform-batches.md) (Proposed)

## Objective

Deliver one group transform action across the existing selection,
from Viewport drag through authoring settlement, Undo/Redo, Apply
recovery and persistence.

## Milestones

1. **S1 — Joint design review.** Resolve J1–J4, approve this Plan
   and its Spec, accept ADR-0163 and manually merge the design.
2. **S2 — Implementation.** Implement guarded admission and native
   and managed compatibility first. Build the Editor's group model
   from authoring snapshots and connect the existing input and
   overlay. Wire release, cancellation and replay through shared
   settlement. Use one existing Apply controller and local history.
3. **S3 — Verification.** Close the checklist on the final candidate.
   Cite the provenance and impact analysis for any reused evidence.
4. **S4 — Review-ready PR.** Link approval, exact revision, scope and
   gate evidence, including remaining human review items.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/connection/include/atlantis/connection/authoring_connection.h`, `src/connection/src/authoring_connection.cpp`, `src/authoring/src/authoring_session.cpp` | Declare the command and capability, admit the bounded shape and reuse candidate validation. |
| `src/remote/src/{codec,remote_client}.cpp`, `src/cli/src/{command_layer,invocation}.cpp` | Carry the optional descriptor member and guard through the existing transaction and refusal path. |
| `src/csharp/Atlantis.Gameplay/{Authoring.cs,Remote/AuthoringCodec.cs,Remote/AuthoringSession.cs}`, `src/mcp/Atlantis.Mcp/{ToolSchemas,ToolRegistry}.cs` | Typed capability, exact tool schema and guarded submission using the same calls. |
| `src/editor_app/Atlantis.Editor.Model/{Gizmo,GizmoDrag,GizmoOverlay,SceneMarkers,DocumentTree,AuthoringDocument,EditHistory,StructuralEdits,GroupGizmo}.cs` | One UI-free group drag model, authoritative frozen inputs, TRS solve, bounded guides and grouped history. `GroupGizmo.cs` is the proposed new model file. |
| `src/platform/src/windows/windows_platform.cpp` | Forward the embedded child's capture-loss notification over the existing message route. Preserve the native capture owner. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession,ViewportWindow}.cs` | Existing worker and input routing, group gesture lifecycle, controls, bounded overlay and transaction settlement. |
| `tests/authoring/authoring_session_tests.cpp`, `tests/remote/remote_authoring_tests.cpp`, `tests/cli/cli_scene_tests.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, `tests/csharp/Atlantis.Gameplay.Tests/{CodecSuite,LiveAuthoringSuite}.cs`, `src/mcp/tests/Atlantis.Mcp.Tests/M3Tests.cs` | Admission, conformance and client checks. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{GizmoDragTests,HierarchyTests,HistoryTests,ApplyOutcomeTests,IntegrationTest,J7Acceptance,Program}.cs` | Geometry, input, conflict, receipt and Windows acceptance. Use disposable scene sources. |
| `README.md`, `docs/architecture/remote_protocol.md`, `docs/specs/README.md` | Document shared invocation and closeout navigation. |

Runtime, World, Renderer, RenderGraph, asset cooking and scene formats
are existing integration inputs.

## Sequencing and dependencies

Design baseline is `f86ed402`, the [manual merge of PR #378](https://github.com/slmao/Atlantis/pull/378).
Start implementation from current main after S1. Reuse [0068](../specs/0068-authoring-editor.md)
Gizmo input, [0083](../specs/0083-authoring-apply-outcomes.md) tracked
Apply, [0093](../specs/0093-editor-visual-layout.md) Editor visuals,
[0103](../specs/0103-hierarchy-reparent.md) TRS conversion, and
[0106](../specs/0106-scene-multiselect-batch-edit.md) selection,
observation and guarded settlement.

| Decision | Recommendation |
| --- | --- |
| **J1 — Gizmo controls and math** | Approve the mean pivot over selected ancestry roots, World XYZ translate and rotate, and positive uniform scale. Preserve inherited local transforms and refuse an unrepresentable group sample. |
| **J2 — Preview and shared transaction** | Approve ADR-0163 and the existing overlay for destination feedback. Use the separately discovered Transform-only guard on the existing transaction. |
| **J3 — Input, settlement and recovery** | Approve capture-loss forwarding on the existing Windows input route, frozen placement context, last-moment selection checks, one conflict-aware Undo/Redo entry, and explicit Apply recovery. |
| **J4 — Acceptance and authorization** | Approve the Windows 100% end-to-end shared-client, overlay and persistence checks and applicable gates. After manual design merge, authorize S2–S4 together. |

Suggested authorization for the reviewed revision

> I approve J1–J4, Spec/Plan 0107 and ADR-0163. After the design PR is
> manually merged, proceed from latest main through S2–S4, including
> implementation, scoped fixes, applicable verification, valid evidence
> reuse and a review-ready PR. Keep Windows 100% acceptance and the
> applicable repository gates. I retain the final implementation merge.

## Verification checklist

- [ ] **R1–R2 — Group math and overlay.** Verify selected nodes
  sharing a parent and under different transformed parents. Cover a
  selected ancestor and descendant separated by an unselected node,
  plus a selected root without renderable geometry. Check pivot and
  root order, world axes and snapping, uniform positive scale,
  unchanged leaves and rotations about the pivot. Exercise an
  invertible nonuniform parent with valid translation and uniform
  scale, and include a rotation that introduces shear. Verify a small
  signed scale that would pass an absolute error floor despite a
  wrong basis direction. Cover singular parent and no-motion release.
  Confirm an invalid final sample refuses the whole group after a
  valid sample. Verify the origin and orientation guides, their
  bounds alongside the current selection, and retirement on refusal,
  cancellation or disconnect. A second client verifies unchanged
  World and authoring values during isolated preview and observes
  that its overlay survives group cleanup.
- [ ] **R3 — Guard and compatibility.** Exercise a valid mixed-leaf
  transaction and the 64-node / 192-write limit with a 193-ticket
  range. Cover the malformed and out-of-bound shapes including a
  mixed guard, stale revision, and normal validation failure on a
  later write. Verify an unchanged document, revision and event
  stream on refusal, and one revision with ordered events on success.
  Test CLI, C# SDK, MCP and Editor through the shared route. An actual
  0106 peer and the candidate read each other's descriptors and
  continue ordinary and 0106 transactions. Verify that missing,
  false or malformed capability suppresses sending, and that an
  old host rejects a raw unknown guard even after an otherwise
  valid write in the same request.
- [ ] **R3–R4 — Conflicts and settlement.** Cover selection changes
  during delayed capability read, A–B–A selection, and Escape during
  queued settlement. Cover unexpected capture loss before LeftUp and
  normal capture termination after LeftUp. Observe relevant ancestor
  changes including ABA, reparenting and same-GUID recreation. Cover
  a revision change after the last client read. Verify Undo/Redo
  stack positions for an untouched dependency changed externally,
  an unrelated edit, stacked Undo/Redo with overlapping values,
  and a known stale revision. Cover lost or malformed settlement, a
  verified edit with no Apply receipt, and a transaction with an older
  pending Apply. Verify publication before fallible refresh on
  disconnect and that late callbacks stay with the original gesture
  and document lifetime.
- [ ] **R1–R5 — Windows 100% actual Viewport workflow.** Start a
  disposable hand-authored scene with two effective roots under
  different parents and a selected descendant of one root. Use the
  actual embedded Viewport handles to translate, rotate and scale.
  Check guides versus applied geometry and rejection of a transform
  that shears under its parent. Use real pointer input to compare an
  in-bounds release, an outside release, and stolen capture before
  mouse-up while the Editor remains active. Check Escape from the
  embedded Viewport and the Editor. Canceled drags leave submission
  and history unchanged.
  For each accepted drag observe one revision and one history
  entry, verify the expected world placement and local values, and
  confirm the descendant inherits the change exactly once. Undo and
  Redo an edit that changed multiple leaves. Force a recoverable Apply
  failure, inspect the edited document and last good projection, then
  explicitly Apply the current document. Verify no duplicate transform
  or history entry. Save and reopen the same source on that generation,
  confirm transient state clears, and read the saved locals, ancestry
  and asset references by GUID before explicitly applying. Then check
  resulting world placements against the accepted values. Check a
  single-node Gizmo still follows its existing workflow.
- [ ] **Existing gates.** Build and test affected native and managed
  targets and run the relevant Editor/Runtime integration with Vulkan
  Validation Layers. The Windows Editor is the feature endpoint.
  Native Authoring/Connection changes retain applicable Android
  build and packaging checks. Keep applicable golden checks and run
  `atlantis_runtime --gate --applies` for the complete final diff.
  If selected, follow the current performance protocol and baseline.

## Rollback Plan

Revert the implementation PR as a unit. Already saved local transforms
remain readable as ordinary scene source. Earlier peers recover their
existing capability and UI behavior.

## Definition of Done

Follow the [repository Definition of Done](../process/definition-of-done.md).
The implementation PR records the reviewed scope and final candidate
evidence for the checklist. The maintainer performs the final merge.
