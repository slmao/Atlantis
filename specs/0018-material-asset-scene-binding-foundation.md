# Spec: Material Asset & Scene Binding Foundation

- **Status:** In Review
- **Author:** slmao
- **Created:** 2026-08-26
- **Related Plan(s):** (none yet — this Spec must be `Approved` first)
- **Related ADR(s):** [ADR-0059](../adr/0059-material-asset-module-boundary-artifact-format-and-shader-identity.md) (`Proposed`), [ADR-0060](../adr/0060-scene-material-binding-and-runtime-transactional-resource-publish.md) (`Proposed`)

## Summary

Close the loop Spec 0016 and Spec 0017 both left open and both named
explicitly as their own successor: a `World`-loaded scene can carry a
real, asset-sourced UV0 mesh (Spec 0017) and the RHI/Renderer already
knows how to sample a texture through an optional-texture `Material`
(Spec 0016), but nothing between "Asset System has a texture" and "a
Runtime-loaded scene actually looks textured" exists yet. This Spec adds
a fourth Asset System asset type — **Material** — a small, versioned DTO
naming a `MaterialKind`, a texture `AssetId`, and RHI `Sampler`
parameters; widens the Scene Asset format and `World::Renderable` with
an *optional* material reference (existing untextured scenes keep
rendering byte-identically); and extends Runtime's own scene-loading
pipeline to resolve, load, and atomically publish materials/textures
alongside meshes, so that `World::Renderable` → `DrawItem` extraction
can bind a real, asset-sourced `SampledTexture`/`Sampler`/`Material` per
entity instead of Runtime's own single, hardcoded, scene-wide `Material`.
Scope is deliberately the smallest closed loop that makes this true —
one unlit textured material kind, no PBR, no material graph, no Asset
Catalog.

## Motivation / Problem Statement

Confirmed directly against real, current source (see "Pre-draft
verification" below, and the three structured research passes this
Spec was drafted from):

- `atlantis::asset_system::DecodedRenderable` and
  `atlantis::world::Renderable` each have **exactly one field** —
  `AssetId meshAsset` — no material or texture field of any kind exists
  anywhere in either type today.
- The scene authoring grammar (`atlantis_scene_source_version: 1`) has
  no `material=` token; a scene node names a mesh by logical path
  (`mesh=<path>`) or nothing else renderable-related.
- Runtime's own real (non-test-fixture) rendering path
  (`RuntimeApplication::runFrame()`) constructs **exactly one**
  `Material` for the entire process, lazily, on the first
  format-known frame, with no `SampledTexture`/`Sampler` argument —
  every `DrawItem` in every frame points at that same single Material:
  `item.material = &*material_;` regardless of which entity or mesh is
  being drawn. Runtime never calls `Device::createSampledTexture()`,
  `Device::createSampler()`, or `atlantis::asset_system::loadTextureAsset()`
  anywhere in its own production code.
- Spec 0016's own textured proof
  (`tests/image_regression/fixture/textured_quad_fixture.cpp`) is not
  scene-driven at all — it hand-builds its own `Material`s/`Mesh`es
  directly in a test fixture, entirely outside the Scene Asset →
  `World` → Runtime pipeline. Spec 0016's Human Review Decision Table
  (item 15) explicitly declined to touch the Scene Asset format,
  calling an unused, speculative texture field on `Renderable`
  "speculative scope-widening... for no consumer this Spec itself
  provides."
- Spec 0017 closed the *authoring-side* half of this gap (a real,
  mandatory, asset-sourced UV0 on every static mesh) but explicitly
  left `World::Renderable`, the Scene Asset format, and Runtime's own
  Material construction untouched — its own Non-Goals name "Material
  Asset & Scene Binding Foundation" as the named successor.

The result: today, no amount of authoring effort in `assets/` can make
a Runtime-loaded scene visibly textured. A scene's mesh can carry
correct UV0 data, a texture asset can exist and be cooked, and RHI/
Renderer can sample it — but there is no data path connecting a scene
node to a texture at all. This Spec builds that path, end to end,
through the real authoring → cook → artifact → load → Runtime
resolve → GPU-resource-creation → `DrawItem` chain — not a fixture
shortcut.

## Goals

1. A fourth Asset System asset type, **Material**, following the exact
   authoring-source → cook → artifact → metadata-sidecar → load shape
   mesh/scene/texture already established, Core-only dependency
   preserved.
2. A minimal, closed material-kind vocabulary — this round, exactly one
   kind, `UnlitTextured` — naming a texture `AssetId` and RHI `Sampler`
   parameters (filter, address mode), with no Asset Catalog, no shader
   identifier scheme beyond what Runtime already hardcodes today.
3. `World::Renderable` (and its Asset-System-side `DecodedRenderable`
   counterpart) gains an **optional** material `AssetId` reference. Every
   existing, already-cooked scene asset keeps rendering exactly as it
   does today — no silent visual change.
4. Runtime's own scene-loading pipeline resolves, loads, and constructs
   real `SampledTexture`/`Sampler`/`Material` GPU resources for every
   distinct material an entity's `Renderable` names, sharing one
   AssetId-keyed resource map per distinct material/texture (the same
   deduplication mechanism `meshResourceMap_` already uses), and
   publishes them atomically alongside `world_`/`meshResourceMap_`.
5. A real, new, minimal scene+material image-regression fixture proves
   the whole path end to end — not a fixture-private hand-built
   `Material` bypassing Asset System, and not a repeat of
   `textured_quad_fixture.cpp`'s own non-scene-driven proof.
6. Zero change to any existing, committed golden
   (`minimal_cube`, `world_scene`, `textured_quad`).

## Non-Goals

