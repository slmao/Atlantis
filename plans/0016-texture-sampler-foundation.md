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
- **Exhaustive, repo-wide implementer audit — every abstract RHI
  interface, not only `Device`/`CommandList`.** Every class under
  `src/rhi/include/atlantis/rhi/` declaring at least one pure-virtual
  (`= 0`) method, and every one of its implementers anywhere in the
  repository, confirmed fresh:

  | Interface | Pure virtuals | Implementers | This Plan's own new pure virtuals? |
  |---|---|---|---|
  | `Device` | 7 (`createCommandList`, `submit`, `waitIdle`, `createBuffer`, `createTexture`, `createPipeline`, `createOffscreenTarget`) | `VulkanDevice` only — **no Fake** | **Yes** — `createSampledTexture()`, `createSampler()` |
  | `CommandList` | 12 (`transitionResource`×2, `clearColor`, `beginRendering`, `endRendering`, `bindPipeline`, `bindVertexBuffer`, `bindIndexBuffer`, `bindUniformBuffer`, `pushConstant`, `drawIndexed`, `copyRenderTargetToBuffer`) | `VulkanCommandList`, `FakeCommandList` (`tests/render_graph/fake_command_list.h:113`) | **Yes** — `copyBufferToTexture()`, a third `transitionResource()` overload, `bindTexture()` |
  | `Buffer` | 3 (`purpose`, `sizeBytes`, `mappedData`) | `VulkanBuffer`, `FakeBuffer` (`fake_command_list.h:75`) | No — only a new `BufferPurpose` enum *value* (`Staging`), not a new method |
  | `Texture` (depth-only) | 2 (`extent`, `format`) | `VulkanTexture`, `FakeTexture` (`fake_command_list.h:58`) | No — completely untouched, per Spec 0016's own explicit requirement |
  | `RenderTarget` | 2 (`extent`, `format`) | **Three**: `VulkanRenderTarget`, `VulkanOffscreenRenderTarget` (both real, production Vulkan classes — headless and windowed presentation each have their own), `FakeRenderTarget` (`fake_command_list.h:44`) | No — untouched |
  | `Pipeline` | **0** (only a virtual destructor — technically not abstract, but functions as an interface with real implementers) | `VulkanPipeline`, `FakePipeline` (`fake_command_list.h:91`, trivially empty) | No new *method* — `PipelineCreateParams` (a plain struct, not `Pipeline` itself) gains a new field, see D6a below |
  | `OffscreenTarget` | 1 (`acquireTarget`) | `VulkanOffscreenTarget` only — **no Fake** | No |
  | `Presentation` | 5 | `VulkanPresentation` only — **no Fake** | No |
  | `SubmissionSignal` | **0** (virtual destructor only) | `VulkanSubmissionSignal` only — **no Fake** | No |
  | `SampledTexture` (new) | 2 (`extent`, `format`) | **New**: `VulkanSampledTexture`, `FakeSampledTexture` | N/A — the interface itself is new |
  | `Sampler` (new) | 2 (`filter`, `addressMode`) | **New**: `VulkanSampler`, `FakeSampler` | N/A — new |

  **This Plan touches the pure-virtual method set of exactly two existing
  interfaces (`Device`, `CommandList`) and introduces exactly two new
  ones (`SampledTexture`, `Sampler`) — every other RHI interface
  (`Buffer`, `Texture`, `RenderTarget`, `Pipeline`, `OffscreenTarget`,
  `Presentation`, `SubmissionSignal`) is confirmed untouched, method-
  signature-wise, by this Plan.** The atomic-step requirement therefore
  applies to exactly: `VulkanDevice` (no Fake to update in parallel —
  `Device` has none); `VulkanCommandList` **and** `FakeCommandList`
  together (its only two implementers); `VulkanSampledTexture` **and**
  `FakeSampledTexture` together; `VulkanSampler` **and** `FakeSampler`
  together — all four in Milestone 2, the same indivisible step.
  `RenderTarget`'s own three-implementer shape (confirmed above) is
  unaffected by this Plan and is cited here only to make clear this
  Plan is not the first place a multi-implementer RHI interface exists —
  `RenderTarget` additionally has a second, Vulkan-Backend-private
  interface, `atlantis::vulkan_backend::detail::VulkanRenderTargetAccess`
  (`src/vulkan_backend/src/vulkan_render_target_access.h:20`, 4 pure
  virtuals, `dynamic_cast`-accessed from `VulkanCommandList`/`VulkanDevice`
  specifically *because* `RenderTarget` has two real Vulkan implementers
  that must be distinguished safely) — **`SampledTexture` does not need
  an analogous private-access interface or `dynamic_cast`,** because it
  has exactly one real Vulkan implementer, `VulkanSampledTexture`,
  exactly mirroring depth `Texture`'s own single-implementer shape.
  Confirmed precedent: `VulkanCommandList`'s existing
  `transitionResource(Texture&, ...)` implementation accesses the
  concrete type via a raw, unchecked `static_cast<VulkanTexture&>(target)`
  (`vulkan_command_list.cpp:77`, and again at `:134` for the depth
  attachment) — **not** `dynamic_cast`, **not** a private access
  interface — because `FakeTexture` is never passed to a real
  `VulkanCommandList` in production code, only to `FakeCommandList`
  itself. `VulkanCommandList`'s new `copyBufferToTexture()`/
  `transitionResource(SampledTexture&, ...)`/`bindTexture()` overrides
  follow this exact, simpler, single-implementer precedent —
  `static_cast<VulkanSampledTexture&>(destination)`/`static_cast<VulkanSampler&>(sampler)`
  — not the `RenderTarget`-style `dynamic_cast` pattern, which exists
  only because `RenderTarget` genuinely has two real implementers.
  **`Material`/`Mesh` are confirmed, verbatim, to declare no base class
  and no `virtual` method of any kind** (`material.h:18-32`, `mesh.h:16-35`
  — both plain, move-only, non-polymorphic concrete classes composing
  owned `unique_ptr<rhi::X>` members) — **there is no `VulkanMaterial`,
  and none is needed；** `Material`'s own new `SampledTexture`/`Sampler`
  fields (D3) are ordinary data members, not a virtual-dispatch surface,
  so no implementer-synchronization concern applies to `Material` at
  all, contrary to what its structural similarity to `Device`/`CommandList`
  might suggest.
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
existing one today, passes neither).

