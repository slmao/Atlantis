# ADR 0069: Environment Asset Preprocessing and Ownership

- **Status:** Accepted
- **Date:** 2026-09-02
- **Deciders:** slmao — Human Review, approved 2026-09-02
- **Related Spec:** [specs/0025-image-based-lighting-foundation.md](../specs/0025-image-based-lighting-foundation.md) (`Approved`)
- **Acceptance Record (2026-09-02):** Accepted by Human Review as part of
  Spec 0025's approval in the Codex task. This record authorizes Plan drafting
  only after the approval record merges; it does not authorize implementation.

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

## Accepted Amendment — 2026-09-03 (`AssetId` derivation correction)

**Status of this section:** Accepted by Human Review through the maintainer's
merge of [PR #117](https://github.com/slmao/Atlantis/pull/117), confirmed in
the Codex task on 2026-09-03. The ADR's top-level status remains `Accepted`,
and the original Decision text above is preserved as the historical record.
This amendment supersedes only the word “content-addressed” in that Decision
and authorizes Plan 0025 drafting, not implementation.

### Context

Plan 0025 preflight found that “content-addressed” conflicts with the existing,
Accepted [ADR-0044](0044-asset-system-identity-provenance-and-import-methodology.md):
Atlantis `AssetId` is path-derived through `normalizeLogicalPath()` plus
`computeAssetId()` (64-bit FNV-1a), consistently across current asset types.
No content-hash identity mechanism exists, and this IBL feature has no reviewed
reason to create a second one.

### Amended Decision

The `.aenv` artifact is versioned and deterministically cooked, but its
`AssetId` is derived from its normalized logical path using the existing
ADR-0044 mechanism. If the artifact embeds an `AssetId`, Asset System compares
it with the independently parsed metadata sidecar and with a fresh
`computeAssetId(metadata.sourceLogicalPath)` result before returning
`EnvironmentAssetData`.

Content determinism and asset identity remain separate contracts: identical
source bytes/settings produce identical cooked payload bytes apart from any
path-derived identity/provenance fields, while distinct logical paths produce
distinct `AssetId` values even when source bytes match.

### Consequences

- Environment assets share the same identity/collision behavior as mesh,
  texture, and material assets.
- No new persistent identity, catalog, or content-addressable storage design is
  introduced.
- A byte-for-byte deterministic-payload test must compare inputs with the same
  normalized logical path; a separate test proves different logical paths
  produce different embedded IDs.
