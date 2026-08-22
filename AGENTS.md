# AGENTS.md — Operating Rules for AI Agents in Atlantis

This file is the canonical, tool-agnostic operating manual for any AI agent
(Claude, Codex, Copilot Workspace, or otherwise) working in this repository.
Tool-specific files (e.g. `CLAUDE.md`) exist only to point back here and add
tool-specific notes — they must never restate or fork these rules.

If you are an AI agent and you have not read this file yet this session,
read it before touching any file.

## The Golden Rule

**AI agents must not make uncontrolled architectural decisions.**

Atlantis is a long-term rendering engine. Its value depends on the coherence
of its architecture over years, not the speed of any single change. An agent
that quietly picks an abstraction, a threading model, a dependency, or a
module boundary — even a "reasonable" one — can lock in a decision nobody
reviewed. That is the single failure mode this document exists to prevent.

Every significant piece of work follows one path, no exceptions:

```
Spec  →  Plan  →  Human Review  →  Implementation  →  Verification  →  PR  →  Merge
```

## What counts as "significant"

Requires the full Spec → Plan → Implementation → Verification → PR path:

- Anything that introduces or changes a public API, module boundary, or
  subsystem (RHI, render graph, memory model, threading model, etc.)
- Anything that adds a new dependency (library, tool, SDK)
- Anything that changes the build system, CI, or repository structure
- Any new rendering feature, backend behavior, or data format
- Any change to process/governance docs in this repository

Does **not** require a spec (still needs a PR and normal review):

- Typo fixes, comment fixes, formatting
- Fixing an already-agreed-upon bug with no design implication
- Editing an existing spec/plan/ADR draft before it is approved

When in doubt, treat it as significant. Escalate to the human instead of
guessing.

## The workflow, stage by stage

1. **Spec** (`specs/`) — Problem, goals/non-goals, requirements, proposed
   design, and an explicit **Architectural Impact** section. If the spec
   touches architecture, it must name the ADR that will be written. A spec
   is not implementation-ready until a human has approved it. Use
   [specs/template.md](specs/template.md).

2. **Plan** (`plans/`) — Turns an approved spec into an ordered, reviewable
   task breakdown: files/modules touched, sequencing, and a verification
   checklist that maps back to the spec. A plan is not implementation-ready
   until a human has approved it. Use [plans/template.md](plans/template.md).

3. **Human Review** — The explicit gate between planning and coding: a
   human has read the spec and plan together and signed off that
   implementation may begin. This is a distinct checkpoint, not implied by
   the individual spec/plan approvals above — an agent does not start
   Implementation on its own judgment that "the spec and plan look
   approved enough."

4. **ADR** (`adr/`) — Any architectural decision identified by a spec gets
   its own ADR: context, decision, consequences, alternatives considered.
   ADRs are the permanent record of *why*; specs and plans may be
   superseded, ADRs are not silently rewritten. Use
   [adr/template.md](adr/template.md). An ADR is drafted alongside the spec
   that identifies the decision (see [specs/template.md](specs/template.md)
   Architectural Impact section) and must reach `Accepted` before or during
   Human Review — not discovered as a gap during implementation.

5. **Implementation** — Code written strictly against the approved plan.
   If reality forces a deviation from the plan, stop and call it out
   explicitly in the PR rather than silently drifting — a deviation that
   changes architecture means back to step 1. Do not modify the spec to
   make implementation easier; a spec that turns out to be wrong gets a
   revision or follow-up spec, reviewed like any other change, not a
   silent edit to match what got built. Do not fold in unrelated
   refactoring — a plan's file list is the scope, not a starting point for
   opportunistic cleanup.

6. **Verification** — Executed against the plan's verification checklist
   and [docs/process/testing-strategy.md](docs/process/testing-strategy.md).
   Vulkan Validation Layers must run clean; see
   [docs/process/definition-of-done.md](docs/process/definition-of-done.md).
   Build and run the test suite after implementation — verification is not
   optional or assumed-passing.

7. **PR → Merge** — A PR is opened using the repository's
   [PR template](.github/PULL_REQUEST_TEMPLATE.md), linking the spec, plan,
   and any ADRs. See [docs/process/git-workflow.md](docs/process/git-workflow.md).
   An agent never merges its own PR to `main` and never pushes directly to
   `main`. A human reviews and merges.

