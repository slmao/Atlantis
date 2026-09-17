# Plan: Block-Compressed Textures (BC7)

- **Spec:** [Spec 0038: Block-Compressed Textures](../specs/0038-block-compressed-textures.md)
  (`Approved`, 2026-09-17) — [ADR-0085](../adr/0085-block-compressed-sampled-texture-format-and-vulkan-mapping.md)
  (`Accepted`)
- **Status:** Draft
- **Author:** slmao (drafted by ZCode agent at explicit human direction)
- **Joint Human Review:** pending — Spec 0038's own `Approved` status authorizes
  drafting this Plan only; Implementation requires a separate Joint Human
  Review of Spec 0038 and this Plan together, per AGENTS.md.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0038 in full: `SampledTextureFormat::Bc7Unorm`/`Bc7Srgb` across
RHI → Vulkan Backend → Asset System (schema v2 + DDS cook path) → Runtime
mapping, with the Spec's dual-quad GPU golden — the workflow-⓪ prerequisite
Spec 0037's texture milestone hard-depends on.

## Pre-drafting reading (cited, not restated)

Confirmed by reading the actual code, not assumed:

- `SampledTextureFormat` is a 4-value enum at
  `src/rhi/include/atlantis/rhi/types.h:113-118` (ADR-0055/0057 territory —
  ADR-0085 extends it).
