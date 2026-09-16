# Spec: Block-Compressed Texture Support

- **Status:** Proposed
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-16
- **Related Plan(s):** None yet — Plan drafting starts only once this
  Spec and its related ADR clear Human Review.
- **Approval:** pending
- **Related ADR(s):** [ADR-0085](../adr/0085-block-compressed-sampled-texture-format-and-vulkan-mapping.md)
  (`Proposed`) — `SampledTextureFormat` extension and `VkFormat`
  mapping, drafted alongside this Spec.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
State each requirement once; link ADR rationale and map verification to the
requirements rather than adding duplicate acceptance or review-decision lists.
Keep implementation code, diffs, review transcripts, and execution logs in PRs.

## Summary

Implements [Spec 0036](0036-bistro-parity-roadmap.md) (Bistro Parity
Roadmap)'s workflow ⓪ — an investigation-discovered, post-Approval
amendment to that roadmap, surfaced while drafting
[Spec 0037](0037-gltf-importer.md) (glTF 2.0 Importer, workflow ①).
Adds native, GPU-side block-compressed texture support: a new
`SampledTextureFormat` value (BC7, both `UNORM` and `SRGB` forms), the
matching Vulkan Backend image-creation/upload path, and an asset-
pipeline cooker path that passes compressed texel data through
verbatim, with no CPU-side decode. This is the RHI/Vulkan Backend-side
capability [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md)
D4 named as outside its own Tools-only scope, ruled necessary by Human
Review (2026-09-15) on quantified-cost grounds: decoding the recommended
Bistro source's own real ~2.18 GB of `BC7_UNORM_SRGB`-compressed DDS
data to this engine's existing uncompressed `Rgba8Unorm`/`Rgba8Srgb`
pipeline would produce an estimated ~8-9 GB of raw texture bytes —
rejected as infeasible.

## Motivation / Problem Statement

`SampledTextureFormat` (`src/rhi/include/atlantis/rhi/types.h:113-118`)
today has exactly four values, all uncompressed: `Rgba8Unorm`,
`Rgba8Srgb`, `Rgba16Float`, `Rg16Float` — 4 or 8 bytes per texel, no
block/compressed concept anywhere in the RHI's own public API. The one
texture-decode dependency this codebase has (`stb_image`, ADR-0041) has
never supported block-compressed formats and is not being asked to
here. Spec 0037's own real, measured investigation found this gap is
not hypothetical: the recommended Bistro glTF source ships its own real
textures **exclusively** as DDS/BC-compressed data — the glTF file's
own `.png`/`.jpg` "fallback" image references do not correspond to any
real file in the source repository. Without this Spec, workflow ①'s own
texture-import milestone has no viable target format to import into.

## Goals

