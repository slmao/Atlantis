# Spec 0122 — Automatic Recovery Copy for Hand-Authored Scenes

- **Status:** Approved
- **Author:** Codex
- **Created:** 2026-11-21
- **Related Plan:** [Plan 0122](../plans/0122-hand-authored-scene-recovery.md)
- **Approval:** J1–J4 approved and S2–S4 authorized for [PR #409](https://github.com/slmao/Atlantis/pull/409) at `2f3a2642`. ADR-0171 accepted. Maintainer merge `20315fef`.
- **Related ADR:** [ADR-0171](../adr/0171-authoring-recovery-copy.md) (Accepted)

## Summary and motivation

Protect committed, unsaved edits when an Editor and its owned Runtime exit
abnormally. [0112](0112-unsaved-document-protection.md) guards ordinary leave
operations, but its saved-content anchor and the open document live in
memory. A failed Runtime reload still leaves the accepted edit in the
document under [0083](0083-authoring-apply-outcomes.md). Recovery must be able
to retain that edit independently of Apply and of the official source.

## Goals and non-goals

Cover an already saved, hand-authored scene in the Windows authoring workflow.
Keep one local recovery copy per original scene. On a subsequent open offer
Preview, Restore or Discard, with a safe default and clear conflict feedback.
Keep Save and Apply explicit.

Out of scope are imported scenes, unsubmitted text and live Gizmo previews,
multiple versions, project or cloud sync, cross-machine recovery, merging
conflicting contents, automatic source writes, and recovery of Undo/Redo or
Runtime state. The recovery preview is read-only text based on scene source.

## Requirements and proposed design

### R1 — Capture committed authoring content

Use the Authoring owner's validated document and lossless scene serialization
from [0065](0065-authoring-data-layer.md), including stable scene and node
GUIDs, node order, component values and asset references. Queue the latest
dirty committed revision after a successful transaction, including Undo/Redo
and transactions from attached clients. A refusal, a clean document and
uncommitted input do not create new copies. Report current coverage by
revision and separately from dirty, saved and Apply status. If capture is
pending or fails, show which earlier revision is recoverable when known.
A failed or pending Apply never prevents a committed revision from being
checkpointed. A checkpoint never causes Apply.

Use the host-local, versioned slot in ADR-0171. The official source, catalog
and build inputs receive no recovery writes. Publication atomically replaces
the complete slot while preserving the last complete copy on failed writes.
The crash guarantee covers the most recent successfully published checkpoint.
Edits accepted after that checkpoint can be lost if the process exits before
publication. Recovery failure leaves normal editing, Save and Apply available.

### R2 — Inspect, resolve and preserve the right copy

Open the official source through the existing guarded startup or **Reopen
from Source** route in 0112. When a pending copy exists, offer it in the
ordinary Editor once the matching source is open and any 0112 leave dialog
has settled. The dialog shows scene,
timestamp, checkpoint revision, source comparison and status. **Preview**
is non-mutating. Show counts for the recovery and current source and a
bounded before/after excerpt of actual authored source. Use canonical text for
comparison and label the current source as such. If they differ, display the
first changed region, up to 20 lines per side and 256 Unicode scalar values
per line, with explicit truncation. Report an unreadable source separately.
Neither the entire payload nor a host storage path is sent to clients.

| Choice | Effect |
| --- | --- |
| **Restore to Document** | Validate the exact pending copy again. Require a clean, same-incarnation authoring document and the official source fingerprint recorded at the copy's base. Install the validated copy as a dirty authoring document with a fresh lifetime, retain the saved anchor from the official source, emit the existing Reopen event once and retain the recovery slot. The Editor reuses existing draft, selection and history invalidation. The old Runtime projection and Apply receipts settle independently. Subsequent Apply or Save requires an explicit user action. |
| **Discard Recovery Copy** | After explicit confirmation, remove only the exact pending copy under its guard. Leave the official bytes, current authoring document, history and Runtime projection as they are. This is available on source conflict while a matching document is open. If the file changed after the live document opened, explain that a separate **Reopen from Source** reads the updated file. |
| **Decide Later** | Escape, closing the dialog and the default action leave the copy available under Scene Files. Normal authoring remains available. Indicate that new changes cannot replace the pending copy until it is resolved. |

Guard both mutations with the current 0112 document token and the reviewed
copy ID. A pending copy can survive a re-open, close without Save or Runtime
restart. Unresolved candidates take priority over any new checkpoint. After
Discard, any already-dirty live document may be captured as a new copy with
a new ID, independently of the finished Discard. Never revive the discarded
payload as a new checkpoint. If two Runtime hosts address the same slot,
only its lease holder can capture or resolve it. A host that cannot acquire
the lease reports recovery unavailable while ordinary authoring continues.

### R3 — Detect conflicts and failures without writing the source

The base fingerprint identifies the exact official bytes read by Open or
written by successful ordinary Save. Restore requires both the live saved
anchor and the current disk contents to match that base. A source change, including an external
client Save, makes the pending copy ineligible for Restore. Check current
source bytes during Preview and immediately before the host installs a
restored document. Also revalidate the source and copy identities, the copy
payload, and the current document token. A source conflict can be inspected
or discarded. There is no merge or force-restore.

If the source cannot be opened at startup, preserve the copy and report the
existing Open failure. If a supported host cannot read the store, show
recovery unavailable. A corrupt or unsupported copy never reaches Authoring.
Where its identity and slot can be safely established, offer explicit Discard
with a diagnostic. Otherwise leave it in place and report the failure.
On stale evidence refresh and require a fresh decision. When a request could
have executed but the reply is lost or invalid, show outcome unknown with
the original failure category. Do not replay it automatically. Query the same
host launch to establish current state and require a new explicit decision
for any remaining recovery action.

Restore and Discard never write the formal source. A direct external write
after the last source check remains possible. Recovery establishes the
document against the observed source and does not add a cross-process lock
for ordinary Save. A later explicit Save follows the existing save contract.
Apply recovery and explicit save/reopen guards from 0083 and 0112 still apply.

### R4 — Use the shared authoring surface

Expose one status/preview query and two guarded mutations across native
Connection and Remote, CLI, SDKs and MCP using ADR-0171. Client support is
additive. Old clients keep their existing behavior on new hosts. A new
client on an old host reports recovery unsupported and keeps normal
save, close and reopen. The Editor uses the shared calls through its worker
and follows [0093](0093-editor-visual-layout.md) for the dialog. Retire stale
replies on document or worker replacement and keep recovery and existing
unsaved-input feedback distinct.

## Architectural impact

[ADR-0171](../adr/0171-authoring-recovery-copy.md) records the authoritative
snapshot, executable-private persistence worker, recovery file format and
additive Authoring contract. Authoring remains the document owner and uses
existing validation and source mapping. The Runtime executable owns the
Windows store. The Editor remains a shared client. Cookers, RuntimeHost,
renderer, RenderGraph and the ordinary scene source format are unchanged.

## Alternatives considered

An Editor-owned file copy would miss edits committed by other clients and
could not authoritatively separate successful transactions from drafts.
Saving in place would alter the official source and could overwrite the
other client's work. Keep one host-owned checkpoint with explicit adoption.

## Verification

[Plan 0122](../plans/0122-hand-authored-scene-recovery.md) maps host and client
checks to R1–R4. The Windows 100% acceptance paths are abnormal exit after a
published checkpoint, including a failed Apply, and safe Discard after a
second client changes the official source. Both assert official bytes and
Runtime behavior independently.
