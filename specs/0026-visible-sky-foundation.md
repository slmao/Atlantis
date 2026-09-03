# Spec: Visible Sky Foundation

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-09-03
- **Related Plan(s):** None yet — drafted after this Spec and ADR-0071 are
  approved.
- **Related ADR(s):** [ADR-0071](../adr/0071-visible-sky-background-rendering-integration.md) (`Accepted`)
- **Human Review Approval (2026-09-04):** Approved by the repository
  maintainer against [PR #120](https://github.com/slmao/Atlantis/pull/120).
  Accepts this Spec's design and ADR-0071 as written, and accepts the four
  items in Risks & Open Questions as deferred to Plan/Implementation, not
  as blockers. This approval authorizes drafting Plan 0026, not
  implementation.

## Summary

When Runtime has an environment configured (Spec 0025), the scene
background is currently a flat clear color even though a real HDR
environment cubemap is already loaded and bound for lighting. This Spec
makes that same, already-realized cubemap visible as a sky background:
drawn behind all scene geometry, tracking camera rotation but not camera
translation, reusing Spec 0025's environment asset and Spec 0024's HDR
intermediate/output-transform pipeline unchanged.

## Motivation / Problem Statement

`ibl_material_demo` (Spec 0025) lights its spheres from a real studio
environment, but the space around them is a solid color — visually
inconsistent with lighting that is clearly coming from somewhere. A
visible sky is the smallest step that closes this gap without touching
lighting, shadows, or any other presentation feature: the environment
data, HDR intermediate, and output transform all already exist.

## Goals

- Render the environment's prefiltered cubemap (its own un-blurred mip 0,
  Plan 0025 P1) as the scene background whenever Runtime has an
  environment configured — reusing the same asset, RHI resources, and
  `EnvironmentLighting` view Spec 0025 already realizes.
- Draw entirely within the existing HDR geometry pass and the existing
  output-transform pass (Spec 0024) — no new RenderGraph pass, no new
  GPU resource beyond one new Pipeline/shader.
- The sky tracks camera orientation (rotation, field of view) but is
  invariant to camera translation, so it reads as infinitely distant.
- Scene geometry always occludes the sky correctly; the sky never draws
  over an opaque `DrawItem`'s pixels.
- Leave every no-environment scene and its golden byte-identical. The
  existing `ibl_material_demo` golden is expected to need a re-capture
  (its background changes), disclosed here, not treated as a surprise
  regression later.

## Non-Goals

- Shadows, ambient occlusion, or any lighting change — this Spec only
  changes what is visible where nothing else drew.
- Environment rotation, blending multiple environments, reflection
  probes, or runtime environment capture.
- Exposure control, bloom, color grading, or any change to Spec 0024's
  tone-mapping/output-transform math.
- Multi-instance/multiple-environment scenes, or any `World`/`Scene`
  schema change — sky enablement stays a Runtime `BootstrapConfig`
  concept, exactly like environment selection already is.
- A new third-party dependency of any kind.
- Android implementation in this round — the design must stay portable to
  Windows/Android Vulkan (Phase 1), matching Spec 0025's own scoping.

## Requirements

### Functional

- When `BootstrapConfig` names a valid environment, Runtime creates one
  additional sky `Pipeline`/shader once, at the same point it already
  creates `EnvironmentLightingResources` (Spec 0025 M7) — no new
  BootstrapConfig field beyond what environment selection already
  provides.
- `Renderer::drawFrame()` draws the sky, when given a non-null sky
  `Pipeline`, before any `DrawItem` in the same HDR geometry pass. The
  sky never depth-writes; every `DrawItem`'s own existing depth test/write
  behavior is completely unchanged, and an opaque `DrawItem` always wins
  wherever it covers a pixel.
- The sky shader samples `EnvironmentLighting::prefilteredEnvironment` at
  an explicit mip level 0 (the directly-resampled, un-blurred base level)
  along a per-pixel ray reconstructed from the existing camera
  view/projection matrices' rotation-only component — never the camera's
  world position, and never a new camera-buffer field.