**Validation entry point and rebind surface, stated precisely (Should
Fix, resolved — not left implicit):** the both-or-neither invariant
(`sampledTexture_ == nullptr` iff `sampler_ == nullptr`) is checked
**exactly once, in the constructor, via `ATLANTIS_CHECK`** — a `Material`
naming a texture with no sampler, or vice versa, fails to construct at
all, a caller-precondition violation, not a representable runtime
state. **There is no other entry point to check, because there is no
other way to set these fields at all**: `Material`'s own public surface
above declares no setter for either pointer (matching `pipeline_`'s own
existing, already-`Accepted` no-setter shape exactly — `pipeline_` has
never been rebindable after construction, and this Plan does not change
that for `sampledTexture_`/`sampler_` either). Once constructed, a
`Material`'s own texture/sampler binding is **fixed for its entire
lifetime** — there is no "reviewed" vs. "unreviewed" rebind path to
distinguish, because no rebind path of any kind is exposed; the only
way to change a `Material`'s own binding is to destroy it and construct
a new one, which re-runs the same, only, `ATLANTIS_CHECK`-guarded
constructor path. This is a structural guarantee (no method exists to
violate it), not a documented convention a future caller could bypass.

**Descriptor-layout branches, both named explicitly:** (1) an
untextured `Material` — `sampledTexture_ == sampler_ == nullptr` —
flows through `createMaterial()` with `PipelineCreateParams::hasSampledTextureBinding
== false` (its own default), producing `VulkanDevice::createPipeline()`'s
existing, single-binding descriptor-set-layout exactly as before this
Plan, byte-for-byte (V23). (2) a textured `Material` — both non-null —
flows through with `hasSampledTextureBinding == true` (set by the
caller from the shader's own real reflection, D5's own exact
mechanism), producing the second, fixed `COMBINED_IMAGE_SAMPLER`
binding. There is no third branch and no partial state: `Material`'s
own constructor-time invariant guarantees these are the only two
reachable shapes.

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
(this section's own regression requirement, V23).

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

**The source staging `Buffer` is deliberately not added to
`ResourceBinding` — this is Spec 0016's own already-approved
architecture (ADR-0056 Decision 4, Human Review item 6: "the source
staging `Buffer` is not itself RenderGraph-tracked, matching
`copyRenderTargetToBuffer()`'s own existing untracked-destination-buffer
precedent"), not reopened or reversed by this Plan.** The reasoning
holds precisely: RenderGraph's own `ResourceBinding`/`incomingState`/
`finalState` machinery exists to drive Vulkan *image layout*
transitions — a `Buffer` has no Vulkan image layout at all, so there is
nothing for that machinery to track for it, exactly as
`copyRenderTargetToBuffer()`'s own existing destination `Buffer`
already establishes. Adding a `Buffer`-carrying field to
`ResourceBinding` would not close a real gap — it would make
`ResourceBinding` track a resource kind that structurally has no state
to transition, the first step toward exactly the "unbounded generic
resource system" this Plan and Spec 0016 both explicitly reject.

**What this Plan does fix — a genuine code-clarity gap, not an
architectural one:** the *pass-building* code must not let the staging
`Buffer` dependency exist only inside an anonymous lambda's own capture
list, invisible to anything reading the pass's own construction site.
The upload pass is built by a small, named helper — not inlined ad hoc
at each call site — taking both resources as explicit, required
parameters:

```cpp
// A named pass-builder, not an anonymous lambda hiding its own inputs.
// Declares the SampledTexture as the one tracked ResourceBinding
// (incomingState=Undefined, finalState=ShaderRead); stagingBuffer is
// an explicit, required parameter to this function -- visible at every
// call site -- even though it is not itself a RenderGraph resource
// (see the Decision note above for why).
void buildTextureUploadPass(atlantis::render_graph::RenderGraphBuilder& builder,
                             atlantis::rhi::Buffer& stagingBuffer,
                             atlantis::rhi::SampledTexture& destination) {
  auto resource = builder.declareResource(&destination);
  auto pass = builder.declarePass("TextureUpload");  // named, not anonymous,
      // so a future graph-debugging/visualization tool has something
      // real to show
  builder.writes(pass, resource, ResourceState::Undefined, ResourceState::ShaderRead);
  builder.setExecute(pass, [&stagingBuffer, &destination](atlantis::rhi::CommandList& cmd) {
    cmd.copyBufferToTexture(stagingBuffer, destination);
  });
}
```

The lambda passed to `setExecute()` still exists (matching every other
pass in this codebase, including `copyRenderTargetToBuffer()`'s own
existing readback pass) — what changes is that `stagingBuffer` is no
longer *introduced* by that lambda's own capture; it arrives as a
named, typed parameter to `buildTextureUploadPass()` itself, so a
reader (or a future caller building the two-texture combined-submission
sequence, Milestone 9) sees the real source/destination/copy-purpose
relationship in one function signature, not buried in a closure body.
Milestone 3's own new test (below) asserts this signature shape
directly, not merely that *some* upload happens to work.

**Location, disclosed explicitly:** Milestone 3's own isolated
upload-primitive GPU test (`tests/vulkan_backend/`) and Milestone 9's
own fixture (`tests/image_regression/fixture/`) are two independent
CMake test targets with no existing shared-helper dependency between
them (confirmed: `tests/vulkan_backend/` and
`tests/image_regression/fixture/` share no common private-header
target today). Each defines its own, independently-declared
`buildTextureUploadPass()`-shaped local helper — matching this
codebase's own already-established precedent of `minimal_cube_fixture.cpp`
and `headless_rendering_gpu_tests.cpp` independently duplicating
near-identical setup/render sequences rather than sharing one, per
Pre-draft verification's own citation of both files. This is not a new
convention invented for this Plan; introducing a first shared
cross-target test helper is explicitly out of this Plan's own scope.

**Explicitly not done**, matching Spec 0016's own Architectural Impact
and this Plan's own scope discipline: no fourth `ResourceBinding`
resource kind, no generic/variant resource-binding refactor, no
`Buffer`-tracking capability added to RenderGraph at all, no
persistent-resource tracking across multiple `execute()` calls.
`isDrawPass()` (`execution.cpp:37-46`) is unmodified — a
`sampledTexture`-carrying binding is never classified as a draw pass.

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
shader's own reflected contract declares it. **Exact mechanism (Should
Fix, resolved precisely — not left as a hand-wave):**
`PipelineCreateParams` (`types.h`) gains one new field,
`bool hasSampledTextureBinding = false;` — set by the caller
constructing a `Material`, which already has the shader's own
`ReflectionMetadata` in hand at that point (the identical, already-
established pattern `vertexInputLayout` itself already uses: derived
from reflection by the caller, passed into pipeline creation as plain
data, never re-derived inside `VulkanDevice`). Concretely:
`hasSampledTextureBinding = std::any_of(reflection.descriptorBindings.begin(), reflection.descriptorBindings.end(), [](const auto& b){ return b.type == DescriptorType::Sampler; })`.
`VulkanDevice::createPipeline()` branches on this one `bool` — **not** a
new enum, not a second `createPipeline()` overload: `false` reproduces
today's exact one-binding layout/pool behavior unconditionally; `true`
adds the second, fixed binding described above. This is the *entire*
untextured-vs-textured descriptor-layout branch — one `if`, one `bool`
field, no other `PipelineCreateParams` field changes shape or meaning.
Existing callers (every pre-this-Plan `createMaterial()` call site)
default-construct `PipelineCreateParams` with `hasSampledTextureBinding`
left at its own default, `false` — **zero source change required at any
existing call site**, satisfying D3's own untextured-`Material`
regression requirement (V23) without a second `createPipeline()` path
to keep in sync. `VulkanCommandList::bindTexture()` writes one
`VkWriteDescriptorSet` (`VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER`,
binding 1) against the currently-bound pipeline's own descriptor set,
pairing the `SampledTexture`'s own `VkImageView` and the `Sampler`'s own
`VkSampler` in one `VkDescriptorImageInfo` (ADR-0056 Decision 9) —
implemented via `static_cast<VulkanSampledTexture&>`/`static_cast<VulkanSampler&>`,
matching `VulkanTexture`'s own established single-implementer precedent
(Pre-draft verification section, above) — never `dynamic_cast`.

### D5a. Staging/readback resource lifecycle under `submit()`/`waitIdle()` failure — not just the success path

**A real gap this Plan's first draft left implicit, fixed here
precisely.** "The staging `Buffer` is destroyed only after `waitIdle()`
returns `Ok`" (this Plan's own original Milestone 9 language) only
states the *success* path. Three distinct failure shapes exist, each
with a different, precisely-stated answer — none handled by leaking,
by a bare `release()`, by assuming `waitIdle()` never fails, or by a
vague "process is exiting anyway":

1. **Early setup failure, before `submit()` is ever called** (e.g.
   `Device::createSampledTexture()` itself returns `Err`, or an earlier
   staging `Buffer`'s own `createBuffer()` fails). **Safe to destroy
   immediately, unconditionally.** No GPU work referencing any of this
   step's resources was ever submitted — there is no outstanding
   GPU-side reference to race against. This is ordinary C++ behavior
   already: the fixture's own setup function returns `Err` early, and
   every already-constructed `unique_ptr<Buffer>`/`unique_ptr<SampledTexture>`
   member is destroyed via normal stack/member unwinding — no special
   handling is added or needed.
2. **`Device::submit()` itself returns `Err(SubmitError::QueueSubmitFailed)`.**
   **Also safe to destroy immediately, unconditionally.** A failed
   `vkQueueSubmit` means the GPU driver never accepted this
   `CommandList`'s own recorded work at all — nothing in it (the copy,
   the barriers, the draw, the readback) ever began executing, so none
   of the resources it references (both staging `Buffer`s, both
   `SampledTexture`s, the readback `Buffer`) has any outstanding
   GPU-side reference from *this* submission. The fixture's own
   `renderOneFrame()`-equivalent function returns `Err` at this point;
   its caller's own `Result`-propagation (matching
   `RuntimeApplication::initializeSteps()`'s own established pattern)
   destroys every resource via ordinary RAII — no `waitIdle()` call is
   needed or made in this path, since nothing was ever queued.
3. **`Device::submit()` succeeds, but the subsequent `Device::waitIdle()`
   returns `Err` — most importantly `Err(SubmitError::DeviceLost)`.**
   **This is treated as fatal for the fixture/test, not a recoverable
   state this Plan attempts to gracefully unwind.** Per the Vulkan
   specification's own `VK_ERROR_DEVICE_LOST` guidance, once a device is
   lost, continued fine-grained interaction with objects created from it
   has no well-defined outcome — there is no Vulkan-spec-guaranteed-safe
   way to selectively determine "did the GPU finish reading this
   specific staging buffer before it died." The only broadly-supported
   recovery shape is destroying the entire `VkDevice` and everything
   created from it together, then (if continuing at all) recreating from
   scratch — not selective per-resource cleanup. **This Plan does not
   build that recovery machinery — it does not exist anywhere else in
   this codebase either.** Every existing headless GPU test already
   treats `waitIdle()` failure as fatal via
   `REQUIRE(device->waitIdle().isOk())` (`headless_rendering_gpu_tests.cpp`,
   `minimal_cube_fixture.cpp`), which aborts the test immediately on
   `Err` — this Plan's own new fixture/test follows the identical,
   already-established pattern, not a new one. **What actually happens
   to the C++ objects on this path, disclosed explicitly, not hidden:**
   ordinary RAII destruction still runs regardless (the test binary
   unwinds/exits), which still calls `VulkanBuffer`/`VulkanSampledTexture`/
   `VulkanSampler`'s own unconditional destructors (`vkDestroyBuffer`/
   `vkDestroyImage`/`vkFreeMemory`/`vkDestroySampler`) — **exactly the
   same behavior every existing `VulkanBuffer`/`VulkanTexture`/
   `VulkanRenderTarget` already has today under `DeviceLost`, with no
   special-casing anywhere in the current codebase.** This Plan
   introduces no new device-loss-awareness to those destructors,
   because doing so would be building a general device-loss-recovery
   system — well beyond Spec 0016's own single-fixture, test-only scope,
   and inconsistent with how every other RHI resource in this codebase
   already behaves.
