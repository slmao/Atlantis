# ADR 0090: Transparency — Pipeline Blend State, Draw Order and Depth Write

- **Status:** Accepted
- **Date:** 2026-09-23
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-09-23 (chat confirmation; reviewed in this
  branch's own PR, alongside Spec 0042's Approval)
- **Related Spec:** [Spec 0042](../specs/0042-transparency.md)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

[Spec 0036](../specs/0036-bistro-parity-roadmap.md) workflow ④ assigns this
ADR the transparent draw-queue and sort-order decision, including where
sorting happens (`0036:478-483`, `:781`). Implementing it also forces two
more decisions, and the three cannot be made apart:
- **blending needs a Pipeline blend state**, and `PipelineCreateParams` has
  no field for one (`src/rhi/include/atlantis/rhi/types.h:268-360`;
  `blendEnable = VK_FALSE` is hardcoded at
  `src/vulkan_backend/src/vulkan_device.cpp:1231`);
- **the depth-write policy** decides what a sorting error looks like, so it
  decides what the sort must guarantee.

Facts this ADR rests on, measured by Spec 0042:

- Every pipeline-state addition to date is an appended, defaulted field on
  `PipelineCreateParams`: `hasCameraUniformBinding`, `hasDepthAttachment`,
  `depthWriteEnabled` (ADR-0071, the sky) and `hasColorAttachment`. **A
  depth-write-off Pipeline is already expressible.**
- `Renderer::drawFrame()` issues draw items in caller order, after the sky,
  inside one RenderGraph draw pass (`src/renderer/src/renderer.cpp:86-113`).
  19 files build draw lists and 23 call `drawFrame()`. The shadow pass
  casts from every item the caller passes.
- All ten PBR shaders already output straight alpha,
  `texColor.a * baseColorFactor.a`, inert today.
- The explicit 4-byte tail pad Spec 0041 left after `emissiveFactor` is
  exactly where a scalar `alphaCutoff` lands under Slang's layout: offset
  108 in three kinds, 124 in `PbrSheen`. The block sizes stay 112 and 128
  (measured with `slangc -reflection-json`).
- The HDR target is `R16G16B16A16_SFLOAT`, for which blending is a
  mandatory format capability in core Vulkan.
- Bistro: 20 `MASK` materials (163 instances; foliage, all cutoff 0.5) and
  3 `BLEND` materials (3 instances, all decals). Its glass is transmission
  with `alphaMode: OPAQUE`.

## Decision

1. **Blend expression — a closed enum on `PipelineCreateParams`.**
   `enum class ColorBlendMode { Disabled, AlphaBlend }`, appended as
   `ColorBlendMode colorBlendMode = ColorBlendMode::Disabled`.
   - `Disabled` is today's state, so every existing call site is unchanged.
   - `AlphaBlend` is the straight-alpha "over" operator. Colour:
     `src × srcAlpha + dst × (1 − srcAlpha)`. Alpha:
     `src × 1 + dst × (1 − srcAlpha)`. The Vulkan backend maps it at the
     single blend-attachment site.
   - The enum is closed and extended only by a future decision, the same
     discipline as `BufferPurpose` and `IndexType` (ADR-0086). Additive or
     premultiplied modes would be new enumerators when a Spec needs them.
2. **Draw order — CPU-side, inside `drawFrame()`, one pass.**
   - The draw pass issues the sky, then every non-blended draw item in the
     caller's order, then every blended item back-to-front.
   - The sort key is the squared distance from the camera's world position
     to the item's world-space sort point; ties keep caller order. The sort
     point is `objectToWorld` applied to the mesh's local-space bounds
     centre, which `createMesh()` computes from the positions it already
     receives (no asset-format change; Spec 0042 O4).
   - The order is produced by one public, pure Renderer function, called by
     `drawFrame()` and unit-tested without a GPU.
   - `drawFrame()` gains an optional camera world position. A blended item
     with none given is a checked programmer error.
   - The shadow pass skips blended items.
   - **RenderGraph is unchanged**: the groups share the draw pass's
     attachments, and ordering within a pass is the Renderer's job.
3. **Depth — blended draws test but do not write; alpha-tested draws are
   opaque.**
   - A `Blend` material's Pipeline has depth test on and
     `depthWriteEnabled = false` (the existing ADR-0071 field).
   - A `Mask` material is an opaque surface with holes: depth test and
     write on, no blending, drawn in the opaque group.
   - Opaque-first ordering is what makes depth-write-off safe: every opaque
     occluder is already in the depth buffer when blended draws test
     against it.
