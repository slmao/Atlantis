# Plan 0141 — Hierarchy Batch Rename

- **Spec:** [Spec 0141](../specs/0141-hierarchy-batch-rename.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Maintainer, 2026-11-22, [joint approval of `0ece893c`](https://github.com/slmao/Atlantis/pull/445#issuecomment-7081010194); J1–J4 and S2–S4 authorized after maintainer merge as `b62fab76`.
- **ADR:** [ADR-0189](../adr/0189-document-guarded-node-name-batch.md) (Accepted)
- **Design baseline:** `4bf9791c`

## Objective

Number a reviewed Hierarchy selection in one durable, undoable scene edit.
Keep the host's stale refusal atomic and the Editor's history tied to a
verified settlement.

## Milestones

1. **S1 — Joint design review.** Resolve J1–J4, record the approved revision
   and ADR decision, and wait for the maintainer to merge the design.
2. **S2 — Shared guard.** Add the closed transaction guard, authoritative
   document check, capability and client codec/dispatch coverage. Verify
   name, ticket and event behavior before wiring the Editor command.
3. **S3 — Editor workflow.** Add reveal, prefix and read-only preview, then
   route confirmation and guarded history through existing Editor ownership,
   edit settlement, Apply, and save flows.
4. **S4 — Verification and review.** Run the applicable gates and
   Windows acceptance below. Include source, build and fixture provenance
   with the evidence in a review-ready implementation PR.

## Files and modules touched (expected)

| Area | Files and work |
| --- | --- |
| Shared admission | `src/connection/include/atlantis/connection/authoring_connection.h`, `src/connection/src/authoring_connection.cpp`, `src/authoring/src/authoring_session.cpp`. Guard, capability, errors and serialized admission using the existing candidate copy. |
| Remote and CLI | `src/remote/src/codec.cpp`, `src/remote/src/remote_client.cpp`, `src/cli/src/command_layer.cpp`, `src/cli/src/invocation.cpp`. Add the closed wire shape, capability negotiation and guarded transaction diagnostics. Existing dispatch and authentication boundaries remain. |
| Managed clients | `src/csharp/Atlantis.Gameplay/Authoring.cs`, `src/csharp/Atlantis.Gameplay/Remote/AuthoringCodec.cs` and `AuthoringSession.cs` in the same directory. Extend `src/mcp/Atlantis.Mcp/ToolSchemas.cs` and `ToolRegistry.cs` for the existing transaction tool. Native callers use the Connection command value. |
| Editor | `src/editor_app/Atlantis.Editor/MainWindow.cs`, `EditorSession.cs` and a new `BatchRenameWindow.cs` beside them. A bounded preview/intent model in `Atlantis.Editor.Model/BatchRename.cs` and the existing `EditHistory.cs` and `StructuralEdits.cs` replay paths. Reuse `DocumentTree`, Hierarchy projection and navigation, name validation, draft protection, history and Apply settlement. |
| Verification and reference | Focused tests in `tests/authoring/authoring_session_tests.cpp`, `tests/remote/remote_authoring_tests.cpp`, `tests/cli/cli_command_tests.cpp`, the C# authoring/codec suite and generated codec vectors under `tests/csharp/`, and `src/mcp/tests/Atlantis.Mcp.Tests/M3Tests.cs`. Add focused model/Windows checks under `src/editor_app/tests/Atlantis.Editor.Tests/`. Update `docs/architecture/remote_protocol.md` for the as-built contract. Use a disposable Bistro hand-authored fixture with existing assets. |

## Sequencing and dependencies

Reuse [0069 name semantics](../specs/0069-scene-node-names.md),
[0083 Apply settlement](../specs/0083-authoring-apply-outcomes.md),
[0106 guarded batch verification](../specs/0106-scene-multiselect-batch-edit.md),
[0112 document tokens and draft protection](../specs/0112-unsaved-document-protection.md)
and [0113 reveal and filter guards](../specs/0113-hierarchy-search-and-filter.md).
Add no numbered source-format version. Keep fixtures and evidence separate
from shipped Bistro content.

## Joint review decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Selection and naming** | Hand-authored scenes, 2–64 explicitly selected targets, unfiltered depth-first Hierarchy order and one prefix with `01`–`64`. Existing same-parent duplicates remain legal. Reveal all hidden targets before confirmation. |
| **J2 — Commit and compatibility** | Accept ADR-0189. Use the full preview document token, a distinct guard and positive capability. Refuse the entire batch on any intervening revision. Preserve existing client operations and source compatibility. |
| **J3 — History and recovery** | One verified entry. Replay only against matching names and node occurrences. Distinguish refusal, uncertainty and Apply failure. Keep source Save and explicit Apply recovery independent. |
| **J4 — Execution authorization** | After approval of the exact Spec/Plan/ADR revision and maintainer merge, authorize S2–S4 in one decision, including ordinary in-scope fixes, applicable verification and a review-ready PR. The maintainer retains final merge. |

## Verification checklist

- [ ] **R1 — Preview, names and UI state.** Verify reversed click order
  and mixed root/child selection across interleaved document slots.
  Cover 2 and 64 targets, refusal above the limit, no implicit descendants,
  search and component filters hiding selected nodes, revealed selection
  parity and a separate Confirm. Check unnamed, duplicate and unchanged
  rows, ASCII numbering under another locale, quoted/Unicode names, the
  exact encoded-byte limit including suffix, invalid controls/surrogates,
  and all-unchanged. Verify Cancel/Escape, focus restoration and active
  draft/gesture protection with no document or history writes.
- [ ] **R2 — Admission, race and compatibility.** Exercise a valid batch,
  wrong document ID, stale/invalid revision, malformed or mixed guards,
  duplicate targets, unsupported provenance and a late invalid name.
  Assert unchanged document bytes/revision/events on refusal. Use a
  second client to rename, reorder, replace a node with the same GUID and
  name, and reopen the source after preview. Hold a capability/preflight
  reply across a UI selection or filter change. Refusals and canceled
  sends add neither history nor Apply. Verify exact ticket interval,
  decoder parity, positive capability and fail-closed older-host
  behavior in Remote, CLI, SDK and MCP. Verify older name-capable
  clients still read and rename through the original contract.
- [ ] **R3 — Settlement and recovery.** Verify one Undo/Redo entry and
  exact replay values even after filters or selection change. Exercise
  external rename and same-GUID replacement conflicts, stale replay,
  successful commit followed by disconnect during refresh, lost or
  invalid ticket, and unknown Apply receipt. Check the distinction
  between document and projection, protected older Apply settlement and
  no automatic retry. In a disposable fixture, cause Apply to fail
  through the existing reload path, restore the prerequisite and use
  explicit Apply. Verify it does not resubmit the batch or its events.
- [ ] **Windows 100% — Bistro.** Use a hand-authored street segment with
  at least three recognizable storefronts or lamps using the pinned
  Bistro assets. Find them via Hierarchy filters, select out of order, hide one,
  reveal all, and show every old/new pair before confirmation. Capture
  the native preview and numbered Hierarchy. Confirm once and compare
  names, GUIDs, parents, ordered slots and component/asset values across
  the whole selected group and its unaffected neighbors. Verify Cancel
  and an external rename between preview and Confirm leave zero batch
  writes. Undo once and redo once, Save and reopen the same source, then
  compare exact names by GUID and check structure/order again. Keep
  the source and shared readback evidence with the screenshots.
- [ ] **Applicable gates.** Windows native/managed build and affected
  Authoring, Connection, Remote, CLI, SDK, MCP and Editor checks.
  Check Android build/packaging applicability for shared native changes
  and run Vulkan validation for the exercised Windows path. Apply
  existing headless/image and final-diff performance applicability
  rules. Reuse evidence only for unchanged relevant inputs. The present
  design review requires document, link and whitespace checks.

## Rollback plan

Remove the optional command and UI. Existing saved names use the same
scene format and remain readable by existing clients.

## Definition of Done

Use the [repository definition of done](../process/definition-of-done.md).
Record implementation and verification evidence in its PR.
