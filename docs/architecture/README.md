# Architecture (as-built)

This directory is intentionally empty of design content right now.

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
