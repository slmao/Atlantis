# Spec: Atlantis Project Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code at explicit human direction; the original
  author metadata left human authorship/ownership confirmation pending.
- **Created:** 2026-08-02
- **Revised:** 2026-08-02 — retargeted from an earlier Linux-based draft to
  Windows per the Windows/Android (primary) + iOS (future) platform decision
  ([AGENTS.md](../../AGENTS.md) Phase 1 constraints). Revised in place because the
  Spec was still `Draft`.
- **Related Plan(s):** [Plan 0001](../plans/0001-project-foundation.md) (`Approved`).
- **Related ADR(s):** [ADR-0006](../adr/0006-dependency-management.md),
  [ADR-0007](../adr/0007-test-framework.md), [ADR-0008](../adr/0008-logging.md),
  [ADR-0009](../adr/0009-assertion.md), [ADR-0010](../adr/0010-cmake-structure.md)
  — the five decisions in **Architectural Impact**, all `Accepted`.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #147](https://github.com/slmao/Atlantis/pull/147) Batch 1. Original scope
  and obligations retained.

## Summary

Establishes the minimal buildable C++20/CMake foundation for Atlantis: an
`Atlantis Core` library target, a proof executable that links it, a logging
abstraction, an assertion abstraction, a wired-up unit test framework, and just
enough platform-independent utility code to exercise all of the above. No
Vulkan, windowing, rendering, or render graph. Its purpose is to give every
later Spec a working build system and a small set of foundational conventions
instead of each inventing its own.

## Motivation / Problem Statement

The repository has no code, no build system, and no CI (CI is deliberately
deferred — [ci-strategy.md](../process/ci-strategy.md)). `src/` and
`tests/` are placeholders whose internal structure is deferred to "the first
approved spec + plan + ADR" ([src/README.md](../../src/README.md),
[AGENTS.md](../../AGENTS.md)). Every future Spec that touches code depends on a
working build, a way to run tests, and a way to log/assert existing first.
Without this Spec those conventions would be decided incidentally inside
whichever module Spec is written first — the uncontrolled-architectural-decision
failure mode [AGENTS.md](../../AGENTS.md)'s Golden Rule exists to prevent.

## Goals

- A CMake project that configures and builds cleanly on Windows (the primary
  development platform, [AGENTS.md](../../AGENTS.md) Phase 1) in Debug and Release.
- A minimal `Atlantis Core` library target — only what the items below need,
  not a general-purpose utility library.
- A minimal executable that links `Atlantis Core` and proves the library,
  logging, and assertion abstractions work at runtime.
- A logging abstraction: leveled logging with at least one default sink.
- An assertion abstraction consistent with AGENTS.md's "programmer errors are
  assertions, not error returns" rule.
- A unit test framework wired into the build, with at least one real test
  against core utility code, runnable without a GPU (testing-strategy.md
  layer 1).
- A documented, reproducible Windows development environment (configure/build/
  test from a clean checkout).

## Non-Goals

- No Vulkan, `Vk*` types, or Vulkan SDK dependency.
- No windowing (GLFW/SDL, Win32/Android NDK windowing, Atlantis Platform
  module, `Window`, `Presentation`).
- No Android or iOS build toolchain (NDK cross-compilation, Gradle, signing, or
  any per-platform build config beyond Windows). Extending the build to
  Android/iOS is explicitly future Spec scope, not assumed to fall out of this
  one.
- No Renderer, RenderGraph, or Shader System.
- No Atlantis Runtime — the proof executable is build-system scaffolding, not a
  preview of Runtime.
- No Atlantis Tools.
- No CI pipeline — still blocked on the build-system/dependency decisions this
  Spec identifies but does not finalize ([ci-strategy.md](../process/ci-strategy.md)).
- No headless rendering or image regression infrastructure (testing-strategy.md
  layers 2–3) — nothing to render yet.
- No resolution of dependency-management strategy beyond the one concrete need
  this Spec introduces (fetching a unit test framework) — see Architectural
  Impact.
