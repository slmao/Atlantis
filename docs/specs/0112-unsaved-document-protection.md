# Spec 0112 — Unsaved Document Protection

- **Status:** Draft
- **Author:** Codex
- **Created:** 2026-11-17
- **Related Plan:** [Plan 0112](../plans/0112-unsaved-document-protection.md)
- **Approval:** Pending joint review
- **Related ADR:** [ADR-0166](../adr/0166-guarded-authoring-save-and-reopen.md) (Proposed)

## Summary and motivation

Protect unsaved scene work when an Editor window closes or reloads its
document from source. Today the window closes after the Saved Scenes busy
check and tears down its owned Runtime, while shared `authoring.open` can
replace the document without a revision precondition. `authoring.save`
reports whether the write succeeded. It supplies no persisted revision, and
Inspector blur can submit input while a dialog takes focus. The Runtime
projection and its Apply receipt refer to a separate snapshot.

## Goals and non-goals

Use one leave decision for the main Editor window and an explicit **Reopen
from Source** action. Preserve the current choices for independently opening
saved scenes, New Scene and Save As. This slice covers current scene
Inspector input, its authoring document and ordinary interactive close or
reload. Asset-management dialog drafts, arbitrary in-window scene switching,
OS termination, disk watchers, collaborative locks, autosave and crash
recovery are outside scope.

## Requirements and proposed design

### R1 — Show three independent states

- **Input draft.** The Editor tracks unsubmitted scene name/property text,
  including grouped and mixed-value Inspector fields. Invalid text is still a
  draft. A picker awaiting confirmation has made no edit. Active Gizmo
  previews use their existing cancellation/cleanup route when the leave
  decision interrupts capture. Local drafts and previews never participate
  in the saved-state calculation.
- **Document.** The shared Authoring session reports whether its current
  committed content matches the canonical content read on last successful
  open or written on last successful ordinary Save. Exact Undo back to that
  content is clean even at a later revision. A refused transaction, no-op,
  failed save and lost connection have distinct outcomes. Imported scenes
  may have transient document edits and show them as unsaved. Their source
  remains read-only.
- **Runtime.** Keep the existing tracked Apply status and recovery from
  [0083](0083-authoring-apply-outcomes.md). Saved changes can coexist with a
  pending, failed or unknown Apply. Dirty changes can already be visible in
  the Runtime. Leave actions issue no Apply, Retry, Step or Resume.
  Existing accepted Apply snapshots settle independently. On
  `DocumentReopened`, keep the current 0083 transition to explicit Apply
  for the new document and suppress any queued follow-up for the old one.
  An older accepted snapshot may still swap in or fail; its result belongs
  to its own receipt and does not certify the new document or the saved file.

### R2 — One leave decision

Protect ordinary window close (including Alt+F4) and the new Reopen from
Source action. Any Editor-initiated handoff that replaces the current
document must use the same gate before teardown. The first version adds
no in-place scene switch. The [0090](0090-saved-scene-build-open.md) entry
still opens an independent Editor and [Save As](0088-scene-save-as.md) and
[New Scene](0089-new-hand-authored-scene.md) still create independent sources.
Retain existing busy-operation guards.

Track the draft as text changes, including its field, original value,
selection and document lifetime. Starting a leave action must capture
this state before focus loss can commit or reset it. Arm the guard before
the Reopen control takes focus. For native title-bar close and Alt+F4,
cancel the first close synchronously and suppress blur submission even if
focus moves before the framework Closing callback. Ordinary focus movement
within Inspector keeps its existing commit rules. Defer refreshes that
would destroy a still-valid draft during the modal. Settle previously
submitted authoring commands and preview cleanup through the existing
worker, then re-read host state. Hold further local document commands and
coalesce repeat leave requests until settlement. Do not wait for an
independent Apply to reach a terminal result.

For confirmed clean content with no draft, continue after the normal guard
without a prompt. Otherwise show the current scene, the destination action,
which work is at risk, and consequences for the Runtime where relevant.
Use [0093](0093-editor-visual-layout.md) dialog styles, with accessible
status and focus and Cancel as the safe default.

| Choice | Behavior |
| --- | --- |
| **Save and Close / Save and Reopen** | Save the reviewed committed content through the guarded shared call. With a draft present, the action requires explicit acknowledgement that the draft will be discarded. Offer guidance to cancel and finish editing to keep it. Save is unavailable for draft-only changes or an imported source. Leave only after the save and the appropriate leave guard succeed. |
| **Close Without Saving / Reopen Without Saving** | Advance only for the reviewed document revision after recheck. On reopen, replace from the source and emit `DocumentReopened`. On close, the owned Runtime follows existing shutdown. An attached Runtime and its shared document remain live. Explain this distinction in the prompt. |
| **Cancel** | Keep the current document, history and valid draft. Restore the originating field, its text and caret/selection if still valid, or focus the current Inspector or invoking control. Escape and closing the prompt act as Cancel before a mutation is sent or after its result has been shown. During an in-flight mutation, keep the window and progress visible until settlement or an unknown result. An interrupted pointer gesture remains cancelled. |

