# Spec: Atlantis Project Foundation

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human direction;
  human authorship/ownership of this spec is pending confirmation at
  Human Review.
- **Created:** 2026-08-02
- **Revised:** 2026-08-02 — updated from an earlier Linux-based draft to
  Windows, following the project's Windows/Android (primary) + iOS
  (future) target-platform decision; Linux is not a target platform for
  Atlantis (see [AGENTS.md](../AGENTS.md) Phase 1 constraints). This spec
  was still `Draft`, so revised in place rather than superseded.
- **Related Plan(s):** [plans/0001-project-foundation.md](../plans/0001-project-foundation.md)
  (`Approved / Ready for Implementation`).
- **Related ADR(s):** [ADR-0006](../adr/0006-dependency-management.md),
  [ADR-0007](../adr/0007-test-framework.md),
  [ADR-0008](../adr/0008-logging.md), [ADR-0009](../adr/0009-assertion.md),
  and [ADR-0010](../adr/0010-cmake-structure.md) — the five ADRs
  identified in **Architectural Impact** below, all now `Accepted`.

## Summary

This spec establishes the minimal, buildable C++20/CMake foundation for
Atlantis: a `core` library target, a proof executable that links it, a
logging abstraction, an assertion abstraction, a wired-up unit test
framework, and just enough platform-independent core utility code to
exercise all of the above. It contains no Vulkan, no windowing, no
rendering, and no render graph. Its purpose is to give every later spec
(RHI, Vulkan Backend, Renderer, RenderGraph, ...) a working build system
and a small set of foundational conventions to build on, instead of each
inventing its own.

## Motivation / Problem Statement

The repository currently has no code, no build system, and no CI ([per
`docs/process/ci-strategy.md`](../docs/process/ci-strategy.md), CI is
deliberately not written yet — note that document still describes a
Linux platform baseline and has not yet been updated for the Windows/
Android target-platform decision; that is a separate, unresolved
inconsistency this spec does not fix). `src/` and `tests/` are empty placeholders
whose internal structure is explicitly deferred to "the first approved
spec + plan + ADR" (see [src/README.md](../src/README.md),
[AGENTS.md](../AGENTS.md)). Every future spec that touches code —
starting with RHI — implicitly depends on a working build, a way to run
tests, and a way to log/assert, existing first. Without this spec, those
conventions would get decided incidentally inside whichever module spec
happens to be written first, which is exactly the "uncontrolled
architectural decision" failure mode [AGENTS.md](../AGENTS.md)'s Golden
Rule exists to prevent.

## Goals

- A CMake project that configures and builds cleanly on Windows, the
  project's primary development platform (see
  [AGENTS.md](../AGENTS.md) Phase 1 constraints), in both Debug and
  Release configurations.
- A minimal `Atlantis Core` library target containing only what's needed
  to exercise the items below — not a general-purpose utility library.
- A minimal executable target that links `Atlantis Core` and proves the
  library, logging, and assertion abstractions work at runtime.
- A logging abstraction: leveled logging with at least one default sink.
- An assertion abstraction consistent with AGENTS.md's "programmer errors
  are assertions, not error returns" rule.
- A unit test framework wired into the build, with at least one real test
  against core utility code, runnable without a GPU (testing-strategy.md
  layer 1).
- A documented, reproducible Windows development environment (what's
  needed to configure/build/test from a clean checkout).

## Non-Goals

- No Vulkan, no `Vk*` types, no Vulkan SDK dependency.
- No windowing (no GLFW/SDL, no Win32/Android NDK windowing code, no
  Atlantis Platform module, no `Window`, no `Presentation`).
- No Android or iOS build toolchain (NDK cross-compilation, Gradle
  integration, code signing, or any per-platform build config beyond
  Windows). Atlantis's target platforms are Windows and Android
  (primary) and iOS (future) per [AGENTS.md](../AGENTS.md), but this
  foundation spec establishes the Windows build only — extending the
  build to Android/iOS is explicitly future spec scope, not silently
  assumed to fall out of this one.
