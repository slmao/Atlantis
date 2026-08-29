# Plan: Descriptor Pool Capacity Foundation

- **Spec:** [specs/0021-descriptor-pool-capacity-foundation.md](../specs/0021-descriptor-pool-capacity-foundation.md)
  (`Approved`)
- **Status:** In Review
- **Author:** slmao

## Objective

Implement the Vulkan-Backend-private, Device-owned, growable descriptor-pool
set Spec 0021/[ADR-0064](../adr/0064-vulkan-backend-descriptor-pool-growth-ownership-model.md)
approved: `VulkanDevice` scans every existing `VkDescriptorPool` in
creation order before growing, growing (geometric doubling, up to a
4-pool hard ceiling — 60 concurrent descriptor sets total) only on a
real, growth-eligible `VkResult`, so the already-supported,
arbitrary-N-material format-change rebuild (Spec 0018) no longer fails
once N ≥ 2 — with zero RHI/Renderer/Material public API change.

## Pre-draft verification against real, current source

Confirmed directly against `main` at `8bcc107` (PR #98's own merge
commit) at Plan-drafting time, by reading every touched file in full —
not by restating Spec 0021's own prose:

- **`VulkanDevice::createPipeline()` in full** (`vulkan_device.cpp:826-1054`
  — the complete, current body, all 229 lines read): the descriptor-set
  allocation step is exactly three statements
  (`vulkan_device.cpp:900-913`) — build `VkDescriptorSetAllocateInfo`
  with `descriptorPool = descriptorPool_`, call
  `vkAllocateDescriptorSets`, and on any non-success `VkResult`, free
  nothing (no set was allocated) and return
  `Err(DescriptorSetAllocationFailed)` after destroying the
  already-created `descriptorSetLayout` and both shader modules. Two
  *later* failure branches also free the descriptor set once it has been
  allocated: `vkCreatePipelineLayout` failure
  (`vulkan_device.cpp:927-935`, frees via
  `vkFreeDescriptorSets(device_, descriptorPool_, 1, &descriptorSet)`)
  and the final `vkCreateGraphicsPipelines` failure
  (`vulkan_device.cpp:1045-1050`, identical free call). Both of these
  free calls reference `descriptorPool_` directly and are the two exact
  call sites this Plan must repoint at the set's own *origin* pool, not
  the Device's first pool.
