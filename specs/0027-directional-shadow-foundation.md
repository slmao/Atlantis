# Spec: Directional Shadow Foundation

- **Status:** Draft
- **Author:** slmao
- **Created:** 2026-09-05
- **Related Plan(s):** None yet — drafted after this Spec and ADR-0072 are
  approved.
- **Related ADR(s):** [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md) (`Proposed`)

## Summary

Adds one directional light's own shadow to `PbrDirectLit`/PBR-IBL
surfaces: a single, fixed-resolution shadow map rendered from the
light's own point of view, sampled once per fragment inside the existing
directional-light term. Every opaque scene entity is both caster and
receiver. Reuses Spec 0024's HDR intermediate/output-transform and Spec
0025/0026's environment/sky paths completely unchanged — shadows affect
only the one directional light's own direct contribution.

## Motivation / Problem Statement

Every PBR surface today receives direct and (when configured) image-based
light with no occlusion at all — two entities never shadow each other,
and nothing shadows the ground. This is the largest remaining gap in the
near-term basic-lighting/PBR/IBL/sky sequence the human-directed roadmap
already named. A single directional light's own shadow — the classic
"sun casts a shadow" case — is the smallest real slice that closes it
without the scheduling/quality-tier complexity of cascades, multiple
shadowed lights, or soft-shadow filtering.

## Goals

- One directional light, one fixed-resolution shadow map, producing a
  real, human-verifiable shadow when a simple occluder sits between the
  light and a receiving surface.
- Shadows attenuate only the directional light's own direct contribution
  in `PbrDirectLit`/PBR-IBL; IBL diffuse/specular and the visible sky
  (Spec 0025/0026) are provably unaffected.
- Every existing non-PBR scene/golden (`minimal_cube`, `world_scene`,
  `textured_quad`, `material_demo`, `lighting_demo`) stays byte-identical
  — neither shader either of those built-in `MaterialKind`s uses is
  touched.
- A fragment outside the shadow map's own fixed coverage volume is
  always treated as lit, never as shadowed.

## Non-Goals

- Cascaded shadow maps, any dynamic/camera-fitted shadow volume, or
  multiple shadowed lights (directional or point).
- Point-light shadows (cube shadow maps) — only the one existing
  directional-light slot is shadowed.
- Soft shadows, PCF, PCSS, or any filtered/multi-tap sampling; no
  hardware depth-compare sampler.
- Shadow atlases, per-entity shadow-casting opt-out, or any new
  World/Scene schema field — every opaque entity already passed to
  `Renderer::drawFrame()` is both caster and receiver.
- Shadowing IBL, the sky, or any `MaterialKind` other than
  `PbrDirectLit`/PBR-IBL.
- Fixing the pre-existing, independent `bindUniformBuffer()` descriptor-
  rebind constraint (see Risks & Open Questions) — this Spec's own
  design does not need that fix and does not attempt it.
- Android implementation in this round — the design must stay portable
  to the Windows/Android Vulkan Phase 1 targets.

## Requirements

### Functional

- A new `ShadowMap` RHI resource type (depth format, dual
  attachment+sampled capability) is created once at Runtime startup, at
  a fixed resolution, independent of window/final-target extent — never
  resized on window resize.
- A new RenderGraph "shadow" pass, executed before the existing "draw"
  pass, renders every opaque `DrawItem` already passed to
  `Renderer::drawFrame()` into the shadow map from the directional
  light's own point of view, using one shared, depth-only Pipeline (no
  per-`DrawItem` material dispatch — position and object-to-world only).
- The light-space view/projection matrix is derived from the one
  directional light's own direction and a fixed, Runtime-configured
  orthographic volume (center, half-extent, near/far) — never fit to
  scene or camera bounds each frame.
- `PbrDirectLit`/PBR-IBL sample the shadow map once per fragment, inside
  their own existing directional-light loop only, comparing the
  fragment's own light-space depth (with a fixed bias) against the
  stored value; the result multiplies only that loop's own accumulated
  contribution.
- A fragment whose light-space position falls outside the shadow map's
  own UV/depth coverage is always treated as fully lit.
- No directional light configured (`directionalLightCount == 0`) means
  the shadow term is never read (the existing light-count loop already
  gates it) — behavior is identical to today, byte-for-byte, for any
  scene with no directional light.

