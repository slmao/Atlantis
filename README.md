# Atlantis

<p align="center">
  <img src="assets/branding/Atlantis_Logo.png" alt="Atlantis Logo" width="400"/>
</p>

Atlantis is a long-term, real-time rendering engine project. It is currently
in its **engineering-foundation stage**: Core, Windows Platform, a
backend-independent RHI, and a Vulkan windowed presentation foundation are
implemented, but nothing is rendered yet — no acquire, present, or draw
call exists anywhere in the codebase. This repository holds the process
and structure the project will be built with.

## Phase 1 technical scope

- C++20
- CMake
- **Target platforms:** Windows and Android (primary), Vulkan on both.
  iOS is a future target (not started, not designed) — see below.
- **Linux is not a target platform.** No Linux-specific source, build
  config, CI jobs, or dependencies.
- A backend-independent RHI (Render Hardware Interface)
- An Atlantis Platform module abstracting per-OS windowing/surface/
  lifecycle (Win32, Android NDK, future iOS) — kept out of the Renderer
  entirely
- A Render Graph as the central rendering abstraction
- Windowed rendering first — the initial working milestone is an
  interactive, on-screen swapchain path, on Windows and/or Android
- Headless rendering after that — built once the windowed path works,
  and what unlocks image regression testing
- Image regression testing (depends on headless rendering landing)
- Vulkan Validation Layers as a correctness gate
- RenderDoc-based debugging

## Planned future phases (not started, not designed yet)

- iOS support — either Vulkan via MoltenVK, or a native Metal RHI
  backend; the choice is explicitly undecided and not to be designed
  ahead of its own spec
- GPU-driven rendering
- Neural rendering / neural shading
- 3D Gaussian Splatting
- World-model–related workloads

These are listed here to communicate direction, not to authorize starting
them. Each will require its own spec before any design work begins.

## Spec-Driven Development

Atlantis is built with a strict, enforced workflow so that architecture is
always a deliberate, reviewed decision — including (especially) when the
work is done by an AI agent:

```
Spec  →  Plan  →  Human Review  →  Implementation  →  Verification  →  PR  →  Merge
```

| Stage | Lives in | Purpose |
|---|---|---|
| Spec | [specs/](specs/) | What problem, what requirements, what design, what's out of scope |
| Plan | [plans/](plans/) | How an approved spec becomes an ordered, reviewable set of changes |
| Human Review | — | Explicit human sign-off on spec + plan before implementation begins |
| ADR | [adr/](adr/) | Permanent record of any architectural decision and why it was made |
| Implementation | `src/`, `tests/` | Code written strictly against the approved plan |
| Verification | PR | Checked against the plan's verification checklist and the [Definition of Done](docs/process/definition-of-done.md) |
| PR → Merge | GitHub | An agent opens the PR; a human reviews and merges — never the reverse |

See [AGENTS.md](AGENTS.md) for the full rules that govern this — including
for AI agents working in this repo — and
[docs/process/git-workflow.md](docs/process/git-workflow.md) for how this
maps onto branches and PRs.

## Repository layout

```
AGENTS.md          Canonical agent operating rules (read this first)
CLAUDE.md           Claude Code–specific pointer to AGENTS.md
README.md           This file
docs/               Architecture records (as-built) and process docs
specs/              Proposed work, pre-implementation
plans/              Approved implementation plans
adr/                Architectural decision records
src/                Source — src/core/ (Atlantis Core, spec/plan/ADR 0001/0006-0010); src/platform/ (Atlantis Platform's Windows path, spec/plan 0002, ADR-0005/0010-0013 — Android/iOS specified but not implemented); src/rhi/ and src/vulkan_backend/ (backend-independent RHI and its sole Phase 1 backend, Windows windowed Vulkan presentation only, spec/plan 0003, ADR-0001-0003/0014-0016); every other module still empty, pending its own spec/plan/ADR
examples/           Non-shipping demo programs (foundation_demo/, platform_demo/, rhi_vulkan_demo/) — see ADR-0010
tests/              Tests — tests/core/, tests/platform/, tests/rhi/ (Catch2 v3, all GPU-independent), tests/vulkan_backend/ (GPU-independent plus a separate, explicitly gpu-labeled Windows/Vulkan integration executable); headless and image-regression layers pending their own spec
shaders/            Shader sources (empty — structure pending first spec/plan/ADR)
assets/             Engine/sample assets (empty — structure pending first spec/plan/ADR)
tools/              Offline/dev tooling (empty — structure pending first spec/plan/ADR)
cmake/              CMake helper modules (CompilerWarnings.cmake)
.github/            PR template and repository automation
```

