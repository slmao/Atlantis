# ADR 0189 — Document-Guarded Node Name Batch

- **Status:** Accepted
- **Date:** 2026-11-22
- **Deciders:** Atlantis maintainer
- **Acceptance:** Maintainer, 2026-11-22, [joint approval of `0ece893c`](https://github.com/slmao/Atlantis/pull/445#issuecomment-7081010194)
- **Related Spec:** [Spec 0141](../specs/0141-hierarchy-batch-rename.md)

## Context

[ADR-0142](0142-shared-node-rename-and-history.md) exposes names through
the existing authoring transaction. It has no host precondition associated
with a reviewed list of names. [ADR-0162](0162-revision-guarded-authoring-property-batch.md)
guards a closed, property-only batch. Names are node metadata. A read
followed by a plain name transaction permits an intervening edit or
same-GUID replacement.

## Decision

1. Add one closed leading command
   `RequireNameBatchDocument{expectedDocument: DocumentToken}`. Accept it
   only as command zero followed by 1–64 ordinary `SetNodeName` commands
   addressing distinct non-nil GUIDs. No other commands or guards are
   admitted, including another name guard. Carry the existing token's
   document ID and revision using the existing token encoding. Require
   exact guard and token members, a canonical non-nil ID and a positive
   revision. The guard receives the first ordinary ticket, counted in the returned range,
   and emits no event.

2. In AuthoringSession's existing serialized transaction admission,
   preserve `NoDocument`. A malformed guarded shape or unsupported
   source provenance returns `InvalidGuardedNameBatch` at the first
   name guard's ticket. For hand-authored scenes require an exact current
   document token. Mismatch returns `NameBatchStaleDocument` at the
   guard's ticket. Validate each name and the candidate with existing
   rules and ticket attribution. Commit the whole copy once, advance
   one revision and publish the existing name events in command order.
   Any refusal leaves the document, revision and event stream untouched.
   The shape and behavior of existing transactions remain as specified.

3. Advertise `supportsGuardedNameTransactions` in the current
   `authoring.document` descriptor only where this guard is supported.
   Only literal true on a valid current descriptor with supported
   provenance enables guarded clients. Absence, false, malformed values
   and no document leave the batch unavailable. Keep legacy descriptor
   reads working. Reset discovery on reattach/reopen. Older hosts fail
   closed when decoding the unknown command before submission. Neither
   the Editor nor another client falls back to unguarded writes. Older
   name-capable clients keep their existing reads, commands and events
   on the upgraded host.

4. Carry the shape and refusals through existing Connection and Remote
   codecs, native `scene tx`, the shared SDK authoring surface where
   exposed, and MCP's exact `authoring.submit_transaction`. Native
   `scene tx` can use a leading `require-name-document <document-guid>
   <revision>` followed by ordinary rename lines. Publish no new MCP
   tool, typed World API, or separate protocol. Apply remains a
   separately tracked request under [ADR-0144](0144-correlated-authoring-apply-outcomes.md).
   Pattern generation, preview, selection and history belong to the
   client. Names remain labels under ADR-0142.

## Consequences

A stale preview cannot write through a competing commit. A guarded
transaction can be refused after an unrelated edit anywhere in the
document. A new command, capability and refusals require conformance
coverage. Existing source files and Runtime scene artifacts need no
migration. Client verification is still needed after acceptance because
another writer can advance the document before readback or Apply.

## Alternatives considered

A client-only preflight leaves a race. An optional parameter on the
existing RPC risks an older host ignoring it. A separate rename RPC would
duplicate transaction admission, event and failure semantics. Widening
the property guard would relax an existing closed shape and obscure
capability discovery for old and new name batches.
