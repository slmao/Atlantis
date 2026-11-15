# ADR 0162 — Revision-Guarded Authoring Property Batch

- **Status:** Accepted
- **Date:** 2026-11-15
- **Deciders:** Atlantis maintainer
- **Acceptance:** The maintainer accepted this ADR in [PR #377 at `3f66ff58`](https://github.com/slmao/Atlantis/pull/377) on 2026-11-15.
- **Related Spec:** [Spec 0106](../specs/0106-scene-multiselect-batch-edit.md).

## Context

Authoring validates a transaction on a document copy and commits it
once. A plain `SetProperty` transaction does not express which revision
the caller observed. A read before submission cannot prevent the next
writer from changing values, removing a component or replacing a node
with the same GUID. [ADR-0160](0160-mutable-adjacent-sibling-order.md)
and [ADR-0161](0161-atomic-sibling-slot-move.md) use host revision
preconditions for individual ordering commands. Batch property
assignment needs the same admission property across several writes.

## Decision

1. **Guard the existing transaction.** Add one closed
   `RequireDocumentRevision{expectedRevision}` command. Its only valid
   shape is the first item of a property-only transaction with one to
   64 following `SetProperty` commands. The writes share one component
   type and one leaf field ID and each addresses a distinct node GUID.
   Values can differ by target so replay can restore distinct originals.
   The guard has a normal command ticket and emits no event. The
   returned ticket range includes the guard and every write. This
   supplements [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)'s
   closed command set and [ADR-0136](0136-structural-edit-records.md)'s
   property-history replay for this batch type.
2. **Admit or refuse the whole batch in AuthoringSession.** Preserve
   the ordinary `NoDocument` refusal. If the guard appears, validate the
   guarded shape and compare its revision with the authoritative
   document revision at the existing serialized admission point.
   Guard-only, duplicate or misplaced guard, duplicate target,
   cross-field, over-limit and mixed command batches return
   `InvalidGuardedPropertyBatch` against the first guard's ticket. A
   stale revision returns `PropertyBatchStaleRevision` against that
   ticket. Then run the existing property and candidate cook
   validation, with its existing refusal codes and ticket attribution.
   Success advances one revision and publishes the ordinary
   `PropertyChanged` events in write order. Any refusal leaves the
   document, revision and event stream unchanged. The guard uses the
   existing authoring access and provenance rules for `SetProperty`.
3. **Use a read-only capability on the open document.** Extend
   `authoring.document`'s existing `DocumentInfo` with the optional
   boolean `supportsGuardedPropertyTransactions`. A capable authoring
   endpoint advertises true. Only a literal true on a valid descriptor
   enables guarded submission. Absence, false or a malformed capability
   leaves guarded writes unavailable, while the otherwise valid
   document descriptor remains readable. A null document cannot
   establish support. Use the capability and the expected revision
   from the same current connection and open document. Discard
   positive discovery on reopen or session change. Encode the member
   only when advertised. Keep legacy descriptor construction. Older
   decoders continue reading their named members. Existing transaction
   commands and event/filter encodings keep their shapes.
   An old Runtime rejects the unknown command while decoding the
   complete transaction before submitting it to AuthoringSession,
   even if a client bypasses discovery. Upgraded clients send the
   guard only on a positively identified endpoint and never resend
   its writes without the guard. Report unsupported capability before
   submission separately from an unknown outcome after a request may
   have reached the host.
4. **Reuse the client surfaces.** Native `scene tx` accepts a leading
   `require-revision <revision>` line followed by normal `set` lines.
   Managed callers use the typed guard with `SubmitTransaction`.
   MCP's existing exact `authoring.submit_transaction` accepts the
   same command, settled through `authoring.drain_failures`. The
   existing unguarded `set_properties` convenience remains available.
   There is no new RPC method or event kind. Editor undo records and
   draft safeguards stay client-local. Apply is a separate tracked
   request with the existing [ADR-0144](0144-correlated-authoring-apply-outcomes.md)
   semantics. The revision guard applies to the property transaction.
   The Apply receipt identifies the actual snapshotted revision.

## Consequences

- A batch retains existing property addressing, transaction tickets,
  validation, and change stream. Neither the saved scene nor Runtime
  projection gains a batch format or selection state.
- A concurrent edit after preflight can refuse the whole batch even
  if it touched an unrelated field. Refresh and explicit retry use
  a new revision. No new lock or finer-grained server version is needed.
- A new command, positive capability, and two refusal values require
  native/managed conformance and legacy-peer checks. Existing clients
  retain the unguarded behavior of ordinary property transactions.

## Alternatives Considered

- A client preflight followed by plain `SetProperty` leaves an
  admission race and could overwrite an external edit or replacement.
- An optional revision parameter on the existing RPC is unsafe for
  raw calls to older peers because their dispatcher reads `commands`
  and can ignore the extra member.
- A dedicated batch RPC adds a separate property mutation entry point
  and duplicates the transaction's admission, failure and observation
  surface. A closed guard retains the existing boundary.
