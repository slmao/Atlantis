# Plan: HDR Color Pipeline & Output Transfer Foundation

- **Spec:** [specs/0024-hdr-color-pipeline-output-transfer-foundation.md](../specs/0024-hdr-color-pipeline-output-transfer-foundation.md)
  (`Approved`, Human Review Approval 2026-08-31 — authorizes drafting
  this Plan only, not Implementation)
- **Status:** In Review
- **Author:** slmao

## Objective

Implement the two-pass HDR output pipeline exactly as ADR-0068 defines
it: a new `HdrColorTarget` intermediate the existing geometry pass
writes into, and a new, shared output-transform pass — one of two
closed shader/Pipeline variants, selected by the final target's real
`Format` class — that tone-maps and encodes into the caller's real
`RenderTarget`. This Plan maps ADR-0068's Decisions to concrete files,
atomic commit boundaries, and verification; it does not revisit any of
D-1–D-11.

## Real-code anchors this Plan builds against

- `PipelineCreateParams::colorFormat` is typed `atlantis::rhi::Format`
  (`src/rhi/include/atlantis/rhi/types.h:190`) — it cannot represent
  `HdrFormat::Rgba16Float` (a deliberately separate enum, ADR-0068
  D-2). **Corrected from this Plan's own drafting review:** a required
  field plus an optional override is a real illegal-state risk (both
  set, or neither) and a priority convention, not a structural
  guarantee — rejected. `PipelineCreateParams::colorFormat` is retyped
  `std::variant<Format, HdrFormat>` instead, matching this exact
  module family's own established precedent for "structurally exactly
  one of a closed set of kinds": `render_graph::CompileError =
  std::variant<MultipleProducersError, DependencyCycleError>`
  (`compile_error.h:85`) and `platform::PlatformEvent`'s own explicit
  rationale — *"`std::variant` chosen over a polymorphic event base,
  consistent with `atlantis::Result`'s existing value-type style"*
  (`platform_event.h:42-43`). See Milestone 1 for the exact type and
  every consuming call site.
- `VulkanDevice::createOffscreenTarget()`/`createSampledTexture()`
  (`vulkan_device.cpp:1191-1341`) are `VulkanHdrColorTarget`'s own
  direct template — same `VkImageCreateInfo`/alloc/bind/view sequence,
  `usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`.
  `VulkanSampledTexture`/`VulkanTexture` (`vulkan_sampled_texture.h`,
  `vulkan_texture.h`) are `VulkanHdrColorTarget`'s own class-shape
  template (`final`, non-copyable/movable, plain `image()`/`imageView()`
  accessors, `static_cast`-reached — single real implementer, no
  `dynamic_cast`).
- `VulkanCommandList::transitionResource(SampledTexture&, ...)`/
  `beginRendering(RenderTarget&, ...)`/`bindTexture(const SampledTexture&, ...)`
  (`vulkan_command_list.cpp:98-119,142-172,271-301`) are the three new
  overloads' own direct templates.
- `render_graph::execute()`'s four widening points:
  `execution.cpp:56-60` (Guard 0), `:117-130` (transition dispatch),
  `:147` (`beginRendering` call site), `:180-194` (trailing `finalState`
  loop) — Spec 0016's own two-kind-to-three-kind widening of this exact
  function is the precedent (unchanged shape, one more branch).
- `selectShaderPair()`/`pushConstantLayoutFor()`
  (`material_realization.cpp:101-157`) are `isSrgbFormat()`'s own
  direct template: closed `switch`, no `default:`,
  `ATLANTIS_CHECK_MSG(false, "unreachable...")` fallback.
- `descriptor_contract.cpp`'s per-kind functions and
  `compile_and_validate.cpp`'s `validateDescriptorContractForStage()`/
  `validatePushConstantsForVertexStage()` (`:133-188`) are the shader-
  compiler touch points; the latter's existing ternary
  (`pbr-direct-lit` ? 96 : 64) widens to a real empty-`expected` case
  for the two new contract names (no push constant at all, not "size
  0 at offset 0").
- `shaders/pbr_direct_lit/CMakeLists.txt` +
  `CMakeLists.txt:101` (`add_subdirectory`) are the shader-CMake
  template.
- `RuntimeApplication`'s existing per-shader-pair members
  (`pbrDirectLitVertexSpirv_` etc., `runtime_application.h:202-204`),
  `lastSeenFormat_`/`lastSeenExtent_` (`:178-179`), and the resize/
  format-change branches (`runtime_application.cpp:486-522`) are the
  Runtime-integration template.
- `tests/image_regression/fixture/pbr_material_demo_fixture.h/.cpp`
  is each fixture's own template for owning a from-scratch camera
  buffer/depth texture independently of `RuntimeApplication`.
- `VulkanCommandList::bindIndexBuffer()` hardcodes
  `VK_INDEX_TYPE_UINT16` (`vulkan_command_list.cpp:231`) — every index
  buffer in this codebase, including the new fullscreen-triangle one,
  is `std::uint16_t`, never `uint32_t`.
- `runtime_application.cpp:338-346` (`cameraBuffer_`'s own startup
  creation) is the template for `hdrColorTarget_`'s own first-creation
  failure: `Result::Err` → `ATLANTIS_LOG_ERROR` →
  `lifecycle_.markFailed()` → a new `RuntimeInitError` enumerator,
  fatal, no retry.
- `runtime_application.cpp:860-882` (submit-then-swap) is the real,
  existing mechanism this Plan's own format-change Pipeline rebuild
  reuses exactly: a rebuild candidate is prepared *before* this
  frame's own draw, but the live `fallbackMaterial_`/
  `materialResourceMap_` swap-in happens only *after* `submit()`
  returns `Ok` — the exact point Plan 0018 Section P13's own comment
  proves safe (submit's own internal drain has by then confirmed the
  *previous* frame's GPU work, which may still reference the old
  Pipeline, has finished). A `submit()` failure
  (`:861-866`, `lifecycle_.markFailed()`, return) discards the whole
  frame, including any prepared-but-unswapped candidate — old and new
  resources can never end up mixed, by construction, not by
  discipline.

## Milestones / Task Breakdown

### Milestone 1 — `HdrFormat`, `HdrColorTarget`, `HdrColorTargetCreateError`, `PipelineCreateParams::colorFormat` as a structural one-of

ADR-0068 D-1/D-2. RHI public surface, exact signatures:

```cpp
// types.h
enum class HdrFormat { Rgba16Float };

