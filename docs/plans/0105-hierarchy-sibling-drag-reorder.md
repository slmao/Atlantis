# Plan: Drag a Hierarchy Node to a Sibling Position

- **Spec:** [Spec 0105](../specs/0105-hierarchy-sibling-drag-reorder.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending.
- **Related ADR:** [ADR-0161](../adr/0161-atomic-sibling-slot-move.md) (Proposed).

## Objective

Deliver one persistent same-parent drag move, with one verified
shared settlement and one Editor history step. Verify saved order
through both live reload and independent cold startup.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Approve the Spec, Plan, J1–J4 and ADR-0161
   together. The maintainer merges the design before implementation.
2. **S2 — Implementation.** Add the host command, event and tests.
   Wire the shared clients and capability tiers. Then add the
   Editor drop intents, guarded settlement, history and Apply wiring.
3. **S3 — Verification.** Complete applicable checks on the final
   candidate. Record inputs, evidence reuse and any failures.
4. **S4 — Review-ready PR.** Link the approval, changes and
   verification, including evidence for both Windows routes and the
   final gate verdicts. Final merge remains manual.

## Files / Modules Touched (expected)

| Scope | Work |
| --- | --- |
| `src/connection/include/atlantis/connection/authoring_connection.h`, `src/connection/src/authoring_connection.cpp`, `src/authoring/src/authoring_session.cpp` | Closed command, refusal and event values, filter and atomic validation. |
| `src/remote/src/{codec,remote_client}.cpp`, `src/cli/src/{command_layer,invocation}.cpp` | Wire values, capability negotiation, shared CLI command and transport-error recovery. Reuse the existing server dispatcher. |
| `src/csharp/Atlantis.Gameplay/Authoring.cs`, `src/csharp/Atlantis.Gameplay/Remote/{AuthoringCodec,AuthoringSession}.cs`, `src/mcp/Atlantis.Mcp/{ToolRegistry.cs,ToolRegistry.Events.cs,ToolSchemas.cs}` | Typed bindings and the existing MCP transaction and drain tools. |
| `src/editor_app/Atlantis.Editor/{MainWindow,EditorSession}.cs`, `src/editor_app/Atlantis.Editor.Model/{AuthoringDocument,DocumentTree,EditHistory,StructuralEdits}.cs` | Drag intent, row feedback, fenced order, replay and existing Apply integration. |
| `tests/authoring/{authoring_session_tests,authoring_round_trip_tests}.cpp`, `tests/remote/remote_authoring_tests.cpp`, `tests/cli/cli_scene_tests.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, existing SDK and MCP test projects | Exact slots, wire and compatibility regressions. |
| `tests/world/scene_bake_tests.cpp`, `tests/runtime/{scene_extraction_tests,scene_picking_tests}.cpp`, `src/editor_app/tests/Atlantis.Editor.Tests/{HierarchyTests,HistoryTests,ApplyOutcomeTests,IntegrationTest,J7Acceptance,Program}.cs` | Slot, history, recovery, bake and Windows acceptance. Use a disposable saved unit. |
| `README.md`, `docs/architecture/remote_protocol.md`, `docs/specs/README.md` | Shared entry point, protocol reference and navigation. |

Existing scene source grammar, cook formats, production bake, Runtime
frame path and renderer are reference inputs. Reuse the 0090 saved
slot and existing acceptance tooling.

## Sequencing & Dependencies

Start S2 after the design is manually merged to main. Reuse the shared
transactions from [0065](../specs/0065-authoring-data-layer.md),
[0083](../specs/0083-authoring-apply-outcomes.md) Apply,
[0103](../specs/0103-hierarchy-reparent.md) drag and incarnation guards,
[0104](../specs/0104-adjacent-sibling-order.md) shared order and
[0090](../specs/0090-saved-scene-build-open.md) independent Build and Open.

| Decision | Recommendation |
| --- | --- |
| J1 — Gesture and slots | Approve row-edge relative placement within one parent, including roots. Keep the center reparent target. Rotate existing sibling slots. |
| J2 — Shared contract | Approve one revision-guarded command and one fixed-size opt-in event. Retain the adjacent and pre-adjacent compatibility tiers with the ADR's mixed editing boundary. |
| J3 — History and recovery | Approve one interval-guarded Undo/Redo step, 0104 positional history rules and explicit recovery after unknown or unverifiable settlement. |
| J4 — Verification and authorization | Approve the Windows 100% Save/reopen and same-source 0090 cold-start checks, with the applicable existing gates. Authorize S2–S4 after manual design merge. |

Suggested authorization for the reviewed revision

> I approve J1–J4, Spec/Plan 0105 and ADR-0161. After manual merge of
> the design, proceed from latest main through S2–S4, including
> implementation, ordinary scoped fixes, applicable verification,
> valid evidence reuse and a review-ready PR. Preserve existing gates.
> Final merge remains a human decision.

## Verification Checklist

- [ ] **R1/R2 gesture and slots.** Exercise top and bottom placement at
  the first, middle and last visible siblings, with valid center
  reparent and distinct indicators. Include an after marker for a
  target with visible children. Cancel an edge drop that would resolve
  to a different row incarnation or intent than the last feedback.
  Move a root and a nested node across at least two siblings in each
  direction in host fixtures. Compare the exact global sequence,
  interleaved descendants and unrelated slots, data and active camera.
  Cover a no-op gap, self, different parents, a missing anchor,
  cancellation, import and mixed-command refusal. A refusal has no
  edit revision, move event, history or Apply. Verify one ticket, one
  revision and one event for a valid move.
- [ ] **R2 guard and observation.** Confirm replacement of a displayed
  participant or an intervening row invalidates the interval, and a
  same-source reopen between fences invalidates the drop. Inject a
  final-check-to-host race. Check fixed-size event positions, source
  and component filters, event order and a fenced refresh with a
  queued lifecycle change and both order event kinds. Verify
  unchanged incarnations and no double application of covered deltas.
- [ ] **R2 contract and peers.** Match native and managed wire
  fixtures. Exercise shared CLI, SDK, MCP and Editor paths. On a
  0104 Runtime keep adjacent actions and whole-row reparent. On a
  pre-0104 peer keep existing authoring reads, writes and reparent
  after same-connection fallback. Verify the exact filter sequence
  and that only the successful tier enables operations. Verify
  unsupported moves are unsent and malformed or unexpected probes
  do not enable support. Keep an old decoder subscribed while a new
  client moves, then verify a legacy event and a fresh list read.
- [ ] **R3 settlement and conflicts.** Replay one move as Undo and
  Redo without restoring unrelated slots, including a move whose
  drop anchor keeps its slot. Refuse a stale interval with changed
  order, replacement, insertion or parent. Allow an unrelated
  insert before the interval that shifts its global positions.
  Verify an external or uncorrelated move drops prior positional
  records, including pending transitions, and correlated local edits
  preserve them. Inject
  lost or invalid replies, an overtaken commit and a lost Apply
  receipt. A matching refusal survives a later refresh error.
  Verify CLI connection-category guidance and Editor replay
  publication. Exercise older paused Apply success and failure.
  Preserve its receipt and terminal outcome without an automatic
  follow-up before explicit recovery of the current revision.
- [ ] **R1–R4 Windows closeout.** At 100%, drag across at least two
  siblings in a disposable registered scene with non-contiguous
  slots, children and distinct coincident pickable siblings whose
  first baked GUID changes with the move. Record a fixed ray and
  the initial winner and projection generation. Check marker,
  selection, exact shared order and references. While paused, queue
  the move's Apply and induce a recoverable reload failure on Step.
  Check the old winner and projection remain, with the new document
  and history intact. Repair, explicitly Retry and Step, and verify
  the winner changes without repeating the move. Undo and Redo
  restore the respective winners after projection. Save, reopen on
  the same generation, confirm local history clears, explicitly
  Apply and record the new winner for the cold comparison.
- [ ] **R4 same-source cold startup.** Record the saved scene and
  registration fragment hashes. Use the existing 0090 Build and Open
  flow to cook those exact bytes in an independent Editor/Runtime.
  Confirm the hashes across Build and Open and compare dependency
  GUIDs and relevant artifact content. The child uses the configured
  saved slot under 0090's ordinary launch. Before any Apply in the
  child, compare shared order and the winner of the same explicit
  raycast with the original reopened projection. Check bake/extraction
  order directly and confirm world matrices and active camera by GUID.
  Runtime entity enumeration is GUID-sorted and does not demonstrate
  bake order.
- [ ] **Existing gates.** Build affected native and managed targets
  and run their applicable checks, including Android build/package
  for affected native targets and Runtime Vulkan validation. Use the
  existing image/golden protocol when applicable and preserve
  golden review rules. Run `atlantis_runtime --gate --applies` against
  the final full diff and follow the established protocol if selected.
  Reuse evidence only where sources, tests, assets, build inputs and
  environment match. Keep baseline and original verdicts.

## Rollback Plan

Revert the relative-move interaction and shared extensions. Saved
scenes still contain ordinary node sequences usable by existing readers.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
The implementation PR owns evidence, deviations and merge conditions.
