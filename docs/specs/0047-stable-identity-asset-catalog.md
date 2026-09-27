# Spec: Cross-Session Stable Identity and Asset Catalog

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-27
- **Related Plan(s):** none yet — Plan 0047 drafting is authorized by the
  Approval below. **Implementation still awaits its own, separate Joint Human
  Review** of Spec + Plan together, per AGENTS.md's own workflow.
- **Approval:** slmao, 2026-09-28 (chat confirmation; reviewed in this
  Spec's own branch PR) — authorizes drafting Plan 0047; Implementation itself
  still awaits its own, separate Joint Human Review of Spec + Plan together.
  Scope was fixed by the maintainer before drafting (2026-09-27, chat; see
  Goals / Non-Goals); the same review ruled all eleven open questions (Q10,
  the ADR split, earlier the same day). See Risks & Open Questions below.
- **Related ADR(s):** both `Accepted` 2026-09-28, alongside this Spec's own
  Approval —
  [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md) — asset
  and entity identity (AssetGuid, the `AssetId` key, derived import identity,
  EntityGuid and `SceneEntityMap`, `EntityRef`, and the reference-carrying
  format changes); supersedes, in part,
  [ADR-0044](../adr/0044-asset-system-identity-provenance-and-import-methodology.md).
  [ADR-0098](../adr/0098-asset-catalog-and-catalog-based-resolution.md) —
  the catalog source, the build-assembled catalog and its validation, and
  Runtime resolution through it; supersedes, in part,
  [ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md)
  Decision 1 and
  [ADR-0094](../adr/0094-imported-scene-assembly-build-step-and-authored-overlay.md)
  Decision 2's manifest output. See Architectural Impact.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Give every asset a persistent 128-bit **AssetGuid** and every authored scene
node a persistent **EntityGuid**, and replace the per-scene, path-keyed
dependency manifests with one build-generated **asset catalog** whose primary
key is the GUID: one record per asset carrying
`{ Source, Artifact, Type, Version, Dependencies, Location }`. The asset's
identity stops being a hash of its source path (ADR-0044), so renaming or
moving a source no longer changes any artifact byte or breaks any reference.
The 64-bit `AssetId` survives as the in-memory/artifact reference key, now
derived from the GUID. `world::EntityId` is untouched: persistent node
identity lives in the asset layer, and scene instantiation returns an
`EntityGuid → EntityId` map beside the `World` it builds.

## Motivation / Problem Statement

Candidate Backlog 7 (`docs/specs/README.md`) was held back with "no consumer
yet to design against". The glTF importer (Spec 0037) and the Bistro assembly
(Spec 0046, ADR-0094) are that consumer: 1062 generated assets whose
identities are derived from positions in a file, resolved through a manifest
format that was designed for one hand-declared scene.

### Current state (read at line level, 2026-09-27, `origin/main` f748773)

1. **Identity is the path.** `AssetId` is FNV-1a-64 of the normalized logical
   path (`asset_id.h:11-20`, ADR-0044). ADR-0044 discloses that a rename or
   move changes it and names a random-GUID scheme as the future answer
   (`0044-…:107-113`, `:435-444`). The cookers derive every reference from
   paths (`cook_scene.cpp:217`, `:227`; `cook_material.cpp:105-131`), and four
   loaders re-derive identity from the sidecar's source path
   (`load.cpp:57`, `load_texture.cpp:69`, `load_material.cpp:123`,
   `load_environment.cpp:58`). Renaming `meshes/pbr_sphere.mesh.txt`
   therefore changes the mesh artifact's header, every material/scene artifact
   that names it, and every source that references it.
2. **Imported identity is positional.** The importer names meshes
   `meshes/<name>/mesh_<i>_<j>` (`scene_import.cpp:171-173`), materials
   `<name>/materials/<i>.material.txt` (`material_import.cpp:237-239`), and
   sets `node_id = glTF node index + 1` (`scene_import.cpp:244-248`).
3. **Node identity does not survive cooking.** The cooker remaps `node_id`
   to a dense index (ADR-0053); `fromValidatedSceneData()` builds its
   index→`EntityId` vector and discards it (`scene_instantiation.h:9-19`).
   Nothing identifies "the same node" across two sessions, two cooks, or a
   re-import. `EntityId` is explicitly never persisted (ADR-0049,
   `0049-…:670-680`).
4. **Location is per scene and path-keyed.** Each scene has a
   `logicalPath\tartifactPath\tmetadataPath` manifest
   (`scene_manifest.h:47-53`) generated from hand-listed
   `MESH_/MATERIAL_/TEXTURE_DEPENDENCIES` (`src/asset_system/CMakeLists.txt:137-206`)
   or, for Bistro, by the cooker's cook-manifest mode (ADR-0094 D2). A shared
   asset appears in every manifest that uses it; an asset from one import
   cannot be referenced by another scene at all. A stale dependency list is
   found only when the Runtime loads the scene (Spec 0015's disclosed gap,
   `0015-…:699-707`). Paths are absolute build-tree paths; Android rewrites
   them twice (`build.gradle:118-170`, `asset_extraction.h:22-43`).
5. **Uniqueness is invocation-scoped.** `--validate-set` checks one declared
   list (`cook_command.cpp:507`); the Bistro import validates its own set
   separately. ADR-0044 obliges "a future asset registry" to re-establish
   global uniqueness on its own terms (`0044-…:264-270`).
6. **Versions are per file, never in one place.** Each artifact and sidecar
   carries its own `schema_version`; nothing records, per asset, which type
   and schema a reference must resolve to until the loader opens the file.

### Scale

| Quantity | Value |
|---|---|
| Asset declarations in `assets/CMakeLists.txt` | 109 (8 mesh, 14 texture, 57 material, 27 scene, 2 environment, 1 imported scene) |
| Hand-authored scene nodes (incl. the 61-node Bistro overlay, fixtures) | 246 |
| Bistro import (content-gated) | 1062 assets, 5908 nodes + 61 overlay |
| Files referencing a dependency manifest (src, tests, Android build) | 66 |

## Goals

Scope as fixed by the maintainer (not to be widened):

- **AssetGuid** — a persistent, rename/move-stable asset identity.
- **EntityGuid persistence** — a persistent identity for every authored or
  imported scene node, carried through source, artifact and validated data.
- **Asset catalog** — one GUID-keyed record per asset.
- **GUID ↔ source / artifact mapping** — in both directions, from one place.
- **Dependency metadata** — each record lists its direct dependencies;
  closure and dangling references are checked at build time.
- **Schema / version metadata** — each record states its type, schemas and
  producing tool.
- **Rename/move stability** — renaming or moving a source changes no
  artifact byte and breaks no reference.
- **Cross-scene reference** — any scene can reference any cataloged asset;
  a persisted form for referring to an entity of a given scene.

## Non-Goals

- **A full Asset Database:** no derived-data cache, no content-addressed
  store, no automatic or background rebuild, no file watching, no hot
  reload, no runtime-mutable catalog. The catalog is a build output,
  regenerated deterministically, read-only at runtime. Re-import stays
  CMake's incremental build (ADR-0044, unchanged).
- **Changing `world::EntityId`** or storing any GUID inside `World`.
- **Serializing `World` state** (save games), replication/network identity,
  identity for entities created at runtime.
- **Schema migration.** Version metadata is recorded to gate a future
  migration; no old-version reader is added (ADR-0045's single-version
  policy continues).
- **Sub-scene / prefab instancing, loading several scenes at once**, or an
  Editor. The Runtime still loads one scene.
- **Name-based import keys** that survive a re-export which reorders or
  inserts into the glTF (see Risks).
- **New third-party dependencies**, non-ASCII paths, packages/UGC namespaces.

## Requirements

### Functional

- **R1 — AssetGuid (ADR-0097 D1).** A 128-bit value; canonical text is
  lowercase RFC 9562 form (`8-4-4-4-12`); binary is the 16 bytes in text
  order (a byte string, no endianness). The nil GUID is invalid everywhere.
  A **minted** GUID is RFC 9562 version 4 from `std::random_device`,
  produced only by an explicit authoring-tool command — never by a build,
  cook or import. A **derived** GUID (R6) is version 8.
- **R2 — AssetId redefined (ADR-0097 D2; uniqueness authority ADR-0098
  D2).** `AssetId` stays a 64-bit value with its current binary encodings,
  redefined as FNV-1a-64 over the GUID's 16 bytes. Logical paths no longer
  participate in identity. `0` stays reserved ("none"); a GUID whose key is
  `0` or collides with another record's key fails catalog assembly (R9).
- **R3 — Catalog source (ADR-0098 D1).** A committed, strict, versioned flat
  text file `assets/asset_catalog.txt` is the only home of root-asset GUIDs:
  one line per asset — GUID, type (`mesh`, `texture`, `material`, `scene`,
  `environment`, `gltf_import`), root (`assets` or `content`), logical path —
  in ascending (root, path) order. GUIDs are not duplicated into CMake,
  sidecars or sources.
- **R4 — Cooks and imports take identity from the catalog source (ADR-0098
  D1).** Every cook and the importer look up their input's GUID by (root,
  logical path). A declared asset with no entry, or an entry of another
  type, is a named error — never an automatically minted GUID.