- Add `SampledTextureFormat::Bc7Unorm` and
  `SampledTextureFormat::Bc7Srgb` — mirroring the existing
  `Rgba8Unorm`/`Rgba8Srgb` linear/sRGB pairing exactly
  ([ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)'s
  own already-proven "hardware sRGB decode, proven via a real dual-quad
  GPU golden" precedent, reused for BC7's own dual form) —
  [ADR-0085](../adr/0085-block-compressed-sampled-texture-format-and-vulkan-mapping.md).
- Extend the Vulkan Backend's image-creation path
  (`vulkan_device.cpp:1418-1515`, `createSampledTexture()`) and its
  `toVkFormat(SampledTextureFormat)` switch (`vulkan_device.cpp:745-758`)
  to map the two new values to `VK_FORMAT_BC7_UNORM_BLOCK`/
  `VK_FORMAT_BC7_SRGB_BLOCK` — confirmed by direct inspection that
  `createSampledTexture()`'s own image-creation logic, format-feature
  query (`hasRequiredSampledTextureFeatures()`,
  `hdr_color_target_capability.cpp:11-16`), and `vkCmdCopyBufferToImage`-
  based upload (`vulkan_command_list.cpp:624-656`) are **already format-
  agnostic** — none of these three require a code change beyond the
  `toVkFormat()` switch itself gaining two new cases, since Vulkan's own
  buffer-to-image copy already expresses `imageExtent` in texels and
  handles the compressed-format byte layout internally.
- Extend the one real place this codebase's own block-size assumption is
  hard-coded and **does** need a real change:
  `bytesPerTexel()`/`isValidSampledTextureUploadRegion()`
  (`vulkan_sampled_texture.cpp:10-60`) — this function's own byte-size
  math (`width * height * texelBytes`) is a per-texel, uncompressed-only
  formula; block-compressed formats need a block-aligned equivalent
  (`ceil(width/4) * ceil(height/4) * blockBytes`, BC7's own 16 bytes per
  4×4 block). `isValidSampledTextureCreateParams()` also needs a real
  mip-chain/dimension validation refinement — BC7's own 4×4 block
  granularity means a mip level smaller than 4×4 is still stored as one
  full block, a real constraint this function's own current power-of-
  two shift math does not express.
- Extend the asset texture-cooking pipeline
  (`cook_command.cpp:263-283`, the one `stbi_load()` call site) with a
  **separate, non-decoding** cook path for pre-compressed DDS input —
  reads the DDS header (width, height, `DXGI_FORMAT`), validates it is a
  supported BC7 variant, and copies the compressed block bytes into a
  new texture-artifact variant verbatim — never routing through
  `stb_image`, which cannot decode this data and is not asked to.
- Prove the new format actually samples correctly on real hardware —
  Testing & Verification Plan below.

## Non-Goals

- **Runtime (de)compression of any kind.** This Spec imports pre-
  compressed BC7 data verbatim; it does not compress an uncompressed
  source image into BC7 at cook time, and does not decompress BC7 at
  load or render time. A source asset must already be BC7-compressed
  (e.g. via an external tool, matching how the recommended Bistro source
  already ships this way) — authoring a *new* CC0 texture directly as
  BC7 (rather than PNG, this codebase's own existing precedent) is not
  addressed by this Spec either.
- **Every other block-compressed format family.** BC1 (opaque/1-bit-
  alpha color, historically common for diffuse/albedo without full
  alpha) and BC3 (color + full alpha, historically common where BC7
  hardware support was assumed unavailable) are **not** added by this
  Spec. **Disclosed investigation gap, not a confident exclusion:** this
  Spec's own real sampling of the recommended Bistro source's own DDS
  files was narrow — 2 of 390 real files were downloaded and their DDS
  headers parsed (both diffuse/`_diff` textures, both confirmed
  `BC7_UNORM_SRGB` via direct `DXGI_FORMAT` inspection); the remaining
  ~388 files (including every `_ddna`/normal-map and `_spec`/specular
  variant) were **not** individually format-checked. BC7 is named as
  this Spec's own committed minimum scope because (a) it is confirmed
  present, (b) it is a strict superset of BC1/BC3's own use case
  (color+alpha, any content type, matching DirectXTex's own common
  "BC7 for everything" modern-pipeline recommendation) — but **Plan
  0038's own first real step must be a full `DXGI_FORMAT` survey across
  all 390 real DDS files**, not assumed uniform from this Spec's own
  2-file sample. If that survey finds non-BC7 formats genuinely in use,
  this Spec (or a fast-follow amendment, matching workflow ⓪'s own
  amendment precedent) must widen before workflow ①'s own texture
  milestone can import the real asset completely.
- **BC4/BC5** (single/dual-channel formats, commonly used for tangent-
  space normal maps in other pipelines) — not confirmed present in the
  recommended Bistro source's own sampled files (both samples were
  BC7), not added speculatively.
- **Mip-chain generation.** This Spec imports whatever mip levels (if
  any) the source DDS file itself already contains — it does not
  generate new mip levels from a base level, matching every existing
  `SampledTextureFormat`'s own current `mipLevelCount = 1`-only
  practice (`types.h:164`) unless the Plan finds real multi-mip DDS
  content requiring otherwise.
- **Sampler semantic changes.** No new `Sampler` filtering/addressing
  mode, no anisotropic-filtering change — BC7 samples through the
  existing `Sampler` type unchanged, matching `hasRequiredSampledTextureFeatures()`'s
  own already-confirmed `SAMPLED_IMAGE_FILTER_LINEAR_BIT` requirement
  (linear filtering is a standard, near-universal BC7 hardware
  capability, not a new capability this Spec has to prove separately
  from the format-feature query already in place).
- **A general "any DDS/any DXGI_FORMAT" import path.** This Spec
  supports exactly the one format family the real, measured Bistro
  source needs (BC7) — not a general-purpose DDS reader for arbitrary
  future content.

## Requirements

### Functional

1. **RHI public API.** `SampledTextureFormat` gains `Bc7Unorm` and
   `Bc7Srgb` ([ADR-0085](../adr/0085-block-compressed-sampled-texture-format-and-vulkan-mapping.md)).
   No other RHI type's own public shape changes — `SampledTextureCreateParams`,
   `SampledTextureUploadRegion`, and `SampledTexture` itself are reused
   completely unchanged (Investigation below confirms none of their
   own fields assume an uncompressed format).
2. **Vulkan mapping.** `toVkFormat(SampledTextureFormat)`
   (`vulkan_device.cpp:745-758`) gains two new `case` arms; the
   `hasRequiredSampledTextureFeatures()` format-feature query
   (`hdr_color_target_capability.cpp:11-16`) applies unchanged to the
   new `VkFormat` values — a real device lacking BC7 hardware support
   surfaces this exactly the way it already surfaces any other
   unsupported format (`SampledTextureCreateError::FormatFeaturesUnsupported`),
   no new error path needed.
3. **Block-aware byte-size validation.** `isValidSampledTextureUploadRegion()`
   (`vulkan_sampled_texture.cpp:37-60`) gains a block-compressed-aware
   byte-size formula for BC7 (`ceil(width/4) * ceil(height/4) * 16`
   bytes), distinct from the existing per-texel formula the four
   uncompressed formats keep using unchanged.
   `isValidSampledTextureCreateParams()` (`vulkan_sampled_texture.cpp:24-35`)
   gains a real mip-level/dimension constraint check for block-
   compressed formats — a mip level's own stored dimensions round up to
   the nearest 4×4 block, not down to zero, a genuine behavioral
   difference from the existing power-of-two-halving-only logic.
4. **Cooker integration.** A new, non-decoding cook path
   (Plan-stage detail: a new `cookCompressedTexture()`-shaped function,
   mirroring `cookTexture()`'s own existing signature/error-handling
   shape but reading a DDS header instead of calling `stbi_load()`)
   validates the source DDS file is a supported BC7 `DXGI_FORMAT`
   variant, and writes a new texture-artifact schema variant (Plan-
   stage detail: whether this is a new artifact `schema_version` on the
   *existing* texture-artifact format, or a materially distinct
   artifact shape, is not fixed by this Spec) carrying the compressed
   block bytes verbatim, block-dimension metadata, and the chosen
   `SampledTextureFormat` value.
5. **Validation, both cook-time and decode-time**, matching this
   repository's own established double-validation discipline (every
   existing artifact format validates at both write and read):
   malformed/truncated DDS header, unsupported `DXGI_FORMAT`, dimensions
   not expressible in whole 4×4 blocks at the base mip level (a real
   BC7 constraint — Plan-stage detail on whether non-block-aligned base
   dimensions are rejected outright or padded), and a decode-time
   artifact byte-count mismatch are each a distinct, named error
   enumerator, never a silent fallback or corrupted read.