enum class HdrColorTargetCreateError {
  FormatFeaturesUnsupported,  // pure classification, checked before any VkResult (see below)
  AllocationFailed,
  ImageCreationFailed,
  ImageViewCreationFailed,
};

struct HdrColorTargetCreateParams {
  Extent2D extent;
  HdrFormat format = HdrFormat::Rgba16Float;
};

struct PipelineCreateParams {
  ShaderStageBytecode vertexShader;
  ShaderStageBytecode fragmentShader;
  VertexInputLayout vertexInputLayout;
  std::variant<Format, HdrFormat> colorFormat = Format::Unknown;  // retyped -- was `Format colorFormat`
  DepthFormat depthFormat = DepthFormat::D32Sfloat;
  std::size_t pushConstantSizeBytes = 0;
  bool hasSampledTextureBinding = false;
};
```

No illegal state exists: `colorFormat` always holds exactly one
alternative, by the type's own construction — never both, never
neither, never a priority/override convention to document or get
wrong. `Format::Unknown` inside the `Format` alternative remains the
one pre-existing illegal *value* (`toVkFormat(Format)` already asserts
on it) — unchanged, not a new concept.

- `src/rhi/include/atlantis/rhi/types.h`: the four declarations above;
  `#include <variant>` added.
- `src/rhi/include/atlantis/rhi/hdr_color_target.h` (new): abstract
  `HdrColorTarget` class — `[[nodiscard]] virtual Extent2D extent() const = 0;`,
  `[[nodiscard]] virtual HdrFormat format() const = 0;` — inheriting
  neither `RenderTarget` nor `SampledTexture` (ADR-0068 D-1's own
  correction: `RenderTarget` inheritance would reopen Guard 2; `SampledTexture::format()`'s
  return type cannot represent `HdrFormat`).
- `src/rhi/include/atlantis/rhi/device.h`: new
  `[[nodiscard]] virtual atlantis::Result<std::unique_ptr<HdrColorTarget>, HdrColorTargetCreateError> createHdrColorTarget(const HdrColorTargetCreateParams&) = 0;`,
  alongside `createOffscreenTarget()`.
- `src/vulkan_backend/src/vulkan_hdr_color_target.h/.cpp` (new):
  `VulkanHdrColorTarget final : public atlantis::rhi::HdrColorTarget`,
  mirroring `VulkanSampledTexture` exactly (device/image/memory/
  imageView/extent/format members, `image()`/`imageView()` accessors,
  non-copyable/movable).
- `src/vulkan_backend/src/vulkan_device.cpp`:
  - New `toVkFormat(HdrFormat)` overload (one case,
    `VK_FORMAT_R16G16B16A16_SFLOAT`).
  - New, **pure, free** classification function —
    `bool hasRequiredHdrColorTargetFeatures(VkFormatFeatureFlags optimalTilingFeatures)`
    — returns whether both `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT`
    and `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT` are set. Takes a plain
    `VkFormatFeatureFlags` value, no `VkPhysicalDevice`/`VkInstance`
    involved — this is the one piece of logic the GPU-independent
    negative test (Milestone 8) calls directly with synthetic inputs;
    it is never exercised through a real Vulkan call in that test.
  - `createHdrColorTarget()`: calls
    `vkGetPhysicalDeviceFormatProperties()` for
    `VK_FORMAT_R16G16B16A16_SFLOAT`, passes the result's
    `optimalTilingFeatures` to `hasRequiredHdrColorTargetFeatures()`;
    `false` → `Result::Err(HdrColorTargetCreateError::FormatFeaturesUnsupported)`,
    returned immediately, before any `vkCreateImage` call (never
    `ATLANTIS_CHECK`). Otherwise follows `createOffscreenTarget()`'s
    own create/alloc/bind/view sequence exactly, usage
    `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT`.
  - `createPipeline()`: the one existing line
    `const VkFormat colorFormat = toVkFormat(params.colorFormat);`
    becomes
    `const VkFormat colorFormat = std::visit([](auto format) { return toVkFormat(format); }, params.colorFormat);`
    — both `toVkFormat(Format)` and `toVkFormat(HdrFormat)` are
    visible at that call site by ordinary overload resolution; nothing
    else in `createPipeline()` changes.
