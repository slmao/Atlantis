# Plan: Minimal Renderer

- **Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md) (`Approved`)
- **Status:** Draft
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; content authored by the agent, pending Human Review.

## Objective

Turn Spec 0007's approved contract — a real, visible, depth-tested mesh
drawn through `Renderer` → RenderGraph → RHI → Vulkan Backend — into an
ordered, reviewable implementation plan. This plan's candidate C++
signatures, algorithms, and file layout (§2–§13) are **Plan-stage
candidates, not yet Human-Review-approved** — per Spec 0007's own
"concrete C++ type/method names... left to the Plan" framing (mirroring
every prior Plan in this line), they are proposed here for review, not
authorized for implementation until this Plan itself passes Human
Review, per [AGENTS.md](../AGENTS.md).

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

## Candidate-API Disclaimer

Every C++ signature, file name, and algorithm in §2–§13 is a **Plan-stage
candidate**. Per AGENTS.md and this repository's own precedent (Plan
0005 §Candidate-API note, Plan 0006's identical disclaimer), naming this
explicitly is not a hedge against review — it is what makes this
document reviewable as a plan rather than a fait accompli. Implementation
may begin only after this Plan reaches `Approved / Ready for
Implementation` at Human Review, and then only exactly as written unless
a deviation is called out explicitly in the implementation PR
(AGENTS.md's explicit-deviation rule).

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

tests/assets/shaders/minimal_mesh.vert.glsl              # human-readable source (checked in, never built)
tests/assets/shaders/minimal_mesh.frag.glsl
tests/assets/shaders/minimal_mesh.vert.spv               # pre-compiled bytecode (checked in, never built)
tests/assets/shaders/minimal_mesh.frag.spv
tests/assets/shaders/README.md                           # exact compiler/version/command-line note (ADR-0027)
tests/vulkan_backend/shader_asset_path.h.in               # CMake-configured absolute path to tests/assets/shaders/

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
src/vulkan_backend/src/vulkan_instance.cpp   # apiVersion request unchanged (still requests the loader's
                                             #   highest available -- see §8); no change expected here beyond
                                             #   what §8 identifies
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
`execution.h` gains `#include <atlantis/rhi/texture.h>`.

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
       target = binding.target != nullptr ? binding.target : nullptr       // (as rhi object reference)
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
       depthBinding = the bindings entry (if any) whose resource has a DepthAttachmentReadWrite usage in pass
       commandList.beginRendering(*colorBinding.target,
                                   depthBinding ? depthBinding.depthTexture : nullptr,
                                   colorBinding ? colorBinding.colorClear : ClearColorValue{},
                                   depthBinding ? depthBinding.depthClear : 1.0f)

     if pass has an executeFn: executeFn(commandList)

     if isDrawPass:
       commandList.endRendering()

5. for each entry b in bindings where b.target != nullptr:
     if currentState.count(b.resource):
       last = currentState[b.resource]
       if last != ResourceState::PresentSource:
         commandList.transitionResource(*b.target, last, ResourceState::PresentSource)
   -- no trailing transition for b.depthTexture entries (ADR-0026 -- never presented, never read back this round)
```

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
if apiVersionAtLeast1_3 && coreFeatureSupported: return Core
if extensionAdvertised && extensionFeatureSupported: return Extension
return Unavailable
```

**Real capability query wrapper** (inside `vulkan_device.cpp`'s existing
physical-device selection loop, extended — not replaced): for each
physical-device candidate that already passes the pre-existing suitability
criteria (queue families, etc., unchanged from Spec 0003), additionally:

1. `vkGetPhysicalDeviceProperties()` → `apiVersionAtLeast1_3 =
   (properties.apiVersion >= VK_API_VERSION_1_3)`.
2. If `apiVersionAtLeast1_3`: `vkGetPhysicalDeviceFeatures2()` with a
   `VkPhysicalDeviceVulkan13Features` chained into `pNext` →
   `coreFeatureSupported`.
3. `vkEnumerateDeviceExtensionProperties()` → `extensionAdvertised =
   ` (`VK_KHR_dynamic_rendering` present in the returned list).
4. If `extensionAdvertised`: `vkGetPhysicalDeviceFeatures2()` with a
   `VkPhysicalDeviceDynamicRenderingFeaturesKHR` chained into `pNext` →
   `extensionFeatureSupported`.
5. Call `decideDynamicRenderingPath()` with the four booleans above.
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
itself makes no Windows-specific Vulkan call — `vkGetPhysicalDeviceFeatures2`,
`vkEnumerateDeviceExtensionProperties`, and the two feature-chain structs
are all core/WSI-independent Vulkan API, so this Plan's implementation is
expected to require no changes for a future Android Platform/Vulkan
Backend spec to reuse directly — that future spec still owns its own
decision about Android's actual device/driver support distribution, per
ADR-0024's own Boundary note.

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

**Destruction** (each resource's own destructor, RAII, mirroring
existing `SwapchainGuard`-style two-phase-construction guard patterns
already used in `vulkan_presentation.cpp`/`vulkan_device.cpp` for
partial-construction-failure safety): `vkDestroyImageView()` (Texture
only) → `vkFreeMemory()` (this resource's own individual allocation,
`vkMapMemory()`'d Buffer memory needs no explicit unmap before free —
`vkFreeMemory()` implicitly unmaps) → `vkDestroyBuffer()`/
`vkDestroyImage()`.

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

`VulkanPipeline final : public rhi::Pipeline` (`vulkan_pipeline.h/.cpp`):
holds `VkPipeline`, `VkPipelineLayout` (owns the push-constant range and
the — this round, empty, since no descriptor-set system exists —
descriptor-set-layout list), and the two `VkShaderModule` handles
(destroyed immediately after `vkCreateGraphicsPipelines()` succeeds, per
standard Vulkan practice — a `VkShaderModule` is not needed after
pipeline creation). Constructed only via `VulkanDevice::createPipeline()`,
using `VkPipelineRenderingCreateInfo` (naming `colorFormat`/`depthFormat`
directly, per ADR-0024) instead of a `VkRenderPass` handle, and
`VK_DYNAMIC_STATE_VIEWPORT`/`VK_DYNAMIC_STATE_SCISSOR` in
`VkPipelineDynamicStateCreateInfo` (ADR-0025).

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
- `bindUniformBuffer(Buffer&)` → `ATLANTIS_CHECK(buffer.purpose() == BufferPurpose::Uniform)`; this
  round has no descriptor-set system (ADR-0025), so binding happens via a Vulkan Backend-internal
  mechanism appropriate to a single, fixed uniform binding slot — exact mechanism (a single
  pre-allocated descriptor set updated once per bind call, vs. `VK_EXT_descriptor_buffer` if
  available) is an Implementation-stage detail this Plan does not fix further; either way, no public
  RHI surface is added beyond `bindUniformBuffer()` itself.
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
class Renderer {
 public:
  Renderer() = default;
  ~Renderer() = default;

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  Renderer(Renderer&&) = delete;
  Renderer& operator=(Renderer&&) = delete;

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

Per ADR-0027, checked in under `tests/assets/shaders/` (shared by both
the GPU test suite and the demo, avoiding duplicate authoring):

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

**No CMake target compiles, parses, or reflects any of the above** — the
`.spv` files are read as raw bytes at runtime by whichever code calls
`Device::createPipeline()` (the GPU test and the demo, each
independently), via `tests/vulkan_backend/shader_asset_path.h.in`, a
`configure_file()`-generated header providing an absolute,
build-independent path to `tests/assets/shaders/` (a standard pattern
for test fixtures that must be located by an absolute path regardless of
the build output directory — never used by any non-test/non-demo
target).

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
     material.reset()  // destroys the old Pipeline
     material = createMaterial(*device, {..., .colorFormat = currentFormat, .depthFormat = DepthFormat::D32Sfloat, ...})
     lastSeenFormat = currentFormat
     // depthTexture is NOT recreated here solely for a format change --
     // its own format (D32Sfloat) never varies with the swapchain's
     // color format this round

2. currentExtent = renderTarget->extent()
   if currentExtent != lastSeenExtent:
     depthTexture = device->createTexture({.extent = currentExtent, .format = DepthFormat::D32Sfloat})
     lastSeenExtent = currentExtent
     // Pipeline is NOT recreated here -- dynamic viewport/scissor (Section 3)

3. Write this frame's view/projection into cameraBuffer->mappedData()   -- see Section 9's
   write-timing note; safe here because acquireNextTarget()'s own internal drain (Spec 0006/
   PR #24) already guarantees no GPU work from the prior frame is still reading this Buffer

4. renderer.drawFrame(*commandList, *renderTarget, *depthTexture, *cameraBuffer, drawItems)
```

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
  full decision table: `(true, true, *, *) → Core`; `(true, false, true, true) → Extension`;
  `(false, *, true, true) → Extension`; `(false, *, false, *) → Unavailable`; `(true, false, true, false) → Unavailable`;
  `(false, *, true, false) → Unavailable`.
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
- **Headless integration tests:** not applicable — unchanged from Spec 0006's equivalent flag.
- **Image regression tests:** not applicable — manual visual verification only (below), a real,
  accepted limitation Spec 0007 itself already states.

### Manual verification (`examples/minimal_renderer_demo`)

Mirrors `examples/frame_execution_demo`'s own structure and non-shipping
disclaimer (Spec 0007 Non-Goals). Confirms, interactively:

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
# LABELS "gpu" / DISCOVERY_MODE PRE_TEST unchanged

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

# tests/vulkan_backend/shader_asset_path.h.in -> configure_file()'d into
# the build tree; consumed only by minimal_renderer_gpu_tests.cpp and
# examples/minimal_renderer_demo/main.cpp

# examples/minimal_renderer_demo/CMakeLists.txt -- mirrors
# examples/frame_execution_demo/CMakeLists.txt, + Atlantis::Renderer link

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
   pure decision functions and their real-query wrappers, independent of
   any concrete resource type yet. GPU-independent unit tests
   (`vulkan_memory_tests.cpp`, `dynamic_rendering_tests.cpp`) land here.
3. **`VulkanDevice` dynamic-rendering capability selection** — extends
   the existing physical-device selection loop (Section 8), adds
   `DeviceCreateError::DynamicRenderingUnavailable`, resolves and stores
   entry-point function pointers. Build-verify only this step (no
   consumer yet); confirm existing `createDevice()` GPU tests still pass
   unmodified (regression check for the loop extension).
4. **`VulkanBuffer`/`VulkanTexture` + `resource_state_mapping.{h,cpp}`
   extensions** — concrete resource classes, `VulkanDevice::createBuffer()`/
   `createTexture()`, the two new transition-table rows. GPU-independent:
   extend `resource_state_mapping_tests.cpp`. First GPU-required smoke
   test: create/destroy a `Buffer` of each purpose and a depth `Texture`,
   no drawing yet.
5. **`VulkanPipeline`** — `VulkanDevice::createPipeline()`, using
   Section 12's checked-in `.spv` files (this step is when those files
   are added, alongside their `.glsl` source and `README.md`). GPU
   test: create/destroy a `Pipeline` successfully.
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
- **Candidate API status:** every signature in §2–§13 is a Plan-stage
  candidate per Spec 0007's own "concrete C++ type/method names... left
  to the Plan" framing (mirroring Spec 0005's and Spec 0006's identical
  disclaimer) — pending Human Review of this Plan; implementation may not
  begin until that review approves it, per AGENTS.md.
