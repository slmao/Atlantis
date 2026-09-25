# Plan: Static-Texture Mip-Chain Passthrough

- **Spec:** [Spec 0045: Static-Texture Mip-Chain Passthrough](../specs/0045-mip-chain-passthrough.md)
  (`Approved`, 2026-09-25; rulings Q1–Q5 binding) —
  [ADR-0093](../adr/0093-static-texture-mip-chain-atex-v3-layout-data-shape-and-per-level-upload.md)
  (`Accepted`)
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-09-25 — reviewed this Plan and
  [Spec 0045](../specs/0045-mip-chain-passthrough.md) together (chat
  confirmation; document set carried by this branch's PR) and explicitly
  authorized Implementation from Milestone 1. The five open points were
  ruled in the same review: O1 textured_quad_fixture follows the
  Runtime's upload and sampler; O2 synthetic mip chains built at test
  time; O3 mip_count metadata line after data_layout; O4 mipFilter
  switches to Linear only when maxLod > 0 (single-mip samplers stay
  identical in every field); O5 the minification golden uses the
  synthetic checker, not string-lights (which only reaches LOD 0.32).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0045 in full — Spec 0036 workflow ①c: every level a BC7
DDS carries goes through the cooker, `.atex` v3, the loader, one staging
buffer and a per-level upload, and is sampled through a derived-`maxLod`
material sampler. ADR-0093's five decisions and rulings Q1–Q5 are
binding and are not reopened here.

## Pre-drafting reading (cited, not restated)

Spec 0045's six investigations were re-walked at Plan granularity against
`origin/main` at `b9593da` (PR #187 merged). Every conclusion holds; items
4 and 6 add one fixture the Spec did not name.

1. **`.atex` v3 and the level function.**
   - Header fields and offsets are unchanged (`texture_artifact.cpp:55-63`);
     only the `mip_count` write (`:60`) and the decoder's `mip_count != 1`
     rejection (`:101-102`) and single-level size rule (`:128-137`) change.
   - `bc7BlockByteCount()` (`texture_artifact.h:47`, `.cpp:43-47`) already
     rounds sub-block levels up to one block. The ADR's pure level function
     sits beside it in `texture_artifact.{h,cpp}` — the format's own header,
     which the Runtime already reaches through Asset System's public
     includes. Shape (normative):

     ```
     struct TextureMipLevel { uint32 width, height; uint64 offsetBytes, sizeBytes; };
     uint32 fullMipChainLength(uint32 width, uint32 height);   // floor(log2(max)) + 1
     std::vector<TextureMipLevel> textureMipLevels(uint32 width, uint32 height,
                                                   TextureDataLayout layout, uint32 mipCount);
     ```

     Precondition: `1 ≤ mipCount ≤ fullMipChainLength`; `offsetBytes` of
     level 0 is 0 (relative to the pixel data). `uint64` throughout, the
     decoder's existing overflow discipline (`:124-137`).
   - Callers today: `cook_texture.cpp:139` (encode), `load_texture.cpp:50`
     (decode), and the tests in `texture_artifact_tests.cpp` (17 cases),
     `cook_texture_tests.cpp:73,218`, `texture_cook_command_tests.cpp:119`,
     `bistro_end_to_end_tests.cpp:202`.
   - Metadata: 8 lines, `atlantis_texture_metadata_version: 2`
     (`texture_metadata.cpp:12`); the loader cross-checks
     width/height/format/layout (`load_texture.cpp:59-62`).
2. **DDS parser.**
   - `parseDdsBc7()` copies level 0 only (`dds_parser.cpp:112-130`) and
     checks nothing beyond `dwMipMapCount != 0` (`:120-123`).
   - DDS stores levels contiguously, largest first, with no inter-level
     padding (the DDS file layout: each surface's levels follow one another
     directly). The Bistro census confirms it for all 343 files (payload ==
     declared chain exactly), and so does the in-repo string-lights DDS:
     148-byte header + 43,728 chain bytes = 43,876-byte file.
   - Q5: payload shorter than the declared chain → `Truncated`; longer →
     `MalformedHeader`; `dwMipMapCount` above `fullMipChainLength` →
     `MalformedHeader`. No flag → 1 level (the importer's white fallback,
     `material_import.cpp:223-246`, has no `DDSD_MIPMAPCOUNT`: unchanged).
   - The only DDS cook caller is `cook_command.cpp:271-305` (`:301`); the
     glTF importer emits cooker command lines (`material_import.cpp:487-502`)
     and never parses DDS itself.
   - Test impact: `dds_parser_tests.cpp:159-169` ("ignores extra mips")
     inverts; its `makeDx10Bc7()` builder (`:30-54`) gains a level count.
3. **The upload-region predicate.**
   - `isValidSampledTextureUploadRegion()` rejects any block-compressed
     region with an extent not a multiple of 4 (`vulkan_sampled_texture.cpp:85-92`).
   - Vulkan's rule for a copy into a block-compressed image: each of
     `imageExtent.width`/`.height` must be a multiple of the texel block's
     width/height, **or** `imageOffset + imageExtent` must equal that
     dimension of the image subresource (the mip level). The RHI region
     has no image offset (`types.h:380-385`; always `{0,0,0}`,
     `vulkan_command_list.cpp:661`), so the rule becomes: a dimension may be
     unaligned only when it equals the level's full dimension.
   - The byte count is unchanged (`blockCount()` already rounds up, `:39-42`,
     `:93-100`); `bufferOffset` stays a multiple of 16 (every level's offset
     in a BC7 chain is).
   - Test impact: `sampled_texture_block_tests.cpp:89-122`. The existing
     "5×4 of an 8×8" rejection (`:106-108`) still holds (5 ≠ 8); new cases
     for whole 2×2/1×1/4×2/2×1 levels accepted and a 2×2 region of a 4×4
     level rejected.
4. **Staging and upload.**
   - `material_realization.cpp:370-384` (base colour) and `:402-414`
     (normal map): texture created from `{extent, format}` only, one staging
     buffer of `pixelBytes.size()`, one `memcpy`. `buildTextureUploadPass()`
     (`:79-86`) records the single-region overload; it runs at `:625`/`:630`.
   - `RealizedMaterialCandidate` (`material_realization.h:53-77`) holds the
     staging buffers; it gains the per-texture region lists.
   - Regions come from `textureMipLevels()` over the loaded
     `TextureAssetData` — the one authority; no Runtime arithmetic.
   - **Not in the Spec:** `textured_quad_fixture.cpp` creates its own two
     textures (`:222-232`), sampler (`:240-242`, `Nearest`) and upload pass
     (`:153`, `:456-457`) instead of calling material realization. It
     renders `textured_quad` and `bc7_dual_quad`. It must mirror the
     Runtime (O1) for Q4 to mean anything and for M2's tests to run on it.
   - Every other texture-realizing fixture goes through the Runtime's
     `realizePendingMaterials()` and inherits the change.
5. **The sampler.**
   - `material_realization.cpp:420-421` creates one sampler per material
     from `filter`/`address_mode`, after both textures are resolved —
     `sampledTexturePtr` and `normalMapTexturePtr` (null when absent) are in
     scope for new and deduplicated textures alike.
   - `maxLod = max(mipLevelCount()) − 1` over those two
     (`SampledTexture::mipLevelCount()`, `sampled_texture.h:22` — the
     created resource is the authority).
   - `mipFilter`: `Linear` when `filter == Linear` **and** `maxLod > 0`,
     else `Nearest` (O4). With `maxLod == 0` the sampler is then field-for-
     field today's (`SamplerCreateParams` defaults, `types.h:208-214`).
6. **`bc7_dual_quad` (Q4).**
   - Its fixture is `textured_quad_fixture` (`bc7_dual_quad_gpu_tests.cpp:79-93`),
     identity camera, 512² target. The left quad spans 0.8 × 1.0 NDC
     (`textured_quad_left.mesh.txt`): 204.8 × 256 px for the 256 × 128
     texture, so ρ = max(1.25, 0.5) and LOD ≈ 0.32.
   - With the fixture's `Nearest` filter, O4 gives `mipFilter = Nearest`,
     which selects level 0 for LOD < 0.5. **Predicted: byte-identical.**
   - Procedure (M1 step 6): cook the 9-level chain → render → compare
     against the committed golden. Identical: nothing to do. Different: an
     ADR-0042 intentional-change re-capture in its own `test:` commit on the
     clean `feat:` tree, with the four evidence items in the PR.

## Plan-stage decisions

**P1 — `.atex` v3 encode/decode** (ADR-0093 D1). `encodeTextureArtifact()`
takes `mipCount`, and `pixelByteCount` is the chain total. The decoder:
v3 only; `1 ≤ mip_count ≤ fullMipChainLength`, else
`TextureArtifactDecodeError::UnsupportedMipCount` (renamed meaning,
same enumerator); `pixel_data_size_bytes` == Σ `textureMipLevels()` sizes,
else `InconsistentPixelDataSize`. `DecodedTextureArtifact` gains
`mipCount`.

**P2 — Metadata v3.** One `mip_count: <n>` line after `data_layout`
(9 lines, version 3). The loader adds `mipCount` to its cross-check
(`MetadataArtifactMismatch`). `TextureMetadata` gains `mipCount`.

**P3 — Cook.** `cookTextureBc7(blockBytes, byteCount, width, height,
mipCount, …)` checks `byteCount` against the chain (`BlockDataSizeMismatch`)
and `mipCount` against the chain length (new
`TextureCookError::InvalidMipCount`). `cookTexture()` (PNG) passes 1.

**P4 — DDS parser.** `DdsBc7Image{width, height, srgb, mipCount,
blockBytes}` (`baseMipBlockBytes` renamed: it is now the chain). Q5's
strict size rule; `cook_command.cpp:301` passes the count through.

**P5 — The RHI predicate** (ADR-0093 D3). Only the block-alignment clause
of `isValidSampledTextureUploadRegion()` changes, as in reading 3. No
public RHI change. `types.h:142-145`'s comment ("base-mip dimensions must
be multiples of 4") stays true and gains "levels below it need not be".

**P6 — Runtime upload** (ADR-0093 D3). The candidate carries
`std::vector<SampledTextureUploadRegion>` per new texture, built from
`textureMipLevels()`; `createSampledTexture` gets `.mipLevelCount`;
`buildTextureUploadPass()` takes the region span. One staging buffer, one
`memcpy`, one pass — unchanged in count.

**P7 — Sampler** (ADR-0093 D4, reading 5, O4).

**P8 — `textured_quad_fixture` mirrors P6/P7** (O1): its two textures get
`mipLevelCount` and region lists from the loaded data, and its sampler the
P7 rule (its `Nearest` filter stays). Plus one test-only knob: an optional
`maxLod` override on its sampler, used by M2's discriminator only.

**P9 — Synthetic mip test textures (M2)** (O2). A test-support header
builds BC7 chains in memory from solid and two-colour mode-6 blocks (the
`whiteFallbackDds()` encoding, `material_import.cpp:238-244`) and cooks
them with `cookTextureBc7()` into a per-process scratch directory; no
binary is committed. Two chains:
- *level-colour*: every level one distinct solid colour;
- *checker*: level 0 a 1-texel black/white checker (mode 6, endpoints
  black and white, alternating indices), every level below it the checker's
  exact box average (solid mid-grey) — what a correct authoring tool would
  store.

**P10 — The level-selection test (M2).** On `textured_quad_fixture`, a
square level-colour texture of 256², 512², 1024², 2048² puts the left
quad's LOD at ≈ 0.32, 1.32, 2.32, 3.32 (width 204.8 px), so `Nearest`
mip selection samples levels 0, 1, 2, 3; the quad's centre pixel must be
exactly that level's colour. Validation Layers on. (Spec 0036 ①c's
"sampling non-base mips of a BC7 texture".)

**P11 — The minification golden (M2)** (O5). `mip_chain_demo`: the checker
chain at 1024² on both quads (LOD ≈ 2.32) — with the chain, each quad is
uniform mid-grey; base-mip only, it is the aliased moiré of a 1-texel
checker sampled every ~5 texels. Discriminator: the same frame with the
P8 `maxLod = 0` override must fail against the golden.

## Milestones / Task Breakdown

**Two milestones; three commits, a fourth only if `bc7_dual_quad` moves.**

### Milestone 1 — the chain end to end (`feat:`; R1–R6)

1. P1 + P2 + P3: level function, v3 encode/decode, metadata v3, cook
   signatures; GPU-independent tests (Spec's list), existing texture tests
   moved to v3.
   *Gate:* all Asset System tests green; every `.atex` re-cooks by the build.
2. P4: DDS parser and cook command; parser tests; the string-lights cook
   yields 9 levels, 43,728 bytes.
   *Gate:* `atlantis_asset_cooker` tests green.
3. P5: predicate and its tests.
   *Gate:* `sampled_texture_block_tests` green, legacy cases unchanged.
4. P6 + P7: Runtime upload and sampler.
   *Gate:* full Debug suite; **every existing golden byte-identical**
   (PNG-sourced textures: one level, `maxLod = 0`, sampler state
   field-identical); Validation Layers clean.
5. P8: `textured_quad_fixture`.
   *Gate:* `textured_quad` golden byte-identical (PNG).
6. `bc7_dual_quad` measurement (reading 6).
   *Gate:* identical → continue; different → stop, report the diff, then
   the Q4 re-capture as its own `test:` commit after the `feat:`.
7. Bistro content tests (`content` label): re-cook and load all 343 textures
   with their chains; Android `assembleDebug`.

### Milestone 2 — verification pieces (`feat:` + `test:`; the ①c row)

1. P9 support header; P10 level-selection GPU test; the P11 generator and
   discriminator wiring (`feat:`).
   *Gate:* level test passes at all four sizes; Validation Layers clean.
2. `mip_chain_demo` golden captured on the clean `feat:` tree, with its
   capture-compare and discriminator cases (`test:`, ADR-0042 Initial
   baseline, four evidence items).
3. Full regression: Debug and Release, Validation Layers, Android, the
   content tests.

## Files / Modules Touched (expected)

- **Asset System:** `texture_artifact.{h,cpp}`, `texture_types.h`,
  `texture_metadata.{h,cpp}`, `cook_texture.{h,cpp}`, `load_texture.cpp`,
  `errors.h`.
- **Tools:** `asset_cooker/dds_parser.{h,cpp}`, `asset_cooker/cook_command.cpp`.
- **Vulkan Backend:** `vulkan_sampled_texture.cpp` (predicate only); the
  comment at `rhi/types.h:142-145`.
- **Runtime:** `material_realization.{h,cpp}`.
- **Tests:** `tests/asset_system/{texture_artifact,cook_texture,load_texture,
  texture_metadata}_tests.cpp`, `tests/tools/asset_cooker/{dds_parser,
  texture_cook_command}_tests.cpp`, `tests/tools/gltf_importer/
  bistro_end_to_end_tests.cpp` (mip assertions),
  `tests/vulkan_backend/sampled_texture_block_tests.cpp`,
  `tests/image_regression/fixture/textured_quad_fixture.{h,cpp}`, a new
  mip test-support header, a new `mip_chain_gpu_tests.cpp`, a golden
  generator, the `mip_chain_demo` golden; `bc7_dual_quad` golden only if
  step 6 finds a change.

**Not touched:** RHI public headers (beyond the one comment),
`vulkan_command_list.cpp`, `vulkan_device.cpp`, the Renderer, RenderGraph,
shaders, `Material`, material source/artifact schemas, the environment
path, the glTF importer's code, scene formats, every golden except the
possible `bc7_dual_quad` re-capture.

## Sequencing & Dependencies

M1 steps 1 → 2 (the cooker needs the v3 API) → 3 → 4 (the upload needs
the predicate) → 5 → 6 → 7. M2 needs M1's `textured_quad_fixture` (P8).
ADR-0042 puts each golden in a `test:` commit after the `feat:` it
captures from.

## Verification Checklist

- [ ] R1: parser returns the full chain; Q5 short/long/over-count
      rejections; no-flag = 1 level; string-lights = 9 levels, 43,728 B.
- [ ] R2: v3 round-trip at 1 / partial / full chains, both layouts;
      v2, `mip_count` 0 or over the chain, ±1 byte rejected; metadata v3
      round-trip and mip mismatch rejected.
- [ ] R3: `textureMipLevels()` for square, 2:1, 1:2, NPOT (2560) bases and
      8192² (sizes fit `u64`, total fits `u32`).
- [ ] R4: whole sub-block levels accepted by the predicate, partial
      unaligned regions rejected; level-selection GPU test (P10);
      Validation Layers clean.
- [ ] R5: single-mip samplers field-identical (every existing golden
      byte-identical); `mip_chain_demo` golden and its `maxLod = 0`
      discriminator.
- [ ] R6: PNG cooks `mip_count = 1`.
- [ ] Regression: Debug and Release full suites; content tests; Android
      `assembleDebug`.

## Open points (for Joint Human Review)

- **O1 — `textured_quad_fixture` mirrors the Runtime (P8).** Recommend
  yes: otherwise `bc7_dual_quad` cannot move (Q4 would be vacuous) and M2
  has no fixture that samples a BC7 chain at a controlled LOD.
- **O2 — Synthetic test chains built at test time (P9)**, not committed
  DDS binaries. Recommend test-time: deterministic, reviewable as code,
  and no new asset provenance. The DDS parser's full-chain path is still
  exercised on the committed string-lights file.
- **O3 — Metadata `mip_count` line after `data_layout` (P2).** Recommend,
  keeping the layout-describing fields together.
- **O4 — `mipFilter = Linear` only when `maxLod > 0` (P7).** A precision
  of Spec R5 ("mipFilter follows filter"): with one level the two
  settings render identically, but only this form keeps the sampler state
  field-identical to today's, which R5 promises. Recommend.
- **O5 — The minification golden uses the synthetic checker (P11)**,
  not the string-lights texture the Spec's Testing section names: on the
  only fixture with a controlled LOD, string-lights sits at LOD 0.32 and
  shows no mip effect. The checker makes the difference analytic
  (uniform grey vs. moiré). Recommend.

## Rollback Plan

Revert the M2 commits, then M1's. `.atex` v2 and metadata v2 come back
with the code, and the build re-cooks every texture; no committed
artifact depends on v3. A `bc7_dual_quad` re-capture reverts with its
own `test:` commit.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas:
- [ ] Every existing golden byte-identical, except a `bc7_dual_quad`
      re-capture under Q4 with its four ADR-0042 evidence items.
- [ ] `mip_chain_demo` golden with a failing `maxLod = 0` discriminator.
- [ ] The level-selection GPU test passes at levels 0–3.
- [ ] The Bistro content tests pass with full chains.
- [ ] Android `assembleDebug` green.
