# ADR 0098: Asset Catalog and Catalog-Based Resolution

- **Status:** Proposed
- **Date:** 2026-09-28 (split from ADR-0097's combined draft of 2026-09-27)
- **Deciders:** slmao (pending)
- **Acceptance:** pending — Human Review of
  [Spec 0047](../specs/0047-stable-identity-asset-catalog.md)
- **Related Spec:** [Spec 0047: Cross-Session Stable Identity and Asset Catalog](../specs/0047-stable-identity-asset-catalog.md) (`Draft`)
- **Related ADR(s):** supersedes in part
  [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
  Decision 1 (location mechanism) and
  [ADR-0094](0094-imported-scene-assembly-build-step-and-authored-overlay.md)
  Decision 2 (its dependency-manifest output). Sibling of
  [ADR-0097](0097-guid-keyed-asset-and-entity-identity.md) (identity): this
  ADR persists the root GUIDs ADR-0097 D1 defines and is the authority for
  the `AssetId` key uniqueness ADR-0097 D2 relies on. Formats follow
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  wire discipline and no-dependency policy.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- ADR-0054 D1 located a scene's dependencies through a per-scene,
  path-keyed manifest (`logicalPath\tartifactPath\tmetadataPath`) built from
  hand-listed CMake `MESH_/MATERIAL_/TEXTURE_DEPENDENCIES`, explicitly "never
  a global catalog". ADR-0094 D2 added a second producer for imported scenes.
- Consequences today: a shared asset appears in every manifest that uses it;
  an asset produced by one import cannot be referenced by another scene; a
  stale dependency list is found only at Runtime load (Spec 0015's disclosed
  gap); paths are absolute build-tree paths, rewritten twice for Android.
- Uniqueness is checked per declared list (`--validate-set`) and per import,
  never across a whole build — ADR-0044 left that to "a future registry".
- ADR-0097 needs a single persisted home for root GUIDs and a whole-build
  uniqueness authority for the 64-bit key.
- Maintainer-fixed boundaries: GUID-primary records
  `{ Source, Artifact, Type, Version, Dependencies, Location }`; no asset
  database (no derived-data cache, no automatic rebuild, no runtime-mutable
  catalog); no new third-party dependency.

## Decision

1. **One committed catalog source holds root GUIDs.**
   `assets/asset_catalog.txt` (strict, versioned flat text) lists `guid`,
   `type` (`mesh`, `texture`, `material`, `scene`, `environment`,
   `gltf_import`), `root` (`assets` | `content`) and logical path for every
   root asset, sorted by (root, path). It is the only place a root GUID is
   written — not CMake, not sidecars, not sources. Cooks and the importer
   look up their input by (root, path); an unregistered or mistyped input is
   a named error, never a minted GUID.
2. **The cooked catalog is a build output, assembled and validated once per
   build.** Each cook writes a one-record catalog fragment; the importer's
   cook-manifest mode writes one fragment for its import in place of its
   dependency manifest. One assembly step per build (replacing
   `atlantis_finalize_asset_validation()`'s `--validate-set`) merges them into
   `<build>/asset_catalog.txt`: one record per asset, sorted by GUID, with
   `guid`, `asset_id`, `type`, `source` (`root:path`, plus `#sub-key` when
   derived), `artifact` and `metadata` locations, `artifact_schema`,
   `source_schema` (or `none`), `tool` version, and the direct-dependency
   GUIDs *as the cook wrote them*. Assembly fails, naming the record, on:
   duplicate GUIDs; duplicate or zero `asset_id` keys; dangling
   dependencies; wrongly-typed dependencies (scene → mesh, material;
   material → texture; nothing else has dependencies); a declaration without
   a matching catalog-source entry. Catalog-source entries with no
   declaration in this build (content-gated) are counted, not errors.
   Locations are `/`-separated, relative to the catalog's directory, and
   never escape it. A closure catalog (one scene's transitive set, same
   format) serves packaging. The catalog is regenerated deterministically
   every build, is not a cache, and is never written at runtime.
3. **The Runtime resolves through the catalog.** The catalog parser is a
   public Asset System API returning an immutable value (concurrent reads
   safe; no global). The Runtime is configured with a catalog path and a
   scene GUID, loads the catalog once, and resolves the scene and every
   dependency by `AssetId`, checking type and artifact schema before
   opening a file. Any scene may reference any cataloged asset, whichever
   scene or import produced it. Per-scene manifests, `scene_manifest.*` and
   the CMake `*_DEPENDENCIES` lists are retired. ADR-0054's all-or-nothing
   load, error domains and load order are unchanged; ADR-0094's single build
   step is unchanged apart from its output.

## Consequences

### Positive

- Global GUID and key uniqueness, dangling references and type-correct
  dependencies become build-time facts; Spec 0015's stale-list gap closes
  because dependencies come from cooked content.
- One record per asset however many scenes use it; any scene can reference
  any import's output.
- A rename is a file move plus one catalog-source line; a forgotten edit
  fails by name.
- Relative locations and closure catalogs remove Android's path rewriting.

### Negative / Trade-offs

- The catalog source is a single file every new asset touches — a merge
  hotspot if many authors work concurrently.
- The Runtime's configuration, the scene whitelist, and 66 manifest-consuming
  files (src, tests, Android build) change.
- One extra assembly step per build, and a whole-build dependency: the
  Runtime depends on every cooked asset target through the catalog.

## Alternatives Considered

- **Per-source `.meta` sidecars (Unity):** a file per source, the same
  forget-to-move hazard, uniqueness only by scanning. Better for many
  concurrent authors; revisit if the single file becomes a merge hotspot.
- **GUIDs as CMake arguments:** identity inside build scripts that
  authoring tools would have to edit.
- **Keep per-scene manifests, generated as views of the catalog:** two
  location formats for one fact.
- **Keep CMake `*_DEPENDENCIES` lists beside cooked dependency metadata:**
  a second source of truth that can go stale.
- **A full asset database with a derived-data cache:** excluded by scope;
  ADR-0044's future cache key remains the guidance if one is ever needed.
