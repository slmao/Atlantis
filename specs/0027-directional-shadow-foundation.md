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
  is (ADR-0068 D-2's established pattern) — a real runtime check, and a
  genuinely necessary one here: Vulkan guarantees sampled-image read
  support for `D32_SFLOAT` specifically, but guarantees depth-attachment
  support only collectively across two formats, not for `D32_SFLOAT`
  unconditionally on every conformant device (ADR-0072 D-1).
- **Determinism:** shadow rendering is a pure function of scene geometry,
  the directional light's own direction, and the fixed orthographic
  volume — repeated frames with unchanged inputs produce identical
  shadow maps and identical shadowed pixels.

## Proposed Design

```
World's own directional light direction
  -> fixed orthographic volume (Runtime constants)
  -> light-space view/projection: appended to the existing camera uniform
     (read by pbr_direct_lit/pbr_ibl) AND written to a second, dedicated
     buffer (read by the shadow Pipeline only — ADR-0072 D-6)
  -> new "shadow" RenderGraph pass: every opaque, standard-layout DrawItem,
     one shared depth-only Pipeline, writes the new ShadowMap
  -> existing "draw" pass: PbrDirectLit/PBR-IBL sample the ShadowMap once,
     inside their own existing directional-light loop only
  -> existing Spec 0024 HDR intermediate / output-transform pass (unchanged)
```

See [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
for the exact mechanism: the `ShadowMap` resource type, its dedicated
`Sampler`, and its RenderGraph/`CommandList` integration (D-1, D-4), the
new `PipelineCreateParams::hasColorAttachment` field for the depth-only
shadow Pipeline (D-2), the shadow Pipeline's own scope to standard
44-byte-stride `DrawItem`s and the precise, narrow A-B-A claim (D-3), the
manual (non-hardware) depth-compare formula and its fixed bias/
out-of-bounds rule (D-5), why the light-space data needs two buffers
rather than one and the identity-matrix no-light sentinel (D-6), and the
exact, disclosed content, descriptor-capacity, and call-site-migration
consequences of the change to `pbr_direct_lit.slang`/`pbr_ibl.slang`
(D-7).

`Renderer::drawFrame()` gains new, caller-owned, borrowed parameters for
the shadow map, its sampler, the shadow-casting Pipeline, and the
dedicated light-space uniform buffer — mirroring `hdrColorTarget`'s/
`skyPipeline`'s own existing caller-owned-resource pattern (ADR-0022's
stateless-orchestrator constraint, unchanged). Runtime creates the
shadow map, its sampler, and the shadow Pipeline once at startup,
unconditionally (not gated on whether a scene happens to have a
directional light — mirroring the sky Pipeline's own "created once when
environment configured" precedent is deliberately *not* followed here,
since a directional light can be added/removed at the World level
without a BootstrapConfig-level signal the way an environment asset is;
see Risks & Open Questions item 1). Every existing call site that
constructs a `PbrDirectLit`/PBR-IBL Material must also supply a real
`ShadowMap`/`Sampler` pair, since both shaders' new binding is
unconditional at Pipeline-creation time (ADR-0072 D-7) — see Testing &
Verification Plan and Risks & Open Questions item 6.

## Architectural Impact

**Yes.** [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
records:

- A new RHI resource type (`ShadowMap`) and its full RenderGraph/
  `CommandList`/`Device` integration — the same *kind* of widening
  ADR-0068 already made for `HdrColorTarget`.
- A new, additive `PipelineCreateParams` field (`hasColorAttachment`) for
  depth-only Pipelines.
- A real, disclosed content and descriptor-contract change to two
  already-shipped shaders (`pbr_direct_lit.slang`, `pbr_ibl.slang`),
  including three separate internal RHI capacity widenings (the closed
  sampled-binding-count set, descriptor-pool sampler sizing, and the
  `VulkanCommandList` texture-descriptor cache array) and a real
  migration of every existing call site that constructs a
  `PbrDirectLit`/PBR-IBL Material.
- A new `Renderer::drawFrame()` public-API change (four more
  caller-owned parameters: shadow map, its sampler, the shadow Pipeline,
  the dedicated light-space uniform buffer).

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
  they use is touched). `ibl_material_demo` **must** remain byte-identical
  — a hard requirement, not merely an expectation — since it has no
  directional light and the shadow term is structurally unreachable for
  it (ADR-0072 D-7); this is asserted by a real pixel-identity check, not
  skipped. `pbr_material_demo`/`hdr_roll_off_demo` follow a compare-first
  process: run each unmodified after implementation; a byte-identical
  result needs no action; a human-reviewed re-capture (ADR-0042) is
  requested only for a golden that actually shows a difference — never
  presumed in advance for either. A new real-GPU test (not an image-
  regression golden) renders one scene with both an environment and a
  directional-light occluder configured, and confirms the shadowed
  region's own IBL/ambient contribution is unchanged between shadowed and
  an otherwise-identical unshadowed render — the explicit, discriminating
  proof that shadows affect only the directional term (ADR-0072 D-7).
- Descriptor capacity: three separate, independent widenings, each
  re-verified — the closed `sampledTextureBindingCount` check (`{0,1,3}`
  → `{0,1,2,3,4}`), the descriptor pool's own combined-image-sampler
  sizing (Plan 0025 P2's `3 * maxSets` → `4 * maxSets`, `pbr_ibl`'s new
  four-sampler worst case), and `VulkanCommandList::textureDescriptorMemos_`'s
  fixed cache array (size 4 → 5). The descriptor-set-count formula
  (Plan 0026's `N+3`/`N+4`) is separately re-derived to `N+4`/`N+5` (the
  always-present `shadow_cast` Pipeline), and the N=6 stress proof
  (already re-run at Plan 0025 and Plan 0026) re-run against both the
  new set-count and the new per-set sampler budget.
- Call-site migration: every existing non-Runtime creator of a
  `PbrDirectLit`/PBR-IBL Material — `pbr_material_demo_fixture.cpp`/`.h`,
  `ibl_material_demo`'s fixture usage, `golden_generator/pbr_material_demo_main.cpp`,
  `tests/runtime/pbr_render_gpu_tests.cpp`,
  `tests/runtime/material_realization_gpu_tests.cpp`,
  `tests/runtime/material_ibl_selection_gpu_tests.cpp` — is updated to
  supply a real `ShadowMap`/`Sampler` pair and continues to build/pass
  after the change (ADR-0072 D-7).
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
   constraint is not a blocking prerequisite for the shadow pass's own,
   specific call sequence** (ADR-0072 D-3: one Pipeline, one descriptor
   set, bound for the shadow pass's entire duration, never revisited
   after a different Pipeline's set was bound in between). This claim is
   scoped to that one pass only — it says nothing about, and does not
   change, the same pre-existing, unrelated risk already latent in the
   main "draw" pass's own per-`DrawItem` dispatch loop. This Spec does
   not fix that constraint anywhere and does not route around it by any
   other means; it remains flagged and separately tracked.
5. **Descriptor-capacity re-derivation is three separate, independent
   changes, not one** (ADR-0072 D-7): the closed `sampledTextureBindingCount`
   check, the descriptor pool's own per-set sampler sizing
   (`3*maxSets` → `4*maxSets`), and `VulkanCommandList`'s own fixed-size
   texture-descriptor cache array — plus a fourth, distinct concern, the
   descriptor-**set**-count peak (`N+3`/`N+4` → `N+4`/`N+5`, the
   always-present `shadow_cast` Pipeline). Plan 0027 must carry out and
   re-prove all four — flagged here so none is discovered as a surprise
   at Implementation time.
6. **Call-site migration cost.** Because the new shadow-map binding is
   unconditional at Pipeline-creation time, every existing creator of a
   `PbrDirectLit`/PBR-IBL Material — not only Runtime — must be updated
   to supply a real `ShadowMap`/`Sampler`, whether or not it exercises
   actual shadow-casting behavior (ADR-0072 D-7, full file list in
   Testing & Verification Plan). Confirm this real, disclosed migration
   cost is accepted as part of this Spec's own scope, rather than judged
   large enough to prefer the separate-shader-variant alternative
   (Alternatives Considered).
7. **`ibl_material_demo`'s golden is a hard byte-identity requirement**,
   not an expectation (ADR-0072 D-7) — Plan 0027's own verification must
   assert this directly (a real pixel-identity check against the
   existing committed golden), not merely omit it from the re-capture
   list.

## For Human Confirmation

Each item below is this revision's own single recommended design for a
point the prior draft left underspecified or wrong; none is left open
for the Implementation phase to decide. Confirm or redirect each:

1. **Light-space data scheme:** two buffers — `pbr_direct_lit`/`pbr_ibl`
   extend their existing binding-0 camera buffer (592 bytes total,
   `pbr_direct_lit` gains 144 bytes of explicit padding to align its tail
   with `pbr_ibl`'s); `shadow_cast` gets its own, separate, dedicated
   128-byte buffer — because `bindUniformBuffer()` allows only one buffer
   per Pipeline. No-light sentinel is the identity matrix (not zero).
   The shadow-map `Sampler` is Runtime-owned, created once at startup,
   borrowed into `Renderer::drawFrame()` alongside the shadow map itself.
   (ADR-0072 D-6)
2. **Descriptor extension:** widen all three real, independent RHI
   limits together — the closed `sampledTextureBindingCount` set, the
   descriptor pool's per-set sampler sizing, and
   `VulkanCommandList::textureDescriptorMemos_`'s cache array — plus
   re-derive the descriptor-**set**-count peak (`N+4`/`N+5`). (ADR-0072
   D-7)
3. **Mesh/compatibility scope:** the shadow Pipeline reads only the
   standard 44-byte asset-cooked vertex layout; it is scoped to
   `DrawItem`s built from real, `World`-driven scene meshes, not assumed
   universal. Every existing non-Runtime `PbrDirectLit`/PBR-IBL call site
   is migrated to supply real shadow infrastructure (file list in
   Testing & Verification Plan) — accepted as this Spec's own real cost,
   rather than adopting the separate-shader-variant alternative.
   (ADR-0072 D-3, D-7)
4. **Vulkan capability wording:** the runtime format-capability check
   for the `ShadowMap`'s depth-attachment-plus-sampled combination is
   genuinely necessary (not merely precautionary), since Vulkan
   guarantees depth-attachment support only collectively, not for
   `D32_SFLOAT` specifically; the trivial empty fragment shader remains
   the right choice for `shadow_cast.slang` because this repository's own
   `createPipeline()` — not Vulkan itself — currently requires a fragment
   stage. No design change from the prior draft, only corrected
   justification. (ADR-0072 D-1, D-3)
5. **Golden strategy:** `ibl_material_demo` is a hard, asserted
   byte-identity requirement. `pbr_material_demo`/`hdr_roll_off_demo`
   follow compare-first — re-capture requested only if an actual
   difference is observed, never presumed. A new discriminating real-GPU
   test proves IBL/ambient contributions are unaffected by shadowing.
   (ADR-0072 D-7)

## Out of Scope / Future Work

This foundation unblocks, but does not authorize, cascaded shadow maps,
camera-frustum-fitted shadow volumes, additional shadowed lights
(directional or point), soft/filtered shadows, shadow atlases, or
per-entity shadow-casting control. Fixing the independent
`bindUniformBuffer()` A-B-A descriptor-rebind constraint remains
separately tracked, unrelated to this Spec's own scope. Exposure,
bloom, and color grading remain later, separately scoped work per the
current near-term roadmap direction.
