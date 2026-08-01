# Atlantis

Atlantis is a long-term, real-time rendering engine project. It is currently
in its **engineering-foundation stage**: no rendering code exists yet. This
repository holds the process and structure the project will be built with.

## Phase 1 technical scope

- C++20
- CMake
- Vulkan as the only graphics backend
- A backend-independent RHI (Render Hardware Interface)
- A Render Graph as the central rendering abstraction
- Windowed rendering first — the initial working milestone is an
  interactive, on-screen swapchain path
- Headless rendering after that — built once the windowed path works,
  and what unlocks image regression testing
- Image regression testing (depends on headless rendering landing)
- Vulkan Validation Layers as a correctness gate
- RenderDoc-based debugging
- Linux for development and CI

## Planned future phases (not started, not designed yet)

- GPU-driven rendering
- Neural rendering / neural shading
- 3D Gaussian Splatting
- World-model–related workloads

These are listed here to communicate direction, not to authorize starting
them. Each will require its own spec before any design work begins.

## Spec-Driven Development

Atlantis is built with a strict, enforced workflow so that architecture is
always a deliberate, reviewed decision — including (especially) when the
work is done by an AI agent:

```
Spec  →  Plan  →  Implementation  →  Verification  →  PR
```

| Stage | Lives in | Purpose |
|---|---|---|
| Spec | [specs/](specs/) | What problem, what requirements, what design, what's out of scope |
| Plan | [plans/](plans/) | How an approved spec becomes an ordered, reviewable set of changes |
| ADR | [adr/](adr/) | Permanent record of any architectural decision and why it was made |
| Implementation | `src/`, `tests/` | Code written strictly against the approved plan |
| Verification | PR | Checked against the plan's verification checklist and the [Definition of Done](docs/process/definition-of-done.md) |

See [AGENTS.md](AGENTS.md) for the full rules that govern this — including
for AI agents working in this repo — and
[docs/process/git-workflow.md](docs/process/git-workflow.md) for how this
maps onto branches and PRs.

## Repository layout

```
AGENTS.md          Canonical agent operating rules (read this first)
CLAUDE.md           Claude Code–specific pointer to AGENTS.md
README.md           This file
docs/               Architecture records (as-built) and process docs
specs/              Proposed work, pre-implementation
plans/              Approved implementation plans
adr/                Architectural decision records
src/                Source (empty — structure pending first spec/plan/ADR)
tests/              Tests (empty — structure pending first spec/plan/ADR)
.github/            PR template and repository automation
```

## Status

Foundation only. No renderer, no build system, no CI pipeline yet. See
[docs/](docs/) for what's documented so far and the open architectural
questions still awaiting human decisions.

## License

Not yet chosen. Do not assume a license until this section is updated.
