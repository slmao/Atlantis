# Plan: RuntimeConnection and a Minimal CLI Client

- **Spec:** [Spec 0054: RuntimeConnection and a Minimal CLI Client](../specs/0054-runtime-connection-minimal-cli.md)
  (`Approved`, 2026-10-07, [PR #222](https://github.com/slmao/Atlantis/pull/222);
  rulings Q1–Q8 binding) —
  [ADR-0105](../adr/0105-runtime-connection-and-cli-client.md) (`Accepted`).
  - It applies [ADR-0033](../adr/0033-runtime-authority-and-client-boundary.md).
  - It extends [ADR-0103](../adr/0103-runtime-world-operation-boundary.md)
    D1 by `listEntities()` ("Additions are later Specs").
  - It keeps [ADR-0104](../adr/0104-runtime-world-transaction-atomicity.md)
    and [ADR-0004](../adr/0004-phase1-threading-baseline.md) unchanged.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-07 — reviewed this Plan and
  [Spec 0054](../specs/0054-runtime-connection-minimal-cli.md) together in
  [PR #223](https://github.com/slmao/Atlantis/pull/223) and explicitly authorized Implementation from Milestone 1.
  J1–J9 were ruled as recommended. J1's Spec Correction is recorded in
  Spec 0054's header (Correction 2026-10-07). See Joint Review decisions
  below.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0054 R1–R11 as ruled:

- `RuntimeWorldAccess::listEntities()` (ruling Q1);
- a new **Atlantis Connection** module: the `RuntimeConnection` interface,
  the `InProcessEndpoint` with per-connection pull subscriptions and
  ticket-routed failures, the schema Query, and the text forms (rulings
  Q2, Q4, Q5, Q8);
- a new **Atlantis CLI** module with the six commands and a script runner
  (ruling Q6);
- `RuntimeApplication::openConnection()` (ruling Q7);
- `atlantis_runtime --exec <file|->` (ruling Q3);
- the north star: `entity set` on the default scene's light, seen in the
  next frame's `FrameLightingData` under fatal VVL.

`runFrame()` is unchanged. Every golden stays byte-identical; nothing is
re-captured.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `5ebca44` (PR #222 merged).

1. **The boundary's queues.** `RuntimeWorldAccess` (merged in #216/#220):
   - one `events` vector and one `failures` vector, each emptied whole by
     `drainEvents()` / `drainFailures()`;
   - tickets are one global increasing counter, consumed by
     `submit()` / `submitTransaction()` (`runtime_world_access.cpp`).

   The endpoint's fan-out sits on exactly this.
2. **The GUID index** is a `std::map<EntityGuid, ecs::EntityId>`, already in
   GUID order (`runtime_world_access.cpp`, `Impl::index`). Liveness is
   `lookup()` (index plus `isValid`).
3. **Runtime's object model:**
   - `RuntimeApplication(RuntimeApplication&&) noexcept = default`
     (`runtime_application.h:60`);
   - `createRuntimeApplication()` builds the object, runs `initializeSteps()`
     (which publishes `scene_`, then `worldAccess_.emplace(...)`,
     `runtime_application.cpp:1101-1102`), and **returns it by value**;
   - `main.cpp` then moves it again (`RuntimeApplication app =
     std::move(appResult.value())`).

   `worldAccess_` is an inline `std::optional`, so it changes address on
   every move. Plan 0052 deviation 3 is this hazard's precedent: a borrowed
   pointer into a moved `RuntimeApplication` dangled. This drives J1.
4. **The host evidence** (Spec 0054 ruling Q3):
   - the main loop, `main.cpp:287-289`;
   - the only message pump, `platform::processEvents()` inside `runFrame()`
     (`runtime_application.cpp:1170`), draining with a non-blocking
     `PeekMessageW` (`windows_platform.cpp:229`);
   - FIFO present (`vulkan_presentation.cpp:429`);
   - commands applied first in `runFrame()` (`runtime_application.cpp:1168`).
5. **Startup flags.** `atlantis::runtime::cli::parseCommandLine()` is a pure
   function in `src/runtime/cli.h/.cpp`, compiled into both
   `atlantis_runtime` and `atlantis_runtime_tests` (`tests/runtime/cli_tests.cpp`).
6. **Builds.** Top-level `CMakeLists.txt`:
   - modules are `add_subdirectory(src/…)` in dependency order;
   - tests are under `ATLANTIS_BUILD_TESTS`;
   - Android's Gradle build (`android/app/build.gradle`) builds `targets
     "atlantis_runtime_android"` only, so a library that target does not
     link is not compiled by `assembleDebug`.
7. **North-star data:**
   - the default scene `integrated_showcase_demo` has one Directional light,
     GUID `0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea`, `intensity=3.0`
     (`assets/scenes/integrated_showcase_demo.scene.txt:10`);
   - extraction copies `Light::intensity` verbatim into
     `FrameLightingData::directionalLights[0].intensity`
     (`scene_extraction.cpp:312`);
   - the smoke test already runs this scene and reads the frame's lighting
     bytes (`RuntimeSmokeTestAccess::lightingPayloadBytes`,
     `runtime_smoke_gpu_tests.cpp`).
8. **Schema names.**
   - `worldSchema()` types: `world::Transform`, `world::Camera`,
     `world::CameraFog`, `world::CameraBloom`, `world::Light`,
     `world::LightKind`, `world::Renderable`, `world::WorldMatrix`;
   - enum constants `Directional`, `Point`;
   - `Camera.fog` is a Struct field (`world_schema.cpp:82-112`).
9. **Output streams.** Core's console log sink writes Info to stdout and
   Warn and above to stderr (`src/core/src/log.cpp:27`).

## Plan-stage decisions

These are details Spec 0054 leaves to the Plan. None changes a Spec
requirement or an ADR-0105 decision. J1 corrects only ruling Q7's creation
point (Spec 0054 Correction 2026-10-07).

**P1 — Files.**

| File | Contents |
|---|---|
| `src/world/include/.../access/runtime_world_access.h`, `src/world/src/access/runtime_world_access.cpp` | `listEntities()` (P2) |
| `src/connection/include/atlantis/connection/runtime_connection.h` | `RuntimeConnection`, `EventKind`, `EventKindSet`, `EventFilter`, `SubscriptionId`, `ConnectionError` (P3) |
| `src/connection/include/atlantis/connection/in_process_endpoint.h`, `src/connection/src/in_process_endpoint.cpp` | the endpoint and its connections (P4) |
| `src/connection/include/atlantis/connection/text.h`, `src/connection/src/text.cpp` | the text forms (P5) |
| `src/connection/CMakeLists.txt` | `atlantis_connection` STATIC, alias `Atlantis::Connection`; PUBLIC `Atlantis::World` (access value types, `world_schema.h`) |
| `src/cli/include/atlantis/cli/command.h`, `src/cli/src/command.cpp` | the six commands over a `RuntimeConnection&` (P6) |
| `src/cli/include/atlantis/cli/script_runner.h`, `src/cli/src/script_runner.cpp` | the per-frame script runner (P7) |
| `src/cli/CMakeLists.txt` | `atlantis_cli` STATIC, alias `Atlantis::Cli`; links **only** `Atlantis::Connection` (and the warnings target) |

Namespaces are `atlantis::connection` and `atlantis::cli`. The latter is
distinct from Runtime's startup-flag namespace `atlantis::runtime::cli`
(ruling Q6).

**P2 — `listEntities()`** (R5; ruling Q1).

```cpp
// Every live entity this boundary addresses, by GUID value. O(n).
[[nodiscard]] std::vector<atlantis::asset_system::EntityGuid> listEntities() const;
```

- **How.** It walks the index, already in GUID order, keeping entries that
  pass `lookup()`. So exactly what `findEntity()` accepts is listed.
- **Not listed:** an ECS entity without a GUID.
- **Headers.** `runtime_world_access.h` is already in the EntityGuid
  allowlist, so the allowlist and AGENTS.md are untouched.

**P3 — The interface** (R1, R2, R4; rulings Q4, Q5, Q8; ADR-0105 D1, D6,
D8).

```cpp
namespace atlantis::connection {
enum class EventKind : std::uint8_t { EntityCreated, EntityDestroyed, ComponentAdded, ComponentRemoved, PropertyChanged };
class EventKindSet { /* bitmask; all() default; with(kind); contains(kind) */ };
struct EventFilter {
  EventKindSet kinds = EventKindSet::all();
  std::optional<asset_system::EntityGuid> entity;  // the event's entity (PropertyChanged: address.entity)
  std::optional<schema::TypeId> component;         // Component*/PropertyChanged only; never matches Entity* events
};
struct SubscriptionId { std::uint64_t value = 0; /* <=> */ };  // endpoint-unique, from 1
enum class ConnectionError { UnknownSubscription };

class RuntimeConnection {  // abstract: a transport (0055) implements it; it does not shape it
 public:
  virtual ~RuntimeConnection() = default;
  // Query (values; Spec 0052 types)
  [[nodiscard]] virtual bool findEntity(const asset_system::EntityGuid&) const = 0;
  [[nodiscard]] virtual std::vector<asset_system::EntityGuid> listEntities() const = 0;
  [[nodiscard]] virtual Result<std::vector<schema::TypeId>, world::access::AccessError> listComponents(const asset_system::EntityGuid&) const = 0;
  [[nodiscard]] virtual Result<world::access::PropertyValue, world::access::AccessError> getProperty(const world::access::PropertyAddress&) const = 0;
  [[nodiscard]] virtual std::span<const schema::TypeDescriptor> schema() const = 0;  // valid while the connection lives (K1)
  // Command / Transaction
  virtual world::access::CommandTicket submit(world::access::Command) = 0;
  virtual world::access::TransactionTicket submitTransaction(std::vector<world::access::Command>) = 0;
  // Event (pull-only)
  [[nodiscard]] virtual SubscriptionId subscribe(EventFilter) = 0;
  virtual Result<std::monostate, ConnectionError> unsubscribe(SubscriptionId) = 0;
  [[nodiscard]] virtual Result<std::vector<world::access::Event>, ConnectionError> drainEvents(SubscriptionId) = 0;
  [[nodiscard]] virtual std::vector<world::access::CommandFailure> drainFailures() = 0;  // this connection's own
};
}
```

- **Values only.** No method returns a pointer or reference into
  Runtime-owned state. `schema()` views static data (ADR-0033).
- **Client misuse is an error value, not an abort.** A subscription id this
  connection does not own is `UnknownSubscription`. This is the one new
  error kind; it is the connection's, not the World's (J5).

**P4 — The InProcess endpoint** (R1–R4; rulings Q5, Q7; ADR-0105 D2, D6).

```cpp
class InProcessEndpoint {                       // non-copyable, non-movable
 public:
  explicit InProcessEndpoint(world::access::RuntimeWorldAccess& boundary);
  ~InProcessEndpoint();                         // ATLANTIS_CHECK: no connection is still open
  [[nodiscard]] std::unique_ptr<RuntimeConnection> open();
};
```

- **The only drainer.** `pump()` moves the boundary's events and failures
  out, then:
  - **Events:** each event is appended to every open subscription, across
    all connections, whose filter matches.
  - **Failures:** each failure goes to the connection whose recorded ticket
    range contains its ticket. A failure no connection owns, i.e. one
    submitted to the boundary directly by its owner, is dropped (J4).
- **When it pumps:** before every `drainEvents`, `drainFailures` and
  `subscribe` (so a new subscription sees only later events), and before
  `unsubscribe`.
- **Ranges.** `submit()` records a one-ticket range and
  `submitTransaction()` its `[first, first+count)`, in a `std::map` keyed by
  first ticket. An empty transaction records nothing.
- **Queries and commands** forward to the boundary unchanged. That makes the
  semantics, timing and validation Spec 0052/0053's (R2).
- **Lifetime.** Connections hold the endpoint by pointer, and the endpoint
  counts them. Its destructor CHECKs that the count is 0, so a connection
  never outlives it.

**P5 — Text forms** (R6; ruling Q2; ADR-0105 D5). These are pure functions
over a schema span.

| Item | Text | Parse rule |
|---|---|---|
| Entity | lowercase RFC 9562 8-4-4-4-12 | `parseEntityGuid` / `toString` (existing) |
| Type | short name (`Light`); qualified (`world::Light`) accepted | unqualified-name lookup over `worldSchema()` Struct types; a test pins uniqueness |
| Field | full path `Light.intensity`, `Camera.fog.density` | walks Struct fields by name to a leaf → `(component TypeId, leaf FieldId)` (Spec 0049 J8) |
| `Float32` | `printf("%.9g")` | `std::strtof` of the whole token (C locale; Atlantis never calls `setlocale`). `%.9g` round-trips a float |
| `Vec3Float32` / `Vec4Float32` | 3 / 4 `Float32` tokens | exactly 3 / 4 tokens |
| `UInt64` (incl. `AssetReference` ids) | decimal | `std::strtoull` of the whole token, no sign |
| Enum | constant name (`Point`) | exact, case-sensitive name from the descriptor |
| `AssetGuid` / `EntityGuid` | GUID text | existing parsers |
| `Optional`, absent | `none` | `none`; otherwise the inner kind's text |

- **Errors.** `TextError` covers malformed GUID, unknown type, unknown
  field, path not ending at a leaf, wrong token count, malformed number,
  and unknown enum constant. Each is reported before anything is
  submitted.
- **One rule source.** Finiteness, editability and component membership
  (`CameraFog.density` is not a component) stay the boundary's refusals.
  So `nan` parses and the boundary refuses it with `NonFiniteValue`.
- **Round-trip guaranteed.** Formatting then parsing any value of any leaf
  yields an equal `PropertyValue`.
- **Locale-independent float formatting.** `%.9g` / `strtof` are used rather
  than `std::to_chars` / `from_chars`, whose floating-point support the
  NDK's libc++ may lack; `assembleDebug` checks this.

**P6 — The six commands** (R7; ADR-0105 D3). `atlantis::cli` executes one
tokenized line against a `RuntimeConnection&`, writing to a `std::ostream&`.

| Command | Output (one item per line) |
|---|---|
| `schema list` | `<ShortName> <struct\|enum>`, in `schema()` order |
| `schema inspect <Type>` | `<ShortName> <kind> (<qualified name>) v<schemaVersion>`, then per field `  <name> <primitive kind\|struct T\|enum T> <flag,flag…>`, or per enum constant `  <Name> = <value>` |
| `world entities` | `<guid> <Comp>,<Comp>…` (components in `listComponents` order, short names) |
| `entity inspect <guid>` | `<guid>`, then per component per leaf `  <Comp>.<path> = <value text>` |
| `entity get <guid> <Type>.<field>` | `<value text>` |
| `entity set <guid> <Type>.<field> <value...>` | submits one `SetProperty`. Its outcome prints after the frame that applied it: `ok <guid> <Comp>.<path> = <value text>` (from the `PropertyChanged` event), or `refused <AccessError name>` |

- **Errors.** A query refusal prints `refused <AccessError name>`. A usage
  or text error prints `error: <message>`.
- **Output is deterministic:** schema order, GUID order and TypeId order
  only.

**P7 — The script runner and `--exec`** (R9; ruling Q3; ADR-0105 D7).

- **Syntax.**
  - One command per line, tokens separated by whitespace (no quoting: no
    value contains spaces).
  - Blank lines and lines whose first non-space character is `#` are
    skipped and consume no frame.
  - A line with an error prints `error:` and the script continues.
- **Pacing.** `ScriptRunner::step()` is called once after each
  `runFrame()`. It does two things, in order:
  1. it reports the outcome of the previous line's `set`, through a
     subscription `{PropertyChanged, entity, component}` opened at submit
     and closed after reporting, and the connection's failures;
  2. it echoes the next command as `> <line>` and runs it.

  After the last line it prints `# exec done: <n> commands, <e> errors,
  <r> refused` once. The Runtime keeps rendering until the window closes.
- **Flags.** `atlantis::runtime::cli::parseCommandLine()` gains
  `--exec <path|->`, returning the source. It stays pure.
- **Loading.** `main.cpp` reads the whole script at startup, before
  `createRuntimeApplication()`: the file, or stdin until EOF for `-`. A
  missing file is `PrintErrorAndExit`.
- **The loop.**

  ```
  while (app.shouldContinue()) { app.runFrame(); if (runner) runner->step(); }
  ```

  `runFrame()` itself is untouched. Without `--exec` there is no runner and
  no connection.
- **Output.** CLI output goes to stdout. Info logs share stdout, while
  warnings and errors go to stderr (reading item 9). CLI lines are
  distinguishable by `> ` / `ok ` / `refused ` / `error: ` / bare values.
- **Acceptance script.** `tests/cli/scripts/north_star.txt` is both the
  scripted acceptance case and the input of the human run:

  ```
  # Spec 0054 R11: the default scene's Directional light
  entity get 0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea Light.intensity
  entity set 0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea Light.intensity 6
  entity get 0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea Light.intensity
  ```

  Expected output: `3`, then `ok … Light.intensity = 6`, then `6`.

**P8 — Runtime ownership** (R3, R10; ruling Q7; ADR-0105 D2; J1).

- **The member.**
  - `RuntimeApplication` gains `std::unique_ptr<connection::InProcessEndpoint>
    endpoint_`, declared after `worldAccess_` so it is destroyed first.
  - It is held in a small private `EndpointSlot` whose move constructor
    `ATLANTIS_CHECK`s that it is empty, so `RuntimeApplication` stays
    `noexcept`-movable by default. A `static_assert` keeps that.
- **Creation (J1).** The endpoint is created at the **first
  `openConnection()`**, not in `initializeSteps()`:
  - the application is moved after `initializeSteps()` (reading item 3), so
    an endpoint made there would borrow a stale `worldAccess_`;
  - created lazily, it binds to the application in its final place;
  - the slot's CHECK makes any later move a loud programming error, never a
    dangling pointer.
- **The API.**

  ```cpp
  [[nodiscard]] std::unique_ptr<connection::RuntimeConnection> openConnection();  // frame thread
  ```

  It requires a loaded scene (CHECK).
- **Teardown.** `main.cpp` declares the runner and its connection after
  `app`, so they are destroyed before it.
- **Unaffected:**
  - **the smoke test** keeps its friend access to `worldAccess_`; it opens
    no connection;
  - **`android_main.cpp`** is untouched.

## Milestones / Task Breakdown

Six milestones. Commit prefixes: `feat:` for code, `test:` for test-only
steps, `docs:` for docs.

Every gate runs:

- a Debug + Release build;
- the full `ctest` in both configurations, with every golden compared
  exactly (they come from the headless fixture, which does not use
  `RuntimeApplication`);
- the golden-directory guard;
- the path guard (Verification), which also reports the files changed
  under `src/runtime/`;
- Android `assembleDebug`.

GPU suites run under fatal VVL.

**Stop rule:** a moved golden stops the work and is reported, never
re-captured.

### M1 — `listEntities()` (R5; ruling Q1)

1. P2 in `runtime_world_access.h/.cpp`.
2. `tests/world/world_access_listing_tests.cpp` (new):
   - the baked scene's GUIDs, in GUID order;
   - equal to `findEntity()` over the index;
   - follows `CreateEntity`/`DestroyEntity` (after `applyPending()`) and
     re-creation of a destroyed GUID;
   - an ECS entity created without the boundary is not listed.

   `tests/world/CMakeLists.txt`.

*Gate:* standard.

### M2 — Atlantis Connection: interface and InProcess endpoint (R1–R4; rulings Q4, Q5, Q8)

1. `src/connection/` (P1, P3, P4); top-level `add_subdirectory(src/connection)`
   after `src/world`; `tests/connection/` and its `add_subdirectory`.
2. `tests/connection/connection_tests.cpp` (new), over the World test
   fixture pattern (a cooked scene, baked):
   - **equivalence with the boundary:** every Query; `submit` and
     `submitTransaction` give the same events, failures and world as direct
     submission (R2);
   - **two connections:**
     - failures route to the submitter, including an aborted transaction's;
     - a subscription sees only events after it was made;
     - filters by kind, entity and component;
     - one connection's drain leaves the other's queue intact;
     - unsubscribe; an unknown or foreign id gives `UnknownSubscription`
       (R4);
   - `schema()` equals `worldSchema()`;
   - `listEntities()` through the connection.

*Gate:* standard.

### M3 — Text forms (R6; ruling Q2)

1. `text.h/.cpp` (P5).
2. `tests/connection/connection_text_tests.cpp` (new):
   - **short-name uniqueness** over `worldSchema()`;
   - short and qualified names resolve alike;
   - nested paths; a path to a Struct is an error;
   - per-kind parse/format;
   - **round-trip of every leaf of every World component** on a baked
     scene's entities: format, then parse, gives an equal value;
   - every `TextError`;
   - `nan` parses, and the boundary then refuses it.

*Gate:* standard.

### M4 — Atlantis CLI and its boundary (R7, R8; ruling Q6)

1. `src/cli/` (P1, P6, P7's `ScriptRunner`); top-level
   `add_subdirectory(src/cli)`; `tests/cli/` and its `add_subdirectory`.
2. `tests/cli/cli_command_tests.cpp` (new), over InProcess on a baked
   fixture scene. Expected output text for each of the six commands, plus:
   - `get` output fed back to `set` gives `ok` and an unchanged snapshot
     (the R6 round-trip, end to end);
   - a refused `set` prints `refused <AccessError>`;
   - usage and text errors.
3. `tests/cli/cli_script_tests.cpp` (new):
   - `ScriptRunner` driven by explicit `step()` / `applyPending()` pairs;
   - blank lines, comments and per-line errors;
   - the done line;
   - **`north_star.txt` over the default scene,** decoded from the build
     catalog and baked, with expected output `3` / `ok … = 6` / `6`.
4. `tests/cli/cli_boundary_tests.cpp` (new), R8:
   - `src/cli/**` includes only `atlantis/cli/`, `atlantis/connection/`,
     `atlantis/world/access/`, `atlantis/schema.h`, `atlantis/result.h`,
     `atlantis/assert.h`, `atlantis/asset_system/asset_guid.h` and standard
     headers;
   - it names no `atlantis/runtime/`, `atlantis/world/ecs/`, Platform, RHI,
     Renderer or Vulkan header;
   - `src/cli/CMakeLists.txt`'s `target_link_libraries` names only
     `Atlantis::Connection` (and the warnings target);
   - the same include rule applies to `src/connection/**`, plus
     `atlantis/world/world_schema.h`;
   - a "not vacuous" check.
5. `android/app/build.gradle`: `targets` gains `"atlantis_cli"`, so
   `assembleDebug` compiles the CLI library (J7).

*Gate:* standard.

### M5 — Runtime: the endpoint, `--exec`, the north star (R3, R9–R11; ruling Q7; J1)

1. P8 in `runtime_application.h/.cpp`. `atlantis_runtime_host` links
   `Atlantis::Connection` (PUBLIC: its header names the connection type).
2. P7's `--exec` in `src/runtime/cli.h/.cpp`; `main.cpp`'s loop;
   `atlantis_runtime` links `Atlantis::Cli`.
3. `tests/runtime/cli_tests.cpp` gains `--exec` parsing cases: path, `-`,
   missing argument, and combined with `--scene`.
4. `tests/runtime/runtime_smoke_gpu_tests.cpp` gains the **north-star
   TEST_CASE**:
   - a windowed `RuntimeApplication` on the default scene, under fatal VVL;
   - `openConnection()`, and the CLI line
     `entity set 0b2c1db2-43ab-4eb1-af89-1a9ae5ef89ea Light.intensity 6`;
   - before the edit, `FrameLightingData::directionalLights[0].intensity ==
     3.0f`; after the next `runFrame()`, `6.0f`;
   - the runner reports `ok …`.

   `atlantis_runtime_gpu_tests` links `Atlantis::Cli`. The existing
   TEST_CASEs are unchanged.

*Gate:* standard. GPU suites under fatal VVL; every golden exact.

### M6 — Acceptance

1. Final path guard and diff review.
2. **Human north-star run:** `atlantis_runtime --exec
   tests/cli/scripts/north_star.txt` on the default scene, in Debug and
   Release, VVL on. Before and after screenshots: the scene visibly
   brightens. The stdout transcript is recorded.
3. Runs per J8.

*Gate:* standard, plus the runs.

## Files / Modules Touched (expected)

**New:**

- `src/connection/**`: the P1 headers, sources and `CMakeLists.txt`;
- `src/cli/**`: the P1 headers, sources and `CMakeLists.txt`;
- `tests/connection/CMakeLists.txt`,
  `tests/connection/connection_tests.cpp`,
  `tests/connection/connection_text_tests.cpp`;
- `tests/cli/CMakeLists.txt`, `tests/cli/cli_command_tests.cpp`,
  `tests/cli/cli_script_tests.cpp`, `tests/cli/cli_boundary_tests.cpp`,
  `tests/cli/scripts/north_star.txt`;
- `tests/world/world_access_listing_tests.cpp`.

**Changed (planned, not deviations):**

- `src/world/include/atlantis/world/access/runtime_world_access.h`,
  `src/world/src/access/runtime_world_access.cpp`: `listEntities()`;
- `tests/world/CMakeLists.txt`;
- `CMakeLists.txt` (top level): the four `add_subdirectory` lines;
- `src/runtime/include/atlantis/runtime/runtime_application.h`,
  `src/runtime/src/runtime_application.cpp`: P8 (outside `runFrame()`);
- `src/runtime/cli.h`, `src/runtime/cli.cpp`, `src/runtime/main.cpp`: P7;
- `src/runtime/CMakeLists.txt`: the two links;
- `tests/runtime/cli_tests.cpp`, `tests/runtime/runtime_smoke_gpu_tests.cpp`,
  `tests/runtime/CMakeLists.txt`: M5;
- `android/app/build.gradle`: one target (J7);
- docs per J6.

**Not touched:**

- `runFrame()`'s body;
- the ECS core, `access_error.h`, `property_access.*`;
- Spec 0052/0053's existing tests;
- `module_boundary_tests.cpp` and the EntityGuid allowlist;
- `android_main.cpp`;
- render extraction, Renderer, RHI, Vulkan backend, Platform;
- assets, shaders, goldens.

No external dependency is added.

## Sequencing & Dependencies

- **M1 → M2.** The connection forwards `listEntities()`.
- **M2 → M3.** The text forms use the connection's schema span.
- **M3 → M4.** The CLI uses the text forms.
- **M4 → M5.** Runtime hosts the CLI.
- **M6** comes last.

## Verification Checklist

This maps to Spec 0054's Testing & Verification Plan.

- [ ] **R1/R2 (M2):** connection ≡ boundary for every Query, command and
  transaction; `schema()`.
- [ ] **R3 (M2, M5):** InProcess only; owned by `RuntimeApplication`; no
  thread; the open-connection count is CHECKed at endpoint destruction.
- [ ] **R4 (M2):** per-connection subscriptions and filters; failures
  routed by ticket; no stealing.
- [ ] **R5 (M1, M2):** listing complete, GUID-ordered, follows
  create/destroy, addressable entities only.
- [ ] **R6 (M3, M4):** short-name uniqueness; per-kind text; every-leaf
  round-trip; `get` → `set` round-trip through the CLI.
- [ ] **R7 (M4):** each of the six commands against expected text.
- [ ] **R8 (M4):** the include and link scans of `src/cli` (and the include
  scan of `src/connection`).
- [ ] **R9 (M4, M5, M6):** `--exec` parsing; one line per frame; the script
  as acceptance case.
- [ ] **R10 (every gate):** `runFrame()` unchanged (diff); every existing
  test and every golden unchanged.
- [ ] **R11 (M5, M6):** the north-star GPU test under fatal VVL; the human
  run with screenshots.
- [ ] **Path guard, every gate:** only the files above; under `src/runtime/`
  only P7/P8's files.
- [ ] **Android:** `assembleDebug` at every gate compiles Connection (via
  `RuntimeHost`) and CLI (J7).

## Risks

- **The move hazard** (reading item 3). It is addressed by J1's lazy
  creation and the slot's CHECK. A future refactor that moves a live
  `RuntimeApplication` fails loudly.
- **Ticket ranges** are kept for the endpoint's lifetime: O(submissions)
  memory, small for v1's scripted use. Pruning belongs with a long-lived
  client (0055/0056).
- **CLI and log output share stdout** for Info logs. This is accepted for
  v1, and the CLI's line forms are distinct.
- **NDK float text.** P5 avoids `to_chars` / `from_chars` for floats;
  `assembleDebug` checks the rest.

## Joint Review decisions — ruled (Joint Human Review, slmao, 2026-10-07, PR #223)

All nine were ruled as recommended. J1 corrects Spec 0054, recorded as a
dated Correction in its header with in-place markers on ruling Q7 (R-a).
ADR-0105 is unchanged: D2 states ownership, declaration order, no thread
and an unchanged `runFrame()`, which all hold under lazy creation.

- **J1 — When the endpoint is created.** **Ruled (2026-10-07): at the first
  `openConnection()`, behind a move guard** (P8; M5).
  - **The conflict.** Ruling Q7 said the endpoint is "emplaced with it in
    `initializeSteps()`".
  - **The evidence** (reading item 3):
    - `createRuntimeApplication()` returns the application by value after
      `initializeSteps()`, and `main.cpp` moves it again;
    - `worldAccess_` is an inline `std::optional` whose address changes
      with each move;
    - so that endpoint would borrow a moved-from object, Plan 0052
      deviation 3's failure.
  - **The ruling.**
    - The endpoint is created when the application is in its final place.
    - The `EndpointSlot` guard makes any later move an `ATLANTIS_CHECK`
      failure.
    - Ownership and declaration order stay as ruled in Q7, and `runFrame()`
      is unchanged line for line.
    - Spec 0054 Correction 2026-10-07 records it.
  - **Rejected:**
    - **(alternative A)** a heap-held `worldAccess_`. It changes
      `runFrame()`'s `worldAccess_.has_value()` line, against "`runFrame()`
      unchanged".
    - **(alternative B)** a hand-written `RuntimeApplication` move
      constructor that rebinds. It means listing every member by hand, and
      is fragile.
- **J2 — The interface shape.** **Ruled (2026-10-07): as P3.**
  - `RuntimeConnection` is an abstract class over Spec 0052/0053's types.
  - `EventFilter{kinds, entity?, component?}`.
  - `SubscriptionId` runs from 1 and is unique within the endpoint.
- **J3 — Fan-out timing.** **Ruled (2026-10-07): on demand** (P4). The
  endpoint drains the boundary once, and distributes, only on
  `drainEvents`, `drainFailures`, `subscribe` and `unsubscribe`. There is
  no per-frame logic.
- **J4 — Failures nobody owns.** **Ruled (2026-10-07): dropped** (P4). A
  failure whose ticket no connection submitted is discarded by the
  endpoint.
- **J5 — `ConnectionError::UnknownSubscription`.** **Ruled (2026-10-07):**
  the one connection-level error, returned for an unknown or foreign
  subscription id (P3). It is not a World error.
- **J6 — Docs.** **Ruled (2026-10-07): split as Plan 0052 J8, applied to new
  modules.**
  - **Normative rule sentences ride with the implementation PR:**
    - AGENTS.md's top-level module list gains Atlantis Connection and
      Atlantis CLI, with their dependency rules (M4);
    - `module_boundaries.md` gains the two modules' sections (their
      dependency lines) and Runtime's "Depends on" line (M4/M5).
  - **Descriptive narratives go to the post-merge docs PR:**
    - the blueprint;
    - the registry's Implementation column;
    - status narratives;
    - Spec 0054's Related Plan.
- **J7 — Android compiles the CLI library.** **Ruled (2026-10-07):** Gradle
  `targets` gains `"atlantis_cli"` (M4). Both libraries are then compiled
  by `assembleDebug`; Connection already is, through `RuntimeHost`.
- **J8 — Acceptance runs.** **Ruled (2026-10-07)** (M6):
  - the human north-star run (`--exec tests/cli/scripts/north_star.txt`,
    before/after screenshots);
  - the Plan 0052 J9 runs repeated:
    - the four Windows whitelist scenes without `--exec`, Debug and
      Release, VVL on;
    - the Android emulator default scene.

  The reason: `RuntimeApplication` and `main.cpp` change.
- **J9 — Script syntax and output.** **Ruled (2026-10-07): as P6/P7.**
  - **Syntax:** `#` comments and blank lines take no frame; an erroneous
    line prints `error:` and the script continues.
  - **Output:** each command is echoed (`> `), and a done line summarizes.
    `ok` / `refused` / `error:` prefixes and bare values distinguish CLI
    output on stdout.
  - **Floats:** `%.9g` / `strtof` text.

## Rollback Plan

- **After merge:** revert the implementation PR as a whole.
  - The new modules are additive.
  - `listEntities()` is an additive Query.
  - Runtime's change is the endpoint member, `openConnection()` and
    `--exec`.
  - No format, asset or golden changes.
- **Before merge:** revert milestone by milestone, in reverse order.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:

- [ ] Every golden exact in Debug and Release at the final gate. None
      re-captured.
- [ ] `runFrame()`'s body unchanged; the path guard holds.
- [ ] The CLI boundary scans green (R8).
- [x] J1's Spec Correction recorded (Spec 0054 Correction 2026-10-07).
- [ ] The post-merge docs items (J6) are queued.
