# Spec: Transparency

- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-23
- **Related Plan(s):** none yet — Plan 0042 is drafted only after this Spec
  is Approved.
- **Approval:** pending
- **Related ADR(s):** [ADR-0090](../adr/0090-transparency-blend-state-draw-order-and-depth-write.md)
  (Proposed, on this branch)

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Give Atlantis's PBR materials the two transparency paths glTF defines:
**alpha test** (`MASK`: a fragment below a cutoff is discarded, everything
else renders as opaque) and **alpha blend** (`BLEND`: the fragment is
composited over what is already in the frame). This is
[Spec 0036](0036-bistro-parity-roadmap.md) workflow ④. It needs three
coupled decisions, recorded together in ADR-0090:
- how a Pipeline expresses blending (a closed `ColorBlendMode` enum on
  `PipelineCreateParams` — an RHI public-API change);
- where and how blended draws are ordered (inside `Renderer::drawFrame()`'s
  one existing draw pass, opaque first in caller order, then blended draws
  back-to-front, per object, on the CPU);
- depth writes (blended draws test depth but do not write it; alpha-tested
  draws are opaque and write it).

The material gains an `alphaMode` and an `alphaCutoff` (schema 7 → 8). The
cutoff costs **no push-constant bytes**: it takes the explicit tail pad
Spec 0041 left in every PBR struct, measured below.

The investigation also corrects the roadmap's premise. **Bistro's glass is
not alpha-blended.** Its 18 glass materials are `KHR_materials_transmission`
with `alphaMode: OPAQUE` and alpha 1.0; its only three `BLEND` materials are
wall decals. What this Spec gives Bistro directly is cutout foliage and
decals. See-through glass needs a separate mapping decision, open question
O3.

## Motivation / Problem Statement

Spec 0036 ④ (`0036:453-484`) records the ceiling: no Pipeline can blend
(`colorBlendAttachment.blendEnable = VK_FALSE`, hardcoded). The base-colour
alpha is stored end to end but inert — deliberately kept RGBA "to avoid a
future schema bump for a Transparency spec" (ADR-0066 item 7). The glTF
importer drops every `alphaMode` it sees, reporting it as undestined
(`src/tools/gltf_importer/material_import.cpp:420-423`).

In Bistro, measured from `content/bistro/bistro.gltf`, that ceiling costs:
- **20 `MASK` materials, 163 mesh instances** — leaves, foliage, flowers,
  plants, chains, a manhole cover, lettering, forge windows, glass
  ornaments. Every one has `alphaCutoff` 0.5, is double-sided, and takes its
  alpha from a BC7 sRGB diffuse texture (`DXGI_FORMAT_BC7_UNORM_SRGB`, which
  the engine already cooks). Rendered opaque today, each renders as a solid
  quad or card.
- **3 `BLEND` materials, 3 instances** — `Decals`, `Decal_Bottomdirt`,
  `Decal_Crack`, all on the same BC7 decal atlas.
- **18 transmission materials, 339 instances** — every glass pane, bottle
  and headlight. They are `OPAQUE` with alpha 1.0 and `transmissionFactor`
  0.66–0.95. glTF core says they render opaque; making them see-through is
  a transmission decision, not an alpha one (O3).

## Goals

- A PBR material can be `OPAQUE` (today's behaviour), `MASK` (discard below
  `alphaCutoff`) or `BLEND` (composited by its alpha).
- Blended draws composite correctly over opaque geometry and over each
  other when they do not interpenetrate: opaque first, then blended
  back-to-front.
- Every existing golden is byte-identical afterwards. An `OPAQUE` material
  renders exactly as today, and opaque draw order is unchanged.
- The ordering is decided by one pure, GPU-independent function, so it is
  unit-tested without a GPU.

## Non-Goals

- **Refraction, screen-space reflections, and transmission as a physical
  effect.** Spec 0036 pins these out. Whether Bistro's transmission glass is
  *approximated* as blending is O3, not a goal here.
- **Order-independent transparency.** The ordering is per object, not per
  triangle or per pixel. Interpenetrating or self-overlapping blended
  meshes can composite in the wrong order; that is the accepted cost of the
  approach (ADR-0090 Consequences).
- **Transparency for `LitTextured` and `UnlitTextured`.** No Bistro material
  and no importer output uses them; the source grammar rejects the fields on
  both kinds.