- `src/vulkan_backend/src/vulkan_result.h/.cpp`: new
  `toHdrColorTargetCreateError(VkResult)`, mirroring
  `toTextureCreateError()` exactly (`vulkan_result.cpp:109-112`) —
  every non-`VK_SUCCESS` `VkResult` → `ImageCreationFailed`; a
  memory-type-selection failure (no `VkResult` involved) →
  `AllocationFailed`, mirroring `createTexture()`'s own identical
  two-case split.
- **Every existing `PipelineCreateParams{.colorFormat = someFormat, ...}`
  call site (the three non-PBR sites plus `PbrDirectLit`'s own,
  `material_realization.cpp:176,301,324` and its own PBR arm) needs no
  syntax change** — `Format` converts implicitly into
  `std::variant<Format, HdrFormat>`, both in aggregate initialization
  and in plain assignment. Only Milestone 6's own new geometry-Pipeline
  call sites (which pass `HdrFormat::Rgba16Float` instead) are new
  code, not migrated old code.

**Atomic:** the RHI types (including the retyped `colorFormat`), the
`Device`/`VulkanDevice` factory method, the pure classification
function, `createPipeline()`'s `std::visit`, and the capability-check
land in one commit — a build where `HdrColorTarget` exists but no
concrete `Device` can create one (or vice versa), or where
`colorFormat`'s type changes without every existing call site still
compiling unmodified, is never checked in.

### Milestone 2 — `CommandList` surface, `ResourceBinding`, `render_graph::execute()` widening

ADR-0068 D-1/D-3. Exact signatures:

```cpp
// command_list.h -- three new/overloaded methods
virtual void transitionResource(HdrColorTarget& target, ResourceState before, ResourceState after) = 0;
virtual void beginRendering(HdrColorTarget& color, Texture* depth, ClearColorValue colorClear, float depthClear) = 0;
virtual void bindTexture(const HdrColorTarget& texture, const Sampler& sampler) = 0;

// execution.h -- ResourceBinding's one new field
struct ResourceBinding {
  CompiledResourceId resource;
  RenderTarget* target = nullptr;
  ClearColorValue colorClear{};
  Texture* depthTexture = nullptr;
  float depthClear = 1.0f;
  SampledTexture* sampledTexture = nullptr;
  HdrColorTarget* hdrColorTarget = nullptr;  // new
  ResourceState incomingState = ResourceState::Undefined;
  std::optional<ResourceState> finalState;
};
```

- `src/vulkan_backend/src/vulkan_command_list.cpp`: three concrete
  implementations, each a direct copy of its `SampledTexture`/
  `RenderTarget` sibling with `VulkanHdrColorTarget` substituted via
  `static_cast` (single real implementer, no `dynamic_cast`) —
  `bindTexture()`'s own descriptor-write reuses `fullColorResourceRange()`/
  binding-1/`COMBINED_IMAGE_SAMPLER` exactly as the existing
  `SampledTexture` overload does.
- `src/render_graph/src/execution.cpp`, four widening points, each a
  narrow fourth branch on an already-three-branch pattern:
  - **Guard 0** (`:57-58`): `boundCount` sums four terms instead of
    three — `(target != nullptr) + (depthTexture != nullptr) + (sampledTexture != nullptr) + (hdrColorTarget != nullptr)`,
    still checked `== 1`. This is the real mechanism that rejects both
    zero-resource and multi-resource ambiguity for the new field
    exactly as it already does for the other three — no separate new
    guard is needed.
  - The per-usage transition dispatch (`:117-130`) gains a fourth
    `else if (hdrColorTargetPtr != nullptr) commandList.transitionResource(*hdrColorTargetPtr, previous, *usage.state);`.
  - The draw-pass `beginRendering()` call site (`:147`) branches on
    which of `colorBinding->target`/`colorBinding->hdrColorTarget` is
    non-null and calls the matching overload — never both checked
    without a resolution, since Guard 0 already guarantees exactly one
    is set on any given binding.
  - The trailing `finalState` loop (`:180-194`) gains a third
    bound-type check (`|| binding.hdrColorTarget != nullptr`) and a
    matching `transitionResource()` branch.
  - Guard 2 (`:79-83`) is **untouched** — already scoped to
    `binding.target == nullptr → skip`, which already excludes the new
    field without any code change.
  - **Correct `Undefined → ColorAttachmentOutput → ShaderRead`
    execution**, traced against the real algorithm: the geometry pass
    declares `writes(hdrColorTargetResource, ColorAttachmentOutput)` —
    `currentState` has no prior entry for this resource, so `previous`
    resolves to `binding.incomingState` (`Undefined`, the field's own
    default) and the dispatch above transitions
    `Undefined → ColorAttachmentOutput` before that pass's own
    `executeFn` runs. The output-transform pass declares
    `reads(hdrColorTargetResource, ShaderRead)` — `currentState` now
    holds `ColorAttachmentOutput` from the prior pass, so the same
    dispatch transitions `ColorAttachmentOutput → ShaderRead` before
    *that* pass's own `executeFn` (which calls the new `bindTexture()`
    overload) runs. No `finalState` is set on this binding — the
    intermediate is never read outside this one `drawFrame()` call, so
    no trailing transition is needed or declared.

