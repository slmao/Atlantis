# Plan: Directional Shadow Foundation

- **Spec:** [specs/0027-directional-shadow-foundation.md](../specs/0027-directional-shadow-foundation.md) (`Approved`)
- **Status:** Draft
- **Author:** slmao

## Objective

Implement Spec 0027 / ADR-0072: one directional light's own shadow on
`PbrDirectLit`/`pbr_ibl` surfaces — a new `ShadowMap` RHI resource, a
depth-only `shadow_cast` Pipeline and RenderGraph pass, a manual
shadow-comparison term inside both shaders' existing directional-light
loop, and the real, disclosed migration this requires across every
existing caller — while leaving IBL/sky/every non-PBR golden untouched.

## Plan-Stage Decisions

Each closes one ADR-0072 mechanism choice against the real, current code
(verified by direct reading this Plan's own research pass, not assumed)
and must not be left to be picked opportunistically during
Implementation.

### P1 — `ShadowMap` RHI resource: mirrors `HdrColorTarget` exactly

```cpp
// src/rhi/include/atlantis/rhi/types.h
struct ShadowMapCreateParams {
  Extent2D extent;
  DepthFormat format = DepthFormat::D32Sfloat;
};

enum class ShadowMapCreateError {
  FormatFeaturesUnsupported,
  AllocationFailed,
  ImageCreationFailed,
  ImageViewCreationFailed,
};
```

```cpp
// src/rhi/include/atlantis/rhi/shadow_map.h (new)
class ShadowMap {
 public:
  virtual ~ShadowMap() = default;
  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual DepthFormat format() const = 0;
};
```

`Device::createShadowMap(const ShadowMapCreateParams&) -> Result<std::unique_ptr<ShadowMap>, ShadowMapCreateError>`,
inserted into the abstract `Device` interface immediately after
`createHdrColorTarget()` (`device.h`).

**Vulkan implementation** — `VulkanShadowMap` mirrors `VulkanHdrColorTarget`
(`vulkan_hdr_color_target.h`) field-for-field: owns its own `VkImage`/
`VkDeviceMemory`/`VkImageView`, non-copyable/non-movable,
`image()`/`imageView()` accessors for Vulkan-Backend-internal use only.
`VulkanDevice::createShadowMap()` mirrors `createHdrColorTarget()`
(`vulkan_device.cpp:1461-1541`) exactly, with three differences:

- `imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;`
  (not `COLOR_ATTACHMENT_BIT | SAMPLED_BIT`).
- `viewCreateInfo.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};`
  (not `ASPECT_COLOR_BIT`).
- Format capability check calls a new, standalone
  `hasRequiredShadowMapFeatures(VkFormatFeatureFlags)` (new
  `shadow_map_capability.h`/`.cpp`, mirroring
  `hdr_color_target_capability.h`/`.cpp` exactly):
  ```cpp
  bool hasRequiredShadowMapFeatures(VkFormatFeatureFlags optimalTilingFeatures) {
    constexpr VkFormatFeatureFlags kRequired =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (optimalTilingFeatures & kRequired) == kRequired;
  }
  ```
  **This check is a genuine correctness requirement, not defensive
  redundancy** (ADR-0072 D-1's own corrected claim): unlike `D32_SFLOAT`'s
  unconditionally-guaranteed `SAMPLED_IMAGE_BIT`,
  `DEPTH_STENCIL_ATTACHMENT_BIT` is guaranteed only collectively across
  `{D32_SFLOAT, X8_D24_UNORM_PACK32}` — this exact combination on
  `D32_SFLOAT` specifically is not spec-guaranteed. (Aside, not fixed by
  this Plan: `types.h:85-88`'s own existing `DepthFormat::D32Sfloat`
  comment — "guaranteed ... per the Vulkan spec's mandatory format
  support table; no capability query needed" — is itself imprecise in
  the identical way ADR-0072 D-1 corrected; that comment guards the
  plain depth-`Texture` path, which this Plan does not touch, so it is
  left as pre-existing, out-of-scope text, not silently rewritten here.)
- `toShadowMapCreateError(VkResult)` mirrors `toHdrColorTargetCreateError()`
  (`vulkan_result.cpp:129-132`) — collapses every non-success `VkResult`
  to `ShadowMapCreateError::ImageCreationFailed`, same precedent.

Fixed resolution (P4), never resized on window/extent change — a real,
disclosed difference from `hdrColorTarget_`/`depthTexture_`, which both
resize. Created once at Runtime startup, unconditionally (Spec 0027
Risk 1). A dedicated `Sampler` (`Filter::Nearest`, `AddressMode::ClampToEdge`,
P4) is created at the same point via the existing `createSampler()` —
no RHI change.

### P2 — Depth-only Pipeline: `hasColorAttachment`, and a real new `beginRendering()` overload

`PipelineCreateParams` gains one new, additive field, appended as the
struct's new last member (mirroring `depthWriteEnabled`'s own Plan 0026
P4 append-at-end precedent exactly):

```cpp
// PipelineCreateParams, immediately after depthWriteEnabled:
bool hasColorAttachment = true;
```

`VulkanDevice::createPipeline()` (`vulkan_device.cpp:1212-1232`):

```cpp
VkFormat colorFormat = VK_FORMAT_UNDEFINED;
if (params.hasColorAttachment) {
  colorFormat = std::visit([](auto format) { return toVkFormat(format); }, params.colorFormat);
}
const VkFormat depthFormat = params.hasDepthAttachment ? toVkFormat(params.depthFormat) : VK_FORMAT_UNDEFINED;

VkPipelineRenderingCreateInfo renderingCreateInfo{};
renderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
renderingCreateInfo.colorAttachmentCount = params.hasColorAttachment ? 1 : 0;
renderingCreateInfo.pColorAttachmentFormats = params.hasColorAttachment ? &colorFormat : nullptr;
renderingCreateInfo.depthAttachmentFormat = depthFormat;
```

The `std::visit(toVkFormat, params.colorFormat)` call is skipped entirely
when `hasColorAttachment == false` — `colorFormat` defaults to
`Format::Unknown` (`types.h:27-33`), and this avoids depending on
whatever `toVkFormat(Format::Unknown)` does today (not read this pass;
irrelevant once unreached).

**Disclosed gap, not resolved by this Plan's own research:** this
codebase's `VkPipelineColorBlendStateCreateInfo` construction inside
`createPipeline()` was not read this pass. Milestone 1 must locate it
and gate its `attachmentCount`/`pAttachments` the same way
`colorAttachmentCount` is gated above (`attachmentCount = 0`, no
attachment entries, when `hasColorAttachment == false`) — Vulkan
requires the two counts to agree. This is flagged explicitly rather than
guessed at, per this Plan's own "no fabricated line numbers" discipline.

**A genuinely new `CommandList::beginRendering(ShadowMap&, ClearDepthValue)`
overload — not a trivial parameterization.** Both existing
`beginRendering()` overloads (`vulkan_command_list.cpp:169-233`,
`239-286`) unconditionally attach a color image and hard-code
`renderingInfo.colorAttachmentCount = 1`. The new overload is written
from scratch, modeled on their structure with the color portion omitted
entirely: `renderingInfo.colorAttachmentCount = 0`,
`pColorAttachments = nullptr`; one `VkRenderingAttachmentInfo` for the
`ShadowMap`'s own `imageView()` (`loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR`,
`clearValue.depthStencil.depth = <ClearDepthValue>`,
`storeOp = VK_ATTACHMENT_STORE_OP_STORE`) as `pDepthAttachment`;
`vkCmdSetViewport`/`vkCmdSetScissor` sized to the `ShadowMap`'s own
`extent()` (not the frame's final-target extent — the two are
independent, P1). A new `ClearDepthValue` type (a `float`-wrapping
struct or a plain `float`, matching this codebase's existing
`ClearColorValue`'s own shape) is added alongside it if one does not
already exist under that name (not confirmed this pass — `float
depthClear` is already used positionally elsewhere, e.g.
`beginRendering(RenderTarget&, Texture*, ClearColorValue, float)`; reuse
that plain-`float` convention rather than inventing a wrapper type
unless the existing `CommandList` interface already establishes one).

`CommandList` also gains `transitionResource(ShadowMap&, ResourceState,
ResourceState)` (mirrors the existing four `transitionResource()`
overloads, `vulkan_command_list.cpp:52-146`, using
`VK_IMAGE_ASPECT_DEPTH_BIT` like `Texture`'s own overload) and
`bindTexture(std::uint32_t binding, const ShadowMap&, const Sampler&)`
(mirrors `bindTexture(const SampledTexture&, ...)`'s own **memoized**
shape, `vulkan_command_list.cpp:353-403` — not `bindTexture(const
HdrColorTarget&, ...)`'s unmemoized, once-per-frame shape — because the
shadow-map binding is re-issued once per `PbrDirectLit`/`pbr_ibl`
`DrawItem`, the same repeated-per-draw pattern the base-color sampler
already has, not the output-transform pass's own once-per-frame
pattern).

### P3 — `shadow_cast.slang`: genuinely position-only, reusing the real asset vertex layout

The first genuinely single-attribute vertex schema in this codebase —
every existing "minimal" layout (`minimalMeshVertexLayout()`,
`runtime_application.cpp:133-141`, and its ~13 independent test-file
copies) is actually position+color, dual-attribute. `shadow_cast.slang`'s
own schema reads **only** position, at the real, shared
`kMeshArtifactPositionOffsetBytes = 0` / `kMeshArtifactVertexStrideBytes = 44`
offsets already defined in `asset_system/mesh_artifact.h` — not a new
local `Vertex` struct, and not `minimalMeshVertexLayout()`'s own
position+color pair:

```cpp
[[nodiscard]] std::optional<VertexInputLayout> shadowCastVertexLayout(const ReflectionMetadata& vertexMetadata) {
  const std::vector<MeshVertexAttributeSchema> schema = {
      MeshVertexAttributeSchema{.location = 0, .offsetBytes = atlantis::asset_system::kMeshArtifactPositionOffsetBytes},
  };
  auto result = toVertexInputLayout(vertexMetadata, schema, atlantis::asset_system::kMeshArtifactVertexStrideBytes);
  if (result.isErr()) return std::nullopt;
  return result.value();
}
```

`shaders/shadow_cast/shadow_cast.slang`:

```slang
struct VertexInput {
  [[vk::location(0)]] float3 position;
};

struct LightSpaceUniform {
  float4x4 view;
  float4x4 projection;
};
[[vk::binding(0, 0)]]
ConstantBuffer<LightSpaceUniform> lightSpace;

struct PushConstants {
  float4x4 objectToWorld;
};
[[vk::push_constant]]
ConstantBuffer<PushConstants> pushConstants;

[shader("vertex")]
float4 vertexMain(VertexInput input) : SV_Position {
  float4 worldPos = mul(pushConstants.objectToWorld, float4(input.position, 1.0));
  return mul(lightSpace.projection, mul(lightSpace.view, worldPos));
}

[shader("fragment")]
void fragmentMain() {}
```

`fragmentMain()` returns `void` with no `SV_Target` — matching
`hasColorAttachment = false` (P2): a depth-only pipeline has no color
attachment for an `SV_Target` output to write into. `PipelineCreateParams`:
`vertexInputLayout` from `shadowCastVertexLayout()`, `pushConstantSizeBytes = sizeof(float)*16`
(one 4x4 matrix, `MaterialPushConstantLayout::ObjectToWorldOnly`-shaped —
no new push-constant layout enumerator), `sampledTextureBindingCount = 0`,
`hasCameraUniformBinding = true` (binding 0 = the dedicated light-space
buffer, P5 — this field's name is historical, not semantic; it only
gates "is there a uniform buffer at binding 0," which is exactly what
the light-space buffer needs), `hasDepthAttachment = true`,
`depthWriteEnabled = true` (default — this pass's entire purpose is
writing real depth), `hasColorAttachment = false` (P2), `depthFormat = DepthFormat::D32Sfloat`.

**Descriptor contract** — new `shadowCastExpectedDescriptorContract()`
(`descriptor_contract.h`/`.cpp`), recommended shape:

```cpp
std::vector<DescriptorBinding> shadowCastExpectedDescriptorContract() {
  return {DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex}};
}
```

One entry, Vertex-stage only (the fragment shader has no bindings at
all). **This is a recommendation, not yet confirmed against real
reflection** — Milestone 4 must run the real `slangc` toolchain against
`shadow_cast.slang` and compare, mirroring Plan 0026 P3's own
established "disposable probe, real `-reflection-json` output, `used:0`
entries drop out" verification exactly. `compile_and_validate.cpp`
(`compile_and_validate.cpp:136-171`) gains one new
`else if (expectedContract == "shadow-cast") { fullContract = shadowCastExpectedDescriptorContract(); }`
arm; `validatePushConstantsForVertexStage()`
(`compile_and_validate.cpp:193-212`) needs **no new case** — `"shadow-cast"`
falls into the existing `else` branch (`isPbr = false`, `expectedSizeBytes = sizeof(float)*16 = 64`),
already correct. The top-level dispatch's `validatePushConstantsForFragmentStage()`
call is gated on `"pbr-direct-lit"`/`"pbr-ibl"` only (line ~368) — already
correctly excludes `"shadow-cast"`, no change needed there.

### P4 — Numeric values: resolution, orthographic volume, bias (for Human Confirmation)

**These are reasoned, derived starting values — not yet measured against
a real captured shadow.** Implementation's own real-GPU verification
(Milestone 8/9) is expected to confirm or adjust them; this Plan fixes
concrete numbers so none is picked opportunistically in code, per the
task's own "propose one recommended value, do not defer to
Implementation" requirement, while disclosing plainly that "recommended"
here means "derived from real scene geometry and D32 precision
reasoning," not "empirically tuned."

- **Resolution: 1024×1024, `DepthFormat::D32Sfloat`.** Derivation:
  proportionate to this repository's own established 512×512
  image-regression golden resolution (`kPbrMaterialDemoExtentPixels`) —
  roughly 2× oversampling headroom for a hard-edged (no PCF) shadow test
  edge, without the memory/bandwidth cost of 2048² a foundation feature
  does not need.
- **Fixed orthographic volume: center `(0, 0, 0)`, half-extent `8.0`
  world units (a 16×16×16 light-space box), `near = 0.1`, `far = 30.0`.**
  Derivation, against real, existing scene geometry (not invented):
  `pbr_material_demo.scene.txt`'s four spheres span `x,y ∈ [-2.3, 2.3]`
  (`±1.3` center offset + ~`1.0` sphere radius); `world_scene.scene.txt`'s
  cubes span roughly `x ∈ [-3.0, 3.0]`; the new shadow-demonstration
  scene (P10) is sized to a `12×12`-unit ground plane, `x,z ∈ [-6, 6]`.
  A `±8` half-extent comfortably covers all three with margin, as one
  single Runtime constant applied uniformly (never scene-fitted, per
  Non-Goals) — not tuned per-scene. `far = 30.0` covers the ground
  plane's own light-space depth range under a steep light angle with
  margin; `near = 0.1` matches every other fixed near-plane literal
  already used in this codebase (`kNearZ`).
- **`kShadowBias = 0.0015`**, compared directly in the `[0, 1]`
  NDC-depth space D-5's own formula already operates in (`shadowNdc.z -
  kShadowBias <= storedDepth`). Derivation: the orthographic projection's
  depth mapping is linear (unlike a perspective projection's nonlinear
  one), so `0.0015` over a `29.9`-unit near-far range corresponds to
  approximately `4.5 cm` of world-space depth slack — comparable to
  conventional shadow-acne-avoidance biases relative to this scene
  family's own `~1-2` unit object scale (sphere radius `~1.0`, occluder
  cube `~1.0`). No slope-scaled or normal-offset term (Non-Goals).

### P5 — Light-space data: two buffers, exact offsets, write timing, identity sentinel

`pbr_direct_lit.slang`/`pbr_ibl.slang` extend their **existing** binding-0
camera buffer — the only route available to them (ADR-0072 D-6). Real,
confirmed current byte layout (`pbr_ibl.slang:26-37`; `pbr_direct_lit.slang`
is identical through `cameraWorldPosition`/`_pad2`):

| Offset | Field | Size |
|---|---|---|
| 0 | `view` | 64 |
| 64 | `projection` | 64 |
| 128 | `directionalLightCount` | 4 |
| 132 | `pointLightCount` | 4 |
| 136 | `_pad1` (uint2) | 8 |
| 144 | `directionalLights[1]` | 32 |
| 176 | `pointLights[4]` | 128 |
| 304 | `cameraWorldPosition` | 12 |
| 316 | `_pad2` | 4 |
| 320 | `irradianceSh[9]` (`pbr_ibl.slang` only) | 144 |
| **464** | **`lightSpaceView` + `lightSpaceProjection`** (new, both shaders) | **128** |

`pbr_direct_lit.slang` today declares fields only through byte 320 — it
gains an explicit, named `float4 _shadowPad[9];` (144 bytes, matching
this codebase's own "explicit padding, never implicit" discipline) to
reach the same 464-byte tail offset `pbr_ibl.slang` already has via its
real `irradianceSh[9]`, then the identical 128-byte `lightSpaceView`/
`lightSpaceProjection` pair. Both shaders' camera buffer grows from its
current size (320 / 464 bytes) to **592 bytes**; `cameraBuffer_`'s own
creation (`runtime_application.cpp`'s Step 4, currently
`sizeBytes = 464`) becomes `sizeBytes = 592`, and every fixture/test rig
creating its own camera `Buffer` with a hard-coded size literal is
updated identically (Milestone 7's own file list).

`shadow_cast.slang` gets a **second, independent, 128-byte** `Buffer`
(view+projection only, P3's `LightSpaceUniform`) — a deliberate choice
for shader-layout hygiene (ADR-0072 D-6's own corrected framing: sharing
the 592-byte buffer across Pipelines is technically legal, since
`bindUniformBuffer()`'s one-buffer-*per-Pipeline* limit does not forbid
binding the same buffer object to more than one Pipeline's own binding
0; it is rejected only because it would force this minimal shader to
declare ~464 bytes of fields it never reads). Both buffers are Runtime-
owned (`std::unique_ptr<Buffer>`), created once at startup alongside
`cameraBuffer_`, and **written every frame with the identical
view/projection values**, at the point `runFrame()` already writes the
main camera buffer (mirroring `extractFrameLightingData()`'s own
per-frame write timing, `runtime_application.cpp`).

**No-directional-light sentinel: the identity matrix, in both buffers**
(not the SH-coefficient precedent's all-zero — a zero 4×4 "projection"
matrix is singular and can produce `NaN`/`Inf` in vertex-shader math even
where the result is never sampled downstream). Written unconditionally,
every frame, regardless of `directionalLightCount`.

**Light-space view/projection derivation:** view = `lookAtMatrix` toward
the one directional light's own `direction` (already extracted into
`FrameLightingData.directionalLights[0].direction`,
`extractFrameLightingData()`, `scene_extraction.cpp:195-263`), positioned
at `P4`'s fixed center minus `direction * (far - near) / 2` (placing the
light's own near plane just outside the fixed volume), up-vector
`(0,1,0)` unless `|direction·up| > 0.999` (near-vertical light), in
which case `(0,0,1)` (matching this codebase's own degenerate-up
handling precedent, if one already exists in `lookAtMatrix()` — verify
at Implementation time rather than assumed here). Projection = a fixed
orthographic matrix built directly from P4's own center/half-extent/
near/far constants — never from `extractCameraMatrices()`'s own
perspective path.

### P6 — `Renderer::drawFrame()`: five new required parameters, a new "shadow" pass, and binding-index-aware shadow-map binding

Current, confirmed signature (`renderer.h:84-92`):

```cpp
void drawFrame(CommandList& commandList, RenderTarget& colorTarget, Texture& depthTarget,
               Buffer& cameraUniformBuffer, std::span<const DrawItem> drawItems, ResourceState finalColorState,
               HdrColorTarget& hdrColorTarget, Buffer& fullscreenTriangleVertexBuffer,
               Buffer& fullscreenTriangleIndexBuffer, Pipeline& outputTransformPipeline,
               Sampler& outputTransformSampler, const EnvironmentLighting* environmentLighting = nullptr,
               Pipeline* skyPipeline = nullptr);
```

New signature — five parameters appended after `skyPipeline`, **all
required, non-nullable references** (not `skyPipeline`'s nullable-pointer
shape) — because shadow infrastructure is unconditionally created (P1),
mirroring `hdrColorTarget`/`outputTransformPipeline`/
`outputTransformSampler`'s own required-reference precedent instead:

```cpp
               ShadowMap& shadowMap, Sampler& shadowMapSampler, Pipeline& shadowCastPipeline,
               Buffer& shadowLightSpaceBuffer, std::span<const DrawItem> shadowCasterDrawItems);
```

**`shadowCasterDrawItems` is genuinely independent from `drawItems`**
(ADR-0072 D-3/Spec 0027 "For Human Confirmation" item 6) — `Renderer`
never derives one from the other and never inspects `Mesh` layout;
Runtime passes `shadowCasterDrawItems = drawItems` whenever a directional
light is configured (every real, `World`-driven mesh already qualifies,
P3), an empty span otherwise — the sole no-directional-light signal.

**New RenderGraph pass, "shadow," declared and compiled first** — before
the existing "draw" pass, which now also **reads** `shadowMap`
(`ShaderRead`), the identical write-then-read single-producer pattern
already proven for `hdrResource` (written by "draw," read by
"output_transform," `renderer.cpp:42-44,126-134`). Three passes total:
`"shadow"` (writes `shadowMap`) → `"draw"` (reads `shadowMap` [new],
writes `hdr_color`+`depth`) → `"output_transform"` (unchanged).

Shadow pass execute callback — bind once, draw the (possibly empty)
caster list, matching ADR-0072 D-3's own precise, narrow A-B-A claim:

```cpp
cmd.bindPipeline(shadowCastPipeline);
cmd.bindUniformBuffer(shadowLightSpaceBuffer);
for (const DrawItem& item : shadowCasterDrawItems) {
  cmd.bindVertexBuffer(item.mesh->vertexBuffer());
  cmd.bindIndexBuffer(item.mesh->indexBuffer());
  cmd.pushConstant(&item.objectToWorld, sizeof(item.objectToWorld));
  cmd.drawIndexed(item.mesh->indexCount());
}
```

**`ResourceBinding`** for `shadowMap`'s own compiled resource:
`{.resource = ..., .shadowMap = &shadowMap, .depthClear = 1.0f}` — the
new field, alongside the existing four (P7) — `depthClear = 1.0f`
(maximum depth, "nothing occludes") is what makes an empty caster list
and the very first frame the identical case (ADR-0072 D-4): the shadow
pass's own `beginRendering()` clears to this value unconditionally,
every frame, before any (possibly zero) draws.

**Shadow-map binding index depends on which shader, determined from
`Material` itself — a real, previously-underspecified mechanism this
Plan resolves by reading `renderer.cpp`'s own current draw loop**
(`renderer.cpp:72-120`): `pbr_direct_lit`/`pbr_ibl` are **not** two
different `MaterialPushConstantLayout` values (both are
`MaterialPushConstantLayout::PbrDirectLit` — confirmed,
`material.h:20-21`) — they are distinguished by
`Material::environmentBinding()` (`MaterialEnvironmentBinding::None` vs
`::Ibl`), which already drives whether the existing loop binds
texture(2)/texture(3) for environment/DFG-LUT. The new shadow-map bind,
inserted in the same per-`DrawItem` loop immediately after the existing
`environmentBinding()` switch, follows the same discrimination:

```cpp
if (item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrDirectLit) {
  const std::uint32_t shadowBinding =
      item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 4U : 2U;
  cmd.bindTexture(shadowBinding, shadowMap, shadowMapSampler);
}
```

No other `MaterialKind`/`MaterialPushConstantLayout` value (`ObjectToWorldOnly`
— `UnlitTextured`, `LitTextured`) is touched; their own Pipelines never
declare the new binding at all.

### P7 — RenderGraph/`CommandList` widening: `ResourceBinding`, Guard 0, and every `if`/`else if` chain that branches on resource kind

`ResourceBinding` (`render_graph/execution.h:69-79`) gains one new
field, appended after `hdrColorTarget`:

```cpp
atlantis::rhi::ShadowMap* shadowMap = nullptr;
```

Guard 0 (`execution.cpp:52-68`) widens from a four-term to a five-term
`boundCount` sum, message text updated to name all five kinds. Every
other resource-kind-dispatching `if`/`else if` chain in `execution.cpp`
(confirmed present, not yet located to exact line numbers post-Guard-0 —
the ones handling per-pass transition-before/clear-and-begin/final-
transition dispatch) gains a fifth arm for `shadowMap`, calling
`transitionResource(ShadowMap&, ...)`/`beginRendering(ShadowMap&,
ClearDepthValue)` (P2) — Milestone 3 must locate and update every one,
not assume a fixed count from this research pass alone.

### P8 — Descriptor capacity: three independent RHI widenings plus one test-only peak re-derivation

Three separate, real, confirmed-by-direct-reading production constants,
all three required (not one):

1. `Device::createPipeline()`'s closed check
   (`vulkan_device.cpp:992-993`):
   `ATLANTIS_CHECK(sampledTextureBindingCount == 0 || == 1 || == 3)`
   → `(0 || 1 || 2 || 3 || 4)`.
2. `createDescriptorPoolOfSize()`'s per-set sampler sizing
   (`vulkan_device.cpp:435`): `poolSizes[1].descriptorCount = 3U * maxSets`
   → `4U * maxSets`.
3. `VulkanCommandList::textureDescriptorMemos_`
   (`vulkan_command_list.h`): `std::array<TextureDescriptorMemo, 4>` →
   `std::array<TextureDescriptorMemo, 5>` — otherwise `bindTexture(4, ...)`
   (`pbr_ibl`'s own new shadow slot) fails its own
   `ATLANTIS_CHECK(binding < textureDescriptorMemos_.size())` outright.

**Descriptor-**set**-count peak is a test-only verification, not a
production constant to change** — confirmed by direct reading: no
`kFallbackPipelineCount`/`kSteadyOutputTransformPipelineCount`-shaped
constant exists anywhere in production code; the real pool-capacity
mechanism is `VulkanDescriptorPoolGrowth`'s growable generation table
(`kDescriptorPoolMaxSetsByGeneration = {4, 8, 16, 32}`, summing to a
60-set ceiling, unaffected by this Plan). The "`N+3`/`N+4`" language in
ADR-0072/Spec 0027 refers to
`tests/runtime/material_realization_gpu_tests.cpp`'s own two existing,
synthetic peak-modeling `TEST_CASE`s (Plan 0026 Milestone 6, confirmed
verbatim: `kMaterialPipelineCount=6`, `kFallbackPipelineCount=1`,
`kSkyPipelineCount=1`, `kSteadyOutputTransformPipelineCount=1`,
`kTransientOutputTransformPipelineCount=1`, asserting `N+2`/`N+3`
no-environment and `N+3`/`N+4` with-sky against the real 60-set ceiling)
— Milestone 8 adds a third, sibling `TEST_CASE` following the exact same
pattern, inserting one new `constexpr std::size_t kShadowCastPipelineCount = 1;`
term into both sums (`static_assert(kExpectedSteadySetCount == 10)` —
`N+4`; `static_assert(kExpectedPeakSetCount == 11)` — `N+5`), building a
real `shadow_cast` Pipeline via `device->createPipeline(...)` (P3)
before the steady/transient output-transform pair, mirroring the
existing sky-inclusion `TEST_CASE`'s own construction block exactly.

### P9 — Existing call-site migration: two independent, precisely-scoped mechanisms, not one undifferentiated list

Confirmed by direct reading `material_realization.cpp` (not read by
ADR-0072's own original research) — the migration is **smaller and more
precise** than "every caller needs a real ShadowMap at Material-creation
time":

**(a) `sampledTextureBindingCount` bump — one production line, covers
Runtime + the image-regression fixture automatically.**
`realizeOneMaterialCandidate()`'s own `createMaterial()` call
(`material_realization.cpp:242-243`):
```cpp
.sampledTextureBindingCount =
    materialData.kind == atlantis::asset_system::MaterialKind::PbrDirectLit && environmentEnabled ? 3U : 1U},
