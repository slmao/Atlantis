# ADR 0065: Explicit Pre-Write Submission Drain for Frame Uniform Safety

- **Status:** Proposed
- **Date:** 2026-08-30
- **Deciders:** Pending Human Review (accompanies Spec 0022)
- **Related Spec:** [Spec 0022 — Dynamic Frame Uniform Updates Foundation](../specs/0022-dynamic-frame-uniform-updates-foundation.md)
  (`In Review`)

## Context

Spec 0022's own investigation traced `RuntimeApplication::runFrame()`'s
exact, real call order (`src/runtime/src/runtime_application.cpp`) against
`VulkanDevice::submit()`'s exact internal order
(`src/vulkan_backend/src/vulkan_device.cpp`) and found a real,
pre-existing gap: the CPU writes the current frame's Camera (and, on one
frame, Lighting) data directly into `cameraBuffer_->mappedData()`
(`runtime_application.cpp:539-541`, `:573-575`) before `device_->submit()`
is called (`runtime_application.cpp:758`). The one CPU-blocking wait that
proves the *previous* frame's GPU work has finished —
`VulkanDevice::waitAndReleaseRetainedSubmission()`, which calls
`vkWaitForFences()` on `submissionFence_` — is invoked only from inside
`VulkanDevice::submit()` (`vulkan_device.cpp:576`), i.e. after the current
frame has already overwritten the buffer. `presentation_->acquireNextTarget()`,
the only call between the previous frame's `submit()` and the current
frame's write, calls `vkAcquireNextImageKHR()` with its fence argument
`VK_NULL_HANDLE` (`src/vulkan_backend/src/vulkan_presentation.cpp:574-575`)
— a GPU-side semaphore handoff, not a CPU wait. The one call that would
fully drain the GPU, `Device::waitIdle()`, runs only conditionally, gated
by `anyMaterialRealizedThisFrame` (`runtime_application.cpp:795`) — not on
an ordinary frame.

