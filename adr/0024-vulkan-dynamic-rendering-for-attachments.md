# ADR 0024: Vulkan Dynamic Rendering for Minimal Renderer Attachment Management

- **Status:** Accepted
- **Date:** 2026-08-11
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-11; see
  [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)'s
  Human Review Approval note for the full approval record this ADR's
  Decision is part of, including the explicit confirmation that dynamic
  rendering is adopted via a capability-detected dual path (Vulkan 1.3
  core, or `VK_KHR_dynamic_rendering` below 1.3) rather than by raising
  the Vulkan Backend's overall minimum supported API version.
- **Related Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md) (`Approved`)

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

## Proposed Amendment (Under Review) — 2026-08-13

**Status of this section:** drafted as a documentation-only architectural
review, per a post-merge verification finding and a subsequent, now-
abandoned code-fix attempt (branch `fix/0007-dynamic-rendering-core-path`,
reverted, no commits landed). **This section does not itself change this
ADR's Status** (`Accepted` above is unchanged, and remains the record of
what actually shipped in PR #28 for the Extension path and the dual-path
structure in general). This section is a proposed amendment to one
sub-decision — the Core path's instance-apiVersion prerequisite — awaiting
its own Human Review before any further implementation resumes. The
original Decision, Context, Consequences, and Alternatives Considered
above are preserved verbatim and are not superseded except where this
section says so explicitly.

### 1. What was found

Post-merge review of PR #28 found that the shipped Core path does not
match the Decision recorded above: `vulkan_device.cpp`'s `createDevice()`
unconditionally adds `VK_KHR_dynamic_rendering` (and its full promoted-
extension dependency chain — `VK_KHR_multiview`, `VK_KHR_maintenance2`,
`VK_KHR_create_renderpass2`, `VK_KHR_depth_stencil_resolve`) to the
enabled device-extension list **on both paths**, including a Vulkan 1.3+
Core-path device, and resolves only the `KHR`-suffixed entry points
(`vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR`) regardless of which
path `decideDynamicRenderingPath()` actually selected. The as-shipped
code's own comment (`vulkan_device.cpp`, the block beginning
"Implementation-forced deviation from Plan 0007 Section 8's stated 'Core
needs no device extension'") states this plainly: it was a real, called-
out deviation from Plan 0007 §8's approved design, made because the
unsuffixed core entry points would not reliably resolve. This is a
correctness defect against the Decision above: a Vulkan 1.3+
implementation is not guaranteed to keep advertising a promoted-to-core
extension name indefinitely, so a Core-path device that has genuinely
dropped `VK_KHR_dynamic_rendering` would fail `vkCreateDevice()`
(extension not present) even though it fully qualifies for the Core path
this ADR's Decision says it should use.

A subsequent fix attempt implemented the textbook-correct behavior — Core
path never requests, enables, or resolves through the KHR extension —
and confirmed via controlled experiments on this repository's real
development hardware (Intel Arc B370, driver reporting Vulkan 1.4.335),
**with the instance's requested `VkApplicationInfo::apiVersion` left at
`VK_API_VERSION_1_0`** (unchanged from the shipped, Human-Review-approved
baseline):

- `vkGetDeviceProcAddr(device, "vkCmdBeginRendering")` returns `nullptr`.
- `vkGetInstanceProcAddr(instance, "vkCmdBeginRendering")` returns a
  non-null pointer, but **calling it crashes** (access violation) —
  reproducible, independent of whether `VK_KHR_dynamic_rendering` is
  enabled at the device.
- The same code path, resolving `vkCmdBeginRenderingKHR`/
  `vkCmdEndRenderingKHR` instead (still via `vkGetInstanceProcAddr`, same
  1.0 instance), resolves and runs correctly.
- 179/179 GPU-independent unit tests passed; the real-GPU integration test
  crashed specifically at the core, unsuffixed entry-point call.

That fix attempt was reverted (no commits landed) because it reintroduced
exactly the design tension ADR-0024's original Decision and Plan 0007 §8
were written to avoid — a genuinely correct Core path appeared, on this
evidence, to require raising the instance's requested `apiVersion`, which
this ADR's Decision and Plan 0007 §8 previously rejected. This section
resolves that tension with a verified, spec-grounded amendment rather than
a second unreviewed attempt.

### 2. Vulkan specification findings