## Building

Requires CMake 3.21+, a C++20 compiler (MSVC, Windows — see
[AGENTS.md](AGENTS.md) Phase 1 constraints), the Windows SDK, and a
pre-installed [Vulkan SDK](https://vulkan.lunarg.com/). CMake locates the
Vulkan SDK via `find_package(Vulkan REQUIRED)` — configuration fails
outright if no Vulkan SDK is found. If it is not discovered
automatically, set the `VULKAN_SDK` environment variable for the current
shell session before configuring, e.g.:

```
$env:VULKAN_SDK = 'C:\VulkanSDK\<version>'
```

The Vulkan SDK is an external prerequisite installed separately, not a
dependency this project downloads (see [ADR-0006](adr/0006-dependency-management.md)'s
external-system-dependency category). The unit test framework (Catch2 v3)
remains the only dependency CMake fetches automatically, via
`FetchContent` on first configure.

```
cmake -S . -B build
cmake --build build --config Debug
cmake --build build --config Release
```

Run the GPU-independent test suite (excludes the Vulkan GPU integration
tests):
```
ctest --test-dir build -C Debug -LE gpu --output-on-failure
```
Run the GPU-required Vulkan integration tests, which need a real,
Vulkan-capable Windows machine (replace `Debug` with `Release` for a
Release build):
```
ctest --test-dir build -C Debug -L gpu --output-on-failure
```
A bare `ctest` runs every registered test regardless of label, including
the GPU-required ones — prefer the explicit `-LE gpu`/`-L gpu` commands
above. See [tests/README.md](tests/README.md) for what each suite covers.

Run the foundation demo: `build/examples/foundation_demo/Debug/atlantis_foundation_demo.exe`
(path varies by generator/configuration).

Run the Windows Platform demo (opens a real, blank window; no rendering):
`build/examples/platform_demo/Debug/atlantis_platform_demo.exe`
(path varies by generator/configuration).

Run the RHI Vulkan verification demo (opens a real, blank window; creates
a Vulkan `Device` and `Presentation`; no rendering):
`build/examples/rhi_vulkan_demo/Debug/atlantis_rhi_vulkan_demo.exe`
(path varies by generator/configuration).

## Status

Engineering foundation stage. `specs/0001-project-foundation.md` is
implemented: a minimal C++20/CMake project (`Atlantis Core` — logging,
assertions, a result/error type — plus its unit tests and a proof-of-build
demo). `specs/0002-platform-foundation.md`'s Windows path is also
implemented: `Atlantis Platform` (application lifecycle, window
creation/destruction, `PlatformEvent` delivery, monotonic timing), its
Catch2 unit tests and Windows-only integration smoke tests, and a
non-rendering `platform_demo` that opens a real, blank window.

`specs/0003-rhi-vulkan-windowed-foundation.md` is also implemented: a
backend-independent `Atlantis RHI` (`Device`, `Presentation`'s non-frame
lifecycle, and their supporting value types) and its sole Phase 1
backend, `Atlantis Vulkan Backend` — Vulkan instance/device construction,
Validation Layer enforcement, Windows WSI surface creation, and
swapchain ownership with resize-driven recreation — plus GPU-independent
unit tests, a Windows/Vulkan GPU integration test suite, and a
non-rendering `rhi_vulkan_demo` that opens a real window and interactively
exercises resize/minimize/restore. This is a **windowed Vulkan
presentation foundation**, not windowed rendering: `Presentation` creates
and recreates a real swapchain and reports its metadata, but nothing
acquires a swapchain image, submits a GPU command, or presents a frame —
an acquire/present API, `RenderTarget`, RenderGraph, and Renderer are all
still unimplemented, deferred as one bundle to a future RenderGraph
specification (see [ADR-0016](adr/0016-presentation-acquire-present-and-recreation-contract.md)).

Android and iOS remain specified architecturally only (not implemented);
Vulkan Backend's Android WSI path is likewise not implemented. No headless
rendering, no image regression testing, and no CI pipeline yet. See
[docs/](docs/) for what's documented so far and the open architectural
questions still awaiting human decisions.

## License

Not yet chosen. Do not assume a license until this section is updated.
