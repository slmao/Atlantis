# Plan: Schema & Reflection Foundation

- **Spec:** [Spec 0048: Schema & Reflection Foundation](../specs/0048-schema-reflection-foundation.md)
  (`Approved`, 2026-10-05, [PR #198](https://github.com/slmao/Atlantis/pull/198);
  rulings Q1–Q4 binding; corrected 2026-10-05 per rulings J1/J2 below) —
  [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md)
  (`Accepted`)
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-05 — reviewed this Plan and
  [Spec 0048](../specs/0048-schema-reflection-foundation.md) together in
  [PR #199](https://github.com/slmao/Atlantis/pull/199) and explicitly
  authorized Implementation from Milestone 1 (chat confirmation). J1 ruled
  (a), carried as the Spec's 2026-10-05 Correction; J2–J7 ruled as
  recommended. See Joint Review decisions below.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0048 R1–R8 exactly: the `atlantis::schema` vocabulary in
Atlantis Core, World's and Asset System's descriptor tables for the R5 type
set, per-module enumeration, and the drift, uniqueness and boundary tests.
Descriptive only (ADR-0099 D5). No existing source, artifact, sidecar,
catalog, golden or shader byte changes. No dependency is added.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `a936c5d` (PR #198 merged).

1. **Core** is three flat public headers, `atlantis/{assert,log,result}.h`,
   plus `src/{assert,log}.cpp`. There is no `atlantis/core/` subdirectory
   (`src/core/CMakeLists.txt`). `tests/core` has no boundary test.
2. **World types** are plain aggregates with default member initializers:
   - `transform.h`: three `Vec3`;
   - `camera.h`: `CameraFog` (Vec3 + 4 float), `CameraBloom` (2 float),
     `Camera` (4 float + `fog` + `bloom`);
   - `light.h`: `LightKind {Directional, Point}`, `Light`;
   - `renderable.h`: `AssetId meshAsset`,
     `std::optional<AssetId> materialAsset`;
   - `vec3.h`: `world::Vec3` is a struct `{float x, y, z}`.
3. **`MaterialAssetData`** (`material_types.h:69-131`) has 18 fields. Vectors
   are C arrays (`float baseColorFactor[4]`, `sheenColor[3]`,
   `emissiveFactor[3]`). Its three texture references (`textureAsset`,
   `normalMapTexture`, `emissiveTexture`) are plain `AssetId` with `0` = none,
   not `std::optional`. Two of its fields have enum types that are **not** in
   R5's v1 set: `MaterialSamplerFilter filter` and
   `MaterialSamplerAddressMode addressMode` (`:12-20`). Ruling J1 added both
   to R5 by Spec correction.
4. **`EntityRef`** (`entity_ref.h`) is `{AssetGuid scene; EntityGuid entity;}`.
   Each GUID is a struct over `std::array<std::byte, 16>` (`asset_guid.h:24-34`).
   No scene grammar stores an `EntityRef` yet; it has text and binary codecs.
5. **Existing FNV-1a-64.** Asset System has a private constexpr
   `detail::fnv1a64(span<const byte>)` (`src/asset_system/src/fnv1a64.h`,
   Plan 0047 ruling I1), plus public `fnv1a128` (`asset_guid.h:55`). Neither
   is touched (Spec Proposed Design: sibling, no churn).
6. **Boundary-scan pattern:** `tests/asset_system/module_boundary_tests.cpp`
   scans `ATLANTIS_ASSET_SYSTEM_SOURCE_DIR` at run time for forbidden include
   prefixes; `tests/world` has the same.
7. **Build/warnings:** `atlantis_compiler_warnings` is `/W4 /WX` (MSVC) and
   `-Wall -Wextra -Werror` (Clang). Android builds `atlantis_runtime_android`
   with tests off, so World and Asset System *library* sources, and any
   `static_assert` in them, compile under the NDK's libc++.
8. **Serialization coverage** (the basis for P6's `Serializable` rule): the
   scene source carries transform, camera (`camera_fov_y/near_z/far_z`,
   `camera_exposure_ev`, `fog=`/`fog_color=`, `bloom=`), light
   (`light=`/`color=`/`intensity=`/`range=`), `mesh=` and `material=`
   (`scene_source.cpp:37-79`). The material source/artifact carry every
   `MaterialAssetData` field.

## Plan-stage decisions

These are details the Spec left to the Plan. None changes a Spec requirement
or ADR decision. Where a Spec or ADR sentence could be read two ways, the
reading chosen is stated and listed under Joint Review decisions.

**P1 — File placement.**
- Core: `src/core/include/atlantis/schema.h` and `src/core/src/schema.cpp`.
  This is ADR-0099 D1's one header/source pair, flat like Core's other
  headers.
- World: `src/world/include/atlantis/world/world_schema.h` (declares
  `worldSchema()`) and `src/world/src/world_schema.cpp` (tables).
- Asset System: `src/asset_system/include/atlantis/asset_system/asset_system_schema.h`
  (declares `assetSystemSchema()`) and `src/asset_system/src/asset_system_schema.cpp`.
- Tables live in the `.cpp`, so the public headers stay free of the described
  types' layout detail. The header file names follow the Q4 function names.

**P2 — Vocabulary shape** (R1, R2; the member layout is the Plan's per R2).
- `TypeId` and `FieldId` are distinct structs with `std::uint64_t value` and
  defaulted `==`/`<=>`. They do not convert to each other.
- `TypeKind : std::uint8_t {Primitive, Struct, Enum}`. `Primitive` is only a
  *field* kind. A `TypeDescriptor` is always `Struct` or `Enum` in v1.
- `PrimitiveKind : std::uint8_t {UInt64, Float32, Vec3Float32, Vec4Float32,
  AssetGuid, EntityGuid}`, exactly R5's six, with no `None` member. It also
  has constexpr `primitiveSize()` and `primitiveAlignment()` (8/8, 4/4,
  12/4, 16/4, 16/1, 16/1), which define the shape contract the drift tests
  check (P4).
- `FieldFlags : std::uint16_t` has `None = 0` plus R1's five bits. It has
  constexpr `|`, `&` and `hasFlags()`. `FieldDescriptor` stores
  `FieldFlags`, not a raw `uint16_t`.
- `FieldDescriptor`: `FieldId id; std::string_view name; TypeKind kind;
  PrimitiveKind primitive; TypeId type; FieldFlags flags;
  std::uint32_t byteOffset;`
  - `primitive` is meaningful only when `kind == Primitive`.
  - `type` is meaningful only otherwise; it is `TypeId{0}` for primitive
    fields.
- `EnumConstantDescriptor`: `std::string_view name; std::int64_t value;`.
  `int64_t` holds any v1 enum's underlying value (all are `int`).
- `TypeDescriptor` is as in the Spec sketch: `TypeId id; std::string_view
  name; TypeKind kind; std::uint32_t schemaVersion;
  std::span<const FieldDescriptor> fields;
  std::span<const EnumConstantDescriptor> constants;`.
- `schema.cpp` holds only `toString(TypeKind)` and `toString(PrimitiveKind)`,
  which test diagnostics and later consumers use (J6). Everything else is
  constexpr in the header.
- The header states the thread-safety contract in one line (R7).

**P3 — Identity** (R3).
- `constexpr std::uint64_t fnv1a64(std::string_view)`: offset basis
  `0xcbf29ce484222325`, prime `0x100000001b3`, over the name's bytes.
- `constexpr TypeId typeId(std::string_view qualifiedName)`.
- `constexpr FieldId fieldId(std::string_view qualifiedType,
  std::string_view fieldName)` hashes `qualifiedType`, then `'.'`, then
  `fieldName` as one continued hash. It equals
  `fnv1a64("<type>.<field>")` with no concatenation or allocation.
- **Qualified name:** the namespace path below `atlantis::`, joined by `::`
  (`world::Camera`, `asset_system::MaterialAssetData`), as in the Spec's own
  examples. Names are ASCII identifiers.
- **Reference vectors** (computed independently for this Plan; M1 pins them):

  | Input | FNV-1a-64 |
  |---|---|
  | `""` | `0xcbf29ce484222325` |
  | `"a"` | `0xaf63dc4c8601ec8c` |
  | `"foobar"` | `0x85944171f73967e8` |
  | `world::Camera` | `0x8eeda43ea50d5544` |
  | `world::Camera.exposureCompensationEv` | `0xdeb0a07e14e61af6` |
  | `asset_system::MaterialAssetData` | `0x74e2bf2e4f643028` |
  | `asset_system::EntityRef.entity` | `0xa63ced230044da30` |

**P4 — Primitive shape contract.**
- A primitive kind states size, alignment and element layout, not a C++
  type.
- `Vec3Float32` is three contiguous `float`s. It therefore covers both
  `world::Vec3` and `float[3]`. `Vec4Float32` covers `float[4]`.
- For an `Optional` field the contract applies to `T`, and the member must be
  `std::optional<T>`.

**P5 — Field rules.**
- `byteOffset` is `offsetof(T, member)` of the member itself. For an
  `Optional` field that is the `std::optional` object; interpreting it is the
  future accessor layer's job (ADR-0099 D5).
- Struct/Enum fields reference a `TypeId` in the **same module's** table. Every
  v1 reference is module-local: Camera → CameraFog/CameraBloom, Light →
  LightKind, MaterialAssetData → its enums. Cross-module references are not
  needed in v1 and are not designed here.
- Enum constants carry no IDs; R2 asks for name + value only.
- Every v1 descriptor's `schemaVersion` is `1` (R8).

**P6 — Flag assignment** (R1, R4; the rule is J3).
- `Serializable`: every v1 field. Each one round-trips through a committed
  format (scene source/artifact, material source/artifact) or, for
  `EntityRef`, through its own codec (reading item 8).
- `Editable`: the World component fields only (`Transform`, `CameraFog`,
  `CameraBloom`, `Camera`, `Light`, `Renderable`). That is the live-editing
  surface Q1's ruling names. `MaterialAssetData` (a cooked load result) and
  `EntityRef` (a reference value) are not `Editable` in v1.
- `AssetReference`: `Renderable.meshAsset`, `Renderable.materialAsset`,
  `MaterialAssetData.{textureAsset, normalMapTexture, emissiveTexture}`
  (UInt64), and `EntityRef.scene` (AssetGuid).
- `EntityReference`: `EntityRef.entity` (EntityGuid).
- `Optional`: `Renderable.materialAsset` only, the one `std::optional`
  member (R4's definition). The material texture references use the `0`
  sentinel, a value-domain fact the validators own, so they are not
  `Optional` (J2).

**P7 — Compile-time verification** (non-functional: "tables
compile-time-verified").
- Core provides `constexpr bool isWellFormed(std::span<const TypeDescriptor>)`
  for one module's table. It checks:
  - each `id == typeId(name)` and each field `id == fieldId(type, field)`;
  - type kind is `Struct` (fields, no constants) or `Enum` (constants, no
    fields);
  - `schemaVersion >= 1`;
  - names are non-empty ASCII identifiers (qualified names use `::`
    separators);
  - field and constant names are unique within their type;
  - reference flags are mutually exclusive;
  - primitive fields carry `TypeId{0}`;
  - every Struct/Enum field's `type` resolves in the same table to a
    descriptor of that kind.
- Each module's `.cpp` `static_assert`s `isWellFormed` over its table, plus
  `std::is_standard_layout_v<T>` for every described struct. These run on the
  Windows build **and the Android NDK build** (reading item 7), so the
  portability requirement is checked on both compilers.

**P8 — Enumeration.**
- `std::span<const schema::TypeDescriptor> worldSchema() noexcept` and
  `assetSystemSchema() noexcept` (Q4) return namespace-scope `constexpr`
  arrays.
- Listing order follows R5's table and is documented as stable listing order,
  not identity. Each header states the concurrent-reads contract (R7).

**P9 — Drift tests** (Spec Testing plan; mechanism in J4). For each described
struct, the module's sync test does four things:
- **Arity, at compile time:** a structured binding with exactly the
  described field count (`const auto& [a, b, c] = Transform{};`). Adding a
  member to the struct breaks the test's compile, and a `static_assert`
  ties that count to the table's `fields.size()`. Structured bindings count
  a C array as one member, so `MaterialAssetData`'s arrays need no special
  case.
- **Name ↔ member:** a test-local macro takes the member token. It
  stringizes it, looks up the field of that name in the table, and checks
  its `byteOffset == offsetof(T, member)`. A header rename then breaks the
  test's compile, and the string comes from the token, never from a second
  literal.
- **Shape:** `sizeof`/`alignof` of the member against `primitiveSize`/
  `primitiveAlignment` for primitives, or the referenced C++ type for
  struct/enum fields, plus the `std::optional<T>` form for `Optional`.
- **Kind and flags** against the P6 rule, stated once in the test.

Enums are checked by name and value for every enumerator via the same macro.
C++20 cannot enumerate an enum's members, so an *added* enumerator is not
detected mechanically (J7).

## Milestones / Task Breakdown

Four milestones. Commit prefixes: `feat:` for code, `test:` for test-only
steps, `docs:` for M4's documentation step. Each milestone's gate must pass
before the next begins.

### M1 — Core vocabulary (R1, R2, R3, R7)

1. `schema.h`/`schema.cpp` per P2, P3 and P7; add `schema.cpp` to
   `src/core/CMakeLists.txt`.
2. `tests/core/schema_tests.cpp`:
   - the P3 reference vectors and pins;
   - compile-time evaluation (`static_assert` on `typeId`/`fieldId`);
   - `TypeId`/`FieldId` distinctness;
   - flag operators;
   - `isWellFormed` accepting a minimal valid synthetic table and rejecting
     each P7 violation on its own, including both reference flags on one
     field.
3. `tests/core/module_boundary_tests.cpp` (Spec Testing plan): every
   `.h`/`.cpp` under `src/core` is scanned. Any `#include` of
   `atlantis/<dir>/` (another module's headers; Core's own are flat) or
   `vulkan/` fails. A new `ATLANTIS_CORE_SOURCE_DIR` compile definition
   follows the Asset System precedent.

*Gate:* Debug + Release build, all tests. This milestone only adds code.

### M2 — World tables (R4, R5 World rows, R6, R8)

1. `world_schema.h/.cpp` holds descriptors for `Transform`, `CameraFog`,
   `CameraBloom`, `Camera`, `Light`, `LightKind` and `Renderable`, with
   flags per P6 and the P7 `static_assert`s.
2. `tests/world/world_schema_tests.cpp`: the P9 sync checks for every World
   type, and the enumeration order and count.

*Gate:* Debug + Release build, all tests; `assembleDebug` succeeds (the
standard-layout and offset `static_assert`s under libc++, including
`Renderable`'s `std::optional`).

### M3 — Asset System tables (R4, R5 Asset System rows, R6, R8)

1. `asset_system_schema.h/.cpp` holds descriptors for `MaterialAssetData`
   (18 fields, reading item 3), `MaterialKind`, `MaterialAlphaMode`,
   `MaterialSamplerFilter`, `MaterialSamplerAddressMode` (R5 as corrected,
   ruling J1), and `EntityRef`.
2. `tests/asset_system/asset_system_schema_tests.cpp`: the P9 sync checks,
   and the enumeration order and count.
3. The existing Asset System boundary scan covers the new files unchanged:
   they include only Core and Asset System headers.

*Gate:* as M2.

### M4 — Cross-module checks and acceptance

1. `tests/world/schema_uniqueness_tests.cpp` (tests/world links both
   modules) checks `worldSchema()` + `assetSystemSchema()` combined. No
   duplicate `TypeId`, `FieldId` or qualified type name may appear.
2. **Byte-behavior guard:** the full Debug + Release suites, including the
   existing format, serialization and image-regression suites, pass
   unchanged. `git diff origin/main --stat` shows no change under
   `assets/`, `tests/image_regression/goldens/`, `shaders/`, or any
   existing `src/` file other than the `CMakeLists.txt` of `src/core`,
   `src/world` and `src/asset_system`.
3. **Docs** (Definition of Done "as-built architecture"):
   - one sentence in `docs/architecture/module_boundaries.md` Atlantis Core
     *Responsibilities* naming the schema vocabulary and ADR-0099;
   - the Spec-named obligation added as one checklist item in
     `docs/process/definition-of-done.md` *Code*: "a shape change to a
     described type updates its descriptor table and bumps its
     `SchemaVersion` in the same PR" (J5).

*Gate:* Debug + Release, all tests; `assembleDebug`; final diff review
against this Plan.

## Files / Modules Touched (expected)

- **Core:**
  - new `src/core/include/atlantis/schema.h`, `src/core/src/schema.cpp`;
  - changed `src/core/CMakeLists.txt`.
- **World:**
  - new `src/world/include/atlantis/world/world_schema.h`,
    `src/world/src/world_schema.cpp`;
  - changed `src/world/CMakeLists.txt`.
- **Asset System:**
  - new `src/asset_system/include/atlantis/asset_system/asset_system_schema.h`,
    `src/asset_system/src/asset_system_schema.cpp`;
  - changed `src/asset_system/CMakeLists.txt`.
- **Tests:**
  - `tests/core/`: new `schema_tests.cpp`, `module_boundary_tests.cpp`;
    `CMakeLists.txt` (sources, `ATLANTIS_CORE_SOURCE_DIR`);
  - `tests/world/`: new `world_schema_tests.cpp`,
    `schema_uniqueness_tests.cpp`; `CMakeLists.txt`;
  - `tests/asset_system/`: new `asset_system_schema_tests.cpp`;
    `CMakeLists.txt`.
- **Docs:** `docs/architecture/module_boundaries.md` (one sentence),
  `docs/process/definition-of-done.md` (one item).

**Not touched:**
- every described type's header (`transform.h`, `camera.h`, `light.h`,
  `renderable.h`, `vec3.h`, `material_types.h`, `entity_ref.h`,
  `asset_guid.h`);
- every parser, cooker, loader, codec and validator;
- Asset System's `fnv1a64.h` and `fnv1a128`;
- Runtime, Tools, Renderer, RenderGraph, RHI, Vulkan Backend, Platform,
  Shader System, Android Gradle files;
- all assets, sidecars, goldens and shaders;
- no dependency is added.

## Sequencing & Dependencies

M1 → M2 (the World tables use the vocabulary and its validator) → M3 (same)
→ M4 (needs both tables). M2 and M3 are independent of each other.

A post-merge docs pass (registry row, CLAUDE.md "Current repository state"
if it is still current, per the PR #197 precedent) follows the implementation
PR as its own `docs/` PR. It is not part of this Plan's code scope.

## Verification Checklist

This maps to Spec 0048's Testing & Verification Plan.

- [ ] **Vocabulary (M1):**
  - reference vectors and ID pins (P3);
  - compile-time derivation;
  - reference-flag exclusion and every other `isWellFormed` rule, each
    violated alone.
- [ ] **Core boundary (M1):** the `src/core` include scan passes and is shown
  to fail on a planted forbidden include during development (not committed).
- [ ] **Descriptor sync (M2, M3):** for every described struct:
  - standard-layout `static_assert`;
  - compile-time arity against the table;
  - every field's name, kind, shape, flags and `offsetof` (P9);
  - every enum's enumerator names and values.
- [ ] **Drift is a build failure, demonstrated:** during development, one
  member added to `CameraBloom` and one renamed in `Light` each fail the
  test build. This is recorded in the PR and not committed.
- [ ] **Cross-module uniqueness (M4).**
- [ ] **Byte-behavior guard (M4):** full Debug + Release `ctest` green; no
  golden re-capture; the M4 step 2 diff check.
- [ ] **Android:** `assembleDebug` at the M2, M3 and M4 gates (compile-time
  checks under libc++). No emulator run: the Runtime does not call the new
  functions.
- [ ] **GPU gates:** no new image-regression test and no new Validation-Layer
  obligation, since no GPU path is touched (Spec Testing plan). The existing
  suites run as part of the byte-behavior guard.

## Joint Review decisions — ruled (Joint Human Review, slmao, 2026-10-05, PR #199)

J1 was ruled (a). J2–J7 were each ruled as recommended. None of them changes
an ADR-0099 decision.

- **J1 — Sampler enums missing from R5.** **Ruled (2026-10-05): (a).**
  - `MaterialAssetData.filter` and `.addressMode` have enum types that the
    approved R5 set did not list. Without them the struct cannot be
    described completely (R2; P9 arity).
  - Spec 0048 carries a post-Approval Correction adding
    `MaterialSamplerFilter` and `MaterialSamplerAddressMode` to R5. It is a
    separate commit in this PR. M3 describes both.
  - Rejected: (b) omitting the two fields, which contradicts R5 and P9;
    (c) describing them as primitives, which is wrong by R2.
- **J2 — `Optional` on `0`-sentinel references.** **Ruled (2026-10-05): as
  recommended.**
  - Per R4, `Optional` marks only `std::optional` members, so only
    `Renderable.materialAsset` (P6).
  - The material texture references are not `Optional`; their `0` = none
    convention stays a value-domain fact owned by the validators.
  - The same Spec Correction fixes Q3's rationale, which had claimed
    `std::optional` references in `MaterialAssetData`.
- **J3 — `Serializable`/`Editable` meaning.** **Ruled (2026-10-05): as
  recommended.** P6 is binding:
  - `Serializable`: the value round-trips through a committed format or
    codec.
  - `Editable`: the field is on the live World editing surface, so World
    component fields only in v1.
- **J4 — A test-only macro in the sync tests.** **Ruled (2026-10-05): as
  recommended.**
  - One stringizing macro is allowed in the sync-test translation units
    only, to tie each name string to its member token (P9).
  - Never in headers or tables: ADR-0099 D4 still governs descriptor
    authoring.
- **J5 — Global Definition of Done item.** **Ruled (2026-10-05): as
  recommended.** M4 adds the same-PR "update the table and bump
  `SchemaVersion`" obligation as one item in
  `docs/process/definition-of-done.md`.
- **J6 — Small Core API beyond the vocabulary nouns.** **Ruled (2026-10-05):
  as recommended.** Core ships `primitiveSize`/`primitiveAlignment`,
  `isWellFormed`, and the two `toString`s in `schema.cpp` (P2, P7).
- **J7 — Enumerator completeness is not mechanical.** **Ruled (2026-10-05):
  as recommended.**
  - Accepted as a residual risk. An added enumerator is caught by the J5
    Definition of Done item and by review.
  - Every listed enumerator's name and value is still checked (P9).

## Rollback Plan

The change is additive: no existing behavior or format depends on it. Revert
the implementation PR as a whole, or milestone by milestone in reverse order
on the branch before merge. No data or content migration is involved.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas:
- [ ] No existing source, artifact, sidecar, catalog, golden or shader byte
      changed; no golden re-captured.
- [ ] Drift-is-a-build-failure demonstration recorded in the PR.
- [ ] `assembleDebug` passing at the final gate.
- [ ] The Definition of Done item for described-type changes added (J5).
