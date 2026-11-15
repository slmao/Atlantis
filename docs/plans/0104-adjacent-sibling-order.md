# Plan: Persist an Adjacent Sibling Swap in Hierarchy

- **Spec:** [Spec 0104](../specs/0104-adjacent-sibling-order.md)
- **Status:** In Review
- **Author:** Codex
- **Joint Human Review:** Pending.
- **Related ADR:** [ADR-0160](../adr/0160-mutable-adjacent-sibling-order.md) (Proposed).

## Objective

Deliver one persistent adjacent-sibling swap across the shared
authoring clients. Record the approved order semantics and prove that
both the live reload and cold path consume the saved order.

## Milestones / Task Breakdown

1. **S1 — Joint review.** Approve the Spec, Plan, J1–J4 and ADR-0160
   together. The maintainer merges the design before implementation.
2. **S2 — Implementation.** Add the native command, refusal and event
   handling first. Wire the native and managed codecs, CLI and MCP.
   Then connect Editor order updates, guarded actions, and structural
   history and apply integration. Document the shared entry points.
3. **S3 — Verification.** Complete the applicable checklist with the
   approved scene-order behavior and existing gates. Record evidence
   and the inputs that justify any reuse.
4. **S4 — Review-ready PR.** Link approval, scope, results and outstanding
   human decisions. Final merge is manual.

## Files / Modules Touched (expected)

| Scope | Work |
| --- | --- |
| `src/connection/{include/atlantis/connection/authoring_connection.h,src/authoring_connection.cpp}` and `src/authoring/src/authoring_session.cpp` | Shared values, filter and authoritative candidate validation, revision and event. |
| `src/remote/src/{codec.cpp,remote_client.cpp,remote_server.cpp}` and `src/cli/src/command_layer.cpp` | Closed codecs, legacy subscription behavior and scene transaction support. |
| `src/csharp/Atlantis.Gameplay/{Authoring.cs,Remote/AuthoringCodec.cs,Remote/AuthoringSession.cs}` and `src/mcp/Atlantis.Mcp/{ToolRegistry.cs,ToolRegistry.Events.cs,ToolSchemas.cs}` | Managed bindings, capability observation and shared MCP contract. |
| `src/editor_app/Atlantis.Editor.Model/{AuthoringDocument,DocumentTree,StructuralEdits,EditHistory}.cs` and `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow}.cs` | Guarded move, observed order, one-step replay and correlated apply. |
| `tests/authoring/{authoring_session_tests,authoring_round_trip_tests}.cpp`, `tests/remote/{remote_authoring_tests,remote_codec_tests}.cpp`, `tests/cli/cli_scene_tests.cpp`, `tests/csharp/conformance/{conformance_tests.cpp,codec_vectors.jsonl}`, and existing SDK and MCP test projects | Transaction, wire and version coverage. |
| `tests/world/scene_bake_tests.cpp`, `tests/runtime/{scene_extraction_tests,scene_picking_tests}.cpp`, `src/editor_app/tests/Atlantis.Editor.Tests/{HierarchyTests,HistoryTests,ApplyOutcomeTests,IntegrationTest,J7Acceptance,Program}.cs` | Slot, history, bake and Windows closeout. Use disposable scene input from the acceptance fixture. |
| `README.md`, `docs/architecture/remote_protocol.md` and `docs/specs/README.md` | Shared entry point, wire reference and merge navigation. |

Scene source grammar, cook formats, production Runtime bake and
Renderer are reference inputs. Acceptance uses the existing fixture and
build mechanisms.

## Sequencing & Dependencies

Start S2 from latest main after manual design merge. Reuse [0065](../specs/0065-authoring-data-layer.md)
transaction and Save rules, [0083](../specs/0083-authoring-apply-outcomes.md)
apply outcomes, [0103](../specs/0103-hierarchy-reparent.md) row identity
guards, and [0090](../specs/0090-saved-scene-build-open.md) independent
saved-scene Build and Open.

