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
[ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).

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
[PR #30](https://github.com/slmao/Atlantis/pull/30) (fix).

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
[ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md).

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
authoring-source → runtime-artifact pipeline for one asset type (a
static position/colour mesh). Target `atlantis_asset_system`, alias
`Atlantis::AssetSystem` (`Atlantis::Core`-only — no RHI, Renderer,
RenderGraph, Shader System, Vulkan Backend, Platform, or Tools
dependency, verified by an include-scanning test). Provides logical-path
normalization (a pure string algorithm, never
`std::filesystem::path`-based), a 64-bit FNV-1a Asset ID, declared-set
collision/case-conflict validation
(`validateAssetSet()`/`DeclaredAsset`), strict parse/serialize for all
three formats (authoring source, metadata sidecar, and an
unconditionally little-endian binary runtime artifact — every field,
including vertex floats via `std::bit_cast`, assembled by explicit
shift/mask, never a host-struct `memcpy`), a deterministic cooker
(`cookStaticMesh()`) with atomic (write-to-temp-then-`rename()`) output,
and a file-level loader (`loadStaticMeshAsset()`) returning CPU-only
`StaticMeshAssetData` — never an RHI type. A composition root outside
this module (currently `tests/image_regression/fixture/`) is
responsible for passing that data into the existing, unmodified
`atlantis::renderer::createMesh()`. Implemented per
[specs/0012-asset-system-foundation.md](../specs/0012-asset-system-foundation.md),
[plans/0012-asset-system-foundation.md](../plans/0012-asset-system-foundation.md),
and
[ADR-0043](../adr/0043-asset-system-module-boundary.md)–[ADR-0045](../adr/0045-asset-system-data-format-versioning-and-dependency-policy.md).

**`tools/asset_cooker/`** — Atlantis Tools' second real content:
`atlantis_asset_cooker`, a CLI invoked at build time by CMake's
`atlantis_add_static_mesh_asset()` (defined in
`src/asset_system/CMakeLists.txt`, mirroring
`atlantis_add_slang_shader_pair()`'s own stamp/`BYPRODUCTS` pattern) to
cook one declared asset, and by `atlantis_finalize_asset_validation()`
to run its own `--validate-set` mode over every declared asset's logical
path. A thin CLI split into a testable library
(`atlantis_asset_cooker_lib`) and a small `main.cpp` doing
`--flag=value` argv parsing — links `Atlantis::AssetSystem` and
`Atlantis::Core` only. Implemented per the same Spec 0012/Plan
0012/ADR-0043–0045 references above.

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
`Atlantis::ShaderSystemRhiIntegration`, `Atlantis::AssetSystem` — not
`Atlantis::RenderGraph` directly, since `Renderer::drawFrame()` already
owns it internally), a private composition library that exists solely so
`tests/runtime/` can exercise real lifecycle/error-classification logic
without a device or a window — not a dependency any other module may
take; and `atlantis_runtime` (the thin executable, PRIVATE dependency on
`Atlantis::RuntimeHost` only). `RuntimeApplication` owns a
`PlatformSession` RAII guard as its first member (destroyed last by
reverse-declaration-order destruction, making window-outlives-GPU-
resources compiler-enforced rather than hand-sequenced), a six-step
initialization sequence (Platform session, shader load, `Device`, mesh
asset load, `Mesh`, camera `Buffer` — `Material` is deliberately deferred
to the first frame, since no real swapchain format is known before the
first `SurfaceCreated` event), a ten-step per-frame orchestration
(event handling, acquire, format-change Material rebuild, extent-change
depth-texture rebuild, draw, submit, present), and a single, idempotent
`shutdown()` that is the sole caller of GPU-resource teardown and the
sole indirect trigger (via `PlatformSession`'s own destructor) of
`platform::shutdown()`. Implemented per
[specs/0013-runtime-host-foundation.md](../specs/0013-runtime-host-foundation.md),
[plans/0013-runtime-host-foundation.md](../plans/0013-runtime-host-foundation.md),
and
[ADR-0046](../adr/0046-runtime-composition-ownership-and-frame-lifecycle.md),
[ADR-0047](../adr/0047-runtime-host-executable-library-structure-and-test-boundary.md).
See
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
for the full public/private boundary statement.
