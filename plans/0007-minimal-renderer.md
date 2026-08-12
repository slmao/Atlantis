# Plan: Minimal Renderer

- **Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Post-Approval Deviation (2026-08-13):** Implementation of this Plan's
  §8 dynamic-rendering Core path is **paused** pending Human Review of a
  proposed amendment to
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)
  (its "Proposed Amendment (Under Review)" section). This Plan's own
  Status line and §2–§13 content are unchanged and remain approved — see
  "Post-Approval Deviation Record" near the end of this document for the
  full blocker record and what resuming implementation requires.
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; content authored by the agent, reviewed and approved by a
  human per the Human Review Approval note below.
- **Human Review Approval (2026-08-11):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-11, completing a **joint
  Spec 0007 + Plan 0007 Human Review** conducted across three prior
  review rounds (see each of this Plan's own commits — `plan: define
  minimal renderer`, `plan: refine minimal renderer plan`, `plan:
  finalize minimal renderer implementation details` — for the issues
  those rounds raised and resolved before this approval). The human
  reviewed and approved this Plan's candidate public API (§2–§13),
  ownership model, state machines, module boundaries, implementation
  order (§18), and verification plan (§15), as this Plan reads after its
  third revision, including, explicitly:

  1. **The fixed, narrowly-scoped camera uniform descriptor mechanism**
     (§10's "Camera uniform binding — full candidate design"): exactly
     one `VkDescriptorSetLayout` binding, one `VulkanDevice`-owned
     `VkDescriptorPool` with a capacity (`maxSets = 4`) derived from this
     Plan's own actual peak concurrent descriptor-set count (2 — one
     `Material` in steady state, plus the prior one momentarily alive
     during a format-change rebuild), one `VkDescriptorSet` per
     `VulkanPipeline` freed at that `Pipeline`'s own destruction — never
     a general descriptor-set/bindless API, and never exposed outside
     `vulkan_pipeline.*`/`vulkan_device.*`.
  2. **Shader assets loaded by a plain relative path from each
     consumer's own build-output working directory** (§12) — no Win32
     file-path/module API (`GetModuleFileNameW` or otherwise), no new
     Core/Platform path-resolution API; `catch_discover_tests()`'s
     `WORKING_DIRECTORY` parameter for the GPU test, the
     `run_minimal_renderer_demo` convenience CMake target for the demo.
  3. **Dynamic rendering's capability-detected dual path, including its
     instance-level query prerequisite** (§8): `VK_KHR_get_physical_device_properties2`
     queried and conditionally enabled at the instance level, before
     `vkCreateInstance()`; `vkGetPhysicalDeviceFeatures2KHR` resolved via
     `vkGetInstanceProcAddr` only once that succeeds, never assumed
     statically linkable; the instance-level query mechanism and the
     device-level core-1.3-vs-`VK_KHR_dynamic_rendering` capability kept
     as two explicitly distinct layers; the Vulkan Backend's overall
     minimum supported API version left unraised; a device with neither
     path available returning the existing
     `DeviceCreateError::DynamicRenderingUnavailable`, never a crash or a
     `VkRenderPass`/`VkFramebuffer` fallback.
  4. **`Mesh`/`Material` as caller-owned aggregates and `Renderer` as a
     stateless, non-caching orchestrator** (§11) — `createMesh()`/
     `createMaterial()` as free functions, never `Renderer` methods;
     `Renderer` retaining no GPU resource or frame-to-frame state across
     calls; the depth `Texture`'s resize responsibility and the
     `Pipeline`'s format-change responsibility both sitting with the
     caller, never `Renderer`.
  5. **This round's vertex/index/uniform `Buffer`s never declared as
     RenderGraph logical resources** (§6) — confirmed as correct given
     they are host-write/device-read-only this round with write-before-
     read already guaranteed by construction, not a bypass of the
     RenderGraph-mandatory-path rule; explicitly scoped as a conclusion
     that would not extend to a future GPU-written buffer.
  6. **The direct, unpooled, per-resource GPU memory allocation policy**
     (§9), **the create-before-destroy format-change/extent-change
     rebuild discipline** (§13), and **the single-frame-in-flight-scoped
     design of every synchronization argument this Plan makes** (§10's
     descriptor-binding write-timing note, §13's camera-buffer write-
     timing note) — all confirmed as correct within, and explicitly not
     generalized beyond, this round's own scope.

  **Implementation is authorized by this approval, but must not begin
  until this Plan's own PR has merged into `main`** — per
  [AGENTS.md](../AGENTS.md)'s Spec → Plan → Human Review → Implementation
  ordering, the implementation branch is created from `main` *after* that
  merge, not from this Plan's own branch (`plan/0007-minimal-renderer`).

## Objective

Turn Spec 0007's approved contract — a real, visible, depth-tested mesh
drawn through `Renderer` → RenderGraph → RHI → Vulkan Backend — into an
ordered, reviewable implementation plan. This plan's C++ signatures,
algorithms, and file layout (§2–§13) have been reviewed and approved by
the joint Spec 0007 + Plan 0007 Human Review recorded above —
implementation follows them as written; per
[AGENTS.md](../AGENTS.md), any place reality forces a deviation during
Implementation must be called out explicitly in the PR, not silently
drifted from.

## Authoritative Sources

Read in full before this Plan was drafted: Spec 0007; ADR-0022–0027;
Spec/Plan 0003; Spec/Plan 0005; Spec/Plan 0006; ADR-0001–0021;
`docs/architecture/{overview,module_boundaries,threading,resource_lifetime}.md`;
`docs/process/{testing-strategy,definition-of-done}.md`;
`specs/README.md`; `docs/project-blueprint.md`; the current `src/core/`,
`src/rhi/`, `src/vulkan_backend/`, `src/render_graph/` source (including
`src/vulkan_backend/src/resource_state_mapping.cpp`,
`src/vulkan_backend/src/vulkan_instance.cpp`,
`src/vulkan_backend/src/vulkan_device.cpp`,
`src/vulkan_backend/include/atlantis/vulkan_backend/vulkan_backend.h`),
tests, `examples/frame_execution_demo/main.cpp`, and every `CMakeLists.txt`
under `src/`, `tests/`, `examples/`, plus `cmake/CompilerWarnings.cmake`
and `cmake/AtlantisDependencies.cmake` (read directly, not summarized
from memory).

## Critical Architectural Boundaries (preserved, not re-decided here)

- No `Vk*` type or Vulkan header outside `src/vulkan_backend/` (ADR-0001).
- RHI's public interfaces are abstract C++ base classes held behind
  `std::unique_ptr`, constructed only via `Device::create*()` factory
  methods for resources, and via Vulkan Backend's free `createDevice()`/
  `createPresentation()` factory functions for `Device`/`Presentation`
  themselves (ADR-0014) — this Plan extends the established
  `Device::create*() -> Result<unique_ptr<Interface>, Error>` pattern
  (already used for `createCommandList()`) to `Buffer`, `Texture`, and
  `Pipeline`, rather than inventing a second mechanism.
- `src/renderer/` depends only on `Atlantis::RHI`, `Atlantis::RenderGraph`,
  `Atlantis::Core` — never Platform, Vulkan Backend, Win32, or any `Vk*`
  type (ADR-0001, ADR-0022).
- `Renderer` never owns a `RenderTarget`, a depth `Texture`, a `Mesh`, or
  a `Material` across frames — every per-frame input is a borrowed
  reference (ADR-0003, ADR-0022).
- Single Phase 1 logical frame thread; nothing introduced here is
  thread-safe for concurrent access (ADR-0004).
- RenderGraph is the mandatory, sole path for recorded GPU work — no
  direct-submission bypass (AGENTS.md Golden Rule; ADR-0021). RenderGraph
  records but never submits or presents (ADR-0021, unchanged).
- Spec 0005's single-producer resource model, dependency derivation,
  cycle detection, and deterministic ordering (ADR-0017, ADR-0018) are
  **unchanged** — the depth attachment's combined read/write access is
  expressed as **exactly one** `writes()` usage, never a paired
  `reads()` + `writes()` on the same pass (ADR-0018's existing rule,
  unmodified, rejects that at declaration time).
- `ResourceState::ColorAttachmentWrite` (Spec 0006/ADR-0020) keeps its
  existing meaning and Vulkan Backend mapping
  (`VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`, per
  `resource_state_mapping.cpp`) exactly as shipped — this Plan never
  reuses it for the new draw pass's color output (ADR-0025, ADR-0026).
- The Vulkan Backend's overall minimum supported API version is **not**
  raised to 1.3 — dynamic rendering is adopted via a capability-detected
  dual path (ADR-0024).
- `Pipeline` fixes its target color/depth attachment *formats* at
  creation; attachment format change is the caller's explicit
  responsibility, never `Renderer`'s (ADR-0022, ADR-0025).
- `ATLANTIS_CHECK`/`ATLANTIS_ASSERT` for programmer errors, `Result<T,E>`
  for recoverable errors, no exceptions (ADR-0009, AGENTS.md).
- No general GPU memory suballocation strategy — each `Buffer`/`Texture`
  gets its own individual `vkAllocateMemory`/`vkFreeMemory` call,
  strictly confined to the Vulkan Backend's private implementation
  (ADR-0023).
- No shader compiler, reflection, or caching is invoked by any Atlantis
  build target or source file (ADR-0027).

## Non-Goals (confirmed matching Spec 0007)

Shader System, Runtime, Android, iOS, headless rendering, image
regression testing, scene graph/ECS/asset system/model loader, multiple
materials, lighting/shadows/texturing, GPU-driven rendering, bindless/
instanced/indirect draw, hot-reload, multiple frames in flight,
multi-threading, a general GPU memory suballocator (VMA or hand-rolled),
a second graphics backend, a general descriptor-set/bindless system, and
cross-owner shared ownership of any GPU resource this Plan introduces.
This Plan does not add a third-party dependency, does not touch
`docs/project-blueprint.md`/`specs/README.md`, and does not reopen any
`Accepted` ADR's conclusions.

## Candidate-API Status

Every C++ signature, file name, and algorithm in §2–§13 was a **Plan-
stage candidate** per AGENTS.md and this repository's own precedent
(Plan 0005's and Plan 0006's identical disclaimers) — naming that
explicitly, while this Plan was still `Draft`, was not a hedge against
review; it was what made this document reviewable as a plan rather than
a fait accompli. The joint Spec 0007 + Plan 0007 Human Review recorded
above has since reviewed and approved this Plan in full: Implementation
now follows §2–§13 exactly as written, per AGENTS.md's explicit-deviation
rule — any place reality forces a deviation during Implementation must
be called out explicitly in the implementation PR, not silently drifted
from or decided unilaterally.

---

## 1. Module and CMake Target Boundaries

**One new module** (`src/renderer/`, target `atlantis_renderer`, alias
`Atlantis::Renderer`), extending three existing modules (`atlantis_rhi`,
`atlantis_vulkan_backend`, `atlantis_render_graph`). No new third-party
dependency; no new top-level CMake option.

### Files to Create

```
src/renderer/include/atlantis/renderer/mesh.h        # Mesh, createMesh()
src/renderer/include/atlantis/renderer/material.h     # Material, createMaterial()
src/renderer/include/atlantis/renderer/draw_item.h     # DrawItem
src/renderer/include/atlantis/renderer/renderer.h      # Renderer
src/renderer/src/mesh.cpp
src/renderer/src/material.cpp
src/renderer/src/renderer.cpp
src/renderer/CMakeLists.txt

src/rhi/include/atlantis/rhi/buffer.h                  # Buffer (abstract), BufferPurpose, BufferCreateParams
src/rhi/include/atlantis/rhi/texture.h                 # Texture (abstract), TextureCreateParams
src/rhi/include/atlantis/rhi/pipeline.h                 # Pipeline (abstract), PipelineCreateParams, VertexInputLayout

src/vulkan_backend/src/vulkan_buffer.h/.cpp
src/vulkan_backend/src/vulkan_texture.h/.cpp
src/vulkan_backend/src/vulkan_pipeline.h/.cpp
src/vulkan_backend/src/vulkan_memory.h/.cpp             # selectMemoryTypeIndex(), allocateAndBindMemory() -- shared by Buffer/Texture
src/vulkan_backend/src/dynamic_rendering.h/.cpp          # decideDynamicRenderingPath() (pure) + the real capability-query wrapper

tests/rhi/buffer_texture_pipeline_tests.cpp             # GPU-independent: param validation, error-enum sanity
tests/render_graph/attachment_execution_tests.cpp        # GPU-independent: multi-binding execute() extension (fake CommandList)
tests/vulkan_backend/dynamic_rendering_tests.cpp          # GPU-independent: decideDynamicRenderingPath() truth table
tests/vulkan_backend/vulkan_memory_tests.cpp              # GPU-independent: selectMemoryTypeIndex() over synthetic VkPhysicalDeviceMemoryProperties
tests/vulkan_backend/minimal_renderer_gpu_tests.cpp        # GPU-required: real Buffer/Texture/Pipeline/draw path
tests/renderer/renderer_ownership_tests.cpp                # GPU-independent: Renderer statelessness, Mesh/Material ownership (compile-time + fake-CommandList checks)
tests/renderer/CMakeLists.txt

shaders/minimal_renderer/minimal_mesh.vert.glsl          # human-readable source (checked in, never built)
shaders/minimal_renderer/minimal_mesh.frag.glsl
shaders/minimal_renderer/minimal_mesh.vert.spv            # pre-compiled bytecode (checked in, never built)
shaders/minimal_renderer/minimal_mesh.frag.spv
shaders/minimal_renderer/README.md                        # exact compiler/version/command-line note (ADR-0027)
                                                             # No dedicated "locate my own executable" helper file --
                                                             #   each consumer's own loadSpirvFile() (Section 12) opens
                                                             #   a plain relative path, platform-API-free.

examples/minimal_renderer_demo/CMakeLists.txt
examples/minimal_renderer_demo/main.cpp
```

### Files to Modify

```
src/rhi/include/atlantis/rhi/types.h        # + ResourceState::ColorAttachmentOutput/DepthAttachmentReadWrite,
                                             #   DepthFormat, VertexAttributeFormat, BufferCreateError,
                                             #   TextureCreateError, PipelineCreateError
src/rhi/include/atlantis/rhi/device.h       # + createBuffer(), createTexture(), createPipeline()
src/rhi/include/atlantis/rhi/command_list.h # + bindPipeline/bindVertexBuffer/bindIndexBuffer/bindUniformBuffer/
                                             #   pushConstant/drawIndexed/beginRendering/endRendering
src/rhi/CMakeLists.txt                      # + 3 new headers

src/vulkan_backend/include/atlantis/vulkan_backend/vulkan_backend.h  # + DeviceCreateError::DynamicRenderingUnavailable
src/vulkan_backend/src/vulkan_instance.cpp   # apiVersion request unchanged (stays VK_API_VERSION_1_0); + query/
                                             #   enable VK_KHR_get_physical_device_properties2 as an instance
                                             #   extension, + resolve vkGetPhysicalDeviceFeatures2KHR via
                                             #   vkGetInstanceProcAddr once instance creation succeeds -- see §8
src/vulkan_backend/src/vulkan_device.h/.cpp  # + dynamic-rendering capability selection in physical-device
                                             #   loop, feature-chain enablement, createBuffer/createTexture/
                                             #   createPipeline, resolved entry-point storage
src/vulkan_backend/src/vulkan_command_list.h/.cpp  # + bind/push-constant/draw/begin-end-rendering bodies
src/vulkan_backend/src/resource_state_mapping.h/.cpp  # + ColorAttachmentOutput/DepthAttachmentReadWrite transition table rows
src/vulkan_backend/src/vulkan_result.h/.cpp   # + toBufferCreateError/toTextureCreateError/toPipelineCreateError
src/vulkan_backend/CMakeLists.txt             # + 5 new .cpp files

src/render_graph/include/atlantis/render_graph/execution.h  # ResourceBinding gains depthTexture/colorClear/depthClear fields
src/render_graph/src/execution.cpp            # multi-binding transition bookkeeping, draw-pass recognition,
                                             #   begin/end-rendering insertion
src/render_graph/CMakeLists.txt               # unaffected (no new source file; execution.cpp already exists)

tests/rhi/CMakeLists.txt                      # + buffer_texture_pipeline_tests.cpp
tests/rhi/types_tests.cpp                     # + new enum/state sanity cases
tests/render_graph/CMakeLists.txt             # + attachment_execution_tests.cpp
tests/render_graph/execution_tests.cpp        # unaffected structurally (existing cases stay valid --
                                             #   ColorAttachmentWrite-only pass still never draw-pass-recognized)
tests/vulkan_backend/CMakeLists.txt           # + 4 new source files across the two existing targets
                                             #   (dynamic_rendering_tests.cpp, vulkan_memory_tests.cpp ->
                                             #   atlantis_vulkan_backend_tests; minimal_renderer_gpu_tests.cpp
                                             #   -> atlantis_vulkan_backend_gpu_tests)

CMakeLists.txt (root)                         # + add_subdirectory(src/renderer);
                                             #   + add_subdirectory(tests/renderer) under ATLANTIS_BUILD_TESTS;
                                             #   + add_subdirectory(examples/minimal_renderer_demo) under
                                             #     ATLANTIS_BUILD_EXAMPLES
```

No file under `src/shader_system/`, `src/runtime/`, or any Android/iOS
path is created or modified. No `vcpkg`/dependency-fetching CMake file is
changed beyond adding the new subdirectories above.

**Renderer's link boundary** (`src/renderer/CMakeLists.txt`):

```cmake
add_library(atlantis_renderer STATIC
  src/mesh.cpp
  src/material.cpp
  src/renderer.cpp
)
add_library(Atlantis::Renderer ALIAS atlantis_renderer)

target_include_directories(atlantis_renderer
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_renderer
  PUBLIC
    Atlantis::Core
    Atlantis::RHI
    Atlantis::RenderGraph
  PRIVATE
    atlantis_compiler_warnings
)
# Deliberately absent: Atlantis::Platform, Atlantis::VulkanBackend,
# Vulkan::Vulkan -- linking any of these would itself be an
# Acceptance-Criteria violation (Spec 0007), not merely undesirable.
```

---

## 2. RHI Candidate API — Type Additions

Added to `src/rhi/include/atlantis/rhi/types.h`:

```cpp
// ResourceState gains two new variants, deliberately distinct in name
// and Vulkan Backend mapping from ColorAttachmentWrite (Spec 0006) --
// see ADR-0025's Decision for why reusing ColorAttachmentWrite here
// would be a genuine layout-correctness bug, confirmed against
// resource_state_mapping.cpp's actual shipped mapping.
enum class ResourceState {
  Undefined,
  ColorAttachmentWrite,       // unchanged (Spec 0006) -- clearColor()'s transfer-dst state, never reused here
  PresentSource,               // unchanged (Spec 0006)
  ColorAttachmentOutput,       // new -- real graphics-pipeline color-attachment-output write
  DepthAttachmentReadWrite,    // new -- depth-test read + depth-write, single writes() usage (ADR-0026)
};

// This round's one depth format. A single-variant enum, not a bare
// bool/constant, so a future spec adding a second depth format (e.g.
// with stencil) extends this enum rather than replacing a non-enum
// representation.
enum class DepthFormat {
  D32Sfloat,  // VK_FORMAT_D32_SFLOAT -- guaranteed VK_IMAGE_TILING_OPTIMAL depth-attachment support
              // per the Vulkan spec's mandatory format support table; no capability query needed.
};

// This round's vertex attributes (position, one per-vertex color
// attribute -- Spec 0007 Risks & Open Questions) are both a 3-float
// vector; this enum exists so VertexInputLayout (below) does not
// silently assume a single hardcoded format.
enum class VertexAttributeFormat {
  Float3,
};

enum class BufferPurpose {
  Vertex,
  Index,
  Uniform,
};

struct BufferCreateParams {
  BufferPurpose purpose;
  std::size_t sizeBytes = 0;
};

struct TextureCreateParams {
  Extent2D extent;
  DepthFormat format = DepthFormat::D32Sfloat;  // this round's only Texture usage is a depth attachment
};

struct VertexAttribute {
  std::uint32_t location = 0;
  std::uint32_t offsetBytes = 0;
  VertexAttributeFormat format = VertexAttributeFormat::Float3;
};

struct VertexInputLayout {
  std::uint32_t strideBytes = 0;
  std::vector<VertexAttribute> attributes;  // this round: exactly 2 (position @0, color @1)
};

// RHI does not parse, validate, or reflect this -- see ADR-0027. A
// non-owning view; the caller (Material construction) owns the actual
// byte storage (loaded once from a checked-in .spv file) for at least
// as long as this call.
struct ShaderStageBytecode {
  const std::uint32_t* spirvWords = nullptr;  // SPIR-V's own natural 32-bit-word granularity
  std::size_t wordCount = 0;
};

struct PipelineCreateParams {
  ShaderStageBytecode vertexShader;
  ShaderStageBytecode fragmentShader;
  VertexInputLayout vertexInputLayout;
  Format colorFormat = Format::Unknown;          // matches the bound RenderTarget's format at Material construction time
  DepthFormat depthFormat = DepthFormat::D32Sfloat;
  std::size_t pushConstantSizeBytes = 0;          // this round: sizeof(float) * 16 (one 4x4 matrix)
};

enum class BufferCreateError {
  AllocationFailed,
  BufferCreationFailed,
};

enum class TextureCreateError {
  AllocationFailed,
  ImageCreationFailed,
  ImageViewCreationFailed,
};

enum class PipelineCreateError {
  ShaderModuleCreationFailed,
  DescriptorSetLayoutCreationFailed,
  DescriptorSetAllocationFailed,  // vkAllocateDescriptorSets against VulkanDevice's fixed-capacity
                                   // pool -- see Section 10's camera-uniform-binding design for why
                                   // this is a distinct enumerator, not folded into PipelineCreationFailed
  PipelineLayoutCreationFailed,
  PipelineCreationFailed,
};
```

**Why three distinct `*CreateError` enums, not one shared
`ResourceCreateError`:** mirrors this codebase's existing precedent
(`CommandListCreateError` distinct from `SubmitError`, Spec 0006) — each
creation call's caller switches on exactly the failure modes relevant to
it, not a shared enum with cases that can never apply to a given call.

**No `VertexAttributeFormat` beyond `Float3` this round** — deliberately
narrow, matching Spec 0007's own minimal-material scope; a future spec
adding a second attribute type (e.g. `Float2` for UVs) extends this enum.

## 3. RHI Candidate API — `Buffer`, `Texture`, `Pipeline`

`src/rhi/include/atlantis/rhi/buffer.h`:

```cpp
#pragma once

#include <cstddef>

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A GPU-visible linear memory region, fixed to one of three purposes at
// creation (ADR-0023). This round, every Buffer is host-visible and
// host-coherent regardless of purpose -- see mappedData()'s own
// contract and ADR-0023's Decision for why (no staging/upload path this
// round). Move-only, single-owner, held behind std::unique_ptr<Buffer>
// (ADR-0014's mechanism, ADR-0023). Not internally thread-safe;
// caller-thread-only (ADR-0004). No hidden cache: Device does not
// deduplicate or retain a reference to any Buffer it creates (ADR-0003).
class Buffer {
 public:
  virtual ~Buffer() = default;

  [[nodiscard]] virtual BufferPurpose purpose() const = 0;
  [[nodiscard]] virtual std::size_t sizeBytes() const = 0;

  // A pointer to this Buffer's host-visible, host-coherent memory,
  // valid for this Buffer's whole lifetime (mapped once, at
  // construction -- never remapped). The caller may write directly at
  // any time; no explicit flush/invalidate call is required (host-
  // coherent). Writing to a Uniform-purpose Buffer while GPU work from
  // a prior frame might still read it is a caller precondition
  // violation -- see Section 7's write-timing contract, which this
  // round's single-frame-in-flight discipline satisfies structurally
  // for the one caller pattern this spec uses (write once per frame,
  // immediately after acquireNextTarget() returns).
  [[nodiscard]] virtual void* mappedData() = 0;
};

}  // namespace atlantis::rhi
```

`src/rhi/include/atlantis/rhi/texture.h`:

```cpp
#pragma once

