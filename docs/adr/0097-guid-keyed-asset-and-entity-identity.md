# ADR 0097: GUID-Keyed Asset and Entity Identity

- **Status:** Proposed
- **Date:** 2026-09-27 (split from the combined draft 2026-09-28)
- **Deciders:** slmao (pending)
- **Acceptance:** pending — Human Review of
  [Spec 0047](../specs/0047-stable-identity-asset-catalog.md)
- **Related Spec:** [Spec 0047: Cross-Session Stable Identity and Asset Catalog](../specs/0047-stable-identity-asset-catalog.md) (`Draft`)
- **Related ADR(s):** supersedes in part
  [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md)
  ("Asset ID: path-derived" and "Collision detection"). Sibling of
  [ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md) (catalog
  and resolution), which persists the root GUIDs this ADR defines and is the
  authority for `AssetId` key uniqueness. Extends
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
  across sessions, not across a rename or move, with a random GUID named as
  the future scheme once a real workflow justified it. It scoped uniqueness
  to one validator invocation and obliged a future registry to re-establish
  it globally.
- The glTF importer (ADR-0083/0084) and the Bistro assembly (ADR-0094)
  produce 1062 assets whose identities are positions in a file, and 5908
  nodes whose identity ends at cook time: ADR-0053 remaps `node_id` to a
  dense index, and instantiation discards its index→`EntityId` vector.
- Every authored reference (scene → mesh/material, material → texture) is a
  path, and four loaders re-derive identity from the sidecar's source path;
  a rename therefore rewrites artifacts and breaks references.
- `AssetId` is embedded in five artifact formats, World's `Renderable`, and
  the Runtime's resource maps. Changing its width ripples well beyond the
  asset layer; changing only its derivation does not.
- Maintainer-fixed boundaries: `world::EntityId` unchanged, persistent node
  identity in the asset layer, mapping on the instantiation side; no new
  third-party dependency.

## Decision

1. **AssetGuid.** An asset's persistent identity is a 128-bit GUID. Text:
   lowercase RFC 9562 `8-4-4-4-12`. Binary: the 16 bytes in text order (a
   byte string, no endianness). Nil is invalid. *Minted* GUIDs are version 4
   from `std::random_device`, created only by an explicit authoring command —
   a build, cook or import never mints one, and a missing GUID is always an
   error. *Derived* GUIDs (D4) are version 8. Root GUIDs are persisted in the
   catalog source ([ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md)
   D1), nowhere else.
2. **`AssetId` is the GUID's 64-bit key.** `AssetId = FNV-1a-64(guid bytes)`,
   with every existing `AssetId` encoding kept. Logical paths stop
   contributing to identity; `computeAssetId(path)` is retired as an identity
   function. `0` stays reserved ("none"). Keys are not unique by
   construction: uniqueness and non-zero keys are enforced over the whole
   build by catalog assembly
   ([ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md) D2) — the
   global check ADR-0044 deferred. A collision is resolved by re-minting.
3. **References name GUIDs; the reference-carrying formats change
   accordingly.** Authored references are AssetGuids, never paths, and carry
   no path hint:
   - scene source v6 → **v7**: `mesh=` / `material=` take AssetGuids; each node
     gains a mandatory `guid=` (D5);
   - scene artifact schema 6 → **7**: a 16-byte `entity_guid` per node record
     (152 → 168 bytes); mesh/material references stay `AssetId`;
   - material source 9 → **10**: texture, normal-map and emissive-texture
     references take AssetGuids; the material artifact layout is unchanged;
   - every metadata sidecar (mesh, texture, material, environment, scene)
     gains an `asset_guid` line, each sidecar's version +1. Loaders check
     `assetId == key(asset_guid)` in place of
     `assetId == computeAssetId(sourceLogicalPath)`; the source path becomes
     provenance only.

   Old versions are rejected (ADR-0045's single-version policy); the
   importer writes the new forms.
4. **Imported identity is derived.** A sub-asset of an import root is
   `v8(FNV-1a-128(root guid bytes ‖ sub-key))`; an imported node's EntityGuid
   is the same function of the scene's GUID and a node sub-key. Sub-keys are
   positional (glTF indices, texture URIs), fixed by the Plan, and stable for
   pinned content. Import remains byte-deterministic.
5. **EntityGuid is persisted in the asset layer; the mapping lives at
   instantiation.** Every scene node carries an `EntityGuid` (the 128-bit
   form of D1, a distinct type from `AssetGuid`): authored in scene source
   v7, stored in scene artifact schema 7 (D3), exposed read-only by
   `ValidatedSceneData`, unique and non-nil within its scene (checked by cook
   and by decode). `node_id` remains a file-local label. World gains an
   instantiation entry point returning the `World` and an immutable
   `SceneEntityMap` (`EntityGuid → EntityId`, a snapshot at instantiation,
   owned by the caller). `World`, `EntityId` and components store no GUID;
   liveness stays `World`'s. `fromValidatedSceneData()` is kept.
6. **Persisted entity references.** A reference to an entity of a scene is
   the pair `EntityRef { scene AssetGuid, EntityGuid }` (text
   `<scene>/<entity>`, binary 32 bytes). It resolves only through a loaded
   instance of that scene — its `SceneEntityMap` and `World` liveness — and an
   unknown scene, unknown entity or dead entity is an explicit error, never a
   null. No scene-grammar field stores an `EntityRef` yet. (Asset-level
   cross-scene references are catalog resolution,
   [ADR-0098](0098-asset-catalog-and-catalog-based-resolution.md) D3.)

## Consequences

### Positive

- A rename or move changes no artifact byte and breaks no reference.
- Every `AssetId`-carrying layout except the scene node record keeps its
  shape; World and Renderer are untouched.
- A scene node is recognisable across sessions, cooks and re-imports of the
  same content, without World owning any persistence.
- The 128-bit form leaves room for coordination-free minting across
  packages later.

### Negative / Trade-offs

- GUIDs in hand-authored sources are unreadable without a lookup tool.
- Two identifiers exist (GUID persisted, 64-bit key in memory/artifacts); a
  key collision, though negligible, costs a re-mint and a reference rewrite.
- Positional import sub-keys do not survive a re-export that reorders the
  glTF or inserts an element before existing ones — every derived GUID after
  the change moves, even for untouched assets.
- Every scene and material source, every sidecar version and the scene
  artifact change in one migration.
- Key uniqueness depends on ADR-0098's assembly step; without it, D2 is not
  safe.

## Alternatives Considered

- **Path identity with rename redirects:** keeps paths primary, contrary to
  the maintainer's ruling; redirects accumulate.
- **Content-hash identity:** changes on every edit (ADR-0044 already rejected
  it as identity).
- **128-bit GUIDs in every artifact and in World:** the same stability with
  far wider churn (World components, Runtime maps, five formats).
- **64-bit random GUIDs:** smaller, no room for coordination-free minting
  once packages or UGC exist.
- **Minted GUIDs for imported sub-assets, kept in a lock file:** 1062
  committed lines for Bistro, and re-import still needs a key to match lines.
- **Paths in sources, resolved to GUIDs at cook time:** readable, but a
  rename again edits every referencing source.
- **EntityGuid stored in World (component or side table):** World would own
  persistence, contrary to the maintainer's boundary and ADR-0049's intent.
