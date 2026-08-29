# ADR 0064: Vulkan Backend Descriptor Pool Growth Ownership Model

- **Status:** Proposed
- **Date:** 2026-08-29
- **Deciders:** slmao
- **Related Spec:** [specs/0021-descriptor-pool-capacity-foundation.md](../specs/0021-descriptor-pool-capacity-foundation.md)

## Context

`VulkanDevice` owns exactly one `VkDescriptorPool`, created once in
`createDevice()` and destroyed once in `~VulkanDevice()`, with a fixed
`maxSets = 4` (`vulkan_device.cpp:1411-1440`). This ceiling was derived
in Plan 0007 Section 10 from one explicit, now-stale assumption —
"exactly one Material exists in steady state" — giving a real peak
concurrent descriptor-set count of 2 during a format-change rebuild,
doubled to `maxSets = 4` as a stated margin. Spec 0018 later introduced
an arbitrary-N-materials model without revisiting this derivation. The
real peak, under Spec 0018 D9's own submit-safe old/new-batch coexistence
design (confirmed unchanged by Spec 0021's own fresh source
investigation), is `2*(N+1)` for N materials plus one fallback — N=1
exactly saturates the historical ceiling; N=2 already exceeds it,
confirmed today by a real GPU regression test
(`tests/runtime/material_realization_gpu_tests.cpp:923-1098`, from
[PR #96](https://github.com/slmao/Atlantis/pull/96)).

`VulkanPipeline` already **borrows** (never owns) its pool handle from
`VulkanDevice` — a non-owning `VkDescriptorPool descriptorPool_` field,
freed back via `vkFreeDescriptorSets` at destruction time, relying on
`VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` (set at pool
creation) to guarantee that free always succeeds. `PipelineCreateError::DescriptorSetAllocationFailed`
is already the sole, isolated signal `vkAllocateDescriptorSets` failure
maps to — never conflated with `toPipelineCreateError()`'s own generic
`PipelineCreationFailed` mapping for the later `vkCreateGraphicsPipelines`
call. `VulkanDevice::descriptorPool()` (a public accessor on the
concrete class only, never part of `atlantis::rhi::Device`'s abstract
interface) has zero callers anywhere in this repository.

A correct fix must decide: where descriptor-pool capacity is owned, how
it grows when exhausted, and whether it is bounded — without changing
`atlantis::rhi`/`atlantis::renderer`'s public surface, and without
disrupting the already-correct submit-safe swap timing Spec 0018 D9
established (`runtime_application.cpp:419-484,766-780`).

## Decision

1. **`VulkanDevice` owns a growable set of descriptor pools, not one
   fixed pool.** Its own `descriptorPool_` member (`VkDescriptorPool`)
   is replaced with a `std::vector<VkDescriptorPool>`, created lazily:
   exactly one pool exists at Device construction, sized identically to
   today's `maxSets = 4` / two-`VkDescriptorPoolSize`-entry shape
   (Spec 0021 D5) — a disclosed, Human-Review-overridable default, not a
   re-derived "big enough" ceiling (the property that was wrong before).
2. **Growth is triggered only by real, observed exhaustion.**
   `createPipeline()`'s own descriptor-set allocation step tries the
   currently-active pool first; only on
   `VK_ERROR_OUT_OF_POOL_MEMORY`/`VK_ERROR_FRAGMENTED_POOL` does it
   create one additional pool (geometric doubling — each new pool's own
   `maxSets` equals the immediately-preceding pool's own `maxSets`,
   Spec 0021 D5) and retry the allocation exactly once against the new
   pool. No pool is ever created speculatively ahead of real demand.
3. **`VulkanPipeline` records which pool its own one descriptor set
   came from**, still as a non-owning `VkDescriptorPool` field (the
   existing ownership relationship, extended from "the Device's one
   pool" to "one of the Device's N pools" — not a new relationship) —
   so its own destructor's `vkFreeDescriptorSets` call targets the
   correct pool, preserving the existing
   always-succeeds-under-`FREE_DESCRIPTOR_SET_BIT` contract per pool.
4. **A real, bounded hard ceiling on the number of pools** (Spec 0021
   D6, a Plan-time-finalized constant) stops growth from masking a
   genuine resource leak as unbounded allocation — exceeding it returns
   the existing `PipelineCreateError::DescriptorSetAllocationFailed`,
   unchanged in shape and meaning.
5. **`~VulkanDevice()` destroys every pool in the set**, after the
   existing, unchanged drain sequence
   (`waitAndReleaseRetainedSubmission()` → `vkDeviceWaitIdle()`),
   preserving the exact log-and-continue, never-throw destructor
   discipline already in place. No pool is ever released before Device
   destruction (no dynamic shrink/reclaim).
6. **No RHI, Renderer, or Material public API changes.** `PipelineCreateError`,
   `CreateMaterialError`, and `MaterialRealizationError` all keep their
   exact current shape — growth changes only what `createPipeline()`
   does *before* ever returning an error, never which error is surfaced
   once genuinely exhausted. `VulkanDevice::descriptorPool()`'s own
   already-dead accessor is either widened to report pool *count* for
   test/diagnostic use, or removed — a Plan-time detail, not an
   architectural one.

## Consequences

### Positive

- Removes a real, currently-reachable content ceiling (any scene with
  two or more distinct materials surviving a color-format change) with
  zero RHI/Renderer/Material public API change, and zero rendered-pixel
  change (no new golden required).
- Extends an already-correct, already-borrowing ownership relationship
  (`VulkanPipeline` borrows a pool handle) mechanically, rather than
  introducing a new one — the smallest change that closes the real gap.
- Preserves every existing safety property this ADR's own Spec
  confirmed by fresh source reading: the submit-safe old/new-batch
  coexistence timing, the isolated `DescriptorSetAllocationFailed`
  signal, and the fail-fast RAII discipline on every `createPipeline()`
  failure path.
- A real, bounded ceiling (item 4) keeps the fix from silently
  converting a genuine Pipeline-leak defect into unbounded memory growth
  — the failure signal a leak should produce is preserved, just moved
  further out.

### Negative / Trade-offs

- `VulkanDevice`'s own pool-management code grows in complexity (a
  vector of pools, an "active pool" notion, a growth/retry loop) versus
  today's single fixed pool — a real, accepted cost for removing a real
  content ceiling.
- `VulkanPipeline` grows by one non-owning field (which pool it belongs
  to) — a small, mechanical widening of an existing borrow relationship,
  not a new one.
- The hard ceiling (item 4) means an extreme, genuinely-leak-free,
  very-high-material-count scene could still be rejected — disclosed as
  a real, accepted limit (Spec 0021 D6), not a silent one; no currently
  planned scene approaches it.
- The first pool's own starting size (item 1) is not re-derived from any
  new, provably-sufficient formula — it is deliberately kept small and
  cheap, with correctness now guaranteed by growth rather than by the
  starting number being big enough. This is a real, disclosed shift in
  what that number *means* (Spec 0021 D5), not a claim that the number
  itself was re-justified from first principles.

## Alternatives Considered

- **A per-`Pipeline`-private descriptor pool.** Rejected: multiplies
  live `VkDescriptorPool` objects with no lifecycle benefit over a
  shared, growable set — `FREE_DESCRIPTOR_SET_BIT` already gives today's
  single pool the same per-set reclaim granularity a per-Pipeline pool
  would provide, at a strictly larger `VulkanPipeline` surface (each
  instance would own and destroy its own pool, not merely free a set
  from a borrowed one) and greater driver-level bookkeeping overhead.
  Disclosed, not hidden: this alternative is simpler to reason about
  per-object, and would be reasonable if Pipeline creation/destruction
  were expected to become very high-frequency — no evidence in this
  codebase's current or planned scope suggests that.
- **A fixed, larger ceiling with no growth logic** (e.g., `maxSets = 32`).
  Rejected: reproduces the exact "assumed sufficient" bet that already
  failed once (Plan 0007's own `maxSets = 4`, invalidated by Spec 0018),
  with no evidence any specific larger number is itself provably
  sufficient for every future scene. Retained in this ADR's own decision
  only as a bound *on growth* (item 4), not as the sole capacity
  mechanism.
- **Widening the RHI's public API** (a new `Device`-level capacity-query
  or reservation call, or a `Renderer`-visible retry signal). Rejected:
  confirmed unnecessary by Spec 0021's own Pre-draft verification — the
  fix is fully achievable underneath the existing, unchanged error-type
  shapes, with zero Material/Runtime-visible behavior change on the
  success path.
- **Bindless descriptor indexing or descriptor buffers.** Rejected as
  disproportionate: a materially larger architectural change (unbounded
  descriptor arrays, shader-side indirection, or a newer, extension-gated
  Vulkan feature) for a real N that stays small and content-bounded —
  see Spec 0021's own Non-Goals for the full reasoning.
