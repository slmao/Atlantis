# ADR 0005: Atlantis Platform Module for Multi-OS Windowing (Windows, Android, Future iOS)

- **Status:** Proposed
- **Date:** 2026-08-02
- **Deciders:** _pending human review_
- **Related Spec:** _none yet — drafted as part of the architecture-
  baseline documentation task; a formal spec should precede `Accepted`
  status_

## Context

Atlantis's target platforms are Windows and Android (primary, Phase 1)
and iOS (future, not started). Linux is explicitly not a target platform.
Windows and Android have fundamentally different windowing/surface/
lifecycle models: Win32 has a window handle and a message pump; Android
has an `ANativeWindow`/`Surface` whose lifetime is driven by Activity
lifecycle events (pause/resume, surface created/destroyed) independent of
app lifetime, typically surfaced through a native glue layer rather than
a simple message loop. [ADR-0001](0001-rhi-backend-independence.md)
already established that Renderer must not depend on a windowing library
or Vulkan types, but it was written assuming a single desktop-style
windowing model (GLFW/SDL-shaped). With two structurally different OS
windowing models now in scope — and a third (iOS/UIKit) anticipated —
that assumption needs to be made explicit and general, or it will get
decided ad hoc inside whichever module's spec happens to touch a second
platform first.

## Decision

- Introduce **Atlantis Platform** as a first-class module: an interface
  for per-OS windowing/surface/lifecycle, with one concrete implementation
  per OS — **Windows Platform**, **Android Platform**, and (future, not
  implemented) **iOS Platform**.
- Atlantis Platform owns all OS-specific types (Win32, Android NDK,
  future UIKit) and exposes only an opaque native-surface handle plus
  lifecycle events to its caller (Runtime). No other module — RHI, Vulkan
  Backend, RenderGraph, Renderer — depends on Atlantis Platform or
  includes any OS-specific header.
- RHI's `Presentation` interface accepts the opaque native-surface handle
  as a parameter at creation time; it does not depend on Atlantis
  Platform's types. Vulkan Backend's `Presentation` implementation
  dispatches internally to the correct WSI extension
  (`VK_KHR_win32_surface`, `VK_KHR_android_surface`, and — future — a
  MoltenVK or native-Metal equivalent) based on the handle it receives.
  Platform and RHI remain siblings; Runtime composes them.
- This extends [ADR-0001](0001-rhi-backend-independence.md)'s forbidden-
  dependency list for Renderer to explicitly include Win32, the Android
  NDK, and `VkSurfaceKHR`, alongside the already-forbidden GLFW/SDL and
  `VkSwapchainKHR`.
- iOS support may eventually use Vulkan via MoltenVK (extending Vulkan
  Backend with a third WSI path) **or** a native Metal RHI backend (a new
  backend module implementing RHI's interfaces directly, parallel to
  Vulkan Backend). This ADR does not choose between them — see
  Alternatives Considered — and no iOS or Metal code is implemented now.

## Consequences

### Positive

- Windows and Android windowed rendering share one Renderer/RHI/
  RenderGraph stack instead of forking per OS — the same benefit
  [ADR-0002](0002-presentation-rendertarget-unification.md) established
  for windowed-vs-headless now extends across operating systems too.
- A future iOS path — whichever backend strategy is chosen — plugs in at
  the Platform/RHI-backend seam without touching Renderer or RenderGraph,
  because those two never depended on any OS or Vulkan surface type in
  the first place.
- Android's structurally different lifecycle model (surface destroy/
  recreate independent of app exit) is confined to Atlantis Platform
  (Android) and Runtime's handling of `Presentation` invalidation,
  rather than leaking into Renderer.

### Negative / Trade-offs

- Platform must be designed generally enough to express both a
  message-pump-driven model (Windows) and an event/lifecycle-driven model
  (Android) through one interface, which is more upfront interface design
  than either platform alone would need — this is the same "must
  generalize before the second consumer exists to validate it" trade-off
  [ADR-0002](0002-presentation-rendertarget-unification.md) already
  accepted for `RenderTarget`, now applied to Platform.
- Android's "surface destroyed entirely, not just resized" case may
  require more than a same-shape `RenderTarget` recreation — Runtime may
  need an explicit "no target available, do not render" state (see
  [resource_lifetime.md](../docs/architecture/resource_lifetime.md) and
  [threading.md](../docs/architecture/threading.md)), which Windows never
  required. This is a real interface-design cost, not fully resolved by
  this ADR.
- The iOS backend-strategy choice (MoltenVK vs. native Metal) is left
  open, which means Vulkan Backend's future extension point is not fully
  pinned down — acceptable since iOS work has not started, but a real
  open question, not a deferred triviality.

## Alternatives Considered

- **Fold Platform's responsibilities into Runtime directly (no separate
  module), as the previous architecture draft did with "Window" owned by
  Runtime via GLFW/SDL.** Rejected: Android's windowing model doesn't map
  onto a GLFW/SDL-shaped abstraction well, and folding OS-specific code
  into Runtime would make Runtime itself platform-specific per build,
  defeating the point of having one Runtime concept across OSes.
- **Use GLFW/SDL for Windows and accept a separate, ad hoc path for
  Android.** Rejected: this reintroduces exactly the per-OS forking
  Atlantis Platform exists to avoid, and neither GLFW nor SDL is a strong
  fit for Android's Activity-lifecycle-driven model.
- **Decide MoltenVK vs. native Metal backend for iOS now, to fully pin
  down Vulkan Backend's future shape.** Rejected: iOS is explicitly a
  future phase not started per [AGENTS.md](../AGENTS.md); deciding this
  now would be exactly the "let future phases shape Phase 1 abstractions
  beyond what's needed" failure mode Phase 1 constraints warn against.
