# Plan: Descriptor Pool Capacity Foundation

- **Spec:** [specs/0021-descriptor-pool-capacity-foundation.md](../specs/0021-descriptor-pool-capacity-foundation.md)
  (`Approved`)
- **Status:** Approved / Ready for Implementation
- **Author:** slmao
- **Human Review Approval (2026-08-29):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-29, following the centralized
  final review round recorded below — accepting this document's own
  final container/RAII design (a fixed `std::array` plus explicit live
  count, never `std::vector`), the complete scan/grow/error-mapping
  algorithm, the fixed capacity-generation table, all four
  `createPipeline()` call-site edits, the three Milestones and their
  own atomic boundaries, the full Files/Modules Touched list, and the
  complete V1-V27 Verification Checklist (including the N=6 correction
  to V16/V17 and the capacity-derivation fixes to V1-V3/V10/V13) — see
  the Plan's own "Final Review Round" section for the complete,
  itemized record. **This approval authorizes Implementation of this
  Plan only once this PR itself has merged — not before.**

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
a paired pool/size struct) — both are strictly smaller-footprint or
more-precise realizations of the same, unchanged, Approved architectural
decision, not deviations from it. Neither requires returning to
Spec/ADR review. **P2's own exact container shape was itself further
corrected during this Plan's own later final review round — see "Final
Review Round" below for the complete record; this Plan's own P2/P3/P5/P6
sections state the corrected, final design directly, not the
superseded first draft.**

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

### P2. Container choice, revised: a fixed `std::array` + live count, never a `std::vector` — full exception-safety reasoning

**Finding from this Plan's own final review round, corrected here, not
silently carried forward:** the first draft of this Plan used
`std::vector<DescriptorPoolEntry>`. This is a real exception-safety
defect, not merely a style preference:

- `std::vector::push_back()` can allocate, and a failing allocation
  throws `std::bad_alloc`. `allocateDescriptorSet()` sits directly on
  `createPipeline()`'s own call path — the render path this codebase's
  own Error Handling rules require to stay exception-free (matching
  Vulkan's own `VkResult` model). An uncaught `std::bad_alloc`
  propagating out of `createPipeline()` (a function with no
  `noexcept`/try-catch boundary anywhere above it in the existing call
  chain — `Material`/`Renderer`/`Runtime` all consume `Result<>` types,
  never `catch`) would very likely reach `main()` unhandled and
  terminate the process — a real, if rare, availability regression this
  codebase's own "recoverable runtime errors use explicit result/error
  types, not exceptions" rule exists specifically to prevent.
- **Worse, a leak is possible on that same throw path:**
  `vkCreateDescriptorPool()` can succeed (a real, valid `VkDescriptorPool`
  handle now exists) immediately before the `push_back()` call that
  would record it — if that `push_back()` then throws, the already-created
  handle is never stored anywhere. A `VkDescriptorPool` is a raw handle,
  not a C++ object with a destructor; nothing unwinds it. This is a
  real, reachable GPU-resource leak on the exception path, not merely a
  process-termination risk.
- **`reserve(kMaxDescriptorPoolCount)` up front does not fix this, only
  relocates it:** if capacity is reserved once, before any growth,
  every *subsequent* `push_back()` (up to the reserved capacity) cannot
  reallocate and — since `DescriptorPoolEntry`'s own members are
  trivial (a handle, a `std::uint32_t`) with trivial, non-throwing
  copy/move — cannot throw either. But the `reserve()` call itself can
  still throw `std::bad_alloc`, and it would have to run during
  `VulkanDevice`'s own construction (`createDevice()`, itself a
  `Result<>`-returning function with the identical "no exceptions"
  obligation) — the same architectural inconsistency, only moved to a
  different call site, not resolved.

**Fix: `std::array<DescriptorPoolEntry, kMaxDescriptorPoolCount>` plus
an explicit live count, never `std::vector`, anywhere.** Spec 0021 D6's
own hard ceiling is a small, fixed, compile-time constant (4) — a
`std::array` is not merely "equivalent with a smaller exception
surface," it is the *more correct* representation of an approved,
unchanging upper bound: it makes "never more than
`kMaxDescriptorPoolCount` pools" a structural, type-level invariant
(there is no fifth slot to write into, even in error), not a
runtime-checked one, directly serving AGENTS.md's own "prefer... standard
containers" principle by choosing the standard container that actually
matches this problem's own fixed-size shape. `std::array`'s own storage
is inline (member/stack), never heap-allocated, so **no operation on
`descriptorPools_` can ever throw** — the exception-safety concern is
eliminated structurally, not merely reduced in probability:

```cpp
// vulkan_device.h, in namespace atlantis::vulkan_backend::detail,
// immediately above class VulkanDevice.
struct DescriptorPoolEntry {
  VkDescriptorPool pool = VK_NULL_HANDLE;  // handle VALUE -- see P5's
                                            // own "Handle-value safety"
                                            // note; never a pointer/
                                            // reference into
                                            // descriptorPools_'s own
                                            // storage.
  std::uint32_t maxSets = 0;               // this pool's own maxSets,
                                            // as passed to
                                            // vkCreateDescriptorPool --
                                            // tracked here because
                                            // Vulkan has no query call
                                            // for it. (Default values
                                            // are defensive only --
                                            // every real read is
                                            // already bounded by
                                            // descriptorPoolCount_,
                                            // below, so an untouched
                                            // slot is never actually
                                            // used.)
};
```

`VulkanDevice`'s own private members (replacing the single
`VkDescriptorPool descriptorPool_`):

```cpp
// Spec 0021/ADR-0064: a fixed-size array, sized to the Approved hard
// ceiling at compile time -- never a std::vector (see this Plan's own
// Plan Review, item 1, for the full exception-safety reasoning). Only
// indices [0, descriptorPoolCount_) are ever live; every entry at or
// past that index holds DescriptorPoolEntry's own default value and is
// never read, written past its own creation, or destroyed.
std::array<detail::DescriptorPoolEntry, detail::kMaxDescriptorPoolCount> descriptorPools_{};
std::size_t descriptorPoolCount_ = 0;
```

