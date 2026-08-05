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

Every other module — RHI, Vulkan Backend, RenderGraph, Renderer, Shader
System, Runtime, Tools (see
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md))
— is still empty by design, per Spec-Driven Development (see
[AGENTS.md](../AGENTS.md)): each module's internal structure is itself an
architectural decision established by that module's own approved
spec + plan + ADR, not invented ahead of time.

Do not add source files for those modules here without a linked spec and
plan.
