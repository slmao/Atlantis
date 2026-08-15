# ADR 0036: Agent-Native Automation and Machine-Verifiable Architecture as Long-Term Goals

- **Status:** Proposed
- **Date:** 2026-08-15
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0009-long-term-engine-architecture-alignment.md](../specs/0009-long-term-engine-architecture-alignment.md)

## Context

Atlantis is already, in practice, developed largely through AI coding
agents operating under AGENTS.md's own Spec → Plan → Human Review →
Implementation → Verification → PR → Merge process — this is the
existing, lived working style of this repository, not a hypothetical.
AGENTS.md itself is explicitly written as "the canonical, tool-agnostic
operating manual for any AI agent... working in this repository." No CI
pipeline exists yet ([docs/process/ci-strategy.md](../docs/process/ci-strategy.md)),
no architecture-boundary test exists, and no structured/machine-readable
diagnostic format is defined anywhere in this repository today.

A human-provided external architecture draft dedicates substantial
content to "Coding Agent Friendly Architecture": CLI-first tooling,
machine-readable module manifests, machine-verifiable dependency rules
enforced in CI, structured (not free-text) diagnostics, an "Agent API
Catalog," and an explicit principle that agent integration should not be
bound to a specific AI vendor or product.

None of this is implemented in Atlantis today, and most of it
(module manifests, a CLI, structured diagnostics, dependency-rule
enforcement) depends on infrastructure — a build/CI strategy, a CLI tool,
a schema/reflection mechanism — that does not exist yet and is not
specced. Adopting the external draft's concrete tooling wholesale now
would mean designing a CLI, a manifest format, and a CI enforcement
mechanism with no real consumer to validate any of it against — the same
premature-commitment risk this whole Spec exists to avoid elsewhere.
But Atlantis's own working reality (already agent-developed, already
governed by a written operating manual) makes "should this even be a
long-term goal" a different, easier question than "should we build the
tooling now."

## Decision

**Atlantis adopts, as a long-term direction (not a Phase 1 requirement
and not something implemented by this ADR), the goal that important
development capabilities — build, test, run, diagnose, inspect
architecture — should be reachable through a CLI/API path, not only
through a GUI, and that architectural constraints should move toward
being machine-checkable over time rather than remaining permanently
Markdown-only.** This direction is explicitly **not vendor-bound** — it
is a property Atlantis's own repository, build, and (eventually) runtime
should have regardless of which AI coding agent, if any, a given
contributor uses.

- This ADR does not create a CLI, a module-manifest format, a
  machine-readable dependency-rule schema, a CI job, or a structured-
  diagnostic format. All of those remain Requirements for whichever
  future Spec actually builds them (a future build/CI Spec per
  `docs/process/ci-strategy.md`'s own Open Questions, or a future
  Tools-module Spec).
- This ADR does not change any Phase 1 constraint. In particular, it
  does not require the current `AGENTS.md`-driven Spec/Plan/Human-Review
  process to change, does not introduce any new required tool, and does
  not make any existing manual step (e.g. the still-manual Windows/
  Vulkan-Validation-Layers verification this repository already relies
  on) newly non-compliant.
- Where a future Spec proposing a *concrete* mechanism (a CLI command
  set, a manifest schema, a CI-enforced dependency rule, a structured
  diagnostic format) is drafted, it evaluates that mechanism on its own
  engineering merits for Atlantis's actual needs — this ADR states a
  direction to weigh future proposals against, it does not pre-approve
  any specific one, including any of the external draft's own concrete
  examples (`engine-cli`, a JSON module manifest, an "Agent API
  Catalog").
- "Agent-native" here is scoped to **development-time tooling and
  process** (build, test, diagnostics, architecture verification) — it
  is explicitly not a claim about Atlantis's own runtime feature set
  (e.g. a future in-game AI agent API, `Neural Rendering`, or a
  `Research API`), which remain future-workload candidates governed by
  AGENTS.md's existing "must not shape Phase 1 abstractions" rule,
  unaffected by this ADR.

## Consequences

### Positive

- Gives a future build/CI Spec and a future Tools-module Spec a stated
  direction to design toward, without this ADR itself having to guess at
  a CLI surface, manifest format, or CI mechanism no one has built yet.
- Names, as an explicit non-goal, the risk of vendor lock-in to a
  specific AI coding tool or product — protecting Atlantis's own
  AGENTS.md-based, tool-agnostic working style (which already predates
  and does not depend on any single agent product) from being
  accidentally narrowed by a future Spec that only considers one vendor's
  integration surface.
- Consistent with, and does not change, Atlantis's own existing
  Definition-of-Done and testing-strategy documents, which already value
  automatable, reproducible verification over manual-only steps.

### Negative / Trade-offs

- A direction with no concrete implementation yet risks staying purely
  aspirational if no future Spec ever picks it up — this ADR does not,
  by itself, schedule or prioritize any of the future work it points
  toward.
- "Machine-verifiable architecture" is a genuinely large, open-ended
  space (dependency-boundary tests, structured diagnostics, module
  manifests, an API catalog are each their own real engineering effort);
  stating it as one ADR risks understating how much future-Spec work
  each piece actually represents — mitigated by this ADR explicitly
  deferring every concrete mechanism to its own future Spec rather than
  bundling them here.

## Alternatives Considered

- **Adopt the external draft's concrete tooling now** (a `engine-cli`
  command set, a TOML/JSON module manifest per package, a machine-
  readable dependency-rule enforcement mechanism, a JSON "Agent API
  Catalog"). Rejected for this round: no CLI, no CI, and no
  package/module-manifest concept exist in this repository yet — Spec
  0009's own governance boundary explicitly defers the full CLI command
  set, module-manifest format, and structured-diagnostic schema fields to
  future Specs.
- **Treat this purely as Spec vision prose, not an ADR.** Considered: the
  vendor-neutrality commitment in particular is a real decision (it rules
  out a future Spec designing a CLI/tooling surface that only works with
  one specific AI product) that future Specs should be bound by, not
  merely inspired by — closer to what this repository's own ADR
  convention is for.
- **Reject the whole "agent-native" framing as out of scope for a
  rendering-engine-first project.** Considered and not adopted: Atlantis
  is already, in practice, developed primarily through AI coding agents
  under a written, tool-agnostic operating manual (AGENTS.md) — this is
  existing reality, not a speculative future workload, so recording a
  direction for it to grow into is not the same category of premature
  commitment as, say, designing a Neural Rendering API ahead of any
  renderer needing one.
