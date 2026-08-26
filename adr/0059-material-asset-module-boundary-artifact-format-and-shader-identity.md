# ADR 0059: Material Asset — Module Boundary, Artifact Format, and Shader-Identity Contract

- **Status:** Proposed
- **Date:** 2026-08-26
- **Deciders:** (pending Human Review)
- **Related Spec:** [specs/0018-material-asset-scene-binding-foundation.md](../specs/0018-material-asset-scene-binding-foundation.md) (`Draft`)
- **Related ADR(s):**
  [ADR-0043](0043-asset-system-module-boundary.md) (Asset System module
  boundary — extended in membership, unchanged in kind: a fourth asset
  type joins Core-only Asset System, exactly as the third, Texture,
  already did),
  [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)
  (data format, versioning, and dependency policy — extended to name a
  fourth asset type; not narrowed or reopened, since Material is wholly
  new content, not a widening of an existing type's own field list),
  [ADR-0055](0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)
  (RHI `SampledTexture`/`Sampler` — this ADR's own `Sampler` parameter
  fields mirror `SamplerCreateParams` exactly, unmodified),
  [ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)
  (texture asset format — the closest existing precedent for a small,
  RHI-free Asset System DTO this ADR follows),
  [ADR-0058](0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md)
  (static mesh UV0 — the texture this ADR's own Material references is
  sampled against a mesh whose UV0 is asset-sourced, per that ADR;
  unaffected by this one).

## Context

[Spec 0016](../specs/0016-texture-sampler-foundation.md) gave
`Atlantis::AssetSystem` a third asset type, Texture, and gave
`atlantis::renderer::Material` an optional, non-owning
`SampledTexture`/`Sampler` pair. [Spec 0017](../specs/0017-mesh-uv-attribute-foundation.md)
gave the one static mesh vertex layout a real, mandatory, asset-sourced
UV0 attribute. Neither Spec gave Asset System any notion of a
"material" — the concept exists only as `Material`, a Renderer-owned
GPU-resource wrapper, constructed today from raw `PipelineCreateParams`
plus two optional borrowed pointers, entirely in composition-root C++
code (fixtures, and Runtime's own single, hardcoded, untextured
instance). There is no small, versioned, deterministic DTO — the shape
every other asset kind in this codebase already has — naming *which*
texture a material samples, with *what* sampler configuration, through
*which* shading behavior.

Three concrete, evidence-based constraints shape this decision,
confirmed directly against real, current source:

- Every existing Asset System asset type (`StaticMeshAssetData`,
  `ValidatedSceneData`, `TextureAssetData`) follows the identical
  authoring-source → cook → binary artifact → metadata sidecar → load
  → CPU-only DTO shape, and every one of them is Core-only — no RHI
  type is named, included, or constructed anywhere in
  `atlantis_asset_system`, verified by an include-scanning test
  (`tests/asset_system/module_boundary_tests.cpp`), not merely stated.
  `atlantis::rhi::SamplerCreateParams` (Spec 0016/ADR-0055) is exactly
  `{ Filter filter; AddressMode addressMode; }` — two closed enums,
  each with two values, applied uniformly to U and V.
- Shader identity, today, is not a concept that exists anywhere in this
  codebase outside a hardcoded relative file path. `loadReflectionMetadata()`
  takes a bare `std::filesystem::path`; every composition root
  (`examples/minimal_renderer_demo/main.cpp`, `src/runtime/src/main.cpp`,
  every image-regression fixture) hardcodes a shader pair's own
  filename as a C++ string literal, with only the *output directory*
  supplied by CMake (`atlantis_add_slang_shader_pair()` exposes exactly
  one `PARENT_SCOPE` variable, the directory — no per-file path, no
  logical-path variable, unlike every `atlantis_add_*_asset()`
  function's own four-variable export). Grepping the entire `src/` tree
  for `registry`/`catalog` (any case) turns up no counter-example: Asset
  System's own `AssetId`/`normalizeLogicalPath()` scheme is the only
  "logical identifier resolves to a build-time artifact" mechanism that
  exists anywhere in this codebase.
- `Material` already supports an optional, non-owning
  `SampledTexture*`/`Sampler*` pair, checked both-or-neither exactly
  once, in its own constructor (`material.cpp:11`,
  `ATLANTIS_CHECK((sampledTexture_ == nullptr) == (sampler_ ==
  nullptr));`) — this ADR's own Material asset supplies exactly the
  data that pair needs (a texture reference plus sampler parameters),
  nothing more.

The real design question this ADR settles: does Material become a
fourth first-class Asset System asset type, following the exact shape
every other asset type already uses, or does this codebase invent a
new, ad hoc, non-Asset-System-owned data format for it — and, whichever
module owns it, how does a Material reference a shader without
building a Shader Asset Catalog this repository has explicitly deferred
since Spec 0015?

## Decision

**Material becomes Asset System's fourth asset type — a small,
versioned DTO naming a closed `MaterialKind`, a texture `AssetId`, and
`Sampler` parameters — following the exact cook/artifact/metadata-
sidecar/load shape mesh, scene, and texture already use. Shader
identity is resolved by a closed `MaterialKind` enum that Runtime maps
to a fixed, already-compiled built-in shader pair — no Shader Asset
Catalog, no shader `AssetId`, no new Shader System machinery.**

1. **Module and dependency:** `MaterialAssetData` (CPU-side DTO),
   `cookMaterial()`, `loadMaterialAsset()`, the material artifact codec,
   and the material metadata sidecar all live in `atlantis_asset_system`.
   No RHI type (`SampledTexture`, `Sampler`, `Pipeline`) is named,
   included, or constructed anywhere in this module — a composition
   root outside Asset System is exclusively responsible for turning a
   loaded `MaterialAssetData` into a real `atlantis::rhi::Sampler` and,
   together with a separately-loaded `TextureAssetData`'s own
   `atlantis::rhi::SampledTexture`, a real `atlantis::renderer::Material`.
   `Atlantis::AssetSystem`'s own `target_link_libraries` closure does
   not change (`Atlantis::Core` only).
2. **DTO shape:**
   ```cpp
   // This module's own independent enums -- never atlantis::rhi's
   // Filter/AddressMode -- mirroring TextureColorSpace's own precedent
   // of never naming an RHI type anywhere in this module.
   enum class MaterialSamplerFilter { Nearest, Linear };
   enum class MaterialSamplerAddressMode { Repeat, ClampToEdge };

   enum class MaterialKind {
     UnlitTextured,
   };

   struct MaterialAssetData {
     MaterialKind kind = MaterialKind::UnlitTextured;
     atlantis::asset_system::AssetId textureAsset = 0;
     MaterialSamplerFilter filter = MaterialSamplerFilter::Nearest;
     MaterialSamplerAddressMode addressMode = MaterialSamplerAddressMode::ClampToEdge;
   };
   ```
   Exactly four fields — kind, texture reference, and the two sampler
   parameters, each a plain value type. No color/tint factor (see
   Spec 0018's own D2 for the full justification: no current consumer
   reads one, and adding an unread field now is exactly the speculative
   addition this codebase's own conventions reject). A composition root
   outside Asset System is responsible for translating
   `MaterialSamplerFilter`/`MaterialSamplerAddressMode` into
   `atlantis::rhi::Filter`/`AddressMode` when constructing the real
   `Sampler` — the same translation-at-the-boundary pattern
   `TextureColorSpace` → `SampledTextureFormat` already establishes.
3. **Shader identity — closed enum, Runtime-resolved:** `MaterialKind`
   is a small, closed enumeration (starting with exactly one value,
   `UnlitTextured`) that a composition root (Runtime) maps to a fixed,
   hardcoded, already-compiled shader pair — reusing
   `shaders/textured_quad/textured_quad.slang` (Spec 0016, already
   built via the existing `atlantis_add_slang_shader_pair()`
   declaration, already proven correct against a real, asset-sourced
   UV0 mesh per Spec 0017's own `textured_quad` golden). No shader
   gains an `AssetId`, a logical path, or any resolution mechanism
   beyond the hardcoded-relative-path convention every composition root
   already uses for every shader pair today. Adding a second
   `MaterialKind` value later means adding a second hardcoded mapping
   Runtime-side — an explicit, accepted limit on this decision's own
   scope, not a design flaw to correct now.
4. **Authoring source and cooking:** a small, versioned text grammar
   (mirroring `mesh_source.cpp`/`scene_source.cpp`'s own established
   shape — a version line, then field lines), naming a `kind` token, a
   `texture=<logical path>` token (resolved to an `AssetId` by
   `cookMaterial()` exactly like `cookScene()` already resolves
   `mesh=<logical path>`), and sampler-parameter tokens. `cookMaterial()`
   normalizes its own `logicalPathInput` exactly like `cookStaticMesh()`/
   `cookTexture()` already do, computing this material's own `AssetId`
   from the normalized path — not from anything the source text itself
   contains.
5. **Cooker CLI/CMake wiring:** a new `AssetKind::Material` in the
   Tools cooker's existing `switch`-based dispatch (`--kind=material`),
   dispatching to a new `runCookMaterialMode()`; a new
   `atlantis_add_material_asset()` CMake function mirroring
   `atlantis_add_static_mesh_asset()`'s own stamp/`BYPRODUCTS`/
   four-variable `PARENT_SCOPE` export exactly, taking a
   `TEXTURE_DEPENDENCIES` argument (a texture asset that must already
   be declared via `atlantis_add_texture_asset()`) purely for CMake
   build-graph ordering and for the transitive manifest-inclusion
   [ADR-0060](0060-scene-material-binding-and-runtime-transactional-resource-publish.md)
   defines — never a compile-time or cook-time dependency the material
   artifact's own bytes encode beyond the plain texture `AssetId` value.
6. **Versioning and binary format contract:** a fixed-size header
   (magic, schema version, and whatever additional fields the exact
   layout needs — settled at Plan time, following the same
   little-endian, explicit-shift/mask, checked-arithmetic-before-
   allocation discipline `mesh_artifact.cpp`/`texture_artifact.cpp`
   already establish, per [ADR-0045](0045-asset-system-data-format-versioning-and-dependency-policy.md)'s
   own unconditional little-endian contract). No dual-version reader;
   an unrecognized schema version is rejected outright. Whether the
   artifact embeds its own `AssetId` (the mesh precedent) or relies on
   metadata-side self-consistency alone (the texture precedent) is
   left to Plan-time comparison against the material format's own
   final byte layout — both are already-`Accepted` precedents in this
   exact codebase; this ADR does not pick one in advance of that
   comparison.
7. **Reference validation stays value-level.** `decodeMaterialArtifact()`/
   `loadMaterialAsset()` decode and return the embedded texture
   `AssetId` as a plain value — they never attempt to resolve or verify
   that a texture with that `AssetId` actually exists anywhere.
   Resolution against a real, on-disk texture artifact is exclusively a
   Runtime-side job via the per-scene manifest
   ([ADR-0060](0060-scene-material-binding-and-runtime-transactional-resource-publish.md)),
   exactly mirroring how a scene's own `mesh_asset_id` is never
   existence-checked by `decodeScene()` itself today.
8. **No new error enumerator where an existing one already fits.**
   `BadMagic`, an `UnsupportedSchemaVersion`-shaped enumerator, a
   `SizeMismatch`-shaped enumerator, and a `LogicalPathInvalid`-shaped
   cook-time enumerator all reuse the exact naming and semantics
   `TextureCookError`/`TextureArtifactDecodeError`/`CookError` already
   establish. A new enumerator is added only for a genuinely new
   failure mode this format introduces that no existing one covers
   (e.g. an unrecognized `MaterialKind` tag value) — settled precisely
   at Plan time against the format's own final field list.

## Consequences

### Positive

- Exactly one place to look for "what is a Material asset" —
  `atlantis_asset_system`, alongside mesh/scene/texture, using the
  identical mechanism reviewers, tests, and tooling already understand.
- Zero new Shader System machinery, zero new identity/catalog concept
  anywhere in the codebase. `MaterialKind::UnlitTextured` reuses a
  shader pair that is already compiled, already reflected, and already
  proven correct on real GPU hardware.
- `Sampler` parameters on the Material DTO are byte-for-byte the same
  shape as the RHI surface they ultimately configure
  (`SamplerCreateParams`) — no translation ambiguity, no unreachable
  RHI sampler configuration, no dead DTO field.
- `Atlantis::AssetSystem`'s own Core-only dependency boundary, already
  verified by an include-scanning test for every existing asset type,
  extends to this fourth type with zero new test-infrastructure
  concept — the same test simply keeps passing.

### Negative / Trade-offs

- `MaterialKind` is not a general shader-selection mechanism. A future
  Spec that wants a second real shading behavior must add both a new
  `MaterialKind` enumerator *and* a new Runtime-side hardcoded
  shader-pair mapping — this ADR deliberately does not build a more
  general mapping mechanism now.
- The material artifact's own `AssetId`-embedding question is left open
  at this ADR's own level, to be settled once at Plan time — a real,
  disclosed deferral, not an oversight.
- A Material asset with a texture reference to a non-existent
  `AssetId` is not caught until Runtime attempts to resolve it against
  a real manifest — Asset System itself cannot and does not detect this
  at cook or load time, matching the exact, already-accepted behavior
  a scene's own dangling mesh reference has today.

## Alternatives Considered

- **Declare Material as a Renderer- or Runtime-owned type with its own
  ad hoc file format**, outside Asset System entirely. Rejected: this
  would be the only asset-shaped, versioned, on-disk data format in the
  entire codebase that is not Core-only Asset System content, forgoing
  every one of Asset System's already-built identity/validation/
  atomic-write guarantees, and would need to independently re-invent
  the AssetId/logical-path scheme (or, worse, invent a second,
  competing one) to reference a texture at all.
- **A shader logical identifier resolved by a new Shader System lookup
  mechanism.** Rejected: Shader System has never needed, and does not
  have today, any resolution mechanism beyond a hardcoded relative
  path; building one now, for exactly one `MaterialKind` value, is
  disproportionate new machinery this ADR is directed to avoid, and
  risks quietly growing into a general Asset Catalog this repository
  has explicitly deferred since Spec 0015.
- **A new, dedicated Shader Asset type inside Asset System**, giving
  shaders their own `AssetId`/logical-path/cook/load pipeline.
  Rejected for this round: real, legitimate future work, but a full new
  asset kind for content that is already compiled, versioned, and
  reflected by Shader System's own separate, working pipeline is
  disproportionate to this ADR's own narrow need (letting exactly one
  built-in shader be selected by a closed enum).
- **A richer Material DTO carrying a color tint/factor, normal-map
  reference, or multiple texture slots up front, anticipating future
  material kinds.** Rejected: no current consumer reads any of these;
  adding them now is exactly the speculative-field pattern this
  codebase's own conventions reject (see Spec 0017's own rejection of
  a general N-attribute vertex schema for the identical reasoning).
  Each is additive, behind a future schema version bump, exactly like
  Spec 0017 added UV0 to the mesh format without redesigning it.