- **`VulkanDevice`'s full member list and constructor**
  (`vulkan_device.h`, read in full, 190 lines): `descriptorPool_` is a
  single `VkDescriptorPool` (`vulkan_device.h:186`), constructed from a
  single `VkDescriptorPool descriptorPool` constructor parameter
  (`vulkan_device.h:89-93`), with a `[[nodiscard]] VkDescriptorPool
  descriptorPool() const noexcept` accessor (`vulkan_device.h:135`).
  Confirmed by a repository-wide `grep` for `descriptorPool()` (matching
  Spec 0021's own claim, re-verified fresh, not trusted): **zero real
  callers anywhere** — only the declaration itself and Spec/ADR prose
  reference the name. This Plan removes it outright (Spec 0021 D11's own
  "or removed" option) — nothing in this Plan's own new tests needs it
  (see "Verification Checklist" below for how pool-count/reuse is proven
  without it, per Human Review's own explicit instruction not to add a
  new production introspection API for test convenience).
- **`VulkanPipeline` in full** (`vulkan_pipeline.h`, 50 lines, and
  `vulkan_pipeline.cpp`, 35 lines — both read in full): its constructor
  already takes a plain `VkDescriptorPool descriptorPool` **value**
  parameter and stores it as a **non-owning** `VkDescriptorPool
  descriptorPool_` field (`vulkan_pipeline.h:42`, comment: "non-owning;
  VulkanDevice's pool, must outlive this object"); its destructor already
  frees its own one set back to exactly that stored handle
  (`vulkan_pipeline.cpp:27`, `vkFreeDescriptorSets(device_,
  descriptorPool_, 1, &descriptorSet_)`, result captured into
  `ATLANTIS_CHECK(freeResult == VK_SUCCESS)` — a hard assertion, not a
  log, since `FREE_DESCRIPTOR_SET_BIT` makes this call documented to
  never fail). **Correction to Spec 0021 D1's own implementation-detail
  wording, found by this fresh, full re-read (not a new architectural
  decision — see "Plan-level decisions" P1 below):** `VulkanPipeline`
  requires **zero** field or signature changes. D1 said it "gains a
  second... field recording *which* pool" its set came from; in fact its
  existing single `descriptorPool_` field and existing constructor
  parameter already express exactly that — "the specific pool this
  Pipeline's one descriptor set belongs to" — precisely, today, for the
  single-pool case. Only the *value* the caller (`createPipeline()`)
  passes into that already-existing parameter needs to change (from
  always-the-Device's-one-pool to whichever pool the scan/growth found).
  This is a strictly smaller implementation footprint than the Spec
  anticipated, not a deviation from the approved model.
- **Pool creation inside `createDevice()`** (`vulkan_device.cpp:1411-1440`,
  read in full alongside `DescriptorPoolGuard`, `vulkan_device.cpp:377-401`):
  a single `VkDescriptorPool`, `maxSets = 4`, two `VkDescriptorPoolSize`
  entries (`{UNIFORM_BUFFER, 4}`, `{COMBINED_IMAGE_SAMPLER, 4}`),
  `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT` set, created via
  the same two-phase guard pattern (`InstanceGuard`/`DeviceGuard`/etc.)
  every other Device-construction-time resource already uses — guard
  constructed immediately after the raw `vkCreateDescriptorPool` call
  succeeds, `.get()` threaded into `VulkanDevice`'s constructor
  (`vulkan_device.cpp:1449-1452`), `.release()` called only after
  `std::make_unique<VulkanDevice>` has returned successfully
  (`vulkan_device.cpp:1462-1467`). **This one-time, construction-time
  pool never needs to grow itself** — growth is a `createPipeline()`-time
  concern only — so `createDevice()`'s own guard-based flow needs no
  structural change, only: (a) the `maxSets = 4` / pool-size-entry
  literals replaced by this Plan's own new named constant (single
  source of truth, see P2), and (b) `VulkanDevice`'s constructor body
  wraps the one already-created pool into the new `descriptorPools_`
  member instead of assigning a scalar.
- **`~VulkanDevice()`** (`vulkan_device.cpp:423-456`, read in full): an
  unconditional, defensive drain (`waitAndReleaseRetainedSubmission()`
  then `vkDeviceWaitIdle()`, each logged-and-swallowed on failure, never
  thrown from a destructor) precedes `vkDestroyDescriptorPool`, itself
  ordered before `vkDestroyDevice`. The five Phase-2 `.release()` calls
  at `vulkan_device.cpp:1462-1467` are explicitly commented "order among
  these five calls does not matter... no dependency between the calls to
  sequence" — confirming, for this Plan's own destructor loop, that no
  ordering requirement exists among `descriptorPools_`'s own entries
  either (every `vkDestroyDescriptorPool` call is independent of every
  other).
- **`toPipelineCreateError()`** (`vulkan_result.cpp:119-122`): maps every
  non-success `VkResult` from `vkCreateGraphicsPipelines` to one generic
  `PipelineCreateError::PipelineCreationFailed` — confirmed unchanged,
  confirming this Plan's new allocation helper's own failures must never
  be routed through this function (they use
  `DescriptorSetAllocationFailed` directly, matching today's exact
  precedent).
- **`vulkan_result.h`'s own existing pure-`VkResult`-classification
  convention** (`vulkan_result.h`, full file read, 89 lines, and
  `vulkan_result_tests.cpp:1-34`): `classifyFailure(VkResult) ->
  VulkanFailureCategory` and every `toXxxError(VkResult)` function are
  explicitly documented "Pure, GPU-independent `VkResult` -> RHI/Vulkan-
  Backend error mapping... safe to unit-test with literal `VkResult`
  enumerators and no real Vulkan call" — and `vulkan_result_tests.cpp`
  does exactly that (e.g. `REQUIRE(classifyFailure(VK_ERROR_DEVICE_LOST)
  == VulkanFailureCategory::DeviceLost)`, zero `VkInstance`/`VkDevice`
  anywhere in the file). This is the exact, established precedent this
  Plan's own new growth-eligibility classifier follows precisely — it
  extends this file, not a new module.
- **`PipelineCreateParams`** (`rhi/types.h:186-201`, full struct read):
  confirms `hasSampledTextureBinding` defaults to `false` and is the
  *only* input controlling whether `createPipeline()`'s own descriptor-
  set-layout carries the second, combined-image-sampler binding — this
  is unrelated to which shader pair is used, confirming this Plan's own
  test design choice (below) of reusing the existing `minimal_mesh`
  shader pair with `hasSampledTextureBinding = true` to exercise the
  textured pool-consumption path, with no new shader/CMake dependency.
- **Vulkan Backend test/library organization**
  (`src/vulkan_backend/CMakeLists.txt`, `tests/vulkan_backend/CMakeLists.txt`,
  both read in full): `atlantis_vulkan_backend` (the production static
  library) lists 22 source files, none of them a pool-growth-specific
  module yet. `atlantis_vulkan_backend_tests` (GPU-independent,
  `ctest -LE gpu`) already adds `src/vulkan_backend/src` as a `PRIVATE`
  include directory specifically so it can include this module's own
  private headers (`vulkan_result.h`, etc.) directly — confirming a new
  private header/source pair here is unit-testable with zero real
  `VkDevice`, matching Spec 0021 D13's own requirement exactly.
  `atlantis_vulkan_backend_gpu_tests` (`ctest -L gpu`) already links
  `Atlantis::VulkanBackend`/`Platform`/`RHI`/`RenderGraph`/`Renderer`/
  `ShaderSystem` and already copies the checked-in `minimal_mesh`
  SPIR-V pair next to its own build output (via
  `add_dependencies(atlantis_vulkan_backend_gpu_tests minimal_mesh_shaders)`)
  — this Plan's own new GPU tests reuse that existing copy step, adding
  no new shader dependency. **No `Fake`/mock Vulkan Backend test double
  of any kind exists anywhere in this module** (confirmed by a
  repository-wide search for `class Fake`/`struct Fake` — zero matches)
  — every GPU test in this codebase, including this Plan's own new ones,
  drives a real `VkInstance`/`VkDevice`. This directly confirms Spec
  0021 D13's own reasoning: a real `VK_ERROR_DEVICE_LOST` cannot be
  reliably injected from a real Device, so the growth-eligibility
  classifier's own correctness (including its handling of
  `VK_ERROR_DEVICE_LOST`) can only be proven by a pure, GPU-independent
  unit test passing literal `VkResult` values directly — never by a
  real-GPU test.
- **PR #96's own N=2 "known limitation" test, re-confirmed fresh, still
  reproducing today** (`tests/runtime/material_realization_gpu_tests.cpp:923-1098`,
  full `TEST_CASE` re-read): Part 1 (`:927-971`) — a low-level,
  kind-independent `Device::createPipeline()` loop; the 5th of 6
  attempted Pipelines (index 4, `CHECK(i == 4)`) fails with exactly
  `PipelineCreateError::DescriptorSetAllocationFailed`, the first four
  consuming the pool's entire declared `maxSets = 4`. Part 2
  (`:973-1097`) — fallback + one `UnlitTextured` + one `LitTextured`
  material realized in "Frame 1"; a real color-format change in
  "Frame 2" builds a new fallback+2-material candidate batch while the
  old 2-material batch is still alive (2 old + 3 new = 5 concurrent
  sets); `REQUIRE(rebuildResult.isErr())`,
  `CHECK(rebuildResult.error() == MaterialRealizationError::MaterialCreateFailed)`.
  Both assertions still pass against today's unmodified code — confirmed
  by inspection of the unchanged `maxSets = 4` pool-creation code
  (re-grepped fresh on this branch, byte-identical to what Spec 0021
  documented). This Plan's own Milestone 2 (below) flips both parts to
  expect success, in the same commit as the `VulkanDevice` fix itself —
  never as a separate, later commit, since an intermediate commit
  landing the fix without flipping this test would leave the tree with
  a test asserting a now-false claim.

No unresolvable conflict with the Approved Spec/ADR was found. One
implementation-detail correction is disclosed (P1, `VulkanPipeline`
needs zero changes) and one Plan-level concretization is disclosed (P2,
a paired pool/size struct instead of a bare `std::vector<VkDescriptorPool>`)
— both are strictly smaller-footprint or more-precise realizations of
the same, unchanged, Approved architectural decision, not deviations
from it. Neither requires returning to Spec/ADR review.

## Plan-level decisions (fixed here, not left to Implementation)

### P1. `VulkanPipeline` requires zero changes

Restated from "Pre-draft verification" above: `VulkanPipeline`'s
existing single `descriptorPool_` field and constructor parameter
already mean exactly "the specific pool my one descriptor set belongs
to." `createPipeline()`'s own new allocation helper (P3) simply passes
a different value into that already-existing parameter — the *origin*
pool the scan/growth found, rather than the Device's sole pool. Zero
new field, zero signature change, zero destructor change to
`vulkan_pipeline.h`/`.cpp`.

### P2. Pool/size pairing: a small named struct, not a bare handle vector

Vulkan has no "query this pool's own `maxSets`" introspection call, and
ADR-0064's own geometric-doubling growth strategy (each new pool's own
`maxSets` equals the *immediately preceding* pool's own `maxSets`)
needs to know the last pool's own size to compute the next one.
`VulkanDevice` must therefore track each pool's own `maxSets` alongside
its handle. Two parallel `std::vector`s (handles and sizes, kept in
sync by convention only) is a real, avoidable robustness risk this
codebase's own style (named, structurally-clear types over parallel
arrays) does not favor. This Plan uses one small, private, aggregate
struct instead:

```cpp
// vulkan_device.h, in namespace atlantis::vulkan_backend::detail,
// immediately above class VulkanDevice.
struct DescriptorPoolEntry {
  VkDescriptorPool pool;    // handle VALUE -- see P3's own "Handle-value
                            // safety" note; never a pointer/reference
                            // into descriptorPools_'s own storage.
  std::uint32_t maxSets;    // this pool's own maxSets, as passed to
                            // vkCreateDescriptorPool -- tracked here
                            // because Vulkan has no query call for it.
};
```