**Atomic:** the `CommandList` interface, its Vulkan implementation, the
`ResourceBinding` field, and all four `execute()` widening points land
together — a partially-widened `execute()` that compiles but silently
drops the new field's own transitions is never checked in.

### Milestone 3 — Shader System: two new descriptor contracts, zero-push-constant validation

ADR-0068 D-10.

- `src/shader_system/include/atlantis/shader_system/descriptor_contract.h`/`.cpp`:
  new `outputTransformExpectedDescriptorContract()` — exactly
  `{DescriptorBinding{.set=0, .binding=0, .type=DescriptorType::Sampler, .stage=ShaderStage::Fragment}}`,
  reused by both variants (one function, not two).
- `src/tools/shader_compiler/compile_and_validate.cpp`:
  `validateDescriptorContractForStage()` gains
  `else if (expectedContract == "output-transform-unorm" || expectedContract == "output-transform-srgb") fullContract = outputTransformExpectedDescriptorContract();`.
  `validatePushConstantsForVertexStage()`'s existing ternary widens to
  a real empty-`expected` case for both new contract names (`expected = {}`,
  not a zero-sized entry) — the check then requires the real reflected
  `pushConstantRanges` to also be empty. No new fragment-stage push-
  constant check is added (mirrors `UnlitTextured`/`LitTextured`'s own
  vertex-only-sufficient precedent — output-transform never declares a
  push constant on either stage, unlike `PbrDirectLit`).
- `src/tools/shader_compiler/main.cpp`: usage string's
  `--expected-contract=<name>` documentation gains the two new values.

**Atomic:** the new descriptor contract, both compiler-side
validation changes, and the usage-string update land together.

### Milestone 4 — Two new shader files, existing shaders' clamp removal

ADR-0068 D-5/D-6/D-7/D-10.

- `shaders/output_transform_unorm/output_transform_unorm.slang` (new):
  vertex stage takes one `Float2` clip-space position (location 0, no
  UV/normal/color), outputs `SV_Position` and a varying
  `uv = position*0.5+0.5`; fragment stage samples `HdrColorTarget` at
  that UV, applies D-5's floor/exposure/Reinhard, then D-6's exact
  piecewise sRGB OETF, writes the encoded value.
- `shaders/output_transform_srgb/output_transform_srgb.slang` (new):
  identical vertex stage; fragment stage applies D-5's tone-mapping
  only — no OETF — writes the linear `tonemapped` value.
- **The fullscreen triangle's own real geometry, drawn via the
  existing `drawIndexed()` — no non-indexed draw API is added:**
  vertex buffer, 3 vertices, `VertexInputLayout{.strideBytes = 8,
  .attributes = {{.location = 0, .offsetBytes = 0, .format = VertexAttributeFormat::Float2}}}`
  — an oversized NDC triangle, `(-1,-1)`, `(3,-1)`, `(-1,3)`, covering
  the full screen with exactly 3 vertices; index buffer, 3 indices,
  `std::uint16_t{0, 1, 2}` (`VK_INDEX_TYPE_UINT16`, matching
  `bindIndexBuffer()`'s own existing hardcoded type,
  `vulkan_command_list.cpp:231` — never `uint32_t`). Both created once
  via the existing `Device::createBuffer({.purpose = BufferPurpose::Vertex/Index, ...})`,
  populated via the same host-visible-then-upload or host-coherent
  write path every other checked-in mesh already uses. `Renderer`'s
  own output-transform pass calls the existing `bindVertexBuffer()`/
  `bindIndexBuffer()`/`drawIndexed(3)` — the exact same three calls
  the geometry pass already makes per `DrawItem`, nothing new on
  `CommandList`.
- `shaders/output_transform_unorm/CMakeLists.txt`,
  `shaders/output_transform_srgb/CMakeLists.txt` (new): each mirrors
  `shaders/pbr_direct_lit/CMakeLists.txt` exactly, `EXPECTED_CONTRACT
  output-transform-unorm`/`output-transform-srgb` respectively.
- `CMakeLists.txt` (root): two new unconditional
  `add_subdirectory(shaders/output_transform_unorm)`/
  `add_subdirectory(shaders/output_transform_srgb)` calls, after
  `shaders/pbr_direct_lit` and before `src/runtime`.
- `shaders/lit_textured/lit_textured.slang:107`,
  `shaders/pbr_direct_lit/pbr_direct_lit.slang:183`: remove the final
  `clamp(..., 0, 1)` (D-7). No other line in either file changes.
  `shaders/textured_quad/textured_quad.slang`: unchanged (never
  clamped).

**Atomic:** both new shader pairs and both existing shaders' clamp
removal land in one commit — a build with the new output-transform
shaders but the old pre-HDR clamp still in place (or vice versa) is
never checked in.

### Milestone 5 — `Renderer::drawFrame()`'s new signature, the two-pass graph

ADR-0068 D-3.

