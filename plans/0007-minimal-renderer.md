# Plan: Minimal Renderer

- **Spec:** [Spec 0007](../specs/0007-minimal-renderer.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code at explicit human direction; reviewed and
  approved by a human per the Human Review Approval note below.
- **Human Review Approval (2026-08-11):** slmao, completing a joint Spec 0007 +
  Plan 0007 Human Review across three prior review rounds. The reviewer
  approved this Plan's candidate public API (§2–§13), ownership model, state
  machines, module boundaries, implementation order (§18), and verification
  plan (§15), including explicitly:
  1. **The fixed, narrowly-scoped camera uniform descriptor mechanism** (§10):
     exactly one `VkDescriptorSetLayout` binding, one `VulkanDevice`-owned
     `VkDescriptorPool` with `maxSets = 4` derived from this Plan's actual peak
     concurrent descriptor-set count (2 — one `Material` in steady state plus
     the prior one momentarily alive during a format-change rebuild), one
     `VkDescriptorSet` per `VulkanPipeline` freed at that `Pipeline`'s
     destruction — never a general descriptor-set/bindless API, never exposed
     outside `vulkan_pipeline.*`/`vulkan_device.*`.
  2. **Shader assets loaded by a plain relative path from each consumer's own
     build-output working directory** (§12) — no Win32 file-path/module API, no
     new Core/Platform path-resolution API; `catch_discover_tests()`'s
     `WORKING_DIRECTORY` for the GPU test, a `run_minimal_renderer_demo`
     convenience CMake target for the demo.
  3. **Dynamic rendering's capability-detected dual path, including its
     instance-level query prerequisite** (§8):
     `VK_KHR_get_physical_device_properties2` queried and conditionally enabled
     at the instance level before `vkCreateInstance()`;
     `vkGetPhysicalDeviceFeatures2KHR` resolved via `vkGetInstanceProcAddr` only
     once that succeeds, never assumed statically linkable; the instance-level
     query mechanism and the device-level core-1.3-vs-`VK_KHR_dynamic_rendering`
     capability kept as two explicitly distinct layers; the Vulkan Backend's
     overall minimum supported API version left unraised; a device with neither
     path returning the existing
     `DeviceCreateError::DynamicRenderingUnavailable`, never a crash or a
     `VkRenderPass`/`VkFramebuffer` fallback.
  4. **`Mesh`/`Material` as caller-owned aggregates and `Renderer` as a
     stateless, non-caching orchestrator** (§11) — `createMesh()`/
     `createMaterial()` as free functions, never `Renderer` methods; `Renderer`
     retaining no GPU resource or frame-to-frame state; the depth `Texture`'s
     resize responsibility and the `Pipeline`'s format-change responsibility
     both sitting with the caller.
  5. **This round's vertex/index/uniform `Buffer`s never declared as
     RenderGraph logical resources** (§6) — correct given they are
     host-write/device-read-only this round with write-before-read guaranteed
     by construction; explicitly not a general rule that extends to a future
     GPU-written buffer.
  6. **The direct, unpooled, per-resource GPU memory allocation policy** (§9),
     **the create-before-destroy format-change/extent-change rebuild
     discipline** (§13), and **the single-frame-in-flight-scoped design of
     every synchronization argument this Plan makes** — all confirmed as
     correct within, and not generalized beyond, this round's scope.

  Implementation is authorized, but must not begin until this Plan's own PR has
  merged into `main`.
- **Post-Approval Deviation (resolved 2026-08-13):** as shipped (PR #28), §8's
  dynamic-rendering **Core** path unconditionally enables `VK_KHR_dynamic_rendering`
  even on a Vulkan 1.3+ device; a fix attempt found a genuinely-core
  (non-`KHR`) dynamic-rendering entry point is not reliably obtainable while
  the instance requests `VkApplicationInfo::apiVersion = VK_API_VERSION_1_0` —
  precisely the Human Review Blocker this Plan's list already names ("any need
  to raise the Vulkan Backend's overall minimum supported API version"). Per
  that rule the fix was **not** decided unilaterally; a docs-only review
  produced
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)'s
  **accepted amendment (2026-08-13)**, which raises the instance's requested
  `apiVersion` to 1.3 only when the loader (`vkEnumerateInstanceVersion`)
  reports at least 1.3, leaving every device-level selection criterion, the
  Extension path, and every served device population unaffected, and rejects
  any "hard error when loader version is insufficient" clause. The §8 Core-path
  fix is authorized to resume via a new `fix/` branch created **from `main`
  only after that documentation PR has merged**, and is expected to need a
  follow-up revision of §8/§10/§15 (add the loader-version query and
  conditional `apiVersion` request; extend `decideDynamicRenderingPath()`'s
  signature and its truth table with `instanceRequestedApiVersionAtLeast1_3`;
  remove the shipped Core-path deviation; re-run the GPU tests and demo) — no
  RHI/RenderGraph/`src/renderer/` public-surface change, consistent with
  ADR-0024's Boundary statement. PR #28 is historical and immutable.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #149](https://github.com/slmao/Atlantis/pull/149) Batch 2. Original scope,
  ordered work, and verification retained. Candidate C++ headers/algorithms and
  the three-round revision history are preserved in this PR's and
  [PR #28](https://github.com/slmao/Atlantis/pull/28)/[PR #30](https://github.com/slmao/Atlantis/pull/30)'s
  history; the full blow-by-blow Post-Approval Deviation Record and the
  Consistency Review are preserved in this PR's and
  [PR #33](https://github.com/slmao/Atlantis/pull/33)'s history, not narrated
  here.

## Objective

Turn Spec 0007's approved contract — a real, visible, depth-tested mesh drawn
through `Renderer` → RenderGraph → RHI → Vulkan Backend — into an ordered,
reviewable implementation plan. This Plan's C++ signatures, algorithms, and
file layout (§2–§13) are approved; implementation follows them as written; per
[AGENTS.md](../AGENTS.md), a forced deviation is called out explicitly in the
PR.

## Architectural boundaries (preserved, not re-decided)

- No `Vk*` type or Vulkan header outside `src/vulkan_backend/` (ADR-0001).
- RHI's public interfaces are abstract C++ base classes held behind
  `std::unique_ptr`, constructed via `Device::create*()` factory methods for
  resources and via Vulkan Backend's free `createDevice()`/`createPresentation()`
  for `Device`/`Presentation` themselves (ADR-0014) — this Plan extends the
  established `Device::create*() -> Result<unique_ptr<Interface>, Error>`
  pattern (already used for `createCommandList()`) to `Buffer`, `Texture`,
  `Pipeline`.
- `src/renderer/` depends only on `Atlantis::RHI`, `Atlantis::RenderGraph`,
  `Atlantis::Core` — never Platform, Vulkan Backend, Win32, or any `Vk*` type
  (ADR-0001, ADR-0022).
- `Renderer` never owns a `RenderTarget`, depth `Texture`, `Mesh`, or
  `Material` across frames — every per-frame input is a borrowed reference
  (ADR-0003, ADR-0022).
- Single Phase 1 logical frame thread; nothing introduced here is thread-safe
  for concurrent access (ADR-0004).
- RenderGraph is the mandatory, sole path for recorded GPU work — no
  direct-submission bypass (AGENTS.md Golden Rule; ADR-0021). RenderGraph
  records but never submits or presents.
- Spec 0005's single-producer resource model, dependency derivation, cycle
  detection, and deterministic ordering (ADR-0017, ADR-0018) are **unchanged**
  — the depth attachment's combined read/write access is expressed as **exactly
  one** `writes()` usage, never a paired `reads()` + `writes()` on the same
  pass (ADR-0018's existing rule rejects that at declaration time).
- `ResourceState::ColorAttachmentWrite` (Spec 0006/ADR-0020) keeps its existing
  meaning and Vulkan Backend mapping (`VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`,
  per `resource_state_mapping.cpp`) exactly as shipped — this Plan never reuses
  it for the new draw pass's color output (ADR-0025, ADR-0026).
- The Vulkan Backend's overall minimum supported API version is **not** raised
  to 1.3 — dynamic rendering is adopted via a capability-detected dual path
  (ADR-0024; see the Post-Approval Deviation note for the accepted amendment's
  narrow, loader-gated exception).
- `Pipeline` fixes its target color/depth attachment *formats* at creation;
  attachment format change is the caller's explicit responsibility, never
  `Renderer`'s (ADR-0022, ADR-0025).
- `ATLANTIS_CHECK`/`ATLANTIS_ASSERT` for programmer errors, `Result<T,E>` for
  recoverable errors, no exceptions (ADR-0009, AGENTS.md).
- No general GPU memory suballocation strategy — each `Buffer`/`Texture` gets
  its own individual `vkAllocateMemory`/`vkFreeMemory` call, strictly confined
  to the Vulkan Backend's private implementation (ADR-0023).
- No shader compiler, reflection, or caching is invoked by any Atlantis build
  target or source file (ADR-0027).

## Non-Goals (matching Spec 0007)

Shader System, Runtime, Android, iOS, headless rendering, image regression
testing, scene graph/ECS/asset system/model loader, multiple materials,
lighting/shadows/texturing, GPU-driven rendering, bindless/instanced/indirect
draw, hot-reload, multiple frames in flight, multi-threading, a general GPU
memory suballocator, a second graphics backend, a general
descriptor-set/bindless system, and cross-owner shared ownership of any GPU
resource this Plan introduces. This Plan does not add a third-party dependency,
does not touch `docs/project-blueprint.md`/`specs/README.md`, and does not
reopen any `Accepted` ADR's conclusions.

## 1. Module and CMake target boundaries

**One new module** (`src/renderer/`, target `atlantis_renderer`, alias
`Atlantis::Renderer`), extending `atlantis_rhi`, `atlantis_vulkan_backend`,
`atlantis_render_graph`. No new third-party dependency; no new top-level CMake
option. `atlantis_renderer` links `Atlantis::Core`, `Atlantis::RHI`,
`Atlantis::RenderGraph` (PUBLIC) and `atlantis_compiler_warnings` (PRIVATE) —
**deliberately absent: `Atlantis::Platform`, `Atlantis::VulkanBackend`,
`Vulkan::Vulkan`** (linking any would itself be an Acceptance-Criteria
violation, not merely undesirable).

**Create:**

```
src/renderer/include/atlantis/renderer/  mesh.h (Mesh, createMesh())  material.h (Material, createMaterial())
                                         draw_item.h (DrawItem)  renderer.h (Renderer)
src/renderer/src/  mesh.cpp  material.cpp  renderer.cpp   src/renderer/CMakeLists.txt
src/rhi/include/atlantis/rhi/  buffer.h (Buffer, BufferPurpose, BufferCreateParams)
                              texture.h (Texture, TextureCreateParams)
                              pipeline.h (Pipeline, PipelineCreateParams, VertexInputLayout)
src/vulkan_backend/src/  vulkan_buffer.{h,cpp}  vulkan_texture.{h,cpp}  vulkan_pipeline.{h,cpp}
                         vulkan_memory.{h,cpp} (selectMemoryTypeIndex(), allocateAndBindMemory())
                         dynamic_rendering.{h,cpp} (decideDynamicRenderingPath() (pure) + the real
                         capability-query wrapper)
tests/rhi/buffer_texture_pipeline_tests.cpp        (GPU-independent: param/error-enum sanity)
tests/render_graph/attachment_execution_tests.cpp   (GPU-independent: multi-binding execute() extension)
tests/vulkan_backend/  dynamic_rendering_tests.cpp   (GPU-independent: decideDynamicRenderingPath() truth table)
                       vulkan_memory_tests.cpp        (GPU-independent: selectMemoryTypeIndex())
                       minimal_renderer_gpu_tests.cpp (GPU-required: real Buffer/Texture/Pipeline/draw path)
tests/renderer/renderer_ownership_tests.cpp  (GPU-independent: Renderer statelessness, Mesh/Material ownership)
tests/renderer/CMakeLists.txt
shaders/minimal_renderer/  minimal_mesh.vert.glsl  minimal_mesh.frag.glsl   (human-readable source, never built)
                           minimal_mesh.vert.spv  minimal_mesh.frag.spv     (pre-compiled bytecode, never built)
                           README.md  (exact compiler/version/command-line note, ADR-0027)
examples/minimal_renderer_demo/  CMakeLists.txt  main.cpp
```

**Modify:** `src/rhi/include/atlantis/rhi/types.h` (+
`ResourceState::ColorAttachmentOutput`/`DepthAttachmentReadWrite`, `DepthFormat`,
`VertexAttributeFormat`, `BufferCreateError`, `TextureCreateError`,
`PipelineCreateError`), `device.h` (+ `createBuffer()`, `createTexture()`,
`createPipeline()`), `command_list.h` (+ `bindPipeline`/`bindVertexBuffer`/
`bindIndexBuffer`/`bindUniformBuffer`/`pushConstant`/`drawIndexed`/
`beginRendering`/`endRendering`), `src/rhi/CMakeLists.txt`;
`src/vulkan_backend/include/atlantis/vulkan_backend/vulkan_backend.h` (+
`DeviceCreateError::DynamicRenderingUnavailable`),
`src/vulkan_backend/src/vulkan_instance.cpp` (apiVersion request unchanged; +
query/enable `VK_KHR_get_physical_device_properties2`, + resolve
`vkGetPhysicalDeviceFeatures2KHR` via `vkGetInstanceProcAddr` once instance
creation succeeds — §8), `vulkan_device.{h,cpp}` (dynamic-rendering capability
selection in the physical-device loop, feature-chain enablement,
`createBuffer`/`createTexture`/`createPipeline`, resolved entry-point storage),
`vulkan_command_list.{h,cpp}` (bind/push-constant/draw/begin-end-rendering
bodies), `resource_state_mapping.{h,cpp}` (+ `ColorAttachmentOutput`/
`DepthAttachmentReadWrite` transition rows), `vulkan_result.{h,cpp}` (+
`toBufferCreateError`/`toTextureCreateError`/`toPipelineCreateError`),
`src/vulkan_backend/CMakeLists.txt` (+ 5 `.cpp`);
`src/render_graph/include/atlantis/render_graph/execution.h` (`ResourceBinding`
gains `depthTexture`/`colorClear`/`depthClear`), `src/render_graph/src/execution.cpp`
(multi-binding transition bookkeeping, draw-pass recognition,
begin/end-rendering insertion); `tests/rhi/CMakeLists.txt`,
`tests/rhi/types_tests.cpp`, `tests/render_graph/CMakeLists.txt`,
`tests/vulkan_backend/CMakeLists.txt` (+ 4 sources across the two existing
targets); root `CMakeLists.txt` (+ `add_subdirectory(src/renderer)`;
`tests/renderer` under `ATLANTIS_BUILD_TESTS`; `examples/minimal_renderer_demo`
under `ATLANTIS_BUILD_EXAMPLES`).

No file under `src/shader_system/`, `src/runtime/`, or any Android/iOS path is
created or modified. No dependency-fetching CMake file is changed beyond the
new subdirectories.

## 2. RHI candidate API — type additions

Added to `types.h`:

- `ResourceState` gains `ColorAttachmentOutput` (real graphics-pipeline
  color-attachment-output write) and `DepthAttachmentReadWrite` (depth-test
  read + depth-write, a single `writes()` usage per ADR-0026) — both distinct
  in name and Vulkan Backend mapping from `ColorAttachmentWrite` (Spec 0006),
  which is never reused here (reusing it would be a genuine layout-correctness
  bug, per ADR-0025, confirmed against `resource_state_mapping.cpp`).
- `enum class DepthFormat { D32Sfloat }` — this round's one depth format
  (`VK_FORMAT_D32_SFLOAT`, guaranteed optimal-tiling depth-attachment support,
  no capability query needed). A single-variant enum, not a bare bool, so a
  future spec extends rather than replaces it.
- `enum class VertexAttributeFormat { Float3 }` — this round's position and
  color attributes are both 3-float vectors; a future attribute type extends
  this.
- `enum class BufferPurpose { Vertex, Index, Uniform }`;
  `struct BufferCreateParams { BufferPurpose purpose; std::size_t sizeBytes; }`.
- `struct TextureCreateParams { Extent2D extent; DepthFormat format = D32Sfloat; }`
  — this round's only `Texture` usage is a depth attachment.
- `struct VertexAttribute { std::uint32_t location, offsetBytes;
  VertexAttributeFormat format = Float3; }`;
  `struct VertexInputLayout { std::uint32_t strideBytes;
  std::vector<VertexAttribute> attributes; }` (this round: exactly 2 — position
  @0, color @1).
- `struct ShaderStageBytecode { const std::uint32_t* spirvWords; std::size_t
  wordCount; }` — RHI does not parse, validate, or reflect it (ADR-0027); a
  non-owning view; the caller (Material construction) owns the byte storage.
- `struct PipelineCreateParams { ShaderStageBytecode vertexShader,
  fragmentShader; VertexInputLayout vertexInputLayout; Format colorFormat =
  Unknown; DepthFormat depthFormat = D32Sfloat; std::size_t
  pushConstantSizeBytes; }` (this round: `pushConstantSizeBytes = sizeof(float)
  * 16` — one 4×4 matrix; `colorFormat` matches the bound `RenderTarget`'s
  format at `Material` construction time).
- `enum class BufferCreateError { AllocationFailed, BufferCreationFailed }`;
  `enum class TextureCreateError { AllocationFailed, ImageCreationFailed,
  ImageViewCreationFailed }`;
  `enum class PipelineCreateError { ShaderModuleCreationFailed,
  DescriptorSetLayoutCreationFailed, DescriptorSetAllocationFailed,
  PipelineLayoutCreationFailed, PipelineCreationFailed }` — three distinct
  `*CreateError` enums, not one shared `ResourceCreateError`, mirroring this
  codebase's precedent (`CommandListCreateError` distinct from `SubmitError`).

## 3. RHI candidate API — `Buffer`, `Texture`, `Pipeline`

Abstract base classes, move-only, single-owner, held behind `std::unique_ptr<Interface>`
(ADR-0014's mechanism, ADR-0023); not internally thread-safe (ADR-0004); no
hidden cache (`Device` does not deduplicate or retain a reference to any it
creates — ADR-0003).

- **`Buffer`** (`buffer.h`) — a GPU-visible linear memory region fixed to one
  of three purposes at creation. This round every `Buffer` is host-visible and
  host-coherent regardless of purpose (no staging/upload path — ADR-0023).
  `purpose()`, `sizeBytes()`, `void* mappedData()` — a pointer to host-visible/
  host-coherent memory valid for the whole lifetime (mapped once at
  construction, never remapped); the caller may write directly at any time, no
  explicit flush/invalidate. Writing to a Uniform `Buffer` while prior-frame
  GPU work might still read it is a caller precondition violation — §7's
  write-timing contract, satisfied structurally by single-frame-in-flight for
  this Spec's one caller pattern (write once per frame, immediately after
  `acquireNextTarget()` returns).
- **`Texture`** (`texture.h`) — a GPU image used this round exclusively as a
  depth attachment (no sampled/shader-read usage, no mipmaps). Owned and
  resize-recreated by the caller, never by `Renderer` or `Presentation`
  (ADR-0022). `extent()`, `format()`.
- **`Pipeline`** (`pipeline.h`) — a fixed graphics pipeline: one vertex + one
  fragment stage, fixed vertex-input layout, depth-test/depth-write enabled,
  opaque rasterization, dynamic viewport/scissor (survives a resize without
  recreation — ADR-0025), color/depth attachment formats fixed at creation via
  dynamic-rendering pipeline info, never a `VkRenderPass` (ADR-0024). Owned
  exclusively by one `Material`. If the bound color/depth format changes the
  *caller* destroys and recreates it — `Pipeline` has no "update format"
  method. Opaque: no accessor beyond the destructor.

## 4. RHI candidate API — `Device` extensions

`device.h` gains, each a stateless factory call (`Device` retains no reference
to what it creates — ADR-0003, ADR-0023):

- `createBuffer(const BufferCreateParams&) -> Result<std::unique_ptr<Buffer>, BufferCreateError>`
- `createTexture(const TextureCreateParams&) -> Result<std::unique_ptr<Texture>, TextureCreateError>`
- `createPipeline(const PipelineCreateParams&) -> Result<std::unique_ptr<Pipeline>, PipelineCreateError>`

`vulkan_backend.h` gains one new `DeviceCreateError` variant (existing four
unchanged): `DynamicRenderingUnavailable` (§8).

## 5. RHI candidate API — `CommandList` extensions

`command_list.h` gains (existing `transitionResource()`/`clearColor()`
unchanged):

- `beginRendering(RenderTarget& color, Texture* depth, ClearColorValue
  colorClear, float depthClear)` / `endRendering()` — attachment scoping
  (ADR-0024/ADR-0025). `depth` may be `nullptr` (this round's draw pass always
  supplies one, but the type does not forbid omitting it). Called only by
  `render_graph::execute()` (§7), never by a pass callback (ADR-0026).
- `bindPipeline(Pipeline&)` — until the next `bindPipeline()` or the end of the
  current attachment scope.
- `bindVertexBuffer(Buffer&)` / `bindIndexBuffer(Buffer&)` — `buffer` must have
  been created with `BufferPurpose::Vertex`/`Index` respectively; a mismatched
  purpose is a programmer error (`ATLANTIS_CHECK`).
- `bindUniformBuffer(Buffer&)` — `buffer` must be `BufferPurpose::Uniform`.
- `pushConstant(const void* data, std::size_t sizeBytes)` — records the
  per-draw-item object-to-world transform as a Vulkan push constant (ADR-0025 —
  a shared uniform buffer overwritten once per draw item during recording would
  corrupt every earlier item's transform by GPU-execution time). `sizeBytes`
  must not exceed the bound `Pipeline`'s `pushConstantSizeBytes`.
- `drawIndexed(std::uint32_t indexCount)`.

`command_list.h` gains `#include <atlantis/rhi/buffer.h>` and
`#include <atlantis/rhi/pipeline.h>` (`texture.h` is reachable transitively via
`types.h`'s `DepthFormat`, included directly for clarity). All remain subject
to ADR-0020's rule: recording only from inside a RenderGraph pass execution
callback (for the bind/push/draw calls) or from `render_graph::execute()`
itself (for `beginRendering`/`endRendering`, ADR-0026) — enforced by
inspection.

## 6. RenderGraph candidate API — `ResourceBinding` extension

`execution.h`'s `ResourceBinding` (Spec 0006) is extended additively — the
existing `resource`/`target` fields keep their exact name and meaning, so
`examples/frame_execution_demo/main.cpp`'s existing call site needs no change:
`{ CompiledResourceId resource; RenderTarget* target = nullptr; ClearColorValue
colorClear{}; Texture* depthTexture = nullptr; float depthClear = 1.0f; }`
(`colorClear` used only when `target != nullptr`; `depthClear` only when
`depthTexture != nullptr`). `execution.h` gains
`#include <atlantis/rhi/texture.h>`.

**Exactly one of `target`/`depthTexture` must be non-null per entry** — a new
**Guard 0** (`ATLANTIS_CHECK_MSG`'d at the top of `execute()`, before Guards
1/2). **`bindings` must also contain no two entries for the same
`CompiledResourceId`** — folded into Guard 0 (not a separate ADR-0026 guard —
Spec 0007/ADR-0026 never contemplated duplicate bindings; a duplicate would
make "which entry does the per-usage lookup find" implementation-order-
dependent).

**Why vertex/index/uniform `Buffer`s are never declared as RenderGraph logical
resources — deliberately, not by oversight.** `Renderer::drawFrame()` (§11)
captures `Mesh`/`Material`/the camera `Buffer` directly in its pass's
execution-callback closure and calls `bindVertexBuffer()`/`bindIndexBuffer()`/
`bindUniformBuffer()` from inside that callback — none is ever
`declareResource()`'d, `reads()`/`writes()`-tagged, or bound via
`ResourceBinding`. This is not a bypass of "all GPU work goes through
RenderGraph": the *drawing itself* still only ever happens from inside a
RenderGraph pass execution callback invoked by `execute()` at the correct
scheduling point (§7 step 4), exactly like `clearColor()` in Spec 0006. What
these three `Buffer`s never need is **`ResourceState`/transition tracking** —
and that omission is deliberate: every one is written on the CPU side only
(never by a GPU command this round) and read on the GPU side only, with the
write always happening-before the GPU read by construction (vertex/index data
written once at `Mesh` construction before any use; the camera uniform `Buffer`
written once per frame by the caller strictly before `Renderer::drawFrame()` is
called that frame — §13 step 3 — relying on the same acquire-time-drain
guarantee ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
PR #24) already established for this write-timing pattern). No resource-state
transition or barrier is required for a purely host-written, device-read-only,
always-already-visible-by-submission-time resource. **A narrow, spec-scoped
conclusion, not a general rule:** a future spec with a GPU command *writing* a
`Buffer` (compute shader, staging-buffer upload copy) would need real
RenderGraph declaration and `ResourceState` tracking.

## 7. RenderGraph candidate API — `execute()` algorithm extension

Guards (generalized/unchanged per ADR-0026):

- **Guard 0 (new):** every `ResourceBinding` entry has exactly one of
  `target`/`depthTexture` non-null; no two entries share a `CompiledResourceId`.
- **Guard 1 (unchanged in principle, extended in scope):** every resource where
  `graph.requiresRhiBinding(r)` has a matching `bindings` entry, regardless of
  whether it is a `target` or `depthTexture` binding.
- **Guard 2 (unchanged in scope):** for every `target != nullptr` entry, no
  declared read usage anywhere in `graph` for that resource. **Not checked for
  `depthTexture` entries** — depth-test read is legitimate, expressed as part
  of the single `DepthAttachmentReadWrite` write usage (ADR-0026).

**Draw-pass recognition (new):** a pass is a **draw pass** iff any declared
usage carries `ColorAttachmentOutput` or `DepthAttachmentReadWrite` — **and
only these two states**, never `ColorAttachmentWrite` (ADR-0026 — this keeps
Spec 0006's `clearColor()`-only pass structurally un-wrapped).

**`execute()` algorithm** (`execution.cpp`): (0) Guard 0 checks; (1) Guard 1;
(2) Guard 2 (`target` entries only); (3) `currentState`: a
`map<CompiledResourceId, ResourceState>`, empty initially — every bound
resource, color **and** depth, treated as entering this `execute()` from
`Undefined` (valid because this round's one draw pass unconditionally clears
both attachments via `beginRendering()`'s load-op, per ADR-0026's extension of
ADR-0019's existing `RenderTarget`-specific rule); (4) for each pass in
compiled order: compute `isDrawPass` from its attachment usages; for each usage
with a state, look up the binding (UB-safe check-then-skip under a
non-terminating handler, mirroring Plan 0006), pick
`binding.target ? *binding.target : *binding.depthTexture`, and if `previous !=
*usage.state` record `transitionResource(resourceObj, previous, *usage.state)`
(the method widens from `RenderTarget&` to a resource reference — see the
signature note below) and update `currentState`; then if `isDrawPass` and every
required binding was found, `commandList.beginRendering(*colorBinding.target,
depthUsagePresent ? depthBinding.depthTexture : nullptr, colorBinding.colorClear,
depthUsagePresent ? depthBinding.depthClear : 1.0f)`, invoke the pass's
`executeFn` if any, `commandList.endRendering()` — else (non-draw pass, or a
missing binding Guard 1 already reported) invoke the `executeFn` if any without
an attachment scope; (5) for each `target != nullptr` entry actually touched,
if its last state `!= PresentSource` record a trailing transition to
`PresentSource` — **no trailing transition for `depthTexture` entries**
(ADR-0026 — never presented, never read back this round).

`beginRendering()`'s `renderArea` is derived from
`colorBinding.target->extent()`; the depth `Texture`'s own `extent()` must
already match — guaranteed by the caller's resize contract (§13), never
validated by `beginRendering()` itself.

**Note on `transitionResource()`'s signature:** Spec 0006 declared it
`transitionResource(RenderTarget&, ResourceState, ResourceState)`. Supporting a
`Texture&` argument uses an **overload**
(`transitionResource(Texture&, ResourceState, ResourceState)`), not a widened
parameter — explicitly anticipated as a low-cost future change by ADR-0020's
own Trade-offs, and requiring no change to `RenderTarget`'s existing method
(Spec 0006's `Accepted` surface untouched).

## 8. Vulkan Backend implementation — dynamic rendering capability detection

Per ADR-0024's dual-path decision. New file `dynamic_rendering.{h,cpp}`:
`enum class DynamicRenderingPath { Core, Extension, Unavailable }` and a **pure**
decision function (GPU-independent, unit-testable with literal booleans):

```
decideDynamicRenderingPath(
  bool physicalDeviceProperties2InstanceExtensionAvailable,  // VK_KHR_get_physical_device_properties2
                                                              // enabled at instance creation (and its
                                                              // entry point resolved) — one instance-wide
                                                              // fact; if false, returns Unavailable
                                                              // unconditionally, ignoring the rest
  bool apiVersionAtLeast1_3,       // physical device's reported apiVersion >= 1.3
  bool coreFeatureSupported,        // VkPhysicalDeviceVulkan13Features::dynamicRendering
  bool extensionAdvertised,         // VK_KHR_dynamic_rendering in vkEnumerateDeviceExtensionProperties
  bool extensionFeatureSupported)   // VkPhysicalDeviceDynamicRenderingFeaturesKHR::dynamicRendering
  -> DynamicRenderingPath

  if !physicalDeviceProperties2InstanceExtensionAvailable: return Unavailable
  if apiVersionAtLeast1_3 && coreFeatureSupported:         return Core
  if extensionAdvertised  && extensionFeatureSupported:    return Extension
  return Unavailable
```

**Two distinct layers, not conflated.** (1) Whether
`VK_KHR_get_physical_device_properties2` is enabled at the *instance* level is a
**query-mechanism** concern — it determines whether
`vkGetPhysicalDeviceFeatures2KHR` may be called at all. (2) Whether a given
physical device supports the core-1.3 feature or the `VK_KHR_dynamic_rendering`
**device** extension is the actual **capability**. The function's first
argument is layer (1); the remaining four are layer (2), kept as separate
independently-meaningful booleans so *why* a candidate failed stays visible.

**Instance-level prerequisite — resolved once, before any physical-device
query, without raising `VkApplicationInfo::apiVersion`.** `vulkan_instance.cpp`
requests `VK_API_VERSION_1_0` and this Plan does not change that request (the
core `vkGetPhysicalDeviceFeatures2` is only unambiguously valid from a 1.1+
instance; raising the request risks the ambiguity Human Review's decision was
written to avoid plus a narrow `VK_ERROR_INCOMPATIBLE_DRIVER` risk on a very
old 1.0-only loader). Instead: before `vkCreateInstance()`, call
`vkEnumerateInstanceExtensionProperties()` and check for
`VK_KHR_get_physical_device_properties2` (`physicalDeviceProperties2InstanceExtensionAvailable`,
computed exactly once); if available, add it to
`VkInstanceCreateInfo::ppEnabledExtensionNames` alongside the existing (WSI
surface) extensions — `vkCreateInstance()` itself is unaffected either way;
immediately after it succeeds (if the extension was enabled) resolve
`vkGetPhysicalDeviceFeatures2KHR` explicitly via `vkGetInstanceProcAddr`,
stored once — **never assumed directly linkable, never called if resolution
was skipped or returned `nullptr`** (the same "resolve a `KHR` entry point via
`vkGet*ProcAddr` rather than assume static linkage" discipline applied to the
device-level `vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR` resolution below).
That boolean (narrowed to also require the function-pointer resolution to have
succeeded) is passed unchanged to every physical-device candidate. If
unavailable, `decideDynamicRenderingPath()` returns `Unavailable` for every
candidate without any `vkGetPhysicalDeviceFeatures2KHR` call — folding into the
same `DeviceCreateError::DynamicRenderingUnavailable` outcome, because from the
caller's perspective it means the same actionable thing.

**Real capability query wrapper** (in `vulkan_device.cpp`'s existing
physical-device selection loop, extended): for each candidate already passing
the pre-existing suitability criteria (queue families, etc., unchanged from
Spec 0003), additionally: `vkGetPhysicalDeviceProperties()` →
`apiVersionAtLeast1_3` (physical-device version, independent of the instance's
requested version); if `physicalDeviceProperties2InstanceExtensionAvailable`
**and** `apiVersionAtLeast1_3`, call the resolved
`vkGetPhysicalDeviceFeatures2KHR` with a `VkPhysicalDeviceVulkan13Features`
chained into `pNext` → `coreFeatureSupported` (else `false`, call skipped);
`vkEnumerateDeviceExtensionProperties()` → `extensionAdvertised` (core 1.0
functionality, no instance extension needed); if
`physicalDeviceProperties2InstanceExtensionAvailable` **and**
`extensionAdvertised`, call the same resolved function with a
`VkPhysicalDeviceDynamicRenderingFeaturesKHR` → `extensionFeatureSupported`
(else skipped); call `decideDynamicRenderingPath()` with all five; on
`Core`/`Extension` accept the candidate and record the resolved path on the
`VulkanDevice`; on `Unavailable` remember `foundSuitableExceptDynamicRendering`
and continue. After the loop: if no candidate accepted,
`foundSuitableExceptDynamicRendering` → `Err(DynamicRenderingUnavailable)`;
else → `Err(NoSuitablePhysicalDevice)` (unchanged from Spec 0003). **No
`VkRenderPass`/`VkFramebuffer` fallback on either error path** (ADR-0024).

Once a candidate is accepted: `Core` → chain
`VkPhysicalDeviceVulkan13Features{.dynamicRendering = VK_TRUE}` into
`VkDeviceCreateInfo::pNext`; resolved entry points
`vkCmdBeginRendering`/`vkCmdEndRendering` (core, linked directly). `Extension`
→ add `VK_KHR_dynamic_rendering` to the enabled device extensions; chain
`VkPhysicalDeviceDynamicRenderingFeaturesKHR{.dynamicRendering = VK_TRUE}`;
resolved entry points `vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR` via
`vkGetDeviceProcAddr` once at construction, stored as member function pointers.
`VulkanDevice` gains a small internal function-pointer struct (used by
`VulkanCommandList::beginRendering()`/`endRendering()`, §10) — **never exposed
on `VulkanDevice`'s public accessor surface**.

**Windows test coverage / future Android boundary:** this Plan's GPU tests
(§15) exercise whichever single path the test machine's GPU/driver reports (§15
states this limitation explicitly). The wrapper makes no Windows-specific
Vulkan call (`vkGetPhysicalDeviceFeatures2KHR`,
`vkEnumerateDeviceExtensionProperties`, `vkEnumerateInstanceExtensionProperties`,
and the two feature-chain structs are all core/WSI-independent), so it is
expected to need no changes for a future Android Platform/Vulkan Backend spec
to reuse — that future spec still owns its own decision about Android's device/
driver support distribution (ADR-0024's Boundary note).

## 9. Vulkan Backend implementation — GPU memory allocation

New file `vulkan_memory.{h,cpp}`, shared by `VulkanBuffer`/`VulkanTexture`
construction (ADR-0023's direct, unpooled, per-resource policy). A **pure**
decision function (unit-testable against a synthetic
`VkPhysicalDeviceMemoryProperties`, no real device):
`selectMemoryTypeIndex(const VkPhysicalDeviceMemoryProperties&, std::uint32_t
typeFilterBits, VkMemoryPropertyFlags requiredProperties) ->
std::optional<std::uint32_t>` — the standard idiom (scan `memoryTypes` for the
first entry whose bit is set in `typeFilterBits` and whose `propertyFlags`
fully contain `requiredProperties`); empty optional → the real caller maps to
`BufferCreateError::AllocationFailed`/`TextureCreateError::AllocationFailed`.

**Allocation sequence** (identical shape for `VulkanBuffer`/`VulkanTexture`,
each its own independent call): `vkCreateBuffer`/`vkCreateImage` (check) →
`vkGetBufferMemoryRequirements`/`vkGetImageMemoryRequirements` →
`selectMemoryTypeIndex` with `HOST_VISIBLE_BIT | HOST_COHERENT_BIT` (Buffer,
all 3 purposes this round) or `DEVICE_LOCAL_BIT` (Texture depth — no host
access needed or provided) → `vkAllocateMemory` (one individual allocation,
this resource's own `VkDeviceMemory`, no suballocation; check) →
`vkBindBufferMemory`/`vkBindImageMemory` (check) → Buffer only: `vkMapMemory`
once, for the whole `VkDeviceMemory` lifetime (host-coherent needs no explicit
flush, so a persistent map costs nothing and avoids repeated map/unmap) →
Texture only: `vkCreateImageView` for the `VK_IMAGE_ASPECT_DEPTH_BIT` view
(check).

**`HOST_COHERENT` is a required, not preferred, property** — both bits are
*required* in `requiredProperties` for every `Buffer` purpose. This
structurally excludes a host-visible-but-non-coherent type (no
`vkFlushMappedMemoryRanges`/`vkInvalidateMappedMemoryRanges` call exists
anywhere in this Plan): either a coherent type is found or none is and
`Err(AllocationFailed)` is returned. **No manual alignment arithmetic** — each
allocation backs one resource at offset 0, sized to `requirements.size` (which
already accounts for `requirements.alignment` per the Vulkan spec's
single-whole-allocation-bind guarantee); alignment math only matters when
suballocating, which this policy never does.

**Partial-construction-failure safety** — mirrors this codebase's two-phase-
construction-guard pattern (`vulkan_device.cpp`'s `createDevice()`,
`vulkan_presentation.cpp`'s `SwapchainGuard`): if a later step fails, every
already-created handle is destroyed/freed before returning `Err`; only once
every step succeeds does the constructor take final unconditional ownership.
**Destruction** (RAII, the same guard pattern, now unconditional):
`vkDestroyImageView` (Texture only) → `vkFreeMemory` (this resource's own
allocation; a mapped Buffer needs no explicit unmap — `vkFreeMemory` implicitly
unmaps) → `vkDestroyBuffer`/`vkDestroyImage`.

**Concrete resource-count bound, per ADR-0023:** at most one vertex, one index,
one uniform `Buffer` and one depth `Texture` per `Mesh`/`Material`/
verification-composition-instance, plus one additional depth `Texture`
allocation per interactive resize (old destroyed, new created). Nowhere near a
typical driver's `maxMemoryAllocationCount` floor (4096+). No pooling, no VMA,
no staging buffer, no implicit cache — **not a general allocator decision**;
the migration trigger is unchanged from ADR-0023's stated boundary.

## 10. Vulkan Backend implementation — `VulkanBuffer`/`VulkanTexture`/`VulkanPipeline`/`VulkanCommandList` draw path

- `VulkanBuffer final : public rhi::Buffer` — holds `VkBuffer`, `VkDeviceMemory`,
  the persistently-mapped `void*`, `BufferPurpose`, `sizeBytes`. Constructed
  only via `VulkanDevice::createBuffer()`.
- `VulkanTexture final : public rhi::Texture` — holds `VkImage`,
  `VkDeviceMemory`, `VkImageView` (depth aspect), `Extent2D`, `DepthFormat`.
  Constructed only via `VulkanDevice::createTexture()`.

### Camera uniform binding — full candidate design (not left to Implementation)

`CommandList::bindUniformBuffer(Buffer&)` (RHI-public, §5) is the only new
RHI-visible surface for the camera uniform binding — no `UniformBinding`-shaped
abstraction, no general descriptor-set API. Everything below is
Vulkan-Backend-private, never referenced outside `vulkan_pipeline.{h,cpp}` and
`vulkan_command_list.cpp`, never in any RHI or `src/renderer/` header — this is
what keeps it a fixed single-purpose mechanism (ADR-0025's boundary):

- **`VkDescriptorSetLayout`** — exactly one binding: `binding = 0`,
  `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, `descriptorCount = 1`,
  `VK_SHADER_STAGE_VERTEX_BIT`. Created once per `VulkanPipeline` (identical for
  every `Pipeline` this round), destroyed in `VulkanPipeline`'s destructor
  after its set is freed.
- **`VkDescriptorPool`** — owned by `VulkanDevice` (a Device-level singleton,
  created once at construction, mirroring the `VkCommandPool` precedent),
  created with `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` (**required**
  — without it `vkFreeDescriptorSets` is invalid usage). **Fixed capacity tied
  to this round's concurrent-resource ceiling:** `maxSets = 4`, one pool-size
  entry `{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}`. Derived from, not merely
  larger than, the worst case: exactly one `Material` in steady state, plus the
  old `Material`'s `Pipeline` (and its one set) momentarily alive during a
  §13 create-before-destroy format-change rebuild — true transient peak **2**;
  `maxSets = 4` is exactly double, a deliberate margin for the GPU suite's
  possible construction/destruction interleavings (§15), not a number chosen
  "to be safe" with no accounting. Repeated `Material` replacement across many
  format changes never exhausts the pool: every `VulkanPipeline` destructor
  unconditionally `vkFreeDescriptorSets` its own one set, so available capacity
  is fully restored the moment each superseded `Pipeline` is destroyed.
  `vkAllocateDescriptorSets` returning `VK_ERROR_OUT_OF_POOL_MEMORY`/
  `VK_ERROR_FRAGMENTED_POOL` maps to
  `PipelineCreateError::DescriptorSetAllocationFailed` (a distinct enumerator,
  not folded into `PipelineCreationFailed`) — not expected reachable, checked
  not assumed.
- **`VkDescriptorSet`** — one per `VulkanPipeline`, allocated via
  `vkAllocateDescriptorSets` (checked) from `VulkanDevice`'s pool at
  `VulkanPipeline` construction, freed via `vkFreeDescriptorSets` (also checked,
  though documented to only return `VK_SUCCESS`) at `VulkanPipeline`
  destruction — **before** `VulkanDevice`'s pool may be destroyed (the same
  "backed-resource destroyed before its owning pool" precondition tier as
  `VulkanCommandList`/`VulkanDevice`'s command pool, Plan 0006 §9; no new
  enforcement mechanism). `vkUpdateDescriptorSets` returns `void` — no
  `VkResult` to check.
- **`bindUniformBuffer(Buffer&)`'s implementation:** `ATLANTIS_CHECK(buffer.purpose()
  == BufferPurpose::Uniform)`, then unconditionally (no "skip if unchanged"
  caching — no cache, no cache-invalidation bug) `vkUpdateDescriptorSets()`
  with `VkDescriptorBufferInfo{buffer.vkBuffer(), 0, VK_WHOLE_SIZE}` targeting
  the currently-bound `Pipeline`'s set, then `vkCmdBindDescriptorSets(...,
  VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &descriptorSet_, 0,
  nullptr)`. Write-timing safety: `vkUpdateDescriptorSets` is an immediate
  host-side call (not a recorded command) — safe here under the same
  single-frame-in-flight reasoning as writing the camera `Buffer`'s own mapped
  memory (§13 step 3): by the time `bindUniformBuffer()` runs (inside a pass
  callback, itself only invoked after `acquireNextTarget()`'s own drain), no
  prior-frame GPU work could still be reading the set.
- **Push-constant range coexistence:** `VkPipelineLayout` = one
  `VkDescriptorSetLayout` + one push-constant range (`VK_SHADER_STAGE_VERTEX_BIT`,
  offset 0, `pushConstantSizeBytes`). Every `Pipeline` this round uses the same
  byte-identical layout shape, so switching `bindPipeline()` between materials
  mid-loop never invalidates a bound set or pushed range; `drawFrame()` (§11)
  re-binds/re-pushes every draw item regardless, for clarity.
- **Camera `Buffer` lifetime vs. the submission that reads it:** the caller
  must not destroy it while any submitted GPU work that read it is incomplete —
  the same lifetime precondition tier as every other resource
  `Device::submit()` touches (§14). In this Plan's own scope the camera `Buffer`
  is created once and lives for the whole session/test case, destroyed only
  after the same `Device::waitIdle()` every exit path requires — the
  precondition is real but not exercised at its edge.
- **Deliberately not generalized beyond single-frame-in-flight** (ADR-0020):
  one `VkDescriptorSet` per `Pipeline`, updated in place every bind call, is
  correct only because at most one frame's GPU work is ever outstanding. A
  future multi-frame-in-flight spec would need to revisit this (e.g. one set
  per frame-in-flight slot) — this Plan does not anticipate that shape.

`VulkanPipeline final : public rhi::Pipeline` — holds `VkPipeline`,
`VkPipelineLayout`, the `VkDescriptorSetLayout`/`VkDescriptorSet` above, and the
two `VkShaderModule` handles (destroyed immediately after
`vkCreateGraphicsPipelines()` succeeds — a `VkShaderModule` is not needed
after). Constructed only via `VulkanDevice::createPipeline()`, using
`VkPipelineRenderingCreateInfo` (naming `colorFormat`/`depthFormat` directly,
per ADR-0024) instead of a `VkRenderPass`, and
`VK_DYNAMIC_STATE_VIEWPORT`/`VK_DYNAMIC_STATE_SCISSOR` (ADR-0025). Destruction
order (two-phase-construction-guard, per §9): `vkDestroyPipeline` →
`vkFreeDescriptorSets` → `vkDestroyDescriptorSetLayout` →
`vkDestroyPipelineLayout`. A `Material` owning a `Pipeline` that outlives its
`Device` is the same lifetime precondition tier as every other Device-backed
resource (§14).

`resource_state_mapping.{h,cpp}` gains two new rows (existing three, Spec 0006,
unchanged); `planTransition()`'s `ATLANTIS_CHECK_MSG(false, ...)` fallback for
an undefined pair is unchanged:

| before | after | oldLayout | newLayout | srcAccess | dstAccess | srcStage | dstStage |
|---|---|---|---|---|---|---|---|
| `Undefined` | `ColorAttachmentOutput` | `UNDEFINED` | `COLOR_ATTACHMENT_OPTIMAL` | `0` | `COLOR_ATTACHMENT_WRITE_BIT` | `TOP_OF_PIPE` | `COLOR_ATTACHMENT_OUTPUT_BIT` |
| `ColorAttachmentOutput` | `PresentSource` | `COLOR_ATTACHMENT_OPTIMAL` | `PRESENT_SRC_KHR` | `COLOR_ATTACHMENT_WRITE_BIT` | `0` | `COLOR_ATTACHMENT_OUTPUT_BIT` | `BOTTOM_OF_PIPE` |
| `Undefined` | `DepthAttachmentReadWrite` | `UNDEFINED` | `DEPTH_ATTACHMENT_OPTIMAL` | `0` | `DEPTH_STENCIL_ATTACHMENT_READ_BIT \| DEPTH_STENCIL_ATTACHMENT_WRITE_BIT` | `TOP_OF_PIPE` | `EARLY_FRAGMENT_TESTS_BIT \| LATE_FRAGMENT_TESTS_BIT` |

`VulkanCommandList` gains (all `static_cast<VulkanX&>` the abstract reference —
safe, only Vulkan Backend constructs one in Phase 1, matching Spec 0006's
precedent): `bindPipeline` → `vkCmdBindPipeline(GRAPHICS)`; `bindVertexBuffer` →
`ATLANTIS_CHECK` purpose + `vkCmdBindVertexBuffers(0, 1, &vkBuffer, &offset0)`;
`bindIndexBuffer` → `ATLANTIS_CHECK` purpose + `vkCmdBindIndexBuffer(vkBuffer, 0,
VK_INDEX_TYPE_UINT16)` (`UINT16` sufficient for this round's small vertex
counts); `bindUniformBuffer` → the descriptor design above; `pushConstant` →
`vkCmdPushConstants(pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeBytes,
data)`; `drawIndexed` → `vkCmdDrawIndexed(indexCount, 1, 0, 0, 0)`;
`beginRendering` → build a `VkRenderingAttachmentInfo` for the color target
(`loadOp=CLEAR`, `storeOp=STORE`, `imageLayout=COLOR_ATTACHMENT_OPTIMAL`) and,
if `depth != nullptr`, a second for it
(`loadOp=CLEAR`, `storeOp=STORE`, `imageLayout=DEPTH_ATTACHMENT_OPTIMAL`), then
call whichever of `vkCmdBeginRendering`/`vkCmdBeginRenderingKHR` `VulkanDevice`
resolved (§8) with a `VkRenderingInfo` naming `renderArea` from the color
target's `extent()`; `endRendering` → the matching
`vkCmdEndRendering`/`vkCmdEndRenderingKHR`. `beginRendering()` is only ever
called after `transitionResource()` has already brought both attachments to
their declared states (matching `clearColor()`'s existing precondition tier).

## 11. Renderer module — `Mesh`, `Material`, `DrawItem`, `Renderer`

- **`Mesh`** (`mesh.h`) — owns exactly one vertex `Buffer` and one index
  `Buffer` (ADR-0022), plus an index count. Move-only, single-owner. Not
  internally thread-safe. Constructed once; never re-uploaded or mutated.
  Accessors `vertexBuffer()`, `indexBuffer()`, `indexCount()`. A convenience
  **free function** (NOT a `Renderer` method) `createMesh(rhi::Device&,
  rhi::VertexInputLayout, const void* vertexData, std::size_t
  vertexDataSizeBytes, const std::uint16_t* indices, std::uint32_t indexCount)
  -> Result<Mesh, CreateMeshError{ VertexBufferCreationFailed,
  IndexBufferCreationFailed }>` — creates both `Buffer`s via `device`, copies
  the data into their mapped memory once, returns an independently-owned `Mesh`.
  Each call produces a new, independent `Mesh` — no cache, no deduplication.
- **`Material`** (`material.h`) — same shape; owns exactly one `Pipeline`;
  `createMaterial()` takes `PipelineCreateParams` and returns `Result<Material,
  CreateMaterialError>`, mirroring `Mesh`/`createMesh()`.
- **`DrawItem`** (`draw_item.h`) — a plain caller-owned aggregate, not a scene
  graph, not registered anywhere persistent:
  `{ const Mesh* mesh; const Material* material; std::array<float, 16>
  objectToWorld; }`. `mesh`/`material` are borrowed (must outlive the
  `drawFrame()` call). `objectToWorld` is a column-major 4×4 float matrix as a
  raw fixed-layout array (Core has no public math type yet — not this Spec's
  scope), matching exactly what `pushConstant()` copies verbatim.
- **`Renderer`** (`renderer.h`) — a stateless orchestrator (ADR-0022): retains
  no GPU resource, no frame-to-frame state, across calls. Depends only on RHI,
  RenderGraph, Core — never Platform, Vulkan Backend, or any `Vk*` type. Not
  internally thread-safe. **Deliberately left at trivial compiler-generated
  copy/move defaults**, unlike `RenderGraphBuilder`'s non-copyable/non-movable
  builder — that restriction exists because `RenderGraphBuilder` vends handles
  tied to its own stable address; `Renderer` has no handles, no vended
  identity, and no member state (an earlier revision deleted `Renderer`'s
  copy/move constructors by mistaken analogy, corrected by leaving every
  special member defaulted). Single method: `void drawFrame(rhi::CommandList&,
  rhi::RenderTarget& colorTarget, rhi::Texture& depthTarget, rhi::Buffer&
  cameraUniformBuffer, std::span<const DrawItem> drawItems)` — builds, compiles,
  and executes one RenderGraph draw pass into `commandList`; never calls
  `Device::submit()`/`Presentation::present()`. `colorTarget`/`depthTarget`/
  `cameraUniformBuffer` are borrowed references the caller already owns and (for
  `cameraUniformBuffer`) has already written this frame's matrices into —
  `Renderer` never touches raw camera math, only binds the `Buffer`.
  `drawItems` is iterated once, retained nowhere.

### `Renderer::drawFrame()` algorithm (candidate, `renderer.cpp`)

1. `RenderGraphBuilder builder; colorResource = builder.declareResource("color");
   depthResource = builder.declareResource("depth"); pass = builder.declarePass("draw");
   builder.writes(pass, colorResource, ResourceState::ColorAttachmentOutput);
   builder.writes(pass, depthResource, ResourceState::DepthAttachmentReadWrite);`
   (single `writes()` per resource — ADR-0018/ADR-0026);
   `builder.setExecute(pass, [&commandList, &cameraUniformBuffer, drawItems](CommandList& cmd){
   for (const DrawItem& item : drawItems) { cmd.bindPipeline(item.material->pipeline());
   cmd.bindVertexBuffer(item.mesh->vertexBuffer()); cmd.bindIndexBuffer(item.mesh->indexBuffer());
   cmd.bindUniformBuffer(cameraUniformBuffer);
   cmd.pushConstant(item.objectToWorld.data(), item.objectToWorld.size() * sizeof(float));
   cmd.drawIndexed(item.mesh->indexCount()); } });`
2. `compileResult = builder.compile();
   ATLANTIS_CHECK_MSG(compileResult.isOk(), "Renderer's fixed one-pass graph never fails to compile");`
3. `bindings = { {resourceAt(0), &colorTarget, kBackgroundClearColor, nullptr, 0.0f},
   {resourceAt(1), nullptr, ClearColorValue{}, &depthTarget, 1.0f} };
   render_graph::execute(compileResult.value(), bindings, commandList);`

`kBackgroundClearColor` is a fixed `Renderer`-internal constant (not a
caller-configurable parameter this round). **`Renderer` never observes
`Presentation::metadata()` or a format change** — by construction:
`drawFrame()`'s signature has no `Device&`/`Presentation&` parameter, so there
is no code path for it to call `metadata()` even if a future edit tried. A
structural, not merely documented, guarantee.

## 12. Shader bootstrap

Per ADR-0027, checked in under `shaders/minimal_renderer/` — **not** under
`tests/` (corrected from an earlier revision). `shaders/` is this repository's
pre-designated neutral product-level location; putting a non-shipping example's
runtime assets under `tests/` would make `examples/minimal_renderer_demo`
depend on the test tree, which is backwards. `shaders/minimal_renderer/` is
shared as a single authoritative copy by both the demo and
`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` (§17's CMake copies it to
each consumer's own build output — never duplicated as source):

- `minimal_mesh.{vert,frag}.glsl` — human-readable GLSL source (GLSL is an
  arbitrary non-binding choice for this bootstrap; ADR-0027 makes no shader
  source language decision). Vertex: `in vec3 position` @0, `in vec3 color` @1;
  `push_constant { mat4 objectToWorld }`;
  `binding = 0 uniform CameraUniform { mat4 view; mat4 projection; }`; outputs
  `gl_Position = projection * view * objectToWorld * vec4(position, 1.0)` and
  passes `color` through. Fragment: outputs the interpolated `color`
  unmodified — no lighting, no texture sample.
- `minimal_mesh.{vert,frag}.spv` — pre-compiled bytecode, produced by a human
  running `glslc` manually (not by any CMake target).
- `README.md` — the exact `glslc` command line, the `glslc`/Vulkan SDK version,
  regeneration instructions. Committed alongside the `.spv` files in the same
  PR.

**No CMake target compiles, parses, or reflects any of the above.**

**Runtime location — copied next to each consumer's executable, never a baked
absolute path.** An earlier revision proposed a `configure_file()`-generated
header embedding `${CMAKE_SOURCE_DIR}` — rejected as fragile (breaks if the
binary is copied elsewhere) and an unwanted hidden environment coupling.
Instead each consumer target gets an
`add_custom_command(TARGET ... POST_BUILD ...)` that `copy_if_different`s the
two `.spv` files into a `shaders/` subdirectory next to that target's own
executable (`$<TARGET_FILE_DIR:target>`, a per-configuration-correct generator
expression). **No file-path API of any kind locates the shader files at
runtime — not `GetModuleFileNameW`, not any other Win32 call, not a new
Core/Platform path-resolution API** (an earlier revision's `GetModuleFileNameW`
helper was rejected — it would make the demo and GPU test directly depend on a
raw Win32 API for something `AGENTS.md`'s Platform-isolation rule / ADR-0005
reserve to the Platform module; unlike `windows_platform_smoke_tests.cpp`'s
`<windows.h>` use, which exists specifically to test Platform's own Win32
implementation). Each consumer opens the shaders by a plain **relative** path
(`"shaders/minimal_mesh.vert.spv"`), relying on its current working directory
already being its own build-output directory, which this Plan arranges
structurally: for the GPU test, `catch_discover_tests()`'s `WORKING_DIRECTORY`
parameter is `"$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>"` (§17), so
CTest launches every discovered case with that directory; for the demo, a
convenience `run_minimal_renderer_demo` `add_custom_target` sets
`WORKING_DIRECTORY` so `cmake --build build --target run_minimal_renderer_demo`
always launches with the correct directory (a human launching the `.exe`
directly must do so from its own output directory — §15 states this as an
operational precondition). **Shader load failure is a recoverable explicit
outcome, never a crash** — each consumer's own small local
`loadSpirvFile(path) -> std::optional<std::vector<std::uint32_t>>` helper (plain
`std::ifstream`, C++ standard library only) returns empty on any open/read
failure; the caller logs and exits gracefully (mirroring
`examples/frame_execution_demo`'s `EXIT_FAILURE` pattern), never calling
`createPipeline()` with incomplete bytecode. Deliberately **not** routed
through `PipelineCreateError` — that enum describes GPU object-creation
failure, distinct from a host-side file-I/O failure before any Vulkan call.

**Vertex-input/binding layout consistency verification:** this round has no
automated reflection-based cross-check between the hand-specified
`VertexInputLayout`/`PipelineCreateParams` and the actual SPIR-V interface
(ADR-0027) — the available signals are `createPipeline()` succeeding, Validation
Layers reporting zero warnings/errors when the pipeline is used to draw, and
the GPU test's/demo's manually-observed correct visual output (§15). A mismatch
Validation Layers do not catch (e.g. two `Float3` attributes silently swapped)
would only be caught by the visual-output check — this limitation is inherited
from ADR-0027's accepted trade-off, not introduced here. Any future
`minimal_mesh.{vert,frag}.glsl` change requires re-running `glslc` manually and
updating the `.spv` files and `README.md`'s recorded command line/version
together in the same commit — reviewable as an ordinary binary-diff-plus-
source-diff PR. Migration boundary to a future Shader System: unchanged from
ADR-0027.

## 13. Resize / format-change contract — verification composition implementation

Per ADR-0022/ADR-0025's confirmed contract, implemented entirely in
`examples/minimal_renderer_demo/main.cpp` (mirroring
`examples/frame_execution_demo`'s structure) — no RHI or `Renderer` code
implements any part of this beyond the primitives §3–§5 already expose
(`Presentation::metadata()` exists unchanged since Spec 0003). Once per frame,
after a successful `acquireNextTarget()`:

1. `currentFormat = presentation->metadata().format`; if it differs from
   `lastSeenFormat`: `device->waitIdle()`, then `createMaterial(*device, {...,
   .colorFormat = currentFormat, .depthFormat = D32Sfloat, ...})`. On `Err`:
   log; **keep** the existing material (still valid, still matches
   `lastSeenFormat`); do **not** update `lastSeenFormat`, so the check retries
   next frame (this frame draws with the old `Pipeline` against the new format
   — a real, accepted, transient mismatch this round has no better fallback
   for). On success: `material = std::move(newMaterialResult.value())` — the
   old `Pipeline` is destroyed **here, only after the new one already
   succeeded** — never destroy-then-create; then update `lastSeenFormat`. The
   depth `Texture` is **not** recreated here solely for a format change — its
   own `D32Sfloat` format never varies with the swapchain's color format.
2. `currentExtent = renderTarget->extent()`; if it differs from
   `lastSeenExtent`: `createTexture({.extent = currentExtent, .format =
   D32Sfloat})`. On `Err`: log; keep the existing depth `Texture` (mismatched
   extent — may itself trigger a Validation Layer warning if drawn against a
   differently-sized `RenderTarget` this frame, an accepted transient gap); do
   not update `lastSeenExtent`, so the check retries. On success: same
   create-before-destroy ordering; update `lastSeenExtent`. `Pipeline` is
   **not** recreated here (dynamic viewport/scissor — §3).
3. Write this frame's view/projection into `cameraBuffer->mappedData()` — safe
   here because `acquireNextTarget()`'s own internal drain (Spec 0006 / PR #24)
   already guarantees no prior-frame GPU work is still reading this `Buffer`.
4. `renderer.drawFrame(*commandList, *renderTarget, *depthTexture, *cameraBuffer,
   drawItems)`.

**Create-before-destroy, never destroy-before-create:** both steps 1 and 2
construct the new resource first and only replace the caller's variable (via
`unique_ptr`/wrapper move-assignment) once construction succeeded. An earlier
revision destroyed the old resource first and then attempted construction — if
that failed, the demo would have been left with *no* `Material` at all, an
unrecoverable state. The corrected ordering means a transient creation failure
leaves the caller with a still-valid, if temporarily stale, resource and a
retry on the very next frame — never a hard failure from what Spec 0007
classifies as a routine event. Steps 1 and 2 are independent, in either order,
run every frame at negligible cost — an extent change never triggers step 1's
`Pipeline` rebuild, and a format change (without a concurrent extent change)
never triggers step 2's `Texture` rebuild, exactly as ADR-0025's contract
requires. If the environment cannot genuinely trigger a format change (no
second monitor/surface with different capabilities), step 1 is verified by code
inspection only — the manual-verification report (§15) must state this
explicitly, per Spec 0007's Acceptance Criteria.

## 14. Error model implementation

| Case | Tier | Mechanism |
|---|---|---|
| `Mesh`/`Material`/`Buffer`/`Texture`/`Pipeline` used outside its valid lifetime window; `Device` destroyed while any it backed are still alive | Lifetime precondition violation | Not detected; caller obligation (documented, tested via correct-discipline manual verification only) |
| `ResourceBinding` with both/neither of `target`/`depthTexture` set (Guard 0); `ResourceState`-tagged usage with no binding (Guard 1); a bound `RenderTarget` with a declared read usage (Guard 2) | Guaranteed-detectable programmer error | `ATLANTIS_CHECK_MSG` inside `execute()` |
| `bindVertexBuffer`/`bindIndexBuffer`/`bindUniformBuffer` with a `Buffer` of the wrong `BufferPurpose` | Guaranteed-detectable programmer error | `ATLANTIS_CHECK` inside `VulkanCommandList` |
| `Buffer`/`Texture`/`Pipeline` creation failure (`vkAllocateMemory`/`vkCreateBuffer`/`vkCreateImage`/`vkCreateImageView`/`vkCreateShaderModule`/`vkCreateGraphicsPipelines`) | Recoverable | `Result::Err(BufferCreateError\|TextureCreateError\|PipelineCreateError)` |
| No physical device supports either dynamic-rendering path | Recoverable | `Result::Err(DeviceCreateError::DynamicRenderingUnavailable)` |
| Attachment/extent change not yet handled by the caller (stale `Pipeline`/`Texture`) | Caller precondition violation (documented contract, §13) | Not detected by RHI/RenderGraph/Renderer; the caller's §13 check-and-rebuild discipline is the only mitigation |
| Every other case (submit/present/acquire failures, zero-extent, out-of-date/suboptimal) | Unchanged from Spec 0006 | Unchanged from Plan 0006 §12 |

`vulkan_result.h` gains three new pure mapping functions (`toBufferCreateError`,
`toTextureCreateError`, `toPipelineCreateError`), mirroring the four existing
ones.

## 15. Testing strategy

**GPU-independent unit tests (layer 1, no Vulkan device):**

- `dynamic_rendering_tests.cpp` — `decideDynamicRenderingPath()`'s truth table.
  When `physicalDeviceProperties2InstanceExtensionAvailable = false`: a
  representative sample (`FFFF`, `TTTT`, `TFFF`, `FFTT`) all return
  `Unavailable` (the short-circuit makes the remaining four arguments provably
  irrelevant by inspection). When `true`: the **exhaustive** 16-case table over
  `(apiVersionAtLeast1_3, coreFeatureSupported, extensionAdvertised,
  extensionFeatureSupported)` — `T,T,*,*` → `Core`; `*,*,T,T` (with not both of
  the first two) → `Extension`; everything else → `Unavailable` — including
  logically-inconsistent combinations a caller should never produce, confirming
  safe degradation.
- `vulkan_memory_tests.cpp` — `selectMemoryTypeIndex()` against a synthetic
  `VkPhysicalDeviceMemoryProperties`: exact match; match among several
  candidates (first-matching-index chosen); no match (empty optional).
- `resource_state_mapping_tests.cpp` (extended) — the two new §10 table rows
  produce their documented values.
- `vulkan_result_tests.cpp` (extended) — the three new mapping functions.
- `attachment_execution_tests.cpp` — against `fake_command_list.h` (Spec 0006,
  extended to record `beginRendering`/`endRendering`/`bindPipeline`/etc.):
  Guard 0 (malformed binding); Guard 1 generalized to a depth `Texture`
  binding; Guard 2 firing for a `target` binding with a read usage and *not*
  firing for an equivalent `depthTexture` binding; draw-pass recognition firing
  for `ColorAttachmentOutput`/`DepthAttachmentReadWrite` and *not* for
  `ColorAttachmentWrite` (the concrete regression for §7/ADR-0026's central
  fix); `beginRendering()` before and `endRendering()` immediately after a
  recognized draw pass's callback; independent per-resource state tracking
  across two bound resources; every bound resource (color and depth) starting
  each `execute()` from `Undefined`, including on a second identical `execute()`.
- `execution_tests.cpp` (unchanged) — Spec 0006's existing cases, including the
  `ColorAttachmentWrite`-only clear pass, must still pass unmodified — the
  concrete proof the new derivation rule does not affect it.
- `buffer_texture_pipeline_tests.cpp` — `BufferCreateParams`/`TextureCreateParams`/
  `PipelineCreateParams` default-construction and equality sanity; no real
  device.
- `renderer_ownership_tests.cpp` — compile-time checks that `Mesh`/`Material`
  are movable-not-copyable and `Renderer` is left at defaulted special members;
  a `drawFrame()` call against a fake `CommandList` with two `DrawItem`s
  sharing the same `Mesh`/`Material` instance (reference reuse, not a cache)
  records exactly two `bindPipeline`/…/`drawIndexed` sequences with the
  *distinct* push-constant data each `DrawItem` supplied — the concrete
  regression for the push-constant-vs-shared-uniform-buffer argument (ADR-0025),
  without a real device.

**GPU-required tests (Windows/Vulkan)** — `minimal_renderer_gpu_tests.cpp`
(carries the `gpu` CTest label, mirroring `frame_execution_gpu_tests.cpp`):
creating and destroying a `Buffer` of each of the three purposes; a depth
`Texture` including at a resized extent; a `Pipeline` from the checked-in
SPIR-V pair (§12); one full draw-pass execution (bind, draw, attachment scope
begin/end) against a real acquired `RenderTarget` and real depth `Texture`,
Validation Layers clean; a frame with **more than one `DrawItem`**, each with a
distinct `objectToWorld`, confirming (via a Validation-Layers-clean run plus
the GPU-independent push-constant test, together) that the recorded draws do
not collide; `createDevice()`'s dynamic-rendering capability detection on
whichever path the test machine's actual GPU/driver provides — **this
environment is expected to exercise exactly one of the two paths**; the other
path and the explicit-error case (`DynamicRenderingUnavailable`) remain
verified by code inspection only where a second real device/driver is
unavailable, stated explicitly in any verification report.

**What this automated GPU test can and cannot confirm about depth occlusion:**
it confirms every API call succeeds and Validation Layers report zero
warnings/errors for a depth-tested draw — it does **not** read back or inspect
the rendered pixels, so it cannot by itself confirm the depth test actually
*behaved* correctly (e.g. that front-facing geometry visually occludes
back-facing geometry, as opposed to an inverted `VkCompareOp` that still runs
without a validation error but produces the wrong image). That confirmation is
the manual verification's job (below), not this automated test's — no automated
test in this Plan claims to substitute for it, consistent with Spec 0007's
stated image-regression limitation.

- **Headless integration tests:** not applicable — unchanged from Spec 0006's
  equivalent flag.
- **Image regression tests:** not applicable — manual visual verification only,
  a real, accepted limitation Spec 0007 itself states.
- **Known environment constraint, carried forward from PR #23/#24's own
  verification history:** Windows Smart App Control has previously blocked
  execution of a freshly-compiled test executable, and `catch_discover_tests`'s
  discovery aborts an entire bare `ctest` run if any single registered
  executable's discovery invocation fails. Whoever executes this Plan's
  verification must, if the same constraint recurs, run `ctest` scoped per
  test-module subdirectory (`ctest --test-dir build/tests/<module> ...`) rather
  than a repository-wide invocation — and must report exactly what was actually
  run this way, never claim a repository-wide `ctest` pass that did not
  execute.

**Manual verification** (`examples/minimal_renderer_demo`) — mirrors
`examples/frame_execution_demo`'s structure and non-shipping disclaimer.
**Must be launched via `cmake --build build --target run_minimal_renderer_demo`
(or by running the built executable directly from its own build-output
directory)** — §12's plain relative shader path only resolves correctly with
that working directory; launching from anywhere else is a usage error.
Confirms, interactively: a visible window showing a recognizable,
correctly-shaded (per-vertex color), correctly depth-ordered 3D mesh
(front-facing geometry occludes back-facing correctly, no z-fighting or
inverted depth test) continuously across repeated frames; the camera transform
(written into `cameraBuffer` each frame) visibly affects the mesh's projected
position/orientation (e.g. a slowly rotating/orbiting camera — confirming the
uniform buffer binding path actually works, not merely that *something* draws);
interactive resize continues to render correctly (including depth) at the new
size, with the depth `Texture` demonstrably recreated (§13 step 2) while
`Pipeline` demonstrably *not* recreated (§13 step 1 untouched) for an
extent-only change (the demo logs or asserts this distinction explicitly);
format-change handling, if the environment allows triggering it (§13's stated
limitation) — `Device::waitIdle()` called, `Material`'s `Pipeline` recreated,
zero Validation Layer warnings/errors across the transition; minimizing results
in no crash, no busy-spin, zero Vulkan calls while minimized; restoring resumes
correct rendering (mesh, depth, camera) with no special recovery step; clean
exit at any point in this sequence, including mid-resize, minimized, and a
deliberate mid-frame exit (acquired but not yet submitted/presented), with no
outstanding acquired `RenderTarget`, no leaked
`CommandList`/`Buffer`/`Texture`/`Pipeline`/`Mesh`/`Material`, and zero
Validation Layer warnings/errors at any point, including at shutdown.

**Debug/Release:** every automated test above runs, and the manual demo is
exercised, under both configurations.

## 16. Explicit prohibitions (grep/code-review checklist)

Run at the end of every implementation step, and again before opening a PR:

- No `Vk*` type / Vulkan header outside Vulkan Backend — `grep -rn "Vk[A-Z]"` /
  `grep -rln "vulkan" -i` over `src/rhi/include src/render_graph/include
  src/renderer/include`, expect none.
- No direct `vkCmd*` outside Vulkan Backend's `CommandList` implementation —
  `grep -rn "vkCmd" src --include=*.cpp --include=*.h | grep -v
  "src/vulkan_backend/"`, expect none.
- No `VkRenderPass`/`VkFramebuffer` anywhere (ADR-0024) — `grep -rn
  "VkRenderPass\|VkFramebuffer" src`, expect none.
- RenderGraph never submits or presents — `grep -rn "submit(\|present(" src/render_graph`,
  expect none.
- Renderer never links Platform or Vulkan Backend — `grep -n
  "Atlantis::Platform\|Atlantis::VulkanBackend\|Vulkan::Vulkan"
  src/renderer/CMakeLists.txt`, expect none.
- Renderer never depends on `Presentation`/`Device` concretely — `grep -rn
  "metadata()\|waitIdle()" src/renderer`, expect none (§13's contract lives
  entirely in the verification composition).
- `ColorAttachmentWrite` never reused by the new draw-pass path — `grep -rn
  "ColorAttachmentWrite" src/render_graph/src/execution.cpp`, expect none.
- No shader compiler invocation — `grep -rln "glslc\|dxc\|shaderc" CMakeLists.txt
  src tests examples cmake`, expect none.
- No new third-party dependency — `git diff --stat CMakeLists.txt cmake/`,
  expect no dependency-fetching changes.
- No `src/shader_system/`, no `src/runtime/` source (directories do not exist).
- No general descriptor-set/sampler/general-`Texture` type — `grep -rn "class
  Sampler\|VkDescriptorSetLayout\|VkDescriptorPool" src/rhi src/renderer`,
  expect none in `src/rhi` (`VkDescriptorPool` may legitimately appear inside
  Vulkan Backend's uniform-binding implementation, §10).
- Each `Buffer`/`Texture` individually allocated — `grep -rn
  "std::vector<VkDeviceMemory>\|VkDeviceMemory\[\]" src/vulkan_backend`, expect
  none (exactly one `VkDeviceMemory` member per instance).
- Descriptor-set/pool machinery never leaks outside
  `vulkan_pipeline.*`/`vulkan_device.*` — `grep -rln
  "VkDescriptorSet\|VkDescriptorPool\|VkDescriptorSetLayout" src/vulkan_backend
  | grep -v "vulkan_pipeline\|vulkan_device"`, expect none.
- Shader assets live under `shaders/`, never `tests/` — `find tests -iname
  "*.glsl" -o -iname "*.spv"`, expect nothing.
- Demo/test never use a raw Win32 file-path/module API to locate shader assets
  — `grep -rln "GetModuleFileNameW\|GetModuleFileName\b" examples/minimal_renderer_demo
  tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` and `grep -n "windows.h"`
  in those two files, expect none.
- `vkGetPhysicalDeviceFeatures2KHR` is only ever called through the resolved
  function pointer — `grep -n "vkGetPhysicalDeviceFeatures2KHR"
  src/vulkan_backend/src/vulkan_instance.cpp
  src/vulkan_backend/src/vulkan_device.cpp`, expect exactly one direct call
  (the `vkGetInstanceProcAddr()` resolution itself).

Code-review checklist (manual, per step): every new `VkResult`-returning call's
result is checked; every new public type documents its thread-safety contract
in one line; no `ATLANTIS_CHECK` where a `Result::Err` belongs, or vice versa
(cross-check §14); `Pipeline`'s viewport/scissor are dynamic state, never baked
in (no `VkViewport`/`VkRect2D` in `VulkanPipeline`'s
`VkGraphicsPipelineCreateInfo`); every checked-in `.spv` has a corresponding
`.glsl` source and a `README.md` compiler/version note in the same commit.

## 17. Build integration

`src/rhi/CMakeLists.txt` — header-only additions (`buffer.h`, `texture.h`,
`pipeline.h` are pure interface headers; no new `.cpp` beyond `types.cpp`'s
`operator==` for the new structs, per the `Extent2D`/`ClearColorValue`
precedent). `src/vulkan_backend/CMakeLists.txt` — add the five new `.cpp` files;
`target_link_libraries` unchanged. `src/render_graph/CMakeLists.txt` —
unchanged (`execution.cpp` already exists; only its contents grow).
`src/renderer/CMakeLists.txt` — §1's link boundary. `tests/rhi/CMakeLists.txt`
— add `buffer_texture_pipeline_tests.cpp`. `tests/render_graph/CMakeLists.txt`
— add `attachment_execution_tests.cpp`. `tests/vulkan_backend/CMakeLists.txt` —
add `dynamic_rendering_tests.cpp` and `vulkan_memory_tests.cpp` to the existing
`atlantis_vulkan_backend_tests` target; add `minimal_renderer_gpu_tests.cpp` to
the existing `atlantis_vulkan_backend_gpu_tests` target (which now also links
`Atlantis::Renderer`, since it exercises `Renderer::drawFrame()`); the existing
`catch_discover_tests(atlantis_vulkan_backend_gpu_tests ...)` call gains a new
`WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>"`
(harmless for every pre-existing case, none of which opens a file by relative
path) and an `add_custom_command(TARGET ... POST_BUILD ...)` that
`copy_if_different`s the two `.spv` files into a `shaders/` subdirectory next
to the executable. `tests/renderer/CMakeLists.txt` (new) — one
`atlantis_renderer_tests` executable from `renderer_ownership_tests.cpp`, a
`PRIVATE` include path for `tests/render_graph` (reuses `fake_command_list.h`),
links `Atlantis::Renderer` + `Catch2::Catch2WithMain` +
`atlantis_compiler_warnings`, `catch_discover_tests(... DISCOVERY_MODE PRE_TEST)`.
`examples/minimal_renderer_demo/CMakeLists.txt` — mirrors
`examples/frame_execution_demo/CMakeLists.txt` + `Atlantis::Renderer` link +
the same shader-copy `add_custom_command()` + a `run_minimal_renderer_demo`
`add_custom_target` with `WORKING_DIRECTORY` set. Root `CMakeLists.txt` — the
three new `add_subdirectory` calls under their guards.

## 18. Implementation order

Each step ends with the relevant §15 build/test commands (GPU commands only
where noted) and §16's grep checklist.

1. **RHI type/interface additions** — `types.h` (§2), `buffer.h`, `texture.h`,
   `pipeline.h`, `device.h`/`command_list.h` signature additions (declarations
   only). Build: header compilation only.
2. **`vulkan_memory.{h,cpp}` and `dynamic_rendering.{h,cpp}`** — the two pure
   decision functions, independent of any concrete resource type or real Vulkan
   call. GPU-independent unit tests (`vulkan_memory_tests.cpp`,
   `dynamic_rendering_tests.cpp`, including the full truth table) land here.
3. **`vulkan_instance.cpp`'s instance-extension query/enable + entry-point
   resolution, then `VulkanDevice`'s dynamic-rendering capability selection** —
   the `VK_KHR_get_physical_device_properties2` availability check, conditional
   enablement, and `vkGetPhysicalDeviceFeatures2KHR` resolution (§8);
   `vulkan_device.cpp` extends the physical-device selection loop to call
   `decideDynamicRenderingPath()` per candidate, adds
   `DeviceCreateError::DynamicRenderingUnavailable`, resolves/stores the
   device-level entry-point function pointers. Build-verify only; confirm
   existing `createDevice()` GPU tests still pass unmodified.
4. **`VulkanBuffer`/`VulkanTexture` + `resource_state_mapping.{h,cpp}`
   extensions** — concrete resource classes, `createBuffer()`/`createTexture()`,
   the two new transition-table rows. GPU-independent: extend
   `resource_state_mapping_tests.cpp`. First GPU-required smoke test:
   create/destroy a `Buffer` of each purpose and a depth `Texture`, no drawing.
5. **`VulkanPipeline`** — `createPipeline()`, including `VulkanDevice`'s
   one-time descriptor pool creation (§10), using §12's checked-in `.spv` files
   (this step adds those files, the `.glsl` source, `README.md`, and the
   shader-copy `add_custom_command()`s). GPU test: create/destroy a `Pipeline`,
   confirming its one `VkDescriptorSet` is freed without error.
6. **`VulkanCommandList` draw path** — `bindPipeline`/`bindVertexBuffer`/
   `bindIndexBuffer`/`bindUniformBuffer`/`pushConstant`/`drawIndexed`/
   `beginRendering`/`endRendering`. GPU test: manually record and submit one
   full draw (direct `CommandList` calls in the test, to isolate this layer
   from §7's RenderGraph changes) against a real acquired `RenderTarget` and
   depth `Texture`, Validation Layers clean.
7. **RenderGraph: `ResourceBinding` extension + `execute()` algorithm** —
   §6–§7. GPU-independent: `attachment_execution_tests.cpp` (every bullet from
   §15), plus confirming `execution_tests.cpp`'s existing cases are unaffected.
8. **`src/renderer/` module** — `Mesh`/`createMesh()`,
   `Material`/`createMaterial()`, `DrawItem`, `Renderer::drawFrame()`.
   GPU-independent: `renderer_ownership_tests.cpp` (fake `CommandList`).
9. **Full GPU integration** — `minimal_renderer_gpu_tests.cpp`: the complete
   `Buffer`/`Texture`/`Pipeline`/`Renderer::drawFrame()`/attachment-scope/
   multi-draw-item path against a real device, Validation Layers clean. `ctest
   -C Debug -L gpu`.
10. **`examples/minimal_renderer_demo`** — the interactive manual verification
    composition (§15), including §13's resize/format-change contract. Run
    interactively: mesh visible, camera transform visible, resize (extent-only,
    no `Pipeline` rebuild), format-change if the environment allows,
    minimize/restore, mid-frame exit, clean exit.
11. **Final consistency pass** — §16's full grep checklist, §19's mapping
    walked line by line, `cmake --build build --config Release` clean, full
    `ctest` suite (both `-LE gpu` and `-L gpu`, Debug and Release) green.

**Sequencing:** steps 1–2 have no cross-dependency. Step 3 depends on 1 (needs
`DeviceCreateError`) and 2 (needs `decideDynamicRenderingPath()`). Step 4
depends on 1–3. Step 5 depends on 1, 4. Step 6 depends on 3–5. Step 7
(RenderGraph) has no dependency on 3–6 and could be built in parallel using
`fake_command_list.h`, but step 9's full integration needs 6 and 7. Step 8
depends on 1 and benefits from step 7's `fake_command_list.h` extensions
existing. Step 9 needs 6, 7, 8. Step 10 needs 9. Step 11 is last.

## 19. Acceptance Criteria Mapping

| Spec 0007 Acceptance Criterion (abbreviated) | Plan Section(s) |
|---|---|
| No `Vk*`/Vulkan header in RHI/RenderGraph public headers | §1 file list, §16 grep |
| No `vkCmd*`/barrier/`VkRenderPass`/`VkFramebuffer` outside Vulkan Backend | §10, §16 grep |
| `Buffer`/`Texture`/`Pipeline` move-only | §3 (mechanism) |
| Every `Buffer`/`Texture` individually allocated, no shared `VkDeviceMemory` | §9, §16 grep |
| `Renderer` retains no GPU resource across calls | §11 (no member state), §15 (`renderer_ownership_tests.cpp`) |
| `Mesh`/`Material` never created/cached/looked-up by `Renderer` | §11 (`createMesh`/`createMaterial` are free functions) |
| `createDevice()` dual-path detection + explicit error when unavailable | §8, §15 |
| No capability/path indicator in RHI/RenderGraph public headers | §8 (entry points stored `VulkanDevice`-private) |
| Guard 1 generalized to depth `Texture` bindings | §7 algorithm step 1 |
| Guard 2 unchanged scope (`target` only) | §7 algorithm step 2 |
| `execute()` never wraps `ColorAttachmentWrite` in attachment scope | §7 (draw-pass recognition), §16 grep, §15 (regression test) |
| Depth declared as exactly one `writes()` usage | §11 step 1 |
| Push constant, not shared uniform, for per-object transform | §5, §10, §15 (multi-`DrawItem` regression test) |
| `Pipeline` uses dynamic viewport/scissor — no rebuild on extent-only resize | §3, §13 step 2, §15 (demo log/assertion) |
| Format change observed via `Presentation::metadata()`, `waitIdle()` before rebuild | §13 step 1 |
| `Renderer` has no format/extent-observing code path | §11 (no `Device&`/`Presentation&` param), §16 grep |
| No shader compiler/reflection invoked by any build target | §12, §16 grep |
| Checked-in `.spv` has matching source + compiler note | §12, §16 checklist |
| Every `VkResult` checked | §8–§10, §16 checklist |
| Validation Layers clean, Debug + GPU CI | §15 |
| Manual demo: visible mesh, camera transform, depth occlusion, resize, minimize/restore, clean exit | §15 manual verification |
| No scene graph/ECS/asset system/second material/instanced draw/multi-frame-in-flight | §Non-Goals, §16 grep |
| No `src/renderer/` dependency on Platform/Win32/Android NDK/`Vk*` | §1 (link boundary), §16 grep |
| Camera uniform binding is a fixed, single-purpose mechanism — no general descriptor-set/bindless API | §10, §16 grep |
| RenderGraph never wraps a non-draw pass in an attachment scope; draw-pass identification is UB-safe under a non-terminating handler | §7 algorithm step 4 (check-then-skip), §15 |

## Verification Checklist

- [ ] Unit tests: all GPU-independent suites listed in §15 — pass under `ctest
      -C Debug -LE gpu` and `ctest -C Release -LE gpu`.
- [ ] Headless integration tests: not applicable (Spec 0007 Non-Goals;
      windowed-first sequencing).
- [ ] Image regression tests: not applicable (manual visual verification only
      this round, per Spec 0007's stated limitation).
- [ ] Vulkan Validation Layers clean: `ctest -C Debug -L gpu`, the Release
      equivalent, and the manual `minimal_renderer_demo` run (including the
      mid-frame-exit and, where the environment allows, the format-change case)
      all produce zero WARNING/ERROR output.
- [ ] Other: §16's grep checklist returns the documented (empty, in every
      prohibited case) result.

## Rollback Plan

Every change is additive to existing, already-shipped modules plus one wholly
new module (`atlantis_renderer`) — no existing public method signature is
altered or removed (`ResourceBinding`'s extension keeps its existing
`resource`/`target` fields; `ColorAttachmentWrite`'s meaning is untouched).
Revert is a straightforward `git revert` of this feature's merge commit(s) —
nothing outside this Plan's own new module and test/demo targets depends on
anything it adds. A partial rollback (keep RHI/Vulkan Backend resource types,
revert RenderGraph attachment integration and/or `src/renderer/`) is possible
since §18's steps are ordered so each layer (RHI resources §1–6, RenderGraph
§7, Renderer §8) is independently reviewable before the next builds on it.

## Definition of Done

Per [docs/process/definition-of-done.md](../docs/process/definition-of-done.md):

- Headless verification: not applicable (Non-Goal, unchanged from every prior
  spec/plan).
- Image regression: not applicable this round (no golden-image harness exists
  yet, per AGENTS.md sequencing).
- ADR: none new required by implementation — ADR-0022–0027 already `Accepted`
  cover every architectural decision this Plan operationalizes; any *deviation*
  discovered during implementation is a Plan/Spec issue to raise (a Human
  Review blocker, not a new ADR to file unilaterally) — see the next section.
- All other items apply as stated in the linked document.

## Human Review Blockers (if triggered during Implementation)

Per AGENTS.md, any of the following discovered during implementation must stop
work and return to Spec/Plan/ADR review — **not** be decided unilaterally in a
PR:

- Any need to change `Buffer`/`Texture`/`Pipeline`'s move-only ownership model,
  or to introduce shared/reference-counted ownership for any GPU resource this
  Plan touches.
- Any need for `Renderer` to retain state, own a GPU resource, or read
  `Presentation::metadata()`/observe a format change itself.
- Any need to widen Guard 2 to depth `Texture` bindings, or to relax ADR-0018's
  same-pass read+write rejection.
- Any need for a `VkRenderPass`/`VkFramebuffer` fallback path, or to raise the
  Vulkan Backend's overall minimum supported API version. *(This was in fact
  triggered post-approval — see the Post-Approval Deviation note in the header;
  resolved via ADR-0024's accepted amendment, not a unilateral PR decision.)*
- Any need for a general GPU memory suballocator, a shared `VkDeviceMemory`
  block, or a resource cache/pool of any kind.
- Any need to invoke a shader compiler from a build target, perform SPIR-V
  reflection, or introduce shader caching/hot-reload.
- Any need for a second graphics backend, a new third-party dependency, or a
  module boundary this Plan does not already list.