- **R5 — Authored references by GUID (ADR-0097 D3).** Scene source v7
  references meshes and materials by AssetGuid; material source v10
  references textures by AssetGuid. Paths no longer appear in references.
  The importer writes GUIDs into what it generates.
- **R6 — Derived identity for imports (ADR-0097 D4).** An imported sub-asset's
  GUID is derived from its import root's GUID and a sub-key
  (`mesh/<i>/<j>`, `material/<i>`, `texture/<uri>`, `fallback/white`,
  `scene`; exact strings fixed by the Plan): FNV-1a-128 over the root's 16
  bytes followed by the sub-key's ASCII bytes, version/variant bits set to 8.
  Imported nodes' EntityGuids are derived the same way from the scene's GUID
  and `node/<glTF index>` or a synthetic key. Import stays byte-deterministic.
- **R7 — EntityGuid persistence (ADR-0097 D5; formats D3).** Every scene
  node carries a mandatory EntityGuid: authored as `guid=` in scene source
  v7 (the overlay included), written into scene artifact schema 7 (16 bytes
  per node record), exposed read-only by `ValidatedSceneData`. `node_id`
  stays a file-local structural label. Cook and decode each reject a nil or
  duplicate EntityGuid within one scene. `EntityGuid` is a distinct type
  from `AssetGuid`.