```cpp
// renderer.h -- drawFrame()'s new signature (every new parameter borrowed, none owned)
void drawFrame(atlantis::rhi::CommandList& commandList, atlantis::rhi::RenderTarget& colorTarget,
               atlantis::rhi::Texture& depthTarget, atlantis::rhi::Buffer& cameraUniformBuffer,
               std::span<const DrawItem> drawItems, atlantis::rhi::ResourceState finalColorState,
               atlantis::rhi::HdrColorTarget& hdrColorTarget,
               atlantis::rhi::Buffer& fullscreenTriangleVertexBuffer,
               atlantis::rhi::Buffer& fullscreenTriangleIndexBuffer,
               atlantis::rhi::Pipeline& outputTransformPipeline,
               atlantis::rhi::Sampler& outputTransformSampler);
```

- `src/renderer/src/renderer.cpp`: the existing single-pass
  `RenderGraphBuilder` becomes two passes on the same builder — pass 1
  (unchanged `DrawItem` loop) writes `ColorAttachmentOutput` into
  `hdrColorTarget`; pass 2 reads it (`ShaderRead`) and writes
  `ColorAttachmentOutput` into the caller's final `colorTarget`,
  binding `outputTransformPipeline`, the fullscreen triangle's vertex/
  index buffers, and `outputTransformSampler` bound to
  `hdrColorTarget`.

**Proof this stays one `CommandList`, one `submit()` per frame, and
`Renderer` never submits or presents** — traced against the real,
unchanged call chain (`runtime_application.cpp:702-866` today):
`device_->createCommandList()` vends exactly one `CommandList`;
`renderer_.drawFrame(*commandList, ...)` receives it *by reference* —
both of `drawFrame()`'s own `RenderGraphBuilder::compile()`/
`render_graph::execute()` calls record into that same referenced
object, never a second one `drawFrame()` creates internally (`Renderer`
has no `Device` reference to create one from — it depends only on
RHI/RenderGraph/Core, unchanged); `drawFrame()` returns `void`, calling
neither `submit()` nor `present()` anywhere in its own body (confirmed
by grep: `renderer.cpp` names no `Device`/`Presentation` symbol at
all); the caller's own, sole `device_->submit(std::move(commandList), *target)`
call (`:860`) — unchanged in count, unchanged in position, still the
one and only `submit()` per `runFrame()` — takes ownership of the
*same* `CommandList` both passes were recorded into.

**Atomic:** the signature change and the two-pass graph construction
land together — `drawFrame()` cannot compile with the old signature
and the new graph body, or vice versa.

### Milestone 6 — Runtime integration

ADR-0068 D-1/D-3/D-4/D-6.

- `src/runtime/include/atlantis/runtime/bootstrap_config.h`: four new
  reflection-path fields (vertex/fragment × unorm/srgb), mirroring
  `pbrDirectLitVertexShaderReflectionPath` (`:51`).
- `src/runtime/include/atlantis/runtime/runtime_application.h`: new
  members, all `RuntimeApplication`-owned, `Renderer` only ever
  borrowing a reference for the duration of one `drawFrame()` call —
  `hdrColorTarget_` (`unique_ptr<HdrColorTarget>`);
  `fullscreenTriangleVertexBuffer_`/`...IndexBuffer_`
  (`unique_ptr<Buffer>`, created once, never resized or recreated);
  `outputTransformSampler_` (`unique_ptr<Sampler>`, created once,
  never recreated); `outputTransformUnormPipeline_`/`...SrgbPipeline_`
  (`unique_ptr<Pipeline>` each); the four new SPIR-V/reflection-layout
  members these two shader pairs need (mirroring the existing per-pair
  member groups).
- `src/runtime/include/atlantis/runtime/material_realization.h`:
  `FormatRebuildCandidates` (`:210-213`, today `{fallback, materials}`)
  gains one new field, `std::unique_ptr<atlantis::rhi::Pipeline> outputTransformPipeline;`
  — the one rebuilt variant this format-change event needs (which of
  the two, decided by `isSrgbFormat()`) — reusing the existing
  pending-candidate struct and its own existing swap-in point, rather
  than a new, parallel mechanism.
