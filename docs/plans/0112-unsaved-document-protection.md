# Plan 0112 — Unsaved Document Protection

- **Spec:** [Spec 0112](../specs/0112-unsaved-document-protection.md)
- **Status:** Approved
- **Author:** Codex
- **Joint Human Review:** Maintainer approved J1–J4 and Spec/Plan 0112, accepted [ADR-0166](../adr/0166-guarded-authoring-save-and-reopen.md) and authorized S2–S4 on 2026-11-17 at [PR #389, `985fd010`](https://github.com/slmao/Atlantis/pull/389#issuecomment-6923439281). The design was manually merged at `e050bbce`.

## Objective and sequencing

Add trustworthy saved-state feedback and one Editor leave decision for
window close and Reopen from Source. Design baseline is `87f064bc`, the
manual merge of [PR #388](https://github.com/slmao/Atlantis/pull/388).
Begin S2 from latest main after the joint review and manual design merge.
Reuse the [0065](../specs/0065-authoring-data-layer.md) document owner,
[0068](../specs/0068-authoring-editor.md) worker/history lifecycle,
[0083](../specs/0083-authoring-apply-outcomes.md) Apply settlement,
[0090](../specs/0090-saved-scene-build-open.md) independent Editor entry
and [0093](../specs/0093-editor-visual-layout.md) visuals.

## Milestones

1. **S1 — Joint review.** Resolve J1–J4, jointly approve Spec/Plan,
   accept ADR-0166 and manually merge the design.
2. **S2 — Shared contract and Editor.** Implement and verify the canonical
   content anchor and guarded operations in Authoring first. Expose them
   through Connection, Remote, SDK, CLI and MCP using existing dispatch and
   encoding. Then wire guarded startup, a UI-free leave coordinator,
   worker settlement, Inspector draft capture and an accessible prompt
   in the ordinary Editor. Route confirmed leave through the existing
   shutdown or document lifecycle after the worker decision is current.
3. **S3 — Verify.** Run the mapped affected checks and the Windows 100%
   flow against the final candidate. Record source, build and test
   provenance for any reused evidence and report the gate selector.
4. **S4 — Submit.** Open a review-ready implementation PR with the
   decision links, final verification, deviations and human merge
   conditions.

## Expected files and modules

| Scope | Work |
| --- | --- |
| `src/authoring/src/authoring_session.cpp`, `src/connection/{include/atlantis/connection/authoring_connection.h,src/authoring_connection.cpp}` | Reuse canonical serialization and current atomic writes. Track the saved anchor and open revision, add read and guarded session operations. |
| `src/remote/src/{codec.h,codec.cpp,remote_server.cpp,remote_client.cpp}` | Dispatch, codec validation and compatibility for additive calls. |
| `src/csharp/Atlantis.Gameplay/{Authoring.cs,Remote/AuthoringCodec.cs,Remote/AuthoringSession.cs}` | Managed values and session operations, exact errors and unknown-result handling. |
| `src/cli/src/{command_layer.cpp,invocation.cpp}`, `src/mcp/Atlantis.Mcp/ToolRegistry.cs` | Expose exact operations and actionable diagnostics. |
| `src/editor_app/Atlantis.Editor.Model/DocumentLeave.cs`, `src/editor_app/Atlantis.Editor/{EditorSession,MainWindow,DocumentLeaveWindow}.cs` | Local decision and draft lifetime, worker refresh and guarded writes, modal and protected close/reopen entry. Reuse existing Editor styles and busy rules. |
| `tests/{authoring/authoring_session_tests.cpp,remote/remote_authoring_tests.cpp,cli/cli_scene_tests.cpp}`, `tests/csharp/{conformance,Atlantis.Gameplay.Tests}`, `src/mcp/tests/Atlantis.Mcp.Tests`, `src/editor_app/tests/Atlantis.Editor.Tests` | Focused host, codec/conformance, client, UI model/worker and integration checks. |
| `docs/architecture/remote_protocol.md`, `README.md`, `docs/specs/README.md` | As-built shared usage and delivery navigation. |

The design keeps source serialization, process ownership, build/CI, frame
scheduling, graphics and dependencies on their existing paths. Use a
disposable scene and fault injection through the existing test harness
for acceptance. Report needed file-list changes under the repository's
existing approval rule.

## Joint review

| Decision | Recommendation |
| --- | --- |
| **J1 — State and draft** | Approve the three independent signals, exact Undo-to-saved, and explicit draft discard acknowledgement when saving committed edits. Cancel restores valid text and focus. |
| **J2 — Shared guard and compatibility** | Accept ADR-0166. Use the canonical saved anchor and the per-open document ID plus revision across clients. Use the same Reopen call with an absence precondition at startup. Preserve legacy calls, and keep automatic Open disabled in the protected Editor on an unsupported host. |
| **J3 — Leave, external edits and recovery** | Approve fresh decisions on conflicts, same-launch observation and explicit retry using the same live guard when still current. Preserve the attached Runtime. Accept the last-read-to-teardown race for an owned Runtime. In-host Reopen remains atomic. |
| **J4 — Verification and authorization** | Approve the mapped host/client and Windows 100% checks, with the existing applicable gates. Authorize S2–S4 together after design merge. |

Suggested authorization for the reviewed revision:

> I approve J1–J4 and Spec/Plan 0112 jointly and accept ADR-0166,
> including guarded startup, the older-host fallback, explicit retry on
> a live editing connection and the final observation boundary for owned
> Runtime close.
> After manual design merge, proceed from latest main through S2–S4,
> covering implementation, scope-preserving fixes, applicable verification
> and a review-ready PR. Reuse valid evidence and retain applicable gates.
> Final merge remains manual.

## Verification checklist

- [ ] **R1 — State and history.** On a real Authoring host, cover initial
  open, equal edit, refused edit, property transaction, exact Undo to the
  canonical saved content, Redo, ordinary and guarded Save. Verify node
  order and identity contribute to equality. Read from a second
  connection. External Save clears dirtiness at the same revision.
  Save As and New Scene leave the anchor unchanged. Imported edits remain
  visible with Save refused. No-document stays distinct from unknown.
- [ ] **R1/R3 — Apply independence.** Hold a tracked Apply while paused,
  then save or reopen. Query the original receipt by ID from a second
  client through settlement and controlled reload failure. Verify Reopen
  leaves the new document at Needs Apply and drops a queued follow-up for
  the old document. Save and Discard send no implicit Apply, Retry, Step
  or Resume. Failed Apply does not make a successful Save dirty. Verify
  explicit recovery targets the new current document.
- [ ] **R3 — Shared conflict and compatibility.** With separate real
  clients edit, save or reopen between the decision read and the guarded
  action. Cover same-GUID replacement and a restarted process with the
  same scene, source and numeric revision. Verify a stale token refuses
  before disk I/O. Race the initial no-document check with an external
  first open and edit, then attach without a second open or revision
  advance. Refused calls leave the document, saved anchor and Apply state
  intact. Success publishes the existing Reopen event once. Exercise
  legacy calls against the new host, new calls against a legacy host, and
  legacy Editor attach with and without an open document. Verify invalid
  request fields, impossible success payloads and the same refusal and
  transport distinctions across CLI, SDK and MCP.
- [ ] **R2/R4 — Leave decision and faults.** Cover dirty, draft-only,
  dirty plus invalid draft, imported and clean. Use single and mixed-value
  text fields. Test pre-Closing blur, Cancel, Escape, rapid repeated
  close/reopen, late startup and operation callbacks, and cancellation
  with a still-valid versus replaced or externally edited draft target.
  Force a write failure and an unreadable/unparseable reopen, including
  successful Save followed by failed Reopen. Test connection loss after
  send and invalid success for both operations, same-launch positive
  persistence evidence, unresolved reads with a request still queued
  and rejected different-launch evidence. Verify explicit live retry
  only writes the same bytes for Save and yields at most one Reopen
  success for a token. An unresolved read while disconnected allows no
  mutation. Verify a stale decision sends no new write or Apply and
  never closes the window.
  Inject a revision change before the final close read to verify fresh
  confirmation. Verify an attached close leaves the Runtime and its
  document in place.
- [ ] **Windows 100% desktop.** On actual Windows with the normal hosted
  Viewport and a disposable hand-authored scene, exercise the standard
  and compact layouts. Edit a committed field and leave text unsubmitted
  in another field. Invoke Reopen from Source by keyboard and pointer,
  and invoke native title-bar Close and Alt+F4 with the input focused.
  Verify Cancel restores text, focus and caret/selection without a
  transaction. Check normal blur commit on ordinary Inspector focus change.
  Save and reopen with explicit draft discard, confirm the committed
  value survives and the old draft/history are gone. Make another
  edit, discard and reopen to confirm the persisted value wins. Show
  Save failed and unknown-result feedback without dismissing the window,
  with long source paths readable by keyboard and pointer. Save and
  close an owned Editor, then open the same source through the existing
  workflow. Verify the same scene and node GUIDs, persisted fields and fresh
  Editor history on reopening. Use an independent client
  for reachable external edits and confirm the reviewed revision is
  refreshed. Retain native captures and the corresponding state evidence.
- [ ] **Repository gates.** Build and run the affected native, CLI,
  codec/SDK, MCP and Editor checks. Run
  `atlantis_runtime --gate --applies` against the full implementation diff
  and follow the existing protocol if selected. Evaluate Windows Runtime
  and Android build/package coverage from the final native diff. Run clean
  Vulkan validation on affected GPU paths in the Windows flow. No
  rendered-output change is expected; reuse matching existing golden
  evidence where valid and apply normal golden review to any rendered
  change. S1 requires only documentation, link and whitespace checks.

## Rollback and definition of done

The additive calls and Editor gate can be reverted together. Ordinary
sources and legacy clients require no migration. Follow the repository
[definition of done](../process/definition-of-done.md).
