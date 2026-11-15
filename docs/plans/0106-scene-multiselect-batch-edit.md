# Plan 0106 — Scene Multiselection and Batch Property Editing

- **Spec:** [Spec 0106](../specs/0106-scene-multiselect-batch-edit.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending.
- **Related ADR:** [ADR-0162](../adr/0162-revision-guarded-authoring-property-batch.md) (Proposed).

## Objective

Ship a bounded Hierarchy selection and a common-field Inspector with
a single guarded assignment, one reversible history step, and the
existing Apply and Save workflows.

## Milestones

1. **S1 — Joint review.** Decide J1–J4, approve Spec/Plan 0106, accept
   ADR-0162 and manually merge the design before implementation.
2. **S2 — Implementation.** Implement and verify host admission and
   the shared codecs, then wire CLI, C# SDK and MCP discovery and
   settlement. Build Editor selection/overlay and common fields on
   existing reads. Add guarded draft, commit, replay and Apply
   integration.
3. **S3 — Verification.** Close the applicable checklist on the final
   candidate. Preserve original results and record the provenance of
   any reused evidence.
4. **S4 — Review-ready PR.** Link joint approval, scope, checks and any
   remaining decisions. The maintainer performs the final merge.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/connection/include/atlantis/connection/authoring_connection.h`, `src/connection/src/authoring_connection.cpp`, `src/authoring/src/authoring_session.cpp` | Closed revision guard, capability and refusals. Validate once through ordinary authoring transactions. |
| `src/remote/src/{codec,remote_client}.cpp`, `src/cli/src/{command_layer,invocation}.cpp` | Shared wire values and capability. Reuse the dispatcher and `scene tx`. Preserve refusal and disconnect reporting. |
| `src/csharp/Atlantis.Gameplay/{Authoring.cs,Remote/AuthoringCodec.cs,Remote/AuthoringSession.cs}`, `src/mcp/Atlantis.Mcp/{ToolRegistry,ToolSchemas}.cs` | Typed guard and discovery, existing exact MCP transaction and failure drain. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession}.cs`, `src/editor_app/Atlantis.Editor.Model/{DocumentTree,Inspector,GizmoOverlay,EditHistory,StructuralEdits,AuthoringDocument}.cs` | Selection, common values, overlay, lifetime/fence handling, replay and Apply controller integration. |
| `tests/authoring/authoring_session_tests.cpp`, `tests/remote/remote_authoring_tests.cpp`, `tests/cli/cli_scene_tests.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, `tests/csharp/Atlantis.Gameplay.Tests/{CodecSuite,LiveAuthoringSuite}.cs`, `src/mcp/tests/Atlantis.Mcp.Tests/M3Tests.cs` | Host semantics, wire, compatibility and shared client coverage. |
| `src/editor_app/tests/Atlantis.Editor.Tests/{HierarchyTests,HistoryTests,ApplyOutcomeTests,IntegrationTest,J7Acceptance,Program}.cs` | Selection, Inspector, replay, fault and Windows verification. Use disposable sources. |
| `README.md`, `docs/architecture/remote_protocol.md`, `docs/specs/README.md` | Shared operation, protocol reference and closeout navigation. |

Selection and batch editing target Atlantis Editor App. Production
World, Runtime, Renderer, scene cook and saved scene formats are
reference inputs. Reuse the existing bounded pipelined reads for
components and property values. Do not use the existing unconditional
pipelined commit-and-apply route for guarded transactions.

## Sequencing and dependencies

Base the design on `40f428d3`, the manual merge of
[PR #376](https://github.com/slmao/Atlantis/pull/376). Start S2 from
latest main after the approved design merges. Reuse
[0065 authoring](../specs/0065-authoring-data-layer.md),
[0068 Editor and history](../specs/0068-authoring-editor.md),
[0083 tracked Apply](../specs/0083-authoring-apply-outcomes.md) and
the row incarnation and observation patterns from
[0103](../specs/0103-hierarchy-reparent.md) and
[0105](../specs/0105-hierarchy-sibling-drag-reorder.md).

| Decision | Recommendation |
| --- | --- |
| **J1 — Editor slice** | Approve up to 64 selected nodes, existing outlines and marker feedback, common fields with four display states, and explicit assignment of one non-optional numeric, vector or enum field. Keep structure and Gizmo single-target. |
| **J2 — Atomic shared contract** | Approve the leading revision guard in the existing property-only transaction, advertised on the open document. Keep positive capability gating, native/managed compatibility and the existing failure drain. |
| **J3 — Conflict and recovery** | Approve the frozen draft and per-target replay checks, one local history record, and explicit current-document Apply after uncertain settlement. |
| **J4 — Acceptance and authorization** | Approve the shared client and fault cases, actual Windows 100% Save/reopen and applicable gates. After manual design merge, authorize S2–S4 as one unit. |

Suggested authorization for the reviewed revision

> I approve J1–J4, Spec/Plan 0106 and ADR-0162. After manual merge of
> this design, proceed from latest main through S2–S4, covering
> implementation, scoped fixes, applicable verification, valid evidence
> reuse and a review-ready PR. Keep the Windows 100% Save/reopen check
> and the applicable existing gates. The implementation merge remains
> a human decision.

## Verification checklist

- [ ] **R1 — Selection and overlay.** Cover replace, toggle, keyboard
  focus, limit and removal, refresh, Apply, recent versus stale picks,
  same-GUID replacement, reopen and session change. Verify multi-select
  sends the selected GUIDs through the existing overlay and highlights
  selected existing marker points. Verify Gizmo and single-target
  structure/reference actions are unavailable with two selected nodes,
  and become available again when one remains and their existing
  guards permit them.
- [ ] **R2 — Values and drafts.** Cover a component common to all and
  another present only on some, equal/mixed values, absent/present
  optional values, unreadable values, and late snapshots from the
  previous selection. Confirm a mixed placeholder and blur submit
  nothing, Escape discards, and an explicit valid value submits once.
  Cover vector parsing with every element explicit, the all-equal
  no-op, external field changes during a draft, node and component
  replacement, and unrelated edits before preflight.
- [ ] **R3 — Host and shared clients.** Prove the guard plus N writes
  obtains N+1 tickets, one revision and N ordinary events on commit.
  Cover 1 and 64 writes, guard-only, a misplaced or duplicate guard,
  duplicate node, mixed/cross-field, over-limit and stale requests.
  Force one invalid target/value after an earlier valid write and
  verify the document, revision and events stay unchanged. Inject a
  last-read-to-admission race. Compare native and managed wire vectors
  and exercise native CLI, direct C# SDK, MCP and Editor. On a real or
  released old peer verify raw guarded submission is refused as a
  whole, upgraded clients avoid sending it when capability is missing,
  and existing ordinary operations continue. Check positive, absent,
  false and malformed capability and old readers of a new document.
- [ ] **R3/R4 — Settlement, history and projection.** Replay distinct
  original typed values with one Undo and Redo, independent of the
  current selection, preserving unrelated values. Verify changed,
  missing and replaced targets refuse the entire replay and settle
  history without sending Apply. Inject refusal then failed refresh,
  missing and malformed tickets/drains, a later revision overtaking a
  confirmed change, and lost Apply acceptance. Check unknown guidance
  and replay publication, including CLI transport category. Settle an
  older paused Apply in both success and failure without issuing an
  automatic follow-up after an uncertain batch. Check explicit
  current-document Retry retains successful history and issues no
  duplicate writes. Use the no-write path for all preflight failures.
- [ ] **R1–R4 — Windows 100% Save/reopen.** In a disposable registered
  hand-authored scene, Ctrl-select two visible mesh nodes with
  distinct values for a common field. Verify both outlines, mixed
  display, an explicit assignment and an untouched control node.
  Add a third selection to verify the component intersection, then
  return to the original pair. Verify one document commit and one
  history entry. With Runtime paused, induce one recoverable reload
  failure, confirm the prior projection and new document/history,
  repair and explicitly Retry. Verify projection, Undo restoring
  distinct original values, and Redo restoring the common value.
  Save and reopen the same source on the same active asset generation,
  verify values and the untouched field by GUID, confirm transient
  multiselection/draft/history cleared, and explicitly Apply the
  reopened document. Retain native DPI, projection and source
  evidence.
- [ ] **Existing gates.** Build affected Windows native/managed
  targets and run their applicable tests. Keep Android build and
  packaging for affected native targets and Vulkan validation for the
  changed overlay usage. Use the existing golden process where
  applicable. Evaluate the complete diff with
  `atlantis_runtime --gate --applies` and run the existing protocol
  if selected. Preserve the original verdicts and baseline. Reuse
  only results with matching relevant inputs and environment.

## Rollback

Revert the additive guard and capability, client support and new
Editor workflows. Saved values remain ordinary scene properties.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
The implementation PR owns gate results, evidence reuse and deviations.
