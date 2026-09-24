# ADR 0092: Bloom — Pass Insertion, Blur Strategy, Intermediate Targets and Parameter Source

- **Status:** Proposed
- **Date:** 2026-09-24
- **Deciders:** slmao
- **Acceptance:** pending
- **Related Spec:** [Spec 0044](../specs/0044-bloom.md)

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

[Spec 0036](../specs/0036-bistro-parity-roadmap.md) workflow ⑥ owes an
ADR for where bloom's pass(es) insert into
[ADR-0068](0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)'s
two-pass structure, the intermediate HDR targets, and the blur strategy
(`0036:582-587`, `:817`). The parameter source is decided with them, as
ADR-0091 did for fog. Facts, measured by Spec 0044's investigations:

- **Today's frame is shadow → draw → output transform**, one
  `RenderGraphBuilder` in `drawFrame()` (`renderer.cpp:94-118,295-340`).
  The draw pass writes the `Rgba16Float` `HdrColorTarget`; the output
  transform samples it through one sampler, the whole of ADR-0068 D-10's
  descriptor contract. Output-transform Pipelines are created in 22
  files, and `drawFrame()` is called from 38 sites in 24 files.
- **Fog is already in that HDR output** (ADR-0091 Decision 1); Spec 0043
  requires ⑥ to read it after the draw pass and not move fog.
- **The `HdrColorTarget` is single-mip** (`vulkan_device.cpp:1545`), and
  the RHI has no per-mip view.
- **The RenderGraph can already run a bloom chain:**
  - `ShaderRead → ShaderRead` records no barrier (`execution.cpp:128-146`),
    so two passes can read `hdr_color`;
  - each resource starts `Undefined` every frame, and the transition table
    has `Undefined → ColorAttachmentOutput → ShaderRead` but no
    `ShaderRead → ColorAttachmentOutput`
    (`resource_state_mapping.cpp:195-217`) — a target cannot be written
    again once read;
  - viewports follow each attachment's extent;
  - Pipelines can drop the camera uniform, take up to five samplers from
    binding 0, and have no depth attachment.
- **The RHI has no additive blend** (`ColorBlendMode` is `Disabled` or
  `AlphaBlend`) and no compute path.
- **Bistro's bloom sources** are HDR emissive at 8–100 (Spec 0041) and HDR
  fog (Spec 0043 ruling Q7). Its lamps want a tight core and a broad haze.

## Decision

1. **Insertion — separate RenderGraph passes between the draw pass and the
   output transform, ending in a separate composite pass.**
   - The chain's first pass and the composite read `hdr_color` (after the
     draw pass, so fogged); nothing writes `hdr_color` but the draw pass.
   - The composite writes a new full-size `Rgba16Float` target; when bloom
     is on, the output transform reads that target instead of `hdr_color`.
   - The output-transform shaders, Pipelines and ADR-0068 D-10's
     one-sampler contract are **unchanged**.
   - **Bloom off declares no bloom pass**: the graph is exactly today's.
2. **Strategy — a six-level downsample/upsample chain from half
   resolution.**
   - `D1` (½ extent) reads `hdr_color` and applies the bright-pass:
     `max(c − threshold, 0)` then `· 1 / (1 + max3 / 1000)` (Filament's
     formula, knee parameterised). `D2…D6` each halve the previous level
     (each dimension ≥ 1).
   - `U_i = D_i + upsample(U_{i+1})` for `i = 5…1`, with `U_6 ≡ D6`.
   - Composite: `rgb = hdr + strength · upsample(U1) / 6`; alpha is
     `hdr`'s.
   - Level count and start resolution are fixed Renderer constants, not
     authored. Filter kernels are the Plan's.
3. **Targets and ownership — twelve `HdrColorTarget`s in one caller-owned
   Renderer bundle, and one new optional `drawFrame()` parameter.**
   - `D1…D6`, `U1…U5` and the composite: each written by exactly one pass
     then read, so every transition already exists and the single-producer
     rule holds.
   - A Renderer RAII type owns them (and a linear clamp sampler). The
     caller creates it from a `Device` and the HDR extent, and recreates
     it where the `HdrColorTarget` is recreated. The Renderer stays a
     stateless orchestrator (ADR-0068 D-1).
   - `drawFrame()` gains one trailing, defaulted parameter: the bundle, the
     bloom Pipelines and `{strength, threshold}`. Absent (or
     `strength == 0`) means off. No existing call site changes.
   - Three new shader pairs (downsample, upsample-add, composite) with new
     Shader System descriptor contracts: one or two samplers, no uniform
     buffer, a small push-constant block.
   - **No RHI, Vulkan Backend or RenderGraph change.**
4. **Parameters — from the scene file, on the camera node.**
   - Optional `strength` and `threshold` tokens after the fog group (scene
     source/artifact v5 → v6), carried as plain data on World's `Camera`,
     as ADR-0091 carried fog. The Runtime passes the active camera's values
     each frame.
   - Absent means off. Ranges: `strength` in `[0, 1]`, `threshold` finite
     and `≥ 0`, checked at cook and decode.
   - The threshold is scene-referred: the output transform's exposure
     multiply applies after the composite.

## Consequences

### Positive

- **The smallest surface that gives a wide halo.** New passes, targets,
  Pipelines and shaders only; ADR-0068's D-1/D-3/D-10 and the RHI stand
  as accepted.
- **Every existing golden stays byte-identical**, by construction: off
  declares nothing. On with nothing above the knee is also exact
  (`hdr + 0`).
- **Fog → bloom order holds structurally**: the chain can only read
  `hdr_color` after its producer, the draw pass.
- **Multi-scale look at bounded cost**: about 1.38 M pixels shaded in the
  chain at 1080p, halo size a fixed fraction of the screen at any
  resolution.
- **Per-scene control** that follows ⑤'s precedent and Filament's
  per-view `BloomOptions`.

### Negative / Trade-offs

- **Memory:** ≈ 1.67× the HDR target when on (≈ 27.6 MB at 1080p), 16.6 MB
  of it the full-size composite that folding into the output transform
  would save.
- **Twelve targets to manage** where additive blending onto one chain
  would need six.
- **Another scene-format bump** (v5 → v6), and a longer `drawFrame()`
  signature (defaulted).
- **Fixed six levels and half-resolution start.** A scene cannot trade
  radius for cost without a later Spec.
- **Threshold before exposure** means a scene that darkens its exposure
  keeps blooming what is bright in scene units.

## Alternatives Considered

- **Composite inside the output-transform shader** (Filament's shape).
  Saves the composite target and one full-size pass. It changes the
  `Accepted` D-10 contract, so it needs a superseding ADR. It also touches
  22 Pipeline sites, and needs a bound bloom texture (a dummy when off) at
  38 call sites. Rejected.
- **Upsample by additive blending onto the down chain.** Six targets
  instead of eleven, but it needs `ColorBlendMode::Additive` and a
  `ShaderRead → ColorAttachmentOutput` transition in the RHI and Backend.
  Rejected.
- **One image with a real mip chain.** Needs per-mip views and per-mip
  attachments in the RHI. Rejected.
- **Separable Gaussian at fixed quarter resolution.** One scale only; a
  Bistro-sized haze needs ~5 iterations or a 65-tap kernel. Rejected.
- **A single large-kernel pass.** O(R²) taps in a fragment shader, or a
  compute path the RHI lacks. Rejected.
- **Parameters from `BootstrapConfig`.** Global to the executable: every
  scene would get Bistro's bloom. Rejected.
- **Bloom on by default.** Would move every golden and every scene's look.
  Rejected; absent means off.