4. **Ownership, restated plainly:** the fixture struct itself (D1's own
   table: `device` is a `unique_ptr<Device>` *member* of the fixture)
   owns every resource for exactly the fixture's own function-local
   scope — there is no "retained until `Device` destruction" state
   distinct from "retained until fixture teardown," because the fixture
   *is* what owns the `Device` too; both happen at the same scope exit.
   No new ownership model is introduced.
5. **Reuses the existing internal retained-submission/fence contract,
   unchanged, not reinvented.** `VulkanDevice::waitIdle()`'s own already-
   existing internal implementation (`waitAndReleaseRetainedSubmission()`,
   a single-fence `vkWaitForFences` drain, `vulkan_device.cpp:456-475`)
   is exactly what this Plan's `waitIdle()` call already goes through —
   this Plan adds no new fence, no new "retained submission" concept at
   the RHI or fixture level.

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
**Empirically confirmed against a real `slangc` compile during this
Plan's own review (Should Fix, resolved with real evidence, not a
guess) — the actual Slang reflection JSON shape for a `[[vk::binding(1,0)]]
Sampler2D` combined-sampler declaration:**

```slang
[[vk::binding(1, 0)]]
Sampler2D texturedSampler;   // Slang's own combined image-sampler type
                              // -- NOT a separate Texture2D + SamplerState
                              // pair, which would reflect as two bindings
                              // and contradict ADR-0056 Decision 9's own
                              // "combined image sampler" commitment.
```

`slangc -target spirv -profile spirv_1_0 -stage fragment ... -reflection-json ...`
against exactly this declaration produces (module-parameter section):
```json
{
  "name": "texturedSampler",
  "binding": {"kind": "descriptorTableSlot", "index": 1},
  "type": {
    "kind": "resource",
    "baseShape": "texture2D",
    "combined": true,
    "resultType": {"kind": "vector", "elementCount": 4, "elementType": {"kind": "scalar", "scalarType": "float32"}}
  }
}
```
and, per fragment-stage entry point, in `entryPoints[].bindings[]`
(exactly the array `slang_json_transform.cpp`'s own existing `used`-field
handling already reads from, `:186-193`):
```json
{"name": "texturedSampler", "binding": {"kind": "descriptorTableSlot", "index": 1, "used": 1}}
```

