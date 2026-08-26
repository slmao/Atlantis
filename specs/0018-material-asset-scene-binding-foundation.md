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
  `AssetId` referenced by any node and loads each's own CPU-only data
  via `loadMaterialAsset()`/`loadTextureAsset()` (deduplicated the same
  way meshes already are), publishing that CPU-side data atomically
  alongside `world_`/`meshResourceMap_` — extending, not replacing, the
  existing `SceneLoadOutcome`/two-`static_assert`-proven-`noexcept`
  publish pattern. The real `SampledTexture`/`Sampler`/`Material` GPU
  resources are **not** constructed here — Spec 0016's own "texture
  upload must go through RenderGraph against a real `RenderTarget`"
  constraint makes that structurally impossible before a window's own
  swapchain exists; realization is deferred to `runFrame()` (see D8).
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
Phase 1 (CPU-only) -- Runtime: loadAndInstantiateScene(),
  inside initializeSteps(), before any RenderTarget exists
  (a) read manifest  (b) decodeScene()  (c) collect distinct mesh AND
  material AssetIds, first-reference order  (d) resolve all via
  manifest  (e) load meshes + their real GPU Mesh buffers (existing;
  Buffer creation needs no RenderTarget) + load materials' and
  textures' own CPU-only DTOs (loadMaterialAsset()/loadTextureAsset(),
  no RHI type constructed)  (f) fromValidatedSceneData() (World,
  unchanged in kind)  (g) atomic publish: world_ + meshResourceMap_ +
  the new CPU-only material/texture data maps
        |
        v
Phase 2 (deferred GPU realization) -- Runtime: runFrame(), at the
  exact point a real RenderTarget + known colorFormat first coexist
  (the existing format-change check): for each not-yet-realized
  material, create Sampler + SampledTexture + staging Buffer + Pipeline/
  Material (all RenderTarget-independent, synchronous calls); record
  its texture-upload RenderGraph pass into THIS frame's CommandList,
  before Renderer::drawFrame()'s own draw-graph call; one submit()
  covers upload + draw; waitIdle() on this realization frame only, then
  move the result into materialResourceMap_/textureResourceMap_/
  samplerResourceMap_ -- the one point it becomes drawable
        |
        v
runFrame(): DrawItem.material = resolved Material if materialAsset is
  present AND already realized; skipped (retried next frame) if present
  but not yet realized; else the existing single hardcoded Material
  (unchanged fallback — old scenes render identically)
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
| Phase 1: material/texture AssetId resolution + CPU-only data loading, atomic publish extension | `Atlantis::Runtime` (`scene_load.cpp`) | the existing mesh resolution/load/publish steps |
| Phase 2: per-frame, deferred GPU realization (`Sampler`/`SampledTexture`/upload/`Pipeline`/`Material`) | `Atlantis::Runtime` (`runtime_application.cpp::runFrame()`) | the existing single-`Material` format-change rebuild block |
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

### D3. Shader identity — recommendation: closed `MaterialKind`, no Shader Asset Catalog — with one real, disclosed CMake fix required

**Decision (recommended): `MaterialKind::UnlitTextured`** — a small,
closed enum on the Material artifact (starting with exactly one
enumerator). Runtime maps each `MaterialKind` value to a fixed,
already-compiled, already-tested built-in shader pair
(`shaders/textured_quad/textured_quad.slang`) — the same
hardcoded-relative-path mechanism Runtime already uses for
`minimal_mesh.slang`, extended to a second, alternative shader pair
selected by `MaterialKind` instead of always loading `minimal_mesh`.

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

**Real gap found and corrected during this Spec's own centralized final
review — a genuine Must Fix, not a nuance:** `shaders/textured_quad/`'s
own `add_subdirectory()` call is declared *inside* the root
`CMakeLists.txt`'s `if(ATLANTIS_BUILD_TESTS)` block, and its own
`CMakeLists.txt` says so explicitly: "Test-only consumer
(`tests/image_regression/fixture/`) -- added inside the
`ATLANTIS_BUILD_TESTS` block, unlike `minimal_renderer`'s own
unconditional placement (`atlantis_runtime` also needs
`minimal_mesh_shaders`)." Confirmed directly: with
`ATLANTIS_BUILD_TESTS=OFF`, the `textured_quad_shaders` CMake target,
its compiled SPIR-V, and its reflection JSON **do not exist at all**.
Runtime (`atlantis_runtime`/`atlantis_runtime_host`) must be buildable
independent of `ATLANTIS_BUILD_TESTS` — reusing `textured_quad.slang`
as written today would make a shipping executable's own build silently
depend on a test-only CMake target, exactly the "silently promote a
test-private shader to a Runtime public dependency" outcome this Spec
is directed to avoid.