- `src/runtime/src/material_realization.cpp`: `realizeOneMaterialCandidate()`/
  `rebuildMaterialsForFormatChange()` (`:101-333`, `:338-...`) —
  `PipelineCreateParams{.colorFormat = atlantis::rhi::HdrFormat::Rgba16Float}`
  replaces every `.colorFormat = colorFormat` for a geometry Pipeline
  (M1's retyped `colorFormat` accepts this directly); both functions'
  own now-unused `colorFormat`/`newColorFormat` parameter is removed —
  every call site (Runtime, every image-regression fixture, Milestone
  7) updated accordingly. New `isSrgbFormat(atlantis::rhi::Format) -> bool`,
  mirroring `selectShaderPair()`'s own closed-switch/no-`default:`
  shape exactly (`:101-124`) — the one place that decides which of the
  two output-transform shader contracts a given final `Format` needs.

**Six scenarios, traced individually against real, existing
mechanisms — no new mechanism invented where an existing one already
proves safe:**

1. **First creation (startup).** `hdrColorTarget_`,
   `fullscreenTriangleVertexBuffer_`/`...IndexBuffer_`,
   `outputTransformSampler_`, and both `outputTransform*Pipeline_`s are
   created once, alongside `cameraBuffer_`'s own existing startup
   sequence (`runtime_application.cpp:338-346`). Any failure —
   `Result::Err` on any one of them — is fatal: `ATLANTIS_LOG_ERROR`,
   `lifecycle_.markFailed()`, a new, distinct `RuntimeInitError`
   enumerator per resource kind (mirroring `CameraBufferCreateFailed`'s
   own precedent exactly), init aborts. No partial-init state is ever
   reached — the existing pattern already fails the whole sequence on
   the first error.
2. **Resize (extent change).** `hdrColorTarget_` is recreated in the
   same branch, at the same trigger (`currentExtent != lastSeenExtent_`,
   `:510-522`), as the depth `Texture` — eager, direct reassignment
   (`hdrColorTarget_ = std::move(newResult.value());`), safe for the
   same reason the depth `Texture`'s own eager reassignment already is:
   this frame's own `Presentation::acquireNextTarget()` (called earlier
   this same frame) has already drained the *previous* frame's GPU
   work before this branch ever runs. Failure: `ATLANTIS_LOG_ERROR`,
   keep the existing `hdrColorTarget_`, `lastSeenExtent_` intentionally
   not updated, retry next frame — byte-for-byte the depth `Texture`'s
   own existing failure branch, applied to a second resource.
3. **Surface format change.** Detected by the existing
   `currentFormat != lastSeenFormat_` branch (`:489-508`).
   `isSrgbFormat(currentFormat)` selects which one of
   `outputTransformUnormPipeline_`/`...SrgbPipeline_` needs rebuilding
   — the other is untouched. The new `Pipeline` candidate is prepared
   into `FormatRebuildCandidates` (alongside the existing
   `fallback`/`materials` candidates) *before* this frame's own draw,
   which still uses whichever output-transform `Pipeline` is currently
   live; the live member is swapped only after this frame's own
   `submit()` returns `Ok` (`:878-882`'s own existing swap-in point,
   widened by one more field) — the exact real mechanism Plan 0018
   Section P13 already proves safe, reused verbatim, not reinvented.
   Failure to create the new candidate: `ATLANTIS_LOG_ERROR`, the whole
   `rebuildMaterialsForFormatChange()`-equivalent call fails,
   `lastSeenFormat_` is not updated, this frame draws (and the swap-in
   is skipped) with the still-live, still-valid old `Pipeline` — old
   and new format-specific resources can never be mixed within one
   frame's own draw, by construction (the swap is one atomic
   move-assignment, gated on one boolean `has_value()`, never a
   partial/field-by-field update).
4. **Zero extent (minimized window).** Already handled upstream, before
   any of the branches above run: `Presentation`'s own existing
   zero-extent contract causes this frame to be skipped entirely (no
   `target`, no draw, no resize check reached) — confirmed by
   `OffscreenTarget::acquireTarget()`'s own doc comment naming this as
   `Presentation`-specific, windowed-only behavior. No new zero-extent
   handling is written for any of this Milestone's own new resources.
5. **`submit()` failure (`QueueSubmitFailed` or `DeviceLost`,
   `SubmitError`).** The existing handling
   (`:860-866`) is unchanged and already sufficient: `ATLANTIS_LOG_ERROR`,
   `classifySubmitError()` (already treats both variants identically —
   confirmed by reading its own call site, the result is discarded
   either way), `lifecycle_.markFailed()`, return. Any prepared-but-
   unswapped format-change candidate (scenario 3) is simply dropped
   along with the whole failed frame — never swapped in, never mixed
   with the resources this failed frame actually drew with. No new
   `DeviceLost`-specific branch is added for `hdrColorTarget_` or
   either output-transform `Pipeline` — the existing, uniform
   `SubmitError` handling already covers them, matching how it already
   covers `cameraBuffer_`/`depthTexture_`/every material `Pipeline`
   today.
6. **Destruction order.** Ordinary reverse-declaration-order RAII via
   `RuntimeApplication`'s own existing member layout, widened by these
   new members — every one of them must be destroyed before `device_`
   itself (the same precondition every other RHI resource this class
   owns already carries), and — for a resource the GPU may still be
   executing against — only after the existing shutdown-path
   `device_->waitIdle()` call, exactly like `cameraBuffer_`/
   `depthTexture_`/every material `Pipeline` today. No new destruction-
   ordering rule is introduced.

**Atomic:** Runtime's own member additions, `isSrgbFormat()`, the
format-change candidate's own widened swap-in, the resize branch's
`hdrColorTarget_` recreation, the `drawFrame()` call-site update, and
`material_realization.cpp`'s `colorFormat` switch land together.

### Milestone 7 — Image-regression fixtures

Every existing fixture (`minimal_cube_fixture`, `world_scene_fixture`,
`world_scene_loaded_fixture`, `textured_quad_fixture`,
`material_demo_fixture`, `lighting_demo_fixture`,
`pbr_material_demo_fixture`) gains its own `hdrColorTarget_`/
fullscreen-buffer/`outputTransformSampler_`/both output-transform
`Pipeline`s, created independently of `RuntimeApplication` (Plan
0023's own "fixture owns its own camera buffer, not shared code"
precedent, `tests/image_regression/fixture/pbr_material_demo_fixture.h`)
— each fixture's own `colorFormat` (already hardcoded per-fixture,
`Format::Rgba8Unorm` throughout, Real-code evidence) determines which
Pipeline variant it builds once at construction (no runtime format-
change path exists in any fixture today, so no `isSrgbFormat()` call
needed there — each fixture picks its one variant once).

