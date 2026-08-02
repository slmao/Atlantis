# CLAUDE.md

This file is Claude Code–specific. The actual rules live in
[AGENTS.md](AGENTS.md) — read that first, every session. This file only
adds notes specific to working in this repo through Claude Code.

## The one rule that matters

**Do not make uncontrolled architectural decisions.** Follow
Spec → Plan → Human Review → Implementation → Verification → PR → Merge as
defined in [AGENTS.md](AGENTS.md). If a task looks like it requires
deciding a module boundary, a public API shape, a dependency, or anything
else architectural, and there is no approved spec/plan/ADR covering it,
stop and say so instead of proceeding. Prefer entering plan mode and
drafting the spec/plan artifacts over jumping to code.

## How Claude Code works in this repo, step by step

1. **Read [AGENTS.md](AGENTS.md) first**, every session, before touching
   any file — it is the canonical rule set; this file only adds notes.
2. **Inspect existing architecture before making changes** —
   [docs/architecture/](docs/architecture/), the relevant module's own
   README under `docs/`, and any prior implementation already in `src/`.
3. **Read the relevant Spec** in [specs/](specs/) for the work at hand.
   Do not start from the task description alone if a spec exists.
4. **Read relevant ADRs** in [adr/](adr/) — both ones the spec names and
   any others that bound the module(s) being touched.
5. **Create an implementation Plan before coding**, in [plans/](plans/)
   using [plans/template.md](plans/template.md), and get it through Human
   Review per AGENTS.md — do not start writing code against an unapproved
   plan.
6. **Do not modify the Spec to make implementation easier.** If the spec
   turns out to be wrong or incomplete, stop and say so; it gets revised
   through its own review, not silently edited mid-implementation.
7. **Do not introduce unrelated refactoring.** Touch what the plan says to
   touch; a bug fix or feature isn't a license to also clean up nearby
   code.
8. **Do not introduce significant architecture without an ADR** — a new
   module boundary, public API shape, dependency, threading model, or
   ownership model needs an ADR before it lands, not after.
9. **Build and test after implementation** — verification is a required
   step, not an assumption. Report failures rather than working around
   them silently.
10. **Review the final diff before reporting completion** — confirm it
    matches the plan, contains no stray/unrelated changes, and that any
    deviation from the plan is called out explicitly rather than left for
    the reviewer to discover.

## Current repository state

`specs/0001-project-foundation.md` is implemented: a minimal C++20/CMake
project exists (`src/core/` — Atlantis Core: logging, assertions, a
result/error type — `examples/foundation_demo/`, `tests/core/`). No RHI,
Renderer, RenderGraph, windowing, Vulkan, or CI pipeline yet — those
remain pending their own specs. There is no `clang-format`/lint config
yet (tracked as open in
[docs/process/ci-strategy.md](docs/process/ci-strategy.md)).

Build/test commands (Windows, MSVC):

```
cmake -S . -B build
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug
```

The unit test framework (Catch2 v3) is fetched automatically via CMake
`FetchContent` on first configure (network access required then only) —
see [ADR-0006](adr/0006-dependency-management.md).

**Platform note:** Atlantis's target platforms are Windows and Android
(primary) and iOS (future, not started) — see [AGENTS.md](AGENTS.md)
Phase 1 constraints. **Linux is not a target platform**; do not add
Linux-specific source, build configuration, CI jobs, or dependencies, and
do not assume a Linux/`gcc`-style toolchain when guessing at commands.
This session runs on Windows, which matches the primary interactive dev
target — do not treat that as coincidental when deciding what a build/run
command should look like once one exists.

## Working conventions in this repo

- Treat `specs/`, `plans/`, and `adr/` as required reading before touching
  anything under `src/` or `tests/` once those gain real content.
- Use the templates in `specs/template.md`, `plans/template.md`, and
  `adr/template.md` — don't freehand new formats for these documents.
- Never push to `main` or merge a PR without explicit human instruction to
  do so, per this session's standing safety rules.
