# Spec: Editor Transaction and Undo/Redo (E2)

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-20
- **Implementation:** merged — M1–M5 in [PR #274](https://github.com/slmao/Atlantis/pull/274). J6, the owner's interaction run,
  passed on 2026-10-22 ([recorded on PR #274](https://github.com/slmao/Atlantis/pull/274#issuecomment-6320983181)).
- **Related Plan(s):** [Plan 0063](../plans/0063-editor-transaction-undo.md) (`Approved`; Joint Human Review
  2026-10-20 recorded in its header; implemented in PR #274). As with Spec 0062, the Plan was drafted together
  with this Spec at the maintainer's direction.
- **Approval:** slmao, 2026-10-20. The owner ruled Q1–Q6, each as its recommendation (Q1 A-a, Q2 S-a, Q3 P-a,
  Q4 K-a, Q5 H-a, Q6 V-a), with one Spec correction (Plan J1: R5's comparison is `PropertyValue` equality, as Plan
  P4 states, not "bitwise for floats"). The rulings are recorded once in
  [Plan 0063](../plans/0063-editor-transaction-undo.md). The text was reviewed in
  [PR #273](https://github.com/slmao/Atlantis/pull/273), merged before the rulings were written to the branch; the
  status lines landed with the implementation PR. The open-question text below keeps the options as the record of
  what was weighed.
- **Related ADR(s):**
  - [ADR-0124](../adr/0124-editor-transaction-and-client-side-undo.md) (`Accepted` 2026-10-20): what an Editor Transaction
    is, the client-side undo/redo model and its safety rules, and the seam to the authoring scene.
  - Builds on [ADR-0100](../adr/0100-scene-semantic-schema-layering-model-and-versioning.md) D7: the property
    address that Spec 0049 R7 fixed for future Editor Transactions. ADR-0100 named Editor Transaction as a future
    consumer only; this Spec is that consumer, and ADR-0100 is unchanged.
  - Unchanged: [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) and
    [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) (the World's five commands, five events and
    transactions), [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md),
    [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md),
    [ADR-0107](../adr/0107-editor-ui-library-selection.md),
    [ADR-0108](../adr/0108-editor-host-and-viewport-composition.md),
    [ADR-0123](../adr/0123-editor-viewport-camera-and-picking-host-services.md) and
    [ADR-0004](../adr/0004-phase1-threading-baseline.md).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

The editor (Spec 0056, E1 Spec 0062) changes the World in two ways:
- an Inspector edit is one `SetProperty`;
- a Gizmo drag writes one four-column transaction per frame and the final value on release.

Neither can be taken back. This Spec is the second editor iteration, **E2**. It defines the **Editor
Transaction**, the "one logical edit" that Spec 0049 named as a future consumer of the semantic schema, and builds
**undo/redo** on it.
- **A logical edit:** an ordered set of property writes, each carrying its Spec 0049 R7 address and its value
  before and after.
- **Drag and field edit alike:** a whole Gizmo drag and a single Inspector field edit are each exactly one Editor
  Transaction, so they undo and redo alike.
- **Undo and redo:** an undo writes the before-values forward as one ordinary Spec 0053 transaction, and a redo
  writes the after-values.

Everything is client-side, in the editor library. **No command, event, query or component is added**, and no
Runtime code changes. What a client-side history cannot make atomic is a race with another client writing the same
property in the same frame. That is recorded as an open question (Q4), with the verb that would close it as the
rejected alternative.

The record is deliberately not tied to the baked, flat Runtime World:
- **Addresses:** properties are addressed by (node/entity GUID, component TypeId, leaf FieldId), the address
  Spec 0049 fixed for exactly this purpose.
- **Edit target:** the history talks to a narrow *edit target*. E2 has one, the Runtime World through
  `RuntimeConnection`. E3's authoring scene can be a second target without changing the record or the history.

**North star:** on the default scene, the user drags a mesh with the Gizmo for 30 frames (30 preview transactions)
and releases. Ctrl+Z puts the mesh back exactly as it was:
- one transaction writes it back;
- `WorldMatrix` is bit-exact;
- the Viewport image is byte-identical to the image before the drag.

Ctrl+Y moves it back to the dropped position. Next, the user edits `Light.intensity` 3 → 6, presses Ctrl+Z (→ 3),
and makes a new edit (→ 4), which clears the redo. Last, a second client sets the light to 5; the next Ctrl+Z is
refused as "changed elsewhere" and writes nothing.

## Motivation / Problem Statement

### What exists

Read at `origin/main` `714da3a` (E1 merged in PR #271, post-merge docs in PR #272).

- **Inspector edits** (`InspectorModel::commit()`): one `SetProperty` per completed edit (Plan 0056 J5: a drag on a
  numeric field commits on release, not per frame). Each edit is settled:
  - *Refused* when `drainFailures()` names its ticket;
  - *Applied* when the subject's `PropertyChanged` for the field arrives.
- **Gizmo drags** (`GizmoModel`): `beginDrag()` remembers the start matrix. `dragTo()` changes the shown matrix.
  `flush()` submits a transaction of `WorldMatrix.column0..3` at most once per editor frame while it changes, plus
  the final value on `endDrag()` (Spec 0056 ruling Q5, G-a). A 30-frame drag is about 31 transactions and 124
  `PropertyChanged` events.
- **The World's surface** (Spec 0052/0053): five commands, five events, all-or-nothing transactions.
  - `PropertyChanged` carries the applied value, not the old one, and no event names the client or ticket that
    caused it.
  - A refusal reaches the submitting connection only, as a `CommandFailure` naming its ticket.
- **Spec 0049** reserved the seam:
  - R7 defines `asset_system::scene::PropertyAddress`, the triple (node `EntityGuid`, component `TypeId`, leaf
    `FieldId`): "This is the form that Editor transactions, agent patches and prefab overrides will address".
  - ADR-0100 names Editor Transaction among the semantic schema's future consumers.
- **Two schemas share the address shape** but not their component types:
  - The authoring scene's components are `asset_system::scene::Transform`, `…::Camera` and so on.
  - The Runtime World's are `world::Transform`, `world::WorldMatrix` and so on.
  - A node and its baked entity share one GUID (ADR-0097 D5).

  The World access layer's own `access::PropertyAddress` is the same triple under another name.

### What is missing

- **No edit is reversible.** One mistaken drag on Bistro moves a building, and only a reload restores it.
- **There is no unit of "one edit".** A drag is dozens of World transactions. Nothing says they are one gesture, so
  any history built on raw transactions would need 30 undos for one drag.
- **Nothing consumes Spec 0049's address yet.** If the first history is written against the flat baked World only,
  E3 (editing the authoring scene) has to rebuild it.

## Goals

Maintainer-fixed (2026-10-20):

- **Reversible edits with one transaction semantics** for drags and property edits.
- **The World verb surface stays closed** (Spec 0052): undo/redo is client-side first. A needed verb would be an open
  question for review; none is proposed (see Q4).
- **Drag merging is the core semantic.** A Gizmo drag (per-frame transactions plus the final value) is one logical
  edit.
- **Designed against Spec 0049's Editor Transaction,** with a seam for E3's authoring scene. Do not design only
  for the flat baked Runtime World.
- **Later iterations (E3→E5→E4→E6) are reference only:** not designed or scaffolded here.

Goals of this Spec within those boundaries:

- One record type for a logical edit, and one history over it, used by both the Inspector and the Gizmo.
- Safety:
  - An undo never silently clobbers another client's later write (best effort; Q4).
  - Nothing is replayed onto an entity that was destroyed after it was edited.
- Zero change outside the editor library and its tests: no Runtime, World, Connection, Remote or protocol change, and
  no frame-path change.

## Non-Goals

- **Undo of structural edits** (create/destroy entity, add/remove component). The editor's UI makes none today.
  Q2 records why the record admits them later.
- **Authoring-scene editing, re-bake and saving** (E3). No authoring edit target is built.
- **A shared or persisted history:**
  - no undo of other clients' edits;
  - no history surviving a scene change or editor restart;
  - no multi-user collaborative undo.
- **Undo of view state:** selection, camera, Gizmo mode and filters are not edits.
- **Text-field undo inside a field being typed in:** the UI library's own text undo handles it, and E2 does not
  intercept it.
- **An edit-history panel.** The layout's Content quadrant stays reserved (Spec 0056 ruling Q6).
- **Any new World verb, event, query, component, protocol message or Runtime change** (R7).

## Requirements

### Functional

- **R1 — Editor Transaction.** A logical edit is an `EditTransaction`:
  - a label for the UI;
  - an ordered, non-empty list of property writes, each with:
    - an address: a Spec 0049 R7 `scene::PropertyAddress` (node/entity GUID, component `TypeId`, leaf `FieldId`);
    - a `before` value and an `after` value, both schema-primitive values (the `PropertyValue` alternatives).

  Each direction is applied as **one** Spec 0053 transaction, or the single command a one-write edit equals. The
  forward direction writes every `after` and the inverse every `before`, in the same order. It is a plain value and names no UI type and no connection.
- **R2 — Which edits are Editor Transactions.**
  - **Inspector edit:** a completed field edit (a commit, including setting or clearing an `Optional` field) is
    one Editor Transaction of one write. `before` is the value the Inspector showed.
  - **Gizmo drag:** from `beginDrag()` to `endDrag()` is one Editor Transaction of four writes
    (`WorldMatrix.column0..3`). `before` is the matrix at the drag's start and `after` the final value.
    - **Preview transactions:** the per-frame writes in between stay as they are (live feedback; Spec 0056 G-a).
      They are previews and are never recorded.
    - **Unchanged drags:** a drag whose final matrix equals its start records nothing.

  The two kinds are treated identically from here on: same history, same settlement, same undo and redo.
- **R3 — Settlement.** A recorded edit is *pending* until the World settles its applying transaction (the
  Inspector's commit, or the drag's release transaction):
  - **Refused** (the transaction's failure is drained): the edit is not kept in the history.
  - **Applied** (all of the transaction's writes are observed in `PropertyChanged`): it becomes undoable.

  An undo or redo is settled the same way.

  While any of this editor's history transactions is pending, Undo and Redo are disabled, for example while the
  Runtime is paused (Q3). New edits are never blocked.
- **R4 — Undo and redo.** The history is linear and owned by one editor session, holding this editor's own edits
  only.
  - **Undo:** the newest applied edit's inverse is submitted as one transaction. When it applies, the edit moves to
    the redo stack.
  - **Redo:** the newest undone edit's forward is submitted again. When it applies, it returns to the undo stack.
  - **A new edit** clears the redo stack.
  - **Depth:** at most 128 edits; the oldest is dropped.
  - **Scene change:** the history is cleared when the Runtime's scene GUID (`RuntimeStatus`) changes.
- **R5 — Conflicts with other writers** (Q4). Before an undo or redo is submitted, every write's address is read.
  - If any current value is not equal (`PropertyValue` equality) to what this edit left there (`after` for undo,
    `before` for redo), nothing is submitted. The UI reports "changed elsewhere", and the entry is dropped from the history
    (Q4, K-a).
  - This check is best-effort: another client's write that lands in the same frame, ahead of the undo, is not
    detected.
- **R6 — Invalidation.** An entry is dropped (and the UI says so) when the World reports either of these after the
  edit was made:
  - `EntityDestroyed` for one of its entities, even if a later `CreateEntity` reuses the GUID;
  - `ComponentRemoved` for one of its (entity, component) pairs.

  A refused undo or redo (a failure drained for it) drops the entry too.
- **R7 — No World or Runtime change.** No command, event, query, component or protocol message is added, and
  Runtime, World, Connection, Remote, the CLI, the SDKs and `runFrame()` are untouched. The editor keeps Spec 0056
  R1's links. Its include allow-list (`tests/editor/editor_boundary_tests.cpp`, which today admits `asset_guid.h` as
  the one Asset System header) gains exactly one entry, `atlantis/asset_system/scene_property_address.h` (Q1). The
  library already reaches the Asset System transitively through Connection, so the link list is unchanged.
- **R8 — The seam for E3** (Q2). The history reaches the World only through an **edit target**, a narrow interface:
  - read an address's current value;
  - submit an ordered list of writes as one transaction, returning a handle;
  - report a handle's settlement (pending, applied or refused);
  - report invalidations (destroyed entities, removed components).

  E2 implements one target, over `RuntimeConnection`. The record and the history name no `RuntimeConnection`, no
  World component and no Runtime-only concept such as "baked".
- **R9 — UI.**
  - **Shortcuts:** Ctrl+Z undoes; Ctrl+Y and Ctrl+Shift+Z redo. They are active when no text field has keyboard
    focus, and are not taken while a Gizmo drag is in progress.
  - **Toolbar:** Undo and Redo buttons, disabled with the reason as their tooltip ("nothing to undo", "waiting for
    the Runtime"), and showing the label of the edit they would act on ("Undo Move 52052052…", "Undo Set
    Light.intensity").
  - **Status line:** the last history notice ("changed elsewhere", "entity destroyed", "refused: …").

### Non-functional

- **Latency:** an undo or redo shows on the next frame, the same one-frame command application as any edit.
- **Cost:** recording a drag reads nothing extra (the start matrix is already held). The conflict check reads at
  most one value per write. Settlement uses one event subscription, filtered client-side to the pending writes.
- **Scale:** usable on Bistro: per-frame work is bounded by the pending transactions, never by the World's size.
- **Threading:** frame thread only (ADR-0004).
- **Dependencies:** none new.
- **Performance contract:** `src/editor/**` and `tests/**` are excluded from the gate's applicability, and
  `runFrame()` is untouched. The implementation PR states N/A with that reason.

## Proposed Design

```
Editor (client library)
  InspectorModel.commit ──┐                       ┌── GizmoModel: beginDrag … endDrag
                          ▼                       ▼    (preview flushes unrecorded)
                 EditTransaction{label, [write{address(R7), before, after}]}
                          │
                  EditHistory  ── undo[≤128] / redo, pending, notices
                          │       (record, undo(), redo(), update(failures, events))
                          ▼
                  EditTarget (interface: read, submit, settlement, invalidations)
                          │
          E2: RuntimeWorldEditTarget ── RuntimeConnection (transactions, PropertyChanged,
                                        EntityDestroyed, ComponentRemoved, failures)
          E3 (not built): an authoring-scene target, same record, same history
```

- **Recording.** The Inspector and the Gizmo hand the history an `EditTransaction` together with the handle of the
  transaction that applies it.
  - **Inspector:** the commit's command stays one `SetProperty`. Its ticket is the handle, taken as a transaction
    of one (`TransactionTicket{ticket, 1}`). Spec 0053 settles a one-command transaction exactly as the single
    command, so both kinds settle the same way, without changing what the Inspector submits.
  - **Gizmo:** the release flush is the applying transaction. Preview flushes are not recorded.
- **Settlement** (R3) matches the target's drained failures against pending handles. An applied transaction is
  recognized by its writes' `PropertyChanged` events, in order, with the written values.
- **Undo/redo** (R4, R5) checks every write's current value, then submits the inverse or forward through the target.
- **Invalidation** (R6) comes from the same subscription: `EntityDestroyed` and `ComponentRemoved`.

## Architectural Impact

Yes. The record format, the history semantics and the edit-target seam are a public API shape of Atlantis Editor and
the first realization of a consumer Spec 0049 named. One ADR, drafted alongside:

- **[ADR-0124](../adr/0124-editor-transaction-and-client-side-undo.md)** (`Accepted` 2026-10-20):
  - D1: the Editor Transaction (R1, R2), including drag merging.
  - D2: a client-side, per-session, linear history that undoes by writing forward (R4).
  - D3: settlement, best-effort conflict refusal and invalidation (R3, R5, R6), with no new verb.
  - D4: the edit-target seam and the Spec 0049 address as the record's address (R8).

**Expected code impact** (the Plan's file list is binding):

- **Editor** (`src/editor/`):
  - new models: `model/edit_transaction.h` (the record), `model/edit_history.{h,cpp}` (the history),
    `model/edit_target.h` (the interface), and `model/runtime_world_edit_target.{h,cpp}` (E2's target);
  - Inspector and Gizmo recording;
  - `Editor` wiring (one more model), plus the view's toolbar, shortcuts and status line.
- **Tests:** the editor's model, view and boundary tests; one GPU north-star case in `tests/runtime/`.
- **Docs:** AGENTS.md (one sentence in the Editor paragraph) and `module_boundaries.md` (the Editor section), with
  the implementation PR.
- **Unchanged:** everything outside `src/editor/` and its tests, including World, Runtime, Connection, Remote, CLI,
  the Gameplay and C# SDKs, Platform, RHI, Renderer, shaders, formats and goldens.

## Alternatives Considered

The open questions compare the options. Rejected across them:

- **A Runtime-side or World-side history** (an undo verb, or a history kept by the Runtime): it widens the closed verb
  surface. It would also make the World remember editor gestures, which are a client's concept.
- **Recording every World transaction** (each preview flush an entry): it breaks the one-gesture-one-edit
  semantics that is this Spec's core.

## Testing & Verification Plan

- **Editor model tests** (GPU-free, over an InProcess connection on the editor fixture scene):
  - **Recording:**
    - an Inspector commit records one entry of one write, with the shown value as `before`;
    - a 30-frame Gizmo drag records exactly one entry of four writes (`before` = start, `after` = final), and the
      preview transactions record none;
    - an unchanged drag records nothing (R1, R2).
  - **Settlement:**
    - a refused commit is not kept;
    - a pending edit disables undo until applied, including while the fake Runtime is paused (R3).
  - **Undo/redo:**
    - undo writes every `before` in one transaction, and `WorldMatrix` is bit-exact after it;
    - redo writes every `after`;
    - a new edit clears redo;
    - depth is capped at 128;
    - a scene-GUID change clears the history (R4).
  - **Conflicts:** a second connection's later write makes the next undo refused as "changed elsewhere", nothing is
    submitted, and the entry is dropped (R5).
  - **Invalidation:** a destroyed entity, or a removed component, drops the entries naming it, even if the GUID is
    re-created (R6).
  - **The seam:** the history runs against a test-double edit target with no connection, scripted settlement and
    invalidation. This proves the history needs nothing but the interface (R8).
- **UI tests** (headless, scripted input; R9):
  - Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z act;
  - Ctrl+Z while a text field is active is left to the field;
  - Undo is ignored while dragging;
  - the toolbar buttons' disabled states and labels;
  - the status line after a conflict.
- **Boundary scan:** the editor still includes nothing of Runtime, ECS, RHI, Renderer, Vulkan, Platform or OS. The
  allow-list differs from today's by exactly `atlantis/asset_system/scene_property_address.h`, and the link list is
  unchanged (R7).
- **GPU north star** (in process, fatal VVL; the Summary's sequence on the default scene):
  - after a 30-frame drag and Ctrl+Z, the frame's `WorldMatrix` is bit-exact and the Viewport image is
    byte-identical to the one before the drag;
  - after Ctrl+Y, the image is byte-identical to the image after the drag;
  - the light sequence 3 → 6 → undo → 4 holds exact values in frame data;
  - a second connection's write makes the next undo refused, with frame data unchanged.
- **Regression:**
  - Debug and Release full suites;
  - every golden unchanged;
  - `git diff` empty outside `src/editor/`, `tests/editor/`, `tests/runtime/` and docs;
  - `assembleDebug`;
  - performance gate N/A (R7, with the reason).

## Risks & Open Questions

Risks:

- **A best-effort conflict check** (R5) leaves a one-frame window. It is acceptable for a single editor beside
  scripts and agents. A collaborative-editing future would need Q4's verb.
- **`PropertyValue` lives in World's access header.** The record reuses it because its alternatives are the schema's
  primitive kinds, not World concepts. An authoring target in E3 may want it hoisted to a neutral home. That is a
  move, not a redesign, and it is E3's decision.
- **Settlement by observed events** relies on Spec 0052's one-event-per-applied-command, in application order. A
  transport that drops events would leave an edit pending. Pending edits never block new edits, only undo. A stuck
  pending edit clears when the history does: on a scene change, or when the editor restarts.

Open questions — all six ruled by the owner (slmao, 2026-10-20), each as its recommendation; rulings recorded in
Plan 0063:

- **Q1 — The record's address type** (ADR-0124 D4).
  - **(A-a) Spec 0049 R7's `asset_system::scene::PropertyAddress`** — recommended. It is the type Spec 0049 named
    for Editor Transactions, and an E3 authoring target uses it unchanged. E2's target converts it field for field
    to `access::PropertyAddress` (same triple). Cost: the editor's include allow-list gains one header,
    `scene_property_address.h`, beside the `asset_guid.h` it already admits. The header is a value type depending
    only on `asset_guid.h`, `schema.h` and `result.h`, all of which the editor already includes. AGENTS.md's Editor
    rule (no Runtime, ECS, Platform, RHI, Renderer, Vulkan or OS header) is unaffected.
  - **(A-b) `world::access::PropertyAddress`.** No new include, but the record is then a Runtime World record, and E3
    either converts the other way or forks the record.
  - **(A-c) A new editor-owned address type.** A third spelling of the same triple.
  - **Why A-a:** the address is the seam the maintainer asked E2 to honour, and Spec 0049 already defines it.

- **Q2 — The seam to E3** (ADR-0124 D4).
  - **(S-a) Record + history + a narrow edit-target interface with one implementation** (Runtime World) —
    recommended. Property writes only. The record's write list is the only shape E2 fixes. Structural operations
    (create/destroy, add/remove) are a later extension of the write variant, named but not added (Non-Goals).
  - **(S-b) A history bound directly to `RuntimeConnection`.** Smaller now, but E3 rewrites it.
  - **(S-c) Build the authoring-scene target now as well.** That is E3's work, and no authoring editing exists to
    drive it (it would be scaffolding).
  - **Why S-a:** one interface with one implementation is the smallest seam that keeps the record and history
    target-neutral. It carries no placeholder for E3, only the boundary E3 would plug into.

- **Q3 — Undo while edits are pending.**
  - **(P-a) Undo and redo wait until every history transaction is settled** — recommended. Edits stay free. This
    is simple and never reorders. While the Runtime is paused, the toolbar says "waiting for the Runtime".
  - **(P-b) Allow undo of a pending edit,** relying on submission order (the inverse applies after the forward).
    It works for the commands but not for the conflict check, which would read values the edit has not yet
    written.

- **Q4 — Conflicts with other writers** (ADR-0124 D3; the verb question).
  - **(K-a) A client-side check before submitting; on conflict, refuse and drop the entry** — recommended.
    - No verb. Correct unless another client writes the same property in the same frame, ahead of the undo.
    - The entry is dropped, not kept: a kept entry would stay unundoable and block the history behind it.
  - **(K-b) Blind undo:** write `before` regardless. Simple, but it silently clobbers a script's or agent's later
    write, which the multi-client World (Spec 0054/0055) makes ordinary.
  - **(K-c) A conditional write verb** (a transaction precondition such as "this property still equals X", checked
    where the transaction applies). It is atomic and closes the race, but it is a sixth command on the closed
    surface: World access, connection, `atlantis.remote/1`, the C# SDK and the CLI would all change, and it would
    need its own Spec and an ADR superseding ADR-0104.
  - **Why K-a:** the race window is one frame against a concurrent writer of the very same property. K-c's cost is
    the whole client surface. If collaborative editing ever needs atomicity, K-c is the route, under its own Spec.
    **No new verb is proposed.**

- **Q5 — History lifetime and scope.**
  - **(H-a) Per editor session, this editor's edits only, 128 deep, cleared on a scene change** — recommended.
  - **(H-b) Unbounded.**
  - **(H-c) Persisted with the scene.** That belongs to authoring editing and saving (E3 and later).

- **Q6 — Selection on undo/redo.**
  - **(V-a) Unchanged** — recommended. Undo is about data. The status line names the entity, and selection is view
    state (Non-Goals).
  - **(V-b) Select the edited entity.** It is convenient, but it mixes view state into the history, and a
    multi-entity edit has no single entity to select.

## Out of Scope / Future Work

- Structural-edit undo, an authoring-scene edit target, saving and re-bake (E3 and later).
- A conditional write verb for atomic conflict detection (Q4 K-c), under its own Spec if collaborative editing needs
  it.
- An edit-history panel; per-entity or per-component history filters; undo grouping across several gestures
  ("macro" edits).
- The later iterations E3→E5→E4→E6, reference only.
