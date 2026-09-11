# Architecture (as-built)

> **Bootstrap exception (2026-08-02), now resolved:** `docs/rhi/`,
> `docs/render_graph/`, `docs/renderer/` originally contained `PROPOSED`
> architecture-baseline content, written directly there ahead of an
> approved spec, at explicit human direction after being told this
> contradicted the as-built-only policy below. Every module those three
> directories described is now implemented, with its real as-built
> design in this directory (chiefly
> [module_boundaries.md](module_boundaries.md)) — those three directories
> are kept only as short redirect stubs, so existing links into them
> keep resolving; they no longer carry `PROPOSED` content.
>
> **See also:** [engine_architecture.md](engine_architecture.md) is a
> separate, still-standing documented exception to the as-built-only
> policy below — an architecture overview/navigation document that
> intentionally combines as-built content with `Accepted`-but-not-yet-
> implemented long-term direction (see its own status banner), added per
> [Spec 0009](../specs/0009-long-term-engine-architecture-alignment.md)/[Plan 0009](../plans/0009-long-term-engine-architecture-alignment.md)'s
> Human Review Approval. It does not change the policy for any other
> file in this directory.

## How this directory gets populated

Once a spec in [specs/](../specs/) is approved, planned, implemented,
and merged, a corresponding doc is added here describing the system *as it
actually was built* — not the proposal, the result. If the implementation
deviated from the spec, this doc reflects reality and links back to the
spec/ADR that explains why.

## Anticipated topics (not yet written, not yet designed)

These are named here only to communicate expected direction from the
project's initial scope — none of them are decided:

- Render Hardware Interface (RHI)
- Render Graph
- Headless rendering
- Vulkan backend
- Debugging workflow (Vulkan Validation Layers, RenderDoc)
- Future-phase roadmap (GPU-driven rendering, neural rendering/shading,
  3D Gaussian Splatting, world-model workloads)

Each becomes a real document only after its own spec → plan → ADR →
implementation cycle.