- The Vulkan-side change surface is exactly the one ADR-0085 named:
  `bytesPerTexel()` and `isValidSampledTextureUploadRegion()` at
  `src/vulkan_backend/src/vulkan_sampled_texture.cpp:10-60` — per-texel bytes
  (4/8) and texel-multiply region math (`region.bufferOffsetBytes % texelBytes`,
  `width * height * texelBytes`). BC7 needs 16-byte-per-4×4-block math, 16-byte
  offset alignment, and `ceil(dim/4)` block counts. `isValidSampledTextureCreateParams()`
  (`:24-35`) has no format-awareness yet — the Spec's reject-non-block-aligned
  rule lands here. `createSampledTexture()`/`copyBufferToTexture()` are
  format-agnostic (ADR-0085's own confirmed finding — no changes there).
- Asset System models color space only: `TextureColorSpace {Unorm, Srgb}`
  (`src/asset_system/include/atlantis/asset_system/texture_types.h:11-15`,
  deliberately never an RHI type — module boundary), and `TextureAssetData::
  pixelBytes` is documented "always tightly packed RGBA8" (`:23-27`). A data-
  layout dimension is missing entirely — this Plan adds it without letting an
  RHI type cross the boundary.
- The runtime artifact is schema v1, 36-byte header, format field derived
  from color space (`texture_artifact.h:26-28`, `texture_artifact.cpp:21`);
  encode/decode both assume RGBA8. Metadata carries its own text format
  token (`texture_metadata.cpp:105-135`).
- The composition-root translation point is `toSampledTextureFormat(TextureColorSpace)`
  (`src/runtime/src/material_realization.cpp:35`), the single call site
  pattern feeding `createSampledTexture`/`copyBufferToTexture` (`:361`, `:391`).
- Cooker texture mode is a single `stbi_load()` call site
  (`src/tools/asset_cooker/cook_command.cpp:263-273`); environment mode
  (`.hdr`/`stbi_loadf`) is the adjacent non-RGBA8 precedent (`:346-354`).
- GPU-independent pure-function tests in `tests/vulkan_backend/` have an
  established precedent (the descriptor-pool growth table's own unit tests,
  `vulkan_descriptor_pool_growth.h`'s own header comment).
- Existing sampled textures upload base mip only (no static-texture mip-chain
  upload exists in `material_realization.cpp`'s commit loop).

## Milestones / Task Breakdown

1. **RHI + Vulkan Backend slice.** Add `Bc7Unorm`/`Bc7Srgb` to
   `SampledTextureFormat` (`types.h`); map them in the Vulkan format
   translation (`VK_FORMAT_BC7_UNORM_BLOCK`/`_SRGB_BLOCK` — the `toVkFormat`
   site `createSampledTexture()` already consults); make the two
   `vulkan_sampled_texture.cpp` helpers block-aware: a per-format
   {bytes-per-block=16, block-extent=4} shape replaces flat texel bytes for
   BC7 (legacy formats keep byte-identical behavior), region math becomes
   `ceil(w/4) * ceil(h/4) * 16` with 16-byte offset alignment, and
   `isValidSampledTextureCreateParams()` rejects base mips whose width or
   height is not a multiple of 4 for BC formats only (Spec Requirement 5's
   recoverable rejection — this validator returns bool to existing
   error-classification paths, no new error plumbing). GPU-independent unit
   tests in `tests/vulkan_backend/`: mapping (both enumerators), byte formula
   for aligned/non-aligned/mip-shifted extents, offset-alignment rejection,
   non-aligned-dimension rejection. **Risk gate**: if `createSampledTexture()`'s
   image-tiling/memory-requirements path turns out to need format-specific
   branching beyond what ADR-0085's investigation found (format-agnostic
   confirmed), stop and report — that would widen the Spec's change surface.

2. **Asset System + Tools slice.** Add `TextureDataLayout {Rgba8, Bc7}`
   beside `TextureColorSpace` (asset-side enum, no RHI include — boundary
   preserved); `TextureAssetData` carries layout and, for Bc7, block-packed
   bytes with the RGBA8-only wording corrected to a per-layout contract.
   Artifact schema v1→v2: header gains one explicit `data_layout` byte field
   (36→40 bytes; little-endian shift/mask discipline unchanged; the old
   format field keeps encoding color space) — an appended field, not magic
   widened values, so decode rejects v1 by version check exactly as today's
   exact-equality rule requires. Metadata text format gains a layout token
   with its own version bump. `cookTexture()` DDS mode in the cooker: accept
   `.dds` sources, parse the DDS+DX10 header, accept exactly
   `BC7_UNORM`/`BC7_UNORM_SRGB` (Spec's survey-finalized scope), validate
   block alignment and mip-chain bounds, copy block bytes through verbatim
   (zero decode), and return Spec Requirement 5's named error enumerators
   (malformed header / unsupported format / truncated / non-aligned). All
   existing texture artifacts re-cook to v2 automatically on rebuild
   (binary sources carry no version lines — no source re-authoring, unlike
   the material-schema precedent). **Risk gate**: if the metadata/artifact
   dual version bumps reveal a cross-version consistency check this Plan did
   not anticipate (loadTextureAsset's metadata-vs-artifact self-check),
   surface it in the PR, don't weaken the check.

3. **Runtime mapping + golden + dual-platform regression.** Extend the
   composition-root mapping to `toSampledTextureFormat(TextureColorSpace,
   TextureDataLayout)` (the single translation point, `material_realization.cpp:35`);
   the existing upload call sites pass through unchanged (milestone 1 already
   made the Vulkan upload path block-aware). Base-mip upload only, matching
   existing RGBA8 behavior — mip-chain passthrough is explicitly deferred to
   Spec 0037's importer milestone (disclosed here, not silent). GPU golden
   per Spec's testing plan: dual-quad `Bc7Unorm`/`Bc7Srgb` scene cooked from
   one committed BC7 DDS test fixture — pick the smallest `_diff`-class SRGB
   DDS from the MIT-licensed Bistro set (target ≤512 KB; provenance sidecar
   per the repository content policy's small-asset branch, Spec 0037) —
   rendered through the real fixture/golden-generator pattern with a
   determinism double render, Initial-baseline category (ADR-0042). Windows:
   full Debug + Release build, full ctest, zero real Validation Layer hits.
   Android: `assembleDebug` chain green (cross-platform compile proof); no
   dedicated on-device BC7 test this Plan — the Vulkan path is platform-
   shared, the emulator renders on the same host GPU as the Windows golden,
   and real-device BC7 coverage is Spec 0038's own already-disclosed
   portability gap; stated here as a scoped decision, not an omission.
   **Risk gate**: any Validation Layer hit on the new format path stops the
   milestone — never log-and-continue.

## Files / Modules Touched (expected)

- `src/rhi/include/atlantis/rhi/types.h` (+2 enumerators).
- `src/vulkan_backend/src/vulkan_sampled_texture.{h,cpp}` (block-aware math
  + create-params validation); the Vulkan format-translation site for the two
  new enumerators (within `vulkan_device.cpp`'s existing mapping).
- `tests/vulkan_backend/` (new pure unit tests + CMake entry).
- `src/asset_system/include/atlantis/asset_system/texture_types.h`,
  `texture_artifact.h`, `src/asset_system/src/texture_artifact.cpp`,
  `texture_metadata.{h,cpp}`, `cook_texture.*`, `load_texture.*`
  (layout dimension, schema v2, DDS validation/passthrough).
- `src/tools/asset_cooker/cook_command.cpp` (DDS source mode + error arms).
- `src/runtime/src/material_realization.cpp` (mapping signature extension —
  Runtime-private, not RHI public API).
- `assets/` (test fixture DDS + provenance sidecar + texture asset
  declarations) — test-side only, **not** a packaged app asset: no
  `android_main.cpp`/`build.gradle` lock-step entries this Plan.
- `tests/image_regression/` (dual-quad BC7 golden test + golden-generator
  wiring + goldens/<slug>/).

### Not-touched (stop and report if needed)

RenderGraph, Renderer shaders/pipelines, Platform, World, the material-asset
schema, the environment/IBL pipeline (`.aenv`), and every existing golden.

## Sequencing & Dependencies

Milestones are strictly sequential (1→2→3): 2's artifact contract needs 1's
format enum to name; 3's golden needs 2's cook path to produce its fixture.
One PR carries all three; per-milestone commits for review granularity.

## Verification Checklist

Mapped to Spec 0038's Testing & Verification Plan:

- [ ] GPU-independent unit tests: `toVkFormat` mapping (both enumerators);
      block byte formula (aligned / non-aligned / mip-shifted); 16-byte
      offset alignment; non-block-aligned base-mip rejection; DDS header
      validation (malformed / unsupported DXGI / truncated / non-aligned)
      — one case per named error enumerator.
- [ ] Image regression: dual-quad `Bc7Unorm`/`Bc7Srgb` golden from the
      committed BC7 fixture, determinism double render, Initial-baseline
      category declared in the golden commit.
- [ ] Vulkan Validation Layers clean on every GPU-touching run above.
- [ ] Windows regression: full Debug + Release build, full ctest (existing
      texture goldens unchanged — v2 re-cook is byte-identical content).
- [ ] Android: `assembleDebug` chain green; no lock-step additions (fixture
      is test-side only).
- [ ] External-content SKIP mechanism: N/A this Plan — the fixture is
      committed directly under the content policy's small-asset branch.

## Rollback Plan

Single `git revert` of the PR: the RHI enum, schema v2, and DDS path are all
additive (legacy formats keep byte-identical math); re-cooked v2 artifacts
regenerate as v1 with a rebuild after the revert (binary sources carry no
version stamps — the reverse of the forward re-cook, equally automatic).
No data migration, no persistent state.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this plan:

- The dual-quad golden is an **Initial-baseline** update (ADR-0042 Amendment
  category) — its commit states the category and the four-part evidentiary
  bar, and the human reviews the capture at PR review.
- Schema v2 re-cook completeness is a DoD item: the PR must show a clean full
  rebuild (no stale v1 artifacts anywhere in the build tree), not just the
  new fixture's own cook.
- The fixture commit carries its provenance sidecar (MIT, Bistro-derived) —
  the content policy's small-asset branch, cited in the commit message.
