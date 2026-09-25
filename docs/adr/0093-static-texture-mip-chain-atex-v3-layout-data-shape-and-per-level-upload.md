# ADR 0093: Static-Texture Mip Chains — `.atex` v3 Layout, Data Shape and Per-Level Upload

- **Status:** Accepted
- **Date:** 2026-09-25
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-09-25 (chat confirmation; reviewed in this
  branch's own PR, alongside Spec 0045's Approval)
- **Related Spec:** [Spec 0045: Static-Texture Mip-Chain Passthrough](../specs/0045-mip-chain-passthrough.md) (`Approved`)
- **Related ADR(s):** extends [ADR-0085](0085-block-compressed-sampled-texture-format-and-vulkan-mapping.md)
  (`Accepted`; its D5 put BC7 into `.atex` base-mip only) and reuses
  [ADR-0056](0056-texture-upload-resource-state-and-descriptor-binding.md)'s
  upload/resource-state contract and
  [ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)'s
  artifact versioning unchanged.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

Spec 0036 ①c owes one ADR for "the `.atex` multi-mip schema bump and the
Runtime/RHI per-level upload contract". The forces, all read at line
level in Spec 0045:

- `.atex` v2's 40-byte header has a `mip_count` word the encoder pins to 1
  and the decoder rejects otherwise; `pixel_data_size_bytes` covers one
  level; there is no unused header word (`texture_artifact.cpp:55-63`,
  `:101-137`).
- All 343 Bistro DDS files carry full chains to 1×1 (6–13 levels), with
  payloads exactly equal to the declared chain; the in-repo string-lights
  DDS carries 9 levels. The parser returns only level 0.
- The RHI already has everything but one predicate: `mipLevelCount` at
  creation, `SampledTextureUploadRegion{bufferOffsetBytes, mipLevel,
  arrayLayer, extent}`, a multi-region `copyBufferToTexture`, views and
  barriers spanning every level, and `SamplerCreateParams{mipFilter,
  minLod, maxLod}`. The environment cubemap already uses one staging
  buffer, one region per level and face, one pass, and a
  `maxLod = mipCount − 1` trilinear sampler.
- The one predicate: `isValidSampledTextureUploadRegion()` rejects any
  block-compressed region whose extent is not a multiple of 4, which
  every BC7 chain's 2×2/1×1 (and 2×1) tail levels are. Vulkan allows a
  partial block when the region reaches the level's edge.
- Material samplers are built from `filter`/`address_mode` alone, so they
  are `mipFilter = Nearest`, `maxLod = 0` — a chain would be uploaded and
  never sampled.
- Every existing golden samples PNG-sourced (single-mip) textures, except
  `bc7_dual_quad`, which samples the 9-level string-lights DDS.

## Decision

1. **Format — `.atex` schema v3 (and texture metadata v3), same header.**
   - The 40-byte header keeps every field and offset; `schema_version`
     becomes 3.
   - `mip_count` is the number of stored levels, `1 ≤ mip_count ≤
     floor(log2(max(width, height))) + 1`.
   - The levels follow the header contiguously, level 0 first, each
     tightly packed for its layout (RGBA8: `w_i·h_i·4`; BC7:
     `ceil(w_i/4)·ceil(h_i/4)·16`, with `w_i = max(1, width >> i)`,
     `h_i` likewise). No per-level padding or alignment.
   - `pixel_data_size_bytes` is the sum of all levels; offsets are
     **derived**, never stored.
   - The base-level multiple-of-4 rule for BC7 is unchanged; levels
     below it are whatever the halving gives.
   - The decoder accepts v3 only (v2 → `UnsupportedSchemaVersion`),
     requires the exact sum, and rejects an out-of-range `mip_count`.
   - Texture metadata v3 adds one `mip_count` line; the loader
     cross-checks it against the artifact like width/height/layout.