### Non-functional

- **Performance:** one additional, fixed-cost RenderGraph pass per frame
  (constant work regardless of scene complexity beyond `DrawItem` count);
  one additional texture sample per PBR fragment. No per-frame dynamic
  frustum computation, no cascade splitting.
- **Memory:** one fixed-resolution depth image, created once, never
  resized. No new persistent CPU-side state beyond the fixed orthographic
  volume's own constants.
- **Portability:** the shadow map's depth format/usage combination is
  checked the same way `HdrColorTarget`'s own format capability already
  is (ADR-0068 D-2's established pattern) — a real runtime check, not an
  unconditional assumption, even though Vulkan's own mandatory-format-
  support table guarantees it on every conformant device.
- **Determinism:** shadow rendering is a pure function of scene geometry,
  the directional light's own direction, and the fixed orthographic
  volume — repeated frames with unchanged inputs produce identical
  shadow maps and identical shadowed pixels.

## Proposed Design

```
World's own directional light direction
  -> fixed orthographic volume (Runtime constants)
  -> light-space view/projection, appended to the existing camera uniform
  -> new "shadow" RenderGraph pass: every opaque DrawItem, one shared
     depth-only Pipeline, writes the new ShadowMap
  -> existing "draw" pass: PbrDirectLit/PBR-IBL sample the ShadowMap once,
     inside their own existing directional-light loop only
  -> existing Spec 0024 HDR intermediate / output-transform pass (unchanged)
```

See [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
for the exact mechanism: the `ShadowMap` resource type and its
RenderGraph/`CommandList` integration (D-1, D-4), the new
`PipelineCreateParams::hasColorAttachment` field for the depth-only
shadow Pipeline (D-2), why one shared Pipeline for every `DrawItem`
sidesteps the pre-existing `bindUniformBuffer()` A-B-A constraint without
fixing it (D-3), the manual (non-hardware) depth-compare formula and its
fixed bias/out-of-bounds rule (D-5), the light-space uniform layout
(D-6), and the exact, disclosed content change to
`pbr_direct_lit.slang`/`pbr_ibl.slang` (D-7).

`Renderer::drawFrame()` gains new, caller-owned, borrowed parameters for
the shadow map, the shadow-casting Pipeline, and the light-space uniform
buffer — mirroring `hdrColorTarget`'s/`skyPipeline`'s own existing
caller-owned-resource pattern (ADR-0022's stateless-orchestrator
constraint, unchanged). Runtime creates the shadow map and shadow
Pipeline once at startup, unconditionally (not gated on whether a scene
happens to have a directional light — mirroring the sky Pipeline's own
"created once when environment configured" precedent is deliberately
*not* followed here, since a directional light can be added/removed at
the World level without a BootstrapConfig-level signal the way an
environment asset is; see Risks & Open Questions item 1).

## Architectural Impact

**Yes.** [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
records:

- A new RHI resource type (`ShadowMap`) and its full RenderGraph/
  `CommandList`/`Device` integration — the same *kind* of widening
  ADR-0068 already made for `HdrColorTarget`.
- A new, additive `PipelineCreateParams` field (`hasColorAttachment`) for
  depth-only Pipelines.
- A real, disclosed content and descriptor-contract change to two
  already-shipped shaders (`pbr_direct_lit.slang`, `pbr_ibl.slang`).
- A new `Renderer::drawFrame()` public-API change (three more
  caller-owned parameters).

No new module, dependency, or threading model. No `World`/`Scene` schema
change. No Sampler RHI capability is added.

## Alternatives Considered

- **Skip shadows, do exposure/bloom/color-grading next instead.**
  Rejected per explicit human direction — shadows are the next-named
  step in the basic-lighting/sky/shadow/PBR/IBL sequence, ahead of
  post-processing.
- **Cascaded or camera-fitted shadows as the first slice.** Rejected —
  a fixed volume is the smallest change that produces a real, verifiable
  shadow; cascades are a natural, separately-scoped follow-on once this
  foundation exists.
- **A separate shadow-enabled shader variant, leaving
  `pbr_direct_lit.slang`/`pbr_ibl.slang` untouched.** Rejected in
  ADR-0072 — the combinatorial variant cost is larger than the two
  shaders' own real, disclosed, structurally-gated modification.

## Testing & Verification Plan

- GPU-independent tests cover the new `ShadowMap`/`PipelineCreateParams::hasColorAttachment`
  RHI surface's own default-compatibility (existing Pipelines
  unaffected), the widened RenderGraph Guard 0 (five kinds), and the
  updated `pbr_direct_lit`/`pbr_ibl` descriptor contracts against their
  own real, compiled shader reflection.
- Shader reflection tests prove the light-space uniform's tail offset/
  size and the new shadow-map binding slot on both shaders, matching
  this codebase's own established real-`slangc`-reflection convention
  (never a stale, committed golden JSON).
- Real-GPU tests: a simple occluder between the light and a receiving
  plane produces a measurably darker sample directly behind it than an
  unoccluded control sample (a real, predicted-direction check, not
  "the image changed" — mirroring Spec 0026's own established
  discriminator style); moving the occluder or the light direction moves
  the shadow's own footprint predictably; a scene with no directional
  light renders byte-identical to today; a fragment outside the fixed
  shadow volume is confirmed lit, not shadowed; Debug and Release with
  Vulkan Validation Layers clean.
- Image regression: `minimal_cube`, `world_scene`, `textured_quad`,
  `material_demo`, `lighting_demo` remain byte-identical (neither shader
  they use is touched). `pbr_material_demo`, `hdr_roll_off_demo`,
  `ibl_material_demo` all need a human-reviewed re-capture under
  ADR-0042 — the shader itself changes for every one of them, regardless
  of whether their own scene has real occluder geometry.
- Descriptor-pool: Plan 0025 P2's own `3 * maxSets` combined-image-
  sampler sizing must be re-derived to `4 * maxSets` (`pbr_ibl`'s own new
  four-sampler worst case) and the N=6 stress proof (already re-run at
  Plan 0025 and Plan 0026) re-run against it.
- Full `ctest -LE gpu`, `ctest -L gpu`, and an `ATLANTIS_BUILD_TESTS=OFF`
  Runtime build in both configurations, matching every prior Spec in this
  sequence.

## Risks & Open Questions

1. **Shadow-map/Pipeline lifecycle trigger.** Unlike the sky Pipeline
   (created only when `BootstrapConfig` names an environment), a
   directional light is a `World`-level construct with no equivalent
   Runtime-config-level signal — the Proposed Design creates the shadow
   map/Pipeline unconditionally, always. Confirm this is acceptable
   (a small, fixed, always-present cost) rather than inventing a new
   BootstrapConfig-level "shadows enabled" flag.
2. **Fixed orthographic volume's own concrete numbers** (resolution,
   center, half-extent, near/far) are Plan-stage values, not fixed by
   this Spec — they must be measured/derived against the real demo
   scenes at Plan time, not chosen opportunistically in code.
3. **The exact bias literal** is likewise a Plan-stage value, tuned
   against real captured shadows (too small self-shadows/acnes, too
   large peter-pans) — this Spec fixes the mechanism (a flat depth bias,
   D-5), not the number.
4. **The pre-existing `bindUniformBuffer()` A-B-A descriptor-rebind
   constraint is not a blocking prerequisite for this Spec's own
   single-shadow-Pipeline design** (ADR-0072 D-3's own reasoning: one
   Pipeline, one descriptor set, for the entire shadow pass, never
   revisited after a different Pipeline's set). This Spec does not fix
   that constraint and does not route around it by any other means; it
   is flagged, separately tracked, out of scope here.
5. **Descriptor-pool capacity re-derivation** (`3*maxSets` → `4*maxSets`)
   is a real, disclosed consequence (ADR-0072 D-7) that Plan 0027 must
   carry out and re-prove — flagged here so it is not discovered as a
   surprise at Implementation time.

## Out of Scope / Future Work

This foundation unblocks, but does not authorize, cascaded shadow maps,
camera-frustum-fitted shadow volumes, additional shadowed lights
(directional or point), soft/filtered shadows, shadow atlases, or
per-entity shadow-casting control. Fixing the independent
`bindUniformBuffer()` A-B-A descriptor-rebind constraint remains
separately tracked, unrelated to this Spec's own scope. Exposure,
bloom, and color grading remain later, separately scoped work per the
current near-term roadmap direction.