4. **Material expression.** The material carries an explicit
   `alphaMode {Opaque, Mask, Blend}` and an `alphaCutoff` (schema 7 → 8,
   Spec 0042 R1–R3).
   - At realization, `alphaMode` chooses `colorBlendMode` and
     `depthWriteEnabled`.
   - `alphaCutoff` is pushed in the push-constant tail slot (0 unless
     `Mask`), and each PBR fragment shader discards a fragment below it.
   - No push-constant struct changes size; `PbrSheen` stays at exactly 128.
   - `renderer::Material` exposes the mode so the Renderer can order and
     filter.

## Consequences

### Positive

- **One ordering policy for every caller.** All 23 `drawFrame()` callers are
  correct by construction; none carries its own sort.
- **Existing output is unchanged.** Every existing material is `Opaque`:
  blending stays `Disabled`, the cutoff is 0 (nothing is discarded), and
  opaque order is untouched. So every golden is a byte-exact regression
  signal.
- **Sorting errors degrade softly.** With depth write off, two mis-ordered
  blended objects composite in the wrong order (a tint error bounded by
  their alphas) rather than one vanishing behind the other.
- **Small RHI surface.** One enum, one defaulted field; depth already
  existed. No dynamic state, no extension, no feature bit.
- **Testable ordering.** The sort is a pure function over plain data.

### Negative / Trade-offs

- **Per-object ordering is not correct for everything.** Interpenetrating,
  self-overlapping or very large blended meshes can composite in the wrong
  order. Order-independent transparency is the remedy, if ever needed.
- **Renderer now does a little camera arithmetic.** A distance from a given
  position is a narrow extension of Renderer's "never touches raw camera
  math" contract. The camera matrices are still pre-written by the caller.
- **`discard` in every PBR shader.** On some GPUs this disables early depth
  testing for those Pipelines even when it never fires (Spec 0042 O2).
  Splitting `Mask` into shader variants is the documented way out, at the
  cost of ten more shader pairs.
- **Solid shadows for cutouts.** The shadow shader has no UV or texture, so
  `Mask` materials cast solid shadows.
- **Straight, not premultiplied, alpha.** Emissive and specular on a blended
  surface are scaled by its alpha, so a clear glass highlight is dimmer than
  a premultiplied model would draw. It is chosen because it matches glTF's
  model and today's shader output, needing no shader change.
- **Push-constant headroom is spent again.** The tail pad becomes
  `alphaCutoff`; `PbrSheen`'s remaining room shrinks to the 8-byte pad at
  offset 88.

## Alternatives Considered

- **Blend expression as a `bool alphaBlendEnabled`.** Smallest, but closes
  the door on a second mode (additive, premultiplied) without another API
  change, and "true" names no operator. Rejected for the closed enum.
- **Blend expression as a mirror of Vulkan's blend factors and ops.**
  Maximally flexible, but exposes Vulkan's vocabulary through the RHI, and
  every caller could build nonsensical combinations. Rejected.
- **Dynamic blend state** (`VK_EXT_extended_dynamic_state3`). Not
  guaranteed on this engine's targets, and nothing needs per-draw changes.
  Rejected.
- **Sorting in every caller.** About 19 copies of one policy, each a place
  to diverge. Rejected for sorting inside `drawFrame()`.
- **Sorting at the RenderGraph pass level** — a separate transparent pass.
  The pass would share the draw pass's attachments, adding a barrier and
  graph surface while still needing the same per-object sort. Rejected.
- **Front-to-back sorting of opaque draws**, for early-Z. It changes the
  order of every existing draw, putting every golden's byte-identity at
  risk for a performance goal nobody has measured. Out of scope.
- **Depth write on for blended draws.** Turns sort errors into missing
  objects. Rejected (Spec 0042 Investigation 5).
- **Sort point = node origin.** No new data, but it mis-sorts meshes whose
  geometry sits away from their origin. The bounds centre costs 12 bytes per
  Mesh and no asset change. Recorded as Spec 0042 O4.
- **Separate `MaterialKind`s for transparency.** Transparency is orthogonal
  to the BRDF. Rejected in favour of a mode on every PBR kind.
