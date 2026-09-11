# Plan: Atlantis Project Foundation

- **Spec:** [Spec 0001](../specs/0001-project-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code at explicit human direction.
- **Plan approval:** 2026-08-02, in chat. ADR-0006–0010, Spec 0001, and this
  Plan were all moved to `Accepted`/`Approved` on 2026-08-02; the informal "1–5"
  labels in Spec 0001's Architectural Impact map in order to `adr/0006`–`0010`.
- **Editorial revision:** [Spec 0033](../specs/0033-documentation-lifecycle-and-compaction.md);
  [PR #147](https://github.com/slmao/Atlantis/pull/147) Batch 1. Original scope,
  ordered work, and verification retained.

## Objective

Turn [Spec 0001](../specs/0001-project-foundation.md) into an ordered,
reviewable set of concrete changes: a buildable C++20/CMake project with an
`Atlantis Core` library, a proof executable, a logging abstraction, an
assertion abstraction, and a wired-up unit test framework, on Windows, with no
Vulkan, windowing, rendering, or RenderGraph content.

## 1. Files / Directory Structure

```
.gitignore                              build/ output, IDE files
CMakeLists.txt                          root
cmake/CompilerWarnings.cmake            warnings-as-errors helper

src/core/
  CMakeLists.txt
  include/atlantis/log.h  assert.h  result.h   public headers
  src/log.cpp  assert.cpp                        private implementation

examples/
  README.md                             placeholder, new top-level dir per ADR-0010
  foundation_demo/CMakeLists.txt  main.cpp

tests/core/
  CMakeLists.txt
  result_tests.cpp  log_tests.cpp  assert_tests.cpp
```

Per [ADR-0010](../adr/0010-cmake-structure.md), the proof executable lives at
`examples/foundation_demo/` — a new top-level directory, sibling to `src/` and
`tests/` — not `src/foundation_demo/`. `src/` is reserved for shipping engine
modules. No shader, asset, or tool files (Spec Non-Goals).

## 2. Files to Modify

- **`README.md`** — add a "Building" section with real build/run/test commands,
  and an `examples/` entry in the repository layout list.
- **`CLAUDE.md`** — replace the "Do not assume build/test/lint commands exist"
  placeholder with the real commands.
- **`src/README.md`**, **`tests/README.md`** — update both to point at this
  Spec/Plan instead of reading as blanket prohibitions.

No `docs/architecture/*` changes — this Plan adds no new module, boundary, or
subsystem.

## 3. CMake Target Structure

Per [ADR-0010](../adr/0010-cmake-structure.md):

- Root `CMakeLists.txt`: C++20 (`CMAKE_CXX_STANDARD 20`, required, no
  extensions); options `ATLANTIS_BUILD_TESTS` / `ATLANTIS_BUILD_EXAMPLES`
  (default `ON`); `add_subdirectory` for the `cmake/` helper, `src/core`,
  `examples/foundation_demo`, and `tests/core` under their guards.
- `atlantis_core` — static library, `include/` as its public include
  directory, consumed via the `Atlantis::Core` alias.
- `atlantis_foundation_demo` — executable linking `Atlantis::Core`.
- `atlantis_core_tests` — executable linking `Atlantis::Core` and the test
  framework, registered with CTest.
- `cmake/CompilerWarnings.cmake` defines an `INTERFACE` target applying the
  MSVC warnings-as-errors flags, linked `PRIVATE` by every first-party target,
  **not** the fetched test-framework target.

## 4. Library Dependencies

Per [ADR-0006](../adr/0006-dependency-management.md) and
[ADR-0007](../adr/0007-test-framework.md):

- **`atlantis_core`**: no third-party dependencies. Standard library only,
  including C++20 `std::format` for log-message formatting (avoids adding a
  formatting library) — requires a high-enough MSVC/STL version (see Open
  Items).
- **`atlantis_foundation_demo`**: `Atlantis::Core` only.
- **`atlantis_core_tests`**: `Atlantis::Core` plus **Catch2 v3**, fetched via
  CMake `FetchContent` pinned to a tagged release, declared only in
  `tests/core/CMakeLists.txt` — never a dependency of `atlantis_core` or the
  demo.
- No system package manager (vcpkg/Conan). The Vulkan SDK and Android NDK/SDK
  are out of scope (external system dependencies needed only by future Specs).

## 5. Unit Test Structure

- One test binary for Phase 1: `atlantis_core_tests`, registered with CTest via
  Catch2's `catch_discover_tests()` so each `TEST_CASE` is an
  individually-reportable CTest test.
- One source file per unit, mirroring the headers:
  - **`result_tests.cpp`**: success/error construction and access for the
    minimal result/error type; at least one test per state.
  - **`log_tests.cpp`**: level filtering and message formatting, via a
    test-only `LogSink` that captures messages into a vector instead of writing
    to stdout.
  - **`assert_tests.cpp`**: `ATLANTIS_CHECK`'s pass/fail logic without crashing
    the binary, via the injectable failure handler
    ([ADR-0009](../adr/0009-assertion.md)) that records a failure instead of
    terminating; `ATLANTIS_ASSERT` (Debug-only) tested only in Debug builds.
- Run via `ctest` from the build directory (primary), or by invoking
  `atlantis_core_tests` directly (IDE debugging).

## 6. Logging Design

Per [ADR-0008](../adr/0008-logging.md): types in the bare `atlantis` namespace
(per [ADR-0010](../adr/0010-cmake-structure.md)) in `<atlantis/log.h>`;
`enum class LogLevel { Trace, Debug, Info, Warn, Error, Fatal }`; an abstract
`LogSink` with a default `ConsoleLogSink` (Warn/Error/Fatal to stderr, the rest
to stdout); global access (`atlantis::log::initialize(sink)` /
`instance()`) as a narrow, AGENTS.md-permitted exception to the
no-global-mutable-singletons rule for diagnostics infrastructure; call-site
macros `ATLANTIS_LOG_TRACE`..`_FATAL` using `std::format` and
`std::source_location`; a runtime-configurable minimum level checked *before*
formatting (proposed default `Trace` in Debug, `Info` in Release); thread-safe
sink dispatch (internal mutex — an implementation detail of this authorized
module, not a threading architecture decision), synchronous per ADR-0008.

## 7. Assertion Design

Per [ADR-0009](../adr/0009-assertion.md): two macros —
`ATLANTIS_ASSERT(cond)` / `_MSG` (**Debug only**, `cond` not evaluated in
Release) and `ATLANTIS_CHECK(cond)` / `_MSG` (**always evaluated**). On failure
either logs via `ATLANTIS_LOG_FATAL` with stringified condition and source
location, then invokes a replaceable failure handler defaulting to
"log, then `std::abort()`" (with a debugger breakpoint in Debug); no exception.
The handler is swappable
(`atlantis::assertions::setFailureHandler(...)`) so `assert_tests.cpp` can
record failures instead of terminating. Android routing (through an
Android-specific `LogSink` to `adb logcat`) is documented by ADR-0009, not
implemented here.

## 8. Build Configurations

- **Debug:** no optimization, debug symbols, `ATLANTIS_ASSERT` active, default
  log level `Trace`, `NDEBUG` not defined.
- **Release:** optimizations on, `NDEBUG` defined, `ATLANTIS_ASSERT` compiled
  out, `ATLANTIS_CHECK` still active, default log level `Info`.
- **Warnings-as-errors** applies to both configurations, every first-party
  target.
- **Generator:** not fixed by this Plan (Visual Studio and Ninja both expected
  to work); the README documents at least one concrete command line. Not
  covered by any ADR and not requiring one.

## 9. Implementation Order

1. Root `CMakeLists.txt` + `.gitignore`; confirm an empty project configures
   and "builds" in Debug and Release.
2. `cmake/CompilerWarnings.cmake`; confirm the warnings flags behave on a
   throwaway target.
3. Wire `FetchContent` + Catch2 v3 in `tests/core/CMakeLists.txt`; confirm an
   empty Catch2 target builds and `ctest` reports zero tests.
4. `atlantis_core` library skeleton (empty TU, public include directory wired)
   — confirm it builds and links.
5. `atlantis/result.h` + `result_tests.cpp`.
6. `atlantis/assert.h` + `assert.cpp` + `assert_tests.cpp`.
7. `atlantis/log.h` + `log.cpp` + `log_tests.cpp`.
8. `examples/foundation_demo/`: links `Atlantis::Core`, logs at multiple
   severities, exercises at least one `ATLANTIS_CHECK` path.
9. Full `ctest` run; confirm all tests pass and are individually discovered.
10. Update `README.md` (+ `examples/README.md`), `CLAUDE.md`, `src/README.md`,
    `tests/README.md`.
11. Full [Definition of Done](../process/definition-of-done.md) pass
    before PR: zero warnings, no dead/commented-out code, docs updated.

All five ADRs steps 3/6/7 depend on are `Accepted`; the remaining gate is
procedural (Spec `Approved`, this Plan through Human Review).

## 10. Verification Steps

Maps to the Spec's Testing & Verification Plan:

- Configure and build, Debug and Release, from a clean Windows checkout, no
  manual setup beyond the updated README.
- `ctest` from the build directory: all tests pass, individually discovered.
- Run `atlantis_foundation_demo` directly: log output at more than one severity
  level, no crash.
- Build output inspected for zero compiler warnings on every first-party
  target.
- Manual repo scan confirming no Vulkan/`Vk*`, no windowing library, no
  Atlantis Platform code, no Linux- or Android/iOS-toolchain content.
- Vulkan Validation Layers and image regression: **N/A**.

## 11. Acceptance Criteria Mapping

| Spec Acceptance Criterion | Plan step(s) | Verified by |
|---|---|---|
| `cmake` configures on a clean Windows checkout, Debug + Release | 1–2 | clean-checkout build |
| Build produces `Atlantis Core` library + linked executable | 4, 8 | build output inspection |
| Executable runs, logs at >1 severity level | 7–8 | run demo executable |
| ≥1 assertion check exists, Debug/Release behavior matches ADR-0009 | 6 | manual code check |
| Test framework invocable via one command, ≥1 real passing test | 3, 5–7, 9 | `ctest` run |
| New targets build with zero warnings | 2 onward | build output inspection |
| No Vulkan/windowing/Renderer/RenderGraph/Platform code or dependency | all (scope discipline) | manual repo scan |
| No Linux- or Android/iOS-toolchain content | all (scope discipline) | manual repo scan |
| All five spec-identified ADRs `Accepted` before spec `Approved` | satisfied 2026-08-02 | Human Review complete |

## Sequencing & Dependencies

Steps 1–2 have no blockers. Steps 3, 6, 7 depended on ADR-0006/0007, 0009,
0008 respectively — all `Accepted`. Steps 4–5 depend only on step 1
(`result_tests.cpp` needs step 3's harness to run). Step 8 depends on 6–7.
Steps 9–11 depend on everything above. This governance-free foundation has no
ordering relationship with any other engine Spec/Plan.

## Rollback Plan

Purely additive: no existing functionality, data, or running system is touched.
Reverting the implementing PR removes the new files and restores `src/`,
`tests/`, `README.md`, and `CLAUDE.md` to their placeholder state — a standard
revert, no migration.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas:

- "Vulkan Validation Layers run clean" and "Image regression tests" are not
  applicable (Spec Non-Goals).
- All five ADRs are `Accepted`, the Spec is `Approved`, and this Plan is
  `Approved / Ready for Implementation` — done 2026-08-02.

## Open Items (not architectural)

- Whether `FetchContent`'s adequacy for this Spec's narrow need also answers
  heavier dependency questions later Specs raise (e.g. Vulkan SDK integration)
  — explicitly left open by [ADR-0006](../adr/0006-dependency-management.md).
- Android log-sink and assertion-failure routing
  ([ADR-0008](../adr/0008-logging.md)/[ADR-0009](../adr/0009-assertion.md)) are
  documented intent, not implemented or verified — no Android build support
  exists yet.
