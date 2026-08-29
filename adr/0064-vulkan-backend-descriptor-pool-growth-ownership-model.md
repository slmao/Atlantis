# ADR 0064: Vulkan Backend Descriptor Pool Growth Ownership Model

- **Status:** Accepted
- **Date:** 2026-08-29
- **Deciders:** slmao
- **Related Spec:** [specs/0021-descriptor-pool-capacity-foundation.md](../specs/0021-descriptor-pool-capacity-foundation.md)
  (`Approved`, Human Review Approval recorded 2026-08-29 following one
  centralized final review round that corrected the growth algorithm
  from "always try only the newest pool" to a scan-existing-pools-first
  design, added a complete `VkResult` error-classification table, and
  closed six further clarifications — see that Spec's own "Final Review
  Round" section for the complete, itemized record; this ADR's own
  Decision below reflects the corrected, `Accepted` design directly, not
  the first draft.)

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
   is replaced with a `std::vector<VkDescriptorPool>` (`pools_`), created
   lazily: exactly one pool exists at Device construction, sized
   identically to today's `maxSets = 4` / two-`VkDescriptorPoolSize`-entry
   shape (Spec 0021 D5) — a disclosed, Human-Review-overridable default,
   not a re-derived "big enough" ceiling (the property that was wrong
   before).
2. **Allocation scans every existing pool, in creation order, before
   ever growing — growth triggers only once every existing pool has
   failed for a growth-eligible reason.** `createPipeline()`'s own
   descriptor-set allocation step tries `pools_[0]`, then `pools_[1]`,
   and so on, naturally reusing whatever capacity an earlier
   `vkFreeDescriptorSets` call already returned to any of them — never
   assuming only the most-recently-created pool can have room. Only once
   every existing pool has returned `VK_ERROR_OUT_OF_POOL_MEMORY` or
   `VK_ERROR_FRAGMENTED_POOL` does `VulkanDevice` create one additional
   pool (geometric doubling — each new pool's own `maxSets` equals the
   immediately-preceding pool's own `maxSets`, Spec 0021 D5) and retry
   the allocation exactly once against that new pool. No pool is ever
   created speculatively ahead of real, observed exhaustion of every
   existing pool.
3. **Only `VK_ERROR_OUT_OF_POOL_MEMORY` and `VK_ERROR_FRAGMENTED_POOL`
   are growth-eligible.** Every other `vkAllocateDescriptorSets` failure
   — `VK_ERROR_DEVICE_LOST`, `VK_ERROR_OUT_OF_HOST_MEMORY`,
   `VK_ERROR_OUT_OF_DEVICE_MEMORY` — maps immediately, unchanged, to
   `PipelineCreateError::DescriptorSetAllocationFailed`, with no retry
   against any other pool and no pool creation attempted, identical to
   today's existing behavior for those cases (Spec 0021 D3's own full
   error-classification table).
4. **`VulkanPipeline` records which pool its own one descriptor set
   came from**, still as a non-owning `VkDescriptorPool` field storing a
   **copy of the handle value** — never a pointer or reference into
   `pools_`'s own backing storage, which `std::vector` growth may
   relocate (the existing ownership relationship, extended from "the
   Device's one pool" to "one of the Device's N pools" — not a new
   relationship) — so its own destructor's `vkFreeDescriptorSets` call
   targets the correct pool, preserving the existing
   always-succeeds-under-`FREE_DESCRIPTOR_SET_BIT` contract per pool, and
   restoring that pool's own capacity for the next `createPipeline()`
   call's own scan to find and reuse.
5. **A real, bounded hard ceiling on the total number of pools ever in
   `pools_`** (Spec 0021 D6, a Plan-time-finalized constant, e.g. 4
   pools total) stops growth from masking a genuine resource leak as
   unbounded allocation — exceeding it returns the existing
   `PipelineCreateError::DescriptorSetAllocationFailed`, unchanged in
   shape and meaning. A pool successfully created by a growth attempt
   whose own retried allocation, or a later `createPipeline()` step,
   still fails is kept in `pools_` rather than rolled back (Spec 0021
   D9) — safe, since Phase 1's single-threaded orchestration means
   nothing else could have used it, and an empty, valid pool holds no
   GPU-in-flight reference.
6. **`~VulkanDevice()` destroys every pool in `pools_`**, after the
   existing, unchanged drain sequence
   (`waitAndReleaseRetainedSubmission()` → `vkDeviceWaitIdle()`),
   preserving the exact log-and-continue, never-throw destructor
   discipline already in place. No pool is ever released, reset
   (`vkResetDescriptorPool` is not used anywhere in this design), or
   selectively reclaimed before Device destruction — including on a
   submit or `DeviceLost` failure, which is handled entirely by the
   existing, unmodified `SubmitError` path with zero interaction with
   `pools_`.
7. **No RHI, Renderer, or Material public API changes.** `PipelineCreateError`,
   `CreateMaterialError`, and `MaterialRealizationError` all keep their
   exact current shape — growth changes only what `createPipeline()`
   does *before* ever returning an error, never which error is surfaced
   once genuinely exhausted. `VulkanDevice::descriptorPool()`'s own
   already-dead accessor is either widened to report pool *count* for
   test/diagnostic use, or removed — a Plan-time detail, not an
   architectural one.
8. **No locking, atomics, or multi-threaded allocator design.** `pools_`,
   the scan, and growth all run entirely within Phase 1's existing
   single-threaded frame-orchestration baseline (AGENTS.md Threading
   rules) — no concurrent-access design is introduced or implied.

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
- A real, bounded ceiling (item 5) keeps the fix from silently
  converting a genuine Pipeline-leak defect into unbounded memory growth
  — the failure signal a leak should produce is preserved, just moved
  further out.

### Negative / Trade-offs

- `VulkanDevice`'s own pool-management code grows in complexity (a
  vector of pools, a creation-order scan-then-grow loop) versus
  today's single fixed pool — a real, accepted cost for removing a real
  content ceiling.
- `VulkanPipeline` grows by one non-owning field (which pool it belongs
  to) — a small, mechanical widening of an existing borrow relationship,
  not a new one.
- The hard ceiling (item 5) means an extreme, genuinely-leak-free,
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
