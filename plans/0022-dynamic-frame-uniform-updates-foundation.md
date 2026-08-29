# Plan: Dynamic Frame Uniform Updates Foundation

- **Spec:** [specs/0022-dynamic-frame-uniform-updates-foundation.md](../specs/0022-dynamic-frame-uniform-updates-foundation.md)
  (`Approved`) — [ADR-0065](../adr/0065-explicit-pre-write-submission-drain-for-frame-uniform-safety.md)
  (`Accepted`)
- **Status:** `In Review`
- **Author:** slmao

## Objective

Turn Spec 0022's approved design into an ordered, reviewable
implementation: add `Device::waitForPreviousSubmission()` (ADR-0065),
route every Camera/Lighting mapped write and every descriptor-set update
through it structurally — not by calling-order discipline — and replace
`lightingDataCaptured_`'s one-time capture with a full, per-successful-frame
re-extraction from `World`'s live state, closing both Hazard A (mapped-write
race) and Hazard B (descriptor-set-update race) Spec 0022's Motivation
names.

## Pre-draft verification against real, current source

Re-read fresh against `origin/main` at `ee93a0d` (PR #102 merged) before
drafting, per this Plan's own governance gate.

**RHI `Device` — all nine existing pure virtuals** (`src/rhi/include/atlantis/rhi/device.h:24-90`):
`createCommandList()`, `submit()`, `waitIdle()`, `createBuffer()`,
`createTexture()`, `createPipeline()`, `createOffscreenTarget()`,
`createSampledTexture()`, `createSampler()`. **Sole real implementer:**
`VulkanDevice` (`src/vulkan_backend/src/vulkan_device.h`/`.cpp`) — a
repository-wide search for `class.*Fake.*Device`/`class.*Mock.*Device`/
`class.*Stub.*Device` found no matches; `Device` has exactly one
implementer today. This Plan's Milestone 1 adds a tenth pure virtual and
must update that one implementer in the same commit — no intermediate
tree may carry a `Device` with an unimplemented pure virtual.

**`VulkanDevice::submit()`/`waitIdle()`/`waitAndReleaseRetainedSubmission()`**
(`vulkan_device.cpp:514-533` and `:566-622`, `:624-`): `submit()` ends the
command buffer, calls `waitAndReleaseRetainedSubmission()` at `:576`
(before `vkQueueSubmit`), submits at `:613`, then stores the *new*
`CommandList` as `retainedSubmission_`/`hasRetainedSubmission_ = true` at
`:618-619`. `waitAndReleaseRetainedSubmission()` itself
(`:514-533`) early-returns `Ok` when `!hasRetainedSubmission_` (`:517-519`
— the existing idempotence this Plan's new public method reuses
verbatim), otherwise `vkWaitForFences`/`vkResetFences` on the single
`submissionFence_` member, then `retainedSubmission_.reset()` and clears
the flag. `waitIdle()` (`:624-`) calls the same private helper first,
then additionally drains presentation-engine-internal state — confirmed
still the coarser, separate operation ADR-0065 keeps distinct.

**Retained `CommandList`/fence/`SubmissionSignal` ownership**
(`src/vulkan_backend/src/vulkan_device.h:224-226`): `VkFence
submissionFence_` (one persistent object, created once, reused/reset
every submission — not per-frame); `std::unique_ptr<atlantis::rhi::CommandList>
retainedSubmission_` (null until the first `submit()`); `bool
hasRetainedSubmission_ = false`. `VulkanSubmissionSignal`
(`src/vulkan_backend/src/vulkan_submission_signal.h`) wraps a
`VkSemaphore` and its destructor "performs no Vulkan call" — the real
semaphore lifetime belongs to `VulkanPresentation`'s own per-image-index
`renderFinishedSemaphores_` (`vulkan_presentation.cpp:514-516`), confirmed
untouched by `waitAndReleaseRetainedSubmission()`, which only ever touches
`retainedSubmission_`/`submissionFence_` — this Plan's new call site
cannot affect `present()`'s own semaphore lifetime (Spec 0022 Final
Review Round, Lifecycle section — re-confirmed here, not re-derived).

**`RuntimeApplication::runFrame()` full path** (`src/runtime/src/runtime_application.cpp`):
event processing → early-return guards (`:403-405`) →
`presentation_->acquireNextTarget()` (`:407`) → acquire-error path
(`:408-413`, no drain reachable) → zero-extent/deferred null-target early
return (`:414-416`, no drain reachable) → format-change candidate build,
read-only (`:419-456`, calls `rebuildMaterialsForFormatChange()`) →
extent-change depth-texture recreation (`:458-471`) →
`world_->updateTransforms()` (`:497`) → active-camera/world-matrix/
camera-component fetch (`:499-522`, `NoActiveCamera` handled by a plain
`if`+log+`markFailed()`+return, `:500-513`; the two subsequent
`ATLANTIS_CHECK_MSG`s at `:517-521` are pre-existing hard asserts, not
`Result`-based, and this Plan does not touch their severity) →
`extractCameraMatrices()` call and mapped camera write (`:524-541`) →
one-time lighting capture, guarded by `lightingDataCaptured_`
(`:543-576`) → pending-material computation → `device_->createCommandList()`
(`:612`) → `realizePendingMaterials()` (records into the same
`CommandList`) → draw-item construction → `renderer_.drawFrame()`
(`:755`) → `device_->submit()` (`:758`) → format-rebuild candidate
swap-in on `Ok` (`:776-779`) → conditional `waitIdle()` on a realization
frame (`:795-802`) → `presentation_->present()` (`:817`).

**Camera/Lighting payload construction and mapped write**
(`:539-576`): one shared `Buffer` (`cameraBuffer_`, `runtime_application.cpp:288-295`,
created once, `sizeof(float) * 32 + sizeof(FrameLightingData)` = 304
bytes). Camera write is unconditional, every frame. Lighting write is
guarded by `lightingDataCaptured_` (declared `runtime_application.h:123`,
immediately after `cameraBuffer_` at `:116`, per that field's own
existing comment `:117-122`) — this Plan removes the field and its
comment entirely (Spec 0022 decision item 16).

