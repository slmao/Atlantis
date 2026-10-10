# ADR 0115: RHI Instrumentation Boundary — Regions, Timestamps, Debug Names

- **Status:** Proposed
- **Date:** 2026-10-10
- **Deciders:** slmao (pending)
- **Acceptance:** Pending (review of Spec 0059's own branch PR).
- **Related Spec:** [Spec 0059](../specs/0059-profiling-instrumentation.md) (`Draft`)
- **Related ADR(s):**
  - [ADR-0001](0001-rhi-backend-independence.md): only the Vulkan Backend names
    `Vk*` types — preserved.
  - [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md): the recording model
    this extends.
  - Paired with [ADR-0114](0114-profiling-api-build-configuration-and-recorder.md).

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- `ATL_PROFILE_GPU_SCOPE(cmd, name)` is written in Renderer and RenderGraph, which
  must not see Vulkan. GPU timestamps, `VK_EXT_debug_utils` labels and object
  names are Vulkan facilities.
- `render_graph::execute()` is the one place every pass is bracketed; it already
  holds each pass's label (`CompiledGraph::label`).
- One frame is in flight, and `VulkanDevice::submit()` already waits the
  previous frame's fence, so timestamp results are readable there without a new
  stall.
- Public RHI structs and the `CommandList` vtable must not vary with build
  options (ODR). The only `CommandList` implementers are `VulkanCommandList` and
  the test fake `tests/render_graph/fake_command_list.h`.
- Backend counters (draws, descriptor writes after the memo) are exact only
  inside the backend.

## Decision

1. **Regions.** `rhi::CommandList` gains `beginRegion(std::string_view name)` and
   `endRegion()`, and `setDebugName(std::string_view)`, as non-pure virtuals with
   empty bodies (existing fakes compile unchanged). Regions nest, are legal inside
   and outside a rendering scope, and must balance.
2. **One backend implementation owns every facility of a region**, each behind
   its own compile-time option: a debug label (`vkCmdBeginDebugUtilsLabelEXT`)
   under `ATLANTIS_GPU_MARKERS`; a timestamp pair into an Atlantis-owned query pool
   and, with Tracy, a Tracy Vulkan GPU zone, under `ATLANTIS_PROFILING`.
3. **Timestamp mechanics (Vulkan Backend only).**
   - One query pool of fixed capacity (overflow dropped and counted); reset by
     `vkCmdResetQueryPool` at the start of each command list (Vulkan 1.0 core);
     results read with `vkGetQueryPoolResults` immediately after the fence wait
     in `waitAndReleaseRetainedSubmission()`, converted with `timestampPeriod`
     and delivered to the Core recorder tagged with the submitting frame —
     one frame late, no new wait.
   - Absent `timestampValidBits` or `timestampPeriod` support is reported as
     "GPU timing unavailable", never an error.
   - Optional pipeline-statistics queries (fragment invocations, input-assembly
     primitives) on the `draw` pass only when the device feature exists.
4. **Automatic pass regions.** `render_graph::execute()` calls
   `beginRegion(label)` before a pass's transitions and `endRegion()` after its
   `endRendering()` (or callback, for non-draw passes). A pass authors nothing.
   Sub-regions inside `draw` (`Sky`, `Opaque`, `Transparent`) are Renderer's own
   `ATL_PROFILE_GPU_SCOPE` calls over contiguous segments of the existing draw
   order.
5. **Debug names.** Every creation-params struct (buffer, texture, sampled
   texture, sampler, HDR color target, shadow map, offscreen target, pipeline)
   gains `std::string_view debugName = {}`, copied by the backend at creation.
   The backend names `VkBuffer`, `VkImage`, `VkImageView`, `VkSampler`,
   `VkPipeline` and memory, names descriptor pools/sets from their pipeline's name,
   and command buffers from `setDebugName`. With markers off or the extension
   unavailable, the field is ignored; callers build names only when markers are
   compiled in.
6. **Instance extension.** `VK_EXT_debug_utils` is requested when markers are on
   and the loader or a layer offers it; its absence is never an error. The
   existing validation-only enabling and the Android gating are unchanged
   (markers are off on Android).
7. **Counters** are added inside the backend with `ATL_PROFILE_ADD` at the
   command-list methods that issue the Vulkan calls; no stats struct crosses the
   RHI.

## Consequences

### Positive

- Renderer and RenderGraph stay Vulkan-free; one public addition covers labels,
  timing and Tracy.
- RenderDoc captures name passes and resources; validation messages gain names.
- Counters are exact where the cost is incurred.

### Negative / Trade-offs

- A public RHI change (three methods, one field on eight structs), additive and
  defaulted.
- Two timestamp writes per region (Atlantis and Tracy) when both are on.
- Names require creation sites in Runtime/Renderer to supply strings.
- Timestamps perturb GPU scheduling slightly (reported in the overhead check).

## Alternatives Considered

- **Separate label and zone method pairs** — double surface, two calls per pass.
- **`Device::setDebugName(handle, name)`** — needs an RHI handle type and a
  second call at every creation site.
- **An ambient Core "next object name"** — hidden state.
- **Timestamps as RenderGraph resources** — a query object in the graph
  vocabulary for diagnostics.
- **Tracy as the only timing source** — the benchmark would have no per-pass GPU
  numbers; replaying our timestamps into Tracy's internal queue API is an
  unsupported surface.
- **Splitting `draw` into separate passes** so zones match the owner's names —
  changes barriers and possibly images; excluded by the Spec's no-behavior-change
  rule.
