# ADR 0072: Directional Shadow Map Resource, Pass, and PBR Shader Integration

- **Status:** Accepted
- **Date:** 2026-09-05
- **Deciders:** slmao — Human Review, approved 2026-09-05
- **Related Spec:** [specs/0027-directional-shadow-foundation.md](../specs/0027-directional-shadow-foundation.md) (`Approved`)
- **Acceptance Record (2026-09-05):** Accepted by Human Review as part of
  Spec 0027's approval against [PR #123](https://github.com/slmao/Atlantis/pull/123)
  (commit `75a72d2`). Does not change this ADR's own Decision,
  Consequences, or Alternatives Considered above.

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

`bindUniformBuffer()` (`vulkan_command_list.cpp:316-350`) hardcodes
`write.dstBinding = 0` unconditionally — this RHI has no mechanism for a
*second* uniform buffer on any Pipeline today, at any binding. Any new
uniform data a Pipeline needs must live inside the one buffer already
bound at binding 0, or in a wholly separate Pipeline's own, independent
binding 0.

Three separate, real, hardcoded limits gate how many sampled-texture
bindings a Pipeline may declare, found by reading the actual code, not
assumed:

- `Device::createPipeline()` asserts
  `ATLANTIS_CHECK(sampledTextureBindingCount == 0 || == 1 || == 3)`
  (`vulkan_device.cpp:992-993`) — a closed set that does not include 2
  or 4.
- `createDescriptorPoolOfSize()` sizes `COMBINED_IMAGE_SAMPLER`
  descriptors at `3U * maxSets` (`vulkan_device.cpp:435`), derived from
  `pbr_ibl`'s own three-sampler worst case (Plan 0025 P2).
- `VulkanCommandList::textureDescriptorMemos_` is a fixed
  `std::array<TextureDescriptorMemo, 4>` (`vulkan_command_list.h:153`);
  `bindTexture()` itself asserts `binding < textureDescriptorMemos_.size()`
  (`vulkan_command_list.cpp:358`) — binding index 4 is out of bounds
  against this array today.

A real, disclosed, independent, pre-existing bug already exists in
`bindUniformBuffer()`'s own redundant-write skip: revisiting a
Pipeline's own descriptor set after a *different* Pipeline's set was
bound in between, within one `CommandList` recording, re-triggers a
real Validation Layers error (flagged during Plan 0026's own
implementation, not fixed there). This ADR's own design must state
precisely which call sequences it does and does not depend on being
free of that pattern — not a blanket "not a dependency" claim.

Every mesh a real, asset-cooked scene entity uses shares one fixed
44-byte vertex stride (`kMeshArtifactVertexStrideBytes`,
position/color/uv/normal) — confirmed by `scene_extraction.cpp`'s own
`DrawItem` construction, which resolves every `Mesh` from
`meshResourceMap_`, itself populated only from cooked mesh assets. This
is **not** a universal property of every `Mesh` the public `createMesh()`
API can produce — a caller-constructed synthetic `Mesh` (already used by
this repository's own `pipeline_depth_write_gpu_tests.cpp` and
`sky_background_gpu_tests.cpp`, each with their own, different, smaller
vertex stride) is not guaranteed to match it.

`Renderer::drawFrame()`'s own contract (ADR-0022) is that of a stateless
orchestrator: it does not read `World`, does not parse or interpret the
contents of any uniform buffer it is handed, and does not introspect a
`Mesh`'s own vertex layout to decide what is safe to draw — every such
decision is made by the caller and communicated through explicit
parameters. Any "skip the shadow pass's own draws this frame" or "this
`DrawItem` is unsafe for the shadow Pipeline" decision must therefore be
signaled by the caller, not derived by `Renderer` itself from `World` or
buffer state — this shapes D-3 and D-4 below.

## Decision

### D-1. A new, single-purpose RHI resource type — `ShadowMap`