- The sky writes into the existing `HdrColorTarget` in scene-linear HDR,
  exactly like every geometry `Pipeline` (ADR-0068 D-4's fixed
  `HdrFormat::Rgba16Float`); it is tone-mapped and encoded by the
  existing, unmodified output-transform pass, never its own transfer
  function.
- No environment configured means no sky `Pipeline` is created and
  `Renderer::drawFrame()` receives `nullptr` for it — the existing flat
  `kBackgroundClearColor` background is exactly reproduced, unconditionally.

### Non-functional

- **Performance:** one additional fullscreen draw per frame, only when an
  environment is configured; no additional GPU resource creation per
  frame, no runtime filtering.
- **Memory:** zero new persistent GPU memory — the sky Pipeline is the
  only new object, and it reuses every existing buffer/texture/sampler
  Spec 0024/0025 already created.
- **Portability:** the sky Pipeline uses only the existing, already-checked
  `HdrFormat::Rgba16Float` and cubemap sampling capabilities (ADR-0068 D-2,
  ADR-0070 P2) — no new format or capability check is introduced.
- **Determinism:** rendering is a pure function of the camera and the
  already-deterministic environment asset (Spec 0025); repeated frames
  with an unchanged camera produce identical sky pixels.

## Proposed Design

```
existing EnvironmentLightingResources (Spec 0025, unchanged)
  -> Runtime creates one sky Pipeline/shader when an environment exists
  -> Renderer::drawFrame() draws sky first, inside the existing "draw" pass,
     depth-test-on/depth-write-off, before every DrawItem
  -> opaque DrawItems draw normally, occluding the sky wherever they cover a pixel
  -> existing Spec 0024 output-transform pass (unchanged)
```

See [ADR-0071](../adr/0071-visible-sky-background-rendering-integration.md)
for the exact mechanism: the one new, additive `PipelineCreateParams`
field (`depthWriteEnabled`), the sky Pipeline's own closed descriptor
contract (frame uniform at binding 0, environment cubemap at binding 1 —
ADR-0070's existing `sampledTextureBindingCount = 1` shape), and why sky
draws inside the existing pass rather than a new one.

The sky's own vertex shader is the same fixed-fullscreen-triangle trick
the output-transform pass already uses (Plan 0024 M5's checked-in 3-vertex
buffer, reused unchanged — no new geometry asset). Its fragment shader
reconstructs a per-pixel world-space ray from screen position and the
camera's existing view/projection matrices with translation dropped
(rotation-only), samples the cubemap at that ray with an explicit mip-0
`SampleLevel`, and writes the result as scene-linear HDR color with a
fixed device-space depth just under the far plane.

`Renderer::drawFrame()` gains one new caller-owned, nullable parameter —
`const atlantis::rhi::Pipeline* skyPipeline = nullptr` — following the
same borrowed/nullable pattern `environmentLighting` already established
(ADR-0070). Sky draws only when both are non-null; a non-null
`skyPipeline` with a null `environmentLighting` is a programmer error
(`ATLANTIS_CHECK_MSG`), mirroring the existing IBL-material guard.

## Architectural Impact

**Yes.** [ADR-0071](../adr/0071-visible-sky-background-rendering-integration.md)
records:

- One new, additive `PipelineCreateParams` field (`depthWriteEnabled`),
  decoupled from the existing `hasDepthAttachment`.
- A new `Renderer::drawFrame()` parameter (`skyPipeline`), the third
  disclosed change to this signature since ADR-0022.
- A new closed sky shader/Pipeline descriptor contract, reusing the
  existing 0/1/3 `sampledTextureBindingCount` shape (ADR-0070 P2) — no
  new descriptor-contract mechanism.

No new module, RenderGraph pass, GPU resource type, dependency, or
threading model is introduced; no `World`/`Scene` schema change.

## Alternatives Considered

- **A separate sky RenderGraph pass before "draw."** Rejected in
  ADR-0071 — it would require changing the "draw" pass's own
  color-attachment load op from clear to load, a larger RenderGraph/
  `ResourceBinding` change than integrating sky into the existing pass.
- **Post-process the sky into the output-transform pass instead of the
  HDR geometry pass.** Rejected — the output-transform pass samples only
  the already-composited `HdrColorTarget`; it has no per-pixel camera-ray
  information and would need its own camera uniform binding, duplicating
  what the geometry pass already has.
- **Skip this Spec and let `ibl_material_demo` keep a flat background
  indefinitely.** Rejected per explicit human direction — a visible sky is
  the next near-term milestone ahead of shadows/glTF/multi-instance scope.

## Testing & Verification Plan

- GPU-independent: the new `depthWriteEnabled` field's default reproduces
  every existing Pipeline's current behavior exactly (a compatibility
  test mirroring `hasCameraUniformBinding`/`hasDepthAttachment`'s own
  Plan 0024 precedent); the sky descriptor contract is reflection-verified
  against the compiled shader, matching every existing `*ExpectedDescriptorContract()`
  test.
