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
  the Runtime. A Save or Reopen never issues Apply, Retry, Step or Resume.
  Existing accepted Apply snapshots settle independently.

### R2 — One leave decision

Protect ordinary window close (including Alt+F4) and the new Reopen from
Source action. Any Editor-initiated handoff that replaces the current
document must use the same gate before teardown. The first version adds
no in-place scene switch. The [0090](0090-saved-scene-build-open.md) entry
still opens an independent Editor and [Save As](0088-scene-save-as.md) and
[New Scene](0089-new-hand-authored-scene.md) still create independent sources.
Retain existing busy-operation guards.

Capture the current field, text and focus before a leave action can trigger
blur submission. Suppress that submission during the decision. Settle already
sent authoring commands through the existing worker and re-read the host
state. Hold further local document commands and coalesce repeated leave
requests while a decision or write is pending. Do not wait for an independent
Apply to reach a terminal result.

For confirmed clean content with no draft, continue after the normal guard
without a prompt. Otherwise show the current scene, the destination action,
which work is at risk, and consequences for the Runtime where relevant.
Use [0093](0093-editor-visual-layout.md) dialog styles, with accessible
status and focus and Cancel as the safe default.

| Choice | Behavior |
| --- | --- |
| **Save and Close / Save and Reopen** | Save the reviewed committed content through the guarded shared call. With a draft present, the action requires explicit acknowledgement that the draft will be discarded. Offer guidance to cancel and finish editing to keep it. Save is unavailable for draft-only changes or an imported source. Leave only after the save and the appropriate leave guard succeed. |
| **Close Without Saving / Reopen Without Saving** | Advance only for the reviewed document revision after recheck. On reopen, replace from the source and emit `DocumentReopened`. On close, the owned Runtime follows existing shutdown. An attached Runtime and its shared document remain live. Explain this distinction in the prompt. |
| **Cancel** | Keep the current document, history and valid draft and restore focus if the field still exists. Cancel is also the effect of Escape or closing the prompt before sending a mutation. During an in-flight mutation, keep the window and progress visible until the result settles or is reported unknown. An interrupted pointer gesture remains cancelled. |

Dropping a draft takes effect on confirmed leave. If a Save succeeds but the
following Reopen fails, report that Save succeeded while Reopen failed.
Keep the document, history and any still-valid draft. A successful Reopen
uses existing document lifetime invalidation, clears its prior selection,
draft and undo/redo history, and shows the resulting document and existing
Runtime status separately. Closing the window ends only that window's
draft and history.

### R3 — Shared state, conflicts and persistence

Use [ADR-0166](../adr/0166-guarded-authoring-save-and-reopen.md) for the
Authoring-owned saved state, guarded Save/Reopen and client compatibility.
Remote Editor, CLI, native and C# clients and MCP use the same values. On
capable hosts, route the ordinary Editor Save button through the same guarded
Save and report its receipt independently of Apply.

Observe external edits, Save and Reopen. Read authoritatively when a leave
starts and immediately before close, using the existing worker. A revision
or incarnation change between observation and guarded write/reopen refuses
the action. If the reviewed document key or revision has changed before
consuming the decision, refresh the prompt and require a fresh choice. A
same-revision external Save may update the dirty indication. Compare document
state during ordinary connected refresh as well, so other clients' saves
become visible even without a new transaction event. Preserve the existing
legacy open/save calls for other clients; an external legacy Reopen bypasses
this Editor's prompt and invalidates its leave intent and history. A draft
that became stale is available to inspect or copy, and cannot submit against
the replacement, including a node with the same GUID.

The final close check observes the latest host state. OS window teardown and
host dispatch have no shared atomic boundary. An external edit can still
land after that check before an owned Runtime exits. Keep that interval
small, report this limit in the authorization, and add no lease or new process
shutdown contract. An in-host guarded Reopen does have an atomic boundary
with other clients' edits and opens.

### R4 — Failure, unknown results and legacy hosts

Keep the window and any valid draft on definite refusal, while reporting
the source and appropriate recovery. On stale refusal refresh before
another decision. On write or source failure, retain the old document and
offer a new attempt once the fault is corrected. A known not-sent request
and a potentially executed request have different outcomes. On disconnect or
an invalid success receipt after handoff, keep the original connection or
protocol error and mark the operation result unknown. Stop the automatic
leave continuation and never replay the mutation automatically.

**Check Again** reads from a live connection to the same authenticated
host launch. It may make a read-only probe using the existing session
credentials after a disconnect. For unknown Save, proof that the reviewed
revision is persisted in the same incarnation and is still current permits
a new leave decision. Any other result shows what could be observed and
keeps the original operation unconfirmed. For unknown Reopen, observe a
replacement under the normal document lifecycle without attributing it to
the lost request. A changed revision or incarnation makes the old guard
unusable. If the requested pair is still current and the result remains
unknown, keep the write blocked until the prior attempt is known settled.
A read from a different host launch
cannot reconcile either mutation. Leave, new writes and recovery controls
continue to respect the current connection and document lifetime; late
callbacks cannot dismiss a new dialog or close a new window.

When state is unavailable, show it as unknown. Keep a confirmed no-document
state distinct. Offer **Close Anyway** with an explicit consequence prompt
when the user chooses to exit despite unknown persistence; a reachable
host is rechecked before that decision is consumed, and newly verified
changes return to the normal prompt. Disable guarded Reopen when the host
lacks the capability, and report why. Legacy explicit toolbar Save remains
available with its existing semantics and cannot certify a guarded leave.

## Architectural impact

[ADR-0166](../adr/0166-guarded-authoring-save-and-reopen.md) records the
additive shared API and saved-state ownership as an extension of ADR-0130.
Use existing Authoring dispatch, C# SDK and Editor worker boundaries. Store
one clean content anchor and the current dirty comparison per open document.
Compare at most once per accepted current revision and use at most one
background state read per 250 ms in the existing worker poll. Explicit
leave and write settlement can request a fresh read. No new frame-path work,
thread, dependency, scene format or Runtime projection control is planned.

## Alternatives, verification and open decisions

An Editor-only flag misses external edits and saves. Apply revision measures
the projected snapshot. Reading revision then calling legacy Open has a race.
The shared guarded option covers these cases while leaving an explicit
window-teardown limit for J3. The [Plan](../plans/0112-unsaved-document-protection.md)
maps R1–R4 to host, client and Windows 100% checks. J1–J4 require joint
review, including the draft choice, legacy and unknown handling, and the
close race boundary.
