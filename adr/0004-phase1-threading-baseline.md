# ADR 0004: Phase 1 Threading Baseline (Single-Threaded Frame Orchestration)

- **Status:** Accepted
- **Date:** 2026-08-02
- **Deciders:** _Human approval confirmed 2026-08-04_
- **Related Spec:** _none — drafted as part of the architecture-baseline
  documentation task; accepted 2026-08-04 by direct human confirmation
  rather than gated on a preceding formal spec._
  [specs/0002-platform-foundation.md](../specs/0002-platform-foundation.md)
  references this decision in its Threading section.

## Context

Every module's interfaces are shaped, in part, by what concurrency they
need to support. Deciding this per-module, ad hoc, risks two failure
modes: over-designing for concurrency Phase 1 doesn't need (speculative
abstraction, which this task was explicitly told to avoid), or
under-designing in a way that blocks reasonable future extension. Phase
1's own milestone (windowed rendering, per [AGENTS.md](../AGENTS.md))
does not require multi-threaded command recording to work.

## Decision

- Phase 1 assumes a single logical frame thread: Runtime's event loop,
  `Presentation` acquire/present, `RenderGraph` construction, and RHI
  command recording all happen on one thread.
- RHI is not required to be internally thread-safe for Phase 1; the
  Vulkan Backend does not need to guard against concurrent calls into a
  single `Device`/`CommandList`.
- This is a baseline assumption, not a permanent constraint: Phase 1
  interfaces should avoid *precluding* multi-threaded command recording
  later (e.g. prefer a `CommandList` parameter over an implicit global
  one, where that costs nothing now), but building multi-threaded
  recording machinery itself is out of scope until a spec calls for it.

## Consequences

### Positive

- Removes an entire axis of design complexity (thread-safety, contention,
  synchronization of shared RHI state) from every Phase 1 module's first
  version.
- Matches the actual Phase 1 milestone — nothing about shipping windowed
  rendering requires solving multi-threaded submission first.

### Negative / Trade-offs

- If a later spec needs multi-threaded recording, some Phase 1 interfaces
  may still need to change despite the "don't preclude" guidance — that
  guidance reduces but does not eliminate future rework.
- "Don't preclude it later" is a principle applied by human judgment at
  each interface, not a mechanical rule; it will not always be obvious
  in review whether a given signature choice violates it.

## Alternatives Considered

- **Design a full multi-threaded submission model now.** Rejected: no
  Phase 1 milestone needs it, and doing so ahead of a concrete spec is
  the speculative-abstraction failure mode this task was explicitly told
  to avoid.
- **Make no statement at all and let each module decide.** Rejected: this
  is the "settled implicitly by whichever spec gets written first"
  failure mode — exactly what AGENTS.md's Golden Rule exists to prevent.
