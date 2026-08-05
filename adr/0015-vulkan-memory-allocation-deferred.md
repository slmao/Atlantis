# ADR 0015: Vulkan GPU Memory Allocation Strategy — Deferred, Not Decided

- **Status:** Accepted
- **Date:** 2026-08-06
- **Deciders:** _Human approval confirmed 2026-08-06_
- **Related Spec:** [specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md)

## Context

[AGENTS.md](../AGENTS.md)'s Vulkan-specific rules already flag that "GPU
memory management strategy (VMA vs. a hand-rolled suballocator) is not yet
decided — an open question for the RHI/Vulkan Backend spec, not to be
picked implicitly by whichever code needs an allocation first."
`specs/0003-rhi-vulkan-windowed-foundation.md` is that spec's first
concrete instance, so it must either make this decision or explicitly
record that it is not making it — silence is not an acceptable option per
[AGENTS.md](../AGENTS.md)'s Golden Rule (an uncontrolled decision made
implicitly, by whichever code happens to need an allocation first, is
exactly the failure mode being prevented).

Examining Spec 0003's own scope: it constructs a `Device`, and constructs
and safely destroys a `Presentation`/swapchain, including resize- and
zero-extent-driven recreation bookkeeping (see that spec's Proposed
Design). It performs no image layout transition, command buffer
recording, or GPU command submission of any kind — that is explicitly
deferred to a future RenderGraph-driven frame. None of Spec 0003's actual
scope requires the application to suballocate
`VkDeviceMemory` — swapchain images are allocated and owned by the
presentation engine itself, not through any allocator Atlantis would
provide. The first point that genuinely needs a general memory-allocation
strategy is whichever future spec introduces RHI `Buffer`/`Texture`
resource creation — out of Spec 0003's Non-Goals entirely.

## Decision

**No GPU memory suballocation strategy is chosen by this ADR or by Spec
0003.** This is a deliberate, recorded non-decision, not an oversight or
an omission:

- No code introduced by Spec 0003 (or anything before whichever future
  spec resolves this) may add a dependency on the Vulkan Memory Allocator
  (VMA) library, or write a hand-rolled suballocator, "because it seemed
  obvious" or "to be ready for later."
- No RHI or Vulkan Backend public or private interface introduced by Spec
  0003 may take a shape that presumes one strategy over the other (e.g.
  no method signature designed only for a suballocator-per-pool model, no
  early, unused dependency on the VMA library).
- This is an explicit **implementation blocker**: whichever future spec
  introduces `Buffer`/`Texture` resource creation must resolve this
  question via its own ADR (either adopting this ADR's successor status
  or being superseded by a new one) before any `vkAllocateMemory` call or
  VMA-equivalent allocation call is written anywhere in the codebase.
- This ADR does not preclude Spec 0003 (or its Plan) from needing a
  small, fixed number of individual, unshared `VkDeviceMemory` allocations
  if implementation reveals a genuine need (none is currently expected,
  since Spec 0003 performs no rendering work — but if one turns out to be
  necessary for some narrow piece of `Device`/`Presentation` construction,
  a single direct `vkAllocateMemory`/`vkFreeMemory` pair, with no pooling
  or suballocation policy, is not what this ADR defers — only a *general,
  reusable allocation strategy* is deferred).

## Consequences

### Positive

- Avoids adopting a dependency (VMA) before there is a concrete resource-
  creation consumer to validate the choice against, consistent with
  [AGENTS.md](../AGENTS.md)'s "no speculative abstraction" principle and
  its rule that any new dependency needs its own spec/ADR.
- Prevents the exact failure mode [AGENTS.md](../AGENTS.md)'s Golden Rule
  exists to avoid: this question being settled implicitly by whichever
  future code happens to need its first allocation.
- Keeps Spec 0003 minimal and honest about what it actually needs, rather
  than padding its scope to "future-proof" a decision nothing in it
  requires yet.

### Negative / Trade-offs

- Whichever future spec introduces `Buffer`/`Texture` creation must
  resolve this before making progress on that work — flagged explicitly
  here so it is a known, planned dependency rather than a surprise
  discovered mid-implementation.
- Until resolved, no code anywhere may rely on VMA's conveniences (e.g.
  its allocator-aware `VkBuffer`/`VkImage` creation helpers), even for
  small, seemingly-obvious cases — this ADR intentionally does not carve
  out an exception, to avoid the "just this once" erosion the
  Renderer/RHI backend-independence ADRs already warn about in a
  different context.

## Alternatives Considered

- **Decide VMA now**, since it is the de facto ecosystem-standard choice
  for Vulkan memory management and adopting it early is low-risk.
  Rejected: Spec 0003 does not allocate any suballocated device memory, so
  adopting VMA now would add a dependency with no concrete consumer to
  validate it against — exactly what [AGENTS.md](../AGENTS.md) asks
  agents not to do ("do not scaffold... for later").
- **Decide a hand-rolled suballocator now.** Rejected for the same reason
  as VMA, plus it is strictly more implementation effort than VMA for a
  need that does not yet exist.
- **Say nothing about this question in Spec 0003 or its ADRs at all.**
  Rejected: this is precisely the "settled implicitly by whichever spec or
  code touches it first" failure mode [AGENTS.md](../AGENTS.md)'s Golden
  Rule exists to prevent. An explicit, recorded non-decision — with a
  named future blocker — is materially different from silence, and is
  what this ADR provides.
