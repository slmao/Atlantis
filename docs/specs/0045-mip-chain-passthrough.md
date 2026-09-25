# Spec: Static-Texture Mip-Chain Passthrough

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-25
- **Related Plan(s):** none yet — Plan 0045 drafting is authorized by the
  Approval below. **Implementation still awaits its own, separate Joint Human
  Review** of Spec + Plan together, per AGENTS.md's own workflow.
- **Approval:** slmao, 2026-09-25 (chat confirmation, no reviewing PR —
  authorizes drafting Plan 0045; Implementation itself still awaits its own,
  separate Joint Human Review of Spec + Plan together). The same review ruled
  all five open questions. See Risks & Open Questions below.
- **Related ADR(s):** [ADR-0093](../adr/0093-static-texture-mip-chain-atex-v3-layout-data-shape-and-per-level-upload.md)
  (`Accepted` 2026-09-25, alongside this Spec's own Approval) — the `.atex`
  v3 multi-mip layout, the `TextureAssetData`
  shape, and the per-level upload and sampling contract (extends
  [ADR-0085](../adr/0085-block-compressed-sampled-texture-format-and-vulkan-mapping.md)).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Carry a BC7 DDS file's whole mip chain from the cooker to the GPU, and
sample it. Today `.atex` holds the base mip only, the Runtime uploads one
level, and every material sampler clamps to level 0 — so Bistro's 4096²
textures alias badly at distance. This is
[Spec 0036](0036-bistro-parity-roadmap.md) workflow ①c. The cooker passes
every level the DDS carries through verbatim; `.atex` goes to schema v3
with the levels stored contiguously, largest first; the loader returns
them as one blob plus a derived per-level layout; the Runtime uploads all
levels from one staging buffer with one region per level and samples them
trilinearly. PNG-sourced textures stay single-mip and render exactly as
today. No mip is generated anywhere.

## Motivation / Problem Statement

Spec 0036 ①c (`0036-bistro-parity-roadmap.md:322-345`) is the contract:
its ADR obligation is "the `.atex` multi-mip schema bump and the
Runtime/RHI per-level upload contract (extending ADR-0085)" (`:825`), and
its verification row is "artifact round-trip tests for multi-mip `.atex`;
a GPU test sampling non-base mips of a BC7 texture; Vulkan Validation
Layers clean" (`:877`). Spec 0038 shipped base-mip only
([Plan 0038](../plans/0038-block-compressed-textures.md) Milestone 3,
`:114-116`) and Plan 0037 deferred the chain again (Ruling 6, `:705-707`).
⑦ (Bistro parity) depends on this workflow.

### Current state (read at line level, 2026-09-25, `origin/main` 5929ffc)

1. **`.atex` v2 is single-mip by construction.** A 40-byte header
   (`texture_artifact.h:13-41`): magic, `schema_version` (2), width,
   height, format, `mip_count`, `pixel_data_offset`,
   `pixel_data_size_bytes`, `data_layout`. The encoder writes
   `mip_count = 1` unconditionally (`texture_artifact.cpp:60`, "always 1
   this round"); the decoder rejects anything else with
   `UnsupportedMipCount` (`:101-102`, `errors.h:202`) and requires
   `pixel_data_size_bytes` to equal one level's bytes (`:128-137`). The
   header has **no unused field** — all ten words are assigned. The
   sidecar (`texture_metadata.h`, version 2, 8 lines) records no mip
   count. `TextureAssetData` (`texture_types.h:37-43`) is
   `{width, height, colorSpace, layout, pixelBytes}`.
2. **The DDS parser drops every level but the base.**
   `parseDdsBc7()` copies `ceil(w/4)·ceil(h/4)·16` bytes from the data
   offset and returns them as `baseMipBlockBytes`
   (`dds_parser.cpp:112-130`, `dds_parser.h:30-35`). Correction to its
   own header comment ("validated for presence", `dds_parser.h:19-21`):
   the code only rejects `dwMipMapCount == 0` when `DDSD_MIPMAPCOUNT` is
   set (`dds_parser.cpp:120-123`); the extra levels' bytes are never
   checked. `cookTextureBc7()` takes one level's bytes
   (`cook_texture.cpp:104-128`).
3. **The RHI upload surface already exists.** `SampledTextureCreateParams`
   has `mipLevelCount` (`types.h:200-205`, Spec 0025/P2);
   `SampledTextureUploadRegion` has `mipLevel`, `arrayLayer`,
   `bufferOffsetBytes`, `extent` (`types.h:380-385`); and
   `CommandList::copyBufferToTexture(source, destination, span<regions>)`
   records one `vkCmdCopyBufferToImage` with every region
   (`command_list.h:103-105`, `vulkan_command_list.cpp:640-669`). The
   image, its view and the texture's barriers already span every level
   (`vulkan_device.cpp:1441`, `:1503-1504`; `vulkan_command_list.cpp:117`).
   The environment cubemap uses exactly this path — one staging buffer,
   `mipCount × 6` regions, one RenderGraph pass
   (`environment_realization.cpp:55-101`). **One gap:**
   `isValidSampledTextureUploadRegion()` requires a block-compressed
   region's extent to be a multiple of 4 (`vulkan_sampled_texture.cpp:85-92`).
   Every BC7 chain ends in levels smaller than a block (2×2, 1×1; 2×1
   for a 2:1 texture), so those regions are rejected today. Vulkan itself
   permits a partial block when the region reaches the level's edge
   (`VkBufferImageCopy` compressed-format rule); the RHI predicate is
   stricter than the API.
4. **Samplers clamp to level 0.** `SamplerCreateParams` already carries
   `mipFilter` (`Nearest`/`Linear`), `minLod` and `maxLod`
   (`types.h:208-214`), mapped straight to `VkSamplerCreateInfo`
   (`vulkan_device.cpp:1713-1718`); there is no LOD bias or anisotropy
   field. Material realization builds its one sampler per material from
   the material's `filter`/`address_mode` only
   (`material_realization.cpp:420-421`), so every material sampler has
   `mipFilter = Nearest`, `maxLod = 0`: a multi-mip texture would still
   be sampled at level 0. The environment sampler sets
   `mipFilter = Linear`, `maxLod = mipCount − 1`
   (`environment_realization.cpp:43-45`).
5. **Memory.** See the census below: full chains are ×4/3 of base, +489
   MiB over Bistro's 343 referenced textures.
6. **Staging and upload.** Per new texture, material realization creates
   one staging buffer sized `pixelBytes.size()`, does one `memcpy`
   (`material_realization.cpp:376-382`, normal map `:408-414`), and
   `buildTextureUploadPass()` records the single-region
   `copyBufferToTexture(staging, destination)` overload (`:79-86`).
   `pixelBytes` is already the exact upload payload; a multi-level blob
   in the same buffer needs only a region list.

### Bistro DDS census (all 343 referenced files, measured 2026-09-25)

Header census over `content/bistro/**.dds` (the fetched, never-committed
Bistro content, `tools/content/bistro_source.provenance.txt`):

| Property | Result |
|---|---|
| Formats | DXGI 99 `BC7_UNORM` ×224, DXGI 98 `BC7_TYPELESS` ×119 |
| `DDSD_MIPMAPCOUNT` set | 343 / 343 |
| Chain length | **every file carries the full chain to 1×1** (`floor(log2(max(w,h))) + 1` levels) |
| Mip-count distribution | 13 ×52, 12 ×157, 11 ×89, 10 ×26, 9 ×15, 7 ×3, 6 ×1 |
| Payload bytes vs. declared chain | exact match in 343 / 343 (no padding, no truncation) |
| Non-power-of-two | 1 file (2560 max dimension) |
| Largest dimension | 4096 |

In-repository: `assets/textures/paris_stringlights_diff.dds` (and its
sRGB variant) is 256×128 with 9 levels — also a full chain, ending in
4×2, 2×1, 1×1 (16 bytes each). It is the `bc7_dual_quad` golden's texture.

Memory, BC7 payload by largest dimension:

| Max dim | Files | Base mip (MiB) | Full chain (MiB) | Added (MiB) |
|---|---|---|---|---|
| 4096 | 52 | 784.0 | 1045.3 | 261.3 |
| 2560 | 1 | 6.2 | 8.3 | 2.1 |
| 2048 | 156 | 588.0 | 784.0 | 196.0 |
| 1024 | 89 | 83.0 | 110.7 | 27.7 |
| 512 | 26 | 5.8 | 7.7 | 1.9 |
| ≤256 | 19 | 0.8 | 1.1 | 0.3 |
| **Total** | **343** | **1467.9 (1.433 GiB)** | **1957.2 (1.911 GiB)** | **489.3 (+33.3 %)** |

This is the worst case, every referenced texture resident with its full
chain. (The brief's "1.169 GB" base figure does not appear in the
repository and does not match this measurement; Spec 0037 records ~2.18
GB of DDS on disk for 390 files.)

## Goals

- Every level a BC7 DDS carries reaches the GPU and is sampled.
- One artifact format and one load/upload path for 1..N levels; a
  single-mip texture is the N = 1 case, not a separate path.
- Every existing PNG-sourced texture, and so every existing golden that
  samples one, renders byte-identically.
- Close Spec 0036 ①c's ADR obligation and verification row.

## Non-Goals

- **Generating mips** — at cook time (CPU box filter) or at run time
  (`vkCmdBlitImage`). PNG textures stay base-mip only (ruling d below).
- **Custom or partial authoring** of chains beyond what the DDS carries.
- **LOD bias, min/max LOD authoring, anisotropic filtering** — no new
  material or sampler field.
- **Streaming, residency or eviction** — all levels of a texture upload
  together, when it is realized.
- **Formats other than BC7** (the census found none), texture arrays,
  cube or 3D textures from DDS.

## Requirements

### Functional

- **R1 — Cooker passthrough.** For a BC7 DDS, the cooker writes every
  level the file declares (`dwMipMapCount` when `DDSD_MIPMAPCOUNT` is
  set, else 1), verbatim, largest first. The DDS payload must hold
  exactly the declared chain's bytes; fewer is `Truncated`. A declared
  count above the full-chain length is `MalformedHeader`.
- **R2 — `.atex` v3.** The artifact records `mip_count` and stores the
  levels contiguously, level 0 first, each tightly packed (BC7:
  `ceil(w_i/4)·ceil(h_i/4)·16` bytes, `w_i = max(1, w >> i)`). Offsets
  are derived, not stored. The decoder accepts `1 ≤ mip_count ≤ full
  chain length` and requires `pixel_data_size_bytes` to equal the sum of
  the levels exactly; v2 artifacts are rejected (`UnsupportedSchemaVersion`),
  the repository's no-dual-reader discipline. The sidecar records the
  mip count and the loader cross-checks it against the artifact.
- **R3 — Loaded shape.** `TextureAssetData` carries `mipCount` beside the
  unchanged `pixelBytes` (now the whole chain); a pure Asset System
  function derives each level's width, height, byte offset and size.
  No RHI type enters Asset System.
- **R4 — Upload.** The Runtime creates the `SampledTexture` with
  `mipLevelCount = mipCount`, copies the blob into one staging buffer as
  today, and records one `copyBufferToTexture` with one region per level
  in the existing upload pass. The RHI accepts a block-compressed region
  that is not block-aligned exactly when it covers the whole level
  (Vulkan's own rule), and still rejects every other unaligned region.
- **R5 — Sampling.** A material sampler's `maxLod` is the largest
  `mipCount − 1` among the textures the material binds, and its
  `mipFilter` follows the material's `filter` (`Linear` → `Linear`,
  `Nearest` → `Nearest`). For a single-mip texture this is `maxLod = 0`,
  exactly today's sampler.
- **R6 — PNG path unchanged.** `stb_image`-sourced textures cook with
  `mip_count = 1` and realize exactly as today.

### Non-functional

- **Performance:** one extra `vkCmdCopyBufferToImage` region per level
  (≤ 14 at 8192²); upload bytes ×4/3. No per-frame cost; trilinear
  sampling is the only shader-side change, and it is a sampler state.
- **Memory:** +33.3 % device-local texture memory and +33.3 % transient
  staging per realized texture (largest: 4096² BC7, 16.0 → 21.3 MiB).
  Bistro worst case 1.433 → 1.911 GiB (table above).
- **Portability:** Vulkan-only, unchanged. BC7 on real Android devices
  remains Spec 0038's disclosed gap; the mip path itself is format-
  generic.
- **Determinism:** bytes are copied, never transformed; a golden over a
  mip chain is exact under ADR-0042 on the calibrated machine.

## Proposed Design

Decisions a–d are recorded in ADR-0093; this section is the shape.

- **(a) Format — `.atex` schema v3, same 40-byte header.** `mip_count`
  keeps its offset and gains its meaning; `pixel_data_size_bytes` becomes
  the chain total (a 4096² BC7 chain is 22.4 MB, and 8192² ~89 MB — well
  inside `u32`); per-level offsets are derived from width, height,
  layout and count, the environment-artifact precedent. Recommended over
  "v2 extended in place": the header has no spare field, and widening
  what a v2 header *means* without a version bump breaks the exact-
  version discipline every other artifact follows (scene v5→v6, mesh
  v4→v5). Texture metadata goes v2→v3 with one `mip_count` line.
- **(b) Data shape — one blob plus a derived level table.**
  `TextureAssetData{…, mipCount, pixelBytes}` with
  `textureMipLevels(width, height, layout, mipCount) → vector<{width,
  height, offsetBytes, sizeBytes}>` in Asset System. The encoder, the
  decoder's size check, the cooker and the Runtime's region builder all
  call the same function. Recommended over `vector<vector<uint8_t>>`: the
  blob is already the staging payload (one `memcpy`, no per-level
  allocation), and it matches `EnvironmentAssetData`.
- **(c) Upload — one staging buffer, N regions, one pass.** Material
  realization keeps its single staging buffer and `memcpy`; the
  candidate carries a region list built from `textureMipLevels()`, and
  `buildTextureUploadPass()` uses the region overload. The RHI change is
  confined to `isValidSampledTextureUploadRegion()`'s block-alignment
  clause (backend-internal; no public signature changes). Recommended
  over per-level staging buffers: N allocations for no benefit, and the
  environment path already proves the one-buffer shape.
- **(d) No mip generation for PNG.** Phase 1 scope is DDS passthrough;
  every PNG texture stays `mip_count = 1` (R6).
- **Sampling (R5).** Derived in the Runtime from the textures the
  material binds; no material schema field. `maxLod = 0` for all-single-
  mip materials reproduces today's sampler exactly.
- **Touched:** Asset System (`texture_artifact`, `texture_types`,
  `texture_metadata`, `cook_texture`, `load_texture`), Tools
  (`dds_parser`, `cook_command`, the glTF importer's DDS call), the
  Vulkan Backend's upload-region predicate, Runtime
  (`material_realization`, `scene_load` pass-through), test fixtures that
  realize textures, and every cooked texture (re-cooked by the build).

## Architectural Impact

Yes — an artifact format change (`.atex` v3, texture metadata v3), a
public Asset System data-shape change (`TextureAssetData`, the cook and
DDS-parse signatures), and a Runtime/RHI upload and sampling contract
extending ADR-0085. Recorded in
[ADR-0093](../adr/0093-static-texture-mip-chain-atex-v3-layout-data-shape-and-per-level-upload.md)
(`Accepted`). No new module, dependency, threading or ownership model;
no RHI public API, RenderGraph or shader change.

## Alternatives Considered

In ADR-0093: v2 extended in place; a stored per-level offset table;
`vector<vector>` levels; per-level staging buffers; one upload pass per
level; `VK_LOD_CLAMP_NONE` on every sampler; a material-authored
`max_lod`/mip-filter field; CPU mip generation for PNG at cook time;
runtime generation by blit.

## Testing & Verification Plan

Mapped to Spec 0036 ①c's row (`:877`) and to the requirements:

- **GPU-independent (R1–R3, R6):**
  - `textureMipLevels()`: full chains for square, 2:1, 1:2 and NPOT
    bases (levels, extents, offsets, sizes; sub-block tail levels at 16
    bytes each); partial counts; the `u32` total at 8192².
  - `.atex` v3 encode/decode round-trip at 1, partial and full chains,
    both layouts; rejection of v2, `mip_count` 0 and above the chain
    length, and a size one byte short or long.
  - Metadata v3 round-trip; the loader's metadata-vs-artifact mip
    mismatch rejection.
  - `parseDdsBc7()` on literal byte inputs: full chain returned, no-flag
    = 1 level, truncated tail, count above the chain length.
  - The cooker on `paris_stringlights_diff.dds`: 9 levels, 43,728 bytes.
  - The RHI predicate: a whole-level 2×1 / 1×1 / 4×2 BC7 region accepted;
    a partial unaligned region still rejected; uncompressed formats
    unchanged.
- **GPU (R4, R5; the ①c row's "sampling non-base mips"):** a BC7 texture
  whose levels are distinct solid colours (a hand-built DDS, one BC7
  solid-colour block pattern per level) drawn at scales that select level
  k; the sampled colour must be level k's. Validation Layers on.
- **Golden (R4, R5):** a minified BC7 texture (the string-lights texture
  on a receding or small quad) — mip chain on. Discriminator: the same
  frame with the sampler forced to `maxLod = 0` (base mip only, today's
  aliasing) must fail against it.
- **Regression:** every existing golden byte-identical in Debug and
  Release except where Q4 rules otherwise; Validation Layers clean;
  Android `assembleDebug`; the Bistro content tests (`content` label)
  re-cook and load.

## Risks & Open Questions

- **Q1 — `.atex` v3 vs. v2 extended in place.** **Ruled (2026-09-25): a
  new schema version, v3** (a).
- **Q2 — Texture metadata v3 with a `mip_count` line.** **Ruled
  (2026-09-25): yes** — the sidecar already mirrors width/height/format/
  layout for the loader's cross-check (Spec 0038 added `data_layout` the
  same way).
- **Q3 — Sampler policy.** **Ruled (2026-09-25): R5's derived rule** —
  `maxLod` is the largest `mipCount − 1` among the material's textures and
  `mipFilter` follows `filter`, so every single-mip material's sampler
  stays bit-identical. No material field.
- **Q4 — `bc7_dual_quad` golden.** Its DDS carries 9 levels; with
  passthrough, a minified quad would now sample lower levels and the
  golden could move. Options: (i) re-baseline it as an ADR-0042
  intentional change in the implementation PR; (ii) keep it base-only by
  cooking that fixture with a truncated count. **Ruled (2026-09-25):
  (i)** — the fixture cooks its full chain like every DDS, and if the
  golden moves it is re-baselined as it actually renders (ADR-0042
  intentional change, with evidence, in the implementation PR). The Plan
  measures first.
- **Q5 — DDS payload strictness.** **Ruled (2026-09-25): the payload
  must equal the declared chain exactly** (the census found no padded or
  short file). Fewer bytes are `Truncated`; trailing bytes are
  `MalformedHeader`.
- **Risk — staging peak.** Realizing all of Bistro in one frame stages
  1.911 GiB instead of 1.433 GiB of host-visible memory at once. The
  per-frame upload budgeting is ⑦'s concern; recorded, not solved here.
- **Risk — Android memory.** +33 % on devices where BC7 exists; the
  format gap dominates today.

## Out of Scope / Future Work

- Mip generation for PNG/JPG textures (cook-time box filter) — a later
  workflow if a non-DDS asset set needs it.
- Anisotropic filtering and LOD bias — the next sampler-quality step for
  Bistro's grazing-angle ground and walls.
- Texture streaming and upload budgeting — ⑦.
