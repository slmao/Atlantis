# Plan: Schema-driven Authoring Scene

- **Spec:** [Spec 0049: Schema-driven Authoring Scene](../specs/0049-schema-driven-authoring-scene.md)
  (`Approved`, 2026-10-05, [PR #202](https://github.com/slmao/Atlantis/pull/202);
  rulings Q1–Q7 binding) —
  [ADR-0100](../adr/0100-scene-semantic-schema-layering-model-and-versioning.md)
  (`Accepted`); builds on Spec 0048 / [ADR-0099](../adr/0099-engine-schema-core-and-descriptor-vocabulary.md)
  and Plan 0048's binding rulings J1–J7.
- **Status:** In Review
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending. Implementation is not authorized until a
  reviewer has read this Plan and Spec 0049 together, ruled J1–J10 below, and
  explicitly authorized it.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0049 R1–R8 within its maintainer-fixed boundaries:

- the seven semantic component types and their descriptors;
- the authoring document and its public mappings;
- the scene semantic schema, holding v1's constraints exactly as enforced
  today;
- the semantic version and its pinned fingerprint;
- the property address;
- schema-driven conformance tests that hold the unchanged source and artifact
  codecs to the schema.

No scene grammar, artifact byte, golden, Runtime, World or importer change.

## Pre-drafting reading (cited, not restated)

Read at `origin/main` `09b06eb` (PR #202 merged).

1. **Codec shapes.**
   - `ParsedSceneNode` (`scene_source.h:32-44`) carries `nodeId`, `EntityGuid`,
     an optional parent `nodeId`, `DecodedTransform`, an optional
     `DecodedCamera`, optional mesh/material `AssetGuid`s and an optional
     `DecodedLight`.
   - `ValidatedSceneNode` carries the same `Decoded*` components, with
     `DecodedRenderable` holding `AssetId` keys (`validated_scene_data.h:14-19`).
   - `Decoded*` use flat `colorR/G/B` and `positionX/...` fields
     (`scene_types.h`).
2. **Three source-text producers, not two.**
   - `serializeSceneSource()` (`scene_source.cpp`, end of file) prints floats
     with `std::to_string` (fixed six decimals). It writes the fog group only
     when `density != 0` and the bloom group only when `strength != 0`.
   - The glTF importer writes imported lines with its own shortest-round-trip
     formatter and a hard-coded `atlantis_scene_source_version: 7` literal
     (`scene_import.cpp:102-111`).
   - The importer emits the Bistro overlay's lines through
     `serializeSceneSource()` (`scene_import.cpp:159`). Changing that
     serializer therefore changes the Bistro scene's bytes.
3. **The committed corpus exercises both of the serializer's lossy paths.**
   - `fog_zero_neutrality.scene.txt` authors `fog=0 2.0 0.5 0.8 fog_color=0.7
     0.7 0.8`, which `serializeSceneSource()` drops entirely.
   - Several scenes and the overlay carry 7-significant-digit values
     (`1.5707963`, `0.6435011`) that six decimals cannot reproduce exactly.
4. **Cooked corpus.** The build cooks every declared scene to
   `build/assets/scenes/*.ascene` plus a sidecar, listed in the assembled
   catalog. Tests already receive the catalog path
   (`ATLANTIS_ASSET_CATALOG_PATH`, `tests/asset_system/CMakeLists.txt`).
5. **Spec 0048 conventions this Plan extends.**
   - `assetSystemSchema()` lists 6 types (`asset_system_schema.cpp`). Its
     listing test pins those 6 names (`asset_system_schema_tests.cpp`).
   - The cross-module uniqueness test pins `combined.size() == 13`
     (`tests/world/schema_uniqueness_tests.cpp`).
   - Sync-test pattern: structured-binding arity, the test-only
     `ATLANTIS_SCHEMA_PROBE` macro (Plan 0048 J4), and the `/w14062`
     enumerator canaries.
   - `Editable` means "on the live World editing surface" (Plan 0048 J3).

## R4 — v1 constraint inventory traced to today's enforcement

**P** = parser (`scene_source.cpp`), **C** = cooker (`cook_scene.cpp`),
**D** = artifact decoder (`scene_artifact.cpp`). "Structural" means the
grammar, or the semantic model, cannot express the violation. Every row
becomes a scene-schema entry (P6) and a conformance case (M5). Findings are
marked **F*n*** and listed after the table; none is fixed by this Plan
(Spec R4).

| # | v1 semantic rule | P | C | D |
|---|---|---|---|---|
| 1 | Document non-empty | — (by design, `scene_source.h` parse comment) | `EmptyScene` :125-126 | `EmptyScene` :178 |
| 2 | Node EntityGuid non-nil | accepts nil :198-206 | `NilEntityGuid` :145 | `NilEntityGuid` :310 |
| 3 | Node EntityGuid unique | — | `DuplicateEntityGuid` :150 | `DuplicateEntityGuid` :321 |
| 4 | Parent, if any, exists | node_id syntax | `UndeclaredParentReference` :169 | `OutOfRangeParentIndex` :301 |
| 5 | Parents acyclic | — | `ParentCycle` :174 | `CyclicParent` :346 |
| 6 | Active camera, if any, exists | node_id syntax | `UndeclaredActiveCameraReference` :180 | `OutOfRangeActiveCameraIndex` :351 |
| 7 | Active camera carries a camera | — | `ActiveCameraMissingCamera` :253 | `ActiveCameraMissingCamera` :354 |
| 8 | ≤ 1 of {camera, renderable, light} per node | structural (token-count dispatch :246-249, :303/:323/:346) | inherits P | **not enforced (F1)** |
| 9 | Fog/bloom only on a camera | `InvalidComponentGroup` :423, :437 | inherits P | structural (camera slot) |
| 10 | Material requires mesh | structural | inherits P | `MaterialWithoutRenderable` :265 |
| 11 | Transform fields finite | accepts nan/inf | `NonFiniteValue` :194-200 | `NonFiniteValue` :212-216 |
| 12 | Camera fov/near/far finite (no range) | accepts nan/inf | `NonFiniteValue` :207-209 | `NonFiniteValue` :240 |
| 13 | Exposure finite, in [−16, 16] (`scene_types.h:90-91`) | — | `NonFiniteValue` :210-211 | `NonFiniteValue` :241 |
| 14 | Fog per `isValidCameraFog`: colour ∈ [0, 65504], density ≥ 0, height finite, falloff ≥ 0, maxOpacity ∈ [0, 1] | number syntax only | `NonFiniteValue` :212 | `NonFiniteValue` :242 |
| 15 | Bloom per `isValidCameraBloom`: strength ∈ [0, 1], threshold ≥ 0 | number syntax only | `NonFiniteValue` :213 | `NonFiniteValue` :242 |
| 16 | Light kind ∈ {Directional, Point} | `InvalidComponentGroup` | inherits P | `NonFiniteValue` :285 (F6) |
| 17 | Light colour finite, ∈ [0, 1] | `InvalidComponentGroup` :388 | finite only :225 (F6) | `NonFiniteValue` :288 |
| 18 | Light intensity finite, ≥ 0 | `InvalidComponentGroup` :403 | finite only :225 | `NonFiniteValue` :289 |
| 19 | Point range finite, > 0 | `InvalidComponentGroup` :413 | finite only :227 | `NonFiniteValue` :289 |
| 20 | Directional range == 0 | structural (no token; default 0) :373 | finite only | `NonFiniteValue` :290 (F2) |
| 21 | ≤ 1 directional light | `TooManyLights` :465-466 | inherits P | `TooManyLights` :340 |
| 22 | ≤ `kMaxPointLightsPerScene` (64) point lights | `TooManyLights` :465-466 | inherits P | `TooManyLights` :340 |
| 23 | Mesh/material reference non-nil | `MalformedGuid` :310, :320 | inherits P | key only; **0 accepted (F3)** |
| — | node_count ≤ 65536 | — | **not enforced (F4)** | `NodeCountOutOfRange` :182 |

Findings, each left unfixed:

- **F1 — The decoder does not enforce component exclusivity.** An artifact
  node may set `has_camera`, `has_renderable` and `has_light` together.
  `decodeSceneArtifact()` accepts it, and `instantiateScene()` would attach all
  three. The source cannot author it. → J1.
- **F2 — Directional `range == 0` is explicit only in the decoder.** The
  parser makes it structural, and the cooker checks finiteness only. The
  outcomes agree. The semantic schema states it as a kind-conditional
  constraint (row 20, P6). No action; recorded for review.
- **F3 — The decoder accepts asset key 0** for mesh/material, although
  `AssetId` reserves 0 as "none" (`asset_id.h`). The semantic rule (non-nil
  GUID) is enforced only on the source side. The artifact cannot express a
  GUID. → J1.
- **F4 — The node-count bound exists only in the decoder.** `cookScene()` can
  write an artifact that `decodeScene()` rejects. → J2.
- **F5 — `serializeSceneSource()` is lossy** (reading items 2–3): fixed six
  decimals, and fog/bloom fields dropped while the group is off. It is a
  production path (the Bistro overlay), so it cannot change under the
  byte-identity requirement. → J3, J4.
- **F6 — One rule, different error kinds.** Light rules report
  `InvalidComponentGroup` (P) but `NonFiniteValue` (D, including an invalid
  kind). The cooker re-checks light finiteness only, relying on the parser.
  The outcomes agree. Conformance asserts today's error per site. No change.
- **F7 — A third source producer.** The importer's own writer and its
  version literal are outside the declared serializer pair (R5). → J9.

## Plan-stage decisions

These are details the Spec leaves to the Plan. None changes a Spec
requirement or ADR decision. Readings that could go two ways are also listed
under the Joint Review decisions.

**P1 — Files** (flat, like every other Asset System header, with a `scene_`
prefix; namespace `atlantis::asset_system::scene` per ruling Q6):
- `scene_semantic_types.h`: the seven types (R1).
- `authoring_scene.h`: the document and node (R2).
- `authoring_scene_mapping.{h,cpp}`: `ParsedSceneSource` ⇄ `AuthoringScene`
  (R5, Q5).
- `scene_semantic_schema.{h,cpp}`: the metamodel, the version and the
  fingerprint (R3, R6).
- `scene_property_address.{h,cpp}`: the property address (R7).
- The seven descriptors are added to the existing `asset_system_schema.cpp`
  (ADR-0099 D2).

**P2 — Semantic component types (R1).**
- Plain standard-layout aggregates with default member initializers equal to
  the `Decoded*` defaults, and defaulted `==`.
- Vectors are `std::array<float, 3>`, which is standard-layout and maps to
  `Vec3Float32`. No new vector type is added: Q6 fixed seven names.
- Fields, aligned by name with World:
  - `Transform{localPosition, localEulerAnglesRadians, localScale}`;
  - `CameraFog{color, density, height, heightFalloff, maxOpacity}`;
  - `CameraBloom{strength, threshold}`;
  - `Camera{fovYRadians, nearZ, farZ, exposureCompensationEv, fog, bloom}`;
  - `LightKind{Directional, Point}`;
  - `Light{kind, color, intensity, range}`;
  - `Renderable{AssetGuid meshAsset; std::optional<AssetGuid> materialAsset}`.
- Descriptor flags: every field is `Serializable` (J6). Renderable's fields
  are `AssetReference`, and `materialAsset` is also `Optional`.
- Every descriptor has `SchemaVersion` 1.
- `assetSystemSchema()` appends the seven types after `EntityRef`, in Q6
  order.

**P3 — Authoring document (R2, Q4 rules, Q5).**
- `AuthoringNode{EntityGuid guid; std::optional<EntityGuid> parent;
  Transform transform; std::optional<Camera> camera;
  std::optional<Renderable> renderable; std::optional<Light> light;}`.
- `AuthoringScene{std::vector<AuthoringNode> nodes; std::optional<EntityGuid>
  activeCamera;}`. Document-level fields sit on the scene, never on a node
  (Q4 rule 5).
- Both are plain values with defaulted `==`.
- Exclusivity is a schema constraint, not a type-level variant. That keeps the
  shape aligned with the per-`TypeId` component rule (Q4 rule 3). The
  document type names are the Plan's (ruling Q6).

**P4 — Mappings (R5, Q5).**
- `toAuthoringScene(const ParsedSceneSource&) → Result<AuthoringScene,
  SceneMappingError>`.
  - It resolves `node_id`s to EntityGuids and converts `Decoded*` component by
    component.
  - It fails only where the references cannot be resolved:
    `DuplicateNodeId`, `UndeclaredParentReference`,
    `UndeclaredActiveCameraReference`, `AmbiguousEntityGuid`.
- `toParsedSceneSource(const AuthoringScene&) → Result<ParsedSceneSource,
  SceneMappingError>`.
  - It assigns `node_id = index + 1`.
  - It fails on `DanglingParentReference`, `DanglingActiveCameraReference`
    and `AmbiguousEntityGuid`.
- Neither function validates other constraints; that stays the cooker's job
  (Q3).
- Both are pure and noexcept-free. They are the only code that names both
  `Decoded*` and `scene::` types (ADR-0100 D1: mapping is the serializer
  side's).

**P5 — Serializer declarations (R5, R6).**
- `scene_source.h` gains `kSceneSourceSemanticVersion = 1`.
- `scene_artifact.h` gains `kSceneArtifactSemanticVersion = 1`.
- These are code-only constants beside the unchanged format versions. No
  `.cpp` codec logic changes.
- A conformance-test `static_assert` ties each constant to
  `scene::kSemanticVersion`, so a semantic bump fails the conformance build
  until each serializer declares the new version.

**P6 — Scene semantic schema data (R3, R4, Q7).** The C++ shape, normative in
information:

```
kSemanticVersion = 1
components: {TypeId, cardinality ∈ {Required, Optional}, exclusiveGroup}
            Transform Required; Camera/Renderable/Light Optional in group 1
domains:    {FieldId, kind ∈ {Finite, Closed[min,max], AtLeast(min),
             NonNilGuid}}   -- every kind implies finite
constraints:{id, limit}  NonEmptyDocument, NodeGuidNonNil, NodeGuidUnique,
            ParentExists, ParentAcyclic, ActiveCameraExists,
            ActiveCameraHasCamera, ComponentExclusivity,
            LightRangeMatchesKind (Point: >0; Directional: ==0),
            MaxDirectionalLights(1), MaxPointLights(kMaxPointLightsPerScene)
```

- Rows 1–23 of the trace table map one-to-one onto these entries.
- Rows 9 and 10 are structural in the semantic model (fog and bloom are
  fields of `Camera`; material is a field of `Renderable`), so they have no
  entry.
- Limits reuse the existing constants (`kExposureCompensationEv*`,
  `kFogColorMax`, `kMaxPointLightsPerScene`), so the schema cannot drift from
  the codecs' numbers.
- Domain values are scene-local data; Core is untouched (Q7).
- The metamodel is checked at compile time: every component and domain id
  resolves in `assetSystemSchema()`'s scene types, and exactly one component
  is `Required`.

**P7 — Fingerprint (R6).**
- `sceneSemanticFingerprint()` (public) is FNV-1a-64 over a canonical byte
  stream:
  - for each component `TypeId`, sorted by id, and each nested struct and enum
    it reaches: id, kind, `SchemaVersion`;
  - fields sorted by `FieldId`: id, kind, primitive, referenced type, flags;
  - enum constants sorted by value: value plus length-prefixed name;
  - then the components, domains (min/max as IEEE-754 bit patterns, little
    endian) and constraints, each sorted by id.
- Excluded:
  - byte offsets and member order (layout, not meaning);
  - descriptor names (already in the IDs);
  - the semantic version itself.
- `kSemanticVersion` and the fingerprint are pinned together in one test.

**P8 — Property address (R7, Q4 rule 4).**
- `PropertyAddress{EntityGuid node; schema::TypeId component; schema::FieldId
  field}` with defaulted `<=>`.
- Canonical text: `<entity guid>/<component id, 16 lowercase hex>/<field id,
  16 lowercase hex>` (J10). `toString`/`parsePropertyAddress` round-trip it.
- Parsing rejects a nil node and zero ids.
- Nested fields (e.g. fog density): `component` is the node component
  (`scene::Camera`), and `field` is the leaf `FieldId`
  (`scene::CameraFog.density`), reachable from the component through exactly
  one field chain (J8).
- No resolver against the schema is built; that is for its first consumer.

**P9 — Conformance design (R5, R8).** All tests are in `tests/asset_system`.

- **Mapping round trip, exact.** Over the corpus (P10), the following must
  each be equal at the semantic level:
  - `toAuthoringScene(parse(text))`;
  - `toAuthoringScene(toParsedSceneSource(A))`.

  `node_id`s are renumbered by `toParsedSceneSource` and ignored. This is where
  R5's "lossless at the semantic level" is checked.
- **Corpus projection.** For every cooked corpus scene (located via the
  catalog), `decodeSceneArtifact(bytes)` must equal an independently
  computed `expectedProjection(toAuthoringScene(parse(text)))`:
  - GUID → `assetKey()`;
  - parent / active-camera GUID → index;
  - `Decoded*` field mapping;
  - EntityGuids in node order.

  No serializer emits text on this path, so F5 cannot interfere.
- **Field coverage (R8).** A registry maps every `FieldId` of the seven types
  to a perturbation of a one-node base scene, using a valid non-default value
  exactly representable in six decimals. Each case checks:
  - the mapping round trip;
  - text round trip through `serializeSceneSource()` within its exact domain
    (J3);
  - cook → decode equal to the projection;
  - and that the projection differs from the base's.

  Enum fields cover every enumerator. A completeness test fails when the
  registry and the schema's `FieldId` set differ in either direction.
- **Constraint and domain coverage (R4, R8).** A registry maps every
  constraint id and every domain bound to:
  - a source-side negative case (literal text) and its expected
    `SceneSourceParseError` or `SceneCookError`;
  - a decode-side negative case (a valid cooked artifact with bytes patched at
    the record offsets `scene_artifact.h` documents) and its expected
    `SceneArtifactDecodeError`;
  - or an explicit marker: `Structural` or `KnownGap(F1/F3)`.

  Each inclusive bound also has a boundary-accepted case. A completeness test
  fails on a missing registry entry.
- **Declarations:** `static_assert`s per P5.

**P10 — Corpus.**
- `assets/scenes/*.scene.txt` (26 files), `assets/bistro/bistro_overlay.scene.txt`,
  and `assets/_test_fixtures/cmake_scene_declaration_test.scene.txt`,
  enumerated at test-run time from compile-time directory definitions.
- Content-gated (`[content]`): the imported Bistro scene, when
  `content/bistro/` is present.

**P11 — Byte-behaviour guard.**
- **M0**, at the branch base (`origin/main`), before any code: a clean
  configure and Debug build, then a SHA-256 manifest of every file under
  `build/assets/` that the cooker or the catalog assembly writes (artifacts,
  sidecars, fragments, the assembled catalog), and the Bistro import outputs
  when content is present. The manifest is kept outside the tree and its
  count is recorded.
- **Every gate from M1:** after the build, recompute the manifest and diff it.
  It must be identical: same file set, same hashes.
- Also, at every gate, `git diff origin/main --name-only -- assets
  tests/image_regression/goldens shaders src/runtime src/world src/tools`
  must be empty.
- The procedure and its results go in the PR. Nothing is committed: a
  committed test cannot see `origin/main`.

## Milestones / Task Breakdown

Seven milestones. Commit prefixes: `feat:` for code, `test:` for test-only
steps, `docs:` for M6's docs. Each gate is:
- Debug + Release build;
- the full `ctest` in both configurations;
- the P11 manifest diff;

plus Android `assembleDebug` where stated.

### M0 — Baseline (no commit)

Capture P11's manifest at `origin/main`, then record the Debug/Release test
counts.

### M1 — Semantic component types (R1)

1. `scene_semantic_types.h` and the seven descriptors in
   `asset_system_schema.cpp` (P2).
2. `tests/asset_system/scene_semantic_types_tests.cpp`: the Spec 0048 sync
   pattern for all seven types, including a `/w14062` canary for
   `scene::LightKind`.
3. **Expected pin changes:**
   - `asset_system_schema_tests.cpp`'s listing test goes from 6 to 13 names;
   - `tests/world/schema_uniqueness_tests.cpp` goes from `combined.size() == 13`
     to **20**.
4. `tests/world/scene_world_alignment_tests.cpp`: for Transform, Camera,
   CameraFog, CameraBloom, Light and LightKind, the field (or constant) names
   and kinds equal World's. For Renderable, the names are equal and the kind
   differs as declared (AssetGuid vs UInt64).

*Gate:* the above, plus `assembleDebug` (standard-layout of
`std::optional<AssetGuid>` under libc++).

### M2 — Authoring document and mappings (R2, R5 mappings)

1. `authoring_scene.h` and `authoring_scene_mapping.{h,cpp}` (P3, P4).
2. `tests/asset_system/authoring_scene_mapping_tests.cpp`:
   - every mapping error;
   - component conversion field by field;
   - default construction equal across `Decoded*` and the semantic types;
   - the corpus mapping round trip (P9, first bullet).

*Gate:* standard.

### M3 — Scene semantic schema, version, fingerprint, declarations (R3, R4, R6)

1. `scene_semantic_schema.{h,cpp}` (P6, P7), with the compile-time metamodel
   checks.
2. `kSceneSourceSemanticVersion` / `kSceneArtifactSemanticVersion` (P5).
3. `tests/asset_system/scene_semantic_schema_tests.cpp`:
   - metamodel well-formedness;
   - one test per trace-table row asserting its schema entry and limit;
   - the version/fingerprint pin;
   - the declaration `static_assert`s.
4. Demonstration (not committed): changing one domain bound fails the pin
   until the version is bumped.

*Gate:* standard.

### M4 — Property address (R7)

1. `scene_property_address.{h,cpp}` (P8).
2. Tests:
   - equality and ordering;
   - the canonical text round trip;
   - each parse rejection (nil node, zero ids, malformed hex, wrong separators);
   - a nested-field example.

*Gate:* standard.

### M5 — Conformance suite (R4, R5, R8)

1. `tests/asset_system/scene_conformance_tests.cpp`: the corpus projection,
   field coverage, and constraint/domain coverage registries with their
   completeness tests (P9).
2. Content-gated Bistro corpus cases.
3. Demonstrations (not committed):
   - a field added to `scene::CameraBloom` and its table, without a registry
     entry, fails the completeness test;
   - a constraint added without a case fails likewise.

*Gate:* standard.

### M6 — Acceptance and docs

1. Final P11 manifest diff and path-diff check.
2. `docs/architecture/module_boundaries.md`, Atlantis Asset System: one
   sentence naming the scene semantic layer (ADR-0100).
3. Final diff review against this Plan.

*Gate:* standard, plus `assembleDebug`.

## Files / Modules Touched (expected)

- **Asset System:**
  - new public headers `scene_semantic_types.h`, `authoring_scene.h`,
    `authoring_scene_mapping.h`, `scene_semantic_schema.h`,
    `scene_property_address.h`;
  - new sources `authoring_scene_mapping.cpp`, `scene_semantic_schema.cpp`,
    `scene_property_address.cpp`;
  - changed: `asset_system_schema.cpp` (seven descriptors), `scene_source.h`
    and `scene_artifact.h` (one constant each), `CMakeLists.txt`.
- **Tests:**
  - `tests/asset_system/`: new `scene_semantic_types_tests.cpp`,
    `authoring_scene_mapping_tests.cpp`, `scene_semantic_schema_tests.cpp`,
    `scene_property_address_tests.cpp`, `scene_conformance_tests.cpp`;
    changed `asset_system_schema_tests.cpp` (listing pin) and `CMakeLists.txt`
    (sources and corpus directory definitions);
  - `tests/world/`: new `scene_world_alignment_tests.cpp`; changed
    `schema_uniqueness_tests.cpp` (pin 13 → 20) and `CMakeLists.txt`.
- **Docs:** `docs/architecture/module_boundaries.md` (one sentence).

**Not touched:**
- the scene grammar, `scene_source.cpp`, `scene_artifact.cpp`, `cook_scene.cpp`,
  `decode_scene.cpp`, `scene_types.h`, `validated_scene_data.h`;
- every other codec;
- World, Runtime, the importer and its writer, Core;
- assets, sidecars, the catalog, goldens and shaders.

No dependency is added.

## Sequencing & Dependencies

M0 → M1 (types) → M2 (the mappings need the types) → M3 (the schema
references the type descriptors) → M4 (the address uses the IDs; independent
of M2/M3, ordered for review size) → M5 (needs M2 + M3) → M6.

## Verification Checklist

Maps to Spec 0049's Testing & Verification Plan.

- [ ] **R1:** sync tests for the seven types; World-name alignment; listing
  pin 13 names; uniqueness pin 20.
- [ ] **R2/R3:** metamodel well-formedness (compile-time and test);
  trace-table rows 1–23 each asserted.
- [ ] **R4:** one source-side and one decode-side negative case per
  constraint and domain bound, or an explicit `Structural`/`KnownGap` marker.
  Today's error asserted per site.
- [ ] **R5:**
  - exact mapping round trip over the corpus;
  - corpus artifacts equal to the independent projection;
  - text round trip within the serializer's exact domain;
  - declarations agree (`static_assert`).
- [ ] **R6:** fingerprint and version pin, with the bump demonstration
  recorded in the PR.
- [ ] **R7:** address equality, text round trip, rejections, nested example.
- [ ] **R8:** both completeness registries, with their failure demonstrations
  recorded in the PR.
- [ ] **Byte guard (P11):** the manifest identical at every gate; the path
  diff empty.
- [ ] **Full suites** Debug + Release at every gate; `assembleDebug` at M1
  and M6.
- [ ] **Boundaries:** the Asset System scan (no World include) and the Core
  scan unchanged and passing.
- [ ] **GPU:** no new image-regression test and no new Validation-Layer
  obligation, since no GPU path is touched. The existing suites run as part of
  the full-suite gate.

## Joint Review decisions (to be ruled)

- **J1 — Decode-side gaps F1 (exclusivity) and F3 (key 0).**
  - **Recommendation:** record both as `KnownGap` markers in the conformance
    registry and leave the decoder unchanged. Tightening the decoder changes
    codec behaviour, which ruling Q3 excludes; it belongs to a follow-up spec.
  - Alternative: tighten the decoder here. That is a codec change outside
    Spec 0049.
- **J2 — The node-count bound (F4).**
  - **Recommendation:** classify it as an artifact-format capacity, not a
    v1 semantic rule. It is listed as a projection precondition and stays out
    of the semantic schema and fingerprint.
  - Alternative: make it a semantic constraint. That would put a serializer
    limit into the meaning.
- **J3 — Where "lossless" is checked (F5).**
  - **Recommendation:** exactly at the `ParsedSceneSource` ⇄ semantic
    mapping and by corpus projection (P9). Text emission through
    `serializeSceneSource()` is tested only within its exact domain, and its
    lossiness is recorded as a serializer limitation. The serializer cannot
    change without changing the Bistro scene's bytes.
- **J4 — Off-group fog/bloom values.**
  - **Recommendation:** v1 keeps them as ordinary fields, with no
    "don't-care while off" equivalence. The parser, the cooker and the
    artifact all preserve them (`fog_zero_neutrality` cooks its fog height
    into the artifact). An inert-field equivalence would be new semantics, a
    candidate for v2.
- **J5 — The transform's cardinality.**
  - **Reading:** R2 makes the transform mandatory, and R3's "0..1" applies to
    the optional components. The metamodel marks Transform `Required` and the
    others `Optional`, consistent with ruling Q4's "at most one".
  - **Recommendation:** confirm.
- **J6 — `Editable` on the scene types.**
  - **Recommendation:** `Serializable` only, following Plan 0048 J3's
    binding definition ("the live World editing surface").
  - Alternative: mark authoring fields `Editable`. That revisits J3's meaning
    and needs its own ruling.
- **J7 — Vectors as `std::array<float, 3>`.**
  - **Recommendation:** accept. This avoids an eighth type beyond ruling Q6,
    and `std::array` maps onto `Vec3Float32`.
- **J8 — Nested-field addressing.**
  - **Recommendation:** the leaf `FieldId` under the node component's
    `TypeId`, valid only while that leaf is reachable through exactly one
    field chain, as all v1 types are. A path form waits for a type that
    repeats a struct.
- **J9 — The importer's writer (F7).**
  - **Recommendation:** no declaration and no change, since the Spec
    excludes importer changes. Its output is held to the schema by the parse
    and projection conformance over the content-gated Bistro scene and the
    existing importer tests.
- **J10 — The address's canonical text.**
  - **Recommendation:** GUID and two 16-hex IDs. IDs are the identity;
    names are resolvable through the schema.
  - Alternative: names in the text, which are readable but must be resolved
    on parse.

## Rollback Plan

The change is additive: the codecs, formats and runtime path are untouched.
Revert the implementation PR as a whole. Before merge, revert milestone by
milestone in reverse order. M1's two pin changes revert with it.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).

Deltas:
- [ ] P11 manifest identical to the M0 baseline at the final gate, with the
      file count reported in the PR.
- [ ] The fingerprint-bump demonstration and the two coverage-completeness
      demonstrations recorded in the PR.
- [ ] Findings F1–F7 restated in the PR with the J rulings applied.
- [ ] `assembleDebug` passing at the final gate.