**This is a genuinely important, previously-mistaken assumption
corrected here**: a combined sampler binding's own top-level
`binding.kind` is `"descriptorTableSlot"` — **the same string a uniform
buffer already uses**, not a distinct `"combinedImageSampler"` kind as
this Plan's own first draft assumed. The real distinguishing
information lives one level deeper, in the module parameter's own
`type` object: `type.kind == "resource"`, `type.baseShape == "texture2D"`,
and — the field that specifically confirms *combined*, not separate,
image+sampler — `type.combined == true` (a JSON boolean). The fix is
therefore **not** a new top-level `*kind` branch in
`slang_json_transform.cpp` — it is extending the *existing*
`if (*kind == "descriptorTableSlot")` branch's own `moduleTypeKind`
check (`:200-210`), which today hard-rejects (`UnexpectedStructure`)
anything except `"constantBuffer"`:

```cpp
if (*moduleTypeKind == "constantBuffer") {
  metadata.descriptorBindings.push_back(
      DescriptorBinding{.set = space.value_or(0), .binding = *index, .type = DescriptorType::UniformBuffer, .stage = stage});
} else if (*moduleTypeKind == "resource") {
  const auto baseShape = readStringField(*moduleType, "baseShape");
  const JsonValue* combinedField = moduleType->find("combined");
  const bool isCombined = combinedField != nullptr && isTruthy(*combinedField);
  if (baseShape.has_value() && *baseShape == "texture2D" && isCombined) {
    metadata.descriptorBindings.push_back(
        DescriptorBinding{.set = space.value_or(0), .binding = *index, .type = DescriptorType::Sampler, .stage = stage});
  } else {
    // A resource binding kind this module does not model (e.g. a
    // separate, non-combined SamplerState/Texture2D pair, or a 3D/
    // cubemap texture) -- still an explicit, named structural error,
    // never silently skipped, matching the existing constantBuffer
    // branch's own "genuinely new shape, not silently mis-typed" comment.
    return TransformResult::Err(TransformError::UnexpectedStructure);
  }
} else {
  return TransformResult::Err(TransformError::UnexpectedStructure);
}
```

The file's own top comment and the trailing "any other binding kind ...
is outside this round's modeled scope ... silently skipped" note
(`:245-248`) is narrowed accordingly: `descriptorTableSlot`/
`pushConstantBuffer` are no longer the only two recognized *top-level*
kinds needing no change — what changes is that `descriptorTableSlot`
itself now recognizes two *module-type* shapes (`constantBuffer`,
`resource`+`texture2D`+`combined`) instead of one; any *other* top-level
`kind` string remains silently skipped exactly as today, and any
*other* resource shape within a `descriptorTableSlot` (a non-combined
sampler, a non-2D texture, a storage buffer, etc.) is now an explicit,
named `UnexpectedStructure` rather than silently absent from
`descriptorBindings` — a real, disclosed narrowing of the existing
"unknown things are silently ignored" rule for exactly this one new
case, matching the existing `constantBuffer` branch's own precedent of
treating an unrecognized shape as a structural error, not a silent gap.

`reflection_loader.cpp`'s own `parseDescriptorType()` (the function
that round-trips a persisted `ReflectionMetadata` JSON file back into
memory — distinct from `slang_json_transform.cpp`'s own raw-Slang-JSON
parser above) gains a matching case for the string `"sampler"` (the
`DescriptorType::Sampler` enumerator's own serialized name, following
whatever short-string convention `"uniformBuffer"` already uses for
`DescriptorType::UniformBuffer` — confirmed against real, current
`reflection_loader.cpp`/`reflection_metadata.cpp` serialization code
during Milestone 4 itself, not assumed here).

`Float2`'s own reflection parsing (a *separate* function,
`vertexAttributeTypeFromTypeNode()`, `slang_json_transform.cpp:109-120`)
is likewise empirically confirmed: the same probe compile's own vertex-
input `uv` field reflects as `{"kind": "vector", "elementCount": 2,
"elementType": {"kind": "scalar", "scalarType": "float32"}}` — the
existing function already checks exactly this shape for `elementCount
== 3` (line 114); the fix is a one-line widening:
```cpp
if (!elementCount.has_value() || (*elementCount != 3 && *elementCount != 2) || ...) return std::nullopt;
...
return *elementCount == 3 ? VertexAttributeType::Float3 : VertexAttributeType::Float2;
```

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

### D11. First textured fixture — exact scene content, vertex layout, and combined-submission ordering