Dropping a draft takes effect on confirmed leave. If a Save succeeds but the
following Reopen fails, report that Save succeeded while Reopen failed.
Keep the document, history and any still-valid draft. A successful Reopen
uses existing document lifetime invalidation, clears its prior selection,
draft and undo/redo history, and shows the resulting document and existing
Runtime status separately. Reissue window close only via a one-use
continuation for the same window, worker and reviewed decision. Closing
the window ends that window's draft and history. Distinguish startup
pending from confirmed document absence. Invalidate startup callbacks
when closing and use existing ownership cleanup for a locally started
Runtime.

### R3 — Shared state, conflicts and persistence

Use [ADR-0166](../adr/0166-guarded-authoring-save-and-reopen.md) for the
Authoring-owned saved state, guarded Save/Reopen and client compatibility.
Remote Editor, CLI, native and C# clients and MCP use the same values. On
capable hosts, route the ordinary Editor Save button through the same guarded
Save and report its receipt independently of Apply.

Observe external edits, Save and Reopen. Read authoritatively when a leave
starts and immediately before close, using the existing worker. A document
identity or revision change between observation and guarded write/reopen
refuses the action. If either has changed before consuming the decision,
refresh the prompt and require a fresh choice. A
same-revision external Save may update the dirty indication. Compare document
state during ordinary connected refresh as well, so other clients' saves
become visible even without a new transaction event. Preserve the existing
legacy open/save calls for other clients; an external legacy Reopen bypasses
this Editor's prompt and invalidates its leave intent and history. A changed
draft subject, removed field, replaced node or external write to the draft
field also invalidates that draft. Keep the captured text available to
inspect or copy during the decision, with no editable restoration or submit
against a changed target. An unrelated edit can invalidate the decision
while leaving the draft valid.

The final close check observes the latest host state. Finish local
settlement and show all confirmations before that check. On success,
consume the decision and start existing teardown in the UI continuation
without another prompt, scheduled delay or new wait. Host dispatch and
OS teardown are separate, including any existing cleanup on that path.
An external edit after the final check can still be lost when an owned
Runtime exits. Keep this limit in the authorization. Guarded Reopen is
atomic with other clients' edits and opens on that host.

### R4 — Failure, unknown results and legacy hosts

Keep the window and any valid draft on definite refusal, while reporting
the source and appropriate recovery. On stale refusal refresh before
another decision. On write or source failure, retain the old document and
offer a new attempt once the fault is corrected. A known not-sent request
and a potentially executed request have different outcomes. On disconnect or
an invalid success receipt after handoff, keep the original connection or
protocol error and mark the operation result unknown. Stop the automatic
leave continuation and never replay the mutation automatically.

**Check Again** reads from the same authenticated host launch. After a
disconnect it may use a read-only connection with the original session
credentials. For unknown Save, the same document and current revision
with a matching persisted anchor establishes that the reviewed content is
saved. For unknown Reopen, a new document identity establishes that the
old target can no longer be reopened by that request. Neither observation
identifies who performed the operation. Present what was verified and wait
for a new leave decision. A different host launch cannot reconcile either
mutation. If the same token remains current and the outcome is unresolved,
offer explicit retry only on a live editing connection, after a fresh
read and renewed confirmation. Repeating guarded Save can write only the
same reviewed content. At most one Reopen with that token can succeed.
Without a live editing connection the probe cannot enable writes or
restore editing. Show which checks were possible, keep the old result
unconfirmed and allow Cancel or explicit Close Anyway. No automatic
reconnect, mutation replay or leave continuation follows an unknown result.
Ignore replies from old window, worker or decision lifetimes.

When state is unavailable, show it as unknown and retain captured text
independently of Inspector cleanup on disconnect. With no usable editing
field, keep the text available to inspect or copy. Keep a confirmed
no-document state distinct. On a capable host, the initial automatic
open uses the shared absence guard. If another client opens first, attach
to that document without reopening it. An uncertain initial open follows
the same check and no-automatic-replay rule. Offer **Close Anyway** with
an explicit consequence prompt when persistence remains unknown. Recheck a
reachable host before consuming that decision. Newly verified
changes return to the normal prompt. Disable guarded Reopen when the host
lacks the capability, and report why. A legacy host with an open document
can still be edited; explicit toolbar Save retains its legacy behavior and
cannot certify a guarded leave. If a legacy host has no open document,
show guidance to use a capable Runtime or open the source explicitly with
a legacy client before attaching. The protected Editor does not
automatically issue a legacy Open.

## Architectural impact

[ADR-0166](../adr/0166-guarded-authoring-save-and-reopen.md) records the
additive shared API and saved-state ownership as an extension of ADR-0130.
Use existing Authoring dispatch, C# SDK and Editor worker boundaries. Store
one clean content anchor and the current dirty comparison per open document.
Compare at most once per accepted current revision and use at most one
background state read per 250 ms in the existing worker poll. Explicit
leave and write settlement can request a fresh read. The comparison runs
on demand in the existing host dispatch. No new per-frame hook, thread,
dependency, scene format or Runtime projection control is planned.

## Alternatives, verification and open decisions

An Editor-only flag misses external edits and saves. Apply revision measures
the projected snapshot. Reading revision then calling legacy Open has a race.
The shared guarded option covers these cases while leaving an explicit
window-teardown limit for J3. The [Plan](../plans/0112-unsaved-document-protection.md)
maps R1–R4 to host, client and Windows 100% checks. J1–J4 require joint
review, including the draft choice, legacy and unknown handling, and the
close race boundary.
