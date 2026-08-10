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
[ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md).

**`vulkan_backend/`** — Atlantis Vulkan Backend: Phase 1's sole graphics
backend, implementing RHI's interfaces. Target `atlantis_vulkan_backend`,
alias `Atlantis::VulkanBackend`. Implements Vulkan instance/device
construction, Validation Layer enforcement, Windows-only private WSI
surface creation, swapchain ownership and resize-driven recreation, the
concrete acquire/execute/submit/present state machine (a per-swapchain-
image render-finished-semaphore pool, a persistent acquire-complete
semaphore, a single-frame-in-flight command pool/fence), and
`vkCmdClearColorImage`/barrier recording — no general rendering (no
pipeline/shader objects, no `Buffer`/`Texture`) and no GPU memory
allocator. Vulkan and Win32 WSI types stay private to this module's own
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
found only by running on real GPU hardware).

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
`execute()` binds a frame-scoped `RenderTarget` to the graph's declared
resources, runs each pass's execution callback against RHI's
`CommandList`, and inserts automatic dependency-derived resource-state
transitions — RenderGraph never calls `Device::submit()` or
`Presentation::present()` itself. **Not yet implemented:** pass culling,
resource lifetime/aliasing, and the Renderer itself. Construction/
compilation implemented per
[specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md),
[plans/0005-render-graph-foundation.md](../plans/0005-render-graph-foundation.md),
and [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md);
`execute()` implemented per
[specs/0006-rhi-render-graph-frame-execution-foundation.md](../specs/0006-rhi-render-graph-frame-execution-foundation.md),
[plans/0006-rhi-render-graph-frame-execution-foundation.md](../plans/0006-rhi-render-graph-frame-execution-foundation.md),
and [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md).

Every other module — Renderer, Shader System, Runtime, Tools (see
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md))
— is still empty by design, per Spec-Driven Development (see
[AGENTS.md](../AGENTS.md)): each module's internal structure is itself an
architectural decision established by that module's own approved
spec + plan + ADR, not invented ahead of time.

Do not add source files for those modules here without a linked spec and
plan.