`VulkanDevice::descriptorPools_` becomes `std::vector<DescriptorPoolEntry>`
(replacing the single `VkDescriptorPool descriptorPool_` member). This
is a concretization of Spec 0021/ADR-0064's own illustrative
`std::vector<VkDescriptorPool>` code, not a deviation from the approved
model — the approved *decision* ("a Device-owned, growable set of
pools, tried in creation order, each remembering enough to support
geometric-doubling growth") is unchanged; only the exact C++ container
shape realizing "remembering enough" is a Plan-time concretization the
Spec's own illustrative code did not need to pin down at that level of
precision (the same way Spec 0021's own D5/D6 left the exact hard-
ceiling constant's *name* and *file* to Plan time while fixing its
*value*).

### P3. New private module: `vulkan_descriptor_pool_growth.h`/`.cpp`

A new header/source pair, `src/vulkan_backend/src/vulkan_descriptor_pool_growth.h`
and `.cpp`, in `namespace atlantis::vulkan_backend::detail`, holding the
two named capacity constants and the one pure growth-size function —
kept separate from `vulkan_result.h` (which is specifically "`VkResult`
-> RHI error mapping," a different concern) and separate from
`vulkan_device.h`/`.cpp` (so the pure, GPU-independent piece is
directly unit-testable without pulling in `VulkanDevice`'s own,
much larger translation unit):

```cpp
#pragma once

#include <cstdint>

// Spec 0021 D5/D6, ADR-0064: the descriptor-pool growth strategy's own
// two fixed capacity constants and its one pure sizing function. Pure,
// GPU-independent, deterministic -- safe to unit-test with literal
// integer inputs and no real Vulkan call, matching vulkan_result.h's
// own established "pure classification" precedent.
namespace atlantis::vulkan_backend::detail {

// The first pool VulkanDevice ever creates (at Device construction,
// createDevice()) is sized to this -- unchanged from this codebase's
// own pre-Spec-0021 value, but now justified differently (Spec 0021
// D5): correctness no longer depends on this number being "big enough"
// (growth, below, guarantees that); it is kept small and cheap so the
// steady-state, already-verified single/double-material scenes this
// codebase ships today never trigger a growth event at all.
inline constexpr std::uint32_t kInitialDescriptorPoolMaxSets = 4;

// The total number of pools VulkanDevice's own growable set may ever
// contain (Spec 0021 D6) -- a leak/defect safety net, never a
// content-scaling ceiling. 4 pools, geometric doubling from
// kInitialDescriptorPoolMaxSets, sums to 4+8+16+32 = 60 concurrent
// descriptor sets before this ceiling is reached.
inline constexpr std::size_t kMaxDescriptorPoolCount = 4;

// Returns the maxSets value the NEXT descriptor pool should be created
// with, given the immediately preceding pool's own maxSets --
// geometric doubling (Spec 0021 D5), the same amortized-growth shape
// std::vector's own default growth factor uses. Pure, total (no
// invalid input -- lastPoolMaxSets is always a real, already-created
// pool's own positive maxSets).
[[nodiscard]] std::uint32_t nextDescriptorPoolMaxSets(std::uint32_t lastPoolMaxSets);

}  // namespace atlantis::vulkan_backend::detail
```

```cpp
// vulkan_descriptor_pool_growth.cpp
#include "vulkan_descriptor_pool_growth.h"

namespace atlantis::vulkan_backend::detail {

std::uint32_t nextDescriptorPoolMaxSets(std::uint32_t lastPoolMaxSets) { return lastPoolMaxSets * 2; }

}  // namespace atlantis::vulkan_backend::detail
```

### P4. `vulkan_result.h`/`.cpp` addition: the growth-eligibility classifier

Extends the existing, established pure-classification file directly —
no new module for this one function, matching its own file-level
purpose exactly:

```cpp
// vulkan_result.h, appended after toOffscreenTargetCreateError().

// vkAllocateDescriptorSets, inside VulkanDevice's own descriptor-pool
// allocation scan (Spec 0021/ADR-0064): true only for the two VkResult
// values that mean "this pool's own capacity is exhausted, a
// different/new pool may still satisfy this allocation" --
// VK_ERROR_OUT_OF_POOL_MEMORY and VK_ERROR_FRAGMENTED_POOL. False for
// every other non-success value (VK_ERROR_DEVICE_LOST,
// VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY, or any
// other), which must propagate immediately, unchanged, as
// PipelineCreateError::DescriptorSetAllocationFailed -- no further pool
// tried, no growth attempted, identical to this codebase's own
// pre-Spec-0021 behavior for those cases.
[[nodiscard]] bool isDescriptorPoolGrowthEligible(VkResult result);
```

```cpp
// vulkan_result.cpp, appended.
bool isDescriptorPoolGrowthEligible(VkResult result) {
  ATLANTIS_CHECK(result != VK_SUCCESS);
  return result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL;
}
```

(`ATLANTIS_CHECK(result != VK_SUCCESS)` matches every sibling function
in this file's own existing precondition-check convention exactly,
confirmed by direct reading of `toPipelineCreateError()`'s own
identical first line.)

### P5. `VulkanDevice`'s own new private allocation helper — exact signature and algorithm

```cpp
// vulkan_device.h, new private member declarations.

// Scans descriptorPools_ in creation order (index 0..last), allocating
// one VkDescriptorSet of the given layout from the first pool with
// capacity -- naturally reusing whatever capacity an earlier
// vkFreeDescriptorSets call already returned to any of them (Spec 0021
// D3/D7). Grows (creates exactly one new pool, geometric doubling) and
// retries exactly once, only after every existing pool has failed for a
// growth-eligible reason and the hard ceiling (kMaxDescriptorPoolCount)
// has not yet been reached. On success, outDescriptorSet/outOriginPool
// are both set and this returns std::nullopt; on any failure, neither
// out-param is touched and this returns
// PipelineCreateError::DescriptorSetAllocationFailed -- the only error
// this function can ever produce (Plan-level decision P7's own
// complete mapping table).
[[nodiscard]] std::optional<atlantis::rhi::PipelineCreateError> allocateDescriptorSet(
    VkDescriptorSetLayout layout, VkDescriptorSet& outDescriptorSet, VkDescriptorPool& outOriginPool);

// Creates one new VkDescriptorPool, sized to maxSets, with the
// identical two-pool-size-entry (UNIFORM_BUFFER, COMBINED_IMAGE_SAMPLER,
// both == maxSets) / FREE_DESCRIPTOR_SET_BIT shape createDevice()'s own
// initial pool already uses (P6's own capacity-derivation table).
// Returns VK_NULL_HANDLE on vkCreateDescriptorPool failure -- the
// caller (allocateDescriptorSet(), above) maps that to
// DescriptorSetAllocationFailed; unlike createDevice()'s own one-time
// call, this is an ordinary, expected-to-be-rare runtime outcome, never
// treated as a Device-construction-time failure.
[[nodiscard]] VkDescriptorPool createGrownDescriptorPool(std::uint32_t maxSets) const;
```

Exact algorithm (`vulkan_device.cpp`, directly mappable to code):

```cpp
std::optional<atlantis::rhi::PipelineCreateError> VulkanDevice::allocateDescriptorSet(
    VkDescriptorSetLayout layout, VkDescriptorSet& outDescriptorSet, VkDescriptorPool& outOriginPool) {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  // Step 1: scan every existing pool, in creation order.
  for (const detail::DescriptorPoolEntry& entry : descriptorPools_) {
    allocInfo.descriptorPool = entry.pool;
    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, &set);
    if (result == VK_SUCCESS) {
      outDescriptorSet = set;
      outOriginPool = entry.pool;
      return std::nullopt;
    }
    if (!detail::isDescriptorPoolGrowthEligible(result)) {
      // VK_ERROR_DEVICE_LOST / host-or-device OOM -- immediate,
      // unchanged failure. No further pool tried, no growth attempted.
      return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
    }
    // OUT_OF_POOL_MEMORY / FRAGMENTED_POOL -- try the next existing pool.
  }

  // Step 2: every existing pool exhausted for a growth-eligible reason.
  if (descriptorPools_.size() >= detail::kMaxDescriptorPoolCount) {
    return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
  }

  // Step 3: grow -- create exactly one new pool.
  const std::uint32_t newMaxSets = detail::nextDescriptorPoolMaxSets(descriptorPools_.back().maxSets);
  const VkDescriptorPool newPool = createGrownDescriptorPool(newMaxSets);
  if (newPool == VK_NULL_HANDLE) {
    return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
  }
  // Kept in descriptorPools_ regardless of the retry's own outcome below
  // (Spec 0021 D9) -- safe, since no set has been allocated from it yet.
  descriptorPools_.push_back(detail::DescriptorPoolEntry{newPool, newMaxSets});

  // Step 4: retry exactly once against the new pool. No loop -- this is
  // the ONE retry Spec 0021 D3 specifies, never a second growth attempt
  // within the same call.
  allocInfo.descriptorPool = newPool;
  VkDescriptorSet set = VK_NULL_HANDLE;
  const VkResult retryResult = vkAllocateDescriptorSets(device_, &allocInfo, &set);
  if (retryResult != VK_SUCCESS) {
    return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
  }
  outDescriptorSet = set;
  outOriginPool = newPool;
  return std::nullopt;
}
```

**Termination, explicitly:** Step 1's own loop is bounded by
`descriptorPools_.size()` (at most `kMaxDescriptorPoolCount == 4`,
enforced by Step 2's own check before any growth); Step 4 is a single,
non-looping attempt. Worst case, this function issues at most
`kMaxDescriptorPoolCount + 1 == 5` `vkAllocateDescriptorSets` calls and
at most one `vkCreateDescriptorPool` call, always terminating —
no unbounded loop is possible.

**No double-free, explicitly:** this function only ever *allocates*; it
never frees. Freeing is governed entirely by two, already-existing,
unchanged mechanisms: (a) `createPipeline()`'s own two mid-creation
failure branches (P6, below — repointed at `outOriginPool` instead of
the old `descriptorPool_`), reached only when this function returned
`std::nullopt` (success) and a *later* step then fails; (b)
`VulkanPipeline`'s own destructor (P1 — unchanged), reached only once
`createPipeline()` has fully succeeded and constructed a `VulkanPipeline`.
These two paths are mutually exclusive by construction (a) only runs on
a `createPipeline()` failure return, (b) only runs on a `VulkanPipeline`
object's destruction, and a `VulkanPipeline` is only ever constructed on
`createPipeline()`'s own success path — so exactly one free ever occurs
per successfully-allocated set.

**RAII object responsible for the free on a later-stage
`createPipeline()` failure:** none — matching this exact function's own
pre-existing, unchanged style. Neither of `createPipeline()`'s two
mid-creation free calls (pipeline-layout failure; final
`vkCreateGraphicsPipelines` failure) is RAII-guarded today, and this
Plan does not introduce one — both remain direct, imperative
`vkFreeDescriptorSets` calls inline in their own failure branch, exactly
mirroring how the two shader-module `vkDestroyShaderModule` cleanup
calls in the same function are also not RAII-guarded. `DescriptorPoolGuard`
(the one RAII type this module uses for a descriptor pool) is, and
remains, scoped exclusively to `createDevice()`'s own one-time,
construction-time pool creation — a different code path entirely, never
reused for per-`createPipeline()`-call cleanup.

### P6. `createGrownDescriptorPool()` — exact body, and `createPipeline()`'s three call-site edits

```cpp
VkDescriptorPool VulkanDevice::createGrownDescriptorPool(std::uint32_t maxSets) const {
  // Identical two-pool-size-entry shape to createDevice()'s own initial
  // pool (Spec 0016 D5's precedent) -- both descriptor types always
  // sized equal to maxSets itself (P8's own derivation).
  VkDescriptorPoolSize poolSizes[2]{};
  poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
  poolSizes[0].descriptorCount = maxSets;
  poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  poolSizes[1].descriptorCount = maxSets;

  VkDescriptorPoolCreateInfo createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  createInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
  createInfo.maxSets = maxSets;
  createInfo.poolSizeCount = 2;
  createInfo.pPoolSizes = poolSizes;

  VkDescriptorPool pool = VK_NULL_HANDLE;
  if (vkCreateDescriptorPool(device_, &createInfo, nullptr, &pool) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  return pool;
}
```

`createPipeline()`'s own three required edits (`vulkan_device.cpp`,
current line numbers per "Pre-draft verification" above):

1. **Lines 900-913** (the direct `vkAllocateDescriptorSets` block)
   replaced by:
   ```cpp
   VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
   VkDescriptorPool originPool = VK_NULL_HANDLE;
   if (const auto allocError = allocateDescriptorSet(descriptorSetLayout, descriptorSet, originPool);
       allocError.has_value()) {
     vkDestroyDescriptorSetLayout(device_, descriptorSetLayout, nullptr);
     vkDestroyShaderModule(device_, fragmentModule, nullptr);
     vkDestroyShaderModule(device_, vertexModule, nullptr);
     return ResultT::Err(*allocError);
   }
   ```
   (identical cleanup shape to today's own failure branch — only the
   allocation call itself is now indirected through the new helper.)
2. **Line 930** (`vkFreeDescriptorSets(device_, descriptorPool_, 1,
   &descriptorSet)`, the pipeline-layout-creation failure branch):
   `descriptorPool_` → `originPool`.
3. **Line 1047** (identical call, the final `vkCreateGraphicsPipelines`
   failure branch): `descriptorPool_` → `originPool`.
4. **Line 1052** (`VulkanPipeline` construction): `descriptorPool_` →
   `originPool` — this is the one line that actually threads the real
   origin pool into `VulkanPipeline`'s own existing, unchanged
   constructor parameter (P1).

### P7. Complete error-mapping table (every real outcome this Plan touches)

| Real outcome | Maps to |
|---|---|
| `vkCreateDescriptorPool` fails during `createDevice()`'s own one-time initial-pool creation | `DeviceCreateError::DeviceCreationFailed` — unchanged, pre-existing path, this Plan does not touch it |
| `vkCreateDescriptorPool` fails inside `createGrownDescriptorPool()` (a runtime growth attempt) | `createGrownDescriptorPool()` returns `VK_NULL_HANDLE`; `allocateDescriptorSet()` maps this to `PipelineCreateError::DescriptorSetAllocationFailed` |
| `vkAllocateDescriptorSets` against an existing pool → `VK_ERROR_OUT_OF_POOL_MEMORY` | growth-eligible (P4); scan continues to the next existing pool, or triggers growth if none remain |
| `vkAllocateDescriptorSets` against an existing pool → `VK_ERROR_FRAGMENTED_POOL` | growth-eligible (P4); same as above |
| `vkAllocateDescriptorSets` → `VK_ERROR_DEVICE_LOST` | **not** growth-eligible (P4); immediate `PipelineCreateError::DescriptorSetAllocationFailed`, no further pool tried, no growth attempted |
| `vkAllocateDescriptorSets` → `VK_ERROR_OUT_OF_HOST_MEMORY` | **not** growth-eligible; immediate `DescriptorSetAllocationFailed`, same as above |
| `vkAllocateDescriptorSets` → `VK_ERROR_OUT_OF_DEVICE_MEMORY` | **not** growth-eligible; immediate `DescriptorSetAllocationFailed`, same as above |
| Every existing pool growth-eligible-exhausted, and `descriptorPools_.size() >= kMaxDescriptorPoolCount` (the hard ceiling already reached) | `DescriptorSetAllocationFailed` — the pool set never grows past 4 pools |
| New pool created successfully, the one retry against it still fails (any `VkResult`) | `DescriptorSetAllocationFailed`; the new, now-empty pool is kept in `descriptorPools_` (P5's own Step 3 comment), never rolled back |
| A *later* `createPipeline()` step fails after a successful set allocation (pipeline-layout creation) | unchanged: `PipelineCreateError::PipelineLayoutCreationFailed`; the already-allocated set is freed back to `originPool` (P6 edit 2) |
| A *later* `createPipeline()` step fails after a successful set allocation (`vkCreateGraphicsPipelines`) | unchanged: `toPipelineCreateError()`'s generic `PipelineCreateError::PipelineCreationFailed`; the already-allocated set is freed back to `originPool` (P6 edit 3) |

Every one of the outcomes above is expressible through the existing,
unchanged `PipelineCreateError` enum — confirmed, not merely restated
from the Spec: no real outcome this Plan's own algorithm can produce
needs a new enumerator or an RHI-visible `VkResult`. No item required
raising a Plan Review objection.

### P8. Descriptor pool capacity — full derivation, not just the final numbers

**Per-pool sizing rule (unchanged from today's existing pool, applied
identically to every grown pool via `createGrownDescriptorPool()`,
P6):** every pool's own `maxSets`, `UNIFORM_BUFFER` descriptor count,
and `COMBINED_IMAGE_SAMPLER` descriptor count are all set to the
**identical** value. Applying `kInitialDescriptorPoolMaxSets = 4` and
`nextDescriptorPoolMaxSets(x) = x * 2`, geometric doubling, up to
`kMaxDescriptorPoolCount = 4` pools total:

| Pool | maxSets | `UNIFORM_BUFFER` count | `COMBINED_IMAGE_SAMPLER` count |
|---|---|---|---|
| 1 (initial, `createDevice()`) | 4 | 4 | 4 |
| 2 (1st growth) | 8 | 8 | 8 |
| 3 (2nd growth) | 16 | 16 | 16 |
| 4 (3rd growth, ceiling reached) | 32 | 32 | 32 |
| **Total, all 4 pools** | **60** | **60** | **60** |

**Proof that no mixed uniform-only/textured/lit workload can exhaust
one descriptor type before `maxSets` itself, for every pool above, not
just the first:** every `VulkanPipeline`'s own descriptor set — fallback
or Material-bound, uniform-only or textured/lit — consumes exactly one
`UNIFORM_BUFFER` descriptor (`createPipeline()`'s own binding 0,
`vulkan_device.cpp:862-866`, unconditional, present in every
`VkDescriptorSetLayout` this codebase ever builds) and counts exactly
once against whichever pool's own `maxSets` it was allocated from. Since
`createGrownDescriptorPool()` (and `createDevice()`'s own identical,
unchanged initial-pool code) always sets `UNIFORM_BUFFER count ==
maxSets` for every pool, `UNIFORM_BUFFER` capacity and `maxSets` are
consumed in strict 1:1 lockstep *within each individual pool* — neither
can be exhausted before the other, for any pool at any growth
generation. A Material-bound Pipeline (`hasSampledTextureBinding =
true`) additionally consumes one `COMBINED_IMAGE_SAMPLER` descriptor
from that same pool, at a rate strictly no greater than the uniform/
`maxSets` rate (the fallback Pipeline, `hasSampledTextureBinding =
false`, consumes zero); since `COMBINED_IMAGE_SAMPLER count == maxSets`
for every pool too, the sampler budget can never be the *first* budget
exhausted in any pool either. This proof does not depend on which pool
generation is being allocated from — it holds identically at 4, 8, 16,
and 32, because every pool preserves the same 1:1:1
(set : uniform-descriptor : sampler-descriptor) sizing ratio by
construction (P6).

**This is a fixed-size-chunk allocation *granularity*, never a
business-capability ceiling on material count, restated precisely:**
the true limit on how many concurrent Materials a real scene may use
during a format-change window is `kMaxDescriptorPoolCount`'s own
resulting total (60 concurrent descriptor sets) divided by Spec 0021's
own real peak formula `2*(N+1)` — solving `2*(N+1) <= 60` gives `N <=
29`. This Plan does not encode "29" anywhere in production code, does
not name it in any comment as a supported maximum, and does not treat
it as a design target — it is a mechanically-derived *consequence* of
`kMaxDescriptorPoolCount`/`kInitialDescriptorPoolMaxSets`, restated here
only to make the real, current headroom legible for this Plan's own
review, exactly as Spec 0021 D6 requires ("never a content-scaling
ceiling," "checked, not assumed").

## Plan Review (drafting-time self-review)

A centralized self-review of this Plan's own draft, checking it against
nine specific concerns before presenting it for Human Review — findings
recorded here, not silently absorbed, matching this codebase's own
established Plan Review disclosure precedent (Plan 0020's identical
section). **This Plan remains `In Review` after this section — no
finding here constitutes a Human Review approval; that decision belongs
to the human maintainer.**

1. **The `4` pools / `60` sets figure is consistent with Spec 0021 D6
   exactly** — re-checked: `kInitialDescriptorPoolMaxSets = 4`,
   geometric doubling, `kMaxDescriptorPoolCount = 4` pools total,
   `4+8+16+32 = 60`. V3 makes this a direct, literal test assertion, not
   merely a Plan-prose claim.
2. **Pool sizes are sufficient for any uniform/textured mixed
   workload, at every pool generation, not only the first** — P8's own
   proof is generation-independent (holds for any `maxSets` value
   because every pool preserves the same 1:1:1 sizing ratio by
   construction), re-derived explicitly rather than assumed to
   generalize from Spec 0021 D4's own single-pool version.
3. **`std::vector` growth cannot invalidate a Pipeline's own origin-pool
   handle** — confirmed twice: (a) `VulkanPipeline`'s own
   `descriptorPool_` field is, and remains, a plain `VkDescriptorPool`
   *value* (P1 — no change at all, so no new risk is introduced); (b)
   `allocateDescriptorSet()`'s own algorithm (P5) assigns
   `outOriginPool` from the local `newPool`/`entry.pool` value, never
   from a pointer/reference into `descriptorPools_`'s own storage —
   confirmed by direct reading of the algorithm's own final draft, not
   merely asserted.
4. **Every `createPipeline()` failure path frees to the correct pool** —
   re-traced all three post-allocation failure branches (P6's own three
   edits): pipeline-layout failure, `vkCreateGraphicsPipelines` failure,
   and (inside `allocateDescriptorSet()` itself) the failed-retry-after-
   growth branch, which frees nothing (no set was ever allocated on that
   path) — confirmed no branch is missing a repoint from the old
   `descriptorPool_` to `originPool`.
5. **An empty, newly-grown-but-unused pool is explicitly never called a
   leak** — P5's own algorithm comment and V11 both state plainly that
   this is a disclosed, safe, Spec-0021-D9-sanctioned retention policy,
   verified by a clean Validation-Layers/Device-destruction sequence,
   not by a claim with no test behind it.
6. **No test depends on an unapproved production introspection API** —
   `descriptorPool()` is removed outright (not widened), and V10's own
   ceiling-then-reuse technique proves reuse-before-growth using only
   the pool set's own real, approved hard ceiling as the discriminating
   signal — exactly the indirect method Human Review specified, not a
   new pool-count accessor.
7. **V10 genuinely distinguishes "reused an existing pool" from "created
   another pool"** — re-checked the logic: Phase 2's own 10 new
   Pipelines can only succeed via reuse, because Phase 1 has already
   driven the pool set to its own hard ceiling (4 pools), making a 5th
   pool's creation impossible by construction (Step 2 of P5's own
   algorithm) — success in Phase 2 has no alternative explanation.
8. **No RHI, Renderer, or Material public API is touched** — "Files /
   Modules Touched" above lists only `vulkan_backend`'s own private
   sources and this module's own tests; re-confirmed by the same
   repository-wide search this Plan's own "Pre-draft verification"
   already ran for `descriptorPool()`'s own zero callers.
9. **No new synchronization mechanism or third-party dependency** — the
   entire algorithm (P5) runs on the same single logical thread every
   other `VulkanDevice` call already runs on (no lock, no atomic); V24/
   V26 make this a checked verification item, not merely a design
   intent.

No finding required objecting to Spec 0021/ADR-0064 or returning to
Spec/ADR review — every item above was either already correctly
designed (re-confirmed by this self-review) or is a disclosed,
in-scope Plan-level detail (P1, P2) that narrows or concretizes the
approved architecture without changing it.

## Milestones / Task Breakdown

### Milestone 1 — Pure, GPU-independent growth-strategy modules (additive, zero interaction with `VulkanDevice` yet)

1. Add `src/vulkan_backend/src/vulkan_descriptor_pool_growth.h`/`.cpp`
   (P3): `kInitialDescriptorPoolMaxSets`, `kMaxDescriptorPoolCount`,
   `nextDescriptorPoolMaxSets()`. Register the new `.cpp` in
   `src/vulkan_backend/CMakeLists.txt`'s own `add_library(atlantis_vulkan_backend
   STATIC ...)` source list.
2. Add `isDescriptorPoolGrowthEligible()` to `vulkan_result.h`/`.cpp`
   (P4) — no new file, extends the existing module.
3. New GPU-independent test file
   `tests/vulkan_backend/descriptor_pool_growth_tests.cpp` (pure
   `nextDescriptorPoolMaxSets()`/constant tests — see "Verification
   Checklist" V1-V3) and an extension to the existing
   `tests/vulkan_backend/vulkan_result_tests.cpp` (pure
   `isDescriptorPoolGrowthEligible()` tests — V4-V6). Register the new
   file in `tests/vulkan_backend/CMakeLists.txt`'s own
   `add_executable(atlantis_vulkan_backend_tests ...)` source list.

Repo state after this milestone: builds and passes every existing test
unchanged; the two new pure functions exist, are fully unit-tested, and
are not yet called from anywhere else — a deliberately inert,
independently-reviewable first step.

### Milestone 2 — `VulkanDevice`'s pool-set core, atomic, together with PR #96's own test flip

**This milestone is a single, non-splittable commit** (per this Plan's
own "Pre-draft verification" finding: flipping PR #96's own N=2 test to
expect success cannot land separately from the fix itself without
leaving an intermediate, semantically-false test state):

1. `vulkan_device.h`: replace `VkDescriptorPool descriptorPool_;`
   (line 186) with `std::vector<detail::DescriptorPoolEntry>
   descriptorPools_;` (P2); add the `DescriptorPoolEntry` struct
   declaration (P2) immediately above `class VulkanDevice`; declare
   `allocateDescriptorSet()`/`createGrownDescriptorPool()` (P5/P6) as
   new private members; **remove** the `descriptorPool()` accessor
   (line 135 — confirmed zero callers, "Pre-draft verification" above);
   update the file-level header comment (lines 36-43) and the member's
   own doc comment (lines 180-185) to describe the growable set and
   cite Spec 0021/ADR-0064, replacing the now-superseded "Section 10...
   maxSets = 4" single-pool description.
2. `vulkan_device.cpp`:
   - Constructor (`vulkan_device.cpp:405-421`): body wraps the single
     `descriptorPool` parameter into `descriptorPools_{{descriptorPool,
     kInitialDescriptorPoolMaxSets}}` (a 1-element vector) — the
     constructor's own parameter list is unchanged (`createDevice()`'s
     own guard-based flow still creates exactly one pool at Device
     construction, P-none change needed there beyond item 4 below).
   - `~VulkanDevice()` (`vulkan_device.cpp:423-456`): replace the single
     `vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);` call
     with a loop over `descriptorPools_`, destroying every entry's own
     `.pool` (order-independent, per "Pre-draft verification" above).
   - Add `allocateDescriptorSet()`/`createGrownDescriptorPool()` bodies
     (P5/P6) as new `VulkanDevice::` member-function definitions.
   - `createPipeline()`: apply P6's own four exact edits (lines
     900-913, 930, 1047, 1052).
   - `createDevice()`'s own pool-creation block
     (`vulkan_device.cpp:1411-1440`): replace the bare `4` literals
     (`maxSets`, both `descriptorPoolSizes[].descriptorCount` entries)
     with `detail::kInitialDescriptorPoolMaxSets`; update the
     surrounding comment (lines 1411-1422) to state the new
     justification (Spec 0021 D5: a small, cheap first pool, correctness
     now guaranteed by growth, not by this number being "big enough")
     in place of the now-superseded "Plan 0007 Section 10... single-
     Material... peak of 2, doubled" reasoning — matching AGENTS.md's
     own "update or remove a comment in the same change that makes it
     stale" rule.
2. `tests/runtime/material_realization_gpu_tests.cpp`: flip PR #96's
   own `TEST_CASE` (`:923-1098`) — Part 1's own loop expectation (all 6
   `createPipeline()` calls now succeed; extend the loop further, to
   confirm growth actually engages past the original `maxSets = 4`
   ceiling — see V16 below for the exact extended shape); Part 2's own
   `REQUIRE(rebuildResult.isErr())` → `REQUIRE(rebuildResult.isOk())`
   plus its own consequent assertions (candidate count, address
   identity, matching every other successful format-rebuild test in
   this same file); remove the `[known_limitation]` Catch2 tag; rewrite
   the surrounding multi-paragraph comment block (`:876-922`) to
   describe the fix (cite Spec 0021/ADR-0064/this Plan) instead of the
   limitation, per Spec 0021 D14.

Repo state after this milestone: builds; the fixed capacity ceiling is
gone; PR #96's own test now correctly asserts success; every other
pre-existing test (including the two single-material format-rebuild
tests in the same file, `:161-456`, whose own N=1 peak already fit
`maxSets = 4` and are unaffected either way) continues passing unchanged.

### Milestone 3 — New, dedicated pool-mechanics GPU tests

1. New file `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp`,
   registered in `tests/vulkan_backend/CMakeLists.txt`'s own
   `add_executable(atlantis_vulkan_backend_gpu_tests ...)` source list
   (no new shader/CMake dependency — reuses the existing
   `minimal_mesh_shaders` copy step already wired for this target, per
   "Pre-draft verification" above). Covers V7-V15 below: reuse-after-
   destruction, the real hard ceiling, mid-creation-failure-after-growth
   leak safety, and mixed uniform-only/textured allocation.
2. New `TEST_CASE` in `tests/runtime/material_realization_gpu_tests.cpp`
   (extending the existing file, matching its own established pattern —
   not a new file, since this is a Material/Runtime-level scenario):
   an N=5 real format-change success test, deliberately chosen (Spec
   0021 D13's own "test parameter only" instruction) to push the peak
   `2*(N+1) = 12` past pool 2's own 8-set capacity into pool 3's own
   territory, proving a *second* growth event also works end to end
   through the real Material/Runtime path, not merely the first
   (V16-V17 below).

Repo state after this milestone: fully verified per the checklist below.

## Files / Modules Touched (expected)

- `src/vulkan_backend/src/vulkan_descriptor_pool_growth.h` (new)
- `src/vulkan_backend/src/vulkan_descriptor_pool_growth.cpp` (new)
- `src/vulkan_backend/src/vulkan_result.h` (modified — one new function
  declaration)
- `src/vulkan_backend/src/vulkan_result.cpp` (modified — one new
  function definition)
- `src/vulkan_backend/src/vulkan_device.h` (modified — member/accessor/
  private-method changes, per Milestone 2)
- `src/vulkan_backend/src/vulkan_device.cpp` (modified — constructor,
  destructor, `createPipeline()`, `createDevice()`, two new private
  methods, per Milestone 2)
- `src/vulkan_backend/CMakeLists.txt` (modified — one new source file)
- `tests/vulkan_backend/descriptor_pool_growth_tests.cpp` (new,
  GPU-independent)
- `tests/vulkan_backend/vulkan_result_tests.cpp` (modified — new test
  cases appended)
- `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp` (new,
  GPU-required)
- `tests/vulkan_backend/CMakeLists.txt` (modified — two new source
  files registered)
- `tests/runtime/material_realization_gpu_tests.cpp` (modified — one
  existing `TEST_CASE` flipped to success and un-tagged; one new
  `TEST_CASE` appended)

**Explicitly not touched:** `src/vulkan_backend/src/vulkan_pipeline.h`/
`.cpp` (P1 — zero changes required); any file under `src/rhi/`,
`src/renderer/`, `src/runtime/include/`; any golden image or its own
sidecar metadata; `specs/`, `adr/` (already `Approved`/`Accepted`, not
reopened by this Plan); any Android-specific path (none exists).

## Sequencing & Dependencies

Milestone 1 → Milestone 2 → Milestone 3, strictly in order — Milestone
2 calls the pure functions Milestone 1 adds; Milestone 3's own new GPU
tests exercise the real `VulkanDevice` behavior Milestone 2 implements.
No milestone depends on anything outside this Plan's own scope; Spec
0018/0019's own format-change lifecycle and Spec 0016's own descriptor-
layout shape are both consumed as fixed, already-`Approved`,
already-implemented inputs, never revisited.

## Verification Checklist

Continuously numbered, mapped to Spec 0021's own Testing & Verification
Plan / D13:

**GPU-independent (`ctest -LE gpu`), Milestone 1:**

- [ ] V1. `nextDescriptorPoolMaxSets(4) == 8`.
- [ ] V2. `nextDescriptorPoolMaxSets(32) == 64` (geometric doubling holds
  beyond the current ceiling's own last real pool size too — a pure
  function, not artificially bounded at 32).
- [ ] V3. `kInitialDescriptorPoolMaxSets == 4` and
  `kMaxDescriptorPoolCount == 4` (a direct, literal confirmation the
  named constants match Spec 0021 D5/D6's own approved values exactly —
  guards against a silent drift between this Plan's own derivation and
  the constants Implementation actually ships).
- [ ] V4. `isDescriptorPoolGrowthEligible(VK_ERROR_OUT_OF_POOL_MEMORY) ==
  true`.
- [ ] V5. `isDescriptorPoolGrowthEligible(VK_ERROR_FRAGMENTED_POOL) ==
  true`.
- [ ] V6. `isDescriptorPoolGrowthEligible(...)` is `false` for each of
  `VK_ERROR_DEVICE_LOST`, `VK_ERROR_OUT_OF_HOST_MEMORY`,
  `VK_ERROR_OUT_OF_DEVICE_MEMORY`, and one unrelated, "other" value
  (`VK_ERROR_INITIALIZATION_FAILED`, matching `vulkan_result_tests.cpp`'s
  own existing "falls back to Other" precedent) — four sub-cases,
  covering both the two explicitly-named non-eligible `VkResult`s and
  the general "anything else" catch-all.

**Real GPU (`ctest -L gpu`), Milestone 2:**

- [ ] V7. PR #96's own low-level probe (Part 1, extended): a loop of at
  least 7 `Device::createPipeline()` calls (past the original `maxSets =
  4` ceiling by at least 3) all succeed — proving growth engaged at
  least once, not merely that the ceiling stopped being hit by
  coincidence.
- [ ] V8. PR #96's own real-shape scenario (Part 2, flipped): the
  fallback + `UnlitTextured` + `LitTextured` N=2 format-change now
  returns `Ok`; `candidates.materials.size() == 2`; the rebuilt
  Pipelines are genuinely new objects (address-distinct from the old
  ones), matching this same file's own existing address-identity
  assertions in its sibling N=1 tests.
- [ ] V9. The two pre-existing, single-material (N=1) format-rebuild
  tests in the same file (`:161-456`) still pass, byte-unchanged —
  confirming this Plan's own fix does not alter behavior for a workload
  that already fit inside the historical `maxSets = 4` ceiling.

**Real GPU (`ctest -L gpu`), Milestone 3, `descriptor_pool_growth_gpu_tests.cpp`:**

- [ ] V10. **Ceiling-and-reuse test (single `TEST_CASE`, two phases,
  proving both properties without any new production introspection
  API — the indirect technique Human Review specified):** Phase 1 —
  create 60 Pipelines via direct `Device::createPipeline()` calls (the
  full `4+8+16+32` capacity across all 4 pools); all 60 succeed; the
  61st fails with exactly `PipelineCreateError::DescriptorSetAllocationFailed`
  (the hard ceiling, D6, correctly enforced — no unbounded growth).
  Phase 2 — destroy 10 of the 60 (freeing capacity in whichever
  pool(s) they came from); create 10 new Pipelines; all 10 succeed.
  Since the pool set is already at its own hard ceiling (4 pools) before
  Phase 2 begins, a 5th pool cannot legally be created — so Phase 2's
  own success is only possible if the freed capacity was found and
  reused by the creation-order scan, definitively proving reuse-before-
  growth (Spec 0021 D3/D7) without querying pool count directly.
- [ ] V11. Mid-creation-failure-after-growth leaves no leak: force a
  growth event (create enough Pipelines to exhaust pool 1), then
  trigger a `createPipeline()` failure at a step *after* successful
  descriptor-set allocation from the newly-grown pool (e.g., an invalid
  `vertexInputLayout` producing a `vkCreateGraphicsPipelines` failure);
  confirm the already-allocated set was freed (a subsequent Pipeline
  creation from that same pool still succeeds) and confirm — via
  Validation Layers clean output, not a new introspection API — that
  the newly-grown-but-ultimately-unused pool itself is neither leaked
  nor double-destroyed (implicitly proven by a clean `waitIdle()`+
  Device-destruction sequence at the test's own end, with zero
  Validation Layers hits).
- [ ] V12. Mixed uniform-only + textured allocation: alternate
  `hasSampledTextureBinding = false`/`true` (reusing the existing
  `minimal_mesh` shader pair for both — P8/"Pre-draft verification"'s
  own confirmed-legal "declared-but-unreferenced binding" approach)
  across enough `createPipeline()` calls to exhaust pool 1 and trigger
  one growth event; confirm no premature, type-specific exhaustion
  before `maxSets` itself is reached, matching P8's own derivation.
- [ ] V13. Origin-pool correctness across pools: create enough Pipelines
  to span at least 2 pool generations; destroy them in a
  different-from-creation order; confirm every `VulkanPipeline`
  destructor's own `vkFreeDescriptorSets` call succeeds (the existing
  `ATLANTIS_CHECK(freeResult == VK_SUCCESS)` in `vulkan_pipeline.cpp:28`
  — an assertion failure here would abort the test process, an
  unmistakable, already-existing signal) and zero Validation Layers
  hits occur.
- [ ] V14. Pipelines created before a growth event remain valid and
  usable after it: create one Pipeline (pool 1), force a growth event
  via other allocations, then confirm the first Pipeline's own
  `vkPipeline()`/`descriptorSet()` handles are still valid — bind and
  record a trivial draw against a real `OffscreenTarget`, submit, and
  confirm success with zero Validation Layers hits.
- [ ] V15. `VulkanDevice`'s own destruction correctly destroys every
  pool the growable set ever created (not merely the first): after a
  test that forced at least 2 pool generations, let the `Device` be
  destroyed with all Pipelines already destroyed first (matching the
  existing, pre-Spec-0021 caller-discipline invariant, "Pre-draft
  verification" above); zero Validation Layers hits, zero
  `ATLANTIS_LOG_ERROR` output from `~VulkanDevice()`'s own drain
  sequence.

**Real GPU (`ctest -L gpu`), Milestone 3, `material_realization_gpu_tests.cpp` (new `TEST_CASE`):**

- [ ] V16. N=5 real format-change success, through the real
  `rebuildMaterialsForFormatChange()`/`Renderer` path (not the low-level
  probe): 5 distinct, real Materials (a mix of `UnlitTextured`/
  `LitTextured`) realized in "Frame 1"; a real color-format change in
  "Frame 2" with the old 5-material batch and the new fallback+5-
  material candidate batch both alive (peak `2*(1+5) = 12`, exceeding
  pool 2's own 8-set capacity, forcing a *second* growth event);
  `rebuildResult.isOk()`; `candidates.materials.size() == 5`; every
  rebuilt Pipeline address-distinct from its own old counterpart.
- [ ] V17. The same N=5 scenario's own real `submit()` call (drawing
  with the new candidate batch, per Spec 0018 D9's own unmodified
  submit-safe sequence) succeeds with zero Validation Layers hits, and
  the swap-in of the new bundle only happens after that `submit()`
  returns `Ok` — matching this same file's own existing N=1/N=2 test
  structure exactly (no new lifecycle assertion invented, reusing the
  proven pattern).

**Final, whole-suite verification:**

- [ ] V18. Fresh Debug build, clean.
- [ ] V19. Fresh Release build, clean.
- [ ] V20. `ctest -LE gpu`, both configurations — every prior test count
  plus this Plan's own 6 new GPU-independent cases (V1-V6), all passing.
- [ ] V21. `ctest -L gpu`, both configurations, on real Vulkan-capable
  hardware — every prior GPU test plus this Plan's own new cases
  (V7-V17), all passing.
- [ ] V22. Vulkan Validation Layers grepped clean (zero `VUID`/
  `Validation Error`/`Validation Warning`) across full verbose GPU test
  output, both configurations.
- [ ] V23. A fresh `ATLANTIS_BUILD_TESTS=OFF` configure+build produces a
  working `atlantis_runtime.exe` with zero test executables anywhere in
  that tree.
- [ ] V24. Module/link graph unchanged: `Atlantis::VulkanBackend`'s own
  `target_link_libraries` (`PUBLIC RHI/Core/Platform`, `PRIVATE
  Vulkan::Vulkan`) confirmed byte-identical to today; no new
  `target_link_libraries` entry anywhere this Plan touches.
- [ ] V25. All five existing goldens (`minimal_cube`, `world_scene`,
  `textured_quad`, `material_demo`, `lighting_demo`) confirmed
  byte-for-byte/pixel-for-pixel unchanged (`git diff main --quiet`
  against each artifact/sidecar path) — this Plan changes zero rendered
  pixels; the golden generator is never run.
- [ ] V26. Zero new third-party dependency (confirmed by an unchanged
  `FetchContent`/`find_package` surface across every touched
  `CMakeLists.txt`).
- [ ] V27. `git diff --check` clean on the final Implementation diff.

## Rollback Plan

Revert the Implementation PR. Every change in this Plan is additive or
narrowly-scoped-modification within `vulkan_backend`'s own private
implementation and its own tests — no RHI/Renderer/Material public API,
no golden, no other module's own source is touched, so a revert carries
zero cross-module risk. `specs/README.md`/`plans/README.md`'s own
registry rows would need a follow-up correction noting the reverted
attempt, matching this codebase's own established precedent for a
reverted Implementation (not yet needed — no Implementation has landed
against this Plan).

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No delta specific to this Plan beyond what the Verification Checklist
above already states in full — every applicable item there restates or
sharpens the general Definition of Done for this Plan's own real scope.
