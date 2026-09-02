# Spec: Image-Based Lighting Foundation

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-09-02
- **Related Plan(s):** None — draft only after this approval record merges.
- **Related ADR(s):** [ADR-0069](../adr/0069-environment-asset-preprocessing-and-ownership.md) (`Accepted`); [ADR-0070](../adr/0070-ibl-frame-binding-and-cubemap-resource-contract.md) (`Accepted`)
- **Human Review Approval (2026-09-02):** Approved by the human maintainer
  in the Codex task following review of this Spec and ADR-0069/ADR-0070.
  This approval accepts the IBL-first scope and both architectural decisions;
  it authorizes drafting Plan 0025 after this approval record merges, not
  implementation.

## Summary

Adds the first indirect-lighting layer needed to move Atlantis from its
direct-lighting PBR baseline toward Filament-class presentation: a
preprocessed HDR environment supplies diffuse irradiance, roughness-aware
specular reflections, and a split-sum DFG lookup. The environment remains a
CPU asset until Runtime realizes it into caller-owned RHI resources; Renderer
only borrows a frame-scoped environment binding.

## Motivation / Problem Statement

Spec 0023 implemented a metallic-roughness Cook-Torrance BRDF, and Spec 0024
now preserves its scene-referred output through an HDR intermediate and shared
output transform. The result still has no ambient diffuse illumination,
metallic reflection of the scene environment, or roughness-dependent
reflections: with direct lights disabled, every PBR surface is black.

That gap is the largest visual discontinuity from the core PBR quality stack
documented by Filament. Its material model combines linear HDR lighting with
image-based diffuse and specular lighting, prefiltered environment maps, and a
DFG lookup; this Spec adopts that narrow, well-understood portion without
claiming full Filament parity.

## Goals

- Add opt-in, HDR image-based diffuse and specular lighting to
  `MaterialKind::PbrDirectLit`; existing direct lights remain additive.
- Define a deterministic, versioned CPU environment asset containing the
  precomputed data required at runtime: third-order diffuse irradiance SH,
  a roughness-prefiltered specular cubemap, and a two-channel DFG LUT.
- Extend the RHI only as far as this feature requires: sampled 2D/cubemap
  images, mip levels, float formats, subresource upload, mip-aware sampling,
  and explicit sampled-texture binding slots.
- Keep Asset System CPU-only and keep the environment selection/realization in
  Runtime, preserving the existing module boundaries.
- Keep `Renderer` stateless and platform/Vulkan-free. A caller-owned
  environment binding is borrowed for one `drawFrame()` call only.
- Leave all existing no-environment scenes and goldens byte-identical; add a
  separately reviewed IBL material-grid golden proving the new path.

## Non-Goals

- Full Filament feature parity, a skybox/background pass, runtime environment
  capture/probes, multiple blended environments, or environment rotation.
- Shadows, ambient occlusion, normal mapping/tangent-space input, clear coat,
  transmission, anisotropy, multi-scattering compensation, physical camera,
  auto exposure, bloom, color grading, or HDR-display output.
- A general descriptor-set, bindless, texture streaming, compression, or
  asset-catalog system.
- Android implementation in this round; the RHI contract must remain portable
  to the Windows/Android Vulkan Phase 1 targets.
- Making `World` own an environment or changing the Scene artifact schema.

## Requirements

### Functional

- The environment cooker accepts a linear Radiance HDR equirectangular source
  using the repository's existing image-loading capability; it introduces no
  third-party dependency. It emits one little-endian, versioned `.aenv`
  artifact with a content-derived `AssetId`.
- An `.aenv` artifact contains exactly: nine RGB irradiance coefficients stored
  as padded `float4`s; six faces for every mip of one prefiltered HDR specular
  cubemap; and one `Rg16Float` DFG LUT. All values are finite linear radiance
  data. The decoder rejects unknown versions, invalid dimensions/mip chains,
  non-finite coefficients, malformed offsets, or a byte-size mismatch.
