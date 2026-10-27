# ADR 0136: Structural Edit Records — Create, Destroy, Components and Reparent in the Editor's History

- **Status:** Proposed
- **Date:** 2026-10-27
- **Deciders:** slmao
- **Acceptance:** pending (review of Spec 0068's branch PR)
- **Related Spec:** [Spec 0068: The E3 Authoring Editor in Atlantis Editor App](../specs/0068-authoring-editor.md)
  (`Draft`; its Q3)
- **Related ADR(s):**
  - **Extends** [ADR-0124](0124-editor-transaction-and-client-side-undo.md). D4 names structural edits as "a later
    extension of the write list's variant". This ADR is that extension. It keeps:
    - D1's one-gesture-one-record rule;
    - D2's linear, forward-writing, client-side history and its waiting rules;
    - D3's safety without a new verb (settlement by observed events, refusal by comparison).
  - **Builds on** [ADR-0135](0135-two-document-edit-and-apply.md): the records address the authoring document, and
    each undo or redo is one authoring transaction followed by an apply.
  - **Keeps** [ADR-0130](0130-authoring-access-interface-and-module-boundary.md)'s closed command set (no new command)
    and [ADR-0129](0129-authoring-document-ownership.md) (the editor's records are its own history, not a copy of the
    document).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **E2's records are property writes:** an address with a `before` and an `after` value (ADR-0124 D1). Undo writes
  `before` and redo writes `after`, each after checking that the address still holds the value the edit left
  ("changed elsewhere", D3).
- **The authoring command set is closed** (Spec 0065 R4):
  - `CreateNode` takes a GUID (client-chosen or minted), a position in the node order and a component list, and
    components start at their defaults.
  - `DestroyNode` removes one node.
  - `AddComponent` / `RemoveComponent` add a component at its defaults or remove it.
  - `SetParent` changes the parent only.
  - `SetActiveCamera` names the active camera.
  - Node order is immutable after creation.
- **Validation runs once per transaction** (`ParentExists`, `ParentAcyclic` and the scene schema). Destroying a node
  whose children remain is refused, so a destroy in the editor is a subtree's destroy in one transaction.
- **A destroyed node's values exist only in the editor's record.** Nothing else keeps them once the transaction
  applies.

## Decision

1. **A record is an ordered list of entries,** each of one variant:
   - `PropertyWrite`: ADR-0124's address, `before` and `after`;
   - `NodeLife`: a node's full state (its GUID, its position in the node order, its parent, and each component with
     every field value), and the direction:
     - `created`: absent before, present after;
     - `destroyed`: present before, absent after;
   - `ComponentLife`: a node's component with every field value, added or removed;
   - `ActiveCamera`: before and after (a GUID or none);
   - `Parent`: a node's parent before and after.

   A reparent is one record of a `Parent` entry and the local `Transform` `PropertyWrite`s that keep the node's world
   placement (Spec 0068 Q5).
2. **Applying a record is one transaction built from the closed command set:**
   - **re-creating a node:** `CreateNode` with its GUID, position and component list, then a `SetProperty` for every
     field that differs from the component default, then `SetParent`;
   - **destroying a node:** `DestroyNode`;
   - **component life:** `AddComponent` plus its `SetProperty`s, or `RemoveComponent`;
   - **active camera:** `SetActiveCamera`.

   A subtree is re-created in ascending position order and destroyed in any order (validation is per transaction).
   A destroy record holds the whole subtree, and whether its active camera was among it.
3. **A step is refused, and the record dropped, when the document no longer holds what the edit left.** That is
   ADR-0124 D3's comparison, extended per variant:
   - **`PropertyWrite`:** the value differs (E2's rule);
   - **undo of `created` / redo of `destroyed`:** the node is gone, has components, values or children other than
     the record's, or has another parent;
   - **undo of `destroyed` / redo of `created`:** a node with the record's GUID exists, or the record's parent is
     gone, or a position is now beyond the node count;
   - **`ComponentLife`:** the component's presence or values differ from what the edit left;
   - **`Parent`:** the current parent differs from the edit's.

   A refusal submits nothing and drops the entry, with E2's notice texts ("dropped: changed elsewhere", "dropped:
   what it edited is gone"). A transaction the document refuses (validation) drops the entry with the refusal's
   reason, as E2's "refused" notice.
4. **Depth and lifetime** follow ADR-0124 D2 (128 entries; a new edit clears redo) and ADR-0135 D4 (clears on
   `DocumentReopened`).

## Consequences

### Positive

- **Every edit the editor makes is undoable,** including a subtree destroy with all its values.
- **No new command, event or verb:** records are replayed with the closed command set, and GUIDs make redo exact.
- **Another client's work is never overwritten by an undo:** a changed state refuses the step.

### Negative / Trade-offs

- **A destroy record holds a subtree's full values.** A large Bistro subtree is a large record and a large undo
  transaction (one validation, about 22 ms, whatever its size).
- **A re-created node returns at its old position.** If other nodes were created meanwhile, its position shifts by
  the order's own rule. The node order is not otherwise restorable, since it is immutable (Spec 0065).
- **The refusal rules are conservative:** a harmless outside change (another client edits a child's field) also
  refuses the undo of a create.

## Alternatives Considered

- **Property records only (E2 as is):** a destroy could not be undone and would lose its subtree for good.
- **A new `RestoreNode` / snapshot command on `AuthoringConnection`:** simpler records, but a new command on the
  shared surface (ADR-0125 D2, ADR-0130 D1) for a need the closed set already meets.
- **Refuse destroying a node that has children** (no subtree records): every subtree destroy becomes a sequence of
  edits, and its undo several steps.
