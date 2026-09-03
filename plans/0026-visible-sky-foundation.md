# Plan: Visible Sky Foundation

- **Spec:** [specs/0026-visible-sky-foundation.md](../specs/0026-visible-sky-foundation.md) (`Approved`)
- **Status:** Draft
- **Author:** slmao

## Objective

Implement Spec 0026's visible sky background: one additional draw inside
the existing HDR "draw" pass, sampling the already-realized environment
cubemap at mip 0 along a camera-rotation-only ray, always occluded by
opaque scene geometry, using ADR-0071's `depthWriteEnabled` mechanism —
with zero change to any no-environment scene's rendered output.

## Plan-Stage Decisions

These values close ADR-0071's own implementation-mechanism choices and
must not be left to be picked opportunistically in code.

### P1 — Sky vertex output: fixed device-space depth, reused fullscreen geometry

The sky Pipeline reuses `RuntimeApplication`'s/each fixture's existing
`fullscreenTriangleVertexBuffer_`/`...IndexBuffer_` and vertex schema
(`VertexInput { [[vk::location(0)]] float2 clipPosition; }`, the same
schema `outputTransformVertexLayout()` already resolves) — no new
geometry buffer, no new vertex-input function. The sky vertex shader
sets:

```
output.position = float4(input.clipPosition, kSkyClipDepth, 1.0);
```

