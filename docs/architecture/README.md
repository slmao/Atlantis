# Architecture (as-built)

> **Bootstrap exception (2026-08-02):** this directory (and the new
> `docs/rhi/`, `docs/render_graph/`, `docs/renderer/` directories) now
> contain `PROPOSED` architecture-baseline content, written directly here
> ahead of an approved spec, at explicit human direction after being told
> this contradicts the as-built-only policy below. Every such file is
> headed with a `PROPOSED — pending spec/ADR approval, not as-built`
> banner. This is a one-time, human-directed exception, not a standing
> change to the policy — whether to formally amend this README's policy
> (e.g. to allow a `Proposed` status here going forward) is itself an open
> question for human review; until decided, treat the policy below as
> still in force for anything not already marked `PROPOSED`.
>
> **See also:** [engine_architecture.md](engine_architecture.md) is a
> second, narrower documented exception to the as-built-only policy
> below — an architecture overview/navigation document that
> intentionally combines as-built content with `Accepted`-but-not-yet-
> implemented long-term direction (see its own status banner), added per
> [Spec 0009](../../specs/0009-long-term-engine-architecture-alignment.md)/[Plan 0009](../../plans/0009-long-term-engine-architecture-alignment.md)'s
> Human Review Approval. It does not change the policy for any other
> file in this directory.

This directory is otherwise intentionally empty of design content right
now.

Atlantis has no implemented architecture yet — no RHI, no render graph, no
headless rendering path. Writing design docs for these ahead of an approved
spec would itself be the kind of uncontrolled architectural decision this
project's process exists to prevent (see [AGENTS.md](../../AGENTS.md)).

## How this directory gets populated

Once a spec in [specs/](../../specs/) is approved, planned, implemented,
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
