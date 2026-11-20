# Spec 0121 — Editor Tasks & Issues for the Current Session

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-20
- **Related Plan:** [Plan 0121](../plans/0121-editor-tasks-and-issues.md)
- **Approval:** J1–J4 approved and S2–S4 authorized for design [PR #407](https://github.com/slmao/Atlantis/pull/407) at `a44068b5`; maintainer merge `1249f3e4`. No ADR or shared interface.
- **Related ADRs:** None proposed. See Architectural Impact.

## Summary and scope

Add one **Tasks & Issues** panel in the Editor. A row answers which operation
affected which asset or document, what is known about its result, and where
the user can inspect or act next. Cover asset import, rebuild/reimport and
publication, including the existing managed texture and material publication
commands, plus scene Apply. A read-only preflight or reference lookup does not
create a task. Keep existing local controls and inline feedback.

The panel shows operations initiated by this Editor in its current Runtime
session. It keeps a bounded in-memory list. Cross-session history, discovery of
other clients' tasks, persistent notifications, generic error logging, job
scheduling, cancellation and new recovery commands are out of scope.

## Requirements

### R1 — Correlate existing operation evidence

Feed the panel from typed admission, receipt, status and session events at the
existing Editor owners. Capture operation kind, target and the current
worker/session at intent creation. Use asset GUID when known, otherwise use the
existing import or material target identity (kind, mode and submitted name).
Use the authoritative returned root or saved-material GUID only from a
matching reply. For scene Apply, record the authoring document identity and
lifetime, its revision if known, and the accepted Apply ID and receipt revision
when available. Apply targets the whole document. Current selection cannot
assign causality to a node.

Use a local attempt key while a request is awaiting admission, then bind its
typed job or Apply ID to that same row. Require matching operation, target,
receipt and owning lifetime before updating it. A request refused before
admission yields its own refusal row without a server job ID. Use the existing
Apply controller's actual issued attempts. Coalesced edits create a row only
if they cause another Apply to be sent. Apply notices about external receipts
may remain in the existing status UI. They cannot settle a local row. A latest
or id-less reply without proven ownership cannot establish or settle a row,
even if its name or GUID matches. Keep accepted IDs and typed diagnostics
when lookup becomes unavailable. Distinguish proven unsent/canceled from a
possibly sent request with a lost receipt. If a path already loses this
distinction, keep uncertainty explicit and reuse only evidence it actually has.

Order rows by local start sequence. Keep up to 32 settled entries per session
in addition to already outstanding tracked attempts. Retire oldest settled
entries first. Status polling updates a row in place. It does not bring that
row to the top or change selection. On transport loss, keep the disconnected
session's rows readable and copyable. Retire their navigation/actions and mark
unresolved attempts unknown when they may have been sent. Keep proven results.
Clear rows on replacement of the worker/session or closing the Editor. On
document replacement within the same session, disable old document navigation
and prevent its replies from updating the new document. A retained Apply whose
final result is unobservable is unknown, with its former document identified.

### R2 — Present operation result separately from what is active

Use existing domain states with concise labels. Include pending/running,
refused, failed, succeeded, superseded and **Outcome unknown** wherever
supported. Distinguish a waiting Apply held by Pause from a failed one.
Render source/registration settlement and workspace-repair flags where
available. A failure after source commit should direct the user to the
existing scoped recovery entry when the owner still permits it.

A texture or material job that has published a named generation shows
**Published · restart required** when the known active generation differs.
Show the published generation and current active generation independently.
A proven match can show that the publication is active. When the active
generation is unconfirmed, show **Published · activation unconfirmed**. Avoid
claiming activation from job success or Reload view. A fresh Editor after
explicit restart can confirm activation through the existing Assets catalog
and active-generation status. The previous session's row does not migrate.

For a proven document commit followed by Apply refusal or reload failure,
show **Edit committed · Apply failed**. Explain that editing state and Undo/Redo
remain in the document. For recoverable reload failure the prior Runtime
projection remains active. A fatal stop gives no promise of a usable Runtime.
Save status continues to come from the existing document state. An Apply row
without a proven commit shows only the Apply result. A missing acceptance or
lookup reply supplies neither a failure nor a success. Unknown rows guide the
user to inspect the target, retained IDs and relevant document or generations
before a fresh explicit action. No automatic retry, automatic Resume, Apply,
restart, or polling is added by the panel.

### R3 — Locate and copy without changing operation state

Open a nonmodal Tasks & Issues panel from the main Editor status bar, using
[0093](0093-editor-visual-layout.md) styles. Give the rows operation, target and
status, with a selected-row detail showing diagnostics and next steps. Rows
accumulate while the panel is closed and remain available when reopened. Status
updates never steal focus. Use keyboard navigation, accessible action labels,
and scrollable, selectable detail text for long paths and messages. Keep the
existing status/diagnostic controls and avoid duplicating inline controls in
the panel.

**Locate** uses exact GUID lookup and the existing Assets reveal path against
the attached, usable catalog. Preflight both the row and destination before
changing selection or filters. If a just-published target is absent from the
current catalog, explain that explicit activation is needed. If the GUID is
unknown, missing or stale, retain current selection and show why it cannot be
located. Never locate by display-name match or borrow the current selection.
For Apply, focus the current matching document's status or existing Apply
surface without selecting a node. Check the document lifetime and current
owner before and after any deferred navigation. A previous document's row can
still be copied while retained in this session. Navigation cannot trigger
writes. Recovery guidance names the existing commands and their prerequisites.
For unknown outcomes, offer inspection and copy only.

**Copy diagnostics** uses the normal clipboard action and includes operation,
target, available receipt ID/revision, published versus active generation
where relevant, typed failure stage/code and existing diagnostic text. Preserve
absent or unknown facts as such. Include actionable context such as the
existing source path or recovery notice. Exclude session tokens, raw
transport envelopes, full document state and unrelated log contents. Report
clipboard failure in the panel without changing the row outcome.

## Architectural impact

None. The panel is an Editor-local projection of existing shared asset and
[0083](0083-authoring-apply-outcomes.md) Apply outcomes. Existing Worker and UI
thread ownership stays in place. No protocol, Runtime contract, persistence,
dependency, scheduling or rendering change is required. The accepted Apply
semantics in [ADR-0144](../adr/0144-correlated-authoring-apply-outcomes.md)
continue to govern status and recovery.

## Alternatives and verification

Linking to individual status strings would lack stable receipt identity and
lose failure evidence when selection changes. A new shared task service or
history would expand ownership and recovery scope. Use the existing typed
evidence and a bounded current-session projection. See Plan 0121 for Windows
100% acceptance, controlled uncertainty tests and the joint decisions.