## Phase 1 constraints (do not silently expand these)

- Language: C++20
- Build: CMake
- Graphics backend: **Vulkan only** — do not scaffold for other backends
  behind the RHI "for later"; the RHI must stay backend-independent in
  interface, but no second backend gets implemented in Phase 1
- Render Graph is the central rendering abstraction — do not bypass it
  with ad hoc direct-submission code paths
- Sequencing: **windowed rendering ships first.** Headless rendering and
  image regression testing follow once the windowed/swapchain path works —
  they are still Phase 1 scope, not deferred to a future phase, but they
  are not the first milestone. Don't block the windowed path on headless
  infrastructure, and don't skip headless once windowed is working — see
  [docs/process/testing-strategy.md](docs/process/testing-strategy.md).
- **Target platforms — Primary: Windows and Android. Future: iOS** (not
  started, not designed). Vulkan is the graphics API on both primary
  platforms. iOS, when it starts, may use Vulkan via MoltenVK or a native
  Metal RHI backend — that choice is explicitly undecided and is not to be
  designed or scaffolded for now (see below).
- **Linux is not a target platform for Atlantis.** Do not add Linux-
  specific source code, build configuration, CI jobs, or runtime
  dependencies. Where prior drafts of this repository's docs referenced
  Linux as the target/dev platform, treat those as superseded by this
  section; see [docs/process/ci-strategy.md](docs/process/ci-strategy.md)
  for what is still pending alignment.
- CI is build-verification only until headless rendering lands; see
  [docs/process/ci-strategy.md](docs/process/ci-strategy.md).
- GPU-driven rendering, neural rendering/shading, 3D Gaussian Splatting,
  and world-model workloads are **future phases**. Do not start
  implementing them, and do not let them shape Phase 1 abstractions beyond
  what an approved spec explicitly calls for. If you see a clean
  opportunity to "future-proof" for one of these, write it up as a spec
  question instead of coding it in.

## Architecture principles

These hold across every subsystem and every phase; a spec may add detail,
none should ever need to contradict these:

- **RHI is backend-agnostic in interface; Vulkan is Phase 1's only
  implementation.** Do not scaffold for a second backend "for later" — see
  Phase 1 constraints above.
- **Render Graph is the mandatory path for GPU work.** No subsystem
  submits ad hoc, hand-scheduled GPU work outside it.
- **The Renderer does not fundamentally depend on Window, Platform, or
  Swapchain.** It consumes RHI + Render Graph + a `RenderTarget` handed to
  it by its caller, nothing more. See Module Boundaries below.
- **Platform-specific code stays outside the core Renderer, on every
  target.** Win32, Android NDK, and (future) iOS/UIKit APIs are owned by
  the Atlantis Platform module, never referenced by Renderer or
  RenderGraph.
- **Windowed and headless rendering share the same Renderer/RHI stack.**
  Headless is a second way to produce a `RenderTarget`, not a fork of the
  rendering code.
- **No speculative abstraction.** GPU-driven rendering, neural rendering/
  shading, 3D Gaussian Splatting, and world-model workloads are future
  phases; do not let them shape Phase 1 interfaces beyond what an approved
  spec explicitly calls for.
- **Every module boundary, public API shape, threading model, and
  dependency choice is a reviewed decision**, recorded in a spec/ADR — see
  the Golden Rule.

## Module boundaries

Top-level modules: **Atlantis Core, Atlantis Platform, Atlantis RHI,
Atlantis Vulkan Backend, Atlantis RenderGraph, Atlantis Renderer, Atlantis
Shader System, Atlantis Asset System, Atlantis Runtime, Atlantis Tools.**
Atlantis Asset System (Spec 0012, `Approved`) depends on Atlantis Core
only — no RHI, Renderer, or Shader System dependency; a composition root
outside the module (a test, an example, or Atlantis Runtime, which now
does this in practice — see below) loads its CPU-side asset data and is
itself responsible for constructing any GPU resource from it. See
[ADR-0043](adr/0043-asset-system-module-boundary.md).

