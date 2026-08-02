# Plan: Atlantis Project Foundation

- **Spec:** [specs/0001-project-foundation.md](../specs/0001-project-foundation.md) (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved by the human reviewer in chat on 2026-08-02.

> **Approval transition complete, 2026-08-02:** ADR-0006 through
> ADR-0010 are now `Accepted` (Status field flipped on disk, decisions/
> rationale unchanged), `specs/0001-project-foundation.md` is now
> `Approved`, and this plan is now `Approved / Ready for Implementation`.
> This supersedes the plan's prior revision note, which flagged the ADR
> files as still literally reading `Status: Proposed` — that gap is now
> closed.
>
> **ADR numbering note (resolved):** the spec's Architectural Impact
> section informally labeled its five required decisions "1" through "5."
> They were in fact filed as `adr/0006`–`0010`, in the same order this
> plan always listed them in — confirmed correct, no discrepancy.

## Objective

Turn `specs/0001-project-foundation.md` into an ordered, reviewable set of
concrete changes: a buildable C++20/CMake project with an `Atlantis Core`
library, a proof executable, a logging abstraction, an assertion
abstraction, and a wired-up unit test framework, on Windows, with no
Vulkan, windowing, rendering, or RenderGraph content.

## 1. Files to Create

```
.gitignore                              (build/ output, IDE files — new)
CMakeLists.txt                          (root)
cmake/CompilerWarnings.cmake            (warnings-as-errors helper)

src/core/CMakeLists.txt
src/core/include/atlantis/log.h
src/core/include/atlantis/assert.h
src/core/include/atlantis/result.h
src/core/src/log.cpp
src/core/src/assert.cpp

examples/README.md                      (placeholder, matching src/ and
                                          tests/ — new top-level directory
                                          per ADR-0010)
examples/foundation_demo/CMakeLists.txt
examples/foundation_demo/main.cpp

tests/core/CMakeLists.txt
tests/core/result_tests.cpp
tests/core/log_tests.cpp
tests/core/assert_tests.cpp
```

No shader, asset, or tool files — out of scope per the spec's Non-Goals.

## 2. Files to Modify

- **`README.md`** — add a "Building" section once build/run/test commands
  are real (currently says none exist yet; CLAUDE.md's "Current
  repository state" section also needs its placeholder note replaced with
  real commands, per that file's own "when a build system lands" note).
  Also add an `examples/` entry to the repository layout list, alongside
  `src/`, `tests/`, `shaders/`, `assets/`, `tools/`, `cmake/` — a new
  top-level directory per ADR-0010, not yet reflected there.
- **`CLAUDE.md`** — same: replace the "Do not assume build/test/lint
  commands exist" placeholder with the real commands once they exist.
- **`src/README.md`** and **`tests/README.md`** — both currently say "do
  not add files here without a linked spec and plan." That condition is
  now met; update both to point at this spec/plan instead of continuing
  to read as blanket prohibitions once implementation lands.

No `docs/architecture/*` changes — this plan adds no new module, boundary,
or subsystem; `Atlantis Core`'s existence and scope are already
established in `docs/architecture/module_boundaries.md`.

## 3. Directory Structure

```
CMakeLists.txt
cmake/
  CompilerWarnings.cmake
src/
  core/
    CMakeLists.txt
    include/atlantis/         (public headers — installed/consumed surface)
      log.h
      assert.h
      result.h
    src/                       (private implementation)
      log.cpp
      assert.cpp
examples/
  README.md
  foundation_demo/             (proof executable; per ADR-0010, kept out
                                 of src/ specifically so it cannot be
                                 mistaken for a shipping engine module)
    CMakeLists.txt
    main.cpp
tests/
  core/
    CMakeLists.txt
    result_tests.cpp
    log_tests.cpp
    assert_tests.cpp
```

Per **ADR-0010 (Accepted)**, the proof executable lives at
`examples/foundation_demo/` — a new top-level directory, sibling to
`src/` and `tests/` — not `src/foundation_demo/` as this plan originally
sketched. `src/` is reserved for shipping engine modules; once it holds
`src/rhi/`, `src/renderer/`, and similar real modules, a smoke-test
program sitting alongside them risks being mistaken for one.

## 4. CMake Target Structure

*(Decided — see [ADR-0010](../adr/0010-cmake-structure.md), Accepted.)*

- Root `CMakeLists.txt`: `cmake_minimum_required(VERSION 3.21)`;
  `project(Atlantis LANGUAGES CXX)`; sets `CMAKE_CXX_STANDARD 20`,
  `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`; options
  `ATLANTIS_BUILD_TESTS` (default `ON`) and `ATLANTIS_BUILD_EXAMPLES`
  (default `ON`); `add_subdirectory` for `cmake/`-provided helpers,
  `src/core`, `examples/foundation_demo` (guarded by
  `ATLANTIS_BUILD_EXAMPLES`), and (guarded by `ATLANTIS_BUILD_TESTS`)
  `tests/core`.
- `atlantis_core` — library target (static, for Phase 1 simplicity — no
  module currently needs dynamic linking) in `src/core/CMakeLists.txt`,
  exposing `include/` as its public include directory. Consumed via a
  namespaced alias, `Atlantis::Core`, per ADR-0010.
- `atlantis_foundation_demo` — executable target in
  `examples/foundation_demo/CMakeLists.txt`, linking `Atlantis::Core`.
- `atlantis_core_tests` — executable target in `tests/core/CMakeLists.txt`,
  linking `Atlantis::Core` and the test framework, registered with CTest.
- `cmake/CompilerWarnings.cmake` defines an `INTERFACE` target
  (`atlantis_compiler_warnings`) applying `/W4 /WX` (MSVC); linked
  `PRIVATE` by every first-party target above. **Not** applied to the
  fetched test-framework target, to avoid failing the build on
  third-party warnings.

## 5. Library Dependencies

*(Decided — see [ADR-0006](../adr/0006-dependency-management.md) and
[ADR-0007](../adr/0007-test-framework.md), both Accepted.)*

- **`atlantis_core`**: no third-party dependencies. Standard library only,
  including `std::format` (C++20) for log-message formatting — this
  avoids adding a formatting library (e.g. `fmt`) as a Phase 1 dependency.
  Note: requires a high-enough MSVC/STL version for full `std::format`
  support; see Open Questions.
- **`atlantis_foundation_demo`**: `Atlantis::Core` only.
- **`atlantis_core_tests`**: `Atlantis::Core` plus **Catch2 v3**, fetched
  via **CMake `FetchContent`** (`FetchContent_Declare` +
  `FetchContent_MakeAvailable`, pinned to a specific tagged release, not
  a floating branch). The test framework is declared only in
  `tests/core/CMakeLists.txt` — it must never become a dependency of
  `atlantis_core` or the demo executable.
- No dependency needs a system package manager (vcpkg/Conan) for this
  spec's narrow scope, per ADR-0006. The Vulkan SDK and Android NDK/SDK
  are explicitly out of scope for this plan (external system/toolchain
  dependencies per ADR-0006, needed only by future specs).

## 6. Unit Test Structure

- One test binary for Phase 1: `atlantis_core_tests`, registered with
  CTest via Catch2's `catch_discover_tests()` CMake integration, so each
  `TEST_CASE` shows up as an individually-reportable CTest test (not one
  opaque pass/fail for the whole binary).
- One source file per unit under test, mirroring the headers:
  `result_tests.cpp`, `log_tests.cpp`, `assert_tests.cpp`.
- **`result_tests.cpp`**: success/error construction and access for the
  minimal result/error utility type; at least one test exercising each
  state.
- **`log_tests.cpp`**: exercises level filtering and message formatting
  by installing a test-only `LogSink` implementation that captures
  messages into a vector instead of writing to stdout, so assertions can
  check content/level without depending on process output.
- **`assert_tests.cpp`**: exercises `ATLANTIS_CHECK`'s pass/fail *logic*
  without crashing the test binary — via the injectable failure handler
  defined by ADR-0009, which tests substitute for the default
  log-and-abort handler, recording that a failure was reported instead of
  terminating the process. `ATLANTIS_ASSERT` (Debug-only, compiled out in
  Release) is tested only in Debug test builds.
- Run via `ctest` from the build directory (primary, documented command)
  or by invoking `atlantis_core_tests` directly (for IDE debugging).

## 7. Logging Design

*(Decided — see [ADR-0008](../adr/0008-logging.md), Accepted.)*

- **Namespace/include:** `atlantis::LogLevel`, `atlantis::log(...)`, etc.,
  in `<atlantis/log.h>` — Core's own types live directly in the `atlantis`
  root namespace, not a nested `atlantis::core`, per **ADR-0010's
  namespace decision** (Accepted), which resolves AGENTS.md's previously
  open "confirm naming convention before the first real module lands"
  item.
- **Levels:** `enum class LogLevel { Trace, Debug, Info, Warn, Error,
  Fatal };`.
- **Sink abstraction:**
  ```cpp
  class LogSink {
  public:
    virtual ~LogSink() = default;
    virtual void write(LogLevel level, std::string_view message) = 0;
  };
  ```
  Default implementation: `ConsoleLogSink`, writing `Warn`/`Error`/`Fatal`
  to stderr and everything below to stdout.
- **Global access, deliberately:** `atlantis::log::initialize(sink)` /
  `atlantis::log::instance()` — a narrow, intentional exception to the
  "no global mutable singletons" rule, explicitly permitted by AGENTS.md's
  Ownership & Lifetime rules for logging/diagnostics infrastructure.
- **Call-site macros:** `ATLANTIS_LOG_TRACE(fmt, ...)` through
  `ATLANTIS_LOG_FATAL(fmt, ...)`, expanding to a call that uses
  `std::format` for message formatting and `std::source_location` to
  capture file/line without extra boilerplate at call sites.
- **Level filtering:** a runtime-configurable minimum level, checked
  *before* formatting (so a filtered-out call pays no formatting cost).
  Proposed default minimum: `Trace` in Debug builds, `Info` in Release.
- **Thread-safety:** safe for concurrent calls from multiple threads (an
  internal mutex around sink dispatch) — an implementation detail of this
  already-authorized module, not a threading architecture decision; the
  Phase 1 single-frame-thread baseline in
  [threading.md](../docs/architecture/threading.md) governs the render
  path, not diagnostic infrastructure that may reasonably be called from
  anywhere. Logging remains synchronous per ADR-0008 — thread-safe is not
  the same as asynchronous.

## 8. Assertion Design

*(Decided — see [ADR-0009](../adr/0009-assertion.md), Accepted.)*

- **Two macros, matching AGENTS.md's "programmer errors are assertions"
  rule while acknowledging some checks must never silently compile away:**
  - `ATLANTIS_ASSERT(cond)` / `ATLANTIS_ASSERT_MSG(cond, msg)` — **Debug
    only.** Compiled to nothing in Release; `cond` is not evaluated in
    Release builds, so assert conditions must be free of required side
    effects (standard `assert`-style discipline).
  - `ATLANTIS_CHECK(cond)` / `ATLANTIS_CHECK_MSG(cond, msg)` — **always
    evaluated, in both Debug and Release.** For invariants that must never
    be silently skipped even in a shipping build.
- **On failure (either macro):** log via `ATLANTIS_LOG_FATAL` with the
  stringified condition, file/line (`std::source_location`), and optional
  message; then invoke a **failure handler** — a replaceable
  `std::function<void(const AssertFailureInfo&)>`, defaulting to
  "log, then `std::abort()`" (with a debugger-present breakpoint via
  platform intrinsics in Debug, before aborting, as a nice-to-have). No
  exception is thrown, per ADR-0009.
- **Testability:** the failure handler is swappable
  (`atlantis::assertions::setFailureHandler(...)`) specifically so
  `assert_tests.cpp` can substitute a handler that records the failure
  instead of terminating the process — this is what makes "test the
  check-firing logic" possible without crashing the test binary.
- **Android behavior** (documented by ADR-0009, not implemented by this
  plan — no Android build support exists yet per the spec's Non-Goals):
  the default handler is expected to route through an Android-specific
  `LogSink` (per ADR-0008) reaching `adb logcat`, once Android build
  support lands.

## 9. Build Configurations

- **Debug:** no optimization, debug symbols, `ATLANTIS_ASSERT` active,
  default log level `Trace`, `NDEBUG` not defined.
- **Release:** optimizations on, `NDEBUG` defined, `ATLANTIS_ASSERT`
  compiled out, `ATLANTIS_CHECK` still active, default log level `Info`.
- **Warnings-as-errors** (`/W4 /WX` via `cmake/CompilerWarnings.cmake`)
  applies to both configurations, for every first-party target.
- **Generator:** not fixed by this plan — Visual Studio generator and
  Ninja (single- or multi-config) are both expected to work given the
  CMake structure above; README documents at least one concrete command
  line, not a generator mandate. This remains an open implementation
  choice, not an architectural one — not covered by any of the five
  accepted ADRs and not requiring one.

## 10. Implementation Order

1. Root `CMakeLists.txt` + `.gitignore`; confirm an empty project
   configures and "builds" (no targets yet) in both Debug and Release.
2. `cmake/CompilerWarnings.cmake`; apply to a throwaway target to confirm
   `/W4 /WX` behaves as expected.
3. Wire `FetchContent` + Catch2 v3 in `tests/core/CMakeLists.txt` per
   ADR-0006/ADR-0007; confirm an empty Catch2 test target builds and
   `ctest` reports zero tests passing (proves the harness, not yet the
   content).
4. `atlantis_core` library skeleton (empty translation unit, public
   include directory wired) — confirm it builds and links.
5. `atlantis/result.h` (minimal result/error utility type) +
   `result_tests.cpp`.
6. `atlantis/assert.h` + `assert.cpp` (macros, failure handler per
   ADR-0009) + `assert_tests.cpp`.
7. `atlantis/log.h` + `log.cpp` (levels, sink, macros per ADR-0008) +
   `log_tests.cpp`.
8. `examples/foundation_demo/`: links `Atlantis::Core`, logs at multiple
   severities, exercises at least one `ATLANTIS_CHECK` call path.
9. Full `ctest` run; confirm all tests pass and are individually
   discovered (not one opaque binary-level pass/fail).
10. Update `README.md` (including its repository layout list and a new
    `examples/README.md`), `CLAUDE.md`, `src/README.md`, `tests/README.md`
    per Files to Modify/Create.
11. Full [Definition of Done](../docs/process/definition-of-done.md) pass
    before PR: zero warnings, no dead/commented-out code, docs updated.

All five ADRs this order depends on (steps 3, 6, 7) are now **Accepted**
(2026-08-02) — none of these steps is currently blocked by a missing
architectural decision. The remaining gate before any step may actually
begin is procedural, not architectural: `specs/0001-project-foundation.md`
must reach `Approved` and this plan must clear Human Review — see the
banner at the top of this document.

## 11. Verification Steps

Maps to the spec's Testing & Verification Plan:

- Configure and build, Debug and Release, from a clean Windows checkout,
  with no manual setup beyond what the updated README documents.
- `ctest` run from the build directory: all tests pass, individually
  discovered.
- Run `atlantis_foundation_demo` (built from `examples/foundation_demo/`)
  directly: confirm log output at more than one severity level, confirm
  no crash (i.e. the demo's `CHECK` call path succeeds under normal
  conditions).
- Build output inspected for zero compiler warnings under `/W4 /WX` on
  every first-party target.
- Manual repo scan (e.g. `grep`) confirming no Vulkan/`Vk*`, no
  windowing library, no Atlantis Platform code, and no Linux- or
  Android/iOS-toolchain-specific content was introduced.
- Vulkan Validation Layers: **N/A**, no Vulkan in this plan.
- Image regression: **N/A**, nothing rendered.

## 12. Acceptance Criteria Mapping

| Spec Acceptance Criterion | Satisfied by (plan step) | Verified by |
|---|---|---|
| `cmake` configures on a clean Windows checkout, Debug + Release | Steps 1–2 | Verification: clean-checkout build |
| Build produces `Atlantis Core` library + linked executable | Steps 4, 8 | Verification: build output inspection |
| Executable runs, logs at >1 severity level | Steps 7–8 | Verification: run demo executable |
| ≥1 assertion-abstraction check exists, Debug/Release behavior matches ADR-0009 | Step 6 | Verification: manual code check |
| Test framework invocable via one command, ≥1 real passing test | Steps 3, 5–7, 9 | Verification: `ctest` run |
| New targets build with zero warnings | All steps (warnings target applied from step 2 on) | Verification: build output inspection |
| No Vulkan/windowing/Renderer/RenderGraph/Platform code or dependency introduced | All steps (scope discipline) | Verification: manual repo scan |
| No Linux-specific or Android/iOS-toolchain content introduced | All steps (scope discipline) | Verification: manual repo scan |
| All five spec-identified ADRs reach `Accepted` before spec is `Approved` | **Satisfied 2026-08-02** — ADR-0006–0010 `Accepted`, spec `Approved` | Human Review complete; see Approval Status in the accompanying report |

## Sequencing & Dependencies

Steps 1–2 have no blockers. Steps 3, 6, 7 were gated on
ADR-0006/0007, ADR-0009, and ADR-0008 respectively — **all now
Accepted**, so no step is currently blocked by a missing architectural
decision; remaining sequencing below is purely order-of-implementation.
Steps 4–5 depend only on step 1 (they don't need the test framework to
exist to write the library skeleton, though `result_tests.cpp` needs step
3's harness to actually run). Step 8 depends on 6 and 7. Steps 9–11 are
final and depend on everything above.

## Rollback Plan

Purely additive: no existing functionality, data, or running system is
touched. Reverting the PR that implements this plan removes the new
files and restores `src/`, `tests/`, `README.md`, and `CLAUDE.md` to
their current placeholder state — no migration or cleanup beyond a
standard revert.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this plan:

- "Vulkan Validation Layers run clean" and "Image regression tests" items
  are not applicable — explicitly excluded by the spec's Non-Goals.
- Add: all five ADRs named below are `Accepted`, the spec is `Approved`,
  and this plan is `Approved / Ready for Implementation` (**all done,
  2026-08-02**). Implementation may proceed against this plan.

## Architectural Decisions — Resolved via Accepted ADRs

All five decisions this plan originally flagged as pending are now
`Accepted` (2026-08-02), "as currently documented":

1. **Dependency management** — [ADR-0006](../adr/0006-dependency-management.md):
   `FetchContent`, pinned tags; Vulkan SDK/Android NDK are external
   system dependencies, not fetched.
2. **Test framework** — [ADR-0007](../adr/0007-test-framework.md):
   Catch2 v3 via `FetchContent`.
3. **Logging design** — [ADR-0008](../adr/0008-logging.md): hand-rolled,
   `std::format`-based, swappable sink, synchronous.
4. **Assertion design** — [ADR-0009](../adr/0009-assertion.md):
   `ATLANTIS_ASSERT`/`ATLANTIS_CHECK`, no exceptions, swappable failure
   handler.
5. **CMake target/library structure, namespace, and example placement** —
   [ADR-0010](../adr/0010-cmake-structure.md): `atlantis_<module>` /
   `Atlantis::<Module>`, bare `atlantis::` namespace for Core, proof
   executable at `examples/foundation_demo/`.

The following items from this plan's prior revision are now **resolved
and removed** as open questions:

- ~~The namespace convention resolution~~ — resolved by ADR-0010.
- ~~The ADR numbering note~~ — confirmed correct (`0006`–`0010`), no
  longer an open question, restated as fact in the banner above.
- ~~The proof executable's location~~ — resolved by ADR-0010:
  `examples/foundation_demo/`.

## Remaining Open Items (not architectural, or not yet resolved)

- **ADR `Deciders` fields still read `_pending human review_`.** Status
  was flipped to `Accepted` on all five per this approval transition, but
  only the `Status` field was changed, per explicit instruction — the
  `Deciders` line is a minor, non-blocking cosmetic follow-up.
- Whether `FetchContent`'s adequacy for this spec's narrow dependency
  need is also meant to answer heavier dependency questions later specs
  (e.g. the Vulkan SDK integration) will raise — explicitly left open by
  ADR-0006 itself, not decided by this plan.
- Android's log-sink and assertion-failure routing (ADR-0008/ADR-0009)
  are documented intent, not implemented or verified — no Android build
  support exists yet.
- `examples/README.md`'s content and the `README.md` repository-layout
  update are now captured in Files to Create/Modify above but not yet
  written — ordinary implementation work (step 10), not an open
  architectural question.
