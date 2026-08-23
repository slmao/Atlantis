# Plan: Texture & Sampler Foundation

- **Spec:** [specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md)
  (`Approved`, Human Review Approval recorded 2026-08-24, accepting all
  18 items of that Spec's own Human Review Decision Table — see that
  Spec's own approval note for the full record)
- **Status:** In Review
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction, following AGENTS.md's Spec → Plan → Human Review →
  Implementation path. Not yet reviewed/approved — see the Independent
  Review note near the end of this document.

## Objective

Implement Spec 0016 in full: a general, sampled 2D color `SampledTexture`
and an independent `Sampler` RHI type pair; a real CPU→GPU upload path
(staging `Buffer`, `copyBufferToTexture()`, `TransferDestination`→
`ShaderRead`); a `Material`/Shader binding path letting a fragment
shader sample bound textures; a build-time texture cooker/loader
following `Atlantis::AssetSystem`'s established cook/artifact/load
conventions; and a new, independent headless fixture proving both
`Rgba8Unorm` and `Rgba8Srgb` sampling on real GPU hardware, in one
combined `Device::submit()` covering upload, draw, and readback. No
Plan content here decides anything ADR-0055–0057, ADR-0041's own
Accepted Amendment, or Spec 0016's own Human Review Decision Table
already fixed; this Plan only supplies the concrete C++/CMake/file-
format shapes those documents deliberately left open.

## Pre-draft verification against real, current source

Confirmed fresh against the actual repository (not assumed from the
Spec's own citations) immediately before drafting this Plan:

- `src/rhi/CMakeLists.txt`: one library source, `src/types.cpp`; `Texture`/`Buffer`
  are header-only abstract interfaces (no `.cpp`) — `src/rhi/include/atlantis/rhi/texture.h`/`buffer.h`
  each declare only a virtual destructor plus pure-virtual accessors.
  `Device::createTexture()`/`createBuffer()` are declared directly on
  `Device` in `device.h`; their bodies live as `VulkanDevice::createTexture()`/`createBuffer()`
  in `vulkan_device.cpp` — no per-resource-type factory file exists.
  `SampledTexture`/`Sampler` follow this exact shape.
- `src/vulkan_backend/CMakeLists.txt`: one flat library-source list (20
  files); `vulkan_texture.h`/`.cpp`, `vulkan_buffer.h`/`.cpp` are the
  exact naming precedent `vulkan_sampled_texture.h`/`.cpp` and
  `vulkan_sampler.h`/`.cpp` follow.
- **No `FakeDevice` exists anywhere in this repository.** The only test
  double is `tests/render_graph/fake_command_list.h`'s `FakeCommandList`
  (implements `atlantis::rhi::CommandList` fully, recording every call;
  zero real Vulkan device in that test binary) plus trivial
  `FakeRenderTarget`/`FakeTexture`/`FakeBuffer`/`FakePipeline` stand-ins.
  It is reused by `tests/render_graph/` and, via an explicit
  `target_include_directories`, `tests/renderer/`. `tests/rhi/types_tests.cpp`
  tests only free-standing value types (no `Device`, no polymorphism).
  Every real `Device::createTexture()`/`createBuffer()`/`submit()` call
  in the test suite goes through the real `VulkanDevice`, `"gpu"`-labeled.
  **Consequence, binding on this Plan's own Milestone 2 (below):** adding
  a new pure-virtual method to `CommandList` (`copyBufferToTexture()`,
  a third `transitionResource()` overload, `bindTexture()`) or to
  `Device` (`createSampledTexture()`, `createSampler()`) breaks
  compilation of every concrete implementer until updated — that means
  `VulkanCommandList`/`VulkanDevice` **and** `FakeCommandList` (the only
  other `CommandList` implementer in the tree) must all be updated in
  the same, indivisible step. There is no `FakeDevice` to update in
  parallel — `Device`'s only implementer is `VulkanDevice`.
- `src/shader_system/CMakeLists.txt`: two separate library targets,
  `atlantis_shader_system` (Core-only: `json_parser.cpp`,
  `reflection_metadata.cpp`, `reflection_loader.cpp`,
  `slang_json_transform.cpp`, `descriptor_contract.cpp`,
  `command_line.cpp`, `version_provenance.cpp`) and
  `atlantis_shader_system_rhi_integration` (alias
  `Atlantis::ShaderSystemRhiIntegration`, the *only* target depending on
  both `Atlantis::ShaderSystem` and `Atlantis::RHI`:
  `rhi_integration/src/vertex_input_mapping.cpp`) — kept deliberately
  separate (`CMakeLists.txt`'s own comment). `DescriptorType::Sampler`/
  `VertexAttributeType::Float2` go in the Core-only target;
  `toRhiFormat()`'s new `Float2` case goes in the RHI-integration target.
- `src/tools/asset_cooker/CMakeLists.txt`: `atlantis_asset_cooker_lib`
  links `PUBLIC Atlantis::AssetSystem, Atlantis::Core` only — **no
  `Stb::Stb` today.** Carries `/w14062` (MSVC "enumerator not handled in
  switch" promoted to hard warning) — every switch over `AssetKind`
  (`cook_command.cpp`'s own dispatch, and every `xxxErrorMessage()`
  function) must gain its new case in the same step the enum value is
  added, or the build fails.
- `src/tools/asset_cooker/cook_command.h`/`.cpp`: `enum class AssetKind { StaticMesh, Scene };`;
  dispatch is a plain `switch (request.kind)` in `runCookCommand()`
  (`cook_command.cpp:208-217`) calling `runCookMeshMode()`/
  `runCookSceneMode()`, each a private free function following an
  identical shape (compute relative path → strip a fixed authoring
  extension → build artifact/metadata paths → call the corresponding
  `atlantis::asset_system::cookXxx()` → map its error via a dedicated
  `xxxErrorMessage()` → `writeStamp()`). `main.cpp:52-60` parses
  `--kind=mesh|scene` into `AssetKind` via a plain `else if` chain.
- `cmake/AtlantisDependencies.cmake` (48 lines): `include_guard(GLOBAL)`;
  Catch2's `FetchContent_Declare`/`MakeAvailable` at lines 17-23; stb's
  at lines 33-39; the `Stb::Stb` `INTERFACE` target at lines 45-47. Root
  `CMakeLists.txt` calls `add_subdirectory(src/tools/asset_cooker)` at
  line 60, **before** `include(cmake/AtlantisDependencies.cmake)` at
  line 97 (itself inside `if(ATLANTIS_BUILD_TESTS)`, line 95) — confirms
  ADR-0041's own Accepted Amendment finding exactly: `Stb::Stb` does not
  exist as a target at the point the cooker would need it, under
  today's ordering, regardless of `ATLANTIS_BUILD_TESTS`.
- `tests/image_regression/`: goldens are named
  `<scene>/<scene>_<W>x<H>_<format>.png` + `.sidecar.txt` (**not**
  `.meta.txt`) — confirmed against `minimal_cube/minimal_cube_512x512_rgba8unorm.png`/`.sidecar.txt`
  and `world_scene/`'s identical shape, and against
  `golden_generator/main.cpp`'s own path-construction code. The golden
  generator is a standalone, non-CTest-registered tool
  (`atlantis_image_regression_golden_generator <goldenName>`), requiring
  a clean `git` working tree, invoked by a human. `tests/image_regression/fixture/CMakeLists.txt`'s
  own `atlantis_image_regression_fixture` library already links
  `Atlantis::RHI`, `Atlantis::Renderer`, `Atlantis::ImageRegressionSupport`,
  `Atlantis::World` `PUBLIC` and `Atlantis::VulkanBackend`,
  `Atlantis::RenderGraph`, `Atlantis::ShaderSystem`,
  `Atlantis::ShaderSystemRhiIntegration`, `Atlantis::AssetSystem`
  `PRIVATE` — already exactly what a new textured fixture needs, no new
  link edge required.
- `minimal_cube_fixture.cpp`'s exact RHI call order (device →
  vertex/index buffers via `createMesh()` → uniform buffer →
  material/pipeline → depth texture → offscreen target → readback
  buffer; then per-frame: `acquireTarget()` → fill uniform → build
  `DrawItem`s → `createCommandList()` → `Renderer{}.drawFrame(...)` →
  a second, caller-built `RenderGraphBuilder` copy pass →
  `submit(commandList, *target)` → `waitIdle()` → read `readbackBuffer`
  → `target.reset()`) is this Plan's own direct template for the new
  textured fixture, extended by one more graph (upload) at the front —
  see Milestone 9.
- **A genuine Plan-level module-boundary question the Spec left open,
  resolved here, not silently:** `Atlantis::AssetSystem` depends on
  `Atlantis::Core` only (confirmed:
  `src/asset_system/CMakeLists.txt:5-20`, no RHI, no `Stb::Stb`) — but
  ADR-0057's own Decision 4 requires `Stb::Stb` linked **only** into
  `atlantis_asset_cooker_lib`, never into `Atlantis::AssetSystem`'s own
  runtime library. If `atlantis::asset_system::cookTexture()` called
  `stbi_load()` directly, it would need `Stb::Stb` linked into
  `Atlantis::AssetSystem` itself, violating that boundary. **Resolution
  (D1 below): `cookTexture()` takes already-decoded pixel bytes as
  input — the actual `stb_image` decode call lives in
  `src/tools/asset_cooker/cook_command.cpp`'s own new
  `runCookTextureMode()` (which does link `Stb::Stb`), which decodes the
  PNG, then calls `atlantis::asset_system::cookTexture()` with the
  decoded bytes** — mirroring how `cookStaticMesh()`/`cookScene()`
  already receive pre-parsed data, not raw files, from their own
  callers' perspective, and keeping `Atlantis::AssetSystem` itself
  exactly as dependency-narrow as Spec 0012/0015 already established.
  Also: `Atlantis::AssetSystem` must not depend on `Atlantis::RHI` (same
  established boundary) — `TextureAssetData`'s own color-space field
  therefore cannot be typed as `atlantis::rhi::SampledTextureFormat`;
  Asset System gets its own small, independent `TextureColorSpace`
  enum (D2), translated into RHI's `SampledTextureFormat` only by the
  composition root (the new fixture), exactly matching Spec 0015's own
  `DecodedTransform`/`DecodedCamera`/`DecodedRenderable` DTO-decoupling
  precedent (never naming a `world::` type) for the identical reason
  (avoiding a forbidden dependency edge).

No part of Spec 0016 or ADR-0055–0057 proved unmappable to real,
current code — every gap the Spec's own Human Review Decision Table
already named (items 1–18) is confirmed still accurate; this section
records only the additional, genuinely Plan-level precision the Spec
left open (exact file paths, the `cookTexture()`/`stb` boundary split,
the `TextureColorSpace` DTO decoupling), not a reopening of any
Approved decision.

## Plan-level decisions (fixed here, not left to Implementation)

### D1. Targets, namespaces, directories — no new module

Every new file extends an already-`Accepted` module or target — no new
CMake target category, no new top-level module, matching Spec 0016's
own Non-Goals:

| New file | Extends | Namespace |
|---|---|---|
| `src/rhi/include/atlantis/rhi/sampled_texture.h` (no `.cpp` — abstract interface, matching `texture.h`) | `atlantis_rhi` (existing target) | `atlantis::rhi` |
| `src/rhi/include/atlantis/rhi/sampler.h` (no `.cpp`) | same | same |
| `src/rhi/include/atlantis/rhi/types.h` (extended in place — see D2) | same | same |
| `src/rhi/include/atlantis/rhi/device.h`, `command_list.h` (extended in place — new pure-virtual declarations) | same | same |
| `src/rhi/src/types.cpp` (extended in place — new `operator==`s) | same | same |
| `src/vulkan_backend/src/vulkan_sampled_texture.h`/`.cpp`, `vulkan_sampler.h`/`.cpp` | `atlantis_vulkan_backend` (existing target) | `atlantis::vulkan_backend` |
| `src/vulkan_backend/src/vulkan_device.h`/`.cpp`, `vulkan_command_list.h`/`.cpp`, `resource_state_mapping.cpp` (extended in place) | same | same |
| `src/render_graph/include/atlantis/render_graph/execution.h`, `src/render_graph/src/execution.cpp` (extended in place) | `atlantis_render_graph` (existing target) | `atlantis::render_graph` |
| `src/renderer/include/atlantis/renderer/material.h`, `src/renderer/src/material.cpp`, `src/renderer/src/renderer.cpp` (extended in place) | `atlantis_renderer` (existing target) | `atlantis::renderer` |
| `src/shader_system/include/atlantis/shader_system/reflection_metadata.h`, `src/shader_system/src/reflection_loader.cpp`, `slang_json_transform.cpp`, `descriptor_contract.cpp` (extended in place) | `atlantis_shader_system` (existing target) | `atlantis::shader_system` |
| `src/shader_system/rhi_integration/src/vertex_input_mapping.cpp` (extended in place) | `atlantis_shader_system_rhi_integration` (existing target) | `atlantis::shader_system::rhi_integration` |
| `src/tools/shader_compiler/src/compile_and_validate.cpp` (extended in place) | `atlantis_shader_compiler_lib` (existing target) | `atlantis::tools::shader_compiler` |
| `src/asset_system/include/atlantis/asset_system/{texture_types,texture_artifact,texture_metadata,cook_texture,load_texture}.h`, matching `.cpp` under `src/asset_system/src/`, `errors.h` (extended in place) | `atlantis_asset_system` (existing target) | `atlantis::asset_system` |
| `src/asset_system/CMakeLists.txt` (extended in place — `atlantis_add_texture_asset()`) | — | CMake |
| `src/tools/asset_cooker/cook_command.h`/`.cpp`, `main.cpp` (extended in place — a third mode), `CMakeLists.txt` (extended — `Stb::Stb` `PRIVATE`) | `atlantis_asset_cooker_lib` (existing target) | `atlantis::tools::asset_cooker` |
| `cmake/AtlantisStb.cmake` (**new** CMake module) | — | CMake |
| `cmake/AtlantisDependencies.cmake`, root `CMakeLists.txt` (extended in place) | — | CMake |
| `tests/render_graph/fake_command_list.h` (extended in place — `FakeSampledTexture`, `FakeSampler`, new recorded-call structs) | `tests/render_graph`, `tests/renderer` (existing targets) | `atlantis::render_graph::test` |
| `tests/image_regression/fixture/textured_quad_fixture.h`/`.cpp` | `atlantis_image_regression_fixture` (existing target) | (unnamespaced, matching `minimal_cube_fixture.cpp`) |
| `tests/image_regression/fixture/textured_quad_source.png` (checked-in authoring source) | — | — |

No new top-level module, no new CMake target, no new namespace. Every
module's own existing dependency edges are preserved: `Atlantis::RHI`
still depends on `Atlantis::Core` only; `Atlantis::VulkanBackend` still
depends on `Atlantis::RHI`, `Atlantis::Core`, `Atlantis::Platform`,
`Vulkan::Vulkan`; `Atlantis::RenderGraph` still depends on `Atlantis::RHI`,
`Atlantis::Core`; `Atlantis::Renderer` still depends on `Atlantis::RHI`,
`Atlantis::RenderGraph`, `Atlantis::Core` (no `Atlantis::VulkanBackend`,
confirmed by that CMakeLists.txt's own comment, unaffected); `Atlantis::AssetSystem`
still depends on `Atlantis::Core` only — no RHI, no `Stb::Stb` (D-note
above). `atlantis_asset_cooker_lib` gains exactly one new link edge,
`Stb::Stb` `PRIVATE` (ADR-0041's own Accepted Amendment).

### D2. RHI value types — exact C++ shapes

```cpp
// types.h additions
enum class SampledTextureFormat {
  Rgba8Unorm,
  Rgba8Srgb,
};

enum class Filter {
  Nearest,
  Linear,
};

enum class AddressMode {
  Repeat,
  ClampToEdge,
};

struct SampledTextureCreateParams {
  Extent2D extent;
  SampledTextureFormat format = SampledTextureFormat::Rgba8Unorm;
  // No mip-count field -- every SampledTexture this round has exactly
  // one mip level (Spec 0016 Human Review item 12); exposing an unused
  // knob is deliberately avoided, matching DepthFormat's own precedent.
};
[[nodiscard]] bool operator==(const SampledTextureCreateParams&, const SampledTextureCreateParams&);

struct SamplerCreateParams {
  Filter filter = Filter::Nearest;
  AddressMode addressMode = AddressMode::ClampToEdge;
};
[[nodiscard]] bool operator==(const SamplerCreateParams&, const SamplerCreateParams&);

enum class SampledTextureCreateError {
  AllocationFailed,
  ImageCreationFailed,
  ImageViewCreationFailed,
};  // mirrors TextureCreateError exactly (types.h's own existing precedent)

enum class SamplerCreateError {
  SamplerCreationFailed,
};

enum class ResourceState {
  Undefined, ColorAttachmentWrite, PresentSource, ColorAttachmentOutput,
  DepthAttachmentReadWrite, TransferSource,
  TransferDestination,  // new
  ShaderRead,            // new
};

enum class BufferPurpose {
  Vertex, Index, Uniform, Readback,
  Staging,  // new -- host-visible, host-coherent, matching Buffer's
            // existing, unmodified contract; holds decoded pixel bytes
            // transiently between CPU load and the GPU copy
};

enum class VertexAttributeFormat {
  Float3,
  Float2,  // new -- types.h's own existing comment already names this
           // exact addition
};
```

```cpp
// sampled_texture.h
class SampledTexture {
 public:
  virtual ~SampledTexture() = default;
  [[nodiscard]] virtual Extent2D extent() const = 0;
  [[nodiscard]] virtual SampledTextureFormat format() const = 0;
};

// sampler.h
class Sampler {
 public:
  virtual ~Sampler() = default;
  [[nodiscard]] virtual Filter filter() const = 0;
  [[nodiscard]] virtual AddressMode addressMode() const = 0;
};
```

`Sampler`'s own immutability (Human Review item 4) is structural: no
setter of any kind exists on the interface — this is a type-level
guarantee, not a documented convention, matching `ValidatedSceneData`'s
own established discipline for "provably immutable, not merely not
observed to be mutated."

`Device` gains (`device.h`):
```cpp
[[nodiscard]] virtual Result<std::unique_ptr<SampledTexture>, SampledTextureCreateError>
    createSampledTexture(const SampledTextureCreateParams&) = 0;
[[nodiscard]] virtual Result<std::unique_ptr<Sampler>, SamplerCreateError>
    createSampler(const SamplerCreateParams&) = 0;
```

`CommandList` gains (`command_list.h`):
```cpp
// buffer must have been created with BufferPurpose::Staging. destination
// must be Undefined or TransferDestination when this is recorded --
// caller precondition (ATLANTIS_CHECK), not checked here, matching
// every other CommandList method's existing discipline.
virtual void copyBufferToTexture(Buffer& source, SampledTexture& destination) = 0;

// Third transitionResource() overload, alongside the existing
// RenderTarget/depth-Texture ones.
virtual void transitionResource(SampledTexture& target, ResourceState before, ResourceState after) = 0;

// texture must be in ResourceState::ShaderRead when this is recorded --
// caller precondition, matching this codebase's existing discipline
// (e.g. bindUniformBuffer()'s own BufferPurpose precondition).
virtual void bindTexture(SampledTexture& texture, Sampler& sampler) = 0;
```

### D3. `Material` — borrowed, optional `SampledTexture`/`Sampler`

```cpp
// material.h
class Material {
 public:
  explicit Material(std::unique_ptr<atlantis::rhi::Pipeline> pipeline,
                     const atlantis::rhi::SampledTexture* sampledTexture = nullptr,
                     const atlantis::rhi::Sampler* sampler = nullptr) noexcept;
  [[nodiscard]] atlantis::rhi::Pipeline& pipeline() const noexcept { return *pipeline_; }
  [[nodiscard]] const atlantis::rhi::SampledTexture* sampledTexture() const noexcept { return sampledTexture_; }
  [[nodiscard]] const atlantis::rhi::Sampler* sampler() const noexcept { return sampler_; }

 private:
  std::unique_ptr<atlantis::rhi::Pipeline> pipeline_;
  const atlantis::rhi::SampledTexture* sampledTexture_ = nullptr;  // borrowed, never owned
  const atlantis::rhi::Sampler* sampler_ = nullptr;                // borrowed, never owned
};
```

Nullable, non-owning raw pointers — not references — because the
binding is genuinely *optional* (an untextured `Material`, like every
existing one today, passes neither). Both are set together or not at
all (`sampledTexture_ == nullptr` iff `sampler_ == nullptr`; enforced by
`ATLANTIS_CHECK` in the constructor, a caller-precondition violation
otherwise — a `Material` naming a texture with no sampler, or vice
versa, is a programmer error, not a representable state).

**Ownership/destruction-order contract, stated explicitly (Spec 0016
Human Review item 7):** the caller-owning composition root — never
`Material` itself — must keep any `SampledTexture`/`Sampler` it passes
to a `Material` alive for at least as long as that `Material` is used
in any `drawFrame()` call. Concretely, in the new fixture (Milestone 9):
**`Material` instances are destroyed before the `SampledTexture`/
`Sampler` instances they borrow** — matching `DrawItem`'s own existing
"mesh/material must outlive the draw call" contract, extended, not
reinvented. The fixture's own member-declaration order enforces this
via ordinary C++ reverse-destruction-order semantics: `SampledTexture`/
`Sampler` members are declared *before* `Material` members in the
fixture's own struct, so `Material` (declared later) is destroyed
*first* on scope exit, before the resources it borrowed — the exact
inverse ordering risk this note exists to prevent is therefore
structurally impossible, not merely documented.

`Renderer`'s existing per-`DrawItem` pass-callback loop
(`renderer.cpp:26-31`) gains, immediately after the existing
`bindUniformBuffer()` call:
```cpp
if (item.material->sampledTexture() != nullptr) {
  cmd.bindTexture(*item.material->sampledTexture(), *item.material->sampler());
}
```
An untextured `Material` (`sampledTexture() == nullptr`) skips this
call entirely — `bindTexture()` is never invoked, the existing
one-binding descriptor set layout path is exercised exactly as before
(D7's own regression requirement).

### D4. RenderGraph — one new resource-carrying field, strictly scoped

```cpp
// execution.h, ResourceBinding
struct ResourceBinding {
  RenderTarget* target = nullptr;
  Texture* depthTexture = nullptr;
  SampledTexture* sampledTexture = nullptr;  // new -- tracks ONLY the
      // destination of a texture upload's own Undefined->TransferDestination
      // ->ShaderRead transition. Never used to track a texture already
      // in ShaderRead being sampled by a later, unrelated draw pass --
      // that consumption is a plain CommandList::bindTexture() call
      // inside a pass callback, exactly like bindUniformBuffer() today,
      // not a RenderGraph-tracked resource.
  std::optional<ResourceState> incomingState;
  std::optional<ResourceState> finalState;
};
```

Exactly one of `target`/`depthTexture`/`sampledTexture` is non-null per
binding — `execute()`'s existing Guard 0 (`execution.cpp:54-61`)
extends from "exactly one of two" to "exactly one of three," same
shape. `execute()`'s transition-insertion logic gains one new branch,
dereferencing `binding->sampledTexture` and calling the new
`transitionResource(SampledTexture&, ...)` overload — structurally
identical to the existing `target`/`depthTexture` branches, reusing the
same `incomingState`/`finalState` mechanism Spec 0010's readback
`finalState` already established, not a new mechanism.

**Explicitly not done**, matching Spec 0016's own Architectural Impact
and this Plan's own scope discipline: no fourth resource kind, no
generic/variant resource-binding refactor, no persistent-resource
tracking across multiple `execute()` calls. `isDrawPass()`
(`execution.cpp:37-46`) is unmodified — a `sampledTexture`-carrying
binding is never classified as a draw pass.

### D5. Vulkan Backend — allocation, barrier table, descriptor layout

`VulkanSampledTexture`/`VulkanSampler` (`vulkan_sampled_texture.h`/`.cpp`,
`vulkan_sampler.h`/`.cpp`) follow `VulkanTexture`'s own exact shape:
manual (no VMA, matching the existing `Accepted`-deferred, ADR-0015,
pattern) `vkCreateImage`/`vkGetImageMemoryRequirements`/
`selectMemoryTypeIndexForDevice` (device-local)/`vkAllocateMemory`/
`vkCreateImageView` (color aspect, `VK_IMAGE_ASPECT_COLOR_BIT`, unlike
`VulkanTexture`'s own `VK_IMAGE_ASPECT_DEPTH_BIT`) sequence for
`VulkanSampledTexture`; a single `vkCreateSampler` call for
`VulkanSampler`, translating `Filter`/`AddressMode` into
`VkSamplerCreateInfo` fields (`VK_FILTER_NEAREST`/`VK_FILTER_LINEAR`;
`VK_SAMPLER_ADDRESS_MODE_REPEAT`/`VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE`
for both U and V, `VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE` for W since
this is a 2D-only texture). `VulkanDevice::createSampledTexture()`/
`createSampler()` (`vulkan_device.cpp`, alongside the existing
`createTexture`/`createBuffer`/`createPipeline`/`createOffscreenTarget`)
implement the `Device` overrides.

`vertexAttributeFormatToVkFormat()` (`vulkan_device.cpp:624-631`) gains
a `Float2 -> VK_FORMAT_R32G32_SFLOAT` case, exhaustive switch preserved
(no `default:`, so a future missing case still fails to build under
`/w14062`-equivalent discipline already governing this file).

**Barrier-plan table** (`resource_state_mapping.cpp:104-131`) gains
exactly two new, explicit entries — no wildcard:
- `Undefined -> TransferDestination`: `VK_IMAGE_LAYOUT_UNDEFINED ->
  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL`, source stage/access `TOP_OF_PIPE`/`0`,
  destination stage/access `TRANSFER`/`VK_ACCESS_TRANSFER_WRITE_BIT`.
- `TransferDestination -> ShaderRead`: `VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL ->
  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL`, source stage/access
  `TRANSFER`/`VK_ACCESS_TRANSFER_WRITE_BIT`, destination stage/access
  `FRAGMENT_SHADER`/`VK_ACCESS_SHADER_READ_BIT`.

Any other `(before, after)` pair naming `SampledTexture` still hits the
existing `ATLANTIS_CHECK_MSG(false, ...)` — regression-confirmed
(Milestone 2's own verification), not merely assumed unchanged.

**`copyBufferToTexture()`** (`VulkanCommandList`): one
`vkCmdCopyBufferToImage`, `VkBufferImageCopy` with `bufferRowLength = 0`
and `bufferImageHeight = 0` (tightly packed, matching
`copyRenderTargetToBuffer()`'s own existing convention exactly, ADR-0040),
`imageSubresource` = color aspect, mip 0, layer 0/1,
`imageExtent` = the destination `SampledTexture`'s own `extent()`.

**Descriptor pool/layout extension**: `VulkanDevice`'s device-level
`VkDescriptorPool` (`vulkan_device.cpp:1245-1254`, today `maxSets = 4`,
one `VkDescriptorPoolSize{UNIFORM_BUFFER, 4}`) gains a second
`VkDescriptorPoolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}`
entry — `maxSets` unchanged (still 4; a `VkDescriptorSet` may now
contain both binding kinds, not one set per kind). `VulkanDevice::createPipeline()`'s
own descriptor-set-layout creation (`vulkan_device.cpp:807-819`) gains a
second, **conditional** `VkDescriptorSetLayoutBinding` — `binding = 1`,
`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`, `VK_SHADER_STAGE_FRAGMENT_BIT`
— added to the layout only when `PipelineCreateParams` indicates the
shader's own reflected contract declares it (see D6); the existing
one-binding shader path is otherwise completely unaffected, satisfying
D8's own regression requirement without a second `createPipeline()`
overload. `VulkanCommandList::bindTexture()` writes one
`VkWriteDescriptorSet` (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`,
binding 1) against the currently-bound pipeline's own descriptor set,
pairing the `SampledTexture`'s own `VkImageView` and the `Sampler`'s own
`VkSampler` in one `VkDescriptorImageInfo` (ADR-0056 Decision 9).

### D6. Shader System — reflected sampler kind, `Float2`, and the second contract

`reflection_metadata.h` gains:
```cpp
enum class DescriptorType {
  UniformBuffer,
  Sampler,  // new -- combined image sampler, matching
            // VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER's own shape
};
enum class VertexAttributeType {
  Float3,
  Float2,  // new
};
```
`reflection_loader.cpp`'s `parseDescriptorType()` gains a case for the
string `"combinedImageSampler"` (matching Slang's own reflection JSON
vocabulary for this binding kind — exact string confirmed empirically
during Milestone 4, not guessed). `slang_json_transform.cpp:245-248`'s
own explicit "silently skip any other binding kind" comment/branch is
replaced with a real `combinedImageSampler` case, alongside the
existing `descriptorTableSlot`/`pushConstantBuffer` handling — the
"silent skip" behavior is preserved for any *other*, still-unmodeled
kind (this Plan adds exactly one new recognized kind, not a general
reflection framework).

`descriptor_contract.cpp` gains a second, named contract function
(e.g. `texturedMaterialExpectedDescriptorContract()`, exact name a
mechanical detail) returning the fixed two-binding shape: `{set 0,
binding 0, UniformBuffer, Vertex}` + `{set 0, binding 1, Sampler,
Fragment}` — additive, alongside the existing
`minimalRendererExpectedDescriptorContract()`, which is untouched.

`rhi_integration/src/vertex_input_mapping.cpp`'s `toRhiFormat()`
(`:10-16`) gains a `Float2 -> VertexAttributeFormat::Float2` case,
exhaustive switch preserved.

### D7. Tools shader-compiler — real `expectedContract` consumption

`compile_and_validate.cpp`'s `validateDescriptorContractForStage()`
(`:129-142`) changes from unconditionally calling
`minimalRendererExpectedDescriptorContract()` to consulting
`CompileAndValidateRequest::expectedContract` (already declared,
`compile_and_validate.h:21`, already populated from CMake's
`EXPECTED_CONTRACT`/`--expected-contract=` all the way through
`main.cpp:51-52` — simply never read at this exact call site until now)
— when `expectedContract` names the existing minimal-renderer contract,
behavior for `minimal_mesh.slang` is byte-for-byte unchanged (regression,
Milestone 4's own verification); when it names the new textured
contract (D6), the new fixture's own shader (Milestone 9) validates
against it correctly. No new CMake parameter — `atlantis_add_slang_shader_pair()`'s
own existing `EXPECTED_CONTRACT` argument (`shader_system/CMakeLists.txt:78`)
is reused, now actually consumed.

### D8. Asset System — texture types, cooker, loader; artifact format

```cpp
// texture_types.h
enum class TextureColorSpace { Unorm, Srgb };  // AssetSystem's own,
    // independent of atlantis::rhi::SampledTextureFormat -- AssetSystem
    // must not depend on RHI (established boundary); the fixture (the
    // composition root) translates TextureColorSpace -> SampledTextureFormat.

struct TextureAssetData {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  TextureColorSpace colorSpace = TextureColorSpace::Unorm;
  std::vector<std::uint8_t> pixelBytes;  // tightly packed RGBA8, row-major
};
```

```cpp
// cook_texture.h -- takes ALREADY-DECODED bytes; no stb dependency here
// (see this Plan's own module-boundary resolution note above).
[[nodiscard]] Result<std::monostate, TextureCookError> cookTexture(
    const std::uint8_t* pixelBytes, std::uint32_t width, std::uint32_t height,
    int channelsInFile, TextureColorSpace colorSpace,
    const std::string& logicalPathInput,
    const std::filesystem::path& artifactOutputPath,
    const std::filesystem::path& metadataOutputPath);

// load_texture.h
[[nodiscard]] Result<TextureAssetData, TextureLoadError> loadTextureAsset(
    const std::filesystem::path& artifactPath,
    const std::filesystem::path& metadataPath);
```

`errors.h` gains, additively:
```cpp
enum class TextureCookError {
  ZeroDimension, DimensionExceedsMaximum, SourceOverflow,
  AtomicWriteFailed,  // mirrors cookStaticMesh()'s own established set
};
enum class TextureArtifactDecodeError {
  BadMagic, UnsupportedSchemaVersion, TruncatedHeader,
  InconsistentPixelDataSize, DimensionExceedsMaximum, UnknownFormat,
  UnsupportedMipCount,  // must equal 1
};
enum class TextureLoadError {
  ArtifactDecodeFailed /* wraps TextureArtifactDecodeError */,
  MetadataParseFailed, MetadataArtifactMismatch, MetadataReadFailed,
};
```

**Texture artifact binary layout** (`texture_artifact.h`/`.cpp`,
matching `mesh_artifact.h`'s discipline exactly — magic + fixed header,
unconditionally little-endian, explicit `appendU32LE`/`appendFloatLE`-style
shift/mask serialization, never a struct `memcpy`):

| Field | Type | Notes |
|---|---|---|
| magic | 8 bytes | `"ATLTEX\0\0"` |
| schemaVersion | u32 LE | starts at `1` |
| width | u32 LE | checked non-zero, ≤ `kMaxTextureDimension` (defensive maximum, Plan-level constant — `8192`, chosen so `8192 * 8192 * 4 = 268,435,456` stays comfortably within `uint32_t`) |
| height | u32 LE | same bound |
| format | u32 LE | `0 = Rgba8Unorm`, `1 = Rgba8Srgb` — `UnknownFormat` for any other value |
| mipCount | u32 LE | must equal `1`; any other value is `UnsupportedMipCount` |
| pixelDataOffset | u32 LE | byte offset of pixel data from artifact start |
| pixelDataSizeBytes | u32 LE | must equal `width * height * 4`, computed and compared in `uint64_t` before trusting either value for allocation (never 32-bit multiplication before the dimension-maximum check — Spec 0016 Human Review item 9's own explicit overflow-safety requirement) |
| pixel data | `pixelDataSizeBytes` bytes | tightly packed, row-major, `width * 4` bytes per row, no padding (`copyBufferToTexture()`'s own `bufferRowLength = 0` convention, D5); row 0 is the authoring image's own first-decoded row (`stb_image`'s un-flipped default, matching ADR-0041's "no vertical flip, ever" convention) |

`decodeTextureArtifact()` independently re-validates every one of these
conditions against the artifact's actual bytes (magic, schema version,
dimensions non-zero and ≤ the maximum — checked via 64-bit arithmetic
*before* trusting `pixelDataSizeBytes` for any allocation, mip count
exactly `1`, a known format value, `pixelDataSizeBytes` exactly
`width * height * 4`) — never assuming a well-formed cooker output,
matching `mesh_artifact.h`'s own decode-time discipline exactly.

**Metadata sidecar** (`texture_metadata.h`/`.cpp`): a new, dedicated
shape (not a reuse of `AssetMetadata`, whose 8-line mesh-specific shape
does not fit) — strict, versioned, flat text, matching `scene_metadata.h`'s
own precedent of a dedicated shape when the existing one does not
apply. Fields: version line, `asset_id` (16 lowercase hex digits,
`AssetId::toHexString()`), `source_logical_path`, `width`, `height`,
`format`, `channels_in_file` (the source's own real decoded channel
count — provenance only, never a hard validation gate the way a
golden's `channels_in_file == 4` is, Spec 0016 Human Review item 9's
own explicit, disclosed difference from the golden-validation model).

`loadTextureAsset()` independently re-validates: artifact decode
succeeds; metadata parses; metadata's own `asset_id` matches its own
`source_logical_path` via `computeAssetId()` (self-consistency, mirroring
`loadStaticMeshAsset()`'s own precedent); metadata's `width`/`height`/`format`
match the artifact's own decoded values (`MetadataArtifactMismatch`,
mirroring `AssetLoadError::MetadataArtifactMismatch`'s established
shape).

**CMake declaration** — `atlantis_add_texture_asset(NAME <name> SOURCE
<authoring-png-path> COLOR_SPACE <Unorm|Srgb>)` (`asset_system/CMakeLists.txt`,
exact function name/argument spelling a mechanical detail, following
`atlantis_add_static_mesh_asset()`'s own `add_custom_command`/stamp/
`BYPRODUCTS`/`PARENT_SCOPE`-export pattern exactly): invokes
`atlantis_asset_cooker --kind=texture --color-space=<...>` against
`SOURCE`, producing `<name>.atex`/`<name>.atex.meta.txt` under the build
tree, and exports `ATLANTIS_<name>_ARTIFACT_PATH`/`ATLANTIS_<name>_METADATA_PATH`
(mirroring the mesh/scene macros' own export-variable convention).
Callable twice against the same `SOURCE` with different `NAME`/
`COLOR_SPACE` — the mechanism the new fixture's own two textures
(Milestone 9) need, and the reason `NAME` (not `SOURCE`) is the
per-artifact identity key, matching `atlantis_add_static_mesh_asset()`'s
own existing `NAME`-keyed shape.

### D9. Tools — `atlantis_asset_cooker`'s new texture mode

`cook_command.h`: `enum class AssetKind { StaticMesh, Scene, Texture };`
(one new enumerator; `/w14062` forces every switch to gain a case, not
optional). `CookCommandRequest` gains `std::string colorSpace;` (parsed
from `--color-space=unorm|srgb`).

`cook_command.cpp` gains `runCookTextureMode()`, matching
`runCookMeshMode()`/`runCookSceneMode()`'s exact shape: compute
`relativePath` → strip a fixed authoring extension (`kTextureAuthoringExtension
= ".png"`) → build `.atex`/`.atex.meta.txt` output paths → **decode the
source PNG via `stbi_load(path, &width, &height, &channelsInFile, 4)`**
(the one and only `stb_image` call site in this entire codebase's own
runtime-adjacent code, confirmed scoped to exactly this function) → on
decode failure, a distinct `TextureCookError`-mapped message
(`textureCookErrorMessage()`, mirroring `sceneCookErrorMessage()`'s own
shape) → call `atlantis::asset_system::cookTexture(decodedBytes, width,
height, channelsInFile, parsedColorSpace, relativePath, artifactPath,
metadataPath)` → `writeStamp()`. `runCookCommand()`'s own `switch`
(`:208-217`) gains a `case AssetKind::Texture: return
runCookTextureMode(request);` arm. `main.cpp:52-60`'s own `else if`
chain gains a `"texture"` branch parsing `AssetKind::Texture`, plus a
new `--color-space=` argument parse.

`CMakeLists.txt` gains `target_link_libraries(atlantis_asset_cooker_lib
PRIVATE Stb::Stb)` and, in the same translation unit as the new
`stbi_load()` call (`cook_command.cpp`), `#define STB_IMAGE_IMPLEMENTATION`
immediately before `#include <stb_image.h>` — the **second**
implementation-macro translation unit in the repository, per ADR-0041's
own Accepted Amendment ("one implementation-macro TU per linking
target," now two targets: `tests/image_regression/support/png_codec.cpp`
and this file) — confirmed no ODR conflict, since the two are never
linked into the same binary.

### D10. `cmake/AtlantisStb.cmake` — the CMake-ordering fix

New file, content: `stb`'s exact `FetchContent_Declare`/
`FetchContent_MakeAvailable`/`Stb::Stb` `INTERFACE`-target block, moved
verbatim from `cmake/AtlantisDependencies.cmake` lines 33-47 (same pinned
commit hash, same `URL_HASH`, unchanged — this Plan does not re-pin
`stb`), wrapped in its own `include_guard(GLOBAL)` (preserving the
existing double-inclusion safety). `cmake/AtlantisDependencies.cmake`
loses those 15 lines — Catch2's own declaration (lines 17-23) is
completely unaffected, still test-only, still gated by
`if(ATLANTIS_BUILD_TESTS)`.

Root `CMakeLists.txt` gains one new line, `include(cmake/AtlantisStb.cmake)`,
inserted **after** `include(cmake/CompilerWarnings.cmake)` (line 12) and
**before** `add_subdirectory(src/asset_system)` (line 44) — unconditional,
outside `if(ATLANTIS_BUILD_TESTS)`, strictly earlier than
`add_subdirectory(src/tools/asset_cooker)` (line 60). `tests/image_regression/`'s
own two existing `Stb::Stb` consumers are unaffected: the target exists,
under the identical name, by the time `if(ATLANTIS_BUILD_TESTS)`'s own
subdirectories process (Milestone 1's own verification: a from-scratch
configure with `ATLANTIS_BUILD_TESTS=OFF` succeeds and produces a
buildable `atlantis_asset_cooker`; a second from-scratch configure with
`ATLANTIS_BUILD_TESTS=ON` still builds and passes the two existing
`tests/image_regression/` targets unmodified).

### D11. First textured fixture — exact scene content

Two 1×2-triangle quads, side by side in clip space (e.g. left quad
`x ∈ [-0.9, -0.1]`, right quad `x ∈ [0.1, 0.9]`, both `y ∈ [-0.5, 0.5]`,
`z = 0`), each with `float2 uv` spanning `[0,1]²` across its own quad.
One checkerboard-patterned source PNG (small — e.g. 64×64, a handful of
alternating-color blocks large enough to be unambiguous in a 512×512
capture), checked in as `tests/image_regression/fixture/textured_quad_source.png`,
cooked **twice** via `atlantis_add_texture_asset()` (D8): once
`COLOR_SPACE Unorm` (left quad's own `Material`), once `COLOR_SPACE
Srgb` (right quad's own `Material`) — both textures share one
`Sampler` (`Filter::Nearest`, `AddressMode::ClampToEdge` — nearest
filtering keeps the checkerboard's own block edges crisp in the
captured golden, avoiding filtering-interpolation ambiguity at block
boundaries).

## Milestones / Task Breakdown

Each step leaves the repository configuring, building, and testing
cleanly (Debug and Release), matching every prior Plan's own established
sequencing convention. Steps are ordered so every later step builds
directly on real, already-compiled, already-tested code from earlier
ones.

1. **`stb` CMake relocation** (D10 — `cmake/AtlantisStb.cmake`, root
   `CMakeLists.txt`'s new unconditional `include()`,
   `AtlantisDependencies.cmake`'s own reduced content). No C++ change.
   Verification: `ATLANTIS_BUILD_TESTS=OFF` configure succeeds and
   `atlantis_asset_cooker` builds (still `AssetKind::{StaticMesh,Scene}`
   only, unchanged behavior); `ATLANTIS_BUILD_TESTS=ON` configure/build/
   test unchanged (`tests/image_regression/`'s own two `Stb::Stb`
   consumers still link and pass).
2. **RHI + Vulkan Backend, one indivisible step** (D2, D5 — new types,
   new `ResourceState`/`BufferPurpose`/`VertexAttributeFormat` values,
   new `Device`/`CommandList` pure virtuals, `VulkanSampledTexture`/
   `VulkanSampler`, `VulkanDevice`/`VulkanCommandList` overrides, barrier
   table, descriptor pool/layout extension, `vertexAttributeFormatToVkFormat()`
   `Float2` case). **`tests/render_graph/fake_command_list.h`'s
   `FakeCommandList` gains the same three new overrides in this same
   step** (`copyBufferToTexture`, the third `transitionResource`,
   `bindTexture`, each recording into a new `RecordedX` struct,
   mirroring the existing ones exactly) plus `FakeSampledTexture`/
   `FakeSampler` stand-ins, matching `FakeTexture`'s own shape — this is
   not optional or deferrable: `CommandList`'s only two implementers in
   this tree are `VulkanCommandList` and `FakeCommandList`, and adding a
   pure virtual without updating both breaks every target that links
   either, including `tests/render_graph/` and `tests/renderer/`
   (Pre-draft verification's own finding, above). `tests/rhi/types_tests.cpp`
   extended: new equality tests for `SampledTextureCreateParams`/
   `SamplerCreateParams`, matching `OffscreenTargetCreateParams`'s own
   existing test shape. `tests/vulkan_backend/resource_state_mapping_tests.cpp`
   extended: the two new barrier-plan entries produce the expected
   Vulkan parameters; an unlisted pair naming `SampledTexture` still
   triggers `ATLANTIS_CHECK_MSG` (regression). A new, narrow GPU-required
   test (`tests/vulkan_backend/headless_rendering_gpu_tests.cpp`,
   extended, or a sibling file) creates a `SampledTexture`/`Sampler`,
   uploads via a one-pass `RenderGraphBuilder` graph (D4 — this is the
   first real exercise of the new `ResourceBinding` field, so depends on
   Milestone 3 landing first; **resequenced**: this specific GPU test
   moves to the end of Milestone 3, not Milestone 2, since it needs
   `ResourceBinding`'s own third field to exist — Milestone 2's own GPU
   verification is limited to confirming `createSampledTexture()`/
   `createSampler()` succeed with valid parameters, real hardware,
   Validation Layers clean).
3. **RenderGraph** (D4 — `ResourceBinding`'s third field, `execute()`'s
   new transition branch, Guard 0 extended to three). `tests/render_graph/`
   extended: a new test builds a one-pass graph with a `sampledTexture`
   binding, `incomingState`/`finalState` driving the expected
   `FakeCommandList`-recorded `transitionResource(SampledTexture&, ...)`
   calls (GPU-independent, via the Milestone-2-extended `FakeCommandList`).
   **The GPU-required upload-primitive test deferred from Milestone 2**
   lands here: a real `SampledTexture`, uploaded via a real one-pass
   `RenderGraphBuilder` graph and `copyBufferToTexture()`, sampled by
   nothing yet (no draw pass) — confirms the barrier/copy mechanics
   alone, real hardware, Validation Layers clean, ahead of the full
   combined-submission fixture (Milestone 9).
4. **Shader System + Tools shader-compiler** (D6, D7 — `DescriptorType::Sampler`,
   `VertexAttributeType::Float2`, `parseDescriptorType()`,
   `slang_json_transform.cpp`'s real `combinedImageSampler` case, the
   new textured contract, `toRhiFormat()`'s `Float2` case,
   `compile_and_validate.cpp`'s real `expectedContract` consumption).
   `tests/shader_system/`: a real captured Slang reflection JSON
   (produced during this step against a small, throwaway test `.slang`
   file declaring both a uniform buffer and a combined sampler, plus a
   `float2` vertex input) drives `DescriptorType::Sampler`/
   `VertexAttributeType::Float2` reflection tests and the new
   contract-shape test. `tests/shader_system/rhi_integration/vertex_input_mapping_tests.cpp`
   extended: `Float2` mapping. `tests/tools/shader_compiler/`: the
   `expectedContract` wiring **regression** test — `minimal_mesh.slang`'s
   own existing reflection still validates against
   `minimalRendererExpectedDescriptorContract()` correctly now that it is
   reached via the newly-read field rather than an unconditional call
   (this is the one mandatory check per Spec 0016's own Risks & Open
   Questions) — plus a new test confirming the textured contract accepts
   a matching two-binding reflection and rejects a mismatched one.
5. **`Material`/`Renderer` wiring** (D3). `tests/renderer/renderer_ownership_tests.cpp`
   extended: `Material`'s new fields are non-owning pointer types,
   confirmed via `static_assert`/type trait, matching `EntityId`'s own
   V27-style compile-time-provable-contract precedent; an untextured
   `Material` (both pointers `nullptr`) drives the existing
   `FakeCommandList`-recorded call sequence **byte-for-byte unchanged**
   from before this Plan (explicit regression assertion, not merely "no
   new failure"); a textured `Material` drives exactly one additional
   recorded `bindTexture` call, in the expected position (immediately
   after `bindUniformBuffer`).
6. **Asset System — texture types, artifact, cooker (decoded-bytes-in),
   loader** (D8, minus the CMake declaration function — see Milestone 7).
   `tests/asset_system/`: `texture_types_tests.cpp` (value-type
   round-trip); `texture_artifact_tests.cpp` (encode/decode round-trip;
   every named `TextureArtifactDecodeError` condition individually
   triggered — bad magic, unsupported schema version, truncated header,
   inconsistent pixel-data size, a dimension exceeding
   `kMaxTextureDimension`, an unknown format value, a mip count other
   than `1`; the 64-bit-safe overflow-check ordering itself, confirmed
   via a crafted header whose 32-bit-truncated size would otherwise pass);
   `cook_texture_tests.cpp` (valid encode; `ZeroDimension`/
   `DimensionExceedsMaximum`/`AtomicWriteFailed` triggered individually;
   determinism — cooking the same decoded bytes twice produces identical
   artifact bytes); `load_texture_tests.cpp` (valid load;
   `MetadataArtifactMismatch`; the metadata self-consistency check).
7. **Tools — `atlantis_asset_cooker`'s texture mode + CMake declaration**
   (D8's own `atlantis_add_texture_asset()`, D9). `tests/tools/asset_cooker/`
   extended: a real PNG source (RGBA, RGB-no-alpha, and grayscale
   variants, each a tiny synthetic fixture image generated or checked in
   for this test alone) cooked via the real CLI, confirming
   `channelsInFile` recorded correctly for each and that a non-RGBA
   source is **not** rejected (Spec 0016 Human Review item 9's own
   disclosed difference from golden validation); a corrupted/
   unreadable PNG source rejected via the correct `TextureCookError`
   message; `/w14062` positive/negative build check (a temporarily
   removed `AssetKind::Texture` case in one of the switch sites fails
   the build naming that exact enumerator; restoring it builds clean
   again — mirroring Spec 0013's own V-item precedent for this exact
   mechanism). A CMake-configure-only check (no build) confirms
   `Stb::Stb` exists as a real target with `ATLANTIS_BUILD_TESTS=OFF`
   (Milestone 1's own fix, regression-confirmed here against the real
   consumer it was fixed for).
8. *(Reserved — folded into Milestone 6/7 above; no separate step.
   Numbering preserved from the Spec's own suggested area list for
   cross-reference only.)*
9. **First textured fixture, combined submission, real GPU exercise**
   (D11). New `tests/image_regression/fixture/textured_quad_fixture.h`/`.cpp`,
   `textured_quad_source.png`, its own new `.slang` shader (declaring
   both the uniform buffer and the combined-sampler binding plus a
   `float2 uv` vertex input), directly extending
   `minimal_cube_fixture.cpp`'s own exact call-ordering template
   (Pre-draft verification, above): device → two `SampledTexture`s (one
   per `COLOR_SPACE`, loaded via `loadTextureAsset()` against the two
   cooked artifacts from Milestone 7) → one shared `Sampler` → two
   staging `Buffer`s → `createMesh()` for the two-quad vertex/index data
   (`float2 uv` attribute, D6's own reflection-driven
   `toVertexInputLayout()` path) → uniform buffer → two `Material`s
   (D3) → depth texture → offscreen target → readback buffer; then, per
   frame: `acquireTarget()` → fill uniform → build two `DrawItem`s → one
   `createCommandList()` → **record, in order: (a) the two upload
   graphs (D4, one per `SampledTexture`); (b) `Renderer{}.drawFrame(...)`
   with both `DrawItem`s; (c) the existing readback-copy
   `RenderGraphBuilder` graph** → **exactly one**
   `device->submit(std::move(commandList), *target)` → **exactly one**
   `device->waitIdle()` → read `readbackBuffer` → destroy both staging
   `Buffer`s (only now, after `waitIdle()` returned `Ok` — Spec 0016
   Human Review item 13) → `target.reset()`. Member-declaration order in
   the fixture struct: `SampledTexture`/`Sampler` members precede
   `Material` members (D3's own destruction-order enforcement). New
   `tests/image_regression/textured_quad_gpu_tests.cpp`, `"gpu"`-labeled,
   confirming: exactly one `submit()` call for the whole sequence
   (instrumented count, or code-inspection-confirmed — Plan-level
   detail); the captured frame is non-degenerate (not all-one-color);
   Vulkan Validation Layers clean. **This step does not itself capture
   or commit a golden** — see the Golden Capture Process below; the code
   in this step is fully mergeable and independently reviewable without
   any golden PNG in the same diff.
10. **Golden capture — mandatory, separate commit** (see Golden Capture
    Process below; matches ADR-0042's "Initial baseline bootstrap" and
    Plan 0015's own Step 9 precedent of a dedicated, evidence-bearing
    commit). `tests/image_regression/goldens/textured_quad/textured_quad_512x512_rgba8unorm.png` +
    `.sidecar.txt`.
11. **Full verification and documentation/registry closeout** — clean
    Debug and Release builds from a fresh configure; `ctest -LE gpu` and
    `ctest -L gpu` both green, both configurations; Vulkan Validation
    Layers grepped clean throughout; module-boundary scan (confirms
    `Atlantis::AssetSystem` still names no RHI/`Stb::Stb`-implementation
    header outside `cook_command.cpp`, `Atlantis::Renderer` still names
    no `Atlantis::VulkanBackend` header); `/w14062` positive/negative
    build check across both `atlantis_asset_cooker_lib` and
    `atlantis_runtime_host`-adjacent switch sites touched by this Plan;
    a genuine, human-observed visual confirmation of the new golden
    (matching Spec 0014/0015's own V20/V24 precedent, though this Plan's
    own fixture is headless-only — no windowed claim is made or needed
    here); `specs/README.md`/`docs/project-blueprint.md`/
    `docs/architecture/module_boundaries.md` updated to record delivery.

## Files / Modules Touched (expected)

- **New**: `src/rhi/include/atlantis/rhi/{sampled_texture,sampler}.h`;
  `src/vulkan_backend/src/{vulkan_sampled_texture,vulkan_sampler}.{h,cpp}`;
  `src/asset_system/include/atlantis/asset_system/{texture_types,texture_artifact,texture_metadata,cook_texture,load_texture}.h`
  and matching `src/asset_system/src/*.cpp`; `cmake/AtlantisStb.cmake`;
  `tests/image_regression/fixture/{textured_quad_fixture.h,textured_quad_fixture.cpp,textured_quad_source.png}`;
  `tests/image_regression/textured_quad_gpu_tests.cpp`;
  `shaders/textured_quad/textured_quad.slang` (or sibling location,
  Plan-level detail); `tests/asset_system/{texture_types,texture_artifact,cook_texture,load_texture}_tests.cpp`;
  `tests/vulkan_backend/`'s own new upload-primitive GPU test file (or a
  case added to `headless_rendering_gpu_tests.cpp`, Plan-level detail).
- **Modified (additive only)**: `src/rhi/include/atlantis/rhi/{types,device,command_list}.h`,
  `src/rhi/src/types.cpp`; `src/vulkan_backend/src/{vulkan_device,vulkan_command_list}.{h,cpp}`,
  `resource_state_mapping.cpp`; `src/render_graph/include/atlantis/render_graph/execution.h`,
  `src/render_graph/src/execution.cpp`; `src/renderer/include/atlantis/renderer/material.h`,
  `src/renderer/src/{material,renderer}.cpp`; `src/shader_system/include/atlantis/shader_system/reflection_metadata.h`,
  `src/shader_system/src/{reflection_loader,slang_json_transform,descriptor_contract}.cpp`;
  `src/shader_system/rhi_integration/src/vertex_input_mapping.cpp`;
  `src/tools/shader_compiler/src/compile_and_validate.cpp`;
  `src/asset_system/include/atlantis/asset_system/errors.h`,
  `src/asset_system/CMakeLists.txt` (`atlantis_add_texture_asset()`);
  `src/tools/asset_cooker/{cook_command.h,cook_command.cpp,main.cpp,CMakeLists.txt}`;
  `cmake/AtlantisDependencies.cmake`, root `CMakeLists.txt`;
  `tests/render_graph/fake_command_list.h`; `tests/rhi/types_tests.cpp`;
  `tests/vulkan_backend/resource_state_mapping_tests.cpp`;
  `tests/render_graph/CMakeLists.txt` (new test file registered);
  `tests/renderer/renderer_ownership_tests.cpp`; `tests/shader_system/`,
  `tests/shader_system/rhi_integration/` (new test files/cases);
  `tests/tools/shader_compiler/` (new test file); `tests/tools/asset_cooker/`
  (new test file); every touched `tests/*/CMakeLists.txt` registering
  new test files.
- **Explicitly untouched**: `src/platform/`, `src/world/`, `src/runtime/`
  (no Runtime integration this round — the fixture is entirely
  `tests/image_regression/`-local, matching `minimal_cube_fixture`'s own
  non-Runtime scope); the mesh authoring grammar, `MeshSourceVertex`,
  the mesh runtime artifact, `StaticMeshAssetData` (Spec 0016's own
  Non-Goals — no Asset System mesh-schema change); `assets/meshes/`,
  `assets/scenes/` and every file under them; `src/asset_system/src/{cook,load,mesh_artifact,mesh_source,scene_source,scene_artifact,cook_scene,decode_scene}.cpp`
  and their own headers (mesh/scene cook/decode logic, unmodified);
  `Device::submit()`, `Device::waitIdle()`, `SubmissionSignal` (RHI,
  unchanged — Spec 0016 Human Review item 6); today's `Texture`/
  `DepthFormat` (RHI, completely unchanged); `minimal_mesh.slang` itself
  (a new, sibling shader is added; the existing one is not edited);
  `tests/image_regression/goldens/minimal_cube/`,
  `tests/image_regression/goldens/world_scene/` and every file under
  them (no existing golden is modified by any step of this Plan).

## Golden Capture Process

Separated into its own numbered sequence, matching ADR-0042's "Initial
baseline bootstrap" category and this Plan's own Milestone 9/10 split:

1. Milestone 9's own commit (fixture, shader, source PNG, the GPU test
   confirming a non-degenerate captured frame) lands first, independently
   reviewable, with **no golden PNG or sidecar in that same diff**.
2. Against that already-landed commit, on a clean `git` working tree
   (`golden_generator`'s own existing clean-tree enforcement), on the
   reference GPU (matching every prior golden's own recorded hardware/
   driver provenance discipline), a human runs
   `atlantis_image_regression_golden_generator textured_quad/textured_quad_512x512_rgba8unorm`.
3. A human directly, visually confirms the generated PNG is correctly
   rendered and non-degenerate — **specifically confirming the left
   (`Rgba8Unorm`) and right (`Rgba8Srgb`) quads are visibly, correctly
   different from each other**, not merely "not black" (Spec 0016 Human
   Review item 14's own explicit bar, since the two quads sharing
   identical stored bytes is the entire evidentiary point of this
   golden).
4. The generated `.png`/`.sidecar.txt` pair is added via its **own,
   separate, subsequent commit** — never combined with Milestone 9's own
   code commit, matching Plan 0015's own established discipline and
   ADR-0042's "Source revision, precisely"/commit-ordering rule applied
   unrelaxed to this bootstrap case.
5. **No existing golden is captured, overwritten, or otherwise touched
   by this process** — `minimal_cube`/`world_scene` are re-run,
   unmodified, as regression evidence only (Milestone 11).

## Sequencing & Dependencies

Milestone 1 has no dependency (pure CMake) and may land first,
independently. Milestone 2 depends on Milestone 1 only in the sense
that both touch build-system-adjacent territory, but has no functional
dependency on it — may be developed in parallel, though this Plan
recommends landing Milestone 1 first since it is the smallest, most
isolated change. Milestone 3 depends on Milestone 2 (needs
`SampledTexture`, the new `ResourceState` values, and `FakeCommandList`'s
own new overrides). Milestone 4 depends on Milestone 2 (needs
`VertexAttributeFormat::Float2`) but not on Milestone 3. Milestone 5
depends on Milestone 2 only (`Material`'s new fields reference
`SampledTexture`/`Sampler`; `bindTexture()` already exists from
Milestone 2) — **not** on Milestone 3 or 4, since `Material`/`Renderer`
wiring is orthogonal to both RenderGraph's own upload-graph mechanics
and Shader System's own reflection. Milestone 6 depends on Milestone 1
only (`cookTexture()`/`loadTextureAsset()` need no RHI/RenderGraph/
Renderer/Shader-System dependency at all — genuinely parallelizable
against Milestones 2–5). Milestone 7 depends on Milestones 1 and 6 (the
cooker's own texture mode calls both the now-available `Stb::Stb`
target and `atlantis::asset_system::cookTexture()`). Milestone 9 depends
on **all** of Milestones 2–7 (the first real textured fixture exercises
the complete pipeline end to end) and is this Plan's own mandatory
separate-commit step (matching Plan 0015's own Step 9 precedent).
Milestone 10 (golden capture) depends on Milestone 9. Milestone 11
depends on Milestone 10.

## Verification Checklist

| # | Verification | Where | Kind |
|---|---|---|---|
| V1 | PNG decode via the cooker's `stbi_load(..., desired_channels=4)`: a real RGBA source, a real RGB (no alpha) source, and a real grayscale source are each cooked successfully, each producing `pixelDataSizeBytes = width*height*4`, with `channels_in_file` recorded accurately per source in the metadata sidecar — none rejected for having fewer than 4 real channels. | `tests/tools/asset_cooker/` | GPU-independent |
| V2 | A corrupted/unreadable/zero-byte PNG source is rejected via a distinct `TextureCookError` (`SourceImageDecodeFailed`-equivalent), no artifact written. | `tests/tools/asset_cooker/` | GPU-independent |
| V3 | A source image whose decoded width or height exceeds `kMaxTextureDimension` is rejected via `DimensionExceedsMaximum`, checked before any allocation proportional to it. | `tests/asset_system/cook_texture_tests.cpp` | GPU-independent |
| V4 | `width * height * 4` overflow safety: a crafted/decode-time width×height combination that would wrap a 32-bit multiplication is rejected by the dimension-maximum check before that multiplication is ever trusted for sizing (64-bit computation first). | `tests/asset_system/texture_artifact_tests.cpp` | GPU-independent |
| V5 | Artifact encode/decode round-trip: cooking known-good decoded bytes then decoding reproduces the exact same width/height/format/mip-count/pixel bytes. | `tests/asset_system/texture_artifact_tests.cpp` | GPU-independent |
| V6 | Both `Rgba8Unorm` and `Rgba8Srgb` artifacts encode/decode with the correct fixed byte value for `format`; row order (first artifact row = source's first-decoded row) and row pitch (`width*4`, no padding) confirmed byte-exact. | `tests/asset_system/texture_artifact_tests.cpp` | GPU-independent |
| V7 | Every named `TextureArtifactDecodeError` condition individually triggered and correctly, distinctly reported: bad magic, unsupported schema version, truncated header, inconsistent `pixelDataSizeBytes`, a dimension exceeding the maximum, an unknown format value, a mip count other than `1`. | `tests/asset_system/texture_artifact_tests.cpp` | GPU-independent |
| V8 | Cooker determinism: cooking the same decoded bytes twice produces byte-identical artifact and metadata bytes; a forced mid-write failure leaves no partial output file and does not disturb a pre-existing valid one — mirroring `cookStaticMesh()`'s own established V11/V12-style test shape exactly. | `tests/asset_system/cook_texture_tests.cpp` | GPU-independent |
| V9 | `stb_image`'s `STB_IMAGE_IMPLEMENTATION` macro is defined in exactly one translation unit per linking target (`png_codec.cpp` for `tests/image_regression/`, `cook_command.cpp` for `atlantis_asset_cooker_lib`) — confirmed by a clean link with no duplicate-symbol error, not merely code inspection. | Build-level (both configurations) | GPU-independent |
| V10 | `ATLANTIS_BUILD_TESTS=OFF`: a from-scratch configure succeeds and `atlantis_asset_cooker` builds with real `Stb::Stb` linkage available (`--kind=texture` functional). `ATLANTIS_BUILD_TESTS=ON`: a separate from-scratch configure/build still passes `tests/image_regression/`'s own two pre-existing `Stb::Stb` consumers unmodified. | Manual, recorded (matching Plan 0012 Section D7's own established CMake-reconfigure procedure) | Manual |
| V11 | RHI value-type equality: `SampledTextureCreateParams`/`SamplerCreateParams` `operator==` confirmed, matching `OffscreenTargetCreateParams`'s own existing test shape. | `tests/rhi/types_tests.cpp` | GPU-independent |
| V12 | `SampledTextureCreateError`/`SamplerCreateError` plumb through `Device::createSampledTexture()`/`createSampler()`'s own `Result::Err` channel correctly for each named failure mode (allocation, image creation, image-view creation, sampler creation), matching `TextureCreateError`'s own existing test discipline. | `tests/vulkan_backend/` (GPU-required, real allocation-failure conditions may be simulated per existing precedent) | GPU-required where real allocation is exercised |
| V13 | `Sampler`'s own immutability: a `static_assert`/type-trait check confirms no public mutator exists anywhere on its interface. | `tests/rhi/` or `tests/vulkan_backend/` | GPU-independent (compile-time) |
| V14 | The two new barrier-plan entries (`Undefined -> TransferDestination`, `TransferDestination -> ShaderRead`) produce the exact expected `VkImageMemoryBarrier` fields (layout, stage, access masks); an unlisted pair naming `SampledTexture` still triggers the existing `ATLANTIS_CHECK_MSG` failure — regression-confirmed, not merely assumed unchanged. | `tests/vulkan_backend/resource_state_mapping_tests.cpp` | GPU-independent |
| V15 | `VertexAttributeFormat::Float2` maps to `VK_FORMAT_R32G32_SFLOAT`; the exhaustive switch in `vertexAttributeFormatToVkFormat()` still hard-fails (`ATLANTIS_CHECK_MSG`) for any hypothetically-unhandled value. | `tests/vulkan_backend/` | GPU-independent |
| V16 | RenderGraph: a one-pass graph declaring a `sampledTexture` binding with `incomingState=Undefined`/`finalState=ShaderRead` drives the expected `FakeCommandList`-recorded `transitionResource(SampledTexture&, ...)` sequence; Guard 0 still rejects a binding naming more than one of `target`/`depthTexture`/`sampledTexture`, or none. | `tests/render_graph/` | GPU-independent (via `FakeCommandList`) |
| V17 | A real `SampledTexture`, uploaded through a real one-pass RenderGraph execution and `copyBufferToTexture()` in isolation (no draw pass yet), completes with Vulkan Validation Layers clean — confirming the barrier/copy mechanics alone are correct before the full combined-submission fixture exercises them together with a draw. | `tests/vulkan_backend/` (new upload-primitive GPU test, Milestone 3) | GPU-required |
| V18 | Shader System: a real captured Slang reflection JSON declaring both the existing uniform-buffer binding and a new `combinedImageSampler` binding produces `DescriptorType::Sampler` correctly; a real captured reflection declaring a `float2` vertex input produces `VertexAttributeType::Float2`, mapped by `toRhiFormat()` to `VertexAttributeFormat::Float2`. | `tests/shader_system/`, `tests/shader_system/rhi_integration/` | GPU-independent |
| V19 | The new two-binding descriptor contract accepts exactly a matching two-binding reflection and rejects a mismatched one (wrong count, wrong stage, wrong descriptor type at either binding), matching `minimalRendererExpectedDescriptorContract()`'s own existing test discipline. | `tests/shader_system/` | GPU-independent |
| V20 | `expectedContract` wiring regression: `minimal_mesh.slang`'s own existing reflection still validates correctly against `minimalRendererExpectedDescriptorContract()` now that `compileAndValidate()` reaches it via the newly-read `expectedContract` field rather than an unconditional call — byte-for-byte the same accept/reject outcome as before this Plan. | `tests/tools/shader_compiler/` | Tool-required (real `slangc`/`spirv-val`, no GPU) |
| V21 | `expectedContract` wiring, positive case: the new textured shader's own real reflection validates successfully against the new two-binding contract when `expectedContract` names it. | `tests/tools/shader_compiler/` | Tool-required |
| V22 | `Material`'s new fields are non-owning pointer types, confirmed via `static_assert`/type trait — not merely "not observed to be mutated." | `tests/renderer/renderer_ownership_tests.cpp` | GPU-independent (compile-time) |
| V23 | **Existing untextured `Material`/`DrawItem` regression, explicit, not assumed:** an untextured `Material` drives the exact same `FakeCommandList`-recorded call sequence, byte-for-byte, as it did before this Plan — no `bindTexture` call recorded, no other call reordered or altered. | `tests/renderer/renderer_ownership_tests.cpp` | GPU-independent |
| V24 | A textured `Material` drives exactly one additional recorded `bindTexture` call, positioned immediately after `bindUniformBuffer`, with the expected `SampledTexture`/`Sampler` identities. | `tests/renderer/renderer_ownership_tests.cpp` | GPU-independent |
| V25 | Vulkan descriptor pool/layout: an untextured `Pipeline`'s own descriptor-set-layout creation is completely unaffected (one binding, as before this Plan); a textured `Pipeline` gains exactly the second, fixed `COMBINED_IMAGE_SAMPLER` binding at binding 1, fragment stage; the device-level pool's own new pool-size entry does not affect `maxSets` or the existing uniform-buffer pool-size entry. | `tests/vulkan_backend/` | GPU-required |
| V26 | `bindTexture()`'s own `VkWriteDescriptorSet` correctly pairs the bound `SampledTexture`'s `VkImageView` and the bound `Sampler`'s `VkSampler` in one `VkDescriptorImageInfo`, confirmed via Vulkan Validation Layers clean during an actual textured draw (not inferred from a correct-looking image alone). | `tests/vulkan_backend/`, Milestone 9's own GPU test | GPU-required |
| V27 | **Combined submission, exactly once:** the textured fixture's own composition code calls `Device::submit()` exactly one time for the upload(s)+draw+readback sequence, against the same `RenderTarget` the draw and readback graphs actually use — confirmed by call-count instrumentation or direct code inspection, not assumed. | `tests/image_regression/textured_quad_gpu_tests.cpp` | GPU-required |
| V28 | Staging-buffer lifetime: both staging `Buffer`s remain alive (not destroyed) until after the combined submission's own `waitIdle()` returns `Ok` — confirmed by code-structure inspection (declaration/destruction ordering) and, where feasible, a debug-build lifetime assertion. | `tests/image_regression/textured_quad_gpu_tests.cpp` | GPU-required |
| V29 | `Material` destruction precedes `SampledTexture`/`Sampler` destruction in the fixture's own scope-exit order — confirmed by the fixture struct's own declared member order (D3) and, if feasible, an explicit destructor-order assertion in debug builds. | `tests/image_regression/fixture/textured_quad_fixture.cpp` | GPU-independent (structural) / manual code review |
| V30 | The two quads, cooked from identical source pixel bytes under `Rgba8Unorm` vs. `Rgba8Srgb`, produce **visibly, measurably different** sampled colors in the captured frame — confirmed both by the golden's own human visual-confirmation step (Golden Capture Process, step 3) and by a direct pixel-value comparison in the GPU test itself (the two quads' own captured regions must differ by more than the existing channel-tolerance-0 rule's own noise floor). | `tests/image_regression/textured_quad_gpu_tests.cpp`, Golden Capture Process | GPU-required |
| V31 | New golden, zero channel difference on a second, independent run of the same fixture (determinism, not "looked right once"). | Golden Capture Process, then re-run against the committed golden | GPU-required |
| V32 | New golden captured strictly under ADR-0042's "Initial baseline bootstrap" category, satisfying all six of its own numbered constraints explicitly (Spec 0016 Human Review item 14) — confirmed in the golden-capture commit's own PR description, not merely asserted here. | PR description (Milestone 10's own commit) | Manual |
| V33 | **Existing-golden regression, explicit:** `minimal_cube` and `world_scene` headless tests re-run unmodified, against their own existing, unmodified goldens, zero channel difference — proving this Plan's RenderGraph/`Material`/Vulkan Backend/descriptor-pool changes did not disturb either existing rendering path. | `tests/image_regression/` | GPU-required |
| V34 | `git diff --stat` (or equivalent) confirms the two existing golden PNG/sidecar pairs under `tests/image_regression/goldens/` are byte-identical to `main` at every commit in this Plan's own PR. | PR-level check | Manual |
| V35 | Module-boundary scan: `Atlantis::AssetSystem` still names no RHI header and no `stb_image.h`/`stb_image_write.h` include outside `src/tools/asset_cooker/cook_command.cpp`; `Atlantis::Renderer` still names no `Atlantis::VulkanBackend` header. | `tests/asset_system/module_boundary_tests.cpp` and an equivalent grep-based check for the `stb_image` scan (new, or extended) | GPU-independent |
| V36 | `/w14062` positive/negative build check on `atlantis_asset_cooker_lib`: a temporarily removed `AssetKind::Texture` case in `runCookCommand()`'s own switch (or in `textureCookErrorMessage()`) fails the build, naming that exact enumerator; restoring it builds clean again — mirroring Spec 0013's own established mechanism. | Manual, recorded (build-log evidence) | Manual |
| V37 | Full Debug and Release builds from a fresh configure; `ctest -LE gpu` and `ctest -L gpu` both green on both configurations; Vulkan Validation Layers grepped clean throughout (zero `VUID`/Validation Error/Warning matches) — not merely absence-of-crash. | Full test suite | GPU-independent + GPU-required |
| V38 | A genuine, human-observed visual confirmation of the new golden's own captured image (matching Spec 0014/0015's own V20/V24 discipline for what "human confirmation" means, applied here to a headless-only capture — no windowed claim). | Golden Capture Process, step 3; recorded in the golden-capture commit's own PR | Manual |

## Rollback Plan

Every file this Plan touches is either new (delete on revert) or
additively extended, with two structural exceptions requiring care:
`VulkanDevice::createPipeline()`'s descriptor-set-layout/pool creation
(D5, conditionally gains a second binding) and `Renderer`'s pass
callback (D3, gains a conditional `bindTexture` call) — both are
`if`-gated on the new, optional fields being present, so reverting this
Plan's own commits cleanly restores the exact prior unconditional
one-binding behavior for every existing caller. `cmake/AtlantisDependencies.cmake`'s
own reduced content and the new `cmake/AtlantisStb.cmake` revert
together cleanly (both touched in Milestone 1's own single commit). The
two existing goldens are never modified by any step, so no
golden-provenance rollback concern exists for them; the one new golden
(Milestone 10) is its own separate commit and can be reverted
independently of every code commit that precedes it.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- V1–V38 all executed and recorded; V10, V32, V34, V36, V38 recorded as
  manual verification in the Implementation PR(s).
- The existing `minimal_cube` and `world_scene` goldens confirmed
  byte-identical to `main` in the final diff (V34) — this Plan captures
  exactly one new golden, `textured_quad`, in its own separate commit.
- `git diff --stat` confirms no file under `src/platform/`, `src/world/`,
  or `src/runtime/` was modified (Files / Modules Touched, "Explicitly
  untouched").
- `tests/asset_system/module_boundary_tests.cpp` passes unmodified,
  confirming no new forbidden dependency edge exists; a new or extended
  scan confirms `stb_image.h`/`stb_image_write.h` inclusion is confined
  to the two already-disclosed translation units (ADR-0041's own
  Accepted Amendment).

## Independent Review (self-review, 2026-08-24)

A centralized, evidence-driven self-review pass, before this Plan
proceeds to Human Review — mechanical issues found and fixed directly,
not left for a reviewer to discover:

- **Found and fixed:** the original draft placed the GPU-required
  upload-primitive verification (now V17) inside Milestone 2, before
  `ResourceBinding`'s own third field (Milestone 3) existed — a real
  step-ordering error, since AGENTS.md's Golden Rule forbids exercising
  the new `copyBufferToTexture()`/barrier pair through anything other
  than a real RenderGraph pass, which cannot be built until Milestone 3
  lands. Moved V17 to the end of Milestone 3, with Milestone 2's own GPU
  verification narrowed to resource-creation-only (V12), matching what
  is actually possible at that point in the sequence.
- **Found and fixed:** the first draft did not explicitly resolve
  where `stbi_load()` itself is called, leaving an implicit assumption
  that `atlantis::asset_system::cookTexture()` would call it directly —
  which would have required linking `Stb::Stb` into `Atlantis::AssetSystem`
  itself, directly contradicting ADR-0057's own Decision 4 boundary.
  Resolved explicitly (Pre-draft verification section, D1, D9): the
  decode call lives in `src/tools/asset_cooker/cook_command.cpp`'s own
  new `runCookTextureMode()`; `cookTexture()` itself takes already-
  decoded bytes. This is a genuine Plan-level architectural decision,
  not a silent workaround — recorded explicitly, with its own rationale,
  rather than left implicit.
- **Found and fixed:** the first draft left `TextureAssetData`'s own
  color-space field typed as `atlantis::rhi::SampledTextureFormat`,
  which would give `Atlantis::AssetSystem` a forbidden compile-time
  dependency on `Atlantis::RHI`. Resolved by introducing `TextureColorSpace`
  as Asset System's own, independent enum (D8), translated to RHI's
  `SampledTextureFormat` only by the composition root — matching Spec
  0015's own already-`Accepted` DTO-decoupling precedent exactly, not a
  new pattern invented for this Plan.
- **Found and fixed:** `Material`'s new fields were first drafted as
  `const SampledTexture&`/`const Sampler&` (non-nullable references),
  which cannot represent "no texture bound" — the overwhelming majority
  of existing `Material`s. Corrected to nullable pointers (D3), with an
  explicit both-or-neither `ATLANTIS_CHECK` invariant.
- **No blocking issue found** in the RHI/Vulkan-Backend atomic-step
  boundary (Milestone 2), the combined-submission design (Milestone 9),
  the `stb` CMake-ordering fix (Milestone 1/D10), or the artifact
  overflow/row-order/mip contract (D8) — each was independently
  re-checked against the Spec's own Human Review Decision Table and
  found to match exactly, with no deviation introduced by this Plan.
- **Left as a genuinely open, disclosed mechanical detail, not
  resolved here** (matching Spec 0015's own precedent for leaving
  concrete shapes to Implementation where no architectural content is
  at stake): the exact Slang reflection-JSON string for a combined
  image sampler binding (`"combinedImageSampler"` is this Plan's own
  best-available guess, to be confirmed empirically against a real
  `slangc`-produced reflection JSON during Milestone 4 itself, before
  `parseDescriptorType()`'s own case is finalized); the exact
  `kMaxTextureDimension` value (this Plan fixes `8192` as a reasoned
  default per D8's own overflow-safety math, Implementation may adjust
  with disclosure if a real constraint is found); the exact new shader
  file's own directory placement (`shaders/textured_quad/` assumed,
  matching `shaders/minimal_renderer/`'s own precedent).

No blocking issue remains. This Plan is set to `In Review` — not
`Approved` — pending the same Human Review path every prior Plan in
this repository has followed.