- No Renderer, no RenderGraph, no Shader System.
- No Atlantis Runtime (the future windowed application entry point) — the
  proof executable this spec introduces is build-system scaffolding, not
  a preview of Runtime.
- No Atlantis Tools.
- No CI pipeline (`.github/workflows/*`) — still blocked on the build-
  system/dependency-management decisions this spec identifies but does
  not itself finalize; see [ci-strategy.md](../docs/process/ci-strategy.md).
- No headless rendering or image regression test infrastructure
  (testing-strategy.md layers 2–3) — nothing to render yet.
- No final resolution of dependency management strategy beyond the one
  concrete need this spec introduces (fetching a unit test framework) —
  see Architectural Impact.
- No public API design for RHI, RenderGraph, Renderer, or any other future
  module.

## Scope

| In scope | Out of scope |
|---|---|
| CMake project, Debug/Release configs | Vulkan, windowing, rendering |
| `Atlantis Core` library (minimal) | RHI, Vulkan Backend, RenderGraph, Renderer, Shader System, Runtime, Tools |
| One proof-of-build executable | Any "real" application entry point |
| Logging abstraction | Any specific third-party logging library (that choice is an ADR) |
| Assertion abstraction | Final Debug/Release assertion semantics (ADR) |
| Unit test framework wiring | Headless/image-regression test infra |
| Windows dev environment docs | CI pipeline (`.github/workflows/*`) |
| | Android/iOS build toolchain (NDK, Gradle, etc.) |
| | Atlantis Platform module (any OS's windowing) |

## Requirements

### Functional

- The CMake project configures and builds from a clean checkout with no
  manual setup steps beyond what the README documents.
- Both Debug and Release configurations build successfully.
- The build produces at least: one library target (`Atlantis Core`) and
  one executable target that links against it and runs.
- The logging abstraction supports multiple severity levels (e.g.
  Trace/Debug/Info/Warn/Error/Fatal — exact set TBD by its own ADR) and
  at least one default output sink (e.g. stdout/stderr).
- The assertion abstraction distinguishes at least "programmer-error,
  fail fast" checks from ordinary control flow, per AGENTS.md's error-
  handling rules; exact macro semantics are TBD by its own ADR.
- The unit test framework is invocable through a single documented
  command (e.g. `ctest`) and discovers/runs at least one real test
  against core utility code.
- Platform-independent core utilities are limited to what the above
  require (e.g. whatever minimal result/error type the logging/assertion
  abstractions need internally) — not a general utility-library land grab.

### Non-functional

- **Performance:** not a concern at this stage; correctness and
  buildability take priority over optimization.
- **Memory:** no specific constraint beyond the RAII/ownership rules
  already stated in [AGENTS.md](../AGENTS.md).
- **Portability (within the Vulkan-only Phase 1 constraint):** must build
  on Windows, the primary development target. Not required to build on
  Android in this spec, but avoid gratuitous Win32-only API usage in
  `Atlantis Core` where a portable standard-library equivalent exists —
  Core is the one module Android's future build will also link against
  (see [AGENTS.md](../AGENTS.md) module boundaries), so needless
  Windows-specific dependencies here are a cost paid twice, not zero.
- **Other:** building requires no network access beyond fetching whatever
  dependency this spec's own ADRs decide are needed (e.g. a test
  framework).

## Build Requirements

- **Language standard:** C++20, no compiler-specific extensions relied
  upon (per AGENTS.md C++ coding conventions).
- **Build system:** CMake (minimum version TBD by the Plan/ADR that picks
  a dependency-management mechanism, since that choice can constrain the
  minimum CMake version).
- **Configurations:** Debug and Release must both be first-class, buildable
  configurations from the same CMake project.
- **Warnings policy:** the new targets build with warnings-as-errors
  enabled (exact flag set is an implementation detail, but the "no new
  warnings" policy from [Definition of Done](../docs/process/definition-of-done.md)
  applies from this spec onward).
- **Platform:** Windows, the project's primary development platform per
  [AGENTS.md](../AGENTS.md) Phase 1 constraints. Compiler/toolchain (MSVC
  vs. Clang-cl vs. other) is not fixed by this spec — see Risks & Open
  Questions.