This is a concretization of Spec 0021/ADR-0064's own illustrative
`std::vector<VkDescriptorPool>` code, not a deviation from the approved
model — the approved *decision* ("a Device-owned, growable set of
pools, tried in creation order, up to a 4-pool hard ceiling") is
unchanged; the exact C++ container realizing "growable... up to a fixed
ceiling" is a Plan-time concretization, and this Plan's own final
review found the fixed-array shape to be *more* faithful to that exact
approved decision than a dynamically-sized one, not merely equivalent.

### P3. New private module: `vulkan_descriptor_pool_growth.h`/`.cpp` — a fixed table, not a computed function

**Finding from this Plan's own final review round, corrected here:** the
first draft computed each pool generation's own size via a generic
`nextDescriptorPoolMaxSets(x) -> x * 2` function. Every one of that
function's own call sites in this Plan happens to be gated by a ceiling
check first, so it can never actually be invoked to compute a
disallowed fifth-generation value in practice — but a generic doubling
function is still capable, *in isolation*, of producing a value Spec
0021 D5/D6 never approved (`nextDescriptorPoolMaxSets(32) == 64`, a
correct answer to a question this Spec never authorized asking). A
fixed, literal table — indexed directly by generation — locks the
growth strategy to the exact, approved four-value sequence structurally,
not merely by the discipline of a single, correctly-gated call site:

```cpp
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Spec 0021 D5/D6, ADR-0064: the descriptor-pool growth strategy's own
// fixed capacity table. Pure, GPU-independent, deterministic -- safe to
// unit-test with literal integer inputs and no real Vulkan call,
// matching vulkan_result.h's own established "pure classification"
// precedent. A fixed table, not a computed doubling function -- see
// this Plan's own Plan Review, item 5, for why: this locks every real
// pool's own maxSets to the literal, Approved four-value sequence
// (4, 8, 16, 32), structurally incapable of producing a fifth-
// generation or otherwise unapproved value, even in isolation from its
// own call site's own ceiling check.
namespace atlantis::vulkan_backend::detail {

// The total number of pools VulkanDevice's own growable set may ever
// contain (Spec 0021 D6) -- a leak/defect safety net, never a
// content-scaling ceiling.
inline constexpr std::size_t kMaxDescriptorPoolCount = 4;

// Index 0 is the initial pool (createDevice()'s own one-time creation);
// index 1 is the first growth generation; and so on. Geometric
// doubling from 4 (Spec 0021 D5) -- written here as the four literal,
// Approved values themselves, not derived at runtime, so this table
// alone is the single, complete, exact contract. Summing all four
// gives the real, current hard ceiling on concurrent descriptor sets:
// 4+8+16+32 = 60.
inline constexpr std::array<std::uint32_t, kMaxDescriptorPoolCount> kDescriptorPoolMaxSetsByGeneration = {4, 8, 16,
                                                                                                            32};

// kDescriptorPoolMaxSetsByGeneration[generationIndex]. generationIndex
// must be < kMaxDescriptorPoolCount -- a violated precondition here is
// a programmer error (every real call site is already gated by
// VulkanDevice's own ceiling check before this is ever called), not a
// recoverable runtime condition, so it is an ATLANTIS_CHECK, matching
// AGENTS.md's own "programmer errors are assertions, not error
// returns" rule -- never a std::optional or an out-of-range value
// silently substituted.
[[nodiscard]] std::uint32_t descriptorPoolMaxSetsForGeneration(std::size_t generationIndex);

}  // namespace atlantis::vulkan_backend::detail
```

```cpp
// vulkan_descriptor_pool_growth.cpp
#include "vulkan_descriptor_pool_growth.h"

#include <atlantis/assert.h>

namespace atlantis::vulkan_backend::detail {

std::uint32_t descriptorPoolMaxSetsForGeneration(std::size_t generationIndex) {
  ATLANTIS_CHECK(generationIndex < kMaxDescriptorPoolCount);
  return kDescriptorPoolMaxSetsByGeneration[generationIndex];
}

}  // namespace atlantis::vulkan_backend::detail
```

`descriptorPoolCount_` (P2) doubles as the generation index of the
*next* pool to create — when `descriptorPoolCount_ == k`, generations
`0..k-1` already exist, so the next one to create is generation `k`
itself: `descriptorPoolMaxSetsForGeneration(descriptorPoolCount_)`.

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

// Scans descriptorPools_[0, descriptorPoolCount_) in creation order,
// allocating one VkDescriptorSet of the given layout from the first
// pool with capacity -- naturally reusing whatever capacity an earlier
// vkFreeDescriptorSets call already returned to any of them (Spec 0021
// D3/D7). Grows (creates exactly one new pool, per the fixed
// generation table, P3) and retries exactly once, only after every
// existing pool has failed for a growth-eligible reason and the hard
// ceiling (kMaxDescriptorPoolCount) has not yet been reached. On
// success, outDescriptorSet/outOriginPool are both set and this
// returns std::nullopt; on any failure, neither out-param is touched
// and this returns PipelineCreateError::DescriptorSetAllocationFailed
// -- the only error this function can ever produce (P7's own complete
// mapping table).
[[nodiscard]] std::optional<atlantis::rhi::PipelineCreateError> allocateDescriptorSet(
    VkDescriptorSetLayout layout, VkDescriptorSet& outDescriptorSet, VkDescriptorPool& outOriginPool);
```

Exact algorithm (`vulkan_device.cpp`, directly mappable to code):

```cpp
std::optional<atlantis::rhi::PipelineCreateError> VulkanDevice::allocateDescriptorSet(
    VkDescriptorSetLayout layout, VkDescriptorSet& outDescriptorSet, VkDescriptorPool& outOriginPool) {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  // Step 1: scan every existing (live) pool, in creation order.
  for (std::size_t i = 0; i < descriptorPoolCount_; ++i) {
    allocInfo.descriptorPool = descriptorPools_[i].pool;
    VkDescriptorSet set = VK_NULL_HANDLE;
    const VkResult result = vkAllocateDescriptorSets(device_, &allocInfo, &set);
    if (result == VK_SUCCESS) {
      outDescriptorSet = set;
      outOriginPool = descriptorPools_[i].pool;
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
  if (descriptorPoolCount_ >= detail::kMaxDescriptorPoolCount) {
    return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
  }

  // Step 3: grow -- create exactly one new pool, generation
  // descriptorPoolCount_ (P3's own fixed table, never computed).
  const std::uint32_t newMaxSets = detail::descriptorPoolMaxSetsForGeneration(descriptorPoolCount_);
  const VkDescriptorPool newPool = detail::createDescriptorPoolOfSize(device_, newMaxSets);
  if (newPool == VK_NULL_HANDLE) {
    // Pre-publish failure (P6's own "two distinct phases" note) --
    // descriptorPoolCount_ is NOT incremented; no handle exists to leak.
    return atlantis::rhi::PipelineCreateError::DescriptorSetAllocationFailed;
  }
  // Publish: a plain assignment into an already-allocated array slot,
  // immediately following pool creation with zero intervening fallible
  // operation -- cannot throw (DescriptorPoolEntry's own members are
  // trivial), cannot leave newPool unpublished (P6's own "Pool
  // RAII/publish" note explains why no separate guard type is needed
  // here). Kept in descriptorPools_ regardless of the retry's own
  // outcome below (Spec 0021 D9) -- safe, since no set has been
  // allocated from it yet.
  descriptorPools_[descriptorPoolCount_] = detail::DescriptorPoolEntry{newPool, newMaxSets};
  ++descriptorPoolCount_;

  // Step 4: retry exactly once against the new pool. No loop, no
  // recursive call back into this function -- this is the ONE retry
  // Spec 0021 D3 specifies, never a second growth attempt within the
  // same call.
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
`descriptorPoolCount_` (at most `kMaxDescriptorPoolCount == 4`,
enforced by Step 2's own check before any growth, and structurally
incapable of exceeding the fixed array's own size regardless); Step 4
is a single, non-looping, non-recursive attempt. Worst case, this
function issues at most `kMaxDescriptorPoolCount + 1 == 5`
`vkAllocateDescriptorSets` calls and at most one
`vkCreateDescriptorPool` call, always terminating — no unbounded loop,
and no recursive growth, is possible.

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
calls in the same function are also not RAII-guarded.

**Scenario-by-scenario re-derivation, confirming the algorithm above
against every case Human Review named:**

| Scenario | Trace through the algorithm above | Outcome |
|---|---|---|
| pool 0 `FRAGMENTED_POOL`, pool 1 `OUT_OF_POOL_MEMORY`, pool 2 succeeds | Step 1: `i=0` growth-eligible, continue; `i=1` growth-eligible, continue; `i=2` `VK_SUCCESS`, return | Success, origin pool = pool 2 |
| pool 0 `VK_ERROR_DEVICE_LOST` | Step 1: `i=0`, `isDescriptorPoolGrowthEligible` false, immediate return | `DescriptorSetAllocationFailed`; pool 1 is **never tried** |
| All existing pools capacity-failed, `descriptorPoolCount_ < 4` | Step 1 loop exhausts with no success/immediate-fail; Step 2 passes (count < ceiling); Step 3 creates generation `count`, publishes, retries once | Success or `DescriptorSetAllocationFailed` depending on the retry — never a second growth attempt either way |
| New pool's first allocation still returns a capacity error | Step 4's `retryResult != VK_SUCCESS` returns immediately | `DescriptorSetAllocationFailed`; no recursion back to Step 1, no second growth |
| `descriptorPoolCount_ == 4` | Step 2's own check fails before Step 3 ever runs | `DescriptorSetAllocationFailed`; no 5th pool created, no array write past index 3 |
| `vkCreateDescriptorPool` itself fails (Step 3) | `newPool == VK_NULL_HANDLE`; the array-write/`++descriptorPoolCount_` lines are never reached | `DescriptorSetAllocationFailed`; `descriptorPoolCount_` unmodified, no handle to leak |
| An unrecognized/"other" `VkResult` | `isDescriptorPoolGrowthEligible` returns `false` for anything other than the two named values | Immediate `DescriptorSetAllocationFailed`, identical to the `DEVICE_LOST` row above |

### P6. `createDescriptorPoolOfSize()` — one shared helper for both the initial pool and every grown pool; `createPipeline()`'s four call-site edits

**Finding from this Plan's own final review round:** the first draft
gave `createDevice()`'s own initial-pool creation and the new growth
path two *separate* pieces of pool-creation code — a real, avoidable
drift risk (Human Review's own explicit concern). Fixed: one shared,
free function, called from both places, so they cannot drift apart —
not merely documented to match, but the *same code* both times:

```cpp
// vulkan_device.cpp, anonymous namespace, alongside the existing guard
// classes (InstanceGuard/DeviceGuard/etc.). The single source of truth
// for "what one descriptor pool in this Device's own growable set
// looks like" -- called by BOTH createDevice()'s own one-time initial-
// pool creation (generation 0) AND VulkanDevice::allocateDescriptorSet()'s
// own runtime growth path (generations 1-3). Returns VK_NULL_HANDLE on
// vkCreateDescriptorPool failure; the two call sites map that
// differently (createDevice(): DeviceCreateError::DeviceCreationFailed,
// unchanged, existing path; allocateDescriptorSet():
// PipelineCreateError::DescriptorSetAllocationFailed, an ordinary,
// expected-to-be-rare runtime outcome, never a Device-construction-
// time failure) -- this function itself makes no judgment about which.
[[nodiscard]] VkDescriptorPool createDescriptorPoolOfSize(VkDevice device, std::uint32_t maxSets) {
  // Both descriptor types always sized equal to maxSets itself (P8's
  // own derivation) -- identical shape at every generation, by
  // construction, since every generation calls this same function.
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
  if (vkCreateDescriptorPool(device, &createInfo, nullptr, &pool) != VK_SUCCESS) {
    return VK_NULL_HANDLE;
  }
  return pool;
}
```

`createDevice()`'s own pool-creation block (`vulkan_device.cpp:1411-1440`)
becomes a call to this shared function
(`detail::createDescriptorPoolOfSize(device, detail::kDescriptorPoolMaxSetsByGeneration[0])`),
still guarded by the existing, unchanged `DescriptorPoolGuard` — no
structural change to `createDevice()`'s own two-phase construction
flow, only the pool-sizing/creation code itself now shared.

**Pool RAII and publish order, the two distinct phases named
explicitly (never conflated, per Human Review's own instruction):**

1. **Pre-publish (inside `createDescriptorPoolOfSize()`):** if
   `vkCreateDescriptorPool` itself fails, no handle exists at all —
   nothing to guard, nothing to publish, nothing to clean up. This is
   the `VK_NULL_HANDLE`-return row in P5's own scenario table above.
2. **Publish (`allocateDescriptorSet()`'s own Step 3, immediately after
   a successful `createDescriptorPoolOfSize()` call):** the array-slot
   assignment and `++descriptorPoolCount_` are the *only* two
   statements between "a new, valid pool handle exists" and "the pool
   is live in the Device-owned collection" — and neither can fail
   (P2's own array-based redesign: no allocation, trivial
   copy-assignment). **This is why no separate local RAII guard type is
   introduced for the growth path**, unlike `createDevice()`'s own
   `DescriptorPoolGuard`: that guard exists because `createDevice()`'s
   own construction sequence has *multiple*, real, intervening fallible
   operations between resource creation and final publish (five
   separate guarded resources, `std::make_unique<VulkanDevice>` itself
   able to fail); the growth path has zero such intervening operations,
   so the "guard until published" property holds by direct construction
   of the code, not by an added type.
3. **Post-publish, first-allocation-still-fails (Spec 0021 D9's own
   "kept, not rolled back" case):** a *distinct*, later phase — the
   pool is already live in `descriptorPools_` (`descriptorPoolCount_`
   already incremented) by the time Step 4's own retry is even
   attempted; if that retry fails, the pool is not un-published, exactly
   as Spec 0021 D9 specifies. This is never the same code path as item 1
   above — item 1 never increments `descriptorPoolCount_` at all.

`createPipeline()`'s own four required edits (`vulkan_device.cpp`,
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
   constructor parameter (P1, restated: **`VulkanPipeline` needs zero
   changes** — its own existing single `descriptorPool_` field and
   constructor parameter already mean exactly "the specific pool my one
   descriptor set belongs to"; nothing in this Plan adds a field to
   it — every place elsewhere in an earlier draft of this Plan that
   still implied otherwise has been removed).

**`~VulkanDevice()`'s own destruction order, and moved-from safety,
re-confirmed fresh (not assumed):** the destructor loop becomes
`for (std::size_t i = 0; i < descriptorPoolCount_; ++i) { vkDestroyDescriptorPool(device_, descriptorPools_[i].pool, nullptr); }`
— bounded by the live count, never touching an unused (default,
`VK_NULL_HANDLE`) array slot; order among iterations is immaterial
(P0's own "Pre-draft verification" citation of the existing five
Phase-2 `.release()` calls' own "order does not matter" comment applies
identically here — no pool depends on another). `VulkanDevice`'s own
copy/move constructor and copy/move assignment operator are all four
explicitly `= delete`d (`vulkan_device.h:96-99`, re-confirmed by direct
reading this round, unchanged by this Plan) — a `VulkanDevice` can
never be moved from, so a "moved-from double-destroy" is not merely
handled, it is structurally unreachable: no code path in this codebase
can ever produce a second `VulkanDevice` instance holding a copy of
`descriptorPools_`' own live entries.

### P7. Complete error-mapping table (every real outcome this Plan touches)

| Real outcome | Maps to |
|---|---|
| `vkCreateDescriptorPool` fails during `createDevice()`'s own one-time initial-pool creation | `DeviceCreateError::DeviceCreationFailed` — unchanged, pre-existing path, this Plan does not touch it |
| `vkCreateDescriptorPool` fails inside `createDescriptorPoolOfSize()` during a runtime growth attempt (Step 3) | `createDescriptorPoolOfSize()` returns `VK_NULL_HANDLE`; `allocateDescriptorSet()` maps this to `PipelineCreateError::DescriptorSetAllocationFailed`, `descriptorPoolCount_` left unmodified |
| `vkAllocateDescriptorSets` against an existing pool → `VK_ERROR_OUT_OF_POOL_MEMORY` | growth-eligible (P4); scan continues to the next existing pool, or triggers growth if none remain |
| `vkAllocateDescriptorSets` against an existing pool → `VK_ERROR_FRAGMENTED_POOL` | growth-eligible (P4); same as above |
| `vkAllocateDescriptorSets` → `VK_ERROR_DEVICE_LOST` | **not** growth-eligible (P4); immediate `PipelineCreateError::DescriptorSetAllocationFailed`, no further pool tried, no growth attempted |
| `vkAllocateDescriptorSets` → `VK_ERROR_OUT_OF_HOST_MEMORY` | **not** growth-eligible; immediate `DescriptorSetAllocationFailed`, same as above |
| `vkAllocateDescriptorSets` → `VK_ERROR_OUT_OF_DEVICE_MEMORY` | **not** growth-eligible; immediate `DescriptorSetAllocationFailed`, same as above |
| Every existing pool growth-eligible-exhausted, and `descriptorPoolCount_ >= kMaxDescriptorPoolCount` (the hard ceiling already reached) | `DescriptorSetAllocationFailed` — the pool set never grows past 4 pools |
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
identically to every grown pool via the shared `createDescriptorPoolOfSize()`,
P6):** every pool's own `maxSets`, `UNIFORM_BUFFER` descriptor count,
and `COMBINED_IMAGE_SAMPLER` descriptor count are all set to the
**identical** value. Reading directly from the fixed, Approved table
(`kDescriptorPoolMaxSetsByGeneration = {4, 8, 16, 32}`, P3):

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
`createDescriptorPoolOfSize()` (called identically by both `createDevice()`'s
own initial-pool creation and every growth event, P6) always sets
`UNIFORM_BUFFER count == maxSets` for every pool, `UNIFORM_BUFFER`
capacity and `maxSets` are
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
`kDescriptorPoolMaxSetsByGeneration`'s own fixed table (P3), restated
here only to make the real, current headroom legible for this Plan's own
review, exactly as Spec 0021 D6 requires ("never a content-scaling
ceiling," "checked, not assumed").

## Plan Review (drafting-time self-review)

A centralized self-review of this Plan's own draft, checking it against
nine specific concerns before presenting it for Human Review — findings
recorded here, not silently absorbed, matching this codebase's own
established Plan Review disclosure precedent (Plan 0020's identical
section). **This Plan remains `In Review` after this section — no
finding here constitutes a Human Review approval; that decision belongs
to the human maintainer.** (**Superseded in part by this Plan's own
later "Final Review Round" below**, which corrected item 3's own
container choice from `std::vector` to a fixed `std::array` — this
section is kept, unedited otherwise, as the historical record of this
Plan's own first-draft self-review, matching this codebase's own
"record findings, don't silently absorb them" discipline; do not read
item 3 below as describing the final, `std::vector`-based design — it
does not use one.)

1. **The `4` pools / `60` sets figure is consistent with Spec 0021 D6
   exactly** — re-checked: `kMaxDescriptorPoolCount = 4` pools total,
   geometric doubling, `4+8+16+32 = 60`. V3 makes this a direct, literal
   test assertion, not merely a Plan-prose claim.
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
   `outOriginPool` from a local value, never from a pointer/reference
   into `descriptorPools_`'s own storage — confirmed by direct reading
   of the algorithm's own final draft, not merely asserted. **(This
   Plan's own later final review round replaced the container itself
   with a fixed `std::array`, for independent exception-safety reasons —
   see "Final Review Round" below — which makes this specific concern
   moot rather than merely addressed: a fixed array never reallocates
   at all.)**
4. **Every `createPipeline()` failure path frees to the correct pool** —
   re-traced all post-allocation failure branches (P6's own edits):
   pipeline-layout failure, `vkCreateGraphicsPipelines` failure, and
   (inside `allocateDescriptorSet()` itself) the failed-retry-after-
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

## Final Review Round (2026-08-29) — closed findings, recorded before approval

A single, centralized final review round verified this Plan's own
concrete implementation shape against real C++/Vulkan safety concerns
across ten specific areas: container choice and exception safety, pool
RAII/publish/destruction order, the allocation-result type, exact scan/
grow termination, geometric-capacity/integer safety, every
`createPipeline()` failure path, initial-vs-grown pool configuration
consistency, test realism, `DeviceLost`/lifecycle, and documentation/
atomic boundaries. Every item below was closed with a real, disclosed
fix to this Plan's own document — none required objecting to the
Approved Spec/ADR, changing the RHI public API, the Pipeline ownership
model, or the four-pool/60-set contract:

1. **`std::vector<DescriptorPoolEntry>` was a real exception-safety
   defect, not a style question — replaced with a fixed
   `std::array<DescriptorPoolEntry, kMaxDescriptorPoolCount>` plus an
   explicit live count.** `push_back()` can throw `std::bad_alloc` on
   the render path this codebase's own Error Handling rules require to
   stay exception-free, and — more seriously — a successfully-created
   `VkDescriptorPool` handle could leak if the *following* `push_back()`
   call then threw (nothing else would ever reference that handle).
   `reserve()`-up-front does not resolve this, only relocates the same
   throw risk to `VulkanDevice`'s own construction. Since the Approved
   ceiling is a small, fixed, compile-time constant, a `std::array` is
   the *more correct* representation, not merely a smaller-exception-
   surface substitute — it makes the four-pool ceiling a structural,
   type-level invariant. See P2's own full reasoning.
2. **The initial-pool-creation path (`createDevice()`) and the runtime
   growth path used two separate, independently-written pieces of pool-
   creation code — unified into one shared function,
   `createDescriptorPoolOfSize()`, called identically by both.** This
   was a real, avoidable drift risk (Human Review's own explicit
   concern) — the two paths can no longer diverge because they now
   execute the literal same code. See P6.
3. **"Hold the new pool in a local RAII guard until published" was
   asked for explicitly — this Plan states, rather than merely assumes,
   why no separate guard *type* is needed for the growth path, unlike
   `createDevice()`'s own `DescriptorPoolGuard`.** With the fixed-array
   redesign (item 1), the publish step (an array-slot assignment plus
   an increment) is the literal next statement after pool creation
   succeeds, with zero intervening fallible operation — the "guarded
   until published" property holds by direct construction of the code
   itself. `createDevice()`'s own guard exists because *that* sequence
   has multiple, real, intervening fallible operations across five
   separate resources; the growth path does not share that shape. See
   P6's own "Pool RAII and publish order" note, which also names the
   two genuinely distinct phases (pre-publish failure vs. post-publish-
   retry-fails) explicitly, never conflating them.
4. **A generic `nextDescriptorPoolMaxSets(x) -> x*2` function was
   replaced with a fixed, literal table, `kDescriptorPoolMaxSetsByGeneration
   = {4, 8, 16, 32}`, indexed by generation.** The doubling function was
   never actually reachable beyond the approved four generations (its
   one call site was already ceiling-gated), but a fixed table locks
   the growth strategy to the exact, Approved sequence *structurally*,
   not merely by call-site discipline — directly closing Human Review's
   own "don't let a surface-generic helper secretly permit a fifth
   generation" concern. See P3.
5. **`descriptorPoolMaxSetsForGeneration()`'s own out-of-range input is
   an `ATLANTIS_CHECK`, never a recoverable error type** — matching
   AGENTS.md's own "programmer errors are assertions, not error
   returns" rule exactly; V3 states explicitly that this path is not,
   and should not be, exercised by a Catch2 assertion, matching this
   codebase's own existing precedent for every other `ATLANTIS_CHECK`-
   guarded function.
6. **V16's own N=5 draft was arithmetically wrong — corrected to N=6
   with a full frame-by-frame trace, not a re-assertion of the naive
   `2*(N+1)` peak formula.** The naive peak formula alone does not
   determine which pool generation a real scenario reaches, because the
   real, reuse-first scan algorithm means Frame 1's own initial
   realization already builds cumulative pool capacity that Frame 2's
   own format-change allocations partially reuse. Tracing N=5 by hand
   shows Frame 2's own 6 new allocations fit *exactly* inside pool0+
   pool1's own combined 12-set capacity (already sized during Frame 1)
   with zero overflow — no third pool is ever created. N=6 is the real,
   smallest N that forces one. See V16's own full trace table.
7. **The reuse test (V10) and the origin-pool-correctness test (V13)
   both now compute their own real capacity totals from
   `kDescriptorPoolMaxSetsByGeneration`/`kMaxDescriptorPoolCount`
   directly, never a hand-copied literal** — requiring one new,
   narrowly-scoped `target_include_directories` addition to
   `atlantis_vulkan_backend_gpu_tests` (Milestone 3 item 1), confirmed
   to add no link-graph change (V24). V10 also now explicitly confirms
   its own dedicated `Device` carries no other, untracked Pipeline that
   could confound the capacity-total derivation, and states its own
   real cost (80 Pipeline creations in one `TEST_CASE`) honestly rather
   than silently.
8. **V13 now requires two independent proofs of origin-pool
   correctness (Validation Layers clean *and* a real reallocation
   success), never either alone** — a wrong-pool free could plausibly
   pass one check while failing the other; Human Review's own explicit
   instruction is now a structural requirement of the test itself, not
   a suggestion left to Implementation's own judgment.
9. **`VulkanDevice`'s own move/copy special members were re-confirmed,
   fresh, this round — not assumed** — all four (`copy ctor`, `copy
   assign`, `move ctor`, `move assign`) are `= delete`d
   (`vulkan_device.h:96-99`), confirming a "moved-from double-destroy"
   of `descriptorPools_` is structurally unreachable, not merely
   unlikely. See P6's own closing note.
10. **Milestone boundaries re-verified complete**: Milestone 1 remains
    genuinely independent (no dead code — its own new table/classifier
    are fully unit-tested in place, simply not yet called); Milestone 2
    is confirmed to include every piece that must land atomically (the
    fixed-array member, the shared creation helper, the allocation
    result type, all four `createPipeline()` call-site edits, the
    constructor/destructor, and both of PR #96's own test-flip edits —
    nothing deferred to a later milestone that would leave an
    intermediate tree half-fixed); Milestone 3 adds only the dedicated
    stress/reuse verification and the one CMake include-path change
    that verification needs.

No finding required changing the Accepted Spec/ADR, the RHI public API,
the `Pipeline` ownership model, or the four-pool/60-set contract. This
Plan's own container/algorithm/test corrections above are all Plan-level
concretizations of the same, unchanged, Approved architecture.

## Milestones / Task Breakdown

### Milestone 1 — Pure, GPU-independent growth-strategy modules (additive, zero interaction with `VulkanDevice` yet)

1. Add `src/vulkan_backend/src/vulkan_descriptor_pool_growth.h`/`.cpp`
   (P3): `kMaxDescriptorPoolCount`, `kDescriptorPoolMaxSetsByGeneration`,
   `descriptorPoolMaxSetsForGeneration()`. Register the new `.cpp` in
   `src/vulkan_backend/CMakeLists.txt`'s own `add_library(atlantis_vulkan_backend
   STATIC ...)` source list.
2. Add `isDescriptorPoolGrowthEligible()` to `vulkan_result.h`/`.cpp`
   (P4) — no new file, extends the existing module.
3. New GPU-independent test file
   `tests/vulkan_backend/descriptor_pool_growth_tests.cpp` (pure
   `kDescriptorPoolMaxSetsByGeneration`/`descriptorPoolMaxSetsForGeneration()`
   tests — see "Verification Checklist" V1-V3) and an extension to the
   existing `tests/vulkan_backend/vulkan_result_tests.cpp` (pure
   `isDescriptorPoolGrowthEligible()` tests — V4-V6). Register the new
   file in `tests/vulkan_backend/CMakeLists.txt`'s own
   `add_executable(atlantis_vulkan_backend_tests ...)` source list.

Repo state after this milestone: builds and passes every existing test
unchanged; the new pure table/classifier exist, are fully unit-tested,
and are not yet called from anywhere else — a deliberately inert,
independently-reviewable first step.

### Milestone 2 — `VulkanDevice`'s pool-set core, atomic, together with PR #96's own test flip

**This milestone is a single, non-splittable commit** (per this Plan's
own "Pre-draft verification" finding: flipping PR #96's own N=2 test to
expect success cannot land separately from the fix itself without
leaving an intermediate, semantically-false test state):

1. `vulkan_device.h`: replace `VkDescriptorPool descriptorPool_;`
   (line 186) with the fixed-size
   `std::array<detail::DescriptorPoolEntry, detail::kMaxDescriptorPoolCount>
   descriptorPools_{};` plus `std::size_t descriptorPoolCount_ = 0;`
   (P2 — never a `std::vector`, per this Plan's own exception-safety
   finding); add the `DescriptorPoolEntry` struct declaration (P2)
   immediately above `class VulkanDevice`; declare
   `allocateDescriptorSet()` (P5) as a new private member (no
   `createGrownDescriptorPool()` member — pool creation is a single,
   shared, anonymous-namespace free function, P6, called directly);
   **remove** the `descriptorPool()` accessor (line 135 — confirmed
   zero callers, "Pre-draft verification" above); update the
   file-level header comment (lines 36-43) and the member's own doc
   comment (lines 180-185) to describe the fixed-size growable set and
   cite Spec 0021/ADR-0064, replacing the now-superseded "Section 10...
   maxSets = 4" single-pool description.
2. `vulkan_device.cpp`:
   - Anonymous namespace (alongside `InstanceGuard`/`DeviceGuard`/etc.,
     `vulkan_device.cpp:257-401`): add `createDescriptorPoolOfSize()`
     (P6) — the one, shared pool-creation function both call sites
     below use.
   - Constructor (`vulkan_device.cpp:405-421`): body sets
     `descriptorPools_[0] = detail::DescriptorPoolEntry{descriptorPool,
     detail::kDescriptorPoolMaxSetsByGeneration[0]};
     descriptorPoolCount_ = 1;` — the constructor's own parameter list
     is unchanged (`createDevice()`'s own guard-based flow still
     creates exactly one pool at Device construction, via the shared
     helper, item 4 below).
   - `~VulkanDevice()` (`vulkan_device.cpp:423-456`): replace the single
     `vkDestroyDescriptorPool(device_, descriptorPool_, nullptr);` call
     with a loop `for (std::size_t i = 0; i < descriptorPoolCount_; ++i)`
     destroying `descriptorPools_[i].pool` (bounded by the live count,
     order-independent, per "Pre-draft verification" above; moved-from
     safety re-confirmed via `VulkanDevice`'s own four explicitly
     `= delete`d copy/move special members, P6).
   - Add `allocateDescriptorSet()`'s own body (P5) as a new
     `VulkanDevice::` member-function definition.
   - `createPipeline()`: apply P6's own four exact edits (lines
     900-913, 930, 1047, 1052).
   - `createDevice()`'s own pool-creation block
     (`vulkan_device.cpp:1411-1440`): replace the entire inline
     `VkDescriptorPoolSize`/`VkDescriptorPoolCreateInfo`/
     `vkCreateDescriptorPool` sequence with a single call to the new,
     shared `detail::createDescriptorPoolOfSize(device,
     detail::kDescriptorPoolMaxSetsByGeneration[0])` (P6) — still
     guarded by the existing, unchanged `DescriptorPoolGuard`; update
     the surrounding comment (lines 1411-1422) to state the new
     justification (Spec 0021 D5: a small, cheap first pool,
     correctness now guaranteed by growth, not by this number being
     "big enough") in place of the now-superseded "Plan 0007 Section
     10... single-Material... peak of 2, doubled" reasoning — matching
     AGENTS.md's own "update or remove a comment in the same change
     that makes it stale" rule.
3. `tests/runtime/material_realization_gpu_tests.cpp`: flip PR #96's
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

1. `tests/vulkan_backend/CMakeLists.txt`: add
   `target_include_directories(atlantis_vulkan_backend_gpu_tests PRIVATE
   ${CMAKE_SOURCE_DIR}/src/vulkan_backend/src)` — matching the
   GPU-independent target's own existing precedent — so the new GPU
   test file below can `#include "vulkan_descriptor_pool_growth.h"` and
   compute its own real capacity totals directly from
   `kDescriptorPoolMaxSetsByGeneration`/`kMaxDescriptorPoolCount`, never
   a hand-copied literal that could silently drift from the real,
   Approved constants (Human Review's own explicit instruction). This
   adds no new *link* dependency — `vulkan_descriptor_pool_growth.h`
   itself includes nothing beyond `<array>`/`<cstddef>`/`<cstdint>`, so
   `Vulkan::Vulkan` does not need linking here either; V24 below
   confirms this stays a pure include-path addition, not a link-graph
   change.
2. New file `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp`,
   registered in `tests/vulkan_backend/CMakeLists.txt`'s own
   `add_executable(atlantis_vulkan_backend_gpu_tests ...)` source list
   (no new shader dependency — reuses the existing `minimal_mesh_shaders`
   copy step already wired for this target, per "Pre-draft verification"
   above). Covers V7-V15 below: reuse-after-destruction, the real hard
   ceiling, mid-creation-failure-after-growth leak safety, and mixed
   uniform-only/textured allocation. This file creates its own,
   dedicated `Device` per `TEST_CASE` (matching every other GPU test in
   this codebase) — no other Pipeline is ever created against that same
   `Device` outside what each test explicitly controls, so its own
   capacity-total derivation (`std::accumulate` over
   `kDescriptorPoolMaxSetsByGeneration`, item 1 above) is never
   confounded by an untracked fixture allocation.
3. New `TEST_CASE` in `tests/runtime/material_realization_gpu_tests.cpp`
   (extending the existing file, matching its own established pattern —
   not a new file, since this is a Material/Runtime-level scenario): an
   **N=6** real format-change success test — corrected during this
   Plan's own final review round from an earlier N=5 draft, which a
   careful frame-by-frame re-derivation showed does *not* actually force
   a third pool generation (see V16's own full derivation table below
   for why N=5 stays within the first two pool generations, and why N=6
   is the real, smallest N that forces a third).

Repo state after this milestone: fully verified per the checklist below.

## Files / Modules Touched (expected)

- `src/vulkan_backend/src/vulkan_descriptor_pool_growth.h` (new)
- `src/vulkan_backend/src/vulkan_descriptor_pool_growth.cpp` (new)
- `src/vulkan_backend/src/vulkan_result.h` (modified — one new function
  declaration)
- `src/vulkan_backend/src/vulkan_result.cpp` (modified — one new
  function definition)
- `src/vulkan_backend/src/vulkan_device.h` (modified — member/accessor
  changes, one new private method, per Milestone 2)
- `src/vulkan_backend/src/vulkan_device.cpp` (modified — constructor,
  destructor, `createPipeline()`, `createDevice()`, one new anonymous-
  namespace helper, one new private method, per Milestone 2)
- `src/vulkan_backend/CMakeLists.txt` (modified — one new source file)
- `tests/vulkan_backend/descriptor_pool_growth_tests.cpp` (new,
  GPU-independent)
- `tests/vulkan_backend/vulkan_result_tests.cpp` (modified — new test
  cases appended)
- `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp` (new,
  GPU-required)
- `tests/vulkan_backend/CMakeLists.txt` (modified — two new source
  files registered, one new `target_include_directories` entry for
  `atlantis_vulkan_backend_gpu_tests`, per Milestone 3 item 1)
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

- [ ] V1. `kDescriptorPoolMaxSetsByGeneration == std::array<std::uint32_t, 4>{4, 8, 16, 32}`
  (a direct, literal confirmation the fixed table matches Spec 0021
  D5/D6's own approved four-value sequence exactly — guards against a
  silent drift between this Plan's own derivation and the constants
  Implementation actually ships) and `kMaxDescriptorPoolCount == 4`.
- [ ] V2. `descriptorPoolMaxSetsForGeneration(i) ==
  kDescriptorPoolMaxSetsByGeneration[i]` for every legal `i` in `{0, 1,
  2, 3}` — all four legal generations, individually, not merely the
  table's own literal check (V1).
- [ ] V3. `std::accumulate(kDescriptorPoolMaxSetsByGeneration.begin(),
  kDescriptorPoolMaxSetsByGeneration.end(), 0u) == 60` — the real,
  current total hard ceiling on concurrent descriptor sets, computed
  from the same constants Milestone 3's own GPU tests will read
  (Milestone 3 item 1), never a separately-hardcoded "60." Calling
  `descriptorPoolMaxSetsForGeneration()` with `generationIndex >= 4` is
  a programmer error (`ATLANTIS_CHECK`), matching AGENTS.md's own
  "programmer errors are assertions, not error returns" rule — not
  exercised by a Catch2 assertion here, matching this codebase's own
  existing precedent (no test anywhere in this repository exercises an
  `ATLANTIS_CHECK` failure path; it is a debug-build-only guard, not a
  runtime-recoverable condition this Plan's own tests need to cover).
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
  API — the indirect technique Human Review specified):** the test
  itself computes `kTotalCapacity = std::accumulate(kDescriptorPoolMaxSetsByGeneration.begin(),
  kDescriptorPoolMaxSetsByGeneration.end(), 0u)` (Milestone 3 item 1's
  own new include path — never a hardcoded "60" literal in the test
  itself, so this test tracks the real, approved constants even if a
  future amendment ever changes them). Phase 1 — create
  `kTotalCapacity` Pipelines via direct `Device::createPipeline()`
  calls on one dedicated `Device` this `TEST_CASE` owns exclusively
  (no other Pipeline is ever created against it — "Pre-draft
  verification"/Milestone 3 item 2's own confirmation); all
  `kTotalCapacity` succeed; the next (`kTotalCapacity + 1`th) fails
  with exactly `PipelineCreateError::DescriptorSetAllocationFailed`
  (the hard ceiling, D6, correctly enforced — no unbounded growth).
  Phase 2 — destroy 10 of the `kTotalCapacity` (freeing capacity in
  whichever pool(s) they came from); create 10 new Pipelines; all 10
  succeed. Since the pool set is already at its own hard ceiling (4
  pools) before Phase 2 begins, a 5th pool cannot legally be created —
  so Phase 2's own success is only possible if the freed capacity was
  found and reused by the creation-order scan, definitively proving
  reuse-before-growth (Spec 0021 D3/D7) without querying pool count
  directly. **Cost/stability, evaluated honestly, not assumed:**
  `kTotalCapacity + 20` (80) `vkCreateGraphicsPipelines` calls in one
  `TEST_CASE`, each against the checked-in, minimal `minimal_mesh`
  shader pair with no texture/vertex-buffer upload — the same
  per-call shape the existing low-level probe (V7) already exercises
  at a smaller scale; this is deliberately the one, single, more
  expensive test in this Plan dedicated specifically to proving the
  reuse property at real, full scale, not scattered redundantly across
  several tests. If this proves unacceptably slow or unstable on real
  hardware during Implementation's own verification pass, that is a
  Verification-stage finding to report, not a reason to silently
  substitute a smaller, unproven number for `kTotalCapacity`.
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
- [ ] V13. Origin-pool correctness across pools, proven **doubly**, per
  Human Review's own explicit instruction (Validation Layers *and* a
  real reallocation success, never either alone): create enough
  Pipelines to span at least 2 pool generations; destroy them in a
  different-from-creation order; **proof 1** — confirm every
  `VulkanPipeline` destructor's own `vkFreeDescriptorSets` call
  succeeds (the existing `ATLANTIS_CHECK(freeResult == VK_SUCCESS)` in
  `vulkan_pipeline.cpp:28` — an assertion failure here would abort the
  test process, an unmistakable, already-existing signal) and zero
  Validation Layers hits occur (a set freed back to the *wrong* pool
  would be exactly the kind of use-after-free/invalid-handle misuse
  Validation Layers is positioned to catch); **proof 2** — after those
  destructions, create new Pipelines and confirm they succeed by
  reusing the now-freed capacity (matching V10's own reuse technique at
  a smaller scale) — a wrong-pool free would leave the *actually*-used
  pool still reporting exhaustion even though objects were destroyed,
  which this second, independent proof would catch even if Validation
  Layers somehow did not.
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

- [ ] V16. **N=6** real format-change success, through the real
  `rebuildMaterialsForFormatChange()`/`Renderer` path (not the low-level
  probe) — **N=6, not N=5, corrected during this Plan's own final
  review round by a full frame-by-frame trace, not merely the naive
  `2*(N+1)` peak formula**, since the *naive* peak alone does not
  determine whether a third pool generation is actually reached (that
  depends on how much cumulative pool capacity Frame 1's own initial
  realization already built, per the real, reuse-first scan
  algorithm). The exact trace:

  | Step | Allocation | Pool state after |
  |---|---|---|
  | Frame 1 (format A): realize fallback + 6 materials, 7 total | alloc 1-4 fill pool 0 (generation 0, cap 4) | pool0 = 4/4 |
  | | alloc 5 exhausts pool0 → grows pool1 (generation 1, cap 8) → succeeds | pool0=4/4, pool1=1/8 |
  | | alloc 6, 7 | pool0=4/4, pool1=3/8 (5 free) |
  | *End Frame 1* | 7 live sets, 2 pools, cumulative capacity 12 | pool0=4/4, pool1=3/8 |
  | Frame 2 (format change): rebuild fallback + 6 materials, 7 NEW, old 7 still alive | new alloc 1-5 fill pool1's own remaining 5 free slots | pool1=8/8 (full) |
  | | new alloc 6 exhausts pool0 (full) and pool1 (full) → grows pool2 (generation 2, cap 16) → succeeds | pool2=1/16 |
  | | new alloc 7 | pool2=2/16 |
  | *Peak, before old batch destroyed* | 14 concurrent live sets, **3 pools** | pool0=4/4, pool1=8/8, pool2=2/16 |

  This is why **N=5 does not** force a third pool (a symmetric trace
  for N=5 shows Frame 1 ending with pool0=4/4, pool1=2/8 — 6 free —
  and Frame 2's own 6 new allocations fit exactly inside that remaining
  free capacity, 12 total = pool0+pool1's own exact combined capacity,
  with zero left over but no overflow either) — and why **N=6 is the
  real, smallest N** that does, matching Spec 0021 D13's own "past the
  first growth boundary, proving a second growth event also works"
  instruction precisely, with an exact derivation behind the chosen N,
  not an approximation. Assertions:
  `rebuildResult.isOk()`; `candidates.materials.size() == 6`; every
  rebuilt Pipeline address-distinct from its own old counterpart.
- [ ] V17. The same N=6 scenario's own real `submit()` call (drawing
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
  `target_link_libraries` entry anywhere this Plan touches. Milestone 3
  item 1's own new `target_include_directories(atlantis_vulkan_backend_gpu_tests
  PRIVATE ...)` line is confirmed to be exactly that — an include-path
  addition only, no accompanying `target_link_libraries` change, and no
  new `Vulkan::Vulkan` link on that target (`vulkan_descriptor_pool_growth.h`
  itself has no Vulkan dependency, "Milestone 3" above) — so this item's
  own "link graph unchanged" claim holds precisely, not loosely.
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