- PBR, metallic/roughness, or any multi-texture material model.
- Lighting, shadowing, or image-based lighting of any kind.
- Post-processing or tone mapping.
- Normal/tangent vertex attributes or normal mapping — this Spec adds
  no new mesh vertex attribute (Spec 0017 already closed UV0; a second
  attribute is out of scope here).
- A material graph, shader graph, or any user-composable shading
  system — `MaterialKind` is a small, closed enum, not an open plugin
  point.
- Hot-reload of any kind, an editor, or runtime asset mutation.
- A distributable, cross-session Asset Catalog/Registry, or any
  rename-stable identity beyond the existing path-derived `AssetId`.
  The per-scene manifest this Spec extends stays exactly what it is
  today: a build-tree-private, scene-scoped file, never a portable or
  globally-queryable index.
- Mipmap generation, texture compression, texture streaming, or a
  bindless descriptor system — all already out of Spec 0016's own
  scope and unchanged here.
- Android, iOS, or Linux implementation — Phase 1 remains Windows-only
  for real hardware verification (per AGENTS.md).
- A new third-party dependency or a new top-level module. Material
  lives inside the existing `Atlantis::AssetSystem` module.
- Multiple materials or textures per `Renderable`/entity. Exactly zero
  or one, matching `Material`'s own existing zero-or-one texture/sampler
  contract (Spec 0016).
- Per-pipeline or per-material GPU-object caching/reuse across distinct
  `AssetId`s. Each distinct material `AssetId` a scene load resolves
  gets its own real `Pipeline`/`Material`, exactly like
  `textured_quad_fixture.cpp` already creates two independent
  `Material`s for its two quads today — no regression, but also no new
  sharing mechanism.

## Requirements

### Functional

- `atlantis::asset_system::MaterialAssetData` (or equivalent CPU-side
  DTO name) — the material's own `MaterialKind`, texture `AssetId`,
  and `Sampler` parameters, returned by a `loadMaterialAsset()`
  function matching `loadStaticMeshAsset()`/`loadTextureAsset()`'s own
  exact `(artifactPath, metadataPath) -> Result<T, E>` shape.
- `cookMaterial()` — authoring source (a small, versioned text grammar,
  mirroring `mesh_source.cpp`/`scene_source.cpp`'s own established
  shape) → runtime artifact + metadata sidecar, atomic write, exactly
  like every existing cook function.
- A new `AssetKind::Material` in the Tools cooker's existing dispatch
  (`--kind=material`), and a new `atlantis_add_material_asset()` CMake
  function mirroring `atlantis_add_static_mesh_asset()`'s own
  stamp/`BYPRODUCTS`/`PARENT_SCOPE` pattern exactly, taking a
  `TEXTURE_DEPENDENCIES` argument (mirroring `atlantis_add_scene_asset()`'s
  own `MESH_DEPENDENCIES`).
- `atlantis_add_scene_asset()` gains a `MATERIAL_DEPENDENCIES` argument;
  the generated per-scene manifest transitively includes each declared
  material's own texture dependency, without widening the manifest's
  own three-column (`logicalPath\tartifactPath\tmetadataPath`) format
  or its scene-scoped, build-tree-private nature.
- Scene authoring grammar version bump (`atlantis_scene_source_version:
  2`); a node may optionally name a material by logical path
  (`material=<path>`), resolved to an `AssetId` by `cookScene()`
  exactly like `mesh=<path>` already is. Version 1 sources are rejected
  outright — no dual-version reader.
- Scene artifact schema version bump; `DecodedRenderable`/
  `ValidatedSceneNode`'s renderable slot gains an optional material
  `AssetId` field, encoded/decoded with the same explicit little-endian
  shift/mask discipline every other multi-byte field already uses.
- `atlantis::world::Renderable` gains `std::optional<AssetId>
  materialAsset` (or equivalent), populated by
  `fromValidatedSceneData()` via the same trivial field-copy pattern
  already used for `meshAsset` — `World` itself constructs no
  Renderer/RHI type and gains no new dependency.
- `RuntimeApplication`'s scene-loading pipeline
  (`loadAndInstantiateScene()`) resolves every distinct material
  `AssetId` referenced by any node, loads each via `loadMaterialAsset()`,
  resolves and loads each material's own texture `AssetId` via
  `loadTextureAsset()` (deduplicated the same way meshes already are),
  constructs the real `SampledTexture`/`Sampler`/`Material` GPU
  resources for each distinct material, and publishes them atomically
  alongside `world_`/`meshResourceMap_` — extending, not replacing,
  the existing `SceneLoadOutcome`/two-`static_assert`-proven-`noexcept`
  publish pattern.
- Per-entity `DrawItem` extraction (`runFrame()`) binds
  `item.material` to the entity's own resolved, asset-sourced Material
  when `Renderable::materialAsset` is present, and falls back to
  Runtime's own existing single hardcoded `Material` when it is absent
  — the exact mechanism that keeps every currently-authored scene
  rendering unchanged.
