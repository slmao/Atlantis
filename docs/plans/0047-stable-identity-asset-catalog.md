# Plan: Cross-Session Stable Identity and Asset Catalog

- **Spec:** [Spec 0047: Cross-Session Stable Identity and Asset Catalog](../specs/0047-stable-identity-asset-catalog.md)
  (`Approved`, 2026-09-28; rulings Q1–Q11 binding) —
  [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md) (identity) and
  [ADR-0098](../adr/0098-asset-catalog-and-catalog-based-resolution.md)
  (catalog and resolution), both `Accepted`
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-09-28 — reviewed this Plan and
  [Spec 0047](../specs/0047-stable-identity-asset-catalog.md) together and
  explicitly authorized Implementation from Milestone 1 (chat confirmation;
  document set carried by this branch's PR). All four open points were ruled
  as recommended, O4 with an added measurement requirement; see Open points
  below.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0047 R1–R15 exactly — GUID-keyed asset and entity identity
(ADR-0097) and the build-assembled catalog with Runtime resolution through it
(ADR-0098) — with every golden byte-identical, `world::EntityId` and World's
component types unchanged, no new third-party dependency, and the
AssetSystem ↛ World include ban intact. The Spec's eleven rulings and both
ADRs are binding; nothing here reopens them.

## Pre-drafting reading (cited, not restated)

Re-walked at `origin/main` `dab6f4a` (PR #194 merged).

1. **Identity sites to switch.**
   - `computeAssetId(path)` callers: five cooks (`cook.cpp:107`,
     `cook_texture.cpp:141`, `cook_environment.cpp:90`,
     `cook_material.cpp:105-131`, `cook_scene.cpp:217,227`), the importer
     (`import_command.cpp:442`), the cooker's `--validate-set`
     (`cook_command.cpp:507-510`) and the Runtime manifest loader
     (`scene_manifest.cpp:168-183`).
   - Loader self-checks against the sidecar path: `load.cpp:57`,
     `load_texture.cpp:69`, `load_material.cpp:123`,
     `load_environment.cpp:58`.
2. **Formats that change** (current version → ADR-0097 D3 target):

   | Format | Now | Where |
   |---|---|---|
   | scene source | v6 | `scene_source.cpp:18`; `mesh=`/`material=` tokens `:40-41` |
   | scene artifact | schema 6, 152-byte record | `scene_artifact.h:34-47` |
   | material source | v9; `texture:`, `normal_map:`, `emissive_texture:` | `material_source.cpp:13-53` |
   | mesh / environment / texture / material / scene sidecar | v1 (8 lines) / v1 (7) / v3 (9) / v8 (19) / v1 (3) | `*_metadata.cpp:12-20` |

   Mesh sources, texture images and the material *artifact* carry no path
   reference and do not change layout.
3. **Importer.** Positional names (`scene_import.cpp:171-173`,
   `material_import.cpp:237-239`), `node_id = glTF index + 1` with synthetic
   children for a second primitive or a light beside a mesh
   (`scene_import.cpp:244-248`), `asset_list.txt` / `cook_manifest.txt`
   (`import_command.cpp:421`, `:580-586`), tool string
   `"atlantis_gltf_importer 1.0"` (`:533`, contains a space).
4. **Build.** Per-kind declaration functions and the global
   `ATLANTIS_DECLARED_ASSET_LOGICAL_PATHS` → `declared_assets.txt` →
   `--validate-set` (`src/asset_system/CMakeLists.txt:77-124`, `:477-489`);
   per-scene manifests from `MESH_/MATERIAL_/TEXTURE_DEPENDENCIES`
   (`:137-236`); material `TEXTURE`/`NORMAL_MAP` arguments used only for
   ordering (`:407-465`); `atlantis_add_imported_scene()` (`:250-288`).
   108 declarations: 106 in `assets/CMakeLists.txt` plus 2 in
   `tests/asset_system/CMakeLists.txt:118-150` (the `_test_fixtures` pair) —
   7 mesh, 13 texture, 58 material, 27 scene, 2 environment, 1 gltf_import,
   as the generated `declarations.txt` (P10) lists them.
5. **Runtime.** `BootstrapConfig::scene{Artifact,Metadata,DependencyManifest}Path`
   (`bootstrap_config.h:31-33`); whitelist from compile definitions
   (`main.cpp:54-77`, `src/runtime/CMakeLists.txt:104-106`, `:191-202`,
   `:252-254`); resolution in `scene_load.cpp:27-100`. Resource maps are
   keyed by `AssetId` and need no change (`runtime_application.cpp:1556-1891`).
   49 image-regression files and 6 runtime test files consume manifest paths.
6. **Android.** Gradle copies a fixed list plus one manifest, rewriting its
   paths (`build.gradle:25-60`, `:118-190`); `android_main.cpp:44-91` and
   `extractSceneManifest()` (`asset_extraction.cpp:93-165`) finish the
   rewrite. The native build runs the root CMake with tests and examples off
   (`build.gradle:214-216`).
7. **World.** `fromValidatedSceneData()` builds `byIndex` and drops it
   (`scene_instantiation.cpp:11-12`); boundary scans exist in
   `tests/asset_system/module_boundary_tests.cpp` and
   `tests/world/module_boundary_tests.cpp`.
8. **Goldens.** 30 under `tests/image_regression/goldens/` (29 committed
   plus the content-gated `bistro_demo`).

## Order note — where the one-time migration lands

The brief orders the work identity primitives → catalog source → cook/loader
switch → format bumps → assembly → Runtime switch and retirement →
EntityGuid/SceneEntityMap → EntityRef → migration → Android. This Plan keeps
that order for the **code**, with one change: the **content** migration (R15)
lands at the end of M3, the first milestone that changes a committed format.

The reason: from the cook/loader switch onwards, the committed sources no
longer cook, so every milestone gate until the migration would fail its
golden check. Keeping them green without migrating would need two formats
readable at once inside the branch, which ADR-0045's single-version policy
forbids.

The migration **tool** is built in M2 and run exactly once, as one commit,
at the end of M3 — still "one reviewed, mechanical change". The brief's late
"migration" slot becomes M9, which retires that tool.

Android's code switch lands with the Runtime switch (M5), since the per-scene
manifest has exactly two consumers and retiring it for one would leave the
Android build broken until M8. M8 keeps the brief's final position for the
emulator closure-catalog verification.

Both moves were confirmed by the Joint Human Review (rulings O1 and O2,
2026-09-28).

## Plan-stage decisions

These are details the Spec left to the Plan; none changes a Spec requirement
or ADR decision.

**P1 — File placement.**
- Asset System (public):
  - `asset_guid.{h,cpp}`: `AssetGuid`, `EntityGuid`, codecs, FNV-1a-128,
    derivation, `assetKey()`;
  - `asset_catalog_source.{h,cpp}`;
  - `asset_catalog.{h,cpp}`: records, parse/serialize, assembly and closure
    logic, immutable `AssetCatalog`;
  - `entity_ref.{h,cpp}`.
- Cooker-private: `guid_mint.{h,cpp}` — minting lives only in the tool
  (P4).
- World: `SceneEntityMap` and `instantiateScene()` in the existing
  `scene_instantiation.h`.
- Runtime-private: `entity_ref_resolution.{h,cpp}`,
  `catalog_extraction.{h,cpp}` (Android).

**P2 — Types.**
- `AssetGuid` and `EntityGuid` are distinct structs, each holding
  `std::array<std::byte, 16>` in text order, with defaulted `<=>`.
- `parseAssetGuid()` / `parseEntityGuid()` return
  `Result<…, GuidParseError>` with the errors `WrongLength`, `MissingHyphen`,
  `NotLowercaseHex` and `NilGuid`.
- `toString()` produces the canonical text.
- `assetKey(AssetGuid) -> AssetId` is FNV-1a-64 over the 16 bytes (ADR-0097
  D2), reusing the existing FNV-1a-64 routine — the algorithm, not the
  path-hashing public entry point (ruling I1).
- Binary codec: `assetGuidFromBytes()` / `entityGuidFromBytes()`, rejecting
  only nil; the byte array is a public member (ruling I3).

**P3 — Derivation (ADR-0097 D4).**
- FNV-1a-128: offset basis `6c62272e07bb014262b821756295c58d`, prime
  2^88 + 0x13b, computed with two 64-bit limbs.
- The input is the owner's 16 bytes followed by the sub-key's ASCII bytes.
- The result is written big-endian into the 16 bytes, then the version
  nibble is set to 8 (byte 6) and the variant to `10` (byte 8).
- `deriveAssetGuid(AssetGuid, std::string_view)` and
  `deriveEntityGuid(AssetGuid scene, std::string_view)` are public: the
  importer and the Runtime's Bistro whitelist entry (P15) both call them.
  They hash the sub-key byte for byte; validating sub-keys is the caller's
  job (ruling I2).

**P4 — Minting.**
- `atlantis_asset_cooker --kind=mint-guid [--count=N]` prints N version-4
  GUIDs, one per line, from `std::random_device`.
- An all-zero draw is retried.
- No library function mints. The structural guarantee is that the
  Asset System library has no mint entry point, so no cook, import or
  Runtime path can reach one.

**P5 — Import sub-keys.**

| Asset | Sub-key |
|---|---|
| mesh | `mesh/<meshIndex>/<primitiveIndex>` |
| material | `material/<i>` |
| texture | `texture/<uri>` (the URI as the importer already normalizes it) |
| white fallback | `fallback/white` |
| scene | `scene` |

Nodes derive from the scene GUID:
- a node itself: `node/<glTF index>`;
- a synthetic extra primitive: `node/<i>/primitive/<j>`;
- a synthetic light: `node/<i>/light`.

Overlay nodes keep their authored GUIDs.

**P6 — Catalog source (normative).** `assets/asset_catalog.txt`:

```
atlantis_asset_catalog_source_version: 1
entry_count: <n>
asset: guid=<guid> type=<mesh|texture|material|scene|environment|gltf_import> root=<assets|content> path=<logical path>
```

- Entries are in bytewise (root, path) order.
- `path` must equal its `normalizeLogicalPath()` form.
- Parse errors: `UnknownVersion`, `EntryCountMismatch`, `MalformedEntry`,
  `NilGuid`, `UnknownType`, `UnknownRoot`, `NonNormalPath`, `Unsorted`,
  `DuplicateGuid`, `DuplicatePath`.
- Lookup is by (root, path) and by GUID.
- Bistro's entry is `root=content path=bistro/bistro.gltf type=gltf_import`.

**P7 — Tool arguments.**
- Every per-asset cook mode requires `--catalog-source=<path>` and looks up
  its own `--source` as `assets:<logical path>`. A miss fails with
  `SourceNotInCatalog`; a wrong type with `CatalogTypeMismatch`.
- `--guid=<guid>` is accepted only on `cook_manifest.txt` lines, where the
  importer supplies derived GUIDs. On the command line it is rejected, so
  CMake cannot carry a GUID.
- The importer requires `--catalog-source=` and looks itself up as
  `content:<content-root name>/<glTF file name>`.
- The importer's tool string becomes `atlantis-gltf-importer/1`, which fits
  the catalog's space-free `tool` token.

**P8 — Sidecars (ADR-0097 D3).**
- `asset_guid: <guid>` becomes line 2, directly after the version line, in
  every sidecar.
- New versions: mesh 1 → 2, environment 1 → 2, texture 3 → 4,
  material 8 → 9, scene 1 → 2.
- Loaders check `assetId == assetKey(asset_guid)`. The scene decoder checks
  that the GUID is well-formed and non-nil.

**P9 — Scene v7 / artifact 7 / material v10.**
- Scene source v7: `guid=<entity guid>` follows `node_id=`, and `mesh=` /
  `material=` take AssetGuids.
- Scene artifact schema 7: `entity_guid` is appended at node-record offset
  152, making the record 168 bytes; the header is unchanged apart from its
  version.
- Material source v10: `texture:`, `normal_map:` and `emissive_texture:`
  take AssetGuids. The material artifact is unchanged.
- New errors:
  - `SceneSourceParseError::MalformedGuid`;
  - `MaterialSourceParseError::MalformedGuid`;
  - `SceneCookError::{NilEntityGuid, DuplicateEntityGuid}`;
  - `SceneArtifactDecodeError::{NilEntityGuid, DuplicateEntityGuid}`.

**P10 — Declarations list.**
- Each `atlantis_add_*_asset()` and `atlantis_add_imported_scene()` appends
  `type\troot\tpath` to a global property.
- `file(GENERATE)` writes it to `<build>/assets/declarations.txt`, replacing
  `declared_assets.txt`.
- It is the migration's input (M3) and the assembly's declaration check
  (M4).

**P11 — Transitional code, M3 to M4/M5 only.**
- From M3, `loadSceneDependencyManifest()` takes each entry's `AssetId` from
  that dependency's sidecar `asset_guid` rather than hashing its path.
- `--validate-set` stays as a path-duplicate check.
- Both are deleted by M5 and M4 respectively; nothing transitional reaches
  the implementation PR's final state.

**P12 — Cooked catalog and fragments (normative).**

```
atlantis_asset_catalog_version: 1
record_count: <n>
record: guid=<g> asset_id=<16 hex> type=<type> source=<root>:<path>[#<sub-key>] artifact=<rel> metadata=<rel> artifact_schema=<u32> source_schema=<u32|none> tool=<token> deps=<k>[ <g>…]
```

- Records are sorted by GUID; `deps` are sorted and de-duplicated.
- Locations are `/`-relative to the catalog's directory.
- A fragment uses the same grammar with absolute locations, which the
  assembler relativizes.
- Where fragments are written:
  - each cook writes `<artifact>.catalog.txt`;
  - the importer's cook-manifest mode writes `<import dir>/import.catalog.txt`
    for every record of its import;
  - the importer writes the mesh records itself, since meshes are written
    directly.
- `source_schema` values:

  | Type | `source_schema` |
  |---|---|
  | mesh (hand-authored) | 3 |
  | scene | 7 |
  | material | 10 |
  | texture, environment, imported mesh | `none` |

**P13 — Assembly (ADR-0098 D2).**
- Command: `atlantis_asset_cooker --kind=assemble-catalog
  --catalog-source= --declarations= --fragment-list= --out=`, plus zero or
  more `--closure=<scene guid>=<out>`.
- Before writing, the assembler reads each record's sidecar and checks its
  `asset_guid`.
- `AssetCatalogAssemblyError` values:

  | Error | Meaning |
  |---|---|
  | `FragmentUnreadable` | a fragment file cannot be read |
  | `MalformedFragment` | a fragment does not parse |
  | `DuplicateGuid` | two records share a GUID |
  | `DuplicateAssetId` | two records share an `asset_id` key |
  | `ZeroAssetId` | a key is 0 |
  | `AssetIdMismatch` | `asset_id` ≠ `assetKey(guid)` |
  | `DanglingDependency` | a dependency GUID has no record |
  | `DependencyTypeMismatch` | a dependency has the wrong type |
  | `DeclarationNotInCatalog` | a declaration has no catalog-source entry |
  | `DeclarationTypeMismatch` | a declaration's type differs from its entry |
  | `SidecarGuidMismatch` | the sidecar's `asset_guid` differs from the record |
  | `LocationEscapesCatalog` | a location leaves the catalog's directory |
  | `UnknownClosureScene` | a `--closure` GUID is not a scene record |

- Catalog-source entries with no declaration are counted in its log line.
- `atlantis_finalize_asset_catalog()` replaces
  `atlantis_finalize_asset_validation()`. Its target depends on every asset
  target and on the Bistro import when present, and writes
  `<build>/asset_catalog.txt` plus
  `<build>/integrated_showcase_demo.catalog.txt` (the Android closure).

**P14 — Runtime API (ADR-0098 D3).**
- `loadAssetCatalog(path) -> Result<AssetCatalog, AssetCatalogError>`.
- `AssetCatalog` is immutable (safe for concurrent reads) and holds records
  sorted by `AssetId`. `find(AssetId)` and `find(const AssetGuid&)` return
  `const AssetCatalogRecord*` whose locations are already resolved against
  the catalog's directory.
- `BootstrapConfig` replaces its three scene paths with `assetCatalogPath`
  and `sceneAsset` (`AssetGuid`).
- `RuntimeInitError` gains `AssetCatalogLoadFailed`, `SceneNotInCatalog`,
  `DependencyTypeMismatch` and `UnsupportedArtifactSchema`, and drops
  `SceneManifestLoadFailed`.
- The load keeps ADR-0054's two-phase, all-or-nothing order.

**P15 — Scene selection.**
- `atlantis_catalog_guid(<var> ROOT <root> PATH <path>)` reads the catalog
  source at configure time; the file is added to `CMAKE_CONFIGURE_DEPENDS`.
- Its result becomes compile definitions: `ATLANTIS_RUNTIME_ASSET_CATALOG_PATH`,
  plus one `ATLANTIS_RUNTIME_<SCENE>_GUID` per whitelist scene. These are
  generated values, not a second authored copy.
- Bistro's entry passes the import root's GUID, and `main.cpp` derives the
  scene with `deriveAssetGuid(root, "scene")`.
- The test fixtures use the same definitions.

**P16 — Retirement (ADR-0098 D3, Spec Q6).**
- Removed:
  - `scene_manifest.{h,cpp}` and its tests;
  - the manifest output of `atlantis_add_scene_asset()`;
  - its `MESH_/MATERIAL_/TEXTURE_DEPENDENCIES`;
  - the material function's `TEXTURE`/`NORMAL_MAP` arguments — the same
    fact, now taken from cooked content (Spec R9, ADR-0098 Alternatives);
  - the cook-manifest mode's `--manifest-out`;
  - the `_MANIFEST_PATH` exports;
  - `extractSceneManifest()` and the Gradle rewrite.
- Build ordering comes from the assembly target depending on all asset
  targets.

**P17 — World (ADR-0097 D5).**
- `struct SceneInstance { World world; SceneEntityMap entities; };` and
  `SceneInstance instantiateScene(const ValidatedSceneData&)`.
- `SceneEntityMap` is immutable: a sorted `std::vector<std::pair<EntityGuid,
  EntityId>>`, with `find(const EntityGuid&) -> std::optional<EntityId>` and
  `size()`.
- `fromValidatedSceneData(scene)` returns `instantiateScene(scene).world`,
  so the two cannot diverge.
- `ValidatedSceneData` gains `entityGuid(index)`.
- The Runtime keeps the map and the loaded scene's GUID beside its `World`.

**P18 — EntityRef (ADR-0097 D6).**
- Text form: `<scene guid>/<entity guid>` (73 characters).
- Binary form: 32 bytes, scene then entity.
- `EntityRefParseError` values: `WrongLength`, `MissingSeparator`, and the
  GUID errors.
- Runtime-private `resolveEntityRef(loaded scene, ref) -> Result<EntityId,
  EntityRefError{UnknownScene, UnknownEntity, DeadEntity}>`, where liveness
  comes from `World`.

**P19 — Android.**
- Gradle packages `integrated_showcase_demo.catalog.txt` and every
  artifact/metadata path listed in it, verbatim, preserving relative
  layout.
- `extractCatalogClosure()` copies the catalog and each listed file under
  `internalDataPath`, using the Asset System parser — no path rewriting.
- The scene GUID reaches the native build through the same P15 compile
  definition.

**P20 — Migration mode (one-off).**
- `atlantis_asset_cooker --kind=migrate-0047 --declarations=
  --asset-root= --catalog-source-out=`:
  - mints one GUID per declaration;
  - writes the catalog source;
  - rewrites every committed scene source (and the overlay) v6 → v7, adding
    minted `guid=` and replacing path references with catalog GUIDs;
  - rewrites every material source v9 → v10 the same way.
- Scene sources that are not declarations are named explicitly with a
  repeatable `--scene-source=<assets-relative path>` (ruling I5); the
  one-time run passes the Bistro overlay this way.
- It works on text tokens and does not depend on the v7/v10 parsers.
- It fails on any reference it cannot map.
- It is removed in M9.

## Milestones / Task Breakdown

Nine milestones. Commit prefixes: `feat:` for code, `chore:` for the M3
content migration and the M9 cleanup, `test:` for test-only steps. Each
milestone's gate must pass before the next begins. Within M3, sub-steps are
verified by building and running the affected unit-test targets; the full
build and golden gate comes at M3's end, where the migration lands.

### M1 — Identity primitives (R1, R2, R6)

1. `asset_guid.{h,cpp}` (P1–P3): types, text/binary codecs, nil and format
   errors, FNV-1a-128, derivation, `assetKey()`.
2. `guid_mint.{h,cpp}` and `--kind=mint-guid` (P4).
3. Tests: round trips; each parse error; FNV-1a-128 reference vectors;
   derivation and key vectors pinned in the test; distinct derived GUIDs for
   distinct sub-keys; minted values carry version 4 and differ.

*Gate:* Debug + Release, all tests; nothing else changes (additive).

### M2 — Catalog source, lookup and migration tool (R3, R4 lookup, R15 tool)

1. `asset_catalog_source.{h,cpp}` (P6), with every error tested.
2. `--kind=lookup --catalog-source= (--guid=|--source=<root>:<path>)`,
   printing the matching entry — the readability aid Q4 relies on.
   Extended to cooked catalogs in M4.
3. The declarations list (P10), additive beside `declared_assets.txt`.
4. `--kind=migrate-0047` (P20), unit-tested on synthetic v6/v9 trees
   including a reference it cannot map. Not run on the repository.

*Gate:* Debug + Release, all tests; additive.

### M3 — Identity cut-over and the one-time migration (R2, R4, R5, R6, R7 format, R8, R15)

1. Sidecars (P8) and loader self-checks; sidecar tests.
2. Cooks take `AssetGuid` resolved through `--catalog-source` (P7);
   `SourceNotInCatalog` and `CatalogTypeMismatch`; cook tests.
3. Scene v7, artifact 7, material v10 (P9); cook and decode reject nil or
   duplicate EntityGuids; every in-test scene and material text literal
   moves to the new versions.
4. Importer (P5, P7): root lookup, derived GUIDs, v7/v10/sidecar output,
   `--guid=` on manifest lines, overlay v7; importer tests on
   `gltf_test_builder.h` output.
5. CMake passes `--catalog-source` to every cook and the importer, and each
   cook `DEPENDS` on the catalog source; transitional manifest loader (P11).
6. **Migration (R15):** run `migrate-0047` once; commit the catalog source
   plus every rewritten scene, material and overlay source as one `chore:`
   commit. The diff is reviewed for:
   - exactly one entry per declaration;
   - only `guid=` added and references replaced;
   - version lines bumped;
   - nothing else.

*Gate:*
- Full Debug + Release.
- All GPU-independent tests.
- **Every golden byte-identical** (29 committed; `bistro_demo` with content
  present). Validation Layers clean.
- `atlantis_runtime --scene <each>` renders.
- The cook and import determinism tests pass, including a new
  real-subprocess import-twice test.
- Android `assembleDebug`.

*Risk gate:* any golden diff is a defect in the cut-over. Stop and report;
never re-baseline.

### M4 — Catalog assembly replaces `--validate-set` (R9, R10)

1. `asset_catalog.{h,cpp}` records, grammar and assembly logic (P12, P13);
   fragments from every cook and from the import.
2. `--kind=assemble-catalog` with `--closure=`; `lookup` accepts a cooked
   catalog.
3. `atlantis_finalize_asset_catalog()` replaces
   `atlantis_finalize_asset_validation()`. Deleted: `--validate-set`,
   `validateAssetSet()` / `asset_set_validation.*`, `declared_assets.txt`
   and the public `computeAssetId(path)` entry point — which has no callers
   left once the transitional loader moves to sidecar GUIDs in M3. Its
   byte-wise FNV-1a-64 core becomes an internal helper, and `assetKey()`
   switches to it (ruling I1).
4. Tests: every P13 error triggered alone (key collisions and zero keys by
   injected values, as `validateAssetSet` tests do today); closure
   contents; content-gated entries counted, not failed; a real-subprocess
   assemble-twice determinism test.

*Gate:* full Debug + Release, all tests, goldens byte-identical (nothing
rendered changes); with Bistro present, the catalog holds every import
record.

### M5 — Runtime resolves through the catalog; retirement (R11, R13 assets)

1. `loadAssetCatalog()` / `AssetCatalog` (P14); `scene_load.cpp` resolves the
   scene and every dependency through it, with type and schema checks.
2. `BootstrapConfig`, whitelist and compile definitions (P15); every
   image-regression fixture and runtime test moves to catalog path and scene
   GUID.
3. Retirement (P16), including the transitional loader (P11).
4. Android code switch (P19): Gradle packaging and `extractCatalogClosure()`.
5. Cross-scene asset test: a hand-authored scene referencing a mesh and a
   material from a builder-generated import resolves through one assembled
   catalog; two scenes sharing a mesh resolve to its single record.

*Gate:*
- Full Debug + Release; goldens byte-identical; Validation Layers clean.
- Every whitelist scene renders windowed, and `bistro` renders when the
  content is present.
- Android `assembleDebug`.

### M6 — EntityGuid exposure and `SceneEntityMap` (R7, R12)

1. `ValidatedSceneData::entityGuid()`; `instantiateScene()`,
   `SceneEntityMap` and the `fromValidatedSceneData()` wrapper (P17).
2. The Runtime keeps the map and scene GUID.
3. Tests:
   - map size equals node count, and each entry names that node's entity;
   - `fromValidatedSceneData()` yields a `World` identical to
     `instantiateScene().world`;
   - a World boundary scan: `EntityGuid` appears in World headers only in
     `scene_instantiation.h`;
   - the AssetSystem ↛ World scan still passes.

*Gate:* Debug + Release, all tests; goldens byte-identical.

### M7 — `EntityRef` (R13 entities)

1. `entity_ref.{h,cpp}` (P18) and the Runtime-private resolver.
2. Tests: codec round trips and each parse error; resolution of a matching
   entity and of an unknown-scene, unknown-entity and destroyed entity.

*Gate:* Debug + Release, all tests.

### M8 — Android closure catalog on the emulator (R10, R11)

1. Build, package and install; run the default scene from its closure
   catalog; screencap + logcat. This is the Plan 0034 M6 pattern with no
   temporary switch, because the default scene is the packaged one.
2. Check that the packaged file set equals the closure's listed set.

*Gate:* the scene renders on the emulator; logcat free of load errors; the
emulator Validation-Layer gap is inherited and recorded.

### M9 — Acceptance and cleanup (R14; determinism; Q11)

1. **R14 test:** a GPU-independent integration test driving the real cooker
   in a temporary tree:
   - Setup: a catalog source; a mesh, texture, material and two scenes
     sharing the mesh; cook and assemble.
   - Then: rename and move the mesh and texture sources, update their two
     catalog lines, re-cook and re-assemble.
   - Expect: every artifact byte-identical, the catalog changed only in
     `source`/location fields, and both scenes still resolve.
   - Negative cases: a move without the catalog edit fails as
     `SourceNotInCatalog`; a copy keeping the GUID fails as `DuplicateGuid`.
   - Import case: a builder-generated glTF imported from a moved file and
     content root yields the same GUIDs.
2. Remove `migrate-0047` (`chore:`).
3. AGENTS.md: update the sentence on World's dependency on Asset System
   (Spec Q11).
4. Final regression: Debug + Release, every golden, Validation Layers, the
   `[bistro]`/`content` tests, Android `assembleDebug`.

*Gate:* all of the above; the PR records build-time before/after for a
clean build and for a one-line catalog-source edit, each measured both
without and with `content/bistro/` present — including whether the one-line
edit re-triggers the full Bistro import, and its time (ruling O4).

## Files / Modules Touched (expected)

- **Asset System** (`src/asset_system/`):
  - new: `asset_guid`, `asset_catalog_source`, `asset_catalog`,
    `entity_ref` (`.h`/`.cpp`);
  - changed: `asset_id.{h,cpp}` (path hashing removed), `asset_metadata`,
    `texture_metadata`, `material_metadata`, `environment_metadata`,
    `scene_metadata`, `scene_source`, `scene_artifact`, `scene_types.h`,
    `validated_scene_data.h`, `decode_scene`, `material_source`, `cook*`,
    `load*`, `errors.h`, `CMakeLists.txt`;
  - removed: `asset_set_validation.{h,cpp}`.
- **World:** `scene_instantiation.{h,cpp}`.
- **Runtime:**
  - `bootstrap_config.h`, `init_error.h`, `scene_load.{h,cpp}`,
    `runtime_application.{h,cpp}`, `main.cpp`, `cli.{h,cpp}`,
    `CMakeLists.txt`;
  - new private: `entity_ref_resolution.{h,cpp}`;
  - removed: `scene_manifest.{h,cpp}`.
- **Android:** `android/app/build.gradle`, `src/runtime/android/android_main.cpp`,
  `asset_extraction.{h,cpp}`, `catalog_extraction.{h,cpp}`.
- **Tools:**
  - `asset_cooker/`: `cook_command.{h,cpp}`, `main.cpp`, new
    `guid_mint.{h,cpp}`;
  - `gltf_importer/`: `import_command.{h,cpp}`, `material_import.cpp`,
    `scene_import.{h,cpp}`, `main.cpp`.
- **Assets:**
  - new `assets/asset_catalog.txt`;
  - rewritten: every `assets/scenes/*.scene.txt`, `assets/materials/*.material.txt`,
    `assets/_test_fixtures/*`, `assets/bistro/bistro_overlay.scene.txt`;
  - changed: `assets/CMakeLists.txt`.
- **Tests:**
  - `tests/asset_system/` — new GUID, catalog-source, catalog and
    `EntityRef` tests; updates to source/metadata/cook/load/decode and the
    CMake-declaration tests; removal of `asset_set_validation_tests.cpp`;
  - `tests/world/` — instantiation and boundary scan;
  - `tests/runtime/` — `scene_load`, `bootstrap_config`, `cli`, new
    resolver tests; removal of `scene_manifest_tests.cpp`;
  - `tests/tools/` — cooker modes, determinism, importer;
  - the 49 image-regression files that take scene paths;
  - the R14 integration test.
- **Docs:** `AGENTS.md` (one sentence, M9).

**Not touched:**
- RHI, Renderer, RenderGraph, Shader System, Platform, Vulkan Backend;
- `world::EntityId`, `World`'s public API and every component type;
- mesh source grammar, `.amesh`, `.atex`, the material and environment
  artifact layouts;
- every golden image and sidecar;
- Bistro content;
- no third-party dependency is added or changed.

## Sequencing & Dependencies

M1 → M2 (the catalog source uses the GUID codec) → M3 (the cut-over needs
both, and the migration tool) → M4 (fragments need cut-over cooks) → M5
(Runtime needs the assembled catalog) → M6 → M7 (`EntityRef` resolves
through the map) → M8 (needs M5's packaging) → M9. M6/M7 may overlap M8.

## Verification Checklist

Maps to Spec 0047's Testing & Verification Plan.

- [ ] **R1/R2/R6 primitives (M1):**
  - GUID codecs and every parse error;
  - FNV-1a-128 reference vectors;
  - derivation and key vectors;
  - minted version and distinctness.
- [ ] **R3/R4 (M2, M3):** every catalog-source error; `SourceNotInCatalog`
  and `CatalogTypeMismatch` from cook and import.
- [ ] **R5/R7/R8 formats (M3):**
  - scene v7 / artifact 7 / material v10 round trips;
  - nil and duplicate EntityGuid rejected by cook and by decode;
  - sidecar key mismatch fails each of the four loaders.
- [ ] **R9/R10 assembly (M4):** every error in P13 triggered alone; closure
  contents; relative locations never escape.
- [ ] **R11/R13-assets (M5):**
  - catalog load errors;
  - type and schema checks;
  - cross-scene asset resolution;
  - shared asset resolves to one record.
- [ ] **R12 (M6):** map completeness; wrapper equivalence.
- [ ] **R13-entities (M7):** codec; all four resolution outcomes.
- [ ] **R14 (M9):** rename/move byte-stability test and its negative cases;
  import from a moved file and content root.
- [ ] **R15 (M3):** one migration commit, reviewed; all goldens unchanged
  after it.
- [ ] **Determinism, measured, not assumed:** real-subprocess
  run-twice-and-compare for the cooker (existing, extended), the importer
  (new, M3) and the assembly (new, M4).
- [ ] **Module boundaries:** the AssetSystem ↛ World scan unchanged and
  passing; the new World header scan (M6).
- [ ] **GPU:**
  - full Debug + Release suites at every milestone gate from M3;
  - **every golden byte-identical**, with no re-capture;
  - Vulkan Validation Layers clean;
  - every whitelist scene renders windowed.
- [ ] **Bistro (content-gated `[bistro]`/`content`):**
  - import, cook and assembly succeed;
  - the catalog holds every import record;
  - `bistro_demo` byte-identical;
  - `--scene bistro` renders.
- [ ] **Android:** `assembleDebug` at every gate from M3; emulator run from
  the closure catalog (M8) with screencap + logcat.
- [ ] **Build time:** clean build and catalog-source-edit rebuild, before and
  after, each without and with `content/bistro/` present (whether the edit
  re-triggers the full Bistro import, and its cost), reported in the PR
  (ruling O4).

## Open points — ruled (Joint Human Review, slmao, 2026-09-28, chat)

- **O1 — Content migration at the end of M3** (Order note). **Ruled
  (2026-09-28): as recommended** — over keeping the brief's late slot, which
  would need either red gates or two readable versions per format.
- **O2 — Android code switch in M5, emulator verification in M8** (Order
  note). **Ruled (2026-09-28): as recommended** — over leaving
  `assembleDebug` broken between M5 and M8.
- **O3 — Material `TEXTURE`/`NORMAL_MAP` CMake arguments retired with the
  `*_DEPENDENCIES` lists** (P16). **Ruled (2026-09-28): as recommended** —
  they are the same hand-maintained dependency fact R9 replaces; keeping them
  would leave a second, stale-able source.
- **O4 — Every cook depends on the catalog source** (M3 step 5). **Ruled
  (2026-09-28): as recommended**, with one addition. A catalog-source edit
  re-cooks all hand-authored assets; this is correct by construction, and the
  alternative is a stale GUID in a sidecar until the next clean build.
  - **Addition:** M9's build-time measurement must include `content/bistro/`
    present. It records whether a one-line catalog-source edit re-triggers
    the full Bistro import, and the numbers go in the implementation PR.
  - If the measured cost is too high, narrowing the importer step's stamp
    input to its own catalog-source line is an implementation detail, not a
    Plan change.

## Implementation rulings (slmao, 2026-09-29, chat)

Clarifications raised while implementing M1 (I1–I4) and M2 (I5). None changes a Spec
requirement or ADR decision.

- **I1 — `assetKey()` and `computeAssetId()`.** P2's "reusing the existing
  FNV-1a-64 routine" means reusing the algorithm's implementation, not the
  path-hashing public entry point.
  - M1's direct call to `computeAssetId()` stays as it is; it is not
    reworked now.
  - M4 step 3 deletes the public `computeAssetId(path)`, extracts its
    byte-wise core into an internal helper, and points `assetKey()` at that
    helper.
- **I2 — Sub-key validation.** The derivation functions stay byte-pure,
  under the caller contract their header already states. M3's importer
  validates its own sub-keys:
  - a URI that still contains non-ASCII bytes after normalization is a
    named import error;
  - so is an empty sub-key;
  - neither is ever hashed silently.
- **I3 — Binary codec shape.** M1's `assetGuidFromBytes()` /
  `entityGuidFromBytes()` are P2's binary codec as intended. They reject
  only nil and reuse `GuidParseError::NilGuid`. The 16 bytes are a public
  member, and a default-constructed value is nil and documented as not to be
  relied on.
- **I4 — Details accepted on review:**
  - `--count` only requires a value ≥ 1, with no upper bound;
  - `fnv1a128()` is exposed publicly.
- **I5 — How the migration reaches the overlay** (raised in M2). P20's
  inputs were only the declarations list, and the overlay is an importer
  input, not a declaration — the only undeclared scene or material source
  in `assets/`.
  - `migrate-0047` gains a repeatable `--scene-source=<assets-relative
    path>` for scene sources that are not declarations.
  - The one-time M3 run passes `bistro/bistro_overlay.scene.txt`.
  - The overlay is rewritten to v7 with a minted `guid=` on each node and
    gets no catalog entry, since it is not an asset.

### Rulings raised in M3–M4 (slmao, chat; recorded before M5)

- **I6 — cook-manifest rows carry the catalog id, not a GUID.** Each
  `cook_manifest.txt` row carries `--catalog-id=content:<root>#<sub-key>`
  (required on manifest rows, rejected on the command line). This replaces
  P7's `--guid=` pass-through on manifest lines.
  - GUID derivation stays inside the cooker/library.
  - Fragments use an `{import_dir}` placeholder for locations.
- **I7 — `atlantis_catalog_guid()` lands in M4.** The CMake function moves
  forward from P15 to M4 (function only). The compile definitions that use
  it remain M5.
- **M4 interpretations accepted on review:**
  - an unreadable sidecar reuses `SidecarGuidMismatch`;
  - a fragment's relative location problem is `MalformedFragment`;
  - a duplicate GUID inside one fragment is reported as `DuplicateGuid`;
  - mesh sidecars are read back under both schemas.
- **Stale documentation:** the outdated descriptions in `src/README.md` and
  `tests/README.md` are updated at the end of M5.

## Rollback Plan

Revert the implementation PR as a whole. M3 changes every committed source,
sidecar version and the scene artifact, so partial reverts past M3 leave the
tree inconsistent. Before merge, milestones revert in reverse order on the
branch.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas:
- [ ] Every golden byte-identical at every gate from M3; none re-captured.
- [ ] No transitional code (P11) or migration mode (P20) left in the final
      tree.
- [ ] R14 test, and the three run-twice determinism tests, in the suite.
- [ ] Android emulator run from the closure catalog recorded.
- [ ] AGENTS.md World-dependency sentence updated (Spec Q11).
- [ ] Build-time before/after reported.
