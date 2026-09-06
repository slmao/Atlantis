# ADR 0074: PBR Normal Map — Material Schema, Renderer API, Descriptor Contract, and Shader Integration

- **Status:** Proposed
- **Date:** 2026-09-06
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — pending Human Review
- **Related Spec:** [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Draft`)
- **Related ADR(s):** [ADR-0073](0073-static-mesh-tangent-attribute-generation-and-schema.md)
  (the tangent vertex attribute this ADR's shaders consume — Proposed,
  same Spec), [ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (the existing PBR material asset schema this ADR extends, including
  its own item 6 base-color-texture-color-space cross-validation
  precedent this ADR mirrors for the normal map), [ADR-0067](0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (the existing BRDF/push-constant contract this ADR reuses
  unmodified), [ADR-0072](0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
  D-7 (the **authoritative source** of today's 4-sampler-per-Pipeline
  ceiling, `4 * maxSets` pool sizing, and 5-slot
  `textureDescriptorMemos_` array — this ADR's own Proposed Amendment,
  filed alongside this ADR, widens each of those three numbers by one;
  see that ADR's own end), [ADR-0064](0064-vulkan-backend-descriptor-pool-growth-ownership-model.md)
  (the descriptor-**set**-count/pool-growth/ownership model — a
  different axis from ADR-0072 D-7's per-set sampler-**type** capacity;
  unaffected by this ADR, needs no amendment), [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)
  (`Material`'s one-Pipeline-ownership and "`Renderer` never creates a
  `Material`" rules, reused unmodified), [ADR-0056](0056-texture-upload-resource-state-and-descriptor-binding.md)
  Decision item 8 (the actual source of `Material`'s existing
  "optional, construction-time, borrowed — never owned —
  `SampledTexture`/`Sampler` pair" contract this ADR extends by one
  more borrowed texture pointer).

## Context

Confirmed directly against current `main`:

- `MaterialAssetData`/`DecodedMaterialArtifact`/`ParsedMaterialSource`/
  `MaterialMetadata` each carry exactly one texture reference,
  `textureAsset` (base color) — no second texture slot exists.
- `atlantis_material_source_version: 2` is a fixed-field-order grammar
  with exactly two legal shapes today — **5 lines** (version, kind,
  texture, filter, address_mode; every `kind` value legal) or **8
  lines** (the 5 above plus `base_color_factor`/`metallic_factor`/
  `roughness_factor`, fixed order) — confirmed directly against
  `material_source.cpp`'s own `kMinLineCount = 5`/`kMaxLineCount = 8`
  and against real, committed files
  (`unlit_textured_quad.material.txt`/`lit_textured_quad.material.txt`
  are 5-line; every `pbr_*.material.txt` is 8-line). The material
  artifact is a fixed 56-byte record, `kMaterialArtifactSchemaVersion = 2`.
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
- **Three separate, real, hard-coded capacity limits — all fixed by
  [ADR-0072](0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
  D-7, not by ADR-0064** (confirmed by direct reading of D-7's own
  text, which names all three by number): `vulkan_device.cpp:433-438`
  sizes `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` at exactly
  `4 * maxSets`; `vulkan_device.cpp:1000-1002` enforces
  `sampledTextureBindingCount ∈ {0,1,2,3,4}` via `ATLANTIS_CHECK`; and
  `VulkanCommandList::textureDescriptorMemos_`
  (`vulkan_command_list.h:177`) is `std::array<TextureDescriptorMemo, 5>`
  — `bindTexture()` (`vulkan_command_list.cpp:432,529`) asserts
  `ATLANTIS_CHECK(binding < textureDescriptorMemos_.size())` before
  every use, so binding index 5 fails this check outright at the
  current size. `setLayoutBindings` itself
  (`vulkan_device.cpp:1059-1068`) is a `std::vector`, sized dynamically
  from `sampledTextureBindingCount` — no fixed-size array needs
  widening there. All three numbers must widen by one for this ADR's
  own binding-5 (`pbr_ibl` + normal map) case; [ADR-0072's own Proposed
  Amendment — 2026-09-06](0072-directional-shadow-map-resource-pass-and-pbr-integration.md#proposed-amendment--2026-09-06),
  filed alongside this ADR, is the authoritative source for this
  widening — this ADR states the consequences, not the numbers
  themselves a second time.
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
  Material before destroying either of them"). This ADR's own normal-
  map addition (Section 2) extends this contract with one more
  borrowed *texture* pointer only — not a second pair — since the
  normal map is fixed to reuse this same `sampler_` (Section 1 item 5).
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

`atlantis_material_source_version` becomes `3` — **three legal shapes,
line-count-exact, matching `parseMaterialSource()`'s own existing
"no partial subset" discipline exactly:**

- **5 lines** (version, kind, texture, filter, address_mode) — every
  `kind` value legal (unchanged from today's real behavior, confirmed
  directly: `unlit_textured_quad.material.txt`/`lit_textured_quad.material.txt`
  are both real, committed 5-line files). No normal map.
- **8 lines** (the 5 above + `base_color_factor`/`metallic_factor`/
  `roughness_factor`, fixed order) — unchanged from today's real
  behavior (every committed `pbr_*.material.txt` file is this shape).
  No normal map.
- **9 lines** — the 8-line form plus a trailing `normal_map:
  <logical-path>` line. **`kind` must be `pbr_direct_lit`** — a 9-line
  file naming `unlit_textured`/`lit_textured` is rejected
  deterministically with a new, distinct
  `MaterialSourceParseError::NormalMapNotSupportedForKind` (checked in
  `parseMaterialSource()` once both `kind`, line 1, and the presence of
  a 9th line are known — neither `lit_textured.slang` nor
  `unlit_textured.slang` declares a normal-map binding, so accepting
  this combination would silently parse a field with no consumer). An
  empty `normal_map:` value (the line present, nothing after the
  prefix) is rejected with the existing `MaterialSourceParseError::MissingField`
  — the same rule `texture:`'s own empty-value case already uses,
  applied identically here (an author who wants "no normal map" omits
  the line entirely, exactly like every other optional field in this
  grammar).
- The maximum line count (`kMaxLineCount` in `material_source.cpp`)
  becomes `9`, up from `8`; a 10-or-more-line file continues to be
  rejected with the existing `MaterialSourceParseError::TrailingContent`,
  unchanged in kind.

Every existing `.material.txt` file needs exactly one line changed
(its own version marker, `2` → `3`) — no other line touched.

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
6. **Color space, authored and cross-validated, not merely a cooking
   convention:** the normal-map texture asset is cooked with the
   existing `TextureColorSpace::Unorm` enumerator (directional data,
   never sRGB-decoded) — no new value. Mirroring
   [ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
   item 6's own base-color-texture `Rgba8Srgb` requirement exactly (a
   `cookMaterial()`-time check is impossible — `cookMaterial()` never
   resolves its own texture reference, per ADR-0059 Decision 7): a
   normal map whose resolved `TextureAssetData::colorSpace` is `Srgb`
   fails scene load with a new `RuntimeInitError` sub-code,
   `PbrNormalMapTextureNotUnorm`, checked at the same point ADR-0066
   item 6's own check runs — Runtime's existing Phase 1
   scene-dependency-resolution step (`scene_load.cpp`, ADR-0060
   Decision 6), only when `normalMapTexture != 0`. A material with no
   normal map is completely unaffected.

### 2. Renderer public API extension — one new borrowed pointer, no second sampler

**`Material` gains exactly one new, optional, borrowed, non-owning
member, `normalMapTexture_` — not a second texture+sampler pair.**
Section 1 item 5 above already fixed the normal map to reuse the
material's own existing base-color sampler; introducing a second
`Sampler*` member would contradict that decision by implying a second,
real sampler slot that nothing ever populates differently. The single
new pointer's own preconditions are that the *existing* base-color
pair is present, and that this `Material`'s own push-constant layout
is `PbrDirectLit` (the only layout the normal-map shaders use):

- `Material`'s constructor gains one new trailing parameter,
  `const atlantis::rhi::SampledTexture* normalMapTexture = nullptr`,
  appended after the existing trailing parameters (every pre-existing
  call site compiles and behaves unchanged, defaulting to `nullptr`).
  `createMaterial()` gains the identical trailing parameter.
- **Two invariants, both checked once, in the constructor, alongside
  the existing both-or-neither check:**

  ```cpp
  ATLANTIS_CHECK(normalMapTexture_ == nullptr || sampledTexture_ != nullptr);
  ATLANTIS_CHECK(normalMapTexture_ == nullptr ||
                 pushConstantLayout_ == MaterialPushConstantLayout::PbrDirectLit);
  ```

  The first: a normal map may never be constructed without the
  base-color pair also present, since both are sampled through the
  one, same `sampler_`. The second, new in this round: a normal map
  may only be constructed on a Material whose push-constant layout is
  `PbrDirectLit` — the only layout the two normal-map shaders
  (`pbr_direct_lit_normal_map.slang`/`pbr_ibl_normal_map.slang`) use
  (Section 5). Without this check, a caller could construct an
  `UnlitTextured`/`LitTextured` Material (`ObjectToWorldOnly`) with a
  non-null `normalMapTexture`; `Renderer::drawFrame()`'s own binding
  logic (below) would still attempt `cmd.bindTexture(3 or 5, ...)`
  against a Pipeline whose real shader never declared that binding — a
  real Vulkan descriptor-binding mismatch, caught here instead, at
  construction time. Both are precondition violations (programmer
  errors, per AGENTS.md's error-handling rules), not recoverable
  `Result` errors — identical in kind to the existing
  `ATLANTIS_CHECK((sampledTexture_ == nullptr) == (sampler_ == nullptr));`
  they sit beside.
- **A third precondition, documented, not mechanically checked:** a
  non-null `normalMapTexture` also requires the caller's own
  `PipelineCreateParams` to have built a Pipeline from
  `pbr_direct_lit_normal_map.slang` (declaring binding 3) when
  `environmentBinding() == None`, or from `pbr_ibl_normal_map.slang`
  (declaring binding 5) when `environmentBinding() == Ibl` — i.e., the
  *correct* normal-map shader variant for that Material's own
  environment state, not merely *a* shader that happens to declare a
  binding at that index. `atlantis::rhi::Pipeline` has no descriptor-
  layout introspection API today (confirmed by direct inspection of
  `pipeline.h`), so this cannot be mechanically verified inside
  `Material`'s own constructor without adding one — out of scope for
  this ADR (no new RHI query, no runtime reflection). This is
  documented as a caller precondition, exactly like the existing
  ownership/destruction-order contract below is a documented, not
  mechanically enforced, precondition. **Runtime's own real path
  satisfies this by construction, not by convention alone** — see the
  dedicated paragraph after this list.
- **Ownership/destruction-order contract, identical in kind to the
  existing one, applied to this one additional pointer:** the
  caller-owning composition root — never `Material` itself — must keep
  the `SampledTexture` passed as `normalMapTexture` alive for at least
  as long as this `Material` is used in any `drawFrame()` call, and
  must destroy this `Material` before destroying it. No new ownership
  model is introduced.
- `Material` gains exactly one new accessor, `normalMapTexture()`,
  mirroring `sampledTexture()`. **No `normalMapSampler()` accessor
  exists** — there is nothing for it to return that `sampler()` does
  not already provide.
- **No bindless resource array, no material-graph abstraction, no
  generic N-texture mechanism** is introduced — this is exactly one
  named, explicit, optional texture pointer added to an existing,
  already-reviewed borrowed-pointer contract (ADR-0056 item 8).

**Runtime's own real path constructively guarantees both the
push-constant-layout invariant and the Pipeline-selection precondition
— not merely by caller discipline:** Section 1's own material-grammar
restriction (a 9-line `normal_map:` line is legal only for
`kind: pbr_direct_lit`, rejected outright for any other kind at parse
time) means `MaterialAssetData::normalMapTexture != 0` implies
`kind == PbrDirectLit` for every material that ever reaches
`realizeOneMaterialCandidate()` — there is no code path that can
produce the opposite combination. `pushConstantLayoutFor(materialData.kind)`
(confirmed by direct reading, `material_realization.cpp:137-148`)
already returns `PbrDirectLit` if and only if `kind == PbrDirectLit`,
so the second new `ATLANTIS_CHECK` above can never fire on Runtime's
own real path. `selectShaderPair()` (this ADR's own Decision preamble
above; the two new shader files themselves are Section 5 below) is
extended so that, for `kind == PbrDirectLit`, it consults
`normalMapTexture != 0`
*before* the existing `environmentEnabled` dispatch and selects
`pbr_direct_lit_normal_map.slang`/`pbr_ibl_normal_map.slang`
accordingly, while `UnlitTextured`/`LitTextured`'s own dispatch
branches never consult `normalMapTexture` at all (it is always `0` for
those kinds, per the grammar restriction) — the *correct* Pipeline for
the Material's own `environmentBinding()` is therefore always the one
`realizeOneMaterialCandidate()` passes to `createMaterial()` alongside
that same `normalMapTexture`, closing the third precondition above for
every real call site without any new introspection mechanism.

`Renderer::drawFrame()` binds the normal map **at the same
conditional-index pattern the shadow map already establishes**
(`renderer.cpp:130`'s own exact shape), reusing `material.sampler()` —
**the same `VkSampler` handle already bound at binding 1 for base
color** — for the normal map's own separate binding index. Binding the
identical sampler handle to two different descriptor bindings in the
same set is ordinary, valid Vulkan usage, not a new RHI capability:

```cpp
if (item.material->normalMapTexture() != nullptr) {
  const std::uint32_t normalMapBinding =
      item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 5U : 3U;
  cmd.bindTexture(normalMapBinding, *item.material->normalMapTexture(), *item.material->sampler());
}
```

**New verification requirement — GPU-independent, no Vulkan Device
needed, since both new `ATLANTIS_CHECK`s live in `Material`'s own
constructor.** `ATLANTIS_CHECK` never aborts the calling test process
by itself — its default handler does, but the handler is replaceable
(`atlantis::assertions::setFailureHandler()`, `assert.h`), and this
codebase's own existing `ScopedFailureHandler` RAII helper
(`tests/renderer/renderer_ownership_tests.cpp:49-58`) already installs
a capturing handler for exactly this purpose, confirmed by direct
reading — this is the real, established pattern to reuse, not a
GoogleTest-style process-death test (this codebase uses Catch2, and
has no such mechanism). Three cases, all GPU-independent, all
following this exact existing pattern:

1. Install a `ScopedFailureHandler`; construct `Material` with a
   non-null `normalMapTexture` and a null `sampledTexture`/`sampler`;
   assert exactly one failure was captured (the first new
   `ATLANTIS_CHECK` fired; both checks use the plain, no-message
   `ATLANTIS_CHECK` form, so the captured `AssertFailureInfo::expression`
   — not `::message`, which is empty for this macro form — is the
   field a test matches against, mirroring how `assert_tests.cpp`'s own
   plain-`ATLANTIS_CHECK` test already asserts on `expression`
   directly, e.g. `recorded[0].expression == "1 == 2"`).
2. Install a `ScopedFailureHandler`; construct `Material` with a
   non-null `normalMapTexture`, a valid base-color pair, and
   `pushConstantLayout = MaterialPushConstantLayout::ObjectToWorldOnly`;
   assert exactly one failure was captured (the second new
   `ATLANTIS_CHECK`).
3. Construct `Material` with a non-null `normalMapTexture`, a valid
   base-color pair, and
   `pushConstantLayout = MaterialPushConstantLayout::PbrDirectLit`,
   once for `MaterialEnvironmentBinding::None` and once for `Ibl` —
   no `ScopedFailureHandler` needed; both constructions must complete
   with zero captured failures, confirming the invariant's own legal
   region is not itself accidentally narrowed.

### 2a. `RealizedMaterialCandidate` and failure rollback — closed, not deferred

`RealizedMaterialCandidate` (`material_realization.h:66-73`) gains
three new, explicitly-named fields, mirroring its own existing
base-color-texture fields exactly in kind:

```cpp
atlantis::asset_system::AssetId normalMapTextureAssetId = 0;
std::unique_ptr<atlantis::rhi::SampledTexture> newNormalMapTexture;  // nullptr if textureAssetId is 0, or already realized
std::optional<std::unique_ptr<atlantis::rhi::Buffer>> normalMapStagingBuffer;  // present iff newNormalMapTexture is non-null
```

No second sampler field — Section 1 item 5/Section 2 above already
established the single shared `sampler` field covers both textures.

**Realization order inside `realizeOneMaterialCandidate()`:** after the
existing base-color texture/staging-buffer attempt and before the
existing `device.createSampler(...)` call, attempt the normal map's
own texture/staging-buffer creation using the identical dedup-then-
create logic the base-color texture already uses (`effectiveSampledTextures`
lookup by `normalMapTextureAssetId`; skip entirely, leaving
`newNormalMapTexture` as `nullptr`, when `normalMapTextureAssetId == 0`
— no normal map on this material). `createMaterial()`'s own call is
extended with the new trailing `normalMapTexture` pointer (Section 2).

**Failure rollback — the existing mechanism, unchanged, not a new one:**
`realizeOneMaterialCandidate()` already builds its result inside one
local, stack-allocated `RealizedMaterialCandidate candidate;` and
returns `ResultT::Err(...)` immediately on any sub-step's failure
(confirmed by direct reading, `material_realization.cpp:165-255`) —
every already-constructed member up to that point (a real
`unique_ptr<SampledTexture>`, a real `unique_ptr<Buffer>`, a real
`unique_ptr<Sampler>`) is destroyed by `candidate`'s own destructor
during the early return's stack unwind, via ordinary RAII, since
nothing has been recorded into a RenderGraph or submitted yet. Adding
the two new `unique_ptr`/`optional<unique_ptr>` members changes
nothing about this mechanism — a failure at any point (base-color
texture creation, staging buffer, **normal-map texture creation**,
**normal-map staging buffer**, sampler, or `createMaterial()` itself)
unwinds the same local `candidate`, destroying every resource created
so far in this one attempt, and the caller
(`realizePendingMaterials()`) already logs and leaves the material
pending, retried next frame (`material_realization.cpp:324-327`),
never touching any persistent resource map. **A candidate is never
partially published** — `realizePendingMaterials()` only moves a
candidate's members into the resource maps after that frame's own
`submit()` and conditional `waitIdle()` both succeed (ADR-0060
Decision 6, ADR-0056 item 6): `sampler`/`material` into
`samplerResourceMap_`/`materialResourceMap_` as today, and
**`newNormalMapTexture` into the same, existing
`sampledTextureResourceMap_`** the base-color texture already uses,
keyed by `normalMapTextureAssetId` — the map is already keyed by
texture `AssetId` regardless of which material field references it, so
no second, `normalMapTextureResourceMap_`-shaped map is needed (Section
1 item 4's own "no new cache, no new resource-map type" holds exactly).
A real Vulkan submission failure on a realization frame is
handled with the same, already-existing severity every other
`submit()` failure has, unconditionally safe by the same argument
ADR-0060 Decision 6 already makes for the base-color texture. This
closes Spec 0029's own prior Open Question about staging-buffer
ownership under a mid-frame failure — it is not deferred to Plan, it
is exactly the mechanism already in place for the base-color texture,
extended to a second texture with zero new machinery.

### 3. Descriptor pool capacity — three real, disclosed limits, all widened by one

**Confirmed necessary because the IBL+normal-map combination needs 5
sampler bindings (base-color@1, environment@2, DFG LUT@3, shadow-map@4,
normal-map@5), one past every existing ceiling.** All three numbers
below are fixed by
[ADR-0072](0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
D-7, and are widened by that same ADR's own [Proposed Amendment —
2026-09-06](0072-directional-shadow-map-resource-pass-and-pbr-integration.md#proposed-amendment--2026-09-06)
filed alongside this ADR — restated here as consequences, not
re-decided:

1. `vulkan_device.cpp:1000-1002`'s own `ATLANTIS_CHECK` widens from
   `∈ {0,1,2,3,4}` to `∈ {0,1,2,3,4,5}`.
2. `vulkan_device.cpp:438`'s own `poolSizes[1].descriptorCount = 4U *
   maxSets` becomes `5U * maxSets`.
3. `VulkanCommandList::textureDescriptorMemos_` widens from
   `std::array<TextureDescriptorMemo, 5>` to
   `std::array<TextureDescriptorMemo, 6>` — without this, every call to
   `cmd.bindTexture(5, ...)` (the `pbr_ibl` + normal-map case) fails
   `ATLANTIS_CHECK(binding < textureDescriptorMemos_.size())`
   unconditionally, regardless of items 1/2 above being fixed. This is
   an internal `VulkanCommandList` capacity bump, not a public API
   change — and it is a genuinely separate limit from items 1/2: a
   Pipeline can be created and a descriptor set allocated successfully
   while this one, distinct array bound still rejects the actual
   `bindTexture()` call at binding 5.

`sampledTextureBindingCountFor()` returns: `3` for `PbrDirectLit` +
normal map, no environment (base-color@1, shadow-map@2, normal-map@3);
`5` for `PbrDirectLit` + normal map + environment (base-color@1,
environment@2, DFG LUT@3, shadow-map@4, normal-map@5). Materials
without a normal map are completely unaffected (`2`/`4`, unchanged).

**This is a per-pool sampler-*capacity* change (items 1/2) plus one
`CommandList`-internal array bump (item 3), not a descriptor-*set-
count* change** — the existing `N+4`/`N+5` steady/peak **set-count**
formula (`material_realization_gpu_tests.cpp`'s own `N=6` test) is
unaffected in its own arithmetic: one material still contributes
exactly one Pipeline/one descriptor set, regardless of how many
sampler bindings that one set declares or how large
`textureDescriptorMemos_` is. What changes is how many *descriptors of
the sampler type* the pool must reserve per set, and how large one
internal per-`CommandList` bookkeeping array must be — two axes the
existing set-count test does not, and cannot, exercise.

**A new, real verification requirement, not a reuse of the existing
set-count test, and one that must specifically exercise binding 5:** a
real Vulkan Device must create a Pipeline whose own
`sampledTextureBindingCount == 5`, allocate a real descriptor set, and
actually call `cmd.bindTexture(5, ...)` (the normal-map binding under
`pbr_ibl`) before submitting a real draw, confirmed under Vulkan
Validation Layers. This exercises all three widened limits together —
item 3 above is invisible to a test that only creates the Pipeline/
descriptor set and never calls `bindTexture()` at binding 5 itself.
The `N+4`/`N+5` test only proves *set-count* headroom against the
pool's own `maxSets` ceiling, and says nothing about whether a single
Pipeline's own 5-sampler descriptor-set-layout is valid, whether the
pool's own widened `5 * maxSets` sampler-type sizing is sufficient, or
whether `textureDescriptorMemos_` is large enough for a real
`bindTexture(5, ...)` call — three materially different properties,
requiring this one new test (Spec 0029's own Testing & Verification
Plan).

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
  `VertexInput` gains `[[vk::location(3)]] float4 tangent`, a
  `normalMapSampler` combined-image-sampler declared at
  `[[vk::binding(3, 0)]]`. This is a shader-side binding *declaration*
  only — Section 2/2a above already fixed that the actual descriptor
  written to it at runtime is the material's own one, shared
  `sampler_`/`sampler` handle, never a second RHI `Sampler` object.
- **`pbr_ibl_normal_map.slang`** — identical to `pbr_ibl.slang`,
  `VertexInput` gains the same location-3 tangent, `normalMapSampler`
  at `[[vk::binding(5, 0)]]`, same shared-handle note as above.
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
- Every capacity number in this ADR (three real limits widened by one
  each — Pipeline ceiling, pool sizing, `textureDescriptorMemos_`;
  4→5/6→7 contract entry counts) is a confirmed, exact figure from
  current source, not an estimate, and its authoritative source (ADR-
  0072 D-7, amended alongside this ADR) is correctly attributed rather
  than restated as if newly decided here.
- Two new shader files, zero edits to either existing PBR shader —
  zero risk to any currently-committed golden.
- The Renderer API extension is exactly one new borrowed, non-owning
  pointer added to an already-reviewed, already-`Accepted` pattern
  (ADR-0056 item 8) — not a second sampler, not a new ownership model,
  not a new binding mechanism.
- The second-texture staging/rollback contract (Section 2a) is closed
  by direct inspection of the existing, already-`Accepted`
  `realizeOneMaterialCandidate()`/`realizePendingMaterials()` mechanism
  — no new Open Question deferred to Plan.
- The `Material` public-API contract for a normal map is fully closed:
  two mechanically-checked preconditions (base-color pair present,
  push-constant layout is `PbrDirectLit`) plus one documented Pipeline-
  matching precondition, and a direct trace showing Runtime's own real
  path satisfies all three by construction (the material grammar's own
  kind restriction), not merely by caller discipline — no open
  question deferred to Plan.

### Negative / Trade-offs

- A real, disclosed widening of `Renderer`'s own public `Material`/
  `createMaterial()` API and of three real Vulkan-Backend capacity
  limits (ADR-0072 D-7, amended) — real architectural surfaces, not
  cosmetic additions, and both now explicit in this ADR rather than
  implied.
- Two new shader files (not one) and two new descriptor-contract
  functions (not one) — real, larger Implementation scope than a
  direct-only design, in exchange for the feature actually working in
  an environment-enabled scene.
- A new, dedicated 5-sampler Pipeline/descriptor-capacity test that
  specifically exercises `bindTexture(5, ...)` is required — the
  existing `N+4`/`N+5` set-count test cannot stand in for it (Section 3
  above).
- Per-material shader selection (not scene-wide) is a real widening of
  `selectShaderPair()`'s own dispatch contract — every call site
  currently passing only `environmentEnabled` must also thread the
  calling material's own `normalMapTexture` presence.
- The material grammar's new 9-line form is legal only for
  `kind: pbr_direct_lit` — a real, disclosed asymmetry in an otherwise
  kind-agnostic grammar, needed because no other kind's shader declares
  a normal-map binding to consume it.
- The third precondition (a non-null `normalMapTexture` requires the
  caller's own Pipeline to be built from the matching normal-map
  shader variant for its own `environmentBinding()`) is documented,
  not mechanically checked inside `Material`'s own constructor —
  `atlantis::rhi::Pipeline` has no descriptor-layout introspection API
  today, and adding one is out of scope for this ADR. A caller that
  bypasses `realizeOneMaterialCandidate()` entirely (a hand-written
  test or fixture) must honor this precondition itself, exactly as it
  must already honor the existing ownership/destruction-order
  contract — a real, disclosed limitation of a borrowed-pointer API
  that predates this ADR, not introduced by it.

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
- **A second, independent `Sampler*` pointer on `Material` (a full
  second texture+sampler pair), the shape this ADR's own earlier draft
  used.** Rejected once Section 1 item 5 fixed the normal map to reuse
  the base-color sampler: a second `Sampler*` member would let a caller
  pass a *different* sampler for the normal map, a capability nothing
  in this Spec's own contract needs or permits, and would leave the
  invariant "these two must always be the same handle" unenforced by
  the type itself rather than simply not existing as a possibility. One
  borrowed texture pointer, reusing the existing `sampler_`, is smaller
  and cannot drift from the fixed contract.
- **Allowing a 9-line source file for `unlit_textured`/`lit_textured`,
  silently ignoring the `normal_map:` line.** Rejected: a silently-
  ignored authored field is exactly the class of defect this
  codebase's own "distinct enumerators for distinct causes" discipline
  exists to surface at parse time rather than leave as silent, dead
  data an author could reasonably expect to do something.