```
becomes `? 4U : 2U`. This is the **only** production call site that
decides `sampledTextureBindingCount` for every real `PbrDirectLit`/
`pbr_ibl` `Material` — both `RuntimeApplication::runFrame()` and
`PbrMaterialDemoFixture` funnel through this same function
(`realizePendingMaterials()` → `realizeOneMaterialCandidate()`), so
neither needs its own separate `sampledTextureBindingCount` edit.

**(b) `tests/runtime/pbr_render_gpu_tests.cpp`'s own four `TEST_CASE`s
bypass `realizeOneMaterialCandidate()` entirely** — each calls
`createMaterial()` directly with a hard-coded `sampledTextureBindingCount = 1`
(confirmed, four call sites: lines 409-419, 521-531, 617-627, 685-695) —
each becomes `2` independently; this file never constructs a `pbr_ibl`
Material, so `3`/`4` never appears here.

**(c) `tests/runtime/material_ibl_selection_gpu_tests.cpp` calls
`realizeOneMaterialCandidate()` directly** — (a)'s single-line fix
reaches it automatically; Milestone 7 must re-read this file's own
assertions to confirm none hard-codes the old `1`/`3` count directly
(not confirmed either way this pass) rather than assuming it is
unaffected.

**(d) `drawFrame()`'s five new required parameters (P6) are a wholly
separate migration — every caller, regardless of whether it uses
`PbrDirectLit`/`pbr_ibl` at all.** Confirmed call sites needing new
arguments (a real `ShadowMap`/`Sampler`/Pipeline/`Buffer` for
production/fixture code; a `FakeShadowMap` test double, mirroring
`FakeHdrColorTarget`, for pure-unit-test code):

- `src/runtime/src/runtime_application.cpp` (1 site, `runFrame()`).
- `tests/image_regression/fixture/pbr_material_demo_fixture.cpp`
  (1 site, `renderPbrMaterialDemoFrame()`) — `ibl_material_demo_fixture.{h,cpp}`
  is a pure forwarding alias (confirmed) and needs no edit of its own.
- `tests/runtime/pbr_render_gpu_tests.cpp` (1 site, the shared
  `renderOneFrame()` helper — reached by all four `TEST_CASE`s).
- `tests/image_regression/sky_background_gpu_tests.cpp` (1 site, the
  shared `renderOnce()` helper).
- `tests/renderer/renderer_ownership_tests.cpp` (confirmed 7 distinct
  `drawFrame(...)` call sites across ~7 `TEST_CASE`s, both the 11-arg
  and 13-arg shapes) — needs one new `FakeShadowMap` (mirroring
  `FakeHdrColorTarget`'s own shape) plus reuse of the existing
  `FakeSampler`/`FakePipeline`/`FakeBuffer` doubles.
- `tests/runtime/material_realization_gpu_tests.cpp`'s own N+3/N+4
  `TEST_CASE`s (P8) build Pipelines directly, not through `drawFrame()`
  — unaffected by this sub-item.

`cameraBuffer_`'s own size-literal bump (320/464 → 592, P5) touches
every one of the same files that constructs its own camera `Buffer`
with a hard-coded size, plus `runtime_application.cpp`'s Step 4 —
enumerated fully once Milestone 7 greps every `sizeBytes = 464`/
`sizeBytes = 320` literal (not exhaustively listed here to avoid
guessing at ones this pass did not directly confirm).

### P10 — Real-GPU discriminating verification: scene, sample points, thresholds

**New file:** `tests/image_regression/shadow_gpu_tests.cpp` (mirrors
`sky_background_gpu_tests.cpp`'s own placement/style — a real-GPU
discriminator suite, not an image-regression golden). Two `TEST_CASE`
groups:

**Group A — occlusion, movement, out-of-bounds, first-use/empty-caster
(no environment needed).** Hand-built geometry via `createMesh()`,
matching P3's real 44-byte `Vertex{position,color,uv,normal}` layout
(not `sky_background_gpu_tests.cpp`'s own smaller position+color-only
local `Vertex` — this scene needs real PBR shading, so it must carry the
same attributes `PbrDirectLit`'s own vertex input already consumes):

- A `12×12`-unit ground quad (`x,z ∈ [-6, 6]`, `y = 0`, normal `+Y`).
- A `1×1×1` occluder cube, centered above the ground within P4's fixed
  volume (e.g. `(0, 1.5, 0)`).
- Both given a real `PbrDirectLit` Material (a small solid-color
  `SampledTexture`+`Sampler`, mirroring `pbr_render_gpu_tests.cpp`'s own
  `rig.texture`/`rig.sampler` construction).
- Camera: eye `≈ (0, 6, 10)` looking toward the origin, `kFovYRadians`/
  `kNearZ`/`kFarZ` (this file's own established constants).
- Directional light: a fixed, clearly-diagonal downward direction (e.g.
  normalized `(-0.3, -1.0, -0.2)`) written directly into the camera
  buffer's own `directionalLights[0]` region (offset 144, P5) plus
  `directionalLightCount = 1` (offset 128) — mirroring
  `sky_background_gpu_tests.cpp`'s own `writeCamera()`-style direct
  buffer-write convention, not routed through `World`/`scene_extraction`.

Checks (sample-point pixel coordinates and exact luminance thresholds
are **derived from this geometry, not yet measured against a real
capture** — Milestone 9 must confirm/adjust them against the real
render, mirroring `kKeyDirX`/`kKeyDirY`/`kKeyDirZ`'s own real-capture-
confirmed precedent in `sky_background_gpu_tests.cpp`):

1. **Occlusion:** the ground point directly beneath the occluder's own
   analytically-predicted shadow footprint (occluder position offset
   along the light direction's own horizontal projection) renders
   measurably darker than an unshadowed ground point at the same
   distance from the camera — real occluder present in
   `shadowCasterDrawItems`.
2. **Movement:** translating the occluder or changing the light
   direction moves the shadow's own darkened footprint to the new,
   correspondingly-predicted location.
3. **Out-of-bounds:** a ground point outside P4's fixed light-space
   coverage volume (placed via a second, separate ground quad far
   outside `±8`, or by shrinking the test's own effective coverage
   check) renders lit, never shadowed (D-5's own out-of-bounds=lit
   rule).
4. **First-use / empty-caster are the same case:** the very first frame
   rendered against a freshly-created `ShadowMap` and any later frame
   with `shadowCasterDrawItems` empty both render the ground
   byte-identical to a scene with no directional light at all — proving
   ADR-0072 D-4's own unconditional-clear mechanism, not merely
   asserted.
5. Vulkan Validation Layers clean throughout.

**Group B — R1/R2/R3 IBL isolation (needs environment; reuses
`IblMaterialDemoFixture`).** Same ground+occluder geometry as Group A,
directional light written the same way, run against
`IblMaterialDemoFixture` (already environment-configured, mirroring
`sky_background_gpu_tests.cpp`'s own fixture reuse) instead of a bare
rig:

- **R1 (shadowed):** real occluder in `shadowCasterDrawItems`,
  `directionalLightCount = 1`.
- **R2 (unshadowed control, same nonzero light):** identical scene,
  `shadowCasterDrawItems` empty, `directionalLightCount` still `1`.
- **R3 (light-off IBL/ambient reference):** identical scene,
  `directionalLightCount = 0`.
- Check 1 (positive control): `R1[P] < R2[P]` by a real margin at the
  shadowed sample point `P` (mirroring
  `sky_background_gpu_tests.cpp`'s own `luminance(...) > luminance(...) + 60`
  established threshold-style, not yet re-derived for this scene).
- Check 2 (IBL isolation): `R1[P] ≈ R3[P]` within a small tolerance —
  proving the shadow factor drives the direct term to (near) zero at
  `P` without touching the IBL/ambient term, which R3 isolates by
  construction.

## Milestones / Task Breakdown

### Milestone 1 — RHI: `ShadowMap` resource + `hasColorAttachment`

- `ShadowMap`/`ShadowMapCreateParams`/`ShadowMapCreateError`,
  `Device::createShadowMap()`, `VulkanShadowMap`,
  `shadow_map_capability.h`/`.cpp`, `toShadowMapCreateError()` (P1).
- `PipelineCreateParams::hasColorAttachment`, its `createPipeline()`
  mapping including the `VkPipelineColorBlendStateCreateInfo` gate
  located and fixed at Implementation time (P2).
- GPU-independent tests: default `hasColorAttachment = true` reproduces
  every existing Pipeline's behavior (mirrors `depthWriteEnabled`'s own
  Plan 0026 Milestone 1 compatibility test); `ShadowMapCreateParams`
  default-construction/equality tests mirroring `HdrColorTargetCreateParams`'s
  own.
- Real-GPU test: `createShadowMap()` with P4's resolution succeeds on
  the real target GPU; the format-capability check path is exercised
  (confirmed via a real `vkGetPhysicalDeviceFormatProperties` query, not
  mocked) mirroring `hdr_color_target`'s own real-GPU coverage.

### Milestone 2 — RHI: depth-only `beginRendering()`, `transitionResource()`, memoized `bindTexture()` for `ShadowMap`

- New `beginRendering(ShadowMap&, ClearDepthValue)` overload (P2, written
  from scratch, not parameterized from an existing one).
- New `transitionResource(ShadowMap&, ResourceState, ResourceState)`
  overload (P2).
- New `bindTexture(std::uint32_t, const ShadowMap&, const Sampler&)`
  overload, memoized shape (P2) — depends on Milestone 6's
  `textureDescriptorMemos_` widening (P8) landing first or in the same
  commit.
- Real-GPU test, new file `tests/vulkan_backend/shadow_map_render_gpu_tests.cpp`
  (mirrors `pipeline_depth_write_gpu_tests.cpp`'s own established
  three-draw-discriminator shape): a depth-only Pipeline writes real
  depth into a `ShadowMap` via the new `beginRendering()` overload (one
  `CommandList`, one `submit()`, no direct `vkCmd*` outside a compiled
  `RenderGraph` pass — mirrors Plan 0026 Milestone 1's own established
  headless draw-then-copy sequence, adapted for a depth-only read-back
  or a downstream sampling probe rather than a color read-back), then a
  second Pipeline samples it via `bindTexture(ShadowMap&, ...)` and
  confirms the sampled depth matches what was written.

### Milestone 3 — RenderGraph: `ResourceBinding`/Guard 0 widening

- `ResourceBinding::shadowMap` field; Guard 0's five-term `boundCount`
  sum and message text (P7).
- Every other resource-kind-dispatching `if`/`else if` chain in
  `execution.cpp` located and given a fifth `shadowMap` arm (P7) — an
  Implementation-time location task, not assumed from this Plan's own
  research alone.
- GPU-independent tests: Guard 0 rejects a `ResourceBinding` binding
  zero or two-or-more of the five kinds (mirrors the existing four-kind
  test's own shape, widened); a `shadowMap`-only binding compiles and
  executes correctly in isolation (a minimal synthetic graph, mirroring
  `execution_tests.cpp`'s own established per-kind coverage pattern).

### Milestone 4 — `shadow_cast` shader, descriptor contract, CMake wiring

- `shaders/shadow_cast/shadow_cast.slang` (P3); `shaders/shadow_cast/CMakeLists.txt`
  (mirrors `shaders/sky/CMakeLists.txt` exactly: `NAME shadow_cast`,
  `EXPECTED_CONTRACT shadow-cast`, the same `PARENT_SCOPE` re-export
  line); root `CMakeLists.txt` gains `add_subdirectory(shaders/shadow_cast)`
  before `add_subdirectory(src/runtime)`.
- `shadowCastVertexLayout()` (Runtime-private, mirrors
  `minimalMeshVertexLayout()`'s own placement in `runtime_application.cpp`,
  duplicated per-file per this codebase's own established "duplicated,
  not shared" convention for every caller that needs it) (P3).
- `shadowCastExpectedDescriptorContract()` (`descriptor_contract.h`/`.cpp`,
  P3); `compile_and_validate.cpp`'s new `"shadow-cast"` dispatch arm (P3).
- `tests/shader_system/shadow_cast_reflection_tests.cpp` (mirrors
  `sky_reflection_tests.cpp`'s own shape) — the real, disposable-probe-
  confirmed descriptor contract against real, freshly-compiled
  `shadow_cast.slang` reflection (P3's own "not yet confirmed"
  resolved here, following Plan 0026 P3's exact precedent).

### Milestone 5 — Light-space buffers, shader content, descriptor extension

- `pbr_direct_lit.slang`/`pbr_ibl.slang`: the light-space tail fields
  (P5's exact byte table), the shadow-map sampler binding (2 / 4), and
  D-5's shadow-factor multiply inside the existing directional-light
  loop, at the one confirmed insertion line
  (`accumulated += (kD * diffuseColor / kPi + specular) * radiance * NdotL;`,
  currently line 145 of `pbr_ibl.slang`, the byte-identical corresponding
  line in `pbr_direct_lit.slang`).
- `pbrDirectLitExpectedDescriptorContract()`/`pbrIblExpectedDescriptorContract()`
  (`descriptor_contract.cpp:45-57`): one new `DescriptorBinding{.binding = 2/4, .type = Sampler, .stage = Fragment}`
  entry each.
- `tests/shader_system/pbr_direct_lit_reflection_tests.cpp`/
  `pbr_ibl_reflection_tests.cpp` (existing files, extended): confirm the
  new tail offset (464) and binding (2/4) against real, freshly-compiled
  reflection.
- `cameraBuffer_`'s size literal (`runtime_application.cpp`'s own Step 4,
  currently `464`, P5) becomes `592`; every other hard-coded camera-
  buffer-size literal this Milestone's own implementation-time grep
  finds (fixtures/test rigs that construct their own camera `Buffer`
  with a hard-coded size) is bumped in the same commit as this
  Milestone's shader change (kept atomic — no partial commit leaves
  shader and buffer size out of sync).

### Milestone 6 — RHI capacity widenings (three, together)

- `ATLANTIS_CHECK` widening, descriptor-pool `3U → 4U` sizing,
  `textureDescriptorMemos_` `4 → 5` (P8, all three in one commit — they
  are independent facts but jointly required for the same new binding
  index to work at all).
- GPU-independent/real-GPU tests: mirrors `descriptor_pool_growth_gpu_tests.cpp`'s
  own established coverage — a Pipeline with `sampledTextureBindingCount = 2`
  and one with `= 4` both create and bind successfully; `bindTexture(4, ...)`
  no longer trips the old size-4-array bound.

### Milestone 7 — `Renderer::drawFrame()` signature, "shadow" pass, shadow-map binding

- `Renderer::drawFrame()`'s five new required parameters (P6); the new
  "shadow" RenderGraph pass; the binding-index-aware shadow-map bind in
  the existing "draw" loop (P6).
- Every existing `drawFrame()` call site updated (P9d's full list) —
  landed as one atomic commit across every caller, matching this
  codebase's own established "signature change + every call site, one
  commit" convention (Plan 0026 Milestone 2's own precedent).
- New `FakeShadowMap` test double (`renderer_ownership_tests.cpp`,
  mirrors `FakeHdrColorTarget`'s own shape).
- `tests/renderer/renderer_ownership_tests.cpp`: every existing
  `TEST_CASE` updated to compile against the new signature (its own
  existing assertions otherwise unchanged); one new `TEST_CASE` asserts
  the "shadow" pass's own draw sequence records strictly before "draw"'s
  own sequence, mirroring the existing sky-ordering `TEST_CASE`'s own
  shape (`renderer_ownership_tests.cpp:427-487`) exactly; one new
  `TEST_CASE` confirms the shadow-map bind lands at binding 2 for a
  plain `PbrDirectLit` `DrawItem` and at binding 4 for an
  `environmentBinding() == Ibl` one, in the same frame.

### Milestone 8 — Descriptor-set-count sibling test

- New `TEST_CASE` in `material_realization_gpu_tests.cpp` (P8): `N+4`/
  `N+5` with a real `shadow_cast` Pipeline present, alongside the
  existing `N+2`/`N+3` and `N+3`/`N+4`-with-sky cases (neither touched).

### Milestone 9 — Runtime lifecycle wiring

- `runtime_application.h`: `shadowMap_`, `shadowMapSampler_`,
  `shadowCastPipeline_`, `shadowLightSpaceBuffer_` members, declared
  immediately after `skyPipeline_` (mirrors that member's own placement
  precedent).
- `bootstrap_config.h`: `shadowCastVertexShaderSpirvPath`/
  `...ReflectionPath`/`shadowCastFragmentShaderSpirvPath`/`...ReflectionPath`
  (mirrors `skyVertexShaderSpirvPath`'s own four-field shape) —
  **unconditionally required** (not gated behind `hasEnvironment`, since
  shadow infrastructure is unconditional, P1) — `bootstrap_config.cpp`
  gains a new, always-checked (not `if (hasEnvironment)`-gated) block.
- `init_error.h`/`.cpp`: `ShadowMapCreateFailed`, `ShadowSamplerCreateFailed`,
  `ShadowCastPipelineCreateFailed`, `ShadowLightSpaceBufferCreateFailed`
  (mirrors `SkyPipelineCreateFailed`'s own shape, one `case` line each in
  `toString()`).
- `runtime_application.cpp`: `initializeSteps()` gains an unconditional
  (not `if (hasEnvironment)`) shadow-shader-load step (mirrors Step 2e's
  own pbrIbl-loading shape) and an unconditional Step 4e (immediately
  after `skyPipeline_`'s own Step 4d) creating `shadowMap_`/
  `shadowMapSampler_`/`shadowCastPipeline_`/`shadowLightSpaceBuffer_`,
  fatal on any error. `runFrame()` writes the light-space buffers every
  frame (P5) and passes the five new arguments (P6, P9d) to
  `drawFrame()`, with `shadowCasterDrawItems = hasDirectionalLight ?
  drawItems : std::span<const DrawItem>{}` (P6). `shutdown()` resets the
  four new members, placed alongside `skyPipeline_.reset()`.
- `main.cpp`/`CMakeLists.txt`: `ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR`
  wiring (mirrors the sky block exactly), `shadow_cast_shaders` added to
  `add_dependencies(atlantis_runtime ...)`.

### Milestone 10 — Existing call-site migration (P9)

- `material_realization.cpp:243`'s one-line `3U:1U → 4U:2U` bump (P9a).
- `pbr_render_gpu_tests.cpp`'s four independent `sampledTextureBindingCount = 1 → 2`
  literals (P9b).
- `material_ibl_selection_gpu_tests.cpp` re-read and, if needed,
  corrected (P9c).
- `pbr_material_demo_fixture.h`/`.cpp`: new `shadowMap`/`shadowMapSampler`/
  `shadowCastPipeline`/`shadowLightSpaceBuffer` fields, created
  unconditionally in `setUpPbrMaterialDemoFixture()` (mirrors
  `outputTransformSampler`'s own always-created shape, not `skyPipeline`'s
  conditional one); `renderPbrMaterialDemoFrame()`'s `drawFrame()` call
  updated (P9d).
- `golden_generator/pbr_material_demo_main.cpp`: `buildConfig()` gains
  the unconditional shadow-shader-path block in both its
  `ATLANTIS_IBL_GOLDEN_GENERATOR` and non-IBL branches (unconditional,
  unlike the sky block which is IBL-only).
- Every remaining fixture/example/test file that calls `drawFrame()`
  purely for signature compatibility (`minimal_cube_fixture.cpp`,
  `textured_quad_fixture.cpp`, `material_demo_fixture.cpp`,
  `lighting_demo_fixture.cpp`, `world_scene_fixture.cpp`,
  `world_scene_loaded_fixture.cpp`, `examples/minimal_renderer_demo/main.cpp`,
  `examples/headless_rendering_demo/main.cpp`,
  `tests/vulkan_backend/{headless_rendering,minimal_renderer,descriptor_pool_growth}_gpu_tests.cpp`)
  — each gains its own minimal `ShadowMap`/`Sampler`/`Pipeline`/`Buffer`
  construction (a small, fixed, always-possible resource set — none of
  these need a real occluder, `shadowCasterDrawItems` stays empty) —
  **zero behavioral change**, confirmed by each file's own existing
  tests/golden continuing to pass unmodified.

### Milestone 11 — Verification and golden strategy

- Group A/B real-GPU tests (P10), `tests/image_regression/shadow_gpu_tests.cpp`.
- Golden strategy (ADR-0072 D-7, Spec 0027 "For Human Confirmation" item 5):
  - `minimal_cube`, `world_scene`, `textured_quad`, `material_demo`,
    `lighting_demo`: existing, unmodified capture-compare tests confirm
    byte-identical (neither shader they use is touched).
  - `ibl_material_demo`: **hard requirement** — a real pixel-identity
    check against the existing committed golden, asserted directly (not
    merely omitted from a re-capture list) — no directional light in
    this scene, shadow term structurally unreachable.
  - `pbr_material_demo`/`hdr_roll_off_demo`: run their own existing,
    unmodified capture-compare tests first; a byte-identical result
    needs no further action; a human-reviewed re-capture (ADR-0042) is
    requested only if either shows an actual difference — never
    presumed in advance.
- Full `ctest -LE gpu`, `ctest -L gpu`, both Debug and Release; a fresh
  `ATLANTIS_BUILD_TESTS=OFF` build; Vulkan Validation Layers clean
  throughout.

## Files / Modules Touched (expected)

- RHI: `src/rhi/include/atlantis/rhi/{types.h,shadow_map.h(new),device.h,command_list.h}`.
- Vulkan Backend: `src/vulkan_backend/src/{vulkan_device.cpp,vulkan_command_list.h,vulkan_command_list.cpp,vulkan_result.cpp,shadow_map_capability.h(new),shadow_map_capability.cpp(new),vulkan_shadow_map.h(new)}`.
- RenderGraph: `src/render_graph/include/atlantis/render_graph/execution.h`,
  `src/render_graph/src/execution.cpp`.
- Renderer: `src/renderer/include/atlantis/renderer/renderer.h`,
  `src/renderer/src/renderer.cpp`.
- Shader/descriptor contract:
  `src/shader_system/include/atlantis/shader_system/descriptor_contract.h`,
  `src/shader_system/src/descriptor_contract.cpp`,
  `src/tools/shader_compiler/compile_and_validate.cpp`,
  `shaders/pbr_direct_lit/pbr_direct_lit.slang`, `shaders/pbr_ibl/pbr_ibl.slang`,
  `shaders/shadow_cast/shadow_cast.slang` (new),
  `shaders/shadow_cast/CMakeLists.txt` (new), root `CMakeLists.txt`.
- Runtime: `src/runtime/include/atlantis/runtime/{bootstrap_config.h,init_error.h,runtime_application.h}`,
  `src/runtime/src/{bootstrap_config.cpp,init_error.cpp,runtime_application.cpp,material_realization.cpp}`,
  `src/runtime/main.cpp`, `src/runtime/CMakeLists.txt`.
- Tests: `tests/rhi/types_tests.cpp`,
  `tests/vulkan_backend/shadow_map_render_gpu_tests.cpp` (new),
  `tests/vulkan_backend/CMakeLists.txt`,
  `tests/render_graph/execution_tests.cpp`,
  `tests/renderer/renderer_ownership_tests.cpp`,
  `tests/shader_system/{shadow_cast_reflection_tests.cpp(new),pbr_direct_lit_reflection_tests.cpp,pbr_ibl_reflection_tests.cpp}`,
  `tests/shader_system/CMakeLists.txt`,
  `tests/runtime/{pbr_render_gpu_tests.cpp,material_realization_gpu_tests.cpp,material_ibl_selection_gpu_tests.cpp}`,
  `tests/image_regression/fixture/pbr_material_demo_fixture.h`/`.cpp`,
  `tests/image_regression/golden_generator/pbr_material_demo_main.cpp`,
  `tests/image_regression/shadow_gpu_tests.cpp` (new),
  `tests/image_regression/sky_background_gpu_tests.cpp`,
  `tests/image_regression/CMakeLists.txt`,
  and every remaining `drawFrame()` call-site file enumerated in
  Milestone 10's own last bullet (signature-compatibility only, no
  assertion change).
- Documentation: this Plan; `specs/README.md` status pointer, updated
  once at PR time to reflect implementation.

Any Implementation-touched source outside this list is a disclosed Plan
deviation, per AGENTS.md.

## Sequencing & Dependencies

Milestones 1-2 (RHI `ShadowMap`/depth-only Pipeline/`CommandList`
overloads) have no dependency on any other Milestone and unblock
everything else. Milestone 3 (RenderGraph widening) depends on
Milestone 1 (needs the real `ShadowMap` type) and unblocks Milestone 7.
Milestone 4 (`shadow_cast` shader) depends on Milestone 2 (needs
`hasColorAttachment`) and may proceed in parallel with Milestone 5.
Milestone 5 (`pbr_direct_lit`/`pbr_ibl` shader content) depends on
Milestone 6 landing first or in the same commit (the descriptor-contract
tests need the widened `sampledTextureBindingCount` check to pass).
Milestone 6 (capacity widenings) has no dependency beyond Milestone 1
conceptually motivating it, but is otherwise independent — land it
alongside Milestone 5. Milestone 7 (`drawFrame()` signature) depends on
Milestones 1, 3, 4, 5, 6 all landing first (it wires together every new
type/pass/binding). Milestone 8 (descriptor-set test) depends on
Milestone 4 (needs a real `shadow_cast` Pipeline). Milestone 9 (Runtime
wiring) depends on Milestone 7. Milestone 10 (call-site migration)
depends on Milestones 5 (buffer size), 7 (signature), and 9 (Runtime's
own new resources exist to pass into fixtures that mirror them).
Milestone 11 (verification/golden) follows a clean commit of
Milestones 1-10; the golden re-capture (if any) is its own separate,
human-reviewed commit, never bundled with a code change, per ADR-0042.

Every atomic commit keeps `Renderer::drawFrame()`'s signature
synchronized with every call site (Milestone 7 lands as one commit
touching every caller) and the light-space buffer size synchronized
across shader and every buffer-creation call site (Milestone 5). No
partial commit leaves an existing target uncompilable.

## Verification Checklist

Maps to Spec 0027's own Testing & Verification Plan:

- [ ] GPU-independent: `hasColorAttachment`'s default reproduces
  existing Pipeline behavior; `ShadowMapCreateParams` compatibility
  tests; Guard 0's five-kind rejection/acceptance tests; the widened
  `sampledTextureBindingCount` check accepts 2 and 4.
- [ ] GPU: `shadow_map_render_gpu_tests.cpp` (Milestone 2) confirms
  depth-only write + sampled read round-trips correctly; `shadow_cast_reflection_tests.cpp`/
  extended `pbr_direct_lit`/`pbr_ibl` reflection tests confirm the real,
  compiled descriptor contract and tail offset (Milestone 4/5);
  `descriptor_pool_growth`-style tests confirm bindings 2 and 4 both
  work (Milestone 6); `shadow_gpu_tests.cpp` Group A confirms occlusion,
  movement, out-of-bounds-is-lit, and first-use/empty-caster equivalence
  (Milestone 11); Group B confirms the positive control and IBL
  isolation (Milestone 11); Debug and Release, Vulkan Validation Layers
  clean.
- [ ] Image regression: five non-PBR goldens byte-identical (existing,
  unmodified tests); `ibl_material_demo` asserted byte-identical (hard
  requirement, Milestone 11); `pbr_material_demo`/`hdr_roll_off_demo`
  compared first, re-capture requested only if an actual difference
  appears.
- [ ] Descriptor pool: the new `N+4`/`N+5`-with-shadow `TEST_CASE`
  (Milestone 8) confirms against the real 60-set ceiling.
- [ ] Full `ctest -LE gpu` and `ctest -L gpu`, Debug and Release; exact
  counts recorded in the Implementation PR.
- [ ] Fresh `ATLANTIS_BUILD_TESTS=OFF` configure/build produces a
  working `atlantis_runtime.exe` with zero test executables.
- [ ] Module/link/include scan: no new cross-module dependency beyond
  what this Plan names; no new `Vk*` symbol outside Vulkan Backend.
- [ ] `git diff --check` and committed-file audit clean; no generated
  `.spv`, reflection JSON, or build directory committed.

## Rollback Plan

Revert Milestone 11's golden-recapture commit first (if any), then
Milestone 10, then Milestones 9 through 1 in reverse dependency order.
`hasColorAttachment` defaults `true` and every existing Pipeline is
unaffected by its mere presence, so a partial rollback stopping after
Milestone 1/2 alone leaves the RHI additions inert and harmless.
`Renderer::drawFrame()`'s five new parameters and every call site are
one atomic rollback group (Milestone 7); reverting it without reverting
Milestone 9's Runtime wiring would fail to compile, so Milestones 7-10
roll back together. No `World`/`Scene` schema or asset format is
introduced, so no data migration is needed at any rollback point.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No deltas beyond the Verification Checklist above.

## Plan Review — Items for Human Confirmation

1. **P4's concrete numeric values** (1024×1024 resolution; center
   `(0,0,0)`, half-extent `8.0`, near `0.1`, far `30.0` orthographic
   volume; `kShadowBias = 0.0015`) are derived from real, existing scene
   geometry and D32 precision reasoning, not yet measured against a real
   captured shadow — confirm these as the Implementation starting point,
   subject to real-GPU adjustment during Milestone 11, not a re-opened
   design question.
2. **P6's shadow-map binding-index dispatch** — `binding = 2` for a
   plain `PbrDirectLit` `Material` (`environmentBinding() == None`),
   `binding = 4` for `pbr_ibl` (`environmentBinding() == Ibl`) — is a
   real, previously-underspecified mechanism this Plan resolves by
   reading `Material`'s own actual fields (both PBR variants share one
   `MaterialPushConstantLayout` value); confirm this dispatch is the
   correct, minimal insertion point rather than a new `MaterialKind`-
   level distinction.
3. **P9's refined migration scope** — a single production-code line
   (`material_realization.cpp:243`) handles the `sampledTextureBindingCount`
   bump for both Runtime and the image-regression fixture automatically;
   the broader, multi-file migration is specifically `drawFrame()`'s five
   new required parameters, not a ShadowMap dependency threaded through
   Material construction itself. Confirm this narrower framing is
   accepted in place of ADR-0072's own less-differentiated original
   description (the underlying decision — every existing call site is
   touched — is unchanged; only the mechanism per file is now precise).
4. **P10's new test scene** (a hand-built 12×12 ground quad + 1×1×1
   occluder cube, via `createMesh()` with the real 44-byte asset vertex
   layout, driven directly by the test rather than through a new
   `World`/asset-scene file) — confirm this is preferred over adding a
   new `.scene.txt` asset (which would exercise the full asset-loading
   path but adds new asset/CMake wiring this Plan's minimal-slice
   framing does not otherwise need).
5. **P2's disclosed, unresolved gap** — the `VkPipelineColorBlendStateCreateInfo`
   gating for `hasColorAttachment == false` was not located this
   research pass; Milestone 1 must find and fix it as a real
   Implementation-time task, not a deferred design question (the
   *requirement* — attachment counts must agree — is not in doubt, only
   its exact current code location).
