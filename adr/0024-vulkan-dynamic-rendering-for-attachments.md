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
for dynamic rendering is confirmed present on that machine. But this
repository's Vulkan Backend must also remain correct on other Windows
machines (and, per
[module_boundaries.md](../docs/architecture/module_boundaries.md)'s
already-anticipated future Android target) whose installed driver may
report a lower Vulkan API version — a single "just raise the minimum
supported version to 1.3" choice would silently exclude any such device
from ever constructing a `Device` at all, for a capability
(`VK_KHR_dynamic_rendering` promoted to core) that is in practice also
available, via extension, well below API version 1.3. **This repository's
current, already-shipped Vulkan Backend implementation neither requests
nor requires 1.3**: `vulkan_instance.cpp` sets
`applicationInfo.apiVersion = VK_API_VERSION_1_0`, and
`vulkan_device.cpp`'s physical-device selection only requires
`properties.apiVersion >= VK_API_VERSION_1_0` — this ADR's Decision below
is written against that as-shipped baseline, not against a hypothetical
already-raised one. **Human Review (2026-08-11) confirmed this repository
should not simply raise that floor to 1.3** — instead, dynamic rendering
availability is treated as its own, independently-detected device
capability, queried and enabled explicitly regardless of which of the two
paths (core or extension) a given physical device supports; see Decision.

## Decision

**The Vulkan Backend adopts dynamic rendering
(`vkCmdBeginRendering`/`vkCmdEndRendering`, `VkRenderingInfo`/
`VkRenderingAttachmentInfo`) for all attachment scoping this spec
introduces, via a capability-detected dual path, and does not create,
cache, or otherwise use `VkRenderPass` or `VkFramebuffer` objects
anywhere in its implementation of the new attachment-scoping surface.**
**Human Review confirmed (2026-08-11) that the Vulkan Backend's overall
minimum supported API version is *not* raised to 1.3 as a side effect of
this decision** — see below for the dual path this replaces that
approach with.

**Capability detection and enablement (both entirely Vulkan-Backend-
private — see "Boundary" below):**

- At physical-device selection/`Device` construction time (the existing
  `createDevice()` path, [ADR-0014](0014-rhi-device-presentation-construction-boundary.md)),
  the Vulkan Backend determines, for the selected physical device, which
  of two paths provides dynamic rendering:
  1. **Core path** — the device reports `apiVersion >= VK_API_VERSION_1_3`.
     The Vulkan Backend queries `VkPhysicalDeviceVulkan13Features::dynamicRendering`
     via `vkGetPhysicalDeviceFeatures2`; if the device reports it
     supported, the Vulkan Backend requests it by chaining a
     `VkPhysicalDeviceVulkan13Features` (with `dynamicRendering = VK_TRUE`)
     into `VkDeviceCreateInfo::pNext` at `vkCreateDevice()` time, and
     records that `vkCmdBeginRendering`/`vkCmdEndRendering` (the core,
     unsuffixed entry points) are the ones to call.
  2. **Extension path** — the device reports `apiVersion < VK_API_VERSION_1_3`
     but advertises the `VK_KHR_dynamic_rendering` device extension (and
     whatever prerequisite extensions/core version floor that extension
     itself requires — the exact prerequisite chain, per the Vulkan
     specification current at implementation time, is a Plan-stage detail
     this ADR does not enumerate). The Vulkan Backend queries
     `VkPhysicalDeviceDynamicRenderingFeaturesKHR::dynamicRendering` via
     `vkGetPhysicalDeviceFeatures2`; if supported, the Vulkan Backend adds
     `VK_KHR_dynamic_rendering` to the enabled device extension list,
     chains `VkPhysicalDeviceDynamicRenderingFeaturesKHR` (with
     `dynamicRendering = VK_TRUE`) into `VkDeviceCreateInfo::pNext`, and
     records that `vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR` (the
     extension-suffixed entry points, resolved via
     `vkGetDeviceProcAddr` — core-1.3 devices never need this resolution
     step, since the loader/SDK links the unsuffixed entry points
     directly) are the ones to call.
  3. **Neither path available** (device below the extension's own
     prerequisite floor, or the extension/feature not reported supported)
     — **`createDevice()` returns a genuine, explicit
     `Result::Err`**, via a new `DeviceCreateError` variant this spec adds
     to the Vulkan Backend's own already-existing public construction-API
     error enum ([ADR-0014](0014-rhi-device-presentation-construction-boundary.md);
     exact enumerator name, e.g. `DynamicRenderingUnavailable`, is a
     Plan-stage detail). This is a **recoverable error, not a crash, not a
     silent `VkRenderPass`/`VkFramebuffer` fallback** — no such fallback
     path is implemented anywhere this spec touches (see Alternatives
     Considered); a physical device otherwise suitable in every other
     respect but lacking this one capability is treated the same as any
     other "no suitable device" case Spec 0003's existing `createDevice()`
     contract already handles via `Result`, not specially.
  4. The physical-device-selection code's existing, coarse minimum-
     version floor (`properties.apiVersion >= VK_API_VERSION_1_0`, per
     `vulkan_device.cpp`) is **unchanged by this decision** — a device
     reporting, say, 1.1 or 1.2 is not rejected by that floor; it is
     accepted or rejected by the dedicated dynamic-rendering capability
     check above, kept as its own explicit, separately-diagnosable gate,
     not folded into (or replacing) the general version floor.