Atlantis Runtime (Spec 0013, `Approved`) is the actual composition root —
a private `atlantis_runtime_host` static library plus a thin
`atlantis_runtime` Windows executable, composing Platform, RHI, Vulkan
Backend, Renderer, Shader System, and Asset System into one fixed
startup → windowed frame loop → shutdown lifecycle. `atlantis_runtime_host`
exists solely for testability (its own GPU-independent lifecycle/error-
classification tests) and is not a dependency any other top-level module
may take. See
[ADR-0046](adr/0046-runtime-composition-ownership-and-frame-lifecycle.md)
and
[ADR-0047](adr/0047-runtime-host-executable-library-structure-and-test-boundary.md).

**Atlantis Platform** is the per-OS windowing/surface/lifecycle
abstraction — it is to *operating systems* what RHI is to *graphics
backends*: an interface with concrete per-OS implementations (Windows
Platform, Android Platform, and — future, not implemented —  iOS
Platform). It owns Win32/Android NDK/(future) UIKit types so nothing else
has to.

The hard boundary rule: **Renderer must not directly depend on Win32, the
Android NDK, GLFW/SDL, `VkSurfaceKHR`, or `VkSwapchainKHR`** (or any `Vk*`
type, or the Vulkan Backend module directly). It depends only on RHI,
RenderGraph, and Core. Only the Vulkan Backend module may include Vulkan
headers. Platform-specific window creation, destruction, and event-loop
handling belong solely to the Atlantis Platform module. A graphics
backend may additionally have a **private WSI boundary** including the
OS headers its own graphics API's platform-surface extension requires
(for Vulkan: `vulkan_win32.h`/`vulkan_android.h` and the OS SDK headers
those declarations need) — strictly to consume Platform's
`NativeWindowHandle` (borrowed, not owned) and produce a `VkSurfaceKHR`;
those OS-specific types stay private to that WSI boundary and never reach
RHI's public API, Renderer, or RenderGraph. See
[ADR-0005](adr/0005-platform-module-multi-os-windowing.md).
**RHI does not depend on Atlantis Platform either** — it receives an
opaque native-surface handle (produced by Platform, threaded through
Runtime) at `Presentation`-creation time and never references Platform's
types, keeping Platform and RHI siblings composed by Runtime rather than
coupled to each other.

Full per-module responsibility/dependency/ownership detail — drafted as a
`PROPOSED`, not-yet-approved architecture baseline — lives in
[docs/architecture/module_boundaries.md](docs/architecture/module_boundaries.md),
with the boundary decisions themselves recorded in
[ADR-0001](adr/0001-rhi-backend-independence.md),
[ADR-0002](adr/0002-presentation-rendertarget-unification.md), and
[ADR-0005](adr/0005-platform-module-multi-os-windowing.md) (all
`Proposed`, not `Accepted`). Treat that document as the detailed
reference; this section is the summary an agent should hold in mind by
default.

## C++ coding conventions

- **Standard:** C++20. Do not rely on compiler-specific extensions.
- **Files:** one primary type (or tightly-coupled cluster) per header/
  source pair; `snake_case.h` / `snake_case.cpp`; `#pragma once` header
  guards.
- **Naming (proposed default — confirm before the first real module
  lands):** types/classes/structs/enums in `PascalCase`; functions and
  methods in `camelCase`; member variables in `camelCase` with a trailing
  underscore (`value_`); constants and enumerators in `PascalCase`;
  namespaces in `lower_snake_case` (`atlantis`, `atlantis::rhi`,
  `atlantis::rg`, `atlantis::platform`, ...) mirroring the module list
  above.
- **Formatting:** no `clang-format` configuration exists yet (tracked as
  an open question in [docs/process/ci-strategy.md](docs/process/ci-strategy.md));
  until one is checked in, match surrounding code rather than inventing a
  new style per file.
- **Includes:** own header first, then C++ standard library, then
  third-party, then project headers, each group blank-line separated. No
  `using namespace` in headers.
- **`auto`:** prefer explicit types in public API signatures (return types,
  parameters); `auto` is fine for local variables where the type is
  obvious from the initializer (iterators, lambdas).
- **Ownership types over raw ownership:** no raw `new`/`delete` outside an
  allocator's own implementation; prefer RAII types, smart pointers, and
  standard containers.

## Error handling

- **Programmer errors are assertions, not error returns.** A violated
  precondition/invariant fails fast (assert/abort in debug); it is not
  silently handled or swallowed.