`kSkyClipDepth = 0.999999` (a fixed, named `static const float`
literal in the shader, mirroring ADR-0068 D-5's own "one literal,
hand-verifiable" convention). With `w = 1.0`, NDC depth equals
`kSkyClipDepth` exactly — strictly less than the pass's own `depthClear
= 1.0f` (`renderer.cpp`), so the sky's own depth test
(`VK_COMPARE_OP_LESS`, unchanged, `vulkan_device.cpp:1189`) passes
against the `1.0f` clear the first time the sky draws. **Per
[ADR-0071's Proposed Correction](../adr/0071-visible-sky-background-rendering-integration.md#proposed-correction--2026-09-04-draw-order-claim),
this is a correctness requirement, not a convenience: the sky must draw
strictly before every `DrawItem` (P5).** If any opaque `DrawItem` drew
first and left a real depth value strictly between `kSkyClipDepth` and
`1.0f` at a covered pixel, the sky's own `LESS` test would still pass
there and incorrectly overwrite that geometry's color —
`depthWriteEnabled = false` (P4) only stops the sky from corrupting the
depth buffer for subsequent draws, it does not make draw order
irrelevant. `D32Sfloat`'s float32 precision resolves `0.999999` from
`1.0` comfortably (~1e-6 separation, far above float32 epsilon at that
magnitude).

### P2 — Ray reconstruction: rotation-only, using the existing view/projection matrices directly

The sky fragment shader reconstructs its own per-pixel ray with no new
uniform-buffer field, no new binding, and no separate `fovY`/`aspect`
value — it reads only the projection matrix's own `[0][0]`/`[1][1]`
diagonal terms (`proj._m00`/`proj._m11` in Slang's matrix-access syntax)
already present in the existing camera uniform, plus the view matrix's
own upper-left 3x3 rotation block:

```
float2 ndc = input.uv * 2.0 - 1.0;                                  // input.uv from clipPosition, same derivation as output-transform's own vertexMain()
float3 viewSpaceDir = normalize(float3(ndc.x / proj._m00, ndc.y / proj._m11, -1.0));
float3x3 viewRotation = (float3x3)view;                             // view's own upper-left 3x3 -- orthonormal (lookAtMatrix()), scene_extraction.cpp
float3 worldSpaceDir = normalize(mul(transpose(viewRotation), viewSpaceDir));
```

`transpose(viewRotation)` is view's own inverse rotation (orthonormal
matrix property) — camera translation (view's own column-3 offset) is
never read, satisfying Spec 0026's "invariant to camera translation"
requirement by construction, not by a runtime branch. `worldSpaceDir` is
sampled against `EnvironmentLighting::prefilteredEnvironment` via
`environment.SampleLevel(worldSpaceDir, 0.0)` — the identical
world-space-direction-to-cubemap-sample convention
`pbr_ibl.slang`'s own `environment.SampleLevel(R, lod)` call already
uses for specular reflection (Spec 0025), not a newly-derived
convention. `input.uv = input.clipPosition * 0.5 + 0.5` mirrors
`output_transform_unorm.slang`'s own identical derivation
(`vertexMain()`) byte-for-byte.

### P3 — Sky descriptor contract and Pipeline parameters

- `PipelineCreateParams`: `colorFormat = HdrFormat::Rgba16Float` (fixed,
  matching every geometry Pipeline, ADR-0068 D-4); `hasCameraUniformBinding
  = true`; `sampledTextureBindingCount = 1` (the cubemap, contiguous at
  binding 1, per ADR-0070 P2's existing binding-index rule);
  `hasDepthAttachment = true`; `depthWriteEnabled = false` (P4).
- Descriptor bindings: binding 0 is the existing 464-byte frame uniform
  (referenced by the sky fragment shader only — P2 needs no vertex-stage
  access to it); binding 1 is `Sampler2D`/`SamplerCube` sampling
  `EnvironmentLighting::prefilteredEnvironment` with
  `EnvironmentLighting::environmentSampler`. No push constant (no
  per-draw transform — the sky is one fixed fullscreen triangle, not
  scene content), matching `outputTransformExpectedDescriptorContract()`'s
  own empty-push-constant shape.
- `skyExpectedDescriptorContract()` (new, `descriptor_contract.h/.cpp`)
  is fixed to exactly:

```cpp
std::vector<DescriptorBinding> skyExpectedDescriptorContract() {
  return {DescriptorBinding{.set = 0, .binding = 0, .type = DescriptorType::UniformBuffer, .stage = ShaderStage::Fragment},
          DescriptorBinding{.set = 0, .binding = 1, .type = DescriptorType::Sampler, .stage = ShaderStage::Fragment}};
}
```

  Both bindings are Fragment-only — no Vertex-stage entry for either.
  This is confirmed, not inferred: a disposable Plan-stage probe shader
  (P2's exact declarations, discarded, never committed) was compiled with
  the real `slangc` toolchain (`C:/VulkanSDK/1.4.357.0/Bin/slangc.exe`),
  once per stage with `-reflection-json`. The vertex-stage reflection's
  own `entryPoints[0].bindings[]` lists both `camera` (index 0) and
  `environmentSampler` (index 1) with `"used": 0` — `slang_json_transform.cpp:192-194`
  (`if (!used) continue;`) drops any `"used": 0` entry before it ever
  reaches `ReflectionMetadata::descriptorBindings`, so the vertex stage's
  own transformed metadata has zero descriptor bindings. The fragment-stage
  reflection lists both with `"used": 1`, producing exactly the two
  Fragment-stage entries above. `validateDescriptorContractForStage()`'s
  own per-stage filter (`compile_and_validate.cpp:157-159`) then compares
  each stage against only the entries whose `.stage` matches — an empty
  scoped list for Vertex (matching the shader's own empty vertex-stage
  metadata) and both entries for Fragment.

### P4 — `depthWriteEnabled`: the exact additive `PipelineCreateParams` field and its Vulkan mapping

```cpp
// PipelineCreateParams, immediately after hasDepthAttachment:
bool depthWriteEnabled = true;  // meaningful only when hasDepthAttachment == true
```

`vulkan_device.cpp:1188` (`depthStencilState.depthWriteEnable = ...`)
becomes:

```cpp
depthStencilState.depthWriteEnable =
    (params.hasDepthAttachment && params.depthWriteEnabled) ? VK_TRUE : VK_FALSE;
```

`depthStencilState.depthTestEnable` (line 1187) and `depthCompareOp`
(line 1189, `VK_COMPARE_OP_LESS`) are unchanged. Default `true`
reproduces every existing Pipeline's current always-write-when-tested
behavior exactly — zero source change at any existing call site
(`createMaterial()`, every `MaterialKind` Pipeline, both output-transform
Pipelines, `fallbackMaterial_`). The sky Pipeline is the only Pipeline
this Plan sets `depthWriteEnabled = false` on.

### P5 — Sky draws first, inside the existing "draw" pass; no resize/format/environment-change rebuild

`Renderer::drawFrame()`'s "draw" pass execute lambda
(`src/renderer/src/renderer.cpp:48-99`) gains one new draw — bind sky
Pipeline, bind the existing fullscreen-triangle vertex/index buffers,
bind the frame uniform at binding 0, bind the environment cubemap+sampler
at binding 1, `drawIndexed(3)` — inserted immediately before the existing
`for (const DrawItem& item : drawItems)` loop, only when both
`skyPipeline` and `environmentLighting` are non-null (`ATLANTIS_CHECK_MSG`
otherwise, mirroring the existing `MaterialEnvironmentBinding::Ibl`
guard). **Per ADR-0071's Proposed Correction, this ordering is a
correctness requirement, not an implementation convenience** (P1) — the
sky draw call must precede the `DrawItem` loop unconditionally, every
frame, with no code path that could reorder them.

The sky Pipeline is created exactly once — alongside `fallbackMaterial_`
(`RuntimeApplication::initializeSteps()` Step 4c, `runtime_application.cpp:504-531`;
each fixture's own construction-time equivalent) — and never rebuilt: it
has no dependency on the negotiated final-target `Format` (unlike the
output-transform Pipeline pair, ADR-0068 D-6) and no dependency on
extent (unlike `hdrColorTarget_`/`depthTexture_`, which are recreated on
resize). It is gated only on whether `BootstrapConfig`/the fixture's own
`config` names an environment — the exact same, already-fixed-for-the-
`RuntimeApplication`-lifetime condition Spec 0025 M7 established for
`environmentData_`/`pbrIblVertexSpirv_` (`hasEnvironment`,
`!config.environmentArtifactPath.empty()`). No new resize, format-change,
or environment-change trigger is added anywhere.

## Milestones / Task Breakdown

### Milestone 1 — RHI `depthWriteEnabled` field

- Add `PipelineCreateParams::depthWriteEnabled` (P4) to
  `src/rhi/include/atlantis/rhi/types.h`.
- Apply the Vulkan mapping in `src/vulkan_backend/src/vulkan_device.cpp`
  (P4).
- GPU-independent compatibility test: default `depthWriteEnabled = true`
  reproduces `hasDepthAttachment = true`'s existing behavior for every
  existing call site — no behavior change (mirrors
  `hasCameraUniformBinding`/`hasDepthAttachment`'s own Plan 0024
  precedent test shape, `tests/rhi/types_tests.cpp`).
- Real-GPU test, fixed to a new file,
  `tests/vulkan_backend/pipeline_depth_write_gpu_tests.cpp` (registered in
  `tests/vulkan_backend/CMakeLists.txt`'s `atlantis_vulkan_backend_gpu_tests`
  target, alongside `descriptor_pool_growth_gpu_tests.cpp`) — a real,
  discriminating three-draw sequence, not a mere presence check:
  - Reuses the existing, already-compiled `minimal_mesh` shader pair
    unchanged (`shaders/minimal_mesh.vert.spv`/`.frag.spv`, relative-path
    convention already established by `descriptor_pool_growth_gpu_tests.cpp`)
    with an identity camera (`view = projection = kIdentityMatrix`) and
    identity `objectToWorld` push constant — the same identity-matrix
    convention `descriptor_pool_growth_gpu_tests.cpp:139`/
    `minimal_renderer_gpu_tests.cpp:404-407` already establish — so
    `output.position = float4(input.position, 1.0)` directly: vertex `z`
    is the NDC depth with no other transform to account for. No new
    shader is introduced.
  - Two `Pipeline`s from the same shader: Pipeline A
    (`hasDepthAttachment = true`, `depthWriteEnabled = false`) and
    Pipeline B (`hasDepthAttachment = true`, `depthWriteEnabled` at its
    default `true`).
  - A small (e.g. 64x64) `OffscreenTarget` (`Rgba8Unorm`) plus a
    `D32Sfloat` depth `Texture`, cleared to `depthClear = 1.0f`, and a
    readback `Buffer` — same resource shape
    `headless_rendering_gpu_tests.cpp` already sets up. One `CommandList`,
    one `submit()`, mirroring that file's own established headless
    draw-then-copy sequence exactly (`headless_rendering_gpu_tests.cpp:452-478`),
    never a second `CommandList` or a second `submit()`:
    1. A "draw" `RenderGraphBuilder`: declares the color and depth
       resources, one pass writing `ColorAttachmentOutput`/
       `DepthAttachmentReadWrite`. Its execute callback issues only the
       three discriminating draws, in order — Pipeline A draws a RED
       quad (NDC `x` in `[-1.0, 0.2]`, `z = 0.3`; "red-only" region
       `x < -0.2`, "overlap" region `-0.2 <= x <= 0.2`); Pipeline B draws
       a GREEN quad (`x` in `[-0.2, 1.0]`, `z = 0.6`, farther than red)
       over the overlap region; Pipeline A draws a BLUE quad (`x` in
       `[-0.2, 1.0]`, `z = 0.9`, farther than green) over the same
       overlap region — no `copyRenderTargetToBuffer()` call anywhere in
       this callback. The color `ResourceBinding`'s own `finalState` is
       fixed to `atlantis::rhi::ResourceState::TransferSource` (matching
       `headless_rendering_gpu_tests.cpp:458`'s own identical headless
       convention); compiled and executed via `render_graph::execute()`
       against the one `CommandList` — dynamic rendering has ended by the
       time this call returns.
    2. A second, independent "copy" `RenderGraphBuilder` — mirroring
       `headless_rendering_gpu_tests.cpp:461-474`'s own copyBuilder
       exactly: one resource, one pass writing `TransferSource`, its
       execute callback calling `cmd.copyRenderTargetToBuffer(*target,
       *readbackBuffer)` and nothing else. Its own `ResourceBinding` sets
       `.incomingState = atlantis::rhi::ResourceState::TransferSource`
       (matching the draw graph's own `finalState` above). Compiled and
       executed via `render_graph::execute()` against the **same**
       `CommandList` as step 1 — `copyRenderTargetToBuffer()` is called
       only from inside this second graph's own pass callback, never
       from the draw pass's callback and never outside any
       `RenderGraphBuilder`/`execute()` call.
    3. `device->submit(std::move(commandList), *target)` — the one and
       only `submit()` call — then `device->waitIdle()`. Only after both
       succeed are the two discriminating pixels read from
       `readbackBuffer->mappedData()` and asserted exactly:
       - The red-only region (`x ≈ -0.6`) is RED — proves Pipeline A's
         own first draw actually executed.
       - The overlap region (`x ≈ 0.0`) is GREEN, not RED and not BLUE —
         GREEN (not RED) proves Pipeline A's own draw did **not** write
         depth (a real `0.3` write would have failed green's own `LESS`
         test and left the pixel RED); GREEN (not BLUE) proves the depth
         **test** stayed enabled for Pipeline A's own third draw (blue,
         farther than green, must fail `LESS` against green's real,
         written `0.6` and never overwrite it — if depth testing had
         also been disabled, blue would have incorrectly won).
  - Vulkan Validation Layers clean. Every GPU command in this test is
    recorded by one of the two `RenderGraphBuilder`/`execute()` calls
    above — no direct `vkCmd*`-adjacent `CommandList` call is ever issued
    outside a compiled `RenderGraph` pass callback, matching this
    codebase's own "no ad hoc submit, no bypass of RenderGraph"
    architecture principle (AGENTS.md).

### Milestone 2 — Renderer integration

- Add the sky draw to `Renderer::drawFrame()`'s "draw" pass (P5);
  add the new, caller-owned, nullable `const atlantis::rhi::Pipeline*
  skyPipeline = nullptr` parameter to
  `src/renderer/include/atlantis/renderer/renderer.h`.
- Update every real call site to pass the new parameter (nullptr for
  every no-environment/no-sky caller — see Files/Modules Touched).
- `tests/renderer/renderer_ownership_tests.cpp`: every existing
  `TEST_CASE` continues to omit the new trailing parameter (its default
  `nullptr` reproduces today's exact recorded event sequence — zero
  existing assertion changes). Add one new `TEST_CASE` (mirroring the
  existing "IBL Material" test's own shape): a non-null `skyPipeline`
  with a non-null `environmentLighting` records the sky's own
  bind/bind/bind/bind/drawIndexed(3) sequence before the first
  `DrawItem`'s own sequence, binding the cubemap at binding 1 with
  `EnvironmentLighting::prefilteredEnvironment`/`environmentSampler`; a
  non-null `skyPipeline` with a null `environmentLighting` fires the
  `ATLANTIS_CHECK_MSG` guard (via the existing `ScopedFailureHandler`
  pattern) and draws no sky.

### Milestone 3 — RHI/Runtime lifecycle wiring

- `src/runtime/include/atlantis/runtime/bootstrap_config.h`: four new
  fields — `skyVertexShaderSpirvPath`, `skyVertexShaderReflectionPath`,
  `skyFragmentShaderSpirvPath`, `skyFragmentShaderReflectionPath` —
  mirroring `pbrIblVertex...`'s own four-field shape exactly, required
  only when `environmentArtifactPath` is non-empty.
  `validateEnvironmentBootstrapConfig()` widens to require all four
  non-empty in that case (mirroring its own existing
  `pbrIbl*`/`environmentArtifactPath`/`environmentMetadataPath` checks).
- `src/runtime/include/atlantis/runtime/init_error.h`/`.cpp`: one new
  enumerator, `SkyPipelineCreateFailed` (mirroring
  `OutputTransformUnormPipelineCreateFailed`'s own shape; shader-load
  failure reuses the existing generic `ShaderLoadFailed`, matching every
  other built-in shader pair).
- `src/runtime/include/atlantis/runtime/runtime_application.h`: add
  `skyVertexInputLayout_`/`skyVertexSpirv_`/`skyFragmentSpirv_` (loaded
  conditionally, mirroring `pbrIblVertexSpirv_`'s own group) and
  `skyPipeline_` (a raw `std::unique_ptr<atlantis::rhi::Pipeline>`, not a
  `Material` — no push constant, no per-`DrawItem` mesh — declared
  immediately after `fallbackMaterial_`, matching its own "created once,
  never rebuilt" lifecycle, P5).
- `src/runtime/src/runtime_application.cpp`:
  - `initializeSteps()` Step 2e (`hasEnvironment` block,
    `runtime_application.cpp:345-367`): also load the sky shader pair's
    SPIR-V/reflection, resolving its vertex layout via the *existing*
    `outputTransformVertexLayout()` function (P1's reused fullscreen
    schema — no new vertex-layout function).
  - Step 4c/4d (immediately after `fallbackMaterial_` creation,
    `runtime_application.cpp:504-531`): when `hasEnvironment`, create
    `skyPipeline_` via a raw `device_->createPipeline(...)` call (P3/P4),
    fatal on error (`SkyPipelineCreateFailed`, mirrors
    `fallbackMaterial_`'s own fatal-on-error treatment).
  - `runFrame()`'s `renderer_.drawFrame(...)` call
    (`runtime_application.cpp:1129-1132`): pass `skyPipeline_.get()` as
    the new trailing argument (already `nullptr` when `!hasEnvironment`,
    matching P5's "same condition throughout the lifetime" invariant —
    no additional guard needed at the call site).
  - `shutdown()`: `skyPipeline_.reset()` immediately after
    `fallbackMaterial_.reset()` (`runtime_application.cpp:1244`),
    matching its own declaration-order placement.
- `src/runtime/main.cpp`, `src/runtime/CMakeLists.txt`: wire the new sky
  shader pair's compiled-output paths into `BootstrapConfig` and its own
  `add_dependencies(... sky_shaders)`, mirroring `pbrIbl`'s own identical
  wiring exactly.

### Milestone 4 — `sky` shader and descriptor contract

- `shaders/sky/sky.slang`: vertex entry per P1 (reuses the fullscreen
  schema), fragment entry per P2/P3. `shaders/sky/CMakeLists.txt`:
  `atlantis_add_slang_shader_pair(NAME sky ... EXPECTED_CONTRACT sky)`,
  mirroring `shaders/output_transform_unorm/CMakeLists.txt`'s own shape
  exactly (P3).
- Root `CMakeLists.txt`: `add_subdirectory(shaders/sky)`, placed after
  `add_subdirectory(shaders/output_transform_srgb)` (matches this file's
  own declared-in-dependency-order convention, `CMakeLists.txt:79-120`).
- `src/shader_system/include/atlantis/shader_system/descriptor_contract.h`/
  `.cpp`: add `skyExpectedDescriptorContract()` (P3).
- `src/tools/shader_compiler/compile_and_validate.cpp`: add an
  `expectedContract == "sky"` branch calling
  `skyExpectedDescriptorContract()`, grouped into the existing
  empty-push-constant branch alongside `"output-transform-unorm"`/
  `"output-transform-srgb"` (`compile_and_validate.cpp:148,193`) — sky
  needs neither `validatePushConstantsForFragmentStage()` (line 364) nor
  a non-empty push-constant expectation, matching output-transform's own
  identical treatment.
- `tests/shader_system/`: a new `sky_reflection_tests.cpp` (mirroring
  `pbr_ibl_reflection_tests.cpp`'s own shape) proving
  `skyExpectedDescriptorContract()`'s exact binding list and stage
  visibility against the real, freshly-compiled `sky.slang` reflection
  (P3's own "confirmed against real reflection" resolution happens here).
  `tests/shader_system/CMakeLists.txt`: register it.

### Milestone 5 — Fixture wiring (image regression)

- `tests/image_regression/fixture/pbr_material_demo_fixture.h`: add
  `skyVertexInputLayout`/`skyVertexSpirv`/`skyFragmentSpirv`/
  `skyPipeline` members, populated only when `config.environmentArtifactPath`
  is non-empty (mirroring the existing `pbrIblVertexInputLayout`/etc.
  group, `pbr_material_demo_fixture.h:61-63`).
- `tests/image_regression/fixture/pbr_material_demo_fixture.cpp`:
  `setUpPbrMaterialDemoFixture()` loads the sky shader pair inside the
  existing `if (!config.environmentArtifactPath.empty())` block
  (`pbr_material_demo_fixture.cpp:178-190`) and creates `fixture.skyPipeline`
  immediately after `fixture.outputTransformPipeline`
  (`pbr_material_demo_fixture.cpp:308-319`), same parameters as
  Milestone 3's `skyPipeline_`. `renderPbrMaterialDemoFrame()`'s own
  `renderer.drawFrame(...)` call (`pbr_material_demo_fixture.cpp:480-484`)
  passes `fixture.skyPipeline.get()` (already `nullptr` for
  `pbr_material_demo`'s own no-environment config).
- `tests/image_regression/fixture/ibl_material_demo_fixture.h`/`.cpp`:
  **untouched** — `IblMaterialDemoFixture` is a type alias to
  `PbrMaterialDemoFixture`; `setUpIblMaterialDemoFixture()`/
  `renderIblMaterialDemoFrame()` already forward to the same
  `setUpPbrMaterialDemoFixture()`/`renderPbrMaterialDemoFrame()`
  functions above with `ibl_material_demo`'s own environment-enabled
  `BootstrapConfig` — this Milestone's changes reach it automatically.
- `tests/image_regression/CMakeLists.txt`: add the sky shader pair's
  compile-definition/dependency wiring for
  `atlantis_image_regression_fixture`/`atlantis_image_regression_gpu_tests`,
  mirroring `ATLANTIS_..._PBR_IBL_SHADER_DIR`'s own existing entry.
- Every OTHER fixture (`minimal_cube_fixture.cpp`,
  `textured_quad_fixture.cpp`, `material_demo_fixture.cpp`,
  `lighting_demo_fixture.cpp`, `world_scene_fixture.cpp`,
  `world_scene_loaded_fixture.cpp`) and example
  (`examples/minimal_renderer_demo/main.cpp`,
  `examples/headless_rendering_demo/main.cpp`) and
  `tests/vulkan_backend/{headless_rendering,minimal_renderer,
  descriptor_pool_growth}_gpu_tests.cpp`: each `drawFrame()` call site
  is updated to compile against the new signature by omitting the
  trailing argument (default `nullptr`) — **zero behavioral change**,
  confirmed by each file's own existing tests/golden continuing to pass
  unmodified (Milestone 7).

### Milestone 6 — Descriptor-pool peak re-derivation

- `tests/runtime/material_realization_gpu_tests.cpp`: the existing "N=6
  HDR pipeline descriptor-set peak is exactly N+3" `TEST_CASE`
  (`material_realization_gpu_tests.cpp:697-710`) models the
  no-environment case and remains byte-for-byte correct as written — its
  own comment is extended by one clause noting it does not model an
  environment-enabled scene. Add one new, sibling `TEST_CASE`
  ("N=6 HDR pipeline descriptor-set peak is exactly N+4 with an
  environment/sky Pipeline present") that adds one more fixed,
  never-rebuilt synthetic Pipeline (mirroring the existing
  `kFallbackPipelineCount`/`kSteadyOutputTransformPipelineCount`
  constants' own shape) and asserts `kExpectedSteadySetCount == 9`
  (`N + 3`: `N` materials + fallback + output-transform + sky) and
  `kExpectedPeakSetCount == 10` (`N + 4`: steady + 1 transient
  output-transform), against the same real 60-set ceiling.

### Milestone 7 — Verification and golden re-capture

- All seven existing no-environment goldens (`minimal_cube`,
  `world_scene`, `textured_quad`, `material_demo`, `lighting_demo`,
  `pbr_material_demo`, `hdr_roll_off_demo`) are proven byte-identical by
  running their own **existing, unmodified** capture-compare
  `TEST_CASE`s in `tests/image_regression/*_gpu_tests.cpp` — no golden
  file, sidecar, or test assertion in this set is touched by this Plan;
  a pass proves Milestone 5's `nullptr`-default change is behaviorally
  inert for every one of them.
- `ibl_material_demo`'s existing golden is expected to change (flat
  clear-color background -> visible sky) and is re-captured on a clean
  post-Milestone-6 commit, following the exact same two-commit process
  Spec 0025/ADR-0042 already established
  (`tests/image_regression/golden_generator/pbr_material_demo_main.cpp`,
  reused unchanged — it already renders `ibl_material_demo` via the same
  fixture): a fixture/provenance commit, then a separate, human-reviewed
  golden-image commit recording `golden_update_reason: "visible sky
  background added, Spec 0026"` alongside the existing source/artifact
  SHA-256 provenance fields
  (`tests/image_regression/support/provenance.h`/`.cpp`, unchanged
  shape). The image is shown to the human reviewer before the golden
  commit is made — never auto-accepted, per AGENTS.md/ADR-0042.
- Full Verification Checklist (below) run in both Debug and Release.

## Files / Modules Touched (expected)

- RHI: `src/rhi/include/atlantis/rhi/types.h`
  (`PipelineCreateParams::depthWriteEnabled`).
- Vulkan Backend: `src/vulkan_backend/src/vulkan_device.cpp`
  (`depthStencilState.depthWriteEnable` mapping).
- Renderer: `src/renderer/include/atlantis/renderer/renderer.h`,
  `src/renderer/src/renderer.cpp` (`drawFrame()`'s new `skyPipeline`
  parameter and sky draw).
- Shader/descriptor contract:
  `src/shader_system/include/atlantis/shader_system/descriptor_contract.h`,
  `src/shader_system/src/descriptor_contract.cpp`,
  `src/tools/shader_compiler/compile_and_validate.cpp`,
  `shaders/sky/sky.slang` (new), `shaders/sky/CMakeLists.txt` (new),
  root `CMakeLists.txt`.
- Runtime: `src/runtime/include/atlantis/runtime/{bootstrap_config.h,
  init_error.h,runtime_application.h}`,
  `src/runtime/src/{bootstrap_config.cpp,init_error.cpp,
  runtime_application.cpp}`, `src/runtime/main.cpp`,
  `src/runtime/CMakeLists.txt`.
- Tests: `tests/rhi/types_tests.cpp`,
  `tests/vulkan_backend/pipeline_depth_write_gpu_tests.cpp` (new),
  `tests/vulkan_backend/CMakeLists.txt`,
  `tests/renderer/renderer_ownership_tests.cpp`,
  `tests/shader_system/sky_reflection_tests.cpp` (new),
  `tests/shader_system/CMakeLists.txt`,
  `tests/runtime/material_realization_gpu_tests.cpp`,
  `tests/image_regression/fixture/pbr_material_demo_fixture.h`/`.cpp`,
  `tests/image_regression/CMakeLists.txt`, and every `drawFrame()` call
  site enumerated in Milestone 5's own last bullet (signature-compatibility
  update only, no test-assertion change).
- Image regression golden:
  `tests/image_regression/goldens/ibl_material_demo/
  ibl_material_demo_512x512_rgba8unorm.{png,sidecar.txt}` (re-captured,
  Milestone 7, human-reviewed).
- Documentation: this Plan; `specs/README.md`, `README.md`,
  `docs/project-blueprint.md`, `src/README.md` status pointers, updated
  once at PR time to reflect implementation (not part of Implementation
  code).

Any Implementation-touched source outside this list is a disclosed Plan
deviation, per AGENTS.md.

## Sequencing & Dependencies

Milestone 1 (RHI field) has no dependency and unblocks Milestone 2's
Renderer integration. Milestone 2 (Renderer signature) unblocks every
call-site update. Milestones 3 (Runtime lifecycle) and 4 (shader/
descriptor contract) depend on Milestones 1–2 and on each other only at
the final wiring step (Runtime needs the compiled shader's real output
paths); they may be implemented in either order but both must land before
Milestone 5. Milestone 5 (fixture wiring) depends on 1–4. Milestone 6
(descriptor-pool re-derivation) depends on 3 (needs the real sky Pipeline
shape). Milestone 7 (verification/golden) follows a clean commit of
Milestones 1–6; the golden re-capture is its own separate, human-reviewed
commit, never bundled with any code change, per ADR-0042.

Every atomic commit keeps `Renderer::drawFrame()`'s signature synchronized
with every call site (Milestone 2 lands as one commit touching every
caller, not a partial signature change) and the sky descriptor contract
synchronized with its own shader reflection test (Milestone 4). No
partial commit leaves an existing target uncompilable.

## Verification Checklist

Maps to Spec 0026's own Testing & Verification Plan:

- [ ] GPU-independent: `depthWriteEnabled`'s default reproduces existing
  Pipeline behavior (`tests/rhi/types_tests.cpp`); sky's own descriptor
  contract reflection-verified (`tests/shader_system/sky_reflection_tests.cpp`).
- [ ] GPU: `pipeline_depth_write_gpu_tests.cpp`'s three-draw discriminator
  confirms `depthWriteEnabled = false` disables writes while keeping the
  depth test enabled (Milestone 1); sky visible with no `DrawItem`s
  present; sky fully occluded behind an opaque `DrawItem` filling the
  frame, confirming sky-first ordering is enforced (Renderer-level test,
  Milestone 2); sky orientation changes under camera rotation, unchanged
  under pure camera translation; no environment configured draws the
  existing flat background byte-identical to today (Milestone 7's
  seven-golden proof); Debug and Release, Vulkan Validation Layers clean.
- [ ] Image regression: all seven no-environment goldens byte-identical
  (existing, unmodified capture-compare tests); `ibl_material_demo`'s
  golden re-captured on a clean commit, human-reviewed, with an updated
  `golden_update_reason` (Milestone 7).
- [ ] Descriptor pool: the new N=6-with-sky `TEST_CASE` confirms `N+4`
  peak against the real 60-set ceiling (Milestone 6).
- [ ] Full `ctest -LE gpu` and `ctest -L gpu`, Debug and Release; exact
  test counts recorded in the Implementation PR.
- [ ] Fresh `ATLANTIS_BUILD_TESTS=OFF` configure/build produces a working
  `atlantis_runtime.exe` with zero test executables.
- [ ] `/w14062 /WX`: no new enum/switch is introduced by this Plan (sky
  is a nullable pointer parameter and one additive bool field, not a new
  enumerator) — confirmed by a clean build alone, no new negative probe
  required.
- [ ] Module/link/include scan: `Atlantis::Renderer` still links
  `Core`/`RHI`/`RenderGraph` only (no new dependency — the sky draw is
  ordinary `CommandList` calls, like every other draw); no new `Vk*`
  symbol outside Vulkan Backend.
- [ ] `git diff --check` and committed-file audit clean; no generated
  `.spv`, reflection JSON, or build directory committed.

## Rollback Plan

Revert Milestone 7's golden-recapture commit first (restoring
`ibl_material_demo`'s prior flat-background golden), then Milestone 6,
then Milestones 5 through 1 in reverse dependency order. `depthWriteEnabled`
defaults `true` and every existing Pipeline is unaffected by its mere
presence, so a partial rollback stopping after Milestone 1 alone leaves
the RHI field inert and harmless. `Renderer::drawFrame()`'s new parameter
and every call site are one atomic rollback group (Milestone 2); reverting
it without reverting Milestone 3's Runtime wiring would fail to compile,
so Milestones 2–5 roll back together. No `World`/`Scene` schema or asset
format is introduced, so no data migration is needed at any rollback
point.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No deltas beyond the Verification Checklist above.

## Plan Review — Items for Human Confirmation

1. P1's fixed `kSkyClipDepth = 0.999999` literal, together with P4's
   `depthWriteEnabled` field and ADR-0071's Proposed Correction requiring
   sky to draw strictly before every `DrawItem` — the Alternatives
   Considered in ADR-0071 already reject a `depthCompareOp` override in
   favor of this write-disable-plus-fixed-ordering mechanism; this Plan
   does not reopen that choice, only fixes the literal and the corrected
   ordering requirement.
2. P2's reuse of the existing view/projection matrices' own diagonal/
   rotation terms, with zero new camera-buffer field — confirms ADR-0071's
   own "shader-only math" decision is achievable with the current 464-byte
   uniform layout exactly as it stands today.
3. Milestone 6's new descriptor-pool `TEST_CASE` re-derives `N+3`/`N+4`
   as a synthetic, sibling test rather than mutating the existing `N+2`/
   `N+3` no-environment test — confirms this does not need to become one
   parameterized test instead.
