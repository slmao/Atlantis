# RenderGraph

> **Status: PROPOSED — pending spec/ADR approval. Not as-built.** This
> directory is new (a repository-structure addition), created directly at
> explicit human direction as part of the same bootstrap exception as
> [docs/architecture/](../architecture/overview.md). See the status note in
> [docs/architecture/README.md](../architecture/README.md). No code exists.

## Scope

Atlantis RenderGraph is the central rendering abstraction. Per
[AGENTS.md](../../AGENTS.md), Phase 1 does not allow an ad hoc
direct-submission rendering path that bypasses it. See
[docs/architecture/module_boundaries.md](../architecture/module_boundaries.md#atlantis-rendergraph)
for its place in the module map.

## Anticipated responsibilities (not yet designed)

- Pass declaration: a unit of GPU work and the RHI resources it reads/
  writes.
- Resource dependency tracking across passes within a frame.
- Automatic barrier synthesis and resource-lifetime resolution, derived
  from the dependency graph rather than authored by hand per pass.
- Execution-order (topological) resolution of a frame's passes.

## Dependencies

Built on RHI + Core only. Never references Vulkan Backend, GLFW/SDL, or
Runtime directly — see
[docs/architecture/overview.md](../architecture/overview.md#dependency-direction).

## Relationship to Renderer

Renderer builds a graph description each frame and asks RenderGraph to
compile/execute it against RHI. RenderGraph has no knowledge of what a
frame "means" (scene, materials) — that stays in Renderer.

## Explicitly out of scope for Phase 1

GPU-driven / data-dependent scheduling is a named future phase (see
[AGENTS.md](../../AGENTS.md)) and must not shape this module's Phase 1
design beyond what an approved spec calls for.

## Related

- [ADR-0002: Presentation/RenderTarget Unification](../../adr/0002-presentation-rendertarget-unification.md)
