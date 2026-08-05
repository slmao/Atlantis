# Atlantis

<p align="center">
  <img src="assets/branding/Atlantis_Logo.png" alt="Atlantis Logo" width="400"/>
</p>

Atlantis is a long-term, real-time rendering engine project. It is currently
in its **engineering-foundation stage**: no rendering code exists yet. This
repository holds the process and structure the project will be built with.

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
src/                Source — src/core/ implements Atlantis Core (spec/plan/ADR 0001, 0006-0010); src/platform/ implements Atlantis Platform's Windows path (spec/plan 0002, ADR-0005/0010-0013) — Android/iOS are specified architecturally but not implemented; every other module still empty, pending its own spec/plan/ADR
examples/           Non-shipping demo programs (foundation_demo/, platform_demo/) — see ADR-0010
tests/              Tests — tests/core/ and tests/platform/ (Catch2 v3, plus Windows-only integration smoke tests); other layers pending their own spec
shaders/            Shader sources (empty — structure pending first spec/plan/ADR)
assets/             Engine/sample assets (empty — structure pending first spec/plan/ADR)
tools/              Offline/dev tooling (empty — structure pending first spec/plan/ADR)
cmake/              CMake helper modules (CompilerWarnings.cmake)
.github/            PR template and repository automation
```

## Building

Requires CMake 3.21+ and a C++20 compiler (MSVC, Windows — see
[AGENTS.md](AGENTS.md) Phase 1 constraints). No package manager install
step is required; the unit test framework (Catch2 v3) is fetched
automatically on first configure (see
[ADR-0006](adr/0006-dependency-management.md)).

```
cmake -S . -B build
cmake --build build --config Debug
cmake --build build --config Release
ctest --test-dir build -C Debug
```

Run the foundation demo: `build/examples/foundation_demo/Debug/atlantis_foundation_demo.exe`
(path varies by generator/configuration).

Run the Windows Platform demo (opens a real, blank window; no rendering):
`build/examples/platform_demo/Debug/atlantis_platform_demo.exe`
(path varies by generator/configuration).

## Status

Engineering foundation stage. `specs/0001-project-foundation.md` is
implemented: a minimal C++20/CMake project (`Atlantis Core` — logging,
assertions, a result/error type — plus its unit tests and a proof-of-build
demo). `specs/0002-platform-foundation.md`'s Windows path is also
implemented: `Atlantis Platform` (application lifecycle, window
creation/destruction, `PlatformEvent` delivery, monotonic timing), its
Catch2 unit tests and Windows-only integration smoke tests, and a
non-rendering `platform_demo` that opens a real, blank window. Android and
iOS remain specified architecturally only (not implemented). No Vulkan, no
RHI, no Renderer, and no CI pipeline yet. See [docs/](docs/) for what's
documented so far and the open architectural questions still awaiting
human decisions.

## License

Not yet chosen. Do not assume a license until this section is updated.