2. **Data shape — one blob plus a derived level table.**
   - `TextureAssetData` gains `std::uint32_t mipCount`; `pixelBytes` holds
     the whole chain in the artifact's order.
   - Asset System owns a pure `textureMipLevels(width, height, layout,
     mipCount)` returning `{width, height, offsetBytes, sizeBytes}` per
     level — the single source of the layout for the encoder, the
     decoder's size check, the cookers and the Runtime.
   - `cookTextureBc7()` and `encodeTextureArtifact()` take the chain and
     its count; `parseDdsBc7()` returns every declared level and its
     count, and requires the payload to equal the declared chain.
   - No RHI type enters Asset System (ADR-0057's boundary).
3. **Upload — one staging buffer, one region per level, one pass.**
   - The Runtime creates the `SampledTexture` with
     `mipLevelCount = mipCount`, fills one staging buffer with
     `pixelBytes` (one copy, as today) and records one
     `copyBufferToTexture(staging, texture, regions)` with a region per
     level from `textureMipLevels()`, inside the existing upload pass.
     Resource states and barriers are ADR-0056's, unchanged (they
     already span every level).
   - **RHI predicate widening (Vulkan Backend internal):** a block-
     compressed region whose width or height is not a multiple of the
     block extent is valid exactly when its offset is 0 and that
     dimension equals the level's full dimension; every other unaligned
     region is still rejected. The byte count stays the rounded-up block
     count. No RHI public type or signature changes.
4. **Sampling — derived per material, no new field.**
   - A material sampler's `maxLod` is the largest `mipCount − 1` among
     the textures it samples (base colour, normal map); `mipFilter`
     follows the material's `filter`. `minLod` stays 0.
   - A material whose textures are all single-mip gets `maxLod = 0` —
     today's sampler exactly.
5. **Scope — DDS passthrough only.** Nothing generates mips. PNG/JPG
   textures cook as `mip_count = 1`.

## Consequences

### Positive

- Bistro's textures sample the authored chain: no minification aliasing
  at distance, and the smaller levels are the artist's own filtering.
- One format and one code path for 1..N levels; single-mip is N = 1.
- Every PNG-sourced texture renders byte-identically: same bytes, same
  image shape, same sampler.
- The RHI surface is reused; the one predicate change brings it in line
  with Vulkan's own rule.

### Negative / Trade-offs

- +33.3 % texture memory and staging for chained textures (Bistro worst
  case 1.433 → 1.911 GiB).
- Every cooked texture is re-cooked (build outputs; v2 is not read).
- `bc7_dual_quad` may move where its quads are minified; if so it is
  re-baselined as an intentional change (Spec 0045 Q4).
- A derived layout means the decoder must recompute the chain to check
  the size — cheap, and the same function the cooker uses.
- Samplers now depend on the textures they are paired with; a material
  sharing one sampler across a chained base colour and a single-mip
  normal map clamps at the chained texture's count, which Vulkan further
  clamps per view.

## Alternatives Considered

- **Extend v2 in place** (reuse `mip_count` and the size word without a
  version bump). Byte-compatible with the header, and a v2 decoder
  already rejects `mip_count ≠ 1`, but it changes what a v2 file means
  under the same number — against the exact-version discipline of every
  other artifact. Rejected.
- **A stored per-level offset table** after the header. Redundant with
  the derivation, and a second thing to validate. Rejected; the
  environment artifact derives too.
- **`vector<vector<uint8_t>>` levels in `TextureAssetData`.** One
  allocation per level at load, and a gather into staging. Rejected.
- **One staging buffer per level, or one upload pass per level.** N
  allocations or N passes with no benefit; the environment path proves
  one of each. Rejected.
- **`VK_LOD_CLAMP_NONE` (or a large `maxLod`) on every sampler.** Also
  correct (the view clamps), but it changes every existing sampler's
  state; the derived value keeps single-mip samplers identical.
  Rejected.
- **A material-authored `max_lod`/mip-filter field.** A material schema
  bump with no Phase 1 use. Rejected.
- **CPU mip generation for PNG at cook time.** Would move every existing
  golden that minifies a PNG texture, and Bistro's content is DDS.
  Deferred (Spec 0045 Non-Goals).
- **Runtime generation by `vkCmdBlitImage`.** Needs blit support queries
  and linear-filter format features, and cannot apply to BC7. Rejected.
