# Plan: Editor Transaction and Undo/Redo (E2)

- **Spec:** [Spec 0063: Editor Transaction and Undo/Redo (E2)](../specs/0063-editor-transaction-undo.md)
  (`Approved` 2026-10-20) — [ADR-0124](../adr/0124-editor-transaction-and-client-side-undo.md) (`Accepted`
  2026-10-20).
  - Drafted together with the Spec at the maintainer's direction (2026-10-20). The owner ruled Q1 A-a, Q2 S-a,
    Q3 P-a, Q4 K-a, Q5 H-a and Q6 V-a, exactly the options this Plan was drafted against.
  - Keeps ADR-0100 (consumed), ADR-0103/0104, ADR-0105, ADR-0106, ADR-0107, ADR-0108, ADR-0123 and ADR-0004.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-20 — reviewed this Plan and
  [Spec 0063](../specs/0063-editor-transaction-undo.md) together. The text was reviewed in
  [PR #273](https://github.com/slmao/Atlantis/pull/273), merged before the rulings were written to the branch; this
  record landed with the implementation PR.
  - **Spec:** Q1–Q6 ruled as recommended.
  - **Plan:** J1–J7 ruled as recommended. J1 carries one Spec correction: R5's conflict comparison is `PropertyValue`
    equality, as P4 states, not "bitwise for floats". It is folded into Spec 0063 R5 and the matching ADR-0124 D3
    sentence.
  - **ADR:** ADR-0124 `Accepted`.
  - **Authorization:** implementation of M1–M5 on `feature/0063-editor-transaction-undo`. No merge by the agent, no
    push to `main`, no change to `tests/performance/baselines/` or `gate_policy.json`, goldens unchanged, changes
    limited to this Plan's file list (M5's path check), and the performance gate stated N/A with its reason. J6's
    interaction run is the owner's.
