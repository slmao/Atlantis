# ADR 0124: Editor Transaction and Client-Side Undo

- **Status:** Proposed
- **Date:** 2026-10-20
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0063: Editor Transaction and Undo/Redo (E2)](../specs/0063-editor-transaction-undo.md)
  (`Draft`; D1 is its R1/R2, D2 its Q3/Q5, D3 its Q4, D4 its Q1/Q2)
- **Related ADR(s):**
  - Consumes [ADR-0100](0100-scene-semantic-schema-layering-model-and-versioning.md) D7, the property address that
    Spec 0049 R7 fixed for Editor Transactions. ADR-0100 named Editor Transaction as a future consumer; this is it.
    ADR-0100 is unchanged.
  - Keeps [ADR-0103](0103-runtime-world-operation-boundary.md) and
    [ADR-0104](0104-runtime-world-transaction-atomicity.md) (the World's verbs and transaction atomicity),
    [ADR-0105](0105-runtime-connection-and-cli-client.md), [ADR-0106](0106-attachable-runtime-transport-and-control.md),
    [ADR-0108](0108-editor-host-and-viewport-composition.md), [ADR-0123](0123-editor-viewport-camera-and-picking-host-services.md)
    and [ADR-0004](0004-phase1-threading-baseline.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The editor's edits are irreversible.** An Inspector edit is one `SetProperty`. A Gizmo drag is a transaction per
  frame plus its final value (Spec 0056 ruling Q5, G-a), so a 30-frame drag is about 31 World transactions.
- **The World's surface is closed** (Spec 0052: five commands, five events; Spec 0053 transactions). The maintainer
  requires undo/redo to be client-side first, and any verb to be argued as an open question (2026-10-20).
- **What the World tells a client:**
  - `PropertyChanged` carries the applied value, but not the old value or the originating client.
  - A refusal reaches the submitter only, as a failure naming its ticket.
  - Commands apply at the next frame, in submission order.
  - Several clients write concurrently: the CLI, Remote, the Gameplay SDKs and agents (Specs 0054, 0055, 0057, 0058).
- **Spec 0049 reserved the seam:**
  - R7's `scene::PropertyAddress` (node/entity GUID, component `TypeId`, leaf `FieldId`) is "the form that Editor
    transactions … will address".
  - The authoring scene and the Runtime World share GUIDs (ADR-0097 D5) but have distinct component types.
  - E3 will edit the authoring scene.

## Decision

1. **An Editor Transaction is one logical edit** (Spec 0063 R1, R2).
   - **Shape:** an ordered, non-empty list of property writes. Each write is a Spec 0049 R7 address with a `before`
     and an `after` schema-primitive value, under a UI label.
   - **Application:** each direction is applied as one Spec 0053 transaction (or the single command a one-write
     edit equals).
   - **An Inspector edit** is one Editor Transaction.
   - **A Gizmo drag**, from begin to release, is one Editor Transaction: its start values as `before`, its release
     values as `after`. The per-frame transactions in between are previews, never recorded. A drag that ends where
     it began records nothing.
2. **The history is client-side, per editor session, linear and forward-writing** (R4; Q3 P-a, Q5 H-a).
   - **Scope:** it holds this editor's own edits, at most 128, and is cleared when the Runtime's scene changes.
   - **Undo:** submits an edit's `before` values as a new transaction. **Redo** submits its `after` values. Nothing
     is "rolled back" in the World.
   - **A new edit** clears redo.
   - **Pending entries:** an entry becomes undoable only once its applying transaction is settled as applied. While
     any history transaction is pending, undo and redo wait; edits never wait.
3. **Safety without a new verb** (R3, R5, R6; Q4 K-a).
   - **Settlement:**
     - A transaction is applied when its writes' `PropertyChanged` events arrive in order with the written values.
     - It is refused when its failure is drained; a refused edit is not kept.
   - **Conflict refusal:** before an undo or redo is submitted, every address is read. If any value differs (bitwise)
     from what this edit left there, nothing is submitted and the entry is dropped ("changed elsewhere").
   - **Invalidation:** an `EntityDestroyed` or `ComponentRemoved` naming an entry's entity or component drops the
     entry, even if the GUID is later re-created.
   - **The conflict check is best-effort.** A concurrent write to the same property that applies in the same frame,
     ahead of the undo, is not detected. A conditional-write verb would close that window. It is not adopted: it
     would be a sixth command on the closed surface, changing World access, the connection, `atlantis.remote/1`,
     the C# SDK and the CLI. If collaborative editing ever needs it, it gets its own Spec and an ADR superseding
     ADR-0104.
4. **The seam: a target-neutral record and an edit-target interface** (R8; Q1 A-a, Q2 S-a).
   - **Target-neutral types:** the record and the history name no connection, no World component and no
     Runtime-only concept.
   - **The edit-target interface:** they reach a world only through an edit target, which offers:
     - read an address;
     - submit ordered writes as one transaction, returning a handle;
     - the handle's settlement;
     - invalidations.
   - **E2's one target** is over `RuntimeConnection`. It converts the Spec 0049 address field for field to the World
     access layer's address.
   - **E3's authoring scene** can be a second target with the same record and history. Nothing is built for it now.
   - **Structural edits** (create/destroy, add/remove) are a later extension of the write list's variant.
   - **One include:** the editor's include allow-list gains exactly `atlantis/asset_system/scene_property_address.h`.
     Its link list is unchanged.

## Consequences

### Positive

- **One semantics for drags and field edits:** one gesture, one entry, one undo.
- **No change to the World, the Runtime, the connection, the protocol or any SDK.** The frame path is untouched, so
  no performance gate applies.
- **No silent clobbering** of a later write by another client, outside a one-frame same-property race.
- **E3 inherits the record and history.** It adds a target; it does not rewrite undo.

### Negative / Trade-offs

- **A best-effort conflict check**, with a one-frame window (see D3).
- **A dropped entry on conflict:** history may shrink under a busy co-writer instead of staying fully navigable.
- **Settlement by observed events:** an edit whose events never arrive (a lossy transport) stays pending and blocks
  undo, but not editing, until the history is cleared.
- **`PropertyValue` is reused from World's access header** for the record's values. E3 may hoist it to a neutral home.
- **Only this editor's edits are undoable;** a script's or another editor's are not.

## Alternatives Considered

- **A Runtime- or World-side history, or an undo verb:** it widens the closed surface, and puts a client's gesture
  concept into the World.
- **Recording every World transaction** (each drag preview an entry): 30 undos for one drag.
- **Writing the drag only on release** (no previews): this loses the live feedback Spec 0056 ruled for (G-a).
- **Blind undo** (Q4 K-b): it clobbers other writers' later values without notice.
- **A conditional-write verb** (Q4 K-c): atomic, but a sixth command across the whole client surface for a one-frame
  window.
- **Undo while pending** (Q3 P-b): the commands would order correctly, but the conflict check would read values not
  yet written.
- **The access layer's or a new editor-owned address type** (Q1 A-b, A-c): the record becomes Runtime-World-shaped,
  or gains a third spelling of the same triple.
- **A history bound directly to `RuntimeConnection`** (Q2 S-b), or **building the authoring target now** (S-c): E3
  would rewrite it, or E2 would scaffold for an editing mode that does not exist yet.
