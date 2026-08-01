# CI Strategy

No CI pipeline exists yet. This document defines the strategy a future
build-system/CI spec must follow; it does **not** authorize writing
`.github/workflows/*.yml` on its own — that YAML depends on build-system
decisions (CMake target structure, dependency management) that haven't
been specced yet. Writing it now would be exactly the kind of uncontrolled
architectural decision [AGENTS.md](../../AGENTS.md) prohibits.

## Sequencing note

Windowed rendering ships before headless rendering (see
[AGENTS.md](../../AGENTS.md)). CI cannot drive an on-screen swapchain, so
until headless rendering lands, CI is limited to build verification and
unit tests — no automated testing of rendered output. Image regression
gating (below) activates once headless rendering exists.

## Baseline requirements, once CI exists

- **Platform:** Linux, matching the project's development platform.
- **Build:** must build from a clean checkout with no manual setup steps
  beyond what's documented in the README.
- **Vulkan Validation Layers:** enabled in every CI build that touches the
  GPU; a validation error or warning fails the job, it does not just log.
- **Image regression tests:** once headless rendering exists, these run in
  CI; on failure, the actual/expected/diff images are uploaded as build
  artifacts so a human can inspect them without reproducing locally.
- **Required check:** CI must be a required status check on `main` once
  branch protection is configured (human action, see
  [git-workflow.md](git-workflow.md)).

## Open questions requiring human/spec decisions

- **GPU access in CI:** software Vulkan implementation (e.g. Lavapipe /
  SwiftShader) for every PR vs. a self-hosted real-GPU runner for full
  validation, and whether both tiers are needed (fast software-only gate
  on every push, real-GPU run nightly or pre-merge).
- **Compiler matrix:** which compilers/versions are supported (candidate:
  GCC + Clang, versions TBD by the C++20 feature set actually used).
- **Static analysis / formatting:** whether `clang-format` and/or
  `clang-tidy` are enforced in CI, and their configuration.
- **Dependency management strategy:** vcpkg, Conan, CMake `FetchContent`,
  or git submodules — this affects how CI caches/builds dependencies and
  needs to be decided (and given an ADR, since it's an architectural
  choice) before CI can be written.

## Not yet in scope

Anything related to GPU-driven rendering, neural rendering, 3D Gaussian
Splatting, or world-model workloads has no CI implications yet because none
of it is implemented. Do not add CI accommodations for future phases ahead
of their own specs.
