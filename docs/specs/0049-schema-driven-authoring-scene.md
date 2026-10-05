# Spec: Schema-driven Authoring Scene

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-05
- **Related Plan(s):** none yet — Plan 0049 drafting is authorized by the
  Approval below. **Implementation still awaits its own, separate Joint Human
  Review** of Spec + Plan together, per AGENTS.md's own workflow.
- **Approval:** slmao, 2026-10-05 (review of this Spec's own branch PR,
  [PR #202](https://github.com/slmao/Atlantis/pull/202)) — authorizes drafting Plan 0049; Implementation itself
  still awaits its own, separate Joint Human Review of Spec + Plan together.
  The direction and boundaries (semantic schema, not syntax; no grammar or
  artifact-byte change; the layering; future consumers named, not built) were
  fixed by the maintainer before drafting (2026-10-05, chat) and are recorded
  under Goals / Non-Goals. The same review ruled all seven open questions,
  each as its recommendation. See Risks & Open Questions below.
- **Related ADR(s):**
  [ADR-0100](../adr/0100-scene-semantic-schema-layering-model-and-versioning.md)
  (`Accepted` 2026-10-05, alongside this Spec's own Approval). It records the layering, the
  authoring-scene semantic model's home and identity rules, the semantic
  version, and the contract-plus-conformance relationship to the serializers.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Build the scene's **semantic schema** on top of Spec 0048's vocabulary. The
semantic schema is one explicit, enumerable, versioned statement of what an
authored scene *means*. It covers:

- the document and its nodes;
- node identity;
- the components a node may carry and their fields;
- the references between nodes and to assets;
- the constraints a valid scene satisfies.

The existing scene source grammar and scene artifact become **serializers
below that layer**. They are held to it by conformance tests. Their syntax
and bytes do not change. Prefab, Editor, JSON5, binary, Editor Transaction and
Agent Patch are named as future consumers of the same schema and are not
designed here. The purpose is to end the current state, in which scene
semantics exist only in parser/encoder code and in the hand-bumped
`atlantis_scene_source_version: 7` / `kSceneArtifactSchemaVersion = 7`
constants.

## Motivation / Problem Statement

Current state read at `origin/main` `e809531`.

### Scene meaning lives in codec code

No artifact in the repository states what a scene is. The meaning is spread
across:

- **Grammar shape as meaning.** "A node carries at most one of camera, mesh or
  light" is never stated. It falls out of a token-count dispatch
  (`scene_source.cpp:27-36`, 14/15 tokens = camera, 16/17 = light). "Material
  requires mesh" is "a structural, not runtime, property of this grammar"
  (`scene_source.h:20-24`). The artifact decoder re-derives it as a separate
  error, `MaterialWithoutRenderable` (`errors.h:152-159`).
- **One value domain, written three times with three error types.** Light
  colour in [0, 1]:
  - the parser: `InvalidComponentGroup` (`scene_source.cpp:388-390`);
  - the artifact decoder: `NonFiniteValue` (`scene_artifact.cpp:286-290`);
  - a field comment on World's own copy (`world/light.h`, `Light::color`).

  Exposure, fog and bloom domains are checked in the cooker
  (`cook_scene.cpp:207-214`) and again in the decoder (`scene_artifact.cpp:241`).
  Light intensity and range are checked in the parser (`scene_source.cpp:403-414`)
  and the decoder, but only for finiteness in the cooker (`cook_scene.cpp:225-228`).
- **Scene-level rules inside the parser.** At most one directional light and
  `kMaxPointLightsPerScene` (64) point lights is a whole-document rule
  enforced as a parse error (`scene_source.cpp:459-466`, `TooManyLights`).
- **Identity mixed with syntax.** Parents and the active camera are
  referenced by `node_id`, a file-local number (`scene_source.h:32-44`). The
  persistent identity is `guid=` (EntityGuid, ADR-0097 D5), and the artifact
  drops `node_id` entirely in favour of array indices.
- **Versions that only count byte layouts.** Scene source v7 and artifact 7
  record seven format generations (v4 exposure, v5 fog, v6 bloom, v7 GUIDs;
  `scene_source.cpp:12-19`, `scene_artifact.h:18-36`). Each generation was a
  semantic change *and* a syntax change at once. Nothing tells the two
  apart, so a future serializer has no way to say which meaning it encodes.

### Spec 0048 left this to the scene-authoring spec

Spec 0048 described World's runtime components and deferred the scene-source
`Decoded*` DTOs "when the scene-authoring tool spec exists" (Spec 0048 ruling
Q1). This is that spec. It must decide what the authoring layer describes;
see ruling Q1 below and *Relation to Spec 0048* under Proposed Design.

### Why now

Every named future consumer needs the same layer: Prefab, an editor (via the
Tool/Editor Connection Protocol, Candidate 2), JSON5 or binary serializers,
editor transactions, and agent patches. Each would otherwise reverse-engineer
scene meaning from the v7 parser, or grow its own. Building the semantic
layer first, against the formats that exist today, fixes the contract while
there is exactly one serializer pair to hold to it.

## Goals

These are maintainer-fixed boundaries and are not to be relaxed in review:

- **A Scene semantic schema, not a Scene syntax.** This step defines what a
  scene means; it defines no new way to write one.
- **The layering below**, with Schema above its consumers and Serializer below
  Scene:

  ```
            Schema
               │
      ┌────────┼────────┐
      ▼        ▼        ▼
    Scene    Prefab   Editor
      │
      ▼
  Serializer
  ```

  *Schema* is Spec 0048's Core vocabulary plus the module-owned tables built
  with it. *Scene* is this spec's semantic layer. *Serializer* is today's
  scene source codec and scene artifact codec, and any future one. Prefab and
  Editor are peer consumers of Schema, named here and not built.
- **End "semantics only in codecs and hand-bumped vN constants".** Every
  semantic fact listed under Motivation has exactly one normative statement
  in the semantic schema. The codecs are checked against it, and a semantic
  change cannot land without a mechanically required semantic-version bump.

Goals of this spec within those boundaries:

- An **authoring-scene semantic model**: document, node, components,
  references and constraints, in Atlantis Asset System, the module that owns
  the scene asset type (ADR-0052).
- **Node identity and node references by EntityGuid.** `node_id` and array
  indices are serializer-level syntax.
- A **scene semantic version**, decoupled from the source and artifact format
  versions in the way Spec 0048 R8 decoupled descriptor versions from file
  versions, with each serializer declaring the semantic version it encodes.
- **Conformance tests** that hold the existing source parser/serializer, the
  cooker and the artifact codec to the semantic schema.
- The **address form** future consumers will use for a property (node,
  component, field) and the identity rules Prefab needs, fixed now so neither
  needs a breaking change later (ruling Q4).

## Non-Goals

These are maintainer-fixed:

- **No scene source grammar change.** No JSON5, no new token, no
  reordering. `atlantis_scene_source_version` stays 7.
- **No scene artifact byte change.** `kSceneArtifactSchemaVersion` stays 7,
  and every committed and test-generated artifact is byte-identical. The
  scene metadata sidecar and the asset catalog are unchanged too.
- **No design, implementation or scaffold** of JSON5, a binary serializer,
  Editor Transaction, Agent Patch, Prefab or Editor. They are named as future
  consumers only. That means no placeholder types, no reserved enum values,
  no empty interfaces.

Also out of scope:

- No new semantics. v1 of the semantic schema states exactly what source v7
  and artifact 7 accept today. No constraint is added or relaxed, for example
  no `nearZ < farZ`. Tightening or loosening is a later semantic-version
  change.
- No change to World, `ValidatedSceneData`, `instantiateScene()`, Runtime, the
  glTF importer or its overlay format.
- No schema-driven parser or encoder (ruling Q3).
- No value accessor or mutation API through descriptors. ADR-0099 D5's
  descriptive-only scope stands.
- No description of the cooked runtime projection (`ValidatedSceneData`,
  `DecodedSceneArtifact`) or of the `Decoded*` codec DTOs (see Relation to
  Spec 0048).

## Requirements

### Functional

- **R1 — Semantic component types.** Asset System provides plain
  standard-layout value types for the authoring components: transform,
  camera, camera fog, camera bloom, renderable, light, and light kind. Each is
  described by Spec 0048 descriptors in Asset System's existing
  `assetSystemSchema()` (ADR-0099 D2's one-function rule), with 0048's sync
  tests.
  - Field names and shapes follow World's counterparts where the meaning is
    the same (`localPosition`, `color` as `Vec3Float32`, and so on), so a later
    World mapping is name-for-name.
  - The renderable's references are **AssetGuids** (`AssetReference`), the
    authoring identity (ADR-0097), not `AssetId` keys. The material reference
    is `Optional`.
  - Namespace `atlantis::asset_system::scene`; the seven types are
    `Transform`, `Camera`, `CameraFog`, `CameraBloom`, `Renderable`, `Light`
    and `LightKind` (ruling Q6).
- **R2 — Authoring scene document.** Asset System provides the in-memory
  semantic instance: an authoring scene, an **ordered** sequence of nodes plus
  an optional active-camera reference. Each node carries:
  - its EntityGuid;
  - an optional parent reference;
  - a transform;
  - at most one of camera, renderable and light.

  Node order is semantic: it is today's instantiation order and the
  artifact's index order. Parent and active-camera references are
  **EntityGuid-valued**. It is public in Asset System as plain values (ruling
  Q5); it is not an
  editing API.
- **R3 — Scene semantic schema (the metamodel).** Asset System provides one
  immutable, enumerable scene schema. It states, as data:
  - the semantic version (R6);
  - the node identity rule: an EntityGuid, non-nil and unique per document;
  - the component set, each component by its `TypeId`, with per-node
    cardinality 0..1 and the exclusivity group {camera, renderable, light};
  - the relations: parent (optional, target exists, acyclic) and active
    camera (optional, target exists and carries a camera);
  - every field's value domain;
  - the document-level constraints: non-empty; at most one directional light;
    at most `kMaxPointLightsPerScene` point lights.

  Domains and constraints are scene-local data in Asset System; no Core
  vocabulary is added (ruling Q7). The exact C++ shape is the Plan's. This requirement fixes the information,
  in the manner of Spec 0048 R2.
- **R4 — The v1 constraint inventory is today's, exactly.** The value domains
  and constraints in R3 are those the v7 parser, cooker and artifact decoder
  enforce on `origin/main`, each traced to its enforcing site by the Plan:
  - transform fields finite;
  - camera fov/near/far finite;
  - exposure in `[kExposureCompensationEvMin, kExposureCompensationEvMax]`;
  - fog and bloom per `isValidCameraFog`/`isValidCameraBloom`;
  - light colour finite in [0, 1] and intensity finite ≥ 0;
  - point range finite > 0, directional range absent (0);
  - mesh and material references non-nil;
  - the R3 structure rules.

  A discrepancy the Plan finds between enforcement sites is reported to
  review, not silently resolved.
- **R5 — Serializers declare and conform.** The scene source codec and the
  scene artifact codec each declare, beside their unchanged format version,
  the scene semantic version they encode (v1 for both).
  - **Source ⇄ semantic** is lossless at the semantic level. `node_id` is
    syntax: a parsed source maps to an authoring scene and back to an equal
    one, though not necessarily to identical text.
  - **Semantic → artifact** is a declared **projection**: asset GUIDs become
    keys (`assetKey()`), node references become indices, and `node_id` is not
    carried. The projection is checked against `cookScene()` →
    `decodeScene()`.
  - Mapping functions between the parsed source and the authoring scene
    are public, pure functions in Asset System (ruling Q5). The existing
    codecs are not rewritten (ruling Q3).
- **R6 — Scene semantic version** (ruling Q2).
  - It is one unsigned integer, starting at **1** for exactly today's
    semantics.
  - It is accompanied by a **semantic fingerprint**: a deterministic
    FNV-1a-64 over a canonical serialization of the scene schema (R3) and of
    every descriptor it references (ids, kinds, flags, per-type
    `SchemaVersion`s, domains, constraints).
  - A test pins the fingerprint for the current semantic version. Any
    semantic change alters the fingerprint and fails until the version is
    bumped and the new pin recorded in the same PR.
  - It is decoupled from ADR-0045 format versions:
    - a format-only change (e.g. an artifact layout change) bumps the format
      version and not the semantic version;
    - a semantic change bumps the semantic version, and every serializer must
      then either declare the new version (changing its format as ADR-0045
      requires) or fail the conformance build;
    - the catalog's `artifactSchema`/`sourceSchema` stay format-scoped
      (ADR-0098).
- **R7 — Property address.** The canonical semantic address of a property
  inside a scene document is the triple (node EntityGuid, component `TypeId`,
  `FieldId`). It is defined as a value type with equality and canonical text.
  No consumer is implemented. This is the form that Editor transactions,
  agent patches and prefab overrides will address (ruling Q4).
- **R8 — Conformance tests are schema-driven** (ruling Q3). The tests enumerate the scene
  schema and require coverage per element:
  - every semantic field survives source and artifact round trips under a
    perturbation case;
  - every constraint has a named negative case rejected by parse/cook and,
    where the artifact can express the violation, by decode.

  An element added to the schema without coverage fails the suite.

### Non-functional

- **Behavior:** byte-identical. No source, artifact, metadata, catalog,
  golden, shader or Runtime-visible change. Every existing scene, overlay and
  fixture parses and cooks as before.
- **Performance:** the schema is static immutable data. The mapping functions
  run only where called (tests in v1). There is no change to the cook, load or
  frame path.
- **Dependencies:** none added. Asset System still depends on Core only; the
  include ban on World (ADR-0053) and the boundary scans stay green.
- **Portability:** pure C++20, Windows and Android. The standard-layout and
  well-formedness checks compile on both, as in Spec 0048.
- **Thread safety:** the schema is immutable static data, safe for concurrent
  reads. Value types and mapping functions are pure. One line at each public
  API.

## Proposed Design

### Layers, concretely

| Layer | What it is in the tree | Changes here |
|---|---|---|
| **Schema** | `atlantis::schema` (Core, ADR-0099) plus module tables | Asset System's table gains the R1 component descriptors |
| **Scene** (semantic) | New in Asset System: R1 value types, R2 document, R3 scene schema, R6 version and fingerprint, R7 address | New |
| **Serializer** | `scene_source.{h,cpp}`, `scene_artifact.{h,cpp}`, `cook_scene`, `decode_scene` | Each gains one "encodes scene semantic vN" constant. Mapping functions sit beside them. No grammar or byte change |
| Prefab, Editor, JSON5, binary, Transaction, Agent Patch | — | Named only |

Dependency direction: the Serializer layer may know the Scene layer. The
Scene layer never names a serializer's types: it does not know `node_id`,
token prefixes or record offsets. Mapping lives on the serializer side, so a
second serializer adds its own mapping without touching Scene.

### The authoring model, by example

The semantic node mirrors what the v7 grammar can express, minus syntax:

```
SceneNode { guid: EntityGuid; parent: EntityGuid?; transform: Transform;
            one of { camera: Camera, renderable: Renderable, light: Light }? }
Renderable { mesh: AssetGuid; material: AssetGuid? }   -- "material without mesh" is unrepresentable
AuthoringScene { nodes: SceneNode[] (ordered); activeCamera: EntityGuid? }
```

The constraint that today is "a token arrangement the grammar does not
have" becomes a shape the semantic model cannot hold. A constraint that
cannot be made structural (acyclic parents, the light counts, value domains)
is stated as data in the R3 schema.

### Relation to Spec 0048 (its ruling Q1)

- Spec 0048 deferred describing the `Decoded*` DTOs to this spec. This spec
  does **not** describe them (ruling Q1).
- They are codec DTOs that sit in the Serializer layer:
  - flat `colorR/G/B` fields;
  - shared by the cooked runtime projection;
  - `DecodedRenderable` carries `AssetId` keys, not authoring GUIDs.
- Spec 0048 identity is name-derived, so describing them would freeze codec
  names such as `DecodedCamera` into the persistent `TypeId`s every future
  consumer addresses.
- The semantic component types (R1) take their place in the schema.
- The `Decoded*` types remain as they are, mapped to and from by the
  serializer layer. Retiring them, by codecs operating on the semantic types
  directly, is the follow-up Q3 names.

### Versioning, concretely

```
scene semantic v1  ── encoded by ──►  scene source v7     (declared constant)
                   ── projected to ─►  scene artifact 7    (declared constant)
fingerprint(v1) = <pinned literal>
```

A future change to fog semantics: semantic v2, a new fingerprint pin, both
codecs bump their format version and declare v2, in one PR. A future artifact
repacking with the same meaning: artifact 8 declaring semantic v1; the
semantic version is untouched.

## Architectural Impact

Yes. This adds a new public semantic layer in Atlantis Asset System and fixes
the dependency direction between it and the scene serializers, with a new
versioning rule beside ADR-0045's. Recorded in
[ADR-0100](../adr/0100-scene-semantic-schema-layering-model-and-versioning.md),
drafted alongside:

- the layering and dependency direction;
- the semantic model's home and its independence from the codec DTOs;
- EntityGuid node identity and references;
- the semantic version and fingerprint, decoupled from format versions;
- contract plus conformance instead of codec rewrite;
- the reserved property address.

No new module, dependency, threading or ownership change. ADR-0099 is
extended in use (new descriptors in an existing table), not amended. No new
Core vocabulary is added (ruling Q7).

## Alternatives Considered

1. **Describe the existing `Decoded*` / `ValidatedSceneData` as the semantic
   layer** (Q1, option A). This adds no new types. It freezes codec naming and
   flat fields into persistent IDs. The documents (vectors, a private-member
   class) are outside Spec 0048's standard-layout vocabulary. The cooked side
   holds keys, not authoring identity. Rejected by ruling Q1.
2. **Make World's components the scene semantics.** Asset System may not
   depend on World (ADR-0053), and the Serializer must sit under Scene.
   Authoring references (GUIDs) differ from runtime ones (keys). Rejected.
3. **Rewrite the parser and encoder as schema-driven now** (Q3, option A). It
   proves the schema by construction, but it is a large refactor of two
   codecs at once, against a fixed byte-identity requirement, with no second
   serializer yet to justify the generality. Deferred to the first new
   serializer spec by ruling Q3.
4. **Layout-free semantic descriptors** (no C++ types). This needs a Spec
   0048 vocabulary change, because descriptors carry byte offsets, and it loses
   the sync tests. Rejected.
5. **Keep versioning implicit in the format constants.** This is today's
   state: a semantic change and a syntax change are indistinguishable, and a
   second serializer cannot say what it encodes. Rejected.
6. **Generic containers and references in Core vocabulary** (array kinds,
   node-reference kinds) so a scene is "just a described type". This is
   speculative generality for one consumer, and it changes ADR-0099's
   vocabulary. The scene metamodel (R3) states document structure
   specifically instead. Revisit when Prefab or a second document type
   exists.

## Testing & Verification Plan

All GPU-independent Catch2 tests. No GPU path is touched, so image regression
and Validation Layers are N/A as new gates. The existing GPU suites run as
the byte-behaviour guard. Mapping to requirements:

- **R1:** Spec 0048-pattern sync tests for every semantic component type.
  World-name alignment is checked against `worldSchema()` field names, in
  `tests/world`, which links both modules.
- **R2/R3:** scene schema well-formedness. Every component `TypeId`, domain
  `FieldId` and relation target resolves; the exclusivity group is
  consistent.
- **R4:** one negative case per constraint, asserting the error today's code
  returns. Traceability of each constraint to its enforcing site is recorded
  in the Plan.
- **R5:**
  - round trip parse → authoring → serialize → parse → equal, over every
    committed scene source, the Bistro overlay and the test fixtures;
  - authoring → cook → decode equals the declared projection;
  - each codec's declared semantic version equals the scene schema's.
- **R6:** fingerprint pin and version. A demonstration (not committed) shows
  a one-field semantic change failing until the version is bumped.
- **R7:** address equality, canonical text round trip, and rejection of nil
  components.
- **R8:** coverage enumerators. A schema element without a perturbation or
  negative case fails, demonstrated during development.
- **Byte guard:** the full Debug + Release suites. No change under `assets/`,
  `tests/image_regression/goldens/` or `shaders/`. Every cooked scene artifact
  is byte-identical to `origin/main`'s, compared over the build tree. Android
  `assembleDebug` succeeds.

## Risks & Open Questions

Risks:

- **A third shape for the same meaning.** For a camera there would be
  `world::Camera`, `DecodedCamera` and the semantic camera. This is mitigated
  by the R1 name-alignment test and by R5's mapping round trips, and it ends
  when Q3's follow-up retires `Decoded*`. Until then, the three are kept in
  agreement by tests, not by convention.
- **Hidden semantics found while tracing R4.** For example, the decoder
  requires directional `range == 0` while the source has no token for it.
  Such findings are listed to review and become stated v1 semantics, never
  silent fixes.
- **Over-reservation for Prefab.** Q4 is limited to identity and address
  rules. Anything more would be scaffolding the maintainer excluded.

Open questions — all seven ruled by Human Review (slmao, 2026-10-05, review
of [PR #202](https://github.com/slmao/Atlantis/pull/202)), each as its recommendation:

- **Q1 — What the semantic layer describes.** **Ruled (2026-10-05): (B).**
  - An independent authoring-scene semantic model (R1–R3).
  - `Decoded*` and `ValidatedSceneData` stay undescribed codec-layer types
    (Relation to Spec 0048).
  - This closes Spec 0048 ruling Q1's deferral.
  - Rejected: (A), describing the existing DTOs, because persistent IDs come
    from names and the codec shapes are serializer artefacts.
- **Q2 — Semantic version vs the v7 constants.** **Ruled (2026-10-05): (A)**
  (R6).
  - One aggregate scene semantic version, starting at v1, with a fingerprint
    pinned by a test.
  - The two serializers each declare the semantic version they encode (R5).
  - Format versions stay under ADR-0045.
  - Rejected: (B) per-descriptor versions only; (C) reusing the source format
    version.
- **Q3 — Codec relationship.** **Ruled (2026-10-05): (B)** (R5, R8).
  - The schema is the contract, held by schema-driven conformance tests.
  - The codecs are not rewritten in this spec.
  - Moving the codecs onto the semantic types and retiring `Decoded*` belongs
    to the spec that introduces the next serializer.
- **Q4 — What Prefab needs reserved.** **Ruled (2026-10-05): the five rules,
  rules only, nothing built:**
  1. node identity and every intra-document node reference are EntityGuids,
     never indices or `node_id`s (R2);
  2. a cross-document entity reference reuses ADR-0097 D6's `EntityRef`;
  3. per node, components are keyed by component `TypeId`, at most one per
     type (R3);
  4. a property is addressed by the (EntityGuid, `TypeId`, `FieldId`) triple
     (R7);
  5. document-level fields (active camera) are separate from the node model
     (R2, R3).
  - Not reserved, and left to the Prefab spec: instance GUID derivation,
    override storage, nesting, variants.
- **Q5 — Public surface of the authoring document and the mappings.**
  **Ruled (2026-10-05): (A)** (R2, R5). Public in Asset System, as plain value
  types and pure functions.
- **Q6 — Namespace and type names.** **Ruled (2026-10-05):**
  - Namespace `atlantis::asset_system::scene`.
  - The seven described types are `Transform`, `Camera`, `CameraFog`,
    `CameraBloom`, `Renderable`, `Light` and `LightKind`. Their descriptor
    names are `asset_system::scene::<Type>`.
  - Their field names align with World's counterparts (R1).
  - The authoring document and node types (R2) are not Spec 0048-described,
    so their names never become `TypeId`s; the Plan names them.
- **Q7 — Home of value domains and constraint vocabulary.** **Ruled
  (2026-10-05): (A)** (R3).
  - Scene-local data in Asset System. Core's `atlantis::schema` vocabulary is
    unchanged.
  - Promoting it to Core needs a second consumer and its own ADR.

## Out of Scope / Future Work

These are named consumers of the semantic schema. None is designed here:

- **Prefab:** document model, instance GUID derivation, overrides, nesting.
- **Editor** and its transactions; **Agent Patch.** Both address properties by
  R7.
- **JSON5 and binary scene serializers.** Each declares the semantic version
  it encodes (R5). Retiring `Decoded*` follows Q3.

Further possibilities:

- Semantic tightening, such as `nearZ < farZ` or bounded fov, as semantic v2.
- Promoting domain/constraint vocabulary to Core (Q7).
- A World ⇄ Scene component mapping driven by the aligned names (R1).
