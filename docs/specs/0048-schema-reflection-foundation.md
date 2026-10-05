# Spec: Schema & Reflection Foundation

- **Status:** In Review ([PR #198](https://github.com/slmao/Atlantis/pull/198))
- **Author:** slmao (drafted by ZCode at explicit human direction)
- **Created:** 2026-10-05
- **Related Plan(s):** none yet — drafting a Plan is authorized only after this
  Spec's Approval.
- **Approval:** pending — scope and sequencing (this spec ahead of the
  Tool/Editor Connection Protocol candidate) fixed by the maintainer,
  2026-10-05 (chat).
- **Related ADR(s):**
  [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md)
  (`Proposed`, drafted alongside this spec) — where the vocabulary lives, who
  owns the descriptor tables, how IDs are derived, and the descriptive-only
  scope of the first version.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Add a small, dependency-free **schema vocabulary** to Atlantis Core —
`TypeId`/`FieldId`, type/field/enum descriptors, a small flag set, and a
per-type `SchemaVersion` — plus hand-authored descriptor tables for the
engine's current data surface: the World components (`Transform`,
`CameraFog`, `CameraBloom`, `Camera`, `Light`, `Renderable`),
`MaterialAssetData`, and Spec 0047's GUID reference types. The first version
is **descriptive only**: it states what the engine's data types are, never
how to read or write them. It exists so the Tool/Editor Connection Protocol
(Candidate 2) and any future schema-migration or authoring tooling can
address engine data through one stable, enumerable, versioned inventory
instead of a per-component verb surface (`GetTransform`/`SetTransform`/`GetFog`/`SetFog`/…)
that grows by one verb pair per component per feature.

## Motivation / Problem Statement

### Schema proliferation is already here

Current state read at line level, 2026-10-05, `origin/main` 2ab7da5.
Sixteen hand-maintained version constants across the asset layer, each
exact-match rejecting every older version ([ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
discipline; the single sanctioned dual reader is the mesh v4/v5 loader
dispatch, [ADR-0087](../adr/0087-asset-system-index-width-representation-and-artifact-version-dispatch.md)):

| Versioned stream | Current | Constant lives at |
|---|---|---|
| Scene source (text) | v7 | `src/asset_system/src/scene_source.cpp:20` |
| Scene artifact | v7 | `src/asset_system/include/atlantis/asset_system/scene_artifact.h:37` |
| Scene metadata | v2 | `src/asset_system/src/scene_metadata.cpp:12` |
| Material source (text) | v10 | `src/asset_system/src/material_source.cpp:15` |
| Material artifact | v9 | `src/asset_system/include/atlantis/asset_system/material_artifact.h:78` |
| Material metadata | v9 | `src/asset_system/src/material_metadata.cpp:12` |
| Mesh source (text) | v3 | `src/asset_system/src/mesh_source.cpp:16` |
| Mesh artifact (u16 indices) | v4 | `src/asset_system/include/atlantis/asset_system/mesh_artifact.h:35` |
| Mesh artifact (u32 indices) | v5 | `src/asset_system/include/atlantis/asset_system/mesh_artifact.h:75` |
| Mesh/asset metadata | v2 | `src/asset_system/src/asset_metadata.cpp:12` |
| Texture artifact | v3 | `src/asset_system/include/atlantis/asset_system/texture_artifact.h:49` |
| Texture metadata | v4 | `src/asset_system/src/texture_metadata.cpp:12` |
| Environment artifact | v1 | `src/asset_system/include/atlantis/asset_system/environment_artifact.h:14` |
| Environment metadata | v2 | `src/asset_system/src/environment_metadata.cpp:14` |
| Asset catalog source | v1 | `src/asset_system/src/asset_catalog_source.cpp:16` |
| Cooker behavior | `/1` | `src/asset_system/include/atlantis/asset_system/cook.h:18` |

Since Spec 0035, every feature spec has bumped at least one: 0035 three
times (its three schema-bumped milestones), 0041 material 6→7, 0042 7→8,
0043 scene source/artifact 4→5, 0044 scene source/artifact 5→6, 0045
`.atex` and texture metadata 2→3, 0046 material 8→9, 0047 scene
source/artifact 6→7, material source 9→10, and every metadata sidecar +1.

The "schema" of each stream exists only as parser/encoder code plus a header
comment. Nothing in the repository can answer "what fields does camera data
carry?" without reading `scene_source.cpp`, `scene_artifact.h`, and
`world/camera.h` and knowing they correspond. Per feature there are two
parallel hand-synced shapes — `world::CameraFog` (`src/world/include/atlantis/world/camera.h:11`)
vs `asset_system::DecodedCameraFog` (`src/asset_system/include/atlantis/asset_system/scene_types.h:31`),
likewise `CameraBloom`/`DecodedCameraBloom` — kept in agreement by
convention. The asset catalog ([ADR-0098](../adr/0098-asset-catalog-and-catalog-based-resolution.md))
already records each asset's `artifactSchema`/`sourceSchema` as opaque
numbers (`asset_catalog.h:45-46`) with nothing that names what those numbers
mean.

### The next consumer would freeze the wrong shape

The next backlog candidate is the Tool/Editor Connection Protocol
(Candidate 2, `docs/specs/README.md`), with its process model still
undecided. Designed against today's API — Asset System and World expose
concrete structs plus concrete free functions, with no generic access — the
protocol naturally becomes per-component verbs: `GetTransform`/`SetTransform`,
`GetLight`/`SetLight`, `GetFog`/`SetFog`, `GetBloom`/`SetBloom`, … That is
the current World API re-expressed as RPC: an unversioned, unenumerable
surface with the same growth pattern as the version constants above, except
embedded in a wire protocol where changing it later costs compatibility.

A tiny descriptive layer built **first** gives that protocol (and eventual
migration/authoring tooling) a stable vocabulary to address properties
through — enumerate types, address a field by ID, carry a schema version —
so features grow by editing descriptor tables, not protocol verbs. Hence
this spec precedes Candidate 2.

## Goals

- A schema **vocabulary** in Atlantis Core: `TypeId`, `FieldId`,
  `TypeKind {Primitive, Struct, Enum}`, a `PrimitiveKind` set,
  `FieldFlags`, `TypeDescriptor`/`FieldDescriptor`/`EnumConstantDescriptor`,
  and a per-type `SchemaVersion`.
- **Build-stable identity**: IDs derived deterministically from qualified
  names, identical across builds, platforms, editor and runtime processes.
- **Descriptor tables for the current data surface** — the v1 type set
  listed under Requirements.
- **Per-module enumeration** with no global registry and no runtime
  registration.
- **Drift detection**: tests validate every descriptor against the C++ type
  it describes, so a struct change without a descriptor change fails the
  build.
- **Sufficient metadata for a later accessor layer** (field byte offsets),
  so future specs layer data access on top of these tables instead of
  re-designing them.

## Non-Goals

- No serialization format, parser, cooker, or loader change: ADR-0045's
  append-bump-exact-reject discipline is untouched, and no existing
  source/artifact byte changes.
- No value get/set (accessor) API — reading and writing instances through
  descriptors is the consumer spec's decision (e.g. the editor protocol),
  layered on the offsets later.
- No runtime type registration, dynamic types, or scripting bindings.
- No code generation, macros, or third-party reflection library.
- No rewiring of Asset System cook/load or World to be schema-driven.
- Not the Editor Connection Protocol itself: Candidate 2 remains a
  candidate; this spec only precedes it.
- The scene-source `Decoded*` DTO mirrors are not described in v1 (see
  Open Questions).

## Requirements

### Functional

- **R1 — Vocabulary.** Atlantis Core provides distinct strong types `TypeId`
  and `FieldId`; `TypeKind {Primitive, Struct, Enum}`; `PrimitiveKind`;
  bit-wise combinable `FieldFlags {Serializable, Editable, AssetReference,
  EntityReference, Optional}` (the two reference flags are mutually
  exclusive); and a `SchemaVersion` (an unsigned 32-bit per-descriptor
  value).
- **R2 — Descriptors.** A `FieldDescriptor` carries its `FieldId`, name,
  field type (primitive kind, or the referenced type's `TypeId` for
  struct/enum fields), flags, and a byte offset within the described type.
  A `TypeDescriptor` carries its `TypeId`, qualified name, kind,
  `SchemaVersion`, and either its field list (struct) or its constant list
  (`EnumConstantDescriptor`: name + value, enums are described by
  enumerators, not by an underlying primitive). The exact member layout is
  the Plan's; this requirement fixes the information each descriptor must
  state.
- **R3 — Identity.** `TypeId` = FNV-1a-64 of the namespace-qualified type
  name (e.g. `world::Camera`); `FieldId` = FNV-1a-64 of
  `qualifiedType.fieldName` (e.g. `world::Camera.exposureCompensationEv`).
  Derivation is computable at compile time, deterministic across
  builds/platforms, and unique across all modules' tables (test-enforced).
- **R4 — Reference semantics without dependencies.** `AssetGuid` and
  `EntityGuid` are descriptive `PrimitiveKind`s: the vocabulary states that
  a field is a 128-bit GUID by kind, and what it references by flag. Core
  includes nothing from Asset System or World. An `AssetId` key field is
  `UInt64` + `AssetReference`; a GUID field carries `AssetReference` or
  `EntityReference` per its meaning; `Optional` marks an absent-able field
  (`std::optional` in the C++ type).
- **R5 — The v1 descriptor set** (exact; fields as the headers state them
  today, the Plan verifying each):

  | Type | Module | Kind | Fields (v1) |
  |---|---|---|---|
  | `world::Transform` | World | struct | `localPosition`, `localEulerAnglesRadians`, `localScale` (Vec3 ×3) |
  | `world::CameraFog` | World | struct | `color` (Vec3), `density`, `height`, `heightFalloff`, `maxOpacity` |
  | `world::CameraBloom` | World | struct | `strength`, `threshold` |
  | `world::Camera` | World | struct | `fovYRadians`, `nearZ`, `farZ`, `exposureCompensationEv`, `fog` → `CameraFog`, `bloom` → `CameraBloom` |
  | `world::Light` | World | struct | `kind` → `LightKind`, `color` (Vec3), `intensity`, `range` |
  | `world::LightKind` | World | enum | `Directional`, `Point` |
  | `world::Renderable` | World | struct | `meshAsset` (UInt64 + `AssetReference`), `materialAsset` (optional, same kinds/flags + `Optional`) |
  | `asset_system::MaterialAssetData` | Asset System | struct | per `material_types.h` as of this spec; the Plan enumerates fields and flags from the current header |
  | `asset_system::MaterialKind` | Asset System | enum | per `material_types.h:44` |
  | `asset_system::MaterialAlphaMode` | Asset System | enum | per `material_types.h:58` |
  | `asset_system::EntityRef` | Asset System | struct | `scene` (AssetGuid + `AssetReference`), `entity` (EntityGuid + `EntityReference`) |

  `PrimitiveKind` v1 ships exactly what this set exercises: `UInt64`,
  `Float32`, `Vec3Float32`, `Vec4Float32`, `AssetGuid`, `EntityGuid`.
  Additional kinds or flag bits are additive vocabulary changes; changed
  flag/kind semantics are not.
- **R6 — Per-module enumeration.** World and Asset System each expose one
  function returning `std::span<const TypeDescriptor>` over static
  immutable storage (working names `worldSchema()`, `assetSystemSchema()`;
  naming ruled with the Plan). No global registry, no runtime registration,
  no mutation.
- **R7 — Thread-safety documented at the API**: immutable static data, safe
  for concurrent reads (one documented line, per AGENTS.md threading rules).
- **R8 — Version rule.** Each `TypeDescriptor`'s `SchemaVersion` starts at 1
  and is bumped whenever the described C++ type's field set changes
  (add/remove/rename/retype). It is deliberately **decoupled** from
  ADR-0045 artifact-format constants: descriptor versions describe C++ shape,
  file versions describe bytes — different namespaces. The correspondence
  (e.g. "`Camera` gained fog when scene source went 4→5") stays in history,
  not in the tables.

### Non-functional

- **Performance:** zero allocation and zero locking at query time; spans
  over static storage; tables compile-time-verified.
- **Memory:** O(static tables); a few hundred bytes for the v1 set.
- **Portability:** pure C++20, no platform code, identical on Windows and
  Android; described types must be standard-layout (byte offsets are only
  meaningful there).
- **Dependencies:** none added anywhere. Core gains no include beyond the
  standard library.
- **Behavior:** byte-identical formats — no existing source, artifact,
  metadata, catalog, golden, or shader output changes.

## Proposed Design

A single header/source pair in Core, namespace `atlantis::schema`,
conceptually:

```cpp
struct FieldId   { std::uint64_t value; };  // fnv1a64("world::Camera.density")
struct TypeId    { std::uint64_t value; };  // fnv1a64("world::Camera")

struct FieldDescriptor {
    FieldId           id;
    std::string_view  name;
    TypeKind          kind;       // Primitive, or Struct/Enum reference
    PrimitiveKind     primitive;  // meaningful when kind == Primitive
    TypeId            type;       // meaningful when kind != Primitive
    std::uint16_t     flags;      // FieldFlags bits
    std::uint32_t     byteOffset; // within the described standard-layout type
};

struct TypeDescriptor {
    TypeId            id;
    std::string_view  name;          // qualified: "world::Camera"
    TypeKind          kind;
    std::uint32_t     schemaVersion;
    std::span<const FieldDescriptor> fields;      // Struct
    std::span<const EnumConstantDescriptor> constants; // Enum
};
```

- ID derivation is a `constexpr` FNV-1a-64 owned by the vocabulary. It is a
  64-bit sibling of Asset System's `fnv1a128` (`asset_guid.h:55`), not a
  refactor of it — no unrelated churn.
- Descriptors live **beside the types they describe**: a World schema
  header authors the `world::*` tables, an Asset System schema header the
  `asset_system::*` tables. Each module keeps ownership of its own
  vocabulary; Core owns only the shared metalanguage.
- Tables are immutable static data (constexpr where practical, consteval-
  verified construction at minimum), which is what makes R7's
  concurrent-reads contract free.
- What the vocabulary deliberately does not carry in v1: value accessors,
  defaults, ranges/limits (the validators in `scene_types.h` stay the
  authority), nested containers, and any file-format knowledge. Adding any
  of these is a reviewed follow-up, not a silent addition.

## Architectural Impact

Yes — a new public cross-module API surface in Atlantis Core that Asset
System and World both consume. The decisions (home in Core rather than a new
top-level module; module-owned constexpr tables with per-module enumeration
and no registry; name-derived FNV-1a-64 identity; descriptive-only scope
with offsets as data but no accessor API; flag semantics; the decoupled
per-descriptor version counter) are recorded in
[ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md),
drafted alongside this spec. No dependency, threading-model, memory-ownership,
or backend change.

## Alternatives Considered

1. **Editor protocol first, per-component verbs** (the backlog's current
   order). Rejected: it freezes an unversioned verb-per-feature surface into
   a wire protocol, reproducing at protocol level exactly the proliferation
   documented above, where changing it later costs compatibility.
2. **A reflection library or codegen** (RTTR, Boost.Describe, `visit_struct`,
   external schema compiler). Rejected: a new dependency against the Golden
   Rule, and far more machinery than describing eleven types needs.
   Reconsider only if hand-authored tables demonstrably fail to keep up.
3. **X-macro member lists** to auto-derive descriptors from one source of
   truth per struct. Rejected for v1: it introduces a macro dialect the
   repository does not have. Constexpr tables plus drift tests first; a
   macro/codegen ergonomics layer can be its own small reviewed step later.
4. **Hand-assigned ordinal IDs with central allocation.** Rejected: it needs
   an allocation authority across modules plus name↔id tables anyway;
   name-derived hashes are deterministic, self-describing, and
   collision-tested per build.
5. **A new top-level Schema/Reflection module.** Rejected for v1: the
   vocabulary is a Core-sized, dependency-free cluster; a module split now
   is boundary ceremony with no consumer pressure. ADR-0099 records the
   revisit condition.
6. **One central engine-wide descriptor table** (all types registered in a
   single Core-owned file). Rejected: modules own their types; a central
   table recreates cross-module edit contention and becomes a dependency
   magnet. Consumers compose the per-module spans.

## Testing & Verification Plan

All GPU-independent Catch2 unit tests; no image regression, no Vulkan
Validation Layer runs — no GPU path is touched (per
[testing strategy](../process/testing-strategy.md), those gates are N/A and
stated so in the implementation PR):

- `tests/core`: vocabulary invariants — compile-time-derivation, reference
  flag mutual exclusion, ID determinism, and **ID stability pins**: expected
  `uint64` literals for a handful of well-known IDs recorded in the test,
  changed only deliberately.
- `tests/world`, `tests/asset_system`: **descriptor sync** — for each
  described struct: a `static_assert(std::is_standard_layout_v<T>)`; field
  count, names, kinds, flags, and `byteOffset` checked against
  `offsetof(T, member)`; enums checked against the C++ enumerators
  (names and values).
- **Cross-module uniqueness:** combined World + Asset System tables — no
  duplicate `TypeId`, `FieldId`, or qualified name.
- **Byte-behavior guard:** the existing format/serialization suites pass
  untouched; no golden re-captures.
- **Core boundary test:** `src/core` may not include any other Atlantis
  module's headers — the first such test for Core, extending the
  `tests/asset_system/module_boundary_tests.cpp` pattern.

## Risks & Open Questions

Risks:

- **Descriptor rot** — a struct change without a descriptor update. The sync
  tests fail on any field/offset/name drift, and the Definition of Done adds
  "a shape change to a described type bumps its `SchemaVersion` and updates
  its table in the same PR."
- **ID collision** — FNV-1a-64 over short qualified names; negligible at
  this scale, detected by the uniqueness test; resolution is renaming, with
  names as the source of truth (a rename deliberately breaks the stability
  pins and surfaces in review).
- **Standard-layout ceiling** — byte offsets confine v1 to standard-layout
  types (all v1 types qualify). A future need to describe non-standard-layout
  or polymorphic types is a future spec's binding decision, not improvised.
- **Duplicated shapes remain** — `world::CameraFog` and `DecodedCameraFog`
  both exist; v1 describes the World side only, so the mirror pair stays
  convention-synced. Unifying them is out of scope here.

Open questions (ruled at review):

- **Q1:** Describe the scene-source `Decoded*` DTOs in v1 as well, or defer?
  Recommendation: defer — the World set covers live editing; pull the DTOs
  in when the scene-authoring tool spec exists.
- **Q2:** Vector granularity — `Vec3Float32`/`Vec4Float32` as primitives
  (proposed) or as structs of `Float32` fields? Recommendation: primitives;
  it matches how the math is consumed and keeps descriptors flat.
- **Q3:** `FieldFlags::Optional` in v1 (proposed; `Renderable.materialAsset`
  and `MaterialAssetData`'s optional references need it) or defer until a
  consumer distinguishes absent? Recommendation: include now; it is
  load-bearing for any generic accessor.
- **Q4:** Enumeration entry-point names (`worldSchema()`/`assetSystemSchema()`
  proposed). Recommendation: rule with the Plan.

## Out of Scope / Future Work

- Value accessors (get/set through a `FieldDescriptor`) — owned by the
  editor protocol spec or a dedicated binding spec layered on the offsets.
- The Tool/Editor Connection Protocol itself (Candidate 2) — expected to
  address properties by `TypeId`/`FieldId` and negotiate `SchemaVersion`s;
  this spec changes its prerequisites, not its undecided process model.
- Schema-driven parsing/serialization, version migration between artifact
  formats, describing the `Decoded*` DTOs, shaders, or RHI/RenderGraph
  types, and any tooling UI.
- A possible later promotion of `atlantis::schema` out of Core into its own
  top-level module, if the vocabulary grows access/serialization machinery
  (revisit condition recorded in ADR-0099).