**Geometry**: two 1×2-triangle quads, side by side in clip space (left
quad `x ∈ [-0.9, -0.1]`, right quad `x ∈ [0.1, 0.9]`, both
`y ∈ [-0.5, 0.5]`, `z = 0`), each with `uv` spanning `[0,1]²` across its
own quad. **No per-vertex color attribute** — the quads' own visible
color comes entirely from the sampled texture, matching this Spec's own
purpose (proving texture sampling, not vertex-color interpolation); this
Plan's own recommendation deliberately differs from Spec 0016's own
illustrative `[[vk::location(2)]]` mention (which implied a
position+color+uv, three-attribute shape carried over from
`minimal_mesh.slang`) — the exact attribute count/location was always a
disclosed Plan-level detail (Spec 0016's own Risks & Open Questions:
"the exact Slang/reflection JSON binding-kind mapping... Plan-level
details"), not a fixed Human Review decision, and a genuinely minimal
two-attribute shape is simpler with no loss of verification value.

**Exact vertex struct, layout, and how `Float2` reaches a real
`VertexInputLayout` (Should Fix, resolved precisely):**
```cpp
struct Vertex {
  float position[3];
  float uv[2];
};  // interleaved, single buffer, single binding -- matching
    // minimal_cube_fixture.cpp's own Vertex{position[3], color[3]}
    // shape exactly (interleaved, not a separate per-attribute buffer);
    // stride = sizeof(Vertex) = 20 bytes (5 floats)
```
`MeshVertexAttributeSchema` (this fixture's own, matching
`minimal_cube_fixture.cpp`'s exact established pattern):
```cpp
{MeshVertexAttributeSchema{.location = 0, .offsetBytes = offsetof(Vertex, position)},   // = 0
 MeshVertexAttributeSchema{.location = 1, .offsetBytes = offsetof(Vertex, uv)}}          // = 12
```
passed, together with `sizeof(Vertex)`, into the **existing, unmodified**
`toVertexInputLayout(vertexMetadata, schema, sizeof(Vertex))` — where
`vertexMetadata` is the new shader's own *real*, `slangc`-produced
reflection (D6, empirically confirmed above: the shader's own
`[[vk::location(0)]] float3 position`/`[[vk::location(1)]] float2 uv`
`VertexInput` fields reflect as `elementCount: 3`/`elementCount: 2`
respectively) — **cross-validated against the real shader, exactly like
every existing fixture, never a hand-built `VertexInputLayout` bypassing
reflection, and no Asset System mesh-schema or test-private-backdoor
involvement of any kind.** `toRhiFormat()` (D6) maps the reflected
`VertexAttributeType::Float3`/`Float2` to RHI's
`VertexAttributeFormat::Float3`/`Float2`; `vertexAttributeFormatToVkFormat()`
(D2/D5) maps those to `VK_FORMAT_R32G32B32_SFLOAT`/`VK_FORMAT_R32G32_SFLOAT`
in `VulkanDevice::createPipeline()`'s own existing attribute loop,
unmodified in shape.

**Textures**: one checkerboard-patterned source PNG (small — e.g. 64×64,
a handful of alternating-color blocks large enough to be unambiguous in
a 512×512 capture), checked in as
`tests/image_regression/fixture/textured_quad_source.png`, cooked
**twice** via `atlantis_add_texture_asset()` (D8): once `COLOR_SPACE
Unorm` (left quad's own `Material`), once `COLOR_SPACE Srgb` (right
quad's own `Material`) — both textures share one `Sampler`
(`Filter::Nearest`, `AddressMode::ClampToEdge` — nearest filtering keeps
the checkerboard's own block edges crisp in the captured golden,
avoiding filtering-interpolation ambiguity at block boundaries).

**Combined-submission ordering, fully explicit (Should Fix, resolved —
matching Milestone 9's own description below exactly):**

1. Staging `Buffer` #1 (`Unorm` pixel bytes) → `SampledTexture` #1
   upload pass (D4's own `buildTextureUploadPass()`; RenderGraph-driven
   `Undefined → TransferDestination → ShaderRead`).
2. Staging `Buffer` #2 (`Srgb` pixel bytes) → `SampledTexture` #2 upload
   pass (same helper, independent call).
3. `Renderer::drawFrame()`'s own draw graph — two `DrawItem`s (left
   quad/`Material` #1/`SampledTexture` #1; right quad/`Material` #2/
   `SampledTexture` #2), both sampling their own now-`ShaderRead`
   texture via `bindTexture()` (D3), both rendering into the **same**
   `RenderTarget` acquired once at the top of the frame; leaves that
   `RenderTarget` in `ResourceState::TransferSource`.
4. Readback graph — `copyRenderTargetToBuffer()` from that same
   `RenderTarget` into the readback `Buffer`.
5. **Exactly one** `Device::submit(std::move(commandList), *target)` —
   covering steps 1–4 together, `target` genuinely drawn-into (step 3)
   and read-from (step 4).
6. **Exactly one** `Device::waitIdle()`.
7. CPU reads `readbackBuffer->mappedData()` and compares — only now
   (D5a's own success-path timing) are both staging `Buffer`s and the
   readback `Buffer` destroyed.

**Proof the `RenderTarget` is genuinely used, not merely passed to
satisfy `submit()`'s signature (Should Fix, new test):** the GPU test
(Milestone 9) additionally captures the readback buffer's own content
against a **known baseline** — the offscreen target's own clear color,
captured via the identical fixture *before* the draw graph runs (a
second, minimal readback exercising only steps 4–6 above against a
freshly-cleared, undrawn target) — and asserts the real, post-draw
capture differs from that baseline in both quads' own screen regions.
A `RenderTarget` reused only as a dummy `submit()` argument, never
actually drawn into, would fail this comparison (it would read back as
the untouched clear color); a genuine draw does not.

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
   `slang_json_transform.cpp`'s real, empirically-confirmed
   `resource`/`texture2D`/`combined`-shape case (D6), the new textured
   contract, `toRhiFormat()`'s `Float2` case, `compile_and_validate.cpp`'s
   real `expectedContract` consumption). `tests/shader_system/`: a real
   Slang reflection JSON — the exact shape already produced and
   confirmed by this Plan's own review (D6's own probe compile,
   `[[vk::binding(1,0)]] Sampler2D` plus `float2 uv`) — becomes this
   step's own committed test fixture input (a small `.slang` file
   declaring both a uniform buffer and a combined sampler, plus a
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
   (D11 — see its own fully explicit, seven-step combined-submission
   ordering and vertex-layout precision). New
   `tests/image_regression/fixture/textured_quad_fixture.h`/`.cpp`,
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
   detail); the `RenderTarget` genuinely participates, confirmed against
   the clear-color baseline comparison D11 describes (not merely passed
   to satisfy `submit()`'s own signature); the captured frame is
   non-degenerate (not all-one-color); Vulkan Validation Layers clean.
   **This step does not itself capture or commit a golden** — see the
   Golden Capture Process below; the code in this step is fully
   mergeable and independently reviewable without any golden PNG in the
   same diff.
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
| V9 | `stb_image`'s `STB_IMAGE_IMPLEMENTATION`/`STB_IMAGE_WRITE_IMPLEMENTATION` macros are each defined in exactly one translation unit per *static library that defines them* (`png_codec.cpp` — both macros — for `atlantis_image_regression_support`; `cook_command.cpp` — `STB_IMAGE_IMPLEMENTATION` only, no write-side needed — for `atlantis_asset_cooker_lib`) — confirmed by a clean link with no duplicate-symbol error for every final binary linking either library (`atlantis_asset_cooker`, `tests/tools/asset_cooker`'s own test executable if it also links `atlantis_asset_cooker_lib`, `atlantis_image_regression_tests`, `atlantis_image_regression_gpu_tests`), not merely code inspection — a static library's own macro-definition count is what avoids ODR conflict, independent of how many executables eventually link it. | Build-level (both configurations) | GPU-independent |
| V10 | `ATLANTIS_BUILD_TESTS=OFF`: a from-scratch configure succeeds and `atlantis_asset_cooker` builds with real `Stb::Stb` linkage available (`--kind=texture` functional). `ATLANTIS_BUILD_TESTS=ON`: a separate from-scratch configure/build still passes `tests/image_regression/`'s own two pre-existing `Stb::Stb` consumers unmodified. | Manual, recorded (matching Plan 0012 Section D7's own established CMake-reconfigure procedure) | Manual |
| V11 | RHI value-type equality: `SampledTextureCreateParams`/`SamplerCreateParams` `operator==` confirmed, matching `OffscreenTargetCreateParams`'s own existing test shape. | `tests/rhi/types_tests.cpp` | GPU-independent |
| V12 | `SampledTextureCreateError`/`SamplerCreateError` plumb through `Device::createSampledTexture()`/`createSampler()`'s own `Result::Err` channel correctly for each named failure mode (allocation, image creation, image-view creation, sampler creation), matching `TextureCreateError`'s own existing test discipline. | `tests/vulkan_backend/` (GPU-required, real allocation-failure conditions may be simulated per existing precedent) | GPU-required where real allocation is exercised |
| V13 | `Sampler`'s own immutability: a `static_assert`/type-trait check confirms no public mutator exists anywhere on its interface. | `tests/rhi/` or `tests/vulkan_backend/` | GPU-independent (compile-time) |
| V14 | The two new barrier-plan entries (`Undefined -> TransferDestination`, `TransferDestination -> ShaderRead`) produce the exact expected `VkImageMemoryBarrier` fields (layout, stage, access masks); an unlisted pair naming `SampledTexture` still triggers the existing `ATLANTIS_CHECK_MSG` failure — regression-confirmed, not merely assumed unchanged. | `tests/vulkan_backend/resource_state_mapping_tests.cpp` | GPU-independent |
| V15 | `VertexAttributeFormat::Float2` maps to `VK_FORMAT_R32G32_SFLOAT`; the exhaustive switch in `vertexAttributeFormatToVkFormat()` still hard-fails (`ATLANTIS_CHECK_MSG`) for any hypothetically-unhandled value. | `tests/vulkan_backend/` | GPU-independent |
| V16 | RenderGraph: a one-pass graph declaring a `sampledTexture` binding with `incomingState=Undefined`/`finalState=ShaderRead` drives the expected `FakeCommandList`-recorded `transitionResource(SampledTexture&, ...)` sequence; Guard 0 still rejects a binding naming more than one of `target`/`depthTexture`/`sampledTexture`, or none. | `tests/render_graph/` | GPU-independent (via `FakeCommandList`) |
| V17 | A real `SampledTexture`, uploaded through `buildTextureUploadPass()` (D4's own named helper, called with a real staging `Buffer` and destination `SampledTexture` as explicit parameters — not a bare lambda) and a real one-pass RenderGraph execution, completes with Vulkan Validation Layers clean — confirming the barrier/copy mechanics alone are correct before the full combined-submission fixture exercises them together with a draw. | `tests/vulkan_backend/` (new upload-primitive GPU test, Milestone 3) | GPU-required |
| V18 | Shader System: a real captured Slang reflection JSON for a `[[vk::binding(1,0)]] Sampler2D` declaration (`binding.kind == "descriptorTableSlot"`, `type.kind == "resource"`, `type.baseShape == "texture2D"`, `type.combined == true` — the real, `slangc`-confirmed shape, D6) produces `DescriptorType::Sampler` correctly, and a mismatched shape (`combined == false`, or a non-`texture2D` `baseShape`) is rejected as `UnexpectedStructure`, never silently dropped; a real captured reflection declaring a `float2` vertex input produces `VertexAttributeType::Float2`, mapped by `toRhiFormat()` to `VertexAttributeFormat::Float2`. | `tests/shader_system/`, `tests/shader_system/rhi_integration/` | GPU-independent |
| V19 | The new two-binding descriptor contract accepts exactly a matching two-binding reflection and rejects a mismatched one (wrong count, wrong stage, wrong descriptor type at either binding), matching `minimalRendererExpectedDescriptorContract()`'s own existing test discipline. | `tests/shader_system/` | GPU-independent |
| V20 | `expectedContract` wiring regression: `minimal_mesh.slang`'s own existing reflection still validates correctly against `minimalRendererExpectedDescriptorContract()` now that `compileAndValidate()` reaches it via the newly-read `expectedContract` field rather than an unconditional call — byte-for-byte the same accept/reject outcome as before this Plan. | `tests/tools/shader_compiler/` | Tool-required (real `slangc`/`spirv-val`, no GPU) |
| V21 | `expectedContract` wiring, positive case: the new textured shader's own real reflection validates successfully against the new two-binding contract when `expectedContract` names it. | `tests/tools/shader_compiler/` | Tool-required |
| V22 | `Material`'s new fields are non-owning pointer types, confirmed via `static_assert`/type trait — not merely "not observed to be mutated." | `tests/renderer/renderer_ownership_tests.cpp` | GPU-independent (compile-time) |
| V23 | **Existing untextured `Material`/`DrawItem` regression, explicit, not assumed:** an untextured `Material` drives the exact same `FakeCommandList`-recorded call sequence, byte-for-byte, as it did before this Plan — no `bindTexture` call recorded, no other call reordered or altered. | `tests/renderer/renderer_ownership_tests.cpp` | GPU-independent |
| V24 | A textured `Material` drives exactly one additional recorded `bindTexture` call, positioned immediately after `bindUniformBuffer`, with the expected `SampledTexture`/`Sampler` identities. | `tests/renderer/renderer_ownership_tests.cpp` | GPU-independent |
| V25 | Vulkan descriptor pool/layout: an untextured `Pipeline`'s own descriptor-set-layout creation is completely unaffected (one binding, as before this Plan); a textured `Pipeline` gains exactly the second, fixed `COMBINED_IMAGE_SAMPLER` binding at binding 1, fragment stage; the device-level pool's own new pool-size entry does not affect `maxSets` or the existing uniform-buffer pool-size entry. | `tests/vulkan_backend/` | GPU-required |
| V26 | `bindTexture()`'s own `VkWriteDescriptorSet` correctly pairs the bound `SampledTexture`'s `VkImageView` and the bound `Sampler`'s `VkSampler` in one `VkDescriptorImageInfo`, confirmed via Vulkan Validation Layers clean during an actual textured draw (not inferred from a correct-looking image alone). | `tests/vulkan_backend/`, Milestone 9's own GPU test | GPU-required |
| V27 | **Combined submission, exactly once, and the `RenderTarget` genuinely participates:** the textured fixture's own composition code calls `Device::submit()` exactly one time for the seven-step upload(s)+draw+readback sequence (D11), against the same `RenderTarget` the draw and readback graphs actually use — confirmed by call-count instrumentation or direct code inspection. **Not a dummy-target risk left unaddressed:** the readback content is compared against a freshly-cleared, undrawn baseline capture of the same target (D11's own explicit baseline-comparison design) — a `RenderTarget` reused only to satisfy `submit()`'s signature, never actually drawn into, would read back identical to that baseline; a genuine draw does not. | `tests/image_regression/textured_quad_gpu_tests.cpp` | GPU-required |
| V28 | Staging-buffer lifetime: both staging `Buffer`s remain alive (not destroyed) until after the combined submission's own `waitIdle()` returns `Ok` — confirmed by code-structure inspection (declaration/destruction ordering) and, where feasible, a debug-build lifetime assertion. | `tests/image_regression/textured_quad_gpu_tests.cpp` | GPU-required |
| V29 | `Material` destruction precedes `SampledTexture`/`Sampler` destruction in the fixture's own scope-exit order — confirmed by the fixture struct's own declared member order (D3) and, if feasible, an explicit destructor-order assertion in debug builds. | `tests/image_regression/fixture/textured_quad_fixture.cpp` | GPU-independent (structural) / manual code review |
| V30 | The two quads, cooked from identical source pixel bytes under `Rgba8Unorm` vs. `Rgba8Srgb`, produce **visibly, measurably different** sampled colors in the captured frame — confirmed both by the golden's own human visual-confirmation step (Golden Capture Process, step 3) and by a direct pixel-value comparison in the GPU test itself (the two quads' own captured regions must differ by more than the existing channel-tolerance-0 rule's own noise floor). | `tests/image_regression/textured_quad_gpu_tests.cpp`, Golden Capture Process | GPU-required |
| V31 | New golden, zero channel difference on a second, independent run of the same fixture (determinism, not "looked right once"). | Golden Capture Process, then re-run against the committed golden | GPU-required |
| V32 | New golden captured strictly under ADR-0042's "Initial baseline bootstrap" category, satisfying all six of its own numbered constraints explicitly (Spec 0016 Human Review item 14) — confirmed in the golden-capture commit's own PR description, not merely asserted here. | PR description (Milestone 10's own commit) | Manual |
| V33 | **Existing-golden regression, explicit:** `minimal_cube` and `world_scene` headless tests re-run unmodified, against their own existing, unmodified goldens, zero channel difference — proving this Plan's RenderGraph/`Material`/Vulkan Backend/descriptor-pool changes did not disturb either existing rendering path. | `tests/image_regression/` | GPU-required |
| V34 | `git diff --stat` (or equivalent) confirms the two existing golden PNG/sidecar pairs under `tests/image_regression/goldens/` are byte-identical to `main` at every commit in this Plan's own PR. | PR-level check | Manual |
| V35 | Module-boundary scan, both header-include and CMake link-graph: `Atlantis::AssetSystem` still names no RHI header and no `stb_image.h`/`stb_image_write.h` include outside `src/tools/asset_cooker/cook_command.cpp`; `atlantis_asset_system`'s own `target_link_libraries` closure (transitively) never reaches `Stb::Stb` — confirmed via CMake's own dependency graph (e.g. `cmake --graphviz` or an equivalent target-property inspection), not header-grep alone; `Atlantis::Renderer` still names no `Atlantis::VulkanBackend` header. | `tests/asset_system/module_boundary_tests.cpp` plus a CMake-level link-graph check (new) | GPU-independent |
| V36 | `/w14062` positive/negative build check on `atlantis_asset_cooker_lib`: a temporarily removed `AssetKind::Texture` case in `runCookCommand()`'s own switch (or in `textureCookErrorMessage()`) fails the build, naming that exact enumerator; restoring it builds clean again — mirroring Spec 0013's own established mechanism. | Manual, recorded (build-log evidence) | Manual |
| V37 | Full Debug and Release builds from a fresh configure; `ctest -LE gpu` and `ctest -L gpu` both green on both configurations; Vulkan Validation Layers grepped clean throughout (zero `VUID`/Validation Error/Warning matches) — not merely absence-of-crash. | Full test suite | GPU-independent + GPU-required |
| V38 | A genuine, human-observed visual confirmation of the new golden's own captured image (matching Spec 0014/0015's own V20/V24 discipline for what "human confirmation" means, applied here to a headless-only capture — no windowed claim). | Golden Capture Process, step 3; recorded in the golden-capture commit's own PR | Manual |
| V39 | `buildTextureUploadPass()`'s own signature requires the staging `Buffer` as an explicit, named, non-optional parameter (D4) — confirmed by a direct signature/compile-time check (the function cannot be called without one) — closing the "hidden via lambda capture" gap without adding the `Buffer` to `ResourceBinding` itself. | `tests/render_graph/` | GPU-independent (compile-time/structural) |
| V40 | **Submit failure, resource safety (D5a, case 2):** `Device::submit()` returning `Err(SubmitError::QueueSubmitFailed)` (simulated per existing precedent) is followed by immediate, unconditional destruction of every resource this operation created, with no crash, no leak-detector warning, and no attempted `waitIdle()` call (since nothing was ever queued). | `tests/vulkan_backend/` or `tests/image_regression/` (GPU-required, simulated failure) | GPU-required |
| V41 | **`waitIdle()` failure, fail-fast confirmed as intentional, not accidental (D5a, case 3):** the new fixture's own test follows the identical `REQUIRE(device->waitIdle().isOk())`-style fail-fast pattern every existing headless GPU test already uses — confirmed by direct code inspection that no bespoke, untested "graceful DeviceLost recovery" code path was silently introduced. | `tests/image_regression/textured_quad_gpu_tests.cpp` (code-inspection) | Manual |
| V42 | **Early setup failure (D5a, case 1):** a forced failure at each individual setup stage (e.g. the second `createSampledTexture()` call, simulated per existing precedent) before `submit()` is ever reached leaves no resource leaked and requires no explicit cleanup code beyond ordinary RAII — confirmed by a clean run under the project's existing leak-detection discipline (matching `cookStaticMesh()`'s own established V11/V12-style forced-failure test shape). | `tests/image_regression/` or `tests/vulkan_backend/` (GPU-required) | GPU-required |
| V43 | The relocated `cmake/AtlantisStb.cmake` declares the identical pinned commit hash (`URL`/`URL_HASH`) `cmake/AtlantisDependencies.cmake` declared before the move — confirmed by direct diff of the two declarations, not merely "it still builds." | PR-level diff check (Milestone 1's own commit) | Manual |
| V44 | `PipelineCreateParams::hasSampledTextureBinding`'s own default (`false`) requires zero source change at any pre-existing `createMaterial()`/`createPipeline()` call site — confirmed by a full-repository build succeeding with no call site touched beyond what Milestone 5 itself lists (D5's own exact mechanism). | Build-level, `git diff --stat` cross-check against Files/Modules Touched | Manual |

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

