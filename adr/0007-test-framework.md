# ADR 0007: Unit Test Framework — Catch2 v3

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _Human approval confirmed 2026-08-04_
- **Related Spec:** [specs/0001-project-foundation.md](../specs/0001-project-foundation.md)

## Context

`specs/0001-project-foundation.md` requires a unit test framework wired
into CMake such that a single documented command discovers and runs
tests, with at least one real test against core utility code. Test
framework choice is already flagged as open in
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md).
[ADR-0006](0006-dependency-management.md) fixed *how* a small dependency
like a test framework is acquired (`FetchContent`, pinned); this ADR
decides *which* framework.

## Decision

Use **Catch2 v3**, fetched via `FetchContent` per
[ADR-0006](0006-dependency-management.md), pinned to a specific tagged
release. Registered with CTest via Catch2's `catch_discover_tests()` CMake
integration, so individual `TEST_CASE`s are reported as separate CTest
tests rather than one opaque pass/fail per binary — matching the Unit
Test Structure already proposed in
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md).

**Scope of use:**
- Unit tests (e.g. `Atlantis Core`'s result/error type, logging,
  assertion-failure logic).
- Small integration tests that don't require a GPU (testing-strategy.md
  layer 1) — e.g., future RHI/RenderGraph bookkeeping/scheduling logic.
- Core utility tests generally.

**Explicitly not used for:** rendering or image-regression testing.
Headless integration tests and image-regression tests
(testing-strategy.md layers 2–3) are a distinct future harness with
different needs (GPU-aware fixtures, golden-image comparison, diff
artifact upload) that this ADR does not design. A future spec/ADR decides
that harness — it may or may not use Catch2 as its runner/assertion
layer underneath, but that's not decided here.

## Consequences

### Positive

- Modern C++ (comfortable fit for C++20); v3 is a compiled library rather
  than v2's header-only model, which is faster for incremental builds as
  the test suite grows.
- `catch_discover_tests()` gives per-`TEST_CASE` CTest reporting out of
  the box, matching the granularity plans/0001 already assumes.
- Expressive `TEST_CASE`/`SECTION` syntax, widely known, low onboarding
  cost.
- Actively maintained with a large community and a well-documented CMake
  integration story.

### Negative / Trade-offs

- Adds Catch2's own build time to a from-scratch configure (mitigated:
  pinned tag, cached after the first build).
- v3's compiled-library model requires building Catch2 itself once per
  build tree — a minor cost on a first build, a net win over v2's
  per-translation-unit header compile cost as the suite grows.
- Test code becomes coupled to Catch2-specific macros; switching
  frameworks later means rewriting test bodies, not just build
  configuration. This is the ordinary, accepted cost of choosing any test
  framework, not specific to Catch2.

## Alternatives Considered

- **GoogleTest** — a reasonable alternative with comparable CMake/CTest
  integration (`gtest_discover_tests`), but a more verbose
  fixture-class-based style. Not chosen: Catch2's lighter-weight,
  single-executable-per-target model and `SECTION`-based expressiveness
  were judged a better fit for a small, currently single-module test
  suite. This is a low-cost choice to revisit later since only test code,
  not product code, depends on the framework.
- **doctest** — lighter-weight and historically faster to compile than
  Catch2 v2, but a smaller community/ecosystem. Not chosen: Catch2 v3
  addresses the header-only compile-time complaint doctest was built to
  solve, while having broader adoption.
- **Custom/hand-rolled test framework** — rejected: reinventing assertion
  macros, test discovery, and CTest integration has no benefit over an
  established, well-maintained library, and contradicts "keep
  dependencies minimal" being about avoiding *unnecessary* dependencies,
  not avoiding reasonable ones.