**Fix, mechanical and directly precedented:** move
`add_subdirectory(shaders/textured_quad)` out of the
`if(ATLANTIS_BUILD_TESTS)` block, to the same unconditional placement
`shaders/minimal_renderer` already has — for the exact same reason
`minimal_renderer`'s own comment already states
(`atlantis_runtime` needs it). This is not "inventing" a new shipping
shader or duplicating `textured_quad.slang`'s own content — it is
widening one CMake target's own build-time availability to match a
precedent this codebase already established for the identical
situation (a shader both Runtime and a test fixture consume). No
`.slang` source content changes; no new shader identity mechanism is
introduced; `tests/image_regression/fixture/textured_quad_fixture.cpp`
keeps consuming the exact same target, now simply available
unconditionally instead of only under `ATLANTIS_BUILD_TESTS`.
Confirmed compatible: the shader's own real, reflected contract —
`ConstantBuffer<CameraUniform>{view; projection;}` at binding(0,0) (32
floats, matching Runtime's own existing camera uniform buffer size
exactly), `PushConstants{objectToWorld}` at
`[[vk::push_constant]]` (16 floats, matching Runtime's own existing
push-constant size exactly), and `VertexInput{position@0 (float3),
uv@1 (float2)}` — requires no change to Runtime's own existing
Camera/`DrawItem` contract; only the vertex-input schema Runtime builds
for this material kind differs from `minimal_mesh`'s own (position@0,
uv@1 instead of position@0, color@1), mirroring
`textured_quad_fixture.cpp`'s own already-proven schema exactly.

**Consequence, disclosed:** `MaterialKind` is not a general
shader-selection mechanism — adding a second kind later means adding a
second Runtime-side hardcoded shader-pair mapping and, if that shader
is not yet unconditionally built, the same CMake-placement fix this
decision already makes once. This is accepted as this round's own
explicit scope limit, not solved here.

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

**The precise, three-way semantics `materialAsset` carries — not to be
conflated with each other anywhere in Implementation:**

1. **Absent (`std::nullopt`):** no material scene binding was ever
   declared for this node. Runtime falls back to its own existing,
   single, hardcoded `Material` — the exact, unconditional behavior
   every currently-authored scene already gets today. This is the
   *only* case the built-in fallback applies to.
2. **Present, but its CPU-side resolve/load fails** (D8 Phase 1 — the
   `AssetId` is unresolvable against the manifest, or `loadMaterialAsset()`/
   `loadTextureAsset()` itself fails): the **whole scene load fails**,
   exactly like an unresolvable or unloadable mesh reference does
   today. There is no silent fallback to the built-in `Material` here
   — a scene that names a material it cannot even load never reaches
   `Running` at all, matching the "no partial scene ever becomes
   visible" contract mesh references already enforce.
3. **Present, resolved and loaded, but not yet GPU-realized** (D8 Phase
   2 — a transient state, or a persistently-retried one if realization
   keeps failing): the affected entity's own `DrawItem` is skipped for
   that frame only (logged, not fatal, retried next frame) — this is
   **not** the same as case 1's fallback (the built-in `Material` is
   never silently substituted for a named-but-not-yet-ready material)
   and **not** the same as case 2's fatal scene-load failure (a
   transient realization delay is not a load failure). This third
   state exists precisely because GPU realization cannot happen at
   scene-load time (D8) — Implementation and its own tests must
   exercise and distinguish all three states, not merely the two that
   existed before this Spec.

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

