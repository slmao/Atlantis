# Plan: Gameplay SDK

- **Spec:** [Spec 0057: Gameplay SDK](../specs/0057-gameplay-sdk.md)
  (`Approved`, 2026-10-09, [PR #234](https://github.com/slmao/Atlantis/pull/234);
  rulings Q1–Q10 binding, with review rounds 1 and 2 folded in) —
  [ADR-0110](../adr/0110-gameplay-sdk-client-library-and-execution-model.md)
  (module, layers, query batch, execution model, D1–D7) and
  [ADR-0111](../adr/0111-schema-generated-typed-bindings.md) (generated
  typed bindings, D1–D5), both `Accepted`.
  - The SDK is a client of [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md)'s
    `RuntimeConnection` and [ADR-0106](../adr/0106-attachable-runtime-transport-and-control.md)'s
    `RuntimeControl`; neither changes.
  - It keeps [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md)
    D4 (the descriptor tables stay hand-authored),
    [ADR-0103](../adr/0103-runtime-world-operation-boundary.md),
    [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md) and
    [ADR-0004](../adr/0004-phase1-threading-baseline.md).
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-09 — reviewed this Plan and
  [Spec 0057](../specs/0057-gameplay-sdk.md) together in [PR #235](https://github.com/slmao/Atlantis/pull/235) and
  explicitly authorized Implementation from Milestone 1. J1–J12 were ruled
  as recommended (each one's first option). No Spec Correction was made
  (J1); ADR-0110 and ADR-0111 are unchanged. See Joint Review decisions
  below.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0057 R1–R10 as ruled:

- **Atlantis Gameplay SDK** (`src/gameplay_sdk/`), a client library over
  `RuntimeConnection` only, with:
  - a reflective layer;
  - a typed layer generated from `worldSchema()` by a host tool, with the
    output committed;
  - an optional transport-neutral `QueryBatch`.
- **One real client**, `atlantis_gameplay_demo`, attached over Atlantis
  Remote, proving the North star.

Runtime, World, Connection and Remote are not changed. Every golden and
`runFrame()` stay byte-identical.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `702dfa1` (PR #233 merged), with Spec 0057 as approved
on `spec/0057-gameplay-sdk` (`6ad3b27`).

1. **The connection's surface** (`src/connection/include/atlantis/connection/`):
   - `runtime_connection.h`: the Query, Command, Transaction and Event
     calls; `EventFilter`, `SubscriptionId`, `ConnectionError`; not
     thread-safe;
   - `runtime_control.h`: `RuntimeStatus`, `StepRequest`, `FrameReport`
     (`FrameData` with point lights' position, colour, intensity and
     range);
   - `text.h`: `parsePath`, `findType` (short or qualified name),
     `leavesOf`, `shortName` — one path grammar, shared by the CLI and the
     editor.
2. **The value types** (`world/access/runtime_world_access.h`):
   - `PropertyValue`'s eight alternatives: `UInt64`, `Float32`, `Vec3`,
     `Vec4`, `AssetGuid`, `EntityGuid`, `EnumValue{int64}`, `Absent`;
   - the five commands; `TransactionTicket::contains`; `AccessError`
     (including `EnumValueOutOfRange`, `NonFiniteValue`,
     `LightLimitExceeded`).
3. **The schema** (`core/include/atlantis/schema.h`,
   `world/src/world_schema.cpp`):
   - eight types — five components; `CameraFog` and `CameraBloom` nested
     under `Camera`; `LightKind` with `Directional = 0`, `Point = 1`;
   - 24 component leaves, all `Editable`;
     `Renderable.materialAsset` `Optional`;
   - `FieldDescriptor::type` names a referenced struct or enum;
   - `isWellFormed` is `constexpr`, and so are `typeId`/`fieldId`.
4. **The batch precedent:** `cli::QueryBatch` (`command_layer.h:63`). Its
   default asks one at a time; `RemoteBatch` in `src/cli/app/main.cpp:28`
   and `NorthStarBatch` in `tests/runtime/runtime_smoke_gpu_tests.cpp`
   adapt `RemoteSession::listComponents` / `getProperties`.
5. **The in-process loopback with real control**
   (`runtime_smoke_gpu_tests.cpp`, `Attached`, Plan 0055 M7):
   - a windowed `RuntimeApplication` with a `RuntimeControlHost` from
     `forApplication()`;
   - a real `RemoteServer` on loopback;
   - the client's `whileWaiting` runs `beforeFrame(); runFrame();
     afterFrame(); server->poll();` — the frames `--listen` runs.
6. **Hold semantics** (`runtime_control_host.cpp`, `runtime_control_tests.cpp`
   "paused frames are held…"):
   - `beforeFrame()` holds application when paused and no step has
     started;
   - `afterFrame()` increments the frame number on every frame;
   - steps queue and run one at a time.
7. **The two-process harness** (`tests/cli/cli_e2e_tests.cpp`, Plan 0055 M7):
   - `CreateProcessW` with piped stdio;
   - `atlantis_runtime --listen 0 --session-file <path>`;
   - a graceful close: `WM_CLOSE`, then exit code 0;
   - test-only Windows code, built `if(WIN32)`.
8. **Remote session lookup:** `remote::resolveSessionPath(explicit, env)`,
   `readSessionFile`, `connectRemote` (`remote_client.h`). Remote's client
   half builds on both OSes (`socket_win32.cpp`, `socket_posix.cpp`).
9. **Build layout:**
   - host tools are a `*_lib` STATIC plus a thin executable
     (`src/tools/asset_cooker/`), added inside `if(NOT ANDROID)`;
   - examples are added inside `if(ATLANTIS_BUILD_EXAMPLES)`;
   - tool tests live under `tests/tools/<tool>/`;
   - Gradle `targets` list `atlantis_editor` (Plan 0056 J11).
10. **Line endings:** `core.autocrlf=true` on the development machine, and
    `.gitattributes` holds only `*.png binary`. A committed text file is
    CRLF in the working tree, so a byte-for-byte comparison with LF
    generator output would fail on checkout alone (J2).
11. **No compile-failure tests exist** in the repository (no `WILL_FAIL`,
    no `try_compile`). Plan 0023's C4062 probes were run by hand (J3).
12. **The default scene** (`assets/scenes/integrated_showcase_demo.scene.txt`):
    - spheres at `(±1.3, 1, ±1.3)`, `(0, 1, -3.2)` and a ground mesh at
      the origin;
    - the Directional light `0b2c1db2-…` (intensity 3);
    - the camera at `(0, 6, 10)`, pitched down;
    - no point lights.

    The smoke test's Point light at `(1, 1, 1)` (range 5) is extracted, and
    its nine-command transaction is the order to keep (Plan 0052 J1).

## Plan-stage decisions

**P1 — Files and modules.**

| Where | Contents |
|---|---|
| `src/gameplay_sdk/include/atlantis/gameplay/` | `world.h` (`gameplay::World`), `transaction.h`, `subscription.h`, `query_batch.h`, `error.h`, `binding.h` (handles, bindings, codec traits the generated header specializes) |
| `src/gameplay_sdk/include/atlantis/gameplay/generated/world.h` | **generated, committed** (P3) |
| `src/gameplay_sdk/src/*.cpp` | the reflective layer, the default batch, the compatibility check |
| `src/gameplay_sdk/CMakeLists.txt` | `atlantis_gameplay_sdk` (`Atlantis::GameplaySdk`): PUBLIC `Atlantis::Connection`, PRIVATE `atlantis_compiler_warnings`; nothing else |
| `src/tools/sdk_codegen/` | `atlantis_sdk_codegen_lib` (`generate_bindings.h/.cpp`: pure function, schema span → text) and `atlantis_sdk_codegen` (`main.cpp`: `--out <path>`); links `Atlantis::World` (for `worldSchema()`) and Core; host-only |
| `examples/gameplay_demo/` | `atlantis_gameplay_demo` (`main.cpp`, `remote_batch.h`): links `Atlantis::GameplaySdk`, `Atlantis::RemoteClient` |
| `tests/gameplay_sdk/` | SDK tests (no GPU), boundary scan, compile checks and probes, `generated/synthetic.h` (committed, P3), the two-process e2e test (WIN32) |
| `tests/tools/sdk_codegen/` | generator tests and both staleness tests |
| `tests/runtime/gameplay_sdk_gpu_tests.cpp` (new) | the in-process North star, Bistro, batch and read-isolation tests (fatal VVL), in `atlantis_runtime_gpu_tests` |
| `.gitattributes` | `eol=lf` for the two generated headers (J2) |

**Dependency-graph changes:**

- **New:** `atlantis_gameplay_sdk` (Connection only);
  `atlantis_sdk_codegen_lib` / `atlantis_sdk_codegen` (World + Core,
  host-only); `atlantis_gameplay_demo` (SDK + Remote client half).
- **Test targets:** `atlantis_runtime_gpu_tests` links
  `Atlantis::GameplaySdk`.
- **No other link changes.** Runtime, Remote, the CLI and the editor do not
  link the SDK (ADR-0110 D1).

**P2 — The reflective layer** (R2, R4; ADR-0110 D2).

- **`gameplay::World`** borrows a `RuntimeConnection&` and an optional
  `QueryBatch*` (a default one-at-a-time batch when null). Its calls:
  - **Queries:** `exists`, `entities`, `components`, and
    `entitiesWith(types)` — `listEntities()` plus one batched
    `listComponents`, filtered client-side.
  - **Properties:** `get` and `set` by path (`"Light.intensity"`,
    `"Camera.fog.density"`) through `text::parsePath`; also by
    `(TypeId, FieldId)`.
  - **Components:** `readComponent(entity, type)` returns a
    `DynamicComponent` — the leaves in `leavesOf` order with values, read
    with one batched `getProperties`.
  - **Submission:** `submit(const Transaction&)` returns a
    `Result<TransactionTicket, Error>`; `submit(Command)` returns a
    `Result<CommandTicket, Error>`.
  - **Events:** `subscribe(EventFilter)` returns a `Subscription`.
  - **Failures:** `drainFailures()`, plus `FailureLog`, which keeps
    drained failures and answers `refusal(TransactionTicket|CommandTicket)`.
- **`gameplay::Transaction`** is a connection-independent builder that
  records operations in call order:
  - `create`, `destroy`;
  - `add` (bare by type, or with values);
  - `remove`, `set` (by path, or typed).

  Names and bindings are resolved only at `World::submit`. Any
  resolution error or incompatible binding refuses the whole transaction:
  nothing is submitted and no ticket is taken (R5; J6).
- **`gameplay::Subscription`** is move-only. It unsubscribes in its
  destructor and `drain()`s by value.
- **`gameplay::Error`:** a kind plus the underlying `AccessError` /
  `ConnectionError` / `text::TextError` when there is one, and the name
  that failed. Kinds:
  - `UnknownType`, `UnknownField`, `NotALeaf`;
  - `SchemaMismatch`;
  - `Refused` (a World `AccessError` from a query);
  - `Connection`.
- **No pre-checks** of finiteness, limits, editability or enum range
  (R4). Those come back as the boundary's refusals.

**P3 — The generator and the generated header** (R3, R6, R7; ADR-0111).

- **`generateBindings(schema, options)`** returns
  `Result<std::string, CodegenError>`. It is pure and deterministic: LF
  only, no timestamp, types in table order, fields in descriptor order.
  - `options` carry the module prefix (`world::`), the target namespace
    (`atlantis::gameplay::world`), the banner's regeneration command and
    the include guard style (`#pragma once`).
  - **Refusals** (`CodegenError`): a name that is not an identifier or is a
    C++ keyword (a fixed keyword list), and a table that is not
    `isWellFormed`.
- **Layout** of the generated header (normative shape; spellings are the
  implementation's):

  ```cpp
  // GENERATED by atlantis_sdk_codegen from world::worldSchema() -- do not edit.
  // Regenerate: <command>
  #pragma once
  #include <atlantis/gameplay/binding.h>
  namespace atlantis::gameplay::world {
  enum class LightKind : std::int64_t { Directional = 0, Point = 1 };   // exact values
  struct CameraFog { std::array<float, 3> color{}; float density{}; /* … */ };
  struct Light { LightKind kind{}; std::array<float, 3> color{}; float intensity{}; float range{}; };
  namespace fields {
  struct LightFieldSet { Field<world::Light, world::LightKind> kind; /* … */ };
  inline constexpr LightFieldSet Light{ /* {TypeId, FieldId, "Light.kind"}, … */ };
  }  // namespace fields
  }  // namespace atlantis::gameplay::world
  // Bindings and codecs: specializations of gameplay::BindingOf<T> / Codec<T>
  // (ids, kinds, flags, referenced TypeIds, constants, schemaVersion,
  //  zeroIsDeclared; per-leaf read/write of the value struct).
  // static_asserts: every id == schema::typeId/fieldId(name).
  ```

  - **Handles:** a non-`Editable` leaf gets `ReadOnlyField<C, T>`.
  - **`add` eligibility:** `Codec<C>::allEditable` is `false` when any leaf
    is read-only; typed `add(entity, C)` requires it (R10).
  - **Enums:** each enum binding carries `zeroIsDeclared`.
- **Committed outputs:**
  - `src/gameplay_sdk/include/atlantis/gameplay/generated/world.h`, from
    `worldSchema()`;
  - `tests/gameplay_sdk/generated/synthetic.h`, from a synthetic table in
    `tests/tools/sdk_codegen/synthetic_schema.h`. It covers every
    `PrimitiveKind`, nesting, `Optional`, a read-only leaf, a long name,
    and enums with non-contiguous values, a negative value, a nonzero
    first constant and no 0 constant.
- **Staleness** (R7; J4): the tests regenerate in memory and compare bytes
  with the committed files. On a mismatch they write the expected text to
  `<build>/sdk_codegen/<name>.expected` and fail with the command to
  regenerate (`atlantis_sdk_codegen --out …` for `world.h`; a copy from the
  `.expected` file for the synthetic header). Nothing rewrites a committed
  file automatically.

**P4 — The typed layer and compatibility** (R3, R5, R10; ADR-0111 D3, D4).

- **Calls:**
  - **Get and set:** `get(entity, handle)` returns `Result<T, Error>`.
    `set(entity, handle, T)` returns `Result<CommandTicket, Error>` and is
    constrained to `Field` (not `ReadOnlyField`) with exactly `T`.
  - **Components:** `read<C>(entity)` returns `Result<C, Error>`. It is one
    batched `getProperties`, decoded through `Codec<C>`, and not a snapshot
    (R10).
  - **Queries:** `entitiesWith<C...>()`.
  - **Transactions:** `Transaction::add(entity, const C&)` requires
    `Codec<C>::allEditable`. `Transaction::add<C>(entity)` is bare (World's
    defaults); `remove<C>`; typed `set`.
  - **Events:** `decode(event, handle)` returns
    `Result<std::optional<T>, Error>`: empty when the event is not that
    property, and `SchemaMismatch` when the type is incompatible.
- **Value mapping:** each leaf's C++ type is its `PropertyValue`
  alternative. An enum maps through `EnumValue{int64}`. `std::optional<T>`
  maps to `T` or `Absent`. A returned alternative that does not match the
  binding is `SchemaMismatch`.
- **Compatibility:**
  - `World` keeps a per-`TypeId` cache of `Compatible` or `Mismatch`,
    filled on the first typed use by comparing the binding with
    `connection.schema()`;
  - the comparison covers ids, kind, version, the exact field set
    (name, id, kind, primitive, referenced `TypeId`, `Optional`,
    `Editable`) and constants by name and value;
  - it recurses into every referenced type, with the cache stopping
    repeats.

**P5 — The query batch** (R2; ADR-0110 D2; ruling Q9).

- **`gameplay::QueryBatch`:** an abstract class with `listComponents(span)`
  and `getProperties(span)`, the same shape as `cli::QueryBatch` but owned
  by the SDK.
- **`SequentialQueryBatch`:** the default, one query at a time over the
  connection.
- **Uses:** the SDK calls the batch for component reads (one
  `getProperties` per read) and for `entitiesWith` (one `listComponents`
  for all listed entities). No other call goes through it.
- **Remote:** the example's `RemoteBatch` (`examples/gameplay_demo/remote_batch.h`)
  forwards to `RemoteSession`, as `atlantis` does.

**P6 — The example, `atlantis_gameplay_demo`** (R8; ruling Q8; ADR-0110 D6).

- **Usage:** `atlantis_gameplay_demo [--session <path>] [--steps <K>]
  [--capture-dir <dir>]`. The session is resolved like `atlantis`'s
  (`--session`, `ATLANTIS_SESSION`, the default path). Defaults: `K = 8`,
  and the capture directory is the system temp directory.
- **The loop**, one line printed per step, `ok` or the failed check:
  1. **Connect** and check the bindings used (`Light`, `LightKind`,
     `WorldMatrix`).
  2. **Find:** `entitiesWith<w::Light>()` and `read<w::Light>`. Exactly
     one entity has `kind == Directional`.
  3. **Pause; capture** a baseline image (`step(1)` with an image path).
  4. **Spawn:** one transaction —
     - `create` the beacon `57005700-0000-4000-8000-0000000000b1`;
     - `add(beacon, w::Light{Point, {1, 0.6, 0.2}, 4, 4})`;
     - `add(beacon, w::WorldMatrix{…, column3 = positionAt(0)})`, the
       Light before the WorldMatrix (Plan 0052 J1).

     Then `step(1)`. The report has one point light, exactly as written,
     and a subscription for the beacon has seen the creation events.
  5. **For `k = 1…K`:**
     - one transaction sets `WorldMatrix.column3 = positionAt(k)` and
       `Light.intensity = intensityAt(k)`;
     - `step(1)`;
     - the report's point light equals them exactly;
     - `positionAt(k)` is a circle of radius 1 at height 2 around the
       origin; `intensityAt(k)` lies in `[2, 6]`.
  6. **Refusal:** one transaction sets `Light.intensity = 9` and
     `Light.color = {NaN, 0, 0}`, then `step(1)`.
     - `FailureLog` shows one failure inside its ticket range
       (`NonFiniteValue`);
     - the report still shows `intensityAt(K)`, so the valid half did not
       apply either.
  7. **Capture:** `step(1)` with an image. Its PNG bytes differ from the
     baseline's (J7).
  8. **Destroy** the beacon and `step(1)`. There are zero point lights,
     the Directional light is unchanged, and the subscription has seen
     `EntityDestroyed`.
  9. **Resume**, and exit.
- **Exit codes:** 0 all checks passed; 1 a check failed; 2 usage;
  3 session, connection or `SchemaMismatch`.
- **Precondition:** an existing beacon GUID (left by an aborted run) is
  reported and exits 1. The run assumes no other client resumes or steps
  the Runtime meanwhile (R10; ruling Q7); it says so in its usage text.

**P7 — Tests that need the Runtime** (`tests/runtime/gameplay_sdk_gpu_tests.cpp`,
fatal VVL; the `Attached` pattern of pre-drafting item 5, with the SDK's `World` over
the session's connection and a `RemoteBatch`).

- **North star in process:** P6's loop as a test.
  - **Exact checks:** every step's frame data; the image change, by
    decoded pixels (`Atlantis::ImageRegressionSupport`); the original
    light set after the destroy.
  - **Reproducibility:** a second run on a fresh application gives the
    same per-`k` values, while the frame numbers differ.
- **Bistro, content-gated** (`SKIP` without content):
  - `entitiesWith<w::Light>()` equals the reflective result (60
    entities);
  - `6b63b12c-…` goes 12 → 24 through a typed `set`, exactly in frame
    data.
- **Batch equivalence:** `RemoteBatch` and `SequentialQueryBatch` give
  equal component reads and filters.
- **Read isolation** (R10; J1). Client A is the SDK over the loopback
  session, reading `read<w::Light>` one leaf at a time
  (`SequentialQueryBatch`). Client B is a second client of the same
  Runtime: its own `app.openConnection()` and the shared
  `RuntimeControlHost`, as `--listen` serves each remote client.
  - **Step mid-read:** with the Runtime paused, B submits a `Light` edit
    and calls `step(1)` after A's first leaf is answered. A's result holds
    the old first leaf and the new later leaves.
  - **Resume mid-read:** the same, with B calling `resume()`.
  - **Pending step:** B requests `step(1)` just before A's read starts.
  - **Isolated:** paused, with no `step` or `resume` from any client and no
    pending step, A's read equals a fresh read and the World's values.

## Milestones / Task Breakdown

Six milestones. Commit prefixes `feat:` / `test:` / `docs:` / `chore:`.

**Every gate:**

- Debug and Release builds, and full `ctest` with every golden compared
  exactly. A moved golden stops the work and is reported, never
  re-captured.
- The golden-directory guard.
- The path guard: the P1 list, with zero changes under `src/runtime/`,
  `src/world/`, `src/connection/`, `src/remote/`, `src/cli/`,
  `src/editor/`, `shaders/` and `assets/`.
- `runFrame()` byte-identical (implied by the path guard, checked anyway).
- Android `assembleDebug`, whose `targets` gain `atlantis_gameplay_sdk`
  from M1.
- GPU suites under fatal VVL.

### M1 — Generator, generated header, SDK skeleton

1. `src/tools/sdk_codegen/` (P3), host-only.
2. `.gitattributes` (J2). Commit the generated `world.h` and
   `synthetic.h`.
3. `src/gameplay_sdk/` skeleton: `binding.h`, `error.h`, the CMake target.
   The generated header compiles, with its `static_assert`s.
4. Tests:
   - `tests/tools/sdk_codegen/`:
     - determinism (two runs equal);
     - both staleness checks;
     - refusals (a keyword, a non-identifier, an ill-formed table);
     - enum values exact, never ordinals;
     - `zeroIsDeclared` against the descriptors;
     - every `worldSchema()` leaf has exactly one handle.
   - `tests/gameplay_sdk/`:
     - the boundary scan: includes limited to `atlantis/connection/`,
       `atlantis/world/access/`, `atlantis/schema.h`, `atlantis/result.h`,
       `atlantis/asset_system/asset_guid.h`, `atlantis/gameplay/` and the
       standard library; no `world_schema.h`, component, ECS, Runtime,
       Platform, RHI, Renderer, Vulkan, Remote or OS header;
     - the link block exactly `Atlantis::Connection` +
       `atlantis_compiler_warnings`.
5. Gradle `targets` gain `atlantis_gameplay_sdk`.

### M2 — The reflective layer and the batch (R2, R4)

1. P2 and P5.
2. Tests (no GPU, InProcess connection on the Spec 0054 fixture scene):
   - get/set by path and by id;
   - `readComponent` and `entitiesWith`;
   - create/destroy and add/remove;
   - transactions are all-or-nothing, with `FailureLog` by ticket;
   - `Subscription` drain and RAII unsubscribe;
   - name errors are client-side and submit nothing;
   - boundary refusals pass through unchanged;
   - a recording batch sees one call per component read and per filter;
   - `SequentialQueryBatch` equals one-at-a-time calls;
   - parity: each operation's `Command` sequence equals a hand-written
     one, through a recording `RuntimeConnection` wrapper.

### M3 — The typed layer and compatibility (R3, R5, R10)

1. P4.
2. Tests:
   - **Typed operations:** get/set, `read<C>` and `add`, typed transactions
     and `decode` over the fixture scene.
   - **Parity:** typed and reflective submit equal `Command` sequences.
   - **Defaults:** `add(e, w::Light{})` writes zeros and
     `kind == Directional` (since `Directional` is 0), while the bare add
     gives World's defaults (colour 1, 1, 1; intensity 1).
   - **Compatibility** (a wrapper connection serving a modified schema
     copy):
     - a changed version, kind, referenced `TypeId`, `Editable`, a
       missing or extra field, or a renamed constant each give
       `SchemaMismatch`;
     - a change only to `CameraFog` or `LightKind` makes `Camera` or
       `Light` incompatible;
     - unrelated types still work;
     - a mixed transaction submits nothing and takes no ticket.
   - **Compile checks** (J3):
     - `requires`-expression `static_assert`s: no `set` with the wrong
       `T`; no `set` on a `ReadOnlyField`; no `add(e, C)` for the
       synthetic type with a read-only leaf;
     - value-initialized synthetic enums have underlying 0, and the
       no-zero enum's 0 is not a declared constant;
     - three build probes, each with a positive twin, expected to fail
       compilation.

### M4 — The example and the two-process test (R8)

1. P6: `examples/gameplay_demo/`.
2. `tests/gameplay_sdk/gameplay_e2e_tests.cpp` (`WIN32 AND
   ATLANTIS_BUILD_EXAMPLES`; the harness of pre-drafting item 7, copied per J8):
   - `atlantis_runtime --listen 0 --session-file <tmp>`, then
     `atlantis_gameplay_demo --session <tmp>`;
   - exit 0, and the expected step lines in order;
   - the Runtime closed gracefully with exit 0;
   - a second run against a fresh Runtime prints identical lines.

### M5 — In-process North star, Bistro, batch and read isolation (R4, R8, R10)

1. P7 in `tests/runtime/gameplay_sdk_gpu_tests.cpp`. `tests/runtime/CMakeLists.txt`
   adds the file and links `Atlantis::GameplaySdk`.

### M6 — Normative docs and acceptance

1. **Docs (normative, J10):**
   - AGENTS.md: the module list gains Atlantis Gameplay SDK, with its rule
     (Connection only; generated bindings committed; no Runtime-hosted
     code); Atlantis Remote's client-half sentence names
     `atlantis_gameplay_demo`.
   - `module_boundaries.md`: a Gameplay SDK section, and the Tools
     section's `atlantis_sdk_codegen`.
2. **Final guards** and a review of the whole diff.
3. **Human run** (J9): `atlantis_runtime --listen` on the default scene,
   Debug and Release, with `atlantis_gameplay_demo` run against it. The
   beacon is seen orbiting in the window; screenshots before, during and
   after; the output lines are recorded in the PR.

## Files / Modules Touched (expected)

**New:**

- `src/gameplay_sdk/**` (including `include/atlantis/gameplay/generated/world.h`);
- `src/tools/sdk_codegen/**`;
- `examples/gameplay_demo/**`;
- `tests/gameplay_sdk/**` (including `generated/synthetic.h`);
- `tests/tools/sdk_codegen/**`;
- `tests/runtime/gameplay_sdk_gpu_tests.cpp`.

**Changed (planned):**

- `CMakeLists.txt`: `src/gameplay_sdk`; `src/tools/sdk_codegen` inside
  `if(NOT ANDROID)`; `examples/gameplay_demo`; `tests/gameplay_sdk`,
  `tests/tools/sdk_codegen`;
- `.gitattributes`;
- `tests/runtime/CMakeLists.txt` (the new file, `Atlantis::GameplaySdk`);
- `android/app/build.gradle` (`atlantis_gameplay_sdk`);
- `AGENTS.md`, `docs/architecture/module_boundaries.md` (normative only).

**Not touched:**

- Runtime (`src/runtime/**`), World (including `world_schema.cpp`),
  Connection, Remote, the CLI, the editor;
- shaders, assets, goldens, existing tests' contents.

## Sequencing & Dependencies

- M1 first: the generated header is the typed layer's input.
- M2 needs M1's skeleton.
- M3 needs M2: the typed layer is built on the reflective layer.
- M4 needs M3.
- M5 needs M3; P6's loop is shared in shape, not code, with M4.
- M6 last.

## Verification Checklist

- [ ] **R1 (M1):** the boundary scan (includes and link block); the
  example's link list.
- [ ] **R2 (M2):** reflective operations; `entitiesWith` and component
  reads through one batch call each.
- [ ] **R3 (M1, M3):** a handle for every leaf; typed operations; compile
  checks and probes.
- [ ] **R4 (M2, M3):** parity with hand-written `Command` sequences; order
  preserved; boundary refusals passed through.
- [ ] **R5 (M3):** recursive compatibility; mixed transactions refused
  whole.
- [ ] **R6 (M1):** no `byteOffset` or World type name in generated code
  (scanned); values by copy.
- [ ] **R7 (M1):** determinism; both staleness tests; a host-only tool.
- [ ] **R8 (M4, M5):** the example's exit 0 and step lines in two
  processes; the in-process North star exact, with reproducible per-`k`
  values.
- [ ] **R9 (every gate):** full suites, goldens exact, path guard,
  `runFrame()` unchanged.
- [ ] **R10 (M3, M5):** zeros and enum value 0; `add` only for
  all-`Editable` types; read isolation (step, resume and pending step
  mid-read show mixed leaves; the isolated read is consistent).
- [ ] **Android:** `assembleDebug` compiles `atlantis_gameplay_sdk`.

## Risks

- **Line endings** (J2). Without the `.gitattributes` rule, autocrlf would
  fail the byte-exact staleness test on every Windows checkout.
- **Compile-failure probes are new** (J3). A probe can "fail" for an
  unrelated reason; each has a positive twin that must compile, so a
  failure is attributable.
- **Read-isolation timing** depends on the loopback harness's frame
  placement. The test drives B's calls from A's `whileWaiting` at a
  counted request, not by timing.
- **Beacon visibility.** If the circle at height 2 is not in the default
  camera's view in practice, M5's image check fails. The positions are
  then adjusted and the change disclosed in the PR (not a Spec change).
- **Remote latency.** About 33 ms per call: the example's run takes a few
  seconds in Release, more in Debug.

## Joint Review decisions — ruled (Joint Human Review, slmao, 2026-10-09, PR #235)

All twelve were ruled as recommended, each as its first option. Spec 0057
is not corrected (J1), and ADR-0110 and ADR-0111 are unchanged. The
rejected alternatives are kept as the record of what was weighed.

- **J1 — The read-isolation tests' second client.** **Ruled (2026-10-09):
  a second in-process client of the same Runtime; no Spec Correction.**
  - **Ruled:** client B has its own connection from `app.openConnection()`
    and the shared `RuntimeControlHost` (P7).
  - **Rationale:**
    - B's calls take the same boundary path a remote client's do, since
      `--listen` serves every remote client as exactly such an in-process
      connection on the shared control;
    - the loopback harness cannot nest blocking Remote calls inside client
      A's wait.
  - **Recorded interpretation:** this satisfies Spec 0057's "two clients
    over Remote loopback" test requirement as written. No Spec Correction
    is raised.
  - **Rejected:** a second `RemoteSession` for B (nested blocking calls
    the harness does not support).
- **J2 — `.gitattributes`.** **Ruled (2026-10-09): `text eol=lf` for the two
  generated headers** (`src/gameplay_sdk/include/atlantis/gameplay/generated/world.h`,
  `tests/gameplay_sdk/generated/synthetic.h`). The staleness tests compare
  bytes exactly on any checkout, including under `core.autocrlf=true`.
  **Rejected:** normalizing CRLF inside the test (no repository config
  change, but no longer byte-for-byte as the Spec says).
- **J3 — Compile checks.** **Ruled (2026-10-09): both forms.**
  - **Static assertions:** every negative case as a `requires`-expression
    `static_assert`.
  - **Build probes:** three `WILL_FAIL` probes — a wrong-kind `set`, a set
    on a `ReadOnlyField`, and `add` on the synthetic read-only type.
  - **Positive twins:** each probe has a twin that must compile, differing
    only in the offending line, so a failure is attributable.

  This negative-test pattern is new to the repository. **Rejected:** the
  `static_assert`s alone (a deviation from the Spec's "ctest cases expected
  to fail compilation").
- **J4 — Staleness output.** **Ruled (2026-10-09): as recommended.** A
  mismatch writes `<build>/sdk_codegen/<name>.expected` and fails with the
  regeneration command. No test or build step rewrites a committed file.
- **J5 — Names.** **Ruled (2026-10-09): through `connection::text`.**
  Short or qualified type names and full paths, one grammar shared by the
  CLI, the editor and the SDK.
- **J6 — Transaction resolution.** **Ruled (2026-10-09): at `submit`,
  whole.** `Transaction` is a connection-independent builder.
  `World::submit` resolves it entirely, and any name or compatibility error
  refuses all of it: nothing is submitted and no ticket is taken.
- **J7 — Image checks.** **Ruled (2026-10-09): as recommended.** The example
  compares PNG bytes (no decoder); the GPU test compares decoded pixels.
- **J8 — The two-process harness.** **Ruled (2026-10-09): copied, not
  extracted.**
  - **Ruled:** `tests/gameplay_sdk/gameplay_e2e_tests.cpp` carries its own
    copy of the `CreateProcessW` / pipe / `WM_CLOSE` helper from
    `tests/cli/cli_e2e_tests.cpp`.
  - **Rationale:** the 0055-verified file stays untouched. This is
    disciplined duplication: test-only Windows code, copied whole, its
    origin cited in a comment.
  - **Rejected:** extracting a shared test helper, which would change a
    verified test file.
- **J9 — Acceptance runs.** **Ruled (2026-10-09): the human example run
  only.**
  - **Ruled:** M6's human run of `atlantis_gameplay_demo` against
    `atlantis_runtime --listen` (default scene, Debug and Release). No
    whitelist-scene or Android-emulator re-run.
  - **Rationale:** the precedent's reason for repeating the Plan 0052 J9
    runs was that frame-path modules changed (0055, 0056). That reason does
    not hold here:
    - every gate already requires zero changes under `src/runtime/`,
      `src/world/`, `src/connection/`, `src/remote/`, `src/cli/` and
      `src/editor/`;
    - goldens and `runFrame()` stay byte-identical;
    - `assembleDebug` covers the library.
  - **Rejected:** repeating the Plan 0052 J9 runs.
- **J10 — Docs.** **Ruled (2026-10-09): the split as recommended.**
  - **With the implementation PR (normative):**
    - AGENTS.md: the module list, the SDK rule, and the Remote client-half
      sentence;
    - `module_boundaries.md`: the Gameplay SDK section and the Tools line.
  - **After merge (narratives):** the registry, the Spec's Related Plan
    and the blueprint.
- **J11 — Names.** **Ruled (2026-10-09): class `gameplay::World` beside the
  generated namespace `gameplay::world`**, as the Spec's sample shows.
  **Rejected:** `gameplay::Client`.
- **J12 — The beacon GUID.** **Ruled (2026-10-09): fixed,
  `57005700-0000-4000-8000-0000000000b1`.** The example's output lines are
  identical run to run. A leftover beacon from an aborted run is reported
  (exit 1), not overwritten.

## Rollback Plan

- **After merge:** revert the implementation PR as a whole. Everything is
  additive (a new module, a tool, an example, tests, one `.gitattributes`
  line, a Gradle target, doc sentences). No format, asset, golden or
  Runtime change.
- **Before merge:** revert milestone by milestone, in reverse order.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:

- [ ] Every golden exact in Debug and Release at the final gate; none
      re-captured.
- [ ] Zero changes under `src/runtime/`, `src/world/`, `src/connection/`,
      `src/remote/`, `src/cli/`, `src/editor/`; `runFrame()` byte-identical.
- [ ] Boundary scans green: Gameplay SDK (new), CLI, Remote, Editor.
- [ ] Both staleness tests green on a fresh checkout.
- [ ] The post-merge docs items (J10) are queued.
