# ADR 0135: Two-Document Editing — Previews on the Runtime World, Commits on the Authoring Document, Automatic Apply

- **Status:** Proposed
- **Date:** 2026-10-27
- **Deciders:** slmao
- **Acceptance:** pending (review of Spec 0068's branch PR)
- **Related Spec:** [Spec 0068: The E3 Authoring Editor in Atlantis Editor App](../specs/0068-authoring-editor.md)
  (`Draft`; its Q1, Q2)
- **Related ADR(s):**
  - **Extends** [ADR-0124](0124-editor-transaction-and-client-side-undo.md):
    - D1: a gesture's previews are unrecorded and its release is one Editor Transaction. The previews now go to the
      Runtime World and the transaction to the authoring document.
    - D4: the history's edit target is the authoring document.
  - **Extends** [ADR-0131](0131-authoring-apply-and-runtime-scene-reload.md) D7: runtime-side edits are not written
    back and are replaced by the next reload. A Gizmo preview relies on exactly that.
  - **Amends** ADR-0131 D5's history clause: E2's history clears on a change of scene generation. That rule stays for
    a history over the Runtime World. A history over the authoring document clears on `DocumentReopened`, not on an
    apply.
  - **Keeps** [ADR-0125](0125-out-of-process-editor-platform-and-uniform-client-interface.md) D2 (no new wire call),
    [ADR-0129](0129-authoring-document-ownership.md) (one owner, no client copy),
    [ADR-0130](0130-authoring-access-interface-and-module-boundary.md) (the closed command and event sets), ADR-0131
    D1–D4 and its 2026-10-23 amendment (a pause holds the reload; a newer apply replaces a pending one),
    [ADR-0132](0132-viewport-overlay-surface.md) (the Gizmo is client-side) and
    [ADR-0103](0103-runtime-world-operation-boundary.md) (no World verb).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The Runtime World is what the Viewport shows,** and it has no hierarchy (Spec 0051 R5). Its transform is a
  `WorldMatrix`. E1's Gizmo previews a drag by writing that matrix through `RuntimeConnection` once per editor frame,
  and E2 records the release (ADR-0124 D1).
- **The authoring document is what a game is made of** (ADR-0129). It has the hierarchy and each node's local
  `Transform`. An edit there reaches the Viewport only through an apply: an in-memory cook and an all-or-nothing
  reload at a frame boundary (ADR-0131).
- **Committing to the document costs a validation:** about 22 ms per transaction on Bistro (Plan 0065 J9, a reported
  target missed). An apply costs about 19 ms of cook plus a 14–28 ms swap frame on Bistro (Plan 0065 J3).
- **Runtime-side edits are disposable:** the next reload replaces them (ADR-0131 D7).
- **E2's history clears on every scene-generation change** (ADR-0131 D5). That made sense when the history's subject
  was the World a reload discards. A history over the document would be wiped by the editor's own applies.
- **A pause holds command application,** and with it a requested reload (ADR-0131, amended 2026-10-23).

## Decision

1. **Previews go to the Runtime World.**
   - During a Gizmo drag, each pointer batch writes the new `WorldMatrix` of the dragged node and of each of its
     descendants (computed from the document's hierarchy) to the Runtime World, in one `RuntimeConnection`
     transaction.
   - Previews are never recorded and never written to the document.
   - If a deep subtree makes this too slow (Spec 0068 Q8), the preview may be limited to the dragged node by an owner
     ruling, without changing this decision.
2. **A committed edit is one authoring transaction.**
   - A Gizmo release, an Inspector commit, a Hierarchy command, an undo and a redo each submit exactly one
     `AuthoringConnection` transaction, from the closed command set.
   - A Gizmo release converts the final world matrix to the node's local `Transform` through its parent's world
     matrix.
   - If the transaction is refused, the editor writes the gesture's start matrices back to the Runtime World, so the
     preview does not outlive the refused edit, and it shows the reason.
3. **Every settled commit requests an apply, automatically and coalesced.**
   - After a commit settles as applied to the document, the editor requests `apply`. A newer apply replaces a pending
     one (Plan 0065 J5). Each apply cooks the whole current document, so coalescing loses no edit.
   - The editor always shows the apply state: `applying`, `applied · generation N`, `pending: paused`, or
     `failed: <reason>`. A failed apply leaves the document unchanged and is retried by the next commit.
   - There is no manual apply step in normal editing.
4. **The history's edit target is the authoring document** (ADR-0124 D4). Its records address the document
   (`scene::PropertyAddress`, and ADR-0136's structural records).
   - **ADR-0131 D5's clause, amended for this history:** it is not cleared by a change of scene generation, because
     the editor's own applies change it on every edit. It clears when the document is reopened from disk
     (`DocumentReopened`), and when the editor's session ends.
   - E2's history over the Runtime World keeps D5 as it is.
5. **What the user sees between a release and `Applied` is the last preview,** which equals the committed result for
   the node and its subtree. When the reload lands, the baked World replaces the previewed one with the same values.
   - **Under a pause,** previews and the apply are both held. The editor shows `pending: paused`, and Step or Resume
     shows them.
   - **Edits with no preview** (Inspector, Hierarchy, undo, redo) are visible at `Applied`.

## Consequences

### Positive

- **A drag costs what E1's drag costs:** no validation per batch, and R-a's measured latency.
- **The document is always the truth:** the Viewport converges to it at every `Applied`, and a preview cannot
  survive a reload or a refused edit.
- **Undo works across applies,** and other clients see each committed edit as an authoring event.
- **No new wire call, command, event or Runtime change.**

### Negative / Trade-offs

- **Each committed edit costs a reload.** On Bistro that is about 60–80 ms from release to `Applied`, and a 14–28 ms
  swap frame stalls the Viewport once per apply. Coalescing bounds a burst to one reload per Runtime frame.
- **An apply replaces gameplay clients' runtime-side state** (ADR-0131 D7). Editing during play resets it at every
  commit; the editor states this in its apply state.
- **A subtree preview writes one matrix per descendant** per batch.
- **Two documents are briefly inconsistent** (preview ≠ document) during a drag, by design.

## Alternatives Considered

- **Authoring writes per pointer batch:** one validation (about 22 ms on Bistro) per batch. A drag would trail by
  several frames and load the session for every client.
- **Live projection of document edits into the World:** the clean answer, named by Spec 0065 as a later Spec. It is
  not built here.
- **Explicit or debounced apply:** the Viewport, Hierarchy and Inspector would disagree until a manual step or a
  delay, and the Gizmo would start a drag from stale World values.
- **Keep ADR-0131 D5 for this history:** every edit's own apply would clear it, so undo would never work.
- **Write the World back into the document after a drag:** contradicts ADR-0131 D7, and the World has no hierarchy
  to write back from.
