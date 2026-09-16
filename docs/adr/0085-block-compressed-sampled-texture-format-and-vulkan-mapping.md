# ADR 0085: Block-Compressed `SampledTextureFormat` and Vulkan Mapping

- **Status:** Proposed
- **Date:** 2026-09-16
- **Deciders:** pending Human Review (drafted alongside
  [Spec 0038](../specs/0038-block-compressed-textures.md))
- **Related Spec:** [Spec 0038: Block-Compressed Texture Support](../specs/0038-block-compressed-textures.md) (`Proposed`)
- **Related ADR(s):** [ADR-0055](0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)
  (`Accepted`) — establishes `SampledTexture`/`SampledTextureFormat` as
  an independent, explicitly-extensible RHI boundary; this ADR is that
  extension, not a new boundary. [ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)
  (`Accepted`) — the `Rgba8Unorm`/`Rgba8Srgb` dual-form, real-GPU-proof
  precedent this ADR reuses for BC7's own `Unorm`/`Srgb` pair.
  [ADR-0056](0056-texture-upload-resource-state-and-descriptor-binding.md)
  (`Accepted`) — confirmed, by direct reading, **unaffected** by this
  ADR (see Context). [ADR-0083](0083-gltf-to-atlantis-asset-format-mapping.md)
  (`Proposed`) — D4's own ruling (2026-09-15) that this capability is
  necessary and belongs in its own Spec/ADR, not decided inside a
  Tools-subsystem ADR.

## Context

[Spec 0036](../specs/0036-bistro-parity-roadmap.md) workflow ①'s own
Spec ([Spec 0037](../specs/0037-gltf-importer.md)) found, via direct
measurement of the recommended Bistro glTF source, that its real texture
data is shipped **exclusively** as DDS/BC7-compressed files (390 files,
~2.18 GB, confirmed `BC7_UNORM_SRGB` via direct DDS header inspection of
sampled files) — the glTF's own `.png`/`.jpg` "fallback" image
references correspond to no real file in the source repository.
[ADR-0083](0083-gltf-to-atlantis-asset-format-mapping.md) D4 named two
paths (CPU-side decode into the existing uncompressed pipeline, or
native GPU block-compressed support) and escalated the choice rather
than deciding it inside that Tools-scoped ADR, since either resolution
reaches into RHI/Vulkan Backend territory. **Human Review ruled
2026-09-15:** CPU decode is infeasible (BC7's own 4:1 compression ratio
against ~2.18 GB of real source data implies an estimated ~8-9 GB of
raw, uncompressed texture bytes) — native block-compressed support is
the only viable path. This ADR is that capability's own RHI/Vulkan
Backend design.

**Real source confirmed by direct reading before drafting** (full
citations in [Spec 0038](../specs/0038-block-compressed-textures.md)'s
own Pre-drafting Investigation section, not repeated in full here):
`SampledTextureFormat` (`src/rhi/include/atlantis/rhi/types.h:113-118`)
is a small, independent, explicitly growth-anticipated enum (its own
doc comment states it is "decoupled from the existing `Format` enum");
`toVkFormat(SampledTextureFormat)` (`vulkan_device.cpp:745-758`) is a
plain, no-`default`, exhaustively-checked `switch`;
`createSampledTexture()` (`vulkan_device.cpp:1418-1515`) and
`copyBufferToTexture()` (`vulkan_command_list.cpp:624-656`) are both
confirmed, by direct reading, format-agnostic already; the one real
uncompressed-only assumption lives in `bytesPerTexel()`/
`isValidSampledTextureUploadRegion()` (`vulkan_sampled_texture.cpp:10-60`).

## Decision

### D1 — Two new `SampledTextureFormat` values: `Bc7Unorm`, `Bc7Srgb`

```
enum class SampledTextureFormat {
  Rgba8Unorm,
  Rgba8Srgb,
  Rgba16Float,
  Rg16Float,
  Bc7Unorm,   // new
  Bc7Srgb,    // new
};
```

