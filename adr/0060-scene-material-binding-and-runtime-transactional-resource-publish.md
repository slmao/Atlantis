# ADR 0060: Scene Material Binding and Runtime Transactional Resource Publish

- **Status:** Proposed
- **Date:** 2026-08-26
- **Deciders:** (pending Human Review)
- **Related Spec:** [specs/0018-material-asset-scene-binding-foundation.md](../specs/0018-material-asset-scene-binding-foundation.md) (`Draft`)
- **Related ADR(s):**
  [ADR-0048](0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](0051-world-to-renderer-extraction-and-asset-resolution-boundary.md)
  (World/Scene module boundary and World-to-Renderer extraction — this
  ADR extends the exact mesh-resolution pattern ADR-0051 already
  establishes to a second asset kind, without changing either ADR's
  own module-boundary decision),
  [ADR-0052](0052-scene-asset-module-boundary-and-ownership.md)–[ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
  (Scene Asset module boundary, data format, and transactional
  instantiation contract — this ADR widens the data format ADR-0053
  governs and extends the transactional contract ADR-0054 governs, in
  kind, not in structure),
  [ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)
  (Material Asset's own module boundary and format — this ADR consumes
  that asset type; it does not redefine it).

## Context

[ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)
gives Asset System a Material asset type. On its own, that asset type
has no consumer: nothing in the Scene Asset format, `World`, or Runtime
today can name, resolve, load, or draw with one. Three concrete,
evidence-based facts fix the shape of the problem this ADR solves:

- `atlantis::asset_system::DecodedRenderable` and
  `atlantis::world::Renderable` are each, today, exactly `{ AssetId
  meshAsset = 0; }` — confirmed by reading both files in full. Neither
  has a material or texture field of any kind, and the scene binary
  artifact's own per-node record is a fixed 72 bytes with no spare
  capacity — a genuine schema version bump is required to add a
  material reference, not a reinterpretation of existing bytes.
- Runtime's own real (non-test-fixture) `runFrame()` builds exactly one
  `Material` for the entire process, lazily, and binds it to every
  `DrawItem` regardless of entity: `item.material = &*material_;`
  unconditionally, confirmed directly in
  `runtime_application.cpp:398-420`. There is no per-entity material
  selection mechanism today, and no `SampledTexture`/`Sampler`
  construction anywhere in Runtime's own production code.
- Runtime's scene-loading pipeline
  (`RuntimeApplication::initializeSteps()` → `loadAndInstantiateScene()`)
  already has a real, working, tested shape for exactly this kind of
  problem — resolve a scene's declared `AssetId` references against a
  build-tree-private manifest, load each exactly once (deduplicated by
  `AssetId`, via `meshResourceMap_`, an `std::unordered_map<AssetId,
  Mesh>`), instantiate `World` infallibly via
  `fromValidatedSceneData()`, then publish `world_`/`meshResourceMap_`
  together as one atomic step, proven `noexcept` at compile time by two
  `static_assert`s rather than argued in prose. This mechanism was
  built once, generically enough in spirit, to extend to a second
  resource kind — the real design question is whether to extend it or
  build something new.

The real design question this ADR settles: how does a scene node
reference an optional material, how does that reference survive
version-bumped serialization without breaking every existing scene
asset's own rendering, how does Runtime resolve/load/construct the
resulting GPU resources without a general Asset Catalog, and how does
the existing atomic-publish/ownership-order contract extend to cover
them.

## Decision

**`Renderable` (both `DecodedRenderable` and `world::Renderable`) gains
an optional material `AssetId` reference. The Scene Asset format bumps
version to carry it, with no dual-version reader. Runtime's existing
per-scene manifest, resolution order, and atomic-publish mechanism
extend, in kind, to resolve, load, and publish materials and their own
texture dependencies alongside meshes — with a hardcoded fallback to
today's single Material whenever a node names no material asset, so
every currently-authored scene keeps rendering exactly as it does
today.**

1. **`Renderable` widening:**
   ```cpp
   // atlantis::asset_system::DecodedRenderable
   struct DecodedRenderable {
     atlantis::asset_system::AssetId meshAsset = 0;
     std::optional<atlantis::asset_system::AssetId> materialAsset;
   };

   // atlantis::world::Renderable
   struct Renderable {
     atlantis::asset_system::AssetId meshAsset = 0;
     std::optional<atlantis::asset_system::AssetId> materialAsset;
   };
   ```
   `std::nullopt` means "no material scene binding for this node" —
   the same optionality convention `Camera`/`activeCameraIndex`/
   `parentNodeId` already use throughout this exact pipeline, never a
   sentinel value. `fromValidatedSceneData()` copies
   `n.renderable->materialAsset` into `Renderable::materialAsset` with
   the same trivial, infallible field-copy shape it already uses for
   `meshAsset` (`scene_instantiation.cpp:30-32`'s own pattern, widened
   by one field) — `atlantis::world::World`'s own module boundary,
   dependency closure (`Atlantis::Core` + `Atlantis::AssetSystem`, for
   `AssetId` only), and construction of zero Renderer/RHI types all
   remain exactly as [ADR-0048](0048-world-scene-module-boundary-and-ownership.md)
   already establishes.
2. **Scene authoring grammar:** `atlantis_scene_source_version: 1 → 2`.
   A node may optionally carry `material=<logical path>`, resolved to
   an `AssetId` by `cookScene()` the same way `mesh=<logical path>`
   already is (`cookScene()`'s own existing `normalizeLogicalPath()` +
   `computeAssetId()` step, applied to one more optional token). A
   version-1 source is rejected outright with the existing
   `SceneSourceParseError::UnknownSourceVersion` — no dual-version
   reader, matching [ADR-0058](0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)'s
   own precedent for the mesh format exactly.
3. **Scene binary artifact:** schema version bump; the per-node record
   widens to carry a `has_material` flag and a `material_asset_id`
   (little-endian `u64`), encoded/decoded with the exact same explicit
   shift/mask discipline the existing `has_renderable`/`mesh_asset_id`
   pair already uses. A version-1 artifact is rejected outright.
4. **Migration — explicit, no silent visual change:** every
   currently-checked-in scene source (`world_scene.scene.txt`) and
   every embedded scene-source-literal test string is re-authored only
   to the version-2 grammar's own version line — no existing node gains
   a `material=` token. Their own cooked artifacts change (new schema
   version, wider per-node record, `has_material = 0` throughout); their
   own rendered pixels do not, guaranteed by Runtime's own fallback
   (item 8, below) and verified directly by the existing
   `minimal_cube`/`world_scene` goldens staying byte-for-byte unchanged
   on disk throughout Implementation.
5. **Per-scene manifest extension, not a catalog:**
   `atlantis_add_scene_asset()` gains a `MATERIAL_DEPENDENCIES`
   argument, mirroring the existing `MESH_DEPENDENCIES` mechanism
   exactly; each entry must already be declared via the new
   `atlantis_add_material_asset()`
   ([ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)),
   itself taking a `TEXTURE_DEPENDENCIES` argument. At CMake configure
   time, a material dependency's own already-known texture-dependency
   artifact/metadata paths are pulled transitively into the **same,
   unchanged, three-column** manifest format
   (`logicalPath\tartifactPath\tmetadataPath`) the mesh-only manifest
   already uses — no new column, no schema change, no new file. The
   manifest remains exactly what it is today: generated fresh per
   scene at CMake generate time, listing only that one scene's own
   declared dependency closure, never a portable part of any artifact,
   never queryable from outside that one scene's own build step.
6. **Runtime resolution order:** `loadAndInstantiateScene()`'s existing
   lettered steps extend, not replace:
   - collect distinct mesh **and** material `AssetId`s in
     first-reference order;
   - resolve every one against the same manifest resolver *before* any
     loading begins — an unresolvable material `AssetId` fails the
     whole scene load with its own `RuntimeInitError` sub-code, exactly
     like an unresolvable mesh `AssetId` does today;
   - load meshes (unchanged); load each distinct material via
     `loadMaterialAsset()`; load each distinct material's own texture
     via `loadTextureAsset()`, deduplicated by texture `AssetId` the
     same way meshes already dedup by mesh `AssetId`; construct the
     real `SampledTexture`/`Sampler`/`Material` for each distinct
     material.
7. **Atomic publish, extended in structure, not in kind:**
   `SceneLoadOutcome` widens to carry the new
   `AssetId`-keyed material/texture/sampler resource maps alongside
   `world`/`meshResourceMap`. Each new map gets its own
   `static_assert(std::is_nothrow_move_constructible_v<...>)` or
   `is_nothrow_move_assignable_v<...>` at the exact same call site the
   existing two already occupy
   (`runtime_application.cpp:83-89`'s own pattern), proving the widened
   publish step is genuinely atomic by compile-time construction — no
   catch/rollback is introduced, matching
   [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)'s
   own existing "compute everything fallibly first, publish only
   infallible moves at the end" structure exactly.
8. **Per-entity binding with fallback:** `runFrame()`'s `DrawItem`
   extraction reads `Renderable::materialAsset`; when present, binds
   `item.material` to the resolved entry in the new material resource
   map; when absent, falls back to Runtime's own existing single,
   hardcoded `Material` — the exact mechanism that keeps every
   currently-authored, unmodified-by-this-Spec scene rendering
   unchanged.
9. **GPU ownership and destruction order:** `SampledTexture`/`Sampler`
   instances must outlive every `Material` that borrows them, and both
   must be destroyed before `Device` — `Material`'s own already-
   documented contract, already demonstrated structurally by
   `textured_quad_fixture.cpp` (declaring its own textures/sampler
   *before* its own materials, so C++'s reverse-declaration-order
   destruction destroys materials first). `RuntimeApplication`'s member
   declaration order places its new texture/sampler resource map(s)
   before its new material resource map, for the identical reason;
   `device_` remains declared first, per the existing pattern, so it
   outlives everything this ADR adds.
10. **Deduplication via `AssetId`-keyed maps, no global registry:**
    `materialResourceMap_`/`textureResourceMap_` are each a fresh
    `std::unordered_map<AssetId, T>`, rebuilt from nothing on every
    scene load and scoped entirely to that load's own call stack —
    exactly `meshResourceMap_`'s own existing shape. Multiple entities
    referencing the same material or underlying texture dedup for free,
    with no new caching design and no persistent, cross-load, or
    cross-session store of any kind.

## Consequences

### Positive

- Every currently-authored scene asset keeps rendering byte-for-byte
  identically — proven by the existing `minimal_cube`/`world_scene`
  goldens, not merely argued.
- Runtime's own already-audited, `static_assert`-proven atomic-publish
  mechanism extends in structure, not in kind — no new transactional
  concept, no new failure-recovery design, just more members published
  by the same proven pattern.
- Deduplication is free: reusing the exact `AssetId`-keyed-map shape
  `meshResourceMap_` already uses means no new caching logic, no new
  invalidation question, and no risk of the kind of global mutable
  state this codebase's own conventions reject.
- `World`'s own module boundary and dependency closure are completely
  unchanged in kind — one more optional `AssetId` field on an existing
  component, using a dependency (`AssetId`) `World` already has for
  exactly this reason.

### Negative / Trade-offs

- The scene format's version bump forces a repository-wide sweep of
  every embedded scene-source-literal test string, mirroring the exact
  risk Spec 0017's own Plan Review found for mesh sources — a
  hardcoded assertion unrelated to the literal string itself could
  hide from a grep-only inventory; the Plan must require running the
  full test suite, not trusting a static search alone.
- Every distinct material `AssetId` a scene load resolves gets its own
  real `Pipeline`/`Material`/`SampledTexture`/`Sampler` — no caching or
  reuse across distinct `AssetId`s sharing a `MaterialKind`, an
  explicit, accepted limitation of this round (Spec 0018's own
  Non-Goals).
- Runtime's `RuntimeApplication` gains three more members
  (material/texture/sampler resource maps) whose declaration order is
  now load-bearing for correctness (item 9) — a real, if
  already-precedented, source of "this must not be reordered casually"
  fragility future changes to this struct must respect.

## Alternatives Considered

- **Make `materialAsset` a required field**, with every existing scene
  asset migrated to name a real (possibly placeholder) material.
  Rejected: forces an unrelated migration burden on every existing
  scene for no functional reason, and removes the natural fallback
  path (item 8) that keeps today's rendering unchanged — the entire
  reason this ADR can make the strong "no silent visual change"
  guarantee it does.
- **A fourth manifest column recording each entry's own asset kind**
  (mesh/material/texture), to let the resolver disambiguate.
  Rejected: Runtime already knows which `AssetId`s are mesh references
  and which are material references directly from the scene artifact's
  own decoded structure (two disjoint fields on `DecodedRenderable`) —
  a manifest-level kind tag would be redundant data solving a
  disambiguation problem that does not exist.
- **A separate, dedicated manifest file for material/texture
  dependencies**, rather than folding them into the existing scene
  manifest. Rejected: would double the number of build-tree-private
  files Runtime must read per scene load for no benefit — the existing
  three-column format already generalizes to any `AssetId`-keyed
  dependency without a schema change, and one file per scene is
  simpler for both the CMake-side generator and the Runtime-side reader
  to reason about than two.
- **Skip the atomic-publish extension; publish meshes/`world_`
  atomically as today, then materials/textures separately, best-effort,
  after.** Rejected: would reintroduce exactly the "a Renderable could
  reference a mesh that loaded but a material that didn't" partial-
  scene risk [ADR-0054](0054-scene-loading-transactional-instantiation-contract.md)
  was written to eliminate for meshes; there is no principled reason a
  material reference deserves weaker guarantees than a mesh reference
  on the same node.
