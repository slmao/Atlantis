# ADR 0069: Environment Asset Preprocessing and Ownership

- **Status:** Proposed
- **Date:** 2026-09-02
- **Deciders:** slmao
- **Related Spec:** [specs/0025-image-based-lighting-foundation.md](../specs/0025-image-based-lighting-foundation.md) (`Draft`)

## Context

Image-based lighting needs HDR data that is expensive to derive: diffuse
irradiance, a roughness-prefiltered cubemap, and a DFG lookup. Atlantis Asset
System is explicitly CPU-only and may depend only on Core; Renderer is
stateless and must not own caches or GPU resources. Runtime is the existing
composition root that turns CPU asset data into GPU resources.

The existing texture artifact holds a single 8-bit 2D image and cannot express
an HDR cubemap, a mip chain, SH coefficients, or a DFG LUT. Creating these at
runtime would add first-frame work and a cache policy before an asset boundary
exists. Adding a new external preprocessing dependency would also require a
separate approved dependency decision.

## Decision

Introduce one versioned, content-addressed `.aenv` CPU artifact cooked offline
from a linear HDR equirectangular source using Atlantis tooling and existing
image-loading capability only. The artifact is self-contained: it stores nine
irradiance SH coefficients, an HDR prefiltered-specular cubemap and its mip
chain, and an `Rg16Float` DFG LUT.

Asset System owns parsing, validation, load errors, and the CPU-only
`EnvironmentAssetData` result. It must never include or name RHI, Renderer,
Vulkan, World, or Runtime types. Runtime optionally selects an environment in
its bootstrap configuration, realizes CPU data into GPU resources, owns those
resources, and releases the CPU payload after successful realization.

Environment association is Runtime bootstrap configuration for this first
consumer. `World` and Scene artifacts do not gain an environment field.

## Consequences

### Positive

- Expensive filtering is deterministic and outside frame time.
- Asset System's Core-only boundary remains intact and testable.
- One portable CPU artifact feeds windowed and headless Runtime paths.
- Runtime has one explicit owner for GPU lifetime and error classification.

### Negative / Trade-offs

- Each environment contains a copy of the otherwise shareable DFG LUT; this
  is accepted for the one-environment foundation and may be deduplicated only
  by a future approved asset-format revision.
- The first Runtime consumer cannot select an environment from a Scene asset.
- Cooker settings become versioned asset compatibility surface and need tests.

## Alternatives Considered

- **Runtime GPU prefiltering:** rejected; it expands frame scheduling and
  cache/lifetime policy and risks visible startup stalls.
- **Raw equirectangular upload only:** rejected; it does not provide
  roughness-aware specular lighting or deterministic diffuse irradiance.
- **RHI/GPU resource fields inside Asset System:** rejected by the established
  Asset System module boundary and ownership model.
- **A new third-party IBL processor:** rejected for now; no dependency has
  been reviewed or is needed for the initial cooker.