- **Cutout shadows.** `shadow_cast.slang` has no UV input and no texture
  binding, so a `MASK` material casts a solid shadow; foliage shadows will be solid shapes. This
  is recorded as a limitation, not solved here (Out of Scope).
- **Alpha-to-coverage, and MSAA generally.** The engine has no MSAA.
- **Decal depth bias.** If a coplanar decal z-fights with its wall, that is
  a later, separate fix (Risks).
- **Premultiplied alpha.** The shader output is straight (unpremultiplied)
  colour; ADR-0090 records why.

## Requirements

### Functional

1. **Material data.** `MaterialAssetData` gains:
   - `alphaMode`, a closed enum `{Opaque, Mask, Blend}`, default `Opaque`;
   - `alphaCutoff`, a float, default 0.5 (the glTF default), meaningful
     only for `Mask`. It must be finite and in `[0, 1]`, validated at cook
     time with the existing factor validator.
2. **Legal kinds.** `Mask` and `Blend` are legal on the four PBR kinds only.
   `Opaque` is legal everywhere. The source grammar rejects the fields on
   `lit_textured`/`unlit_textured` with a named error.
3. **Formats.** Source, artifact and metadata bump together with no
   dual-version reader, per ADR-0066:
   - source 7 → 8, with two optional prefix-identified lines
     (`alpha_mode:`, `alpha_cutoff:`) — the Spec 0041 O1 mechanism;
   - artifact 7 → 8, 108 → 116 bytes;
   - metadata 6 → 7, both lines mandatory.
4. **RHI blend state (ADR-0090 Decision 1).** `PipelineCreateParams` gains
   `ColorBlendMode colorBlendMode = ColorBlendMode::Disabled`. `AlphaBlend`
   maps to straight-alpha "over" in the Vulkan backend; `Disabled` is
   today's state, so every existing Pipeline is unchanged.
5. **Pipeline state per mode (ADR-0090 Decision 3).**
   - `Opaque` and `Mask`: blending off, depth test on, depth write on — as
     today.
   - `Blend`: `AlphaBlend`, depth test on, **depth write off**, via the
     existing `depthWriteEnabled` field (ADR-0071).
6. **Alpha test in the shader.** All ten PBR fragment shaders discard a
   fragment whose final alpha is below `alphaCutoff`. The cutoff is pushed
   as 0 for `Opaque`/`Blend`, where alpha ≥ 0 means no fragment is ever
   discarded. The cutoff occupies the explicit tail pad after
   `emissiveFactor` (Investigation 4); no struct changes size.
7. **Draw order (ADR-0090 Decision 2).** `Renderer::drawFrame()`'s draw
   pass issues:
   1. the sky, as today;
   2. every non-`Blend` draw item, in the caller's order (unchanged);
   3. every `Blend` draw item, sorted back-to-front by the squared distance
      from the camera's world position to the item's world-space sort
      point, ties kept in caller order.

   The order comes from a public, pure Renderer function. `drawFrame()`
   gains an optional camera world position; a `Blend` item with no
   position given is a checked programmer error.
8. **Shadows.** `Blend` draw items are skipped by the shadow pass; `Mask`
   items cast (solid) shadows.
9. **Importer (recommended in scope, O1).** glTF `MASK` maps to
   `Mask` + `alphaCutoff`, and `BLEND` to `Blend`, replacing the current
   drop-and-report. Transmission materials stay `Opaque` (O3).

### Non-functional

- **Performance.** The blended sort is O(n log n) in blended items only —
  3 in Bistro. The `discard` in all ten PBR fragment shaders can disable
  early depth testing for opaque PBR draws on some GPUs. That cost is
  accepted now, and O2 records the variant-split alternative with its
  price.
- **Memory.** 8 more bytes per material artifact. The Mesh gains a 12-byte
  local sort point (O4).
- **Portability.** Blending into `VK_FORMAT_R16G16B16A16_SFLOAT`, the HDR
  target, is a mandatory format capability
  (`VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT`) in core Vulkan. There is
  no dynamic state, and no feature bit is needed.
- **Golden neutrality.** Every existing material is `Opaque`: the cutoff is
  0 so no fragment is discarded, blending stays `Disabled`, and opaque order
  is unchanged. So every existing golden must stay byte-identical — a
  stop-and-report gate.

## Pre-drafting Investigation (required conclusions, cited against real files)

### Investigation 1 — blend state today

