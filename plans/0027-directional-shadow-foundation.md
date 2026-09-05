# Plan: Directional Shadow Foundation

- **Spec:** [specs/0027-directional-shadow-foundation.md](../specs/0027-directional-shadow-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** slmao
- **Human Review Approval (2026-09-05):** Approved by the repository
  maintainer against [PR #124](https://github.com/slmao/Atlantis/pull/124)
  (commit `d3dce50`), as a joint Spec 0027 + Plan 0027 Human Review.
  Accepts all five Plan Review items as written: P4/P10/P11's fixed
  numeric values, pixel coordinates, and thresholds (no silent
  adjustment during Implementation — a real-GPU result requiring a
  change stops for human confirmation first); P6's shadow-binding-index
  dispatch via `Material::environmentBinding()`; P9's exhaustive
  call-site, camera-buffer, and CMake migration tables; P10's hand-built
  GPU test scene, with no-directional-light byte compatibility proven by
  the existing `ibl_material_demo` golden rather than retested; and
  Milestone 8/9's buildable, atomic-commit split. **Implementation
  starts only after this PR merges to `main` — not before**, and must
  follow the approved milestones, numeric values, and verification
  matrix exactly; any deviation found necessary during Implementation is
  called out explicitly, not silently applied (per this Plan's own
  Non-negotiable rule and AGENTS.md).

## Objective

Implement Spec 0027 / ADR-0072: one directional light's own shadow on
`PbrDirectLit`/`pbr_ibl` surfaces — a new `ShadowMap` RHI resource, a
depth-only `shadow_cast` Pipeline and RenderGraph pass, a manual
shadow-comparison term inside both shaders' existing directional-light
loop, and the real migration this requires across every existing
`drawFrame()` caller — while leaving IBL/sky/every non-PBR golden
untouched.

## Non-negotiable rule for Implementation

**Every numeric value fixed in P4/P10/P11 below (resolution, orthographic
volume, `kShadowBias`, the up-vector degeneracy threshold, every world-
space sample point, and every luminance threshold in P10) is approved as
written.** If a real GPU capture during Implementation shows any of them
needs to change, **stop and request human confirmation before changing
it** — never adjust silently and continue.

**Carve-out, stated precisely so it is not read as an escape hatch:**
mechanically re-executing P10's own fixed shadow-footprint/pixel-projection
formula against its own fixed inputs (scene positions, camera, light
directions) and getting an integer pixel that differs from this Plan's
own hand-computed value by a point or two (floating-point rounding only)
is not a value change — it is finishing the same computation this Plan
already specifies. Changing the formula itself, the world-space points,
the camera/light parameters, or a threshold's underlying rationale is a
value change and requires stopping.

## Plan-Stage Decisions

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
added to the abstract `Device` interface immediately after
`createHdrColorTarget()`.

**Vulkan implementation** — `VulkanShadowMap` mirrors `VulkanHdrColorTarget`
field-for-field (owns its own `VkImage`/`VkDeviceMemory`/`VkImageView`,
non-copyable/non-movable). `VulkanDevice::createShadowMap()` mirrors
`createHdrColorTarget()` (`vulkan_device.cpp:1461-1541`), with:

- `imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;`
- `viewCreateInfo.subresourceRange = VkImageSubresourceRange{VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1};`
- A new, standalone `hasRequiredShadowMapFeatures(VkFormatFeatureFlags)`
  (new `shadow_map_capability.h`/`.cpp`, mirroring
  `hdr_color_target_capability.h`/`.cpp`), requiring
  `VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT`
  together. **This check is a genuine correctness requirement**
  (ADR-0072 D-1): `SAMPLED_IMAGE_BIT` is unconditionally guaranteed for
  `D32_SFLOAT`, but `DEPTH_STENCIL_ATTACHMENT_BIT` is guaranteed only
  collectively across `{D32_SFLOAT, X8_D24_UNORM_PACK32}` — this exact
  combination on `D32_SFLOAT` is not spec-guaranteed on every device.
- `toShadowMapCreateError(VkResult)` mirrors `toHdrColorTargetCreateError()`
  (collapses every non-success `VkResult` to `ImageCreationFailed`).

Fixed resolution (P4), never resized. Created once at Runtime startup,
unconditionally. A dedicated `Sampler` (`Filter::Nearest`,
`AddressMode::ClampToEdge`) is created via the existing `createSampler()`
— no RHI change for the sampler itself.

### P2 — `hasColorAttachment`: exact `createPipeline()` gating, exact `beginRendering()` overload

`PipelineCreateParams` gains one additive field, appended as the
struct's new last member:

```cpp
bool hasColorAttachment = true;
```

**`VkPipelineRenderingCreateInfo` gating** (`vulkan_device.cpp:1212-1232`):

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

`std::visit(toVkFormat, params.colorFormat)` is skipped entirely when
`hasColorAttachment == false` (avoids any dependency on
`toVkFormat(Format::Unknown)`, `params.colorFormat`'s own default).

**`VkPipelineColorBlendStateCreateInfo` gating — located precisely,
confirmed this pass** (`vulkan_device.cpp:1196-1204`, current code):

```cpp
VkPipelineColorBlendAttachmentState colorBlendAttachment{};
colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
colorBlendAttachment.blendEnable = VK_FALSE;

VkPipelineColorBlendStateCreateInfo colorBlendState{};
colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
colorBlendState.attachmentCount = 1;
colorBlendState.pAttachments = &colorBlendAttachment;
```

becomes:

```cpp
colorBlendState.attachmentCount = params.hasColorAttachment ? 1 : 0;
colorBlendState.pAttachments = params.hasColorAttachment ? &colorBlendAttachment : nullptr;
```

(`colorBlendAttachment` itself is still constructed unconditionally —
harmless, unused when the pointer above is `nullptr` — no need to guard
its own construction.) Vulkan requires `VkPipelineColorBlendStateCreateInfo::attachmentCount`
to match `VkPipelineRenderingCreateInfo::colorAttachmentCount`; both are
now gated by the same field.

**New `CommandList::beginRendering(ShadowMap&, float depthClear)`
overload — a plain `float`, not a new type.** `ClearDepthValue` does
**not** exist anywhere in this codebase (confirmed, repo-wide search) —
this overload reuses the existing plain-`float` `depthClear` convention
`beginRendering(RenderTarget&, Texture*, ClearColorValue, float)`
already establishes; no new type is introduced. Both existing
`beginRendering()` overloads (`vulkan_command_list.cpp:169-233`,
`239-286`) unconditionally attach a color image and hard-code
`colorAttachmentCount = 1` — this overload is written from scratch:
`renderingInfo.colorAttachmentCount = 0`, `pColorAttachments = nullptr`;
one `VkRenderingAttachmentInfo` for the `ShadowMap`'s own `imageView()`
(`loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR`, `clearValue.depthStencil.depth = depthClear`,
`storeOp = VK_ATTACHMENT_STORE_OP_STORE`) as `pDepthAttachment`;
`vkCmdSetViewport`/`vkCmdSetScissor` sized to the `ShadowMap`'s own
`extent()`.

`CommandList` also gains `transitionResource(ShadowMap&, ResourceState,
ResourceState)` (mirrors `Texture`'s own overload, `VK_IMAGE_ASPECT_DEPTH_BIT`)
and `bindTexture(std::uint32_t, const ShadowMap&, const Sampler&)`
(mirrors `bindTexture(const SampledTexture&, ...)`'s own **memoized**
shape, `vulkan_command_list.cpp:353-403` — not `HdrColorTarget`'s
unmemoized, once-per-frame shape — the shadow-map binding is re-issued
once per `PbrDirectLit`/`pbr_ibl` `DrawItem`).

### P3 — `shadow_cast.slang`: position-only, real asset layout, descriptor contract confirmed by a real compile

The first genuinely single-attribute vertex schema in this codebase.
Reads only position, at the real, shared `kMeshArtifactPositionOffsetBytes = 0` /
`kMeshArtifactVertexStrideBytes = 44` offsets already defined in
`asset_system/mesh_artifact.h` (not a new local `Vertex` struct):

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

`fragmentMain()` is `void`, no `SV_Target` (matches `hasColorAttachment = false`).
`PipelineCreateParams`: `pushConstantSizeBytes = 64` (one 4x4 matrix,
`ObjectToWorldOnly`-shaped), `sampledTextureBindingCount = 0`,
`hasCameraUniformBinding = true` (binding 0 = the dedicated light-space
buffer — this field only gates "is there a uniform buffer at binding
0," which is what the light-space buffer needs), `hasDepthAttachment = true`,
`depthWriteEnabled = true`, `hasColorAttachment = false`,
`depthFormat = DepthFormat::D32Sfloat`.

**Descriptor contract — confirmed by a real, temporary, uncommitted
`slangc` compile of this exact shader text (`slangc -target spirv
-profile spirv_1_0 -warnings-disable 50011 -stage {vertex,fragment}
-entry {vertexMain,fragmentMain} -reflection-json`, run and inspected
during this Plan's own drafting; the probe file was never committed).**
Real reflection output: the vertex stage's `entryPoints[0].bindings[]`
lists `lightSpace` (`descriptorTableSlot` index 0) with `"used": 1`; the
fragment stage lists the identical `lightSpace` entry with `"used": 0`
(dropped by the established `used:0`-filtering transform, matching Plan
0026 P3's precedent) and has no `result`/color output at all. This
confirms, not merely recommends:

```cpp
std::vector<DescriptorBinding> shadowCastExpectedDescriptorContract() {
  return {DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Vertex}};
}
```

One entry, Vertex-stage only. `compile_and_validate.cpp:136-171` gains
`else if (expectedContract == "shadow-cast") { fullContract = shadowCastExpectedDescriptorContract(); }`.
`validatePushConstantsForVertexStage()` needs no new case — `"shadow-cast"`
falls into the existing `else` branch (`expectedSizeBytes = 64`),
already correct. `validatePushConstantsForFragmentStage()`'s call is
gated on `"pbr-direct-lit"`/`"pbr-ibl"` only — already excludes
`"shadow-cast"`.

### P4 — Numeric values: resolution, orthographic volume, bias

**Derived from real, existing scene geometry and D32 precision
reasoning — not measured against a real capture.** Fixed here so no
number is chosen opportunistically in code. Per the Non-negotiable rule
above, none of these may be silently changed during Implementation.

- **Resolution: 1024×1024, `DepthFormat::D32Sfloat`.** Proportionate to
  this repository's own 512×512 image-regression resolution — ~2×
  oversampling for a hard-edged (no PCF) shadow, without 2048²'s cost.
- **Fixed orthographic volume: center `(0, 0, 0)`, half-extent `8.0`
  world units, `near = 0.1`, `far = 30.0`.** `pbr_material_demo.scene.txt`'s
  four spheres span `x,y ∈ [-2.3, 2.3]`; `world_scene.scene.txt`'s cubes
  span `x ∈ [-3.0, 3.0]`; the new shadow-verification scene (P10) is
  `x,z ∈ [-6, 6]`. `±8` covers all three with margin, as one Runtime
  constant applied uniformly (never scene-fitted).
- **`kShadowBias = 0.0015`**, compared in the `[0, 1]` NDC-depth space
  D-5's formula operates in. Over the `29.9`-unit linear (orthographic)
  near-far range, this is `~4.5 cm` of world-space slack — comparable to
  conventional acne-avoidance biases at this scene family's `~1-2` unit
  object scale.

### P5 — Light-space data: byte layout, buffers, write timing, sentinel

`pbr_direct_lit.slang`/`pbr_ibl.slang` extend their existing binding-0
camera buffer. Confirmed current byte layout (`pbr_ibl.slang:26-37`;
`pbr_direct_lit.slang` is identical through `cameraWorldPosition`/`_pad2`):

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

`pbr_direct_lit.slang` (currently ends at byte 320) gains an explicit
`float4 _shadowPad[9];` (144 bytes) to reach the same 464-byte tail
offset `pbr_ibl.slang` already has, then the identical 128-byte
light-space pair. Both shaders' camera buffer grows to **592 bytes**
(from 320 / 464).

`shadow_cast.slang` gets a **second, independent, 128-byte** `Buffer`
(view+projection only). Sharing the 592-byte buffer across Pipelines is
technically legal (`bindUniformBuffer()`'s limit is one buffer per
Pipeline, not one consumer per buffer) but is rejected: it would force
this minimal shader to declare ~464 bytes of fields it never reads.
Both buffers are Runtime-owned, created once at startup, **written
every frame with identical view/projection values**, at the same point
`runFrame()` already writes the main camera buffer.

**No-directional-light sentinel: the identity matrix, in both buffers**
(not the SH-coefficient precedent's all-zero — a zero 4×4 "projection"
is singular). Written unconditionally, every frame.

### P6 — `Renderer::drawFrame()`: five new required parameters, a new "shadow" pass, binding-index-aware bind

Current signature (`renderer.h:84-92`):

```cpp
void drawFrame(CommandList& commandList, RenderTarget& colorTarget, Texture& depthTarget,
               Buffer& cameraUniformBuffer, std::span<const DrawItem> drawItems, ResourceState finalColorState,
               HdrColorTarget& hdrColorTarget, Buffer& fullscreenTriangleVertexBuffer,
               Buffer& fullscreenTriangleIndexBuffer, Pipeline& outputTransformPipeline,
               Sampler& outputTransformSampler, const EnvironmentLighting* environmentLighting = nullptr,
               Pipeline* skyPipeline = nullptr);
```

Five parameters appended after `skyPipeline`, **all required,
non-nullable references** (not `skyPipeline`'s nullable shape) — shadow
infrastructure is unconditionally created (P1):

```cpp
               ShadowMap& shadowMap, Sampler& shadowMapSampler, Pipeline& shadowCastPipeline,
               Buffer& shadowLightSpaceBuffer, std::span<const DrawItem> shadowCasterDrawItems);
```

`shadowCasterDrawItems` is independent from `drawItems` — `Renderer`
never derives one from the other and never inspects `Mesh` layout.
Runtime passes `shadowCasterDrawItems = drawItems` when a directional
light is configured, an empty span otherwise.

**New RenderGraph pass, "shadow," compiled first** — writes `shadowMap`
(`DepthAttachmentReadWrite`); "draw" also **reads** `shadowMap`
(`ShaderRead`), the identical write-then-read pattern already proven for
`hdrResource`. Execute callback, bind once, draw the (possibly empty)
list:

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

`ResourceBinding` for `shadowMap`: `{.resource = ..., .shadowMap = &shadowMap, .depthClear = 1.0f}`
— `1.0f` (max depth) is what makes an empty caster list and the first
frame identical (P7).

**Shadow-map binding index is decided by `Material::environmentBinding()`,
not by a new `MaterialKind`.** `pbr_direct_lit`/`pbr_ibl` share one
`MaterialPushConstantLayout::PbrDirectLit` value (confirmed,
`material.h`) — they are distinguished only by `environmentBinding()`
(`None` vs `Ibl`), which already drives the existing loop's
texture(2)/texture(3) binds. Inserted immediately after that existing
switch:

```cpp
if (item.material->pushConstantLayout() == MaterialPushConstantLayout::PbrDirectLit) {
  const std::uint32_t shadowBinding =
      item.material->environmentBinding() == MaterialEnvironmentBinding::Ibl ? 4U : 2U;
  cmd.bindTexture(shadowBinding, shadowMap, shadowMapSampler);
}
```

### P7 — RenderGraph depth-only path: exact, complete, three edits to `execution.cpp`

Full current file read this pass (`src/render_graph/src/execution.cpp`,
221 lines) — the depth-only path below is closed-form, not a "locate at
Implementation time" placeholder.

**Edit 1 — Guard 0 (lines 58-68):** `boundCount` widens from four terms
to five, adding `(binding.shadowMap != nullptr)`; message text names all
five kinds.

**Edit 2 — per-usage transition dispatch (lines 121-137):** the
`if (targetPtr != nullptr) / else if (depthPtr...) / else if (sampledTexturePtr...) / else (hdrColorTargetPtr)`
chain gains a fifth, explicit arm (not a changed final `else`):

```cpp
atlantis::rhi::ShadowMap* shadowMapPtr = binding->shadowMap;
...
} else if (hdrColorTargetPtr != nullptr) {
  commandList.transitionResource(*hdrColorTargetPtr, previous, *usage.state);
} else {
  commandList.transitionResource(*shadowMapPtr, previous, *usage.state);
}
```

**Edit 3 — the `drawPass` `beginRendering()` dispatch (lines 146-176) —
the actual depth-only gap.** Today, `canBeginRendering = colorBinding !=
nullptr && (...)` — a pass with **no** `ColorAttachmentOutput` usage at
all (the shadow pass's own case: its only tagged usage is
`DepthAttachmentReadWrite` on `shadowMap`) always has `colorBinding ==
nullptr` and is unconditionally skipped today. This is the real gap, not
an already-working `if`/`else if` chain needing one more arm:

```cpp
const bool depthOnly = depthUsagePresent && depthBinding != nullptr && depthBinding->shadowMap != nullptr;
const bool canBeginRendering =
    depthOnly ? true : (colorBinding != nullptr && (!depthUsagePresent || depthBinding != nullptr));
if (canBeginRendering) {
  if (depthOnly) {
    commandList.beginRendering(*depthBinding->shadowMap, depthBinding->depthClear);
  } else {
    atlantis::rhi::Texture* depthPtr = depthUsagePresent ? depthBinding->depthTexture : nullptr;
    const float depthClearValue = depthUsagePresent ? depthBinding->depthClear : 1.0f;
    if (colorBinding->target != nullptr) {
      commandList.beginRendering(*colorBinding->target, depthPtr, colorBinding->colorClear, depthClearValue);
    } else {
      commandList.beginRendering(*colorBinding->hdrColorTarget, depthPtr, colorBinding->colorClear, depthClearValue);
    }
  }
  if (executeFn) executeFn(commandList);
  commandList.endRendering();
}
```

**No change needed, confirmed:** the trailing `finalState` loop
(lines 199-217) already excludes any binding that is not `target`/
`sampledTexture`/`hdrColorTarget` (line 200) — a `shadowMap` binding is
already, correctly, never given a trailing transition, matching
`depthTexture`'s own existing precedent ("never presented, never read
back this round"). Guard 2 (lines 83-87) and `isDrawPass()` (lines
37-46) already work unmodified: the shadow pass's own
`DepthAttachmentReadWrite` usage already makes `isDrawPass()` return
`true` with no change, and Guard 2 only scopes `target`-bound entries.
Every resource, including `shadowMap`, starts each `execute()` call from
`Undefined` by convention (line 91, no per-resource override in current
callers) — this is *why* D-4's "first frame and any later empty-caster
frame are the same case" holds structurally: nothing preserves
cross-frame state at this layer for any resource today, `shadowMap`
included; the per-frame `beginRendering()` clear is what re-establishes
a known, valid depth value every single frame, exactly like every
existing resource already does.

### P8 — Descriptor capacity: three independent RHI widenings, one test-only peak sibling

1. `Device::createPipeline()`'s closed check (`vulkan_device.cpp:992-993`):
   `{0, 1, 3}` → `{0, 1, 2, 3, 4}`.
2. `createDescriptorPoolOfSize()`'s sampler sizing (`vulkan_device.cpp:435`):
   `3U * maxSets` → `4U * maxSets`.
3. `VulkanCommandList::textureDescriptorMemos_`: `std::array<TextureDescriptorMemo, 4>`
   → `..., 5>`.

**Descriptor-**set**-count peak is test-only** — no
`kFallbackPipelineCount`-shaped constant exists in production code; the
real capacity mechanism is the growable `kDescriptorPoolMaxSetsByGeneration
= {4, 8, 16, 32}` table (60-set ceiling), unaffected by this Plan. The
"`N+3`/`N+4`" language refers to `material_realization_gpu_tests.cpp`'s
own two existing, synthetic `TEST_CASE`s (`kMaterialPipelineCount=6`,
`kFallbackPipelineCount=1`, `kSkyPipelineCount=1`,
`kSteadyOutputTransformPipelineCount=1`,
`kTransientOutputTransformPipelineCount=1`) — a third, sibling
`TEST_CASE` adds `kShadowCastPipelineCount = 1` to both sums
(`kExpectedSteadySetCount == 10`, `kExpectedPeakSetCount == 11`),
building a real `shadow_cast` Pipeline before the output-transform pair.

### P9 — Existing call-site migration: exhaustively enumerated, not sampled

**(a) `sampledTextureBindingCount` bump — one production line.**
`material_realization.cpp:242-243`:
```cpp
.sampledTextureBindingCount =
    materialData.kind == atlantis::asset_system::MaterialKind::PbrDirectLit && environmentEnabled ? 3U : 1U},
```
becomes `? 4U : 2U`. Both `RuntimeApplication::runFrame()` and
`PbrMaterialDemoFixture` funnel through this one function
(`realizeOneMaterialCandidate()`), so neither needs a separate edit.

**(b) `pbr_render_gpu_tests.cpp`'s four `TEST_CASE`s bypass that
function** — direct `createMaterial()` calls with a hard-coded
`sampledTextureBindingCount = 1` at lines 409-419, 521-531, 617-627,
685-695, each independently → `2`. This file never constructs `pbr_ibl`.

**(c) `material_ibl_selection_gpu_tests.cpp` calls
`realizeOneMaterialCandidate()` directly — confirmed unaffected, not
hedged.** Re-read this pass: its only assertions
(`CHECK(direct.value().material->environmentBinding() == ...::None)`,
`CHECK(ibl.value().material->environmentBinding() == ...::Ibl)`,
lines 69/76) never reference `sampledTextureBindingCount` — (a)'s fix
reaches it with zero further edit required.

**(d) Camera-buffer size literal — real, `rg`-confirmed split, not
uniform.** Only files whose camera buffer already carries the full
PBR/lighting tail need the 592-byte bump; files using the smaller
128-byte (`sizeof(float) * 32`, `minimal_mesh`/`textured_quad`/`lit_textured`-only)
or 304-byte (`sizeof(float) * 32 + sizeof(FrameLightingData)`,
`LitTextured`-lighting-only) buffers are untouched:

| File | Current size | New size |
|---|---|---|
| `src/runtime/src/runtime_application.cpp` (line 471) | `464` | `592` |
| `tests/image_regression/fixture/pbr_material_demo_fixture.cpp` (line ~275) | `464` | `592` |
| `tests/runtime/pbr_render_gpu_tests.cpp` (4 sites: 400, 535, 631, 699) | `sizeof(float)*32 + sizeof(FrameLightingData) + sizeof(CameraWorldPositionData)` (= 320) | `592` |
| `tests/renderer/renderer_ownership_tests.cpp` (3 sites: 391, 441, 503, `FakeBuffer` doubles) | `464` | `592` |
| `tests/runtime/pbr_reflection_cross_check_tests.cpp` | asserts `irradianceSh->offset + irradianceSh->size == 464` (SH's own end offset) | assertion **unchanged** — SH still ends at 464; only the test's own title/comment mentioning "a 464-byte block" needs updating to describe the now-larger 592-byte total |

`tests/image_regression/fixture/{lighting_demo,material_demo}_fixture.cpp`
(both `sizeof(float)*32 + sizeof(FrameLightingData)` = 304,
`LitTextured`/`UnlitTextured` only) and every `sizeof(float) * 32` = 128
site (`examples/{headless_rendering_demo,minimal_renderer_demo}/main.cpp`,
`tests/image_regression/fixture/{minimal_cube,textured_quad,world_scene,world_scene_loaded}_fixture.cpp`,
`tests/vulkan_backend/{descriptor_pool_growth,headless_rendering,minimal_renderer,pipeline_depth_write}_gpu_tests.cpp`)
are **not touched** — confirmed by direct `rg` inspection of every
`BufferPurpose::Uniform` construction in the repository, not sampled.

**(e) `drawFrame()`'s five new required parameters — the real, complete,
`rg`-confirmed list of every call site in the repository (16 files, 25
call sites total):**

| File | Call sites |
|---|---|
| `src/runtime/src/runtime_application.cpp` | 1 (line 1176) |
| `tests/image_regression/fixture/pbr_material_demo_fixture.cpp` | 1 (line 521) |
| `tests/image_regression/fixture/lighting_demo_fixture.cpp` | 1 (line 412) |
| `tests/image_regression/fixture/material_demo_fixture.cpp` | 1 (line 403) |
| `tests/image_regression/fixture/minimal_cube_fixture.cpp` | 1 (line 364) |
| `tests/image_regression/fixture/textured_quad_fixture.cpp` | 2 (lines 421, 507) |
| `tests/image_regression/fixture/world_scene_fixture.cpp` | 1 (line 482) |
| `tests/image_regression/fixture/world_scene_loaded_fixture.cpp` | 1 (line 477) |
| `tests/image_regression/sky_background_gpu_tests.cpp` | 1 (line 263) |
| `tests/renderer/renderer_ownership_tests.cpp` | 9 (lines 145, 235, 240, 292, 342, 401, 418, 456, 515) |
| `tests/runtime/pbr_render_gpu_tests.cpp` | 1 (line 362, shared `renderOneFrame()` helper — reached by all 4 `TEST_CASE`s) |
| `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp` | 1 (line 507) |
| `tests/vulkan_backend/headless_rendering_gpu_tests.cpp` | 1 (line 457) |
| `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` | 1 (line 427) |
| `examples/headless_rendering_demo/main.cpp` | 1 (line 562) |
| `examples/minimal_renderer_demo/main.cpp` | 1 (line 703) |

`ibl_material_demo_fixture.{h,cpp}` is a pure forwarding alias to
`PbrMaterialDemoFixture` and needs no edit of its own.
`tests/vulkan_backend/pipeline_depth_write_gpu_tests.cpp` does not call
`drawFrame()` at all (confirmed — it drives its own Pipelines directly)
and is unaffected.

Every non-Runtime, non-`pbr_material_demo_fixture` file above needs only
a **minimal, always-possible** `ShadowMap`/`Sampler`/`Pipeline`/`Buffer`
(real for GPU-backed tests — see P9(f) for exactly where each one's own
`shadow_cast.slang` SPIR-V comes from; `FakeShadowMap` — new, mirroring
`FakeHdrColorTarget` — for `renderer_ownership_tests.cpp`'s pure-unit
doubles) — `shadowCasterDrawItems` stays empty; none of these need a
real occluder. **Zero behavioral change**, confirmed by each file's own
existing tests/golden continuing to pass unmodified.

**(f) Shader-artifact provenance for every real-GPU/example target
needing a real `shadow_cast` Pipeline — every `add_dependencies`/
compile-definition/copy site, confirmed by direct `rg` reading of every
CMakeLists.txt this touches, not sampled.** Two existing conventions in
this codebase, and every consumer of each:

**Convention 1 — compile-definition (`ATLANTIS_<PREFIX>_..._SHADER_DIR`
pointing at the build-tree `${ATLANTIS_<name>_SHADER_OUTPUT_DIR}`
absolute path, no file copy).** Add
`ATLANTIS_<PREFIX>_SHADOW_CAST_SHADER_DIR="${ATLANTIS_shadow_cast_SHADER_OUTPUT_DIR}"`
and `shadow_cast_shaders` to `add_dependencies(...)`, mirroring exactly
how `..._PBR_DIRECT_LIT_SHADER_DIR`/`pbr_direct_lit_shaders` is already
wired for each target below:

| CMakeLists.txt | Target(s) | New macro prefix |
|---|---|---|
| `src/runtime/CMakeLists.txt` | `atlantis_runtime` | `ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR` |
| `tests/runtime/CMakeLists.txt` | `atlantis_runtime_gpu_tests` | `ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR` |
| `tests/shader_system/CMakeLists.txt` | `atlantis_shader_system_tests` | `ATLANTIS_SHADOW_CAST_SHADER_DIR` |
| `tests/image_regression/CMakeLists.txt` | `atlantis_image_regression_gpu_tests` | `ATLANTIS_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR`, `ATLANTIS_LIGHTING_DEMO_SHADOW_CAST_SHADER_DIR`, `ATLANTIS_PBR_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR`, `ATLANTIS_HDR_ROLL_OFF_DEMO_SHADOW_CAST_SHADER_DIR`, `ATLANTIS_IBL_DEMO_SHADOW_CAST_SHADER_DIR` (5 — one per fixture-config reusing `pbr_material_demo_fixture.cpp`/`material_demo_fixture.cpp`/`lighting_demo_fixture.cpp`) |
| `tests/image_regression/golden_generator/CMakeLists.txt` | `atlantis_image_regression_material_demo_golden_generator` | `ATLANTIS_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR` |
| (same file) | `atlantis_image_regression_lighting_demo_golden_generator` | `ATLANTIS_LIGHTING_DEMO_SHADOW_CAST_SHADER_DIR` |
| (same file) | `atlantis_image_regression_pbr_material_demo_golden_generator` | `ATLANTIS_PBR_MATERIAL_DEMO_SHADOW_CAST_SHADER_DIR` |
| (same file) | `atlantis_image_regression_ibl_material_demo_golden_generator` | `ATLANTIS_IBL_DEMO_SHADOW_CAST_SHADER_DIR` |
| (same file) | `atlantis_image_regression_hdr_roll_off_demo_golden_generator` | `ATLANTIS_HDR_ROLL_OFF_DEMO_SHADOW_CAST_SHADER_DIR` |

**Convention 2 — plain relative path (`shaders/shadow_cast.{vert,frag}.spv`,
`WORKING_DIRECTORY`-resolved, copied via `POST_BUILD`
`copy_if_different`).** Add `shadow_cast_shaders` to the target's
existing `add_dependencies(...)` and one new `copy_if_different` command
(the same four files: `.vert.spv`, `.vert.refl.json`, `.frag.spv`,
`.frag.refl.json`) to its existing `POST_BUILD` block:

| CMakeLists.txt | Target(s) | Covers (via this Plan's own call-site table) |
|---|---|---|
| `tests/vulkan_backend/CMakeLists.txt` | `atlantis_vulkan_backend_gpu_tests` | `descriptor_pool_growth`, `headless_rendering`, `minimal_renderer` GPU tests (shared executable, one copy block) |
| `examples/headless_rendering_demo/CMakeLists.txt` | `atlantis_headless_rendering_demo` | its own `main.cpp` |
| `examples/minimal_renderer_demo/CMakeLists.txt` | `atlantis_minimal_renderer_demo` | its own `main.cpp` |
| `tests/image_regression/CMakeLists.txt` | `atlantis_image_regression_gpu_tests` (same target as Convention 1's row above — needs **both**) | `minimal_cube`, `textured_quad`, `world_scene`, `world_scene_loaded` fixtures (shared executable, one copy block) |
| `tests/image_regression/golden_generator/CMakeLists.txt` | `atlantis_image_regression_golden_generator` (bare `main.cpp`, minimal_cube) | its own capture |
| (same file) | `atlantis_image_regression_world_scene_golden_generator` | its own capture |
| (same file) | `atlantis_image_regression_textured_quad_golden_generator` | its own capture |

`tests/renderer/CMakeLists.txt` needs **no** shader wiring at all
(confirmed — `renderer_ownership_tests.cpp` links no shader macro or
`add_dependencies` today; its `FakeShadowMap` is a pure C++ double, no
real Vulkan device involved).
`tests/vulkan_backend/pipeline_depth_write_gpu_tests.cpp` (same
executable as the three Convention-2 vulkan_backend tests above) needs
no new wiring of its own — confirmed unaffected (P9(e)) — Milestone 2's
own new `shadow_map_render_gpu_tests.cpp` test reuses the
already-copied `minimal_mesh`/`textured_quad` pairs, not `shadow_cast`.

### P10 — Real-GPU discriminating verification: exact points, projection, thresholds

**New file:** `tests/image_regression/shadow_gpu_tests.cpp` (mirrors
`sky_background_gpu_tests.cpp`'s placement/style). No-directional-light
byte compatibility is **not** retested here — it is already a hard,
golden-backed requirement on `ibl_material_demo` (Milestone 10's own
golden-strategy bullet); this file tests shadow mechanics only:
occlusion, movement, out-of-bounds, first-use-vs-reused, and R1/R2/R3
IBL isolation.

**Fixed scene (Group A, no environment).** Hand-built via `createMesh()`
with the real 44-byte `Vertex{position,color,uv,normal}` layout:

- Ground quad: `x,z ∈ [-6,6]`, `y=0`, normal `+Y`, `PbrDirectLit`
  Material, `baseColorFactor = (0.8,0.8,0.8,1)`, `metallicFactor = 0.0`,
  `roughnessFactor = 0.8` (a small solid-color `SampledTexture`+`Sampler`
  supplies this, mirroring `pbr_render_gpu_tests.cpp`'s `rig.texture`/
  `rig.sampler`).
- Occluder: `1×1×1` cube centered at `(0, 1.5, 0)`, same Material.
- A second, small `2×2` quad centered at `(7.765, 0, -11.647)`, same
  Material — the out-of-bounds probe. This point is deliberately chosen
  as `origin + right_L · 14`, where `right_L` is the light view's own
  right-axis basis vector (derived below) — not a world-space `|x|`/`|z|`
  comparison against the `8` half-extent, which does **not** correctly
  predict light-space membership for a rotated light frame (see the
  correction note after check 4).
- Camera: eye `(0, 6, 10)`, forward `= normalize((0,0,0)-eye)`, world-up
  `(0,1,0)`, `kFovYRadians = 60°`, aspect `1.0`, `kNearZ = 0.1`,
  `kFarZ = 100.0` (`sky_background_gpu_tests.cpp`'s own constants).
  Written via `lookAtMatrixFromForward`/`perspectiveMatrixDirect` (same
  functions that file already establishes).
- Directional light: `direction = normalize(-0.3, -1.0, -0.2)`
  `≈ (-0.28224, -0.94046, -0.18816)`, `color = (1,1,1)`,
  `intensity = 3.0`, written directly into the camera buffer's
  `directionalLights[0]` (offset 144) plus `directionalLightCount = 1`
  (offset 128).

**Shadow-footprint formula (exact, reused for every occluder position/
light direction below — this is the one fixed method, not a per-case
guess):** for an occluder center at world `(cx, cy, cz)` and a unit
light direction `d`, the ground-plane (`y=0`) shadow-footprint center is
`t = -cy / d.y`, `footprint = (cx + t·d.x, 0, cz + t·d.z)`. Camera pixel
projection: `viewX = dot(p-eye, right)`, `viewY = dot(p-eye, camUp)`,
`viewZ = -dot(p-eye, forward)`, `clipX = f·viewX`, `clipY = -f·viewY`
(`f = 1/tan(30°) = 1.732051`), `clipW = -viewZ`,
`pixel = round((clip/clipW + 1)/2 · 512)`.

**Light-space/shadow-NDC formula (P5's own fixed derivation, applied
here) — this is the method check 4 actually requires, not the camera
formula above:** light eye `= center - d·(far-near)/2 = -d·14.95`; light
forward `= d`; light `right_L = normalize(cross(d, worldUp))`, light
`camUp_L = cross(right_L, d)` (P11's own up-vector rule applies
identically here — irrelevant for this `d`, since `|cross(d,(0,1,0))| ≈
0.339`, far above the `1e-6` degeneracy threshold). For a world point
`p`: `lightViewX = dot(p - lightEye, right_L)`,
`lightViewY = dot(p - lightEye, camUp_L)`,
`lightForwardDist = dot(p - lightEye, d)`; shadow NDC
`= (lightViewX/8, -lightViewY/8, (lightForwardDist-0.1)/29.9)` (D-5's own
`[0,1]`-depth, `[-1,1]`-XY convention). In-bounds requires all three
components inside `[-1,1]`/`[-1,1]`/`[0,1]` respectively (D-5's
`inBounds` expression) — a check on this transform, never on raw world
`x`/`z` against `8`, which is what check 4 below fixes.

For this scene's `d ≈ (-0.28224,-0.94046,-0.18816)`: `lightEye ≈
(4.220, 14.062, 2.814)`, `right_L ≈ (0.5547, 0, -0.8320)`,
`camUp_L ≈ (-0.7824, 0.3392, -0.5217)` (hand-computed this Plan; a
degenerate-basis or arithmetic error here would itself be a Non-negotiable-rule
stop-and-ask item, not something Implementation silently re-derives
differently).

Both formulas above, not a re-derivation, are what Implementation must
literally execute — an integer-rounding difference of a pixel or two, or
a shadow-NDC difference in the fourth decimal place, against this Plan's
own hand-computed values below is expected and is **not** a value
change requiring stop-and-ask; a different formula, a different scene
position, or a different light direction is.

Checks:

1. **Occlusion.** Occluder at `(0,1.5,0)` → `t = 1.5951`, footprint
   `P ≈ (-0.450, 0, -0.300)` → pixel **(239, 250)**. Reference point
   `Q = (3.0, 0, 3.0)` (well outside the footprint, same ground plane)
   → pixel **(402, 331)**. With the occluder present in
   `shadowCasterDrawItems`: `luminance(P) == 0` **exactly** —
   `pbr_direct_lit.slang:124`'s own "no ambient term" comment, confirmed
   in the current shader source, means a fully-shadowed point's
   `accumulated` stays at exactly `(0,0,0)` through HDR/tonemap/sRGB
   (zero is a fixed point of both). `luminance(Q) > 30` (RGB8 sum,
   `0-765` range) — a conservative, disclosed floor: this Plan does not
   hand-derive the exact tonemapped/sRGB-encoded value of an 80%-albedo
   Lambertian-dominant surface under `NdotL = 0.940`, but any
   non-degenerate result clears `30` by a wide margin.
2. **Movement — occluder translation.** Move the occluder to
   `(1.0, 1.5, 0.0)`; by the formula above (`t` depends only on `cy`,
   unchanged), the new footprint is the old one shifted by the same
   `(+1, 0)` world-space delta: `P' ≈ (0.550, 0, -0.300)` → pixel
   **(276, 250)**. Check: `luminance(P') == 0` (now shadowed);
   `luminance` at the **old** `P ≈ (-0.450,0,-0.300)` / pixel `(239,250)`
   is now `> 30` (no longer shadowed).
3. **Movement — light-direction change.** Restore the occluder to
   `(0,1.5,0)`; change light direction to
   `normalize(0.3, -1.0, -0.2) ≈ (0.28224, -0.94046, -0.18816)` (mirrored
   in `x`). Recompute the footprint with the **same formula** and this
   new `d`: `t = 1.5951` (unchanged, `|d.y|` unchanged),
   `P'' ≈ (0.450, 0, -0.300)` → pixel **(276, 250)** — the same
   destination pixel as check 2's `P'`, since both moves shift the
   footprint by the identical `+0.45` in `x` (a real, checkable
   coincidence of this Plan's own chosen numbers, not an error).
   `luminance(P'') == 0`.
4. **Out-of-bounds — via the shadow-NDC transform, not world `x`/`z`.**
   Probe quad center `(7.765, 0, -11.647) = origin + right_L·14`. By
   construction (`right_L` is unit-length and orthogonal to both `d` and
   `camUp_L`): `lightViewX ≈ 14.0`, `lightViewY ≈ 0.0`,
   `lightForwardDist ≈ 14.95` (identical to the origin's own forward
   distance, since moving purely along `right_L` doesn't change depth).
   Shadow NDC `≈ (14.0/8, -0.0/8, (14.95-0.1)/29.9) = (1.75, 0.00, 0.497)`.
   **`shadowNdc.x = 1.75` is the out-of-bounds component** — `0.75`
   beyond the valid `[-1,1]` range (75% of the valid half-width; `6`
   world units beyond the light-space `±8` boundary along `right_L`).
   `shadowNdc.y` and `shadowNdc.z` both land safely inside their own
   valid ranges (`0.00` and `0.497`), so this probe isolates exactly one
   failing axis, not an accidental multi-axis coincidence. Camera pixel
   (same camera as every other check): eye-to-point `(7.765,-6,-21.647)`,
   `viewX=7.765`, `viewY≈5.992`, `viewZ≈-21.650`, `clipX≈13.449`,
   `clipY≈-10.379`, `clipW≈21.650`, `ndc≈(0.6213,-0.4794)` → pixel
   **(415, 133)**. D-5's out-of-bounds-is-lit rule requires
   `luminance(pixel(415,133)) > 30` (the same conservative floor as
   check 1) under check 1's light/occluder configuration, regardless of
   the occluder never actually reaching this point geometrically either.

   **Correction note, kept for the record:** an earlier draft of this
   check used world point `(0,0,-9.5)` and compared `|{-9.5}| > 8`
   directly. Transformed through the formula above, that point's own
   `shadowNdc.x ≈ 0.988` — inside `[-1,1]`, i.e. **not actually
   out-of-bounds** (a rotated light frame does not align with world
   axes, so a world-space magnitude comparison against the half-extent
   is not a valid membership test). That point is not used by this Plan.
5. **First-use vs. reused `ShadowMap` (the actual comparison).** Two
   renders, identical nonzero light (check 1's configuration) and
   identical geometry, both with `shadowCasterDrawItems` empty: (i) the
   very first frame against a freshly-created `ShadowMap`, (ii) a later
   frame reusing that same `ShadowMap` after ≥1 prior frame rendered
   through it. Byte-identical full-frame comparison — `firstDifferingByte()`
   (mirroring `sky_background_gpu_tests.cpp`'s own helper) `==` the
   buffer's own size.
6. Vulkan Validation Layers clean throughout.

**Re-verification of P, Q, P′, P″ against the same shadow-NDC
transform (checks 1-3 above), prompted by finding check 4's own
original error — no error found, none of these change:**
`P ≈ (-0.450,0,-0.300)` → `shadowNdc ≈ (0.00003, -0.0063, 0.503)` — safely
inside on all three axes (its `x` is ≈0 by construction: it lies on the
light ray through the occluder center, which sits directly above the
origin the light aims at, and `right_L·d = 0` always, so every point on
that ray shares the origin's own `lightViewX = 0`). `Q = (3,0,3)` →
`shadowNdc ≈ (-0.104, 0.489, 0.449)` — safely inside (stronger than it
needs to be: it doesn't even rely on D-5's out-of-bounds-is-lit escape
hatch). `P′ ≈ (0.550,0,-0.300)` → `shadowNdc ≈ (0.069, 0.034, 0.493)` —
safely inside. `P″ ≈ (0.450,0,-0.300)` (check 3's mirrored-light-direction
case) is not separately recomputed — the whole check-3 configuration
(light direction mirrored in `x`, occluder and volume both already
symmetric in `x`) is the exact mirror image of check 1's, so `P″`'s own
shadow-NDC membership mirrors `P`'s by the same symmetry, with no new
arithmetic needed.

**Group B — R1/R2/R3 IBL isolation (needs environment; reuses
`IblMaterialDemoFixture`).** Identical geometry, camera, and light as
Group A's check 1 (occluder at `(0,1.5,0)`, sample point `P` at pixel
`(239,250)`):

- **R1 (shadowed):** occluder present in `shadowCasterDrawItems`,
  `directionalLightCount = 1`.
- **R2 (unshadowed control, same nonzero light):** identical scene,
  `shadowCasterDrawItems` empty, `directionalLightCount` still `1`.
- **R3 (light-off IBL/ambient reference):** identical scene,
  `directionalLightCount = 0`.
- **Check 1 (positive control):** `luminance(R2[P]) - luminance(R1[P]) > 15`
  — a conservative, disclosed floor (same rationale as check 1 above:
  the exact IBL+direct BRDF+tonemap output is not hand-derived here,
  but a real, nonzero directional contribution at `NdotL = 0.940` should
  clear a margin this small easily).
- **Check 2 (IBL isolation):** `|luminance(R1[P]) - luminance(R3[P])| <= 5`
  — `P` is chosen well inside the shadow's own interior (not near an
  edge — the occluder's `1×1` footprint at this light angle projects to
  roughly a `1×1`-unit ground region centered on `P`, comfortably larger
  than any bias-affected boundary sliver), so R1 and R3 should differ
  only by quantization/floating-point noise if the shadow factor
  correctly leaves the IBL/ambient term untouched.

### P11 — Light-space up-vector: real convention checked, a deliberate new choice, not a reuse

**Checked, not assumed:** `extractCameraMatrices()`
(`scene_extraction.cpp:121-150`) computes `right = cross(forward,
worldUp)` and, if `length(right) < kDegenerateLengthEpsilon` (`= 1e-6f`,
`scene_extraction.cpp:12`), returns
`SceneExtractionError::DegenerateCameraBasis` — **this codebase's only
existing precedent for a near-parallel forward/up is fail-fast, not an
up-vector swap.** `lookAtMatrix()` itself has no internal fallback at
all; the caller must check first.

**Fail-fast is not appropriate for the light-space view**, unlike a
camera: a directional light pointing straight down `(0,-1,0)` — exactly
parallel to world-up `(0,1,0)` — is an ordinary, expected "sun overhead"
case, not a scene-authoring mistake. This Plan therefore introduces the
first fallback-based handling in this codebase, as a deliberate,
disclosed new choice:

```cpp
Vec3 up{0.0f, 1.0f, 0.0f};
if (length(cross(direction, up)) < kDegenerateLengthEpsilon) {  // same test and threshold as extractCameraMatrices()
  up = Vec3{0.0f, 0.0f, 1.0f};
}
```

Reuses the exact same cross-product-length test and the exact same
`1e-6f` threshold `extractCameraMatrices()` already established — only
the *response* to degeneracy (swap, not fail) is new.

## Milestones / Task Breakdown

Sequenced so every commit compiles: RHI/RenderGraph/shader foundations
first (Milestones 1-7), then Runtime's own new resources with **no**
`drawFrame()` signature change yet (Milestone 8, compiles standalone),
then the signature change landed as **one** atomic commit touching
every one of the 25 call sites at once (Milestone 9), then verification
(Milestone 10).

### Milestone 1 — RHI: `ShadowMap` resource + `hasColorAttachment` + colorBlendState gate

- `ShadowMap`/`ShadowMapCreateParams`/`ShadowMapCreateError`,
  `Device::createShadowMap()`, `VulkanShadowMap`,
  `shadow_map_capability.h`/`.cpp`, `toShadowMapCreateError()` (P1).
- `PipelineCreateParams::hasColorAttachment`; the `VkPipelineRenderingCreateInfo`
  gate and the `VkPipelineColorBlendStateCreateInfo` gate (P2, both
  exact edits given above).
- GPU-independent: default `hasColorAttachment = true` reproduces every
  existing Pipeline's behavior; `ShadowMapCreateParams` construction/
  equality tests mirroring `HdrColorTargetCreateParams`'s own.
- Real-GPU: `createShadowMap()` at P4's resolution succeeds on the
  target GPU, exercising the real format-capability query.

### Milestone 2 — RHI: depth-only `beginRendering()`/`transitionResource()`/memoized `bindTexture()`

- New `beginRendering(ShadowMap&, float)`, `transitionResource(ShadowMap&, ...)`,
  `bindTexture(uint32_t, const ShadowMap&, const Sampler&)` overloads
  (P2) — depends on Milestone 6's `textureDescriptorMemos_` widening
  landing first or in the same commit.
- Real-GPU test, new file `tests/vulkan_backend/shadow_map_render_gpu_tests.cpp`
  (mirrors `pipeline_depth_write_gpu_tests.cpp`'s three-draw-discriminator
  shape) — **fixed to downstream color-sample verification, no new
  `ShadowMap` readback API:** a depth-only Pipeline (reusing the
  already-compiled `minimal_mesh` shader pair, matching
  `pipeline_depth_write_gpu_tests.cpp`'s own established reuse) writes
  real depth into a `ShadowMap` via the new `beginRendering()` overload;
  a second Pipeline reusing the already-compiled `textured_quad` shader
  pair unchanged (its `Sampler2D texturedSampler` binding is agnostic to
  whether a `SampledTexture` or a `ShadowMap` backs it at the SPIR-V
  level) binds the `ShadowMap` via the new `bindTexture(1, const
  ShadowMap&, const Sampler&)` overload and writes the sampled depth
  directly into a normal color `RenderTarget`; the readback goes through
  the **existing** `copyRenderTargetToBuffer()` path, identical to every
  other GPU test in this repository. One `CommandList`, one `submit()`.

### Milestone 3 — RenderGraph: `ResourceBinding`, Guard 0, and the depth-only `beginRendering()` path

- `ResourceBinding::shadowMap` field; Guard 0's five-term `boundCount`
  (P7 Edit 1).
- The per-usage transition dispatch's fifth arm (P7 Edit 2).
- The `depthOnly` branch in the `drawPass` `beginRendering()` dispatch
  (P7 Edit 3) — the real fix, not an assumed-working chain.
- New tests (none of this exists today — confirmed by search, not
  mirrored from an existing "four-kind" test): Guard 0 rejects a
  `ResourceBinding` binding zero or two-or-more of the five kinds; a
  `shadowMap`-only, no-`colorBinding` pass compiles and executes via the
  new `depthOnly` path in isolation (a minimal synthetic graph).

### Milestone 4 — `shadow_cast` shader, descriptor contract, CMake wiring

- `shaders/shadow_cast/shadow_cast.slang` (P3, real reflection already
  confirmed this Plan); `shaders/shadow_cast/CMakeLists.txt` (mirrors
  `shaders/sky/CMakeLists.txt`: `NAME shadow_cast`,
  `EXPECTED_CONTRACT shadow-cast`, the same `PARENT_SCOPE` re-export);
  root `CMakeLists.txt` gains `add_subdirectory(shaders/shadow_cast)`
  before `add_subdirectory(src/runtime)`.
- `shadowCastVertexLayout()` (Runtime-private, duplicated per this
  codebase's own established "duplicated, not shared" convention).
- `shadowCastExpectedDescriptorContract()`
  (`descriptor_contract.h`/`.cpp`); `compile_and_validate.cpp`'s new
  `"shadow-cast"` dispatch arm (P3).
- `tests/shader_system/shadow_cast_reflection_tests.cpp` (new, mirrors
  `sky_reflection_tests.cpp`) — a real, freshly-compiled reflection
  check confirming the contract this Plan already verified via probe.
  `tests/shader_system/CMakeLists.txt` gains this test's own
  `ATLANTIS_SHADOW_CAST_SHADER_DIR`/`shadow_cast_shaders` wiring now
  (P9(f)) — this one target's wiring is not tied to the atomic
  `drawFrame()` commit, since this test never calls `drawFrame()`.
- This Milestone creates the `shadow_cast_shaders` CMake target itself
  (`shaders/shadow_cast/CMakeLists.txt` + root `add_subdirectory`) —
  every *other* target's own dependency on it (every row in P9(f)'s two
  tables besides `atlantis_shader_system_tests` above) is wired in
  Milestone 9, landing together with the C++ code that actually reads
  the resulting shader path, not as a disconnected CMake-only change
  here.

### Milestone 5 — Light-space buffers, shader content, descriptor extension

- `pbr_direct_lit.slang`/`pbr_ibl.slang`: the light-space tail fields
  (P5), the shadow-map sampler binding (2 / 4), D-5's shadow-factor
  multiply at the one confirmed insertion line (`pbr_ibl.slang:145`;
  the byte-identical line in `pbr_direct_lit.slang`).
- `pbrDirectLitExpectedDescriptorContract()`/`pbrIblExpectedDescriptorContract()`:
  one new `Sampler`/Fragment entry each (binding 2 / 4).
- `tests/shader_system/{pbr_direct_lit,pbr_ibl}_reflection_tests.cpp`
  (existing, extended): confirm the new tail offset (464) and binding
  against real, freshly-compiled reflection.
- The five camera-buffer-size edits from P9(d)'s table, landed in this
  same commit (buffer size and shader content stay in sync; this does
  not require the `drawFrame()` signature change and compiles
  standalone).

### Milestone 6 — RHI capacity widenings (three, together)

- `ATLANTIS_CHECK` widening, descriptor-pool `3U → 4U`,
  `textureDescriptorMemos_` `4 → 5` (P8).
- Tests mirroring `descriptor_pool_growth_gpu_tests.cpp`'s coverage: a
  Pipeline with `sampledTextureBindingCount = 2` and one with `= 4` both
  create and bind successfully.

### Milestone 7 — Descriptor-set-count sibling test

- New `TEST_CASE` in `material_realization_gpu_tests.cpp` (P8): `N+4`/
  `N+5` with a real `shadow_cast` Pipeline present, alongside the
  existing `N+2`/`N+3` and `N+3`/`N+4`-with-sky cases (neither touched).
  Depends on Milestone 4 (needs a real `shadow_cast` Pipeline).

### Milestone 8 — Runtime's own new resources (no `drawFrame()` signature change yet — compiles standalone)

- `runtime_application.h`: `shadowMap_`, `shadowMapSampler_`,
  `shadowCastPipeline_`, `shadowLightSpaceBuffer_`, declared immediately
  after `skyPipeline_`.
- `bootstrap_config.h`: `shadowCastVertexShaderSpirvPath`/`...ReflectionPath`/
  `shadowCastFragmentShaderSpirvPath`/`...ReflectionPath` — unconditionally
  required (shadow infrastructure is unconditional, P1);
  `bootstrap_config.cpp` gains a new, always-checked block.
- `init_error.h`/`.cpp`: `ShadowMapCreateFailed`, `ShadowSamplerCreateFailed`,
  `ShadowCastPipelineCreateFailed`, `ShadowLightSpaceBufferCreateFailed`.
- `runtime_application.cpp`: `initializeSteps()` gains an unconditional
  shadow-shader-load step and an unconditional step (immediately after
  `skyPipeline_`'s own creation) building `shadowMap_`/`shadowMapSampler_`/
  `shadowCastPipeline_`/`shadowLightSpaceBuffer_`, fatal on error.
  `shutdown()` resets the four new members alongside `skyPipeline_.reset()`.
  **`runFrame()`'s own `drawFrame()` call site is deliberately NOT
  touched in this Milestone** — that happens atomically in Milestone 9.
- `main.cpp`/`CMakeLists.txt`: `ATLANTIS_RUNTIME_SHADOW_CAST_SHADER_DIR`
  wiring (mirrors the sky block), `shadow_cast_shaders` added to
  `add_dependencies(atlantis_runtime ...)`.
- This Milestone's own build is verified green before Milestone 9
  starts — the new members/resources exist and are exercised by their
  own construction/destruction, but nothing yet calls the new
  `drawFrame()` parameters.

### Milestone 9 — `Renderer::drawFrame()` signature, "shadow" pass, and every call site — one atomic commit

Everything in this Milestone lands together; no intermediate state is
committed.

- `Renderer::drawFrame()`'s five new required parameters (P6); the new
  "shadow" RenderGraph pass; the binding-index-aware shadow-map bind
  (P6); `material_realization.cpp:243`'s `3U:1U → 4U:2U` bump (P9a);
  `pbr_render_gpu_tests.cpp`'s four independent `1 → 2` literals (P9b).
- **Every one of the 25 call sites in P9(e)'s table**, updated in this
  same commit: `runtime_application.cpp`'s `runFrame()` now passes
  `shadowMap_`/`shadowMapSampler_`/`shadowCastPipeline_`/
  `shadowLightSpaceBuffer_`/`shadowCasterDrawItems` (empty when no
  directional light); `pbr_material_demo_fixture.h`/`.cpp` gain the same
  four fields, created unconditionally in
  `setUpPbrMaterialDemoFixture()`; every other file in the table gets
  its own minimal, always-possible resource set (real for GPU tests, a
  new `FakeShadowMap` — mirroring `FakeHdrColorTarget` — for
  `renderer_ownership_tests.cpp`'s 9 call sites) — **every real (not
  `FakeShadowMap`) resource set's own `shadow_cast.slang` SPIR-V comes
  from exactly the CMake wiring P9(f) specifies for that file's own
  executable**, landed in this same commit: every row of P9(f)'s two
  tables except `atlantis_shader_system_tests` (already wired in
  Milestone 4).
- `golden_generator/pbr_material_demo_main.cpp`'s `buildConfig()` gains
  the unconditional shadow-shader-path block in both its
  `ATLANTIS_IBL_GOLDEN_GENERATOR` and non-IBL branches, reading the
  `ATLANTIS_{PBR_MATERIAL_DEMO,HDR_ROLL_OFF_DEMO,IBL_DEMO}_SHADOW_CAST_SHADER_DIR`
  macros P9(f) adds to their own three golden-generator executables.
- `renderer_ownership_tests.cpp`: every existing `TEST_CASE` updated to
  compile (existing assertions otherwise unchanged); one new `TEST_CASE`
  asserts the "shadow" pass's draw sequence records strictly before
  "draw"'s own sequence (mirrors the existing sky-ordering `TEST_CASE`
  shape); one new `TEST_CASE` confirms the shadow-map bind lands at
  binding 2 for a plain `PbrDirectLit` `DrawItem` and binding 4 for an
  `environmentBinding() == Ibl` one, in the same frame.
- Depends on Milestones 1, 3, 4, 5, 6, 8 all landing first.

### Milestone 10 — Verification and golden strategy

- P10 Group A (occlusion, movement ×2, out-of-bounds, first-use-vs-reused)
  and Group B (R1/R2/R3 IBL isolation), in
  `tests/image_regression/shadow_gpu_tests.cpp`.
- Golden strategy: `minimal_cube`, `world_scene`, `textured_quad`,
  `material_demo`, `lighting_demo` — existing, unmodified tests confirm
  byte-identical. `ibl_material_demo` — hard requirement, a real
  pixel-identity check against the existing committed golden, asserted
  directly; **this is also the no-directional-light byte-compatibility
  proof** (this scene has none, by design) — `shadow_gpu_tests.cpp`
  itself does not retest it. `pbr_material_demo`/`hdr_roll_off_demo` —
  run existing tests first; re-capture (ADR-0042) requested only if an
  actual difference appears, never presumed.
- Full `ctest -LE gpu`, `ctest -L gpu`, Debug and Release; a fresh
  `ATLANTIS_BUILD_TESTS=OFF` build; Vulkan Validation Layers clean.

## Files / Modules Touched (expected)

- RHI: `src/rhi/include/atlantis/rhi/{types.h,shadow_map.h(new),device.h,command_list.h}`.
- Vulkan Backend: `src/vulkan_backend/src/{vulkan_device.cpp,vulkan_command_list.h,vulkan_command_list.cpp,vulkan_result.cpp,shadow_map_capability.h(new),shadow_map_capability.cpp(new),vulkan_shadow_map.h(new)}`.
- RenderGraph: `src/render_graph/include/atlantis/render_graph/execution.h`,
  `src/render_graph/src/execution.cpp` (the three exact edits in P7).
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
  `tests/runtime/{pbr_render_gpu_tests.cpp,material_realization_gpu_tests.cpp,pbr_reflection_cross_check_tests.cpp}`
  (`material_ibl_selection_gpu_tests.cpp` confirmed to need **no** edit, P9c),
  `tests/image_regression/fixture/pbr_material_demo_fixture.h`/`.cpp`,
  `tests/image_regression/golden_generator/pbr_material_demo_main.cpp`,
  `tests/image_regression/golden_generator/CMakeLists.txt`,
  `tests/image_regression/shadow_gpu_tests.cpp` (new),
  `tests/image_regression/sky_background_gpu_tests.cpp`,
  `tests/image_regression/CMakeLists.txt`,
  and every remaining file in P9(e)'s 16-file table (signature-compatibility
  only, no assertion change): `tests/image_regression/fixture/{lighting_demo,material_demo,minimal_cube,textured_quad,world_scene,world_scene_loaded}_fixture.cpp`,
  `tests/vulkan_backend/{descriptor_pool_growth,headless_rendering,minimal_renderer}_gpu_tests.cpp`,
  `examples/{headless_rendering_demo,minimal_renderer_demo}/main.cpp`.
- CMake (every target in P9(f)'s two tables, cross-checked against the
  files above — none omitted): `examples/headless_rendering_demo/CMakeLists.txt`,
  `examples/minimal_renderer_demo/CMakeLists.txt`, plus the six
  CMakeLists.txt files already listed above (`tests/vulkan_backend/`,
  `tests/runtime/`, `tests/shader_system/`, `tests/image_regression/`,
  `tests/image_regression/golden_generator/`, `src/runtime/`) — each
  gains exactly the `shadow_cast_shaders` dependency plus either the
  compile-definitions or the `copy_if_different` command P9(f) specifies
  for it, no more and no less.
- Documentation: this Plan; `specs/README.md` status pointer, updated at
  PR time.

Any Implementation-touched source outside this list is a disclosed Plan
deviation, per AGENTS.md.

## Sequencing & Dependencies

Milestones 1-2 (RHI) have no dependency and unblock everything else.
Milestone 3 (RenderGraph) depends on Milestone 1. Milestone 4
(`shadow_cast` shader) depends on Milestone 2. Milestone 5
(`pbr_direct_lit`/`pbr_ibl` content) depends on Milestone 6 landing
first or in the same commit. Milestone 6 is otherwise independent.
Milestone 7 (descriptor-set test) depends on Milestone 4. Milestone 8
(Runtime resources, no signature change) depends on Milestones 1, 4, 5,
6 — and is independently buildable/testable before Milestone 9 ever
starts. Milestone 9 (the atomic `drawFrame()` signature + all 25 call
sites) depends on Milestones 1, 3, 4, 5, 6, 8. Milestone 10
(verification/golden) follows a clean commit of 1-9; any golden
re-capture is its own separate, human-reviewed commit, per ADR-0042.

Every commit in this sequence builds and passes its own existing tests
before the next lands. Milestone 9 is the single point where
`Renderer::drawFrame()`'s signature and every one of its 25 call sites
change together — never split across commits.

## Verification Checklist

- [ ] GPU-independent: `hasColorAttachment`'s default reproduces
  existing Pipeline behavior; `ShadowMapCreateParams` compatibility
  tests; Guard 0's five-kind rejection/acceptance tests (new, Milestone
  3); the widened `sampledTextureBindingCount` check accepts 2 and 4.
- [ ] GPU: `shadow_map_render_gpu_tests.cpp` confirms depth-only write +
  downstream color-sampled read round-trips correctly via the existing
  `copyRenderTargetToBuffer()` path (Milestone 2); `shadow_cast_reflection_tests.cpp`/
  extended `pbr_direct_lit`/`pbr_ibl` reflection tests confirm the real,
  compiled contract and tail offset (Milestone 4/5); `shadow_gpu_tests.cpp`
  Group A confirms occlusion, both movement cases, out-of-bounds-is-lit,
  and first-use-vs-reused-`ShadowMap` equivalence against P10's own
  fixed pixel coordinates and thresholds (Milestone 10); Group B
  confirms the positive control and IBL isolation; Debug and Release,
  Vulkan Validation Layers clean.
- [ ] Image regression: five non-PBR goldens byte-identical;
  `ibl_material_demo` asserted byte-identical (hard requirement — also
  this Plan's sole no-directional-light byte-compatibility proof);
  `pbr_material_demo`/`hdr_roll_off_demo` compared first, re-capture
  requested only if an actual difference appears.
- [ ] CMake: every target in P9(f)'s two tables links `shadow_cast_shaders`
  (`add_dependencies`) and resolves its own compiled SPIR-V/reflection
  JSON (compile-definition or `copy_if_different`, per convention) —
  cross-checked against Files/Modules Touched below, not just declared.
- [ ] Descriptor pool: the new `N+4`/`N+5`-with-shadow `TEST_CASE`
  (Milestone 7) confirms against the real 60-set ceiling.
- [ ] Full `ctest -LE gpu` and `ctest -L gpu`, Debug and Release.
- [ ] Fresh `ATLANTIS_BUILD_TESTS=OFF` build produces a working
  `atlantis_runtime.exe` with zero test executables.
- [ ] No `Vk*` symbol outside Vulkan Backend; no new cross-module
  dependency beyond what this Plan names.
- [ ] `git diff --check` and committed-file audit clean; no generated
  `.spv`, reflection JSON, or build directory committed (including this
  Plan's own temporary `slangc` probe, already deleted, never committed).
- [ ] Every numeric value in P4/P11 either survives Implementation
  unchanged or was changed only after an explicit human confirmation —
  recorded in the Implementation PR either way.

## Rollback Plan

Revert Milestone 10's golden-recapture commit first (if any), then
Milestones 9 through 1 in reverse order. `hasColorAttachment` defaults
`true` and every existing Pipeline is unaffected by its mere presence,
so a partial rollback stopping after Milestone 1/2 leaves the RHI
additions inert. Milestone 9 is one atomic commit (signature + all 25
call sites); reverting it cleanly removes the entire public-API change
in one step. Milestone 8 (Runtime resources) can be reverted
independently of Milestone 9 only if Milestone 9 is reverted first (the
reverse order would leave `drawFrame()` calling into resources that no
longer exist). No `World`/`Scene` schema or asset format is introduced.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No deltas beyond the Verification Checklist above.

## Plan Review — Items for Human Confirmation

Confirmed by the Human Review Approval recorded above (2026-09-05,
PR #124) — kept here as the permanent record of what was reviewed, not
rewritten to remove the questions once answered.

1. **P4/P11's concrete numeric values** (1024×1024 resolution; center
   `(0,0,0)`, half-extent `8.0`, near `0.1`, far `30.0`; `kShadowBias =
   0.0015`; the `1e-6f` up-vector degeneracy threshold) and **P10's own
   fixed scene positions, camera/light parameters, pixel coordinates,
   and luminance thresholds** (occluder/quad/camera/light values; pixels
   `(239,250)`, `(402,331)`, `(276,250)` (used twice — checks 2 and 3),
   `(415,133)`; thresholds `> 30`, `> 15`, `<= 5`) — approved as written,
   including which thresholds are exact structural predictions
   (`luminance == 0`, from `pbr_direct_lit.slang:124`'s confirmed "no
   ambient term") versus disclosed conservative floors (`> 30`, `> 15`,
   `<= 5`, not hand-derived through the full BRDF/tonemap/sRGB
   pipeline), and **including check 4's out-of-bounds probe, which must
   be verified through the fixed light-space/shadow-NDC transform
   (`shadowNdc.x = 1.75`, `0.75` beyond the `[-1,1]` bound), never
   through a world-space `x`/`z` magnitude comparison against the `8`
   half-extent** — a prior draft's own `(0,0,-9.5)` point, checked this
   way, turned out to be in-bounds (`shadowNdc.x ≈ 0.988`), not out.
   Per the Non-negotiable rule, any change during Implementation (beyond
   the stated integer-rounding carve-out) requires
   stopping and requesting confirmation.
2. **P6's shadow-map binding-index dispatch** via
   `Material::environmentBinding()` (binding 2 / 4) rather than a new
   `MaterialKind` — confirm this is the correct, minimal insertion
   point.
3. **P9's exhaustive migration tables** (P9(d)'s 5-file buffer-size
   table, P9(e)'s 16-file/25-call-site `drawFrame()` table, P9(f)'s
   two-convention CMake table covering every one of those files' own
   `shadow_cast.slang` artifact provenance) — confirmed complete by
   direct `rg` search of the whole repository this pass, including every
   CMakeLists.txt that references `sky`'s or `pbr_ibl`'s own
   shader-output-dir convention. Confirm no additional caller or build
   target is expected to exist outside this list.
4. **P10's new test scene** (hand-built ground quad + occluder cube +
   out-of-bounds quad via `createMesh()`, not a new `.scene.txt` asset;
   no-directional-light byte compatibility is proven by the existing
   `ibl_material_demo` golden alone, not retested in the new file) —
   confirm this scope is preferred over exercising the full
   asset-loading path or duplicating the no-light proof.
5. **Milestone 8/9's split** (Runtime's own new resources, including
   their own CMake wiring for the one target that needs it early, land
   standalone and buildable before the `drawFrame()` signature changes
   in one atomic commit touching all 25 call sites and every remaining
   CMake target in P9(f)) — confirm this sequencing is the preferred
   shape for reviewable, always-buildable commits.

## Proposed Correction — `drawFrame()` default arguments

**Status: Human Review Approved.** Recorded
here per this Plan's own Non-negotiable rule ("any deviation found
necessary during Implementation is called out explicitly, not silently
applied") rather than silently adjusting the signature this Plan's own
P6 text specifies.

**What P6 specifies:** `environmentLighting`/`skyPipeline` keep their
existing `= nullptr` defaults, and the five new shadow parameters
(`shadowMap`, `shadowMapSampler`, `shadowCastPipeline`,
`shadowLightSpaceBuffer`, `shadowCasterDrawItems`) are appended after
them, all required (no default).

**What Implementation found:** this is not expressible in C++ — once a
parameter has a default argument, every parameter after it must also
have one (`[dcl.fct.default]`). Appending five non-default parameters
after `skyPipeline`'s own `= nullptr` default is a compile error, not an
implementation choice.

**Proposed resolution:** drop the `= nullptr` default from both
`environmentLighting` and `skyPipeline`. Both keep their exact existing
nullable pointer types and their exact existing nullptr-means-"none"
meaning — this is a purely mechanical signature change, not a design
change. Every one of the 25 call sites already needs updating for the
five new trailing parameters in this same commit regardless, so no call
site pays an additional cost from also writing `nullptr, nullptr`
explicitly where it previously relied on the default.

**Alternatives not taken, for the record:** (a) reordering parameters so
the five new ones precede `environmentLighting`/`skyPipeline` — rejected
as a larger, less-reviewable diff to every call site's own argument
order, and it would separate the sky-pipeline-adjacent shadow
parameters from their own natural "immediately after skyPipeline"
narrative position in P6's own text; (b) a params-struct — rejected as
a larger API-shape change than this Plan's own scope calls for, not
something to introduce as a side effect of a default-argument
mechanics problem.

This section recorded what Implementation actually did, at the time
not yet confirmed by Human Review. The corresponding commit's own
message discloses the same correction. That confirmation is now
recorded below.

### Human Review approval — 2026-09-06

Approved as proposed above. Scope: (1) remove the `= nullptr` default
from `environmentLighting` and `skyPipeline`; (2) both parameters keep
their existing pointer types and existing nullptr-means-"none"
semantics, unchanged; (3) `drawFrame()`'s current parameter order is
unchanged. No other part of the signature is affected by this
approval.
