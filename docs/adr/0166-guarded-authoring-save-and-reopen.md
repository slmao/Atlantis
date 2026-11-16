# ADR 0166 — Guarded Authoring Save and Reopen

- **Status:** Proposed
- **Date:** 2026-11-17
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending
- **Related Spec:** [Spec 0112](../specs/0112-unsaved-document-protection.md)

## Context

[ADR-0129](0129-authoring-document-ownership.md) gives Authoring ownership
of the document and its atomic source write. [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)
defines shared session calls, including unconditional Save and Open.
[ADR-0144](0144-correlated-authoring-apply-outcomes.md) identifies revisions captured
for the Runtime; it does not describe source persistence. Multiple
connections share a document, and a source may reopen with the same scene
and node GUIDs. A client-side check followed by an unguarded call would
allow a concurrent edit to be written or discarded without review.

## Decision

Extend ADR-0130 with an additive shared state read and two guarded
variants of the current session calls. Use ordinary Authoring host dispatch
for each complete read or compare-and-act operation. The document stays
owned by Authoring. Runtime still owns the projection.

### State and lifetime

The existing session revision advances on every accepted document edit
and successful open as defined by 0083. Its value on successful open is
the document's `openRevision`. The pair
`{openRevision, documentRevision}` identifies a current document in the
authenticated host launch. Across host launches the pair is incomparable.
Clients bind it to the session launch identity already used to connect;
an Editor window also binds any pending continuation to its own lifetime.
The pair is an observation and mutation precondition, not a secret.

On open, retain canonical lossless scene source bytes for the validated
document and initialize `lastPersistedRevision` to the new revision. On
successful ordinary Save, replace that anchor with the exact canonical
bytes written and set `lastPersistedRevision` to the written revision.
Legacy Save and Open update this same state. Failed operations, Save As,
New Scene and Apply leave it intact. Derive `dirty` by comparing canonical
current content with the anchor, including node order, identities and
serialized property values. This is content equality against the last
observed disk state. `lastPersistedRevision` records the successful read
or write and can differ from the current revision of clean content after
Undo. Undo, no-op transactions, external saves and imported transient
edits use the same rule. This does not watch for direct file edits outside
Authoring or certify what another process may have written after the read or
write. Cache the comparison for the current revision and invalidate it
on a new revision or anchor change. Retain only the current anchor and
comparison, with no per-operation log.

### Shared operations

Use typed Connection and C# SDK value APIs over these additive methods in
`atlantis.remote/1`. Each method uses the usual strict request shape and
method-local `ok`/`err` envelope.

| Method | Request | Success | Shared refusals |
| --- | --- | --- | --- |
| `authoring.documentState` | Empty | State or `null` for no open document | Standard RPC refusal if the host serves no Authoring |
| `authoring.saveIfCurrent` | `{openRevision, documentRevision}` | State at successful atomic write | `NoDocument`, `StaleDocument`, `ImportedSceneNotSavable`, `WriteFailed` |
| `authoring.reopenIfCurrent` | `{openRevision, documentRevision}` | State after successful source reload | `NoDocument`, `StaleDocument`, `SourceUnreadable`, `SourceParseFailed`, `SourceInvalid` |

State has the existing canonical `scene` (GUID), `source` and
`provenance` encodings, both revisions, `lastPersistedRevision` and
boolean `dirty`. Revisions are positive unsigned 64-bit integers and
`openRevision <= lastPersistedRevision <= documentRevision`. Use the
existing exact unsigned integer encoding; an invalid or inverted token
uses RPC `BadRequest`. When
`lastPersistedRevision == documentRevision`, `dirty` is false. Imported
state keeps the open revision as its persisted anchor. Save echoes the
requested pair, a matching persisted revision, hand-authored provenance
and clean state. Reopen returns a clean state for the same scene and
source with all three revisions equal to the previous document revision
plus one. Ship only a source reopen of an already open document. Initial
Open and clients with no current document retain their existing paths.

The two mutation handlers check current identity and revision before disk
I/O, then use existing atomic write or validated source reopen without
interleaving another authoring request on the same host. A refusal does
not alter the document, anchor, history events or Apply state. A successful
Save changes neither document revision nor document lifetime. A successful
Reopen publishes the existing `DocumentReopened`, advances the lifetime
and leaves accepted Apply snapshots and projection untouched. There is no
new file layout, lock, event or connection type.

Expose CLI `scene document-state`, `scene save-if-current <open-revision>
<document-revision>` and `scene reopen-if-current <open-revision>
<document-revision>`. Expose MCP exact tools
`authoring.document_state`, `authoring.save_if_current` and
`authoring.reopen_if_current` via the managed SDK. Use the established
nonzero CLI / MCP tool-error semantics for refusals and transport errors.
Do not add an implicit open, drain or apply to these calls.

### Compatibility and unknown results

Existing payloads, codes and semantics stay intact. Default native
interface implementations decline new calls with `Unsupported`. Clients
also map the exact existing `BadRequest` / `unknown method <name>` error
for each new method to `Unsupported`, separately from no Authoring, no
document, malformed responses and connection failures. A closed contract
validates revision ranges, nullability and success invariants. Guarded
mutation clients verify the requested pair where the success permits it;
Editor also checks known source identity, the session and its dialog
lifetime before acting on a reply.

After handing off a mutation, a lost connection or malformed success is
an unknown outcome. Preserve the original transport/protocol failure
category and include the operation and requested pair in the diagnostic.
Never fall back to the unconditional call. Reading the same launch can
establish current persistence or that the old incarnation has been
replaced. It cannot attribute another client's matching result to the
lost request. An unknown write is never automatically retried.

## Consequences and alternatives

Clients can detect exact Undo-to-saved even after other clients modify the
document. Authoring retains one canonical scene representation and does
one comparison per changed revision that is observed. Equality ignores
formatting and comments consumed by the existing parser. Apply outcomes
remain independently observable. Guarding an in-host Reopen is atomic
with shared edits. The Editor's last close check and OS teardown remain
separate, as detailed in Spec 0112.

A counter-only saved test would mislabel Undo and equal writes. A
file hash or watcher would add a separate disk-ownership policy.
Changing legacy methods in place would break unconditional clients.
A session lock or persisted mutation ledger would exceed the close and
reopen requirements; this design retains one current observation and
explicit unknown-result handling.