#include <atlantis/rhi/types.h>

namespace atlantis::rhi {

// A GPU image used, this round, exclusively as a depth attachment
// (ADR-0023) -- no sampled/shader-read usage, no mipmaps. Move-only,
// single-owner, held behind std::unique_ptr<Texture>. Owned and
// resize-recreated by the caller, never by Renderer or Presentation
// (ADR-0022). Not internally thread-safe; caller-thread-only (ADR-0004).
class Texture {
 public:
  virtual ~Texture() = default;

  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual DepthFormat format() const = 0;
};

}  // namespace atlantis::rhi
```

`src/rhi/include/atlantis/rhi/pipeline.h`:

```cpp
#pragma once

namespace atlantis::rhi {

// A fixed graphics pipeline: one vertex + one fragment stage, a fixed
// vertex-input layout, depth-test/depth-write enabled, opaque
// rasterization, dynamic viewport/scissor (ADR-0025 -- so this object
// survives a resize without recreation), and color/depth attachment
// formats fixed at creation via dynamic-rendering pipeline info, never a
// VkRenderPass (ADR-0024). Move-only, single-owner, held behind
// std::unique_ptr<Pipeline>, owned exclusively by one Material
// (ADR-0022). If the bound color/depth format ever changes (ADR-0025's
// format-change contract), the *caller* destroys and recreates this
// object -- Pipeline itself has no "update format" method. Opaque: no
// accessor beyond the destructor -- CommandList::bindPipeline() takes it
// by reference and never needs to introspect it (mirrors
// SubmissionSignal's "intentionally declares no method" precedent, Spec
// 0006). Not internally thread-safe; caller-thread-only (ADR-0004).
class Pipeline {
 public:
  virtual ~Pipeline() = default;
};

}  // namespace atlantis::rhi
```

## 4. RHI Candidate API — `Device` Extensions

`src/rhi/include/atlantis/rhi/device.h` gains:

```cpp
[[nodiscard]] virtual atlantis::Result<std::unique_ptr<Buffer>, BufferCreateError>
createBuffer(const BufferCreateParams& params) = 0;

[[nodiscard]] virtual atlantis::Result<std::unique_ptr<Texture>, TextureCreateError>
createTexture(const TextureCreateParams& params) = 0;

[[nodiscard]] virtual atlantis::Result<std::unique_ptr<Pipeline>, PipelineCreateError>
createPipeline(const PipelineCreateParams& params) = 0;
```

Each is a stateless factory call — `Device` does not retain a reference
to any `Buffer`/`Texture`/`Pipeline` it creates (ADR-0003, ADR-0023).

`src/vulkan_backend/include/atlantis/vulkan_backend/vulkan_backend.h`
gains one new `DeviceCreateError` variant (existing four unchanged):

```cpp
enum class DeviceCreateError {
  InstanceCreationFailed,
  ValidationLayerUnavailable,
  NoSuitablePhysicalDevice,
  DeviceCreationFailed,
  DynamicRenderingUnavailable,  // new -- see Section 8
};
```

## 5. RHI Candidate API — `CommandList` Extensions

`src/rhi/include/atlantis/rhi/command_list.h` gains (existing
`transitionResource()`/`clearColor()`, Spec 0006, unchanged):

```cpp
// Attachment scoping (ADR-0024/ADR-0025). depth may be nullptr (no
// depth attachment for this scope) -- this round's own draw pass always
// supplies one, but the type itself does not forbid omitting it.
// Called only by render_graph::execute() (Section 7) -- never by a pass
// execution callback, per ADR-0026.
virtual void beginRendering(RenderTarget& color, Texture* depth,
                             ClearColorValue colorClear, float depthClear) = 0;
virtual void endRendering() = 0;

// Binds the pipeline subsequent draw calls use, until the next
// bindPipeline() or the end of the current attachment scope.
virtual void bindPipeline(Pipeline& pipeline) = 0;

// buffer must have been created with BufferPurpose::Vertex/Index
// respectively -- a mismatched purpose is a programmer error
// (ATLANTIS_CHECK), not a silently accepted call.
virtual void bindVertexBuffer(Buffer& buffer) = 0;
virtual void bindIndexBuffer(Buffer& buffer) = 0;

// buffer must have been created with BufferPurpose::Uniform.
virtual void bindUniformBuffer(Buffer& buffer) = 0;

// Records the per-draw-item object-to-world transform as a Vulkan push
// constant (ADR-0025) -- not a second uniform buffer; see that ADR's
// Decision for the correctness argument (a shared uniform buffer,
// overwritten once per draw item during recording, would corrupt every
// earlier draw item's transform by the time the GPU actually executes
// any of them). sizeBytes must not exceed the bound Pipeline's
// pushConstantSizeBytes.
virtual void pushConstant(const void* data, std::size_t sizeBytes) = 0;

// Records one indexed draw call against whatever pipeline/vertex/index/
// binding state is currently bound.
virtual void drawIndexed(std::uint32_t indexCount) = 0;
```

`command_list.h` gains `#include <atlantis/rhi/buffer.h>` and
`#include <atlantis/rhi/pipeline.h>` (`texture.h` is already reachable
transitively via `types.h`'s `DepthFormat`, but is included directly for
clarity — `Texture` is used by name in `beginRendering()`'s signature).

All of the above remain subject to ADR-0020's existing rule: recording
happens only from inside a RenderGraph pass execution callback (for
`bindPipeline`/`bindVertexBuffer`/`bindIndexBuffer`/`bindUniformBuffer`/
`pushConstant`/`drawIndexed`) or from `render_graph::execute()` itself
(for `beginRendering`/`endRendering`, ADR-0026) — enforced by inspection,
not the type system, exactly as `transitionResource()`/`clearColor()`
already are.

---

## 6. RenderGraph Candidate API — `ResourceBinding` Extension

`src/render_graph/include/atlantis/render_graph/execution.h`'s
`ResourceBinding` (Spec 0006) is extended additively — the existing
`resource`/`target` fields keep their exact name and meaning, so
`examples/frame_execution_demo/main.cpp`'s existing
`ResourceBinding{{resource, &target}}` aggregate-init call site needs no
change:

```cpp
struct ResourceBinding {
  CompiledResourceId resource;
  atlantis::rhi::RenderTarget* target = nullptr;         // unchanged (Spec 0006)
  atlantis::rhi::ClearColorValue colorClear{};             // new -- used only when target != nullptr
  atlantis::rhi::Texture* depthTexture = nullptr;          // new
  float depthClear = 1.0f;                                 // new -- used only when depthTexture != nullptr
};
```

**Exactly one of `target`/`depthTexture` must be non-null per entry** —
a new **Guard 0**, `ATLANTIS_CHECK_MSG`'d at the top of `execute()`,
before Guards 1/2 run (a malformed binding with both null or both
non-null is a programmer error, not a silently-accepted ambiguous case).
**`bindings` must also contain no two entries for the same
`CompiledResourceId`** — a second new check, folded into Guard 0 for
this Plan's own bookkeeping purposes (not a separate ADR-0026 guard,
since Spec 0007/ADR-0026 never contemplated a caller supplying duplicate,
possibly-contradictory bindings for one resource): a duplicate would
otherwise make "which entry does `execute()`'s per-usage binding lookup
find" an unspecified, implementation-order-dependent question — cheap to
check once, up front, and it removes an ambiguity this Plan should not
leave implicit. `execution.h` gains `#include <atlantis/rhi/texture.h>`.

