# ADR 0003: RHI Resource & RenderTarget Ownership Model (Explicit Ownership, Renderer Borrows)

- **Status:** Proposed
- **Date:** 2026-08-02
- **Deciders:** _pending human review_
- **Related Spec:** _none yet — drafted as part of the architecture-baseline
  documentation task; a formal spec should precede `Accepted` status_

## Context

RHI resources (buffers, textures, `RenderTarget`s) need an ownership
model before any interface can be designed against them, because the
choice affects nearly every RHI method signature. Left undecided, this
gets settled implicitly — by whatever the first piece of code that needs
a resource handle happens to assume — which is precisely the kind of
uncontrolled architectural decision [AGENTS.md](../AGENTS.md) exists to
prevent. It also directly affects the windowed/headless unification in
[ADR-0002](0002-presentation-rendertarget-unification.md): Renderer's
"agnostic to `RenderTarget` origin" property only holds if Renderer never
owns the target it draws into.

## Decision

- RHI resources are explicitly owned by whoever creates them through RHI,
  with RAII-style teardown. There is no hidden global resource cache or
  implicit refcounted pool inside RHI itself in Phase 1.
- `RenderTarget` ownership follows origin: `Presentation` owns
  swapchain-backed `RenderTarget`s (windowed path); the requesting caller
  owns offscreen `RenderTarget`s (headless path).
- Renderer never owns a `RenderTarget`. It receives a non-owning
  reference/view scoped to a single frame's draw and does not retain it
  afterward.
- Resource pooling/suballocation strategy (e.g. GPU memory suballocation)
  is a Vulkan Backend implementation detail beneath this ownership
  contract, not part of it.

This ADR fixes the ownership *model*; it does not fix the concrete C++
handle type (move-only vs. shared-ownership wrapper) — that is deferred
to the RHI spec, see Open Questions in
[resource_lifetime.md](../docs/architecture/resource_lifetime.md).

## Consequences

### Positive

- Renderer's independence from `RenderTarget` provenance (ADR-0002) is
  enforced structurally, not just by convention.
- No hidden caching means resource lifetime is predictable and
  debuggable — a resource is alive exactly as long as its owning handle
  is alive.
- Swapchain resize/recreation stays entirely inside `Presentation`;
  Renderer cannot be holding a stale owned reference because it never
  owns one.

### Negative / Trade-offs

- No built-in resource sharing/caching means any module that wants shared
  ownership (e.g. a texture used by multiple passes) must build that on
  top of RHI explicitly, which is more work per-caller than an implicit
  cache would be.
- The concrete handle type is still an open question (see
  [resource_lifetime.md](../docs/architecture/resource_lifetime.md)),
  so this ADR alone does not fully unblock RHI interface design — a
  follow-up decision (in the RHI spec) is still required.

## Alternatives Considered

- **Implicit refcounted resource cache inside RHI.** Rejected for Phase
  1: adds hidden lifetime behavior and cache-invalidation complexity
  before there's a concrete need for it; can be layered on top later if a
  spec justifies it, without changing the base ownership contract.
- **Renderer owns a persistent `RenderTarget` and RHI mutates it in place
  on resize.** Rejected: this reintroduces exactly the coupling ADR-0002
  is designed to avoid — Renderer would need to know about
  resize/recreation semantics.