- **Dependencies:** limited to whatever this spec's Architectural Impact
  ADRs (dependency-management strategy, test framework choice) determine
  is needed — no dependency is added outside that decision.

## Proposed Design (module structure)

This spec is scoped to **Atlantis Core** only, plus the minimal CMake/
executable scaffolding needed to prove it builds, runs, and is testable.
It does not create real RHI/RenderGraph/Renderer/Vulkan Backend/Shader
System/Runtime/Tools modules — those remain empty placeholders (see
[src/README.md](../src/README.md)) until their own specs.

Illustrative layout — **not final**; exact paths/target names are a Plan-
stage decision, not fixed by this spec:

```
CMakeLists.txt        top-level: C++20, Debug/Release configs
cmake/                 minimal helpers this spec's targets need
                        (e.g. a warnings-as-errors helper)
src/
  core/                Atlantis Core library sources: logging,
                       assertion abstraction, minimal utility types
  <proof executable>/  minimal executable linking Atlantis Core
                       (name/location TBD — see Risks & Open Questions)
tests/
  core/                unit tests for Atlantis Core
```

The proof executable exists only to demonstrate the library links and the
logging/assertion abstractions work at runtime — it is not a preview of
Atlantis Runtime and should not accrete windowing, rendering, or
application-loop concerns.

## Architectural Impact

This spec does introduce architecture — it is the first real code in the
repository and fixes conventions every later module inherits. The
following decisions were identified as requiring their own ADR before
this spec could move from `In Review` to `Approved` (per
[AGENTS.md](../AGENTS.md)); **none are decided by this spec itself** —
each was filed as its own ADR (linked below) and is now `Accepted`:

1. **Dependency management strategy** (vcpkg / Conan / CMake
   `FetchContent` / git submodules) — already flagged open in
   [ci-strategy.md](../docs/process/ci-strategy.md); this spec is the
   first to actually need it, at minimum to pull in a test framework.
   Filed as [ADR-0006](../adr/0006-dependency-management.md)
   (`Accepted`).
2. **Unit test framework choice** (Catch2 / GoogleTest / custom) —
   already flagged open in
   [testing-strategy.md](../docs/process/testing-strategy.md); this spec
   is what first requires the decision. Filed as
   [ADR-0007](../adr/0007-test-framework.md) (`Accepted`).
3. **Logging abstraction design** — interface shape, sink model, and
   whether it wraps a third-party library (e.g. spdlog) or is hand-rolled.
   Filed as [ADR-0008](../adr/0008-logging.md) (`Accepted`).
4. **Assertion abstraction design** — macro semantics and Debug vs.
   Release behavior, and how it embodies AGENTS.md's "programmer errors
   are assertions" rule concretely. Filed as
   [ADR-0009](../adr/0009-assertion.md) (`Accepted`).
5. **CMake target/library structure convention** — how `src/` divides
   into CMake targets going forward; this spec's `core` target sets the
   precedent every later module (RHI, RenderGraph, ...) will follow.
   Filed as [ADR-0010](../adr/0010-cmake-structure.md) (`Accepted`).

## Alternatives Considered

- **Skip a dedicated foundation spec; let the first module spec (e.g.
  RHI) establish build/test/logging conventions incidentally.** Rejected:
  it would leave these conventions owned by no single reviewable
  decision, and every later spec would inherit undocumented precedent
  rather than a deliberate one.
- **Decide the dependency-manager/test-framework/logging/assertion
  choices directly in this spec, to save a review cycle.** Rejected per
  AGENTS.md: each is exactly the kind of significant decision that
  requires its own ADR, not a spec-embedded footnote.

## Testing & Verification Plan

- **Unit tests:** at least one meaningful test of core utility code,
  executed through the chosen test framework, runnable without a GPU
  (testing-strategy.md layer 1).