**`Renderer::drawFrame()`** (`src/renderer/src/renderer.cpp:17-55`):
for every `DrawItem`, calls `cmd.bindPipeline()`, `cmd.bindVertexBuffer()`,
`cmd.bindIndexBuffer()`, `cmd.bindUniformBuffer(cameraUniformBuffer)`,
optionally `cmd.bindTexture()`, `cmd.pushConstant()`, `cmd.drawIndexed()`
— all inside a `RenderGraphBuilder` pass execute callback, i.e. during
`render_graph::execute()`'s own recording, called from inside
`drawFrame()` itself (`:54`).

**`VulkanCommandList::bindUniformBuffer()`/`bindTexture()`**
(`src/vulkan_backend/src/vulkan_command_list.cpp:234-269`, `:271-318`):
each calls `vkUpdateDescriptorSets()` against `boundDescriptorSet_` —
`vulkanPipeline.descriptorSet()`, a single, persistent `VkDescriptorSet`
per Pipeline, bound once at `bindPipeline()` (`:213-218`) and never
recreated for that Pipeline's own lifetime. Each method's own
`lastUpdatedDescriptorSet_`/`lastUpdatedUniformBuffer_` (and the texture
equivalents `lastUpdatedTextureDescriptorSet_`/`lastUpdatedSampledTexture_`/
`lastUpdatedSampler_`) are **members of `VulkanCommandList`**
(`vulkan_command_list.h`, confirmed by the class's own field list) —
**not** persisted across frames, because `createCommandList()`
(`vulkan_device.cpp:535-563`) allocates a *new* `VulkanCommandList`
instance every frame. This is the exact mechanism behind Hazard B: the
skip only ever applies to an exact repeat *within the current
recording*.

**Material candidate/format-change rebuild and submit-safe swap-in**
(`runtime_application.cpp:419-456`, `:776-779`): `rebuildMaterialsForFormatChange()`
(`src/runtime/include/atlantis/runtime/material_realization.h`/`.cpp`)
builds an entirely new candidate `Material`/`Pipeline` bundle — new
descriptor set(s) allocated fresh via Spec 0021's own growable pool set —
bound to the *same*, unchanged `cameraBuffer_`; the swap into
`fallbackMaterial_`/`materialResourceMap_` happens only after this
frame's own `submit()` returns `Ok` (`:776-779`), confirmed independent
of this Plan's own drain-timing change (Spec 0022 Final Review Round,
item 8) — this Plan does not touch `rebuildMaterialsForFormatChange()`'s
own logic, only where in `runFrame()` its *call* sits relative to the new
drain (unchanged: still after the drain, per the frame order below).

**`World` transform update model** (`src/world/src/world.cpp:235-269`,
`src/world/include/atlantis/world/world.h:54-60`): `setLocalTransform(EntityId,
Transform)` and `setParent(EntityId, EntityId)` are the real mutator
names (not `setTransform()` — corrected from this Plan's own drafting
directive, which used an informal name). `World::updateTransforms()`
performs a **full, unconditional recompute of every live entity's
`cachedWorldMatrix` on every call** — no dirty bit, no cross-call
memoization (the "memoized" in its own header comment refers only to
within-one-call traversal, each node visited once per call). **This means
no gap exists here**: `runFrame()` already calls `world_->updateTransforms()`
unconditionally, once, every successful frame (`:497`), so any
`setLocalTransform()`/`setParent()` call made before that point — this
frame or any earlier one — is already correctly reflected once this
Plan's helper (below) calls `world.activeCamera()`/`getWorldMatrix()`/
`world.lightEntities()`/`getLight()` after it. No `World`-side change is
needed; this Plan only needs to (a) keep calling `updateTransforms()`
before extraction, exactly as today, and (b) extend what gets extracted
after it from "Camera only" to "Camera and Lighting, every frame."

**Multi-frame GPU fixtures/composition roots**
(`tests/runtime/runtime_smoke_gpu_tests.cpp:91-92`: `for (int i = 0; i <
kSmokeTestFrameCount && app.shouldContinue(); ++i) { app.runFrame(); }`;
`tests/runtime/material_realization_gpu_tests.cpp`: exercises `runFrame()`'s
own format-change/realization path across frames). Both drive the same,
unforked `RuntimeApplication::runFrame()` — confirmed no `headless`-named
branch exists anywhere in `runtime_application.cpp` (search returned no
matches); a headless composition root supplies an offscreen `RenderTarget`
through the identical function. This Plan's Milestone 3/4 tests extend
these two files rather than introducing new fixture machinery.

**No `FakeDevice` exists** (repository-wide search, confirmed above).
`Device` has 9 pure virtuals today (10 once this Plan's Milestone 1
lands) — a full local fake implementing all 10 is real but small work
(each unused method's body is a one-line unreachable marker); `Buffer`
has exactly 3 (`purpose()`, `sizeBytes()`, `mappedData()`,
`src/rhi/include/atlantis/rhi/buffer.h:17-35`) — trivial to fake. This
Plan's own test-seam design (P7 below) scopes the fake to exactly these
two interfaces, in one test translation unit, never exported.

**`Buffer`'s mapped/coherent contract and thread-safety documentation**
(`buffer.h:9-16`, `:24-34`): "every `Buffer` is host-visible and
host-coherent regardless of purpose... Not internally thread-safe;
caller-thread-only (ADR-0004)." Re-confirmed against `vulkan_device.cpp:770-772`
(unconditional `HOST_VISIBLE | HOST_COHERENT` memory-type selection
across every `BufferPurpose`) — unchanged since Spec 0022's own Final
Review Round; no new evidence needed, cited here for this Plan's own
completeness.

## Plan-level decisions (fixed here, not left to Implementation)

### P1. New RHI API — `Device::waitForPreviousSubmission()`, exact signature and implementer update

Add to `device.h`, immediately after `submit()`:

```cpp
// ADR-0065: waits for the one previous submission this Device may still
// be retaining (Phase 1's single-frame-in-flight baseline, Plan 0006
// Section 11) and releases it -- after this call returns Ok, the caller
// may safely mutate any host-visible bytes or descriptor-set state that
// submission's own recorded commands referenced. A no-op returning Ok
// when nothing is retained; safe to call twice in a row (the second call
// is a verified no-op). Not equivalent to waitIdle() -- does not drain
// presentation-engine-internal state, only the one retained submission.
// Not internally thread-safe; caller-thread-only (ADR-0004), matching
// every other Device method's existing, implicit contract.
[[nodiscard]] virtual atlantis::Result<std::monostate, SubmitError> waitForPreviousSubmission() = 0;
```

`VulkanDevice`'s implementation (`vulkan_device.cpp`, adjacent to
`submit()`) is a one-line forwarding call:

```cpp
atlantis::Result<std::monostate, atlantis::rhi::SubmitError> VulkanDevice::waitForPreviousSubmission() {
  return waitAndReleaseRetainedSubmission();
}
```

`submit()`'s own internal call to `waitAndReleaseRetainedSubmission()`
(`:576`) is **not removed** — kept verbatim, per ADR-0065's own Decision,
as a defensive, idempotent fallback for any `submit()` caller that does
not call the new method first. Both the new public method and `submit()`
call the identical private helper — never two implementations. Confirmed
sole implementer: `VulkanDevice` — this Milestone's own commit updates
`device.h` and `vulkan_device.h`/`.cpp` together, atomically (Milestone 1
below); no other file implements `Device`.

### P2. Runtime-private helper — exact design, not a calling-discipline comment

A new, free function (matching this module's own established shape for
`extractCameraMatrices()`/`extractFrameLightingData()` in
`scene_extraction.h`/`.cpp` and `rebuildMaterialsForFormatChange()` in
`material_realization.h`/`.cpp` — Runtime already factors orchestration
logic like this out of `runtime_application.cpp`'s own anonymous
namespace specifically "so this... logic is unit-testable," per
`scene_extraction.h:34-38`'s own precedent comment), in a new file pair
`src/runtime/include/atlantis/runtime/frame_uniform_publish.h` /
`src/runtime/src/frame_uniform_publish.cpp`:

```cpp
namespace atlantis::runtime {

// Two variants, matching this operation's own two genuinely different
// failure domains and the two genuinely different ways runFrame() logs
// them today (see Plan 0022 P5): a SubmitError-domain wait failure (the
// only variant classifySubmitError() is called for, matching every other
// SubmitError-domain failure this file already handles that way), or a
// pre-existing "should never happen in correct operation" scene-
// extraction failure (World has no active camera, or
// extractCameraMatrices()/extractFrameLightingData() itself failed) --
// each already logged, with its own original message text, by this
// function's own body before it returns, matching runFrame()'s pre-
// existing per-site log text exactly (P5's mapping table).
enum class FrameUniformPublishError {
  PreviousSubmissionWaitFailed,
  DataExtractionFailed,
};

// Drains the previous frame's retained submission
// (Device::waitForPreviousSubmission(), ADR-0065) -- and only on
// success -- updates World's cached transforms, extracts this frame's
// complete Camera+Lighting payload from World's current live state, and
// writes it into cameraBuffer's mapped memory. One function: no caller
// can record a draw or update a descriptor set against payload written
// before the drain, or skip the drain before either write, because the
// write statements exist only inside this function's own body -- not a
// calling-order convention documented in a comment (Spec 0022 Proposed
// Design). On PreviousSubmissionWaitFailed, world.updateTransforms() is
// never called and cameraBuffer is never written -- the caller must not
// create a CommandList or call Renderer::drawFrame() for this frame
// (Plan 0022 P3). Precondition, not re-checked here: cameraBuffer was
// created with BufferPurpose::Uniform, sized for exactly one
// CameraMatrices (128 bytes) immediately followed by one
// FrameLightingData (176 bytes) -- 304 bytes total, unchanged from Plan
// 0019 Section P7 -- a RuntimeApplication-construction-time invariant
// (VulkanCommandList::bindUniformBuffer()'s own ATLANTIS_CHECK already
// guards the Buffer's purpose at bind time). aspect is the caller's
// current RenderTarget extent's own width/height ratio (unchanged
// parameter shape from extractCameraMatrices() today). Not thread-safe;
// caller-thread-only (ADR-0004), matching device/world/cameraBuffer's
// own existing contracts.
[[nodiscard]] atlantis::Result<std::monostate, FrameUniformPublishError> drainAndPublishFrameUniforms(
    atlantis::rhi::Device& device, atlantis::world::World& world, atlantis::rhi::Buffer& cameraBuffer, float aspect);

}  // namespace atlantis::runtime
```

Body (informative — exact code is Implementation's, this fixes the
contract and call order, not final syntax):

1. `device.waitForPreviousSubmission()`; on `Err`, log
   `"drainAndPublishFrameUniforms(): waitForPreviousSubmission() failed"`,
   call `classifySubmitError()` (matching `submit()`'s own existing
   pattern, `runtime_application.cpp:761`), return
   `Err(PreviousSubmissionWaitFailed)` — **zero further calls below this
   line execute.**
2. `world.updateTransforms()`.
3. `world.activeCamera()`; if empty, log `"drainAndPublishFrameUniforms():
   World has no active camera"` (identical text to today's
   `runtime_application.cpp:510`), return `Err(DataExtractionFailed)`.
4. `world.getWorldMatrix(*activeCamera)`/`world.getCamera(*activeCamera)` —
   kept as `ATLANTIS_CHECK_MSG`, unchanged severity, verbatim message
   text from `:517-521` (these were already hard asserts before this
   Plan, not `Result`-checked; this Plan does not soften or harden them).
5. `extractCameraMatrices(...)`; on `Err`, log `"drainAndPublishFrameUniforms():
   extractCameraMatrices() failed"` (identical text to today's
   `:534`), return `Err(DataExtractionFailed)`.
6. Write the 32 camera floats into `cameraBuffer.mappedData()` — identical
   two loops to today's `:540-541`.
7. Build `lightInputs` from `world.lightEntities()`/`getLight()`/
   `getWorldMatrix()` — identical to today's `:549-556`, now unconditional
   (the `lightingDataCaptured_` guard is gone).
8. `extractFrameLightingData(lightInputs)`; on `Err`, log
   `"drainAndPublishFrameUniforms(): extractFrameLightingData() failed"`
   (identical text to today's `:564`), return `Err(DataExtractionFailed)`.
9. Write the 176-byte `FrameLightingData` at byte offset 128 through the
   same `cameraData + 32` pointer — identical to today's `:573-574`.
10. Return `Ok({})`.

### P3. `runFrame()`'s new call point and full frame order

Exact new order (renumbered from Spec 0022's own Final Review Round
nineteen-step trace, now naming the concrete function calls this Plan
introduces):

1. `processEvents()`.
2. Early-return guards (`:403-405`) — unchanged.
3. `presentation_->acquireNextTarget()` (`:407`) — unchanged.
4. Acquire-error path (`:408-413`) — unchanged; **no call to
   `drainAndPublishFrameUniforms()` on this path.**
5. Zero-extent/deferred null-target early return (`:414-416`) —
   unchanged; **no call on this path.**
6. **New:** `const Extent2D currentExtent = target->extent();` (moved
   earlier from its current position at `:459`, a pure read, harmless to
   move — reused by the existing extent-change check below, no duplicate
   logic) → `const float aspect = currentExtent.height != 0 ?
   static_cast<float>(currentExtent.width) / static_cast<float>(currentExtent.height)
   : 1.0f;` (moved earlier from `:524-526`, unchanged formula) →
   `auto publishResult = atlantis::runtime::drainAndPublishFrameUniforms(*device_, *world_, *cameraBuffer_, aspect);`
   → `if (publishResult.isErr()) { lifecycle_.markFailed(); return; }`
   (the function's own body already logs and classifies internally, per
   P2 — `runFrame()`'s own call site adds no further logging, avoiding a
   duplicate message).
7. Format-change candidate build (`:419-456`, moved to *after* step 6 —
   confirmed independent of drain timing, Pre-draft verification above
   and Spec 0022 Final Review Round item 8).
8. Extent-change depth-texture recreation (`:458-471`, using the
   `currentExtent` already computed at step 6 — the duplicate
   `const Extent2D currentExtent = target->extent();` at old `:459` is
   deleted, not kept as a shadowing redeclaration).
9. Pending-material computation (unchanged).
10. `device_->createCommandList()` (`:612`) — now proven to occur after
    step 6's drain and after step 6's own mapped writes.
11. `realizePendingMaterials()` — unchanged; new resource creation,
    independent of the retained submission (Pre-draft verification
    above).
12. Draw-item construction (unchanged).
13. `renderer_.drawFrame()` (`:755`) — every `bindUniformBuffer()`/
    `bindTexture()` call inside it now proven to occur after step 6's
    drain, closing Hazard B for every Pipeline drawn this frame.
14. `device_->submit()` (`:758`) — its own internal defensive wait
    (`vulkan_device.cpp:576`) is now a verified no-op (Milestone 1's own
    dedicated GPU test proves this at the `VulkanDevice` level;
    Milestone 3's own regression proves `runFrame()`'s own integration
    does not regress it).
15. Format-rebuild candidate swap-in on `Ok` (`:776-779`) — unchanged.
16. Conditional `waitIdle()` on a realization frame (`:795-802`) —
    unchanged.
17. `presentation_->present()` (`:817`) — unchanged.

**Invariants this order preserves, verified against every listed step:**
every GPU-visible host write and every descriptor-set update happens
strictly after step 6's drain (steps 10-13 all follow step 6); a
zero-extent/deferred/acquire-failure frame never drains and never writes
(steps 4-5 return before step 6 is reached); a `submit()` failure never
leaves a retained submission (`vulkan_device.cpp:613-619`: a failed
`vkQueueSubmit` never updates `retainedSubmission_`/`hasRetainedSubmission_`,
unchanged by this Plan); the next successful frame always re-derives its
payload from `World`'s then-current state (step 6 calls
`drainAndPublishFrameUniforms()` fresh every time it is reached — no
persisted "pending update" flag exists to go stale); the format-change
candidate/old-bundle submit-safe replacement rule is untouched (step 15
unchanged, still gated on step 14's own `Ok`).

### P4. Dynamic Lighting extraction model

Full re-extraction, every successful frame, per P2's helper body steps
7-9 above — `lightingDataCaptured_` (`runtime_application.h:117-123`) is
deleted, not merely un-set. No dirty bit, no revision counter: because
`World::updateTransforms()` already unconditionally recomputes every
entity's world matrix on every call (Pre-draft verification above), and
`world.lightEntities()`/`getLight()`/`getWorldMatrix()` always read
`World`'s current live state, this model automatically and correctly
covers every mutation source Spec 0022 requires coverage for: `setLight()`,
a Light entity's own `setLocalTransform()`, a parent's `setLocalTransform()`/
`setParent()` affecting a Light's world matrix, and Light entity
creation/removal (each reflected in `lightEntities()`'s/`getWorldMatrix()`'s
own existing, already-correct behavior — no `World`-side code change).
Same-frame multiple mutations resolve to final-value semantics for free
(the extraction reads whatever `World` holds at the moment it is called,
once per frame, after the drain). A skipped/deferred/failed frame never
"consumes" anything, since there is no persisted update state to consume
— the next successful frame's own call to step 6 re-derives everything
fresh from `World`, independent of what any earlier frame did.

### P5. `SubmitError`/extraction error mapping — exhaustive, no duplication, no assert downgrade

| Original call site (today) | Original handling | New handling |
|---|---|---|
| `!activeCamera.has_value()` (`:500-513`) | `ATLANTIS_LOG_ERROR("runFrame(): World has no active camera")`, `markFailed()`, return | Same log text (function name updated to `drainAndPublishFrameUniforms()`), `Err(DataExtractionFailed)`; caller does `markFailed()`+return |
| `cameraWorldMatrixResult`/`cameraComponentResult` (`:517-521`) | `ATLANTIS_CHECK_MSG` (hard assert) | Unchanged — still `ATLANTIS_CHECK_MSG`, not softened to a `Result` path by this Plan |
| `extractionResult.isErr()` (`:529-536`) | Log, `markFailed()`, return | Same log text, `Err(DataExtractionFailed)`; caller does `markFailed()`+return |
| `lightingResult.isErr()` (`:558-566`) | Log, `markFailed()`, return | Same log text, `Err(DataExtractionFailed)`; caller does `markFailed()`+return |
| `device_->submit()` failure (`:758-762`) | Log, `classifySubmitError()`, `markFailed()`, return | **Unchanged** — this Plan does not touch `submit()`'s own call site |
| **New:** `device.waitForPreviousSubmission()` failure | — | Log (inside `drainAndPublishFrameUniforms()`), `classifySubmitError()` (matching `submit()`'s own existing convention for this exact error domain), `Err(PreviousSubmissionWaitFailed)`; caller does `markFailed()`+return |

No error is swallowed, downgraded to an assert, or given a new,
duplicate error domain — `FrameUniformPublishError` has exactly two
variants, matching the two genuinely different classify-or-not behaviors
above, and reuses `SubmitError` for the wait step as-is (ADR-0065,
unchanged). `waitIdle()`'s own semantics and its two existing call sites
(shutdown/mid-frame-exit cleanup, and the conditional post-realization
drain at `:795-802`) are untouched — this Plan does not replace either
with the new method. If `runFrame()`'s own call site needs a per-variant
log message distinguishing `PreviousSubmissionWaitFailed` from
`DataExtractionFailed` beyond what `drainAndPublishFrameUniforms()`
already logs internally, that dispatch is a `switch` over
`FrameUniformPublishError` with **no `default:` case**, matching
`selectShaderPair()`'s own existing, cited precedent
(`material_realization.cpp:87-99`) — this reuses the *already-target-scoped*
`/w14062` flag on `atlantis_runtime_host`
(`src/runtime/CMakeLists.txt:48-57`, confirmed present, no new CMake
edit needed) so a future third `FrameUniformPublishError` variant added
without a matching case is a hard build error, not a silent fallback.

### P6. Buffer/Pipeline lifecycle — ownership order, proven by existing member order, not merely asserted

`RuntimeApplication`'s own member declaration order
(`runtime_application.h:104-180`, confirmed unchanged by this Plan):
`device_` (`:105`) → `presentation_` (`:106`) → `meshResourceMap_`
(`:115`) → `cameraBuffer_` (`:116`) → [`lightingDataCaptured_`, deleted
by this Plan] → `depthTexture_` (`:124`) → ... → `materialResourceMap_`/
`fallbackMaterial_` (`:156-157`) → ... → `world_` (`:180`). Per the file's
own existing comment (`:100-103`): construction is declaration order,
destruction is the reverse — confirmed real shutdown order is Material →
Texture → Buffer → Mesh → Presentation → Device, i.e. `cameraBuffer_`
outlives every Material/Pipeline that borrows it and is destroyed only
after every Texture and before `meshResourceMap_`/`presentation_`/
`device_`. This Plan changes nothing about this order — deleting
`lightingDataCaptured_` removes a `bool` between two `unique_ptr`
members, no ownership relationship depends on that field's presence.
Candidate (format-change) and persistent Material bundles both bind the
*same* `cameraBuffer_` (Pre-draft verification above) — the drain/write
reordering in P3 does not change which buffer any Pipeline binds, only
when the shared buffer's bytes are written relative to the drain.

### P7. Minimal test seam — one local fake pair, one test TU, no new architecture

No `FakeDevice` exists (Pre-draft verification above). This Plan adds
exactly one, matching the user-approved first option: a **local,
non-exported `Device` implementation and a local, non-exported `Buffer`
implementation, both defined only inside a new
`tests/runtime/frame_uniform_publish_tests.cpp`** (GPU-independent,
matching this directory's existing non-`_gpu_` naming convention, e.g.
`lifecycle_state_tests.cpp`, `error_classification_tests.cpp`).

`FakeDevice` implements all ten `Device` pure virtuals: nine (every
method except `waitForPreviousSubmission()`) each body is
`ATLANTIS_CHECK_MSG(false, "not expected to be called by
drainAndPublishFrameUniforms()")` — a genuinely unreachable marker, never
called by the function under test, matching `selectShaderPair()`'s own
established "unreachable, fail-fast guard" idiom (P5 above) applied here
to an entire method body rather than a `switch` arm.
`waitForPreviousSubmission()` itself is the one instrumented method: it
appends a token (e.g. `"wait"`) to a `std::vector<std::string>&` call-log
the test owns, and returns whatever `Result<std::monostate, SubmitError>`
the test configured (`Ok` or a specific `Err`) for that test case.

`FakeBuffer` implements all three `Buffer` pure virtuals: `purpose()`
returns `BufferPurpose::Uniform`; `sizeBytes()` returns 304; `mappedData()`
appends a token (e.g. `"write"`) to the same call-log and returns a
pointer into a real, test-owned `std::array<std::byte, 304>` backing
store (so the two real write loops inside `drainAndPublishFrameUniforms()`
execute against real, addressable memory — not a null pointer — letting a
test also assert on the written byte content directly, not only on call
order).

Tests construct a real `atlantis::world::World` (already GPU-independent
and unit-tested elsewhere, `tests/world/`) with a real active `Camera`
and a real `Light`, so `drainAndPublishFrameUniforms()`'s own extraction
steps run against real, correct `World` behavior — only the RHI-facing
edges (`Device`, `Buffer`) are faked.

This satisfies every constraint: no new `public` `FakeDevice` module (the
class is a local, unnamed-in-any-header type inside one `.cpp`); no
service locator (`drainAndPublishFrameUniforms()` already takes `Device&`/
`Buffer&`/`World&` by reference — the exact same explicit-dependency-
injection-by-parameter shape `extractCameraMatrices()` et al. already
use, not a new mechanism); no test-only introspection added to any
production `Device`/`Buffer` type (the instrumentation lives entirely in
the test-local fakes, `VulkanDevice`/`VulkanBuffer` are untouched); no
duplication of `runFrame()`'s own state machine (the fakes exercise only
`drainAndPublishFrameUniforms()` in isolation, never a simulated frame
loop).

**Split responsibility, per this Plan's own drafting directive:** this
`FakeDevice`-based suite proves call *order* (wait before write; wait
failure leaves the write, and the log-appended "write" token, entirely
absent) and error propagation — it cannot, and does not attempt to,
prove `VulkanDevice::waitForPreviousSubmission()`'s own real idempotence
or `submit()`'s own internal defensive-wait no-op behavior, since those
depend on `VulkanDevice`'s real fence/retained-submission state a fake
cannot stand in for. Those two properties are proven instead by real
Vulkan Backend GPU tests (Milestone 1, `tests/vulkan_backend/frame_execution_gpu_tests.cpp`):
submit a frame, call `waitForPreviousSubmission()` explicitly, call it a
second time and confirm it returns `Ok` promptly (no hang — a second,
incorrect internal wait on an already-reset fence would either hang or
produce a validation/driver error, so a bounded-time, validation-clean
pass is the real proof); then call `submit()` again and confirm it also
succeeds without a validation-layer complaint about waiting on a fence in
an unexpected state — proving `submit()`'s own internal call became a
correct no-op after the explicit drain, not by inspecting private state,
but by the absence of the failure that an incorrect no-op would cause.

### P8. Descriptor-set-update safety (Hazard B) — a test that fails if the fix regresses, not one that only happens to pass

Two dedicated verification angles, neither satisfied by final-pixel
correctness alone (a regression could still render correctly on fast
hardware while remaining unsafe, exactly as today's undetected gap does):

- **Call-order regression test** (GPU-independent, P7's own fake):
  because `drainAndPublishFrameUniforms()` is now the *only* place any
  Camera/Lighting write happens, and it is called (P3 step 6) strictly
  before `createCommandList()` (step 10) and `renderer_.drawFrame()`
  (step 13), a test that mis-orders these — calling
  `drainAndPublishFrameUniforms()` after constructing a `CommandList` —
  is structurally impossible to write against `runFrame()`'s own real
  code today (the write statements no longer exist at the old call site
  at all, per P2) — this is the "fails if moved" property this Plan
  achieves by construction, not by a new assertion. A dedicated
  regression test additionally locks P3's own step order via the
  call-log: asserts the `"wait"` token's index in the shared log precedes
  every `"write"` token's index, for both the Camera and Lighting writes.
- **Real GPU, multi-material stress test** (`tests/vulkan_backend/frame_execution_gpu_tests.cpp`
  or `tests/runtime/material_realization_gpu_tests.cpp`, extended): a
  scene with two or more distinct Materials (reusing Spec 0021's own
  established multi-material stress pattern, `descriptor_pool_growth_gpu_tests.cpp`'s
  own precedent) drawn across multiple frames, Validation Layers running
  with full verbose output, confirming zero `VUID`/Validation Error/
  Validation Warning hits across the run — the class of hit Hazard B
  would produce if the drain were ever positioned after
  `Renderer::drawFrame()`'s own descriptor updates. Spec 0021's own N=2/
  N=6 pool-growth/reuse regression tests are re-run unmodified as part of
  this suite to confirm no interaction (Pre-draft verification above,
  Spec 0022 Final Review Round item 8).

No `UPDATE_AFTER_BIND` flag is added to any descriptor set layout, and no
per-frame descriptor-set ring or allocator is introduced — both remain
explicitly out of scope (Spec 0022 Non-Goals; this Plan's own fix needs
neither).

### P9. Milestone atomic boundaries — summary (see Milestones below for the full breakdown)

1. RHI API (P1): `device.h` + `vulkan_device.h`/`.cpp` + its own GPU test,
   together — the only implementer, updated atomically, never left with
   an unimplemented pure virtual.
2. Runtime helper (P2) + its own `FakeDevice`/`FakeBuffer` unit tests
   (P7) — standalone, compiles and is tested independently of `runFrame()`
   integration, since it is a free function.
3. `runFrame()` integration (P3) + `lightingDataCaptured_` removal (P4) +
   error mapping (P5) — together, since splitting them would leave
   `runFrame()` calling a helper that either doesn't exist yet or exists
   unused; this Milestone also lands the dedicated multi-frame Camera/
   Lighting GPU regression tests, since an integration this Plan
   disclosed as a real behavior change must not land without its own
   regression coverage in the same commit.
4. Dedicated Hazard B verification (P8) — depends on Milestone 3's own
   integration existing to test against.
5. Full verification matrix, golden-boundary confirmation, and
   documentation/registry sync — depends on every prior Milestone.

## Plan Review (drafting-time self-review)

1. **New wait point earlier than mapped write and descriptor update?**
   Yes — P3 step 6 (the sole call to `drainAndPublishFrameUniforms()`)
   precedes step 10 (`createCommandList()`) and step 13
   (`renderer_.drawFrame()`'s own bind/update calls); the write
   statements themselves no longer exist outside `drainAndPublishFrameUniforms()`'s
   own body (P2), so this is a structural, not disciplinary, guarantee.
2. **Acquire failure/zero extent avoid wasted drain?** Yes — P3 steps 4-5
   return before step 6 is ever reached, confirmed against
   `runtime_application.cpp:408-416`.
3. **`submit()`'s defensive wait retained and shared?** Yes — P1 keeps
   `vulkan_device.cpp:576` verbatim; both it and the new public method
   call the identical private `waitAndReleaseRetainedSubmission()`, never
   two implementations.
4. **Wait failure truly zero side effects?** Yes — P2's helper returns
   immediately after step 1's `Err`, before `world.updateTransforms()`,
   before any extraction, before any mapped write; `runFrame()`'s own
   call site (P3 step 6) never reaches `createCommandList()`/`drawFrame()`/
   `submit()` on that path, matching P5's mapping table exactly.
5. **Host-coherent contract has source evidence?** Yes —
   `vulkan_device.cpp:770-772`, re-confirmed against current `main` in
   Pre-draft verification above, not carried forward from memory alone.
6. **World transforms updated every frame?** Yes — confirmed
   `world_->updateTransforms()` is already unconditional, every
   successful frame, and `World::updateTransforms()` itself is a full
   recompute with no dirty-bit gate (Pre-draft verification above) — no
   gap found, no change needed beyond continuing to call it once per
   frame from inside the new helper.
7. **Dynamic payload covers every `World` mutation source?** Yes — P4
   enumerates `setLight()`, Light `setLocalTransform()`, parent
   `setLocalTransform()`/`setParent()`, and Light creation/removal, each
   covered for free by full re-extraction reading `World`'s live state,
   not by enumerating mutation call sites individually.
8. **Buffer/Pipeline lifecycle safe?** Yes — P6 traces the real,
   unchanged member declaration/destruction order; this Plan does not
   alter it, only deletes one `bool` field between two unrelated
   `unique_ptr` members.
9. **Test seam minimal, not architecture-expanding?** Yes — P7: one local
   fake pair in one test `.cpp`, reusing the existing explicit-parameter-
   injection shape `scene_extraction.h`'s own functions already use; no
   new `public` type, no service locator, no production-code
   introspection hook.
10. **Ring buffer/staging not pre-implemented?** Confirmed — this Plan
    implements only Model A (ADR-0065); Model B/C remain Spec 0022's own
    disclosed-but-rejected alternatives, untouched here.
11. **No new threads, locks, third-party dependency, Android/Linux code?**
    Confirmed — every new symbol in this Plan (`Device::waitForPreviousSubmission()`,
    `drainAndPublishFrameUniforms()`, `FrameUniformPublishError`) is
    ordinary, single-threaded C++/Vulkan matching this codebase's
    existing style; no CMake target list change beyond the two new
    `frame_uniform_publish.h`/`.cpp` source files being added to
    `atlantis_runtime_host`'s own existing target.
12. **No change to Spec 0018/0019/0021's own approved lifecycle
    contracts?** Confirmed — P3's own frame-order table keeps the
    format-change candidate swap-in (Spec 0018 D9) gated on this frame's
    own `submit()` returning `Ok`, unchanged; Spec 0019's `FrameLightingData`
    byte layout/size is unchanged (still 304 bytes total, still at the
    same offsets — this Plan changes *when* and *how often* it is
    written, never its shape); Spec 0021's descriptor-pool growth/reuse
    behavior is unmodified and re-run as regression coverage (P8).

No item above required stopping to raise a blocking objection — every
real constraint traced against current source either already holds
(World transform recompute, buffer coherence, member order) or is closed
by this Plan's own design (structural helper, minimal test seam, exact
error mapping).

## Milestones / Task Breakdown

### Milestone 1 — RHI `Device::waitForPreviousSubmission()`, atomic with its sole implementer

- Add the pure virtual to `src/rhi/include/atlantis/rhi/device.h` (P1).
- Implement it in `src/vulkan_backend/src/vulkan_device.h`/`.cpp` as a
  one-line forward to the existing private `waitAndReleaseRetainedSubmission()`
  (P1) — `submit()`'s own internal call is untouched.
- New GPU tests in `tests/vulkan_backend/frame_execution_gpu_tests.cpp`:
  no-op when nothing retained; idempotent second consecutive call;
  `submit()` → explicit `waitForPreviousSubmission()` → `submit()` again
  succeeds cleanly, Validation Layers clean (P7's split-responsibility
  real-GPU proof).
- Buildable and testable standalone — no Runtime change yet.

### Milestone 2 — `drainAndPublishFrameUniforms()`, standalone and unit-tested

- New `src/runtime/include/atlantis/runtime/frame_uniform_publish.h` /
  `src/runtime/src/frame_uniform_publish.cpp` (P2), added to
  `atlantis_runtime_host`'s existing CMake target.
- New GPU-independent `tests/runtime/frame_uniform_publish_tests.cpp`
  (P7): local `FakeDevice`/`FakeBuffer`, real `World` — call-order proof
  (wait before write), wait-failure zero-side-effect proof, extraction-
  failure-path proof (each of the three `DataExtractionFailed` sources),
  full 176+128-byte deterministic-content proof against real `World`
  Camera/Light state, same-frame-multiple-`setLight()`-calls final-value
  proof.
- Not yet called by `runFrame()` — buildable and testable standalone.

### Milestone 3 — `runFrame()` integration, `lightingDataCaptured_` removal, and dedicated multi-frame GPU regression

- Reorder `runFrame()` per P3's seventeen-step order; delete the old
  inline camera-write loops, the old one-time lighting-capture block, and
  `lightingDataCaptured_` (member + its comment, P4).
- Apply P5's exact error-mapping call site.
- Extend `tests/runtime/runtime_smoke_gpu_tests.cpp` and/or
  `tests/runtime/material_realization_gpu_tests.cpp` (Pre-draft
  verification above — the two existing multi-frame fixtures) with:
  multi-frame Camera Transform-change regression; multi-frame Directional
  Light direction/color/intensity and Point Light position/color/intensity
  regression (each independently); Light `setLocalTransform()` regression;
  Light parent `setLocalTransform()`/`setParent()` regression; Light
  creation/removal regression; format-change + dynamic-Lighting-in-the-
  same-frame regression; zero-extent/acquire-deferred frame does not lose
  a pending change (next successful frame still reflects current `World`
  state); submit-failure frame followed by a successful frame reflects
  current `World` state, not a stale value.
- Confirm (via repository-wide search in this Milestone's own PR) zero
  remaining references to `lightingDataCaptured_` anywhere in the tree.

### Milestone 4 — Hazard B dedicated verification

- Call-order regression test locking the `"wait"`-before-`"write"`
  invariant explicitly (P8), on top of Milestone 2's own suite.
- Real-GPU multi-material stress test with full verbose Validation
  Layers output, zero `VUID`/Error/Warning hits (P8).
- Re-run Spec 0021's own N=2/N=6 descriptor-pool-growth/reuse tests
  unmodified, confirming no interaction.
- Re-run Spec 0018's own submit-safe format-rebuild-candidate regression
  unmodified, confirming no interaction.

### Milestone 5 — Full verification matrix, golden boundary, documentation sync

- Debug and Release, `ctest -L gpu` and `ctest -LE gpu`, full verbose
  Vulkan Validation Layers scan, `ATLANTIS_BUILD_TESTS=OFF` clean build,
  module/link boundary scan, `git diff --check`.
- Confirm all five existing goldens byte-identical/pixel-zero-difference
  — no golden regenerated.
- Programmatic multi-frame pixel/byte comparison evidence for dynamic
  Camera/Lighting correctness (Spec 0022's own decided golden strategy) —
  no new golden PNG created by this Plan; if this Milestone's own review
  finds programmatic evidence insufficient, stop and raise it rather than
  silently adding one outside ADR-0042's own two-phase bootstrap process.
- `specs/README.md`: Spec 0022's own row gains a "Related Plan" link to
  this Plan (`In Review`) — no other registry file touched (this is a
  Plan-drafting round, not a docs-closeout round; PR #101's own Spec/Plan
  0021 closeout content is not duplicated here).

## Files / Modules Touched (expected)

- `src/rhi/include/atlantis/rhi/device.h` — new pure virtual.
- `src/vulkan_backend/src/vulkan_device.h`/`.cpp` — new implementation.
- `src/runtime/include/atlantis/runtime/frame_uniform_publish.h` (new) —
  `FrameUniformPublishError`, `drainAndPublishFrameUniforms()`.
- `src/runtime/src/frame_uniform_publish.cpp` (new).
- `src/runtime/CMakeLists.txt` — add the two new source files to the
  existing `atlantis_runtime_host` target (no new target, no new flag —
  the `/w14062` C4062 flag is already target-scoped here).
- `src/runtime/include/atlantis/runtime/runtime_application.h` — delete
  `lightingDataCaptured_` and its comment.
- `src/runtime/src/runtime_application.cpp` — reorder `runFrame()` per
  P3; delete the old inline camera/lighting write code.
- `tests/vulkan_backend/frame_execution_gpu_tests.cpp` — new
  `waitForPreviousSubmission()` GPU tests.
- `tests/runtime/frame_uniform_publish_tests.cpp` (new) —
  `FakeDevice`/`FakeBuffer` unit tests.
- `tests/runtime/runtime_smoke_gpu_tests.cpp` and/or
  `tests/runtime/material_realization_gpu_tests.cpp` — multi-frame
  dynamic Camera/Lighting/Hazard-B regression additions.
- `specs/README.md` — Spec 0022's own Related Plan link only.

No other file is expected to change. A deviation from this list is a
finding to call out explicitly in the Implementation PR, not to fold in
silently (AGENTS.md).

## Sequencing & Dependencies

Milestone 1 → Milestone 2 (independent of 1's own Runtime-side effects,
but needs `Device::waitForPreviousSubmission()` to exist to compile
against) → Milestone 3 (needs Milestone 2's helper) → Milestone 4 (needs
Milestone 3's integration to test against) → Milestone 5 (needs every
prior Milestone complete). No Milestone may be merged leaving `Device`
with an unimplemented pure virtual, `runFrame()` calling a
not-yet-existing helper, or a Hazard-B regression test added before the
integration it tests exists.

## Verification Checklist

**GPU-independent:**

- [ ] `Device::waitForPreviousSubmission()` — `Ok` no-op when nothing
      retained (`FakeDevice`-independent; also covered at the real
      `VulkanDevice` level in Milestone 1).
- [ ] Call-order proof: `drainAndPublishFrameUniforms()`'s own `"wait"`
      log token precedes every `"write"` token.
- [ ] Wait-failure proof: zero `"write"` tokens, zero further calls, for
      every configured `SubmitError` variant.
- [ ] Extraction-failure proofs: `NoActiveCamera`-equivalent,
      `extractCameraMatrices()` failure, `extractFrameLightingData()`
      failure — each returns `DataExtractionFailed`, each still performs
      the drain (world/`Buffer` untouched only on the wait-failure path,
      not on these).
- [ ] Full 304-byte deterministic content proof against a real `World`.
- [ ] Same-frame multiple `setLight()` calls resolve to final value.
- [ ] Zero-extent/acquire-failure paths never call
      `drainAndPublishFrameUniforms()`.
- [ ] Repository-wide zero remaining references to `lightingDataCaptured_`.
- [ ] `git diff --check` clean.

**Real Vulkan/GPU:**

- [ ] `submit()` → explicit `waitForPreviousSubmission()` → second
      `waitForPreviousSubmission()` → `Ok`, no hang, Validation Layers
      clean.
- [ ] `submit()` → explicit drain → `submit()` again succeeds, Validation
      Layers clean (proves the internal defensive wait's own no-op).
- [ ] Multi-frame Camera Transform-change regression, pixel-level.
- [ ] Directional Light direction/color/intensity, each independently,
      multi-frame, pixel-level.
- [ ] Point Light position/color/intensity, each independently,
      multi-frame, pixel-level.
- [ ] Light `setLocalTransform()` regression, pixel-level.
- [ ] Light parent `setLocalTransform()`/`setParent()` regression,
      pixel-level.
- [ ] Light creation/removal regression, pixel-level.
- [ ] Multi-material scene: all descriptor updates occur after the drain,
      Validation Layers clean, full verbose output scanned.
- [ ] Format-change + dynamic Lighting in the same frame.
- [ ] Spec 0018 submit-safe bundle-replacement regression, unmodified,
      re-passing.
- [ ] Spec 0021 descriptor-pool N=2/N=6/reuse regression, unmodified,
      re-passing.
- [ ] Full verbose Validation Layers scan across the entire suite: zero
      `VUID`/Validation Error/Validation Warning.

**Golden and full-matrix:**

- [ ] All five existing goldens byte-identical to `main`.
- [ ] All five existing goldens pixel-zero-difference.
- [ ] Dynamic-correctness evidence via programmatic multi-frame
      buffer/pixel comparison — no new golden PNG created without
      returning to ADR-0042's own Initial baseline bootstrap process.
- [ ] Debug and Release builds clean.
- [ ] `ctest -LE gpu` full pass, both configurations.
- [ ] `ctest -L gpu` full pass, both configurations.
- [ ] `ATLANTIS_BUILD_TESTS=OFF` clean configure+build.
- [ ] Module/link boundary scan: no new dependency edge.
- [ ] `specs/README.md` Spec 0022 row's Related Plan link updated.

## Rollback Plan

Every Milestone is independently revertible in reverse order (5→1): a
Milestone-5-only revert removes only documentation/registry sync;
Milestone 4's revert removes only the added Hazard-B-specific tests, not
the fix itself; Milestone 3's revert restores `runFrame()`'s previous
inline order and `lightingDataCaptured_` (reverting the single commit
that removed them); Milestone 2's revert removes the standalone,
not-yet-integrated helper with zero effect on `runFrame()`'s own
behavior (nothing calls it until Milestone 3); Milestone 1's revert
requires Milestone 2/3/4 already reverted first (since they depend on
the new `Device` method existing) — this ordering constraint is stated
explicitly here so a partial rollback is never attempted out of order.

## Definition of Done

Per [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan: the Verification Checklist above's
GPU-independent, real-GPU, and golden/full-matrix sections must each be
fully checked before this Plan's own Implementation PR is considered
complete; no golden is regenerated by this Plan under any checklist item.