- **Recoverable runtime errors use explicit result/error types, not
  exceptions**, in Core, RHI, RenderGraph, Renderer, and Runtime — this
  matches Vulkan's own error model (`VkResult`) and keeps the render path
  exception-free. Whether offline tooling (Shader System compilation,
  asset pipeline, Tools) may use exceptions is left to that module's own
  spec — it is not part of the render path.
- **Every `VkResult` is checked.** No Vulkan call's return value is
  discarded, including ones "expected" to always succeed.
- Vulkan Validation Layer output is treated as an error, not advisory
  logging — see Vulkan-specific rules below.

## Ownership and lifetime rules

- **RAII by default.** Every resource (heap memory, GPU handle, file
  handle, thread) has one clear owner type responsible for its release.
- **Borrowed access never implies ownership transfer.** Passing a raw
  pointer/reference/view means "you may use this, you do not own it."
  Ownership transfer is expressed by moving an owning type or returning by
  value, never implied by convention alone.
- **No global mutable engine-state singletons** (no global `Device`, no
  global `RenderTarget`, no global scene state). Ownership lives in the
  composition root (Runtime) and is threaded down explicitly. A narrowly-
  scoped, deliberate exception may exist for logging/diagnostics
  infrastructure in Core — that is a stated exception, not a precedent for
  further singletons.
- Subsystem-specific ownership models (e.g., how RHI resources and
  `RenderTarget`s are owned) are decided by that subsystem's own spec/ADR,
  not invented ad hoc in implementation — see
  [ADR-0003](adr/0003-resource-rendertarget-ownership-model.md) (currently
  `Proposed`) for the RHI baseline.

## Threading rules

- **Phase 1 baseline is single-threaded frame orchestration**: Runtime's
  event loop, `Presentation` acquire/present, RenderGraph construction, and
  RHI command recording all happen on one logical thread. See
  [docs/architecture/threading.md](docs/architecture/threading.md) and
  [ADR-0004](adr/0004-phase1-threading-baseline.md) (both `Proposed`).
