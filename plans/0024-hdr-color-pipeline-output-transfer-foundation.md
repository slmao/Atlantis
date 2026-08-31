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
  `HdrFormat::Rgba16Float` (a deliberately separate enum, ADR-0068 D-2).
  Mirroring `depthFormat`'s own existing sibling-field pattern
  (`types.h:191`), `PipelineCreateParams` gains a new
  `std::optional<HdrFormat> hdrColorFormat` field: when set, Vulkan
  Backend's `createPipeline()` uses it for the color-attachment format
  instead of `colorFormat` (which stays `Format::Unknown` on those
  calls). This is the one real structural addition ADR-0068 did not
  spell out at struct-field level — named explicitly here, not
  silently invented during Implementation.
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

## Milestones / Task Breakdown

### Milestone 1 — `HdrFormat`, `HdrColorTarget`, `HdrColorTargetCreateError`, `PipelineCreateParams::hdrColorFormat`

ADR-0068 D-1/D-2. RHI public surface:

- `src/rhi/include/atlantis/rhi/types.h`: new `enum class HdrFormat { Rgba16Float };`; new
  `enum class HdrColorTargetCreateError { FormatFeaturesUnsupported, AllocationFailed, ImageCreationFailed, ImageViewCreationFailed };`;
  new `struct HdrColorTargetCreateParams { Extent2D extent; HdrFormat format = HdrFormat::Rgba16Float; };`;
  `PipelineCreateParams` gains `std::optional<HdrFormat> hdrColorFormat;`.
