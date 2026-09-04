# ADR 0072: Directional Shadow Map Resource, Pass, and PBR Shader Integration

- **Status:** Proposed
- **Date:** 2026-09-05
- **Deciders:** slmao — Human Review pending
- **Related Spec:** [specs/0027-directional-shadow-foundation.md](../specs/0027-directional-shadow-foundation.md) (`Draft`)

## Context

`Texture` (`src/rhi/include/atlantis/rhi/texture.h`) is, by its own header
comment, "used, this round, exclusively as a depth attachment — no
sampled/shader-read usage." A shadow map needs both: written as a depth
attachment from the light's own point of view, then sampled (compared)
from the camera's point of view while shading `PbrDirectLit`/PBR-IBL
surfaces. This is the identical shape ADR-0068 already solved for
`HdrColorTarget` — "a new resource type, distinct from `RenderTarget`/
`Texture`/`SampledTexture`... exposing exactly two narrow capabilities."

`Device::createPipeline()` unconditionally sets
`VkPipelineRenderingCreateInfo::colorAttachmentCount = 1`
(`vulkan_device.cpp:1230`) — every real `Pipeline` in this codebase
today has exactly one color attachment. A shadow pass draws occluders
into a depth-only target with no color output at all; no existing
`PipelineCreateParams` combination expresses that.

`pbr_direct_lit.slang`'s own directional-light loop
(`shaders/pbr_direct_lit/pbr_direct_lit.slang:126-153`) is the one place
a directional light's contribution is accumulated, at exactly one line:
`accumulated += (kD * diffuseColor / kPi + specular) * radiance * NdotL;`.
`pbr_ibl.slang`'s own copy of this loop is byte-for-byte identical
("the direct-light loops intentionally match pbr_direct_lit.slang").
Both shaders' own point-light loop and (for `pbr_ibl.slang`) IBL terms
sit outside this loop entirely — the natural, precise place to gate a
shadow factor without touching anything else.

`SamplerCreateParams` has no comparison-sampler concept
(`compareEnable`/`compareOp`) — Vulkan's own hardware depth-compare
sampling is not modeled anywhere in this RHI today.

Plan 0025 P2 already fixed descriptor-pool sizing at `3 * maxSets`
combined-image-sampler descriptors per set — sized for `pbr_ibl`'s own
three-sampler worst case (base color, prefiltered cubemap, DFG LUT). A
fourth sampler (the shadow map) on the same Pipeline exceeds that
budget.