- `colorBlendAttachment.blendEnable = VK_FALSE` is at
  `src/vulkan_backend/src/vulkan_device.cpp:1231`, not `:1125` as ADR-0066
  and Spec 0036 cite — that citation predates later growth of the file.
  `colorWriteMask` is RGBA (`:1229-1230`).
- **`PipelineCreateParams` has no blend field**
  (`src/rhi/include/atlantis/rhi/types.h:268-360`), so expressing blending
  is an RHI public-API change and needs an ADR. Every past pipeline-state
  addition took the same shape: an appended, defaulted field that leaves
  existing call sites untouched — `hasCameraUniformBinding`,
  `hasDepthAttachment`, `hasColorAttachment`, and **`depthWriteEnabled`,
  which already exists** (ADR-0071, added for the sky; wired at
  `vulkan_device.cpp:1217-1226`). The depth half of this Spec therefore
  needs no new RHI surface.
- Depth compare is `VK_COMPARE_OP_LESS` (`:1226`); culling is
  `VK_CULL_MODE_NONE` everywhere (`:1202`), so double-sided materials
  already rasterize both faces.

### Investigation 2 — draw order today

- `Renderer::drawFrame()` issues `drawItems` in exactly the caller's order
  (`src/renderer/src/renderer.cpp:113`), after the sky (`:100-111`). There
  is no sorting and no partitioning. Its RenderGraph has three passes —
  shadow, draw, output transform (`:72`, `:86`, `:258`) — and the graph
  orders passes, not draws within a pass.
- `DrawItem` is `{mesh, material, objectToWorld}`
  (`src/renderer/include/atlantis/renderer/draw_item.h:17-21`). Renderer's
  contract is that it "never touches raw camera math": the camera matrices
  arrive pre-written in a buffer it only binds.
- **19 files build `DrawItem` lists and 23 call `drawFrame()`**: the
  Runtime (`runtime_application.cpp:1540-1639`) and every image-regression
  fixture. Sorting in callers means about 19 copies of one policy; sorting
  in `drawFrame()` means one, at the price of `drawFrame()` receiving the
  camera's world position. The Runtime already computes it every frame
  (the `CameraWorldPositionData` write).
- The shadow pass casts from the caller's shadow list, which the Runtime
  passes as the whole `drawItems` span (`runtime_application.cpp:1653-1657`).
- **No mesh carries bounds**: nothing in the Asset System or Renderer
  public headers records an AABB. Every Bistro mesh node has a local
  transform (2,909 of 2,909), so a node's origin is a usable sort point,
  but not always the geometry's centre (O4).

### Investigation 3 — the material schema

- Schema v7 (Spec 0041) has `baseColorFactor[4]` with alpha
  (`material_types.h`) but no mode. The shaders already compute
  `alphaOut = texColor.a * baseColorFactor.a`
  (`shaders/pbr_direct_lit/pbr_direct_lit.slang:160`, the same line in all
  ten) and write it, inert.
- **A cutoff alone is not enough.** glTF's three modes differ in behaviour,
  not only in a threshold:
  - `OPAQUE` must ignore alpha even when the texture has some;
  - `BLEND` must composite and not write depth.

  An implicit rule ("cutoff > 0 means mask; alpha < 1 means blend") would
  misclassify every opaque material whose texture happens to carry alpha,
  and would give Pipeline state no explicit source. So the schema takes an
  explicit mode plus a cutoff: a v8 bump, the ADR-0081/0089 shape.
- Bistro's `alphaCutoff` is 0.5 on all 20 `MASK` materials, which is also
  the glTF default.

### Investigation 4 — shader impact, quantified

- **Alpha test:** one `discard` line in each of the ten PBR fragment shaders,
  and one push-constant field. Measured with `slangc -reflection-json` on
  scratch copies with `float alphaCutoff` after `emissiveFactor`:

  | Kind | `emissiveFactor` | `alphaCutoff` | Block size |
  |---|---|---|---|
  | `PbrDirectLit`/`PbrIbl`, Clearcoat, Anisotropic | 96 | **108** | 112 (unchanged) |
  | `PbrSheen` | 112 | **124** | **128 (unchanged)** |

  The cutoff sits exactly in the explicit `_padEmissive` slot Spec 0041
  added, so the C++ side renames a pad rather than growing a struct. Sheen
  stays at exactly the 128-byte guarantee.
