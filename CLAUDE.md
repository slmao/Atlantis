# CLAUDE.md

This file is Claude Code–specific. The actual rules live in
[AGENTS.md](AGENTS.md) — read that first, every session. This file only
adds notes specific to working in this repo through Claude Code.

## The one rule that matters

**Do not make uncontrolled architectural decisions.** Follow
Spec → Plan → Implementation → Verification → PR as defined in
[AGENTS.md](AGENTS.md). If a task looks like it requires deciding a module
boundary, a public API shape, a dependency, or anything else architectural,
and there is no approved spec/plan/ADR covering it, stop and say so instead
of proceeding. Prefer entering plan mode and drafting the spec/plan
artifacts over jumping to code.

## Current repository state

This repository is at the engineering-foundation stage: governance docs,
directory scaffolding, and process templates only. There is no build
system, no source code, and no CI pipeline yet — those are themselves
architectural decisions pending specs (see
[docs/process/ci-strategy.md](docs/process/ci-strategy.md) for why CI
config is deliberately not written yet). Do not assume build/test/lint
commands exist; if none are documented here, ask rather than guessing at
`cmake`/`ninja`/`ctest` invocations.

When a build system lands, the commands to build/test/lint this project
will be documented in this section.

## Working conventions in this repo

- Treat `specs/`, `plans/`, and `adr/` as required reading before touching
  anything under `src/` or `tests/` once those gain real content.
- Use the templates in `specs/template.md`, `plans/template.md`, and
  `adr/template.md` — don't freehand new formats for these documents.
- Never push to `main` or merge a PR without explicit human instruction to
  do so, per this session's standing safety rules.