Mirrors `HdrColorTarget`'s own exact shape (ADR-0068 D-1): a new type,
inheriting neither `Texture` nor `SampledTexture`, owning one GPU image
created with both `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` and
`VK_IMAGE_USAGE_SAMPLED_BIT`, `DepthFormat::D32Sfloat` (the RHI's only
existing depth format). `Device::createShadowMap(ShadowMapCreateParams)`
mirrors `createHdrColorTarget()`'s own two-outcome `Result` shape,
`ShadowMapCreateError::FormatFeaturesUnsupported` checked first
(ADR-0068 D-2's own established pattern, not a new mechanism).

**Corrected capability claim:** the Vulkan spec's own mandatory
format-support rules do not guarantee this exact combination on every
conformant device. Reading (sampling) a depth image is guaranteed only
for `D16_UNORM` and `D32_SFLOAT` — `D32_SFLOAT` is one of exactly two
formats required to support `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT` for
this purpose. `VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT`, however,
is guaranteed only collectively: the spec requires *at least one* of
`D32_SFLOAT`/`X8_D24_UNORM_PACK32` to support it — not `D32_SFLOAT`
specifically. A conformant device could in principle satisfy that
collective requirement using only `X8_D24_UNORM_PACK32`, leaving
`D32_SFLOAT` without attachment support. Unlike ADR-0068 D-2's own
`HdrColorTarget` case (where a real `vulkaninfo` run against this
repository's own target GPU independently confirmed both required bits),
this combination is not unconditionally spec-guaranteed — the real
runtime `vkGetPhysicalDeviceFormatProperties()` check (already the
mechanism, not new) is a genuine correctness requirement here, not
defensive redundancy.

Fixed resolution (`extent`), never resized on window resize (a shadow
map's own resolution is independent of the final presentable target's
extent — a real, disclosed difference from `HdrColorTarget`/depth
`Texture`, which both resize with the window). Caller-owned,
`std::unique_ptr<ShadowMap>`, created once at Runtime startup,
unconditionally (Risks & Open Questions item 1 in the Spec).

A dedicated `Sampler` for reading it (`Filter::Nearest`,
`AddressMode::ClampToEdge`, matching the output-transform sampler's own
1:1-mapping convention — D-5) is created once at the same point,
caller-owned by Runtime, borrowed into `Renderer::drawFrame()` exactly
like `outputTransformSampler`/`environmentSampler` already are — no new
ownership model.

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

### D-3. One shadow-casting Pipeline; scope, Mesh-layout compatibility, and the precise A-B-A claim

A single, new `shaders/shadow_cast/shadow_cast.slang` pair — a minimal
vertex shader (`output.position = mul(lightViewProjection,
mul(objectToWorld, float4(position, 1.0)))`, reading only the position
attribute from the shared 44-byte mesh vertex stride, mirroring
`minimalMeshVertexLayout()`'s own single-attribute-from-shared-stride
convention) and a trivial, empty fragment shader.

**Corrected fragment-shader claim:** Vulkan itself does not require a
fragment shader stage for a depth-only pipeline — a complete graphics
pipeline needs only its pre-rasterization state (vertex/tessellation/
geometry stage); the fragment stage is genuinely optional for
depth/stencil-only output. This repository's own `Device::createPipeline()`
is what currently requires one: it unconditionally creates a fragment
shader module from `PipelineCreateParams::fragmentShader`
(`vulkan_device.cpp`, `createShaderModule(params.fragmentShader, ...)`
called with no conditional skip) — an existing constraint of this RHI's
own implementation, not of Vulkan. A trivial, empty fragment shader is
the minimal-impact choice given that *existing* constraint; widening
`createPipeline()` to make the fragment stage itself optional is a
separate, larger RHI change this ADR does not make.

One `ObjectToWorldOnly`-shaped push constant, matching
`fallbackMaterial_`'s own shape. Binding 0 is the dedicated light-space
uniform buffer (D-6); no sampled-texture binding
(`sampledTextureBindingCount = 0`, an already-legal value).

**A single, explicit shadow-caster list — decided here, not left to
Plan.** `Renderer::drawFrame()` gains a new parameter,
`shadowCasterDrawItems` (the same span shape as the existing
`drawItems`), **distinct from it.** The shadow pass draws exactly, and
only, the `DrawItem`s in this list; `Renderer` never derives it from
`drawItems` automatically and never filters `drawItems` internally by
inspecting `Mesh` layout (Context — `Renderer` introspects neither
`World` nor `Mesh` internals). This resolves, now, the one question the
prior draft deferred to Plan — whether the shadow pass reuses `drawItems`
or a caller-curated subset: it is always the latter, via one explicit,
independent parameter.

This places exactly two responsibilities on the caller, precisely
because `Renderer` performs neither itself:

1. **Mesh-layout compatibility.** Only include a `DrawItem` in
   `shadowCasterDrawItems` if its `Mesh` matches the shadow Pipeline's
   fixed 44-byte asset-cooked layout (Context). Every entity a real,
   `World`-driven scene produces qualifies, so Runtime's own real usage
   passes `shadowCasterDrawItems = drawItems` unchanged whenever a
   directional light is configured — no filtering logic needed on that
   path. A caller supplying a `DrawItem` built from a different layout
   (as this repository's own `pipeline_depth_write_gpu_tests.cpp`/
   `sky_background_gpu_tests.cpp` already do, for unrelated reasons)
   simply omits it from `shadowCasterDrawItems` while still including it
   in `drawItems` for ordinary drawing — the two lists are independent;
   omission from one has no effect on the other.
2. **The no-directional-light / skip signal.** An **empty**
   `shadowCasterDrawItems` span is the entire signal. Runtime, which
   already knows `directionalLightCount` from World extraction, passes
   an empty span whenever it is 0 — `Renderer` needs no separate boolean
   parameter and performs no light-count inspection of its own. See D-4
   for what the shadow pass still does, unconditionally, when that list
   is empty — an empty list and a "first use" frame are handled by the
   identical mechanism, not two special cases.

**The precise A-B-A claim:** the shadow pass binds exactly one Pipeline
— `shadow_cast`'s own — for its entire duration, whether that bind call
is issued once before its own `DrawItem` loop or redundantly inside it
every iteration; either way, `bindUniformBuffer()`'s own existing
consecutive-same-set skip already handles it correctly (the same
pattern every existing multi-`DrawItem`-same-Material scene in
production already exercises safely). **This claim is scoped to the
shadow pass's own internal call sequence only.** It says nothing about,
and does not change, the pre-existing A-B-A risk already latent in the
*separate* main "draw" pass's own per-`DrawItem` Material/Pipeline
dispatch loop (which can, independent of shadows, revisit an earlier
Pipeline's descriptor set after a different one, if a real scene happens
to interleave materials that way) — that risk is neither introduced,
worsened, nor fixed by this ADR; it remains exactly what it was,
tracked separately.

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

**Unconditional execution — the empty-caster and first-use cases are the
same case, not two.** The shadow pass records every frame, regardless of
`shadowCasterDrawItems`'s own length or `directionalLightCount` —
`Renderer` has no notion of either as a reason to skip the pass itself
(D-3). `beginRendering(ShadowMap&, ClearDepthValue{1.0})` (maximum depth
— "nothing occludes") always runs first; zero or more draws from the
(possibly empty) caster list follow. RenderGraph's own existing
transition machinery — already established for `HdrColorTarget` (this
D-4's own opening paragraph) — carries `ShadowMap` from its prior state
(`UNDEFINED` on the very first frame, `ShaderRead` on every later one)
into `DepthAttachmentReadWrite` before the clear, then into `ShaderRead`
afterward for "draw" to sample; nothing distinguishes frame one from any
other frame, or an empty caster list from a populated one — both are
just "the clear runs, then zero draws happen." This is what keeps
`bindTexture(binding, const ShadowMap&, const Sampler&)` always valid in
"draw": the image is clear-initialized and correctly transitioned every
single frame, whether or not an occluder was actually drawn into it that
frame.

### D-5. Sampling — plain `Sampler2D`, manual comparison, no hardware compare sampler

`SamplerCreateParams` is not widened. The shadow map is sampled with the
dedicated `Sampler` from D-1; the shader reads the stored depth and
compares it against the fragment's own light-space depth directly:

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

### D-6. Light-space matrix — the existing camera buffer for `pbr_direct_lit`/`pbr_ibl`; a second, independent buffer for `shadow_cast`, chosen to avoid shader-layout coupling

The directional light's own `direction` (already extracted into
`FrameLightingData.directionalLights[0]`) drives a fixed-size
orthographic projection — center, half-extent, near/far are Runtime
constants (Plan-stage values), **not** dynamically fit to scene/camera
bounds each frame (the first step toward cascaded shadow maps,
explicitly out of scope).

`pbr_direct_lit.slang`/`pbr_ibl.slang` already read their own
camera/lighting data from a buffer bound at their own Pipeline's binding
0 (`bindUniformBuffer()` binds exactly one buffer per Pipeline, Context)
— appending the light-space `view`/`projection` pair (128 bytes) to the
tail of that same buffer is the only way these two shaders gain the new
data without a second binding on their own Pipeline. For them
specifically this is not a design choice: they already occupy binding 0
with data they still need, so the tail is the only route the existing
single-uniform-binding contract offers, mirroring Spec 0025 P3's own "SH
coefficients appended after the existing region, shaders only
declare/read their own current prefix" precedent.

`pbr_ibl.slang` already declares fields through byte 464 (its own SH
tail) — the light-space pair appends directly after, at 464, for 592
bytes total. `pbr_direct_lit.slang` today declares fields only through
byte 320 (`cameraWorldPosition`/`_pad2`) — it never reaches the SH
region at all. To keep the light-space pair at the **same 464-byte
offset in both shaders** (a single, consistent layout, not two
diverging ones), `pbr_direct_lit.slang` must also declare the
intervening 144 bytes as explicit, named, unused padding (matching this
codebase's own "explicit padding, never implicit" discipline already
used for `_pad0`/`_pad1`/`_pad2`) before its own light-space fields —
one further real, disclosed content addition to that shader, on top of
D-7's own shadow-sampling logic.

`shadow_cast.slang` is a **different Pipeline**, with its own,
independent binding 0 — and here there genuinely is a choice, not a
forced outcome. Nothing in `bindUniformBuffer()`'s own one-buffer-
*per-Pipeline* limit prevents binding the *same* 592-byte buffer object
to `shadow_cast`'s own binding 0 as well — a `Buffer` resource can be
bound to more than one Pipeline's own descriptor set; the limit is one
buffer per Pipeline, not one consumer per buffer. The two real options:

- **(Rejected) Share the existing 592-byte camera/lighting buffer.**
  `shadow_cast.slang` would need to declare the full leading 464 bytes
  (camera matrices, point lights, SH) as unused padding purely to reach
  its own two matrices at the tail — real, avoidable coupling between a
  minimal, depth-only shader and a layout shaped entirely by unrelated
  `pbr_direct_lit`/`pbr_ibl` concerns it has nothing to do with. Saves
  exactly one small `Buffer` object, at the cost of that coupling.
- **(Recommended) A second, small (128-byte), independent Buffer** —
  `view`/`projection` only — created once at Runtime startup alongside
  the main camera buffer, written with the identical light-space values
  every frame the main buffer's own tail is written with.

The second option is the recommended design: a second small `Buffer` is
a negligible resource cost next to the real, layout-level coupling a
future change to `pbr_direct_lit`/`pbr_ibl`'s own point-light or SH
region would otherwise create for an unrelated depth-only shader. This
is a deliberate choice made for shader-layout hygiene, not a consequence
the RHI imposes — no `CommandList`/`Device` capability is added or
widened by either option, and both work, unmodified, against today's
RHI; widening `bindUniformBuffer()` to accept a second binding (a
larger, separate RHI change) is not needed by either and remains
rejected (Alternatives Considered).

**No-directional-light behavior:** when `directionalLightCount == 0`,
neither buffer's own light-space region is ever read by `pbr_direct_lit`/
`pbr_ibl` (D-7's own gating), and the shadow pass itself does not draw
any occluders that frame (D-7). Runtime still writes a deterministic
value to both buffers every frame — **the identity matrix, not zero.**
A zero-valued 4x4 "projection" matrix is singular (non-invertible) and
can produce `NaN`/`Inf` in vertex output even where the *result* is
never sampled downstream; identity keeps the vertex math well-defined
in the (currently unreachable, but not asserted-impossible) case
anything ever executes against it. This corrects
`writeEnvironmentIrradianceSh()`'s own all-zero precedent, which is safe
for irradiance coefficients (zero contribution is mathematically benign)
but not for a projection matrix (zero is degenerate).

### D-7. `pbr_direct_lit.slang`/`pbr_ibl.slang` — the real, disclosed content, descriptor, capacity, compatibility, and golden consequences

Both shaders gain one new binding (the shadow map sampler, at the next
free slot — binding 2 for `pbr_direct_lit`, today at binding 1 only;
binding 4 for `pbr_ibl`, today at bindings 1–3), D-6's own light-space
tail, and D-5's shadow-factor multiply inside their own existing
directional-light loop. This is the same class of disclosed
shader-content change ADR-0068 D-7 already made to
`lit_textured.slang`/`pbr_direct_lit.slang` (removing their final clamp)
— the first Spec since ADR-0068 unable to claim "zero further `.slang`
file modified" for these two files. Both shaders' own descriptor
contract functions (`pbrDirectLitExpectedDescriptorContract()`/
`pbrIblExpectedDescriptorContract()`) gain the new binding entry;
`sampledTextureBindingCount` moves from 1→2 (`pbr_direct_lit`) and
3→4 (`pbr_ibl`).

**Full descriptor-extension disclosure (three separate, real limits,
all requiring a widening — Context):**

1. `Device::createPipeline()`'s own closed
   `sampledTextureBindingCount` check widens from `{0, 1, 3}` to
   `{0, 1, 2, 3, 4}` — `2` (`pbr_direct_lit` + shadow) and `4`
   (`pbr_ibl` + shadow) are the two new legal values this ADR needs;
   `3` remains legal (harmless to keep, even though no current consumer
   uses it after this change) rather than narrowed, avoiding an
   unrelated, unreviewed removal.
2. `createDescriptorPoolOfSize()`'s own `COMBINED_IMAGE_SAMPLER` sizing
   widens from `3U * maxSets` to `4U * maxSets` — the real, new
   per-set sampler worst case (`pbr_ibl` + shadow).
3. `VulkanCommandList::textureDescriptorMemos_` widens from
   `std::array<TextureDescriptorMemo, 4>` to
   `std::array<TextureDescriptorMemo, 5>` — without this, `bindTexture()`
   binding index 4 (`pbr_ibl`'s own new shadow slot) fails its own
   `ATLANTIS_CHECK(binding < textureDescriptorMemos_.size())` outright.
   This is an internal `VulkanCommandList` capacity bump, not a public
   API change.

**Descriptor-set-count consequence (distinct from the sampler-per-set
budget above):** the shadow Pipeline (D-1/D-3) is one more fixed,
never-rebuilt Pipeline, created once and always present, on top of
Plan 0026's own `fallbackMaterial_`/output-transform/sky set. Plan
0026's own re-derived formula (`N+3` steady state, `N+4` peak, N=6
stress-proven) becomes `N+4` steady state (`N` materials + fallback +
output-transform + sky + shadow_cast) / `N+5` peak — Plan 0027 must
re-run the same N=6 stress proof against this real, new count, on top of
the sampler-budget re-derivation above.

**Compatibility path — a real, disclosed migration, not assumed
automatic:** because both shaders' own new sampled-texture binding is
declared unconditionally at Pipeline-creation time (not toggled per
draw), **every existing call site that creates a `PbrDirectLit`/PBR-IBL
Material must also supply a real, valid `ShadowMap` and its `Sampler`
— a Material whose Pipeline declares a combined-image-sampler binding
that is never written before a draw is a real Vulkan correctness
violation, not a graceful no-op.** This is not limited to Runtime's own
main path. Plan 0027 must audit and update every one of: Runtime
(`runtime_application.cpp`), `pbr_material_demo_fixture.cpp`/
`ibl_material_demo_fixture.h` (via the shared fixture), the golden
generator (`golden_generator/pbr_material_demo_main.cpp`), and every
GPU test that constructs a `PbrDirectLit`/PBR-IBL `Material` directly
(`tests/runtime/pbr_render_gpu_tests.cpp`,
`tests/runtime/material_realization_gpu_tests.cpp`,
`tests/runtime/material_ibl_selection_gpu_tests.cpp`) — even where no
actual occluder-casting behavior is being tested, each needs a real
(even if trivially empty) `ShadowMap`/`Sampler` pair to remain valid.
This is a real, sizable, disclosed cost of modifying the two existing
shaders in place (see Alternatives Considered for the rejected
per-variant alternative and why this ADR still prefers the in-place
change).

**Corrected golden strategy — compare first, never presume:** the
shadow term only executes inside the existing `directionalLightCount`
loop; a scene with no directional light never reaches it, and its own
rendered pixels are provably unaffected by this shader change,
regardless of the new binding's mere presence in the descriptor layout.
`ibl_material_demo` has no directional light by its own existing design
("renders four environment-lit PBR spheres without direct lights",
Spec 0025) — **its golden must remain byte-identical; it is not a
re-capture candidate.** `pbr_material_demo`/`hdr_roll_off_demo` do have
a directional light, but whether their own existing geometry happens to
cast a self-shadow the shader can detect is a real, scene-geometry
question this ADR does not assume an answer to. Plan 0027's own process
is: implement, then run every existing capture-compare test unmodified;
any golden that comes back byte-identical is confirmed unaffected and
needs no further action; a human-reviewed re-capture (ADR-0042) is
requested only for a golden that actually shows a difference — never
presumptively, for a scene whose own geometry has not been shown to
self-shadow.

**Discriminating verification that shadows do not affect IBL/sky — three
renders of one scene, never toggling `directionalLightCount` to bypass
the shadow path itself:**

- **R1 (shadowed):** environment configured, `directionalLightCount = 1`,
  a real occluder included in `shadowCasterDrawItems` (D-3) — the shadow
  path genuinely executes; the shadow factor is ≈0 at a chosen receiver
  sample point `P` the occluder blocks from the light.
- **R2 (unshadowed control, same nonzero light):** identical scene,
  camera, light, environment, and geometry — the occluder is still drawn
  as an ordinary `DrawItem` (every camera-visible pixel stays
  geometrically identical to R1) — but `shadowCasterDrawItems` is empty
  (D-3/D-4's own clear-to-far rule applies), so the shadow factor is ≈1
  everywhere, including at `P`. `directionalLightCount` stays 1 in both
  R1 and R2 — the light is never turned off to produce this comparison.
- **R3 (light off, IBL/ambient reference):** identical scene, camera,
  environment, and geometry, `directionalLightCount = 0` — the direct
  term is structurally absent (the existing light-count loop gates it),
  leaving only IBL/ambient at every pixel, including `P`.

Two real, predicted-direction comparisons (never "the image changed"
alone):

1. **Positive control — shadows take effect under real, nonzero direct
   light:** `R1[P]` is measurably darker than `R2[P]`. The only
   difference between these two renders is whether a real occluder's
   depth reached the shadow map, proving the shadow factor genuinely
   attenuates the direct term while the light is genuinely on.
2. **IBL isolation:** `R1[P]` closely matches `R3[P]`. At `P`, R1's own
   direct term is driven to (near) zero by the shadow factor, leaving
   only IBL/ambient — which should match R3's own light-off render of
   the identical point almost exactly. A bug that let the shadow factor
   leak into the IBL/specular-IBL terms (outside the modified loop, this
   D-7's own opening paragraph) would make `R1[P]` measurably darker than
   `R3[P]`, not merely close to it — this catches exactly that failure
   mode without needing to isolate the IBL term analytically.

Exact sample point(s), scene geometry, and pass/fail thresholds are
Plan-stage values (mirroring `kShadowBias`'s own precedent, D-5) — this
ADR fixes the three-render, two-comparison mechanism, not the numbers.

Non-PBR goldens (`minimal_cube`, `world_scene`, `textured_quad`,
`material_demo`, `lighting_demo`) are untouched — neither shader they use
(`minimal_mesh`, `textured_quad`, `lit_textured`) is modified by this
ADR.

## Consequences

### Positive

- Shadow-map write/sample duality is available through a backend-neutral
  RHI contract, matching `HdrColorTarget`'s own established pattern —
  not a new kind of resource-ownership problem for this codebase.
- The shadow pass's own single-Pipeline draw loop is provably free of
  the pre-existing `bindUniformBuffer()` A-B-A pattern, without fixing
  or depending on a fix for it, scoped precisely to that pass's own call
  sequence (D-3).
- No new Sampler capability, no PCF/cascade infrastructure — the closed,
  minimal slice the Spec asks for.
- Shadows are structurally confined to the one directional light's own
  direct term; IBL diffuse/specular and the sky pass are provably
  untouched (outside the modified loop and outside the modified shaders
  entirely) — and this is verified by a real discriminating test (D-7),
  not asserted alone.
- `ibl_material_demo`'s golden is confirmed unaffected by construction
  (no directional light) — not swept into a blanket re-capture.

### Negative / Trade-offs

- A real, disclosed public RHI surface: one new resource type
  (`ShadowMap`), one new `PipelineCreateParams` field
  (`hasColorAttachment`), three new/overloaded `CommandList` methods, one
  new `ResourceBinding` field, one new `Device` factory method, and three
  separate internal capacity widenings (the closed sampled-binding-count
  set, descriptor-pool sampler sizing, `textureDescriptorMemos_`).
- A real, disclosed content change to two already-shipped shaders
  (`pbr_direct_lit.slang`, `pbr_ibl.slang`), including an explicit
  144-byte padding declaration in `pbr_direct_lit.slang` purely to keep
  a consistent tail offset with `pbr_ibl.slang`.
- A real, sizable migration: every existing call site creating a
  `PbrDirectLit`/PBR-IBL Material — Runtime, three fixture/generator
  files, three GPU test files — must be updated to supply a real
  `ShadowMap`/`Sampler`, whether or not it exercises actual shadowing.
- Descriptor-set-count re-derivation (`N+4`/`N+5`, on top of the
  sampler-per-set budget change) and its own N=6 stress re-proof.
- A fixed, non-scene-fitted orthographic shadow volume is a real quality
  ceiling (coverage is a Runtime constant, not adaptive) — acceptable
  for a first slice, explicitly not cascaded/adaptive.
- One more new production shader pair (`shadow_cast`), on top of the two
  modified existing ones, and a second, small, duplicated light-space
  buffer (D-6).
- `Renderer::drawFrame()` gains a fifth new, caller-owned parameter
  (`shadowCasterDrawItems`, D-3) beyond the shadow map, its sampler, the
  shadow Pipeline, and the dedicated light-space buffer — every caller
  must now pass two related-but-independent `DrawItem` spans instead of
  one.

## Alternatives Considered

- **Widen `Texture`/`SampledTexture` instead of a new `ShadowMap` type.**
  Rejected for the identical reason ADR-0068 rejected widening
  `RenderTarget`/`SampledTexture` for `HdrColorTarget`: it conflates two
  purpose-built contracts rather than keeping both intact.
- **Hardware depth-compare sampling (`VkSamplerCreateInfo::compareEnable`).**
  Rejected for this minimal slice — a real new RHI `Sampler` capability
  for a benefit this ADR's own "no soft-shadow system" scope does not
  need; a manual compare in the shader is exactly as correct for a
  single hard-edged tap.
- **Dynamic, camera-frustum-fitted shadow volume (the first CSM step).**
  Rejected — explicitly out of scope; a fixed volume is the smallest
  slice that produces a real, verifiable shadow.
- **A separate shadow-enabled shader variant, leaving the existing two
  shaders byte-for-byte untouched.** Rejected — the combinatorial
  variant count (direct/direct+shadow/ibl/ibl+shadow) is a real
  complexity cost; the two existing shaders' own real, disclosed, and
  now fully-itemized modification (D-6/D-7) — including its own real
  migration cost across every existing call site — is judged the smaller
  overall change, but this trade-off is named explicitly for Human
  Review to weigh, not decided unilaterally as obviously correct.
- **A second `bindUniformBuffer()` binding, instead of appending to the
  existing buffer plus a second, duplicated buffer for `shadow_cast`.**
  Rejected — widening `CommandList`'s own uniform-binding contract to
  support more than one buffer per Pipeline is a larger, separate RHI
  change with its own caller-migration cost, for a benefit (avoiding one
  small, disclosed 128-byte duplication) this minimal slice does not
  need.
- **Fixing `bindUniformBuffer()`'s own A-B-A constraint as part of this
  ADR.** Rejected — out of scope per explicit direction; D-3's
  single-Pipeline shadow pass does not need it, and fixing it is
  tracked separately as independent follow-up work, unrelated to this
  ADR's own main "draw" pass, which carries the same, pre-existing,
  unrelated risk today, unaffected by this change either way.
- **Have `Renderer` derive the shadow-caster list from `drawItems`
  itself** (e.g. by inspecting each `Mesh`'s own vertex stride and
  silently excluding mismatches), instead of requiring an explicit,
  caller-supplied `shadowCasterDrawItems` (D-3). Rejected — this would
  require `Renderer` to introspect `Mesh` internals to make a drawing
  decision on its own, which ADR-0022's own stateless-orchestrator
  contract does not permit (Context); an explicit, caller-owned second
  list keeps `Renderer` ignorant of `Mesh` layout details, at the cost
  of one more parameter every caller must pass.
