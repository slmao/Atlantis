# ADR 0065: Explicit Pre-Write Submission Drain for Frame Uniform Safety

- **Status:** `Accepted`
- **Date:** 2026-08-30
- **Deciders:** slmao <slmaosjtu@gmail.com>
- **Related Spec:** [Spec 0022 — Dynamic Frame Uniform Updates Foundation](../specs/0022-dynamic-frame-uniform-updates-foundation.md)
  (`Approved`)

## Context

Spec 0022's own investigation traced `RuntimeApplication::runFrame()`'s
exact, real call order against `Renderer::drawFrame()`,
`VulkanCommandList`, and `VulkanDevice::submit()`'s exact internal order
and found two real, currently-shipped hazards sharing one root cause:

**Hazard A.** The CPU writes the current frame's Camera (and, on the
Spec's revised design, every frame's) Lighting data directly into
`cameraBuffer_->mappedData()`
(`src/runtime/src/runtime_application.cpp:539-541`, `:573-575`) before
`device_->submit()` is called (`:758`).

**Hazard B.** `Renderer::drawFrame()`
(`src/renderer/src/renderer.cpp:26-42`) calls, for every `DrawItem`,
`CommandList::bindUniformBuffer()`/`bindTexture()`
(`src/vulkan_backend/src/vulkan_command_list.cpp:234-318`), each of which
calls `vkUpdateDescriptorSets()` against the drawn Pipeline's own single,
persistent `VkDescriptorSet` — once per frame per Pipeline, since the
intra-recording redundant-write cache (`lastUpdatedDescriptorSet_` et al.)
is a member of the per-frame `VulkanCommandList` object and starts empty
every frame. This happens during command recording, in the same
before-`submit()` window as Hazard A. The descriptor set layout was built
without `VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT`, so this is not a
supported pattern while a previous, still-pending submission may reference
that same descriptor set.

Both hazards trace to the same missing synchronization point: the one
CPU-blocking wait that would prove the *previous* frame's GPU work has
finished — `waitAndReleaseRetainedSubmission()`, via `vkWaitForFences` —
runs only from *inside* `VulkanDevice::submit()`
(`vulkan_device.cpp:576`), after both hazards' own writes/updates for the
current frame have already happened. `presentation_->acquireNextTarget()`
provides no such guarantee (`vkAcquireNextImageKHR`'s fence argument is
`VK_NULL_HANDLE`, `vulkan_presentation.cpp:574-575`) — a GPU-side
semaphore handoff only. The one full drain, `Device::waitIdle()`, runs
only conditionally, gated by a material having been realized that frame
(`runtime_application.cpp:795`) — not on an ordinary frame.

`Buffer::mappedData()`'s own documented contract
(`src/rhi/include/atlantis/rhi/buffer.h:24-33`) claims this is
"structurally" satisfied by Phase 1's single-frame-in-flight discipline;
the trace above shows no call in the current path actually provides that
guarantee before either hazard, and the contract is silent on Hazard B
entirely (it is scoped to `Buffer`'s own write precondition, not to
`CommandList`'s own descriptor-update timing).

This decision must close both hazards with one mechanism at one call
point, not design a Lighting-specific fix while leaving Camera's — and
every drawn Pipeline's descriptor set — unaddressed. Forces this decision
must honor: no multi-threaded frame orchestration (ADR-0004); Plan 0006's
single-frame-in-flight baseline is not being replaced; the fix must not
force Material/Pipeline duplication unless a materially better model
requires it (confirmed unnecessary — Spec 0022's Final Review Round,
Buffer/Pipeline binding section); and this codebase's own "one decision
per ADR" convention — this ADR records only the RHI surface/frame-lifecycle
decision, not Lighting's own update-extraction model (Spec 0022's own
Final Review Round item 6).

## Decision

Add one new public method to the RHI `Device` interface,
`Device::waitForPreviousSubmission()`, returning
`Result<std::monostate, SubmitError>` — reusing `SubmitError` as-is (no
new error variant; the method performs exactly the Vulkan operations
`submit()`'s own internal call already performs, so no new failure mode
exists to classify). Its Vulkan Backend implementation is exactly
`VulkanDevice::waitAndReleaseRetainedSubmission()`'s existing body
(`vulkan_device.cpp:514-533`), exposed publicly rather than kept private,
with no change to that body's own logic — `submit()` and the new public
method share the identical implementation, never two divergent copies.