**Not atomic with Milestone 6** — six independent, mechanical, same-
shape edits; may land as one commit or split per-fixture at
Implementation's own discretion, since none depends on another.

### Milestone 8 — New tests

- **GPU-independent:** `FormatFeaturesUnsupported` classification unit
  test against synthetic `VkFormatFeatureFlags` inputs (both bits
  present / one missing / both missing) — never a real GPU, never a
  fabricated "hardware lacks the format" condition; D-5's tone-mapping
  curve and D-6's sRGB OETF, CPU-side reference implementations, unit-
  tested at negative/below-1.0/exactly-1.0/well-above-1.0 inputs;
  `render_graph::execute()`'s widened Guard 0/dispatch, unit-tested
  against a synthetic 2-pass graph; shader-reflection-vs-C++-layout
  cross-check for both new descriptor contracts.
- **Real GPU (positive path and full render path only):**
  `HdrColorTarget` creation/resize; the two-pass graph executes
  cleanly (Validation Layers) on all three `MaterialKind`s, both
  windowed and offscreen origins, under **both** final-target format
  classes; a display-equivalence test between the `*_Unorm` and
  `*_Srgb` paths for identical linear input, compared within a defined,
  non-zero tolerance (measured at Implementation time, never exact
  byte equality); an above-1.0-input pixel comparison proving real
  roll-off; a real N=6 descriptor-pool stress test re-confirming
  D-4's `N+2`/`N+3` formula, reusing Spec 0021's own existing N=6
  fixture pattern (`tests/runtime/material_realization_gpu_tests.cpp`).

**Not atomic with Milestone 7** — new test files only, no production
code changes; may land in the same commit as Milestone 7 or its own,
at Implementation's own discretion.

### Milestone 9 — Six existing goldens, re-captured individually (implementation must already be merged/committed first)

Per ADR-0042's own two-phase process, applied six times independently
— never a batch operation, and never in the same commit as any
implementation code. **Precondition, checked before this Milestone
starts:** Milestones 1–8 are already merged to a clean commit — every
existing fixture already carries its own Milestone 7 changes, so its
own already-existing golden-generator binary (no new generator code
this Milestone) produces real, HDR-pipeline pixels when run today. For
each of `minimal_cube`, `world_scene`, `textured_quad`,
`material_demo`, `lighting_demo`, `pbr_material_demo`: run that
generator against that clean commit; record that capture's own real
provenance (`source_revision`, GPU/driver, timestamp) in its sidecar; a
human reviews the new image before it is accepted (non-black, no
garbage, recognizably the same scene, expected transfer-function shift
only) — never auto-accepted because a diff exists or because five
others were already approved. **Six separate acceptance decisions, six
separate sidecars, six separate commits or one commit containing only
these six sidecars+PNGs** — never combined with a code change of any
kind.

### Milestone 10 — New 7th golden: HDR roll-off baseline