Mirrors the existing `Rgba8Unorm`/`Rgba8Srgb` linear/sRGB pairing
exactly — `Bc7Srgb` is hardware-linearized by Vulkan's own texture unit
at sample time (ADR-0057's own established contract, reused verbatim,
not reinterpreted); `Bc7Unorm` is not. No other enumerator's own
meaning changes. This is additive-only to an enum ADR-0055 already
designed to grow this way.

### D2 — `VkFormat` mapping: `VK_FORMAT_BC7_UNORM_BLOCK` /
`VK_FORMAT_BC7_SRGB_BLOCK`

```cpp
case atlantis::rhi::SampledTextureFormat::Bc7Unorm:
  return VK_FORMAT_BC7_UNORM_BLOCK;
case atlantis::rhi::SampledTextureFormat::Bc7Srgb:
  return VK_FORMAT_BC7_SRGB_BLOCK;
```

Added to `toVkFormat(SampledTextureFormat)`'s own existing switch
(`vulkan_device.cpp:745-758`) — the direct, standard Vulkan format
enumerators for BC7, no alternative naming considered (these are the
Vulkan specification's own defined names).

### D3 — Image creation, format-feature validation, and upload reuse
existing code paths unchanged

Confirmed by direct reading (Spec 0038's own Investigation, cited
above): `createSampledTexture()`, `hasRequiredSampledTextureFeatures()`,
and `copyBufferToTexture()` each already operate generically over
`VkFormat`/`SampledTextureFormat` with no uncompressed-specific
assumption. **Decision: none of these three functions change.** A
device lacking real BC7 hardware support surfaces this through the
*existing* `vkGetPhysicalDeviceFormatProperties()` query
(`hasRequiredSampledTextureFeatures()`) exactly the way any other
unsupported format already does today
(`SampledTextureCreateError::FormatFeaturesUnsupported`) — no new error
path, no new capability-detection mechanism.

### D4 — A new, block-aware byte-size formula replaces the per-texel
formula, scoped to exactly the functions that need it

`bytesPerTexel()` (`vulkan_sampled_texture.cpp:10-20`) computes bytes
per *texel*, multiplied by `width * height` in
`isValidSampledTextureUploadRegion()` (line 58) — correct for every
existing uncompressed format, wrong for a block-compressed one (BC7
stores 16 bytes per 4×4=16-texel block, not per-texel). **Decision:**
introduce a parallel, block-aware size computation
(`ceil(width/4) * ceil(height/4) * 16` for BC7) used only for
`Bc7Unorm`/`Bc7Srgb`; the four existing uncompressed formats keep using
the unmodified per-texel path — this is a real branch on format
*kind* (block-compressed vs. not), not a generalized "block size = 1×1"
reinterpretation of the existing formula that would risk subtly
changing behavior for the four already-shipping formats.
`isValidSampledTextureCreateParams()` (`vulkan_sampled_texture.cpp:24-35`)
gains an analogous block-aware mip-dimension check — **Plan-stage
detail, not fixed here:** the exact function signature/structure of
this split (a `switch`-on-format-kind inside the existing functions, vs.
two parallel sibling functions) is left to Spec 0038's own Plan.

### D5 — Cooker-side: a new, non-decoding compressed-texture cook path,
absorbed into this ADR — no separate ADR (ruled 2026-09-15)

The one existing `stbi_load()` call site (`cook_command.cpp:273`)
decodes PNG/JPG-class sources into raw RGBA8 pixels — structurally
unable to handle pre-compressed BC7 data, and not asked to. **Decision:**
a new cook code path reads a DDS file's own header directly (magic,
dimensions, `DXGI_FORMAT`), validates it is a supported BC7 variant, and
copies the compressed block bytes into a new texture-artifact variant
verbatim — no decode, no `stb_image` involvement at all for this path.

**Ruled 2026-09-15, closing this D5's own two previously-open
questions:**