- GPU tests: sky visible with no environment-lighting DrawItems present;
  sky fully occluded behind an opaque `DrawItem` that fills the frame;
  sky orientation changes with camera rotation and is unchanged under
  pure camera translation; no environment configured draws the exact
  existing flat-clear-color background (byte-identical to today); Debug
  and Release with Vulkan Validation Layers clean.
- Image regression: all seven current no-environment goldens
  (`minimal_cube`, `world_scene`, `textured_quad`, `material_demo`,
  `lighting_demo`, `pbr_material_demo`, `hdr_roll_off_demo`) stay
  byte-identical, proven first. `ibl_material_demo`'s existing golden is
  expected to change (flat background -> visible sky) and needs a
  separate, human-reviewed re-capture under ADR-0042, recording the
  update reason explicitly.
- Full `ctest -LE gpu`, `ctest -L gpu`, and an `ATLANTIS_BUILD_TESTS=OFF`
  Runtime build in both configurations, matching Spec 0025's own
  verification shape.

## Risks & Open Questions

- The exact sky-depth literal (how close to the far plane) and the exact
  ray-reconstruction formula are Plan-stage values, not fixed by this
  Spec — they must be measured/derived at Plan time, not chosen
  opportunistically in code.
- `depthWriteEnabled = false` alongside a fixed near-far-plane sky depth
  needs a real Vulkan Validation Layers run to confirm clean at
  Implementation time (ADR-0071's own disclosed trade-off) — no
  hardware-specific fallback is designed here if it is not.
- Whether the sky Pipeline is built once (at environment-realization time)
  or needs a rebuild-on-resize trigger similar to the output-transform
  Pipeline depends on whether any of its inputs are extent-dependent; the
  current design believes none are (it has no final-target-format
  dependency, unlike output-transform), but this must be reconfirmed at
  Plan time.

## Out of Scope / Future Work

This foundation unblocks, but does not authorize, environment rotation,
multiple/blended environments, reflection probes captured from the sky,
and any exposure/bloom/color-grading work layered on top of what is
visible. Shadows, glTF import, and multi-instance/complex example scenes
remain later, separately scoped work per the current near-term roadmap
direction.

## Human Review Approval — 2026-09-04

Approved by the repository maintainer against
[PR #120](https://github.com/slmao/Atlantis/pull/120), accepting this
Spec and [ADR-0071](../adr/0071-visible-sky-background-rendering-integration.md)
(now `Accepted`) as written. The four items in Risks & Open Questions —
the sky-depth literal/ray-reconstruction formula, `depthWriteEnabled`'s
Validation Layers confirmation, whether the sky Pipeline needs a
resize/rebuild trigger, and the expected `ibl_material_demo` golden
re-capture — are accepted as deferred to Plan/Implementation, not as
blockers to this approval. This authorizes drafting Plan 0026, not
implementation.
