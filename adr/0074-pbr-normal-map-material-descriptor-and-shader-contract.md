# ADR 0074: PBR Direct-Lit Normal Map — Material Schema, Descriptor Contract, and Shader Integration

- **Status:** Proposed
- **Date:** 2026-09-06
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — pending Human Review
- **Related Spec:** [specs/0029-tangent-space-normal-mapping-foundation.md](../specs/0029-tangent-space-normal-mapping-foundation.md) (`Draft`)
- **Related ADR(s):** [ADR-0073](0073-static-mesh-tangent-attribute-generation-and-schema.md)
  (the tangent vertex attribute this ADR's shader consumes — Proposed,
  same Spec), [ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (the existing PBR material asset schema this ADR extends),
  [ADR-0067](0067-pbr-direct-lighting-brdf-and-push-constant-contract.md)
  (the existing BRDF/push-constant contract this ADR reuses
  unmodified), [ADR-0064](0064-vulkan-backend-descriptor-pool-growth-ownership-model.md)
  (the descriptor-pool growth/capacity model whose fixed 4-sampler-
  per-set ceiling this ADR stays within, unmodified).

## Context

Confirmed directly against current `main`:

- `MaterialAssetData`/`DecodedMaterialArtifact`/`ParsedMaterialSource`/
  `MaterialMetadata` (`material_types.h:51-59`, `material_artifact.h:50-58`,
  `material_source.h:22-30`, `material_metadata.h:24-32`) each carry
  exactly one texture reference, `textureAsset` (the base-color
  texture) — no second, optional texture slot exists anywhere in the
  material schema.
- `atlantis_material_source_version: 2` is an 8-line, fixed-field-order
  grammar (`material_source.h:53-61`); the material artifact is a
  fixed 56-byte record, `kMaterialArtifactSchemaVersion = 2`
  (`material_artifact.h:16-43`).
- `sampledTextureBindingCountFor(MaterialKind, bool environmentEnabled)`
  (`material_realization.cpp:371-380`) is the one, single place
  deciding a Pipeline's own sampler-binding count: `UnlitTextured`/
  `LitTextured` always `1`; `PbrDirectLit` is `2` without an
  environment (base-color@1, shadow-map@2, `pbr_direct_lit.slang`) or
  `4` with one (base-color@1, environment@2, DFG LUT@3, shadow-map@4,
  `pbr_ibl.slang` — `selectShaderPair()`, `material_realization.cpp:101-126`,
  dispatches on the *scene-wide* `environmentEnabled` flag, not a
  per-material one).
- **The descriptor pool's own sampler capacity is a hard-coded
  ceiling, not per-Pipeline-dynamic**: `vulkan_device.cpp:433-438`
  sizes `VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER` at exactly
  `4 * maxSets`, and `vulkan_device.cpp:1000-1002` enforces
  `sampledTextureBindingCount ∈ {0,1,2,3,4}` via `ATLANTIS_CHECK` — 4
  is already the system-wide maximum for any one Pipeline's own
  sampler count, confirmed by direct inspection, not assumed.
- `pbr_direct_lit.slang`'s own `VertexInput` has exactly 3 locations
  (position@0, uv@1, normal@2, `pbr_direct_lit.slang:86-90`); its
  vertex shader transforms `input.normal` via
  `mul((float3x3)pushConstants.objectToWorld, input.normal)`
  (`pbr_direct_lit.slang:141`), correct only when Runtime's own
  `checkConformalTransform()` has already gated that entity
  (`pbr_direct_lit.slang:137-140`'s own comment, confirmed against
  `scene_extraction.cpp:335-376`). `PushConstants` is 96 bytes
  (`objectToWorld` 64B + `baseColorFactor` 16B + `metallicFactor` 4B +
  `roughnessFactor` 4B + 8B pad); `pbr_ibl.slang` already reuses this
  identical layout for its own, structurally different shader
  (confirmed by `MaterialPushConstantLayout::PbrDirectLit` being
  passed for both, `material_realization.cpp:244`).
- `descriptor_contract.h:45-65` names one contract function per
  distinct shader pair (`pbrDirectLitExpectedDescriptorContract()`,
  `pbrIblExpectedDescriptorContract()`) — never one function branching
  internally on a runtime flag.

A normal map is authored per-material (some materials in a scene want
one, others do not) — unlike environment, which is a single, scene-
wide flag. Combining a per-material normal map with the scene-wide
environment path would need a 5th sampler binding
(base-color@1, environment@2, DFG LUT@3, shadow-map@4, normal-map@5),
exceeding the confirmed hard 4-sampler ceiling above — a real,
descriptor-pool-capacity-affecting change this ADR does not make.

## Decision

**One new, optional, per-material texture reference,
`normalMapTexture`, is added to the material schema. When present on a
`PbrDirectLit` material realized in a scene with no environment
configured, it selects a new, fourth built-in shader pair,
`pbr_direct_lit_normal_map.slang`, consuming ADR-0073's own tangent
attribute and sampling a tangent-space normal map at a new, fourth
descriptor binding. A `PbrDirectLit` material combining a normal map
with an environment-enabled scene is explicitly out of scope — see
Spec 0029's own Open Questions for the recommended, disclosed
behavior in that case.**

1. **Material schema.** `MaterialAssetData`/`DecodedMaterialArtifact`/
   `ParsedMaterialSource`/`MaterialMetadata` each gain
   `AssetId normalMapTexture = 0;` (`0` = no normal map, the same
   "unassigned" convention `Renderable::meshAsset`'s own default
   already establishes — never a new optional-value wrapper type).
   `atlantis_material_source_version` becomes `3`: the existing 8-line
   form is unchanged and still valid (no normal map); a 9th, optional
   trailing line, `normal_map: <logical-path>`, selects one. Every
   existing `.material.txt` file needs exactly one line changed (its
   own version marker, `2` → `3`) — no other line touched, and no
   material gains a normal map it did not previously have.
   `kMaterialArtifactSchemaVersion` becomes `3`; the fixed record
   widens from 56 to 64 bytes (append `normal_map_texture_asset_id`,
   8 bytes, at offset 56) — schema version 1/2 rejected outright, no
   migration reader, matching ADR-0066 item 3's own precedent exactly.
2. **Shader selection is per-material, not scene-wide.**
   `selectShaderPair()` gains a new parameter (the calling material's
   own `normalMapTexture != 0`, already available at every call site
   via `materialData`, threaded no further than this one function).
   For `PbrDirectLit` with no environment: `pbr_direct_lit.slang`
   (unchanged, existing materials) or `pbr_direct_lit_normal_map.slang`
   (new). `pbr_ibl.slang` selection (`environmentEnabled == true`)
   is unaffected by this parameter in either direction — seeSpec
   0029's own Open Questions for the one combination this leaves
   undecided.
3. **`pbr_direct_lit_normal_map.slang`** — a new file, not an edit to
   `pbr_direct_lit.slang` (existing materials' own shader is
   byte-for-byte untouched, zero golden risk). Identical
   `CameraUniform`/`PushConstants`/BRDF math to `pbr_direct_lit.slang`,
   with exactly these differences:
   - `VertexInput` gains a 4th attribute,
     `[[vk::location(3)]] float4 tangent`.
   - `Varying` gains `float4 worldTangent` (xyz + the unmodified `w`
     handedness) and `worldNormal` is still emitted as today (the
     un-perturbed geometric normal, needed to build the TBN basis in
     the fragment stage).
   - `vertexMain()` additionally computes
     `output.worldTangent = float4(mul((float3x3)pushConstants.objectToWorld,
     input.tangent.xyz), input.tangent.w)` — the *same* 3x3
     `objectToWorld` submatrix and the *same* `checkConformalTransform()`
     gate `pbr_direct_lit.slang` already relies on for its own normal
     transform (ADR-0073's own disclosed mirrored-transform limitation
     applies identically here, not newly introduced by this ADR).
   - `fragmentMain()` reconstructs an orthonormal TBN basis in the
     fragment stage (`T = normalize(input.worldTangent.xyz)`,
     `N = normalize(input.worldNormal)`, `T = normalize(T - N *
     dot(N, T))` — a second, fragment-stage Gram-Schmidt pass,
     independent of ADR-0073's own cook-time one, needed because
     per-vertex interpolation across a triangle does not preserve
     exact orthogonality; `B = cross(N, T) * input.worldTangent.w`),
     samples the new normal map (assumed OpenGL/DirectX tangent-space
     convention: RGB8 `[0,1]` linearly remapped to `[-1,1]`, Z always
     positive-hemisphere), builds `perturbedN = normalize(TBN *
     sampledTangentSpaceNormal)`, and uses `perturbedN` in place of
     `N` everywhere the existing BRDF loops already reference it — no
     other line of the accumulation math changes.
4. **New descriptor binding**, next free slot after the existing
   shadow-map sampler:
   `[[vk::binding(3, 0)]] Sampler2D normalMapSampler;` (fragment-only).
   `sampledTextureBindingCountFor()` returns `3` for this new
   combination (base-color@1, shadow-map@2, normal-map@3) — confirmed
   within the existing `{0,1,2,3,4}` ceiling and the pool's own fixed
   `4 * maxSets` sizing (Context above): **no descriptor-pool-capacity
   change of any kind**, no new descriptor-peak test, no change to
   Spec 0021's own capacity model.
5. **New descriptor contract function**,
   `pbrDirectLitNormalMapExpectedDescriptorContract()`
   (`descriptor_contract.h`), mirroring the file's own one-function-
   per-shader-pair convention: camera@0 (vertex+fragment, unchanged),
   base-color@1 (fragment), shadow-map@2 (fragment), normal-map@3
   (fragment).
6. **No push-constant change.** `pbr_direct_lit_normal_map.slang`
   reuses `MaterialPushConstantLayout::PbrDirectLit` and the identical
   96-byte layout verbatim — no per-draw "normal map strength" or
   similar scalar is introduced (a normal map, when present, always
   applies at full strength; a lower-strength blend is explicitly out
   of scope, matching Spec 0029's own minimal-target framing).
7. **RHI/shader-reflection additions, reused from ADR-0073.**
   `atlantis::rhi::VertexAttributeFormat::Float4` and
   `atlantis::shader_system::VertexAttributeType::Float4` (ADR-0073's
   own item 8) are this ADR's only consumer today — no further RHI
   type is added here.
8. **Materials without `normalMapTexture` are unaffected in every
   respect** — same shader, same descriptor contract, same push-
   constant layout, same `sampledTextureBindingCountFor()` result as
   before this ADR. Every existing golden that exercises `PbrDirectLit`
   must remain byte-identical (Spec 0029's own explicit requirement).

## Consequences

### Positive

- Stays entirely within the existing 4-sampler-per-Pipeline ceiling —
  zero change to descriptor pool sizing, zero new descriptor-peak
  test, zero risk to Spec 0021's own already-verified capacity model.
- A new file for the new shader variant means zero risk to any
  existing `PbrDirectLit` golden — the existing shader is never
  touched.
- Reuses `MaterialPushConstantLayout::PbrDirectLit` and every existing
  BRDF term verbatim — the only new math is TBN reconstruction and one
  texture sample, isolated to the one new shader file.

### Negative / Trade-offs

- Per-material shader selection (not scene-wide) is a real,
  previously-absent axis in `selectShaderPair()`'s own dispatch logic
  — a small but genuine widening of that function's own contract,
  requiring every call site that currently passes only
  `environmentEnabled` to also thread the calling material's own
  `normalMapTexture` presence.
- The environment + normal-map combination is explicitly left
  undecided by this ADR (see Spec 0029's Open Questions) — a real,
  disclosed scope boundary, not a silent gap.
- Every composition root with its own local `pbrDirectLitVertexLayout()`-
  style helper (fixtures, `runtime_application.cpp`) needs a new,
  parallel tangent-aware layout function to realize a normal-mapped
  material at all — existing composition roots that never load a
  normal-mapped material need no change (Spec 0029's own migration
  scope).

## Alternatives Considered

- **A per-material boolean baked into `PipelineCreateParams` directly**,
  bypassing a new shader file (e.g. a shader-side `#ifdef`/specialization
  constant toggling the normal-map sample). Rejected: this codebase has
  no shader specialization-constant mechanism today, and introducing
  one is materially larger scope than one new `.slang` file following
  the exact pattern `pbr_ibl.slang` already established for "a new
  shader variant, not a branch inside the existing one."
- **Widening `sampledTextureBindingCount`'s own ceiling to 5** (to
  allow normal-map + environment together in one Pipeline).
  Rejected for this ADR: touches `vulkan_device.cpp`'s own descriptor-
  pool sizing and Spec 0021's own capacity model, a materially larger,
  separate architectural decision this Spec's own minimal-target scope
  does not require — deferred as explicit future work.
- **A single, combined `pbr_direct_lit.slang` with a runtime branch**
  reading a push-constant flag. Rejected: every `PbrDirectLit` draw
  would then declare the normal-map descriptor binding whether or not
  the bound material fills it, an RHI/descriptor-layout mismatch this
  codebase's own "declared bindings must be used" discipline
  (`descriptor_contract.h`'s own validation) does not permit cleanly
  without either a dummy always-bound texture or a second Pipeline
  variant — the latter is exactly this ADR's own chosen design, made
  explicit as a separate file rather than implicit inside one.
