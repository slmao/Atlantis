# ADR 0070: IBL Frame Binding and Cubemap Resource Contract

- **Status:** Proposed
- **Date:** 2026-09-02
- **Deciders:** slmao
- **Related Spec:** [specs/0025-image-based-lighting-foundation.md](../specs/0025-image-based-lighting-foundation.md) (`Draft`)

## Context

The current RHI `SampledTexture` is deliberately a single-mip 2D
`Rgba8Unorm`/`Rgba8Srgb` image. `CommandList::bindTexture()` always writes one
combined sampler at a fixed binding, and `PipelineCreateParams` only models a
boolean sampled-texture presence. That was sufficient for one base-color
texture, but IBL needs an HDR cubemap with mip levels, a 2D DFG LUT, and
separate bindings alongside that existing base-color texture.

The environment is shared by all PBR draws in one frame. Making it a Material
field would duplicate borrowed references and blur lighting context with
material identity. Renderer must remain a stateless RHI/RenderGraph consumer,
never a Vulkan-aware owner.

## Decision

Widen the backend-agnostic RHI sampled-image contract only for the closed IBL
need: 2D and cube dimensions, an explicit mip count, `Rgba16Float` and
`Rg16Float` sampled formats, whole-image state transitions, subresource upload
for every face/mip, and mip-aware sampler creation. Existing single-mip 2D
creation defaults retain their exact current behavior.

Replace the fixed implicit sampled binding with explicit, closed binding slots.
Pipelines state their required sampled-binding count/slots; command recording
binds a texture and sampler at a named slot. The PBR IBL contract is fixed to
binding 1 base color, binding 2 prefiltered cubemap, and binding 3 DFG LUT,
with frame uniform data at binding 0. This is not an arbitrary descriptor
layout API, bindless system, or material-defined binding scheme.

Runtime owns an `EnvironmentLightingResources` aggregate and passes a nullable
borrowed `EnvironmentLighting` frame input to `Renderer::drawFrame()`. Renderer
binds it only for `PbrDirectLit`; direct-light and no-environment behavior is
otherwise unchanged. The shader adds SH diffuse plus split-sum specular before
the existing HDR output-transform path.

## Consequences

### Positive

- Cubemap/mip capability is available through a backend-neutral RHI contract.
- Environment data has one frame-scoped owner and is not duplicated per
  Material.
- The descriptor change is explicit, reflection-verifiable, and limited to the
  known three-sampler PBR shape.
- All IBL GPU work remains inside Renderer → RenderGraph → RHI → Vulkan.

### Negative / Trade-offs

- The public RHI, renderer call surface, shader contract, reflection validator,
  descriptor-pool sizing, and Vulkan implementation all change together.
- A PBR pipeline now requires more combined-image-sampler descriptors than the
  existing one-sampler capacity proof; the Plan must re-derive it.
- Frame uniform data grows to carry SH coefficients and must be reflected and
  layout-tested exactly.

## Alternatives Considered

- **Keep a fixed one-texture binding and pack IBL into the base-color texture:**
  rejected; dimensions, color spaces, and sampling semantics differ.
- **Per-Material environment pointers:** rejected; it makes shared lighting
  state look like material ownership and duplicates lifetime obligations.
- **An arbitrary vector/map of descriptors:** rejected as speculative; the
  feature has one known, closed layout.
- **Separate direct Vulkan binding path:** rejected by the mandatory RenderGraph
  and RHI boundaries.