- Asset System exposes only CPU `EnvironmentAssetData`; it names no RHI,
  Renderer, Vulkan, or World type. Runtime alone converts that CPU data into
  RHI buffers/textures/samplers and owns their lifetime.
- Runtime's bootstrap configuration may select one optional environment asset.
  No selected environment means the exact pre-Spec-0025 indirect contribution
  of zero, so existing scenes remain unchanged by construction.
- `PbrDirectLit` evaluates diffuse irradiance from the nine coefficients at
  the normalized surface normal. It samples the prefiltered cubemap at
  `roughness * (mipCount - 1)` along the reflection vector, combines it with
  the DFG LUT sampled by `(NdotV, roughness)`, and adds those indirect terms to
  the existing direct-lighting result in scene-linear HDR before Spec 0024's
  existing output transform.
- The diffuse term is attenuated for metalness; the IBL Fresnel/diffuse split
  uses the same metallic base-color/F0 convention as Spec 0023. Exact shader
  equations and CPU reference cases are fixed by ADR-0070 and the eventual
  Plan, never left to implementation preference.
- The RHI sampled-image contract supports exactly the texture dimensions and
  formats this Spec needs: existing 2D `Rgba8Unorm`/`Rgba8Srgb`, HDR
  `Rgba16Float` cubemaps, and `Rg16Float` 2D LUTs; all existing single-mip 2D
  call sites preserve their behavior through defaults.
- Pipelines describe the required count and slots of sampled bindings. Command
  recording binds a texture/sampler at an explicit slot; the IBL PBR pipeline
  uses the existing base-color slot plus closed environment-cubemap and DFG
  slots. No arbitrary material-defined descriptor layout is introduced.
- Cubemap faces and all mips are uploaded before the resource first reaches
  `ShaderRead`; RenderGraph owns the whole-image state transition and all GPU
  work stays on the existing one-command-list/one-submit frame path.

### Non-functional

- **Performance:** preprocessing is offline. Per shaded PBR pixel adds one SH
  evaluation, one cubemap sample, and one DFG-LUT sample; no runtime filtering
  or probe capture is permitted.
- **Memory:** Runtime holds one decoded CPU asset only while creating the GPU
  resources, then releases it. GPU memory is one HDR prefiltered cubemap, one
  small DFG LUT, and the existing per-frame uniform buffer extension; no
  hidden renderer cache is allowed.
- **Portability:** creation checks the real Vulkan format/image-view/sampling
  capabilities needed for `Rgba16Float` cubemaps and `Rg16Float` LUTs and
  returns an explicit recoverable error if unavailable. No fallback quality
  tier is defined in this Spec.
- **Determinism:** cooker output is deterministic for identical source bytes
  and fixed cooker version/settings. Golden generation records source and
  artifact provenance under ADR-0042.

## Proposed Design

```
HDR equirectangular source
  -> offline Atlantis environment cooker
  -> .aenv { SH9 irradiance, prefiltered specular cube+mips, DFG LUT }
  -> Asset System decodes CPU EnvironmentAssetData
  -> Runtime realizes caller-owned RHI resources and frame uniform data
  -> Renderer borrows EnvironmentLighting for PbrDirectLit draws
  -> RenderGraph / RHI / Vulkan sample the environment in the HDR geometry pass
  -> existing Spec 0024 output-transform pass
```

The environment is frame-scoped rather than a `Material` property: many
materials share one lighting context, while a material retains only its own
base-color and metallic/roughness parameters. Runtime owns an
`EnvironmentLightingResources` aggregate containing the cubemap, LUT,
samplers, and frame-uniform payload; it passes a nullable borrowed view to
`Renderer::drawFrame()`. The aggregate is rebuilt with the same established
safe submission-drain discipline used for other Runtime GPU resources.

The shader uses a split-sum approximation. Diffuse irradiance comes from the
nine SH coefficients evaluated at `N`. Specular uses `R = reflect(-V, N)`, a
perceptual-roughness mip selection, and a DFG lookup. This is deliberately
offline-precomputed; no first-frame stalls, compute prefilter pass, or
Vulkan-specific path is introduced.

