# ADR 0047: Runtime Host Executable/Library Structure and Test Boundary

- **Status:** Proposed
- **Date:** 2026-08-20
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0013-runtime-host-foundation.md](../specs/0013-runtime-host-foundation.md)

## Context

`docs/architecture/module_boundaries.md`'s own `PROPOSED` (not `Accepted`)
Runtime section currently frames Runtime as "largely private... the
composition root, not a library other modules link against." Taken
literally, that framing would make Runtime's own lifecycle/lifecycle-
failure logic untestable except by actually launching a real window and a
real GPU device — every other substantial piece of GPU-independent logic
in this codebase (RenderGraph's dependency/compilation logic, Shader
System's reflection transform, Asset System's importer/loader) already has
real unit test coverage that does not require a device; Runtime's own
composition logic, arguably the single place all of those pieces are
wired together for the first time, would otherwise be the one major
subsystem with none.

A real precedent already exists for resolving exactly this tension:
Atlantis Tools' `atlantis_shader_compiler` executable (Spec 0008,
`Approved`, implemented via
[PR #36](https://github.com/slmao/Atlantis/pull/36)) factors its own
process-execution logic into a private `atlantis_shader_compiler_lib`
specifically so that logic is unit-testable independent of actually
spawning `slangc` as a subprocess — disclosed in that spec's own registry
entry as a deliberate, accepted, non-architectural deviation from its
Plan (a mechanical testability improvement, not a new public dependency
surface).

Separately, [ADR-0033](0033-runtime-authority-and-client-boundary.md)
leaves "whether Runtime is built as a linked library, a statically-linked
executable, or some other packaging shape" explicitly Out of Scope,
deferred to Runtime's own future spec — this is that spec, and this ADR is
the decision ADR-0033 pointed to.

## Decision

**Atlantis Runtime is realized as two CMake targets: a private
`atlantis_runtime_host` static library (alias `Atlantis::RuntimeHost`)
owning all composition logic, and a thin `atlantis_runtime` executable
containing only a per-OS entry point that constructs and drives it.
`Atlantis::RuntimeHost` exists solely for testability and is not a new
public dependency surface any other top-level module may consume.**

- `atlantis_runtime_host` contains: the Runtime Host composition object
  (object model, initialization, per-frame orchestration, resize/
  lifecycle event handling, shutdown — per
  [ADR-0046](0046-runtime-composition-ownership-and-frame-lifecycle.md)),
  and the GPU-independent `RuntimeLifecycleState` transition type. It
  depends on `Atlantis::Core`, `Atlantis::Platform`, `Atlantis::RHI`,
  `Atlantis::VulkanBackend`, `Atlantis::Renderer`,
  `Atlantis::ShaderSystem`/`ShaderSystemRhiIntegration`, and
  `Atlantis::AssetSystem` — exactly the dependency list Spec 0013's own
  Requirements fix, and (per that spec's own module-boundary correction)
  **not** `Atlantis::RenderGraph` directly. It contains no `main`/
  `WinMain` and no OS entry point of any kind.
- `atlantis_runtime` contains only the Windows entry point (exact form —
  `WinMain` vs. `main` — a Plan-stage detail matching every existing
  Windows-targeting executable in this codebase) and whatever minimal
  argument handling Spec 0013's own Decisions Requiring Human Review item
  8 allows. It depends on `Atlantis::RuntimeHost` only.
- **`Atlantis::RuntimeHost` is not listed as a dependency of any other
  top-level module, ever, under this decision.** No Renderer, RHI,
  Platform, Asset System, or Shader System code may depend on it — the
  dependency direction runs `atlantis_runtime` → `Atlantis::RuntimeHost` →
  (every other module Runtime needs), the same "leaf executable" shape
  `module_boundaries.md` already describes for Runtime today, merely
  split into two targets for testability rather than one.
- **This split is not an exercise of
  [ADR-0033](0033-runtime-authority-and-client-boundary.md)'s Client-
  boundary concept.** `Atlantis::RuntimeHost`'s consumers are exactly two:
  `atlantis_runtime` itself, and `tests/runtime/`'s own GPU-independent
  unit tests. Neither is a Client in ADR-0033's sense (an external module
  observing/mutating Runtime-owned world state through a query/command/
  event surface) — this ADR introduces no such surface, and does not
  claim to resolve ADR-0033's own deferred packaging-shape question beyond
  the narrow "library vs. pure executable" choice it names explicitly.
- **Runtime depends on `Atlantis::VulkanBackend` directly**, calling
  `vulkan_backend::createDevice()`/`createPresentation()` exactly as every
  existing composition root (`examples/minimal_renderer_demo`,
  `examples/frame_execution_demo`,
  `tests/image_regression/fixture/minimal_cube_fixture.cpp`) already does.
  This is not new coupling and not a backend-selection mechanism: Phase 1
  has exactly one backend, `AGENTS.md`'s Vulkan-header-visibility rule
  restricts only Renderer/RenderGraph/Platform/generic-RHI's own public
  surface (never a composition root, which every one of those existing
  examples already is), and `Atlantis::RuntimeHost`'s own public surface
  (if any is ever needed by its own tests beyond internal linkage) names
  no `Vk*` type.
- **The GPU-independent test boundary is a pure state-machine type, not a
  general dependency-injection or service-locator framework.**
  `RuntimeLifecycleState` and its transition-checking logic depend on
  nothing beyond `Atlantis::Core` (`ATLANTIS_CHECK`/`ATLANTIS_ASSERT`) and
  are directly unit-testable. The real orchestration function that drives
  actual `Platform`/`Device`/`Presentation` calls is not made generically
  mockable, injectable, or pluggable — it calls the real, concrete
  functions Requirements names, by name, exactly as every existing
  composition root already does. No abstract `IPlatform`/`IDevice`
  interface, no service registry, and no plugin/extension point is
  introduced anywhere in `Atlantis::RuntimeHost`.

## Consequences

### Positive

- Makes Runtime's own composition/lifecycle logic unit-testable for the
  first time, closing the one remaining major gap in this codebase's
  otherwise-consistent "GPU-independent logic has GPU-independent tests"
  practice (RenderGraph, Shader System, Asset System all already have it).
- Directly reuses an already-`Accepted`, already-implemented precedent
  (Shader System's `atlantis_shader_compiler_lib`) rather than inventing a
  new pattern — reduces review burden and keeps this decision narrow.
- Leaves `module_boundaries.md`'s eventual corrected Runtime section
  (Spec 0013's own Architectural Impact) able to describe Runtime as
  "still, functionally, one leaf module" — the two-target split is an
  internal implementation detail of that one module, not a restructuring
  of Runtime's own place in the ten-module dependency graph.
- Does not foreclose a future Android entry point: `Atlantis::RuntimeHost`
  is, by construction, per-OS-entry-point-agnostic — a future Android
  Runtime spec could, in principle, link a new `atlantis_runtime_android`
  target against the same `Atlantis::RuntimeHost` library behind its own
  native entry point, without this ADR having designed or promised that
  reuse.

### Negative / Trade-offs

- Two CMake targets instead of one is marginally more build-configuration
  surface for a module that, in Phase 1, still has exactly one real
  consumer of its own library (`atlantis_runtime` itself) beyond its own
  tests — an accepted cost for testability, matching the same trade-off
  Shader System's own precedent already accepted.
- A future reader unfamiliar with this ADR could plausibly misread
  `Atlantis::RuntimeHost`'s existence as an invitation to link against it
  from another module (exactly the kind of "public library other modules
  consume" framing this decision explicitly rejects) — mitigated by
  stating the restriction explicitly here and in Spec 0013's own
  Requirements, but not structurally enforced by any build-system
  mechanism beyond ordinary code review discipline (matching how every
  other "module X must not depend on module Y" rule in this codebase is
  enforced today — by an include-scanning test, not a build-system-level
  guarantee; whether Runtime needs its own such test is a Plan-stage
  detail).
- The pure-state-machine testing boundary genuinely does not cover real
  resource construction/teardown ordering (Spec 0013's own Testing &
  Verification Plan discloses this explicitly) — a real, accepted
  limitation of this decision, not a claim that GPU-independent tests
  alone fully verify Runtime's correctness.

## Alternatives Considered

- **A pure `atlantis_runtime` executable, no library split at all.**
  Rejected — see Spec 0013's own Alternatives Considered: this is the
  status quo shape `module_boundaries.md` currently describes, and it
  would leave Runtime's own lifecycle logic with zero unit-test coverage,
  unlike every other substantial GPU-independent subsystem in this
  codebase.
- **A general, reusable "Runtime Host framework" library other future
  executables (a headless Runtime, a dedicated tool) could configure and
  extend via callbacks/plugins.** Rejected: no second consumer exists yet
  to validate such a framework's shape against, and this is precisely the
  speculative, premature-abstraction risk `AGENTS.md`'s Golden Rule and
  Spec 0013's own Non-Goals name explicitly ("no plugin system, no
  general dependency-injection or service-locator framework").
- **Introduce abstract `IPlatform`/`IDevice`/`IPresentation` interfaces
  purely to make the orchestration function unit-testable with fakes.**
  Rejected: `Atlantis::RHI`'s `Device`/`Presentation` are already abstract
  interfaces (virtual base classes) for backend-independence reasons
  (ADR-0001), but introducing a *second*, Runtime-specific abstraction
  layer on top of them, or an abstract wrapper around `Atlantis::Platform`
  (whose own public API is free functions, not an interface, by design —
  Spec 0002), would be new public API surface with no real second
  implementation to justify it, exactly the pattern Spec 0013's own
  Non-Goals rule out.
- **Test the real orchestration function directly, against a real `Device`,
  as a `gpu`-labeled test rather than splitting out a separate pure state
  machine.** Considered, and adopted as a *complement* rather than a
  replacement: Spec 0013's own Testing & Verification Plan already
  includes GPU-required tests covering the full real composition — this
  ADR's own pure-state-machine boundary exists specifically to cover the
  lifecycle/failure-classification logic that does *not* need a real
  device, at the speed and determinism GPU-independent tests already
  provide throughout this codebase.