`Buffer::mappedData()`'s own documented contract
(`src/rhi/include/atlantis/rhi/buffer.h:24-33`) already states that
writing to a Uniform-purpose `Buffer` while a prior frame's GPU work might
still read it is a caller precondition violation, and claims Phase 1's
single-frame-in-flight discipline satisfies this "structurally" for the
one caller pattern the RHI documented ("write once per frame, immediately
after `acquireNextTarget()` returns"). The trace above shows no call in
the current path actually provides that guarantee before the write. This
is a genuine, currently-shipped data race on host-coherent memory (no
mutual exclusion between a concurrent CPU write and GPU read), not merely
a theoretical concern, and it predates Lighting Foundation — it is a
property of the original Camera-only design.

Any per-frame dynamic Lighting update (Spec 0022's own secondary goal)
that reused the same "write, then rely on the next `submit()`'s internal
drain" pattern would inherit the identical hazard, on every frame a light
changes rather than once per process lifetime — so this decision must
close the gap for both fields using one mechanism, not design Lighting
around it while leaving Camera as-is.

The forces this decision must honor: no multi-threaded frame
orchestration (ADR-0004); Plan 0006's single-frame-in-flight baseline is
not being replaced (out of Spec 0022's own scope); the fix must not force
Material/Pipeline duplication unless a materially better model requires
it; and the codebase's own "one decision per ADR" convention — this ADR
records only the RHI surface/frame-lifecycle decision below, not
Lighting's own dirty-tracking shape or update-visibility timing, both of
which remain open Human Review items on Spec 0022 itself.

## Decision

Add one new public method to the RHI `Device` interface,
`Device::waitForPreviousSubmission()`, returning
`Result<std::monostate, SubmitError>` — the same signature shape
`waitIdle()` already uses. Its Vulkan Backend implementation is exactly
`VulkanDevice::waitAndReleaseRetainedSubmission()`'s existing body
(`vulkan_device.cpp:514-533`), exposed publicly rather than kept private,
with no change to that body's own logic. `RuntimeApplication::runFrame()`
calls this new method once per frame, before writing any new bytes into
the Camera/Lighting uniform buffer, and before recording that frame's
`CommandList`. `VulkanDevice::submit()` keeps its own existing internal
call to `waitAndReleaseRetainedSubmission()` (`vulkan_device.cpp:576`)
unchanged — on a frame that already drained explicitly, this becomes a
no-op via the function's own existing `hasRetainedSubmission_` early
return (`vulkan_device.cpp:517-519`), so `submit()` requires no
conditional logic of its own to accommodate the new call site.

This closes the gap by relocating an already-necessary, already-occurring
wait earlier in frame order, rather than introducing a new buffer,
ownership model, or `CommandList` recording capability.

## Consequences

### Positive

- Closes a real, currently-shipped race in the Camera update path, not
  only a hypothetical future Lighting one.
- Minimal API surface: one new `Device` method, matching an existing
  private implementation exactly — no new type, no new buffer, no new
  ownership question.
- No Material/Pipeline duplication; no new `CommandList` capability.
- `submit()`'s own existing behavior and error semantics are unchanged;
  the new method reuses `SubmitError`'s existing classification rather
  than introducing a new error domain.
- Portable: uses only `vkWaitForFences`/`vkResetFences`, already in use
  today, with no new Vulkan feature or extension dependency — consistent
  with Android portability (Spec 0022 Human Review Decision Item 18).

### Negative / Trade-offs

- Moves a CPU-blocking wait earlier in `runFrame()`'s own execution: the
  CPU now blocks, once per frame, before recording that frame's draw
  commands, rather than immediately before submitting them — narrowing
  the window in which unrelated per-frame CPU work
  (`world_->updateTransforms()`, material realization) could otherwise
  overlap with the previous frame's tail-end GPU execution. The actual
  wall-clock cost is not measured by this ADR; Spec 0022's own Human
  Review Decision Item 2 covers whether this is an acceptable Phase-1
  baseline.
- Adds a second public `Device` method with wait semantics
  (`waitForPreviousSubmission()` alongside `waitIdle()`); callers must
  understand the two are not interchangeable — `waitIdle()` additionally
  drains presentation-engine-internal state (`vulkan_device.cpp`'s own
  "belt-and-suspenders" comment past line 624) that
  `waitForPreviousSubmission()` intentionally does not.
- Does not, by itself, give Lighting a dynamic-update mechanism — it only
  makes the write timing safe. Spec 0022's own remaining Human Review
  items (dirty tracking, update-visibility boundary) still need
  resolution before Lighting updates can actually ship.

## Alternatives Considered

- **A multi-slot uniform-buffer ring buffer.** Rejected as this ADR's own
  decision (not as a future possibility — Spec 0022 leaves it open as
  Human Review Decision Item 1/3) because it is a substantially larger
  change — new buffer ownership, slot-selection logic, and a real
  question about Material/Pipeline duplication — for the same safety
  property this decision achieves with zero new buffers. Investigation
  found it structurally feasible (`VulkanCommandList::bindUniformBuffer()`
  already reissues `vkUpdateDescriptorSets()` every call, so per-slot
  rebinding needs no `CommandList` signature change), but that feasibility
  does not by itself justify the larger surface when a minimal fix exists.
- **A staging buffer plus a `CommandList`-recorded copy into the real
  uniform buffer.** Rejected for this ADR's own decision because
  `CommandList` has no buffer-to-buffer copy today
  (`src/rhi/include/atlantis/rhi/command_list.h`'s existing
  `copyRenderTargetToBuffer()`/`copyBufferToTexture()` do not fit), so
  this alternative requires a strictly larger new RHI recording
  capability for the same outcome.
- **Reuse the existing public `Device::waitIdle()` at the new call site
  instead of adding a narrower method.** Rejected because `waitIdle()`
  deliberately does more than drain the one retained submission — it also
  drains presentation-engine-internal state Plan 0006 Section 11
  intentionally scoped to coarser lifecycle events (shutdown, mid-frame
  exit, post-material-realization publish), not the per-frame steady-state
  path this decision targets. Reusing it here would conflate a narrow need
  with a broader one and make a future audit of "why does this frame call
  `waitIdle()`" ambiguous between the two purposes.
- **Do nothing to Camera; add a narrower fix scoped only to Lighting's own
  new write.** Rejected outright — it would leave the real, disclosed
  Camera-path race unfixed while shipping a second field with the
  identical hazard, contradicting Spec 0022's own explicit mandate to
  treat a pre-existing Camera-path gap, if found, as its central problem.
