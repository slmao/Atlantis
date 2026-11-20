# Plan 0121 — Editor Tasks & Issues for the Current Session

- **Spec:** [Spec 0121](../specs/0121-editor-tasks-and-issues.md)
- **Status:** Draft
- **Author:** Codex
- **Joint Human Review:** Pending

## Objective and milestones

Deliver a unified, current-session operation view with truthful outcomes,
object location and copyable diagnostics. Start from the latest main after
manual design merge, using the existing asset and Apply contracts.

1. **S1 — Joint review.** Rule on J1–J4 before implementation.
2. **S2 — Model and evidence.** Define the bounded row projection, operation
   ownership and receipt matching. Feed it from current asset admission and
   status paths and typed Apply observations. Keep existing status owners,
   retries and transport behavior. Cover lost, unsent, superseded and stale
   replies without adding background work.
3. **S3 — Panel and navigation.** Add the status-bar affordance and nonmodal
   panel using 0093 resources. Wire safe GUID and document location, copy
   diagnostics, visible refusal reasons and existing recovery guidance.
   Verify normal Editor feedback, focus, and prior editing behavior.
4. **S4 — Verification and review.** Run final candidate checks, record any
   reused evidence with matching inputs, attach Windows 100% evidence and
   submit a review-ready PR for manual merge.

## Expected implementation files

| Files | Responsibility |
| --- | --- |
| `src/editor_app/Atlantis.Editor.Model/TaskIssues.cs` (new), `src/editor_app/Atlantis.Editor.Model/DocumentTree.cs` | Session row projection and typed observations of existing Apply settlement. |
| `src/editor_app/Atlantis.Editor/{MainWindow.cs,EditorSession.cs,EditorVisuals.cs}`, `src/editor_app/Atlantis.Editor/TaskIssuesPanel.cs` (new) | Feed observations from the owners, present the panel and route guarded navigation and clipboard access. |
| `src/editor_app/tests/Atlantis.Editor.Tests/TaskIssuesTests.cs` (new), `src/editor_app/tests/Atlantis.Editor.Tests/{Program.cs,InspectorTests.cs}` | Model checks and hosted Windows acceptance. |
| `docs/specs/README.md`, approval metadata for this Spec and Plan | Record approval and link implementation evidence. |

Use the existing asset status, Apply controller, Assets reveal and document
lifetime guards. No native, shared client, package or build change is planned.
Use the repository's narrow approval rule for any required file-list adjustment.

## Joint decisions

| Decision | Recommendation |
| --- | --- |
| **J1 — Scope and ownership** | Current Editor and Runtime session only. Capture local intent and correlate typed receipts. Keep outstanding attempts plus 32 settled rows. End navigation on disconnect and clear on session replacement (R1). |
| **J2 — Result and next step** | Separate publication from activation and document commit from projection. Use existing recovery guidance. Preserve proof and expose unknown results without initiating recovery (R2). |
| **J3 — Panel and actions** | Nonmodal status-bar panel with GUID-based Assets reveal, document-scoped Apply location and scoped clipboard diagnostics. Reuse 0093 and current owner guards. No new ADR (R3). |
| **J4 — Verification and authorization** | Retain Windows 100%, controlled uncertainty coverage and existing gates. After joint approval and manual design merge, authorize S2–S4 once, including scoped fixes, applicable verification, valid evidence reuse and a review-ready PR. Final merge stays with the maintainer. |

## Verification checklist

- [ ] **R1–R2 — Model.** Test receipt correlation across import, rebuild,
  publication and Apply, repeated polling, same-target older and newer
  attempts, external/latest-only observations, a job that lacks admission,
  proven unsent, sent with a lost reply, expired known ID, disconnect and
  replaced worker/document. Verify the row bound and stable ordering. Cover
  pending when paused, superseded, recoverable Apply failure, fatal stop and
  an Apply result without commit proof. Confirm verified publication and
  unknown activation are distinct.
- [ ] **R3 — Interaction.** Exercise focus, keyboard navigation, narrow and
  standard window layout at 100%, and long diagnostic copy through the
  clipboard. Close and reopen the panel to verify retained rows. Verify
  stale, missing and not-yet-activated targets leave current
  selection and filters intact. Verify exact GUID reveal with repeated
  names and document-lifetime guarding on deferred navigation. Assert no
  additional polling or write from opening the panel, locating, copying or
  unknown outcome handling. Keep existing Apply, Undo/Redo and inline
  guidance working.
- [ ] **R1–R3 — Windows 100% texture publication.** Publish one texture through
  the existing Editor import. Capture the row showing **Published · restart
  required**, the published and active generation IDs, and its next step.
  While the texture is absent from the active catalog, Locate keeps the
  current selection and explains activation. Copy the job and asset IDs.
  Explicitly restart into the named generation and confirm the same GUID in
  Assets. The prior session's tasks are cleared.
- [ ] **R1–R3 — Windows 100% committed edit with failed Apply.** Use a
  controlled recoverable Runtime reload failure after a confirmed edit
  commit. Reuse the disposable-catalog fixture in InspectorTests: temporarily
  invalidate the candidate material artifact, then restore its original
  bytes. Compare the committed document revision and unchanged Runtime
  projection. Capture **Edit committed · Apply failed**, locate the
  document's Apply surface and copy the exact available error and receipt.
  Verify retained Undo/Redo and explicit Apply after correction using
  existing controls. With a controlled lost receipt, confirm **Outcome
  unknown**, inspect guidance and zero automatic retry.
- [ ] **Applicable gates.** Build and run the Editor/model and focused
  interaction checks. Record native Windows screenshots and use clean
  Vulkan validation for hosted Runtime verification. Review final
  image/golden, platform and performance applicability. Run
  `--gate --applies` on the final diff and retain the standard protocol
  if selected. Windows DPI acceptance is 100% only.

## Rollback and definition of done

Revert the panel and observation wiring to restore the existing status
surfaces. Existing operations and persisted data require no migration. Meet the
repository [definition of done](../process/definition-of-done.md) and record
applicable verification in the implementation PR.