Verified against the official Vulkan specification (`docs.vulkan.org`,
mirroring `registry.khronos.org`'s content) and the Vulkan-Loader
project's own architecture documentation. Citations inline.

**(a) Three distinct version concepts — precise scope of each.**

1. **The loader's own maximum supported instance version**, queried via
   `vkEnumerateInstanceVersion` (global command, callable without an
   instance). A loader that predates Vulkan 1.1 has no such function —
   "[i]f the `vkGetInstanceProcAddr` returns `NULL` for
   `vkEnumerateInstanceVersion`, it is a Vulkan 1.0 implementation"
   ([Initialization chapter](https://docs.vulkan.org/spec/latest/chapters/initialization.html),
   confirmed against the
   [Versions & Porting Guide](https://docs.vulkan.org/guide/latest/versions.html)).
   This is a single, queryable, environment-level fact — not a request,
   not something the application controls.
2. **`VkApplicationInfo::apiVersion`**, the value the application requests
   at `vkCreateInstance()` time. Per the spec's own initialization
   chapter, this is the application's *declared target*, and it is what
   gates guaranteed availability of core (unsuffixed) function-pointer
   resolution at both instance and device level — see finding (c) below.
   It does **not** gate which physical devices may be enumerated or used.
3. **Each physical device's own `VkPhysicalDeviceProperties::apiVersion`**,
   queried per-candidate via `vkGetPhysicalDeviceProperties()`, entirely
   independent of what the instance itself requested.

**(b) Instance apiVersion vs. physical device apiVersion — spec-legal
mismatch, confirmed.** The Versions & Porting Guide states explicitly:
"As long as the instance supports at least Vulkan 1.1, an application can
use different versions of Vulkan with an instance than it does with a
device or physical device." The Initialization chapter's own phrasing of
the same rule: new core physical-device-level functionality requires
"both `VkPhysicalDeviceProperties::apiVersion` and
`VkApplicationInfo::apiVersion`" to be at or above the version that
introduced it — a per-feature AND condition on the two independent
values, not a requirement that the physical device's version match or
exceed the instance's requested version for *unrelated* functionality.
**Concretely: an instance requesting `apiVersion = VK_API_VERSION_1_3`
can still enumerate and select a physical device whose own
`VkPhysicalDeviceProperties::apiVersion` is, say, 1.1 — that selection is
spec-legal and unproblematic. It simply cannot use 1.3-gated core
functionality (including core dynamic rendering) against that device**;
the existing, unmodified Extension-path branch of
`decideDynamicRenderingPath()` (device `apiVersion < 1.3` +
`VK_KHR_dynamic_rendering` advertised) remains exactly the right, and
still fully available, answer for such a device. This confirms the
premise Section 5's compatibility analysis below depends on.

**(c) `vkCreateInstance` and `VK_ERROR_INCOMPATIBLE_DRIVER` — confirmed,
and confirmed fixable by a version query first.** Per the Initialization
chapter: "Vulkan 1.0 implementations were required to return
`VK_ERROR_INCOMPATIBLE_DRIVER` if `apiVersion` was larger than 1.0,"
whereas "[i]mplementations that support Vulkan 1.1 or later must not
return `VK_ERROR_INCOMPATIBLE_DRIVER` for any value of `apiVersion`." The
failure mode Plan 0007 §8 was worried about is therefore real but
**narrowly scoped and fully avoidable**: it can only occur against a
genuinely Vulkan-1.0-only loader (one where
`vkGetInstanceProcAddr(NULL, "vkEnumerateInstanceVersion")` itself
returns `NULL`), and the spec's own recommended detection method — check
for that `NULL` before ever calling `vkEnumerateInstanceVersion` or
requesting a raised `apiVersion` — eliminates the risk deterministically,
because a request that stays at `VK_API_VERSION_1_0` for such a loader is
exactly today's shipped behavior. The failure, when it can occur at all,
belongs to the **loader/ICD acting as the target of `vkCreateInstance`**,
not the application; querying first, as Plan 0007 §8 already proposed
doing for a different reason (`VK_KHR_get_physical_device_properties2`
availability), is confirmed as the correct and sufficient mitigation.

**(d) Core-vs-KHR entry-point resolution — the crux finding, and why the
observed crash is expected-per-spec, not a fix-attempt bug.** The
Initialization chapter's Command Function Pointers section states:
"Device-level commands which are part of the core version specified by
`VkApplicationInfo::apiVersion` when creating the instance will always
return a valid function pointer." For core commands **beyond** the
requested `apiVersion` (unsuffixed `vkCmdBeginRendering` is a Vulkan-1.3
core command; this repository's instance requests 1.0), the same section
states the implementation "may either return `NULL` or a function
pointer" (absent the unrelated `maintenance5` feature) — i.e. a **non-null
but not-necessarily-functional** pointer is explicitly spec-permitted
behavior for a core command outside the requested version. This exactly
reproduces the fix attempt's observation:
`vkGetInstanceProcAddr(instance, "vkCmdBeginRendering")` returning
non-null yet crashing on call is not loader misbehavior or a bug in the
fix attempt's own resolution code — it is the specification's documented
"may return NULL or [an unreliable] function pointer" outcome for a core
command requested outside the instance's declared `apiVersion`.
`vkGetDeviceProcAddr` returning `nullptr` outright for the same name is
the same rule's more conservative (and, per this finding, arguably more
correct) manifestation at the device level. **The controlling factor is
the instance's own requested `apiVersion` — not the physical device's own
reported `apiVersion`, and not which device extensions are enabled.** A
device reporting 1.3 does not, by itself, make `vkCmdBeginRendering`
reliably resolvable if the instance that created it only ever requested
1.0. This is the ADR-level correction to Plan 0007 §8's own (reasonable,
at the time) assumption that the instance-level prerequisite for dynamic
rendering was purely `VK_KHR_get_physical_device_properties2` querying —
it is that, plus, as this finding establishes, the instance's requested
`apiVersion` for the Core path specifically.

**(e) Android Vulkan version landscape.** Android's own Compatibility
Definition Document requires Vulkan 1.1 drivers on Vulkan-capable
handheld devices as of Android 14/15 (2024/2025); no Android CDD
requirement to support Vulkan 1.3 was found. This means a realistic
near-term Android device population includes devices whose loader/driver
report exactly 1.1 or 1.2 — genuinely below 1.3, and therefore only
reachable via the Extension path, never the Core path, under any
apiVersion-request strategy. This is a real, current-generation
constraint, not a hypothetical one, and directly shapes Section 5 below.

### 3. Amended Decision

**The Vulkan Backend's instance creation queries the loader's own
maximum supported version before `vkCreateInstance()`, and requests
`VkApplicationInfo::apiVersion = VK_API_VERSION_1_3` if and only if the
loader supports at least that version; otherwise it requests
`VK_API_VERSION_1_0`, exactly as today.** This is a **variant** of the
candidate strategy this review was asked to evaluate (see Section 5,
Alternative (a) for the literal candidate and why one of its clauses is
rejected below) — concretely:

1. **Before `vkCreateInstance()`**, in `vulkan_instance.cpp`'s existing
   instance-creation function (already extended once, for Plan 0007 §8's
   `VK_KHR_get_physical_device_properties2` query): resolve
   `vkGetInstanceProcAddr(nullptr, "vkEnumerateInstanceVersion")`. If it
   is `nullptr`, the loader is a Vulkan-1.0-only implementation — request
   `apiVersion = VK_API_VERSION_1_0`, unchanged from today. Otherwise,
   call it to get `loaderVersion`, and request
   `apiVersion = VK_API_VERSION_1_3` if `loaderVersion >= VK_API_VERSION_1_3`,
   else `VK_API_VERSION_1_0`.
2. **This step never fails `createDevice()` by itself.** A loader
   reporting below 1.3 (including a genuine 1.0-only loader) is not an
   error condition — it is exactly today's shipped, working
   configuration, continued unchanged. **This rejects the literal
   candidate strategy's "return a recoverable error when loader version
   is insufficient" clause** — see Section 5's discussion of why an
   unconditional error there would silently regress the Extension-path
   compatibility this very ADR exists to protect. No new `DeviceCreateError`
   variant, and no new earlier-than-`DeviceCreateError` error stage, is
   introduced for "loader version insufficient" — it resolves the design
   question the review brief posed: this is not an error case at all
   under the amended strategy, so no enum extension is needed for it.
   `DeviceCreateError::InstanceCreationFailed` remains the correct,
   unchanged outcome for a genuine `vkCreateInstance()` failure (distinct
   from — and not conflated with — a loader simply reporting a version
   below 1.3, which is not a failure).
3. **The instance's requested `apiVersion` never excludes a physical
   device whose own `apiVersion` is lower** — confirmed spec-legal by
   finding (b) above. The existing, unmodified physical-device selection
   loop (`selectPhysicalDevice()`, `vulkan_device.cpp`) is otherwise
   untouched: it still evaluates every candidate's own `apiVersion`,
   swapchain support, and queue-family support exactly as today.
4. **`decideDynamicRenderingPath()` gains a new, sixth boolean argument:
   `instanceRequestedApiVersionAtLeast1_3`** — a single, instance-wide
   fact (mirroring `physicalDeviceProperties2InstanceExtensionAvailable`'s
   own existing "computed once, passed unchanged to every candidate"
   shape), computed once from step 1 above. The **Core** branch's
   condition becomes `apiVersionAtLeast1_3 && coreFeatureSupported &&
   instanceRequestedApiVersionAtLeast1_3` — finding (d) above is exactly
   why the third clause is required: without it, a physical device
   reporting 1.3+ against an instance that only requested 1.0 would be
   (mis)classified as Core-capable and reproduce the crash. The
   **Extension** branch is unchanged (`extensionAdvertised &&
   extensionFeatureSupported`, no dependency on instance `apiVersion`).
   **Consequence, stated explicitly because it is a real, structural
   trade-off, not a detail:** for the whole lifetime of one `VkInstance`,
   the Core path is available to *no* physical device at all whenever the
   loader itself reported below 1.3 at startup — even if some enumerated
   physical device individually reports 1.3+. Such a device still
   succeeds, via the Extension path, provided it advertises
   `VK_KHR_dynamic_rendering` (which every real 1.3+ implementation is
   expected to, per finding (a) below and Consequences/Negative below).
5. **Core path (amended): never requests, enables, or resolves through
   `VK_KHR_dynamic_rendering` or its promoted-extension dependency chain.**
   Device-extension list and `VkDeviceCreateInfo::pNext` chain for a
   Core-selected physical device contain only
   `VK_KHR_swapchain` plus whatever this repository's existing,
   unrelated device-extension needs already require — never
   `VK_KHR_dynamic_rendering`/`VK_KHR_multiview`/`VK_KHR_maintenance2`/
   `VK_KHR_create_renderpass2`/`VK_KHR_depth_stencil_resolve`. Entry
   points are resolved via `vkGetDeviceProcAddr(device,
   "vkCmdBeginRendering")` / `"vkCmdEndRendering"` (unsuffixed) — per
   finding (d), reliable specifically because the owning instance now
   requested `apiVersion >= 1.3`. If either resolves to `nullptr`
   (an unexpected, but checked, outcome — not assumed impossible),
   `createDevice()` returns `Result::Err(DeviceCreateError::DynamicRenderingUnavailable)`,
   the same existing, recoverable-error tier as every other
   dynamic-rendering-unavailable outcome — **never falls back to the
   Extension path or to a `VkRenderPass`/`VkFramebuffer` mechanism.**
6. **Extension path: unchanged in design from the original Decision
   above and from the as-shipped (defective only on the Core side) code**
   — gated to a physical device whose own `apiVersion < 1.3` (or whose
   `apiVersion >= 1.3` but whose owning instance requested < 1.3, per
   point 4's amendment) that fully advertises
   `VK_KHR_dynamic_rendering`, its feature, and its promoted-extension
   dependency chain; `KHR`-suffixed entry points resolved via
   `vkGetDeviceProcAddr`; never assumes core availability.
7. **A device with neither path available continues to return
   `Result::Err(DeviceCreateError::DynamicRenderingUnavailable)`** —
   unchanged from the original Decision.

### 4. Compatibility impact

- **This machine (Windows, Intel Arc B370, loader/driver reporting
  Vulkan 1.4.335):** loader version query returns ≥ 1.3, so the instance
  now requests `apiVersion = VK_API_VERSION_1_3`. Per finding (b), this
  does not affect which physical devices are enumerable, and per finding
  (c), `vkCreateInstance` is guaranteed not to fail with
  `VK_ERROR_INCOMPATIBLE_DRIVER` on any 1.1+ loader regardless of the
  requested version — this machine's loader is far newer than 1.1.
  Expected outcome: Core path now genuinely works (the fix attempt's own
  evidence, once combined with this instance-level change) with no
  `VK_KHR_dynamic_rendering` requested at the device.
- **A hypothetical older Windows machine with a genuine Vulkan-1.0-only
  loader:** the loader-version query (step 1) detects this before ever
  requesting 1.3, and the instance continues to request
  `VK_API_VERSION_1_0` — byte-for-byte the same request this repository
  has shipped since Spec 0003. No behavior change, no regression, no new
  `VK_ERROR_INCOMPATIBLE_DRIVER` exposure for this machine class — this
  is the direct payoff of finding (c)'s "query first" mitigation.
- **A hypothetical Windows/Android machine with a loader reporting 1.1 or
  1.2** (a realistic, current population per finding (e)'s Android CDD
  research): the instance requests `VK_API_VERSION_1_0` (loader < 1.3),
  Core path is structurally unavailable for the whole instance (point 4
  above), but the Extension path is entirely unaffected and remains
  available to any physical device on that instance that advertises
  `VK_KHR_dynamic_rendering` — **exactly the device population ADR-0024's
  original Decision was written to keep serving.** No narrowing of
  AGENTS.md's Android target-platform commitment occurs under this
  amendment for that device population; this amendment does not raise
  any *device-level* apiVersion floor, only the *instance's* requested
  value, and only when the loader itself already supports it.
- **Loader-version-vs-physical-device-version mismatch** (loader reports
  1.3+, but the actual physical device selected reports lower): remains
  handled correctly and unchanged by the existing per-device
  `decideDynamicRenderingPath()` logic (now with the added
  `instanceRequestedApiVersionAtLeast1_3` input) — such a device is never
  misclassified as Core-capable, and falls through to Extension or
  Unavailable exactly as intended.
- **The literal candidate strategy's "hard error when loader < 1.3" clause
  is rejected**, not adopted: applied literally, it would make `Device`
  construction fail outright on every loader below 1.3 — including every
  Extension-path-capable device on such a loader, which today's shipped
  code (and the unmodified Extension-path branch of this very ADR)
  successfully serves. That would be a real, silent narrowing of the
  Windows/Android device compatibility ADR-0024's original Decision and
  Human Review explicitly protected, and is flagged in the accompanying
  report as a **Human-Review-Blocker-tier item**: confirm this rejection,
  or state a reason the human reviewer sees for wanting it that this
  review did not surface.
- **No narrowing of AGENTS.md's Android target-platform commitment is
  proposed by this amendment.** This is stated explicitly per the review
  brief's own instruction not to silently narrow it — the amendment's
  net effect on any device below loader-version 1.3 (the realistic
  near-term Android population, per finding (e)) is precisely zero:
  same instance apiVersion request, same Extension-path eligibility, same
  `DynamicRenderingUnavailable` fallback behavior as today.

### 5. Alternatives considered (this amendment's own scope)

- **(a) Literal candidate: loader-version-gated single instance,
  including a hard error when loader < 1.3.** Partially adopted — the
  loader-version-gated single-instance mechanism is adopted (Section 3);
  the hard-error sub-clause is rejected, per the Compatibility impact
  section above, as an unreviewed narrowing of exactly the compatibility
  guarantee this ADR exists to provide. Flagged as a Human-Review-Blocker
  item precisely because the review brief asked for this candidate to be
  evaluated as stated, not silently modified without flagging the
  disagreement.
- **(b) Hard split: two separate instance-creation code paths/configs**
  (a "Core-only" build requesting 1.3 outright vs. an "extension-
  compatible" build requesting 1.0/1.1). **Rejected.** This would require
  Atlantis to decide, at build or launch-configuration time, which device
  population a given binary serves — directly contradicting
  `decideDynamicRenderingPath()`'s entire reason to exist (a single
  binary that adapts to whatever device it actually finds at runtime,
  per ADR-0024's original Decision and Human Review). It would also
  reintroduce a real regression risk this amendment's runtime-gated
  design avoids entirely: a "Core-only" build launched on a sub-1.3
  loader would need its own separate error/fallback story that the
  runtime-gated single-instance strategy never needs, since it degrades
  automatically and correctly by construction.
- **(c) Abandon an independent Core path; always require and use
  `VK_KHR_dynamic_rendering`, even on 1.3+ devices** — i.e., keep (a
  cleaned-up version of) the as-shipped, defective behavior permanently
  rather than fixing it. **Not recommended, but listed fairly per the
  review brief's instruction.** Trade-offs: strictly simpler (one
  code path, one entry-point family, no instance-apiVersion amendment
  needed at all) and would have avoided this entire review. Against it:
  this is precisely the defect this review exists to fix — `VK_KHR_dynamic_rendering`
  is a promoted-to-core extension, and the Vulkan specification places no
  obligation on an implementation to keep advertising a promoted
  extension name indefinitely; a future 1.3+-only implementation that
  drops the name would fail `vkCreateDevice()` under this alternative for
  a capability it genuinely has. Choosing this alternative would mean
  reverting Human Review's own confirmed instruction (Spec 0007's Human
  Review Confirmations, point 1: "the `dynamicRendering` feature at
  device creation" is explicitly path-specific, not a blanket
  extension-always policy) without a new Human Review round to authorize
  that reversal — this review does not do that unilaterally.
- **Recommendation: Section 3's amended Decision (candidate (a)'s
  mechanism, without its hard-error clause).** It is the only option of
  the three that (i) fixes the confirmed Core-path defect using the
  fix attempt's own now-verified-correct approach, (ii) is affirmatively
  supported by the spec findings in Section 2 rather than asserted from
  one crash's evidence alone, and (iii) provably preserves every device
  population ADR-0024's original Decision and Spec 0007's Human Review
  already committed to serving.

### 6. Verification plan (for the eventual code-fix implementation)

Once this amendment is approved, the resuming implementation must satisfy
all of the following before it is considered complete — extending, not
replacing, Spec 0007's own existing Testing & Verification Plan:

- A Core-path device that does **not** advertise `VK_KHR_dynamic_rendering`
  still successfully constructs a `Device` (the direct regression test
  for the original defect).
- The Core path's enabled device-extension list contains no entry from
  `VK_KHR_dynamic_rendering`'s chain
  (`VK_KHR_dynamic_rendering`/`VK_KHR_multiview`/`VK_KHR_maintenance2`/
  `VK_KHR_create_renderpass2`/`VK_KHR_depth_stencil_resolve`) —
  verifiable by inspection/log of the actual `VkDeviceCreateInfo` built
  for a Core-path candidate.
- The Core path resolves only the unsuffixed entry points
  (`vkCmdBeginRendering`/`vkCmdEndRendering`) via `vkGetDeviceProcAddr`;
  never falls back to the `KHR`-suffixed names.
- The Extension path resolves only the `KHR`-suffixed entry points
  (`vkCmdBeginRenderingKHR`/`vkCmdEndRenderingKHR`); never assumes core
  availability.
- `decideDynamicRenderingPath()`'s expanded truth table (now six
  boolean inputs, per Section 3 point 4) is exhaustively unit-tested,
  GPU-independent, including the specific new case this amendment adds:
  device `apiVersion >= 1.3` and `coreFeatureSupported == true` but
  `instanceRequestedApiVersionAtLeast1_3 == false` → `Extension` (if
  eligible) or `Unavailable`, never `Core`.
- A deterministic, non-crashing outcome (an explicit `Result::Err`, never
  a partially-constructed `Device` or an unresolved entry point silently
  left `nullptr`-called) whenever the Core-path entry points fail to
  resolve despite the path having been selected — this is checked, not
  assumed unreachable, per this codebase's existing "every failure path
  is checked" discipline.
- A physical device below 1.3 with full `VK_KHR_dynamic_rendering`
  capability still correctly selects and uses the Extension path,
  regardless of what the owning instance's own requested `apiVersion`
  ended up being.
- **Real hardware verification on this environment's Intel Arc Core
  path**, confirming the fix attempt's own crash no longer reproduces
  once the instance requests `apiVersion 1.3` on this loader.
- **Explicit acknowledgment that the Extension path remains unverifiable
  on real hardware in this environment** — this machine's loader/driver
  is 1.4.335 and qualifies for the Core path; the Extension path's real-
  device behavior continues to rely on pure-logic unit tests and code
  review only, the same limitation Spec 0007's own Testing &
  Verification Plan already states for the pre-amendment design — not
  newly introduced or newly resolved by this amendment.
- Debug and Release builds, the existing GPU test suite, the
  `minimal_renderer_demo` example, and Vulkan Validation Layers
  (zero warnings/errors) are all re-verified once implementation resumes
  — the amendment changes instance/device construction, which every one
  of these exercises.

### 7. Open item carried into the accompanying report, not decided here

Whether the literal candidate strategy's hard-error-on-loader<1.3 clause
was actually intended by the human requesting this review, or whether
Section 3/5's rejection of it (in favor of the zero-net-effect-on-older-
loaders variant) correctly reads the human's actual intent, is flagged
explicitly as a **Human-Review-Blocker-tier item** requiring an explicit
human decision before implementation resumes — this ADR amendment does
not resolve that question by itself; see the accompanying report.
