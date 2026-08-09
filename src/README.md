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

**`rhi/`** — Atlantis RHI: backend-independent, non-frame RHI interfaces.
Target `atlantis_rhi`, alias `Atlantis::RHI`. Currently provides `Device`
(an opaque logical-GPU handle) and `Presentation` (construction, resize
notification, conditional swapchain recreation, and a read-only metadata
query only — no acquire, no present, no `RenderTarget`, no command
recording, and no synchronization object), plus the supporting value
types `Extent2D`, `Format`, `SwapchainMetadata`, and `PresentationError`.
RHI's public headers reference no Vulkan or Platform type. Implemented
per [specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md),
[plans/0003-rhi-vulkan-windowed-foundation.md](../plans/0003-rhi-vulkan-windowed-foundation.md),
and [ADR-0001](../adr/0001-rhi-backend-independence.md),
[ADR-0002](../adr/0002-presentation-rendertarget-unification.md),
[ADR-0003](../adr/0003-resource-rendertarget-ownership-model.md),
[ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md).

**`vulkan_backend/`** — Atlantis Vulkan Backend: Phase 1's sole graphics
backend, implementing RHI's interfaces. Target `atlantis_vulkan_backend`,
alias `Atlantis::VulkanBackend`. Currently implements Vulkan instance/
device construction, Validation Layer enforcement, Windows-only private
WSI surface creation, swapchain ownership and resize-driven recreation,
and read-only swapchain metadata — no acquire/present, no swapchain
image vending, no command buffers, no rendering, and no GPU memory
allocator. Vulkan and Win32 WSI types stay private to this module's own
implementation files; Windows is currently the only implemented WSI path
(Android is not implemented). Implemented per
[specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md),
[plans/0003-rhi-vulkan-windowed-foundation.md](../plans/0003-rhi-vulkan-windowed-foundation.md),
and [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md),
[ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md),
[ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md).

**`render_graph/`** — Atlantis RenderGraph: GPU-independent render graph
construction and compilation. Target `atlantis_render_graph`, alias
`Atlantis::RenderGraph`; PUBLIC dependency is `Atlantis::Core` only.
`RenderGraphBuilder` declares passes/logical resources and their
read/write usage (single-producer-per-resource model); `compile()`
derives producer→reader dependency edges, a deterministic
declaration-order-tie-break pass order, and either a `CompiledGraph` or
a `CompileError` (`MultipleProducersError`/`DependencyCycleError`,
with a deterministic cycle witness). `CompiledGraph` is independently
owned and move-only — it outlives, and never borrows from, the builder
that produced it. **Not yet implemented:** RHI resource binding, command
recording, GPU execution, barriers/synchronization, pass culling,
resource lifetime/aliasing, and the Renderer itself. Implemented per
[specs/0005-render-graph-foundation.md](../specs/0005-render-graph-foundation.md),
[plans/0005-render-graph-foundation.md](../plans/0005-render-graph-foundation.md),
and [ADR-0017](../adr/0017-render-graph-construction-compile-layering.md),
[ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md).

Every other module — Renderer, Shader System, Runtime, Tools (see
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md))
— is still empty by design, per Spec-Driven Development (see
[AGENTS.md](../AGENTS.md)): each module's internal structure is itself an
architectural decision established by that module's own approved
spec + plan + ADR, not invented ahead of time.

Do not add source files for those modules here without a linked spec and
plan.