- **The compressed-texture artifact extends the existing `.atex`
  format in place** ([ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)'s
  own format) — the two new `SampledTextureFormat` enumerators (D1)
  become legal header values, plus a `schema_version` bump exercising
  that format's own already-mandatory versioning field. **Not** a
  materially distinct artifact shape — mirroring ADR-0045's own three
  prior amendments, each of which widened the mesh artifact's own
  existing format in place rather than inventing a parallel one.
- **This cooker-side change gets no dedicated fourth ADR** — it is
  documented here, as this ADR's own D5, and reuses two already-
  `Accepted` ADRs unchanged: [ADR-0056](0056-texture-upload-resource-state-and-descriptor-binding.md)'s
  own existing `ResourceState`/descriptor-binding contract (D3 above
  already confirmed this ADR's own D1/D2 introduce no new resource-
  state or barrier concern; the cook-path change is CPU-side/offline
  and touches none of ADR-0056's own runtime machinery either) and
  [ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)'s
  own existing artifact-format/versioning mechanism (reused, not
  reinvented, per the bullet above). This matches Spec 0016's own
  precedent for its small `VertexAttributeFormat::Float2` addition
  (documented in-Spec/in-ADR rather than via a dedicated ADR, since it
  gates no module boundary or capability beyond what this ADR's own
  D1-D3 already establish) — the basis for this ruling, not a fresh
  argument invented for D5 alone.

## Consequences

### Positive

- D1-D3 are the minimum real change needed — confirmed by direct
  reading, not assumed, that the large majority of the existing
  `SampledTexture` creation/upload machinery is already format-
  agnostic and needs zero modification.
- D4's own scoped, branch-on-format-kind approach means the four
  existing, already-shipping `SampledTextureFormat` values keep their
  own exact current behavior, byte-for-byte — zero regression risk to
  anything already working.
- Unblocks Spec 0037's own texture-import milestone, and by extension
  Spec 0036's own Bistro Parity Roadmap's eventual finale.

### Negative / Trade-offs

- A new RHI public-API surface (two enumerators) is a real, permanent
  addition to a public boundary — this codebase's own established
  "public API changes need Human Review" discipline applies in full.
- D4 introduces a second, format-kind-branching code path inside
  functions that were previously uniform — a real, if narrow, increase
  in that file's own cognitive surface.
- BC7's own real hardware-support gap on some Android/mobile GPUs
  (Spec 0038's own Non-functional/Portability section) is a real,
  disclosed, unresolved consequence of choosing BC7 specifically, not
  hidden by this ADR.

## Alternatives Considered

See [Spec 0038](../specs/0038-block-compressed-textures.md)'s own
Alternatives Considered (CPU-side decode, rejected by the Human Review
ruling this ADR carries out; ASTC, named Future Work per that same
ruling; BC1/BC3/BC4/BC5/BC6H, rejected on Investigation 5's own
full-file-set survey evidence — zero real use found across all 390
files) — not restated here, since each rejection's own reasoning is
Spec-level, not RHI/Vulkan-mapping-specific.

## Risks & Open Questions

Every question this ADR's own earlier draft left open here has been
ruled (2026-09-15, Spec 0038's own Risks section carries the full
record) — none remain open at ADR level:

- **D5's artifact-format shape and ADR question — ruled, not open**
  (see D5 above): existing `.atex` format extended in place, no
  separate ADR.
- **Non-block-aligned base-mip dimensions — ruled, not open**: a
  recoverable `Result` error at import time (Spec 0038 Requirement 5),
  never a silent pad — Investigation 5 confirms this path is never
  exercised by the real Bistro source itself, specified defensively
  regardless.
- **Remaining Plan-stage detail** (not open design questions): D4's
  exact function-level implementation shape (branching within existing
  functions vs. new sibling functions); the exact new error-enumerator
  names; the exact `schema_version` number for the extended `.atex`
  format.
