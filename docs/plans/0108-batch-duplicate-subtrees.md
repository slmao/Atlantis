# Plan 0108 — Duplicate Selected Scene Subtrees

- **Spec:** [Spec 0108](../specs/0108-batch-duplicate-subtrees.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending J1–J4 and implementation authorization
- **Related ADR:** [ADR-0164](../adr/0164-guarded-duplicate-transactions.md) (Proposed)

## Objective

Give one Hierarchy selection one persistent duplicate action, one
authoring commit, and one conflict-aware Undo/Redo entry.

## Milestones

1. **S1 — Joint design review.** Resolve J1–J4, approve this Plan
   and its Spec, accept ADR-0164 and manually merge the design.
2. **S2 — Implementation.** Add the bounded shared guard and client
   compatibility first. Reuse the complete authoring reads, ordered
   structural commands and naming from 0102. Wire the multi-selection
   action, guarded history and independent tracked Apply settlement
   into the existing Editor worker and controls.
3. **S3 — Verification.** Close the checklist on the final candidate,
   recording revision, configuration and provenance for reused evidence.
4. **S4 — Review-ready PR.** Link joint approval, final revision,
   file scope and applicable gate results for human review and merge.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/connection/include/atlantis/connection/authoring_connection.h`, `src/connection/src/authoring_connection.cpp`, `src/authoring/src/authoring_session.cpp` | Declare and admit the two guarded shapes, using the existing candidate and final validation. |
| `src/remote/src/{codec,remote_client}.cpp`, `src/cli/src/{command_layer,invocation}.cpp` | Carry the additive command, discovery and refusals over the existing native and CLI surface. |
| `src/csharp/Atlantis.Gameplay/{Authoring.cs,Remote/AuthoringCodec.cs,Remote/AuthoringSession.cs}`, `src/mcp/Atlantis.Mcp/{ToolSchemas,ToolRegistry}.cs` | Typed command, optional capability and the existing exact MCP transaction tool. |
| `src/editor_app/Atlantis.Editor.Model/{HierarchyCommands,StructuralEdits,DocumentTree,AuthoringDocument,EditHistory}.cs` | Derive closure, read and plan the batch, and retain one complete and guarded replay record. Reuse existing node lifetime tracking and positional history behavior. |
| `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` | Hierarchy action, captured selection and result publication using existing worker, Apply and visual controls. |
| `tests/authoring/authoring_session_tests.cpp`, `tests/remote/remote_authoring_tests.cpp`, `tests/cli/cli_scene_tests.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, `tests/csharp/Atlantis.Gameplay.Tests/{CodecSuite,LiveAuthoringSuite}.cs`, `src/mcp/tests/Atlantis.Mcp.Tests/M3Tests.cs` | Admission, native and managed codec conformance, and shared client checks. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{HierarchyTests,HistoryTests,ApplyOutcomeTests,IntegrationTest,J7Acceptance,Program}.cs` | Batch model, replay conflicts, receipt settlement and Windows acceptance on disposable source. |
| `README.md`, `docs/architecture/remote_protocol.md`, `docs/specs/README.md` | Document shared invocation and closeout navigation. |

Runtime projection, World, Renderer, RenderGraph, asset cooking and
scene formats are existing integration inputs.

## Sequencing and dependencies

Design baseline is `edcd46c1`, the [manual merge of PR #380](https://github.com/slmao/Atlantis/pull/380).
Begin S2 from current main after the approved design is merged.
Reuse [0102](../specs/0102-duplicate-scene-subtree.md) structural copy,
[0106](../specs/0106-scene-multiselect-batch-edit.md) selection and
result correlation, [0107](../specs/0107-multi-object-gizmo.md)
settlement ordering, [0083](../specs/0083-authoring-apply-outcomes.md)
tracked Apply and [0093](../specs/0093-editor-visual-layout.md) Editor
visuals.

| Decision | Recommendation |
| --- | --- |
| **J1 — Selection and copy layout** | Approve one batch for 2–64 selected rows, deduplicate by ancestry, and cap the complete union at 64 nodes. Copy fresh GUIDs, same local placement and one appended block in global source order. |
| **J2 — Shared structural guard** | Accept ADR-0164. Use the separately discovered guard for append and suffix removal through the existing authoring transaction. Preserve legacy operations. |
| **J3 — Replay and recovery** | Approve exact copy and tail checks, same-GUID Redo, conservative conflict refusal, and history publication before refresh. Settle Apply independently and require explicit recovery after uncertain or failed Apply. |
| **J4 — Acceptance and authorization** | Approve Windows 100% Save/reopen, guarded and legacy client checks, and applicable repository gates. After manual design merge, authorize S2–S4 together. |

Suggested authorization for the reviewed revision

> I approve J1–J4, Spec/Plan 0108 and ADR-0164. After manual merge
> of the design, proceed from latest main through S2–S4, including
> implementation, scoped fixes, applicable verification, valid
> evidence reuse and a review-ready PR. Keep Windows 100% acceptance
> and the applicable repository gates. Final merge is manual.

## Verification checklist

- [ ] **R1–R2 — Selection and copy.** Cover overlapping selections
  through an unselected intermediate, several surviving parents
  including scene root, and a multi-selection reduced to one root.
  Check exactly 64 copied nodes and rejection at 65 before complete
  leaf reads or GUID allocation. Use an interleaved source with
  child-before-parent order, sibling-name collisions within the
  batch, and the existing unnamed and UTF-8 naming cases. Compare
  full values, including signed scale, asset references, absent
  optional values and active camera. Refuse an incomplete snapshot.
- [ ] **R3 and ADR-0164 — Guard and compatibility.** Verify both
  valid shapes, one revision, ordered events and the full ticket range
  including the guard. Refuse malformed shapes, misplaced or mixed
  guards, excessive count, duplicate GUID or address, existing-node
  writes, an invalid append position and deletion outside the suffix.
  Force a failure in a later command and a stale revision at host
  admission. Check that each refusal leaves the candidate unpublished.
  Exercise the shared contract in CLI, C# SDK, MCP and Editor, with
  native and managed vectors. Verify bidirectional compatibility
  with an actual legacy peer, ordinary and earlier guarded edit
  paths, absent/false/malformed capability, reconnect and reopen,
  and raw legacy refusal of an unknown guard after an ordinary write
  in the same request. Exercise the existing single-node action.
- [ ] **R3–R4 — Settlement, replay and recovery.** Change selection
  during capability discovery and replace a row with the same GUID
  before send. Verify a known refusal has no edit, history or Apply.
  Cover a later selection during verified settlement, lost or malformed
  submission receipts and verified commit overtaken before readback.
  Exercise stacked replay, a changed copy or unrecorded child, changed
  active camera, equal-valued same-GUID replacement of a copy or
  surviving parent, changed tail boundary and Redo GUID collision.
  Verify an unrelated edit can coexist, stale revision alone retains
  the entry, and an indeterminate replay follows the explicit recovery
  path. Disconnect during post-settlement refresh and confirm verified
  history was published. Cover older pending Apply settlement and a
  verified edit with a lost Apply receipt.
- [ ] **R1–R5 — Windows 100% actual Editor.** Open a disposable
  hand-authored scene with selected nodes under different parents and
  a selected descendant of one root. Invoke the Hierarchy action and
  record the originals and copies by GUID, full authored state and
  document order. Check selection feedback, one authoring revision
  and one Undo operation. Undo and Redo the whole batch and verify
  Redo retains its GUIDs and slots. Use the existing recoverable Apply
  failure setup, distinguish the edited source document from the last
  good projection, then explicitly Apply the current document without
  duplicating history or nodes. Save after Redo and reopen that same
  source with the same active asset generation. Verify GUIDs, names,
  parents, order, component values, asset references and active
  camera before applying, confirm history cleared and check the
  resulting Runtime projection after explicit Apply.
- [ ] **Existing gates.** Build and test affected native and managed
  targets and run the applicable Editor/Runtime integration, Vulkan
  Validation Layers and golden checks. Assess applicable Android
  native build and packaging from the affected modules. Run
  `atlantis_runtime --gate --applies` against the complete final
  diff and keep the established protocol and baseline when selected.
  Document checks apply to S1. Engine and Windows verification
  apply to the implementation candidate in S3.

## Rollback Plan

Use a follow-up implementation PR to revert the action and additive
command if needed. Existing scene files keep ordinary nodes and remain
openable.

## Definition of Done

Follow [the repository definition of done](../process/definition-of-done.md).
Complete S3 and link the evidence in S4.
