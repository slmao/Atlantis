# Spec: Dynamic Frame Uniform Updates Foundation

- **Status:** In Review
- **Author:** slmao
- **Created:** 2026-08-30
- **Related Plan(s):** None yet — this Spec must reach `Approved` before a
  Plan is drafted against it.
- **Related ADR(s):** [ADR-0065](../adr/0065-explicit-pre-write-submission-drain-for-frame-uniform-safety.md)
  (`Proposed`) — the one RHI surface change this Spec's own recommended
  design requires. See Architectural Impact below.

## Summary

This Spec proposes a minimal, provably-safe model for updating per-frame
uniform data — the Camera view/projection matrices and the World Light
snapshot Lighting Foundation (Spec 0019) introduced — from the CPU each
frame, such that the CPU never overwrites bytes the GPU might still be
reading. Investigating the exact, real call order that motivates this
Spec found a second, more consequential fact: **the existing Camera
uniform update path itself already has this exact hazard, today, on
`main`, independent of Lighting.** This Spec's central problem is that
hazard — not merely a mechanism to let a `World::setLight()` call become
visible on the GPU, which is Spec 0019's own disclosed limitation and
this Spec's secondary goal.

## Motivation / Problem Statement

### The disclosed Lighting limitation this Spec was originally scoped to close

Spec 0019 (`Approved`, implemented) ships `World::setLight()` as a real,
working mutator of `World`'s own CPU-side light state, but Runtime
captures a single `FrameLightingData` snapshot into the GPU-visible
uniform buffer exactly once, guarded by `lightingDataCaptured_`
(`src/runtime/src/runtime_application.cpp:548`), and never re-captures it.
Spec 0019's own D1 states this outcome explicitly and treats it as an
accepted Phase-1 limitation, proven by a reverse test that a post-capture
`setLight()` call changes no published byte. Closing this gap — making a
runtime light change visible at an explicit, testable time — was this
Spec's original mandate.

### The real, pre-existing gap this Spec's own investigation found

Before designing any dynamic-update mechanism, this Spec required tracing
`RuntimeApplication::runFrame()`'s real, as-shipped call order rather than
assuming "a per-frame `memcpy` to `mappedData()` is fine." That trace
found a genuine, currently-shipped race in the Camera update path that
predates Lighting entirely (it traces to Plan 0006/0007's original
single-buffer, single-frame-in-flight design) and is not narrowed to
Lighting in any way. The trace, in exact call order, by file and line:

1. `RuntimeApplication::runFrame()` calls
   `presentation_->acquireNextTarget()`
   (`runtime_application.cpp:407`, exact line number approximate — see the
   function's own acquire step). `VulkanPresentation::acquireNextTarget()`
   calls `vkAcquireNextImageKHR(device_.device(), swapchain_, UINT64_MAX,
   acquireCompleteSemaphore_, VK_NULL_HANDLE, &imageIndex)`
   (`src/vulkan_backend/src/vulkan_presentation.cpp:574-575`). The fence
   argument is `VK_NULL_HANDLE` — this call synchronizes swapchain image
   availability against a `VkSemaphore` consumed later on the GPU
   timeline; it performs **no CPU-blocking wait** and proves nothing about
   whether any previously submitted GPU work has finished executing.
2. `runFrame()` continues through format-change candidate construction
   (read-only, Spec 0018 D9's own submit-safe pattern — nothing is
   swapped in yet), extent-change handling, and
   `world_->updateTransforms()`.
3. `runFrame()` then writes the current frame's view/projection matrices
   directly into the mapped camera buffer:
   `auto* cameraData = static_cast<float*>(cameraBuffer_->mappedData());`
   followed by two unconditional loops writing 32 floats
   (`runtime_application.cpp:539-541`). On the one frame where
   `lightingDataCaptured_` is still false, the same call also writes the
   176-byte `FrameLightingData` snapshot through the same pointer, at byte
   offset 128 (`runtime_application.cpp:573-575`). **Both writes happen
   here — before a `CommandList` is even created** (`createCommandList()`
   is called afterward, `runtime_application.cpp:612`).
4. Only much later in the same function — after draw items are built and
   `renderer_.drawFrame()` has recorded the draw commands that bind this
   exact buffer — does `runFrame()` call
   `device_->submit(std::move(commandList), *target)`
   (`runtime_application.cpp:758`).
5. `VulkanDevice::submit()` (`src/vulkan_backend/src/vulkan_device.cpp:566`)
   begins by ending the command buffer, then — at line 576, **before**
   this frame's own `vkQueueSubmit` — calls
   `waitAndReleaseRetainedSubmission()`. That function
   (`vulkan_device.cpp:514-533`) is what actually calls
   `vkWaitForFences(device_, 1, &submissionFence_, VK_TRUE, UINT64_MAX)`
   — the one and only CPU-blocking wait for a previously submitted
   frame's GPU work anywhere in this path. It drains the **previous**
   frame's retained submission — the one this frame's own `submit()` call
   is about to replace — and only then does `vkQueueSubmit()` run
   (`vulkan_device.cpp:613`) and the current frame's `CommandList` become
   the new retained submission (`vulkan_device.cpp:618-619`).
6. The only other wait in `runFrame()` is a fully synchronous
   `device_->waitIdle()` at the end of the function
   (`runtime_application.cpp:795-802`) — but it is **conditional**,
   gated by `anyMaterialRealizedThisFrame` (Plan 0018 Section P12 /
   Spec 0018 D8 step 5). On an ordinary frame — no material newly
   realized — this wait does not run at all.

**The conclusion this trace supports:** on an ordinary frame, between the
moment frame *N*'s own `submit()` returns and the moment frame *N+1*
overwrites the same camera buffer's bytes (step 3 above), nothing in the
call graph performs a CPU-blocking wait for frame *N*'s own GPU work to
finish. The one wait that would prove it (`waitAndReleaseRetainedSubmission()`,
step 5) does not run until frame *N+1* reaches its **own** `submit()`
call — which is after frame *N+1* has already overwritten the buffer
(step 3) and after `renderer_.drawFrame()` has already recorded frame
*N+1*'s draw commands against the new contents. If frame *N*'s GPU work
(a vertex/fragment shader invocation reading `cameraBuffer_` through its
bound descriptor set) is still executing when frame *N+1* reaches step 3,
the CPU write and the GPU read target the same host-coherent memory
concurrently, with no fence, semaphore, or barrier ordering them. This is
a genuine data race, not a theoretical one — `Buffer::mappedData()`'s own
documented contract
(`src/rhi/include/atlantis/rhi/buffer.h:24-33`) already states plainly
that "writing to a Uniform-purpose Buffer while GPU work from a prior
frame might still read it is a caller precondition violation," and claims
this is satisfied "structurally" by "this round's single-frame-in-flight
discipline... write once per frame, immediately after
`acquireNextTarget()` returns." The trace above shows no such structural
guarantee actually exists in the current call graph: `acquireNextTarget()`
performs no CPU wait (step 1), and the real wait that would matter is
issued too late (step 5, inside the *current* frame's own `submit()`,
not before the *current* frame's write).

Host-coherent memory (confirmed at `buffer.h:10-12`, no staging path this
round) removes the need for an explicit flush/invalidate once ordering is
otherwise guaranteed — it does not, by itself, provide CPU/GPU mutual
exclusion or ordering. Coherency is orthogonal to this hazard, not a fix
for it.

This gap has not manifested as an observed rendering defect: every
existing golden is byte-identical and all `ctest -L gpu` suites pass.
That is expected of a genuine but timing-dependent race and must not be
read as evidence the race does not exist — the actual window (CPU write
duration vs. GPU shader completion time, on the specific hardware and
scene complexity every existing test exercises) may simply not have been
hit yet. Per this Spec's own mandate, this is disclosed here as this
Spec's central problem, not merely fixed quietly while designing dynamic
Lighting updates.

### Why this affects Lighting's own design directly

Any dynamic-Lighting mechanism that follows the same pattern — CPU write
into the shared buffer, sometime before that frame's own `submit()` —
inherits the identical hazard for the Lighting bytes, worse than today
because it would now happen on **every** frame a light changes, not once
per process lifetime. A correct fix must close the gap for both the
Camera write (every frame, unconditionally) and the Lighting write
(whenever a change is pending) using the same mechanism — this Spec does
not propose two different safety models for two halves of one buffer.

## Goals

- Camera continues to be updated correctly every frame, with the same or
  better safety than today.
- A `World::setLight()` change made at runtime becomes visible on the GPU
  at an explicit, testable frame boundary — removing Spec 0019 D1's
  "captured once, never updated again" limitation.
- The CPU never overwrites uniform bytes the GPU might still be reading —
  closing the real gap described above, for both Camera and Lighting.
- Windowed and headless `RuntimeHost` share one safety model — no
  divergent sync logic between the two.
- No multi-threaded frame orchestration is introduced; the engine remains
  single-threaded at the frame level (Phase 1 constraint, ADR-0004).

## Non-Goals

- PBR Material, Shadow, IBL, Post-processing — unrelated feature work,
  explicitly deferred per human-directed priority ordering (see
  `specs/README.md` Section B).
- Animation system.
- Multi-threaded rendering or any job system.
- A generic, arbitrary-N-frames-in-flight framework — Plan 0006's
  single-frame-in-flight baseline is not being replaced; if a ring buffer
  is the design Human Review selects, its slot count is derived from and
  bounded by that existing contract, not a general framework.
- Bindless rendering or descriptor indexing.
- A general-purpose GPU upload scheduler for arbitrary resources — this
  Spec is scoped to the two existing frame-uniform fields (Camera,
  Lighting) only.
- Android, iOS, or Linux implementation — the design must remain portable
  to Android per Phase 1's target platforms, but no platform-specific
  code is written this round.
- Any Editor/Client-facing API.

## Requirements

### Functional

- Camera view/projection matrices are written to GPU-visible memory once
  per frame, unconditionally, exactly as today.
- A `World::setLight()` call is reflected in the GPU-visible Lighting
  data no later than an explicit, Spec-defined frame boundary (a Human
  Review decision below; not defined by this Spec unilaterally).
- Multiple `setLight()` calls affecting the same frame's update resolve
  to final-value semantics (the last call before the update boundary
  wins) — no averaging, no queuing of intermediate values.
- A frame that consumes zero extent, is deferred as out-of-date, hits a
  resize, or fails to submit must not silently and incorrectly consume a
  pending Lighting update it never actually published.
- The mechanism must not require Material or Pipeline objects to be
  duplicated unless the selected design (Human Review decision below)
  specifically requires it, and if it does, that cost must be disclosed
  explicitly, not discovered later.

### Non-functional

- Performance: any new per-frame CPU stall this Spec introduces (e.g. a
  wait) must be measured and disclosed against the existing baseline; a
  Phase-1-acceptable regression must be explicitly approved, not silently
  accepted.
- Memory: any new buffer allocation (e.g. ring-buffer slots) must state
  its total byte cost explicitly.
- Portability (within the Vulkan-only Phase 1 constraint): the design
  must not assume any Windows-only or desktop-only Vulkan extension or
  timeline-semaphore feature not already available on the Android target
  API level Spec 0002 anticipates.
- Other: no new third-party dependency; no new global mutable frame
  state; no new threads or locks.

## Proposed Design

### Recommended model: explicit pre-write submission drain

Before `runFrame()` writes any new bytes into a frame-uniform buffer,
Runtime calls a new, narrow RHI method — proposed name
`Device::waitForPreviousSubmission()` — that performs exactly what
`VulkanDevice::waitAndReleaseRetainedSubmission()` already does privately
today (`vulkan_device.cpp:514-533`: wait on `submissionFence_`, release
the retained `CommandList`), just exposed publicly and called explicitly
**before** the Camera/Lighting write instead of relying on it happening,
implicitly and too late, inside `submit()`.

This is the minimal-diff fix the evidence above supports:

- It does not change buffer count, ownership, or lifetime.
- It does not require Material/Pipeline duplication.
- It does not require a new `CommandList` recording capability.
- It closes the gap by relocating an already-necessary wait earlier in
  frame order, rather than adding conceptually new synchronization. The
  wait already happens every ordinary frame today (inside `submit()`);
  this only moves its call site.
- `VulkanDevice::submit()`'s own internal call to
  `waitAndReleaseRetainedSubmission()` (`vulkan_device.cpp:576`) becomes
  a no-op on the frame that already drained it explicitly
  (`hasRetainedSubmission_` is `false`, `waitAndReleaseRetainedSubmission()`
  already early-returns `Ok` in that case — `vulkan_device.cpp:517-519`)
  — no double-wait, no behavior change to `submit()` itself.

Cost: this pulls one frame's worth of GPU-completion latency earlier in
`runFrame()` — the CPU now blocks, once per frame, before recording that
frame's draw commands, rather than blocking once per frame right before
submitting them. Given Plan 0006's committed single-frame-in-flight
baseline, a CPU-side wait for the previous frame's GPU completion already
happens on the identical cadence (once per frame); this changes only
*when in the frame* it happens, not whether it happens or how often. The
actual wall-clock cost of moving the wait earlier — versus letting other
CPU work (`world_->updateTransforms()`, material realization) overlap
with the *previous* frame's tail-end GPU execution, as today's ordering
incidentally allows — is a real Phase-1 performance question Human
Review must weigh explicitly (Decision Item 2 below), not one this Spec
resolves unilaterally.

Lighting's own write moves from a one-shot, `lightingDataCaptured_`-guarded
write to a per-frame check against a pending-update marker (e.g. a dirty
flag or generation counter — exact shape is a Human Review decision, item
6/16 below) maintained by comparing `World`'s current light state against
what was last published. When pending, the write happens at the same
point in `runFrame()`, guarded by the same pre-write drain as Camera.

### Alternatives evaluated but not recommended by default

**Model B — multi-slot uniform-buffer ring.** N buffers, one written per
in-flight frame slot, the bound descriptor set rewritten to that slot's
buffer each frame. Investigation found this structurally feasible without
an RHI signature change to `CommandList::bindUniformBuffer()`:
`VulkanCommandList::bindUniformBuffer()` already calls
`vkUpdateDescriptorSets()` fresh on every call
(`src/vulkan_backend/src/vulkan_command_list.cpp:234-262`), rather than
binding once at Pipeline-creation time — so rebinding to a different
`VkBuffer` handle per frame slot is already within the existing call
pattern's reach. The unresolved cost is elsewhere: slot count must derive
from a real in-flight bound (today, exactly one frame — Plan 0006 Section
11's `hasRetainedSubmission_` state machine — so N=2 is the structural
minimum a ring buggier than "explicit wait" would need), and slot
selection needs a real, testable rule (round-robin keyed to acquired
image index, or a separate frame counter). This is a larger, more
invasive change for the same safety outcome the explicit-drain model
achieves with zero new buffers.

**Model C — staging buffer + RenderGraph-recorded copy.** Write into a
CPU-only staging buffer, record a `CommandList`-issued copy into the real
uniform buffer as part of the frame's own GPU work, ordered by the
RenderGraph's own barrier discipline. Investigation found
`CommandList` has no buffer-to-buffer copy today —
`copyRenderTargetToBuffer()` and `copyBufferToTexture()`
(`src/rhi/include/atlantis/rhi/command_list.h:79-100`) are the only
existing copy-recording methods, and neither's source/destination shape
fits a uniform buffer update. This model requires a new RHI recording
capability (e.g. `copyBufferToBuffer()`), a materially larger surface
change than Model A's single `Device` method, for a safety property Model
A already achieves.

**Rejected outright — call the existing public `waitIdle()` before every
write, unconditionally.** `Device::waitIdle()` deliberately does more
than drain the retained submission (`vulkan_device.cpp:624`, and its own
"belt-and-suspenders" presentation-engine-internal drain past that);
using it as a per-frame primitive conflates a narrow "wait for the one
retained submission" need with a broader, heavier "drain everything"
operation Plan 0006 Section 11 deliberately kept for coarse-grained
lifecycle events (shutdown, mid-frame exit, post-realization publish),
not the steady-state per-frame path.

Final selection among Model A, B, and C is Human Review Decision Item 1
below — this Spec recommends Model A on the evidence above but does not
treat that recommendation as approved.

## Architectural Impact

Yes — the recommended design (Model A) adds one new public method to the
RHI `Device` interface (`Device::waitForPreviousSubmission()` or an
equivalent name Human Review may prefer), and changes the frame-lifecycle
contract Runtime follows (an explicit wait now precedes the CPU write,
where none was required to precede it before). Neither the RenderGraph
path, the Vulkan-Backend-boundary rule, nor Renderer's own
non-dependence on Platform/Vulkan Backend changes. If Human Review
instead selects Model B or Model C, that decision would introduce its own
distinct architectural impact (new buffer-ownership/ring-slot model, or a
new `CommandList` recording capability, respectively) — not evaluated to
the same depth here, since this Spec does not presuppose which model is
approved, and only the recommended model gets a paired ADR this round
(see below).

[ADR-0065](../adr/0065-explicit-pre-write-submission-drain-for-frame-uniform-safety.md)
(`Proposed`) records the Model A decision: the new `Device` method's
exact name, signature, and error semantics, and the frame-lifecycle
change it implies. It is scoped to this one decision only, per this
codebase's "one decision per ADR" convention — it does not also decide
the Lighting dirty-tracking shape or any other Human Review item below.

## Alternatives Considered

See "Alternatives evaluated but not recommended by default" above (Model
B, Model C, and the rejected always-`waitIdle()` variant) — kept in the
Proposed Design section rather than duplicated here, since each
alternative's own trade-off is inseparable from the call-order evidence
that motivates the recommendation.

## Testing & Verification Plan

- **Synchronization proof, CPU-side:** a test that instruments (or
  otherwise makes observable) the exact ordering of "CPU write to the
  frame uniform buffer" relative to "GPU-confirmed completion of the
  previous frame's submission," across multiple frames, demonstrating the
  write never precedes the confirmation under the selected model. This is
  the direct regression test for the gap this Spec discloses.
- **Multi-frame Camera regression:** a real, multi-frame GPU test that
  changes the active Camera's Transform between frames and confirms the
  rendered output reflects each change — proving the existing Camera path
  still works correctly under the new ordering, not just that it compiles.
- **Dynamic Lighting regression, GPU-level:** Directional Light direction/
  color/intensity changes and Point Light position/intensity changes,
  each made via `World::setLight()` at runtime, confirmed visible in real
  rendered pixel output at the approved update boundary (Decision Item 6).
- **Final-value semantics:** multiple `setLight()` calls against the same
  entity within one frame publish only the last call's value — no
  intermediate value ever reaches the GPU.
- **No stale/undefined bytes:** a frame with no pending Lighting change
  publishes byte-identical Lighting data to the previous frame — never
  garbage, never a partial write.
- **Non-consumption on skip paths:** zero-extent, out-of-date/deferred
  acquire, resize, and submit-failure frames must not consume a pending
  Lighting update that was never actually published — a following frame
  must still publish it.
- **Format-change + dynamic Lighting interaction:** a scene undergoing a
  format-change candidate rebuild (Spec 0018 D9) in the same frame a
  Lighting update is pending — both must complete correctly and
  independently.
- **Windowed/headless parity:** the identical safety-model code path is
  exercised by both a windowed `RuntimeHost` GPU test and a headless
  fixture — no divergent sync logic.
- **Dual-level evidence:** both CPU mapped-byte comparison (fast,
  deterministic) and real GPU pixel-level evidence (the actual correctness
  claim) — CPU-byte checks alone are not sufficient per this Spec's own
  motivation (passing tests must not stand in for proof of absence of a
  race).
- **Existing goldens:** all five existing goldens (`minimal_cube`,
  `world_scene`, `textured_quad`, `material_demo`, `lighting_demo`)
  remain byte-for-byte identical to `main` and pixel-zero-difference — no
  visual change for any scene that does not exercise dynamic Lighting.
  Any new golden this Spec's own eventual Plan adds to demonstrate dynamic
  Lighting follows ADR-0042's existing two-phase bootstrap process exactly.
- **Full matrix:** Debug and Release, `ctest -L gpu` and `ctest -LE gpu`,
  Vulkan Validation Layers clean (zero `VUID`/Validation Error/Validation
  Warning), a clean `ATLANTIS_BUILD_TESTS=OFF` build, and a module/link
  boundary scan confirming no new Android/Linux code and no boundary
  violation (World independent of RHI/Renderer/Runtime; Renderer
  independent of Platform/Vulkan Backend; Vulkan types confined to the
  Vulkan Backend).

## Risks & Open Questions

The following are explicit Human Review decision items. None are decided
by this Spec; each is listed so a human reviewer chooses among real,
disclosed alternatives rather than the choice being made silently during
implementation.

1. **The safety update model itself.** Explicit wait-before-write (Model
   A, recommended above), a multi-slot ring buffer (Model B), a
   staging-buffer-plus-RenderGraph-copy (Model C), or another real,
   API-supported model not listed here.
2. **If Model A is selected:** whether to reuse the existing public
   `Device::waitIdle()` or add the new, narrower
   `Device::waitForPreviousSubmission()` proposed above — and whether the
   resulting per-frame CPU stall (moved earlier in the frame, not newly
   added — see Proposed Design) is an acceptable Phase-1 performance
   baseline, or whether it needs measurement against a concrete target
   before approval.
3. **If Model B is selected:** how the ring's slot count derives from
   the real, existing in-flight contract (Plan 0006 Section 11's
   single-retained-submission state machine — N=2 is the structural
   floor); how a frame selects its slot (round-robin vs. acquired-image-
   index-keyed); and whether it forces Material/Pipeline duplication per
   slot, or whether the already-per-call `vkUpdateDescriptorSets()`
   pattern in `bindUniformBuffer()` (confirmed above) is sufficient
   without duplication.
4. **Whether the RHI public API must change**, and if so, its exact
   minimal shape and `Result`/error semantics (this Spec's own evidence
   says yes for Model A and Model C; Human Review confirms the exact
   surface).
5. **Camera and Lighting: one unified `FrameUniformData` struct, or two
   separate buffers** — never merged merely "for tidiness"; a real
   argument (e.g. shared safety-drain timing, single descriptor binding)
   must justify whichever choice is made.
6. **Lighting update visibility timing** — the next successful frame
   after `World::setLight()` returns, or another explicit boundary (e.g.
   the next frame whose `submit()` succeeds, which may differ on a
   skipped/deferred frame).
7. **Whether an acquire deferral (zero-extent, out-of-date), a resize, or
   a submit failure consumes one pending Lighting update** — this Spec's
   own Functional Requirements state it must not, but the exact mechanism
   (a flag cleared only on confirmed publish vs. some other bookkeeping)
   is a Human Review decision.
8. **The relationship between a format-change candidate Material (Spec
   0018 D9) and the dynamic uniform buffer** — whether they can be
   swapped in independently within the same frame, and whether either
   ever blocks the other.
9. **Uniform buffer ownership and destruction order** under the selected
   model — unchanged from today's single `cameraBuffer_` under Model A;
   a real question under Model B (N buffers) or Model C (plus a staging
   buffer).
10. **Host-visible/coherent memory requirements** — every `Buffer` is
    host-coherent today (`buffer.h:10-12`); if the selected model ever
    introduces a non-coherent buffer, the flush/invalidate contract must
    be stated explicitly, not assumed.
11. **How windowed and offscreen fixtures share the implementation**
    rather than duplicating synchronization logic — both already share
    `RuntimeApplication`'s own `runFrame()`; confirm the selected model
    does not introduce a windowed-only or headless-only branch.
12. **`DeviceLost` and buffer-update-failure error classification** — how
    a failure in the new wait/copy step is classified and surfaced,
    consistent with `classifySubmitError()`'s existing categories.
13. **Whether a per-frame stall (Model A) is acceptable as a Phase-1-only
    baseline**, and if so, what future condition (e.g. adopting Model B)
    would trigger revisiting it.
14. **Whether tests need real multi-frame GPU evidence** rather than only
    CPU mapped-byte comparison — this Spec's own Testing & Verification
    Plan above says yes; Human Review confirms the exact scenarios are
    sufficient.
15. **Whether the existing Camera update path itself needs its own fix
    and regression coverage** — this Spec's own Motivation section
    answers yes; Human Review confirms the fix and coverage this Spec
    proposes actually close it.
16. **How the one-shot `lightingDataCaptured_` field and any other
    static-snapshot state get removed**, so no two authoritative data
    sets (a stale snapshot flag and a new dirty-tracking mechanism)
    coexist after this Spec's own eventual implementation.
17. **Thread-safety** — confirm the selected model stays single-threaded,
    introduces no lock, and does not require one implicitly (e.g. a ring
    buffer accessed only from the frame thread needs no lock; confirm
    this holds for whichever model is chosen).
18. **Android portability** — confirm the selected model's Vulkan usage
    (fences, semaphores, buffer types) is available at the Android API
    level Spec 0002 anticipates; design portable, do not implement this
    round.

## Out of Scope / Future Work

Everything in Non-Goals above. In addition: this Spec does not attempt to
generalize the selected mechanism to any future per-frame GPU resource
beyond Camera and Lighting (e.g. a future Shadow-map matrix set, PBR
material parameter block) — a future Spec would need to confirm the
chosen model still fits before reusing it, not assume it does. This Spec
also does not revisit Plan 0006's single-frame-in-flight baseline itself
— multiple frames in flight remains explicitly out of scope (see
Non-Goals).
