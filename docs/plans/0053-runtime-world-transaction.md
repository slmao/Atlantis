# Plan: Runtime World Transaction

- **Spec:** [Spec 0053: Runtime World Transaction](../specs/0053-runtime-world-transaction.md)
  (`Approved`, 2026-10-07, [PR #218](https://github.com/slmao/Atlantis/pull/218);
  rulings Q1–Q7 binding) —
  [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) (`Accepted`).
  It extends [ADR-0103](../adr/0103-runtime-world-operation-boundary.md) D9
  and changes no ADR-0103 decision. It builds on
  [Spec 0052](../specs/0052-runtime-world-query-command-event-foundation.md)'s
  boundary as merged in [PR #216](https://github.com/slmao/Atlantis/pull/216),
  following [Plan 0052](0052-runtime-world-query-command-event-foundation.md)'s
  test layout and gates.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-07 — reviewed this Plan and
  [Spec 0053](../specs/0053-runtime-world-transaction.md) together in
  [PR #219](https://github.com/slmao/Atlantis/pull/219) and explicitly authorized Implementation from Milestone 1.
  J1–J9 were ruled as recommended. J1's Spec Correction is recorded in
  Spec 0053's header (Correction 2026-10-07). See Joint Review decisions
  below.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0053 R1–R8 as ruled:

- `RuntimeWorldAccess::submitTransaction()`, an all-or-nothing group of
  Spec 0052 commands, returning a `TransactionTicket`;
- atomicity by projected pre-validation (ruling Q1) against shadow state.
  Execution cannot be refused; a refusal there is an `ATLANTIS_CHECK`;
- Spec 0052's per-command checks refactored into one rule routine over a
  state view, with real and projected implementations (ruling Q7);
- the smoke test's Point-light creation group moved into one transaction
  (ruling Q6, C2).

Runtime code does not change. Every golden stays byte-identical; nothing is
re-captured.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `4bb1640` (PR #218 merged).

1. **The validate-apply-event path today**
   (`src/world/src/access/runtime_world_access.cpp`):
   - `Impl::apply(const C&)` overloads (`:110`–`:216`) interleave the checks
     and the ECS calls for each command. Each overload returns
     `Result<Event, AccessError>`, and every check precedes the overload's
     first ECS call.
   - The state the checks read:
     - `lookup()` (`:76`): GUID index plus `isValid`;
     - `has<T>()`;
     - `isActiveCamera()` (`:82`);
     - `countLights(kind, except)` (`:95`): one `query<const Light, const
       WorldMatrix>` per call;
     - `lightFits()` (`:105`);
     - the current `Light.kind`, for `SetProperty(Light.kind)` (`:204`).
   - `resolveField()` and `checkValue()` (`property_access.h`) are
     stateless.
   - `submit()` (`:260`) takes `++lastTicket`. `applyPending()` (`:266`)
     walks `pending` (`std::vector<std::pair<CommandTicket, Command>>`),
     pushing one event or one failure per command.
2. **Check precedence today.** This is the order R6 must keep:

   | Command | Refusals, in order |
   |---|---|
   | `CreateEntity` | `NilGuid` → `DuplicateGuid` |
   | `DestroyEntity` | `UnknownEntity` → `ActiveCameraProtected` |
   | `AddComponent` | `UnknownEntity` → `UnknownComponentType` → `ComponentAlreadyPresent` → `LightLimitExceeded` |
   | `RemoveComponent` | `UnknownEntity` → `UnknownComponentType` → `ComponentMissing` → `ActiveCameraProtected` (`Camera`/`WorldMatrix` only) |
   | `SetProperty` | `UnknownEntity` → `resolveField` (`UnknownComponentType`, `UnknownField`) → `ComponentMissing` → `checkValue` (`FieldNotEditable`, `KindMismatch`, `EnumValueOutOfRange`, `NonFiniteValue`) → `LightLimitExceeded` (only `Light.kind`, with a `WorldMatrix`, and only when the kind changes) |

3. **Bounded light passes.** `countLights` iterates only Light +
   WorldMatrix holders. The guards and the scene schema cap these at 1 + 64,
   so one pass is O(1).
4. **Light kinds and component types.**
   - `LightKind { Directional, Point }` (`light.h:7`);
   - `ecs::WorldComponentTypes` = `Transform, Camera, Light, Renderable,
     WorldMatrix` (`world_components.h:49`).
5. **Runtime.**
   - `runFrame()` calls `worldAccess_->applyPending()` first
     (`runtime_application.cpp:1168`). It ignores the report.
   - Transactions use the same pending queue, so Runtime needs no change
     (Spec 0053 ruling Q6, verified).
6. **The C2 target.** `tests/runtime/runtime_smoke_gpu_tests.cpp:405`–`:415`
   submit nine commands, then call `runFrame()`. The test then checks:
   - no failures;
   - nine events, the first `EntityCreated` and the last
     `PropertyChanged(column3)`;
   - the `FrameLightingData` bytes, including the directional/point counts,
     position, color and intensity.
7. **The existing World access tests** are
   `world_access_{property,query,command,validation}_tests.cpp` and
   `tests/runtime/runtime_world_access_tests.cpp`. They duplicate a
   cooked-scene fixture: a renderable, a Directional light, a Point light,
   the active camera, and a bare node.
8. **`ATLANTIS_CHECK`** is evaluated in Debug and Release
   (`assert.h:35`).

## Plan-stage decisions

These are details Spec 0053 leaves to the Plan. None changes a Spec
requirement or an ADR-0104 decision. J1 corrects only ruling Q2's wording
(Spec 0053 Correction 2026-10-07).

**P1 — Files.** All under the World module.

| File | Contents |
|---|---|
| `src/world/include/atlantis/world/access/runtime_world_access.h` | `TransactionTicket`; `submitTransaction()`; the class and `applyPending()` comments |
| `src/world/src/access/command_rules.h` (new, private) | the state-view concept and `checkCommand()`, **the single rule source** (P3) |
| `src/world/src/access/projected_world_state.h` / `.cpp` (new, private) | the shadow state: a state view plus `project(command)` bookkeeping (P4) |
| `src/world/src/access/runtime_world_access.cpp` | the real state view, the executors, pending entries, transaction application (P5) |

`access_error.h` and `property_access.*` are unchanged: no error kind and no
accessor change.

**P2 — Public API** (R1, R4; rulings Q2, Q5).

```cpp
struct TransactionTicket {
  CommandTicket first;          // the first command's ticket; {} (value 0) for an empty transaction
  std::uint64_t count = 0;      // commands in the transaction
  [[nodiscard]] constexpr bool contains(CommandTicket t) const noexcept;  // first <= t < first + count
  friend bool operator==(const TransactionTicket&, const TransactionTicket&) = default;
};
TransactionTicket submitTransaction(std::vector<Command> commands);  // sink, like submit(Command)
```

- **Tickets.** The transaction's commands take consecutive tickets
  (`lastTicket + 1 … lastTicket + count`). A failure's position in the
  transaction is `failure.ticket.value − first.value`.
- **Empty transaction (ruling 4d).** Returns `{CommandTicket{}, 0}`, takes no
  ticket and queues nothing.
- **`ApplyReport`, unchanged in shape.**
  - A committed transaction adds `count` to `applied`.
  - An aborted one adds its single failure to `failures`.
- **`submit(Command)` is unchanged in signature and behaviour** (R6).

**P3 — One rule source over a state view** (R7; ruling Q7; ADR-0104 D3).

```cpp
template <typename V>
concept WorldStateView = requires(const V& v, const EntityGuid& g, schema::TypeId t, LightKind k) {
  { v.exists(g) } -> std::same_as<bool>;                 // a live entity names g
  { v.has(g, t) } -> std::same_as<bool>;                 // pre: exists(g), t a World component type
  { v.lightKind(g) } -> std::same_as<LightKind>;         // pre: has(g, Light)
  { v.isActiveCamera(g) } -> std::same_as<bool>;
  { v.lightCount(k, g) } -> std::same_as<std::uint32_t>; // Light+WorldMatrix holders of kind k, other than g
};
template <WorldStateView V>
[[nodiscard]] std::optional<AccessError> checkCommand(const V& view, const Command& command);
```

- **The checks move out of the `apply` overloads.** Every check of the five
  `Impl::apply` overloads moves, in the exact precedence of reading item 2,
  into `checkCommand`. The overloads keep only ECS lowering, index updates
  and the event: they become executors.
- **Keyed by GUID.** The view is keyed by GUID, so a projected entity that
  has no `EntityId` yet is addressed like a real one. No `ecs::` type
  appears in the rule code.
- **Instantiated twice.** `checkCommand` is a template, instantiated for
  `RealWorldState` (P5) and `ProjectedWorldState` (P4). The same rule text
  runs in both; only the views differ (J2).
- **`lightFits` stays a rule.** It is
  `lightCount(kind, g) + 1 <= limit(kind)`, with the limit constants of
  `runtime_world_access.h`.

**P4 — Shadow state** (R2, R3; ruling Q1; Spec 0053 Proposed Design).

```cpp
struct ShadowEntity { bool live; std::bitset<5> components; LightKind lightKind; bool activeCamera; };
class ProjectedWorldState {                 // models WorldStateView
  const RealWorldState& real_;              // read-only during projection
  std::map<EntityGuid, ShadowEntity> entities_;   // lazily seeded
  std::array<std::uint32_t, 2> lightCounts_;      // by LightKind; seeded once
 public:
  void project(const Command&);             // pre: checkCommand(*this, c) passed
};
```

- **Lazy entity seeding.** A GUID is seeded from `real_` the first time a
  command touches it:
  - `live = real.exists(g)`;
  - the component bitset over `WorldComponentTypes`;
  - its `Light.kind` if it has one;
  - `isActiveCamera`.

  The real world does not change during projection, so seeding late equals
  seeding at the start. Projected state is O(entities touched).
- **Light counts, one pass.** The counts are taken in one
  `query<const Light, const WorldMatrix>` over both kinds when the
  transaction's projection starts (J4). They are then maintained by delta:
  for the touched GUID, `project()` computes "counted under kind *k*"
  (`live ∧ Light ∧ WorldMatrix ∧ kind = k`) before and after the command,
  and adjusts the counts by the difference. That one rule covers:
  - `AddComponent(Light | WorldMatrix)`;
  - `RemoveComponent(Light | WorldMatrix)`;
  - `DestroyEntity`;
  - `SetProperty(Light.kind)`.
- **`lightCount(k, g)`** is `lightCounts_[k]`, minus 1 if `g` is counted
  under `k`. This is the same "other than g" semantics as `countLights`'s
  `except`.
- **`project()` per command:**

  | Command | Shadow effect |
  |---|---|
  | `CreateEntity(g)` | `live`, no components, not the active camera |
  | `DestroyEntity(g)` | not live, components cleared |
  | `AddComponent` / `RemoveComponent` | the bit set or cleared; adding `Light` sets `lightKind = Light{}.kind` |
  | `SetProperty` | only `Light.kind` updates `lightKind`; other fields are not validation inputs |

- **4a and 4b.** A projected destroy makes later commands on `g` see
  `UnknownEntity` (4a). A later `CreateEntity(g)` is accepted and `g` names
  the new shadow entity (4b). Both are exactly what sequential application
  does.

**P5 — Application** (R1–R6; rulings Q1–Q4; ADR-0104 D2).

```
pending: vector<PendingEntry{ CommandTicket first; variant<Command, vector<Command>> body }>
applyPending():
  for entry in pending (submission order):
    single     -> err = checkCommand(real, c); err ? failure{first, err} : (execute(c), event)   // R6
    transaction-> proj = ProjectedWorldState(real)
                  for i, c: err = checkCommand(proj, c); if err: failure{first+i, err}; abort entry
                                proj.project(c)
                  for c: ATLANTIS_CHECK_MSG(!checkCommand(real, c)); execute(c); event   // J3
```

- **`RealWorldState`** is a view over `Impl`: the index plus `isValid`,
  `has<T>` via `visitComponentType`, `get<Light>().kind`, `activeCamera`,
  and `countLights` as today.
- **Projection starts when the entry is reached.** It sees every earlier
  pending entry already applied, singles and transactions alike (R1
  ordering).
- **Abort (R3).** No ECS call, no index change, no event. Exactly one
  `CommandFailure` (R4, ruling Q2 F1).
- **Commit (R2, R5).** Each command is re-checked against the real view and
  then executed in order, appending its event. A refusal there means the
  projection drifted, a programming error. `ATLANTIS_CHECK_MSG` stops the
  process, so no partial commit can be observed (ADR-0104 D2).
- **No transaction-level event (ruling Q3).**
- **The active camera (ruling 4c)** is a per-command rule in
  `checkCommand`, so a refusal anywhere aborts. There is no
  end-of-transaction check and no deferred guard.

**P6 — Smoke-test migration** (ruling Q6, C2).

- **The change.** The nine `boundary.submit(...)` calls at
  `runtime_smoke_gpu_tests.cpp:405`–`:415` become one
  `boundary.submitTransaction({...})` with the same nine commands in the
  same order. The test adds `REQUIRE(ticket.count == 9)`.
- **Kept byte-for-byte:**
  - every existing check after the `runFrame()` (failures empty, nine
    events with the same first and last, every `FrameLightingData` field
    check);
  - the later single-command move and destroy steps.
- **Validation.** The test runs under fatal VVL, as today.
- **Ordering inside the transaction.** The order J1 (Spec 0052) needs,
  `Light` and `kind = Point` before `WorldMatrix`, holds per command inside
  the transaction (P3 rules are per command).

## Milestones / Task Breakdown

Six milestones. Commit prefixes: `test:` for test-only steps, `refactor:`
for M2, `feat:` for M3.

Every gate runs:

- a Debug + Release build;
- the full `ctest` in both configurations, with every golden compared exactly;
- the golden-directory guard;
- the path guard (Verification), which also reports the count of changed
  files under `src/runtime/`, required to be 0;
- Android `assembleDebug`.

GPU suites run under fatal VVL.

**Stop rule:** a moved golden stops the work and is reported, never
re-captured.

### M1 — Pin today's check precedence (R6 groundwork; J6)

1. `tests/world/world_access_precedence_tests.cpp` (new), against the
   **current** code. For each command, inputs with two or more faults
   refuse with the first refusal of reading item 2. Examples:
   - `SetProperty` with an unknown field on an entity lacking the
     component gives `UnknownField`;
   - a missing component plus a NaN value gives `ComponentMissing`;
   - `RemoveComponent(Camera)` on the active camera when the type is
     unknown gives `UnknownComponentType`.
2. `tests/world/CMakeLists.txt`.

No `src/` change. *Gate:* standard.

### M2 — Refactor validation into the state-view routine (R6, R7; ruling Q7)

1. `command_rules.h` (P3) and `RealWorldState` (P5). The `Impl::apply`
   overloads become executors, and the single path becomes
   `checkCommand(real, c)` → execute.
2. **No public header change, no test change.** R6's proof is that every
   Spec 0052 test (`world_access_*`, `runtime_world_access_tests`, the
   smoke test) and M1's precedence tests pass **unmodified**. The path
   guard shows those files untouched.

*Gate:* standard.

### M3 — Transactions (R1–R5, R8; rulings Q1–Q5)

1. `TransactionTicket`, `submitTransaction()`, pending entries, and
   `projected_world_state.h/.cpp` (P2, P4, P5). CMake sources.
2. `tests/world/world_access_transaction_tests.cpp` (new):
   - **Commit:**
     - each command kind;
     - the Motivation's Point-light group;
     - the world equals the same commands applied one by one, compared
       with a boundary snapshot (P7);
     - events one per command, in order;
     - `ApplyReport.applied == count`.
   - **Abort**, for each reachable `AccessError`, placed first, middle and
     last:
     - the boundary snapshot is unchanged (every GUID's existence,
       component set and every leaf field);
     - no event;
     - exactly one failure, whose `ticket − first` is the position.
   - **Guards inside the transaction:**
     - a 65th Point light and a second Directional light arising
       mid-transaction abort;
     - an earlier `DestroyEntity` of a Point light frees room for a later
       one, which commits;
     - an earlier command takes the last slot and a later one aborts.
   - **Ruling Q4:**
     - **(4a)** `[Destroy(g), SetProperty(g…)]` aborts at 1 with
       `UnknownEntity`, and `g` still exists;
     - **(4b)** `[Destroy(g), Create(g), AddComponent(g, Light)]` commits,
       and `g` names a new entity with only `Light`;
     - **(4c)** a refused `DestroyEntity(camera)` or
       `RemoveComponent(camera, Camera|WorldMatrix)` at first, middle and
       last aborts. `[RemoveComponent(camera, WorldMatrix),
       AddComponent(camera, WorldMatrix)]` aborts at 0, so no end-state
       check exists;
     - **(4d)** `submitTransaction({})` returns `{CommandTicket{}, 0}`; the
       next `submit()` ticket is unchanged; `applyPending()` yields no
       event, no failure and `applied == 0`.
   - **Ordering:**
     - `submit`, `submitTransaction`, `submit` take tickets 1, 2–3, 4 and
       apply in that order;
     - a transaction that depends on a single command earlier in the same
       `applyPending()` commits;
     - a single command after an aborted transaction still applies;
     - `TransactionTicket::contains`.

*Gate:* standard.

### M4 — Sequential-equivalence test (R7; ruling Q7; J7)

`tests/world/world_access_transaction_equivalence_tests.cpp` (new):

- **The generator.** An in-test `splitmix64` (no `std::` distributions,
  whose output is implementation-defined), driven by a fixed list of
  seeds. It draws from:
  - GUIDs: baked nodes, the active camera, fresh GUIDs, nil;
  - every World component type, plus an unknown one;
  - fields: `Light.kind` with values 0, 1 and out of range, finite and
    non-finite floats, wrong kinds, `Absent`, an unknown field.
- **Two kinds of sequence:**
  - random sequences;
  - oracle-built sequences: commands kept only if a scratch world accepts
    them, so they commit. Some have one refusable command spliced in at a
    seeded position.

  Both kinds also run in a near-limit setup with 62 extra Point lights, so
  light-limit refusals occur.
- **Per sequence**, on two identically baked worlds:
  - world A applies the commands singly, and world B as one transaction;
  - B commits ⇔ A had no failure;
  - on commit, A's and B's events and snapshots are equal;
  - on abort, B's single failure equals A's first failure (position and
    error), and B's snapshot equals its pre-transaction snapshot.

*Gate:* standard.

### M5 — Smoke creation group as one transaction (ruling Q6, C2)

`tests/runtime/runtime_smoke_gpu_tests.cpp` (P6). *Gate:* standard. The
smoke test runs under fatal VVL, and every golden must be exact.

### M6 — Acceptance

1. Final path guard. `src/runtime/` shows zero changes, and the diff is
   reviewed.
2. Acceptance runs per J9.

*Gate:* standard, plus the runs.

## Files / Modules Touched (expected)

**New:**

- `src/world/src/access/command_rules.h`;
- `src/world/src/access/projected_world_state.h`;
- `src/world/src/access/projected_world_state.cpp`;
- `tests/world/world_access_precedence_tests.cpp`;
- `tests/world/world_access_transaction_tests.cpp`;
- `tests/world/world_access_transaction_equivalence_tests.cpp`.

**Changed (planned, not deviations):**

- `src/world/include/atlantis/world/access/runtime_world_access.h`: P2;
- `src/world/src/access/runtime_world_access.cpp`: P3, P5;
- `src/world/CMakeLists.txt`, `tests/world/CMakeLists.txt`: sources;
- `tests/runtime/runtime_smoke_gpu_tests.cpp`: P6.

**Not touched.** Any change to these is a deviation, reported in the PR:

- **Runtime:** anything under `src/runtime/` (ruling Q6, verified).
- **ECS and World:**
  - the ECS core and its headers;
  - `access_error.h`, `property_access.*`;
  - every other World source.
- **Existing tests:**
  - the existing `world_access_*_tests.cpp`;
  - `tests/runtime/runtime_world_access_tests.cpp`;
  - `module_boundary_tests.cpp`;
  - every other test file.
- **Everything else:**
  - Asset System, Renderer, RHI;
  - shaders, assets, goldens;
  - `AGENTS.md` and `docs/` (J8).

No dependency is added.

## Sequencing & Dependencies

- **M1 → M2.** The precedence is pinned on the old code before it is
  restructured.
- **M2 → M3.** The projection reuses `checkCommand`.
- **M3 → M4 → M5.** The API exists before its consumers.
- **M6** comes last.

## Verification Checklist

This maps to Spec 0053's Testing & Verification Plan.

- [ ] **R1 (M3):**
  - one entry point, consecutive tickets, `TransactionTicket` fields;
  - interleaved ordering with single commands.
- [ ] **R2/R5 (M3, M4):**
  - commit equals one-by-one application (world snapshot and events);
  - no transaction event.
- [ ] **R3/R4 (M3, M4):**
  - abort leaves the snapshot unchanged and emits no event;
  - one failure at the first refused command, `ticket − first` = position.
- [ ] **R6 (M1, M2):** every Spec 0052 test and M1's precedence tests pass
  unmodified after the refactor. The path guard shows them untouched.
- [ ] **R7 (M2, M4):**
  - one `checkCommand` instantiated for both views;
  - the fixed-seed equivalence test.
- [ ] **R8 (M3):** 4a, 4b, 4c (first, middle and last, no end-state check)
  and 4d (no event, no failure, no ticket).
- [ ] **Guards in transactions (M3, M4):** light limits mid-transaction,
  room freed and taken, the active camera.
- [ ] **Ruling Q6 (M5):** the smoke creation group as one transaction under
  fatal VVL, with the same events and `FrameLightingData` checks.
- [ ] **Path guard, every gate:**
  - only the files above;
  - `src/runtime/` has 0 changed files;
  - nothing under `assets/`, `tests/image_regression/`, `shaders/`, or
    the Asset System or GPU modules.
- [ ] **Goldens:** every golden exact in Debug and Release at every gate;
  none re-captured.
- [ ] **Android:** `assembleDebug` at every gate. The J9 runs at M6.

## Risks (recorded in Spec 0053, carried here)

- **No transaction size limit.** A very large transaction is validated in
  full before any effect: O(*n*) work and memory, by design.
- **Projection must track every validation input.** Any future validation
  rule that reads world state must extend both the state view (P3) and the
  shadow state (P4). The concept makes a missing view method a compile
  error. The M4 test catches bookkeeping drift, and execution's CHECK is
  the backstop.
- **No undo after commit.** A committed transaction can be reverted only by
  further commands; Undo/Redo is named only.

Plan-level:

- **The refactor touches Spec 0052's live path** (M2). M1's precedence pins
  and the unmodified 0052 suites gate it.

## Joint Review decisions — ruled (Joint Human Review, slmao, 2026-10-07, PR #219)

All nine were ruled as recommended. J1 corrects Spec 0053, recorded as a
dated Correction in its header with an in-place marker on ruling Q2.
ADR-0104 is unchanged.

- **J1 — R6 versus ruling Q2's "byte-for-byte" sentence.** **Ruled
  (2026-10-07): R6 is behavioural** (M1, M2).
  - **The conflict.** Ruling Q2 said "Spec 0052's path stays byte-for-byte
    the code it is today (R6)". Ruling Q7 refactors that path's validation
    into `checkCommand`, so the code cannot stay byte-for-byte.
  - **The Correction.** Spec 0053 Correction 2026-10-07: the path behaves
    exactly as today (R6). Its validation code is refactored by ruling Q7,
    an internal change and not a contract change, matching ADR-0104's
    Consequences.
  - **R6's proof:** every existing Spec 0052 test, plus M1's new precedence
    tests, passes unmodified after M2.
  - **Rejected:** keeping the single path's code and duplicating the rules
    for the projection, which is ruling Q7's V2.
- **J2 — State-view shape.** **Ruled (2026-10-07):** a C++20 concept, with
  `checkCommand` as a template function, keyed by GUID (P3). The rule code
  names no ECS type, has no virtual dispatch, and is written once.
  Rejected: an abstract base class with virtual methods.
- **J3 — The execution-phase CHECK.** **Ruled (2026-10-07):** once the
  projection passes, each command is re-checked against the real view
  before it executes. A refusal is `ATLANTIS_CHECK_MSG` (P5), the backstop
  ADR-0104 D2 names.
  - Each extra light pass is O(1), bounded by 1 + 64 holders (reading item
    3), so the Spec's cost requirement holds asymptotically. No Spec
    Correction is needed.
  - Rejected: checking only the ECS calls' results.
- **J4 — Seeding the shadow state.** **Ruled (2026-10-07)** (P4):
  - entities are seeded lazily, per GUID, on first touch;
  - light counts are taken in one pass when each non-empty transaction's
    projection starts, then adjusted by the touched entity's before/after
    difference in what is counted.

  Rejected: taking the counts lazily on the first light-relevant command.
- **J5 — `TransactionTicket`.** **Ruled (2026-10-07):**
  `{CommandTicket first; std::uint64_t count}` plus `contains()` (P2).
  - An empty transaction returns `{CommandTicket{}, 0}`. Ticket value 0 is
    never issued, and the empty transaction takes no ticket.
  - The parameter is `std::vector<Command>` by value, as ruling Q2 writes
    it.
- **J6 — A precedence-pinning milestone.** **Ruled (2026-10-07):** M1 first
  pins today's check precedence with multi-fault inputs, in a new test
  file. It changes neither `src/` nor Spec 0052's tests, which stay
  untouched as R6's evidence. M2's refactor follows.
- **J7 — The equivalence generator.** **Ruled (2026-10-07)** (M4):
  - an in-test `splitmix64` and a fixed seed list;
  - two kinds of sequence: purely random, and oracle-built (only commands
    a scratch world accepts are kept);
  - a light-limit saturation group on top.
- **J8 — Docs.** **Ruled (2026-10-07):** the implementation PR changes no
  docs. A post-merge docs PR follows, on the precedent of Plan 0052 J8:
  - the registry's Implementation column;
  - `module_boundaries.md`'s operation-boundary bullet;
  - the blueprint's ECS/World note;
  - Spec 0053's Related Plan(s).
- **J9 — Acceptance runs.** **Ruled (2026-10-07):** M6 repeats Plan 0052
  J9:
  - Windows: the four whitelist scenes, Debug and Release, VVL on;
  - Android emulator: the default scene (install, screencap, logcat).

  The reason is that the World code `runFrame()` calls every frame changes
  in M2. Rejected: no runs.

## Rollback Plan

- **After merge:** revert the implementation PR as a whole. There is no
  format, asset, golden or Runtime change. Spec 0052's behaviour is
  restored with the M2 revert.
- **Before merge:** revert milestone by milestone, in reverse order. M1, M4
  and M5 are test-only.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:

- [ ] Every golden exact in Debug and Release at the final gate. None
      re-captured.
- [ ] Path guard holds. `src/runtime/` is unchanged, and the existing files
      changed are only those listed.
- [ ] Every Spec 0052 test passes unmodified (R6).
- [x] J1's Spec Correction recorded (Spec 0053 Correction 2026-10-07).
- [ ] The post-merge docs items (J8) are queued.
