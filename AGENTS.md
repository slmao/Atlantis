# AGENTS.md — Operating Rules for AI Agents in Atlantis

This file is the canonical, tool-agnostic operating manual for any AI agent
(Claude, Codex, Copilot Workspace, or otherwise) working in this repository.
Tool-specific files (e.g. `CLAUDE.md`) exist only to point back here and add
tool-specific notes — they must never restate or fork these rules.

If you are an AI agent and you have not read this file yet this session,
read it before touching any file.

## The Golden Rule

**AI agents must not make uncontrolled architectural decisions.**

Atlantis is a long-term rendering engine. Its value depends on the coherence
of its architecture over years, not the speed of any single change. An agent
that quietly picks an abstraction, a threading model, a dependency, or a
module boundary — even a "reasonable" one — can lock in a decision nobody
reviewed. That is the single failure mode this document exists to prevent.

Every significant piece of work follows one path, no exceptions:

```
Spec  →  Plan  →  Implementation  →  Verification  →  PR
```

## What counts as "significant"

Requires the full Spec → Plan → Implementation → Verification → PR path:

- Anything that introduces or changes a public API, module boundary, or
  subsystem (RHI, render graph, memory model, threading model, etc.)
- Anything that adds a new dependency (library, tool, SDK)
- Anything that changes the build system, CI, or repository structure
- Any new rendering feature, backend behavior, or data format
- Any change to process/governance docs in this repository

Does **not** require a spec (still needs a PR and normal review):

- Typo fixes, comment fixes, formatting
- Fixing an already-agreed-upon bug with no design implication
- Editing an existing spec/plan/ADR draft before it is approved

When in doubt, treat it as significant. Escalate to the human instead of
guessing.

## The workflow, stage by stage

1. **Spec** (`specs/`) — Problem, goals/non-goals, requirements, proposed
   design, and an explicit **Architectural Impact** section. If the spec
   touches architecture, it must name the ADR that will be written. A spec
   is not implementation-ready until a human has approved it. Use
   [specs/template.md](specs/template.md).

2. **Plan** (`plans/`) — Turns an approved spec into an ordered, reviewable
   task breakdown: files/modules touched, sequencing, and a verification
   checklist that maps back to the spec. A plan is not implementation-ready
   until a human has approved it. Use [plans/template.md](plans/template.md).

3. **ADR** (`adr/`) — Any architectural decision identified by a spec gets
   its own ADR: context, decision, consequences, alternatives considered.
   ADRs are the permanent record of *why*; specs and plans may be
   superseded, ADRs are not silently rewritten. Use
   [adr/template.md](adr/template.md).

4. **Implementation** — Code written strictly against the approved plan.
   If reality forces a deviation from the plan, stop and call it out
   explicitly in the PR rather than silently drifting — a deviation that
   changes architecture means back to step 1.

5. **Verification** — Executed against the plan's verification checklist
   and [docs/process/testing-strategy.md](docs/process/testing-strategy.md).
   Vulkan Validation Layers must run clean; see
   [docs/process/definition-of-done.md](docs/process/definition-of-done.md).

6. **PR** — Opened using the repository's
   [PR template](.github/PULL_REQUEST_TEMPLATE.md), linking the spec, plan,
   and any ADRs. See [docs/process/git-workflow.md](docs/process/git-workflow.md).

An agent never merges its own PR to `main` and never pushes directly to
`main`. A human approves and merges.

## Phase 1 constraints (do not silently expand these)

- Language: C++20
- Build: CMake
- Graphics backend: **Vulkan only** — do not scaffold for other backends
  behind the RHI "for later"; the RHI must stay backend-independent in
  interface, but no second backend gets implemented in Phase 1
- Render Graph is the central rendering abstraction — do not bypass it
  with ad hoc direct-submission code paths
- Sequencing: **windowed rendering ships first.** Headless rendering and
  image regression testing follow once the windowed/swapchain path works —
  they are still Phase 1 scope, not deferred to a future phase, but they
  are not the first milestone. Don't block the windowed path on headless
  infrastructure, and don't skip headless once windowed is working — see
  [docs/process/testing-strategy.md](docs/process/testing-strategy.md).
- Development target: Linux. CI is build-verification only until headless
  rendering lands; see [docs/process/ci-strategy.md](docs/process/ci-strategy.md).
- GPU-driven rendering, neural rendering/shading, 3D Gaussian Splatting,
  and world-model workloads are **future phases**. Do not start
  implementing them, and do not let them shape Phase 1 abstractions beyond
  what an approved spec explicitly calls for. If you see a clean
  opportunity to "future-proof" for one of these, write it up as a spec
  question instead of coding it in.

## Repository map

- [README.md](README.md) — project overview
- [docs/](docs/) — architecture records (as-built) and process docs
- [specs/](specs/) — proposed work, pre-implementation
- [plans/](plans/) — approved implementation plans
- [adr/](adr/) — architectural decision records
- `src/`, `tests/` — currently empty placeholders; their internal structure
  is itself an architectural decision and will be established by the first
  approved spec + plan + ADR, not invented ahead of time

## Definition of Done

See [docs/process/definition-of-done.md](docs/process/definition-of-done.md).
No PR is complete until every applicable item is checked.

## When you're unsure

Stop and ask the human. Escalating a question costs a message. Guessing
wrong on architecture costs months.
