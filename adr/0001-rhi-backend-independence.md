# ADR 0001: RHI Backend Independence (Renderer/RenderGraph Must Not Depend on Vulkan or Windowing Libraries)

- **Status:** Proposed
- **Date:** 2026-08-02
- **Deciders:** _pending human review_
- **Related Spec:** _none yet — drafted as part of the architecture-baseline
  documentation task; a formal spec should precede `Accepted` status_

## Context

Atlantis's Phase 1 scope is Vulkan-only (per [AGENTS.md](../AGENTS.md)),
but the same document requires the RHI to "stay backend-independent in
interface" even though no second backend is built in Phase 1. Separately,
the task establishing this baseline explicitly requires: "The Renderer
must NOT directly depend on GLFW/SDL or `VkSwapchainKHR`."

Without an explicit boundary, it is easy for a Vulkan type (a `VkImage`,
a `VkCommandBuffer`, a `VkSwapchainKHR`) or a windowing-library call to
leak into Renderer or RenderGraph code "just this once," because Vulkan
Backend is the only backend that exists and the shortcut costs nothing
locally. Each such leak is individually reasonable and collectively
destroys the boundary.

## Decision

- Atlantis RHI defines all backend-facing concepts (`Device`,
  `CommandList`, resources, pipelines, `RenderTarget`, `Presentation`,
  sync primitives) as backend-agnostic interfaces/opaque handles, with no
  `Vk*` type and no windowing-library type in any RHI public header.
- Atlantis Vulkan Backend is the only module permitted to include Vulkan
  headers or reference `Vk*` types.
- Atlantis Renderer and Atlantis RenderGraph depend only on RHI and Core.
  They must not reference Vulkan Backend, any `Vk*` type, or any
  windowing/platform library directly, under any circumstance, including
  "temporary" or "just for now" code.
- Atlantis Runtime does not depend on a windowing library directly — it
  owns an Atlantis Platform instance for windowing/surface production
  instead. It does not pass Platform, windowing-library, or OS-specific
  types into RHI, Renderer, or RenderGraph.

> **Amended 2026-08-02** (see
> [ADR-0005](0005-platform-module-multi-os-windowing.md)): the forbidden-
> dependency list for Renderer/RenderGraph is extended to explicitly
> include **Win32, the Android NDK, and `VkSurfaceKHR`**, alongside the
> already-forbidden GLFW/SDL and `VkSwapchainKHR` — reflecting Atlantis's
> Windows/Android (primary) + iOS (future) target-platform decision.
> Windowing responsibility, previously described here as Runtime owning a
> `Window` via GLFW/SDL, is now owned by the Atlantis Platform module
> (Runtime owns a Platform instance instead) — see ADR-0005 for the full
> module-boundary decision this amendment defers to.

## Consequences

### Positive

- Renderer/RenderGraph code is testable and reasoned-about independent of
  Vulkan specifics.
- Headless rendering (Phase 1, after windowed) reuses Renderer/RenderGraph
  unchanged, because they were never coupled to swapchain/window concepts
  in the first place.
- A future second backend (not Phase 1 scope) would not require touching
  Renderer/RenderGraph at all, only a new Vulkan-Backend-equivalent
  module.

### Negative / Trade-offs

- Every RHI-level concept needs a backend-agnostic interface designed
  before Vulkan Backend can implement it, which is more upfront design
  work than calling Vulkan directly from Renderer.
- Some indirection cost (virtual dispatch or equivalent) is likely at the
  RHI boundary; the exact mechanism (vtable interface vs. compile-time
  backend selection) is not decided by this ADR and is left to the RHI
  spec.

## Alternatives Considered

- **Let Renderer call Vulkan directly, since Vulkan is the only backend
  in Phase 1.** Rejected: this is exactly the shortcut this ADR exists to
  block, per the task's explicit requirement and AGENTS.md's backend-
  independence-in-interface constraint; it would also block headless
  reuse without a later rewrite.
- **Give Renderer a compile-time backend template parameter instead of a
  runtime interface.** Not rejected outright — this is a legitimate
  alternative mechanism for achieving the same independence and should be
  weighed in the RHI spec; this ADR fixes the *boundary*, not the
  mechanism.