- This capability query, the extension-enable decision, the feature-
  struct chaining, and the choice of which entry-point family
  (unsuffixed core vs. `KHR`-suffixed) to call are **entirely the Vulkan
  Backend's own responsibility**, resolved once at `Device` construction
  and used consistently by every later `CommandList` recording call on
  that `Device` — never re-queried per frame, never exposed to or decided
  by any caller.

- The Vulkan Backend's `CommandList` implementation records
  `vkCmdBeginRendering`/`vkCmdBeginRenderingKHR` (whichever this `Device`
  resolved above; targeting the bound color `RenderTarget`'s image view
  and, when present, the bound depth `Texture`'s image view) and the
  matching end call, around the drawable operations RenderGraph scopes to
  one pass (see
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
  are created with `VkPipelineRenderingCreateInfo` (identical struct on
  both paths — `VK_KHR_dynamic_rendering` was designed as a strict subset
  of the core-1.3 feature, so this struct requires no path-dependent
  variation) chained in, specifying color/depth attachment formats
  directly — never through a `VkRenderPass` handle.

**Boundary: no Vulkan capability type, feature-detection result, or
path-selection concept crosses into RHI's or RenderGraph's public
surface, and no second graphics backend is designed or scaffolded by this
decision.** `atlantis::rhi::Device`, `atlantis::rhi::CommandList`, and
every other RHI public interface remain exactly as backend-agnostic as
[ADR-0001](0001-rhi-backend-independence.md) already requires — a caller
has no way to observe, and does not need to know, which of the two paths
a given `Device` resolved to. This dual-path capability detection is also
**not** a step toward a second graphics backend: both paths are Vulkan,
both are implemented inside the same Vulkan Backend module, and neither
introduces an abstraction "for" a future non-Vulkan backend (out of scope
per [AGENTS.md](../AGENTS.md) Phase 1 constraints). It is, however, worth
recording for a future Android Platform/Vulkan Backend spec's benefit:
the same capability-detected dual path is expected to generalize to
Android hardware directly, since it already does not assume any
Windows-specific capability — no separate decision should be needed there
solely for this feature, though that future spec must still make its own
call on Android's actual device/driver support distribution.

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
- The capability-detected dual path means this repository's minimum
  supported Vulkan API version is *not* forced up to 1.3 — a device
  reporting a lower core version, but advertising
  `VK_KHR_dynamic_rendering`, still successfully constructs a `Device`;
  only a device with neither path available fails, and does so with a
  distinct, explicit, recoverable error rather than an opaque generic
  failure or a silent capability degradation.

### Negative / Trade-offs

- Two capability-detection/feature-enablement code paths (core vs.
  extension) inside `createDevice()`, and two possible entry-point
  families (`vkCmdBeginRendering` vs. `vkCmdBeginRenderingKHR`) a
  `CommandList` implementation must resolve and call consistently, is
  more implementation and testing surface than a single "always require
  1.3" gate would be — accepted deliberately, per Human Review, as the
  cost of not excluding otherwise-capable pre-1.3 devices.
- A device with neither path available now fails at `Device` construction
  with an explicit error, rather than falling back to a
  `VkRenderPass`/`VkFramebuffer` implementation — this spec does not
  design or implement that fallback (see Alternatives Considered), so
  such a device simply cannot run this spec's rendering path at all,
  full stop; a future spec could add the classic render-pass/framebuffer
  path as a genuine third option for such devices, but that is new,
  separately-scoped work, not something this decision provides.
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
- **Unconditionally raise the Vulkan Backend's minimum supported API
  version to 1.3**, using only the core path, and reject every device
  below that version outright (an earlier draft of this ADR proposed
  exactly this). **Rejected by Human Review (2026-08-11):** this would
  silently exclude any Windows machine (and any future Android device)
  whose driver reports a lower core version but still genuinely supports
  dynamic rendering via `VK_KHR_dynamic_rendering` — a real, avoidable
  loss of device compatibility for a capability that has been available
  well below Vulkan 1.3 for years. The capability-detected dual path costs
  more implementation surface (see Negative/Trade-offs) but is the
  materially better fit for this repository's actual Windows/Android
  target-platform breadth.
- **Silently fall back to `VkRenderPass`/`VkFramebuffer` when neither
  dynamic-rendering path is available**, rather than returning an
  explicit `Device`-construction error. Rejected: this spec does not
  design or implement the classic render-pass/framebuffer path at all —
  building it only as an unreviewed fallback for an edge case, rather
  than as its own considered decision, would be exactly the kind of
  under-designed, silently-invented mechanism [AGENTS.md](../AGENTS.md)'s
  Golden Rule exists to prevent. An explicit, diagnosable
  `Result::Err` is the honest outcome for a device this spec's
  implementation genuinely cannot serve; a future spec may add a real
  fallback path as its own reviewed decision if a concrete need for one
  appears.