- **Headless integration tests:** not applicable — no rendering exists.
- **Image regression tests:** not applicable.
- **Vulkan Validation Layers:** not applicable — no Vulkan in this spec.
- **Manual verification:** clean build (Debug and Release) from a clean
  checkout on Windows; the proof executable runs and produces expected
  log output at more than one severity level; the test command reports
  the new test(s) passing.

## Acceptance Criteria

These criteria are considered locked once this spec is `Approved` — per
this task's explicit instruction, they are not to be modified afterward
to fit whatever implementation turns out to be convenient. A deviation
discovered during implementation is a signal to revisit the spec through
review, not to quietly edit this list.

- [ ] `cmake` configures successfully from a clean Windows checkout, with
      no manual setup beyond what the README documents, for both a Debug
      and a Release configuration.
- [ ] The build produces at least one library target (`Atlantis Core`)
      and one executable target that links against it.
- [ ] The executable runs and produces log output via the logging
      abstraction at more than one severity level.
- [ ] At least one assertion-abstraction check exists in the codebase,
      with Debug/Release behavior matching whatever ADR-4 (assertion
      design) decides.
- [ ] The unit test framework is invocable via a single documented
      command and discovers/runs at least one real passing test against
      core utility code.
- [ ] The new targets build with zero compiler warnings under whatever
      warnings-as-errors configuration is adopted.
- [ ] No Vulkan, windowing library (GLFW/SDL/Win32/Android NDK), Atlantis
      Platform module code, Renderer, or RenderGraph code or dependency
      is introduced anywhere in this work.
- [ ] No Linux-specific source code, build configuration, CI job, or
      runtime dependency is introduced (Linux is not a target platform —
      see [AGENTS.md](../AGENTS.md)), and no Android/iOS build toolchain
      (NDK, Gradle, etc.) is introduced either — this spec is Windows-only.
- [ ] All five ADRs listed in Architectural Impact reach `Accepted`
      before this spec is marked `Approved`.

## Risks & Open Questions

- Exact assertion macro semantics (does a failed assertion abort in
  Release, or only Debug, or differ by assertion class?) — not decided
  here; requires ADR-4.
- Whether the proof executable belongs under `src/` alongside `core`, or
  in a separate directory (e.g. `examples/`) — left to the Plan stage.
- Whether this spec's dependency-management decision (ADR-1) also settles
  the CI dependency-management question already flagged in
  [ci-strategy.md](../docs/process/ci-strategy.md) — note that document
  still describes a Linux CI baseline and is itself pending an update for
  the Windows/Android target-platform decision — or only covers this
  spec's narrower Windows need, should be decided explicitly when ADR-1
  is written, not assumed either way by this spec.
- Whether the dependency-management (ADR-1) and CMake-structure (ADR-5)
  decisions this spec requires should anticipate future Android NDK
  cross-compilation from the same CMake project, or whether that's left
  entirely to Android's own future build-support spec — not decided here;
  flagged so ADR-1/ADR-5 don't paint into a corner by accident.
- Compiler/toolchain on Windows (MSVC vs. Clang-cl vs. other) is not
  fixed by this spec — left to the Plan stage or its own ADR if it turns
  out to have architectural weight (e.g. if a dependency only builds with
  one toolchain).
- The naming convention AGENTS.md currently states as a "proposed
  default... confirm before the first real module lands" — this spec's
  `Atlantis Core` is that first real module. Confirming or revising that
  convention is surfaced here as an open question this spec does not
  itself resolve.
- Whether `cmake/` helper modules in scope here should anticipate the
  dependency-management ADR's needs, or be added only after that ADR
  lands — proposed default is the latter (avoid building CMake plumbing
  for a decision that isn't made yet), but this is not fixed by this spec.

## Out of Scope / Future Work

Vulkan, windowing (including the Atlantis Platform module), Renderer,
RenderGraph, Shader System, Runtime, Tools, CI pipeline, headless/image-
regression test infrastructure, Android/iOS build toolchain support, and
any dependency-management or platform decisions beyond this spec's narrow
Windows-only needs are all future spec scope, per
[AGENTS.md](../AGENTS.md) Phase 1 constraints and this document's
Non-Goals above.
