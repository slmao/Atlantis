# ADR 0171 — Host-Owned Recovery Copy for Hand-Authored Scenes

- **Status:** Proposed
- **Date:** 2026-11-21
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending
- **Related Spec:** [Spec 0122](../specs/0122-hand-authored-scene-recovery.md)

## Context

[ADR-0129](0129-authoring-document-ownership.md) and
[ADR-0166](0166-guarded-authoring-save-and-reopen.md) give Authoring
ownership of validated document state and the formal saved anchor.
[ADR-0144](0144-correlated-authoring-apply-outcomes.md) separates document
commit from Runtime reload. The host must retain committed edits across
process loss while preserving both those boundaries.

## Decision

### D1 — Owner, slot and publication

Authoring captures immutable, canonical scene source for a dirty committed
revision on its existing dispatch thread. It pairs that snapshot with the
current document token and the fingerprint of the raw formal source bytes
read or successfully saved. Use a host-provided fingerprint callback over
those same bytes and retain its evidence. Preserve the independent canonical
saved anchor from 0166. Authoring takes no Windows dependency. Apply does
not participate.

The Windows Runtime executable hosts a lazy recovery store scoped to its
local OS user, under LocalAppData/Atlantis/SceneRecovery/v1. Derive the slot
key from the full canonical normal-file source path and scene GUID, using
consistent Windows alias handling for supported fixed-volume paths. Fail
closed on ambiguous identity, escaping paths, reparse points, hard links or
a store that cannot be safely accessed. Use an OS-held exclusive per-slot
lease that releases when the owning host exits, including abnormal exit.
Do not derive a path from a client string. A lease only covers recovery
storage. It grants no permission to write the formal source.

The slot is one versioned envelope containing the scene and source key,
opaque copy ID, capture time and revision, source SHA-256, payload SHA-256
and length, and the canonical scene source bytes. Copy IDs are minted per
published snapshot using the existing GUID mint. A copy's original revision
is descriptive on a different host launch. Check identities, digests, limits,
parse, scene mapping and authoring validation before use. Cap the source and
payload separately at 16 MiB. Unsupported paths or larger scenes remain
editable with recovery unavailable. Use the existing Windows SHA-256
facility and no new dependency.

Coalesce capture work to one active write plus the newest pending snapshot.
A single lazy executable-private worker prepares, flushes and closes a
uniquely owned temporary file in the slot directory. Publish with a
same-directory atomic replacement after checking that the prepared slot
epoch and document identity still match at the existing authoring
settlement point. The worker never mutates a live document or Runtime state.
A superseded completion cannot publish after Save, reopen, clean Undo or
Discard. Failure preserves the last complete slot. Temporary files are
never candidates. There is no recovery work in RuntimeHost, rendering,
or a Runtime that has never opened a supported authoring document.

Only a live copy of the current document is retired on successful formal
Save or a return to the exact saved content. Failed Save has no retirement
effect. A prior unresolved copy survives ordinary Save, Open and
without-saving leave until explicit Discard or successful adoption.
A same-host Reopen with an outstanding dirty checkpoint freezes its
capture and offers only the last completely published copy if available.
An adopted copy is live for the restored incarnation and retained on disk
until a later checkpoint, formal Save, clean Undo or an explicit pending
Discard. If retirement fails, expose that status. A later launch finding
official bytes identical to the payload reports the copy as already saved
and offers Discard. It does not restore it as new work.

### D2 — Shared recovery decision

Extend [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)
and 0166 additively over atlantis.remote/1. Dispatch through the existing
Authoring owner. Clients choose among these operations without passing a
path or a source payload.

| Method | Request | Success |
| --- | --- | --- |
| authoring.recoveryStatus | Empty for status. An exact copy ID for preview. | Capture state and latest protected revision where known, pending/live copy metadata where present, and a bounded copy preview plus a current source comparison when requested. Status refresh does not read or hash the file. |
| authoring.restoreRecovery | Current document token and copy ID. | Reviewed copy ID and new dirty document state after validated adoption with a fresh lifetime. |
| authoring.discardRecovery | Current document token and copy ID. | Reviewed copy ID and unchanged document state after removal of the single pending slot. |

A preview request is read-only and refuses an obsolete copy ID. A decision
requires a matching pending copy, source identity, owner lease and document
token. The host binds the reviewed copy ID to its validated slot identity
and exact envelope bytes. Discard checks that identity again before removal.
An out-of-band slot change does not authorize deletion. Restore additionally requires a clean current document and the exact
raw source digest. Require the live document's raw saved anchor and a fresh
read of the official source to match the copy's recorded base. The source
may have changed and changed back while the current document was open.
Incomplete or unsupported files and changed source bytes refuse Restore.
Keep refusals distinct for unsupported recovery, no document, busy or
unavailable storage, stale document, stale copy, dirty document, changed or
unreadable source, invalid copy and I/O failure. Never present a missing or
invalid receipt as a successful action. Ordinary Save/Open and Apply retain
their legacy contracts. Old hosts' exact unknown-method responses map to
unsupported. Clients never substitute unconditional Save or Open for a
refused or uncertain recovery mutation.

## Consequences and alternatives

Capture is independent of Runtime reload and of which client committed the
edit. Storage is disposable and local, with one candidate rather than a
durable audit log. An unresolved slot or an active competing host can leave
new revisions unprotected. The latest accepted edit and latest durable copy
can differ during I/O or a crash. Atomic slot publication protects process
crash recovery. Machine power loss and external source races after the last
read are outside the guarantee.

Editor-owned snapshots miss the authoritative transaction boundary.
Overwriting official source makes both conflict and recovery ambiguous.
Full synchronous file streaming on the authoring thread would make edits
wait for storage. Full revision history adds retention, identity and product
surface beyond this slice.