| Decision | Recommendation |
| --- | --- |
| J1 — Scope and slots | Approve one adjacent swap under the current parent or scene root. Use the canonical global slots and the existing bake order. |
| J2 — Shared contract and compatibility | Approve one revision-guarded swap command through the existing transaction and one opt-in order event. Accept the legacy-reader and mixed positional editing boundary in ADR-0160. |
| J3 — History and apply | Approve one guarded reversible swap and conservative invalidation of prior position-dependent history on external or uncorrelated reorder. Keep explicit recovery and current reload semantics. |
| J4 — Acceptance and authorization | Approve targeted shared-client checks, Windows 100% Save/Open, and independent cold startup from the same source. Authorize S2–S4 after design merge. |

Suggested joint authorization for the reviewed revision

> I approve J1–J4, Spec/Plan 0104 and ADR-0160. After manual merge of
> the design, proceed from latest main through S2–S4, including
> implementation, ordinary scoped fixes, applicable verification, valid
> evidence reuse and a review-ready PR. Preserve existing gates. Final
> merge remains a human decision.

## Verification Checklist

- [ ] **R1/R2 transaction and slots.** Test roots, nested siblings and an
  interleaved unrelated node and descendants. Compare exact global
  order, node data and the active camera. Exercise both directions, ends
  of the list, missing or replaced rows, different parents, a no-op,
  imported provenance, a stale revision and a mixed-command batch.
  Check refusal consumes no edit revision, emits no order event and
  leaves the source untouched. Verify one valid ticket, one revision
  and one order event for acceptance.
- [ ] **R2 shared clients and events.** Verify the same transaction in
  native and managed codec fixtures and across Editor, CLI, SDK and MCP.
  Check GUID-filtered order events, preserved unrelated events and
  current `listNodes` results. Keep an old client subscribed to the
  declared legacy set while a new client swaps. Confirm fresh legacy
  reads see the result and the old decoder remains usable. Check
  upgraded clients connected to an old Runtime keep existing actions
  through the documented fallback and report the unavailable swap.
- [ ] **R2/R3 conflicts and replay.** Cover a missing or invalid receipt,
  externally overtaken commit and unknown apply result. Undo and Redo
  one swap without restoring unrelated slots. Verify a changed pair,
  parent or adjacency is refused. Check that a local reorder can be
  undone before earlier create/copy/delete history, and that external
  reorder invalidates the affected class of positional records while
  other edits retain their guards.
- [ ] **R4 Runtime ordering.** Verify fresh bake indices and extracted
  order with GUIDs, world matrices and active camera preserved. Use
  coincident pickable meshes with distinct GUIDs so equal-distance
  picking selects the first baked candidate, then the other after
  swapping. Keep a no-reorder regression fixture on its existing
  draw and pick path. Use the existing Vulkan Validation Layers and
  applicable image/golden protocol for the Runtime checks.
- [ ] **R1–R4 Windows closeout.** At Windows 100%, use one disposable
  registered hand-authored saved scene with an interleaved pair, child
  nodes, and the order-sensitive mesh fixture. Swap under a paused
  Runtime with a known projection. Induce a correlated reload failure
  with a recoverable external edit, restore that edit, and explicitly
  Retry. Verify no duplicate swap. Undo and Redo, then Save. Reopen
  the source on the original generation and verify Hierarchy and
  shared list order, preserved references and cleared local history.
  Explicitly Apply and record ordered Runtime results.
- [ ] **R4 same-source cold start.** Record the saved source hash. Use
  that exact saved unit with existing 0090 Build and Open in an
  independent Editor/Runtime. Keep 0090's configured build asset
  inputs and verify the relevant mesh/material content and GUIDs match
  the live acceptance fixture. Record both routes' asset inputs and
  confirm the source hash, ordered GUIDs and pick winner match the
  applied reopen. The cold path starts from cooked data without an
  authoring Apply in the new process.
- [ ] **Existing gates.** Build affected clients and native targets,
  run linked tests and applicable Android build/package checks.
  Evaluate the final full diff with `--gate --applies` and follow the
  existing protocol if selected. Reuse valid evidence when inputs
  match. Preserve baselines and golden review rules.

## Rollback Plan

Revert the new action, command, event and replay support. Saved scenes
contain ordinary node sequences and remain readable by existing
readers, including their new order.

## Definition of Done

Follow the [Definition of Done](../process/definition-of-done.md).
The implementation PR owns evidence, deviations and merge conditions.
