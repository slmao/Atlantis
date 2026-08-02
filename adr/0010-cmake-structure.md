# ADR 0010: Initial CMake Target Structure, Namespace Convention, and Example Placement

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _pending human review_
- **Related Spec:** [specs/0001-project-foundation.md](../specs/0001-project-foundation.md)

## Context

`specs/0001-project-foundation.md` needs a CMake target structure for the
initial `Atlantis Core` library, a proof executable, and a test target.
Its Architectural Impact section flags "CMake target/library structure
convention" as requiring its own ADR because — per
[AGENTS.md](../AGENTS.md) module boundaries — many more modules (Platform,
RHI, Vulkan Backend, RenderGraph, Renderer, Shader System, Runtime,
Tools) will be added by later specs, and the structure fixed here sets
the precedent every one of them inherits.
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md)
proposed concrete targets/aliases pending this ADR; this ADR is that
decision, and also resolves two adjacent open questions raised alongside
it: the C++ namespace convention, and where the proof executable should
live.

## Decision

### CMake targets

- `atlantis_core` — static library.
- `atlantis_foundation_demo` — executable (see Example Placement, below,
  for its location).
- `atlantis_core_tests` — executable, registered with CTest.

### Namespaced aliases

Every library target gets a namespaced `ALIAS`, e.g.
`add_library(Atlantis::Core ALIAS atlantis_core)`. Consuming code links
against the alias (`target_link_libraries(... PRIVATE Atlantis::Core)`),
never the raw target name. This convention is fixed now so every future
module — illustratively, `Atlantis::RHI`, `Atlantis::Vulkan`,
`Atlantis::Renderer`, `Atlantis::RenderGraph` (none implemented by this
ADR) — follows the same pattern without renegotiating it per module.

### Include-path hygiene

Each module's public headers live under
`src/<module>/include/atlantis/<public-header>.h`, exposed via
`target_include_directories(... PUBLIC .../include ...)`. Private
implementation code lives under `src/<module>/src/` and is **never**
added to any target's include search path — consumers cannot reach
implementation-private headers even by relative path, because that
directory is structurally absent from their include path, not just
discouraged by convention.

### Convention for future modules

Every future module follows `src/<module_dirname>/{include/atlantis/...,
src/}`, CMake target `atlantis_<module_dirname>`, alias
`Atlantis::<ModuleName>` — e.g. (illustrative only; **none implemented by
this ADR**) `src/rhi/` → `atlantis_rhi` → `Atlantis::RHI`;
`src/renderer/` → `atlantis_renderer` → `Atlantis::Renderer`. This ADR
establishes the pattern via `atlantis_core` alone.

### Namespace convention

C++ code uses a single top-level `namespace atlantis { }`.
**`Atlantis Core`'s types live directly in `atlantis::`, not
`atlantis::core::`.** Rationale: "core" is a module/source-organization
and CMake-target boundary, not a semantic namespace boundary — nesting a
namespace under it would add typing for a module whose contents
(logging, assertions, result types) are meant to read as the engine's own
shared vocabulary, not as belonging to a peer subsystem called "core."
Future modules whose namespace *does* carry semantic meaning nest under
`atlantis::` as already stated in AGENTS.md's C++ coding conventions
(`atlantis::rhi`, `atlantis::rg`, `atlantis::platform`, ...) — this ADR is
the formal decision record backing that already-stated convention and
resolves AGENTS.md's "confirm before the first real module lands" flag,
since `Atlantis Core` is that module.

### Example placement

The proof executable lives at **`examples/foundation_demo/`** — a new
top-level directory, sibling to `src/` and `tests/` — **not**
`src/foundation_demo/` as originally sketched in
[plans/0001-project-foundation.md](../plans/0001-project-foundation.md).
`src/` is reserved for shipping engine modules; once it contains
`src/rhi/`, `src/renderer/`, and similar real modules, a "does the
library link and run" smoke-test program sitting alongside them risks
being mistaken for one. `examples/` is a conventional, unambiguous home
for non-shipping demonstration code. This introduces a new top-level
directory not yet reflected in `README.md`'s repository layout list or
given a placeholder `README.md` matching `src/README.md`/
`tests/README.md`'s pattern — both flagged as necessary follow-up, not
performed by this ADR (no implementation or doc-tree changes are made as
part of drafting it).

## Consequences

### Positive

- A consistent, low-friction pattern every future module follows without
  a fresh CMake-structure debate each time.
- Include-path hygiene is enforced structurally (by directory placement),
  not by developer discipline alone.
- The namespace convention now has a recorded rationale instead of only
  an AGENTS.md aside, and the "confirm before first module" flag is
  resolved.
- Demo/example code can no longer be mistaken for shipping engine code by
  its location alone.

### Negative / Trade-offs

- Introduces a new top-level `examples/` directory; `README.md`'s
  repository layout and a placeholder `examples/README.md` need updating
  to match — not done by this ADR, flagged as follow-up.
- `Atlantis::<Module>` aliasing plus the `include/atlantis/...` layout is
  more CMake boilerplate per module than the simplest possible flat
  structure — accepted as the cost of the hygiene/consistency benefits.
- A single flat `atlantis::` namespace for Core means Core's own names
  must not collide with anything else in `atlantis::`, since there is no
  `core::` wall to prevent it. Mitigated by Core's scope being
  deliberately minimal per the spec's Non-Goals, but a real constraint as
  Core grows.
- `plans/0001-project-foundation.md` currently describes
  `src/foundation_demo/`, not `examples/foundation_demo/` — this ADR's
  Example Placement decision means the plan is now inconsistent with it.
  Per this task's explicit instruction, that plan is **not** modified
  here; the inconsistency is called out in the accompanying report
  instead.

## Alternatives Considered

- **No namespaced aliases; link raw target names (`atlantis_core`)
  everywhere.** Rejected: loses the CMake-community-standard
  `Namespace::Target` self-documentation, and aliases cost nothing to add
  now versus being disruptive to retrofit across many modules later.
- **`atlantis::core` namespace for Core, mirroring every other module
  1:1.** The "obviously consistent" choice; rejected per the Namespace
  Convention rationale above — Core is foundational shared vocabulary,
  not a peer subsystem. If this is judged wrong in review, adopting
  `atlantis::core` instead is a low-cost reversal; flagged explicitly
  rather than silently locked in, per this task's instruction to document
  rather than silently decide if a concrete problem is seen.
- **`src/foundation_demo/`** (the plan's original proposal) — rejected
  per Example Placement above: implies shipping-module status.
  **`sandbox/foundation_demo/`** — rejected: connotes disposable/scratch
  code, which undersells a program that is actually one of this spec's
  acceptance criteria. **`tools/foundation_demo/`** — rejected: `tools/`
  is reserved for Atlantis Tools (offline/dev tooling, a distinct module
  with distinct ownership per
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md));
  folding a library smoke-test into it would blur that boundary.
  `examples/` was judged the best fit of the options considered.
- **Shared/dynamic libraries instead of static for `atlantis_core`.** Not
  evaluated in depth — out of scope given Phase 1 has exactly one library
  target and no cross-module dynamic-linking need yet. Static is simplest
  and reversible later without affecting the namespace/alias/directory
  conventions this ADR fixes.
