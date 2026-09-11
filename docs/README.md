# docs/

This directory holds every kind of project documentation, kept in
distinct, purpose-specific subdirectories. Don't mix content across them.

- **[specs/](specs/)** — proposed work, pre-implementation: problem,
  requirements, proposed design, what's out of scope. See
  [specs/README.md](specs/README.md) for the status registry.
- **[plans/](plans/)** — an approved spec turned into an ordered,
  reviewable set of changes.
- **[adr/](adr/)** — the permanent record of *why* an architectural
  decision was made. Accepted ADRs are not rewritten; a changed decision
  gets a new, superseding ADR.
- **[architecture/](architecture/)** — the *as-built* record of Atlantis's
  design. A doc appears here once a spec describing it has been approved
  and implemented. This is descriptive, not aspirational.
- **[process/](process/)** — how the project runs: git workflow, Definition
  of Done, testing strategy, CI strategy. This is prescriptive and applies
  regardless of what's been built yet.
- **[project-blueprint.md](project-blueprint.md)** — a roadmap/status
  navigation index across all of the above; not itself an authority (see
  its own header note).

See [AGENTS.md](../AGENTS.md) for the full Spec → Plan → Human Review →
Implementation workflow these directories serve.
