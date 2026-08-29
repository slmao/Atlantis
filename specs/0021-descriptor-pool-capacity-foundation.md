# Spec: Descriptor Pool Capacity Foundation

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-08-29
- **Related Plan(s):** [plans/0021-descriptor-pool-capacity-foundation.md](../plans/0021-descriptor-pool-capacity-foundation.md)
  (`Approved / Ready for Implementation`, drafted 2026-08-29, Human
  Review Approval recorded 2026-08-29 following one centralized final
  review round — see the Plan's own "Final Review Round" section).
  **This approval authorizes Implementation of Plan 0021 only once its
  own Implementation PR has merged — not before.**
- **Related ADR(s):** [ADR-0064](../adr/0064-vulkan-backend-descriptor-pool-growth-ownership-model.md)
  (`Accepted`)
- **Human Review Approval (2026-08-29):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-29, accepting this document's
  own "Decisions for Human Review" section in full, per the corrections
  produced during one centralized, final review round (below), and
  accepting [ADR-0064](../adr/0064-vulkan-backend-descriptor-pool-growth-ownership-model.md)
  (`Proposed` → `Accepted`) in the same pass. This approval authorizes
  drafting Plan 0021 only — see the header note above.

## Final Review Round (2026-08-29) — closed findings, recorded before approval

A single, targeted final review round examined the growable-pool design
against real Vulkan Backend code across nine specific areas: pool/set
ownership, growth-and-retry semantics, pool capacity configuration,
`createPipeline()` RAII, format-change/GPU lifecycle interaction, error
classification, single-threading/portability, test boundaries, and
documentation governance. Every item below was closed at the Spec/ADR
level with a real design correction or an explicit clarification —
recorded here so each change is visible, not silently folded in:

1. **The growth algorithm's own "always try only the newest pool, grow
   immediately on its exhaustion" first draft was a real, substantive
   design gap — corrected to a scan-existing-pools-first algorithm.**
   The first draft would have created a new pool on every format change
   whose old batch had not yet been destroyed, even when an *earlier*
   pool already had spare capacity freed by a previously-destroyed
   batch — defeating `FREE_DESCRIPTOR_SET_BIT`'s own reuse capability and
   growing the pool set without bound across a long-running session with
   many resize/format-change events, despite steady-state *concurrent*
   usage staying bounded. Corrected: `VulkanDevice` now tries every
   existing pool, in creation order, before ever growing — D1/D3/D7/D8
   and the "Proposed Design" section are all rewritten around this
   corrected algorithm, and D13 gains a dedicated real-GPU test proving
   reuse actually happens instead of unconditional re-growth.
2. **The specific `VkResult` values that are, and are not, growth-eligible
   were named informally but never given a complete, explicit mapping.**
   D3 now states a full error-classification table:
   `VK_ERROR_OUT_OF_POOL_MEMORY`/`VK_ERROR_FRAGMENTED_POOL` are the only
   growth-eligible outcomes; `VK_ERROR_DEVICE_LOST`,
   `VK_ERROR_OUT_OF_HOST_MEMORY`, and `VK_ERROR_OUT_OF_DEVICE_MEMORY` map
   immediately, unchanged, to the existing `DescriptorSetAllocationFailed`
   error with no retry and no pool creation attempted — matching today's
   existing, already-correct behavior for those cases exactly. D13 gains
   a dedicated GPU-independent unit test for this classification function
   in isolation, since reliably injecting a real `VK_ERROR_DEVICE_LOST`
   from a live Device is not practical.
3. **`VkDescriptorPool` handle storage safety under `std::vector` growth
   was implicit, not stated.** The "Proposed Design" section now states
   explicitly that every stored "which pool" reference — `VulkanDevice`'s
   own `pools_` vector and `VulkanPipeline`'s own new origin-pool field —
   is a *copy of the handle value*, never a pointer or reference into
   `pools_`'s own backing storage, which `std::vector::push_back`'s own
   growth may relocate. This was already the only correct implementation
   shape (a `VkDescriptorPool` is an opaque value, not an addressable C++
   object), but is now a stated implementation constraint for Plan 0021,
   not left to be independently rediscovered.
4. **Whether a mixed uniform-only/textured workload could exhaust one
   descriptor type before `maxSets` itself was asserted, not proven.**
   D4 now includes a direct proof: every Pipeline's descriptor set
   consumes exactly one `UNIFORM_BUFFER` descriptor (the always-present
   binding 0), in strict 1:1 lockstep with `maxSets` itself, so
   `UNIFORM_BUFFER` capacity can never be the first budget exhausted; a
   textured/lit Pipeline additionally consumes one
   `COMBINED_IMAGE_SAMPLER` descriptor at a rate no greater than that
   lockstep rate, so the sampler budget can never be exhausted first
   either, for this codebase's current binding shape.
5. **Whether a newly-grown-but-ultimately-unused pool (a later
   `createPipeline()` step fails after a successful growth+retry) should
   be rolled back or kept was unaddressed.** D9 now states explicitly:
   kept, never rolled back — safe (Phase 1's single-threaded
   orchestration means nothing else could have raced to use it, and an
   empty valid pool holds no GPU-in-flight reference), at the cost only
   of one growth event happening slightly earlier than steady-state
   strictly required. D13 gains a test-coverage note confirming this
   pool is neither leaked nor double-destroyed.
6. **Whether a submit or `DeviceLost` failure could trigger a selective
   pool reset/reclaim was unstated.** D8 now states explicitly: no —
   `vkResetDescriptorPool` is not used anywhere in this design (matching
   today's code exactly), and a pool is destroyed only at `VulkanDevice`'s
   own destruction, never selectively.
7. **Single-threading was implied by the surrounding codebase's own
   baseline but never stated for this design specifically.** D12 now
   states explicitly that `pools_`, the scan, and growth all run entirely
   within Phase 1's existing single-threaded frame-orchestration
   baseline — no locking, no atomics, no concurrent-access design.
8. **A perceived tension with the "no cross-frame descriptor-set caching"
   Non-Goal was identified and resolved by precise wording, not by
   removing the reuse fix.** The Non-Goals section now distinguishes
   explicitly: this Spec never reuses an already-allocated
   `VkDescriptorSet` object across Pipelines (every Pipeline still gets
   its own fresh allocation call, unchanged) — it reuses freed *pool
   capacity* as the target for a brand-new allocation, a genuinely
   different, narrower thing.
9. **D6's own "four pool-growth generations" wording was ambiguous
   (could be read as 4 or 5 total pools).** Corrected to an unambiguous
   statement: the ceiling bounds the total number of pools ever in the
   set (e.g., 4 pools total — the initial pool plus at most 3 growth
   events — matching the `4+8+16+32 = 60`-set figure already given).

No unresolvable architectural conflict was found, and no finding
required changing the RHI/Renderer/Material public API, the Pipeline
ownership model, or introducing any new GPU synchronization mechanism —
every item above was closed within this Spec/ADR's own existing,
Vulkan-Backend-private scope.

## Summary

`VulkanDevice`'s own single, fixed-capacity `VkDescriptorPool`
(`maxSets = 4`) is exhausted by a real, already-shipped, currently-supported
scene shape: two or more distinct Materials surviving a color-format
change. This is not a hypothetical — it is reproduced today by a real GPU
regression test (`tests/runtime/material_realization_gpu_tests.cpp`,
landed on [PR #96](https://github.com/slmao/Atlantis/pull/96) during
Spec 0019's own final review) and was explicitly disclosed, not fixed,
at that time as out of that Spec's own scope. This Spec proposes a
Vulkan-Backend-private, Device-owned, **growable** descriptor pool set
that removes this ceiling for the currently-supported, arbitrary-N-material
model Spec 0018 already introduced — without changing the RHI, Renderer,
or Material public API, and without pre-building bindless/descriptor-indexing
infrastructure this codebase has no present consumer for.

## Motivation / Problem Statement

Plan 0007's own descriptor pool design (Section 10) derived `maxSets = 4`
from one explicit, stated assumption: "exactly one Material exists in
steady state" (that Plan's own Non-Goals). Under that assumption, the true
peak concurrent descriptor-set count a format-change rebuild can ever
produce is 2 (one old Pipeline's set, momentarily alive alongside one new
one) — `maxSets = 4` is exactly double that peak, a stated, accounted-for
margin.

Spec 0018 (Material Asset & Scene Binding Foundation) later introduced an
arbitrary-N-materials model (`materialResourceMap_`, keyed by `AssetId`)
— lifting the "exactly one Material" assumption Plan 0007's own capacity
derivation entirely depends on — **without ever revisiting the pool's own
fixed capacity.** Spec 0018 D9 also fixed the format-change lifecycle
shape that makes the real peak formula `2*(N+1)` (N materials + 1
fallback, old batch and new candidate batch both alive from the moment
`rebuildMaterialsForFormatChange()` begins until the frame's own
`submit()` call returns `Ok`, per that Spec's own submit-safe-swap
design, confirmed unchanged and correctly implemented by this Spec's own
fresh source investigation below). N=1 exactly saturates the historical
ceiling (`2*2=4`); **N=2 already exceeds it (`2*3=6 > 4`)**, independent
of `MaterialKind` — a real, currently-reachable content shape (any scene
with two or more distinct textured materials, undergoing a real
resize-driven or swapchain-recreation-driven color-format change),
confirmed exhausted by direct GPU testing (below).

This is a real, silent regression risk for content, not a corner case:
nothing in Spec 0018's own Approved scope, nor in Runtime's own public
behavior, limits a scene to fewer than two materials — the two-material
ceiling exists only inside `vulkan_device.cpp`'s own private pool-creation
code, invisible to Spec/Plan authors and scene authors alike until a
format change actually occurs at runtime.

## Goals

1. Remove the fixed `maxSets = 4` ceiling's constraint on the
   already-supported, arbitrary-N-material format-change rebuild shape
   Spec 0018 introduced — a scene with N ≥ 2 distinct materials must
   survive a real color-format change without a descriptor-set allocation
   failure, for any N a real, content-driven scene can produce within a
   provable, tested, non-hardcoded bound.
2. Keep the fix fully contained within the Vulkan Backend's own private
   implementation (`VulkanDevice`/`VulkanPipeline`) wherever the real,
   evidence-grounded lifecycle permits it — no RHI, Renderer, or Material
   public API change, unless this Spec's own investigation finds that
   impossible (see "Architectural Impact" and D11 below: it does not).
3. Preserve every existing, already-correct safety property this Spec's
   own fresh investigation confirmed: the submit-safe old/new-batch
   coexistence timing (Spec 0018 D9), the fail-fast RAII discipline on
   every `createPipeline()` failure path, and the existing, isolated
   `PipelineCreateError::DescriptorSetAllocationFailed` signal — all
   unchanged in shape, not merely "not regressed."
4. Replace the fixed ceiling with a real, justified, tested growth and
   sizing strategy — never a second unexplained magic constant standing
   in for the first.

## Non-Goals

- **Bindless rendering or descriptor indexing** (`VK_EXT_descriptor_indexing`
  or core 1.2 descriptor indexing) — a materially larger architectural
  change (unbounded/update-after-bind descriptor arrays, shader-side
  indirection) with no current consumer; this Spec's own N is small and
  content-bounded, not the "thousands of materials" scale that
  justifies bindless.
- **Descriptor buffers** (`VK_EXT_descriptor_buffer`) — a newer, more
  invasive extension-gated model; same reasoning as above, and a real
  portability risk this Spec's own D12 explicitly avoids taking on for a
  problem growth already solves.
- **A cross-frame descriptor-*set* caching/reuse system** — this Spec
  does not attempt to avoid re-allocating a descriptor *set* for a
  rebuilt Pipeline (every Pipeline still gets its own fresh
  `vkAllocateDescriptorSets` call, unchanged); it only ensures the *pool*
  underneath can satisfy however many concurrent set allocations the
  existing, unchanged format-change design already requires. **Distinct,
  and not a contradiction of this Non-Goal:** D3/D7's own reuse-before-growth
  scan reuses freed *pool capacity* (an empty slot a destroyed Pipeline's
  own earlier `vkFreeDescriptorSets` call already returned) as the
  target for a brand-new set allocation — it never reuses an
  already-allocated `VkDescriptorSet` object itself across Pipelines.
- **A general-purpose GPU memory/descriptor allocator** — the fix stays
  scoped to exactly the one pool `VulkanDevice` already owns; it does not
  generalize into a reusable allocator abstraction for other GPU resource
  kinds (`VkBuffer`/`VkImage` allocation is unaffected and out of scope).
- **A Material Graph, shader graph, or any user-composable shading
  system.**
- **PBR, Shadow Foundation, or any other rendering-feature Spec** — this
  is a pure capacity/lifecycle fix; it changes zero rendered pixels and
  requires zero new golden.
- **Android implementation** — this Spec is Vulkan-Backend-private and
  Windows-verified only, per this codebase's current Phase 1 scope;
  D12 states the portability reasoning for a future Android backend
  without implementing it.
- **Any uniform-only vs. combined-image-sampler descriptor-type budget
  split** (see D4) — this round keeps the existing, symmetric
  (`maxSets`-sized) allocation for both descriptor types.
- **Any change to Spec 0018's own format-change lifecycle, timing, or
  submit-safe swap discipline** — confirmed correct and unmodified by
  this Spec (see "Pre-draft verification" below); the fix operates
  entirely underneath that unchanged contract.
- **Any dynamic pool shrink/reclaim-to-OS strategy** — once grown, a pool
  is never released before `VulkanDevice`'s own destruction, matching the
  existing single-pool precedent exactly (created once, lives for the
  Device's full lifetime).

## Requirements

### Functional

- `VulkanDevice::createPipeline()` must succeed for a real, GPU-verified
  N ≥ 2 (at minimum, the exact scenario PR #96's own regression test
  already reproduces failing today: fallback + one `UnlitTextured` + one
  `LitTextured` material, old batch and new candidate batch both alive
  during a real color-format change) without any RHI/Renderer/Material
  public API change.
- `VulkanDevice` must try `vkAllocateDescriptorSets` against its own
  existing pools **in a deterministic, fixed order (creation order)**,
  reusing whatever capacity a pool has already reclaimed via an earlier
  `vkFreeDescriptorSets` call — never assuming only the most-recently-
  created pool can ever have room. Only once **every** existing pool has
  been tried and every attempt returned `VK_ERROR_OUT_OF_POOL_MEMORY` or
  `VK_ERROR_FRAGMENTED_POOL` does `VulkanDevice` attempt to grow (create
  one additional pool, per D5's own sizing strategy) and retry the
  allocation against that newly created pool before surfacing
  `PipelineCreateError::DescriptorSetAllocationFailed` — that error
  remains reachable, unchanged in meaning, once a real, bounded ceiling
  (D6) is hit. Any `vkAllocateDescriptorSets` failure **other than** those
  two specific `VkResult` values (`VK_ERROR_DEVICE_LOST`,
  `VK_ERROR_OUT_OF_HOST_MEMORY`, `VK_ERROR_OUT_OF_DEVICE_MEMORY`) is
  **not** growth-eligible — it maps directly, immediately, to
  `DescriptorSetAllocationFailed`, with no retry against any other pool
  and no new pool created, identical to today's existing, unchanged
  behavior for those cases (D3's own "Error classification" table gives
  the complete mapping).
- Every `VulkanPipeline` must free its own one descriptor set back to the
  exact pool it was allocated from (never merely "the Device's first
  pool") — the existing `vkFreeDescriptorSets`
  always-succeeds-under-`FREE_DESCRIPTOR_SET_BIT` contract that today's
  single-pool code already relies on continues to hold for every pool in
  the growable set.
- `VulkanDevice::~VulkanDevice()` must destroy every pool it ever created,
  after the existing drain sequence (`waitAndReleaseRetainedSubmission()`
  → `vkDeviceWaitIdle()`), preserving the exact log-and-continue,
  never-throw discipline the current single-pool destructor already uses.

### Non-functional

- **Performance:** a growth event (a new `vkCreateDescriptorPool` call) is
  expected to be rare — at most once per session for every currently
  shipped scene (see D5) — and never occurs on the steady-state path once
  a scene's own material count stabilizes; ordinary per-frame rendering
  performs zero pool-related work, unchanged from today.
- **Memory:** capacity grows only in response to real, observed
  exhaustion — never pre-allocated speculatively — and is bounded by a
  real, tested, disclosed hard ceiling (D6), never unbounded.
- **Portability (within the Vulkan-only Phase 1 constraint):** the fix
  uses only core Vulkan 1.0 API surface already in use elsewhere in this
  file (`vkCreateDescriptorPool`, `vkAllocateDescriptorSets`,
  `vkFreeDescriptorSets`, `vkDestroyDescriptorPool`) — no new extension,
  no new instance/device feature requirement (D12).
- **Other:** zero new third-party dependency; zero change to the
  `Atlantis::VulkanBackend`/`Atlantis::RHI`/`Atlantis::Renderer` module
  link graph.

## Pre-draft verification against real, current source

Confirmed directly against `main` at `91f9855` (the real merge commit of
[PR #96](https://github.com/slmao/Atlantis/pull/96)) at Spec-drafting
time, by reading full files, **not** by treating that PR's own review
conclusions as a trusted premise:

- **Pool creation** (`vulkan_device.cpp:1411-1440`, inside `createDevice()`):
  a single `VkDescriptorPool`, `maxSets = 4`, two `VkDescriptorPoolSize`
  entries (`{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4}`,
  `{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4}` — the second added by
  Spec 0016 D5, sized identically to the first, confirmed against the
  live comment there), `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`
  set. On `vkCreateDescriptorPool` failure, maps to
  `DeviceCreateError::DeviceCreationFailed` (a Device-construction-time
  failure, not reachable at runtime once the Device exists) — confirmed
  distinct from the runtime exhaustion path this Spec fixes.
- **Pipeline creation and descriptor-set allocation**
  (`VulkanDevice::createPipeline()`, `vulkan_device.cpp:826-1054`):
  exactly one `VkDescriptorSet` allocated per Pipeline
  (`vkAllocateDescriptorSets` with `descriptorSetCount = 1`) from
  `descriptorPool_`; failure maps directly and solely to
  `PipelineCreateError::DescriptorSetAllocationFailed`. A complete,
  leak-free RAII cleanup chain exists across all five of this function's
  own distinct failure paths (two shader-module creations,
  descriptor-set-layout creation, descriptor-set allocation,
  pipeline-layout creation, final `vkCreateGraphicsPipelines`) — each
  destroys, in reverse order, every resource already successfully
  created before it. `toPipelineCreateError()` (`vulkan_result.cpp:119-122`)
  maps every non-success `VkResult` from the final
  `vkCreateGraphicsPipelines` call to one generic
  `PipelineCreateError::PipelineCreationFailed` — confirming
  `DescriptorSetAllocationFailed` is the sole, isolated,
  never-conflated signal for descriptor-pool exhaustion specifically.
- **Pipeline destruction** (`VulkanPipeline::~VulkanPipeline()`,
  `vulkan_pipeline.cpp:17-31`, and `vulkan_pipeline.h:41-46`): the exact
  destruction order comment cites "Plan 0007 Section 10" —
  `vkDestroyPipeline` → `vkFreeDescriptorSets` (this Pipeline's own one
  set, freed back into `VulkanDevice`'s own pool) →
  `vkDestroyDescriptorSetLayout` → `vkDestroyPipelineLayout`.
  `descriptorPool_` is stored as a **non-owning** `VkDescriptorPool`
  field (header comment: "non-owning; `VulkanDevice`'s pool, must
  outlive this object") — `VulkanPipeline` **borrows**, never owns, the
  pool handle it frees back into. `vkFreeDescriptorSets`'s own result is
  captured and relied on to "only ever return `VK_SUCCESS`" — true
  specifically because `FREE_DESCRIPTOR_SET_BIT` was set at pool
  creation, confirmed above.
- **Device destruction** (`~VulkanDevice()`, `vulkan_device.cpp:423-456`):
  an unconditional, defensive drain
  (`waitAndReleaseRetainedSubmission()` then `vkDeviceWaitIdle()`, each
  logged-and-swallowed on failure, never thrown from a destructor)
  precedes `vkDestroyDescriptorPool`, itself ordered before
  `vkDestroyDevice` — matching the existing `VkCommandPool`/`VkFence`
  teardown precedent exactly. The real caller discipline this teardown
  depends on (every `VulkanPipeline` already destroyed before
  `VulkanDevice` is) is asserted only by comment, not by code — an
  existing, pre-this-Spec invariant, unchanged by this Spec.
- **Retained-submission/fence safety point** (`VulkanDevice::submit()`
  and `waitAndReleaseRetainedSubmission()`, `vulkan_device.cpp:458-566`):
  every `submit()` call first drains the *previous* frame's retained
  submission (`vkWaitForFences` + `vkResetFences`) before recording the
  new one — confirming the exact point at which a *previous* frame's GPU
  work is provably finished, which Spec 0018 D9's own swap-only-after-
  `submit()`-succeeds design (below) depends on and this Spec does not
  alter.
- **Format-change old/new batch coexistence lifecycle**
  (`material_realization.cpp:277-332` `rebuildMaterialsForFormatChange()`,
  and `runtime_application.cpp:419-484` and `:766-780`): confirmed, by
  direct reading, that a complete new candidate batch (one fallback
  Pipeline plus one rebuilt Pipeline per currently-realized material) is
  built in full **while the caller's own existing bundle
  (`fallbackMaterial_`/`materialResourceMap_`) is read-only, never
  mutated, moved, or destroyed** — and that the actual swap-and-destroy
  of the OLD bundle happens **only after** that same frame's own
  `submit()` call has returned `Ok` (`runtime_application.cpp:776-780`),
  which is the exact point `waitAndReleaseRetainedSubmission()` has
  already confirmed the *previous* frame's GPU work has finished. This
  means the true peak concurrent descriptor-set count during a
  format-change window, for N already-realized materials plus one
  fallback, is real and reproducible: the OLD fallback + N OLD materials
  (still alive, unmodified) coexist with the NEW fallback + N NEW
  materials (freshly built) for the full duration of
  `rebuildMaterialsForFormatChange()`'s own call plus that frame's own
  `submit()` — `2*(N+1)` concurrent sets, exactly matching Plan 0007
  Section 10's own doubling-of-peak-2 derivation, now applied to Spec
  0018's own real N.
- **The N=2 GPU regression test**
  (`tests/runtime/material_realization_gpu_tests.cpp:923-1098`, read in
  full): contains (1) a low-level, kind-independent
  `Device::createPipeline()` loop proving the 5th of 6 attempted
  Pipelines fails with exactly `PipelineCreateError::DescriptorSetAllocationFailed`
  (the 1st through 4th succeed, consuming the pool's entire declared
  capacity, `maxSets = 4`); and (2) the real-shape regression case
  (fallback + one `UnlitTextured` + one `LitTextured` material, realized
  in "Frame 1," then format-changed in "Frame 2" with the OLD 2-material
  batch still alive while `rebuildMaterialsForFormatChange()` builds a
  NEW fallback+2-material candidate batch — 2 old + 3 new = 5 > 4) —
  asserting `Err(MaterialRealizationError::MaterialCreateFailed)`, with
  an explicit comment instructing that this assertion should be
  **reversed to `isOk()`**, not merely deleted, once a real capacity fix
  lands (D14, below) — this Spec's own recommendation follows that
  comment's own instruction exactly.
- **Error-type shape, confirmed unchanged and load-bearing for this
  Spec's own "no RHI/Material public API change" claim:**
  `PipelineCreateError` (`rhi/types.h:254-262`) — five enumerators,
  `DescriptorSetAllocationFailed` third, with an explicit comment naming
  it as the distinct signal for pool-exhaustion, never folded into
  `PipelineCreationFailed`. `CreateMaterialError`
  (`renderer/material.h:56-58`) — exactly **one** enumerator,
  `PipelineCreationFailed`, folding every `PipelineCreateError`
  sub-reason (including pool exhaustion) into one value.
  `MaterialRealizationError` (`runtime/material_realization.h:36-41`) —
  four enumerators, `MaterialCreateFailed` the relevant one, folding the
  Material-level failure identically. Because this Spec's own fix only
  changes what `createPipeline()` does **before** ever returning an
  error (attempt growth, retry), and changes nothing about which error
  is surfaced when allocation is still exhausted after growth is
  exhausted (D6), **none of these three enumerations needs a new
  member or a shape change** for this Spec's own Goals to be met.
- **`VulkanDevice`'s own `descriptorPool()` public accessor**
  (`vulkan_device.h:135`): confirmed, by a repository-wide search, to
  have **zero callers anywhere** — not from `vulkan_backend`'s own other
  source files, not from any test, not from `rhi`/`renderer`/`runtime`.
  It is dead, vestigial surface on the concrete `VulkanDevice` class
  only — never part of the abstract `atlantis::rhi::Device` interface —
  directly supporting D11's "no RHI public API is touched" claim; this
  Spec's own eventual Plan should evaluate widening it to expose pool
  *count* (for test/diagnostic use only) or removing it, a Plan-time
  detail, not an architectural one.

No unresolvable architectural conflict was found. The real, evidence-grounded
recommendation below is fully containable inside `vulkan_backend`'s own
private implementation.

## Proposed Design

### The growable pool set, end to end

```
VulkanDevice::createDevice()
  creates ONE VkDescriptorPool, sized identically to today's existing
  maxSets = 4 / two-pool-size-entry shape (D5) -- unchanged starting
  point, not a new number. Appended to VulkanDevice's own
  std::vector<VkDescriptorPool> pools_.
        |
        v
VulkanDevice::createPipeline() -- descriptor-set allocation step
  for each existing pool in pools_, IN CREATION ORDER (index 0..last):
    try vkAllocateDescriptorSets() against it
        |
        +-- VK_SUCCESS --------------------------------------> proceed,
        |    (this may be pools_[0] again, if an earlier Pipeline that
        |    once held one of its sets has since been destroyed and
        |    freed that capacity back -- reuse, not merely "the newest
        |    pool", is the normal, expected path)
        |
        +-- VK_ERROR_OUT_OF_POOL_MEMORY / VK_ERROR_FRAGMENTED_POOL
        |    -> try the NEXT existing pool in pools_; only once every
        |       existing pool has failed this way does growth trigger
        |       (below)
        |
        +-- any OTHER VkResult (VK_ERROR_DEVICE_LOST,
             VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY)
             --> Err(PipelineCreateError::DescriptorSetAllocationFailed)
                  IMMEDIATELY -- no further pool tried, no growth
                  attempted, identical to today's existing, unchanged
                  behavior for these cases (D3's own error-
                  classification table)
        |
        v  (every existing pool tried, every one returned
        |   OUT_OF_POOL_MEMORY/FRAGMENTED_POOL)
        v
        has the hard ceiling (D6) already been reached?
              |
              +-- yes --> Err(PipelineCreateError::DescriptorSetAllocationFailed)
              |            -- unchanged shape, unchanged meaning
              |
              +-- no --> vkCreateDescriptorPool() ONE new pool, sized
                          per D5's own geometric growth rule; on
                          success, appended to pools_; retry
                          vkAllocateDescriptorSets() against this one
                          new pool exactly once
                               |
                               +-- succeeds --> proceed, VulkanPipeline
                               |                 records WHICH pool
                               |                 (by handle VALUE, a
                               |                 copy -- never a
                               |                 pointer/reference into
                               |                 pools_'s own storage,
                               |                 which std::vector
                               |                 growth may relocate)
                               |                 its own set came from
                               |
                               +-- fails again, or pool creation itself
                                    fails --> Err(DescriptorSetAllocationFailed);
                                    a new pool that WAS successfully
                                    created but whose retry then failed
                                    for an unrelated reason (see D9) is
                                    kept in pools_, not rolled back --
                                    safe, since no set was ever
                                    allocated from it; identical RAII
                                    cleanup, for every OTHER
                                    already-created resource this call
                                    made, as every other
                                    createPipeline() failure path
        |
        v
VulkanPipeline::~VulkanPipeline()
  vkFreeDescriptorSets() against the SPECIFIC pool handle VALUE this
  Pipeline's own set was allocated from (not necessarily pools_[0]) --
  unchanged FREE_DESCRIPTOR_SET_BIT contract, per-pool -- restoring that
  pool's own capacity for the NEXT createPipeline() call's own
  creation-order scan to find and reuse
        |
        v
VulkanDevice::~VulkanDevice()
  after the existing drain sequence, vkDestroyDescriptorPool() every
  pool in pools_ -- unchanged log-and-continue discipline
```

**Handle-value safety, stated explicitly:** `VkDescriptorPool` is an
opaque Vulkan handle *value* (a pointer-sized integral/pointer type on
every platform Vulkan defines it for), not a C++ object with its own
address inside `pools_`. Every place this design stores "which pool" —
`VulkanDevice`'s own `pools_` vector, and `VulkanPipeline`'s own new
field recording its origin pool — stores a **copy of the handle value
itself**, never a `VkDescriptorPool*`/reference into `pools_`'s own
internal storage. This is required, not merely convenient:
`std::vector::push_back` (the growth operation itself) may reallocate
its own backing storage, which would invalidate any pointer or reference
into a previously-inserted element — a handle-by-value copy is entirely
unaffected by that reallocation, since it is just an opaque integer/
pointer value identifying a driver-side object, not an address into
`pools_`'s own memory. This Spec's own eventual Plan must state this
as an explicit implementation constraint: no `VkDescriptorPool*` or
`VkDescriptorPool&` is ever taken from `pools_` and stored anywhere
beyond the lifetime of one `createPipeline()` call's own local scope.

## Architectural Impact

**Yes** — this Spec changes `VulkanDevice`'s own descriptor-pool
ownership model, from "exactly one `VkDescriptorPool`, created once, for
the Device's full lifetime" to "a Device-owned, growable *set* of pools,
created on demand, each living for the Device's full lifetime once
created." This is real, if narrowly scoped, resource-ownership-model
architecture — [ADR-0064](../adr/0064-vulkan-backend-descriptor-pool-growth-ownership-model.md)
(`Accepted`) records the full decision, alternatives, and trade-offs,
per this codebase's convention (matching ADR-0061/ADR-0062's own scope
for Spec 0019). No RHI, Renderer, or Material public API, dependency,
threading model, or backend-abstraction contract is touched — confirmed
by the "Pre-draft verification" section above, not merely asserted.

## Alternatives Considered

- **A per-`Pipeline`-private descriptor pool** (each `VulkanPipeline`
  creates and owns its own one-set pool). Rejected: multiplies the
  number of live `VkDescriptorPool` objects with no lifecycle benefit
  over a shared, growable set — `FREE_DESCRIPTOR_SET_BIT` already lets
  today's single pool reclaim capacity per-set, the exact granularity a
  per-Pipeline pool would also provide, at the cost of N times the
  driver-level pool bookkeeping and a strictly larger `VulkanPipeline`
  surface (each instance would need to track and destroy its own pool,
  not merely free a set from a borrowed one). Disclosed in full, not
  hidden to favor the recommended option: this alternative is simpler to
  reason about per-object and would be a legitimate choice if this
  codebase's own Pipeline creation/destruction rate were expected to
  become very high-frequency; nothing in this codebase's current or
  planned scope suggests that.
- **A fixed, larger ceiling** (e.g., `maxSets = 32`, or some other bigger
  constant, with no growth logic). Rejected on the merits this Spec's own
  Human Review item 5 explicitly names: any fixed number is exactly the
  same kind of "assumed sufficient" bet Plan 0007's own `maxSets = 4`
  already was, proven wrong once by Spec 0018's own scope change, with no
  evidence any specific bigger number is itself "provably sufficient" for
  every future scene. A fixed ceiling is retained in this Spec's own
  recommendation, but as a **safety bound on growth** (D6), not as the
  sole capacity mechanism.
- **Widening the RHI's public error/capability surface** (e.g., a new
  `Device::reserveDescriptorCapacity()` call, or a `Renderer`-visible
  "pool exhausted, retry" signal). Rejected for this Spec's own Goal 2:
  the "Pre-draft verification" section above confirms the fix is fully
  achievable underneath the existing `PipelineCreateError`/
  `CreateMaterialError`/`MaterialRealizationError` shapes, with zero
  Material/Runtime-visible behavior change on the success path (a
  format-change rebuild simply succeeds where it previously failed) and
  an unchanged, understood failure shape on the (now much rarer, bounded)
  failure path.
- **Bindless / descriptor indexing** — see Non-Goals; rejected as
  disproportionate to this Spec's own real, currently-small N, and a
  materially larger portability/verification surface than this fix
  needs.

## Decisions for Human Review

Fifteen items, matching the human-directed drafting brief for this Spec
exactly, one to one.

### D1. Descriptor pool ownership model

**Recommendation:** a Device-managed, growable **set** of
`VkDescriptorPool` objects — `VulkanDevice` owns `std::vector<VkDescriptorPool>`
(replacing today's single `VkDescriptorPool descriptorPool_` member),
starting with exactly one pool at Device creation (sized identically to
today, D5). Allocation tries every existing pool **in creation order**,
naturally reusing capacity an earlier `vkFreeDescriptorSets` call already
returned to any of them (D3) — growth (appending one additional pool)
triggers only once **every** existing pool has been tried and failed
with `VK_ERROR_OUT_OF_POOL_MEMORY`/`VK_ERROR_FRAGMENTED_POOL`, up to a
real, tested, disclosed hard ceiling (D6). `VulkanPipeline` gains a
second, still non-owning `VkDescriptorPool` field — storing a **copy of
the handle value**, never a pointer/reference into `pools_`'s own
storage (see "Proposed Design"'s own "Handle-value safety" note; a
`std::vector`'s own growth may reallocate, which would invalidate any
such pointer) — recording *which* pool in the set its own one descriptor
set was allocated from (needed so its own destructor frees back to the
correct pool — see "Alternatives Considered" above for why per-Pipeline
ownership was rejected, and D2 below for why this specific model suits
the current lifecycle).

### D2. Why this model suits the current Pipeline/Material/format-change lifecycle

**Recommendation, evidence-grounded:** `VulkanPipeline` already borrows
(never owns) a pool handle from `VulkanDevice` today — extending "borrows
the Device's one pool handle" to "borrows one of the Device's N pool
handles" is a minimal, mechanical extension of an already-correct
relationship, not a new ownership concept. Spec 0018 D9's own
submit-safe-swap format-change design already requires a shared pool
capable of holding both an old and a new batch's worth of descriptor sets
simultaneously (confirmed, "Pre-draft verification" above) — growth
serves exactly this coexistence window without requiring any new
synchronization, timing, or lifetime decision beyond what Spec 0018
already specified and this Spec's own investigation confirmed unchanged.
A per-Pipeline pool (rejected above) would still need its own answer to
"how big," reproducing this same open question at a smaller, per-object
granularity instead of resolving it once at the Device level.

### D3. Pool exhaustion: scan-existing-then-grow-and-retry, not immediate error — full error classification

**Recommendation:** try every existing pool, in creation order, before
ever growing; grow (create one new pool) and retry exactly once only
after every existing pool has failed for a growth-eligible reason; fall
back to the existing `DescriptorSetAllocationFailed` error path
unchanged for every other case. See the "Proposed Design" flow above for
the full, ordered algorithm. **Complete error classification, every real
`vkAllocateDescriptorSets`/`vkCreateDescriptorPool` outcome this design
touches, mapped explicitly (never left implicit):**

| Real outcome | Growth-eligible? | Resulting behavior |
|---|---|---|
| `vkAllocateDescriptorSets` against an existing pool returns `VK_SUCCESS` | — | Proceed; that pool (any index, not only the most recently created) is this Pipeline's own origin pool. |
| `vkAllocateDescriptorSets` against an existing pool returns `VK_ERROR_OUT_OF_POOL_MEMORY` or `VK_ERROR_FRAGMENTED_POOL` | Yes | Try the next existing pool (if any); once every existing pool has failed this way, attempt growth (below). |
| `vkAllocateDescriptorSets` against an existing pool returns `VK_ERROR_DEVICE_LOST`, `VK_ERROR_OUT_OF_HOST_MEMORY`, or `VK_ERROR_OUT_OF_DEVICE_MEMORY` | **No** | Immediately `Err(PipelineCreateError::DescriptorSetAllocationFailed)` — no further pool tried, no growth attempted; identical to today's existing, unchanged behavior for these cases (today's code already maps every non-success `VkResult` here to this same enumerator uniformly — this Spec narrows *when a retry is attempted*, never *which error is ultimately surfaced*). |
| Hard ceiling (D6) already reached, every existing pool exhausted for a growth-eligible reason | — | `Err(DescriptorSetAllocationFailed)` — unchanged shape, unchanged meaning. |
| `vkCreateDescriptorPool` (the growth attempt itself) fails | — | `Err(DescriptorSetAllocationFailed)`; the failed-to-create pool is never appended to `pools_` (nothing to roll back). |
| `vkAllocateDescriptorSets` against a freshly-grown pool still fails (any `VkResult`) | — | `Err(DescriptorSetAllocationFailed)`, exactly once, no second growth attempt in the same `createPipeline()` call; the newly-created pool is kept in `pools_` regardless (D9 — safe, since it never had any set allocated from it). |

The existing error path is preserved unchanged, in shape and in every
case that already reaches it today, as the genuine exhaustion signal —
never removed, never widened to a new enumerator (D11).

### D4. Uniform-only vs. textured/lit descriptor-type capacity budgets

**Recommendation:** keep the existing, symmetric shape — every pool's own
two `VkDescriptorPoolSize` entries (`UNIFORM_BUFFER`, `COMBINED_IMAGE_SAMPLER`)
remain sized identically to that pool's own `maxSets`, matching Spec
0016 D5's own existing precedent exactly. **Not** split into a separate
uniform-only budget this round: every currently-shipped Material
allocates from the same pool regardless of whether it uses the sampler
binding (the fallback Material's own Pipeline has no sampler binding at
all, per `hasSampledTextureBinding`, yet still allocates one set from the
combined pool) — no currently-shipped or currently-planned workload is
skewed enough toward one descriptor type to justify the added complexity
of two separately-sized budgets. **Disclosed trade-off:** an
all-uniform-only-Pipeline scene still "reserves" unused
`COMBINED_IMAGE_SAMPLER` capacity per set — an accepted, minor
inefficiency, not a correctness risk, and reversible later without any
API change if a real workload ever demonstrates it matters.

**Proven, not merely assumed, that no mixed workload can exhaust one
descriptor type before `maxSets` itself is reached:** every Pipeline's
own descriptor set — fallback or Material-bound, uniform-only or
textured/lit — consumes exactly one `UNIFORM_BUFFER` descriptor (the
always-present binding 0) and counts exactly once against `maxSets`; the
two are therefore consumed in strict lockstep, 1:1, for every allocation
this codebase's own current binding shape can produce — `UNIFORM_BUFFER`
capacity can never be exhausted separately from (before or after)
`maxSets` itself, since a pool sized `{maxSets, UNIFORM_BUFFER: maxSets}`
has both ceilings reached by the identical allocation count. A
Material-bound Pipeline (`hasSampledTextureBinding = true`) additionally
consumes one `COMBINED_IMAGE_SAMPLER` descriptor, at a rate strictly no
greater than the uniform/`maxSets` rate (the fallback Pipeline consumes
zero) — so, sized identically to `maxSets`, the sampler budget can never
be the *first* budget exhausted either, for any real mix of
fallback/uniform-only/textured Pipelines this codebase's own
`PipelineCreateParams` shape can produce today. A future Pipeline kind
with more than one sampler binding, or a second uniform buffer, would
invalidate this specific proof and require re-deriving the pool-size
ratio — explicitly out of this Spec's own scope (no such kind exists or
is proposed here).

### D5. New pool initial capacity and growth strategy

**Recommendation, explicitly not a second unexplained magic constant:**
the **first** pool keeps today's existing `maxSets = 4` value — but its
own justification changes, and this Spec states the new justification
explicitly rather than silently carrying the old, now-known-incorrect
one forward: with growth now guaranteeing correctness for any N, the
first pool's own size no longer needs to be "big enough for the worst
case" (the property that was actually wrong before) — it only needs to
be a small, cheap first allocation, sized to avoid any growth event at
all for the steady-state, already-verified single/double-material
scenes this codebase ships today (`minimal_cube`, `world_scene`,
`textured_quad`, `material_demo`, `lighting_demo`), while genuinely
fixing every larger-N case via growth instead of by guessing a bigger
number. **Growth strategy: geometric doubling** — each new pool is sized
to `maxSets` equal to the immediately-preceding pool's own `maxSets`,
so the growable set's own total capacity doubles with each growth event
(4 → 8 → 16 → …, bounded by D6) — a standard, self-documenting,
amortized-growth strategy (the same shape `std::vector`'s own default
growth factor uses), never a second hand-picked ceiling. **Open for
Human Review to size differently:** if a specific future scene's own
real, planned material count is already known, Human Review may prefer
seeding the first pool's own `maxSets` to that number directly instead —
this Spec's own recommendation optimizes for "zero behavior change to
today's shipped content" over "anticipate a specific future number,"
consistent with this Spec's own Non-Goals.

### D6. Hard ceiling

**Recommendation:** yes — a real, Plan-time-named constant bounding the
**total number of pools ever in the set** (not a bound on material count
directly) — e.g. 4 pools total (the initial pool plus at most 3 growth
events; a Plan-time-finalized exact number, disclosed as a leak/defect
safety net, not a content-scaling ceiling: reaching it after geometric
doubling from a starting `maxSets = 4` already represents `4+8+16+32 = 60`
concurrent descriptor sets — a scale no currently-planned scene
approaches). Exceeding it returns the existing
`PipelineCreateError::DescriptorSetAllocationFailed`,
unchanged in shape — a signal that something is likely leaking
(Pipelines never being destroyed) rather than a legitimate, growing
content need, exactly the same "checked, not assumed" discipline Plan
0007 Section 10 already applied to the original, single-pool ceiling.

### D7. Descriptor set/pool release/reclaim timing

**Recommendation: unchanged — no new decision needed.** This Spec's own
fresh investigation ("Pre-draft verification" above) confirmed the
existing timing is already correct: `VulkanPipeline`'s destructor frees
its own one set immediately on destruction, and every caller-side
destruction of a superseded `Pipeline`/`Material` already happens only
after the submit-safe point Spec 0018 D9 established (confirmed by
direct reading of `runtime_application.cpp:766-780`). This Spec extends
*which pool* a free targets (D1) without touching *when* any free
happens. **A free's own capacity is available for reuse by the very
next `createPipeline()` call's own creation-order scan (D3)** — this is
the direct mechanism by which a long-running session with many
format-change events over time stays bounded in pool *count*, rather
than growing once per format change forever: once an old batch's
Pipelines are destroyed (after their own submit-safe point), their
capacity returns to whichever pool(s) they came from and is found again
by the next allocation's own scan, before any further growth is ever
considered.

### D8. Safety when the format-change old and new batches are simultaneously alive

**Recommendation: unaffected, no new mechanism required.** This is
exactly the scenario D1's growth mechanism is designed to accommodate —
the existing, already-correct Spec 0018 D9 coexistence window (old batch
alive, new candidate batch built, swap only after `submit()` succeeds)
is untouched; growth simply ensures the pool underneath can satisfy the
concurrent-allocation peak that window already, correctly, produces. The
old and new batches may legitimately end up with descriptor sets from
**different** pools in `pools_` (e.g., the old batch's sets came from
`pools_[0]`, and the new candidate batch's own allocation scan finds
`pools_[0]` still exhausted by the still-alive old batch and lands on
`pools_[1]`, freshly grown) — this is expected and correct, not a defect;
nothing in this design requires a batch's own Pipelines to share one
pool. **No submit or `DeviceLost` failure ever triggers a selective
pool reset or reclaim** — pools are never reset (`vkResetDescriptorPool`
is not used anywhere in this design, matching today's code exactly) and
are never destroyed except at `VulkanDevice`'s own destruction (D10);
a submit failure is handled entirely by the existing, unmodified
`SubmitError`/`lifecycle_.markFailed()` path, with zero interaction with
the descriptor-pool set.

### D9. Transactional/RAII behavior when a partial Pipeline creation fails

**Recommendation:** the existing all-or-nothing RAII discipline in
`createPipeline()` is preserved exactly, extended by exactly one more
failure branch (every existing pool exhausted for a growth-eligible
reason, then either `vkCreateDescriptorPool` for the new pool fails, or
the retried `vkAllocateDescriptorSets` still fails against it) — that
branch destroys every resource already successfully created earlier in
the same call (shader modules, descriptor-set layout) in the identical
reverse order every other failure path already uses, and returns
`PipelineCreateError::DescriptorSetAllocationFailed`, unchanged.

**A newly-created pool that turns out to be unused by this specific
`createPipeline()` call is kept, not rolled back — an explicit, safe
policy, stated so a future Plan/Implementation never "helpfully"
second-guesses it:** if growth's own `vkCreateDescriptorPool` succeeds
and the retried `vkAllocateDescriptorSets` also succeeds, but a *later*
step in the same `createPipeline()` call fails (pipeline-layout
creation, or the final `vkCreateGraphicsPipelines`), the existing,
unmodified reverse-order cleanup already frees that just-allocated
descriptor set back into its own (freshly-grown) pool — leaving a
real, valid, now-empty `VkDescriptorPool` in `pools_`. This pool is
**never destroyed as part of that unwind** — it remains in `pools_`,
available capacity for the very next `createPipeline()` call's own
scan (D3/D7). This is safe (Phase 1's single-threaded orchestration
means no other call can have raced to use it in between, and an empty,
valid pool has no GPU-in-flight reference of any kind) and matches this
Spec's own Non-Goal of never dynamically shrinking/reclaiming a pool
before Device destruction — the only accepted cost is that one growth
event happened slightly earlier than steady-state usage strictly
required, not that anything leaks or is destroyed incorrectly.

### D10. `DeviceLost`/shutdown behavior

**Recommendation:** the existing fail-fast, full-teardown,
log-and-continue destructor discipline (`~VulkanDevice()`) is preserved
exactly, extended mechanically from "destroy the one pool" to "destroy
every pool in the growable set," after the same, unchanged drain
sequence. No `DeviceLost`-specific handling is introduced by this Spec,
since none exists in the current destructor either — a real, honest
statement of scope, not an implied new guarantee.

### D11. Does the RHI public API need to change?

**Recommendation: no.** Confirmed, not merely asserted, by the "Pre-draft
verification" section above: `atlantis::rhi::Device`/`atlantis::rhi::Pipeline`'s
own abstract interfaces expose no descriptor-pool concept at all; the one
concrete-class accessor that does (`VulkanDevice::descriptorPool()`) has
zero callers anywhere in this repository; and every error type this fix
touches (`PipelineCreateError`, `CreateMaterialError`,
`MaterialRealizationError`) keeps its exact current shape, since growth
only changes what happens *before* an error is ever returned, never
which error is surfaced once genuinely exhausted.

### D12. Windows/future-Android Vulkan portability

**Recommendation:** the fix uses only core Vulkan 1.0 entry points already
in use elsewhere in `vulkan_device.cpp`
(`vkCreateDescriptorPool`/`vkAllocateDescriptorSets`/`vkFreeDescriptorSets`/
`vkDestroyDescriptorPool`) with no new instance/device extension or
feature requirement — a future Android Vulkan Backend (Candidate Order 1,
unaffected by this Spec, see "Cross-cutting note" below) would inherit
this exact same growable-pool code path unchanged, with no
platform-conditional branch anywhere in this Spec's own design. **No
locking, atomics, or any multi-threaded allocator design is introduced
or implied** — `pools_`, the creation-order scan, and growth all run
entirely within Phase 1's existing single-threaded frame-orchestration
baseline (AGENTS.md Threading rules), on the same one logical thread
every other `VulkanDevice` call already runs on; a `std::vector<VkDescriptorPool>`
with no concurrent access is sufficient, and this Spec does not evaluate
or propose anything beyond that baseline.

### D13. Test scale

**Recommendation, at minimum:**

- GPU-independent unit tests for the capacity-calculation/growth-selection
  logic in isolation (a pure function of "current pool sizes so far,
  ceiling" → "next pool size, or exhausted"; no real `Device` needed).
- **GPU-independent unit tests for the error-classification decision
  itself** (D3's own table) — a pure function taking a synthetic
  `VkResult` value (no real `Device`/allocation needed, since `VkResult`
  is a plain enum) and returning "growth-eligible" or "not
  growth-eligible": `VK_ERROR_OUT_OF_POOL_MEMORY`/`VK_ERROR_FRAGMENTED_POOL`
  must classify as growth-eligible; `VK_ERROR_DEVICE_LOST`/
  `VK_ERROR_OUT_OF_HOST_MEMORY`/`VK_ERROR_OUT_OF_DEVICE_MEMORY` (and any
  other non-success value) must not — this is the reliable way to prove
  the classification logic itself is correct, since reliably *injecting*
  a real `VK_ERROR_DEVICE_LOST` from a real Device on demand is not
  practical; the real-GPU tests below prove the growth-eligible path
  end-to-end, this pure-function test proves the classification boundary
  precisely, including the cases the real-GPU tests cannot reach.
- **A real-GPU pool-capacity-reuse test**, directly proving D3/D7's own
  reuse-before-growth claim, not merely asserting it: allocate enough
  Pipelines to exhaust the first pool and trigger one growth event;
  destroy a subset of the Pipelines whose sets came from the *first*
  pool (freeing capacity there, past the submit-safe point); create a
  new Pipeline and confirm — via the low-level, kind-independent probe
  pattern the existing N=2 test already uses — that it is satisfied
  without a second growth event, i.e. that the freed first-pool capacity
  was actually found and reused by the creation-order scan, not
  ignored in favor of always growing again.
- A real-GPU fallback + `UnlitTextured` + `LitTextured` format-change
  **success** test — the exact PR #96 scenario, converted per D14.
- A real-GPU, deterministic N greater than 2 (Human Review to pick the
  exact value at Plan time; this Spec recommends a value clearly past the
  first growth boundary, e.g. enough distinct materials that the
  format-change peak `2*(N+1)` exceeds the *first* grown pool's own
  doubled capacity too, proving a *second* growth event also works, not
  merely the first) — the test's own chosen N is a **test parameter
  only**, explicitly never hardcoded into any production capacity
  constant (this Spec's own Non-Goals restate this).
- Uniform-only, textured-only, and lit-mixed descriptor-combination
  coverage, confirming D4's own symmetric-budget choice holds under a
  real mixed workload.
- Confirmation that Pipelines created **before** a growth event remain
  valid and usable (still bindable, still drawable) **after** a later
  growth event — the existing pool(s) they belong to are never touched by
  a growth event that only appends a new pool.
- No leak and no partial publish on a mid-creation failure (D9), directly
  exercised (a forced/simulated growth-attempt failure path, or, at
  minimum, static code-review confirmation matching every other
  `createPipeline()` failure-path test's own existing precedent) —
  including confirming a newly-grown-but-ultimately-unused pool (D9's own
  "kept, not rolled back" policy) is neither leaked (still destroyed,
  correctly, at Device destruction) nor erroneously double-destroyed.
- No destruction of a still-GPU-referenced descriptor set/pool before the
  submit-safe point (D7/D8) — re-run of the existing, already-passing
  `rebuildMaterialsForFormatChange` GPU tests, confirming zero regression.
- Full Debug/Release `ctest` suites passing; zero Vulkan Validation
  Layers `VUID`/`Validation Error`/`Validation Warning` hits; the
  existing five goldens (`minimal_cube`, `world_scene`, `textured_quad`,
  `material_demo`, `lighting_demo`) byte-for-byte/pixel-for-pixel
  unchanged; a fresh `ATLANTIS_BUILD_TESTS=OFF` configure+build producing
  a working `atlantis_runtime.exe` with zero test executables; an
  unchanged module/link graph; zero new third-party dependency.

### D14. PR #96's own "current capacity fails" test

**Recommendation:** convert it into a success regression test once this
Spec's own fix lands — the test's own existing comment already instructs
exactly this ("this test... should be revisited and very likely deleted
at that point, not 'fixed' by flipping the assertion" — this Spec reads
that as "do not merely flip the assertion in place without also removing
the now-inaccurate `[known_limitation]` framing and comment block around
it," not as "never flip it"). Concretely: retain the low-level Part 1
probe (the 6-Pipeline loop), but update its own expectation — with
growth, all 6 (or more) should now succeed, which becomes a direct,
free regression test proving growth actually engaged, not merely that
exhaustion no longer occurs. Retain Part 2's own real-shape scenario, but
change `REQUIRE(rebuildResult.isErr())` to `REQUIRE(rebuildResult.isOk())`,
remove the `[known_limitation]` tag, and rewrite the surrounding
comment block to describe the fix instead of the limitation — keeping a
stale "known limitation, not fixed here" comment in the tree after a real
fix lands would misrepresent this codebase's own current state, which
this Spec's own repository-wide honesty discipline (AGENTS.md) does not
permit.

### D15. Explicit Non-Goals

Restated from the Non-Goals section above, for completeness against this
Spec's own drafting brief: bindless rendering, descriptor indexing,
descriptor buffers, a cross-frame descriptor-set caching system, a
general-purpose GPU memory/descriptor allocator, PBR, Shadow Foundation,
and Android implementation are all explicitly out of scope for this Spec.

## Testing & Verification Plan

See D13 above for the complete, itemized minimum test scale. In addition
to those items, this Spec's own eventual Plan must include: a fresh
Debug and Release build from a clean tree; `ctest -LE gpu` and
`ctest -L gpu` both configurations, on real Vulkan-capable hardware, with
Validation Layers enabled and grepped clean; confirmation that
`Atlantis::VulkanBackend`'s own link graph is unchanged (still links only
what it links today); and a `git diff --check` pass on the final
Implementation diff. No new golden is required or permitted — this
Spec changes zero rendered pixels.

## Risks & Open Questions

- **D5's own first-pool sizing choice (keep `maxSets = 4`) is a
  disclosed, Human-Review-overridable default, not a proven-optimal
  number** — see D5's own "Open for Human Review to size differently"
  note. Getting this wrong only costs one extra, rare growth event for
  content that happens to need more than 4 concurrent sets on its very
  first format change; it does not risk correctness either way, since
  growth (D1/D3) is the actual correctness mechanism, not the starting
  size.
- **D6's own hard-ceiling value is a Plan-time detail this Spec does not
  finalize** — this Spec fixes the *existence* of a bounded ceiling and
  the reasoning for having one (leak detection, not content-scaling), not
  its exact numeric value; Human Review may adjust the recommended value
  in D6 without changing this Spec's own architecture.
- **The existing caller-discipline invariant** ("every `VulkanPipeline`
  is destroyed before its owning `VulkanDevice`," enforced only by
  comment, confirmed pre-existing and unchanged by this Spec) remains
  unverified by any automated check — a real, disclosed, pre-existing gap
  this Spec neither introduces nor closes; Human Review may wish to name
  it as its own, separate, future hardening candidate.

## Out of Scope / Future Work

- A future, separate Spec could revisit D4's own symmetric-budget choice
  if a real, uniform-only-heavy or sampler-heavy workload ever
  demonstrates the current symmetric shape wastes meaningful capacity.
- A future, separate Spec could pursue bindless/descriptor indexing if
  this codebase's own real material count ever grows into a range (tens
  to hundreds of concurrently live materials) where per-Pipeline
  descriptor-set allocation itself, not merely this pool's own fixed
  size, becomes the actual bottleneck — explicitly not the case today
  (see Non-Goals).
- The pre-existing, comment-only "Pipeline must outlive Device" caller
  discipline (Risks, above) could become its own future, narrowly scoped
  hardening item (e.g., a debug-build liveness assertion) — not proposed
  or required by this Spec.

## Cross-cutting note (governance, not scope)

This Spec was drafted at explicit human direction, placed ahead of
Android Platform (`specs/README.md` Section B, Candidate Order 1) and
ahead of Shadow/PBR drafting, in response to a real, disclosed,
previously-undisclosed-until-PR-#96 architectural finding from Spec
0019's own final review. **Android Platform's own Candidate Order 1
registration, dependencies, and scope are entirely unaffected** — see
`specs/README.md`'s own updated Section B note recording this
reprioritization, matching the identical pattern every prior
reprioritization note in that section already uses.
