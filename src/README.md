# src/

**`core/`** — Atlantis Core: logging, assertions, and a minimal
result/error utility type. Implemented per
[specs/0001-project-foundation.md](../specs/0001-project-foundation.md),
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md),
and [ADR-0006](../adr/0006-dependency-management.md)–[ADR-0010](../adr/0010-cmake-structure.md).

Every other module — RHI, Vulkan Backend, RenderGraph, Renderer,
Platform, Shader System, Runtime, Tools (see
[docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md))
— is still empty by design, per Spec-Driven Development (see
[AGENTS.md](../AGENTS.md)): each module's internal structure is itself an
architectural decision established by that module's own approved
spec + plan + ADR, not invented ahead of time.

Do not add source files for those modules here without a linked spec and
plan.
