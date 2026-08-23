# ADR 0055: Sampled Texture and Sampler RHI Module Boundary and Ownership

- **Status:** Proposed
- **Date:** 2026-08-23
- **Deciders:** Pending Human Review (as part of Spec 0016)
- **Related Spec:** [specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md)

## Context

Today's RHI `Texture` (`src/rhi/include/atlantis/rhi/texture.h:7-18`) is
explicitly, deliberately narrow — its own class comment states: *"A GPU
image used, this round, exclusively as a depth attachment (ADR-0023) --
no sampled/shader-read usage, no mipmaps."* Its only format is
`DepthFormat::D32Sfloat` (`types.h:69-72`), a single-variant enum.
[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
(Spec 0007) explicitly deferred a general, sampled texture as future
work (`adr/0023-...md:74-77`): *"A general, sampled `Texture` (material
color maps, etc.) is explicitly future work."*

RHI has no `Sampler` concept of any kind today — confirmed by directory
listing (`src/rhi/include/atlantis/rhi/` contains `buffer.h,
command_list.h, device.h, offscreen_target.h, pipeline.h,
presentation.h, render_target.h, submission_signal.h, texture.h,
types.h`, no `sampler.h`). ADR-0023 itself names this gap directly
(`adr/0023-...md:164`): *"No `Sampler` type, no general resource-format
table."*

This repository's existing resource-ownership principle
([resource_lifetime.md](../docs/architecture/resource_lifetime.md))
applies to any new RHI resource type without modification: *"RHI
resources ... are explicitly owned by whoever created them through RHI,
with RAII-style teardown ... There is no hidden global cache or
refcounted resource pool in Phase 1."* Every existing RHI resource type
(`Buffer`, `Texture`, `RenderTarget`) is move-only, single-owner, held
behind a caller-owned smart pointer, created via a `Device` factory
method returning a `Result`.

## Decision

1. **A new, independent `SampledTexture` RHI type**, not a
   generalization of today's `Texture`. `SampledTexture` represents a GPU
   image usable as a shader-read (sampled) resource: extent,
   `SampledTextureFormat` (see
   [ADR-0057](0057-texture-asset-format-decoder-dependency-and-color-space-contract.md)
   for the format enum's own contract), and a fixed mip-level count of 1
   — no mip-count creation parameter is exposed. Created via a new
   `Device::createSampledTexture(SampledTextureCreateParams)` factory
   (exact name/param shape a Plan-level detail), returning a
   `Result<std::unique_ptr<SampledTexture>, SampledTextureCreateError>`,
   matching `createTexture()`/`createBuffer()`'s own existing shape
   exactly. Move-only, single-owner, RAII-torn-down; owned and held by
   the caller, never by Renderer, Presentation, or RenderGraph (matching
   `Texture`'s own existing ADR-0022 ownership precedent).
   **Today's `Texture` and `DepthFormat` are not modified in any way** —
   no new field, no new constructor parameter, no new derived type. Every
   existing depth-`Texture` call site is untouched.
2. **A new, independent, immutable `Sampler` RHI type.** Created once via
   a new `Device::createSampler(SamplerCreateParams)` factory, move-only,
   single-owner, RAII-torn-down — matching `SampledTexture`'s own
   ownership shape. `SamplerCreateParams` carries exactly two fields this
   round: a `Filter` (`Nearest`\|`Linear`, applied to both minification
   and magnification — no separate mip filter, since every
   `SampledTexture` has exactly one mip level) and an `AddressMode`
   (`Repeat`\|`ClampToEdge`, applied to both U and V). No LOD bias,
   anisotropy, or compare-op parameter exists this round. Once created, a
   `Sampler` exposes no mutator of any kind — its own configuration is
   fixed for its entire lifetime, matching "immutable" literally, not
   merely "not observed to be mutated."
3. **`Sampler` is reusable across multiple `SampledTexture`s.** Nothing
   in `Sampler`'s own construction or lifetime ties it to any one
   `SampledTexture` — a caller may create one `Sampler` and bind it
   against several different textures over its own lifetime (this Spec's
   own fixture only needs one of each, but the type itself does not
   impose a 1:1 relationship).
4. **Both types follow the existing "explicit ownership, borrowed use"
   principle without modification** — no resource pool, no hidden cache,
   no reference counting introduced by this ADR. A caller (this Spec's
   own composition-root-equivalent fixture code) explicitly owns both
   objects for as long as they are needed and destroys them via ordinary
   C++ scope exit / RAII.

## Consequences

### Positive

- Today's depth-only `Texture` remains exactly as narrow, and exactly as
  low-risk to every existing caller, as ADR-0023 left it — zero blast
  radius on Specs 0006–0015's own already-`Approved`/merged code.
- `SampledTexture`/`Sampler` each get a purpose-built, minimal shape
  rather than inheriting unrelated depth-attachment concerns (aspect
  mask, depth-specific usage bits) that a generalized `Texture` would
  otherwise carry.
- Matches this repository's own established pattern of distinct RHI
  types for distinct resource roles even where some fields might
  superficially resemble each other (`RenderTarget` vs. depth `Texture`
  are already two separate types for exactly this reason).
- `Sampler`'s reusability avoids forcing a 1:1 texture/sampler
  relationship a future multi-texture consumer would immediately need to
  undo.

### Negative / Trade-offs

- Two RHI image-like types (`Texture`, `SampledTexture`) now exist side
  by side, with some superficial structural overlap (both wrap a
  `VkImage` + `VkDeviceMemory` + a `VkImageView`) — a future spec
  unifying them behind a shared internal (not necessarily public)
  implementation base is not precluded by this decision, but is not
  designed here either.
- `Sampler`'s fixed, minimal filter/address surface will need a real,
  disclosed extension (LOD bias, anisotropy, compare-op, separate min/
  mag/mip filters) once a real consumer needs one — deferred, not solved,
  by this ADR.

## Alternatives Considered

- **Generalize `Texture` with a `Kind`/`Usage` tag spanning Depth and
  Sampled.** Rejected: `DepthFormat` and a future sampled-color format
  concept would have to coexist in one type's public surface, and every
  existing depth-`Texture` caller (`Renderer`, `render_graph::execute()`,
  Vulkan Backend's own depth-attachment path) would need re-auditing for
  a change with zero functional benefit to any of them.
- **A single, shared `Image` base type with `Texture`/`SampledTexture` as
  thin derived views.** Rejected for this round as unnecessary
  abstraction ahead of a second, genuinely shared concern — matches
  AGENTS.md's own "do not add abstraction knobs for a capability that
  isn't being built" discipline. Nothing here forecloses introducing one
  later if a real shared need emerges.
- **A combined `TextureAndSampler` object, rather than two independent
  types.** Rejected — the Spec's own Goals explicitly require an
  "independent, immutable `Sampler`," reusable across textures; bundling
  them would make that impossible.
