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
| **J1 — Editor slice** | Approve up to 64 selected nodes, existing outlines and marker feedback, and an explicit assignment of one non-optional numeric, vector or enum field from a current common-field snapshot. Keep drafts local and structure and Gizmo single-target. |
| **J2 — Atomic shared contract** | Approve the leading revision guard in the existing property-only transaction. Enable guarded submissions only after positive capability discovery from the current open document. Keep legacy decoding, closed admission and the existing failure drain. |
| **J3 — Conflict and recovery** | Approve the frozen draft, one locally verified history record, and guarded Undo/Redo. Drop conflicted or indeterminate replay entries. Retain an entry on a stale revision alone. Preserve older receipt settlement and require explicit Apply after uncertain edits. |
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
  focus, limit and removal, refresh, Apply, same-GUID replacement,
  reopen and session change. Verify a late mesh pick loses to a newer
  marker pick or repeat Hierarchy click, and a replaced hit is
  ignored. Verify selected mesh GUIDs and existing marker points use
  the existing overlay. Check that entering multiselection cancels
  active single-target gestures and disables structure and reference
  actions until a single node remains and their guards permit them.
- [ ] **R2 — Values and drafts.** Cover a component common to all and
  another present only on some, equal/mixed values, absent/present
  optional values, incomplete component reads and unreadable values.
  Exercise A → B → A and a late same-selection refresh. Confirm a
  mixed placeholder and blur submit nothing, Escape discards, and
  an explicit valid value submits once. Cover vectors, all-equal
  assignment, draft preservation on unrelated refresh, and changes
  away and back after the displayed read. Cover node and component
  replacement, selection change before versus after submission, and
  conflicting refreshes that require new input.
- [ ] **R3 — Host and shared clients.** Prove the guard plus N writes
  obtains N+1 tickets, one revision and N ordinary events on commit.
  Cover 1 and 64 writes, guard-only, a misplaced or duplicate guard,
  duplicate node, mixed/cross-field, over-limit and stale requests.
  Force one invalid target/value after an earlier valid write and
  verify the document, revision and events stay unchanged. Inject a
  last-read-to-admission race. Compare native and managed wire vectors
  and exercise native CLI, direct C# SDK, MCP and Editor. On a real or
  released old peer verify raw guarded submission is refused as a
  whole even if an ordinary write precedes the unknown guard in the
  raw message. Check upgraded clients send no guarded command for an
  absent, false, malformed or stale capability, and ordinary
  operations continue. Verify a capable open, a null document,
  rediscovery after reopen or reconnect, and old readers of a new
  document descriptor.
- [ ] **R3/R4 — Settlement, history and projection.** Replay distinct
  original typed values with one Undo and Redo, independent of the
  current selection, preserving unrelated values. Verify changed,
  missing and replaced targets refuse the entire replay and drop its
  entry. Race an unrelated edit against replay admission and verify
  the stale refusal keeps that entry on its original stack. Inject
  refusal then failed refresh, missing or malformed tickets/drains,
  a confirmed commit overtaken during its verification fence, and
  lost Apply acceptance. Check the two history stacks, replay
  publication and CLI transport category. Settle an older paused
  Apply in both success and failure without sending an automatic
  follow-up after an uncertain batch. Advance the document between
  verification and Apply acceptance and attribute the result to
  the actual receipt revision. Verify explicit Retry retains
  history and issues no duplicate property writes.
- [ ] **R1–R4 — Windows 100% Save/reopen.** In a disposable registered
  hand-authored scene, Ctrl-select two visible mesh nodes with
  distinct local scales. Verify both outlines and the mixed value.
  Add a non-mesh node to verify the component intersection, then
  return to the original pair. While paused, assign one full scale
  vector and verify one revision, one history entry, the untouched
  control and unchanged source bytes. Induce a recoverable
  dependency reload failure. Check the exact Apply receipt and
  the previous Runtime generation and transforms by GUID. Repair
  and explicitly Retry, then verify the projected common scale,
  Undo restoring both distinct values and Redo restoring the
  shared value after Apply.

  Save, retain the saved source path and digest, and reopen that
  source on the same active asset generation. Verify document
  values and the untouched control by GUID, cleared transient
  multiselection/draft/history and explicit Apply of the reopened
  revision. Retain the native DPI reading and document/Runtime
  observations. The check uses the existing shared open and Apply
  path on the already active generation.
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