- **Every type used across threads documents its thread-safety contract**
  at its public API — one line in the header ("not thread-safe", "safe for
  concurrent reads", etc.) is enough, but silence is not an acceptable
  contract.
- Do not add multi-threaded submission, a job/task system, or lock-free
  data structures ahead of a spec that actually needs them — see
  Architecture principles (no speculative abstraction).

## Vulkan-specific rules

- **Validation Layers are always enabled** in debug builds and in any CI
  job that touches a GPU. A validation error or warning fails the build/
  test, it is not logged and ignored — see
  [docs/process/ci-strategy.md](docs/process/ci-strategy.md) and
  [docs/process/testing-strategy.md](docs/process/testing-strategy.md).
- **Only the Vulkan Backend module may include Vulkan headers or reference
  `Vk*` types.** See [ADR-0001](adr/0001-rhi-backend-independence.md).
- **No global `VkInstance`/`VkDevice` singleton.** These are owned by the
  RHI `Device` construction path and passed down explicitly.
- **No direct `vkCmd*` calls outside the Vulkan Backend's `CommandList`
  implementation.** All command recording goes through RHI/RenderGraph.
- **Swapchain (`VkSwapchainKHR`) lifetime and recreation logic lives
  entirely inside the `Presentation` implementation** — see
  [ADR-0002](adr/0002-presentation-rendertarget-unification.md). Renderer
  never sees a swapchain object.
- **Platform-specific WSI surface creation** (`vkCreateWin32SurfaceKHR` /
  `VK_KHR_win32_surface` on Windows, `vkCreateAndroidSurfaceKHR` /
  `VK_KHR_android_surface` on Android) happens inside the Vulkan Backend's
  `Presentation` implementation, consuming the opaque native handle
  Atlantis Platform produced — never inside Atlantis Platform itself, and
  never inside Renderer. Future iOS/MoltenVK (`VK_MVK_ios_surface` /
  `VK_EXT_metal_surface`) follows the same pattern if that path is chosen;
  not implemented now.
- GPU memory management strategy (VMA vs. a hand-rolled suballocator) is
  **not yet decided** — an open question for the RHI/Vulkan Backend spec,
  not to be picked implicitly by whichever code needs an allocation first.

## Testing requirements

- Every new piece of GPU-independent logic (render graph scheduling, RHI
  bookkeeping, math/containers) gets unit tests that run without a Vulkan
  device — see [docs/process/testing-strategy.md](docs/process/testing-strategy.md).
- Any change to rendered output requires image regression tests — the
  harness exists (Spec 0011, `Approved`, `tests/image_regression/`); any
  golden-image diffs in the PR were reviewed by a human, not
  auto-accepted, and categorized per
  [ADR-0042](adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md)'s
  golden-update-reason rule. This is currently a **local/manual** gate —
  no CI pipeline exists yet — so it's run and reported by whoever opens
  the PR, alongside a clean Vulkan Validation Layers run.
- No PR merges with failing tests, or with validation-layer warnings/
  errors on any GPU-touching path.
- Build and run the relevant tests after every implementation step, not
  only right before opening a PR.

## Git workflow

- `main` is protected: no direct pushes or commits by anyone, human or
  agent. All changes land via PR, cut from a branch off `main`.
- Branch prefixes: `spec/`, `plan/`, `feature/`, `fix/`, `docs/`, `chore/`
  — `<slug>` matches the corresponding spec/plan filename for
  traceability. Commit messages use
  [Conventional Commits](https://www.conventionalcommits.org/) prefixes
  (`feat:`, `fix:`, `docs:`, `refactor:`, `test:`, `chore:`, `spec:`,
  `plan:`, `adr:`).
- An agent opens PRs but never merges them, and never pushes to `main`.
  Full detail: [docs/process/git-workflow.md](docs/process/git-workflow.md).

## Repository map

- [README.md](README.md) — project overview
- [docs/](docs/) — architecture records (as-built) and process docs
- [specs/](specs/) — proposed work, pre-implementation
- [plans/](plans/) — approved implementation plans
- [adr/](adr/) — architectural decision records
- `src/`, `tests/` — currently empty placeholders; their internal structure
  is itself an architectural decision and will be established by the first
  approved spec + plan + ADR, not invented ahead of time

## Documentation and code comments

Applies repository-wide: to this file, `README.md`,
`docs/project-blueprint.md`, specs, plans, ADRs, and code comments
alike. See [specs/0004-context-efficiency-guidelines.md](specs/0004-context-efficiency-guidelines.md)
for the full rationale; this section states the rule, not the reasoning.

**Documentation:**

- A given decision, rule, or status has **one authoritative source** —
  an Accepted ADR for *why*, an Approved Spec for *what*, an Approved
  Plan for *how/sequencing*, this file for repository-wide governance
  and coding rules. Every other document links to that source with a
  short summary, rather than restating its detail.
- Navigation/index documents (README, blueprint, spec/plan registries,
  and similar) stay focused on index, status, dependency, and roadmap
  content — not full design rationale or argumentation.
- Read documentation proportional to the task at hand rather than
  defaulting to the entire historical set. This does not authorize
  skipping a document a task genuinely needs, and does not loosen this
  file's own read-in-full-every-session rule above.
- Concision never justifies omitting a requirement, constraint, design
  rationale, risk, verification step, or governance status that a
  document's own role or template requires it to state. No mechanical
  line/word/token/size limit is used to judge this — it is a reviewed
  judgment call, like the rest of this document's qualitative rules.

**Code comments:**

- A comment explains what the code cannot: non-obvious rationale,
  invariants, ownership/lifetime/borrowing rules, thread-safety
  contracts, protocol/platform requirements, or easy-to-misuse
  behavior — not what clear naming, structure, and types already say.
  Prefer a better name, type, or a small extracted function before
  reaching for a comment.
- Required public-API contract documentation — thread-safety (see
  Threading rules), ownership/lifetime (see Ownership and lifetime
  rules), error semantics (see Error handling) — stays fully
  documented; this narrows *how* it is written, never *whether*.
- Complex algorithms, Vulkan synchronization, WSI boundary code,
  resource lifetime, and non-obvious workarounds keep comments
  sufficient to understand them, with a link to the backing spec/ADR
  where one exists.
- Update or remove a comment in the same change that makes it stale —
  a stale comment is worse than none.
- Never trade correctness or completeness of a documented contract for
  brevity.

## Definition of Done

See [docs/process/definition-of-done.md](docs/process/definition-of-done.md).
No PR is complete until every applicable item is checked.

## When you're unsure

Stop and ask the human. Escalating a question costs a message. Guessing
wrong on architecture costs months.
