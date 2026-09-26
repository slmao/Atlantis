# ADR 0097: GUID-Keyed Asset Identity, Persistent Entity Identity, and a Build-Generated Asset Catalog

- **Status:** Proposed
- **Date:** 2026-09-27
- **Deciders:** slmao (pending)
- **Acceptance:** pending — Human Review of
  [Spec 0047](../specs/0047-stable-identity-asset-catalog.md)
- **Related Spec:** [Spec 0047: Cross-Session Stable Identity and Asset Catalog](../specs/0047-stable-identity-asset-catalog.md) (`Draft`)
- **Related ADR(s):** supersedes in part
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)
  ("Asset ID: path-derived" and "Collision detection");
  [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
  Decision 1 (location mechanism); and
  [ADR-0094](0094-imported-scene-assembly-build-step-and-authored-overlay.md)
  Decision 2 (its dependency-manifest output). Extends
  [ADR-0053](0053-scene-artifact-format-versioning-and-node-identity.md)
  (scene node records) under
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
  format and no-dependency policy. Leaves
  [ADR-0049](0049-entity-identity-and-handle-invalidation.md) (`EntityId`)
  unchanged.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- ADR-0044 made `AssetId` FNV-1a-64 of the source's logical path: stable
  across sessions, not across a rename or move, with a sidecar-persisted
  random GUID named as the future scheme once a real workflow justified it.
  It also scoped uniqueness to one validator invocation and obliged a future
  registry to re-establish it globally.
- ADR-0054 D1 located a scene's dependencies through a per-scene,
  path-keyed manifest built from CMake dependency lists, explicitly "never a
  global catalog". ADR-0094 D2 added a second producer of that manifest for
  imported scenes.
- The glTF importer (ADR-0083/0084) and the Bistro assembly (ADR-0094) now
  produce 1062 assets whose identities are positions in a file, and 5908
  nodes whose identity ends at cook time (ADR-0053 remaps `node_id`;
  instantiation discards its index→`EntityId` vector). An asset produced by
  one import cannot be referenced by any other scene.
- Maintainer-fixed boundaries: GUID-primary records
  `{ Source, Artifact, Type, Version, Dependencies, Location }`; no asset
  database (no derived-data cache, no automatic rebuild, no runtime-mutable
  catalog); `world::EntityId` unchanged, persistent node identity in the
  asset layer, mapping on the instantiation side; no new third-party
  dependency.
- `AssetId` is embedded in five artifact formats, World's `Renderable`, and
  the Runtime's resource maps. Changing its width ripples well beyond the
  asset layer; changing only its derivation does not.

## Decision

1. **AssetGuid.** An asset's persistent identity is a 128-bit GUID. Text:
   lowercase RFC 9562 `8-4-4-4-12`. Binary: the 16 bytes in text order. Nil
   is invalid. *Minted* GUIDs are version 4 from `std::random_device`, created
   only by an explicit authoring command — a build, cook or import never mints
   one, and a missing GUID is always an error. *Derived* GUIDs (D4) are
   version 8.
2. **`AssetId` is the GUID's 64-bit key.** `AssetId = FNV-1a-64(guid bytes)`,
   with every existing `AssetId` encoding kept. Logical paths stop
   contributing to identity; `computeAssetId(path)` is retired as an identity
   function, and loaders verify `assetId == key(asset_guid)` from the sidecar.
   `0` stays reserved. Key uniqueness and non-zero keys are validated over
   the whole catalog (D6) — the global check ADR-0044 deferred.
3. **One committed catalog source holds root GUIDs.**
   `assets/asset_catalog.txt` (strict, versioned flat text, ADR-0045 wire
   discipline) lists `guid`, `type`, `root` (`assets` | `content`) and logical
   path for every root asset, sorted by (root, path). It is the only place a
   root GUID is written. Cooks and the importer look up their input by
   (root, path); an unregistered or mistyped input is a named error.
   Authored references (scene, material sources) name GUIDs, never paths.
4. **Imported identity is derived.** A sub-asset of an import root is
   `v8(FNV-1a-128(root guid bytes ‖ sub-key))`; an imported node's EntityGuid
   is the same function of the scene's GUID and a node sub-key. Sub-keys are
   positional (glTF indices, texture URIs), fixed by the Plan, and stable for
   pinned content. Import remains byte-deterministic.
5. **EntityGuid is persisted in the asset layer; the mapping lives at
   instantiation.** Every scene node carries an `EntityGuid` (same 128-bit
   form, distinct type): authored in scene source v7, stored in scene
   artifact schema 7, exposed read-only by `ValidatedSceneData`, unique and
   non-nil within its scene (checked by cook and by decode). `node_id` remains
   a file-local label. World gains an instantiation entry point returning the
   `World` and an immutable `SceneEntityMap` (`EntityGuid → EntityId`,
   snapshot at instantiation, owned by the caller). `World`, `EntityId` and
   components store no GUID; liveness stays `World`'s.
6. **The asset catalog is a build output.** Each cook and each import writes
   catalog fragments; one assembly step per build (replacing `--validate-set`)
   merges them into `<build>/asset_catalog.txt`: one record per asset, sorted
   by GUID, with `guid`, `asset_id`, `type`, `source` (`root:path`, plus
   `#sub-key` when derived), `artifact` and `metadata` locations relative to
   the catalog's directory, `artifact_schema`, `source_schema`, `tool`
   version, and the direct-dependency GUIDs *as the cook wrote them*.
   Assembly rejects duplicate GUIDs, duplicate or zero keys, dangling or
   wrongly-typed dependencies (scene → mesh, material; material → texture),
   and declarations without a matching catalog-source entry. Closure catalogs
   (one scene's transitive set, same format) serve packaging. The catalog is
   regenerated deterministically every build, is not a cache, and is never
   written at runtime.
7. **The Runtime resolves through the catalog.** The catalog parser is a
   public Asset System API returning an immutable value (concurrent reads
   safe; no global). The Runtime is configured with a catalog and a scene
   GUID, and resolves every reference by `AssetId`, checking type and
   artifact schema before loading. Per-scene manifests and the CMake
   `*_DEPENDENCIES` lists are retired. ADR-0054's all-or-nothing load, error
   domains and load order are unchanged.
8. **Cross-scene references.** Asset references resolve against the one
   catalog regardless of which scene or import produced the asset. A
   persisted entity reference is the pair `EntityRef { scene AssetGuid,
   EntityGuid }` (text `<scene>/<entity>`, binary 32 bytes), resolved only
   through a loaded instance's `SceneEntityMap` and `World` liveness; failure
   is an explicit error. No scene-grammar field stores an `EntityRef` yet.

## Consequences

### Positive

- A rename or move changes no artifact byte and breaks no reference; a
  forgotten catalog edit fails by name instead of silently re-identifying.
- Global uniqueness, dangling references and type-correct dependencies are
  build-time facts, not Runtime surprises; the stale-dependency-list gap of
  Spec 0015 closes because dependencies come from cooked content.
- One record per asset, whatever uses it; any scene can reference any
  import's output.
- Every `AssetId`-carrying layout except the scene record keeps its shape;
  World and Renderer are untouched.
- Relative locations and closure catalogs remove Android's path rewriting.
- The 128-bit form leaves room for coordination-free minting across
  packages later.

### Negative / Trade-offs

- GUIDs in hand-authored sources are unreadable without a lookup tool.
- Two identifiers exist (GUID persisted, 64-bit key in memory/artifacts);
  a key collision, though negligible, is resolved by re-minting and
  rewriting references.
- Positional import sub-keys do not survive a reordering re-export.
- One large migration: every source reference, every sidecar version, the
  scene artifact, the Runtime's configuration and 66 manifest-consuming
  files.
- The catalog source is a single file every new asset touches.

## Alternatives Considered

- **Path identity with rename redirects:** keeps paths primary, contrary to
  the maintainer's ruling; redirects accumulate.
- **Per-source `.meta` sidecars (Unity):** a file per source, the same
  forget-to-move hazard, uniqueness only by scanning. Better for many
  concurrent authors; revisit if the single file becomes a merge hotspot.
- **GUIDs as CMake arguments:** identity inside build scripts that authoring
  tools would have to edit.
- **128-bit GUIDs in every artifact and in World:** the same stability with
  far wider churn (World components, Runtime maps, five formats).
- **Minted GUIDs for imported sub-assets, kept in a lock file:** 1062
  committed lines for Bistro, and re-import still needs a key to match lines.
- **Paths in sources, resolved through the catalog source at cook time:**
  readable, but a rename again edits every referencing source.
- **EntityGuid stored in World (component or side table):** World would own
  persistence, contrary to the maintainer's boundary and ADR-0049's intent.
- **Per-scene manifests generated as views of the catalog:** two location
  formats for one fact.
- **A full asset database with a derived-data cache:** excluded by scope;
  ADR-0044's future cache key remains the guidance if one is ever needed.
