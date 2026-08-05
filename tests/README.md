# tests/

**`core/`** — unit tests for Atlantis Core, per
[specs/0001-project-foundation.md](../specs/0001-project-foundation.md),
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md),
and [ADR-0007](../adr/0007-test-framework.md) (Catch2 v3, fetched via
CMake `FetchContent` per [ADR-0006](../adr/0006-dependency-management.md)).
Run via `ctest` from the build directory, or by invoking
`atlantis_core_tests` directly.

**`platform/`** — tests for Atlantis Platform, per
[specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)
and [plans/0002-platform-foundation.md](../plans/0002-platform-foundation.md),
built as the `atlantis_platform_tests` Catch2 v3 executable:
- Portable, no-GPU, no-live-window unit tests (`NativeWindowHandle`,
  `PlatformEvent`, `WindowExtent`, the monotonic clock) plus a compile/
  link check of the public Platform API.
- `windows_platform_smoke_tests.cpp` — Windows-only (`#if defined(_WIN32)`)
  integration tests, tagged `[integration]`, that drive a real (created
  and destroyed within the test process) window through
  `initialize()`/`processEvents()`/`shutdown()` to verify `SurfaceCreated`
  ordering, close-request/duplicate-close handling, resize, minimize,
  focus, and shutdown ordering. Along with
  `src/platform/src/windows/windows_platform.cpp` itself, this is the
  only other file in the repository permitted to include `<windows.h>`.

Run via `ctest` from the build directory, or by invoking
`atlantis_platform_tests` directly (optionally filtered, e.g.
`atlantis_platform_tests "[integration]"`).

Headless integration and image-regression test layers (see
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md))
are not implemented — they're blocked on headless rendering, which
doesn't exist yet. Their concrete directory structure and test framework
choice (if different from Catch2) will be established by the spec that
introduces that harness, not invented ahead of time. See
[AGENTS.md](../AGENTS.md).

Do not add test files for other modules here without a linked spec and
plan.
