# src/

**`core/`** — Atlantis Core: logging, assertions, and a minimal
result/error utility type. Implemented per
[specs/0001-project-foundation.md](../specs/0001-project-foundation.md),
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md),
and [ADR-0006](../adr/0006-dependency-management.md)–[ADR-0010](../adr/0010-cmake-structure.md).

**`platform/`** — Atlantis Platform: application lifecycle, window
creation/ownership/destruction, `NativeWindowHandle`, `PlatformEvent`
delivery, and monotonic timing. Only the **Windows** path is implemented,
per [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md),
[plans/0002-platform-foundation.md](../plans/0002-platform-foundation.md),
and [ADR-0005](../adr/0005-platform-module-multi-os-windowing.md),
[ADR-0010](../adr/0010-cmake-structure.md)–[ADR-0013](../adr/0013-platform-window-ownership-and-lifetime.md).
Android and iOS are specified architecturally (ADR-0005, ADR-0012,
ADR-0013) but **not implemented** — `src/platform/src/windows/` is the
only per-OS implementation directory that currently exists.

**`rhi/`** — Atlantis RHI: backend-independent RHI interfaces. Target
`atlantis_rhi`, alias `Atlantis::RHI`. Provides `Device` (an opaque
logical-GPU handle, with `createCommandList()`/`submit()`/`waitIdle()`),
`Presentation` (construction, resize notification, conditional swapchain
recreation, metadata query, and `acquireNextTarget()`/`present()`), a
frame-scoped write-only `RenderTarget`, a minimal `CommandList`
(`transitionResource`, `clearColor`), and `SubmissionSignal`, plus the
supporting value types `Extent2D`, `Format`, `SwapchainMetadata`,
`PresentationError`, and `ResourceState`. RHI's public headers reference
no Vulkan or Platform type. Non-frame construction implemented per
[specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md),
[plans/0003-rhi-vulkan-windowed-foundation.md](../plans/0003-rhi-vulkan-windowed-foundation.md),
and [ADR-0001](../adr/0001-rhi-backend-independence.md),
[ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
[ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
[ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md);
the frame-execution surface (`RenderTarget`, `CommandList`, `submit()`/
`acquireNextTarget()`/`present()`, `SubmissionSignal`) implemented per
[specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md),
[plans/0006-rhi-render-graph-frame-execution-foundation.md](../plans/0006-rhi-render-graph-frame-execution-foundation.md),
and [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md),
[ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md);
the minimal GPU resource/pipeline/draw surface (`Buffer`, `Texture`,
`Pipeline`, `Device::createBuffer/createTexture/createPipeline()`, and
`CommandList`'s bind/push-constant/draw operations) implemented per
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md),
[plans/0007-minimal-renderer.md](../plans/0007-minimal-renderer.md), and
[ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md),
[ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md);
a sampled-texture surface (`SampledTexture`, `Sampler`,
`Device::createSampledTexture()/createSampler()`,
`CommandList::copyBufferToTexture()`, and `BufferPurpose::Staging` as a
host-visible upload source) implemented per
[specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md),
[plans/0016-texture-sampler-foundation.md](../plans/0016-texture-sampler-foundation.md),
and
[ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)–[ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md),
merged via [PR #78](https://github.com/slmao/Atlantis/pull/78).

**`vulkan_backend/`** — Atlantis Vulkan Backend: Phase 1's sole graphics
backend, implementing RHI's interfaces. Target `atlantis_vulkan_backend`,
alias `Atlantis::VulkanBackend`. Implements Vulkan instance/device
construction, Validation Layer enforcement, Windows-only private WSI
surface creation, swapchain ownership and resize-driven recreation, the
concrete acquire/execute/submit/present state machine (a per-swapchain-
image render-finished-semaphore pool, a persistent acquire-complete
semaphore, a single-frame-in-flight command pool/fence), and
`vkCmdClearColorImage`/barrier recording; still no general GPU memory
allocator (direct, unpooled, per-resource allocation only — see
[ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)).
Vulkan and Win32 WSI types stay private to this module's own
implementation files; Windows is currently the only implemented WSI path
(Android is not implemented). Non-frame swapchain construction
implemented per
[specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md),
[plans/0003-rhi-vulkan-windowed-foundation.md](../plans/0003-rhi-vulkan-windowed-foundation.md),
and [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
[ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md);
frame execution implemented per
[specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md),
[plans/0006-rhi-render-graph-frame-execution-foundation.md](../plans/0006-rhi-render-graph-frame-execution-foundation.md),
and [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)–[ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md),
merged via [PR #23](https://github.com/slmao/Atlantis/pull/23) and a
post-merge GPU-verification fix PR,
[PR #24](https://github.com/slmao/Atlantis/pull/24) (three real Vulkan
Validation Layer defect fixes and one resize→minimize crash fix, all
found only by running on real GPU hardware); `VulkanBuffer`/
`VulkanTexture`/`VulkanPipeline`, the graphics-pipeline/draw-command
recording path, and Vulkan dynamic rendering as a capability-detected
Core/Extension dual path (no `VkRenderPass`/`VkFramebuffer` anywhere)
implemented per
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md),
[plans/0007-minimal-renderer.md](../plans/0007-minimal-renderer.md), and
[ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)–[ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md),
merged via [PR #28](https://github.com/slmao/Atlantis/pull/28); the
dynamic-rendering Core path's post-merge fix (separating it fully from
`VK_KHR_dynamic_rendering`) implemented per
[ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)'s
"Accepted Amendment — 2026-08-13", merged via
[PR #29](https://github.com/slmao/Atlantis/pull/29) (amendment) and
[PR #30](https://github.com/slmao/Atlantis/pull/30) (fix); `VulkanSampledTexture`/
`VulkanSampler` and the real one-time CPU→GPU texture upload
(`copyBufferToTexture()`'s own staging-buffer-to-image copy and
Undefined→TransferDestination→ShaderRead barrier sequencing) implemented
per
[specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md),
[plans/0016-texture-sampler-foundation.md](../plans/0016-texture-sampler-foundation.md),
and
[ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)–[ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md),
merged via [PR #78](https://github.com/slmao/Atlantis/pull/78).

**`render_graph/`** — Atlantis RenderGraph: render graph construction,
compilation, and execution. Target `atlantis_render_graph`, alias
`Atlantis::RenderGraph`; PUBLIC dependency is `Atlantis::Core` and
`Atlantis::RHI`. `RenderGraphBuilder` declares passes/logical resources
and their read/write usage, each tagged with a `ResourceState`
(single-producer-per-resource model); `compile()` derives
producer→reader dependency edges, a deterministic declaration-order-
tie-break pass order, and either a `CompiledGraph` or a `CompileError`
(`MultipleProducersError`/`DependencyCycleError`, with a deterministic
cycle witness). `CompiledGraph` is independently owned and move-only —
it outlives, and never borrows from, the builder that produced it.
`execute()` binds a frame-scoped `RenderTarget` (and, since Spec 0007, a
depth `Texture`) to the graph's declared resources, runs each pass's
execution callback against RHI's `CommandList`, and inserts automatic
dependency-derived resource-state transitions — RenderGraph never calls
`Device::submit()` or `Presentation::present()` itself. `execute()` also
recognizes a **draw pass** (a `ColorAttachmentOutput`/
`DepthAttachmentReadWrite`-tagged usage) and automatically brackets it
with Vulkan dynamic-rendering attachment-scoping calls. **Not yet
implemented:** pass culling and resource lifetime/aliasing. Construction/
compilation implemented per
[specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md),
[plans/0005-render-graph-foundation.md](../plans/0005-render-graph-foundation.md),
and [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md);
`execute()` implemented per
[specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md),
[plans/0006-rhi-render-graph-frame-execution-foundation.md](../plans/0006-rhi-render-graph-frame-execution-foundation.md),
and [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md);
multi-attachment/draw-pass execution implemented per
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md),
[plans/0007-minimal-renderer.md](../plans/0007-minimal-renderer.md), and
[ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md);
`ResourceBinding` gained a third bind-kind (`sampledTexture`, alongside
`target`/`depthTexture` — exactly one of the three non-null per entry),
tracking a one-time texture upload's own
Undefined→TransferDestination→ShaderRead transition, with `finalState`
widened to sampledTexture bindings (the same trailing-transition
mechanism Spec 0010/ADR-0039 already established, not a new one),
implemented per
[specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md),
[plans/0016-texture-sampler-foundation.md](../plans/0016-texture-sampler-foundation.md),
and
[ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md),
merged via [PR #78](https://github.com/slmao/Atlantis/pull/78).

**`renderer/`** — Atlantis Renderer: the thin, stateless frame
orchestrator that turns a caller-supplied mesh, material, and camera into
recorded GPU work. Target `atlantis_renderer`, alias `Atlantis::Renderer`,
depending only on `Atlantis::Core`, `Atlantis::RHI`, `Atlantis::RenderGraph`
— never Platform, Vulkan Backend, Win32, or any `Vk*` type. Provides
`Mesh`/`createMesh()`, `Material`/`createMaterial()` (both caller-owned,
never created/cached/looked-up by `Renderer` itself), `DrawItem`, and
`Renderer::drawFrame()`, which retains no GPU resource or frame-to-frame
state across calls. Implemented per
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md),
[plans/0007-minimal-renderer.md](../plans/0007-minimal-renderer.md), and
[ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
merged via [PR #28](https://github.com/slmao/Atlantis/pull/28). This same
spec extended `rhi/` with `Buffer`/`Texture`/`Pipeline` and a minimal
graphics-pipeline/binding/draw-command surface
([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md),
[ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)),
`vulkan_backend/` with Vulkan dynamic rendering as a capability-detected
Core/Extension dual path
([ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md) —
its Core path was fixed post-merge by
[PR #29](https://github.com/slmao/Atlantis/pull/29)/[PR #30](https://github.com/slmao/Atlantis/pull/30),
see that ADR's "Accepted Amendment — 2026-08-13" section), and
`render_graph/` with multi-attachment/draw-pass execution
([ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)).
Only a single, fixed, solid/vertex-color material and a single, fixed,
hand-authored mesh are supported this round — see the spec's own
Non-Goals for the full list of what this module deliberately does not
do (Shader System, scene graph/ECS/asset system, multiple materials,
lighting/texturing, GPU-driven/bindless/instanced draws, and more).
`Material`'s constructor gained a second, nullable, both-or-neither
pair — `const atlantis::rhi::SampledTexture*`/`const atlantis::rhi::Sampler*`,
both defaulting to `nullptr` — so a `Material` may optionally sample one
fixed texture through one fixed descriptor binding; still one texture
per `Material`, never a material graph or multiple slots, implemented
per
[specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md),
[plans/0016-texture-sampler-foundation.md](../plans/0016-texture-sampler-foundation.md),
and
[ADR-0056](../adr/0056-texture-upload-resource-state-and-descriptor-binding.md),
merged via [PR #78](https://github.com/slmao/Atlantis/pull/78).

**`shader_system/`** — Atlantis Shader System: a build-time Slang →
SPIR-V compile/reflect/validate pipeline, superseding
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)'s
temporary checked-in-bytecode bootstrap. Target `atlantis_shader_system`,
alias `Atlantis::ShaderSystem` (`Atlantis::Core`-only): a private JSON
parser, the versioned `ReflectionMetadata` schema and its loader/saver,
`transformSlangReflectionJson()` (mapping Slang's raw `-reflection-json`
output onto that schema), the fixed Minimal Renderer descriptor contract
and its validator, and `slangc`/`spirv-val` argv builders. A second
target, `atlantis_shader_system_rhi_integration` (alias
`Atlantis::ShaderSystemRhiIntegration`), is the only target in the
repository depending on both `Atlantis::ShaderSystem` and
`Atlantis::RHI`: `toVertexInputLayout()`/`toPushConstantSize()` combine
reflected shader data with a caller-supplied host vertex schema into
RHI's existing `VertexInputLayout`/push-constant size, never constructing
or caching any RHI/GPU resource itself. Implemented per
[specs/0008-shader-system-foundation.md](../specs/0008-shader-system-foundation.md),
[plans/0008-shader-system-foundation.md](../plans/0008-shader-system-foundation.md),
and [ADR-0028](../adr/0028-shader-system-source-language-and-compiler.md)–[ADR-0031](../adr/0031-shader-system-artifact-versioning-and-reproducibility.md),
merged via [PR #36](https://github.com/slmao/Atlantis/pull/36).

**`asset_system/`** — Atlantis Asset System: a deterministic
authoring-source → runtime-artifact pipeline, now for four asset types.
Target `atlantis_asset_system`, alias `Atlantis::AssetSystem`
(`Atlantis::Core`-only — no RHI, Renderer, RenderGraph, Shader System,
Vulkan Backend, Platform, Tools, or `atlantis::world` dependency,
verified by an include-scanning test). Provides logical-path
normalization (a pure string algorithm, never
`std::filesystem::path`-based), a 64-bit FNV-1a Asset ID, declared-set
collision/case-conflict validation
(`validateAssetSet()`/`DeclaredAsset`), strict parse/serialize for
every format (authoring source, metadata sidecar, and an
unconditionally little-endian binary runtime artifact — every field,
including vertex/transform floats via `std::bit_cast`, assembled by
explicit shift/mask, never a host-struct `memcpy`), and atomic
(write-to-temp-then-`rename()`) cooker output. Static mesh (a position/
color/UV0 mesh, schema version 2 -- a fixed 32-byte stride at byte
offsets 0/12/24, mandatory UV0 for every vertex, no optional/variant
layout; version 1, the pre-UV0 24-byte layout, is rejected outright by
both the source parser and the artifact decoder, no dual-version reader):
`cookStaticMesh()`, `loadStaticMeshAsset()` returning CPU-only
`StaticMeshAssetData` — never an RHI type; a composition root
outside this module (`tests/image_regression/fixture/`, Atlantis
Runtime) is responsible for passing that data into the existing,
unmodified `atlantis::renderer::createMesh()`. Scene graph (a node
hierarchy with Transform/Camera/Renderable-shaped DTOs, `ValidatedSceneData`
— no public default constructor, constructible only by `decodeScene()`,
the sole entry point, enforced by a friend relationship with no
test-only exception; schema version 2 — the `Renderable` slot's own
`DecodedRenderable` widened with an *optional* material Asset ID
alongside its existing mandatory mesh Asset ID, version 1 rejected
outright, no dual-version reader): `cookScene()`/`decodeScene()`,
independently re-validating every cook-time condition at decode time,
never trusting the cooker. Neither DTO shape ever names an `atlantis::world::` type
(`scene_types.h`) — the one place permitted to convert between them,
`atlantis::world::fromValidatedSceneData()`, lives in `src/world/`, not
here. Texture (a plain sampled color image, `Rgba8Unorm`/`Rgba8Srgb`):
`cookTexture()`, `loadTextureAsset()` returning CPU-only
`TextureAssetData` — never an RHI type; a composition root outside this
module is responsible for constructing the actual `atlantis::rhi::SampledTexture`.
`cookTexture()` takes already-decoded pixel bytes and never calls
`stbi_load()` itself — the PNG decode call, and this module's only
`Stb::Stb` linkage boundary, lives solely in `tools/asset_cooker/`'s own
`runCookTextureMode()`, below. `cookTexture()` normalizes its own
logical path exactly like `cookStaticMesh()`, so every cooked texture
asset has its own unique, normalized logical path and Asset ID — a
post-merge Human Review Correction
([PR #79](https://github.com/slmao/Atlantis/pull/79),
[PR #80](https://github.com/slmao/Atlantis/pull/80)) fixed an initial
implementation that let two color-space variants of the same texture
silently share one Asset ID. Material (a small, versioned DTO naming a
closed `MaterialKind` enum — one enumerator this round,
`UnlitTextured` — a texture Asset ID, and `Sampler` parameters mirroring
`atlantis::rhi::SamplerCreateParams` exactly): `cookMaterial()`,
`loadMaterialAsset()` returning CPU-only `MaterialAssetData` — never an
RHI type or a shader path/identifier; a composition root outside this
module (Atlantis Runtime) is responsible for resolving `MaterialKind` to
a fixed, built-in shader pair and constructing the actual
`atlantis::rhi::Sampler`/`SampledTexture`/`atlantis::renderer::Material`.
The Material artifact embeds no self-Asset-ID (the texture artifact's
own precedent, not the mesh artifact's — self-consistency is fully
covered by the metadata sidecar's own cross-check); reference
validation (the embedded texture Asset ID) is value-level only, never an
existence check — an unresolvable reference surfaces as a Runtime-side
error at scene-load time, not an Asset System error. Implemented per
[specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md),
[plans/0012-asset-system-foundation.md](../plans/0012-asset-system-foundation.md),
[ADR-0043](../adr/0043-asset-system-module-boundary.md)–[ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md);
the scene graph asset type extended per
[specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md),
[plans/0015-scene-asset-serialization-foundation.md](../plans/0015-scene-asset-serialization-foundation.md),
and
[ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)–[ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md);
the texture asset type added per
[specs/0016-texture-sampler-foundation.md](../specs/0016-texture-sampler-foundation.md),
[plans/0016-texture-sampler-foundation.md](../plans/0016-texture-sampler-foundation.md),
and
[ADR-0055](../adr/0055-sampled-texture-and-sampler-rhi-module-boundary-and-ownership.md)–[ADR-0057](../adr/0057-texture-asset-format-decoder-dependency-and-color-space-contract.md);
the static mesh format's UV0 attribute added per
[specs/0017-mesh-uv-attribute-foundation.md](../specs/0017-mesh-uv-attribute-foundation.md),
[plans/0017-mesh-uv-attribute-foundation.md](../plans/0017-mesh-uv-attribute-foundation.md),
and
[ADR-0058](../adr/0058-static-mesh-uv0-vertex-layout-and-sampling-convention.md),
merged via [PR #84](https://github.com/slmao/Atlantis/pull/84); the
Material asset type and the scene graph's own optional material
reference added per
[specs/0018-material-asset-scene-binding-foundation.md](../specs/0018-material-asset-scene-binding-foundation.md),
[plans/0018-material-asset-scene-binding-foundation.md](../plans/0018-material-asset-scene-binding-foundation.md),
and
[ADR-0059](../adr/0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)–[ADR-0060](../adr/0060-scene-material-binding-and-runtime-transactional-resource-publish.md),
merged via [PR #88](https://github.com/slmao/Atlantis/pull/88).

**`world/`** — Atlantis World: Atlantis's in-memory, multi-entity scene.
Target `atlantis_world`, alias `Atlantis::World` (PUBLIC dependency
`Atlantis::Core` and, narrowly, `Atlantis::AssetSystem` — for the
`AssetId` type only, named in `Renderable`'s own two public fields, a
mandatory `meshAsset` and an *optional* `materialAsset` (Spec 0018) — no
RHI, Renderer, RenderGraph, Shader System, Vulkan Backend, Platform,
Runtime, or Tools dependency in either direction, verified by an
include-scanning test). `World` is a slot map: `createEntity()`/`destroyEntity()` return
an index+generation `EntityId` handle, formally overflow-safe via
permanent slot retirement at the generation counter's maximum value;
`EntityId` carries a `private`, non-owning reference to its own `World`
instance's heap-allocated, address-stable identity token (never a
writable public field, never an accessor for it), so a handle used
against a different, live `World` instance is rejected with
`WorldError::WrongWorld`, never silently misapplied to the wrong entity.
Each entity carries a mandatory `Transform` plus two optional
components, `Camera` and `Renderable` — fixed-type storage, not a
generic ECS registry. `setParent()` maintains an atomic parent/child
hierarchy with cycle prevention; `destroyEntity()` cascades to every
transitive descendant via a collect-then-mutate worklist, never a
recursive walk. `updateTransforms()` is a fully iterative (non-
recursive) traversal producing each entity's own world matrix
(`getWorldMatrix()`), composed `parentWorld · T · R · S` with a fixed
`Ry · Rx · Rz` Euler order. `World` itself is move-constructible, not
copyable, not move-assignable; every public accessor returns by value,
never a reference or pointer into `World`'s own internal storage.
`fromValidatedSceneData()` (`scene_instantiation.h`, deliberately not a
`World` member function) is the one place permitted to convert Asset
System's own `ValidatedSceneData` into a real `World` — two-pass,
deterministic, genuinely infallible instantiation; scene-local node
indices are never persisted as `EntityId`. Implemented per
[specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md),
[plans/0014-world-scene-foundation.md](../plans/0014-world-scene-foundation.md),
[ADR-0048](../adr/0048-world-scene-module-boundary-and-ownership.md)–[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md);
`fromValidatedSceneData()` extended per
[specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md)
and
[ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md).

**`tools/asset_cooker/`** — Atlantis Tools' second real content:
`atlantis_asset_cooker`, a CLI invoked at build time by CMake's
`atlantis_add_static_mesh_asset()`/`atlantis_add_scene_asset()`/
`atlantis_add_texture_asset()` (defined in
`src/asset_system/CMakeLists.txt`, mirroring
`atlantis_add_slang_shader_pair()`'s own stamp/`BYPRODUCTS` pattern) to
cook one declared asset, and by `atlantis_finalize_asset_validation()`
to run its own `--validate-set` mode over every declared asset's logical
path — kind-agnostic, so it already covers texture logical paths
exactly as it covers mesh/scene ones. A thin CLI split into a testable
library (`atlantis_asset_cooker_lib`) and a small `main.cpp` doing
`--flag=value` argv parsing — publicly links `Atlantis::AssetSystem` and
`Atlantis::Core`; privately links `Stb::Stb` for its own `--kind=texture`
mode's `stbi_load()` call site (`runCookTextureMode()`, ADR-0041's own
Accepted Amendment), the sole place this binary's own PNG decode
happens. Implemented per the same Spec 0012/Plan 0012/ADR-0043–0045
references above; texture cook mode added per the same Spec 0016/Plan
0016/ADR-0055–0057 references above.

**`tools/shader_compiler/`** — Atlantis Tools' first real content:
`atlantis_shader_compiler`, a CLI invoked at build time by CMake's
`atlantis_add_slang_shader_pair()` (defined in
`src/shader_system/CMakeLists.txt`) to compile, reflect, and
build-time-validate one Slang shader pair, publishing its four artifacts
(vertex/fragment `.spv` and reflection JSON) only after every check
succeeds. Owns a private, Windows-only `CreateProcessW` wrapper
(`process_launch.{h,cpp}`) — never a Platform or Shader System API —
and links neither `Atlantis::RHI` nor the RHI-integration target.
Implemented per the same Spec 0008/Plan 0008/ADR-0028–0031 references
above, merged via [PR #36](https://github.com/slmao/Atlantis/pull/36).

**`runtime/`** — Atlantis Runtime: the real Windows windowed composition
root. Two targets: `atlantis_runtime_host` (STATIC, alias
`Atlantis::RuntimeHost`; PUBLIC dependencies `Atlantis::Core`,
`Atlantis::Platform`, `Atlantis::RHI`, `Atlantis::VulkanBackend`,
`Atlantis::Renderer`, `Atlantis::ShaderSystem`,
`Atlantis::ShaderSystemRhiIntegration`, `Atlantis::AssetSystem`,
`Atlantis::World` — not `Atlantis::RenderGraph` directly, since
`Renderer::drawFrame()` already owns it internally), a private
composition library that exists solely so `tests/runtime/` can exercise
real lifecycle/error-classification logic without a device or a window —
not a dependency any other module may take; and `atlantis_runtime` (the
thin executable, PRIVATE dependency on `Atlantis::RuntimeHost` only).
`RuntimeApplication` owns a `PlatformSession` RAII guard as its first
member (destroyed last by reverse-declaration-order destruction, making
window-outlives-GPU-resources compiler-enforced rather than
hand-sequenced), a five-step initialization sequence (Platform session,
shader load, `Device`, camera `Buffer`, then a real scene asset's own
manifest-driven load: read the scene's dependency manifest, decode its
`ValidatedSceneData`, resolve and load every distinct mesh it
references in ascending first-reference order into a keyed
`meshResourceMap_`, and instantiate the one real `World` instance via
`atlantis::world::fromValidatedSceneData()` — replacing the project's
own former fixed, hardcoded six-entity validation scene. Spec 0018
widened this same CPU-only load step ("Phase 1") to also resolve and
load every distinct material an entity's `Renderable` names (and each
material's own referenced texture, deduplicated by texture Asset ID)
into two further CPU-only maps, published atomically alongside `world_`/
`meshResourceMap_`; no `SampledTexture`/`Sampler`/`Material` GPU
resource is ever constructed here, since no real swapchain format is
known yet. Real GPU material realization ("Phase 2") is deferred to a
new, Runtime-private, independently-testable `material_realization.h`/
`.cpp` module (`computePendingMaterialIds()`, `realizePendingMaterials()`,
`rebuildMaterialsForFormatChange()`), called from the per-frame
orchestration below), a per-frame orchestration (event handling,
acquire, format-change check — now rebuilding a *complete* candidate
batch (fallback `Material` plus every existing per-material entry, each
reusing its own already-uploaded `SampledTexture`/`Sampler` unchanged)
and swapping it in only *after* this frame's own `submit()` returns
`Ok`, never destroying the old bundle while GPU work that could
reference it might still be in flight — extent-change depth-texture
rebuild, `World::updateTransforms()` + Runtime-private camera/asset
extraction (`scene_extraction.h`/`.cpp`, now resolving against both
`meshResourceMap_`'s and the per-frame material-resolution result's own
key sets rather than one hardcoded `AssetId`) + per-entity `DrawItem`
build — falling back to a single hardcoded `Material` only when an
entity's own `materialAsset` is absent, never when it is merely not yet
GPU-realized — + one `submit()` (covering any pending material's own
texture-upload pass, recorded before the draw graph, plus the draw
itself) + present), and a single, idempotent `shutdown()` that is the
sole caller of GPU-resource teardown and the sole indirect trigger (via
`PlatformSession`'s own destructor) of `platform::shutdown()`. `world_`
is `std::optional<World>`, published via in-place move-construction
only once the scene load fully succeeds (`World` is not
move-assignable); `meshResourceMap_` occupies the single former `Mesh`
member's own declaration slot, preserving the existing GPU-resource
reverse-destruction-order guarantee unchanged; the Spec 0018 material/
texture/sampler resource maps are each held as `std::unique_ptr<T>` map
values (address-stable borrows, independent of map rehash/move) in a
declaration order that keeps every `SampledTexture`/`Sampler` a borrowed
`Material` might reference destroyed strictly after that `Material`.
Implemented per
[specs/0013-runtime-host-foundation.md](../specs/0013-runtime-host-foundation.md),
[plans/0013-runtime-host-foundation.md](../plans/0013-runtime-host-foundation.md),
[ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md),
[ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md);
the `World`-driven scene and extraction adapter extended per
[specs/0014-world-scene-foundation.md](../specs/0014-world-scene-foundation.md),
[plans/0014-world-scene-foundation.md](../plans/0014-world-scene-foundation.md),
and
[ADR-0051](../adr/0051-world-to-renderer-extraction-and-asset-resolution-boundary.md);
the manifest-driven real scene asset load implemented per
[specs/0015-scene-asset-serialization-foundation.md](../specs/0015-scene-asset-serialization-foundation.md),
[plans/0015-scene-asset-serialization-foundation.md](../plans/0015-scene-asset-serialization-foundation.md),
and
[ADR-0052](../adr/0052-scene-asset-module-boundary-and-ownership.md)–[ADR-0054](../adr/0054-scene-loading-transactional-instantiation-contract.md);
the Phase 1 CPU-transaction widening, Phase 2 deferred GPU material
realization, and the format-change rebuild's own old-`Pipeline`-in-
flight safety fix implemented per
[specs/0018-material-asset-scene-binding-foundation.md](../specs/0018-material-asset-scene-binding-foundation.md),
[plans/0018-material-asset-scene-binding-foundation.md](../plans/0018-material-asset-scene-binding-foundation.md),
and
[ADR-0059](../adr/0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)–[ADR-0060](../adr/0060-scene-material-binding-and-runtime-transactional-resource-publish.md),
merged via [PR #88](https://github.com/slmao/Atlantis/pull/88).
See
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
for the full public/private boundary statement.