6. **Error handling** follows this repository's own `Result`/error-enum
   convention throughout (AGENTS.md), matching Requirement 5's own
   enumeration.

### Non-functional

- **Performance:** Real, quantified motivation (Motivation above):
  avoids an estimated ~8-9 GB CPU-side decompression this Spec's own
  existence exists to prevent. GPU upload cost for compressed data is
  strictly smaller (less data transferred) than the uncompressed
  alternative would have been — not independently re-measured by this
  Spec beyond that qualitative direction.
- **Memory:** BC7's own real compression ratio (4:1 against raw RGBA8,
  16 bytes per 4×4=16-texel block) means the recommended Bistro
  source's own real ~2.18 GB of DDS data occupies **~2.18 GB of GPU
  memory** under this Spec's design (verbatim pass-through, no
  inflation) — versus the ~8-9 GB the rejected CPU-decode alternative
  would have required. This is the real, load-bearing memory argument
  for this Spec's own existence, not a secondary benefit.
- **Portability (Vulkan-only Phase 1):** BC7 (`VK_FORMAT_BC7_*_BLOCK`)
  is a **mandatory-support format class on desktop Vulkan
  implementations** per the Vulkan specification's own device-support
  guarantees for `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT`-class usage on
  most real desktop GPUs, but is **not** universally guaranteed on
  every Vulkan-capable mobile/Android GPU (ASTC is the more commonly
  mandatory compressed format class on mobile hardware) — a real,
  disclosed portability risk for Atlantis's own Android target (AGENTS.md
  Phase 1 constraints), not resolved by this Spec: `hasRequiredSampledTextureFeatures()`'s
  own existing format-feature query already surfaces an unsupported
  device honestly (`FormatFeaturesUnsupported`, Requirement 2) rather
  than silently failing, but this Spec does not add an ASTC fallback
  path or otherwise guarantee Android device coverage. Flagged as a
  real Risk below, not minimized.

