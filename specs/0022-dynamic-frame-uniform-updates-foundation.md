# Spec: Dynamic Frame Uniform Updates Foundation

- **Status:** `Approved` (see "Human Review Approval" below). **This
  approval authorizes drafting Plan 0022 only — not any Implementation.**
- **Author:** slmao
- **Created:** 2026-08-30
- **Related Plan(s):** None yet — Plan 0022 may now be drafted against
  this Spec.
- **Related ADR(s):** [ADR-0065](../adr/0065-explicit-pre-write-submission-drain-for-frame-uniform-safety.md)
  (`Accepted`) — the one RHI surface change this Spec's recommended
  design requires. See Architectural Impact below.

## Summary

This Spec establishes a minimal, provably-safe model for updating
per-frame GPU-visible frame state — the Camera view/projection matrices
and the World Light snapshot Lighting Foundation (Spec 0019) introduced —
from the CPU each frame. Investigating the real call order this Spec
required before proposing any design found that the hazard is broader
than "the CPU might overwrite uniform bytes the GPU is still reading": the
**same** missing synchronization point also lets this frame's CPU-side
`vkUpdateDescriptorSets()` calls (for both the uniform-buffer binding and,
for textured Materials, the sampled-texture binding) rewrite a
`VkDescriptorSet` that the *previous* frame's still-possibly-executing GPU
work may still reference. Both hazards — the mapped-memory write and the
descriptor-set update — are real, currently-shipped, and are closed by
the identical fix: draining the previous frame's retained submission
before *any* of this frame's writes, updates, or recording begins, not
only before the raw `memcpy`.

## Motivation / Problem Statement

### The disclosed Lighting limitation this Spec was originally scoped to close

Spec 0019 (`Approved`, implemented) ships `World::setLight()` as a real,
working mutator of `World`'s own CPU-side light state, but Runtime
captures a single `FrameLightingData` snapshot into the GPU-visible
uniform buffer exactly once, guarded by `lightingDataCaptured_`
(`src/runtime/src/runtime_application.cpp:548`), and never re-captures it.
Closing this gap — making a runtime light change visible on the GPU at an
explicit, testable time — is this Spec's secondary goal; the primary goal
is the synchronization model below, which both Camera and Lighting must
share.

### The real, pre-existing gap this Spec's own investigation found — and its full scope

Tracing `RuntimeApplication::runFrame()`'s real, as-shipped call order
against `Renderer::drawFrame()`, `VulkanCommandList`, and
`VulkanDevice::submit()`'s exact internal order found **two** distinct,
real hazards sharing one root cause, not one:

**Hazard A — mapped-memory overwrite.** `runFrame()` writes the current
frame's view/projection matrices directly into
`cameraBuffer_->mappedData()` (`runtime_application.cpp:539-541`), and,
once, the 176-byte `FrameLightingData` snapshot at byte offset 128
(`:573-575`) — both **before** a `CommandList` is created (`:612`) and
**before** `device_->submit()` is called (`:758`).

