# ADR 0164 — Guarded Duplicate Transactions

- **Status:** Proposed
- **Date:** 2026-11-16
- **Deciders:** Atlantis maintainer
- **Acceptance:** Pending joint review
- **Related Spec:** [Spec 0108](../specs/0108-batch-duplicate-subtrees.md)

## Context

The existing transaction validates a candidate document and commits
all writes together. [0102](../specs/0102-duplicate-scene-subtree.md)
composes ordinary authoring commands to copy a subtree. It checks
the observed revision on the client, leaving a gap before host
admission. Batch duplication needs a predictable append boundary.
Undo also needs to protect the observed complete copy state before
removing several nodes. The guards in
[ADR-0162](0162-revision-guarded-authoring-property-batch.md) and
[ADR-0163](0163-guarded-transform-batches.md) admit property writes.

## Decision

1. **Add a closed leading revision guard.** Use
   `RequireDuplicateBatchRevision{expectedRevision}` in
   `authoring.submitTransaction`. Encode it as an authoring command
   with `kind: "RequireDuplicateBatchRevision"` and the same unsigned
   `expectedRevision` field as the existing guards. A batch has one
   of two shapes.
   - **Append copies.** Create 1–64 nodes with distinct explicit
     nonzero GUIDs and contiguous ascending slots starting at the
     admitted document's node count. The GUIDs must all be absent
     from the admitted document. Only ordinary `CreateNode`,
     `SetProperty`, `SetNodeName` and `SetParent` follow the guard.
     Writes address a node declared by an earlier `CreateNode` in
     this batch. A parent can be an existing node or a node created
     later in the batch. Transform is implicit in `CreateNode`.
     Optional component declarations use Camera, Renderable and
     Light, each at most once per node. Limit writes to supported
     serializable leaves of those four components, with at most
     one write per canonical leaf address, one name and one parent
     write per created node. This bounds the command count by the
     supported schema and the 64-node limit. Use the usual command
     validation and final document validation.
   - **Remove copies.** Use 1–64 distinct `DestroyNode` commands
     whose GUID set exactly covers the current suffix of that size.
     Use the recorded reverse slot order in Editor Undo. The host
     checks suffix membership and the final candidate. No camera,
     parent or property writes accompany this shape.

   This guard limits the write shape and admitted revision. Editor
   checks which subtrees to copy and which suffix belongs to its
   local history.
2. **Admit and settle in AuthoringSession.** Preserve `NoDocument`.
   Use the first revision guard to select the admission shape and
   refusal family, including when a request mixes the existing
   property and Transform guards with this one. The selected shape
   refuses a guard-only, misplaced, repeated or mixed guard, an
   unsupported command shape, or a document without the capability.
   For this guard report `InvalidGuardedDuplicateBatch` at the first
   guard's ticket. Leave both earlier admission shapes and error
   attribution unchanged. Check the expected revision at the existing
   serialized admission point before slot, existing-GUID and suffix
   checks. A mismatch reports `DuplicateBatchStaleRevision` at the
   guard ticket. On a matching revision, a GUID already present,
   wrong slots or wrong suffix membership is
   `InvalidGuardedDuplicateBatch`. Existing command and cook
   failures keep their normal attribution. A refusal leaves the
   document, revision and events unchanged. Success advances one
   revision and publishes ordinary events in command order. The
   guard adds one ticket and emits no event. Settle with the existing
   complete failure drain. Apply is a separate tracked operation
   under [ADR-0144](0144-correlated-authoring-apply-outcomes.md).
3. **Discover on the current open document.** Publish optional
   `supportsGuardedDuplicateTransactions` in `authoring.document`
   for a capable hand-authored document. Only literal true on a valid
   descriptor enables this path. Omit it when unavailable. Absence,
   false, malformed values and null documents leave the capability
   unavailable. Keep the rest of the descriptor readable. Reconnect
   and reopen invalidate discovery. Existing property and Transform
   capabilities are independent. Legacy clients read the additive
   descriptor and continue their existing operations. A legacy host
   rejects the unknown raw command while decoding the complete
   transaction, before applying any earlier command in that request.
   Updated clients require positive discovery and never retry these
   writes without the guard. Report a discovery failure before send
   separately from an unknown result after possible send.
4. **Carry the same command across existing clients.** Native CLI
   accepts `require-duplicate-revision <revision>` in the existing
   `scene tx` form. The C# SDK exposes the typed command and
   capability. The existing exact MCP authoring transaction tool
   accepts it. MCP clients explicitly call the existing failure
   drain before requesting Apply, as with other exact authoring
   transactions. Editor uses the same transaction and status APIs
   and the 0107 send boundary after discovery. Preserve the wire
   envelope, existing verbs and event encodings. Keep history,
   correlation to the selection and recovery feedback client-local.

## Consequences

- Forward duplication and replay rely on one authoritative revision
  check and the standard document validation path. The new guard
  supplements [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)
  without changing the earlier guard shapes.
- Tail placement makes Undo and Redo predictable. Structural
  changes can invalidate an insertion boundary and require a new
  explicit action. Other edits can coexist when replay preflight
  still matches and there is no later revision race.
- Both command codecs and client capability paths require
  conformance and mixed-version verification. Saved data remains
  ordinary scene source.

## Alternatives Considered

- Client preflight followed by a plain transaction leaves a race
  between validation and admission. Several independent subtree
  transactions can leave a partial copy and partial history.
- Widening an earlier guard under its capability would make an old
  capable peer appear to support structural commands.
- A dedicated duplicate RPC or a general structural edit envelope
  would add another authoring surface. The closed guard carries the
  bounded creation and removal shapes through the existing one.