## Pre-drafting Investigation (required reading, cited against real
source)

Directly read before drafting, per this Spec's own instruction:

- **`SampledTextureFormat`** (`src/rhi/include/atlantis/rhi/types.h:110-118`):
  four existing values (`Rgba8Unorm`, `Rgba8Srgb`, `Rgba16Float`,
  `Rg16Float`), the enum's own doc comment already states it is
  "decoupled from the existing `Format` enum" specifically so it can
  grow independently — this Spec's own two new values are exactly the
  kind of independent growth that comment anticipates.
  `SampledTextureCreateParams` (`types.h:159-165`) — `extent`, `format`,
  `dimension`, `mipLevelCount` — no field assumes an uncompressed byte
  layout.
- **`toVkFormat(SampledTextureFormat)`** (`vulkan_device.cpp:745-758`):
  a plain, exhaustive `switch`, no `default` case (this repository's own
  established `/w14062` MSVC-enforced-exhaustiveness discipline,
  `asset_cooker`/`CMakeLists.txt` precedent) — adding two new
  enumerators requires the compiler itself to flag every switch over
  `SampledTextureFormat` left unhandled, a real, existing safety net
  this Spec's own implementation inherits for free.
- **`createSampledTexture()`** (`vulkan_device.cpp:1418-1515`): image
  creation, format-feature query, image-format-property query
  (`vkGetPhysicalDeviceImageFormatProperties`), memory allocation/
  binding, image-view creation — every step reads `params.format`
  generically via `toVkFormat()`/the returned `VkFormat`; **confirmed,
  not assumed, that no step here special-cases an uncompressed byte
  layout** — this function needs zero changes beyond what
  `toVkFormat()`'s own extension already provides.
- **`hasRequiredSampledTextureFeatures()`** (`hdr_color_target_capability.cpp:11-16`):
  `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | ..._FILTER_LINEAR_BIT | ..._TRANSFER_DST_BIT`
  — a real Vulkan format-feature-flag query, correct and unchanged for
  any `VkFormat` including BC7's own.
- **`copyBufferToTexture()`** (`vulkan_command_list.cpp:624-656`):
  `vkCmdCopyBufferToImage` with `bufferRowLength = 0`/`bufferImageHeight = 0`
  (tightly-packed, format-implied layout) and `imageExtent` expressed in
  texels (`sourceRegion.extent.width/height`, not blocks) — **Vulkan's
  own copy command already handles block-compressed source data
  correctly under this exact calling convention**; confirmed by reading
  the Vulkan specification's own `VkBufferImageCopy` semantics for
  compressed formats, not merely assumed. This function needs zero
  changes.
