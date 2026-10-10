# ADR 0114: Profiling API, Build Configuration and In-Process Recorder

- **Status:** Proposed
- **Date:** 2026-10-10
- **Deciders:** slmao (pending)
- **Acceptance:** Pending (review of Spec 0059's own branch PR).
- **Related Spec:** [Spec 0059: Profiling and Performance Instrumentation Foundation](../specs/0059-profiling-instrumentation.md) (`Draft`)
- **Related ADR(s):**
  - [ADR-0008](0008-logging.md): Core already hosts a deliberate global
    diagnostics facility (the log sink); this ADR adds a second, narrower one.
  - [ADR-0004](0004-phase1-threading-baseline.md): single frame thread, unchanged.
  - Paired with [ADR-0115](0115-rhi-instrumentation-boundary.md),
    [ADR-0116](0116-tracy-profiler-dependency.md) and
    [ADR-0117](0117-benchmark-host-json-schema-and-baseline-comparison.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- Nothing in the frame path is timed or counted (Spec 0059, Motivation).
- Every module that needs a zone depends on Core already; a new module would
  add a dependency edge from RHI, RenderGraph, Renderer, Runtime and the Vulkan
  Backend to carry one header.
- AGENTS.md forbids global mutable engine-state singletons, with a stated,
  narrow exception for logging/diagnostics infrastructure in Core.
- The benchmark needs per-zone, per-pass and per-counter distributions from
  inside the process, headless, with Tracy neither required nor able to supply
  them.
- A default build must not gain a network fetch or change behavior; Android
  must not carry profiling.

## Decision

1. **One public header, `atlantis/profile.h`, in Atlantis Core.**
   - Macros: `ATL_PROFILE_FRAME()`, `ATL_PROFILE_SCOPE(name)`,
     `ATL_PROFILE_GPU_SCOPE(cmd, name)`, `ATL_PROFILE_VALUE(name, value)`
     (per-frame gauge, last write wins) and `ATL_PROFILE_ADD(name, delta)`
     (per-frame accumulator).
   - `name` is a string literal, except `ATL_PROFILE_SCOPE_DYNAMIC(string_view)`
     for names such as RenderGraph pass labels. `cmd` is an `rhi::CommandList&`.
   - The header names no Tracy and no Vulkan type. Spelling follows the
     owner's examples (`ATL_PROFILE_*`); `ATLANTIS_PROFILE_*` is the drop-in
     alternative the owner may rule instead (Spec 0059 Q1).
2. **Compiled out means nothing.** When `ATLANTIS_PROFILING` is off every macro
   expands to `((void)0)`: arguments are not evaluated, no symbol or branch is
   emitted, and no public struct or virtual set depends on the option.
3. **Three CMake options.**

   | Option | Default | Effect |
   |---|---|---|
   | `ATLANTIS_PROFILING` | `OFF` | Macros live; recorder, counters, GPU timestamps. |
   | `ATLANTIS_PROFILING_TRACY` | `ON` (effective only with profiling) | Tracy sink (ADR-0116). |
   | `ATLANTIS_GPU_MARKERS` | `ON` on Windows, `OFF` on Android | Debug-utils names and labels (ADR-0115); independent of profiling. |

   On Android, `ATLANTIS_PROFILING` and `ATLANTIS_GPU_MARKERS` are forced `OFF`
   with a configure message; no Tracy target is declared.
4. **An in-process recorder in Core**, independent of Tracy.
   - It stores, per frame, zone durations (by interned name), counters and
     per-pass GPU times, in fixed-capacity storage; it is active only while a
     consumer (the benchmark driver) has started it, so an inactive recorder
     costs a predictable branch per macro.
   - `ATL_PROFILE_FRAME()` closes the previous frame's record and starts the
     next. A frame that never reaches `Present` is flagged and excluded from
     statistics.
   - Tracy, when enabled, receives the same events as a sink.
5. **The recorder is global diagnostics state, the second instance of the
   Core exception** (after the log sink). It is not a precedent for engine
   state: it holds measurements only, never influences engine behavior, and is
   frame-thread-only (documented in the header, ADR-0004).
6. **Statistics are nearest-rank percentiles** on sorted measured samples
   (p50, p95, p99), with average, min, max and count. Defined once in Core so
   the benchmark and tests share it.

## Consequences

### Positive

- One include for business code; the Tracy dependency is invisible to it.
- Default builds are unchanged: no fetch, no behavior difference.
- The benchmark has its numbers without a GUI; Tracy remains optional.
- RenderDoc support (markers) does not require the profiler.

### Negative / Trade-offs

- A second global in Core, justified narrowly.
- `runFrame()` and other hot functions gain macro lines; their byte-identical
  diff guards become "plus `ATL_PROFILE_*` lines".
- Two timing paths (recorder and Tracy) must agree; tests compare them on a
  fake clock.
- Disabled macros do not evaluate arguments, so an argument with a side effect
  is a bug the review must catch (documented in the header).

## Alternatives Considered

- **A new Atlantis Profiling module** — a new edge into five modules for one
  header.
- **Macros per module** — duplicates the abstraction and the build switch.
- **Tracy-only (no recorder)** — the benchmark would have to post-process a
  Tracy capture with the viewer's tools; not one command line.
- **Instance-threaded profiler object** (no global) — every signature from
  `runFrame()` to the backend's command list would grow a parameter for a
  diagnostics facility; the Core exception exists for exactly this class.
- **One build switch** — cannot offer RenderDoc names without the profiler, or
  Tracy-free benchmark builds.