- **Alpha blend:** **zero shader changes.** The shaders already output
  straight alpha; only the Pipeline's blend state changes.
- `lit_textured` and `textured_quad` (unlit) are not touched.
- `shadow_cast.slang` has one binding (the light-space uniform,
  `shaders/shadow_cast/shadow_cast.slang:21-27`), a position-only vertex
  input and no texture, so it cannot alpha-test: cutout shadows would need
  a UV input and a texture binding as well as the test.

### Investigation 5 — depth writes

With opaque draws issued first, depth *testing* is what makes an opaque
wall hide a window behind it, and it stays on for every mode. Depth
*writing* is the choice. It fails differently depending on the answer when
the per-object sort is wrong, which it will sometimes be (interpenetration,
large objects):

| Blended draw writes depth? | When two blended objects are mis-ordered |
|---|---|
| Yes | The nearer one, drawn first, fails the depth test for the farther one: **the farther one disappears** behind it. A hard, highly visible error. |
| No | Both draw; they composite in the wrong order: **a wrong tint**, bounded by the alphas involved. A soft error. |

Opaque geometry is unaffected either way, because it is drawn first. So
blended draws do not write depth. Alpha-tested (`MASK`) fragments either
survive fully opaque or are discarded, so they are opaque surfaces: they
write depth and belong in the opaque queue, where their order does not
matter.

### Investigation 6 — what Bistro needs from each path

| glTF class | Materials / instances | Path | What it needs from this Spec |
|---|---|---|---|
| `MASK` | 20 / 163 | Opaque queue + `discard` | The alpha test, `alphaCutoff` 0.5, BC7 alpha (already cooked), double-sided (already: culling off). Shadows stay solid. |
| `BLEND` | 3 / 3 | Blended queue, sorted | Blend state, depth write off, sort. All three are decals, so coplanar z-fighting is the practical risk. |
| Transmission | 18 / 339 | Opaque (glTF core) | Nothing directly. Rendering them see-through means treating transmission as blend (O3). |

## Proposed Design

Material → realization → Pipeline:
1. `alphaMode` travels the path every material field travels (v8).
2. At realization, the Runtime chooses the Pipeline's `colorBlendMode` and
   `depthWriteEnabled` from `alphaMode`, and passes `alphaCutoff` (0 unless
   `Mask`) into the material's push-constant payload, in the existing tail
   slot.
3. `renderer::Material` gains the mode, so the Renderer can order and filter
   without re-deriving it.

The frame: `drawFrame()` calls one public pure function to produce the
draw order, then issues the sky, the non-blended items in caller order,
and the blended items back-to-front. The sort point is the item's
`objectToWorld` applied to the mesh's local sort point (O4). The shadow
pass skips blended items. No RenderGraph change: all three groups share
the draw pass's attachments, and splitting them into passes would add
barriers and graph surface for nothing.

## Architectural Impact

**Yes — ADR-0090 (Proposed, on this branch).** It covers three decisions
that cannot be taken separately:
1. the RHI Pipeline blend expression (a public-API change);
2. the draw-queue order and sort key, and where sorting lives;
3. the depth-write policy.

They are coupled: depth-write-off is only safe because opaque draws come
first; the sort exists only because blending exists; and the blend
expression determines what the sort protects. ADR-0090 also records the
material expression (explicit mode plus cutoff, cutoff in the push-constant
tail pad), which drives the Pipeline state.

Other surfaces:
- `renderer::Material` gains a defaulted parameter and an accessor (the
  ADR-0081/0089 precedent).
- `drawFrame()` gains one optional parameter (existing callers unchanged).
- `Mesh` gains a local sort point computed from positions it already
  receives (O4) — no asset-format change.
- No module boundary, dependency, threading or ownership change.
  RenderGraph, Platform and World are untouched.

## Alternatives Considered

The three decision surfaces' alternatives are in ADR-0090. At Spec level:
- **Blend only, no alpha test.** Would render all 163 foliage instances as
  sorted, depth-write-off blended surfaces: heavy mis-ordering in dense
  foliage, and no depth for later effects. Rejected: glTF distinguishes the
  modes for exactly this reason.
- **An implicit mode (cutoff > 0 or alpha < 1).** Rejected in
  Investigation 3.
- **New `MaterialKind`s for transparency** (the ADR-0081 shape). Rejected:
  transparency is orthogonal to the BRDF — a sheen material can be
  alpha-tested — so a kind per combination multiplies without end.