- **R8 — Metadata sidecars carry the GUID (ADR-0097 D3).** Mesh, texture,
  material, environment and scene sidecars gain an `asset_guid` line (each
  sidecar's version bumps). Loaders check `assetId == key(asset_guid)` in
  place of `assetId == computeAssetId(sourceLogicalPath)`; the source path
  becomes provenance only.
- **R9 — Cooked catalog (ADR-0098 D2).** Every cook writes a one-record
  catalog fragment; the importer's cook-manifest mode writes one fragment for
  its whole import (replacing its dependency manifest). One assembly step per
  build — replacing `atlantis_finalize_asset_validation()`'s `--validate-set`
  — merges them into `<build>/asset_catalog.txt`, one record per asset,
  sorted by GUID:
  `guid`, `asset_id`, `type`, `source` (`<root>:<path>` or, when derived,
  `<root>:<path>#<sub-key>`), `artifact` and `metadata` locations,
  `artifact_schema`, `source_schema` (or `none`), `tool` (producing tool's
  version), and the direct-dependency GUID list. Assembly fails, naming the
  record, on: a duplicate GUID; a duplicate or zero `asset_id`; a dependency
  with no record; a dependency of the wrong type (scene → mesh, material;
  material → texture; nothing else has dependencies); a declaration whose
  catalog-source entry is missing or mistyped. Catalog-source entries with no
  declaration in this build (content-gated) are counted, not errors.
  Dependencies come from what the cook actually wrote, never from a
  hand-maintained list.
- **R10 — Location (ADR-0098 D2).** Record locations are `/`-separated paths
  relative to the catalog file's directory, never escaping it. A **closure
  catalog** — the same format restricted to one scene's transitive
  dependencies — can be written for packaging (Android).
- **R11 — Runtime resolves through the catalog (ADR-0098 D3).** The catalog
  parser is a public Asset System API producing an immutable value (safe for
  concurrent reads; no global). The Runtime is configured with a catalog path
  and a scene GUID, loads the catalog once, and resolves the scene and every
  dependency by `AssetId`, checking type and artifact schema before opening a
  file. Per-scene dependency manifests, `scene_manifest.*` and the CMake
  `*_DEPENDENCIES` lists are retired. ADR-0054's transactional,
  all-or-nothing load and load order are unchanged.
- **R12 — Instantiation-side mapping (ADR-0097 D5).** A World-module entry
  point returns the instantiated `World` together with an immutable
  `SceneEntityMap` (`EntityGuid → EntityId`, one entry per node, a snapshot
  taken at instantiation). `World`, `EntityId` and every component are
  unchanged. `fromValidatedSceneData()` remains, returning `World` alone.
- **R13 — Cross-scene reference.**
  - *Assets (ADR-0098 D3):* any scene may reference any cataloged asset,
    including another import's sub-asset; one asset has one record however
    many scenes use it.
  - *Entities (ADR-0097 D6):* the persisted form is `EntityRef { AssetGuid
    scene; EntityGuid entity; }` (text `<scene guid>/<entity guid>`, binary
    32 bytes). It resolves only against a loaded instance of that scene, through
    its `SceneEntityMap` and `World` liveness; an unknown scene, unknown
    entity or dead entity is an explicit error, never a null. No scene-grammar
    field stores an `EntityRef` in this Spec.
- **R14 — Rename/move stability (the acceptance property; ADR-0097 D1–D3,
  ADR-0098 D1–D2).** Renaming or moving any source (with its one
  catalog-source line updated) leaves every artifact byte-identical; only
  sidecar source-path lines and catalog `source`/location fields change.
  Moving the glTF or the content root changes no imported GUID. Moving a
  source without updating the catalog source fails the build by name;
  copying a source without minting a new GUID fails as a duplicate.
- **R15 — Migration (ADR-0097 D3, ADR-0098 D1).** Every committed source,
  the overlay and the catalog source are migrated in one reviewed,
  mechanical change; old source, artifact and sidecar versions are rejected
  afterwards.

### Non-functional

- **Determinism:** cooking, importing and catalog assembly are each
  byte-identical across two runs (the ADR-0044 empirical discipline).
- **Build time:** assembly over ~1.2 k records is text work; the Bistro
  step's time is measured, not gated.
- **Memory:** a catalog of ~1.2 k records is resident for the Runtime's
  lifetime; no gate.
- **Portability:** identical GUIDs, keys and catalogs on Windows and Android;
  no host path, endianness or filesystem case dependence.
- **Dependencies:** C++ standard library only (`std::random_device`, a
  hand-rolled FNV-1a-128).
- **Rendering:** no pixel changes; every golden stays byte-identical.

## Proposed Design

```
authoring (committed)             build (generated)                 runtime
assets/asset_catalog.txt ──┐
 guid ↔ (root, path, type) │   cook / import ──► artifact + sidecar(asset_guid)
scene v7: guid=, mesh=<g>  ├─►        └─────────► fragment ─┐
material v10: texture=<g>  │                                 ▼
overlay (scene v7) ────────┘            assemble ──► <build>/asset_catalog.txt ──► AssetCatalog (immutable)
                                        (validate)   (+ closure catalogs)            │ AssetId → record
                                                                                     ▼
                                     ValidatedSceneData ─► instantiateScene ─► World + SceneEntityMap
```

- **(a) Where GUIDs live — one committed catalog source (Q1).** One
  reviewable file is the identity registry; a rename is a file move plus one
  line edit; a forgotten edit fails loudly (unregistered source + dangling
  entry). Recommended over per-source `.meta` sidecars (a second file per
  source, same forget-to-move hazard, uniqueness needs a directory scan) and
  over GUIDs as CMake arguments (identity in build scripts that tools would
  have to edit).
- **(b) What references carry at runtime — the 64-bit key (Q3).** Deriving
  `AssetId` from the GUID keeps every binary layout that holds an `AssetId`
  (mesh/texture/material/environment artifacts, scene references, World's
  `Renderable`, Runtime's maps) unchanged in shape. Uniqueness of the key is
  now checked over the whole catalog, which is the global check ADR-0044
  deferred. Only the scene artifact changes layout, for EntityGuid.
- **(c) Authored references — GUID only (Q4).** A path reference in a source
  is exactly what breaks on rename. Readability is recovered by tooling, not
  by a path hint that would go stale.
- **(d) Imports — derived, not minted (Q5).** One minted GUID per imported
  file (a catalog-source line); everything beneath it is derived, so the
  import stays deterministic and needs no 1062-line lock file. Sub-keys are
  positional — the same stability the pinned content already has.
- **(e) Tooling.** A cooker mode mints GUIDs on request (`--kind=mint-guid`,
  name fixed by the Plan) and one prints a catalog record for a GUID or path;
  the migration (R15) is a one-off script or mode, recorded in the Plan.
- **(f) Android.** Relative locations plus a closure catalog let packaging
  copy files with their relative layout; the manifest rewrite in Gradle and
  `extractSceneManifest()` go away. Mechanics are the Plan's.

## Architectural Impact

Yes — two ADRs, split by ruling Q10:

- **[ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md)
  (`Accepted`) — identity.** D1 AssetGuid; D2 `AssetId` as the GUID's 64-bit
  key (its uniqueness enforced by ADR-0098 D2); D3 GUID references and the
  reference-carrying format changes — scene source v6 → v7, scene artifact
  6 → 7 (node record 152 → 168 bytes), material source 9 → 10, an
  `asset_guid` line in every metadata sidecar (+1 each); D4 derived import
  identity; D5 EntityGuid and the instantiation-side `SceneEntityMap` (World
  unchanged; extends ADR-0053); D6 `EntityRef`. **Supersedes ADR-0044's
  "Asset ID: path-derived" and invocation-scoped "Collision detection"
  sections**; ADR-0044's re-import, metadata-provenance and determinism
  decisions stand.
- **[ADR-0098](../adr/0098-asset-catalog-and-catalog-based-resolution.md)
  (`Accepted`) — catalog and resolution.** D1 the catalog source
  `assets/asset_catalog.txt` as sole home of root GUIDs; D2 the
  build-assembled catalog, its record fields, validation, relative locations
  and closure catalogs; D3 Runtime resolution through the catalog (including
  asset-level cross-scene references). **Supersedes ADR-0054 Decision 1's
  location mechanism** (per-scene manifest, CMake dependency lists, "never a
  global catalog") **and ADR-0094 Decision 2's dependency-manifest output**
  (now a catalog fragment); ADR-0054's transactional load and ADR-0094's
  build step otherwise stand.

Public API changes: new `asset_guid.h`, `entity_ref.h` (ADR-0097) and
`asset_catalog.h` (ADR-0098) in Asset System; `instantiateScene()`/
`SceneEntityMap` in World (ADR-0097 D5); `BootstrapConfig` and the scene
whitelist in Runtime (ADR-0098 D3); new cooker and importer options. No RHI,
Renderer, RenderGraph, Shader System or Platform change; the AssetSystem →
World include ban (ADR-0053) holds.

## Alternatives Considered

- **Keep path identity, add rename redirects** (Unreal-style): paths remain
  the key, every rename leaves a redirect record. Rejected: the maintainer
  ruled GUID-primary; redirects accumulate and are themselves path-keyed.
- **Content-hash identity:** changes on every edit (ADR-0044 already rejected
  it as identity).
- **128-bit GUIDs in every artifact and in World's `Renderable`:** one ID
  instead of two, at the cost of changing every artifact layout, World's
  component types and Runtime's maps for no behavioral gain (Q3).
- **Per-source `.meta` sidecars / GUIDs in CMake:** see (a).
- **A minted-GUID lock file per import:** 1062 committed lines for Bistro,
  and a re-import must match old sub-assets to lines anyway (Q5).
- **Keep per-scene manifests as a generated view of the catalog:** two
  location formats for one fact (Q6).
- **Store EntityGuids in World (a component or side table):** violates the
  maintainer's boundary; World would own persistence semantics.

## Testing & Verification Plan

Following [testing-strategy.md](../process/testing-strategy.md); each item
maps to the requirements it proves.

- **GPU-independent unit tests (`tests/asset_system/`, `tests/world/`,
  `tests/runtime/`, `tests/tools/`):**
  - GUID text/binary round trip; rejection of nil, uppercase, malformed text;
    minted GUIDs carry version 4 and differ; derivation and FNV-1a-128
    known-answer vectors; `AssetId` key vectors (R1, R2, R6).
  - Catalog-source grammar: order, duplicates, unknown type/root (R3).
  - Every assembly error in R9, each triggered alone (injected keys for the
    collision and zero-key cases, as `validateAssetSet` does today); closure
    extraction; relative locations never escaping (R9, R10).
  - Scene v7 / artifact 7 and material v10 round trips; duplicate and nil
    EntityGuid rejected by cook and by decode; `ValidatedSceneData` exposes
    the GUID per node (R5, R7).
  - Sidecar `asset_guid` check: a key mismatch fails each loader (R8).
  - `instantiateScene()`: map size equals node count and every entry names
    that node's entity; `fromValidatedSceneData()` yields the same World
    (R12). `EntityRef` codec; resolution of a matching, foreign-scene,
    unknown and destroyed entity (R13).
  - Runtime catalog load: unreadable, malformed, type mismatch, schema
    mismatch, unresolved dependency — each a named error (R11).
- **Rename/move test (R14, the headline):** cook a mesh, texture, material
  and two scenes that share the mesh in a temporary tree; assemble; rename
  and move the mesh and texture sources and update the catalog source;
  re-cook; every artifact byte-identical, catalog differs only in `source`
  and location fields, and both scenes still resolve. Move without the
  catalog edit → named error; copy with the same GUID → duplicate error.
- **Import stability (R6, R13):** import a builder-generated glTF twice
  (byte-identical), then from a moved file and content root (same GUIDs); a
  hand-authored scene referencing one imported mesh and material resolves
  through the assembled catalog.
- **Determinism:** two cooks, two imports, two assemblies → byte-identical.
- **Module boundaries:** the existing AssetSystem ↛ World scan still passes;
  World headers mention `EntityGuid` only in `scene_instantiation.h`.
- **Regression (GPU):** full Windows Debug and Release suites; every golden
  byte-identical with no re-capture; Validation Layers clean.
- **Bistro (content-gated `[bistro]`):** import + cook + assembly succeed,
  the catalog holds every import record, the Bistro golden is
  byte-identical, build time before/after reported.
- **Android:** default scene on the emulator from a closure catalog (the
  Plan 0034 M6 pattern): screencap + logcat; the emulator Validation-Layer
  gap is inherited.
- **Migration (R15):** the migration diff is reviewed as one change; after
  it, every scene renders byte-identically.

## Risks & Open Questions

All eleven questions were ruled by Human Review (slmao, 2026-09-28, chat
confirmation): Q10 as recorded below, every other one as its recommendation.

- **Q1 — Home of asset GUIDs.** **Ruled (2026-09-28):** one committed
  catalog source (R3). Alternatives: per-source sidecars; CMake arguments.
  Reopen if concurrent multi-author edits make the one file a merge hotspot.
- **Q2 — GUID form.** **Ruled (2026-09-28):** 128-bit, RFC 9562 text, v4
  minted / v8 derived. Alternative: 64-bit random (smaller, but no room for
  coordination-free minting once packages or UGC exist).
- **Q3 — Runtime reference key.** **Ruled (2026-09-28):** keep 64-bit
  `AssetId`, redefined as FNV-1a-64 of the GUID bytes (R2). Alternative:
  128-bit everywhere.
- **Q4 — Authored references.** **Ruled (2026-09-28):** GUID only (R5), with
  a lookup tool. Alternative: keep paths in sources, resolved through the
  catalog source at cook time — readable, but a rename again means editing
  every referencing source. This is the main readability cost of the Spec.
- **Q5 — Imported identity.** **Ruled (2026-09-28):** derived from the root
  GUID and a positional sub-key (R6). Alternative: a committed lock file of
  minted GUIDs.
- **Q6 — Per-scene manifests and CMake `*_DEPENDENCIES`.** **Ruled
  (2026-09-28):** retire both; dependencies come from cooked content,
  checked at assembly (R9, R11) — which also closes Spec 0015's stale-list
  gap.
- **Q7 — Runtime configuration.** **Ruled (2026-09-28):** `BootstrapConfig`
  carries a catalog path and a scene GUID; whitelist entries map a name to a
  GUID; locations relative to the catalog (R10, R11).
- **Q8 — Cross-scene entity references.** **Ruled (2026-09-28):** the
  `EntityRef` form, codec and resolution rule only, no grammar field (R13).
  Alternatives: defer entirely; or let the overlay parent to imported nodes
  by EntityGuid (a real consumer, but it changes ADR-0094 D3 — a separate
  decision).
- **Q9 — `fromValidatedSceneData()`.** **Ruled (2026-09-28):** keep it
  beside the new entry point to avoid unrelated churn (R12).
- **Q10 — One ADR or two.** **Ruled (2026-09-28): two.** Identity is
  [ADR-0097](../adr/0097-guid-keyed-asset-and-entity-identity.md) (with the
  reference-carrying format changes); catalog and resolution are
  [ADR-0098](../adr/0098-asset-catalog-and-catalog-based-resolution.md). The
  dependency between them — ADR-0097 D2's key uniqueness rests on ADR-0098
  D2's assembly check — is stated by cross-links in both, not by merging
  them.
- **Q11 — Pointers in superseded ADRs.** **Ruled (2026-09-28):** on each
  ADR's acceptance, add a one-line "superseded in part by" pointer (metadata
  only, decisions untouched) to the superseded ADR's header — ADR-0044 ←
  ADR-0097; ADR-0054 and ADR-0094 ← ADR-0098 — and update AGENTS.md's
  World-dependency sentence in the implementation PR. The three pointers
  were added with this Approval; the AGENTS.md sentence remains for the
  implementation PR.
- **Risk — positional import keys.** A re-exported glTF that reorders meshes,
  materials or nodes changes derived GUIDs. So does one that **inserts** an
  element before existing ones: every index after the insertion point
  shifts, so every derived GUID after it changes even though those assets
  did not. Harmless for SHA-256-pinned content; a name- or content-matching
  key is future work.
- **Risk — migration size.** 109 catalog entries, 246 node GUIDs, all scene
  and material references, 66 manifest-consuming files. Mechanical, but one
  large diff; the byte-identical goldens are its safety net.
- **Risk — `std::random_device` quality.** Minting runs only in a Windows
  authoring tool (MSVC: OS entropy); a test checks distinctness and version
  bits.
- **Risk — debugging without path-recomputable IDs.** Mitigated by the
  lookup mode and by `source` in every catalog record.

## Out of Scope / Future Work

- A derived-data cache or asset database; hot reload; runtime catalog
  mutation.
- Schema migration readers; World save/load; network identity.
- Stable import keys across re-export; prefab/sub-scene instancing; loading
  several scenes; a scene-grammar field holding an `EntityRef`.
- Packages, UGC and multi-repository GUID spaces (the 128-bit form leaves
  room for them).
