# ADR 0099: Engine Schema Core — Descriptor Vocabulary Location, Ownership, and Identity

- **Status:** Proposed
- **Date:** 2026-10-05
- **Deciders:** slmao
- **Acceptance:** pending — to be `Accepted` before or during the joint
  Spec+Plan review of [Spec 0048](../specs/0048-schema-reflection-foundation.md).
- **Related Spec:** [Spec 0048: Schema & Reflection Foundation](../specs/0048-schema-reflection-foundation.md)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- Atlantis is in schema proliferation: sixteen hand-maintained version
  constants across the asset layer (Spec 0048's motivation table), each
  feature spec bumping at least one since Spec 0035. Each format's schema
  exists only as parser/encoder code plus a header comment; nothing
  enumerates the engine's data surface, and per feature two hand-synced
  shapes exist (`world::CameraFog` vs `asset_system::DecodedCameraFog`).
- The next backlog candidate, the Tool/Editor Connection Protocol, has an
  undecided process model. Designed against today's concrete-struct APIs it
  becomes per-component wire verbs (`GetTransform`/`SetTransform`/…), which
  would freeze an unversioned, unenumerable surface into a protocol.
- The maintainer directed (2026-10-05) a minimal descriptive schema
  foundation first: type/field descriptors able to state the current data
  surface (World components, `MaterialAssetData`, Spec 0047's GUID
  reference types) — explicitly not a reflection system.
- Atlantis Core today is three files (`result.h`, `assert.h`, `log.h`), zero
  dependencies beyond the standard library. Asset System depends on Core
  only; World depends on Core and narrowly on Asset System
  ([ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)).
  Any vocabulary both Asset System and World consume must sit at or below
  both — i.e., in Core.

## Decision

1. **The schema vocabulary lives in Atlantis Core**, namespace
   `atlantis::schema`, one header/source pair to start. No new top-level
   module. Core stays standard-library-only: `AssetGuid`/`EntityGuid`
   appear as descriptive `PrimitiveKind`s, so the vocabulary never includes
   Asset System or World headers.
2. **Modules author their own descriptor tables**, beside the types they
   describe, as immutable static (constexpr-where-practical) data, and each
   exposes one enumeration function returning a span. No global registry,
   no runtime registration, no mutation — consistent with the no-global-
   mutable-singletons rule.
3. **Identity is name-derived.** `TypeId` and `FieldId` are distinct strong
   types over `uint64`, computed as FNV-1a-64 of the qualified type name
   and of `qualifiedType.fieldName`. Deterministic across builds,
   platforms, and processes (the property a future editor↔runtime protocol
   needs); uniqueness is enforced by tests across all modules' tables. This
   follows the repository's existing FNV identity practice
   ([ADR-0097](0097-guid-keyed-asset-and-entity-identity.md)'s `AssetId`
   key) without refactoring Asset System's 128-bit variant.
4. **Hand-authored constexpr tables; no macros, no codegen, no third-party
   reflection library.** Descriptor↔struct drift is caught by tests
   (field names/kinds/offsets vs `offsetof`, enums vs enumerators), not by
   generated synchronization.
5. **The first version is descriptive only.** No value get/set API, no
   serialization, no ranges/defaults (validators stay authoritative). A
   `FieldDescriptor` carries a byte offset within its described
   standard-layout type — data, not an accessor — so a later access layer
   can be added without re-designing the tables. The accessor design
   belongs to the consuming spec (e.g. the editor protocol).
6. **Flags describe meaning, kinds describe shape.** `FieldFlags`:
   `Serializable`, `Editable`, `AssetReference`, `EntityReference`
   (mutually exclusive with each other), `Optional`. `PrimitiveKind` v1
   ships exactly what the v1 type set exercises; more kinds/flag bits are
   additive, changed semantics are not.
7. **Per-descriptor `SchemaVersion`, decoupled from file formats.** It
   starts at 1 and bumps on described-type shape change. It is a separate
   namespace from [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
   artifact-format constants (C++ shape vs file bytes); the catalog's
   per-asset schema numbers ([ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md))
   remain format-scoped.

## Consequences

### Positive

- One stable, machine-readable inventory of the engine's data surface,
  addressable by ID across processes — the prerequisite the editor protocol
  was missing, delivered before the protocol is designed.
- Descriptor drift from the C++ types becomes a build failure instead of a
  convention.
- Zero new dependencies; module ownership of types preserved (Core owns the
  metalanguage only).
- Existing formats, parsers, and goldens untouched.

### Negative / Trade-offs

- Every shape change to a described type now carries a same-PR obligation:
  update the table, bump the descriptor version.
- Byte offsets confine described types to standard-layout structs; a
  non-standard-layout or polymorphic type needs a future binding decision.
- Name-derived IDs change on rename (renames surface via ID stability-pin
  tests and review; names are the source of truth) and can in principle
  collide (tested per build; rename is the remedy).
- The duplicated fog/bloom shapes (`world::CameraFog` vs `DecodedCameraFog`)
  are not unified by this decision; the mirrors stay convention-synced.
- A descriptive-only core still leaves the accessor layer as required
  future work before any tool can read/write through descriptors.

## Alternatives Considered

- **Per-component protocol verbs now, schema later** — rejected: locks the
  unversioned verb-per-feature surface into a wire protocol (Spec 0048
  motivation).
- **Reflection library or codegen** (RTTR, Boost.Describe, `visit_struct`,
  external compiler) — rejected: new dependency against the Golden Rule;
  far more machinery than describing eleven types needs.
- **X-macro member lists** — rejected for v1: a macro dialect the repo does
  not have; constexpr tables + drift tests first, macros as a possible
  later ergonomics step with their own review.
- **Centrally allocated ordinal IDs** — rejected: requires an allocation
  authority and name↔id tables anyway; name-derived hashes are
  self-describing and collision-tested.
- **A new top-level Schema module** — rejected for v1: Core-sized,
  dependency-free cluster; revisit condition: if the vocabulary grows
  access/serialization/migration machinery or a second strong consumer,
  promote `atlantis::schema` to its own module via a superseding ADR.
- **One central engine-wide table in Core** — rejected: recreates
  cross-module edit contention and a dependency magnet; modules own their
  tables, consumers compose spans.