**Why vertex/index/uniform `Buffer`s are never declared as RenderGraph
logical resources — deliberately, not by oversight.** `Renderer::drawFrame()`
(Section 11) captures `Mesh`/`Material`/the camera `Buffer` directly in
its pass's execution-callback closure and calls
`bindVertexBuffer()`/`bindIndexBuffer()`/`bindUniformBuffer()` on them
straight from inside that callback — none of the three is ever
`builder.declareResource()`'d, `reads()`/`writes()`-tagged, or bound via
`ResourceBinding`. This is not a bypass of "all GPU work goes through
RenderGraph" (AGENTS.md's Golden Rule): the *drawing itself*
(`bindPipeline`/`bindVertexBuffer`/`bindIndexBuffer`/`bindUniformBuffer`/
`pushConstant`/`drawIndexed`) still only ever happens from inside a
RenderGraph pass execution callback, invoked by `execute()` at the
correct point in its scheduling algorithm (Section 7's step 4), exactly
like `clearColor()` already does in Spec 0006. What these three
`Buffer`s specifically never need is **`ResourceState`/transition
tracking** — and that omission is deliberate, not silent: every one of
this round's `Buffer`s is written on the CPU side only (never by a GPU
command, this round — see Non-Goals) and read on the GPU side only,
with the write always happening-before the GPU read by construction —
vertex/index data is written exactly once, at `Mesh` construction,
before that `Mesh` is ever used in any `drawFrame()` call; the camera
uniform `Buffer` is written once per frame, by the caller, strictly
before `Renderer::drawFrame()` is even called that frame (Section 13
step 3), relying on the same acquire-time-drain guarantee
([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
PR #24) already established for exactly this write-timing pattern. No
resource-state transition or barrier is ever required for a purely
host-written, device-read-only, always-already-visible-by-submission-
time resource — there is nothing for RenderGraph's dependency model to
usefully schedule. **This is a narrow, spec-scoped conclusion, not a
general rule:** if a future spec ever has a GPU command *write* to a
`Buffer` (e.g. a compute shader, or a staging-buffer upload copy), that
write would need real RenderGraph resource declaration and
`ResourceState` tracking — this Plan does not do that, and does not
claim the conclusion above extends to that case.

## 7. RenderGraph Candidate API — `execute()` Algorithm Extension

Guards, generalized/unchanged per ADR-0026:

- **Guard 0 (new):** every `ResourceBinding` entry has exactly one of
  `target`/`depthTexture` non-null.
- **Guard 1 (unchanged in principle, extended in scope):** every
  resource where `graph.requiresRhiBinding(r)` has a matching entry in
  `bindings`, regardless of whether that entry is a `target` or
  `depthTexture` binding.
- **Guard 2 (unchanged in scope):** for every entry with `target !=
  nullptr`, no declared read usage anywhere in `graph` for that
  resource. **Not checked for `depthTexture` entries** — see ADR-0026's
  Decision for why (depth-test read is legitimate, expressed as part of
  the single `DepthAttachmentReadWrite` write usage, never as a second,
  separate declared read).

**Draw-pass recognition (new):** a pass is a **draw pass** iff any of its
declared usages carries `ResourceState::ColorAttachmentOutput` or
`ResourceState::DepthAttachmentReadWrite` — **and only these two
states**, never `ColorAttachmentWrite` (ADR-0026; this is what keeps
Spec 0006's existing `clearColor()`-only pass, which does declare
`ColorAttachmentWrite`, structurally un-wrapped by this new rule).

### `execute()` algorithm (candidate, `execution.cpp`)

```
0. For each entry b in bindings:
     ATLANTIS_CHECK_MSG((b.target != nullptr) != (b.depthTexture != nullptr),
                         "ResourceBinding must bind exactly one of target/depthTexture")
   ATLANTIS_CHECK_MSG(no two entries in bindings share the same .resource value,
                       "ResourceBinding must not bind the same resource twice")
1. For each resource r where graph.requiresRhiBinding(r):
     ATLANTIS_CHECK_MSG(bindings contains an entry for r,
                         "ResourceState-tagged resource has no binding")
2. For each entry b in bindings where b.target != nullptr:
     ATLANTIS_CHECK_MSG(graph has no read usage anywhere for b.resource,
                         "bound RenderTarget has a declared read usage")
   -- entries with b.depthTexture != nullptr are not checked here (Guard 2 scope)
3. currentState: map<CompiledResourceId, ResourceState>  (empty initially --
   every bound resource, color AND depth, is treated as entering this
   execute() call from ResourceState::Undefined; valid because this
   round's one draw pass unconditionally clears both attachments via
   beginRendering()'s load-op, per ADR-0026's extension of ADR-0019's
   existing RenderTarget-specific rule)
4. for position in [0, graph.passCount()):
     pass = graph.passOrder(position)
     attachmentUsages = usages of pass whose state is ColorAttachmentOutput or DepthAttachmentReadWrite
     isDrawPass = !attachmentUsages.empty()

     for i in [0, graph.usageCount(pass)):
       usage = graph.usage(pass, i)
       if !usage.state.has_value(): continue         // untagged Spec 0005 usage -- no transition
       binding = find in bindings by usage.resource
       if binding not found: continue                 // Guard 1 already reported this; UB-safe skip under
                                                        // a non-terminating handler, mirroring Plan 0006's
                                                        // existing pattern
       resourceObj = binding.target != nullptr ? *binding.target : *binding.depthTexture
       previous = currentState.count(usage.resource) ? currentState[usage.resource] : ResourceState::Undefined
       if previous != *usage.state:
         commandList.transitionResource(resourceObj, previous, *usage.state)   // note: transitionResource()
                                                                                 // widens from RenderTarget&
                                                                                 // to a resource reference --
                                                                                 // see Section 10's note
         currentState[usage.resource] = *usage.state

     if isDrawPass:
       colorBinding = the bindings entry (if any) whose resource has a ColorAttachmentOutput usage in pass
       depthUsagePresent = pass has a usage tagged DepthAttachmentReadWrite
       depthBinding = depthUsagePresent ? the bindings entry whose resource has that usage : n/a

       // UB-safe check-then-skip, mirroring step 4's own binding-not-found handling above and
       // Plan 0006's existing pattern: Guard 1 already ATLANTIS_CHECK_MSG'd that every
       // ResourceState-tagged usage has a binding, but under a non-terminating handler (test),
       // execution continues past that check -- colorBinding/depthBinding must never be
       // dereferenced here without first confirming they were actually found.
       if colorBinding found AND (!depthUsagePresent OR depthBinding found):
         commandList.beginRendering(*colorBinding.target,
                                     depthUsagePresent ? depthBinding.depthTexture : nullptr,
                                     colorBinding.colorClear,
                                     depthUsagePresent ? depthBinding.depthClear : 1.0f)
         if pass has an executeFn: executeFn(commandList)
         commandList.endRendering()
       // else: skip beginRendering/executeFn/endRendering entirely for this pass -- Guard 1's
       // check already reported the missing binding; nothing safe remains to record for it
     else:
       if pass has an executeFn: executeFn(commandList)

5. for each entry b in bindings where b.target != nullptr:
     if currentState.count(b.resource):
       last = currentState[b.resource]
       if last != ResourceState::PresentSource:
         commandList.transitionResource(*b.target, last, ResourceState::PresentSource)
   -- no trailing transition for b.depthTexture entries (ADR-0026 -- never presented, never read back this round)
```

**`beginRendering()`'s `renderArea` precondition:** derived from
`colorBinding.target->extent()`; the depth `Texture`'s own `extent()`
must already match it at this point — guaranteed by the caller's own
resize contract (Section 13), never validated by `beginRendering()`
itself (a caller precondition, same tier as every other cross-object
precondition in this Plan, not a new guaranteed-detectable check).

This satisfies every bullet from Spec 0007's own Testing & Verification
Plan for "RenderGraph multi-attachment and draw-pass execution
integration" (Section 15 below maps each one).

**Note on `transitionResource()`'s signature:** Spec 0006 declared it as
`transitionResource(RenderTarget&, ResourceState, ResourceState)`.
Supporting a `Texture&` argument too requires either (a) an overload
(`transitionResource(Texture&, ResourceState, ResourceState)`), or (b)
widening the parameter to a common base/variant. **Candidate choice: an
overload**, not a widened parameter — this was explicitly anticipated as
a low-cost future change by ADR-0020's own Negative/Trade-offs ("Widening
`transitionResource()`'s parameter type is a natural, low-cost future
change once `Buffer`/`Texture` exist"), and an overload requires no
change to `RenderTarget`'s own existing method, keeping Spec 0006's
`Accepted` surface untouched rather than modified in place.

---

## 8. Vulkan Backend Implementation — Dynamic Rendering Capability Detection

Per ADR-0024's confirmed dual-path decision. New file
`src/vulkan_backend/src/dynamic_rendering.h/.cpp`:

```cpp
namespace atlantis::vulkan_backend::detail {

enum class DynamicRenderingPath { Core, Extension, Unavailable };

// Pure decision function -- GPU-independent, unit-testable with literal
// booleans (tests/vulkan_backend/dynamic_rendering_tests.cpp), no
// Vulkan call inside. Mirrors this codebase's existing
// decideRecreateAction()/decideAcquireAction() extraction pattern.
[[nodiscard]] DynamicRenderingPath decideDynamicRenderingPath(
    bool physicalDeviceProperties2InstanceExtensionAvailable,
                                      // VK_KHR_get_physical_device_properties2 successfully enabled
                                      // at instance creation (see "Instance-level prerequisite"
                                      // below) -- a single, instance-wide fact, computed once,
                                      // never per-candidate-device. If false, none of the four
                                      // arguments below could have been meaningfully queried for
                                      // any physical device, and this function returns Unavailable
                                      // unconditionally, regardless of their values.
    bool apiVersionAtLeast1_3,
    bool coreFeatureSupported,       // VkPhysicalDeviceVulkan13Features::dynamicRendering;
                                      // only meaningful (and only ever queried by the real
                                      // wrapper) when apiVersionAtLeast1_3 is true
    bool extensionAdvertised,        // VK_KHR_dynamic_rendering present in
                                      // vkEnumerateDeviceExtensionProperties
    bool extensionFeatureSupported); // VkPhysicalDeviceDynamicRenderingFeaturesKHR::dynamicRendering;
                                      // only meaningful when extensionAdvertised is true

}  // namespace atlantis::vulkan_backend::detail
```

Decision logic (candidate):

```
if !physicalDeviceProperties2InstanceExtensionAvailable: return Unavailable
if apiVersionAtLeast1_3 && coreFeatureSupported: return Core
if extensionAdvertised && extensionFeatureSupported: return Extension
return Unavailable
```

**Two distinct layers — do not conflate them.** (1) Whether
`VK_KHR_get_physical_device_properties2` is enabled at the *instance*
level is purely a **query-mechanism** concern: it determines whether
`vkGetPhysicalDeviceFeatures2KHR` may be safely called at all, and says
nothing yet about which rendering path any given physical device
actually supports. (2) Whether a given physical device supports the
core-1.3 feature or the `VK_KHR_dynamic_rendering` **device** extension
is the actual **capability** being detected, using the query mechanism
(1) makes available. `decideDynamicRenderingPath()`'s first argument is
layer (1); its remaining four arguments are layer (2) — this Plan keeps
them as five separate, independently-meaningful booleans specifically so
this distinction is visible in the function's own signature, not
collapsed into one combined "is dynamic rendering usable" flag that
would hide *why* a given candidate failed.

**Instance-level prerequisite — resolved once, before any physical-
device query, without raising `VkApplicationInfo::apiVersion`.**
`vulkan_instance.cpp` currently requests `VK_API_VERSION_1_0` (Section 1's
Authoritative Sources) and this Plan does not change that request. The
*core* function `vkGetPhysicalDeviceFeatures2` is only unambiguously
valid to call when the instance itself was created requesting Vulkan
1.1+ — calling it from a `VK_API_VERSION_1_0` instance is, at best,
loader-dependent behavior Validation Layers may legitimately flag.
Raising the instance's requested `apiVersion` to resolve this would risk
exactly the ambiguity Human Review's confirmed decision was written to
avoid (whether a higher *instance* request could be read as
re-introducing a version floor) and, separately, a real (if narrow) risk
of `vkCreateInstance` returning `VK_ERROR_INCOMPATIBLE_DRIVER` on a very
old Vulkan 1.0-only loader. This Plan instead:

1. **Before `vkCreateInstance()`** (inside `vulkan_instance.cpp`'s
   existing instance-creation function, extended — the one file this
   Plan's Authoritative Sources previously, incorrectly, described as
   needing no change): calls `vkEnumerateInstanceExtensionProperties()`
   and checks whether `VK_KHR_get_physical_device_properties2` is
   present in the returned list — `physicalDeviceProperties2InstanceExtensionAvailable`,
   computed exactly once, here, before the instance exists at all.
2. **If available:** adds `VK_KHR_get_physical_device_properties2` to
   `VkInstanceCreateInfo::ppEnabledExtensionNames` alongside whatever
   extensions this repository's existing instance creation already
   enables (e.g. the WSI surface extensions, unchanged). **If
   unavailable:** the extension is simply not requested — `vkCreateInstance()`
   itself is entirely unaffected either way; only the later capability
   query (below) is gated by this fact.
3. **Immediately after `vkCreateInstance()` succeeds, if the extension
   was enabled:** resolves `vkGetPhysicalDeviceFeatures2KHR`'s function
   pointer explicitly via `vkGetInstanceProcAddr(instance,
   "vkGetPhysicalDeviceFeatures2KHR")`, stored once for the instance's
   whole lifetime — **never assumed to be directly linkable, and never
   called if this resolution step was skipped or returned `nullptr`.**
   This mirrors exactly the same "resolve a `KHR` entry point via
   `vkGet*ProcAddr` rather than assuming static linkage" discipline
   already applied to the device-level
   `vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR` resolution below — the
   instance-level and device-level `KHR` entry points are resolved the
   same way, for the same reason, at their own respective points in
   `Device` construction.
4. `physicalDeviceProperties2InstanceExtensionAvailable` (step 1's
   boolean, narrowed to also require step 3's function-pointer resolution
   to have actually succeeded) is then passed, **unchanged, to every
   physical-device candidate** in the selection loop below — it is an
   instance-wide fact, never re-queried or re-resolved per candidate.

If `VK_KHR_get_physical_device_properties2` is unavailable (or its entry
point fails to resolve), `decideDynamicRenderingPath()` returns
`Unavailable` for every candidate without attempting any
`vkGetPhysicalDeviceFeatures2KHR` call at all — this is not a distinct
failure mode from "the device itself lacks dynamic rendering," it folds
into the same `DeviceCreateError::DynamicRenderingUnavailable` outcome
(below), because from the caller's perspective both mean the same
actionable thing: this environment cannot serve this spec's rendering
path.

**Real capability query wrapper** (inside `vulkan_device.cpp`'s existing
physical-device selection loop, extended — not replaced): for each
physical-device candidate that already passes the pre-existing suitability
criteria (queue families, etc., unchanged from Spec 0003), additionally:

1. `vkGetPhysicalDeviceProperties()` → `apiVersionAtLeast1_3 =
   (properties.apiVersion >= VK_API_VERSION_1_3)`. (This physical-device-
   reported version is independent of the instance's own requested
   version — a 1.0-requesting instance can still enumerate and query a
   physical device that itself reports a higher `apiVersion`.)
2. If `physicalDeviceProperties2InstanceExtensionAvailable` (the same
   instance-wide value for every candidate) **and** `apiVersionAtLeast1_3`:
   call the resolved `vkGetPhysicalDeviceFeatures2KHR` function pointer
   with a `VkPhysicalDeviceVulkan13Features` chained into `pNext` →
   `coreFeatureSupported`. Otherwise `coreFeatureSupported` stays `false`
   and this call is skipped entirely — never attempted when the function
   pointer was never resolved.
3. `vkEnumerateDeviceExtensionProperties()` → `extensionAdvertised =
   ` (`VK_KHR_dynamic_rendering` present in the returned list). This
   call needs no instance extension of its own — device-extension
   enumeration is core Vulkan 1.0 functionality.
4. If `physicalDeviceProperties2InstanceExtensionAvailable` **and**
   `extensionAdvertised`: call the same resolved
   `vkGetPhysicalDeviceFeatures2KHR` function pointer with a
   `VkPhysicalDeviceDynamicRenderingFeaturesKHR` chained into `pNext` →
   `extensionFeatureSupported`. Otherwise skipped, same reasoning as
   step 2.
5. Call `decideDynamicRenderingPath()` with all five values (the
   instance-wide availability boolean plus the four per-candidate
   values above).
6. If `Core` or `Extension`: this candidate is accepted; record the
   resolved path on the (soon-to-be-constructed) `VulkanDevice` instance.
   If `Unavailable`: this candidate is otherwise-suitable but rejected
   specifically for this reason — remember that fact (a single boolean,
   `foundSuitableExceptDynamicRendering`) and continue to the next
   candidate.

**After the loop:** if no candidate was accepted:
`foundSuitableExceptDynamicRendering` being `true` at any point during
the loop → `Result::Err(DeviceCreateError::DynamicRenderingUnavailable)`;
otherwise (no candidate met even the pre-existing criteria, unchanged
from Spec 0003) → `Result::Err(DeviceCreateError::NoSuitablePhysicalDevice)`,
exactly as today. **No `VkRenderPass`/`VkFramebuffer` fallback exists on
either error path** (ADR-0024).

**Device creation, once a candidate with `Core` or `Extension` is
accepted:**

- `Core`: chain `VkPhysicalDeviceVulkan13Features{.dynamicRendering =
  VK_TRUE}` into `VkDeviceCreateInfo::pNext`. Resolved entry points:
  `vkCmdBeginRendering`/`vkCmdEndRendering` (core, linked directly via
  the Vulkan loader/SDK — no `vkGetDeviceProcAddr` resolution needed).
- `Extension`: add `VK_KHR_dynamic_rendering` to the enabled device
  extension list; chain `VkPhysicalDeviceDynamicRenderingFeaturesKHR{
  .dynamicRendering = VK_TRUE}` into `VkDeviceCreateInfo::pNext`.
  Resolved entry points: `vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR`,
  loaded via `vkGetDeviceProcAddr(device, "vkCmdBeginRenderingKHR")` (and
  the `End` equivalent) once, at `VulkanDevice` construction, stored as
  member function pointers.

`VulkanDevice` gains a small internal struct/pair of function pointers
(populated at construction, used by `VulkanCommandList::beginRendering()`/
`endRendering()`, Section 10) — **never exposed on `VulkanDevice`'s own
public accessor surface**, since no RHI-facing or even Vulkan-Backend-
public code outside `VulkanCommandList`'s own implementation needs to
know which path was resolved.

**Windows test coverage / future Android boundary:** this Plan's own GPU
tests (§15) exercise whichever single path the test machine's actual
GPU/driver reports (§15 states this limitation explicitly, per Spec
0007's own Testing & Verification Plan). The capability-query wrapper
itself makes no Windows-specific Vulkan call — `vkGetPhysicalDeviceFeatures2KHR`,
`vkEnumerateDeviceExtensionProperties`, `vkEnumerateInstanceExtensionProperties`,
and the two feature-chain structs are all core/WSI-independent Vulkan
API, so this Plan's implementation is expected to require no changes for
a future Android Platform/Vulkan Backend spec to reuse directly — that
future spec still owns its own decision about Android's actual
device/driver support distribution, per ADR-0024's own Boundary note.

## 9. Vulkan Backend Implementation — GPU Memory Allocation

New file `src/vulkan_backend/src/vulkan_memory.h/.cpp`, shared by
`VulkanBuffer`/`VulkanTexture`'s construction (per ADR-0023's direct,
unpooled, per-resource policy):

```cpp
namespace atlantis::vulkan_backend::detail {

// Pure decision function -- GPU-independent, unit-testable against a
// synthetic VkPhysicalDeviceMemoryProperties value with no real device
// (tests/vulkan_backend/vulkan_memory_tests.cpp). Standard Vulkan idiom:
// scans memoryProperties.memoryTypes for the first entry whose bit is
// set in typeFilterBits and whose propertyFlags fully contain
// requiredProperties. Returns an empty optional if none matches (the
// real caller maps that to BufferCreateError::AllocationFailed /
// TextureCreateError::AllocationFailed).
[[nodiscard]] std::optional<std::uint32_t> selectMemoryTypeIndex(
    const VkPhysicalDeviceMemoryProperties& memoryProperties,
    std::uint32_t typeFilterBits,
    VkMemoryPropertyFlags requiredProperties);

}  // namespace atlantis::vulkan_backend::detail
```

**Allocation sequence** (identical shape for `VulkanBuffer` and
`VulkanTexture`'s construction, each its own independent call —
`vulkan_buffer.cpp`/`vulkan_texture.cpp` each call this shared helper,
not a shared allocation object):

```
1. vkCreateBuffer()/vkCreateImage() -- check VkResult
2. vkGetBufferMemoryRequirements()/vkGetImageMemoryRequirements()
3. selectMemoryTypeIndex(memoryProperties, requirements.memoryTypeBits,
                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)   -- Buffer only, all 3 purposes this round;
                                                                       Texture (depth) uses
                                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT instead
                                                                       (no host access to a depth attachment is
                                                                       needed or provided this round)
   -- ATLANTIS_CHECK / Err(AllocationFailed) if none found
4. vkAllocateMemory() -- one individual allocation, this resource's own
   VkDeviceMemory, no suballocation -- check VkResult
5. vkBindBufferMemory()/vkBindImageMemory() -- check VkResult
6. Buffer only: vkMapMemory() once, for the whole VkDeviceMemory's
   lifetime -- store the returned pointer as mappedData()'s backing
   value; never vkUnmapMemory()'d until destruction (host-coherent
   memory needs no explicit flush, so a persistent map costs nothing and
   avoids repeated map/unmap calls per write)
7. Texture only: vkCreateImageView() for the depth attachment's
   VK_IMAGE_ASPECT_DEPTH_BIT view -- check VkResult
```

**Why `HOST_COHERENT` is a required, not merely preferred, property:**
`selectMemoryTypeIndex()`'s `requiredProperties` argument for every
`Buffer` purpose is `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` — both bits are *required*, not a
preference the caller falls back from. This is what structurally
excludes a host-visible-but-non-coherent memory type from ever being
selected: either a coherent type is found (the only path this Plan
implements — no `vkFlushMappedMemoryRanges`/`vkInvalidateMappedMemoryRanges`
call exists anywhere in this Plan's design) or none is found and
`Result::Err(BufferCreateError::AllocationFailed)` is returned. A
non-coherent memory type is therefore never silently used incorrectly —
it is simply never a candidate `selectMemoryTypeIndex()` can return for
a `Buffer` this round.

**No manual alignment arithmetic is needed.** Each `VkDeviceMemory`
allocation exactly backs one resource, at offset 0, sized to
`requirements.size` (which already accounts for `requirements.alignment`
internally, per the Vulkan spec's own guarantee for a single, whole-
allocation bind) — alignment math only becomes necessary when
suballocating multiple resources into one shared `VkDeviceMemory` block,
which this policy (ADR-0023) never does.

**Partial-construction-failure safety** — mirrors this codebase's
existing two-phase-construction-guard pattern (`vulkan_device.cpp`'s
`createDevice()`, `vulkan_presentation.cpp`'s `SwapchainGuard`) exactly:
if `vkAllocateMemory` (step 4) fails after `vkCreateBuffer`/`vkCreateImage`
(step 1) already succeeded, the already-created `VkBuffer`/`VkImage` is
destroyed before returning `Err` — never leaked. If `vkBindBufferMemory`/
`vkBindImageMemory` (step 5) fails after allocation (step 4) already
succeeded, both the memory and the buffer/image are destroyed/freed
before returning `Err`. Only once every step through image-view creation
(Texture, step 7) succeeds does `VulkanBuffer`/`VulkanTexture`'s
constructor take final, unconditional ownership of every handle.

**Destruction** (each resource's own destructor, RAII, the same guard
pattern, now unconditional since construction already succeeded):
`vkDestroyImageView()` (Texture only) → `vkFreeMemory()` (this
resource's own individual allocation, `vkMapMemory()`'d Buffer memory
needs no explicit unmap before free — `vkFreeMemory()` implicitly
unmaps) → `vkDestroyBuffer()`/`vkDestroyImage()`.

**Concrete resource-count bound, per ADR-0023:** this Plan's own
implementation creates, at most, one vertex `Buffer`, one index
`Buffer`, one uniform `Buffer`, and one depth `Texture` per `Mesh`/
`Material`/verification-composition-instance — plus one additional depth
`Texture` allocation per interactive resize (old destroyed, new
created). Nowhere near a typical driver's `maxMemoryAllocationCount`
floor (commonly 4096+). No pooling, no VMA, no staging buffer, no
implicit cache — **this is not a general allocator decision**; the
migration trigger (a future spec needing enough concurrent resources to
approach that limit, or a real performance need) is unchanged from
ADR-0023's own stated boundary.

---

## 10. Vulkan Backend Implementation — `VulkanBuffer`/`VulkanTexture`/`VulkanPipeline`/`VulkanCommandList` Draw Path

`VulkanBuffer final : public rhi::Buffer` (`vulkan_buffer.h/.cpp`):
holds `VkBuffer`, `VkDeviceMemory`, the persistently-mapped `void*`,
`BufferPurpose`, `sizeBytes`. Constructed only via
`VulkanDevice::createBuffer()`.

`VulkanTexture final : public rhi::Texture` (`vulkan_texture.h/.cpp`):
holds `VkImage`, `VkDeviceMemory`, `VkImageView` (depth aspect),
`Extent2D`, `DepthFormat`. Constructed only via
`VulkanDevice::createTexture()`.

### Camera uniform binding — full candidate design (not left to Implementation)

`CommandList::bindUniformBuffer(Buffer&)` (RHI-public, Section 5) is
this Plan's only new RHI-visible surface for the camera uniform binding
— no `UniformBinding`-shaped abstraction, no general descriptor-set API
of any kind is introduced or exposed. Everything below is Vulkan-Backend-
private, never referenced outside `vulkan_pipeline.{h,cpp}` and
`vulkan_command_list.cpp`, and never appears in any RHI or `src/renderer/`
header — this is what keeps this design a fixed, single-purpose binding
mechanism (ADR-0025's own boundary) rather than a general descriptor-set
system:

- **`VkDescriptorSetLayout`** — exactly one binding: `binding = 0`,
  `descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`,
  `descriptorCount = 1`, `stageFlags = VK_SHADER_STAGE_VERTEX_BIT`.
  Created once per `VulkanPipeline` (identical for every `Pipeline` this
  round, since every material shares the same one-uniform-binding
  layout), destroyed in `VulkanPipeline`'s own destructor after the
  descriptor set it backs (below) is freed.
- **`VkDescriptorPool`** — owned by `VulkanDevice` (a Device-level
  singleton resource, created once at `VulkanDevice` construction,
  mirroring the existing `VkCommandPool` precedent exactly), created with
  `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` set (**required**,
  not optional — without it, `vkFreeDescriptorSets` below is invalid
  usage and the pool could only ever be reset as a whole via
  `vkResetDescriptorPool`, which this Plan does not use). **Fixed
  capacity, tied explicitly to this round's actual concurrent-resource
  ceiling, not an arbitrary round number:** `maxSets = 4`, one pool-size
  entry `{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}`. This ceiling is derived
  from, not merely larger than, this round's own worst case: exactly one
  `Material` exists in steady state (Non-Goals: single material, no
  dynamic material-creation loop), and Section 13's create-before-destroy
  format-change discipline means the *old* `Material`'s `Pipeline` (and
  therefore its one `VkDescriptorSet`) remains alive, momentarily,
  alongside the *new* one during a format-change rebuild — so the true
  peak concurrent count this Plan's own design can ever produce is **2**,
  not an unbounded or merely-assumed-small number. `maxSets = 4` is
  exactly double that peak, a stated, deliberate margin (not unlimited
  headroom) for the GPU test suite's own possible construction/
  destruction interleavings (§15), not a number chosen to "be safe" with
  no accounting behind it. **Why repeated `Material` replacement across
  many format changes over one session never exhausts this pool:** every
  `VulkanPipeline` destructor unconditionally calls `vkFreeDescriptorSets`
  for its own one set (immediately below) — the pool's available capacity
  is fully restored the moment each superseded `Material`/`Pipeline` is
  actually destroyed, so steady-state usage never accumulates allocations
  across format changes; the transient peak stays at 2 no matter how many
  format changes occur across a session. `VulkanDevice::createPipeline()`
  returning `Err(PipelineCreateError::DescriptorSetAllocationFailed)` —
  a new, distinct enumerator (Section 2), not folded into
  `PipelineCreationFailed`, so this specific failure is diagnosable
  without guessing which pipeline-creation step failed — is the explicit,
  checked outcome if `vkAllocateDescriptorSets` ever returns
  `VK_ERROR_OUT_OF_POOL_MEMORY`/`VK_ERROR_FRAGMENTED_POOL` despite this
  accounting; not expected to be reachable given the ceiling's own
  derivation above, but checked, not assumed.
- **`VkDescriptorSet`** — one per `VulkanPipeline`, allocated via
  `vkAllocateDescriptorSets` (its `VkResult` checked; failure maps to
  `PipelineCreateError::DescriptorSetAllocationFailed` above) from
  `VulkanDevice`'s pool at `VulkanPipeline` construction (using the
  layout above), freed via `vkFreeDescriptorSets` (its `VkResult` also
  checked, though `vkFreeDescriptorSets` is documented to only ever
  return `VK_SUCCESS` — checked anyway, per this Plan's own "every
  `VkResult` is checked" rule with no silent exceptions) at
  `VulkanPipeline` destruction — **before** `VulkanDevice`'s pool itself
  may be destroyed, the same "backed-resource destroyed before its owning
  pool" precondition tier already established for
  `VulkanCommandList`/`VulkanDevice`'s command pool (Plan 0006 §9); this
  Plan does not add any new mechanism to enforce that ordering beyond the
  caller discipline every other Device-backed resource in this codebase
  already relies on. **`vkUpdateDescriptorSets` itself returns `void`
  per the Vulkan specification — there is no `VkResult` to check for
  that specific call**, stated explicitly here so a reviewer does not
  expect one.
- **`bindUniformBuffer(Buffer&)`'s implementation**:
  `ATLANTIS_CHECK(buffer.purpose() == BufferPurpose::Uniform)`, then
  unconditionally (no "skip if unchanged" caching — no cache-invalidation
  bug is possible if there is no cache) `vkUpdateDescriptorSets()` with a
  `VkDescriptorBufferInfo{buffer.vkBuffer(), 0, VK_WHOLE_SIZE}` targeting
  the currently-bound `Pipeline`'s `VkDescriptorSet`, then
  `vkCmdBindDescriptorSets(buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS,
  pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr)`. **Write-timing
  safety:** `vkUpdateDescriptorSets` is an immediate, host-side Vulkan
  call (not a recorded GPU command) — calling it here is safe under
  exactly the same single-frame-in-flight reasoning already established
  for writing the camera `Buffer`'s own mapped memory (Section 13 step 3):
  by the time `bindUniformBuffer()` is called (inside a pass execution
  callback, itself only ever invoked after `acquireNextTarget()`'s own
  drain), no GPU work from a prior frame could still be reading the
  descriptor set being updated.
- **Push-constant range coexistence:** `VkPipelineLayout` is built from
  exactly one `VkDescriptorSetLayout` (above) plus one push-constant
  range (`VK_SHADER_STAGE_VERTEX_BIT`, offset 0, `pushConstantSizeBytes`
  — Section 2). Since every `Pipeline` this round uses this same,
  byte-identical descriptor-set-layout-plus-push-constant-range shape,
  switching `bindPipeline()` between different `Material`s mid-loop
  (Section 11's multi-`DrawItem` case) never invalidates a previously
  bound descriptor set or previously pushed constant range — Vulkan's own
  "identical layout, no re-bind needed" rule applies uniformly, though
  this Plan's own `drawFrame()` algorithm (Section 11) re-binds/re-pushes
  every draw item regardless, for simplicity and clarity, not because it
  is strictly required.
- **Camera `Buffer` lifetime versus the GPU submission that reads it:**
  the caller (verification composition) must not destroy the camera
  `Buffer` while any submitted GPU work that read it (via the descriptor
  set it was bound into) has not yet completed — the same lifetime
  precondition tier as every other resource `Device::submit()` touches
  (Section 14), not a new or weaker guarantee specific to uniform
  binding. In this Plan's own manual-verification and GPU-test scope, the
  camera `Buffer` is created once and lives for the whole session/test
  case, destroyed only after the same `Device::waitIdle()` every other
  exit path already requires — it is never destroyed mid-session, so this
  precondition, while real, is not exercised at its edge by anything this
  Plan's own tests do.
- **Deliberately not generalized beyond this round's single-frame-in-
  flight baseline** ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)):
  one `VkDescriptorSet` per `Pipeline`, updated in place every bind call,
  is only correct because at most one frame's GPU work is ever
  outstanding at a time — there is never a second, concurrently-in-flight
  frame that could still be reading a descriptor set this round's next
  `bindUniformBuffer()` call is about to overwrite. A future spec
  introducing multiple frames in flight would need to revisit this design
  (e.g. one descriptor set per frame-in-flight slot, not one per
  `Pipeline`) — this Plan does not attempt to anticipate that shape, per
  [AGENTS.md](../AGENTS.md)'s "no speculative abstraction" principle.

`VulkanPipeline final : public rhi::Pipeline` (`vulkan_pipeline.h/.cpp`):
holds `VkPipeline`, `VkPipelineLayout`, the `VkDescriptorSetLayout` and
`VkDescriptorSet` described above, and the two `VkShaderModule` handles
(destroyed immediately after `vkCreateGraphicsPipelines()` succeeds, per
standard Vulkan practice — a `VkShaderModule` is not needed after
pipeline creation). Constructed only via `VulkanDevice::createPipeline()`,
using `VkPipelineRenderingCreateInfo` (naming `colorFormat`/`depthFormat`
directly, per ADR-0024) instead of a `VkRenderPass` handle, and
`VK_DYNAMIC_STATE_VIEWPORT`/`VK_DYNAMIC_STATE_SCISSOR` in
`VkPipelineDynamicStateCreateInfo` (ADR-0025). **Destruction order**
(`VulkanPipeline`'s own destructor, two-phase-construction-guard pattern,
per Section 9's note): `vkDestroyPipeline` → `vkFreeDescriptorSets` (this
`Pipeline`'s one set, from `VulkanDevice`'s pool) → `vkDestroyDescriptorSetLayout`
→ `vkDestroyPipelineLayout`. `Material` (Section 11) owning a `Pipeline`
that outlives the `Device` it was created from is the same lifetime
precondition violation tier as every other Device-backed RHI resource
in this codebase (Section 14).

`resource_state_mapping.{h,cpp}` gains two new rows (existing three
rows, Spec 0006, unchanged):

| before | after | oldLayout | newLayout | srcAccess | dstAccess | srcStage | dstStage |
|---|---|---|---|---|---|---|---|
| `Undefined` | `ColorAttachmentOutput` | `UNDEFINED` | `COLOR_ATTACHMENT_OPTIMAL` | `0` | `COLOR_ATTACHMENT_WRITE_BIT` | `TOP_OF_PIPE` | `COLOR_ATTACHMENT_OUTPUT_BIT` |
| `ColorAttachmentOutput` | `PresentSource` | `COLOR_ATTACHMENT_OPTIMAL` | `PRESENT_SRC_KHR` | `COLOR_ATTACHMENT_WRITE_BIT` | `0` | `COLOR_ATTACHMENT_OUTPUT_BIT` | `BOTTOM_OF_PIPE` |
| `Undefined` | `DepthAttachmentReadWrite` | `UNDEFINED` | `DEPTH_ATTACHMENT_OPTIMAL` | `0` | `DEPTH_STENCIL_ATTACHMENT_READ_BIT \| DEPTH_STENCIL_ATTACHMENT_WRITE_BIT` | `TOP_OF_PIPE` | `EARLY_FRAGMENT_TESTS_BIT \| LATE_FRAGMENT_TESTS_BIT` |

`planTransition()`'s `ATLANTIS_CHECK_MSG(false, ...)` fallback for an
undefined pair is unchanged — these three new rows are additive, the
existing three (Spec 0006) untouched.

`VulkanCommandList` gains (all `static_cast<VulkanX&>` the abstract
reference argument — safe, since only Vulkan Backend ever constructs one
in Phase 1, per ADR-0001, matching Spec 0006's existing precedent):

- `bindPipeline(Pipeline&)` → `vkCmdBindPipeline(buffer_, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline)`.
- `bindVertexBuffer(Buffer&)` → `ATLANTIS_CHECK(buffer.purpose() == BufferPurpose::Vertex)`, then
  `vkCmdBindVertexBuffers(buffer_, 0, 1, &vkBuffer, &offset0)`.
- `bindIndexBuffer(Buffer&)` → `ATLANTIS_CHECK(buffer.purpose() == BufferPurpose::Index)`, then
  `vkCmdBindIndexBuffer(buffer_, vkBuffer, 0, VK_INDEX_TYPE_UINT16)` (index type is a Plan-stage
  detail matching whatever the fixed mesh's index width is — `UINT16` sufficient for this round's
  small vertex counts).
- `bindUniformBuffer(Buffer&)` → see this section's own "Camera uniform
  binding — full candidate design" subsection above for the complete,
  concrete `VkDescriptorSetLayout`/`VkDescriptorPool`/`VkDescriptorSet`
  design this call implements — not left as an implementation-stage
  detail.
- `pushConstant(const void*, sizeBytes)` → `vkCmdPushConstants(buffer_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, static_cast<uint32_t>(sizeBytes), data)`.
- `drawIndexed(indexCount)` → `vkCmdDrawIndexed(buffer_, indexCount, 1, 0, 0, 0)`.
- `beginRendering(RenderTarget&, Texture*, ClearColorValue, float)` →
  builds `VkRenderingAttachmentInfo` for the color target
  (`loadOp=CLEAR`, `storeOp=STORE`, the given clear value,
  `imageLayout=COLOR_ATTACHMENT_OPTIMAL`) and, if `depth != nullptr`, a
  second `VkRenderingAttachmentInfo` for it (`loadOp=CLEAR`,
  `storeOp=STORE`, `imageLayout=DEPTH_ATTACHMENT_OPTIMAL`), then calls
  whichever of `vkCmdBeginRendering`/`vkCmdBeginRenderingKHR`
  `VulkanDevice` resolved (Section 8) with a `VkRenderingInfo` naming
  `renderArea` from the color target's `extent()`.
- `endRendering()` → the matching `vkCmdEndRendering`/`vkCmdEndRenderingKHR`.

**Precondition, caller-enforced by `execute()`'s own algorithm (Section
7), not re-checked here:** `beginRendering()` is only ever called after
`transitionResource()` has already brought both attachments to their
declared states for this pass — matching `clearColor()`'s existing
precondition-tier (Spec 0006, unchanged).

---

## 11. Renderer Module — `Mesh`, `Material`, `DrawItem`, `Renderer`

`src/renderer/include/atlantis/renderer/mesh.h`:

```cpp
#pragma once

#include <cstdint>
#include <memory>

#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/device.h>
#include <atlantis/rhi/types.h>
#include <atlantis/result.h>

namespace atlantis::renderer {

// Owns exactly one vertex Buffer and one index Buffer (ADR-0022). Move-
// only, single-owner. Constructed once; never re-uploaded or mutated.
// Not internally thread-safe; caller-thread-only (ADR-0004).
class Mesh {
 public:
  Mesh(std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer,
       std::unique_ptr<atlantis::rhi::Buffer> indexBuffer,
       std::uint32_t indexCount) noexcept;
  ~Mesh() = default;

  Mesh(const Mesh&) = delete;
  Mesh& operator=(const Mesh&) = delete;
  Mesh(Mesh&&) noexcept = default;
  Mesh& operator=(Mesh&&) noexcept = default;

  [[nodiscard]] atlantis::rhi::Buffer& vertexBuffer() const noexcept { return *vertexBuffer_; }
  [[nodiscard]] atlantis::rhi::Buffer& indexBuffer() const noexcept { return *indexBuffer_; }
  [[nodiscard]] std::uint32_t indexCount() const noexcept { return indexCount_; }

 private:
  std::unique_ptr<atlantis::rhi::Buffer> vertexBuffer_;
  std::unique_ptr<atlantis::rhi::Buffer> indexBuffer_;
  std::uint32_t indexCount_;
};

enum class CreateMeshError {
  VertexBufferCreationFailed,
  IndexBufferCreationFailed,
};

// Convenience free function -- NOT a Renderer method (ADR-0022: Renderer
// never creates a Mesh). Creates both Buffers via device, copies
// vertexData/indexData into their mapped memory once, and returns an
// independently-owned Mesh. Each call produces a new, independent Mesh
// -- no cache, no deduplication.
[[nodiscard]] atlantis::Result<Mesh, CreateMeshError> createMesh(
    atlantis::rhi::Device& device, atlantis::rhi::VertexInputLayout layout,
    const void* vertexData, std::size_t vertexDataSizeBytes,
    const std::uint16_t* indices, std::uint32_t indexCount);

}  // namespace atlantis::renderer
```

`src/renderer/include/atlantis/renderer/material.h` (same shape,
`Material` owns exactly one `Pipeline`; `createMaterial()` takes
`PipelineCreateParams` and returns `Result<Material, CreateMaterialError>`,
mirroring `Mesh`/`createMesh()` above — full listing omitted here for
brevity, same pattern).

`src/renderer/include/atlantis/renderer/draw_item.h`:

```cpp
#pragma once

#include <array>

#include <atlantis/renderer/material.h>
#include <atlantis/renderer/mesh.h>

namespace atlantis::renderer {

// A plain, caller-owned aggregate -- not a scene graph, not registered
// anywhere persistent. mesh/material are borrowed (must outlive the
// Renderer::drawFrame() call they are passed to). objectToWorld is a
// column-major 4x4 float matrix -- Atlantis Core has no public math
// type yet (not part of this spec's scope to add one), so this is a
// raw, fixed-layout array, matching exactly what pushConstant() (Section
// 5) copies verbatim.
struct DrawItem {
  const Mesh* mesh = nullptr;
  const Material* material = nullptr;
  std::array<float, 16> objectToWorld{};
};

}  // namespace atlantis::renderer
```

`src/renderer/include/atlantis/renderer/renderer.h`:

```cpp
#pragma once

#include <span>

#include <atlantis/render_graph/render_graph_builder.h>
#include <atlantis/renderer/draw_item.h>
#include <atlantis/rhi/buffer.h>
#include <atlantis/rhi/command_list.h>
#include <atlantis/rhi/render_target.h>
#include <atlantis/rhi/texture.h>

namespace atlantis::renderer {

// Stateless orchestrator (ADR-0022): retains no GPU resource, no
// frame-to-frame state, across calls. Depends only on RHI, RenderGraph,
// Core -- never Platform, Vulkan Backend, or any Vk* type. Not
// internally thread-safe; caller-thread-only (ADR-0004).
//
// Deliberately copyable and movable, unlike RenderGraphBuilder
// (Spec 0005/ADR-0017's non-copyable/non-movable builder). That
// restriction exists specifically because RenderGraphBuilder vends
// PassHandle/ResourceHandle values whose provenance is tied to the
// builder's own stable address -- moving it would silently invalidate
// every handle a caller already holds. Renderer has no handles, no
// vended identity, and (see above) no member state of any kind: an
// earlier revision of this Plan deleted Renderer's copy/move
// constructors anyway, by analogy with RenderGraphBuilder, without a
// reason of its own that actually applies here -- an unforced,
// pointless restriction on a class review corrected by leaving every
// special member at its trivial compiler-generated default (no line
// needed at all, since an empty class with no user-declared special
// member already gets defaulted copy/move/destructor).
class Renderer {
 public:
  Renderer() = default;

  // Builds, compiles, and executes one RenderGraph draw pass into
  // commandList -- never calls Device::submit()/Presentation::present()
  // itself (ADR-0022). colorTarget/depthTarget/cameraUniformBuffer are
  // borrowed references the caller already owns and (for
  // cameraUniformBuffer) has already written this frame's view/
  // projection matrices into -- Renderer never touches raw camera math,
  // only binds the Buffer it is handed. drawItems is an ordinary
  // caller-supplied span, iterated once, retained nowhere.
  void drawFrame(atlantis::rhi::CommandList& commandList,
                 atlantis::rhi::RenderTarget& colorTarget,
                 atlantis::rhi::Texture& depthTarget,
                 atlantis::rhi::Buffer& cameraUniformBuffer,
                 std::span<const DrawItem> drawItems);
};

}  // namespace atlantis::renderer
```

### `Renderer::drawFrame()` algorithm (candidate, `renderer.cpp`)

```
1. RenderGraphBuilder builder;
   colorResource = builder.declareResource("color");
   depthResource = builder.declareResource("depth");
   pass = builder.declarePass("draw");
   builder.writes(pass, colorResource, ResourceState::ColorAttachmentOutput);
   builder.writes(pass, depthResource, ResourceState::DepthAttachmentReadWrite);   // single writes() call -- ADR-0018/ADR-0026
   builder.setExecute(pass, [&commandList, &cameraUniformBuffer, drawItems](CommandList& cmd) {
     for (const DrawItem& item : drawItems) {
       cmd.bindPipeline(item.material->pipeline());
       cmd.bindVertexBuffer(item.mesh->vertexBuffer());
       cmd.bindIndexBuffer(item.mesh->indexBuffer());
       cmd.bindUniformBuffer(cameraUniformBuffer);
       cmd.pushConstant(item.objectToWorld.data(), item.objectToWorld.size() * sizeof(float));
       cmd.drawIndexed(item.mesh->indexCount());
     }
   });

2. compileResult = builder.compile();
   ATLANTIS_CHECK_MSG(compileResult.isOk(), "Renderer's fixed one-pass graph never fails to compile");

3. bindings = {
     {compileResult.value().resourceAt(0), &colorTarget, kBackgroundClearColor, nullptr, 0.0f},
     {compileResult.value().resourceAt(1), nullptr, ClearColorValue{}, &depthTarget, 1.0f},
   };
   render_graph::execute(compileResult.value(), bindings, commandList);
```

`kBackgroundClearColor` is a fixed, Renderer-internal constant (not a
caller-configurable parameter this round — consistent with Spec 0007's
own minimal-material scope; a future spec may expose it).

**Why `Renderer` never observes `Presentation::metadata()` or a format
change:** by construction — `drawFrame()`'s signature has no `Device&`
or `Presentation&` parameter at all, so there is no code path for it to
call `metadata()` even if a future edit tried to. This is a structural,
not merely documented, guarantee (Acceptance Criteria, Spec 0007).

---

## 12. Shader Bootstrap

Per ADR-0027, checked in under `shaders/minimal_renderer/` — **not**
under `tests/`, corrected from an earlier revision of this Plan that put
them there. `shaders/` is this repository's own pre-designated,
neutral, product-level location for shader sources
([README.md](../README.md)'s repository-layout table: "`shaders/`
Shader sources (empty — structure pending first spec/plan/ADR)" — this
Plan is the first to give it real content). Putting a non-shipping
*example*'s runtime assets under `tests/` would make
`examples/minimal_renderer_demo` depend on the test tree existing at
all, which is backwards — an example must not need `tests/` to build or
run. `shaders/minimal_renderer/` is shared, as a single authoritative
copy, by both `examples/minimal_renderer_demo` and
`tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` (Section 17's CMake
copies it to each consumer's own build output — never duplicated as
source):

- `minimal_mesh.vert.glsl` / `minimal_mesh.frag.glsl` — human-readable
  GLSL source (GLSL is an arbitrary, non-binding choice for this
  bootstrap only — see ADR-0027's own "no shader source language
  decision is made by this spec"). Vertex shader: takes `layout(location
  = 0) in vec3 position`, `layout(location = 1) in vec3 color`; a single
  `layout(push_constant) uniform PushConstants { mat4 objectToWorld; }`;
  a single `layout(binding = 0) uniform CameraUniform { mat4 view; mat4
  projection; }`; outputs `gl_Position = projection * view *
  objectToWorld * vec4(position, 1.0)` and passes `color` through to the
  fragment stage. Fragment shader: outputs the interpolated `color` as
  `layout(location = 0) out vec4 outColor`, unmodified — no lighting, no
  texture sample, matching Spec 0007's minimal-material Non-Goal exactly.
- `minimal_mesh.vert.spv` / `minimal_mesh.frag.spv` — pre-compiled
  bytecode, produced by a human running `glslc` manually (not by any
  CMake target).
- `README.md` — records the exact `glslc` command line (e.g. `glslc
  --target-env=vulkan1.3 -o minimal_mesh.vert.spv minimal_mesh.vert.glsl`),
  the `glslc`/Vulkan SDK version used, and regeneration instructions.
  Committed alongside the `.spv` files in the same PR that adds them.

**No CMake target compiles, parses, or reflects any of the above.**

**Runtime location — copied next to each consumer's executable, never a
baked developer-machine absolute path.** An earlier revision of this
Plan proposed a `configure_file()`-generated header embedding
`${CMAKE_SOURCE_DIR}`'s absolute path — rejected on review: baking a
specific developer machine's source-tree path into a generated header
(and, transitively, into the built binary) is fragile (breaks if the
binary is copied elsewhere, e.g. to run on the GPU-test machine used for
PR #23/#24's own verification) and is exactly the kind of hidden,
environment-specific coupling this repository's own artifacts should not
carry. Instead:

- Each consumer target (`atlantis_minimal_renderer_demo`,
  `atlantis_vulkan_backend_gpu_tests`) gets an `add_custom_command(TARGET
  ... POST_BUILD ...)` step (Section 17) that copies
  `shaders/minimal_renderer/minimal_mesh.{vert,frag}.spv` into a
  `shaders/` subdirectory next to that target's own built executable,
  using `$<TARGET_FILE_DIR:target>` (a generator expression, correctly
  resolving per-configuration on the multi-config Visual Studio generator
  this repository already builds with) and `copy_if_different` (a no-op
  on an unchanged file, correct for incremental builds).
- **No file-path API of any kind is used to locate the shader files at
  runtime — not `GetModuleFileNameW`, not any other Win32 call, not a
  new Core/Platform path-resolution API.** An earlier revision of this
  Plan proposed a `GetModuleFileNameW`-based helper — rejected on review:
  it would make both the demo and the GPU test directly depend on a raw
  Win32 API for something this repository's own module boundaries
  (`AGENTS.md`'s Platform-isolation rule; ADR-0005) reserve to the
  Atlantis Platform module, and `examples/minimal_renderer_demo`/
  `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` are exactly the
  kind of code that rule exists to keep Win32-free wherever avoidable —
  unlike `windows_platform_smoke_tests.cpp`'s own direct `<windows.h>`
  use (which exists specifically to test Atlantis Platform's own Win32
  implementation, a different and narrower justification that does not
  apply here). Instead, each consumer opens the shader files by a plain
  **relative** path — `"shaders/minimal_mesh.vert.spv"` — and relies on
  its **current working directory already being its own build output
  directory** at the moment it runs, which this Plan arranges structurally
  rather than leaving as an unstated assumption:
  - **For the GPU test** (`catch_discover_tests`-registered, CTest-driven):
    `catch_discover_tests()`'s own `WORKING_DIRECTORY` parameter is set to
    `"$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>"` (Section 17)
    — CTest itself then launches the test process with that directory as
    its working directory, for every discovered test case, on every
    configuration the multi-config generator produces. No code in the
    test needs to know or compute this path at all; it is simply where
    the process already is when `main()` starts.
  - **For the demo** (launched interactively, not through CTest): this
    Plan adds a convenience CMake target, `run_minimal_renderer_demo`
    (`add_custom_target(run_minimal_renderer_demo COMMAND
    $<TARGET_FILE:atlantis_minimal_renderer_demo> WORKING_DIRECTORY
    "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>")`), so
    `cmake --build build --target run_minimal_renderer_demo` (or the
    generator's own "run" action on that target) always launches the demo
    with the correct working directory regardless of where the command
    was invoked from. A human launching the built `.exe` directly instead
    (e.g. by double-clicking it, or `cd`-ing to it manually) must do so
    from its own output directory for the relative shader path to
    resolve — this Plan's own manual-verification instructions (§15)
    state this explicitly as an operational precondition, not an
    implicit assumption a reader has to infer.
  - **Shader load failure is a recoverable, explicit outcome, never a
    crash.** Each consumer's own small, local `loadSpirvFile(path) ->
    std::optional<std::vector<std::uint32_t>>` helper (plain
    `std::ifstream`-based, C++ standard library only — no Core/Platform
    API, no new public type) returns an empty optional on any failure to
    open or read the file; the caller logs the failure and exits
    gracefully (mirroring `examples/frame_execution_demo`'s own existing
    `EXIT_FAILURE` pattern), never proceeding to call
    `Device::createPipeline()` with incomplete bytecode. This is
    deliberately **not** routed through `PipelineCreateError` (Section
    2) — that enum describes GPU object-creation failure, a distinct
    concern from a host-side file-I/O failure that happens strictly
    before any Vulkan call is made.
  - This mechanism is correct regardless of which machine built or is
    running the binary (no absolute path, developer-specific or
    otherwise, appears anywhere), regardless of Debug/Release, and
    regardless of which generator produced the build — the working
    directory is always resolved by CMake/CTest at build or test-run
    time, never hardcoded.

**Vertex-input/binding layout consistency verification:** this round has
no automated reflection-based cross-check between the hand-specified
`VertexInputLayout`/`PipelineCreateParams` (C++) and the actual SPIR-V
interface (ADR-0027) — the available signals are: `Device::createPipeline()`
succeeding, Vulkan Validation Layers reporting zero warnings/errors when
the pipeline is actually used to draw, and the GPU test's/demo's
manually-observed correct visual output (§15). A mismatch that
Validation Layers do not catch (e.g. two attributes silently swapped,
both `Float3`) would only be caught by the visual-output check — this
limitation is inherited directly from ADR-0027's own accepted trade-off,
not introduced by this Plan.

**Shader binary update review process:** any future change to
`minimal_mesh.{vert,frag}.glsl` requires the author to re-run `glslc`
manually, update the `.spv` files and `README.md`'s recorded command
line/version together, in the same commit — reviewable as an ordinary
binary-diff-plus-source-diff PR, the same way any other checked-in test
fixture is reviewed in this repository.

**Migration boundary to a future Shader System:** unchanged from
ADR-0027 — this Plan does not narrow or widen that boundary.

---

## 13. Resize / Format-Change Contract — Verification Composition Implementation

Per ADR-0022/ADR-0025's confirmed contract, implemented entirely in the
verification composition (`examples/minimal_renderer_demo/main.cpp`,
mirroring `examples/frame_execution_demo`'s existing structure) — no RHI
or Renderer code implements any part of this section beyond the
primitives Sections 3–5 already expose (`Presentation::metadata()`
already exists, unchanged, since Spec 0003).

```
Once per frame, after a successful acquireNextTarget():

1. currentFormat = presentation->metadata().format
   if currentFormat != lastSeenFormat:
     device->waitIdle()
     newMaterialResult = createMaterial(*device, {..., .colorFormat = currentFormat, .depthFormat = DepthFormat::D32Sfloat, ...})
     if newMaterialResult.isErr():
       log the error; KEEP the existing material (still valid, still matches lastSeenFormat) --
       do NOT update lastSeenFormat, so this same check retries next frame; this frame draws
       with the old Pipeline against the new format (a real, accepted, transient mismatch this
       round has no better fallback for -- see Section 14)
     else:
       material = std::move(newMaterialResult.value())   // old Pipeline destroyed HERE, only after
                                                            // the new one already succeeded -- never
                                                            // destroy-then-create (see note below)
       lastSeenFormat = currentFormat
     // depthTexture is NOT recreated here solely for a format change --
     // its own format (D32Sfloat) never varies with the swapchain's
     // color format this round

2. currentExtent = renderTarget->extent()
   if currentExtent != lastSeenExtent:
     newTextureResult = device->createTexture({.extent = currentExtent, .format = DepthFormat::D32Sfloat})
     if newTextureResult.isErr():
       log the error; KEEP the existing depthTexture (mismatched extent -- may itself trigger a
       Validation Layer warning if drawn against a differently-sized RenderTarget this frame, an
       accepted transient gap); do NOT update lastSeenExtent, so this check retries next frame
     else:
       depthTexture = std::move(newTextureResult.value())   // same create-before-destroy ordering
       lastSeenExtent = currentExtent
     // Pipeline is NOT recreated here -- dynamic viewport/scissor (Section 3)

3. Write this frame's view/projection into cameraBuffer->mappedData()   -- see Section 9's
   write-timing note; safe here because acquireNextTarget()'s own internal drain (Spec 0006/
   PR #24) already guarantees no GPU work from the prior frame is still reading this Buffer

4. renderer.drawFrame(*commandList, *renderTarget, *depthTexture, *cameraBuffer, drawItems)
```

**Create-before-destroy, never destroy-before-create:** both steps 1 and
2 construct the *new* resource first and only replace the caller's
`material`/`depthTexture` variable — destroying the old one, via the
`unique_ptr`/wrapper's own move-assignment — once construction has
already succeeded. An earlier revision of this Plan destroyed the old
resource first (`material.reset()`) and then attempted to construct the
new one — if that construction failed, the demo would have been left
with *no* `Material` at all, an unrecoverable state for the remainder of
its run. The corrected ordering above means a transient creation failure
(e.g. a momentary out-of-memory condition) leaves the caller with a
still-valid, if temporarily stale, resource to keep rendering with, and a
retry on the very next frame — never a hard failure from what Spec 0007
itself classifies as a routine, expected event (a format or extent
change).

Step 1 and Step 2 are independent checks, in either order, run every
frame at negligible cost (a single enum/struct comparison) — an extent
change never triggers step 1's `Pipeline` rebuild, and a format change
(without a concurrent extent change) never triggers step 2's `Texture`
rebuild, exactly as ADR-0025's contract requires.

**If the test environment's hardware cannot genuinely trigger a format
change** (no second monitor/surface with different capabilities), Step 1
is verified by code inspection only — the manual-verification report
(§15) must state this explicitly, per Spec 0007's own Acceptance
Criteria.

---

## 14. Error Model Implementation

| Case | Tier | Mechanism |
|---|---|---|
| `Mesh`/`Material`/`Buffer`/`Texture`/`Pipeline` used outside its valid lifetime window; `Device` destroyed while any of these it backed are still alive | Lifetime precondition violation | Not detected; caller obligation (documented, tested via correct-discipline manual verification only) |
| `ResourceBinding` with both/neither of `target`/`depthTexture` set (Guard 0); `ResourceState`-tagged usage with no binding (Guard 1); a bound `RenderTarget` with a declared read usage (Guard 2) | Guaranteed-detectable programmer error | `ATLANTIS_CHECK_MSG` inside `execute()` |
| `bindVertexBuffer`/`bindIndexBuffer`/`bindUniformBuffer` called with a `Buffer` of the wrong `BufferPurpose` | Guaranteed-detectable programmer error | `ATLANTIS_CHECK` inside `VulkanCommandList` |
| `Buffer`/`Texture`/`Pipeline` creation failure (`vkAllocateMemory`/`vkCreateBuffer`/`vkCreateImage`/`vkCreateImageView`/`vkCreateShaderModule`/`vkCreateGraphicsPipelines`) | Recoverable | `Result::Err(BufferCreateError\|TextureCreateError\|PipelineCreateError)` |
| No physical device supports either dynamic-rendering path | Recoverable | `Result::Err(DeviceCreateError::DynamicRenderingUnavailable)` |
| Attachment format change / extent change not yet handled by the caller (stale `Pipeline`/`Texture`) | Caller precondition violation (documented contract, §13) | Not detected by RHI/RenderGraph/Renderer; caller's own §13 check-and-rebuild discipline is the only mitigation |
| Every other case (submit/present/acquire failures, zero-extent, out-of-date/suboptimal) | Unchanged from Spec 0006 | Unchanged from Plan 0006 §12 |

`vulkan_result.h` gains three new pure mapping functions
(`toBufferCreateError`, `toTextureCreateError`, `toPipelineCreateError`),
mirroring the four existing ones' pattern exactly.

---

## 15. Testing Strategy

### GPU-independent unit tests (layer 1, no Vulkan device)

- `tests/vulkan_backend/dynamic_rendering_tests.cpp` — `decideDynamicRenderingPath()`'s
  **exhaustive** truth table over all five boolean parameters. When
  `physicalDeviceProperties2InstanceExtensionAvailable = false`, every one of the 2⁴ = 16
  combinations of the remaining four booleans must return `Unavailable` — tested with the four
  "extreme corner" combinations below (all-false, all-true, and the two single-bit-set cases)
  as a representative, not exhaustive, sample of that collapse, since the decision logic's own
  short-circuit (`if !physicalDeviceProperties2InstanceExtensionAvailable: return Unavailable`)
  makes the remaining four arguments provably irrelevant by inspection, not merely by
  enumeration. When `physicalDeviceProperties2InstanceExtensionAvailable = true`, the full,
  **exhaustive** 2⁴ = 16-case table over
  `(apiVersionAtLeast1_3, coreFeatureSupported, extensionAdvertised, extensionFeatureSupported)`
  below applies — including every combination where an argument is documented as "only
  meaningful when" some other argument is true, confirming the function degrades safely (never
  crashes, never returns a wrong path) even when a caller passes a logically inconsistent
  combination it should never actually produce:

  `physicalDeviceProperties2InstanceExtensionAvailable = false` (representative sample — all four
  must return `Unavailable`): `(F,F,F,F)`, `(T,T,T,T)`, `(T,F,F,F)`, `(F,F,T,T)`.

  `physicalDeviceProperties2InstanceExtensionAvailable = true`:

  | apiVersionAtLeast1_3 | coreFeatureSupported | extensionAdvertised | extensionFeatureSupported | → |
  |---|---|---|---|---|
  | F | F | F | F | Unavailable |
  | F | F | F | T | Unavailable |
  | F | F | T | F | Unavailable |
  | F | F | T | T | Extension |
  | F | T | F | F | Unavailable |
  | F | T | F | T | Unavailable |
  | F | T | T | F | Unavailable |
  | F | T | T | T | Extension |
  | T | F | F | F | Unavailable |
  | T | F | F | T | Unavailable |
  | T | F | T | F | Unavailable |
  | T | F | T | T | Extension |
  | T | T | F | F | Core |
  | T | T | F | T | Core |
  | T | T | T | F | Core |
  | T | T | T | T | Core |
- `tests/vulkan_backend/vulkan_memory_tests.cpp` — `selectMemoryTypeIndex()` against a
  synthetic `VkPhysicalDeviceMemoryProperties` with known type/property combinations: exact
  match found; match found among several candidates (first-matching-index chosen); no match
  (empty optional returned).
- `tests/vulkan_backend/resource_state_mapping_tests.cpp` (extended) — the two new table rows
  from Section 10 produce their documented layout/access/stage values.
- `tests/vulkan_backend/vulkan_result_tests.cpp` (extended) — the three new mapping functions.
- `tests/render_graph/attachment_execution_tests.cpp` — against `fake_command_list.h`
  (Spec 0006, extended to record `beginRendering`/`endRendering`/`bindPipeline`/etc. calls it
  receives): Guard 0 (malformed binding); Guard 1 generalized to a depth `Texture` binding;
  Guard 2 firing for a `target` binding with a read usage and *not* firing for an equivalent
  `depthTexture` binding; draw-pass recognition firing for `ColorAttachmentOutput`/
  `DepthAttachmentReadWrite` and *not* firing for `ColorAttachmentWrite` (the concrete
  regression test for Section 7/ADR-0026's central fix); `beginRendering()` called before, and
  `endRendering()` immediately after, a recognized draw pass's callback; independent per-
  resource "most-recently-recorded state" tracking across two simultaneously bound resources;
  every bound resource (color and depth) starting each `execute()` call from `Undefined`,
  including on a second, otherwise-identical `execute()` call.
- `tests/render_graph/execution_tests.cpp` (unchanged) — Spec 0006's existing cases,
  including the `ColorAttachmentWrite`-only clear pass, must still pass unmodified — the
  concrete regression proof that this round's new derivation rule does not affect it.
- `tests/rhi/buffer_texture_pipeline_tests.cpp` — `BufferCreateParams`/`TextureCreateParams`/
  `PipelineCreateParams` default-construction and equality sanity (mirroring `types_tests.cpp`'s
  existing style); no real device required.
- `tests/renderer/renderer_ownership_tests.cpp` — compile-time checks that `Mesh`/`Material`
  are movable-not-copyable; that `Renderer` is non-copyable/non-movable; a `drawFrame()` call
  against a fake `CommandList` (reusing `fake_command_list.h`) with two `DrawItem`s sharing the
  same `Mesh`/`Material` instance (reference reuse, not a cache) records exactly two
  `bindPipeline`/`bindVertexBuffer`/.../`drawIndexed` sequences with the *distinct* push-constant
  data each `DrawItem` supplied — the concrete regression test for the push-constant-vs-shared-
  uniform-buffer correctness argument (ADR-0025), exercised here without a real device.

### GPU-required tests (Windows/Vulkan)

- `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` (new; carries the `gpu` CTest label,
  mirroring `frame_execution_gpu_tests.cpp`'s existing pattern): creating and destroying a
  `Buffer` of each of the three purposes; creating and destroying a depth `Texture`, including
  at a resized extent; creating and destroying a `Pipeline` from the checked-in SPIR-V pair
  (§12); one full draw-pass execution (bind, draw, attachment scope begin/end) against a real
  acquired `RenderTarget` and a real depth `Texture`, Validation Layers clean; a frame with
  **more than one `DrawItem`**, each with a distinct `objectToWorld`, confirming (via a
  Validation-Layers-clean run plus the GPU-independent push-constant test above, together) that
  the recorded draws do not collide; `createDevice()`'s dynamic-rendering capability detection
  on whichever path the test machine's actual GPU/driver provides — **this environment is
  expected to exercise exactly one of the two paths**; the other path and the explicit-error
  case (`DynamicRenderingUnavailable`) remain verified by code inspection only, where a second
  real device/driver combination is unavailable — stated explicitly in any verification report,
  not silently treated as fully covered.
- **What this automated GPU test can and cannot confirm about depth occlusion:** it confirms
  every API call succeeds and Validation Layers report zero warnings/errors for a depth-tested
  draw — it does **not** read back or inspect the rendered pixels, so it cannot by itself
  confirm the depth test actually *behaved* correctly (e.g. that front-facing geometry visually
  occludes back-facing geometry, as opposed to, say, an inverted `VkCompareOp` that still runs
  without a validation error but produces the wrong image). That confirmation is the manual
  verification's job (below), not this automated test's — no automated test in this Plan claims
  to substitute for it, consistent with Spec 0007's own stated image-regression limitation.
- **Headless integration tests:** not applicable — unchanged from Spec 0006's equivalent flag.
- **Image regression tests:** not applicable — manual visual verification only (below), a real,
  accepted limitation Spec 0007 itself already states.
- **Known environment constraint, carried forward from PR #23/#24's own verification history:**
  Windows Smart App Control has previously blocked execution of an unrelated, freshly-compiled
  test executable (`atlantis_core_tests.exe`) in this repository's own verification environment,
  and `catch_discover_tests`'s discovery mechanism aborts an entire bare `ctest` run if any single
  registered executable's discovery invocation fails. Whoever executes this Plan's own
  verification must, if the same constraint recurs, run `ctest` scoped per test-module
  subdirectory (`ctest --test-dir build/tests/<module> ...`) rather than a single repository-wide
  invocation — and must report exactly what was actually run this way, never claim a
  repository-wide `ctest` pass that did not actually execute.

### Manual verification (`examples/minimal_renderer_demo`)

Mirrors `examples/frame_execution_demo`'s own structure and non-shipping
disclaimer (Spec 0007 Non-Goals). **Must be launched via
`cmake --build build --target run_minimal_renderer_demo` (or by running
the built executable directly from its own build output directory) —
Section 12's plain relative shader path only resolves correctly with
that working directory; launching it from any other directory is a
usage error, not a defect to work around.** Confirms, interactively:

- A visible window shows a recognizable, correctly-shaded (per-vertex
  color), correctly depth-ordered 3D mesh — front-facing geometry
  occludes back-facing geometry correctly, no z-fighting or inverted
  depth test — continuously across repeated frames.
- The camera transform (view/projection, written into `cameraBuffer`
  each frame) visibly affects the mesh's projected position/orientation
  — e.g. a slowly rotating or orbiting camera, confirming the uniform
  buffer binding path actually works, not merely that *something* draws.
- Interactive resize: the mesh continues to render correctly (including
  depth correctness) at the new size; the depth `Texture` is
  demonstrably recreated (§13 Step 2) while `Pipeline` is demonstrably
  *not* recreated (§13 Step 1 untouched) for an extent-only change — the
  demo logs or asserts this distinction explicitly, so it is not merely
  claimed but observably true.
- Format-change handling, if the test environment allows triggering it
  (§13's own stated limitation) — `Device::waitIdle()` called,
  `Material`'s `Pipeline` recreated, zero Validation Layer warnings/
  errors across the transition.
- Minimizing results in no crash, no busy-spin, zero Vulkan calls while
  minimized; restoring resumes correct rendering (mesh, depth, camera)
  with no special recovery step.
- Clean exit at any point in this sequence, including mid-resize,
  minimized, and a deliberate mid-frame exit (acquired but not yet
  submitted/presented), with no outstanding acquired `RenderTarget`, no
  leaked `CommandList`/`Buffer`/`Texture`/`Pipeline`/`Mesh`/`Material`,
  and zero Validation Layer warnings/errors at any point, including at
  shutdown.

### Debug/Release

Every automated test above runs, and the manual demo is exercised, under
both Debug and Release configurations — unchanged expectation from every
prior plan in this line.

---

## 16. Explicit Prohibitions (grep/code-review checklist)

Run at the end of every implementation step, and again before opening a
PR:

```
# No Vk* type or Vulkan header outside Vulkan Backend
grep -rn "Vk[A-Z]" src/rhi/include src/render_graph/include src/renderer/include   # expect: no matches
grep -rln "vulkan" src/rhi/include src/render_graph/include src/renderer/include -i  # expect: no matches (excluding this comment style)

# No direct vkCmd* call outside Vulkan Backend's CommandList implementation
grep -rn "vkCmd" src --include=*.cpp --include=*.h | grep -v "src/vulkan_backend/"   # expect: no matches

# No VkRenderPass/VkFramebuffer anywhere (ADR-0024)
grep -rn "VkRenderPass\|VkFramebuffer" src   # expect: no matches

# RenderGraph never submits or presents
grep -rn "Device::submit\|->submit(\|\.submit(\|->present(\|\.present(" src/render_graph  # expect: no matches

# Renderer never links Platform or Vulkan Backend
grep -n "Atlantis::Platform\|Atlantis::VulkanBackend\|Vulkan::Vulkan" src/renderer/CMakeLists.txt  # expect: no matches

# Renderer never depends on Presentation or Device concretely -- no metadata()/format-change code in Renderer
grep -rn "metadata\(\)\|waitIdle\(\)" src/renderer  # expect: no matches (Section 13's contract lives entirely
                                                     #   in the verification composition)

# ColorAttachmentWrite never reused by the new draw-pass path
grep -rn "ColorAttachmentWrite" src/render_graph/src/execution.cpp  # expect: no matches (only
                                                                     #   ColorAttachmentOutput/DepthAttachmentReadWrite
                                                                     #   drive draw-pass recognition)

# No shader compiler invocation anywhere in the build
grep -rln "glslc\|dxc\|shaderc" CMakeLists.txt src tests examples cmake  # expect: no matches

# No new third-party dependency
git diff --stat CMakeLists.txt cmake/  # expect: no changes to dependency-fetching files

# No src/shader_system/, no src/runtime/ source
find src/shader_system src/runtime 2>/dev/null  # expect: nothing (dirs do not exist)

# No general descriptor-set/sampler/general-Texture type introduced
grep -rn "class Sampler\|VkDescriptorSetLayout\|VkDescriptorPool" src/rhi src/renderer  # expect: no matches in
                                                                                          #   src/rhi (VkDescriptorPool
                                                                                          #   may legitimately appear
                                                                                          #   inside Vulkan Backend's
                                                                                          #   own uniform-binding
                                                                                          #   implementation, Section 10)

# Each Buffer/Texture individually allocated -- no shared VkDeviceMemory
grep -rn "std::vector<VkDeviceMemory>\|VkDeviceMemory\[\]" src/vulkan_backend  # expect: no matches (exactly one
                                                                                #   VkDeviceMemory member per
                                                                                #   VulkanBuffer/VulkanTexture instance)

# Descriptor-set/pool machinery never leaks outside vulkan_pipeline.*/vulkan_device.*
grep -rln "VkDescriptorSet\|VkDescriptorPool\|VkDescriptorSetLayout" src/vulkan_backend | grep -v "vulkan_pipeline\|vulkan_device"
                                                                                # expect: no matches

# Shader assets live under shaders/, never under tests/
find tests -iname "*.glsl" -o -iname "*.spv" 2>/dev/null   # expect: nothing
find shaders/minimal_renderer -type f 2>/dev/null          # expect: minimal_mesh.{vert,frag}.{glsl,spv}, README.md

# Demo/test never use a raw Win32 file-path/module API to locate shader assets
grep -rln "GetModuleFileNameW\|GetModuleFileName\b" examples/minimal_renderer_demo tests/vulkan_backend/minimal_renderer_gpu_tests.cpp
                                                                                # expect: no matches
grep -n "windows.h" examples/minimal_renderer_demo/main.cpp tests/vulkan_backend/minimal_renderer_gpu_tests.cpp
                                                                                # expect: no matches -- shader loading
                                                                                #   in these two files is std::ifstream
                                                                                #   over a plain relative path only

# vkGetPhysicalDeviceFeatures2KHR is only ever called through the resolved function pointer,
# never assumed to be statically linkable
grep -n "vkGetPhysicalDeviceFeatures2KHR" src/vulkan_backend/src/vulkan_instance.cpp src/vulkan_backend/src/vulkan_device.cpp
                                                                                # expect: exactly one direct call --
                                                                                #   the vkGetInstanceProcAddr()
                                                                                #   resolution itself in
                                                                                #   vulkan_instance.cpp; every other
                                                                                #   use goes through the stored
                                                                                #   function pointer, not the bare
                                                                                #   symbol name
```

Code-review checklist (manual, per step):

- [ ] Every new `VkResult`-returning call's result is checked.
- [ ] Every new public type documents its thread-safety contract in one
      line, per AGENTS.md.
- [ ] No `ATLANTIS_ASSERT`/`ATLANTIS_CHECK` used where a `Result::Err`
      belongs, or vice versa (cross-check against Section 14's table).
- [ ] `Pipeline`'s viewport/scissor are dynamic state, never baked in —
      confirm no `VkViewport`/`VkRect2D` appears in `VulkanPipeline`'s
      own `VkGraphicsPipelineCreateInfo` construction.
- [ ] Every checked-in `.spv` file has a corresponding `.glsl` source
      file and a `README.md` compiler/version note in the same commit.

---

## 17. Build Integration

```cmake
# src/rhi/CMakeLists.txt -- header-only additions (buffer.h, texture.h,
# pipeline.h are pure interface headers, same as render_target.h/
# command_list.h/submission_signal.h before them); no new .cpp beyond
# whatever types.cpp needs for the new structs' operator== (if any is
# added, following Extent2D/ClearColorValue precedent)

# src/vulkan_backend/CMakeLists.txt
add_library(atlantis_vulkan_backend STATIC
  src/vulkan_result.cpp
  src/validation.cpp
  src/vulkan_instance.cpp
  src/vulkan_device.cpp
  src/vulkan_presentation.cpp
  src/vulkan_render_target.cpp
  src/vulkan_command_list.cpp
  src/vulkan_submission_signal.cpp
  src/resource_state_mapping.cpp
  src/vulkan_buffer.cpp        # new
  src/vulkan_texture.cpp       # new
  src/vulkan_pipeline.cpp      # new
  src/vulkan_memory.cpp        # new
  src/dynamic_rendering.cpp    # new
  src/wsi/win32_surface.cpp
)
# target_link_libraries unchanged

# src/render_graph/CMakeLists.txt -- unchanged (execution.cpp already
# exists as a source; no new file added, only its contents grow)

# src/renderer/CMakeLists.txt -- see Section 1's full listing above

# tests/vulkan_backend/CMakeLists.txt
add_executable(atlantis_vulkan_backend_tests
  vulkan_result_tests.cpp
  presentation_logic_tests.cpp
  validation_policy_tests.cpp
  resource_state_mapping_tests.cpp
  dynamic_rendering_tests.cpp    # new
  vulkan_memory_tests.cpp        # new
)
# ... (target_include_directories/target_link_libraries unchanged from
# the existing file, per Plan 0006's own precedent)

add_executable(atlantis_vulkan_backend_gpu_tests
  vulkan_presentation_gpu_tests.cpp
  frame_execution_gpu_tests.cpp
  minimal_renderer_gpu_tests.cpp   # new
)
target_link_libraries(atlantis_vulkan_backend_gpu_tests
  PRIVATE
    Atlantis::VulkanBackend
    Atlantis::Platform
    Atlantis::RHI
    Atlantis::RenderGraph
    Atlantis::Renderer            # new -- this GPU test exercises Renderer::drawFrame() too
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)
# LABELS "gpu" / DISCOVERY_MODE PRE_TEST unchanged -- but this existing
# catch_discover_tests() call gains a new WORKING_DIRECTORY argument, so
# every discovered test case (not only minimal_renderer_gpu_tests.cpp's
# own new cases) launches with this target's own build output directory
# as its current working directory -- required for the plain relative
# shader path this Plan uses (Section 12); harmless for every pre-
# existing test case in this file, none of which opens any file by a
# relative path today:
catch_discover_tests(atlantis_vulkan_backend_gpu_tests
  DISCOVERY_MODE PRE_TEST
  PROPERTIES LABELS "gpu"
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>"
)

# Section 12's shared shader artifacts, copied next to this executable's
# own build output -- see Section 12 for why a copy, not a baked
# absolute path. copy_if_different is a no-op when already up to date,
# safe to re-run on every build.
add_custom_command(TARGET atlantis_vulkan_backend_gpu_tests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/shaders/minimal_renderer/minimal_mesh.vert.spv"
      "${CMAKE_SOURCE_DIR}/shaders/minimal_renderer/minimal_mesh.frag.spv"
      "$<TARGET_FILE_DIR:atlantis_vulkan_backend_gpu_tests>/shaders/"
)

# tests/rhi/CMakeLists.txt -- add buffer_texture_pipeline_tests.cpp
# tests/render_graph/CMakeLists.txt -- add attachment_execution_tests.cpp

# tests/renderer/CMakeLists.txt (new file)
add_executable(atlantis_renderer_tests
  renderer_ownership_tests.cpp
)
target_include_directories(atlantis_renderer_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/render_graph   # reuses fake_command_list.h -- see Section 15
)
target_link_libraries(atlantis_renderer_tests
  PRIVATE
    Atlantis::Renderer
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)
catch_discover_tests(atlantis_renderer_tests DISCOVERY_MODE PRE_TEST)

# examples/minimal_renderer_demo/CMakeLists.txt -- mirrors
# examples/frame_execution_demo/CMakeLists.txt, + Atlantis::Renderer link,
# + the same shader-copy add_custom_command() pattern as
# atlantis_vulkan_backend_gpu_tests above, targeting
# atlantis_minimal_renderer_demo instead, + a convenience run target so
# a human always launches it with the correct working directory (Section
# 12) regardless of where the build/run command itself was invoked from:
add_executable(atlantis_minimal_renderer_demo main.cpp)
target_link_libraries(atlantis_minimal_renderer_demo PRIVATE
  Atlantis::Core Atlantis::Platform Atlantis::RHI Atlantis::VulkanBackend
  Atlantis::RenderGraph Atlantis::Renderer atlantis_compiler_warnings)

add_custom_command(TARGET atlantis_minimal_renderer_demo POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
      "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>/shaders"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/shaders/minimal_renderer/minimal_mesh.vert.spv"
      "${CMAKE_SOURCE_DIR}/shaders/minimal_renderer/minimal_mesh.frag.spv"
      "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>/shaders/"
)

add_custom_target(run_minimal_renderer_demo
  COMMAND $<TARGET_FILE:atlantis_minimal_renderer_demo>
  WORKING_DIRECTORY "$<TARGET_FILE_DIR:atlantis_minimal_renderer_demo>"
  DEPENDS atlantis_minimal_renderer_demo
)

# CMakeLists.txt (root)
add_subdirectory(src/core)
add_subdirectory(src/platform)
add_subdirectory(src/rhi)
add_subdirectory(src/vulkan_backend)
add_subdirectory(src/render_graph)
add_subdirectory(src/renderer)          # new

if(ATLANTIS_BUILD_EXAMPLES)
  add_subdirectory(examples/foundation_demo)
  add_subdirectory(examples/platform_demo)
  add_subdirectory(examples/rhi_vulkan_demo)
  add_subdirectory(examples/frame_execution_demo)
  add_subdirectory(examples/minimal_renderer_demo)   # new
endif()

if(ATLANTIS_BUILD_TESTS)
  enable_testing()
  include(cmake/AtlantisDependencies.cmake)
  add_subdirectory(tests/core)
  add_subdirectory(tests/platform)
  add_subdirectory(tests/rhi)
  add_subdirectory(tests/vulkan_backend)
  add_subdirectory(tests/render_graph)
  add_subdirectory(tests/renderer)     # new
endif()
```

---

## 18. Implementation Order

Each step ends with the relevant Section 15 build/test commands
(GPU commands only where noted) and Section 16's grep checklist.

1. **RHI type/interface additions** — `types.h` additions (Section 2),
   `buffer.h`, `texture.h`, `pipeline.h`, `device.h`/`command_list.h`
   signature additions (declarations only). Build: header compilation
   only.
2. **`vulkan_memory.{h,cpp}` and `dynamic_rendering.{h,cpp}`** — the two
   pure decision functions, independent of any concrete resource type or
   real Vulkan call yet. GPU-independent unit tests
   (`vulkan_memory_tests.cpp`, `dynamic_rendering_tests.cpp`) land here,
   including the full five-boolean truth table (Section 15).
3. **`vulkan_instance.cpp`'s instance-extension query/enable + entry-
   point resolution, then `VulkanDevice`'s dynamic-rendering capability
   selection** — `vulkan_instance.cpp` gains the
   `VK_KHR_get_physical_device_properties2` availability check,
   conditional enablement, and `vkGetPhysicalDeviceFeatures2KHR`
   resolution via `vkGetInstanceProcAddr` (Section 8); `vulkan_device.cpp`
   extends the existing physical-device selection loop to call
   `decideDynamicRenderingPath()` per candidate, adds
   `DeviceCreateError::DynamicRenderingUnavailable`, and resolves/stores
   the device-level entry-point function pointers (core vs. `KHR`).
   Build-verify only this step (no consumer yet); confirm existing
   `createDevice()` GPU tests still pass unmodified (regression check for
   both the instance- and device-level loop extensions).
4. **`VulkanBuffer`/`VulkanTexture` + `resource_state_mapping.{h,cpp}`
   extensions** — concrete resource classes, `VulkanDevice::createBuffer()`/
   `createTexture()`, the two new transition-table rows. GPU-independent:
   extend `resource_state_mapping_tests.cpp`. First GPU-required smoke
   test: create/destroy a `Buffer` of each purpose and a depth `Texture`,
   no drawing yet.
5. **`VulkanPipeline`** — `VulkanDevice::createPipeline()`, including
   `VulkanDevice`'s one-time descriptor pool creation (Section 10's
   camera-uniform-binding design), using Section 12's checked-in `.spv`
   files (this step is when those files, and the shader-copy
   `add_custom_command()`s that reference them, are added, alongside the
   `.glsl` source and `README.md`). GPU test: create/destroy a `Pipeline`
   successfully, confirming its one allocated `VkDescriptorSet` is freed
   without error.
6. **`VulkanCommandList` draw path** — `bindPipeline`/`bindVertexBuffer`/
   `bindIndexBuffer`/`bindUniformBuffer`/`pushConstant`/`drawIndexed`/
   `beginRendering`/`endRendering`. GPU test: manually record and submit
   one full draw (no RenderGraph yet — direct `CommandList` calls in the
   test, to isolate this layer from Section 7's RenderGraph changes)
   against a real acquired `RenderTarget` and depth `Texture`, Validation
   Layers clean.
7. **RenderGraph: `ResourceBinding` extension + `execute()` algorithm** —
   Section 6–7. GPU-independent: `attachment_execution_tests.cpp`
   (every bullet from Section 15), plus confirming
   `execution_tests.cpp`'s existing cases are unaffected.
8. **`src/renderer/` module** — `Mesh`/`createMesh()`,
   `Material`/`createMaterial()`, `DrawItem`, `Renderer::drawFrame()`.
   GPU-independent: `renderer_ownership_tests.cpp` (fake `CommandList`).
9. **Full GPU integration** — `minimal_renderer_gpu_tests.cpp`: the
   complete `Buffer`/`Texture`/`Pipeline`/`Renderer::drawFrame()`/
   attachment-scope/multi-draw-item path against a real device,
   Validation Layers clean. `ctest -C Debug -L gpu`.
10. **`examples/minimal_renderer_demo`** — the interactive manual
    verification composition (Section 15), including Section 13's
    resize/format-change contract implementation. Run interactively:
    mesh visible, camera transform visible, resize (extent-only, no
    `Pipeline` rebuild), format-change if the environment allows it,
    minimize/restore, mid-frame exit, clean exit.
11. **Final consistency pass** — Section 16's full grep checklist, this
    Plan's Section 19 Acceptance Criteria mapping walked line by line,
    `cmake --build build --config Release` clean, full `ctest` suite
    (both `-LE gpu` and `-L gpu`, Debug and Release) green.

### Sequencing & Dependencies

Steps 1–2 have no cross-dependency. Step 3 depends on 1 (needs
`DeviceCreateError`) and 2 (needs `decideDynamicRenderingPath()`). Step 4
depends on 1–3. Step 5 depends on 1, 4 (needs `resource_state_mapping`'s
new rows conceptually, though `Pipeline` creation itself does not call
`transitionResource()`). Step 6 depends on 3–5. Step 7 (RenderGraph) has
no dependency on 3–6 and could be built in parallel by a different
reviewer, using `fake_command_list.h`, but Step 9's full integration
needs both 6 and 7 complete. Step 8 depends on 1 (RHI types) and could
otherwise be built in parallel with 3–7, but its own GPU-independent
tests (`renderer_ownership_tests.cpp`) benefit from Step 7's
`fake_command_list.h` extensions already existing. Step 9 needs 6, 7,
and 8. Step 10 needs 9. Step 11 is last, always.

---

## 19. Acceptance Criteria Mapping

| Spec 0007 Acceptance Criterion (abbreviated) | Plan Section(s) |
|---|---|
| No `Vk*`/Vulkan header in RHI/RenderGraph public headers | §1 file list, §16 grep |
| No `vkCmd*`/barrier/`VkRenderPass`/`VkFramebuffer` outside Vulkan Backend | §10, §16 grep |
| `Buffer`/`Texture`/`Pipeline` move-only | §3 (mechanism) |
| Every `Buffer`/`Texture` individually allocated, no shared `VkDeviceMemory` | §9, §16 grep |
| `Renderer` retains no GPU resource across calls | §11 (no member state), §15 (`renderer_ownership_tests.cpp`) |
| `Mesh`/`Material` never created/cached/looked-up by `Renderer` | §11 (`createMesh`/`createMaterial` are free functions, not `Renderer` methods) |
| `createDevice()` dual-path detection + explicit error when unavailable | §8, §15 |
| No capability/path indicator in RHI/RenderGraph public headers | §8 (entry points stored `VulkanDevice`-private) |
| Guard 1 generalized to depth `Texture` bindings | §7 algorithm step 1 |
| Guard 2 unchanged scope (`target` only) | §7 algorithm step 2 |
| `execute()` never wraps `ColorAttachmentWrite` in attachment scope | §7 (draw-pass recognition), §16 grep, §15 (regression test) |
| Depth declared as exactly one `writes()` usage | §11 step 1 (`builder.writes(pass, depthResource, DepthAttachmentReadWrite)` — single call) |
| Push constant, not shared uniform, for per-object transform | §5, §10, §15 (multi-`DrawItem` regression test) |
| `Pipeline` uses dynamic viewport/scissor — no rebuild on extent-only resize | §3, §13 step 2, §15 (demo log/assertion) |
| Format change observed via `Presentation::metadata()`, `waitIdle()` before rebuild | §13 step 1 |
| `Renderer` has no format/extent-observing code path | §11 (signature has no `Device&`/`Presentation&`), §16 grep |
| No shader compiler/reflection invoked by any build target | §12, §16 grep |
| Checked-in `.spv` has matching source + compiler note | §12, §16 checklist |
| Every `VkResult` checked | §8–§10 (every step "check"s), §16 checklist |
| Validation Layers clean, Debug + GPU CI | §15 |
| Manual demo: visible mesh, camera transform, depth occlusion, resize, minimize/restore, clean exit | §15 manual verification |
| No scene graph/ECS/asset system/second material/instanced draw/multi-frame-in-flight | §Non-Goals, §16 grep |
| No `src/renderer/` dependency on Platform/Win32/Android NDK/`Vk*` | §1 (link boundary), §16 grep |
| Camera uniform binding is a fixed, single-purpose mechanism — no general descriptor-set/bindless API | §10 (full descriptor pool/layout/set design), §16 grep |
| RenderGraph never wraps a non-draw pass in an attachment scope; draw-pass identification is UB-safe under a non-terminating handler | §7 algorithm step 4 (check-then-skip), §15 |

---

## Verification Checklist

- [ ] Unit tests: all GPU-independent suites listed in §15 (layer 1) —
      pass under `ctest -C Debug -LE gpu` and `ctest -C Release -LE gpu`.
- [ ] Headless integration tests: not applicable (Spec 0007 Non-Goals;
      windowed-first sequencing).
- [ ] Image regression tests: not applicable (manual visual verification
      only this round, per Spec 0007's own stated limitation).
- [ ] Vulkan Validation Layers clean: `ctest -C Debug -L gpu`, the
      Release equivalent, and the manual `minimal_renderer_demo` run
      (including the mid-frame-exit and, where the environment allows,
      the format-change case) all produce zero WARNING/ERROR output.
- [ ] Other: §16's grep checklist returns the documented expected
      (empty, in every prohibited case) result.

## Rollback Plan

Every change is additive to existing, already-shipped modules
(`atlantis_rhi`, `atlantis_vulkan_backend`, `atlantis_render_graph`) plus
one wholly new module (`atlantis_renderer`) — no existing public method
signature is altered or removed (`ResourceBinding`'s extension keeps its
existing `resource`/`target` fields; `ColorAttachmentWrite`'s meaning is
untouched). If a defect is found post-merge, revert is a straightforward
`git revert` of this feature's merge commit(s) — nothing outside this
Plan's own new module and test/demo targets depends on anything it adds.
A partial rollback (keep RHI/Vulkan Backend resource types, revert
RenderGraph attachment integration and/or `src/renderer/`) is possible
but not expected to be needed, since Section 18's steps are ordered so
each layer (RHI resources §1–6, RenderGraph §7, Renderer §8) is
independently reviewable before the next builds on it.

## Definition of Done

Per [docs/process/definition-of-done.md](../docs/process/definition-of-done.md),
with these Plan-specific notes:

- Headless verification: not applicable (Non-Goal, unchanged from every
  prior spec/plan's own DoD treatment of this item).
- Image regression: not applicable this round (no golden-image harness
  exists yet, per AGENTS.md sequencing).
- ADR: none new required by implementation — ADR-0022–0027 already
  `Accepted` cover every architectural decision this Plan operationalizes;
  any *deviation* from them discovered during implementation is a
  Plan/Spec issue to raise (a Human Review blocker, not a new ADR to file
  unilaterally) — see the next section.
- All other items apply as stated in the linked document.

## Human Review Blockers (if triggered during Implementation)

Per AGENTS.md, any of the following discovered during implementation
must stop work and return to Spec/Plan/ADR review — **not** be decided
unilaterally in a PR:

- Any need to change `Buffer`/`Texture`/`Pipeline`'s move-only ownership
  model, or to introduce shared/reference-counted ownership for any GPU
  resource this Plan touches.
- Any need for `Renderer` to retain state, own a GPU resource, or read
  `Presentation::metadata()`/observe a format change itself.
- Any need to widen Guard 2 to depth `Texture` bindings, or to relax
  ADR-0018's same-pass read+write rejection.
- Any need for a `VkRenderPass`/`VkFramebuffer` fallback path, or to
  raise the Vulkan Backend's overall minimum supported API version.
- Any need for a general GPU memory suballocator, a shared
  `VkDeviceMemory` block, or a resource cache/pool of any kind.
- Any need to invoke a shader compiler from a build target, perform
  SPIR-V reflection, or introduce shader caching/hot-reload.
- Any need for a second graphics backend, a new third-party dependency,
  or a module boundary this Plan does not already list.

## Post-Approval Deviation Record (Under Review) — 2026-08-13

**This section records a post-approval blocker, per this repository's own
precedent for a Plan amendment (no distinct "Deviation"/"Blocker" heading
exists yet in Plan 0005 or Plan 0006 — neither needed one — so this
section establishes that precedent for Plan 0007, following the same
principle both plans' own "Human Review Blockers" sections already state:
a real deviation discovered during implementation stops work and returns
to Spec/Plan/ADR review, it is not decided unilaterally in a PR.)** This
Plan's own Status (`Approved / Ready for Implementation`) and its §2–§13
content are **unchanged** by this record — this is an addendum, not a
rewrite.

**What happened:** Implementation proceeded against this Plan's §8 as
approved (Human Review Approval note, point 3, top of this document).
Post-merge verification (after PR #28 merged) found the shipped Core path
does not match §8/ADR-0024's approved design — it unconditionally
requests, enables, and resolves through `VK_KHR_dynamic_rendering` even
on a Vulkan 1.3+ Core-path device, a real, called-out implementation
deviation (see the comment block in `vulkan_device.cpp`'s `createDevice()`
beginning "Implementation-forced deviation from Plan 0007 Section 8's
stated 'Core needs no device extension'"). A subsequent fix attempt
(branch `fix/0007-dynamic-rendering-core-path`, reverted, no commits
landed) implemented the textbook-correct Core path and, via controlled
experiments on this repository's real development hardware, found that a
functioning, genuinely-core (non-`KHR`-suffixed) dynamic-rendering entry
point is not reliably obtainable while the Vulkan Backend's instance
keeps requesting `VkApplicationInfo::apiVersion = VK_API_VERSION_1_0` —
exactly the value §8's own "Instance-level prerequisite" subsection fixed
and Human Review approved. This is precisely the Human Review Blocker
this Plan's own list (above) already names: *"Any need... to raise the
Vulkan Backend's overall minimum supported API version."* Per that
section's own rule, and per AGENTS.md, this was correctly **not** decided
unilaterally in the fix attempt's PR — the fix attempt was reverted, and
this docs-only review was conducted instead.

**Current blocker:** Implementation of §8's dynamic-rendering Core path
(and any dependent §10/§15 work that assumes a working Core path on this
repository's own development hardware) is **paused** pending Human Review
of the proposed amendment recorded in
[ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)'s
"Proposed Amendment (Under Review)" section. That amendment proposes
raising the *instance's* requested `apiVersion` to 1.3 when (and only
when) the Vulkan loader itself reports at least 1.3 (queried via
`vkEnumerateInstanceVersion` before `vkCreateInstance()`), while leaving
every physical-device-level selection criterion, the Extension path, and
every device population this Plan's own compatibility analysis already
committed to serving (per §8's "Windows test coverage / future Android
boundary" note) unaffected — see that ADR section's own Compatibility
impact analysis for the full argument, including a Human-Review-Blocker-
tier open item of its own (whether a literal reading of the reviewed
candidate strategy's "error when loader version is insufficient" clause
was actually intended, which this review's own analysis recommends
rejecting).

**What resuming implementation looks like, once the ADR-0024 amendment is
approved:** this Plan's own §8, §10, and §15 are expected to need a
follow-up Plan revision (not a silent implementation-time reinterpretation)
covering, at minimum:

- `vulkan_instance.cpp`: add the loader-version query (§8's existing
  `VK_KHR_get_physical_device_properties2` query precedent shows the
  pattern) and the conditional `apiVersion` request, per the amended
  ADR-0024 Decision.
- `dynamic_rendering.h`/`.cpp`: extend `decideDynamicRenderingPath()`'s
  signature with the new `instanceRequestedApiVersionAtLeast1_3` boolean
  and its added Core-branch condition; extend
  `tests/vulkan_backend/dynamic_rendering_tests.cpp`'s truth table
  accordingly.
- `vulkan_device.cpp`: remove the as-shipped Core-path deviation (the
  unconditional `VK_KHR_dynamic_rendering`-and-dependency-chain
  enablement and `KHR`-suffixed-only resolution) — Core path reverts to
  §8's originally-approved shape (no KHR extension, unsuffixed entry
  points via `vkGetDeviceProcAddr`), now genuinely correct because the
  owning instance requests `apiVersion 1.3` whenever the loader supports
  it.
- `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` and the manual
  verification composition: re-run on this repository's real development
  hardware to confirm the fix attempt's own crash no longer reproduces,
  per ADR-0024's amended Section 6 verification plan.
- No RHI, RenderGraph, or `src/renderer/` public-surface change is
  expected — this remains entirely Vulkan-Backend-internal, consistent
  with ADR-0024's original and amended Boundary statement.

## Consistency Review

Walked against Spec 0007 in full:

- **Every Functional Requirement** (Renderer module, `Mesh`/`Material`,
  minimal RHI GPU resources, minimal pipeline/binding/draw surface,
  RenderGraph multi-attachment integration, resize/format-change
  lifecycle, minimal material, threading contracts) has a corresponding
  Plan section (§2–§13) and a test (§15).
- **Every Non-Goal** is checked against §16's grep list or explicitly
  absent from §1's file list (no Shader System, no scene graph/ECS/asset
  system, no second material/lighting/texturing, no GPU-driven/bindless/
  instanced/indirect draw, no multi-frame-in-flight, no general
  allocator, no second backend, no general descriptor system, no
  cross-owner shared ownership).
- **Every Acceptance Criterion** is mapped in §19.
- **No `Accepted` ADR's conclusion is restated, reopened, or
  contradicted** — ADR-0018's same-pass read+write rejection is applied,
  not modified (§11 step 1); ADR-0019's `RenderTarget`-specific
  always-`Undefined` rule is extended to the depth `Texture` binding by
  this Plan's own §7 algorithm design, exactly as ADR-0026 already
  authorized, not by editing ADR-0019 itself; ADR-0020's `ColorAttachmentWrite`
  keeps its exact existing meaning and mapping (§10); ADR-0021's
  "RenderGraph decides when, RHI decides how" split is extended to
  attachment scoping (§7), not replaced; ADR-0023's direct allocation
  policy is implemented exactly as described, with no pooling introduced
  (§9); ADR-0024's dual path is implemented with no minimum-version
  change (§8); ADR-0025's format-change contract is implemented as a
  concrete algorithm (§13), not reinterpreted.
- **Candidate API status:** every signature in §2–§13 was a Plan-stage
  candidate per Spec 0007's own "concrete C++ type/method names... left
  to the Plan" framing (mirroring Spec 0005's and Spec 0006's identical
  disclaimer) until the joint Spec 0007 + Plan 0007 Human Review reviewed
  and approved this Plan in full (see the Human Review Approval note at
  the top of this document) — implementation follows these signatures as
  written, per AGENTS.md, not as still-open candidates.