- `assets/scenes/hdr_roll_off_demo.scene.txt` (new): reuses the
  existing `pbr_sphere` mesh and existing PBR material assets
  (Spec 0023's own reuse precedent), one light deliberately authored
  well above `1.0` intensity.
- `assets/CMakeLists.txt`: one new `atlantis_add_scene_asset()` entry,
  mirroring `pbr_material_demo_scene`'s own dependency declaration.
- `tests/image_regression/fixture/hdr_roll_off_demo_fixture.h/.cpp`,
  `tests/image_regression/golden_generator/hdr_roll_off_demo_main.cpp`,
  `tests/image_regression/hdr_roll_off_demo_gpu_tests.cpp` (new),
  mirroring `pbr_material_demo`'s own three-file shape exactly.
- Fixture-then-golden lands as two strictly separate commits, matching
  Plan 0023's own M8/M9 discipline — this Milestone's own fixture
  commit precedes its own golden-capture commit, both after Milestone
  9's six re-captures are already landed.

## Files / Modules Touched (expected)

`src/rhi/include/atlantis/rhi/{types.h,hdr_color_target.h,device.h,command_list.h}`,
`src/vulkan_backend/src/{vulkan_device.cpp,vulkan_device.h,vulkan_hdr_color_target.h,vulkan_hdr_color_target.cpp,vulkan_command_list.cpp,vulkan_command_list.h,vulkan_result.h,vulkan_result.cpp}`,
`src/render_graph/include/atlantis/render_graph/execution.h`,
`src/render_graph/src/execution.cpp`,
`src/shader_system/include/atlantis/shader_system/descriptor_contract.h`,
`src/shader_system/src/descriptor_contract.cpp`,
`src/tools/shader_compiler/{compile_and_validate.cpp,main.cpp}`,
`shaders/output_transform_unorm/**` (new),
`shaders/output_transform_srgb/**` (new),
`shaders/lit_textured/lit_textured.slang`,
`shaders/pbr_direct_lit/pbr_direct_lit.slang`, `CMakeLists.txt` (root),
`src/renderer/include/atlantis/renderer/renderer.h`,
`src/renderer/src/renderer.cpp`,
`src/runtime/include/atlantis/runtime/{bootstrap_config.h,runtime_application.h}`,
`src/runtime/src/{runtime_application.cpp,material_realization.cpp,material_realization.h}`,
`tests/image_regression/fixture/*_fixture.{h,cpp}` (all seven, six
existing + `hdr_roll_off_demo_fixture` new),
`tests/image_regression/golden_generator/hdr_roll_off_demo_main.cpp` (new),
`tests/image_regression/hdr_roll_off_demo_gpu_tests.cpp` (new),
`assets/scenes/hdr_roll_off_demo.scene.txt` (new),
`assets/CMakeLists.txt`,
new GPU-independent test file(s) for the capability classification,
tone-mapping/OETF CPU reference, and `execute()` widening,
`tests/runtime/material_realization_gpu_tests.cpp` (N=6 regression),
`tests/image_regression/goldens/{minimal_cube,world_scene,textured_quad,material_demo,lighting_demo,pbr_material_demo,hdr_roll_off_demo}/**`.

## Sequencing & Dependencies

M1 → M2 (needs `HdrColorTarget`) → M3 (independent of M1/M2, needed
before M4) → M4 (needs M3's contract names) → M5 (needs M1/M2's new
types/methods) → M6 (needs M1–M5 all landed) → M7 (needs M4/M6's own
pattern) → M8 (needs M1–M7) → M9 (needs M1–M8 merged, clean commit) →
M10 (needs M9 landed first, its own fixture-then-golden split).

Atomic groupings this Plan does not split across commits: M1 (RHI
type + factory + `PipelineCreateParams::colorFormat` retyped to a
`std::variant` + capability check); M2 (`CommandList` interface + impl
+ `ResourceBinding` + all four `execute()` widening points); M3
(descriptor contract + both compiler checks + usage string); M4 (both
new shaders + both existing shaders' clamp removal); M5 (`drawFrame()`
signature + two-pass body); M6 (Runtime members + `FormatRebuildCandidates`'s
new field + both rebuild branches + call site +
`material_realization.cpp`'s `colorFormat` switch); M9 (each golden's
own capture + sidecar, six independent atomic units); M10 (fixture
commit, strictly separate from its own golden commit).

## Verification Checklist

1. [ ] GPU-independent tests: `HdrColorTargetCreateError::FormatFeaturesUnsupported`
   synthetic-flags classification (M8); D-5 tone-mapping curve and D-6
   sRGB OETF CPU reference, hand-computed values (M8); shader-
   reflection-vs-C++-layout cross-check for both output-transform
   descriptor contracts (M8); `render_graph::execute()`'s widened
   Guard 0/dispatch against a synthetic 2-pass graph (M8).
2. [ ] Real-GPU positive-path tests: `HdrColorTarget` creation/resize
   (M8); two-pass graph executes cleanly under both final-target
   format classes, all three `MaterialKind`s, windowed and offscreen
   origins (M8); `*_Unorm`/`*_Srgb` display-equivalence within a
   defined, non-zero tolerance (M8); above-1.0-input roll-off proof
   (M8).
3. [ ] Spec 0021 N=6 descriptor-pool regression, re-confirming D-4's
   `N+2`/`N+3` formula against the real 60-descriptor-set ceiling (M8).
4. [ ] Image regression: all six existing goldens re-captured
   individually, each own sidecar/provenance, each own human visual
   review (M9); new `hdr_roll_off_demo` golden captured and reviewed
   (M10) — never a batch acceptance.
5. [ ] Vulkan Validation Layers clean, both Debug and Release, under
   both final-target format classes.
6. [ ] `ctest -LE gpu` and `ctest -L gpu`, both configurations.
7. [ ] `ATLANTIS_BUILD_TESTS=OFF` configure+build produces a working
   `atlantis_runtime.exe` with zero test executables, re-cooking every
   asset (including the new `hdr_roll_off_demo` scene) successfully.
8. [ ] C4062 (`/w14062`/`/WX`): `isSrgbFormat()`'s own `Format` switch
   (M6) confirmed to fail to compile if a case is omitted, matching
   `selectShaderPair()`'s own existing coverage.
9. [ ] Module/link graph: `Atlantis::Renderer`'s own dependency set
   unchanged (Core/RHI/RenderGraph only); no Vulkan header/`Vk*` type
   reaches `Renderer`, `RenderGraph`, or RHI's public headers from the
   new `HdrColorTarget` type or its Vulkan implementation.
10. [ ] `git diff --check` clean on the final Implementation diff.

## Rollback Plan

Milestones 1–8 are independently revertible in reverse order (M8
before M7 before M6 …), since later milestones only add new call
sites/files and never rewrite an earlier milestone's own shape.
Reverting M1/M2 (the `HdrColorTarget`/`CommandList` core) also requires
reverting every milestone depending on it (M3 onward) — called out
explicitly if a partial rollback is ever needed, never attempted
silently. M9/M10's own golden captures are reverted independently of
the code milestones — a golden re-capture found wrong after merge is
re-captured again, not rolled back alongside working code.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No deltas beyond this Plan's own Verification Checklist above.
