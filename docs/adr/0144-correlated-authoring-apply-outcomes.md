# ADR 0144: Correlated Authoring Apply Outcomes

- **Status:** Accepted
- **Date:** 2026-11-07
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-11-07; [joint ruling](https://github.com/slmao/Atlantis/pull/329#issuecomment-6702522226) at `1a65902`; #329 manually merged
- **Related Spec:** [Spec 0083](../specs/0083-authoring-apply-outcomes.md)
- **Related ADR(s):** Partially supersedes [ADR-0130 D1/D3](0130-authoring-access-interface-and-module-boundary.md), [ADR-0131 D3](0131-authoring-apply-and-runtime-scene-reload.md), [ADR-0106 D3](0106-attachable-runtime-transport-and-control.md) as extended by ADR-0130, and [ADR-0135 D3/D5](0135-two-document-edit-and-apply.md). Keeps [ADR-0129](0129-authoring-document-ownership.md), [ADR-0125 D2](0125-out-of-process-editor-platform-and-uniform-client-interface.md) and [ADR-0136](0136-structural-edit-records.md).

## Context

An apply can be accepted before Runtime dependency/resource preparation refuses
the reload. ADR-0131 D3 intentionally reports only a successful generation and
logs failures. #328 disclosed that a client can then wait forever, or confuse a
different client's successful reload with its own. The document transaction has
already succeeded; undoing it would violate the two-document ownership model.
The Runtime has one latest-wins pending slot and a paused swap point. These are
useful bounds and should not become a durable job queue merely to report failure.

## Decision

1. **Use an additive receipt and query.** Connection owns ApplyId, receipts,
   revisions and closed outcome/error values; add applyTracked/applyStatus to
   AuthoringConnection and carry them unchanged over Remote and all clients.
   Spec 0083 R1–R4 defines the normative fields, states and wire methods.
   The legacy apply result and Applied event remain unchanged. No new World or
   authoring event, private editor endpoint, protocol version or dependency.
2. **Authoring owns the bounded observation record; Runtime decides the result.**
   AuthoringSession identifies the accepted document snapshot; Runtime carries
   its ApplyId through the scene sink and reports a swap, failure or displaced
   pending request via the executable-private AuthoringHost. Generation alone
   is insufficient. Retain one pending plus 64 terminal metadata records and
   the last successful projection metadata, across open/disconnect, in memory.
   That projection's exact id remains queryable as Applied after leaving the
   terminal window; otherwise its retained positive evidence and UnknownApply
   would contradict one another. No successor-chain pinning. An id-less latest
   is global discovery, not recovery of a caller's lost receipt. Query is
   non-destructive. IDs are never reused; otherwise unknown/expired results and
   transport loss remain unknown, not failures manufactured by timeout.
3. **Keep latest-pending-wins and transactional reload.** An accepted replacement
   settles the old identity as Superseded; a refusal cannot replace it. Applied
   means swap committed, not that a frame was presented. Failed recoverable
   preparation keeps the old World and generation. Fatal device/lifecycle
   failures keep their existing fatal behavior and no crash-delivery guarantee.
   Accept and record replacement in one serialized operation, not a next-frame
   completion queue: multiple accepts in one batch cannot lose supersessions.
   Release displaced/finished snapshots; keep at most one unforwarded executed
   result before the next request batch. No background queue, automatic retry,
   cancellation or retained historical scene copies.
4. **Recovery changes projection, not successful document history.** On matched
   failure/refusal/supersession the editor releases its waiter, suspends automatic
   applies and lets the user repair/Undo/Redo, then explicitly apply the latest
   document. Reopen retains its existing destructive-document/history semantics
   and does not cancel an already accepted snapshot. Revision and request guards
   prevent its older outcome from settling the new document. Poll pending results
   independently of authoring events, and bypass unconditional pipelined apply
   for suspended document/history edits. Ending recovery requires the current
   projection as well as a matching successful result, not historical success.
   Disable new Gizmo previews and invalidate active uncommitted gestures locally
   while recovery leaves their World basis stale; do not restore an
   invented last-good document or World snapshot. Spec 0083 R5 owns the action
   rules. Normal successful coalescing and document/history ownership remain.

The supersession is limited: ADR-0130's session API set gains the two operations;
ADR-0106/0130's wire set gains their exact encodings; ADR-0131 D3's diagnostic-only
failure clause is replaced by correlated queryable outcomes, and its handoff
carries identity; ADR-0135 D3/D5 gain explicit failed/uncertain recovery and the
preview restriction. Existing success/legacy shapes, bake/swap/resource ownership,
pause ordering, World surface and conflict-aware authoring undo are retained.
This ADR is Proposed; none of those accepted decisions changes before joint
approval. Do not edit the earlier Accepted ADRs in place.

## Consequences

### Positive

- Editor, CLI, SDK and MCP can independently observe the same exact request;
  draining another subscription cannot steal completion.
- Failed projection no longer strands an upgraded client's in-flight state or
  erases a successful edit; continuous applies have explicit supersession.
- Existing wire clients continue using legacy methods; no extra event/filter
  migration or general job system is required.

### Negative / Trade-offs

- Upgraded clients poll; existing clients retain their failure limitation.
  Editor App uses the read-only status query as a readiness check before editing;
  older servers fail via the existing protocol rules before new local mutations.
- The store cannot survive process loss. Evicted results are unavailable outside
  the independent last projection. An unreceived acceptance reply cannot be
  replayed idempotently.
- Explicit recovery adds a user step after failure and temporarily limits Gizmo
  previews. Reopening does not itself prevent a previously accepted snapshot
  from becoming the Runtime scene; the UI must state that boundary.
- Public native interfaces, adapters, codecs, clients and tests must change
  together; Runtime source paths select the existing performance gate.

## Alternatives Considered

- **One more event:** still needs identity and retained query for late observers;
  unnecessary when the existing worker already polls.
- **Replace legacy apply or the protocol version:** wider compatibility churn.
- **Busy refusal/FIFO:** alters latest-wins and pause behavior or grows a queue.
- **Automatic rollback/retry:** can discard valid document work or repeat failures.
- **Persistent result log/cancel/last-good snapshot:** a larger recovery subsystem.