- **`bytesPerTexel()`/`isValidSampledTextureUploadRegion()`/
  `isValidSampledTextureCreateParams()`** (`vulkan_sampled_texture.cpp:10-60`):
  the one real place a per-texel, uncompressed-only byte-size formula
  is hard-coded (Requirement 3 above) — this Spec's own real, necessary
  code-change surface, confirmed narrow (one file, three functions) by
  this direct reading, not a broad refactor.
- **[ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)**
  (`Accepted`): established `SampledTexture` as its own independent RHI
  type with an explicitly-extensible `SampledTextureFormat` — this
  Spec's own extension is exactly the kind of growth that ADR's own
  Decision anticipated, not a boundary violation.
- **[ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md)**
  (`Accepted`): `ResourceState::Undefined → TransferDestination →
  ShaderRead`, an exhaustive Vulkan Backend barrier-plan table with no
  wildcard transition — confirmed, by direct reading, that these
  transitions are Vulkan image-layout concerns wholly independent of
  pixel format; **this Spec touches none of ADR-0056's own barrier/
  resource-state machinery**, a real, disclosed scope-narrowing
  confirmation, not an assumption.

## Proposed Design

High-level shape only (Plan-stage detail for concrete function
signatures/algorithms, per AGENTS.md's documentation-home table):

Two new `SampledTextureFormat` enumerators
([ADR-0085](../adr/0085-block-compressed-sampled-texture-format-and-vulkan-mapping.md)),
mapped to `VK_FORMAT_BC7_UNORM_BLOCK`/`VK_FORMAT_BC7_SRGB_BLOCK` in the
Vulkan Backend's existing `toVkFormat()` switch. Image creation, format-
feature validation, and buffer-to-image upload reuse existing,
confirmed-format-agnostic code paths unchanged (Investigation above). A
new block-aware byte-size formula replaces the per-texel-only one in
exactly the validation functions that need it. A new, non-decoding
cooker path reads a DDS header directly and emits a new texture-
artifact variant carrying compressed bytes verbatim.

## Architectural Impact

**Yes** — a new `SampledTextureFormat` public-API extension and its
`VkFormat` mapping, recorded in
[ADR-0085](../adr/0085-block-compressed-sampled-texture-format-and-vulkan-mapping.md),
extending [ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)'s
own already-established, deliberately-extensible boundary — not a new
module, not a new ownership model, not a new threading concern. The
asset-cooker-side non-decoding compressed-texture cook path is a new
capability within the existing `atlantis_asset_cooker`/Asset System
boundary (no new Tools subsystem, unlike Spec 0037's own glTF
importer) — whether it needs its own, separate ADR beyond ADR-0085, or
is small enough to document within this Spec's own Requirements/Plan
(matching Spec 0016's own precedent for its small `VertexAttributeFormat::Float2`
addition, documented in-Spec rather than via a dedicated ADR), is
**Plan-stage/Human-Review detail, not fixed here**.

## Alternatives Considered

- **CPU-side decode to existing `Rgba8Unorm`/`Rgba8Srgb`.** Rejected —
  this is the alternative this Spec's own existence supersedes,
  quantified and ruled out in
  [ADR-0083](../adr/0083-gltf-to-atlantis-asset-format-mapping.md) D4
  (2026-09-15). Restated here only as the alternative this Spec is the
  chosen resolution *against*, not re-argued.
- **ASTC instead of, or in addition to, BC7.** Considered given ASTC's
  own better mobile/Android hardware-support guarantee (Non-functional,
  Portability above) — not adopted this round: the recommended Bistro
  source's own real data is DDS/BC7, not ASTC; transcoding BC7→ASTC at
  cook time is real additional work this Spec does not need for its own
  stated goal (Windows-side Bistro import), and would reopen the exact
  "decide a format transcode without real data forcing it" pattern
  ADR-0006's own dependency discipline warns against. Named as a real,
  disclosed future Android-portability gap (Non-functional), not
  silently ignored.
- **BC1/BC3/BC4/BC5 alongside BC7 in this same Spec.** Considered —
  deferred per Non-Goals' own disclosed investigation gap (2-of-390
  files sampled); adding formats speculatively, without a confirmed
  real need, is exactly the pattern this repository's own dependency/
  scope discipline (ADR-0006, ADR-0045's own original reasoning) warns
  against.

## Testing & Verification Plan

Per AGENTS.md's Testing requirements:

- **GPU-independent unit tests**: `toVkFormat()` mapping (both new
  enumerators), the block-aware byte-size formula in
  `isValidSampledTextureUploadRegion()` (correct for block-aligned and
  non-block-aligned dimensions alike), `isValidSampledTextureCreateParams()`'s
  own new mip/dimension constraint, and the cooker's own DDS-header
  validation (malformed header, unsupported `DXGI_FORMAT`, truncated
  file) — each a distinct test case per Requirement 5's own named error
  enumerators.
- **A real, GPU-required texture-sampling golden**, mirroring Spec
  0016's own established dual-quad `Rgba8Unorm`/`Rgba8Srgb` golden
  precedent exactly: two textured quads, one `Bc7Unorm` one `Bc7Srgb`,
  cooked from the **same** real BC7 source texel data, rendered in one
  golden — proving both the cook→load→upload→bind→sample path works end
  to end, and that Vulkan's own real hardware sRGB decode is visibly,
  measurably different between the two quads (ADR-0057's own exact
  "proof, real and GPU-based, not CPU-only" standard, reused). Vulkan
  Validation Layers clean.
- **External-content-dependent tests SKIP when the source BC7 texture
  file is absent**, printing fetch instructions — this Spec's own real
  BC7 test source is small enough (a single representative DDS file,
  not the full Bistro set) that it may qualify for direct commit under
  Spec 0037's own repository-level content policy's 50 MB threshold;
  Plan-stage detail whether it is committed directly or fetched via that
  same policy's script mechanism.
- **Golden images are committed unconditionally**, per ADR-0042's own
  contract and Spec 0037's own content-policy carve-out — never subject
  to the SKIP-on-missing-content mechanism above, which applies only to
  large *source* content, not golden PNGs/sidecars.

## Risks & Open Questions

- **BC1/BC3/BC4/BC5 scope is not conclusively settled** — Non-Goals'
  own disclosed 2-of-390-file sample; Plan 0038's own first real step
  must be a full `DXGI_FORMAT` survey of the real Bistro DDS set before
  this Spec's own scope can be considered final.
- **Android/mobile BC7 hardware-support coverage is a real, unresolved
  portability gap** (Non-functional above) — not solved by this Spec,
  disclosed rather than silently deferred.
- **Whether the compressed-texture cook path needs its own dedicated
  ADR, or is documented within this Spec alone**, is left to Human
  Review (Architectural Impact above).
- **Non-block-aligned base-mip dimensions** (a source texture whose
  width/height is not a multiple of 4) — whether Requirement 5's own
  validation rejects these outright or pads them is not fixed by this
  Spec; the real recommended Bistro source's own sampled files (512×512,
  4096×4096) are both already block-aligned, so this may prove moot in
  practice, not confirmed either way across the full 390-file set.
- **Whether the new compressed-texture artifact is a new
  `schema_version` on the existing texture-artifact format or a
  materially distinct artifact shape** (Requirement 4) is Plan-stage
  detail.

## Out of Scope / Future Work

- Everything in Non-Goals.
- An ASTC (or other mobile-oriented compressed format) path, if
  Android's own real BC7 hardware-support gap (Risks above) proves to
  matter in practice.
- Workflows ①-⑦ of Spec 0036's own roadmap — this Spec implements
  workflow ⓪ only; workflow ①'s own texture-import milestone consumes
  this Spec's own output but is not implemented by it.
