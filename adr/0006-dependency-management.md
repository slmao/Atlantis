# ADR 0006: Dependency Management for the Initial Project Foundation

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _pending human review_
- **Related Spec:** [specs/0001-project-foundation.md](../specs/0001-project-foundation.md)

## Context

The repository has no build system yet. `specs/0001-project-foundation.md`
requires a minimal buildable C++20/CMake project including a unit test
framework, and its Architectural Impact section names "dependency
management strategy" as a required decision — already flagged as open in
[docs/process/ci-strategy.md](../docs/process/ci-strategy.md) and
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md).
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md)
proposed CMake `FetchContent` pending this ADR; this ADR is that decision.

Two categories of external dependency exist for Atlantis, and they behave
very differently:
- **Small, source-buildable development/test dependencies** — e.g. a unit
  test framework. Cheap to fetch and build from source per build tree.
- **Large platform SDKs/toolchains** — the Vulkan SDK (needed by a future
  RHI/Vulkan Backend spec, not this one) and the Android NDK/SDK (needed
  by a future Android build-support spec, not this one). These are large,
  versioned, binary/installer-distributed, and platform-specific; treating
  them the same way as a small pinned source dependency does not fit how
  they're actually distributed or how CI/developer machines actually
  provision them.

## Decision

- Small development/test dependencies for the initial project foundation
  are acquired via CMake's built-in **`FetchContent`** module, each
  pinned to a specific tagged release (`GIT_TAG` set to a release tag or
  commit hash, never a floating branch). Concretely, this covers Catch2
  (see [ADR-0007](0007-test-framework.md)).
- **vcpkg and Conan are not used** at this stage. No package manager is
  required to configure or build Atlantis — a supported compiler, CMake,
  and (for the first configure) network access to fetch pinned
  dependencies is sufficient.
- **The Vulkan SDK and the Android NDK/SDK are external system/toolchain
  dependencies, not `FetchContent` dependencies.** They are expected to
  be installed on the host (or provided by a CI image) and located via
  `find_package`/environment variables (e.g. `VULKAN_SDK`) or toolchain
  files — never downloaded and built from source by CMake. This ADR does
  not itself configure their discovery (no Vulkan/Android code exists
  yet, per the spec's Non-Goals); it fixes the *category* they belong to
  so a future RHI/Android spec doesn't have to re-litigate it.
- This decision covers the initial project-foundation stage's actual need
  (one test framework). **Future dependencies that don't fit the "small
  pinned source dependency via FetchContent" model require their own
  ADR** rather than being silently folded into this one.

## Consequences

### Positive

- Zero extra tooling to build: CMake + a supported compiler (+ the Vulkan
  SDK/NDK, once later specs need them) is sufficient — no vcpkg/Conan
  install step for contributors or CI.
- Reproducible: pinned tags mean the same dependency version is fetched
  every time, with no floating "latest" drift between machines or CI runs.
- Matches the actual Phase 1 need (one test framework) without adopting a
  heavier dependency-management system whose benefits (binary caching,
  transitive resolution, license auditing) aren't needed at this scale.
- A clean category boundary ("small pinned source dep" vs. "external
  system SDK") gives future specs (RHI/Vulkan, Android build support) a
  decided answer for "how do we get the Vulkan SDK/NDK" without having to
  invent or justify it themselves.

### Negative / Trade-offs

- `FetchContent` rebuilds dependencies from source per build tree (unless
  a shared cache directory is separately configured), which is slower
  than a binary package manager's cached install — acceptable for one
  small test-only dependency; may not scale gracefully if many more
  source dependencies accumulate later.
- No automatic transitive dependency resolution or version-conflict
  handling the way vcpkg/Conan provide — each new dependency's
  compatibility must be verified manually.
- First configure requires network access to fetch pinned dependencies;
  offline/air-gapped builds would need a pre-populated cache or vendored
  copy, which this ADR does not address.
- Two different acquisition stories (FetchContent for small deps,
  pre-installed for big SDKs) means two different sets of setup
  instructions rather than one uniform package-manager story — a
  documentation cost, not just a build-config one.

## Alternatives Considered

- **vcpkg** — rejected per explicit requirement to avoid it for the
  initial foundation; would add a package-manager dependency and (for a
  Windows+Android target) manifest/triplet complexity disproportionate to
  "we need one test framework."
- **Conan** — rejected for the same reason; an additional Python-based
  package manager and setup step disproportionate to current need.
- **Git submodules** — rejected: submodule version pinning has real
  ergonomic footguns (detached-HEAD state, easy-to-forget
  `--recurse-submodules` on clone) that `FetchContent`'s `GIT_TAG` avoids
  by expressing the pin directly in CMake.
- **Vendoring dependency source directly into the repository** — rejected:
  bloats repository history, turns updating a dependency into a manual
  diff/copy exercise, and obscures provenance/licensing versus a declared,
  pinned fetch.
- **Treating the Vulkan SDK/NDK as `FetchContent` dependencies** —
  rejected per explicit requirement: these are large, versioned,
  platform-provided toolchains with their own installers; forcing them
  through `FetchContent` would be slow, fragile, and duplicate work the
  Vulkan SDK installer / Android SDK manager already do correctly.
