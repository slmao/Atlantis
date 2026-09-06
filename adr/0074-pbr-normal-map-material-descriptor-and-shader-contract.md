# ADR 0074: PBR Normal Map — Material Schema, Renderer API, Descriptor Contract, and Shader Integration

- **Status:** Proposed
- **Date:** 2026-09-06
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — pending Human Review
- **Related Spec:** [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Draft`)
- **Related ADR(s):** [ADR-0073](0073-static-mesh-tangent-attribute-generation-and-schema.md)
  (the tangent vertex attribute this ADR's shaders consume — Proposed,
  same Spec), [ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (the existing PBR material asset schema this ADR extends),
  [ADR-0067](0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (the existing BRDF/push-constant contract this ADR reuses
  unmodified), [ADR-0064](0064-vulkan-backend-descriptor-pool-growth-ownership-model.md)
  (the descriptor-pool growth/capacity model this ADR widens by
  exactly one sampler slot), [ADR-0022](0022-renderer-material-ownership-and-drawitem-contract.md
  if present, else the ADR governing `Renderer`/`Material`'s own
  ownership contract — confirmed at Plan time; `material.h`'s own
  comments cite "ADR-0022" for `Material`'s one-Pipeline-ownership and
  borrowed-texture rules, reused verbatim by this ADR's own second
  borrowed-texture pair).

## Context

Confirmed directly against current `main`:

- `MaterialAssetData`/`DecodedMaterialArtifact`/`ParsedMaterialSource`/
  `MaterialMetadata` each carry exactly one texture reference,
  `textureAsset` (base color) — no second texture slot exists.
- `atlantis_material_source_version: 2` is an 8-line, fixed-field-order
  grammar; the material artifact is a fixed 56-byte record,
  `kMaterialArtifactSchemaVersion = 2`.
- `TextureColorSpace` (`texture_types.h:14-17`) already has exactly
  two values, `Unorm` and `Srgb` — `Unorm` is the existing, correct
  choice for non-color directional data (confirmed by direct
  inspection; no new enumerator needed).
- `sampledTextureBindingCountFor(MaterialKind, bool environmentEnabled)`
  (`material_realization.cpp:371-380`) is the one place deciding a
  Pipeline's own sampler count: `PbrDirectLit` is `2` without an
  environment (`pbr_direct_lit.slang`) or `4` with one
  (`pbr_ibl.slang`) — `selectShaderPair()`'s own scene-wide
  `environmentEnabled` flag decides which shader pair, never a
  per-material one.
- **The descriptor pool's own sampler capacity is a hard-coded
  ceiling**: `vulkan_device.cpp:433-438` sizes
  `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` at exactly `4 * maxSets`,
  and `vulkan_device.cpp:1000-1002` enforces
  `sampledTextureBindingCount ∈ {0,1,2,3,4}` via `ATLANTIS_CHECK`.
  `setLayoutBindings` itself (`vulkan_device.cpp:1059-1068`) is a
  `std::vector`, sized dynamically from `sampledTextureBindingCount`
  — no fixed-size array needs widening there; only the two numeric
  literals above (the `ATLANTIS_CHECK` ceiling and the pool's own
  `4 * maxSets` sizing) are hard-coded and must both become `5`.
- `descriptor_contract.cpp:45-63` confirms the exact current entry
  counts: `pbrDirectLitExpectedDescriptorContract()` has **4** entries
  (camera×2 stages, base-color, shadow-map); `pbrIblExpectedDescriptorContract()`
  has **6** entries (camera×2, base-color, environment, DFG LUT,
  shadow-map).
- `Renderer::drawFrame()` (`renderer.cpp:92-131`) **already computes a
  binding index conditionally on `environmentBinding()`** — the
  shadow-map binding is literally
  `item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl
  ? 4U : 2U` (`renderer.cpp:130`). This is the exact, already-
  established pattern a normal-map binding must extend, not a new
  kind of dispatch.
- `Material`/`createMaterial()` (`material.h:54-115`) already own
  exactly one **borrowed, non-owning, both-or-neither** texture+
  sampler pair (`sampledTexture_`/`sampler_`), checked once in the
  constructor, no setter, with an explicit ownership/destruction-order
  contract ("the caller-owning composition root ... must destroy this
  Material before destroying either of them"). This is the exact
  shape a second, normal-map texture+sampler pair should mirror.
- `pbr_direct_lit.slang`'s `VertexInput` has exactly 3 locations
  (position@0, uv@1, normal@2); its vertex shader transforms `normal`
  via `(float3x3)objectToWorld`, gated by `checkConformalTransform()`.
  `PushConstants` is 96 bytes, reused verbatim by `pbr_ibl.slang` via
  `MaterialPushConstantLayout::PbrDirectLit`.

## Decision

**One new, optional, per-material texture reference,
`normalMapTexture`, is added to the material schema. A `PbrDirectLit`
material carrying one selects a normal-map shader variant of whichever
shader its own scene-wide environment state would otherwise select —
`pbr_direct_lit_normal_map.slang` (no environment) or
`pbr_ibl_normal_map.slang` (environment enabled) — both consuming
ADR-0073's own tangent attribute. The descriptor pool's own sampler
ceiling widens from 4 to 5 per Pipeline to accommodate the IBL+
normal-map combination's own 5 samplers.**

### 1. Material schema

`MaterialAssetData`/`DecodedMaterialArtifact`/`MaterialMetadata` each
gain `AssetId normalMapTexture = 0;` (`0` = none, the same
"unassigned" convention `Renderable::meshAsset`'s own default already
establishes). `ParsedMaterialSource` (the pre-cook, logical-path-only
representation) gains `std::string normalMapLogicalPath;` (empty =
none) — a distinct field, distinct type, matching this struct's own
existing `textureLogicalPath` precedent exactly (a logical path, not
yet resolved to an `AssetId`; resolution to `AssetId` is exclusively
`cookMaterial()`'s own job, mirroring `textureLogicalPath`'s own
identical division of responsibility).

`atlantis_material_source_version` becomes `3`: the existing 8-line
form is unchanged and still valid (no normal map); a 9th, optional
trailing line, `normal_map: <logical-path>`, sets
`normalMapLogicalPath`. Every existing `.material.txt` file needs
exactly one line changed (its own version marker, `2` → `3`) — no
other line touched.

`kMaterialArtifactSchemaVersion` becomes `3`; the fixed record widens
from 56 to 64 bytes (append `normal_map_texture_asset_id`, 8 bytes, at
offset 56). Versions 1/2 rejected outright, no migration reader.

**Full dependency chain, named explicitly (every stage a normal-map
reference passes through):**

1. **Source parse/serialize:** `parseMaterialSource()`/
   `serializeMaterialSource()` read/write the optional 9th line into/
   from `ParsedMaterialSource::normalMapLogicalPath`.
2. **Cook / path normalization / AssetId resolution:** `cookMaterial()`
   normalizes `normalMapLogicalPath` via the exact same
   `normalizeLogicalPath()` + `computeAssetId()` pipeline
   `textureLogicalPath` already uses (ADR-0059 D6/D7) — when
   `normalMapLogicalPath` is empty, the resulting `AssetId` is `0` and
   no dependency is recorded; when non-empty, it becomes a **new,
   distinct scene dependency-manifest entry** (mirroring how
   `TEXTURE_DEPENDENCIES` already threads a base-color texture through
   `atlantis_add_scene_asset()`'s own manifest, `assets/CMakeLists.txt`).
3. **Artifact/metadata encode/decode/cross-validation:**
   `encodeMaterialArtifact()`/`decodeMaterialArtifact()` gain the new
   8-byte field; `MaterialMetadata`'s own sidecar gains the matching
   field; `loadMaterialAsset()`'s own existing metadata-vs-artifact
   cross-validation (already checking `kind`/`textureAsset`/factors)
   is extended to also cross-validate `normalMapTexture`, identically
   in kind.
4. **Texture loading/dedup:** Runtime's existing AssetId-keyed
   `textureDataMap`/`sampledTextureResourceMap` dedup (the same map a
   base-color texture shared across materials already dedups through)
   applies to `normalMapTexture` unchanged — no new cache, no new
   resource-map type.
5. **Sampler reuse (not a second sampler):** the normal map is sampled
   through the **same** `atlantis::rhi::Sampler` `createMaterial()`
   already builds from the material's own `filter`/`addressMode`
   fields for the base-color texture — one filter, one address mode,
   one sampler, two textures. This is the recommended, minimal design
   (no per-texture sampler parameters are introduced) and is hereby
   the fixed contract, not left to Plan-stage guessing.
6. **Color space:** the normal-map texture asset is cooked with
   `TextureColorSpace::Unorm` (directional data, never sRGB-decoded)
   — the existing enumerator, confirmed above; no new value.

### 2. Renderer public API extension (disclosed explicitly, not buried)

**`Material` gains a second, optional, borrowed, non-owning
texture+sampler pair — `normalMapTexture_`/`normalMapSampler_` —
mirroring `sampledTexture_`/`sampler_`'s own exact shape:**

- `Material`'s constructor gains two new trailing parameters,
  `const atlantis::rhi::SampledTexture* normalMapTexture = nullptr`
  and `const atlantis::rhi::Sampler* normalMapSampler = nullptr`,
  appended after the existing trailing parameters (every pre-existing
  call site compiles and behaves unchanged, defaulting to `nullptr`).
  `createMaterial()` gains the identical two trailing parameters.
- **Both-or-neither, checked once, in the constructor** — the exact
  same invariant `sampledTexture_`/`sampler_` already enforce, applied
  independently to this second pair (a Material may have neither
  texture pair, only the base-color pair, or both pairs — but never
  a normal-map texture with no sampler or vice versa).
- **Ownership/destruction-order contract, identical in kind to the
  existing one:** the caller-owning composition root — never
  `Material` itself — must keep any `SampledTexture`/`Sampler` passed
  as the normal-map pair alive for at least as long as this `Material`
  is used in any `drawFrame()` call, and must destroy this `Material`
  before destroying either of them. No new ownership model is
  introduced; this is the existing contract, applied twice.
- `Material` gains one new accessor pair,
  `normalMapTexture()`/`normalMapSampler()`, mirroring
  `sampledTexture()`/`sampler()` exactly.
- **`RealizedMaterialCandidate`** (`material_realization.h`'s own
  per-material realization-result struct) gains a second, optional
  `std::unique_ptr<atlantis::rhi::SampledTexture> newNormalMapTexture`
  field (mirroring its own existing `newSampledTexture` field exactly
  — `nullptr` when the normal map was already realized by an earlier
  material and is being reused from the dedup map, non-null exactly
  once, on first realization, matching `newSampledTexture`'s own
  identical "populated only on first upload" contract) and its own
  `AssetId normalMapTextureAssetId` (mirroring `textureAssetId`). The
  **sampler is not duplicated** — item 1.5 above already established
  one shared sampler per material, so no second sampler field is
  needed on this struct; the same `sampler` field already used for
  base-color is reused for the normal map too.
- **No bindless resource array, no material-graph abstraction, no
  generic N-texture mechanism** is introduced — this is exactly two
  named, explicit, optional texture+sampler pairs, matching this
  codebase's own existing "one combined-image-sampler descriptor per
  Pipeline, per binding, hand-declared" discipline throughout.

`Renderer::drawFrame()` binds the normal map **at the same
conditional-index pattern the shadow map already establishes**
(`renderer.cpp:130`'s own exact shape, extended by one more line):

```cpp
if (item.material->normalMapTexture()) {
  const std::uint32_t normalMapBinding =
      item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 5U : 3U;
  cmd.bindTexture(normalMapBinding, *item.material->normalMapTexture(), *item.material->normalMapSampler());
}
```

### 3. Descriptor pool capacity — a real, disclosed capacity change

**The descriptor pool's own hard-coded sampler ceiling widens from 4
to 5 per Pipeline** — the one, single change this ADR makes to
ADR-0064's own capacity model, confirmed necessary because the IBL+
normal-map combination needs 5 sampler bindings (base-color@1,
environment@2, DFG LUT@3, shadow-map@4, normal-map@5), one past the
existing ceiling:

- `vulkan_device.cpp:1000-1002`'s own `ATLANTIS_CHECK` widens from
  `∈ {0,1,2,3,4}` to `∈ {0,1,2,3,4,5}`.
- `vulkan_device.cpp:438`'s own `poolSizes[1].descriptorCount = 4U *
  maxSets` becomes `5U * maxSets`.
- `sampledTextureBindingCountFor()` returns: `3` for `PbrDirectLit` +
  normal map, no environment (base-color@1, shadow-map@2, normal-
  map@3); `5` for `PbrDirectLit` + normal map + environment
  (base-color@1, environment@2, DFG LUT@3, shadow-map@4,
  normal-map@5). Materials without a normal map are completely
  unaffected (`2`/`4`, unchanged).
- **This is a per-pool sampler-*capacity* change, not a descriptor-
  *set-count* change** — the existing `N+4`/`N+5` steady/peak
  **set-count** formula (`material_realization_gpu_tests.cpp`'s own
  `N=6` test) is unaffected in its own arithmetic: one material still
  contributes exactly one Pipeline/one descriptor set, regardless of
  how many sampler bindings that one set declares. What changes is
  how many *descriptors of the sampler type* the pool must reserve
  per set (`4 * maxSets` → `5 * maxSets`), a completely different axis
  the existing set-count test does not, and cannot, exercise.
- **A new, real verification requirement, not a reuse of the existing
  set-count test:** a real Vulkan Device must actually create a
  Pipeline whose own `sampledTextureBindingCount == 5` and a real
  descriptor set with all 5 sampler bindings bound, then submit a real
  draw referencing it, confirmed under Vulkan Validation Layers — the
  `N+4`/`N+5` test only proves *set-count* headroom against the pool's
  own `maxSets` ceiling, and says nothing about whether a single
  Pipeline's own 5-sampler descriptor-set-layout is itself valid or
  whether the pool's own widened `5 * maxSets` sampler-type sizing is
  sufficient — a materially different property, requiring its own
  new test (Spec 0029's own Testing & Verification Plan).

### 4. New descriptor contracts (real entry counts, not estimated)

- `pbrDirectLitNormalMapExpectedDescriptorContract()` — **5 entries**
  (camera×2 stages, base-color, shadow-map, normal-map — one more
  than `pbrDirectLitExpectedDescriptorContract()`'s own confirmed 4).
- `pbrIblNormalMapExpectedDescriptorContract()` — **7 entries**
  (camera×2 stages, base-color, environment, DFG LUT, shadow-map,
  normal-map — one more than `pbrIblExpectedDescriptorContract()`'s
  own confirmed 6).

### 5. Two new shader files, both reusing the identical push-constant layout

Neither existing shader (`pbr_direct_lit.slang`, `pbr_ibl.slang`) is
edited — both new files are additions, zero risk to any existing
golden:

- **`pbr_direct_lit_normal_map.slang`** — identical
  `CameraUniform`/`PushConstants`/BRDF math to `pbr_direct_lit.slang`,
  `VertexInput` gains `[[vk::location(3)]] float4 tangent`,
  `normalMapSampler` at `[[vk::binding(3, 0)]]`.
- **`pbr_ibl_normal_map.slang`** — identical to `pbr_ibl.slang`,
  `VertexInput` gains the same location-3 tangent, `normalMapSampler`
  at `[[vk::binding(5, 0)]]`.
- Both: `vertexMain()` transforms `tangent.xyz` via the **same**
  `(float3x3)objectToWorld` and the **same** `checkConformalTransform()`
  gate the existing normal transform already relies on (never a new
  or different transform path); `tangent.w` passes through unmodified.
  `fragmentMain()` reconstructs an orthonormal TBN basis in the
  fragment stage (a second, fragment-stage Gram-Schmidt pass,
  independent of ADR-0073's own cook-time one — per-vertex
  interpolation across a triangle does not preserve exact
  orthogonality), samples the normal map per the fixed tangent-space
  convention below, and substitutes the perturbed normal for the
  geometric one everywhere the existing BRDF/IBL math already
  references it — no other line of either shader's own accumulation
  math changes.
- **No push-constant change** — both reuse
  `MaterialPushConstantLayout::PbrDirectLit` and the identical 96-byte
  layout verbatim; no per-draw "normal map strength" scalar (a normal
  map, when present, always applies at full strength).

### 6. Fixed tangent-space convention — one convention, stated once

- **OpenGL-style, `+Y`-up tangent-space normal map.** The stored RGB8
  Unorm texel is mapped `[0,1] → [-1,1]` per channel by the direct,
  linear formula `n = texel * 2.0 - 1.0` — no channel is ever flipped
  by the shader (in particular, the green/`Y` channel is never
  inverted; a normal-map texture authored in the opposite, DirectX
  `-Y` convention is out of scope and must be re-exported by its own
  author before use, not silently corrected here).
- **Bitangent:** `B = cross(N, T) * tangent.w` — `tangent.w` is
  ADR-0073's own cook-time handedness, never re-derived in the shader.
- **TBN matrix:** columns `[T, B, N]` (or the row-equivalent,
  compiler-convention-dependent or equivalently expressed) transforms
  the sampled tangent-space normal into world space; the perturbed
  world normal replaces `N` in the existing BRDF/IBL accumulation
  unchanged.
- A mirrored (negative-determinant) object transform still produces
  incorrect chirality under this convention — ADR-0073's own disclosed
  limitation, not solved here (Spec 0029's own Open Questions).

## Consequences

### Positive

- Supports the feature on both the direct-lit and IBL paths — no
  scene is forced to choose between "environment" and "normal map,"
  the real reason the earlier direct-only-scoped draft could not
  demonstrate the feature in the project's own default showcase scene.
- Every capacity number in this ADR (4→5 ceiling, `4*maxSets`→
  `5*maxSets`, 4→5/6→7 contract entry counts) is a confirmed, exact
  figure from current source, not an estimate.
- Two new shader files, zero edits to either existing PBR shader —
  zero risk to any currently-committed golden.
- The Renderer API extension is a second instance of an already-
  reviewed, already-`Accepted` pattern (one more borrowed, both-or-
  neither, non-owning texture+sampler pair) — not a new ownership
  model, not a new binding mechanism.

### Negative / Trade-offs

- A real, disclosed widening of `Renderer`'s own public `Material`/
  `createMaterial()` API and of the descriptor pool's own capacity
  model (ADR-0064) — both real architectural surfaces, not
  cosmetic additions, and both now explicit in this ADR rather than
  implied.
- Two new shader files (not one) and two new descriptor-contract
  functions (not one) — real, larger Implementation scope than a
  direct-only design, in exchange for the feature actually working in
  an environment-enabled scene.
- A new, dedicated 5-sampler Pipeline/descriptor-capacity test is
  required — the existing `N+4`/`N+5` set-count test cannot stand in
  for it (item 3 above).
- Per-material shader selection (not scene-wide) is a real widening of
  `selectShaderPair()`'s own dispatch contract — every call site
  currently passing only `environmentEnabled` must also thread the
  calling material's own `normalMapTexture` presence.

## Alternatives Considered

- **Scoping to the no-environment path only** (the earlier draft of
  this ADR). Rejected: leaves the feature unable to appear in any
  environment-enabled scene, including the project's own default
  showcase scene — the real reason this revision widens scope to
  cover both paths, at the real, disclosed cost of a genuine
  descriptor-pool-capacity change.
- **Widening the sampler ceiling further than 5** (e.g. to leave
  headroom for a future second optional texture). Rejected as
  speculative — this ADR sizes the ceiling to the one, real,
  confirmed-necessary combination (IBL + normal map = 5), not a
  guessed future need.
- **A per-material boolean baked into a shader `#ifdef`/specialization
  constant**, avoiding a second shader file. Rejected: no shader
  specialization-constant mechanism exists in this codebase today;
  two new files following `pbr_ibl.slang`'s own established
  "new shader variant, not a branch inside the existing one" pattern
  is smaller real scope than introducing one.
- **A single, combined shader with a runtime-bound dummy normal-map
  texture** for materials without one. Rejected: wastes a descriptor
  binding and a real texture upload for the common case (no normal
  map), and still requires the same descriptor-contract/Pipeline
  branching this ADR's own two-shader design already provides more
  directly.
- **A bindless/material-graph-style generic texture array.** Rejected
  per this Spec's own explicit instruction and this codebase's own
  established, hand-declared-binding discipline throughout — two
  named, explicit texture slots is not a general mechanism.