- V1–V44 all executed and recorded; V10, V32, V34, V36, V38, V41, V43,
  V44 recorded as manual verification in the Implementation PR(s).
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

## Independent Review — Round 1 (self-review, 2026-08-24)

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
  at stake): the exact `kMaxTextureDimension` value (this Plan fixes
  `8192` as a reasoned default per D8's own overflow-safety math,
  Implementation may adjust with disclosure if a real constraint is
  found); the exact new shader file's own directory placement
  (`shaders/textured_quad/` assumed, matching
  `shaders/minimal_renderer/`'s own precedent). **The Slang reflection-
  JSON shape for a combined image sampler binding, flagged in this
  round as an open guess, is resolved with real evidence in Independent
  Review — Round 2 below** — no longer open.

Round 1 found no blocking issue beyond the two corrected above. Round 2
below, prompted by a further Plan Review, replaces the guess this round
left open with real evidence and fixes three further, genuine gaps.

## Independent Review — Round 2 (final Plan Review, 2026-08-24)

A second, targeted review round — not a broad re-review — resolving
three Must Fix findings and five Should Fix precision items before this
Plan proceeds to formal Human Review, without broadening scope further:

- **Must Fix, resolved — not by reversing Approved architecture.** A
  reviewer asked whether the third `ResourceBinding` field should be a
  composite type also carrying the source staging `Buffer`. Re-verified
  against ADR-0056 Decision 4 and Spec 0016 Human Review item 6: the
  Approved design **already explicitly decided** the staging `Buffer` is
  not RenderGraph-tracked, matching `copyRenderTargetToBuffer()`'s own
  established precedent — reversing that at Plan-review time would be
  exactly the kind of silent architecture change AGENTS.md forbids, and
  would also reintroduce the "unbounded generic resource system" both
  Spec 0016 and this Plan explicitly reject (Buffers have no Vulkan
  image layout — there is nothing for `ResourceBinding`'s own
  state-transition machinery to track). The **real, separate** gap the
  same finding pointed at — the dependency being visible only inside an
  anonymous lambda's own capture list — is genuine and is fixed: D4 now
  specifies a named `buildTextureUploadPass()` helper taking the staging
  `Buffer` as an explicit, required parameter, with the architectural
  reasoning stated inline so a future reader sees a deliberate design,
  not an oversight (V39).
- **Must Fix, resolved with a full, repo-wide audit, not the
  previously-checked subset.** Every abstract RHI interface
  (`Device`, `CommandList`, `Buffer`, `Texture`, `RenderTarget`,
  `Pipeline`, `OffscreenTarget`, `Presentation`, `SubmissionSignal`) and
  every one of its real implementers (Vulkan and Fake) is now
  enumerated in the Pre-draft verification section's own table. Two
  genuine findings: (1) `RenderTarget` has **three** implementers
  (`VulkanRenderTarget`, `VulkanOffscreenRenderTarget`, `FakeRenderTarget`)
  — irrelevant to this Plan (its interface is untouched) but recorded so
  a future reader does not assume the two-implementer `CommandList`
  shape generalizes; (2) `Material`/`Mesh` are confirmed, verbatim, to
  have zero virtual methods and no base class — there is no
  `VulkanMaterial`, and the "every implementer in one atomic step" rule
  simply does not apply to `Material`'s own new fields, which are
  ordinary data members. `SampledTexture`/`Sampler` follow depth
  `Texture`'s own single-real-implementer precedent (`static_cast`, not
  `RenderTarget`'s own `dynamic_cast`-based access pattern, which exists
  only because `RenderTarget` genuinely has two real implementers).
