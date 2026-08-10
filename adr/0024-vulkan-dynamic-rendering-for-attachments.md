# ADR 0024: Vulkan Dynamic Rendering for Minimal Renderer Attachment Management

- **Status:** Proposed
- **Date:** 2026-08-11
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)

## Context

Drawing into a color attachment plus a depth attachment requires the
Vulkan Backend to scope GPU work to those attachments somehow. Classic
Vulkan does this via `VkRenderPass` + `VkFramebuffer` objects, created
ahead of time and matched to specific image views; core Vulkan 1.3 (and
the `VK_KHR_dynamic_rendering` extension on earlier versions) instead
lets a command buffer begin/end rendering directly against a list of
image views with no render-pass or framebuffer object at all.

This choice is entirely internal to the Vulkan Backend's `CommandList`
implementation — RHI's public surface does not need to expose either
mechanism by name (see
[ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
for the backend-agnostic attachment-scoping surface this ADR's choice sits
behind). But it is still a genuine, long-term architecture decision:
whichever mechanism the Vulkan Backend adopts now shapes how every future
multi-attachment, MSAA, or subpass-dependent feature gets built, and is
expensive to reverse once real rendering code depends on its
consequences (e.g. how attachment layouts are tracked, whether a
framebuffer-object cache exists). Per this repository's own explicit
instruction for this spec: this must be a reviewed `Proposed` ADR, not a
silent implementation-time choice, even though it never crosses RHI's
public interface.

The verified development/CI-relevant environment for this repository
(per Spec 0006's PR #24 verification record) runs a physical device
reporting Vulkan API version 1.4.335 against a
`VK_LAYER_KHRONOS_validation` Validation Layer, so hardware/driver support
for dynamic rendering is confirmed present. **However, this repository's
current, already-shipped Vulkan Backend implementation does not yet
request or require that version**: `vulkan_instance.cpp` sets
`applicationInfo.apiVersion = VK_API_VERSION_1_0`, and
`vulkan_device.cpp`'s physical-device selection only requires
`properties.apiVersion >= VK_API_VERSION_1_0`. Adopting this ADR's
decision therefore requires two concrete, previously-unstated
implementation changes this ADR fixes as requirements (exact code
locations/sequencing left to the Plan):

1. **Raise the requested/required Vulkan API version** to at least 1.3 —
   either by raising `applicationInfo.apiVersion` and the physical-device
   minimum-version check both to `VK_API_VERSION_1_3` (adopted this
   round; see Decision), or, if the Plan finds a reason to keep supporting
   a pre-1.3 core version, by keeping a lower core version and instead
   requiring the `VK_KHR_dynamic_rendering` device extension explicitly.
2. **Explicitly enable the `dynamicRendering` feature at device creation**
   — Vulkan 1.3 promotes dynamic rendering to core, but, like every
   Vulkan 1.2+ "core optional" feature, it is **not enabled by default
   merely by requesting a 1.3 device**: it must be requested via
   `VkPhysicalDeviceVulkan13Features::dynamicRendering` (or the equivalent
   `VkPhysicalDeviceDynamicRenderingFeatures` struct, if the
   extension-route above is chosen instead) chained into
   `VkDeviceCreateInfo::pNext` at `vkCreateDevice()` time. Omitting this
   step would make every `vkCmdBeginRendering` call in this spec's
   implementation invalid despite a sufficient API version being
   requested — a distinct failure mode from the version question above,
   and one this ADR calls out explicitly so the Plan does not conflate
   "requested a high enough version" with "enabled the feature."

## Decision

**The Vulkan Backend adopts Vulkan's core dynamic rendering
(`vkCmdBeginRendering`/`vkCmdEndRendering`, `VkRenderingInfo`/
`VkRenderingAttachmentInfo`) for all attachment scoping this spec
introduces, and does not create, cache, or otherwise use `VkRenderPass` or
`VkFramebuffer` objects anywhere in its implementation of the new
attachment-scoping surface.**

- The Vulkan Backend's `CommandList` implementation records
  `vkCmdBeginRendering` (targeting the bound color `RenderTarget`'s image
  view and, when present, the bound depth `Texture`'s image view) and a
  matching `vkCmdEndRendering`, around the drawable operations RenderGraph
  scopes to one pass (see
  [ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md)
  for exactly how RenderGraph derives when to call this).
  Load/store operations, clear values, and image layouts for each
  attachment are set per `VkRenderingAttachmentInfo`, computed from the
  same `ResourceState` transition bookkeeping
  ([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md),
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md))
  already governing every other transition — no new, parallel
  attachment-state model is introduced.
- `VkPipeline` objects created for this spec's minimal graphics pipeline
  ([ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
  are created with `VK_KHR_dynamic_rendering`'s
  `VkPipelineRenderingCreateInfo` chained in (or the equivalent core-1.3
  mechanism), specifying color/depth attachment formats directly —
  never through a `VkRenderPass` handle.
- This decision is confined to the Vulkan Backend's private
  implementation. It requires no new RHI public type and no new Vulkan
  Backend construction-API parameter — a future backend (not Phase 1
  scope) implementing the same RHI interfaces is free to make a different
  choice internally, since RHI's public surface never names either
  mechanism.
- The requested/required Vulkan API version is raised and the
  `dynamicRendering` feature is explicitly enabled at device creation, per
  the two concrete implementation requirements this ADR's Context section
  fixes above — this ADR decides *that* dynamic rendering is adopted;
  whether via a raised core version or an explicitly-required
  `VK_KHR_dynamic_rendering` extension on an older core version is a
  Plan-stage detail (see Context).

## Consequences

### Positive

- No `VkFramebuffer` object needs to be recreated on every swapchain
  resize/recreation, because none exists — this removes an entire class of
  "framebuffer object out of sync with a just-recreated swapchain image
  view" bug that a render-pass/framebuffer design would otherwise have to
  guard against explicitly (relevant given this codebase's resize-driven
  swapchain recreation, [ADR-0016](0016-presentation-acquire-present-and-recreation-contract.md)).
- No `VkRenderPass` compatibility/subpass-dependency bookkeeping is needed
  for this spec's single-pass, single-subpass-equivalent scope — matches
  the "minimal" bar this spec is held to.
- Keeps attachment scoping information co-located with the draw call that
  uses it (`vkCmdBeginRendering`'s own parameters), rather than split
  across a separately-constructed, separately-cached `VkRenderPass`/
  `VkFramebuffer` pair — simpler to reason about and to test for a
  single-pass consumer.
- Directly compatible with this codebase's existing model of "no
  persistent framebuffer wrapping" already implicit in how
  `VulkanPresentation` hands out swapchain images — no new object type
  needs inventing to bridge that gap.

### Negative / Trade-offs

- Requires Vulkan 1.3 (or `VK_KHR_dynamic_rendering` on an older core
  version) as a baseline — a real, if currently inconsequential, floor on
  supported hardware/drivers. Not a concern for the verified Windows/
  Vulkan 1.4 development environment this repository already targets, but
  a real constraint any future minimum-spec decision must account for.
- If a future spec needs genuine multi-subpass techniques with
  input-attachment dependencies (e.g. deferred shading's G-buffer read-
  back within the same render pass, tile-based mobile bandwidth
  optimizations), dynamic rendering does not provide subpass input
  attachments the way a classic render pass does — that future spec would
  need to either use multiple dynamic-rendering scopes with explicit
  barriers between them, or revisit this decision. Not a concern for this
  spec's single draw pass.
- Locks in a Vulkan-Backend-internal architecture choice that, while not
  crossing RHI's public surface, would still require real rework
  (reintroducing render-pass/framebuffer machinery) if a future need
  invalidates it — accepted as the standard cost of any Phase 1 decision
  under AGENTS.md's "no speculative abstraction" principle: decide for the
  need that exists now, revisit explicitly if a real future need appears.

## Alternatives Considered

- **Classic `VkRenderPass` + `VkFramebuffer` objects.** Rejected for this
  round: requires framebuffer-object recreation tied to every swapchain
  recreation (a real, avoidable synchronization/lifetime surface this
  spec's single-pass scope has no need to carry), and render-pass
  compatibility rules add real complexity with no payoff at this spec's
  single-subpass-equivalent scope. Remains a legitimate choice a future
  spec could still make if a genuine subpass-dependency need appears (see
  Negative/Trade-offs) — this ADR does not claim dynamic rendering is
  strictly superior forever, only that it is the better fit for this
  spec's actual, current scope.
- **Defer this decision entirely, leave attachment-scoping mechanism
  unspecified until a future spec needs to choose.** Rejected: Spec 0007
  cannot draw a single mesh without *some* attachment-scoping mechanism
  existing in the Vulkan Backend today; deferring would mean this spec's
  own implementation silently picks one, which is exactly the "settled
  implicitly by whichever code is written first" failure mode
  [AGENTS.md](../AGENTS.md)'s Golden Rule exists to prevent — the
  repository's own instruction for this spec explicitly requires this be
  a reviewed decision, not a silent one.
- **Expose "begin/end rendering" (or the render-pass/framebuffer
  equivalent) as a named concept in RHI's own public interface**, rather
  than keeping it entirely Vulkan-Backend-private behind a generic
  attachment-scoping call. Rejected: RHI's existing backend-independence
  rule ([ADR-0001](0001-rhi-backend-independence.md)) already requires no
  Vulkan-specific concept to leak into RHI's public surface; the
  backend-agnostic surface
  [ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md)
  defines is sufficient and keeps this specific mechanism choice fully
  swappable without touching RHI or RenderGraph.