`RuntimeApplication::runFrame()` calls this new method exactly once per
frame, at exactly one point: **immediately after a successful, non-null
`acquireNextTarget()` result, before the format-change candidate build,
before any Camera/Lighting mapped-memory write, before
`createCommandList()`, and before `Renderer::drawFrame()`'s own
`bindUniformBuffer()`/`bindTexture()` calls.** It is not called on an
acquire failure or a zero-extent/deferred-acquire frame (both already
return before reaching this point; shutdown's existing, unconditional
`waitIdle()` still drains any retained submission before destruction on
those paths). See Spec 0022's Final Review Round, "Full frame timing,"
for the complete nineteen-step trace proving this call point precedes
both Hazard A and Hazard B for every Pipeline drawn that frame.

`VulkanDevice::submit()` keeps its own existing internal call to
`waitAndReleaseRetainedSubmission()` (`vulkan_device.cpp:576`) unchanged —
on a frame that already drained explicitly, this becomes a verified no-op
via the function's own existing `hasRetainedSubmission_` early return
(`:517-519`). It is kept deliberately, as a defensive, idempotent
fallback for any other caller of `submit()` that does not call the new
method first, rather than removed — removing it would require proving
every such caller in this codebase and its tests already drains
explicitly, which this ADR does not attempt.

The method is backend-independent in interface (no `Vk*` type crosses the
RHI boundary), single-threaded/non-reentrant (matching every other
`Device` method's existing, implicit contract under Phase 1's ADR-0004
baseline), idempotent (a second consecutive call is a verified no-op), and
distinct from `waitIdle()` (does not drain presentation-engine-internal
state — scoped to exactly the one retained submission Phase 1's baseline
ever has).

## Consequences

### Positive

- Closes both a real, currently-shipped mapped-memory race (Hazard A) and
  a real, currently-shipped descriptor-set-update race (Hazard B) in the
  Camera update path, not only a hypothetical future Lighting one — for
  every Pipeline/Material drawn in a frame, not a single special case.
- Minimal API surface: one new `Device` method, matching an existing
  private implementation exactly — no new type, no new buffer, no new
  ownership question, no new error domain.
- No Material/Pipeline duplication; no new `CommandList` capability; no
  new `RenderGraph` `Buffer`-tracking dimension.
- `submit()`'s own existing behavior and error semantics are unchanged.
- Confirmed to not interact with `present()`'s own semaphore lifecycle,
  Spec 0021's descriptor pool growth, or Spec 0018 D9's format-change
  candidate publish gate (Spec 0022 Final Review Round, Lifecycle
  section) — each independently verified against real code, not assumed.
- Portable: uses only `vkWaitForFences`/`vkResetFences`, already in use
  today, with no new Vulkan feature or extension dependency.

### Negative / Trade-offs

- Moves a CPU-blocking wait earlier in `runFrame()`'s own execution,
  narrowing the window in which unrelated per-frame CPU work could
  otherwise overlap with the previous frame's tail-end GPU execution.
  Accepted as a Phase 1 correctness-first baseline (Spec 0022 Final
  Review Round, Performance section) — not a change in how often the CPU
  blocks (once per frame, unchanged), only when.
- Adds a second public `Device` method with wait semantics
  (`waitForPreviousSubmission()` alongside `waitIdle()`); callers must
  understand the two are not interchangeable.
- Does not, by itself, give Lighting a dynamic-update mechanism — Spec
  0022's own extraction-timing decision (full re-extraction every
  successful frame) is a separate, non-architectural decision recorded in
  the Spec, not this ADR.

## Alternatives Considered

- **A multi-slot uniform-buffer ring buffer (Model B).** Rejected for
  this decision: `bindUniformBuffer()` already supports per-frame
  rebinding without a `CommandList` signature change, but a ring buffer
  does not avoid Hazard B by itself — it still needs its own proof that
  the slot being written is not the one a still-pending submission reads,
  the identical class of analysis this ADR performs for Model A — plus
  real slot-count derivation and slot-selection identity, neither of
  which exists in `Presentation` today. Larger surface for the same
  safety property.
- **A staging buffer plus a `CommandList`-recorded copy (Model C).**
  Rejected for this decision: `CommandList` has no buffer-to-buffer copy
  today, and `RenderGraph`'s own `ResourceState` tracking does not cover
  `Buffer` — a new tracking dimension, not only a new method. Still needs
  its own Hazard B answer for the copy's destination buffer's descriptor
  set. Larger surface for the same safety property.
- **Reuse the existing public `Device::waitIdle()` at the new call site.**
  Rejected: `waitIdle()` deliberately drains more than the one retained
  submission (presentation-engine-internal state too), conflating a
  narrow per-frame need with a broader, coarser-grained one.
- **Fix only the Camera/Lighting mapped-write race (Hazard A), leaving
  descriptor-set updates (Hazard B) unaddressed.** Rejected outright
  during the Final Review Round: this was the original draft's own scope,
  found during Spec 0022's own final review to leave a real, distinct
  Vulkan synchronization hazard unaddressed for every drawn Pipeline, not
  only Camera/Lighting's own bytes.