- `src/rhi/include/atlantis/rhi/hdr_color_target.h` (new): abstract
  `HdrColorTarget` class — `extent()`, `format()` — inheriting neither
  `RenderTarget` nor `SampledTexture` (ADR-0068 D-1's own correction).
- `src/rhi/include/atlantis/rhi/device.h`: new
  `createHdrColorTarget(const HdrColorTargetCreateParams&)` factory
  method, alongside `createOffscreenTarget()`.
- `src/vulkan_backend/src/vulkan_hdr_color_target.h/.cpp` (new):
  `VulkanHdrColorTarget final : public atlantis::rhi::HdrColorTarget`,
  mirroring `VulkanSampledTexture` exactly (device/image/memory/
  imageView/extent/format members, `image()`/`imageView()` accessors).
- `src/vulkan_backend/src/vulkan_device.cpp`: new `toVkFormat(HdrFormat)`
  overload (one case, `VK_FORMAT_R16G16B16A16_SFLOAT`); new
  `createHdrColorTarget()` — queries
  `vkGetPhysicalDeviceFormatProperties()` first, returns
  `Result::Err(FormatFeaturesUnsupported)` if `optimalTilingFeatures`
  lacks `COLOR_ATTACHMENT_BIT` or `SAMPLED_IMAGE_BIT` (never
  `ATLANTIS_CHECK`), then follows `createOffscreenTarget()`'s own
  create/alloc/bind/view sequence; `createPipeline()` gains a small
  branch — `params.hdrColorFormat.has_value() ? toVkFormat(*params.hdrColorFormat) : toVkFormat(params.colorFormat)`
  — for the color-attachment `VkFormat` only, nothing else in
  `createPipeline()` changes.
- `src/vulkan_backend/src/vulkan_result.h/.cpp`: new
  `toHdrColorTargetCreateError(VkResult)`, mirroring
  `toTextureCreateError()` exactly (every non-success `VkResult` →
  `ImageCreationFailed`).

**Atomic:** the RHI types, the `Device`/`VulkanDevice` factory method,
`PipelineCreateParams`'s new field and `createPipeline()`'s branch, and
the capability-check land in one commit — a build where
`HdrColorTarget` exists but no concrete `Device` can create one (or
vice versa) is never checked in.

### Milestone 2 — `CommandList` surface, `ResourceBinding`, `render_graph::execute()` widening

ADR-0068 D-1/D-3.

- `src/rhi/include/atlantis/rhi/command_list.h`: three new/overloaded
  methods —
  `transitionResource(HdrColorTarget&, ResourceState, ResourceState)`,
  `beginRendering(HdrColorTarget&, Texture* depth, ClearColorValue, float)`,
  `bindTexture(const HdrColorTarget&, const Sampler&)`.
- `src/vulkan_backend/src/vulkan_command_list.cpp`: three concrete
  implementations, each a direct copy of its `SampledTexture`/
  `RenderTarget` sibling with `VulkanHdrColorTarget` substituted via
  `static_cast` (single real implementer, no `dynamic_cast`) —
  `bindTexture()`'s own descriptor-write reuses `fullColorResourceRange()`/
  binding-1/`COMBINED_IMAGE_SAMPLER` exactly as the existing
  `SampledTexture` overload does.
- `src/render_graph/include/atlantis/render_graph/execution.h`:
  `ResourceBinding` gains `HdrColorTarget* hdrColorTarget = nullptr;`.
- `src/render_graph/src/execution.cpp`: Guard 0's `boundCount` widens
  to four terms; the per-usage transition dispatch (`:117-130`) gains a
  fourth `else if` branch; the draw-pass `beginRendering()` call site
  (`:147`) checks which binding field is populated and calls the
  matching overload; the trailing `finalState` loop (`:180-194`) gains
  a third bound-type check. Guard 2 itself is untouched — it is already
  scoped to `binding.target == nullptr → skip`, which already excludes
  the new field.

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

- `src/renderer/include/atlantis/renderer/renderer.h`: `drawFrame()`
  gains required parameters: `HdrColorTarget& hdrColorTarget`,
  `Buffer& fullscreenTriangleVertexBuffer`,
  `Buffer& fullscreenTriangleIndexBuffer`,
  `Pipeline& outputTransformPipeline`, `Sampler& outputTransformSampler`
  — all borrowed, none owned (`Renderer` stays stateless).
- `src/renderer/src/renderer.cpp`: the existing single-pass
  `RenderGraphBuilder` becomes two passes on the same builder — pass 1
  (unchanged `DrawItem` loop) writes `ColorAttachmentOutput` into
  `hdrColorTarget`; pass 2 reads it (`ShaderRead`) and writes
  `ColorAttachmentOutput` into the caller's final `colorTarget`,
  binding `outputTransformPipeline`, the fullscreen triangle's vertex/
  index buffers, and `outputTransformSampler` bound to
  `hdrColorTarget`. One `RenderGraphBuilder::compile()`/
  `render_graph::execute()` call pair, unchanged in count. `Renderer`
  still never calls `Device::submit()`/`Presentation::present()`.

**Atomic:** the signature change and the two-pass graph construction
land together — `drawFrame()` cannot compile with the old signature
and the new graph body, or vice versa.

### Milestone 6 — Runtime integration

ADR-0068 D-1/D-3/D-4/D-6.

- `src/runtime/include/atlantis/runtime/bootstrap_config.h`: four new
  reflection-path fields (vertex/fragment × unorm/srgb), mirroring
  `pbrDirectLitVertexShaderReflectionPath` (`:51`).
- `src/runtime/include/atlantis/runtime/runtime_application.h`: new
  members — `hdrColorTarget_` (`unique_ptr<HdrColorTarget>`);
  `fullscreenTriangleVertexBuffer_`/`...IndexBuffer_`
  (`unique_ptr<Buffer>`, created once, never resized);
  `outputTransformSampler_` (`unique_ptr<Sampler>`, created once);
  `outputTransformUnormPipeline_`/`...SrgbPipeline_`
  (`unique_ptr<Pipeline>` each); the four new SPIR-V/reflection-layout
  members these two shader pairs need (mirroring the existing per-pair
  member groups).
- `src/runtime/src/runtime_application.cpp`:
  - Startup: create the fullscreen-triangle buffers and
    `outputTransformSampler_` once (never touched again).
  - The existing format-change branch (`:489-508`) additionally
    rebuilds whichever of `outputTransformUnormPipeline_`/
    `...SrgbPipeline_` matches the new `currentFormat`, via a new,
    small `isSrgbFormat(atlantis::rhi::Format) -> bool` helper
    (`material_realization.cpp`, mirroring `selectShaderPair()`'s
    closed-switch shape) — only ONE of the two Pipelines is rebuilt
    per event, matching D-4's own `+1` transient-set accounting; the
    other stays as last built.
  - The existing extent-change branch (`:510-522`) additionally
    recreates `hdrColorTarget_`, same trigger, same `Result`-based
    failure handling as the depth `Texture`.
  - The `Renderer::drawFrame()` call site passes `*hdrColorTarget_`,
    the fullscreen buffers, whichever output-transform `Pipeline`/
    `Sampler` matches the current target's format class, and the
    unchanged existing arguments.
- `src/runtime/src/material_realization.cpp`: `realizeOneMaterialCandidate()`/
  `rebuildMaterialsForFormatChange()` (`:101-333`, `:338-...`) —
  `PipelineCreateParams{.hdrColorFormat = atlantis::rhi::HdrFormat::Rgba16Float}`
  replaces every `.colorFormat = colorFormat` for a geometry Pipeline;
  both functions' own now-unused `colorFormat`/`newColorFormat`
  parameter is removed — every one of their own call sites (Runtime,
  every image-regression fixture, Milestone 7) updated accordingly.

**Atomic:** Runtime's own member additions, the two rebuild-branch
widenings, the `drawFrame()` call-site update, and
`material_realization.cpp`'s `hdrColorFormat` switch land together.

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
— never a batch operation. For each of `minimal_cube`, `world_scene`,
`textured_quad`, `material_demo`, `lighting_demo`, `pbr_material_demo`:
run that fixture's own golden-generator against the clean commit
Milestones 1–8 land on; record that capture's own real provenance
(`source_revision`, GPU/driver, timestamp) in its sidecar; a human
reviews the new image before it is accepted (non-black, no garbage,
recognizably the same scene, expected transfer-function shift only).
**Six separate acceptance decisions, six separate sidecars** — a human
approving one does not imply approval of the other five.

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
type + factory + `PipelineCreateParams` field + capability check); M2
(`CommandList` interface + impl + `ResourceBinding` + all four
`execute()` widening points); M3 (descriptor contract + both compiler
checks + usage string); M4 (both new shaders + both existing shaders'
clamp removal); M5 (`drawFrame()` signature + two-pass body); M6
(Runtime members + both rebuild branches + call site +
`material_realization.cpp`'s `hdrColorFormat` switch); M9 (each
golden's own capture + sidecar, six independent atomic units); M10
(fixture commit, strictly separate from its own golden commit).

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
