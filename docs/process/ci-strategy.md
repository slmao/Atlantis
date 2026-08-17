# CI Strategy

No CI pipeline exists yet. This document defines the strategy a future
build-system/CI spec must follow; it does **not** authorize writing
`.github/workflows/*.yml` on its own — that YAML depends on build-system
decisions (CMake target structure, dependency management) that haven't
been specced yet. Writing it now would be exactly the kind of uncontrolled
architectural decision [AGENTS.md](../../AGENTS.md) prohibits.

> **Revised 2026-08-02:** removed Linux-specific CI content. Atlantis's
> target platforms are Windows and Android (primary) and iOS (future);
> **Linux is not a target platform** and CI must not add Linux-specific
> jobs, dependencies, or platform assumptions — see
> [AGENTS.md](../../AGENTS.md) Phase 1 constraints.

## Sequencing note

Windowed rendering shipped before headless rendering, which shipped
before image regression testing, per [AGENTS.md](../../AGENTS.md)'s
sequencing rule — both headless rendering (Spec 0010) and image
regression testing (Spec 0011) are now `Approved` and implemented, with
a real, working **local/manual** gate (a human or agent runs
`ctest -L gpu` against real Vulkan-capable hardware and reports
pass/fail — see
[testing-strategy.md](testing-strategy.md#golden-images)). **This is a
fact about what exists locally, not a change to this document's own
subject: no CI pipeline exists in this repository, so nothing below
this line is true of CI yet.** CI still cannot drive an on-screen
swapchain, and CI-enforced image regression gating (below) remains
blocked on the same GPU-in-CI/dependency-fetch prerequisites this
document already logs as open — reaching a working local harness does
not, by itself, resolve either.

## Baseline requirements, once CI exists

- **Platform:** Windows, the project's primary development platform (see
  [AGENTS.md](../../AGENTS.md) Phase 1 constraints). Android CI (build
  verification, most likely via NDK cross-compilation, and however the
  project decides to validate on-device/emulator) is anticipated but not
  designed here — see Open Questions.
- **Build:** must build from a clean checkout with no manual setup steps
  beyond what's documented in the README.
- **Vulkan Validation Layers:** enabled in every CI build that touches the
  GPU; a validation error or warning fails the job, it does not just log.
- **Image regression tests:** the harness itself now exists and runs
  locally/manually (Spec 0011, `Approved`,
  `tests/image_regression/`) — on failure, it already writes the
  actual/expected/diff images to a documented build-output location (see
  [testing-strategy.md](testing-strategy.md#golden-images)), so the
  artifact-upload step below is a small, already-satisfied precondition,
  not new design. **Not yet true:** these do not run in CI, because no CI
  pipeline exists. Once CI exists, uploading that same
  already-produced actual/expected/diff output as build artifacts is the
  remaining, straightforward step — so a human can inspect a failure
  without reproducing it locally.
- **Required check:** CI must be a required status check on `main` once
  branch protection is configured (human action, see
  [git-workflow.md](git-workflow.md)).

## Open questions requiring human/spec decisions

- **GPU access in CI:** a software Vulkan implementation (e.g.
  SwiftShader, which runs on Windows) for every PR vs. a self-hosted
  real-GPU Windows runner for full validation, and whether both tiers are
  needed (fast software-only gate on every push, real-GPU run nightly or
  pre-merge).
- **Compiler matrix:** which Windows compiler(s)/versions are supported
  (candidate: MSVC as primary, possibly Clang-cl; versions TBD by the
  C++20 feature set actually used). Android's own toolchain (NDK Clang)
  is a separate future consideration, not decided here.
- **Android CI:** how/whether Android builds are verified in CI at all in
  Phase 1 (NDK cross-compilation build-only vs. emulator/device runs) —
  not designed here; deferred to whatever spec adds Android build support
  (see [specs/0001-project-foundation.md](../../specs/0001-project-foundation.md),
  which is Windows-only and explicitly excludes this).
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