## Testing & Verification Plan

- **R1–R3 (GPU-independent):**
  - parse — present, absent, malformed and wrong-kind for both lines;
  - cook — cutoff range;
  - artifact v8 round-trip, and v7 rejected;
  - metadata round-trip and agreement.
- **R4–R5:** a GPU-independent unit test that each `alphaMode` maps to the
  expected Pipeline parameters. Pipeline creation with `AlphaBlend` passes
  Validation Layers (the new blend-state surface Spec 0036's test row names).
- **R6:** extend the 10/10 push-constant reflection cross-check to
  `alphaCutoff`'s offset (108 / 124), block sizes unchanged.
- **R7 (GPU-independent), the "real back-to-front ordering test" Spec 0036
  asks for** — unit tests of the ordering function:
  - blended items come after all others;
  - blended items are ordered farthest-first by the documented key;
  - ties and non-blended items keep caller order;
  - a blended item with no camera position trips the check.
- **R7 (GPU):**
  - a **transparency golden** — two overlapping translucent spheres of
    different colours in front of an opaque backdrop;
  - a test that the **same scene with the two spheres declared in the
    opposite order renders byte-identically**, which is the proof that the
    sort, not the caller's order, decides.
- **R6 (GPU):** a **cutout golden** — a quad with a small, new, committed
  alpha-pattern texture, alpha-tested at 0.5 over a backdrop, with sharp,
  depth-correct holes. No committed texture has real alpha today; every RGBA
  one is uniformly 255.
- **R8:** a test that a blended item does not appear in the shadow map.
- **Every existing golden byte-identical** (Debug and Release).
- **Validation Layers** zero warnings or errors (Debug, fatal).
- **Android** `assembleDebug`.

## Risks & Open Questions

- **O1 — importer mapping in this Spec?** **Recommend yes** for `MASK` and
  `BLEND`: it is small (the Spec 0041 R8 precedent), it replaces the current
  drop-and-report with a real destination, and it gives ⑦ real data. The
  alternative is leaving all importer work to ⑦.
- **O2 — `discard` in every PBR shader, or `MASK`-only shader variants?**
  A `discard` anywhere in a fragment shader can make some GPUs disable early
  depth testing for that Pipeline, even when it never fires, so every opaque
  PBR draw could lose early-Z. Variants avoid that but add ten shader pairs
  and a `hasAlphaTest` dimension to shader selection. **Recommend the single
  guarded `discard` now:** correctness first, no variant explosion. Measure
  in ⑦, where Bistro's opaque load is real, and split then if it costs.
- **O3 — Bistro's glass (transmission, 339 instances).** It is `OPAQUE` in
  glTF. Making it see-through means approximating transmission as `Blend`
  with alpha = 1 − `transmissionFactor` (0.05–0.34 in Bistro). That is an
  appearance decision, not a correctness one. **Recommend deciding it in ⑦**,
  with this Spec's blend path as its prerequisite, rather than baking an
  approximation into the importer now.
- **O4 — the sort point: node origin, or mesh bounds centre?** The origin
  costs nothing, but a mesh whose geometry sits away from its node origin
  sorts by the wrong point. A local-space AABB centre, computed once in
  `createMesh()` from the positions it already receives (no asset-format
  change), fixes that for 12 bytes per Mesh. **Recommend the bounds
  centre.**
- **O5 — decal z-fighting.** All three Bistro `BLEND` materials are decals.
  If their geometry is coplanar with the wall, `LESS` depth testing will
  make them flicker or vanish. This Spec does not measure the offset;
  **recommend** deciding depth bias (or `LESS_OR_EQUAL` for blended
  Pipelines) in ⑦ against the real mesh, since a guess now could hide
  genuinely offset decals behind the wall.
- **Risk — golden movement.** Treated as a stop-and-report gate. A moved
  golden would mean an `Opaque` material discarded a fragment, or opaque
  order changed.

## Out of Scope / Future Work

- **Transmission glass** (O3), and physical transmission and refraction
  (Spec 0036).
- **Cutout shadows:** a `shadow_cast` variant with a UV input, a texture
  binding and the same test.
- **Order-independent transparency**, if ⑦ finds per-object sorting
  inadequate.
- **Decal depth bias** (O5).
- **`MASK`-only shader variants**, if O2's measurement warrants them.