- A new, minimal, independent scene + material image-regression
  fixture and its own first golden (ADR-0042's "Initial baseline
  bootstrap" category), proving the real end-to-end path.

### Non-functional

- **Performance:** not a goal of this round. One real `Pipeline`/
  `Material`/`SampledTexture`/`Sampler` per distinct material `AssetId`
  a scene load resolves is accepted, matching existing per-fixture
  behavior; no caching/pooling is required or attempted.
- **Memory:** no unbounded growth — every new resource map is
  rebuilt fresh per scene load and scoped to that scene's own declared
  dependency closure, exactly like `meshResourceMap_` today.
- **Portability (within the Vulkan-only Phase 1 constraint):** the
  material artifact is unconditionally little-endian, matching every
  other Asset System format (ADR-0045's own contract); no
  platform-specific field.
- **Other:** zero new error enumerator reused where an existing one
  already fits (mirroring Spec 0017's own discipline); new enumerators
  are added only where no existing one covers a genuinely new failure
  mode (e.g. an unknown `MaterialKind` value has no existing analog).

## Pre-draft verification against real, current source

Re-confirmed directly against `main` at Plan-drafting time (2026-08-26)
via three independent, structured research passes over the real
source tree (not assumed from memory of earlier Specs):

- `atlantis::asset_system::DecodedRenderable` (`scene_types.h:29-31`)
  and `atlantis::world::Renderable` (`renderable.h:7-9`) are each
  exactly `{ AssetId meshAsset = 0; }` — one field, confirmed by
  reading both files in full, not inferred from a comment.
- The scene authoring grammar's real version line is
  `atlantis_scene_source_version: 1` (`scene_source.cpp:11`); its
  per-node grammar has no `material=`/`texture=` token anywhere
  (`scene_source.cpp:9-274`, full parse function read).
- The scene binary artifact's per-node record is a fixed 72 bytes
  (`scene_artifact.h:17-21`), with the renderable slot being exactly
  `has_renderable (u32) + mesh_asset_id (u64)` — no spare bytes
  reserved for a material reference; a genuine schema version bump is
  required to add one, not a decode-time reinterpretation.
- `Material` (`material.h:34-54`) already has exactly the shape this
  Spec needs to consume: an owned `Pipeline` plus two independently
  nullable, non-owning, both-or-neither `SampledTexture*`/`Sampler*`
  fields, with a documented destruction-order obligation on the
  caller. `createMaterial()`/`PipelineCreateParams` (Spec 0016) already
  carry `hasSampledTextureBinding`; **no RHI, Renderer, or RenderGraph
  public API change is required by this Spec** — confirmed by reading
  `material.h`, `material.cpp`, `types.h`'s `PipelineCreateParams`, and
  `renderer.cpp`'s draw loop (`if (item.material->sampledTexture() !=
  nullptr) cmd.bindTexture(...)`) in full.
- `RuntimeApplication::runFrame()`'s real `DrawItem` loop
  (`runtime_application.cpp:398-420`) confirmed: `item.material =
  &*material_;` for every entity, unconditionally, today — the single
  hardcoded Material this Spec's own fallback path must preserve
  exactly when `materialAsset` is absent.
- `meshResourceMap_`'s exact type,
  `std::unordered_map<AssetId, atlantis::renderer::Mesh>`
  (`runtime_application.h:111`), and the two `static_assert`s proving
  its own publish step is `noexcept` (`runtime_application.cpp:83-89`)
  are the direct template this Spec's own material/texture resource
  maps and their own publish-step extension follow.
- Shader identity: confirmed there is **no** shader `AssetId`, logical
  path, or catalog entry anywhere in `atlantis::shader_system` or
  `atlantis::asset_system` — `loadReflectionMetadata()` takes a bare
  `std::filesystem::path`; every composition root (including Runtime)
  hardcodes a relative SPIR-V/reflection-JSON filename as a C++ string
  literal, with only the output *directory* supplied by CMake
  (`ATLANTIS_RUNTIME_SHADER_DIR`). `atlantis_add_slang_shader_pair()`
  exposes exactly one `PARENT_SCOPE` variable (the output directory)
  — no per-file path, no logical-path variable, unlike every Asset
  System `atlantis_add_*_asset()` function's own four-variable export.
  Grepping the whole `src/` tree for `registry`/`catalog` (any case)
  confirms `AssetId`/`normalizeLogicalPath()` is the *only* such
  identity mechanism anywhere in this codebase — there is no
  precedent, anywhere outside Asset System, for "a logical identifier
  that resolves to a build-time artifact."
- `assets/textures/textured_quad_source_unorm.png`/`_srgb.png` and
  `shaders/textured_quad/textured_quad.slang` (Spec 0016) already exist,
  are already cooked/compiled/tested, and are already proven to sample
  correctly against a real, asset-sourced UV0 mesh (Spec 0017's own
  Milestone 3, zero pixel difference against the existing
  `textured_quad` golden). This Spec's own new material/scene fixture
  reuses this exact texture and shader — no new PNG, no new `.slang`
  file.

## Proposed Design

### The minimal closed loop

```
material authoring (.material.txt)
  --texture=<logical path>--> resolved to texture AssetId at cook time
  --sampler params----------> filter, address mode
  --kind=UnlitTextured------> closed MaterialKind enum
        |
        v
cookMaterial() --> .amaterial artifact + .amaterial.meta.txt sidecar
        |
        v
scene authoring (.scene.txt, version 2)
  node: ... mesh=<path> material=<path>   (material= optional)
        |
        v
cookScene() --> .ascene artifact (version 2, renderable slot widened)
        |
        v
atlantis_add_scene_asset(... MESH_DEPENDENCIES ... MATERIAL_DEPENDENCIES ...)
  --> per-scene manifest (still 3-column, transitively includes each
      material's own texture entry)
        |
        v
Runtime: loadAndInstantiateScene()
  (a) read manifest  (b) decodeScene()  (c) collect distinct mesh AND
  material AssetIds, first-reference order  (d) resolve all via
  manifest  (e) load meshes (existing) + load materials + load each
  material's own texture, dedup by AssetId, construct real
  SampledTexture/Sampler/Material  (f) fromValidatedSceneData()
  (World, unchanged in kind — one more optional AssetId field copied)
  (g) atomic publish: world_ + meshResourceMap_ + materialResourceMap_
      (+ the texture/sampler maps a Material's own borrowed pointers
      require to outlive it)
        |
        v
runFrame(): DrawItem.material = resolved Material if materialAsset is
  present, else the existing single hardcoded Material (unchanged
  fallback — old scenes render identically)
```

### Where each new piece lives

| Piece | Module | Mirrors |
|---|---|---|
| `MaterialAssetData`, `MaterialKind`, material artifact/metadata/cook/load | `Atlantis::AssetSystem` (Core-only) | `texture_types.h`/`texture_artifact.h`/`cook_texture.h`/`load_texture.h` |
| `atlantis_add_material_asset()` | `src/asset_system/CMakeLists.txt` | `atlantis_add_static_mesh_asset()` |
| `MATERIAL_DEPENDENCIES` on `atlantis_add_scene_asset()` | `src/asset_system/CMakeLists.txt` | existing `MESH_DEPENDENCIES` |
| `runCookMaterialMode()`, `AssetKind::Material` | `Atlantis::Tools` (`src/tools/asset_cooker/`) | `runCookTextureMode()` |
| Scene grammar `material=` token, artifact renderable-slot widening | `Atlantis::AssetSystem` | the existing `mesh=` token / `mesh_asset_id` field |
| `Renderable::materialAsset` (both `DecodedRenderable` and `world::Renderable`) | `Atlantis::AssetSystem` + `Atlantis::World` | the existing `meshAsset` field, verbatim pattern |
| Material/texture resolution, loading, GPU-resource construction, atomic publish extension | `Atlantis::Runtime` (`scene_load.cpp`, `runtime_application.cpp`) | the existing mesh resolution/load/publish steps |
| Per-entity Material binding with fallback | `Atlantis::Runtime` (`runtime_application.cpp::runFrame()`) | the existing `item.mesh = &meshResourceMap_.at(...)` line |

No new top-level module. `Atlantis::World`'s link closure is unchanged
(`Atlantis::Core` + `Atlantis::AssetSystem`, both already there for
`AssetId`). `Atlantis::AssetSystem`'s link closure is unchanged
(`Atlantis::Core` only).

## Decisions for Human Review

Numbered to match this Spec's own governing questions one to one.

### D1. Material as Asset System's fourth asset type

**Decision:** Yes. `Atlantis::AssetSystem` gains a fourth asset kind,
Material, following the exact authoring-source → cook → artifact →
metadata-sidecar → load shape mesh (Spec 0012), scene (Spec 0015), and
texture (Spec 0016) already establish. `Atlantis::AssetSystem` remains
Core-only — `MaterialAssetData` names no RHI type, exactly like
`TextureAssetData` and `StaticMeshAssetData` today.

**Why:** a Material is structurally identical in shape to a texture —
a small, versioned DTO with no sub-asset dependency graph of its own
beyond a single texture `AssetId` reference. Every mechanism it needs
(cook/load functions, an artifact codec, a metadata sidecar, a CMake
declaration function, atomic writes, `AssetId` computation) already
exists in this exact shape three times over. Placing it anywhere else
(a new module, or inside Runtime/Renderer) would duplicate that
mechanism for no benefit and would violate the established rule that
CPU-side asset data never depends on RHI.

**Alternative rejected:** declaring Material as a Renderer-owned or
Runtime-owned type with its own ad hoc file format. Rejected: this
would be the only asset-shaped data format outside Asset System in the
entire codebase, forgoing every one of Asset System's already-built
identity/validation/versioning/atomic-write guarantees for no reason.

### D2. Minimal Material DTO/artifact fields

**Decision:** exactly three fields — `MaterialKind kind`, `AssetId
textureAsset`, and `Sampler` parameters (`Filter filter`, `AddressMode
addressMode`, mirroring `SamplerCreateParams`'s own exact two-enum
shape). **A color tint/factor field is explicitly excluded from this
round.**

**Why no color factor:** `textured_quad.slang` (the one shader this
Spec's own `UnlitTextured` kind maps to) has no color-tint uniform
input — nothing in this Spec's own consumer would read it. Adding an
unread field now would be exactly the kind of speculative,
untested-by-any-real-consumer addition this codebase's own engineering
conventions reject (AGENTS.md: don't add fields for hypothetical future
requirements). If a future material kind needs a tint, it is a new,
additive field behind a schema version bump — the same mechanism
Spec 0017 already used to add UV0, with no retroactive redesign
required.

**Why exactly `Filter`/`AddressMode`, not a richer sampler surface:**
`atlantis::rhi::SamplerCreateParams` (Spec 0016/ADR-0055) is already
exactly this shape — one `Filter` (`Nearest`/`Linear`), one
`AddressMode` (`Repeat`/`ClampToEdge`), applied uniformly to both U and
V. A Material DTO richer than the RHI surface it ultimately configures
would be dead data; a Material DTO narrower than it would make some
already-supported RHI Sampler configuration unreachable through the
Material asset path. Matching it exactly is the only option that is
neither.

### D3. Shader identity — recommendation: closed `MaterialKind`, no Shader Asset Catalog

**Decision (recommended): `MaterialKind::UnlitTextured`** — a small,
closed enum on the Material artifact (starting with exactly one
enumerator). Runtime maps each `MaterialKind` value to a fixed,
already-compiled, already-tested built-in shader pair it hardcodes
today (`shaders/textured_quad/textured_quad.slang`, already built via
the existing `atlantis_add_slang_shader_pair()` declaration Spec 0016
added) — the same hardcoded-relative-path mechanism Runtime already
uses for `minimal_mesh.slang`, extended to a second, alternative shader
pair selected by `MaterialKind` instead of always loading
`minimal_mesh`.

**Why this option, evidence-based:** confirmed directly that Shader
System has **no** identity mechanism of any kind beyond a hardcoded
relative file path — no `AssetId`, no logical path, no registry, no
lookup function. The two rejected alternatives each require inventing
new machinery this round should not build:

- *A shader logical identifier* (option b) would require Shader System
  to gain some resolution mechanism it has never needed before —
  exactly the kind of "quietly grow toward an Asset Catalog" this Spec
  is directed to avoid. There is no existing precedent anywhere in
  this codebase for "a logical identifier that resolves to a
  build-time artifact" outside Asset System's own `AssetId` scheme,
  and building a second, parallel one for shaders alone, used by
  exactly one `MaterialKind` value, is disproportionate.
- *A new Shader Asset type* (option c) is the heaviest option — a full
  new Asset System asset kind, its own cooker mode, its own artifact
  format, for content that is already compiled, versioned, and
  reflected by Shader System's own existing (non-Asset-System)
  pipeline. This duplicates real, working infrastructure and is
  legitimate future work for its own Spec, not something this round's
  minimal closed loop needs to solve.
- The closed-enum option requires **zero** new Shader System machinery
  and reuses a shader pair that already exists, is already compiled,
  and is already proven correct on real GPU hardware (Spec 0016/0017's
  own `textured_quad` golden) — the only option that closes this round
  without secretly building toward a catalog.

**Consequence, disclosed:** `MaterialKind` is not a general
shader-selection mechanism — adding a second kind later means adding a
second Runtime-side hardcoded shader-pair mapping, exactly like adding
a second built-in shader to `minimal_renderer_demo` would today. This
is accepted as this round's own explicit scope limit, not solved here.

### D4. `Renderable`'s material reference — recommendation: optional `AssetId`

**Decision:** `std::optional<AssetId> materialAsset` on both
`DecodedRenderable` and `world::Renderable`. `std::nullopt` means "no
material scene binding for this node" — Runtime's existing fallback
(its own single, hardcoded, untextured `Material`) renders it exactly
as today.

**Why optional, not mandatory:** mandatory would force every one of
this repository's existing scene sources (`minimal_cube`, `world_scene`)
to gain a synthetic material reference for no reason, and would give
Runtime no fallback path to preserve today's exact rendering for assets
this Spec does not intend to touch. Optional is the direct, minimal
extension of the exact pattern `Camera`/`Renderable` themselves already
use on every `World` entity (both already optional per-entity
components) — no new kind of "optionality" is introduced.

**Why not an explicit built-in-fallback name string** (the third
option offered): this would be a second way to express "no real
material asset," redundant with `std::nullopt`, which already means
exactly that everywhere else in this codebase (`Camera`,
`activeCameraIndex`, `parentNodeId`, and now `materialAsset` itself all
use `std::optional` for "this component/reference is absent," never a
sentinel string).

**Migration, explicit:** `minimal_cube.mesh.txt`'s own consuming scene
sources (`world_scene.scene.txt`, and any other currently-checked-in
`.scene.txt`) are **not required to gain a `material=` token** by this
Spec — they are re-authored only to the extent the version-2 grammar
requires (the version line itself), with `material=` left absent on
every existing node. Their own cooked artifacts change (new schema
version, wider per-node record with `has_material = 0`), but their
own rendered pixels do not — Runtime's fallback path guarantees this,
and the existing `minimal_cube`/`world_scene` goldens are the direct
proof (see Testing & Verification Plan). This mirrors Spec 0017's own
migration precedent for `minimal_cube.mesh.txt`'s UV0 columns exactly.

### D5. Scene authoring/artifact version bump

**Decision:** yes, both bump. `atlantis_scene_source_version: 1 → 2`;
the scene binary artifact's schema version and per-node record layout
both change to carry the new optional material reference. Version 1
sources/artifacts are rejected outright (existing
`SceneSourceParseError::UnknownSourceVersion` /
`SceneArtifactDecodeError::UnknownSchemaVersion`) — no dual-version
reader, matching Spec 0017's own D4 framing exactly: this is today's
single-target build pipeline's own policy (every `.ascene` this
pipeline produces is regenerated from its own checked-in source on the
next configure/build), not a permanent claim that a versioned scene
artifact could never be independently distributed in the future.

**Consequence, disclosed:** every currently-checked-in `.scene.txt`
source, and every embedded scene-source-literal test string across
`tests/` (a repository-wide sweep, mirroring Spec 0017's own Milestone
1 five-file sweep, is required and will be enumerated exhaustively at
Plan time) must move to the version-2 grammar in the same atomic step
that changes the parser/artifact — none may be left on the old grammar,
and the sweep's own completeness must be verified by actually running
the full test suite, not by a static grep alone (Spec 0017's own
`load_tests.cpp` lesson: a hardcoded-stride/version assertion can hide
from a string search).

### D6. Material artifact contract

**Decision:** mirror the mesh/texture artifact precedent exactly:

- A fixed-size binary header (magic, schema version, and whatever
  fields the exact layout needs), unconditionally little-endian,
  every multi-byte field assembled by explicit shift/mask — never a
  struct `memcpy` (ADR-0045's own contract, unchanged).
- All size/offset arithmetic computed and range-checked in a wide
  integer type before any allocation or read, matching
  `decodeMeshArtifact()`'s own already-audited discipline.
- Reference validation is **value-level only** at decode time — the
  embedded texture `AssetId` is decoded as a plain `u64` and returned;
  `decodeMaterialArtifact()`/`loadMaterialAsset()` never attempts to
  resolve or verify that the referenced texture actually exists (Asset
  System has no access to a manifest or any other resolution
  mechanism, and must not gain one — resolution is exclusively a
  Runtime-side job, exactly like a scene's own `mesh_asset_id` today).
  An unresolvable texture `AssetId` surfaces as Runtime's own
  `SceneDependencyUnresolved`-shaped error at scene-load time, not an
  Asset System error.
- A corrupted/truncated/wrong-schema-version input rejects with its
  own distinct enumerator, following the existing
  `TextureArtifactDecodeError`/`ArtifactDecodeError` naming pattern —
  reusing an existing enumerator name/shape wherever the failure mode
  is identical in kind (e.g. `BadMagic`, `UnsupportedSchemaVersion`),
  adding a new one only for a genuinely new failure this format
  introduces (e.g. an unrecognized `MaterialKind` value — no existing
  enumerator covers "unknown enum tag").
- Atomic write: write-to-temp-then-`rename()`, identical to every
  existing cooker output.
- Determinism: cooking the same material source twice produces
  byte-identical artifact and metadata output — verified by a
  dedicated determinism test, mirroring
  `cooker_determinism_tests.cpp`'s own existing mesh/texture coverage.

**Open, disclosed at Spec level, to close at Plan level:** whether the
material artifact embeds its own `AssetId` (the mesh precedent) or
relies entirely on metadata-side self-consistency (the texture
precedent, which embeds no `AssetId` in the artifact itself). Both
are real, already-`Accepted` precedents in this exact codebase; the
Plan must pick one and justify it against the material format's own
specific shape, not default to either without comparison.

### D7. Per-scene manifest extension without an Asset Catalog

**Decision:** extend `atlantis_add_scene_asset()` with a
`MATERIAL_DEPENDENCIES` argument, mirroring the existing
`MESH_DEPENDENCIES` mechanism exactly. Each `MATERIAL_DEPENDENCIES`
entry must already be declared via the new
`atlantis_add_material_asset()` (itself taking `TEXTURE_DEPENDENCIES`,
mirroring the same pattern one level down). At CMake configure time,
when building a scene's own manifest, each material dependency's own
already-known texture-dependency artifact/metadata paths are pulled in
transitively into the **same, unchanged, three-column** manifest format
(`logicalPath\tartifactPath\tmetadataPath`) — no new column, no new
file, no schema change to the manifest itself.

**Why this stays a manifest, not a catalog:** the manifest is
generated fresh, per scene, at CMake generate time, and lists only
that one scene's own declared dependency closure — never every asset
in the project, never queryable by an arbitrary `AssetId` from outside
that one scene's own build step, and never a portable part of any
artifact (this is already true of the existing mesh-only manifest and
remains true here; the transitive-pull mechanism is a CMake-configure-
time convenience, not a new runtime lookup capability). This is the
same reasoning `atlantis_add_scene_asset()`'s own existing doc comment
already gives for why `MESH_DEPENDENCIES` is not a catalog, extended
to one more dependency kind.

**Alternative rejected:** a fourth manifest column recording "kind"
(mesh/material/texture). Rejected as unnecessary: Runtime already
knows, from the scene artifact's own decoded structure, which
`AssetId`s are mesh references and which are material references (two
disjoint fields on `DecodedRenderable`) — a manifest-level kind tag
would be redundant data the resolver never actually needs to
disambiguate anything.

### D8. Runtime resolution order and transactional boundary

**Decision:** extend `loadAndInstantiateScene()`'s existing lettered
step sequence (not replace it):

- (c) widen "collect distinct AssetIds in first-reference order" to
  also collect distinct material AssetIds from
  `node.renderable->materialAsset`.
- (d) resolve every mesh **and** material AssetId via the same
  manifest resolver before any loading begins — an unresolvable
  material AssetId fails the whole scene load with its own
  `RuntimeInitError` sub-code, exactly like an unresolvable mesh
  AssetId does today.
- (e) load meshes (unchanged), then load each distinct material via
  `loadMaterialAsset()`, then load each distinct material's own
  texture via `loadTextureAsset()` (deduplicated by texture `AssetId`
  exactly like meshes already dedup by mesh `AssetId`), then construct
  the real `SampledTexture`/`Sampler`/`Material` GPU resources for
  each distinct material.
- (f)/(g) unchanged in kind: `fromValidatedSceneData()` stays
  infallible; the publish step widens from two moves
  (`world_.emplace()`, `meshResourceMap_ = ...`) to include the new
  material/texture/sampler resource maps, each protected by its own
  `static_assert(std::is_nothrow_...)` proving the widened publish
  stays genuinely atomic — no catch/rollback, matching the existing
  proof's own structure exactly.

**Hard requirement, matching the existing contract's own spirit:** no
partial scene ever becomes visible. All of mesh, material, texture,
and (trivially, since it is compile-time hardcoded) shader resolution
must succeed before `world_`/the resource maps are published together;
any failure anywhere in this chain leaves Runtime in its own existing,
already-tested failed-init state, never a half-textured scene.

### D9. GPU ownership and destruction order

**Decision:** `SampledTexture`/`Sampler` instances must outlive every
`Material` that borrows them, and both must be destroyed before
`Device` — the exact contract `Material`'s own header comment already
states and `textured_quad_fixture.cpp` already demonstrates
structurally (declaring `sampledTextureUnorm`/`sampledTextureSrgb`/
`sampler` *before* `materialUnorm`/`materialSrgb` in its own struct, so
C++'s reverse-declaration-order destruction destroys Materials first).
`RuntimeApplication`'s own member declaration order must place its new
texture/sampler resource map(s) **before** its new material resource
map, for the same reason. `device_` remains declared first (per the
existing `PlatformSession`-first / `Device`-early pattern) so it
outlives everything.

**Why this is not a new design question:** it is a direct application
of a contract this codebase already documents and already exercises
correctly in a real fixture — this Spec adds no new ownership model,
only a second real consumer of the existing one.

### D10. Deduplication across entities sharing a material/texture

**Decision:** `materialResourceMap_`/`textureResourceMap_`, each an
`AssetId`-keyed `std::unordered_map`, exactly mirroring
`meshResourceMap_`'s own existing shape. Because distinct materials/
textures are collected and loaded exactly once per distinct `AssetId`
(step (c)/(e) above), multiple entities referencing the same material
or the same underlying texture automatically share one loaded resource
— free deduplication, with no additional design, and no global mutable
registry: each map is rebuilt fresh, from nothing, on every scene load,
scoped entirely to that one load's own call stack, exactly like
`meshResourceMap_` already is.

### D11. World→Renderer extraction without a `World`→Renderer/RHI dependency

**Decision:** `World`'s own boundary is unchanged in kind.
`world::Renderable` gains one more plain `std::optional<AssetId>`
field — `AssetId` is a type `World` already depends on
`Atlantis::AssetSystem` for (`Renderable::meshAsset`, today).
`fromValidatedSceneData()` copies `n.renderable->materialAsset` into
`Renderable::materialAsset` with the same trivial, infallible
field-copy shape it already uses for `meshAsset` — `World` still
constructs no `Renderer`/`RHI` type anywhere, and its own
`target_link_libraries` closure (`Atlantis::Core` + `Atlantis::AssetSystem`)
does not change.

The real `DrawItem` construction — reading `Renderable::materialAsset`,
looking it up in `materialResourceMap_`, and falling back to the single
hardcoded `Material` when absent — stays exactly where mesh resolution
already happens: Runtime's own `runFrame()`/`scene_extraction.cpp`,
never `World` itself. This is the same module-boundary reasoning
ADR-0051/ADR-0052 already establish for mesh resolution, applied
without modification to material resolution.

### D12. Verification scene and golden

**Decision:** a new, minimal, independent scene authored specifically
for this Spec (not a reuse of `world_scene.scene.txt`, and not a
fixture-private, hand-built `Material` bypassing Asset System) —
one or two entities, each with a real `mesh=`/`material=` reference,
cooked through the real `cookScene()`/`cookMaterial()`/`cookTexture()`
pipeline, loaded through the real `loadAndInstantiateScene()` path this
Spec extends, reusing Spec 0016's already-proven
`textured_quad_source_unorm.png`/`textured_quad.slang` (no new PNG, no
new shader). A new image-regression golden for this new scene, captured
following ADR-0042's "Initial baseline bootstrap" category (the
established procedure for a scene's genuinely first golden — direct
visual review plus the build-time comparator's own self-consistency
check plus a real GPU/Validation-Layers run, since no prior golden
exists to diff against). `minimal_cube`, `world_scene`, and
`textured_quad`'s own existing goldens are asserted byte-for-byte
unchanged on disk throughout Implementation (`git diff` evidence, not
inferred from "tests passed" — Spec 0017's own V35 precedent).

**Why not extend `world_scene.scene.txt` itself:** doing so would
conflate "does the existing five-cube scene still render identically"
(a regression concern, already covered by leaving `material=` absent
on its own nodes) with "does a real, new textured scene work end to
end" (this Spec's own genuinely new claim) in one golden, making a
future failure ambiguous about which property broke.

### D13. Runtime windowed verification

**Decision:** unchanged in kind from every prior Spec — a windowed
smoke test (programmatic resize/minimize/restore/close via real Win32
message injection, proving lifecycle correctness) plus a genuine,
recorded human visual confirmation of the real `atlantis_runtime.exe`
window showing the new textured entity/entities correctly, both Debug
and Release. No automated swapchain pixel readback is invented or
claimed — every prior Spec (0013, 0014, 0015) already disclosed this
as infeasible in this environment, and that limitation is unchanged
here.

### D14. No Renderer/RHI/RenderGraph public API change

**Decision:** none needed. Confirmed directly: `Material` already
supports an optional, non-owning texture+sampler pair
(Spec 0016); `PipelineCreateParams::hasSampledTextureBinding` already
exists; RenderGraph's sampled-resource binding kind already exists;
`Renderer::drawFrame()`'s draw loop already conditionally binds a
texture when `item.material->sampledTexture() != nullptr`. This Spec's
entire surface is new Asset System content plus Runtime-side wiring —
if Implementation discovers this claim is wrong, that is a Human
Review-blocking architectural finding to raise immediately, not a gap
to quietly patch by inventing new RHI/Renderer surface mid-Implementation.

### D15. Boundary with future material/lighting work

**Decision:** PBR, a material graph, lighting, shadowing, and
post-processing all remain independent future Specs, unblocked but not
started by this one. This Spec's own successor candidate is named in
Out of Scope / Future Work below; nothing in this Spec's own design
presumes or forecloses that successor's own shape.

## Architectural Impact

This Spec introduces real architectural decisions, filed as two
`Proposed` ADRs (kept to the minimum number, each with a single,
coherent responsibility, matching Spec 0016's own module-boundary /
format / transactional-contract split):

- [ADR-0059](../adr/0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)
  — Material Asset's own module boundary (Asset System, Core-only),
  artifact/metadata format and versioning contract, and the closed
  `MaterialKind`-to-built-in-shader identity scheme (D1, D2, D3, D6).
- [ADR-0060](../adr/0060-scene-material-binding-and-runtime-transactional-resource-publish.md)
  — the Scene Asset format's widened `Renderable` (optional material
  reference, version bump), the per-scene manifest's
  `MATERIAL_DEPENDENCIES` extension, and Runtime's resolution-order/
  ownership/atomic-publish contract for the new material/texture/
  sampler resources (D4, D5, D7, D8, D9, D10, D11).

Neither ADR proposes a change to any already-`Accepted` ADR's own
Decision text. [ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md)
(data format/versioning/dependency policy) is extended in *kind*
(a fourth asset type joins the three it already names) but its own
Decision text is not narrowed or reopened the way ADR-0058 narrowed it
for mesh UV0 — Material is a wholly new asset type, not a widening of
an existing one's field list, so no Accepted Amendment to ADR-0045 is
anticipated; if Plan-time drafting finds otherwise, that is itself a
Human-Review-relevant finding to raise, not to silently apply.

## Alternatives Considered

- **Split this Spec into two — "Material Asset Foundation" and "Scene
  Material Binding Foundation."** Considered directly, evidence-based:
  a Material Asset with no scene binding has no real consumer this
  round provides (echoing Spec 0016's own item-15 rejection of an
  unused `Renderable` field); Scene Material Binding with no Material
  Asset has nothing to bind to. The two halves are only independently
  meaningful if a second, unrelated consumer of one already existed —
  none does. Kept as one Spec because the real code evidence shows a
  single, minimal, closed loop, matching this Spec's own explicit
  goal — not because a two-Spec split was assumed impossible before
  checking.
- **Add the material/texture reference directly to `World::Renderable`
  as a required, non-optional field, with a synthetic default for
  every existing scene.** Rejected — see D4: would force every
  existing, unrelated scene asset through this Spec's own migration
  for no functional reason, and removes the natural fallback path that
  keeps old rendering unchanged.
- **Give `MaterialKind` an open extension point (a string shader name
  resolved at Runtime startup from a config file or directory scan).**
  Rejected — see D3: this is a shader-identity mechanism Shader System
  does not have, would need to invent ad hoc outside any Spec that
  actually designs it, and risks exactly the "quietly grow toward a
  catalog" outcome this Spec is directed to avoid.
- **Skip a new golden; extend the existing `textured_quad` golden's
  own assertions to also prove scene-driven material loading.**
  Rejected — see D12: `textured_quad_gpu_tests.cpp` is deliberately
  not scene-driven (Spec 0016's own design); conflating it with a
  scene-driven claim would either force an architecturally unrelated
  change to that fixture or produce a test that does not actually
  exercise the real Runtime scene-loading path this Spec's own claim
  depends on.
- **Cache/reuse `Pipeline`/`Material` objects across distinct material
  `AssetId`s that happen to share a `MaterialKind`.** Rejected for
  this round — real, unstarted design work (a cache keyed by what,
  invalidated when, owned by whom) with no existing precedent anywhere
  in this codebase to extend; named explicitly in Non-Goals rather than
  attempted partially.

## Testing & Verification Plan

Full V-numbered checklist deferred to Plan (per this repository's own
process), but the following classes of proof are required, mirroring
Spec 0017's own checklist shape:

- **GPU-independent:** material authoring grammar round-trip (including
  rejection of a malformed/unknown-`MaterialKind`/wrong-version source);
  material artifact encode/decode fixed-byte pinning test (independent
  of the encoder, matching `mesh_artifact_tests.cpp`'s own established
  pattern); a dedicated cook→load round-trip proving a real texture
  `AssetId` reference survives intact; scene grammar/artifact version-2
  round-trip including the optional `material=` token both present and
  absent; every embedded version-1 scene-source-literal test string
  updated in the same atomic step (a repository-wide sweep, verified by
  running the full suite, not by grep alone).
- **`tool`-labeled:** a real CLI cook of a material source via
  `atlantis_asset_cooker --kind=material`; determinism (cooking twice
  produces byte-identical output).
- **GPU-required:** the new scene+material fixture renders a
  non-degenerate, visibly textured frame; a full capture-compare cycle
  against its own new golden with zero difference, Debug and Release;
  the existing `minimal_cube`/`world_scene`/`textured_quad` goldens
  confirmed byte-for-byte unchanged on disk (`git diff` evidence) both
  before and after Implementation; Vulkan Validation Layers grepped
  clean across full verbose GPU test output, both configurations.
- **Manual:** `ATLANTIS_BUILD_TESTS=OFF` fresh configure builds every
  declared asset including the new material/its texture dependency;
  the CMake re-import-triggering procedure (touching a material or
  scene source re-cooks automatically); a `/w14062` positive-proof
  build plus a temporary negative C4062 probe on any new
  `switch`-exhaustive enum this Spec introduces; module-boundary scan
  reconfirming `Atlantis::AssetSystem` and `Atlantis::World`'s own
  link closures are unchanged in membership; a genuine, recorded human
  visual confirmation of the real windowed `atlantis_runtime.exe`
  showing the new textured scene correctly, Debug and Release.

## Risks & Open Questions

- **Material artifact's own `AssetId` embedding choice (D6)** is
  explicitly left open for the Plan to resolve with a direct
  comparison against both existing precedents (mesh embeds it,
  texture does not) — not decided here to avoid picking one without
  the format's own exact byte layout in front of the decision.
- **The repository-wide scene-source-literal sweep (D5)** carries the
  same risk Spec 0017's own Plan Review found for mesh sources: a
  hardcoded assertion unrelated to the literal string itself (e.g. a
  node-record byte-size expectation) could hide from a text search.
  The Plan must explicitly call for running the full test suite, not
  trust a grep-based inventory alone.
- **Whether exactly one new scene fixture is sufficient GPU-required
  proof**, or whether a second entity/material combination is needed
  to exercise deduplication (D10) for real, is a Plan-time judgment
  call once the exact fixture shape is drafted — this Spec requires
  the proof to exist, not a specific entity count.

## Out of Scope / Future Work

- **PBR Material** — metallic/roughness, normal mapping, IBL. Its own
  future Spec, unblocked by this one but not designed here.
- **Lighting Foundation** — the next named candidate (see
  `specs/README.md`'s own Candidate Backlog); not drafted as part of
  this Spec.
- **Shadow, post-processing, tone mapping.**
- **A general Shader Asset Catalog** — if a future Spec needs more than
  one `MaterialKind` per real shader pair, or needs shaders resolved by
  something other than a Runtime-hardcoded mapping, that Spec must
  design the identity mechanism this Spec deliberately does not build.
- **Pipeline/Material caching and reuse** across distinct material
  `AssetId`s sharing a `MaterialKind` — named in Non-Goals; real,
  unstarted design work.
- **A distributable, cross-session Asset Catalog** — remains explicitly
  deferred, as it has been since Spec 0015.