A real, disclosed, independent, pre-existing bug already exists in
`VulkanCommandList::bindUniformBuffer()`'s own redundant-write skip
(`vulkan_command_list.cpp:328`): revisiting a Pipeline's own descriptor
set after a *different* Pipeline's set was bound in between, within one
`CommandList` recording, re-triggers a real Validation Layers error
(flagged during Plan 0026's own implementation, not fixed there). This
ADR's own design must state plainly whether it depends on that bug being
fixed first.

## Decision

### D-1. A new, single-purpose RHI resource type — `ShadowMap`

Mirrors `HdrColorTarget`'s own exact shape (ADR-0068 D-1): a new type,
inheriting neither `Texture` nor `SampledTexture`, owning one GPU image
created with both `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` and
`VK_IMAGE_USAGE_SAMPLED_BIT`, `DepthFormat::D32Sfloat` (the RHI's only
existing depth format — Vulkan's own mandatory-format-support table
guarantees this combination on `D32_SFLOAT`, optimal tiling, on every
Vulkan-conformant device; no new capability check class beyond
ADR-0068 D-2's own established pattern). `Device::createShadowMap(ShadowMapCreateParams)`
mirrors `createHdrColorTarget()`'s own two-outcome `Result` shape.
Fixed resolution (`extent`), never resized on window resize (a shadow
map's own resolution is independent of the final presentable target's
extent — a real, disclosed difference from `HdrColorTarget`/depth
`Texture`, which both resize with the window). Caller-owned,
`std::unique_ptr<ShadowMap>`, created once at Runtime startup.

### D-2. `PipelineCreateParams::hasColorAttachment` — a depth-only Pipeline

A new, additive field, `bool hasColorAttachment = true`, mirroring
`hasDepthAttachment`'s own established shape and default-preserves-
today's-behavior convention. When `false`,
`VkPipelineRenderingCreateInfo::colorAttachmentCount = 0` and
`pColorAttachmentFormats = nullptr`; `colorFormat` (still present in
`PipelineCreateParams`, structurally required today) is ignored in this
case, mirroring how `depthAttachmentFormat` is already ignored when
`hasDepthAttachment == false`. The shadow Pipeline sets
`hasColorAttachment = false`, `hasDepthAttachment = true`.

### D-3. One shadow-casting Pipeline, reusing every DrawItem's own Mesh

A single, new `shaders/shadow_cast/shadow_cast.slang` pair — a
minimal vertex shader (`output.position = mul(lightViewProjection,
mul(objectToWorld, float4(position, 1.0)))`, reading only the position
attribute from the shared 44-byte mesh vertex stride, mirroring
`minimalMeshVertexLayout()`'s own single-attribute-from-shared-stride
convention) and a trivial, empty fragment shader (Vulkan's own pipeline
creation requires a fragment stage even for depth-only output; this one
does no work). One `ObjectToWorldOnly`-shaped push constant, matching
`fallbackMaterial_`'s own shape. Binding 0 is the light-space uniform
(D-6); no sampled-texture binding.

**Every opaque `DrawItem` already passed to `Renderer::drawFrame()` is
both shadow-caster and shadow-receiver — no new per-entity "casts
shadow" flag.** This means the shadow pass draws with **one** Pipeline
for every `DrawItem`, never switching Pipelines mid-pass — directly
relevant to the pre-existing `bindUniformBuffer()` constraint (Context):
a single-Pipeline draw loop only ever rebinds the *same* descriptor set
consecutively, which `bindUniformBuffer()`'s own existing skip already
handles correctly (proven safe by every existing multi-`DrawItem`-same-
Material scene already in production). **This design does not depend on
that bug being fixed, and does not fix it** — it simply never exercises
the A-B-A pattern that triggers it, by construction (one Pipeline, one
descriptor set, the whole pass). A future extension needing more than
one shadow-casting Pipeline (e.g., per-vertex-layout variants) would
need to re-examine this.

### D-4. RenderGraph — a third pass, before "draw"; the same *kind* of widening ADR-0068 D-3 already made

`Renderer::drawFrame()`'s internal `RenderGraphBuilder` gains a "shadow"
pass, declared and compiled before "draw": writes a new `shadowMap`
resource with `ResourceState::DepthAttachmentReadWrite` (the shadow
Pipeline's own depth output); "draw" then **reads** that same resource
(`ResourceState::ShaderRead`) — the identical write-then-read,
single-producer ordering RenderGraph's own dependency derivation already
handles for `HdrColorTarget`. `ResourceBinding` gains one new field,
`ShadowMap* shadowMap`; Guard 0's "exactly one of ..." rule widens from
four kinds to five. `CommandList` gains: `transitionResource(ShadowMap&,
...)`; `beginRendering(ShadowMap&, ClearDepthValue)` (a depth-only
overload, no color, no color clear); `bindTexture(binding, const
ShadowMap&, const Sampler&)`. This is the same *kind* of change
ADR-0068 D-3 already made when it widened Guard 0/1 from three kinds to
four for `HdrColorTarget` — not a new precedent, the second instance of
an established one.

### D-5. Sampling — plain `Sampler2D`, manual comparison, no hardware compare sampler

`SamplerCreateParams` is not widened. The shadow map is sampled with an
ordinary `Sampler2D` (`Filter::Nearest`, `AddressMode::ClampToEdge`,
mirroring the output-transform sampler's own 1:1-mapping convention);
the shader reads the stored depth and compares it against the fragment's
own light-space depth directly:

```
shadowCoord = lightViewProjection * float4(worldPosition, 1.0)   // clip space
shadowNdc = shadowCoord.xyz / shadowCoord.w
shadowUv = shadowNdc.xy * 0.5 + 0.5
inBounds = all(shadowUv >= 0) && all(shadowUv <= 1) && shadowNdc.z >= 0 && shadowNdc.z <= 1
storedDepth = shadowMapSampler.Sample(shadowUv).r
shadowFactor = (!inBounds) ? 1.0 : ((shadowNdc.z - kShadowBias) <= storedDepth ? 1.0 : 0.0)
accumulated += shadowFactor * (kD * diffuseColor / kPi + specular) * radiance * NdotL   // directional term only
```

`kShadowBias` is a single, fixed, named literal (Plan-stage value,
mirroring ADR-0068 D-5's own "one literal, hand-verifiable" convention)
— no slope-scaled or normal-offset bias, no PCF, no cascades, no soft
edges (all explicitly out of scope). **Out-of-bounds behavior:** a
fragment outside the shadow frustum's own coverage (`!inBounds`) is
always treated as fully lit (`shadowFactor = 1.0`) — never "always
shadowed," which would visibly break every surface outside the fixed
coverage volume.

### D-6. Light-space matrix — a fixed orthographic volume, appended to the existing camera uniform

The directional light's own `direction` (already extracted into
`FrameLightingData.directionalLights[0]`) drives a fixed-size
orthographic projection — center, half-extent, near/far are Runtime
constants (Plan-stage values), **not** dynamically fit to scene/camera
bounds each frame (that is the first step toward cascaded shadow maps,
explicitly out of scope). The resulting `view`/`projection` pair (128
bytes) is appended to the existing per-frame camera uniform buffer
(464 → 592 bytes), mirroring Spec 0025 P3's own "SH coefficients
appended after the existing region, shaders only declare/read their own
current prefix" precedent exactly. When no directional light exists
(`directionalLightCount == 0`), this tail region's content is never read
by any shader (the shadow lookup lives inside the same `for (i <
directionalLightCount)` loop the shadow factor multiplies into) — Runtime
still writes a deterministic value there every frame (never
uninitialized memory), matching `writeEnvironmentIrradianceSh()`'s own
"null source produces the no-environment all-zero contribution"
precedent.

### D-7. `pbr_direct_lit.slang`/`pbr_ibl.slang` — a real, disclosed content change to both existing shaders

Both shaders gain one new binding (the shadow map sampler, at the next
free slot — binding 2 for `pbr_direct_lit` today at binding 1 only;
binding 4 for `pbr_ibl`, today at bindings 1–3) and the shadow-factor
multiply inside their own existing directional-light loop (D-5). This
is the same class of disclosed shader-content change ADR-0068 D-7
already made to `lit_textured.slang`/`pbr_direct_lit.slang` (removing
their final clamp) — **the first Spec since ADR-0068 unable to claim
"zero further `.slang` file modified" for these two files.** Both
shaders' own descriptor contract functions
(`pbrDirectLitExpectedDescriptorContract()`/
`pbrIblExpectedDescriptorContract()`) gain the new binding entry;
`sampledTextureBindingCount` moves from 1→2 (`pbr_direct_lit`) and
3→4 (`pbr_ibl`).

**Descriptor-pool consequence:** Plan 0025 P2's own `3 * maxSets`
combined-image-sampler sizing was derived from `pbr_ibl`'s own
three-sampler worst case; a fourth sampler on that same Pipeline exceeds
it. Plan 0027 must re-derive this to `4 * maxSets` and re-run the N=6
descriptor-pool stress proof (Spec 0021/ADR-0064's own established
proof style, already re-run twice — Plan 0025, Plan 0026) against the
real, new worst case.

**Golden consequence:** every existing golden built on `PbrDirectLit`
or `pbr_ibl` (`pbr_material_demo`, `hdr_roll_off_demo`,
`ibl_material_demo`) needs a human-reviewed re-capture — the shader
itself changes for all of them, matching ADR-0068 D-6's own disclosed
"introducing a real transform where none existed before shifts byte
values for every existing golden, not only affected content" precedent.
Non-PBR goldens (`minimal_cube`, `world_scene`, `textured_quad`,
`material_demo`, `lighting_demo`) are untouched — neither shader they use
(`minimal_mesh`, `textured_quad`, `lit_textured`) is modified by this
ADR.

## Consequences

### Positive

- Shadow-map write/sample duality is available through a backend-neutral
  RHI contract, matching `HdrColorTarget`'s own established pattern —
  not a new kind of resource-ownership problem for this codebase.
- One shadow-casting Pipeline, reused for every `DrawItem`, sidesteps the
  known `bindUniformBuffer()` A-B-A constraint by construction, without
  fixing or depending on a fix for it.
- No new Sampler capability, no PCF/cascade infrastructure — the closed,
  minimal slice the Spec asks for.
- Shadows are structurally confined to the one directional light's own
  direct term; IBL diffuse/specular and the sky pass are provably
  untouched (they live outside the modified loop and outside the
  modified shaders entirely).

### Negative / Trade-offs

- A real, disclosed public RHI surface: one new resource type
  (`ShadowMap`), one new `PipelineCreateParams` field
  (`hasColorAttachment`), three new/overloaded `CommandList` methods, one
  new `ResourceBinding` field, one new `Device` factory method.
- A real, disclosed content change to two already-`Approved`,
  already-shipped shaders (`pbr_direct_lit.slang`, `pbr_ibl.slang`) —
  every existing PBR-family golden needs re-capture.
- Descriptor-pool sampler-per-set provisioning must grow from `3 *
  maxSets` to `4 * maxSets` (Plan 0025 P2), re-derived and re-proven at
  Plan 0027 time.
- A fixed, non-scene-fitted orthographic shadow volume is a real quality
  ceiling (coverage is a Runtime constant, not adaptive) — acceptable
  for a first slice, explicitly not cascaded/adaptive.
- One more new production shader pair (`shadow_cast`), on top of the two
  modified existing ones.

## Alternatives Considered

- **Widen `Texture`/`SampledTexture` instead of a new `ShadowMap` type.**
  Rejected for the identical reason ADR-0068 rejected widening
  `RenderTarget`/`SampledTexture` for `HdrColorTarget`: it conflates two
  purpose-built contracts (`Texture` is depth-attachment-only by its own
  stated contract; `SampledTexture` is upload-only, never a render
  target) rather than keeping both intact.
- **Hardware depth-compare sampling (`VkSamplerCreateInfo::compareEnable`).**
  Rejected for this minimal slice — a real new RHI `Sampler` capability
  for a benefit (built-in single-tap PCF-lite filtering) this ADR's own
  "no soft-shadow system" scope does not need; a manual compare in the
  shader is exactly as correct for a single hard-edged tap.
- **Dynamic, camera-frustum-fitted shadow volume (the first CSM step).**
  Rejected — explicitly out of scope (Spec 0027's own Non-Goals); a
  fixed volume is the smallest slice that produces a real, verifiable
  shadow.
- **A separate shadow-enabled shader variant (mirroring `pbr_ibl.slang`'s
  own split from `pbr_direct_lit.slang`), leaving the existing two
  shaders byte-for-byte untouched.** Rejected — the combinatorial
  variant count (direct/direct+shadow/ibl/ibl+shadow) is a real
  complexity/maintenance cost this minimal slice does not need, since
  the shadow term is already structurally gated by the existing
  `directionalLightCount` loop and costs nothing when that count is
  zero; the two existing shaders' own real, disclosed modification
  (D-7) is the smaller change.
- **Fixing `bindUniformBuffer()`'s own A-B-A constraint as part of this
  ADR**, since shadows are the first feature to introduce a genuinely
  new multi-pass-per-frame Pipeline pattern. Rejected — out of this
  ADR's own scope per explicit direction; D-3's single-Pipeline shadow
  pass does not need it, and fixing it is flagged separately (not here)
  as independent follow-up work.
