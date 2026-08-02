# tests/

**`core/`** — unit tests for Atlantis Core, per
[specs/0001-project-foundation.md](../specs/0001-project-foundation.md),
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md),
and [ADR-0007](../adr/0007-test-framework.md) (Catch2 v3, fetched via
CMake `FetchContent` per [ADR-0006](../adr/0006-dependency-management.md)).
Run via `ctest` from the build directory, or by invoking
`atlantis_core_tests` directly.

Headless integration and image-regression test layers (see
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md))
are not implemented — they're blocked on headless rendering, which
doesn't exist yet. Their concrete directory structure and test framework
choice (if different from Catch2) will be established by the spec that
introduces that harness, not invented ahead of time. See
[AGENTS.md](../AGENTS.md).

Do not add test files for other modules here without a linked spec and
plan.