The first consumer is a new `ibl_material_demo` that renders the existing
dielectric/metallic × rough/smooth sphere grid with direct lights disabled.
It demonstrates the intended visual facts: non-black dielectric diffuse,
metallic colored reflections, and a broader/dimmer reflection as roughness
increases. A visible skybox is intentionally separate from lighting so this
Spec does not add a second render pass merely for presentation.

## Architectural Impact

**Yes.** Two tightly scoped ADRs are required before this Spec can be
approved:

- [ADR-0069](../adr/0069-environment-asset-preprocessing-and-ownership.md)
  defines the environment artifact and the Asset System → Runtime ownership
  boundary.
- [ADR-0070](../adr/0070-ibl-frame-binding-and-cubemap-resource-contract.md)
  defines the RHI sampled-image widening, closed binding contract, and
  frame-scoped Renderer input.

No new module, dependency, threading model, or backend is introduced.

## Alternatives Considered

- **Bloom/color grading first:** improves highlights but does not illuminate
  unlit PBR surfaces or create metallic reflections; it follows IBL.
- **Shadows first:** important for grounding, but they only affect direct
  lights; IBL fixes the larger all-black/no-reflection gap in the present PBR
  baseline.
- **Runtime GPU prefiltering:** rejected for this milestone because it adds
  compute scheduling, startup latency, and cache/lifetime policy before a
  stable asset contract exists.
- **Per-material environment maps:** rejected because environment lighting is
  scene/frame context, not material identity, and it duplicates GPU resources.
- **Irradiance cubemap rather than SH:** rejected for the first diffuse path;
  SH is compact, naturally frame-uniform data, and matches the established
  Filament-style representation. A future spec may revisit quality limits.
- **A generic arbitrary descriptor-layout API:** rejected; the known closed
  PBR contract needs three sampled bindings, not a new general abstraction.

## Testing & Verification Plan

- GPU-independent tests cover `.aenv` round-trips, corruption/error cases,
  finite-value validation, content identity, SH basis/reference evaluations,
  roughness-to-mip mapping, DFG reference values, and the explicit sampled
  binding-slot contract.
- Shader reflection tests prove the C++ frame-uniform layout and the closed
  PBR descriptor bindings match the compiled Slang shader. Negative mutation
  probes prove omitted enum/switch cases fail under `/w14062 /WX`.
- GPU tests create/upload a cube with all faces and mips, exercise 2D and cube
  sampling, cover no-environment and enabled-environment paths, and assert
  roughness/reflection and direct-light additivity behavior under both Debug
  and Release with Vulkan Validation Layers clean.
- Image regression keeps every existing no-environment golden byte-identical.
  The new IBL fixture and its golden land in separate commits; the golden is
  human-reviewed under ADR-0042 and records its HDR source/artifact provenance.
- Full `ctest -LE gpu`, `ctest -L gpu`, and an `ATLANTIS_BUILD_TESTS=OFF`
  Runtime build run in both configurations. Module/link checks prove Asset
  System still links Core only and Renderer still links only Core/RHI/
  RenderGraph.

## Risks & Open Questions

- The exact cubemap face size, mip count, DFG resolution, integration sample
  count, and accepted numerical tolerances need measured Plan-stage values;
  they must be fixed there rather than chosen opportunistically in code.
- The repository's current descriptor-pool sizing assumes at most one sampled
  binding per pipeline. The Plan must re-derive pool descriptor counts and the
  Spec 0021 growth proof for the closed three-sampler PBR layout.
- HDR source licensing/provenance must be documented before adding a visible
  demo environment. A procedurally generated source is preferred if a suitable
  redistributable HDR source is not approved.

## Out of Scope / Future Work

This foundation unblocks, but does not authorize, a skybox pass, environment
rotation/blending, scene-owned environment references, reflection probes,
shadow maps, tangent-space normal maps/AO, physical camera and exposure,
filmic tone mapping/color grading/bloom, and higher-order/multi-scattering
IBL quality work. Together these remain the staged route toward the user's
Filament-quality target, not a claim that one Spec reaches it.
