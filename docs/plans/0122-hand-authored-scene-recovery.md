# Plan 0122 — Automatic Recovery Copy for Hand-Authored Scenes

- **Spec:** [Spec 0122](../specs/0122-hand-authored-scene-recovery.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** J1–J4 approved and S2–S4 authorized for [PR #409](https://github.com/slmao/Atlantis/pull/409) at `2f3a2642`. ADR-0171 accepted. Maintainer merge `20315fef`.
- **ADR:** [ADR-0171](../adr/0171-authoring-recovery-copy.md) (Accepted)

## Objective and milestones

Recover committed, unsaved hand-authored scene content without writing the
formal source or changing the Runtime projection. Design baseline is
`dccece17`, the maintainer merge of [PR #408](https://github.com/slmao/Atlantis/pull/408).

1. **S1 — Joint review.** Resolve J1–J4 and accept ADR-0171. Start S2
   from latest main after manual design merge.
2. **S2 — Authoring owner and shared contract.** Capture after successful
   dirty transactions and retain raw source identity independently of 0112's
   saved-content anchor. Implement the single guarded Windows slot with
   bounded background preparation, safe publication and lifecycle fencing.
   Bind Restore to Authoring validation and the existing Reopen event.
   Expose exact shared status/preview, restore and discard.
3. **S3 — Editor workflow and acceptance.** Add the pending-copy dialog
   to guarded Open/Reopen and Scene Files using 0093 styles. Keep pending
   storage, ordinary Save, Apply and close feedback distinct. Exercise
   the two Windows 100% paths and the mapped host and client checks below.
4. **S4 — Final verification and PR.** Recheck final candidate input and
   gate applicability, record pass/fail evidence and submit for human merge.

## Expected files and module boundaries

| Files | Work |
| --- | --- |
| `src/authoring/{src/authoring_session.cpp,include/atlantis/authoring/authoring_session.h}`, `src/connection/{include/atlantis/connection/authoring_connection.h,src/authoring_connection.cpp}` | Validated canonical checkpoint, saved-source evidence and guarded document installation. Shared recovery values and operations. |
| `src/runtime/{authoring_host.cpp,authoring_host.h,CMakeLists.txt}`, private `src/runtime/scene_recovery_store.{cpp,h}` (new) | Attach Windows persistence to the Authoring session, using the existing settlement point. Compile the store into the Windows executable only and reuse its existing OS dependencies. |
| `src/remote/src/{codec.h,codec.cpp,remote_server.cpp,remote_client.cpp}`, `src/csharp/Atlantis.Gameplay/{Authoring.cs,Remote/AuthoringCodec.cs,Remote/AuthoringSession.cs}` | Add strict dispatch and protocol encoding, SDK contracts and unknown mutation outcome reporting. Native SDK callers use the shared Authoring connection. |
| `src/cli/src/{command_layer.cpp,invocation.cpp}`, `src/mcp/Atlantis.Mcp/ToolRegistry.cs` | Surface the same bounded read and guarded mutations as CLI scene actions and exact MCP tools. |
| `src/editor_app/Atlantis.Editor/{EditorSession.cs,MainWindow.cs,DocumentLeaveWindow.cs,EditorVisuals.cs}`, `src/editor_app/Atlantis.Editor/SceneRecoveryWindow.cs` (new), `src/editor_app/Atlantis.Editor.Model/DocumentLeave.cs` | Dialog, startup and reopen decisions, worker guards and feedback. Keep draft handling on the established 0112 route. |
| `tests/authoring/authoring_session_tests.cpp`, `tests/connection/authoring_connection_tests.cpp`, `tests/remote/remote_authoring_tests.cpp`, `tests/cli/cli_scene_tests.cpp`, `tests/runtime/CMakeLists.txt`, `tests/runtime/scene_recovery_store_tests.cpp` (new), `tests/csharp/{conformance,Atlantis.Gameplay.Tests}`, `src/mcp/tests/Atlantis.Mcp.Tests/`, `src/editor_app/tests/Atlantis.Editor.Tests/` | Extend existing fixtures for store and authoring lifecycle, codecs, adapters and hosted Windows checks. |
| `docs/architecture/remote_protocol.md`, `docs/specs/README.md`, approval metadata for this Spec, Plan and ADR | Record the approved usage and workflow links. |

Reuse the 0112 guarded open/save and 0083 Apply result model. No cooker,
scene source schema, Renderer or RuntimeHost change is in scope. A necessary
file-list adjustment uses the repository's narrow approval rule.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — Ownership and durability** | Approve only committed dirty revisions of hand-authored Windows scenes. Use the host-local single slot, writer lease, background preparation and 16 MiB limit. Disclose the latest durable revision and the crash window before publication (R1, ADR-0171 D1). |
| **J2 — User decision and conflicts** | Use a bounded source comparison. Require the current document, reviewed copy and raw source fingerprints for Restore. On conflict allow inspected Discard only. Restore stays dirty and keeps the recovery copy. Keep unresolved copies across ordinary close/reopen, with Decide Later as the default and no forced restore (R2–R3). |
| **J3 — Shared contract and isolation** | Approve the three additive shared methods and existing Reopen invalidation, with no automatic Apply or Save and no automatic replay. Preserve legacy operations and keep ordinary authoring usable if recovery is unavailable (R3–R4, ADR-0171 D2). |
| **J4 — Verification and authorization** | Approve both Windows 100% cases, targeted fault/compatibility coverage and applicable gates. After joint approval and manual design merge, one authorization covers S2–S4, including scope-preserving fixes, applicable verification, valid evidence reuse and a review-ready PR. Final merge stays manual. |

## Verification checklist

- [ ] **R1/R3 — Store and revision boundaries.** Verify newest complete
  committed payload, canonical scene identity and raw baseline including
  formatting-only changes, external saves, multiple transactions with
  coalescing, Undo-to-saved, failed Save, and no capture from a refusal,
  draft or imported scene. With controlled publication/cleanup failures,
  stale epochs and competing hosts, verify the previous complete copy
  survives and retired work cannot overwrite or revive it. Cover oversize,
  invalid/unsupported envelope, aliases or reparse paths, and unreadable
  storage. Assert the formal source and catalog are unaffected.
- [ ] **R2/R3 — Resolve safely.** Change scene data, source bytes, or the slot
  after Preview. Refuse stale Restore or Discard as applicable. Preserve
  an unresolved copy while leaving or saving an independent document.
  Verify the exact Discard removes only the reviewed copy, restores no
  state, and permits future checkpoints. Verify successful Restore has a
  fresh token, dirty document, stable identities, one Reopen event, no
  imported history and no implicit Apply or Save. After lost or invalid
  mutation receipts, show unknown and issue no automatic replay.
- [ ] **R4 — Clients.** Run focused native/managed codec conformance,
  CLI and MCP checks, Editor worker/model checks and both version
  directions. Verify the headline state and preview bounds, exact copy
  and token guards, preserved transport error and stale reply isolation.
- [ ] **R1/R2 — Windows 100% abnormal exit.** Open a disposable hand-authored
  scene in the ordinary hosted Editor. Commit an authored change, force
  a recoverable Apply failure with the existing fixture, and wait for
  the matching durable revision. Abruptly terminate the Editor and owned
  Runtime without graceful close. Launch again via the normal saved scene
  entry, Preview the actual edit and Restore. Verify exact authored
  changes and identities, retained recovery slot, unchanged formal source
  bytes and unchanged Runtime projection on restore. Assert no imported
  undo history or automatic Apply. After explicit Apply succeeds, then
  explicit Save, verify source change, dirty clears and a later reopen
  shows the saved edit without offering the same copy.
- [ ] **R2/R3 — Windows 100% source conflict.** With a separate client and
  Runtime, edit and Save the official source after a durable checkpoint.
  Reopen the scene and verify the conflict, useful preview, disabled
  Restore and safe default. Confirm Discard. Verify exact external source
  bytes, current document and projection are unaffected, the discarded
  copy is gone and a later reopen shows the external edit. Also verify
  in a controlled host test that a source change between review and
  Restore is refused.
- [ ] **Applicable gates.** Build and run affected Authoring, Runtime and
  shared-client checks. Record Windows 100% native screenshots and clean
  Vulkan validation from the hosted flows. Assess golden and platform
  applicability, including affected native Android targets if applicable.
  Run final diff `--gate --applies` and retain the existing performance
  protocol if selected. Reuse only inputs with matching provenance.

## Rollback and definition of done

Revert the shared operations, store attachment and Editor controls together.
Recovery files are outside formal scene and build inputs. Leave existing
copies in place on rollback. The
[definition of done](../process/definition-of-done.md) applies, with
verification and manual merge recorded in the implementation PR.