- No public API design for RHI, RenderGraph, Renderer, or any future module.

## Scope

| In scope | Out of scope |
|---|---|
| CMake project, Debug/Release configs | Vulkan, windowing, rendering |
| `Atlantis Core` library (minimal) | RHI, Vulkan Backend, RenderGraph, Renderer, Shader System, Runtime, Tools |
| One proof-of-build executable | Any "real" application entry point |
| Logging abstraction | A specific third-party logging library (an ADR) |
| Assertion abstraction | Final Debug/Release assertion semantics (an ADR) |
| Unit test framework wiring | Headless/image-regression test infra |
| Windows dev environment docs | CI pipeline (`.github/workflows/*`) |
| | Android/iOS build toolchain (NDK, Gradle) |
| | Atlantis Platform module (any OS's windowing) |

## Requirements

### Functional

- The CMake project configures and builds from a clean checkout with no manual
  setup beyond what the README documents.
- Both Debug and Release configurations build successfully.
- The build produces at least one library target (`Atlantis Core`) and one
  executable target that links against it and runs.
- The logging abstraction supports multiple severity levels (exact set TBD by
  [ADR-0008](../adr/0008-logging.md)) and at least one default output sink.
- The assertion abstraction distinguishes "programmer-error, fail fast" checks
  from ordinary control flow per AGENTS.md; exact macro semantics TBD by
  [ADR-0009](../adr/0009-assertion.md).
- The unit test framework is invocable through a single documented command and
  discovers/runs at least one real test against core utility code.
- Platform-independent core utilities are limited to what the above require —
  not a general utility-library land grab.

### Non-functional

- **Performance / Memory:** not a concern at this stage beyond the RAII/
  ownership rules in [AGENTS.md](../../AGENTS.md); correctness and buildability
  take priority.
- **Portability:** must build on Windows. Not required to build on Android
  here, but avoid gratuitous Win32-only API usage in `Atlantis Core` where a
  portable standard-library equivalent exists — Core is the one module
  Android's future build also links against.
- **Other:** building requires no network access beyond fetching whatever this
  Spec's own ADRs decide is needed.

## Build Requirements

- **Language standard:** C++20, no compiler-specific extensions.
- **Build system:** CMake, minimum version TBD by the dependency-management ADR.
- **Configurations:** Debug and Release both first-class from the same project.
- **Warnings policy:** new targets build warnings-as-errors; the "no new
  warnings" policy from [Definition of Done](../process/definition-of-done.md)
  applies from this Spec onward.
- **Platform:** Windows. Compiler/toolchain (MSVC / Clang-cl / other) is not
  fixed here — see Risks & Open Questions.
- **Dependencies:** limited to what the Architectural Impact ADRs determine is
  needed — nothing added outside that decision.

## Proposed Design (module structure)

Scoped to **Atlantis Core** only, plus the minimal CMake/executable scaffolding
to prove it builds, runs, and is testable. Real RHI/RenderGraph/Renderer/Vulkan
Backend/Shader System/Runtime/Tools modules remain empty placeholders
([src/README.md](../../src/README.md)) until their own Specs.

Illustrative layout — **not final**; exact paths/target names are a Plan-stage
decision:

```
CMakeLists.txt        top-level: C++20, Debug/Release configs
cmake/                minimal helpers this Spec's targets need
src/
  core/               Atlantis Core: logging, assertion abstraction,
                      minimal utility types
  <proof executable>/ minimal executable linking Atlantis Core
tests/
  core/               unit tests for Atlantis Core
```

The proof executable exists only to demonstrate the library links and the
logging/assertion abstractions work at runtime — it is not a preview of Runtime
and must not accrete windowing, rendering, or application-loop concerns.

## Architectural Impact

This Spec introduces architecture — it is the first real code in the repository
and fixes conventions every later module inherits. Five decisions were each
required to be `Accepted` before this Spec could be `Approved`; **none is
decided by this Spec** — each was filed as its own ADR and is now `Accepted`:

| Decision | ADR |
|---|---|
| Dependency management strategy (vcpkg / Conan / `FetchContent` / submodules) | [ADR-0006](../adr/0006-dependency-management.md) |
| Unit test framework choice | [ADR-0007](../adr/0007-test-framework.md) |
| Logging abstraction design (interface, sink model, third-party or hand-rolled) | [ADR-0008](../adr/0008-logging.md) |
| Assertion abstraction design (macro semantics, Debug vs. Release) | [ADR-0009](../adr/0009-assertion.md) |
| CMake target/library structure convention | [ADR-0010](../adr/0010-cmake-structure.md) |

## Alternatives Considered

- **Skip a dedicated foundation Spec; let the first module Spec establish
  build/test/logging conventions incidentally.** Rejected: the conventions
  would be owned by no single reviewable decision.
- **Decide the dependency-manager / test-framework / logging / assertion
  choices directly in this Spec.** Rejected per AGENTS.md: each is a
  significant decision requiring its own ADR.

## Testing & Verification Plan

- **Unit tests:** at least one meaningful test of core utility code, run
  through the chosen framework, no GPU required (testing-strategy.md layer 1).
- **Headless / image regression / Vulkan Validation:** not applicable — nothing
  rendered, no Vulkan.
- **Manual verification:** clean Debug and Release build from a clean Windows
  checkout; the proof executable runs and logs at more than one severity level;
  the test command reports the new test(s) passing.

## Acceptance Criteria

Original obligations remain unmarked; this editorial revision does not certify
historical execution.

- [ ] `cmake` configures from a clean Windows checkout with no manual setup
      beyond the README, for both Debug and Release.
- [ ] The build produces at least one library target (`Atlantis Core`) and one
      executable target that links against it.
- [ ] The executable runs and logs via the logging abstraction at more than one
      severity level.
- [ ] At least one assertion-abstraction check exists, with Debug/Release
      behavior matching [ADR-0009](../adr/0009-assertion.md).
- [ ] The unit test framework is invocable via a single documented command and
      discovers/runs at least one real passing test against core utility code.
- [ ] New targets build with zero compiler warnings under the adopted
      warnings-as-errors configuration.
- [ ] No Vulkan, windowing library, Atlantis Platform code, Renderer, or
      RenderGraph code or dependency is introduced.
- [ ] No Linux-specific source, build config, CI job, or dependency, and no
      Android/iOS build toolchain, is introduced — this Spec is Windows-only.
- [ ] All five ADRs in Architectural Impact reach `Accepted` before this Spec
      is `Approved`.

## Risks & Open Questions

- Exact assertion macro semantics (abort in Release, only Debug, or by
  assertion class) — deferred to [ADR-0009](../adr/0009-assertion.md).
- Whether the proof executable belongs under `src/` or a separate directory
  (e.g. `examples/`) — left to the Plan.
- Whether the dependency-management decision also settles the CI
  dependency-management question in [ci-strategy.md](../process/ci-strategy.md)
  (which still describes a Linux CI baseline), or only this Spec's narrower
  Windows need — to be decided explicitly when the ADR is written.
- Whether the dependency-management and CMake-structure decisions should
  anticipate future Android NDK cross-compilation from the same CMake project.
- Compiler/toolchain on Windows (MSVC / Clang-cl / other) — left to the Plan or
  its own ADR if it turns out to carry architectural weight.
- Confirming or revising the AGENTS.md naming convention (stated there as a
  "proposed default... confirm before the first real module lands") —
  `Atlantis Core` is that first module.
- Whether `cmake/` helper modules should anticipate the dependency-management
  ADR's needs or be added only after it lands (proposed default: the latter).

## Out of Scope / Future Work

Vulkan, windowing (including the Atlantis Platform module), Renderer,
RenderGraph, Shader System, Runtime, Tools, CI pipeline, headless/image-
regression infrastructure, Android/iOS build toolchain, and any
dependency-management or platform decisions beyond this Spec's narrow
Windows-only needs are all future Spec scope, per [AGENTS.md](../../AGENTS.md)
Phase 1 constraints and the Non-Goals above.