**Hazard B — descriptor-set update while still referenced by a pending
submission.** `Renderer::drawFrame()` (`src/renderer/src/renderer.cpp:26-42`)
records, for every `DrawItem` in this frame, `cmd.bindPipeline(...)`
followed by `cmd.bindUniformBuffer(cameraUniformBuffer)` and, for a
textured Material, `cmd.bindTexture(...)` — **all during this frame's own
command recording**, i.e. between `createCommandList()` (`:612`) and
`submit()` (`:758`), the same window Hazard A occupies.
`VulkanCommandList::bindUniformBuffer()`
(`src/vulkan_backend/src/vulkan_command_list.cpp:234-269`) and
`bindTexture()` (`:271-318`) each call `vkUpdateDescriptorSets()` against
`boundDescriptorSet_` — the Pipeline's own single, persistent
`VkDescriptorSet` (`vulkanPipeline.descriptorSet()`, bound once at Pipeline
creation and reused every frame it is drawn — confirmed at
`vulkan_command_list.cpp:217`). Each method's own
`lastUpdatedDescriptorSet_`/`lastUpdatedUniformBuffer_` (and the texture
equivalents) skip only a *redundant* re-write **within the same
`VulkanCommandList` instance's own recording** (documented at
`vulkan_command_list.cpp:240-245`: re-writing a set already bound via
`vkCmdBindDescriptorSets` *earlier in the same recording* would invalidate
that command buffer — a different, already-handled concern). Because
`createCommandList()` allocates a **new** `VulkanCommandList` object every
frame (`vulkan_device.cpp:535-563`), that cache starts empty each frame,
so `vkUpdateDescriptorSets()` genuinely runs again on the very first draw
item of every frame that draws through a given Pipeline — on the same,
persistent `VkDescriptorSet` object the *previous* frame's retained
submission may still be executing against. The pipeline layout was built
without `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT` (confirmed at
`vulkan_command_list.cpp:245`'s own comment: "no UPDATE_AFTER_BIND"), so
writing to a descriptor set while a command buffer that references it is
still pending execution is not a supported pattern under Vulkan's own
synchronization requirements — this is a genuine, distinct hazard from
Hazard A, not a restatement of it, and it can affect **every** Pipeline
(one persistent descriptor set per Material) drawn in a frame, not only
the shared Camera/Lighting buffer's own bytes.

**Neither hazard is guarded by any CPU-blocking wait before it occurs.**
The one wait that would prove the *previous* frame's GPU work has
finished — `VulkanDevice::waitAndReleaseRetainedSubmission()`
(`vulkan_device.cpp:514-533`, which calls `vkWaitForFences()` on
`submissionFence_`) — runs only from *inside* `VulkanDevice::submit()`
(`vulkan_device.cpp:576`), called at `runtime_application.cpp:758` —
after both Hazard A's writes and Hazard B's descriptor updates have
already happened for this frame. `presentation_->acquireNextTarget()`
(`runtime_application.cpp:407`), the only call between the previous
frame's `submit()`/`present()` returning and the current frame's writes,
calls `vkAcquireNextImageKHR(..., acquireCompleteSemaphore_,
VK_NULL_HANDLE, ...)` (`vulkan_presentation.cpp:574-575`) — fence argument
`VK_NULL_HANDLE`, a GPU-side semaphore handoff, not a CPU wait. The one
call that would fully drain the GPU, `Device::waitIdle()`, runs only
conditionally, gated by `anyMaterialRealizedThisFrame`
(`runtime_application.cpp:795`) — not on an ordinary frame.

`Buffer::mappedData()`'s own documented contract
(`src/rhi/include/atlantis/rhi/buffer.h:24-33`) claims Phase 1's
single-frame-in-flight discipline satisfies the write-timing precondition
"structurally," for "write once per frame, immediately after
`acquireNextTarget()` returns." This trace shows no call in the current
path actually provides that guarantee before either hazard, and the
contract comment does not mention Hazard B (the descriptor-set update
timing) at all — it is scoped only to `Buffer`'s own write precondition,
not to `CommandList::bindUniformBuffer()`/`bindTexture()`'s own timing,
which is a real, independent gap this Spec's investigation is the first
to trace explicitly.

### Why this is not merely a Camera/Lighting `memcpy` race

Both hazards trace to the same root cause — nothing in the current call
graph proves the previous frame's GPU work has finished before this
frame begins mutating host-visible/GPU-referenced state — and a correct
fix must close both, using one mechanism, at one call point early enough
to precede all of: the mapped-memory writes, every `bindUniformBuffer()`/
`bindTexture()` call `Renderer::drawFrame()` issues for every drawn
Pipeline, and command recording itself. This Spec's Proposed Design below
states the exact call point and proves, by the same call-order evidence,
that it precedes both hazards for every Pipeline drawn that frame, not
only the shared Camera/Lighting buffer.

### Memory coherence — confirmed, not assumed

`VulkanDevice::createBuffer()` selects its memory type unconditionally
requiring `VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
VK_MEMORY_PROPERTY_HOST_COHERENT_BIT` for every `BufferPurpose`,
including `Uniform` (`vulkan_device.cpp:770-772` — the `switch` above it,
`:737-753`, only selects `VkBufferUsageFlags`; the memory-type selection
is unconditional across all purposes). No `vkFlushMappedMemoryRanges` or
`vkInvalidateMappedMemoryRanges` call exists anywhere in this codebase —
consistent with coherent memory, which needs neither. This confirms
`buffer.h`'s own documented coherence claim against real code: coherence
itself is real and requires no design change here. Coherence removes the
need for an explicit flush once ordering is otherwise guaranteed — it
does not, by itself, order a CPU write and a GPU read of the same
location, which is exactly the ordering Hazard A and Hazard B are
missing.

### Not yet an observed defect — expected of a real, timing-dependent race

Every existing golden is byte-identical and all `ctest -L gpu` suites
pass, including validation-layer-clean multi-frame smoke tests
(`tests/runtime/runtime_smoke_gpu_tests.cpp:91-92`, looping
`app.runFrame()` `kSmokeTestFrameCount` times). This is expected of a
genuine but timing-dependent race, not evidence against one: on the
simple scenes and fast GPU completion times every existing test exercises,
the previous frame's GPU work plausibly finishes before the next frame's
CPU reaches the write/update window, most of the time. Passing tests must
not be read as proof of absence — the actual window depends on scene
complexity and hardware timing neither this codebase's tests nor this
Spec control. Separately: ordinary Vulkan Validation Layers (core
validation) do not reliably catch "a descriptor set was updated while a
pending command buffer still references it" without the optional
Synchronization Validation feature explicitly enabled — this codebase's
own instance creation
(`src/vulkan_backend/src/vulkan_instance.cpp`/`vulkan_instance.h`) enables
`VK_LAYER_KHRONOS_validation` but was not found, in this investigation, to
enable that specific feature — which is consistent with Hazard B going
undetected by the existing "zero VUID hits" result without that result
constituting evidence the hazard is absent.

## Goals

- Camera continues to be updated correctly every frame, with the same or
  better safety than today.
- A `World::setLight()` change (and, per the Final Review Round below,
  every other World mutation that affects the published light payload)
  made at runtime becomes visible on the GPU at an explicit, testable
  frame boundary — removing Spec 0019 D1's "captured once, never updated
  again" limitation.
- The CPU never overwrites uniform bytes the GPU might still be reading,
  and never updates a descriptor set a still-pending submission may still
  reference — closing both real gaps described above, for every Pipeline
  drawn in a frame, not only Camera/Lighting's own buffer.
- Windowed and headless share one safety model — `RuntimeApplication::runFrame()`
  is the single, unforked implementation both already use (confirmed: no
  `headless`-specific branch exists in `runtime_application.cpp`; a
  headless composition root supplies an offscreen `RenderTarget` through
  the same `runFrame()`, per Spec 0010/AGENTS.md's own "windowed and
  headless share the same Renderer/RHI stack" principle) — no divergent
  sync logic is introduced.
- No multi-threaded frame orchestration is introduced; the engine remains
  single-threaded at the frame level (Phase 1 constraint, ADR-0004).

## Non-Goals

- PBR Material, Shadow, IBL, Post-processing — unrelated feature work,
  explicitly deferred per human-directed priority ordering (see
  `specs/README.md` Section B).
- Animation system.
- Multi-threaded rendering or any job system.
- A generic, arbitrary-N-frames-in-flight framework — Plan 0006's
  single-frame-in-flight baseline is not being replaced.
- Bindless rendering or descriptor indexing.
- A general-purpose GPU upload scheduler for arbitrary resources — this
  Spec is scoped to the two existing frame-uniform fields (Camera,
  Lighting) and the descriptor-set updates that publish them.
- Android, iOS, or Linux implementation — the design must remain portable
  to Android per Phase 1's target platforms; no platform-specific code is
  written this round.
- Any Editor/Client-facing API.
- A generic dependency-injection or service-locator mechanism for RHI test
  doubles — if Plan 0022 needs a minimal test seam to lock call order (no
  `FakeDevice` exists in this codebase today — confirmed by search), that
  seam is scoped to this Spec's own verification needs, not a general
  testing framework.

## Requirements

### Functional

- Camera view/projection matrices are written to GPU-visible memory once
  per frame, unconditionally, exactly as today.
- A successful frame re-extracts and publishes the complete, current
  `FrameLightingData` from `World`'s live state (see the Final Review
  Round's "Dynamic Lighting update model" below) — not only in response to
  `setLight()`, but to every World mutation that can change the published
  payload (light Transform, parent-hierarchy Transform, light entity
  creation/removal).
- Multiple World mutations affecting the same frame's update resolve to
  final-value semantics (the value read at extraction time, immediately
  after this frame's drain) — no averaging, no queued intermediate values.
- A frame that consumes zero extent, is deferred as out-of-date, hits a
  resize, or fails to submit must not publish stale/uninitialized bytes
  and must not skip a real change — the next successful frame always
  re-extracts current `World` state, so no separate "pending update" flag
  can go stale or be wrongly consumed (see the full-re-extraction model
  below).
- The mechanism must not require Material or Pipeline objects to be
  duplicated — confirmed unnecessary for the recommended model (Final
  Review Round, Buffer/Pipeline binding section).
- The new synchronization call must precede, for every frame: every
  mapped-memory write into the Camera/Lighting buffer, every
  `bindUniformBuffer()`/`bindTexture()` call `Renderer::drawFrame()` may
  issue for any Pipeline drawn that frame, and `createCommandList()`
  itself.

### Non-functional

- Performance: the new per-frame CPU wait is disclosed against the
  existing baseline (Final Review Round, Performance section) as an
  accepted Phase-1 correctness-first trade-off, not a silently absorbed
  regression.
- Memory: no new buffer allocation this Spec's recommended model
  introduces (confirmed: Model A adds no buffer).
- Portability (within the Vulkan-only Phase 1 constraint): the design
  uses only `vkWaitForFences`/`vkResetFences`, already in use today, with
  no new Vulkan feature or extension dependency.
- Other: no new third-party dependency; no new global mutable frame
  state; no new threads or locks.

## Proposed Design

### Recommended model: explicit pre-write submission drain (Model A)

Before `runFrame()` performs *any* of the mutations Hazard A/B describe,
Runtime calls a new, narrow RHI method — `Device::waitForPreviousSubmission()`
— whose Vulkan Backend implementation is exactly
`VulkanDevice::waitAndReleaseRetainedSubmission()`'s existing body,
exposed publicly. See the Final Review Round's "Full frame timing" and
"RHI API contract" sections below for the exact call point, error
semantics, and idempotence guarantees, and ADR-0065 (`Accepted`) for the
architectural record.

This is the minimal-diff fix the evidence above supports: it does not
change buffer count, ownership, or lifetime; it does not require
Material/Pipeline duplication (confirmed below); it does not require a
new `CommandList` recording capability; it closes both Hazard A and
Hazard B by relocating an already-necessary, already-occurring wait
earlier in frame order, once, at one call point that precedes both.

### Alternatives evaluated but not recommended by default

**Model B — multi-slot uniform-buffer ring.** N buffers, one written per
in-flight frame slot, the bound descriptor set rewritten to that slot's
buffer each frame. `VulkanCommandList::bindUniformBuffer()` already calls
`vkUpdateDescriptorSets()` fresh once per frame per Pipeline (Hazard B's
own evidence above), so rebinding to a different `VkBuffer` handle per
frame slot needs no `CommandList` signature change. This model does
**not** avoid Hazard B by itself, though — updating a descriptor set to
point at a *different* slot's buffer is still a `vkUpdateDescriptorSets`
call against a persistent, per-Pipeline `VkDescriptorSet`, and still needs
its own proof that the slot being written to is not the one the GPU is
still reading — the ring buffer moves *which* memory is written, not
*whether* the write/update is ordered safely, so it would need the
identical class of safety analysis this Spec performs for Model A, plus a
real answer for slot-count derivation (Plan 0006 Section 11's
single-retained-submission state machine gives N=2 as the structural
floor) and slot-selection identity (no frame-slot concept exists in
`Presentation` today — confirmed: `acquireNextTarget()` returns an image
index internal to swapchain bookkeeping, not exposed as a general
frame-slot identity). Rejected as this round's recommendation: a larger,
more invasive change for a safety property Model A already achieves with
zero new buffers.

**Model C — staging buffer + RenderGraph-recorded copy.** Write into a
CPU-only staging buffer, record a `CommandList`-issued copy into the real
uniform buffer as part of the frame's own GPU work. `CommandList` has no
buffer-to-buffer copy today — `copyRenderTargetToBuffer()` and
`copyBufferToTexture()` (`src/rhi/include/atlantis/rhi/command_list.h:79-100`)
are the only existing copy-recording methods, and neither's
source/destination shape fits a uniform-buffer update; `ResourceState`
tracking in RenderGraph (`src/rhi/include/atlantis/rhi/types.h`'s own
`ResourceState` enum and its transition machinery) is scoped to
`RenderTarget`/`Texture`/`SampledTexture` today, not `Buffer` — a real,
new RenderGraph tracking dimension, not only a new `CommandList` method.
This model still needs its own descriptor-update-timing answer for
Hazard B (the copy's *destination* buffer is the same persistent buffer
bound by the same per-Pipeline descriptor set). Rejected as this round's
recommendation: a materially larger RHI/RenderGraph surface change for a
safety property Model A already achieves.

**Rejected outright — call the existing public `waitIdle()` before every
write, unconditionally.** `Device::waitIdle()` deliberately does more
than drain the retained submission — it also drains
presentation-engine-internal state Plan 0006 Section 11 intentionally
scoped to coarser lifecycle events, not the per-frame steady-state path.

Model A is the recommendation this Spec's Human Review Approval below
adopts; Model B and Model C remain disclosed, real alternatives a future
Spec may revisit if Phase 1's single-frame-in-flight baseline itself is
ever reconsidered — not a decision this Spec makes.

## Architectural Impact

Yes — the adopted design (Model A) adds one new public method to the RHI
`Device` interface, `Device::waitForPreviousSubmission()`, and changes the
frame-lifecycle contract Runtime follows: an explicit drain now precedes
every mapped write, every descriptor-set update, and command recording,
for every frame. Neither the RenderGraph path, the Vulkan-Backend-boundary
rule, nor Renderer's own non-dependence on Platform/Vulkan Backend
changes.

[ADR-0065](../adr/0065-explicit-pre-write-submission-drain-for-frame-uniform-safety.md)
(`Accepted`) records the Model A decision, refined during this Spec's own
Final Review Round to state the exact call point and error-handling
contract precisely, closing the gap where the original draft named only
Hazard A.

## Alternatives Considered

See "Alternatives evaluated but not recommended by default" above (Model
B, Model C, and the rejected always-`waitIdle()` variant).

## Testing & Verification Plan

**Call-order proof (GPU-independent where possible):**

- The new drain call happens before every mapped-memory write into the
  Camera/Lighting buffer, before every `bindUniformBuffer()`/
  `bindTexture()` call for every Pipeline drawn that frame, and before
  `createCommandList()`. No `FakeDevice` exists in this codebase today;
  Plan 0022 must define the minimal test seam needed to lock this order
  under automated test (e.g. call-order instrumentation reachable only
  from test code, or an equivalent mechanism) — this Spec states the test
  *goal*, not the mechanism, and does not authorize a general-purpose
  DI/service-locator architecture change to achieve it (see Non-Goals).
- `Device::waitForPreviousSubmission()` is a no-op (returns `Ok`, no
  Vulkan call beyond what "nothing retained" already implies) when no
  submission is retained, and is idempotent: calling it twice in a row
  succeeds both times, the second call doing no additional waiting
  (mirrors `waitAndReleaseRetainedSubmission()`'s own existing
  `hasRetainedSubmission_` early return, `vulkan_device.cpp:517-519`).
- `submit()`'s own internal drain call becomes a verified no-op on any
  frame that already called the new method explicitly first — proven
  either by the same call-order instrumentation or by confirming
  `hasRetainedSubmission_` is `false` at that point.
- A wait failure (`Err`) results in zero buffer writes, zero descriptor
  updates, zero draw calls, and zero `submit()` calls for that frame —
  Runtime returns immediately, matching `submit()`'s own existing
  fail-fast pattern (`runtime_application.cpp:758-762`).

**Multi-frame GPU correctness:**

- Multi-frame Camera regression: Camera Transform changes between frames
  are reflected in real rendered output — proving the reordered write
  timing does not regress the existing, working Camera path.
- Multi-frame Lighting regression: Directional Light direction/color/
  intensity and Point Light position/intensity changes made via
  `World::setLight()`, and via a Light entity's own `Transform`/
  parent-hierarchy change, are each visible in real rendered pixel output
  at the approved update boundary (next successful frame).
- Final-value semantics: multiple World mutations affecting the same
  entity within one frame publish only the state read at this frame's own
  extraction point.
- No stale/undefined bytes: a frame with no World light change publishes
  byte-identical Lighting data to the previous frame.
- Non-consumption on skip paths: zero-extent, deferred/out-of-date
  acquire, resize, and submit-failure frames never publish stale data and
  never lose a pending change — the full-re-extraction model (Final
  Review Round) makes "consuming" a concept that does not apply, since
  every successful frame re-reads current `World` state independent of
  what any previous frame did or skipped.
- Format-change + dynamic Lighting interaction: a scene undergoing a
  Spec 0018 D9 format-change candidate rebuild in the same frame a
  Lighting change is present — both complete correctly and independently
  (confirmed independent in the Final Review Round's Lifecycle section).
- Windowed/headless parity: both exercise the identical
  `RuntimeApplication::runFrame()` code path — no separate test-only sync
  logic.
- Dual-level evidence: both CPU mapped-byte comparison and real GPU
  pixel-level evidence (see Golden Strategy below) — CPU-byte checks
  alone are not sufficient, per this Spec's own motivation.
- Existing goldens: all five existing goldens (`minimal_cube`,
  `world_scene`, `textured_quad`, `material_demo`, `lighting_demo`) remain
  byte-for-byte identical to `main` and pixel-zero-difference.
- Full matrix: Debug and Release, `ctest -L gpu` and `ctest -LE gpu`,
  Vulkan Validation Layers clean, a clean `ATLANTIS_BUILD_TESTS=OFF`
  build, and a module/link boundary scan.

**Golden strategy (Final Review Round decision):** dynamic-correctness
evidence (a change takes effect) is proven primarily through **programmatic,
within-test pixel/byte comparison across a controlled multi-frame
sequence** — e.g. "pixel color at (x, y) after increasing Point Light
intensity is brighter than before, by the expected relationship" — not a
new stored golden PNG, because the property under test ("output changed
as parameter X changed") is a relative claim a direct assertion proves
more reviewably than a new fixed baseline image would. The five existing
goldens are not touched or regenerated by this work. If Plan 0022 later
finds a real need for a new stored baseline (e.g. to also regression-guard
a dynamic-lighting demo scene's own default rendered appearance, the way
`lighting_demo` does for static lighting), that new golden must follow
ADR-0042's Initial baseline bootstrap two-phase process with human review
before landing — this Spec does not mandate creating one and does not
authorize skipping that process if the Plan does.

## Risks & Open Questions

Eighteen Human Review decision items were raised during this Spec's
drafting. This Spec's Final Review Round (below) traced real code to
close as many as evidence permits; each item's resolution is recorded
there. The items themselves, for reference:

1. The safety update model (Model A/B/C or another). **Resolved: Model A.**
2. If Model A: reuse `waitIdle()` vs. add a narrower method, and the
   performance cost. **Resolved: narrower `Device::waitForPreviousSubmission()`;
   cost disclosed in the Final Review Round's Performance section.**
3. If Model B: slot-count derivation, slot selection, Material/Pipeline
   duplication. **Not applicable — Model A adopted; the analysis above
   remains disclosed for a future revisit.**
4. Whether the RHI public API must change. **Resolved: yes, one method.**
5. Camera/Lighting: one struct or two buffers. **Resolved: unchanged —
   remains the existing single shared buffer (Final Review Round,
   Buffer/Pipeline binding section) — not merged or split "for tidiness."**
6. Lighting update visibility timing. **Resolved: the next successful
   frame's own extraction point, immediately after that frame's drain —
   see the Final Review Round's Dynamic Lighting model section.**
7. Whether a skipped/deferred/failed frame consumes a pending update.
   **Resolved: the concept does not apply under full re-extraction — see
   Testing & Verification Plan above.**
8. Format-change candidate vs. the dynamic uniform buffer relationship.
   **Resolved: independent — see the Final Review Round's Lifecycle
   section.**
9. Uniform buffer ownership and destruction order. **Resolved: unchanged
   from today — see the Final Review Round's Buffer/Pipeline binding
   section.**
10. Host-visible/coherent memory requirements. **Resolved: confirmed
    coherent by real code — see Motivation above.**
11. Windowed/offscreen sharing. **Resolved: already one shared
    `runFrame()` — see Goals above.**
12. `DeviceLost`/buffer-update-failure error classification. **Resolved —
    see the Final Review Round's Error Domain section.**
13. Whether a per-frame `waitIdle()`-equivalent stall is an acceptable
    Phase-1 baseline. **Resolved: yes, with an explicit future-revisit
    condition — see the Final Review Round's Performance section.**
14. Whether tests need real multi-frame GPU evidence. **Resolved: yes —
    see Testing & Verification Plan above.**
15. Whether the existing Camera path needs its own fix and coverage.
    **Resolved: yes — this Spec's central problem; coverage specified
    above.**
16. Removing static-snapshot state. **Resolved: `lightingDataCaptured_`
    is removed — see the Final Review Round's Dynamic Lighting model
    section.**
17. Thread-safety. **Resolved: confirmed single-threaded, no lock
    introduced — see the Final Review Round's RHI API Contract section
    (single-threaded, non-reentrant, matching every other `Device`
    method's existing, undocumented-but-consistent single-thread-only
    usage in this codebase).**
18. Android portability. **Resolved: uses only `vkWaitForFences`/
    `vkResetFences`, already in use today — no new extension dependency.**

No item remains open pending further Human Review beyond the approval
recorded below — see the Final Review Round for the evidence closing
each.

## Out of Scope / Future Work

Everything in Non-Goals above. This Spec does not attempt to generalize
the selected mechanism to any future per-frame GPU resource beyond
Camera and Lighting. This Spec also does not revisit Plan 0006's
single-frame-in-flight baseline itself.

## Final Review Round

A centralized final review, conducted at explicit human direction before
approval, re-traced the real `Renderer`/`CommandList`/`VulkanDevice`
implementation (not only `RuntimeApplication`) to verify the recommended
`Device::waitForPreviousSubmission()` actually closes *every* race between
the previous frame's GPU resource usage and this frame's CPU mutation —
not only the uniform-buffer `memcpy` the original draft named. This round
found the descriptor-set-update hazard (Hazard B, described in Motivation
above) was missing from the original draft's problem statement, and
closed the remaining Human Review decision items with fresh evidence.
Corrections below are made directly to this Spec's own body (this Spec
was still `In Review`, never `Approved`, until this round — not a
post-approval amendment).

**1. Race scope widened from "uniform bytes" to "uniform bytes +
descriptor-set updates" (the review's central finding).** See Motivation's
"Hazard A"/"Hazard B" split above. The fix (Model A, one call point) closes
both — verified by call-order, not simply asserted, since the recommended
call point (immediately after a successful, non-null acquire) precedes
`createCommandList()`, which precedes every `bindUniformBuffer()`/
`bindTexture()` call `Renderer::drawFrame()` issues.

**2. Full frame timing — exact call point.** Real order, confirmed
against `runtime_application.cpp`:

1. `processEvents()`.
2. Early-return guards (`!presentation_ || closeRequested_ ||
   lifecycle_.state() == Failed`) — `:403-405`.
3. `presentation_->acquireNextTarget()` — `:407`.
4. Acquire-error classification/`markFailed()` — `:408-413` — **no drain
   call on this path**: the frame is abandoned and Runtime's own
   `Failed`-state guard (step 2, next frame) prevents any further mutation;
   shutdown's existing, unconditional `waitIdle()` (Plan 0006 Section 11)
   still drains any retained submission before `Presentation`/`Device`
   destruction, so nothing is left unsynchronized.
5. Zero-extent/deferred-out-of-date early return (`acquireResult.value()
   == nullptr`) — `:414-416` — **no drain call on this path**, by design:
   draining here would be a wasted stall on a frame producing no GPU work
   at all; the next frame that actually acquires a target performs the
   drain before it does anything else.
6. **`Device::waitForPreviousSubmission()` — the new call, placed here:**
   immediately after a successful, non-null acquire, before the
   format-change candidate build (`:419` onward).
7. Format-change candidate build (`:436-456`) — read-only against
   existing state; unaffected by drain timing (see item 8 below).
8. Extent-change depth-texture recreation (`:458-471`) — unaffected;
   creates a brand-new `Texture`, never touches the retained submission.
9. `world_->updateTransforms()`, camera-matrix extraction (`:497-537`).
10. Camera mapped-memory write (`:539-541`) — now proven to occur after
    step 6's drain.
11. Lighting extraction and mapped-memory write (revised: every
    successful frame, not the former one-time guard — see item 6 below) —
    now proven to occur after step 6's drain.
12. `device_->createCommandList()` (`:612`) — after step 6's drain.
13. `realizePendingMaterials()` — new resource creation, independent of
    the retained submission (brand-new objects).
14. Draw-item construction.
15. `renderer_.drawFrame()` (`:755`) — every `bindUniformBuffer()`/
    `bindTexture()` call inside it now proven to occur after step 6's
    drain, closing Hazard B.
16. `device_->submit()` (`:758`) — its own internal
    `waitAndReleaseRetainedSubmission()` call (`vulkan_device.cpp:576`)
    is now a verified no-op (`hasRetainedSubmission_` already `false` from
    step 6), kept as a defensive, idempotent no-op for any other caller of
    `submit()` that does not call the new method first (Decision below) —
    not removed, since removing it would require proving every other
    `submit()` caller in this codebase and its tests independently calls
    the new drain first, which this Spec does not attempt to prove.
17. On `submit()` success: format-rebuild candidate swap-in (`:776-779`)
    — unaffected; this gate's own safety already depends only on *this*
    frame's `submit()` returning `Ok`, not on when the *previous* frame's
    retained submission was drained (confirmed independent — see item 8,
    Lifecycle, below).
18. Conditional `waitIdle()` on a realization frame (`:795-802`) —
    unaffected; still a coarser, separate drain for a separate purpose.
19. `presentation_->present()` (`:817`).

Resize: handled entirely at step 8, after the drain — no special-casing
needed. Submit failure: `submit()` returning `Err` already causes
`markFailed()` and an early return (`:758-762`); the *next* frame's own
step 6 drain (once the app leaves the `Failed` state, if ever — today it
does not resume) is unaffected either way, since nothing was left
retained by a failed `submit()` call (`vkQueueSubmit` failing means
`retainedSubmission_`/`hasRetainedSubmission_` are never updated,
`vulkan_device.cpp:613-619`).

**3. RHI API contract, precisely.** `Device::waitForPreviousSubmission()`
returns `Result<std::monostate, SubmitError>` (reusing `SubmitError`,
matching `waitIdle()`'s own existing signature shape) with this contract:
backend-independent (no Vulkan type crosses the RHI boundary — matches
every other `Device` method); a no-op returning `Ok` when no submission is
retained; when one is retained, waits for its completion and releases the
retained `CommandList` — after which the caller may safely mutate any
host-visible bytes or descriptor state that submission referenced; not
equivalent to `waitIdle()` (does not drain presentation-engine-internal
state); safe to call twice in a row (the second call is a verified no-op,
per the `hasRetainedSubmission_` early return already in
`waitAndReleaseRetainedSubmission()`); single-threaded, not safe for
concurrent calls (matching every other `Device` method's existing,
implicit single-thread-only contract — Phase 1's ADR-0004 baseline); and
implemented as a thin public wrapper that calls the *same* private
`waitAndReleaseRetainedSubmission()` helper `submit()` itself calls — one
implementation, never two divergent copies. Confirmed during this round:
`VulkanDevice` is the RHI's only `Device` implementation in this codebase
today (searched for `Fake`/`Mock`/`Stub` `Device` types — none exist), so
"every `Device` implementer updates atomically" has exactly one
implementer to update; Plan 0022 must re-confirm this at implementation
time in case a test double is introduced by then. The method name
`waitForPreviousSubmission()` was compared against
`drainRetainedSubmission()` and kept as originally proposed — it describes
the caller-visible effect ("what you get to safely do after calling
this"), not the internal mechanism, matching this codebase's existing
naming register (`waitIdle()`, not `drainAllGpuWork()`); no broader
frame-scheduler API was introduced for naming symmetry.

**4. Error domain, precisely.** `waitAndReleaseRetainedSubmission()`
today returns `Err` only from `vkWaitForFences()` or `vkResetFences()`
returning other than `VK_SUCCESS`, converted via the existing
`toSubmitError()` — which already maps to exactly `SubmitError::DeviceLost`
or `SubmitError::QueueSubmitFailed` (`src/rhi/include/atlantis/rhi/types.h:277-280`
— confirmed no third variant exists to represent host/device OOM
separately; `vkWaitForFences`/`vkResetFences` can return
`VK_ERROR_OUT_OF_HOST_MEMORY`/`VK_ERROR_OUT_OF_DEVICE_MEMORY`, both of
which fall into whatever `toSubmitError()`'s existing classification
already assigns them — this Spec does not widen `SubmitError` beyond
what `submit()` itself already exposes for the identical failure modes,
since the new public method is calling the identical Vulkan operations
`submit()`'s own internal call already performs). Runtime's own handling
of an `Err` from the new method mirrors its existing `submit()`-failure
handling exactly (`runtime_application.cpp:758-762`): log, classify via
the existing `classifySubmitError()`, call `lifecycle_.markFailed()`, and
return immediately — zero buffer writes, zero descriptor updates, zero
draw calls, zero `submit()` call for that frame. No error is swallowed,
downgraded to an assertion, or given a new, duplicate error domain; if a
future need for finer-grained OOM classification arises, that is a
genuine Human Review decision for that future work, not silently resolved
here.

**5. Memory coherence — confirmed.** See Motivation above
(`vulkan_device.cpp:770-772`): unconditional `HOST_VISIBLE | HOST_COHERENT`
for every `BufferPurpose`. No blocking objection.

**6. Dynamic Lighting update model — decided.** Full re-extraction, every
successful frame, after this frame's drain: the existing one-time-capture
block (`runtime_application.cpp:548-576`) is generalized to run every
frame (its `lightingDataCaptured_` guard is removed), reading
`world_->lightEntities()`/`getLight()`/`getWorldMatrix()` fresh each time
— exactly the same calls the one-time capture already makes, just
un-guarded. This model requires no new dirty-bit or revision-counter state
and, because it re-reads live `World` state rather than tracking which
specific mutation occurred, it correctly and automatically covers every
source of change this Spec's Requirements name: `setLight()`, a Light
entity's own `Transform` change, a parent-hierarchy `Transform` change
affecting a Light's world matrix, and Light entity creation/removal (each
already reflected in `world_->lightEntities()`/`getWorldMatrix()`'s own
existing, correct behavior — no new `World`-side code is implied). No two
authoritative data sets ever coexist, since the static-snapshot field is
removed, not shadowed. Rejected alternative: a revision/dirty-bit model —
adds real state (a counter or flag per light-affecting mutation site) for
a performance saving (skipping re-extraction on an unchanged frame) this
Spec has no evidence is needed at Phase 1's scene complexity; re-extracting
176 bytes from already-in-memory `World` component data every frame is not
a demonstrated cost. A future Spec may reintroduce dirty tracking with
real profiling evidence; this Spec does not pre-optimize for it.

**7. Buffer/Pipeline binding — confirmed, corrected from the original
draft's imprecise wording.** Camera and Lighting remain one shared
`Buffer` (`cameraBuffer_`, created once, `runtime_application.cpp:288-295`)
— never split or merged differently, matching decision item 5. Each
Material's `Pipeline` owns exactly one persistent `VkDescriptorSet`
(`vulkanPipeline.descriptorSet()`), created once at Pipeline-construction
time and never recreated for the Pipeline's own lifetime; `bindUniformBuffer()`/
`bindTexture()` re-issue `vkUpdateDescriptorSets()` **once per frame per
Pipeline actually drawn that frame** (skipping only an exact intra-frame
repeat across multiple `DrawItem`s sharing one Pipeline, per
`vulkan_command_list.cpp:240-245`/`:286-295`'s own existing "redundant
write" comments) — corrected from the original draft's looser "reissues
… every call" phrasing, which did not distinguish the intra-frame skip
from the cross-frame re-issue that Hazard B depends on. Because the drain
(item 2 above) now precedes every such call, every Pipeline's descriptor
set is provably safe to update, regardless of how many distinct Materials
are drawn in a frame — the fix's placement, not a per-Pipeline special
case, is what makes this general. A format-change rebuild
(Spec 0018 D9) builds an entirely new candidate `Pipeline`/`Material`
bundle with its own new descriptor set(s), bound to the *same*, unchanged
`cameraBuffer_` (`rebuildMaterialsForFormatChange()`'s own construction
path) — never requiring the shared buffer itself to be recreated or the
old bundle's descriptor sets to be touched before the new bundle's own
first bind, which happens on or after the same frame's drain, exactly
like every other Pipeline. `cameraBuffer_` outlives every Pipeline/Material
that borrows it (declared and constructed before any Material in
`RuntimeApplication`'s own member order and Bootstrap Sequencing, and
reset only during shutdown, `runtime_application.cpp:854`, after every
Pipeline/Material member has already been destroyed by C++'s own
reverse-declaration-order member destruction) — unchanged by this Spec.
No per-frame Material/Pipeline duplication is required under Model A.

**8. Lifecycle interaction — confirmed safe, no blocking objection.**
Moving the drain earlier does not affect `present()`'s own semaphore
lifecycle: `waitAndReleaseRetainedSubmission()` only ever touches
`retainedSubmission_` (the `CommandList`/`VkCommandBuffer`, released via
`retainedSubmission_.reset()`) and `submissionFence_` — never any
`VkSemaphore`. The `SubmissionSignal` `submit()` returns and `present()`
consumes (`VulkanSubmissionSignal`, `src/vulkan_backend/src/vulkan_submission_signal.h`)
wraps a `VkSemaphore` whose own destructor "performs no Vulkan call" (its
own header comment) — the semaphore's real lifetime belongs to
`VulkanPresentation`'s own per-image-index `renderFinishedSemaphores_`
(`vulkan_presentation.cpp:514-516`), never touched by `VulkanDevice`.
Sequentially: frame *N*'s own `present()` call (`runtime_application.cpp:817`)
completes, synchronously, before frame *N+1*'s `runFrame()` invocation
even begins — so frame *N+1*'s new, earlier drain call can never race
frame *N*'s own `present()`, regardless of where in frame *N+1* the drain
is placed. Descriptor pool reuse (Spec 0021) is unaffected: pool growth
only ever allocates a *new* `VkDescriptorSet` for a *new* Material's
Pipeline; it never touches an existing Pipeline's already-allocated
descriptor set, so it does not interact with Hazard B's own fix at all.
Format-change candidate publish (Spec 0018 D9) is confirmed independent
in item 7 above. Upload/draw/readback combined submissions and headless's
own one-submission-per-`runFrame()`-call path are unaffected, since they
already go through the identical `runFrame()`/`submit()` call graph this
Spec's fix modifies once, centrally.

**9. Performance — disclosed, accepted as Phase 1 baseline.** Phase 1 is
already, by Plan 0006's own design, at most one submission in flight; this
fix does not change *how often* the CPU blocks on the previous frame's GPU
completion (once per frame, unchanged), only *when in the frame* it does
— moved from immediately before this frame's own `vkQueueSubmit` to
immediately after a successful acquire. This narrows, but does not
introduce, the window in which unrelated per-frame CPU work
(`world_->updateTransforms()`, material realization, format-change
candidate construction) could otherwise overlap with the previous frame's
tail-end GPU execution. This is not `vkDeviceWaitIdle` — it is the same,
already-paid, single retained-submission fence wait, relocated — and is
accepted as the correctness-first Phase 1 baseline: a real, disclosed
synchronization gap is closed unconditionally, ahead of preserving a CPU/
GPU overlap window this codebase has never measured or relied on
explicitly. Future revisit condition: if Phase 1's single-frame-in-flight
baseline is ever replaced by Model B (a real multi-frame-in-flight ring),
re-measure whether the overlap this change narrows is worth recovering
before that Plan re-optimizes it — this Spec does not pre-design that
ring (Non-Goals).

**10. Verification/golden boundary — decided, see Testing & Verification
Plan above.**

**11. Human Review Decision Items 1–18 — closed.** See the updated Risks
& Open Questions list above, each item now marked with its resolution and
a pointer to the section that closes it.

No blocking objection was found: host coherence is confirmed true (item
5), the present-semaphore lifecycle is confirmed unaffected (item 8), the
existing `SubmitError` domain is confirmed sufficient without a new error
type (item 4), and no design wider than Model A's single RHI method proved
necessary to close either hazard.

## Human Review Approval

**Approved by slmao <slmaosjtu@gmail.com>, 2026-08-30**, following the
Final Review Round above, which traced the real `Renderer`/`CommandList`/
`VulkanDevice` implementation beyond the original draft's own
Camera/Lighting-`memcpy`-only framing, found and closed a second, real
hazard (descriptor-set updates against a still-possibly-pending previous
submission), and closed all 18 originally-raised Human Review decision
items with call-order and source-code evidence rather than assumption.

This approval covers, specifically: the final synchronization API
(`Device::waitForPreviousSubmission()`, contract per the Final Review
Round's item 3); the exact call point (item 2's nineteen-step frame
trace); that the fix closes both the Camera/Lighting mapped-write race
and the descriptor-set-update race, for every Pipeline drawn in a frame
(items 1 and 7); the dynamic-update extraction semantics (item 6, full
re-extraction every successful frame, replacing `lightingDataCaptured_`);
error handling (item 4, reusing `SubmitError`, fail-fast, no swallowing);
the accepted Phase 1 performance trade-off (item 9); and the verification
and golden-strategy boundary (Testing & Verification Plan section,
including that automated tests — not manual inspection — must lock the
call order, and that no existing golden is auto-updated).

**This approval authorizes drafting Plan 0022 only — not any
Implementation.** ADR-0065 is `Accepted` alongside this approval, per the
same Final Review Round.
