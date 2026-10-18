# ADR 0122: Clustered Light Association

- **Status:** Accepted
- **Date:** 2026-10-19 (accepted 2026-10-19)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-19 (owner rulings on Spec 0061 Revision 1 Q6–Q11, recorded once in
  [Plan 0061 Stage 2](../plans/0061-stage2-clustered-lights.md); the ADR text was reviewed in
  [PR #264](https://github.com/slmao/Atlantis/pull/264), merged).
- **Related Spec:** [Spec 0061](../specs/0061-point-light-cost.md), Revision 1 (Stage 2; `Approved` 2026-10-19)
- **Related ADR(s):**
  - [ADR-0121](0121-point-light-association-model.md): the point-light association model. **Decision 3 ("no
    precomputed association structure is adopted") is superseded here**: its first reopening condition is met by
    measurement, and this ADR adopts one structure. D1 (exact range rejection on the shader's own attenuation),
    D2 (the equivalence invariant, binding on any structure) and D4 (dependence on a finite-range falloff) stand,
    and this ADR is built on them.
  - [ADR-0088](0088-frame-lighting-data-successor-structure-and-binding-strategy.md): `FrameLightingData`'s
    structure and binding.
    - **D3** ("one uniform buffer, unchanged … the whole frame uniform is 2,512 bytes") stands in kind: still one
      uniform buffer at binding 0. Its sizing statement is superseded: the block grows to at most 16,384 bytes.
    - **D4** (one capacity constant) gains a coupling: `kMaxPointLights` is also the cluster-mask width, so raising
      it past 64 reopens this ADR.
    - **D5**, already superseded in part by ADR-0121, is superseded further: the loop is no longer bounded by
      `pointLightCount` alone.
    - D1, D2 and D6 stand.
  - [ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md) (stage visibility),
    [ADR-0004](0004-phase1-threading-baseline.md) (single frame thread),
    [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)/[ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
    (the RHI binding surface): unchanged.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

Stage 1 (ADR-0121 D1, merged in [PR #263](https://github.com/slmao/Atlantis/pull/263)) removed 20.0 % of Bistro's
point-light cost. The cost `L` went from 10.19 to 8.15 ms of `passes.draw.gpu_ms` at 1920×1080 on the reference
machine, leaving 80 %, far over Spec 0061 R7's 25 % trigger. Spec 0061's Phase 1 showed why:

- A probe that keeps every per-light load and the preamble, but evaluates no BRDF, still costs 81–86 % of `L`. The
  cost is the per-light loop itself, and the only lever left is iterating fewer lights per fragment.
- `L` grows sub-linearly with fragment count (×3.3 fragments gives ×1.46 `L`), consistent with helper-lane
  overshading on micro-triangles. Lane work, not counted fragments, is what a structure must reduce.
- A CPU analysis of the benchmark frame found that a clustered (froxel) association leaves about 0.8 lights per
  cluster at 16×9×24, and 1.3–2.0 at grids that fit the guaranteed uniform budget. Per-draw lists leave the largest
  Bistro meshes (77–101 m across) 27–34 lights each.

Forces, stated honestly:

- **The RHI has no compute dispatch,** so the association must be built on the CPU.
- **The RHI exposes exactly one uniform buffer per pipeline:** `CommandList::bindUniformBuffer(Buffer&)` takes no
  binding index, and its content binds at binding 0. `BufferPurpose` has no storage buffer. Indexed bindings exist
  only for sampled textures.
  - A second buffer binding therefore means changing the RHI surface: an indexed uniform or storage binding, a new
    `BufferPurpose`, Vulkan descriptor-layout changes.
  - A texture-encoded list needs an integer texture format the RHI does not offer, plus a per-frame staging copy
    scheduled through the RenderGraph.
- **The Vulkan-guaranteed `maxUniformBufferRange` is 16,384 bytes.** Spec 0040 holds every design to the guarantee,
  not the desktop value, because Android is a primary target. The camera block is 2,544 bytes today.
- **Visual equivalence is the owner's fixed requirement,** and ADR-0121 D2 already states the invariant any
  structure must satisfy: a conservative superset, ascending order, the per-light expression unchanged.
- **The frame thread is the only thread** (ADR-0004). No job system.
- **`uniform_write_bytes` is a gated counter at tolerance 0.** Any per-frame upload of association data changes
  it, so the Performance Contract's category-1 refresh is part of this decision's cost.

## Decision

1. **Structure: a CPU-built clustered light mask, one 64-bit mask per cluster.**
   - The view volume is divided into `Cx × Cy × Cz` clusters: `Cx × Cy` regions of clip-space xy, and `Cz` depth
     slices (a near slice from the near plane to `z1`, then exponential slices from `z1` to the camera's far plane).
   - Bit `j` of a cluster's mask is set when point light `j` may reach the cluster.
   - The masks are rebuilt every frame on the frame thread, in Runtime's extraction adapter beside
     `extractFrameLightingData()`, from the same `FrameLightingData` and camera matrices the frame uploads.
   - A mask iterated low bit first is ascending by construction.
   - The mask width equals `kMaxPointLights` (64). The grid dimensions are constants fixed by the Plan within the
     budget of Decision 2.

2. **Carrier: an appended region of the existing camera uniform block.** The region follows `FogData`, at offset
   2,544. It holds a small header (the grid dimensions and the depth-slicing parameters), then the masks packed two
   per `uint4` (std140 gives a `uint2` array element a 16-byte stride, so pairs halve the cost).
   - The whole block, header included, stays **≤ 16,384 bytes**, the Vulkan guarantee. At most 1,726 clusters fit.
   - There is **no new binding, no RHI change and no new RenderGraph pass.** The region is written through the same
     mapped pointer, at the same point in the frame, as every other region of the block, under the safety argument
     the block already relies on: `acquireNextTarget()` drains the retained submission before any mapped write
     (Spec 0022; Spec 0040 Investigation 4).
   - Every writer of the camera block fills the region, the image-regression fixtures included. **A mask with bits
     0 … `pointLightCount`−1 set (all lights) is always a valid fill:** it is the trivially conservative association.
   - The ten PBR shaders declare the extended block. `lit_textured` keeps its prefix (as today), and Plan 0040's
     11/11 reflection cross-check extends to the new region.

3. **Fragment-side association.** The four looping PBR shaders find their cluster from `input.worldPosition`
   through the frame's own `camera.view` and `camera.projection`. They use the clip-space xy and the view depth of
   the same position the light computation uses, which makes the mapping independent of the render target's extent
   (swapchain, capture, editor viewport). Indices are clamped into the grid.
   - The point-light loop iterates the set bits of that cluster's mask in ascending order.
   - The body is Stage 1's, unchanged, **including the `NdotL > 0 && atten > 0` predicate** (ADR-0121 D1).
   - No wave or subgroup intrinsics are used.

4. **Conservativeness is a requirement on the CPU test, with explicit margins.**
   - A light is put in a cluster when its sphere, with radius inflated to `range · (1 + 10⁻³) + 0.01 m`, intersects
     the cluster's world-space bounds.
   - The near slice starts at the camera's near plane and the last slice ends at its far plane, so every fragment
     that survives clipping lies inside some cluster's bounds up to rounding.
   - The margin absorbs the device's division and square-root precision in `atten`, and the rounding between the
     interpolated `worldPosition` and its re-projection. An over-included light costs one loop iteration and adds
     an exact `+0`; it never changes a result.
   - With this, every light the fragment would evaluate (`atten > 0`) is in its cluster's mask. **ADR-0121 D2's
     invariant holds, and Stage 1's bit-exactness argument carries over unchanged** (Spec 0061 Revision 1,
     equivalence analysis).

5. **Scope boundaries.**
   - This is a light-association structure only: no geometry culling, no GPU-driven work, no job system.
   - The Renderer's public API, `DrawItem`, `Mesh`, push constants, the RHI and the RenderGraph are unchanged. The
     Runtime builds the data and writes it into the buffer the Renderer already binds.

## Consequences

### Positive

- **Lights iterated per lane fall from 59 to about 1.3–2.0 on average** (indicative, benchmark camera), attacking the
  measured cost directly.
- **No RHI, binding, descriptor or RenderGraph change.** `descriptor_writes`, `descriptor_set_binds` and every other
  binding counter stay identical; only `uniform_write_bytes` grows, by exactly the region's size.
- **Bit-exactness is inherited from Stage 1** under the stated conditions. Goldens are expected byte-identical, and
  an all-lights mask is a built-in control that separates an association bug from code-generation drift.
- **The mapping is extent-independent,** so captures, the editor viewport and headless targets need nothing
  special.
- **CPU cost is small and bounded:** 59 lights against the clusters each touches, plus a fixed-size clear, on the
  frame thread.

### Negative / Trade-offs

- **The guaranteed uniform budget caps the grid** at about 1,700 clusters with 64-bit masks. Finer grids, or more
  than 64 lights, need a storage-buffer binding: an RHI change and a new ADR.
- **`kMaxPointLights` is now also the mask width.** Raising it reopens this ADR (ADR-0088 D4's single constant gains
  a second meaning).
- **Lanes in one wave may sit in different clusters.** The loop then runs to the longest mask in the wave, with
  lane-divergent indices into the light array. This is measured, not assumed (Spec 0061 Revision 1's decision
  table).
- **Every camera-block writer must fill the region,** which adds maintenance in the fixtures and runtime tests,
  mitigated by the all-lights fill and the reflection cross-check.
- **The per-frame upload grows** from 2,672 to roughly 13–16 KB, a deliberate counter change that needs the
  owner-instructed category-1 baseline refresh. The implementation PR's gate verdict is therefore a pre-declared
  `fail` on `uniform_write_bytes` alone.
- **The fragment shader gains a cluster lookup** (a matrix multiply, a `log2`, a clamp, one 8-byte load), paid
  even by fragments in empty clusters.

## Alternatives Considered

- **A storage buffer (new `BufferPurpose::Storage` and an indexed storage binding).** It allows variable-length
  index lists and grids beyond 16 KB. Deferred: it is an RHI surface change (a new purpose, an indexed binding,
  Vulkan descriptor-layout changes, an ADR against ADR-0020/0025) that buys nothing the guaranteed uniform budget
  cannot hold at 64 lights. It is the path if `kMaxPointLights` or the grid must grow.
- **A second uniform buffer at a new binding index.** It needs an indexed `bindUniformBuffer` (an RHI change) and a
  new descriptor, to carry what fits in the existing block. Rejected.
- **A texture-encoded list (masks in an integer texture).** It needs an integer format the RHI does not offer, plus a
  per-frame staging copy and a barrier scheduled in the RenderGraph. Rejected as more machinery than the block.
- **Index lists (offset/count plus a flat index array) instead of masks.** More compact for very sparse grids and
  not bounded by 64. But they are variable-sized, the CPU must write them ascending, and the worst case does not fit
  a fixed uniform region. Rejected at 64 lights.
- **Per-draw masks.** Rejected on Phase 1's I-5: the largest meshes keep 27–34 of 59 lights, and they cover most
  pixels. The design would also need a `DrawItem` field (Spec 0023 D10) and `Mesh` extents.
- **A compute-built tiled or clustered list.** No compute dispatch exists. Rejected, as in ADR-0121.
- **Wave-uniform iteration** (iterating the wave-wide OR of the lanes' masks with subgroup operations). The OR is
  still a conservative superset, so equivalence would hold, and it removes lane-divergent indexing. But it needs
  subgroup arithmetic, which is not universal on Android. Excluded now, reopened only if divergence is measured to
  cost more than the decision table allows, and then with a capability gate in its own ADR.