- **Implementation:** M1–M5 merged in [PR #274](https://github.com/slmao/Atlantis/pull/274). J6's interaction run passed on 2026-10-22 ([recorded on PR #274](https://github.com/slmao/Atlantis/pull/274#issuecomment-6320983181)).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0063 R1–R9 as recommended, entirely inside Atlantis Editor and its tests:

- the Editor Transaction record;
- a client-side linear history with settlement, conflict refusal and invalidation;
- an edit-target interface with one implementation over `RuntimeConnection`;
- Inspector and Gizmo recording, with a whole drag as one edit;
- Undo/Redo in the toolbar and on Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z.

No World, Runtime, Connection, Remote, protocol or frame-path change.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `714da3a`.

1. **Inspector** (`src/editor/src/model/inspector.cpp`):
   - `commit()` submits one `SetProperty` and returns its `CommandTicket`.
   - `update(failures)` settles it: refused by a drained failure naming the ticket; applied by the subject's
     `PropertyChanged` for the field.
   - `fields()[i].value` is the value as last read, i.e. what the field showed when committed.
2. **Gizmo** (`src/editor/src/model/gizmo.cpp`):
   - `beginDrag()` stores `dragStart_`.
   - `flush()` submits `column0..3` as one transaction when the drag state changed, or when a drag ended (the
     release, `released_`), and returns its `TransactionTicket`.
   - `Editor::frame()` calls `flush()` once after the view.
3. **Editor wiring** (`src/editor/src/editor.cpp`):
   - drains `connection.drainFailures()` once per frame and hands the span to the Inspector;
   - `RuntimeControl::status()` carries the scene GUID.
4. **World events** (`runtime_world_access.h`):
   - `PropertyChanged{address, value}`, one per applied command, in application order;
   - `EntityDestroyed{entity}`; `ComponentRemoved{entity, component}`;
   - `TransactionTicket::contains()` maps a failure to its transaction.
5. **Spec 0049 R7** (`src/asset_system/include/atlantis/asset_system/scene_property_address.h`): the
   `scene::PropertyAddress{node, component, field}` value type, which depends on `asset_guid.h`, `schema.h` and
   `result.h`.
6. **Editor boundary scan** (`tests/editor/editor_boundary_tests.cpp`):
   - an allow-list of Atlantis include prefixes (`connection/`, `world/access/`, `schema.h`, `result.h`,
     `assert.h`, `asset_system/asset_guid.h`, `editor/`);
   - the link list Connection + ImGui.
7. **UI** (`src/editor/src/view/editor_view.cpp`):
   - two toolbar rows (Spec 0062);
   - `widgetRect()` keys;
   - key handling gated on `io.WantTextInput`.
8. **Gate applicability:** `src/editor/**` and `tests/**` are excluded, so the gate is N/A.

## Plan-stage decisions

- **P1 — The record** (`model/edit_transaction.h`, header only):
  - `PropertyWrite{scene::PropertyAddress address; PropertyValue before; PropertyValue after;}`.
  - `EditTransaction{std::string label; std::vector<PropertyWrite> writes;}`.
  - Equality and nothing else. `PropertyValue` is `world::access::PropertyValue` (Spec 0063 Risks).
- **P2 — The target interface** (`model/edit_target.h`):

  ```
  struct EditHandle { std::uint64_t first = 0; std::uint64_t count = 0; };   // E2: a TransactionTicket
  enum class Settlement : std::uint8_t { Pending, Applied, Refused };
  struct Invalidation { EntityGuid entity; std::optional<schema::TypeId> component; };  // none = whole entity
  class EditTarget {
    virtual std::optional<PropertyValue> read(const scene::PropertyAddress&) = 0;
    virtual EditHandle submit(std::span<const std::pair<scene::PropertyAddress, PropertyValue>>) = 0;  // tracked
    virtual void track(EditHandle, std::span<const std::pair<scene::PropertyAddress, PropertyValue>>) = 0;  // submitted elsewhere
    virtual Settlement settlement(EditHandle) const = 0;   // Refused carries no detail; refusalText(handle) gives it
    virtual std::string refusalText(EditHandle) const = 0;
    virtual void update(std::span<const CommandFailure> failures) = 0;   // once per editor frame
    virtual std::vector<Invalidation> drainInvalidations() = 0;
  };
  ```

  `track()` exists because the Inspector and the Gizmo submit their own edits. The history only adopts them.
- **P3 — `RuntimeWorldEditTarget`** (`model/runtime_world_edit_target.{h,cpp}`):
  - **Subscription:** one subscription (`PropertyChanged`, `EntityDestroyed`, `ComponentRemoved`, every entity),
    drained in `update()`.
  - **Applied:** a tracked handle is applied when its writes' events have all been seen, in order and with the
    written values.
  - **Refused:** when a failure's ticket falls in the handle.
  - **Invalidations:** destroyed entities and removed components.
  - **Address conversion:** `scene::PropertyAddress` → `access::PropertyAddress` field for field.
  - **Forgetting:** settled handles are forgotten once read.
- **P4 — `EditHistory`** (`model/edit_history.{h,cpp}`):
  - **Recording and stacks:**
    - `record(EditTransaction, EditHandle)` (pending; a non-empty redo stack is cleared at once);
    - an undo stack and a redo stack, at most 128 entries;
    - `undo()` / `redo()` return a `HistoryResult` (`Submitted`, `NothingTo`, `Waiting`, `ChangedElsewhere`,
      `Invalidated`).
  - **Queries for the UI:** `canUndo()`, `canRedo()`, `undoLabel()`, `redoLabel()`, `waiting()`, and `notice()`
    (the last result's text).
  - **`update(failures)`:** calls the target's `update()`, then settles pending entries (applied → the right stack;
    refused → dropped, with a notice), then applies invalidations.
  - **`clear()`.**
  - **Conflict check** (R5): `read()` each address and compare it with the expected value by `PropertyValue`
    equality. Floats compare by value, and the World stores and returns exactly what was written.
- **P5 — Gizmo:** `std::optional<CompletedDrag> takeCompletedDrag()`.
  - `flush()` fills it on a release whose final columns differ from `dragStart_`:
    `{subject, mode, start columns, final columns, release TransactionTicket}`.
  - Preview flushes never fill it.
  - The flush contract and its tests are unchanged.
- **P6 — Inspector:** `std::vector<CompletedEdit> takeCommits()`, filled by `commit()` with
  `{address, field path, before (the field's shown value), after, CommandTicket}`.
  - A field whose value was unreadable records nothing.
  - `commit()`'s submission and return are unchanged (J2).
- **P7 — Editor wiring** (`editor.{h,cpp}`):
  - `Impl` owns a `RuntimeWorldEditTarget` and an `EditHistory`.
  - `frame()`:
    1. drains failures as today and passes them to the Inspector and `history.update()`;
    2. clears the history when `status().scene` changes;
    3. runs the view;
    4. after the Gizmo's flush, records `takeCompletedDrag()` and `takeCommits()`, in that order (the Gizmo's flush
       comes last in the frame, after the view's commits).
  - `Editor::history()` is the model accessor, like `gizmo()`.
  - Labels:
    - a drag: "Move|Rotate|Scale <first 8 GUID hex>";
    - a commit: "Set <Type.path>".
- **P8 — View:**
  - **Toolbar:** row 1 gains Undo and Redo buttons, `widgetRect` keys `toolbar:Undo` and `toolbar:Redo`. Their
    tooltip is the label or the disabled reason.
  - **Status:** row 2 gains the history notice (`status:history`).
  - **Shortcuts:** handled once per frame at the layout, not only over the Viewport, when `!io.WantTextInput` and
    no Gizmo drag is in progress:
    - Ctrl+Z → `undo()`;
    - Ctrl+Y or Ctrl+Shift+Z → `redo()`.
- **P9 — Boundary scan:**
  - add `"atlantis/asset_system/scene_property_address.h"` to `kAllowedAtlantisPrefixes`, exactly that header;
  - keep the link-list check unchanged.

## Milestones / Task Breakdown

Each milestone builds and runs the editor suites (Debug) before the next (AGENTS.md).

### M1 — Record, target interface, history (R1, R4, R5, R6, R8)

- `edit_transaction.h`, `edit_target.h`, `edit_history.{h,cpp}` (P1, P2, P4).
- GPU-free tests against a **scripted test-double target**, with no connection:
  - record → pending → applied → undoable;
  - refused → dropped;
  - undo/redo submissions carry every `before` / `after` in order, as one submit;
  - a new edit clears redo;
  - depth 128;
  - `clear()`;
  - a conflicting read → `ChangedElsewhere`, nothing submitted, entry dropped;
  - invalidation of an entity or a component → entries dropped, including after GUID reuse;
  - `Waiting` while anything is pending.

  These double as the seam's proof (Spec 0063 R8).

### M2 — The Runtime World target (R3, R6, R7)

- `runtime_world_edit_target.{h,cpp}` (P3).
- Tests over the InProcess fixture, with the fake Runtime's frames:
  - applied: a two-write transaction; a one-command edit taken as `{ticket, 1}`;
  - refused: a bad address;
  - pending while paused, applied after a step;
  - another connection's identical write does not settle a handle out of order;
  - `EntityDestroyed` / `ComponentRemoved` → invalidations;
  - `read()` of a removed component → empty.

### M3 — Recording from the Inspector and the Gizmo (R2)

- `gizmo.{h,cpp}` (P5), `inspector.{h,cpp}` (P6), `editor.{h,cpp}` (P7).
- Model tests:
  - **Drags:**
    - a 30-update drag → one entry of four writes, `before` = start, `after` = final;
    - preview transactions are not recorded;
    - an unchanged drag records nothing;
    - a drag refused at release (entity destroyed mid-drag) records nothing.
  - **Commits:** an Inspector commit → one entry, `before` = the shown value; an `Optional` cleared → `after` =
    `Absent`.
  - **Undo through the Editor:** restores `WorldMatrix` bit-exactly, and the Inspector shows the restored value.
  - **Scene change:** a scene-GUID change clears the history.
  - **Two connections:** the fixture's second connection makes the next undo `ChangedElsewhere`.

### M4 — The view (R9)

- `editor_view.{h,cpp}` (P8); `tests/editor/editor_boundary_tests.cpp` (P9).
- Headless UI tests:
  - Ctrl+Z, Ctrl+Y and Ctrl+Shift+Z act;
  - Ctrl+Z inside the Hierarchy filter or an Inspector text box does not undo;
  - Undo is ignored mid-drag;
  - the buttons are disabled with reasons and labelled;
  - the status line shows "changed elsewhere".

### M5 — North star, normative docs, acceptance

- **North star:** `tests/runtime/runtime_smoke_gpu_tests.cpp`, one case (fatal VVL, default scene, the Spec
  Summary's sequence):
  - **The drag:** a 30-frame Gizmo drag; Ctrl+Z (via Platform key events, as Spec 0062's north star does) → bit-exact
    `WorldMatrix`, and a Viewport byte-identical to before the drag; Ctrl+Y → byte-identical to after it.
  - **The light:** `Light.intensity` 3 → 6 → undo → 4, exact in frame data.
  - **The second writer:** a second connection writes 5; the next undo is refused, and frame data still says 5.
- **Docs:**
  - AGENTS.md: one sentence in the Atlantis Editor paragraph (the Editor Transaction and the client-side history,
    ADR-0124, with the allow-list's added header);
  - `docs/architecture/module_boundaries.md`, the Editor section.
- **Acceptance:**
  - Debug and Release full suites;
  - goldens unchanged;
  - VVL clean;
  - `assembleDebug`;
  - the path check `git diff origin/main --name-only` limited to `src/editor/`, `tests/editor/`,
    `tests/runtime/runtime_smoke_gpu_tests.cpp`, AGENTS.md, `module_boundaries.md` and this Plan's docs;
  - performance gate N/A, with the reason;
  - the owner's interaction run (J6).

## Files / Modules Touched (expected)

- **Editor:**
  - `src/editor/include/atlantis/editor/model/edit_transaction.h` (new);
  - `src/editor/include/atlantis/editor/model/edit_target.h` (new);
  - `src/editor/include/atlantis/editor/model/edit_history.h` and `src/editor/src/model/edit_history.cpp` (new);
  - `src/editor/include/atlantis/editor/model/runtime_world_edit_target.h` and
    `src/editor/src/model/runtime_world_edit_target.cpp` (new);
  - `src/editor/include/atlantis/editor/model/gizmo.h` and `src/editor/src/model/gizmo.cpp`;
  - `src/editor/include/atlantis/editor/model/inspector.h` and `src/editor/src/model/inspector.cpp`;
  - `src/editor/include/atlantis/editor/editor.h` and `src/editor/src/editor.cpp`;
  - `src/editor/src/view/editor_view.h` and `src/editor/src/view/editor_view.cpp`;
  - `src/editor/CMakeLists.txt`.
- **Tests:**
  - `tests/editor/editor_model_tests.cpp`, `tests/editor/editor_view_tests.cpp`,
    `tests/editor/editor_boundary_tests.cpp`, `tests/editor/editor_fixture.h` (if a second-connection helper is
    needed);
  - `tests/runtime/runtime_smoke_gpu_tests.cpp`.
- **Docs:** `AGENTS.md`, `docs/architecture/module_boundaries.md` (M5); registry, blueprint, README and status lines
  after merge (J5).
- **Not touched:** everything else, in particular `src/world/`, `src/runtime/`, `src/connection/`, `src/remote/`,
  `src/cli/`, `src/gameplay_sdk/`, `src/csharp/`, `src/asset_system/`, `src/platform/`, `src/rhi/`,
  `src/renderer/`, `shaders/`, goldens and `tests/performance/`.

## Sequencing & Dependencies

- M1 first: the history is proven against a test double before any real target exists.
- M2 depends on M1's interface. M3 needs M1 and M2. M4 needs M3. M5 is last.
- One branch, `feature/0063-editor-transaction-undo`, one PR.

## Verification Checklist

- [ ] Unit (GPU-free):
  - history over the test-double target (M1);
  - the Runtime World target (M2);
  - recording and undo through the Editor (M3);
  - headless UI (M4).
- [ ] GPU (fatal VVL): the E2 north star (M5), including the byte-identical Viewport after undo and redo.
- [ ] Image regression: all goldens pass, none changed.
- [ ] Vulkan Validation Layers clean.
- [ ] Other:
  - boundary scan, with the allow-list's one added header and the link list unchanged;
  - path check (M5);
  - Debug and Release full suites;
  - `assembleDebug`;
  - performance gate N/A, stated;
  - the owner's run (J6).

## Risks

- **Event-match settlement** (P3) relies on in-order events with exact values. The M2 tests cover interleaving with
  another writer.
- **Recording order within a frame** (P7): a drag release and an Inspector commit in the same frame are recorded
  drag first. They touch different gestures and cannot conflict in the UI, since one pointer cannot both drag and
  type.
- **UI-library shortcut capture:** Ctrl+Z inside a text field must stay the field's. M4 tests it.

## Joint Review decisions — ruled (slmao, 2026-10-20): J1–J7 as proposed, J1 with the R5 correction

- **J1 — Spec Corrections:** none proposed.
- **J2 — The Inspector keeps submitting one `SetProperty`** (P6). Its ticket becomes the handle `{ticket, 1}`. The
  proposal is to accept. The alternative, a one-command transaction, changes the Inspector's submission and its
  tests for no semantic gain.
- **J3 — Labels** (P7): "Move|Rotate|Scale <GUID prefix>" and "Set <Type.path>". The proposal is to accept.
- **J4 — Shortcut scope** (P8): the whole window, outside text fields and drags. The proposal is to accept, since
  undo is not a Viewport-only action.
- **J5 — Docs split:** as Plans 0056 and 0062.
- **J6 — Acceptance run:** the owner runs `atlantis_runtime --editor` on the default scene and Bistro:
  - drag and undo;
  - field edits;
  - a CLI write (`--listen` plus `atlantis`) making an undo refuse;
  - an undo while paused waiting, and applying on Step.
- **J7 — Settlement match** (P3): every write's event must be seen, in order. The proposal is to accept; matching
  the last write only would accept a transaction whose earlier events another writer's identical values shadowed.

## Rollback Plan

Revert the implementation PR. Everything is inside the editor library and its tests, plus two normative doc
sentences. No format, golden, baseline or protocol depends on it.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:

- Performance gate N/A, stated with the reason (no frame-path file changes).
- The owner's interaction run (J6) is recorded in the PR.
