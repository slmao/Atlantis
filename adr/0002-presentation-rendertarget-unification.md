# ADR 0002: Presentation/RenderTarget Unification for Windowed and Headless Paths

- **Status:** Proposed
- **Date:** 2026-08-02
- **Deciders:** _pending human review_
- **Related Spec:** _none yet — drafted as part of the architecture-baseline
  documentation task; a formal spec should precede `Accepted` status_

## Context

Atlantis needs two rendering entry points over its lifetime: windowed
(ships first) and headless (ships after, and is what unlocks image
regression testing — see
[docs/process/testing-strategy.md](../docs/process/testing-strategy.md)).
[AGENTS.md](../AGENTS.md) requires both to "share the same Renderer/RHI
infrastructure" rather than forking into two rendering stacks. Without an
explicit shared abstraction, the natural failure mode is that the
windowed path gets built first with swapchain concepts baked into
Renderer, and headless then requires either a rewrite or an awkward
"fake swapchain" shim.

## Decision

Introduce `RenderTarget` as an RHI-level abstraction representing a
drawable surface (color/depth attachments), with two ways to produce one:

- **Windowed:** `Window` (Runtime-owned) → `Presentation` (RHI interface,
  Vulkan-Backend-implemented, `VkSwapchainKHR`-backed) → `RenderTarget`
  vended per frame.
- **Headless:** an explicitly-requested offscreen target → `RenderTarget`
  backed by an offscreen image, no `Presentation` involved.

Renderer and RenderGraph consume only `RenderTarget` and cannot observe
which path produced it — no member, flag, or capability query on
`RenderTarget` exposes "am I a swapchain image or an offscreen image."
`Presentation` exists solely to manage swapchain-specific concerns
(acquire/present, resize/recreation, present modes) and is never seen by
Renderer.

> **Amended 2026-08-02** (see
> [ADR-0005](0005-platform-module-multi-os-windowing.md)): "windowed" now
> spans multiple operating systems — Windows and Android (primary), iOS
> (future) — each with its own Atlantis Platform implementation producing
> the native surface handle that feeds `Presentation`. This decision's
> core claim is unchanged and now carries more weight: `RenderTarget`
> unifies not just windowed-vs-headless but windowed-across-every-OS too.
> `Window` in the paragraph above should be read as "whatever Atlantis
> Platform's concrete implementation produces," not specifically a
> GLFW/SDL-style desktop window.

## Consequences

### Positive

- Headless rendering, when it lands, is additive (a new way to produce a
  `RenderTarget`) rather than a fork or rewrite of Renderer/RenderGraph.
- Image regression testing (which needs headless rendering) is not
  blocked by Renderer-level rework once headless lands.
- Swapchain-specific complexity (resize, out-of-date/suboptimal handling,
  present modes) stays isolated in `Presentation`/Vulkan Backend and never
  leaks into rendering logic.

### Negative / Trade-offs

- `RenderTarget` must be designed generally enough to serve both origins
  from day one (during windowed-only Phase 1 work), even though headless
  isn't built yet — this is deliberate shared-infrastructure design, not
  speculative abstraction, but it does mean the windowed-first spec must
  still get this interface right before headless exists to validate it
  against.
- If `RenderTarget`'s design turns out not to generalize once headless is
  actually specced, that's a deviation requiring a follow-up ADR, not a
  silent fix.

## Alternatives Considered

- **Build windowed rendering directly against `Presentation`/swapchain
  concepts, design headless's abstraction later.** Rejected: this is the
  failure mode AGENTS.md's "share the same Renderer/RHI infrastructure"
  requirement exists to prevent, and is likely to force a rewrite rather
  than an addition when headless is specced.
- **Two separate Renderer implementations (windowed, headless) sharing
  only RenderGraph.** Rejected: contradicts AGENTS.md's explicit
  requirement that both paths share Renderer, not just RenderGraph.