**Consolidated error-domain accounting, exhaustively itemized (per
item 9 of this Spec's own centralized final review):**

*Enumerators genuinely new, one per exhaustively-checked `switch`,
each requiring its own `/w14062` positive-and-negative build probe
(mirroring Spec 0017's own C4062 regression check exactly):*
- A cook-time `MaterialCookError` (naming convention matching
  `TextureCookError`) needs at minimum: `LogicalPathInvalid` (reused
  name, new enum — matching `CookError`/`TextureCookError`'s own
  precedent of a same-named enumerator per format), `UnknownMaterialKind`
  (a genuinely new failure mode — no existing enumerator anywhere covers
  "unrecognized enum tag value" in cook-time source parsing), and
  `AtomicWriteFailed` (matching `TextureCookError`'s own single-
  write-failure shape, decided at Plan time against D6's own open
  `AssetId`-embedding question above).
- A decode-time `MaterialArtifactDecodeError` needs at minimum:
  `BadMagic`, an `UnsupportedSchemaVersion`-shaped enumerator, a
  `SizeMismatch`/`TruncatedHeader`-shaped enumerator (naming decided at
  Plan time by which of `ArtifactDecodeError`'s or
  `TextureArtifactDecodeError`'s own precedent the final byte layout
  more closely matches), and `UnknownMaterialKind` (decode-side
  counterpart of the cook-time one above — an artifact whose own
  `kind` byte does not match any real `MaterialKind` enumerator).
- A load-time `MaterialLoadError` needs at minimum:
  `ArtifactFileUnreadable`/`MetadataFileUnreadable` or a combined
  shape (decided at Plan time against `AssetLoadError`'s vs.
  `TextureLoadError`'s own two slightly different shapes),
  `ArtifactDecodeFailed`, `MetadataParseFailed`,
  `MetadataArtifactMismatch`.
- A new `AssetKind::Material` enumerator in the Tools cooker's own
  `switch (request.kind)` dispatch (`cook_command.cpp:328-336`) — this
  `switch` has no default case today (relying on `/w14062` for
  exhaustiveness, confirmed by direct inspection), so adding this
  enumerator without a corresponding `case` is a build error by
  construction, not merely a convention.

*Enumerators reused verbatim, zero widening required (confirmed by
direct inspection, not assumed):* `SceneManifestError`'s existing five
enumerators (D7, above); `RuntimeInitError` gains new sub-code
enumerators for material-specific scene-load failures (mirroring its
own existing `SceneDependencyUnresolved`/`SceneDependencyLoadFailed`
naming exactly — a new `MaterialDependencyUnresolved`/
`MaterialDependencyLoadFailed` pair, or a decision to reuse the
existing mesh-named ones generically, is a Plan-time naming choice, not
an architectural one); no existing `SourceParseError`/`MetadataParseError`/
`CookError`/`ArtifactDecodeError`/`AssetLoadError` (the mesh-specific
ones) gains a new enumerator — Material is a wholly new, independent
enum family, not an extension of mesh's own.

**Explicit rejection of severity-collapsing:** per item 9's own
instruction, no recoverable Asset-System or Runtime error introduced by
this Spec is ever downgraded to an `ATLANTIS_CHECK`/assertion, and no
single, broad enumerator (e.g. a generic `MaterialError::Failed`) is
used to paper over what are, on inspection, genuinely distinct failure
stages (cook-time parse vs. decode-time corruption vs. load-time
cross-check vs. scene-load-time unresolvable-reference vs. per-frame
realization failure) — each stage keeps its own distinct enumerator
family, exactly matching every existing asset kind's own established
discipline.

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

**Error domain — reused, not widened, and exhaustively accounted for:**
`loadSceneDependencyManifest()`'s existing `SceneManifestError`
(`ManifestUnreadable`, `MalformedEntry`, `DuplicateLogicalPath`,
`AssetIdCollision`, `MetadataArtifactMismatch`) already processes each
manifest line generically — one `{logicalPath, artifactPath,
metadataPath}` triple at a time, with no awareness of which asset kind
produced it. Every one of these five enumerators already generalizes
correctly to a material or texture entry with **zero widening**:
`DuplicateLogicalPath`/`AssetIdCollision` already catch a colliding
entry regardless of kind (a mesh and a material can never legitimately
share a logical path or an `AssetId`, and the existing collision check
does not need to know which is which to catch it);
`MetadataArtifactMismatch` already re-derives each entry's own
`AssetId` from its logical path and cross-checks it against that
entry's own metadata sidecar, again with no kind-awareness needed. A
**wrong-kind reference** — a scene's own `material=<path>` accidentally
pointing at a logical path that was actually declared as a mesh (or
vice versa) — is not a manifest-level failure at all: the manifest
entry itself is perfectly well-formed (a real, valid artifact/metadata
pair exists at that path), so it resolves successfully; the failure
surfaces one layer up, when `loadMaterialAsset()` attempts to decode
what is actually a mesh artifact's own bytes and hits `BadMagic` (every
artifact kind's own magic bytes differ — mesh `"ATLMESH\0"`, scene
`"ASCN"`, and the material/texture artifacts' own distinct magics) —
already an existing-shaped enumerator this format reuses (ADR-0059's
own D6). No dedicated "wrong kind" or "kind mismatch" enumerator is
needed anywhere in this pipeline.

**Unused-but-declared dependencies are explicitly permitted, not an
error:** a `MATERIAL_DEPENDENCIES`/`TEXTURE_DEPENDENCIES` entry that no
scene node actually references at runtime is not a validation failure
— `atlantis_add_scene_asset()`'s own existing `MESH_DEPENDENCIES`
already tolerates this identically (a declared-but-unreferenced mesh
dependency cooks and validates cleanly today); the manifest is a
build-time dependency-closure declaration, not a runtime usage
guarantee, and this Spec does not narrow that existing tolerance.

**Iteration order:** `SceneDependencyResolver::find()` is, and remains,
a point lookup (`std::lower_bound` over an `AssetId`-sorted vector,
per ADR-0054's own existing contract) — nothing in this Spec's own
extension iterates the resolver end-to-end or depends on manifest-file
line order for correctness; load order is, and remains, driven
entirely by "first-reference order" as `loadAndInstantiateScene()`
walks the scene artifact's own decoded node list, never by
`std::unordered_map` iteration order anywhere.

### D8. Two-phase resolution: CPU transactional scene publish, then deferred, per-frame GPU material/texture realization

**This decision was substantially rewritten during this Spec's own
centralized final review.** The original draft claimed real
`SampledTexture`/`Sampler`/`Material` GPU resources could be
constructed inside `loadAndInstantiateScene()`'s own step (e), which
runs entirely inside `RuntimeApplication::initializeSteps()` —
confirmed directly, this happens **before any real windowed
`RenderTarget` has ever been acquired**: `Presentation` itself is not
constructed until the first `platform::SurfaceCreated` event, observed
inside `runFrame()`'s own event loop, which runs only after
`initializeSteps()` has already returned. `Device::submit()`'s own real
signature (`submit(std::unique_ptr<CommandList>, const RenderTarget&
target)`) takes `target` by non-null reference — there is no
target-independent overload, confirming Spec 0016's own established
constraint exactly. A one-time CPU→GPU texture upload is, by that same
constraint, mandatorily a RenderGraph-recorded operation against a
real `RenderTarget`'s own `CommandList` submission (Spec 0016/ADR-0056)
— it cannot happen during `initializeSteps()`, full stop. The original
draft's own claim that "the existing atomic-publish mechanism extends
directly" was wrong for GPU-resource construction specifically, and is
corrected here.

**Decision: two distinct phases, not one.**

**Phase 1 — CPU-only, inside `initializeSteps()`/`loadAndInstantiateScene()`,
unchanged in kind from today's mesh handling:**

- (c) widen "collect distinct AssetIds in first-reference order" to
  also collect distinct material AssetIds from
  `node.renderable->materialAsset`.
- (d) resolve every mesh **and** material AssetId via the same
  manifest resolver before any loading begins — an unresolvable
  material AssetId fails the whole scene load with its own
  `RuntimeInitError` sub-code, exactly like an unresolvable mesh
  AssetId does today.
- (e) load meshes and construct their real GPU `Mesh` buffers
  (unchanged — `Buffer` creation via `Device::createBuffer()` needs no
  `RenderTarget` and is not subject to Spec 0016's upload constraint;
  only a texture's own pixel-data *upload* is). Load each distinct
  material via `loadMaterialAsset()` and each distinct material's own
  texture via `loadTextureAsset()` (deduplicated by texture `AssetId`
  exactly like meshes already dedup by mesh `AssetId`) — both return
  **CPU-only DTOs** (`MaterialAssetData`, `TextureAssetData`), naming no
  RHI type. No `SampledTexture`, `Sampler`, `Pipeline`, or `Material` is
  constructed in this phase.
- (f)/(g): `fromValidatedSceneData()` stays infallible. The publish
  step widens from two moves (`world_.emplace()`,
  `meshResourceMap_ = ...`) to include the new mesh resource map
  (unchanged) plus two new **CPU-only** maps — loaded
  `MaterialAssetData`/`TextureAssetData` keyed by their own `AssetId`
  — each protected by its own
  `static_assert(std::is_nothrow_...)` proving the widened publish
  stays genuinely atomic, matching the existing proof's own structure
  exactly. **No GPU material/texture resource map is published here.**

**Hard requirement for Phase 1, matching the existing contract's own
spirit:** no partial scene ever becomes visible at the CPU level. All
of mesh GPU-buffer creation, material CPU-data loading, and texture
CPU-data loading must succeed before `world_`/the CPU-side resource
maps are published together; any failure anywhere in this chain leaves
Runtime in its own existing, already-tested failed-init state.

**Phase 2 — deferred GPU realization, inside `runFrame()`, at the exact
point Material already rebuilds today:** `runFrame()`'s existing
format-change block (the `if (!lastSeenFormat_.has_value() ||
currentFormat != *lastSeenFormat_)` check, `runtime_application.cpp:295-313`)
is the only place in Runtime's own real code where a real, acquired
`RenderTarget` and a known `colorFormat` coexist for the first time.
This decision extends that same per-frame check to also realize any
CPU-loaded material that has not yet been turned into real GPU
resources:

1. On every frame, after acquiring `target` and before drawing,
   compute the set of distinct material `AssetId`s referenced by any
   currently-renderable entity that are **not yet** a key in
   `materialResourceMap_` ("pending realization" — computed fresh each
   frame from `World`'s own current renderable set minus the resource
   map's own current keys; never a separately persisted queue or a
   second source of truth).
2. For each pending material, attempt realization as one local,
   all-or-nothing sequence, entirely with function-local, RAII-owned
   objects until the final step: create its `Sampler`
   (`Device::createSampler()`, no `RenderTarget` needed); create its
   `SampledTexture` (`Device::createSampledTexture()`, no
   `RenderTarget` needed — this call only allocates the GPU image, it
   does not upload pixels); create a staging `Buffer`
   (`BufferPurpose::Staging`) and `memcpy` the already-loaded
   `TextureAssetData::pixelBytes` into its mapped memory; create its
   `Pipeline`/`Material` via `createMaterial()` against the shader
   pair `MaterialKind` maps to and the frame's own current
   `colorFormat` (a synchronous factory call, needs only the
   already-known `colorFormat` value, not a `RenderTarget`). Any one of
   these four sub-steps failing destroys everything created so far via
   ordinary RAII on early return (nothing has been submitted yet — safe
   to abandon unconditionally, mirroring Spec 0016/D5a's own "case 1"
   analysis) and leaves this material pending for a retry next frame —
   logged, never fatal to the frame.
3. If every pending material's own step 2 succeeds for at least one
   material this frame, record each one's texture-upload RenderGraph
   pass (`buildTextureUploadPass()`-shaped: `Undefined →
   TransferDestination`, `finalState = ShaderRead`, Spec 0016/ADR-0056)
   into the **same `CommandList`** this frame's real draw graph will
   also use, recorded **before** `Renderer::drawFrame()`'s own call —
   exactly `textured_quad_fixture.cpp`'s own already-proven two-graphs-
   one-`CommandList` ordering. Recording order alone (both graphs in
   the same `VkCommandBuffer`, upload before draw) is what makes the
   upload's own barrier complete before the draw's `cmd.bindTexture()`
   call executes on the GPU — no additional CPU-side synchronization is
   needed for this ordering guarantee; `Renderer::drawFrame()`'s own
   internal `RenderGraphBuilder` (confirmed directly,
   `renderer.cpp:20-54`) never itself declares the sampled texture as a
   tracked resource — `cmd.bindTexture()` is a raw call that simply
   requires the texture to already be `ShaderRead`, which the
   upload pass recorded immediately before it already guarantees.
4. Exactly **one** `Device::submit()` call per frame, covering the
   upload pass(es) (if any were recorded this frame) and the real draw
   graph together — never a second, separate `submit()`. This is
   unchanged from today's existing single-`submit()`-per-frame shape;
   a realization frame simply has more work recorded into the same one
   `CommandList` before that one call.
5. **Staging buffer lifetime — resolved, not left open:** immediately
   after a realization frame's own `submit()` succeeds and **before**
   `present()`, call `device_->waitIdle()` **only on frames where at
   least one material was newly realized this frame**. `waitIdle()`
   blocks until the just-submitted GPU work (upload and draw) has
   finished — at that point every staging `Buffer` this frame created
   is safe to destroy via ordinary RAII (function-scope, matching
   `textured_quad_fixture.cpp`'s own existing staging-buffer lifetime
   exactly), and the `SubmissionSignal` `submit()` returned already
   represents completed GPU work by the time `present()` receives it —
   a `present()`/`vkQueuePresentKHR` wait on an already-signaled
   semaphore is a legal, ordinary precondition, not a special case.
   This adds a one-time CPU stall **only** on the frame(s) a new,
   distinct material is first realized — never a per-frame cost once
   every currently-referenced material is realized. A cheaper,
   next-`submit()`-implicit-wait-based staging-buffer reclamation
   (relying on `Device::submit()`'s own documented "internally waits on
   ... any previously-retained submission before accepting this one"
   single-frame-in-flight behavior to avoid the stall) is real,
   legitimate future optimization, explicitly not attempted this round
   (Non-Goals) — correctness first, on a per-material one-time cost.
6. Only **after** `waitIdle()` returns `Ok` does this frame move the
   newly realized `SampledTexture`/`Sampler`/`Material` from their
   local variables into `textureResourceMap_`/`samplerResourceMap_`/
   `materialResourceMap_` (`AssetId`-keyed, mirroring
   `meshResourceMap_`'s own shape) — this is the one and only point a
   material becomes visible to any draw. If `submit()` itself fails,
   this is treated with the same severity `runFrame()`'s own existing
   plain-draw `submit()` failure already has today (`classifySubmitError()`
   + `lifecycle_.markFailed()`, `runtime_application.cpp:433-439`) — a
   failed `submit()` means the driver never accepted the `CommandList`
   at all (Spec 0016/D5a's own "case 2" analysis), so nothing from it,
   including the upload, executed; every locally-created object for
   this frame's realization attempts is destroyed via ordinary RAII on
   the same failure path, unconditionally safe.

**Per-entity fallback when a material stays unrealized:** an entity
whose `Renderable::materialAsset` is present but not yet a key in
`materialResourceMap_` (still pending, or permanently failing to
realize) is skipped for this frame's `DrawItem` list — the exact same
recoverable, per-entity `continue` pattern `resolveMeshAsset()`'s own
failure path already uses (`runtime_application.cpp:402-410`), not a
lifecycle-fatal error and not a silent substitution of Runtime's
built-in fallback Material (that fallback is reserved for
`materialAsset == std::nullopt`, per D4 — a *present-but-unrealized*
reference is a transient or failing state, not "no material," and
conflating the two would hide a real realization failure behind a
plausible-looking render).

**Resize/zero-extent/acquire-failure/DeviceLost/shutdown-with-pending-
realization — each already covered by an existing mechanism, not a new
one:** a zero-extent or internally-deferred `acquireNextTarget()`
result already returns from `runFrame()` before any of the above runs
(`runtime_application.cpp:288-290`) — a pending material simply stays
pending, retried next frame. `DeviceLost` surfacing through `submit()`
flows through the exact same `classifySubmitError()` path item 6 above
already relies on — no separate `DeviceLost` handling is added, because
none is needed: upload and draw share one `submit()` call, so
Runtime's own existing submit-failure classification already covers a
realization frame. `shutdown()`'s own existing `device_->waitIdle()`
(called whenever `lifecycle_.hasEverRun()`,
`runtime_application.cpp:460-467`) already drains any GPU work in
flight before any resource map is cleared — since a material only ever
enters `materialResourceMap_` after its own `waitIdle()` has already
completed (item 6), there is never a "shutdown while an upload is still
in flight" case to special-case: by the time any given material is in
the map at all, its own upload already finished.

**No global resource database, no hand-rolled rollback:** "pending
realization" is not a persisted data structure — it is recomputed each
frame as a set difference (currently-renderable materials minus
`materialResourceMap_`'s own current keys). The only new persistent
state is the three `AssetId`-keyed resource maps themselves, each
exactly `meshResourceMap_`'s own established shape.

### D9. GPU ownership/destruction order, and Material lifecycle across a swapchain format change

**Decision, part 1 — ownership/destruction order:** `SampledTexture`/
`Sampler` instances must outlive every `Material` that borrows them,
and both must be destroyed before `Device` — the exact contract
`Material`'s own header comment already states and
`textured_quad_fixture.cpp` already demonstrates structurally
(declaring `sampledTextureUnorm`/`sampledTextureSrgb`/`sampler`
*before* `materialUnorm`/`materialSrgb` in its own struct, so C++'s
reverse-declaration-order destruction destroys Materials first).
`RuntimeApplication`'s own member declaration order must place its new
`textureResourceMap_`/`samplerResourceMap_` **before** its new
`materialResourceMap_`, for the same reason. `device_` remains declared
first (per the existing `PlatformSession`-first / `Device`-early
pattern) so it outlives everything. `shutdown()`'s own existing,
ordered `.reset()`/`.clear()` sequence
(`runtime_application.cpp:469-474`) widens to clear the three new maps
in the same texture/sampler-before-material order, after `material_`
and before `device_.reset()`.

**Why this is not a new design question:** it is a direct application
of a contract this codebase already documents and already exercises
correctly in a real fixture — this Spec adds no new ownership model,
only a second real consumer of the existing one.

**Decision, part 2 — format-change rebuild extends from one `Material`
to every entry in `materialResourceMap_`, using the exact tradeoff
already accepted for the single-Material case:** `PipelineCreateParams::colorFormat`
is baked into a `Pipeline` at creation time (`types.h:186-201`) — a
`Material` built against one swapchain format cannot be redrawn
correctly into a `RenderTarget` of a different format. Today's single-
`material_` code already accepts this and already has a working,
Human-Review-precedented resolution: on a detected format change,
attempt to build a **new** `Pipeline`/`Material` first; only replace
the old one if the new one succeeds ("old Pipeline (if any) destroyed
HERE, only after the new one already succeeded",
`runtime_application.cpp:308-312`); if the rebuild fails, **keep
drawing with the old, now-format-mismatched `Material` and retry next
frame** — an explicitly accepted, already-shipped risk (a transient
wrong-but-drawable frame preferred over a black one), not a new
tradeoff this Spec invents.

This Spec extends the identical create-before-destroy-and-keep-old-on-
failure rule to **every** entry of `materialResourceMap_`, independently,
on the same format-change detection: for each already-realized
material, attempt a new `Pipeline`/`Material` against the new
`colorFormat` (`SampledTexture`/`Sampler` are **not** rebuilt — a
texture's own format, `Rgba8Unorm`/`Rgba8Srgb`, is entirely independent
of the swapchain's own `colorFormat`, confirmed directly:
`SampledTextureCreateParams`/`SamplerCreateParams` name no swapchain-
format field at all); on success, replace that one map entry
(old `Pipeline` destroyed only after the new one exists); on failure,
leave that one entry unchanged and retry it on a future frame,
independently of every other entry's own outcome — no entry's own
rebuild failure blocks or is coupled to any other's.

**Deduplication interacts with rebuild only as a key, not a value:**
because `materialResourceMap_` is keyed by material `AssetId`
(D10), a format-change rebuild replaces the `Material` **value** at an
existing key in place — it never changes which `AssetId`s are present,
and every entity already pointing at that `AssetId` (via
`DrawItem` extraction reading the map fresh each frame, never caching
a raw pointer across frames) automatically draws with the rebuilt
`Material` on the very next frame it succeeds.

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

### D12. Verification scene and golden — must exercise Runtime's real code, not a fixture-private lookalike

**Decision:** a new, minimal, independent scene authored specifically
for this Spec (not a reuse of `world_scene.scene.txt`, and not a
fixture-private, hand-built `Material` bypassing Asset System) —
one or two entities, each with a real `mesh=`/`material=` reference,
cooked through the real `cookScene()`/`cookMaterial()`/`cookTexture()`
pipeline, reusing Spec 0016's already-proven
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

**A deliberate, disclosed departure from this codebase's own existing
fixture-duplication precedent — required, not optional:**
`tests/image_regression/fixture/world_scene_loaded_fixture.cpp` today
independently *duplicates* `src/runtime/src/scene_load.cpp`'s own
mesh-loading steps, disclosed explicitly in its own top-of-file
comment ("duplicates ... D10 steps (a)-(g) independently -- not a
shared function"). That precedent is acceptable for mesh loading
because the duplicated logic is simple and stable. It is **not**
acceptable for this Spec's own Phase 2 GPU-realization logic (D8) —
upload timing, staging-buffer lifetime, and the two/three-state
`materialAsset` semantics (D4) are exactly the kind of easy-to-subtly-
diverge logic where a fixture's own "looks similar" reimplementation
could quietly stop proving what its golden claims to prove, and no one
would notice until a real Runtime regression shipped anyway. This
Spec's own new fixture must instead **directly link and call**
Runtime's own real `Atlantis::RuntimeHost` library — the real
`loadAndInstantiateScene()` (Phase 1) and the real Phase 2 per-frame
realization logic this Spec adds — not a fixture-local reimplementation
of either. `Atlantis::RuntimeHost` is already built as a library
precisely so its own internals are testable independent of a real
window (`tests/runtime/` already links it for this exact reason); this
Spec's own fixture linking it too is a natural extension of an
already-established pattern, not a new one. If Plan-time drafting finds
a real, structural reason this is not achievable (e.g. Phase 2's own
logic turns out to be inseparable from `RuntimeApplication`'s own
private frame-loop state), that is itself a Human-Review-relevant
finding to raise explicitly, not a license to quietly fall back to a
fixture-private duplicate.

**Required negative proofs — the golden must be shown to actually fail
when it should, not merely pass when everything is correct:**

- Removing the scene node's own `material=` reference (reverting to
  `std::nullopt`) must produce a visibly different, untextured frame
  (Runtime's own fallback `Material`) — a real, observable difference
  from the golden, not a coincidental pixel match.
- Corrupting the cooked material artifact's own embedded texture
  `AssetId` to reference a different, real texture must produce a
  visibly different (wrong-texture) frame.
- Bypassing the real material-asset path (e.g. hand-constructing a
  `SampledTexture`/`Material` in the test the way
  `textured_quad_fixture.cpp` still legitimately does for its own,
  different, non-scene-driven purpose) must **not** be an accepted
  substitute anywhere in this Spec's own new fixture or its own test
  cases — the whole point of this golden is that the pixels came from
  the real scene→material→texture asset path, not from a hand-built
  shortcut that happens to render the same bytes.

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
  clean across full verbose GPU test output, both configurations; D12's
  own three negative proofs (absent `material=` renders untextured;
  a corrupted texture reference renders a visibly different frame;
  no fixture-private `Material` bypass is used anywhere); a dedicated
  repeated-frame test proving a given material's own texture upload is
  recorded exactly once across many consecutive frames (e.g. by
  counting `buildTextureUploadPass()`-shaped calls, or an equivalent
  direct instrumentation) — not re-recorded every frame once realized,
  directly exercising D8's own "no re-upload of an already-`ShaderRead`
  texture" requirement.
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
- **D8's own deferred-realization design is genuinely new complexity**
  for this codebase — the first place a Runtime-owned resource map is
  populated incrementally across many frames rather than once, atomically,
  at scene-load time. The Plan must give this its own explicit,
  dedicated test coverage (a repeated-frame no-re-upload proof, a
  format-change-mid-realization interaction check, a realization-
  failure-then-retry-then-success sequence) rather than treating it as
  a minor extension of the existing mesh-loading tests.
- **The rejected next-`submit()`-implicit-wait staging-buffer
  optimization (ADR-0060's own Alternatives Considered) may look
  tempting to implement anyway during Implementation** for its own
  lower per-realization-frame cost — Implementation must not adopt it
  without returning to Human Review first, since it changes a real,
  disclosed lifetime-safety argument (D8's own `waitIdle()`-based
  guarantee) for one this Spec has not itself verified.

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

## Readiness for Human Review (2026-08-27 centralized final review)

A second, evidence-driven centralized review pass re-verified this
Draft's own architectural claims directly against real code the review
had not yet read line-by-line the first time — `RuntimeApplication::initializeSteps()`/
`runFrame()` in full, `Renderer::drawFrame()`'s own real
`RenderGraphBuilder` usage, `Device::submit()`'s and
`Presentation::present()`'s own real signatures, and
`shaders/textured_quad/CMakeLists.txt`'s own real `add_subdirectory()`
placement. It found and closed two genuine architectural gaps — both
fixable with real, evidenced designs, neither requiring an objection
that the recommended approach is unsupportable:

- **Closed, a real Must Fix, not a nuance (D8, and its ADR-0060
  mirror):** the original draft claimed real `SampledTexture`/
  `Sampler`/`Material` GPU resources could be constructed as part of
  `loadAndInstantiateScene()`'s own step (e), inside
  `initializeSteps()`. Confirmed directly this is architecturally
  impossible: `initializeSteps()` runs entirely before any real
  windowed `RenderTarget` exists (`Presentation` is not constructed
  until the first `SurfaceCreated` event, observed only inside
  `runFrame()`), and `Device::submit()`'s own real signature takes a
  non-null `RenderTarget&` with no target-independent overload — Spec
  0016's own "texture upload must go through RenderGraph against a
  real RenderTarget, no target-independent submission" constraint,
  applied literally, not merely cited. D8 is rewritten in full: CPU-only
  resolve/load stays in the atomic Phase 1 publish (unchanged in kind
  from mesh handling); GPU realization (`Sampler`, `SampledTexture`,
  staging-buffer upload, `Pipeline`/`Material` construction) is
  deferred to `runFrame()`'s own existing format-change check — the
  one real place a `RenderTarget` and a known `colorFormat` first
  coexist — sharing one `CommandList` and one `submit()` call with that
  frame's real draw graph, with staging-buffer lifetime resolved via an
  explicit `waitIdle()` on realization frames only (a one-time,
  disclosed CPU stall, not a per-frame cost), and a three-state
  `materialAsset` semantics (absent → fallback; present-but-unloadable
  → scene-load-fatal; present-but-not-yet-realized → per-frame,
  per-entity recoverable skip) made explicit in D4 to prevent these
  three states from ever being conflated in Implementation.
- **Closed, a real, disclosed CMake fix (D3):** `MaterialKind::UnlitTextured`
  mapping directly to `shaders/textured_quad/textured_quad.slang` as
  originally drafted would have silently made a shipping executable
  (`atlantis_runtime`) depend on a CMake target declared only inside
  the `if(ATLANTIS_BUILD_TESTS)` block — confirmed directly, that
  shader's own `add_subdirectory()` call is test-gated today, exactly
  the "silently promote a test-private shader to a Runtime public
  dependency" outcome this Spec is directed to avoid. Fixed by moving
  that one `add_subdirectory()` call to the same unconditional
  placement `shaders/minimal_renderer` already has, for the identical,
  already-precedented reason (`atlantis_runtime` needs it) — no new
  shader content, no duplicate `.slang` file, and the shader's own real
  reflected contract (camera uniform, push constant, vertex-input
  shape) confirmed compatible with Runtime's existing `Camera`/`DrawItem`
  contract with no change.

Additional findings, smaller in scope, closed in the same pass: D4 now
states the three-way `materialAsset` semantics explicitly rather than
leaving "present but not yet realized" implicit; D7 now accounts for
every `SceneManifestError` enumerator's own generalization and
explicitly resolves the "wrong asset kind" question (surfaces as
`BadMagic` one layer up, no dedicated enumerator needed) and the
"unused declared dependency" question (explicitly permitted, matching
`MESH_DEPENDENCIES`'s own existing tolerance); D6 gained a consolidated,
exhaustive accounting of every new error enumerator this Spec's own
format needs versus every existing one it reuses verbatim, plus an
explicit rejection of severity-collapsing (no recoverable error becomes
an assertion, no broad enumerator papers over a genuinely distinct
failure stage); D9 extends the already-accepted single-`Material`
format-change-rebuild tradeoff to every `materialResourceMap_` entry
independently; D12 now requires the new verification fixture to
directly link and call Runtime's own real `Atlantis::RuntimeHost`
code (a deliberate, disclosed departure from
`world_scene_loaded_fixture.cpp`'s own "duplicate, don't share"
precedent, justified because Phase 2's own realization logic is
exactly the kind of code a fixture-private lookalike could silently
stop proving) and lists explicit negative proofs the golden must
demonstrate, not merely a passing positive case.

No finding in this pass reversed this Spec's own core recommendations
(Material as a fourth Asset System asset type; a closed `MaterialKind`
shader-identity scheme; an optional `Renderable` material reference
with a hardcoded fallback) — every gap found had a real, evidenced fix
within the existing architecture, confirmed against real signatures and
real CMake files, not assumed. **This Spec, and both
[ADR-0059](../adr/0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)/
[ADR-0060](../adr/0060-scene-material-binding-and-runtime-transactional-resource-publish.md),
are assessed ready for a formal Human Review pass** — Spec 0018 remains
`In Review`, both ADRs remain `Proposed`, pending that review's own
approval, amendment, or rejection.