- **Must Fix, resolved precisely, not left implicit.** "Staging
  `Buffer`s survive until `waitIdle()` returns `Ok`" only ever covered
  the success path. New D5a section states three distinct failure
  shapes explicitly: early setup failure (safe, immediate, unconditional
  cleanup — nothing was ever submitted); `submit()` itself failing
  (equally safe and immediate — the GPU never began executing this
  `CommandList`'s own work); `waitIdle()` returning `Err` including
  `DeviceLost` (treated as fatal for the fixture, matching every
  existing headless GPU test's own already-established
  `REQUIRE(...isOk())` fail-fast pattern — not a new graceful-recovery
  mechanism this codebase does not have anywhere else, and building one
  would be solving a problem well outside Spec 0016's own single-fixture
  scope). Confirmed this Plan reuses `VulkanDevice::waitIdle()`'s own
  existing internal `waitAndReleaseRetainedSubmission()`/fence contract
  unchanged — no new fence or retained-submission concept introduced
  (V40, V41, V42).
- **Should Fix, resolved with real evidence, not a guess — the most
  consequential finding of this round.** A real `slangc` compile (this
  repository's own pinned Vulkan SDK toolchain, run directly during this
  review) of a `[[vk::binding(1,0)]] Sampler2D` declaration confirms the
  reflection JSON's top-level `binding.kind` is `"descriptorTableSlot"`
  — **the same string a uniform buffer already uses** — not a distinct
  `"combinedImageSampler"` kind as Round 1 had guessed. The real
  distinguishing shape is one level deeper:
  `type.kind == "resource"`, `type.baseShape == "texture2D"`,
  `type.combined == true`. D6 now shows the exact confirmed JSON and the
  exact, corrected `slang_json_transform.cpp` extension (widening the
  existing `descriptorTableSlot` branch's own `moduleTypeKind` check,
  not adding a new top-level branch) — a real, previously-unverified
  assumption this round would have caught only after Milestone 4 was
  already underway, not before. `Float2`'s own reflection shape
  (`elementCount: 2`) is confirmed by the same probe compile (V18).
- **Should Fix, resolved:** `Material`'s both-or-neither invariant is
  confirmed to have exactly one entry point (the constructor) and no
  rebind surface of any kind (no setter exists, matching `pipeline_`'s
  own existing precedent) — a structural guarantee, not a documented
  convention. `PipelineCreateParams::hasSampledTextureBinding`'s own
  exact mechanism (a caller-derived `bool`, defaulting to `false`,
  zero change to any existing call site) is now specified precisely
  rather than hand-waved as "indicates" (D5, V44).
- **Should Fix, resolved:** the fixture's own `Vertex` struct, binding,
  offset, stride, and location assignment for `Float2` are now fully
  specified (D11) — interleaved, single buffer, matching
  `minimal_cube_fixture.cpp`'s own established shape, cross-validated
  against the shader's own real reflection, no Asset System or
  test-private-backdoor involvement. This Plan's own recommendation
  drops the per-vertex color attribute Spec 0016's own illustrative
  `location(2)` mention implied, explicitly disclosed as a Plan-level
  simplification (a texture-sampling proof needs no vertex color), not
  a silent deviation.
- **Should Fix, resolved:** the combined-submission ordering (D11) is
  now a fully explicit seven-step sequence, and a new baseline-
  comparison test (V27, expanded) proves the `RenderTarget` is
  genuinely drawn into and read from, not merely passed to satisfy
  `submit()`'s own signature.
- **Should Fix, resolved:** `stb`'s CMake split is re-verified against
  the pinned commit hash staying identical across the file move (V43),
  the per-static-library (not per-executable) implementation-macro
  count (V9, corrected wording), and an explicit CMake link-graph check
  — not header-grep alone — confirming `Atlantis::AssetSystem`'s own
  link closure never reaches `Stb::Stb` (V35, corrected wording).

No blocking issue remains after this round. This Plan is set to
`In Review` — not `Approved` — pending the same Human Review path every
prior Plan in this repository has followed. Every Must Fix and Should
Fix item from this round's own review is now resolved with either a
corrected design (backed, where relevant, by real empirical evidence —
the `slangc` probe) or an explicit, disclosed confirmation that the
Approved architecture already correctly addressed the concern raised.
