# Architecture Overview

> **Status: PROPOSED — pending spec/ADR approval. Not as-built.**
> This document was drafted directly into `docs/architecture/` ahead of an
> approved spec, at explicit human direction, as a bootstrap exception to
> the as-built-only policy stated in [docs/architecture/README.md](README.md).
> See the status note at the top of that file. Nothing described here is
> implemented; no code exists yet. Do not treat any statement in this
> document as authorizing implementation — that still requires its own
> spec → plan → ADR → implementation cycle per [AGENTS.md](../../AGENTS.md).
>
> **Revised 2026-08-02** to reflect Atlantis's target-platform decision:
> **Windows and Android are Phase 1's primary target platforms; iOS is a
> future target (not started, not designed); Linux is not a target
> platform at all.** This revision introduces **Atlantis Platform** as a
> named module and generalizes the windowed rendering path across
> multiple operating systems. See
> [ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md).

## Purpose

This document establishes the initial module map for Atlantis and the
boundaries between modules, so that later specs (Platform, RHI, Vulkan
backend, render graph, renderer, shader system) have a shared frame of
reference instead of each inventing its own. It does not authorize
building any of these modules; it exists so that when a module's spec is
written, its boundaries are already agreed rather than decided ad hoc
inside that spec.

## Modules

| Module | Role |
|---|---|
| **Atlantis Core** | Foundation: math, containers, memory, logging, non-graphics/non-windowing platform-independent utilities. No graphics or OS-windowing knowledge. |
| **Atlantis Platform** | Per-OS windowing/surface/lifecycle abstraction. Interface + concrete per-OS implementations (Windows Platform, Android Platform, future iOS Platform). Owns Win32/Android NDK/(future) UIKit types so nothing else has to. |
| **Atlantis RHI** | Backend-agnostic Render Hardware Interface: device, resources, command recording, pipelines, `RenderTarget`, `Presentation` abstraction. No Vulkan types, no Platform types, in its public surface. |
| **Atlantis Vulkan Backend** | The sole Phase 1 implementation of RHI, built on Vulkan, for both Windows and Android. Owns every `Vk*` type and every Vulkan extension, including the platform-specific WSI surface extensions (`VK_KHR_win32_surface`, `VK_KHR_android_surface`). |
| **Atlantis RenderGraph** | The central rendering abstraction: pass declaration, resource dependency tracking, barrier/lifetime resolution, execution ordering. Built on RHI. |
| **Atlantis Renderer** | Frame orchestration built on RenderGraph + RHI. Consumes a `RenderTarget` handed to it by its caller. Has no knowledge of windows, platforms, swapchains, or Vulkan — see the hard rule below. |
| **Atlantis Shader System** | Shader authoring/compilation/reflection. Produces artifacts (e.g. SPIR-V + reflection metadata) consumed by RHI/Vulkan Backend. |
| **Atlantis Runtime** | The application/executable layer: owns an Atlantis Platform instance (Windows or Android, at build/run time) and the event loop, and wires `Platform` → `Presentation` → `RenderTarget` → `Renderer` together each frame. |
| **Atlantis Tools** | Offline/dev tooling (asset processing, shader precompilation, debug capture glue). Depends downward on other modules as needed; nothing depends on Tools. |

Full per-module detail: [module_boundaries.md](module_boundaries.md).
Deeper single-module notes: [docs/rhi/README.md](../rhi/README.md),
[docs/render_graph/README.md](../render_graph/README.md),
[docs/renderer/README.md](../renderer/README.md).

## Dependency direction

```
Core
 ^
 |
 +-- Platform ---------------+  (Windows Platform, Android Platform,
 |    ^                      |   future iOS Platform — owns Win32/NDK/
 |    | (implements)         |   UIKit types; produces an opaque native
 |    Windows / Android /    |   surface handle, nothing more)
 |    (future) iOS Platform  |
 |                           |
 +-- RHI --------------------+
 |    ^                      |
 |    | (implements)         |
 |    Vulkan Backend         |  (consumes the opaque native handle to
 |                           |   create VkSurfaceKHR via the matching
 |                           |   WSI extension; future: a native Metal
 |                           |   backend could do the same for iOS)
 +-- RenderGraph <-----------+
 |    ^
 |    |
 +-- Renderer
 |
 +-- Shader System  (feeds pipeline creation in RHI / Vulkan Backend)

Runtime   -> Platform (Windows/Android/future iOS), RHI (Device +
             Presentation), Renderer, RenderGraph
Tools     -> Core, Shader System, RHI (optional, for offline baking)
```

Arrows point from dependent to dependency. Nothing below Core depends on
anything; nothing depends on Runtime or Tools. **Platform and RHI are
siblings** — neither depends on the other; Runtime composes them, passing
the opaque native surface handle Platform produces into RHI's
`Presentation` creation call.

**Forbidden dependencies** (the point of this document):

- Renderer → Win32, the Android NDK, GLFW/SDL, or any windowing/platform
  library: forbidden.
- Renderer → `Vk*` types, or the Vulkan Backend module directly:
  forbidden.
- Renderer → `VkSurfaceKHR`, `VkSwapchainKHR`, or any presentation/
  swapchain/surface type: forbidden.
- Renderer → Atlantis Platform module directly: forbidden. Renderer never
  sees a `Window`, a `Platform` instance, or anything OS-specific.
- RenderGraph → Vulkan Backend or Atlantis Platform directly: forbidden
  (goes through RHI).
- RHI → Vulkan Backend: forbidden (dependency points the other way —
  Vulkan Backend implements RHI's interfaces, RHI never references its
  concrete implementation).
- RHI → Atlantis Platform: forbidden. RHI's `Presentation` interface
  accepts an opaque native-surface handle as a parameter; it does not
  depend on Platform's module types to define that parameter's shape.
- Atlantis Platform → RHI, Vulkan Backend, RenderGraph, or Renderer:
  forbidden. Platform only knows about the OS it targets and Core; it has
  no graphics-API knowledge at all — it produces a handle and lifecycle
  events, nothing more.

## Window vs. Platform vs. Presentation vs. RenderTarget vs. Renderer vs. RHI vs. Vulkan Backend

This is the distinction the task explicitly requires and the one most
likely to get silently blurred later, so it is called out on its own:

- **Window** — the OS-level concept of a drawable surface: an `HWND` on
  Windows, an `ANativeWindow`/`Surface` on Android, a `UIView`/
  `CAMetalLayer` on future iOS. It is not a module — it's the thing each
  concrete Platform implementation owns.
- **Platform** — the Atlantis Platform module: an interface (lifecycle,
  input/event pump, "give me an opaque native surface handle") with one
  concrete implementation per OS (Windows Platform, Android Platform,
  future iOS Platform). Owned by **Runtime**. This is the only module
  that includes Win32/Android NDK/(future) UIKit headers.
- **Presentation** — an RHI-level abstraction (interface lives in RHI,
  implementation lives in Vulkan Backend as a `VkSwapchainKHR` wrapper).
  Takes the opaque native surface handle Platform produced (threaded
  through Runtime) and vends a sequence of presentable `RenderTarget`s,
  one per frame, plus present/acquire operations. This is the only
  module-boundary place `VkSurfaceKHR`/`VkSwapchainKHR` exist — behind
  the Vulkan Backend's implementation of the `Presentation` interface,
  which internally picks the right WSI extension
  (`VK_KHR_win32_surface`, `VK_KHR_android_surface`, and — future, not
  implemented — a MoltenVK or native-Metal equivalent) for whichever
  Platform handle it was given.
- **RenderTarget** — an RHI abstraction for "a drawable surface" (color/
  depth attachments), regardless of origin. It is backed by a swapchain
  image in the windowed path (on any OS), or by an offscreen image in the
  headless path (Spec 0010, `Approved`, implemented and merged via
  [PR #48](https://github.com/slmao/Atlantis/pull/48)). This is the
  abstraction that unifies windowed rendering across Windows/Android/
  (future) iOS *and* headless.
- **Renderer** — consumes RHI + RenderGraph and a `RenderTarget` supplied
  by its caller. It does not create, own, resize, or know the provenance
  of the `RenderTarget`, and has no idea which OS or Platform produced it
  — it only draws into it.
- **RHI** — the backend-agnostic contract: `Device`, command recording,
  resources, pipelines, `RenderTarget`, `Presentation`. Defines these as
  interfaces/opaque handles.
- **Vulkan Backend** — the concrete Vulkan implementation of every RHI
  interface for Phase 1, on both Windows and Android. The only module
  permitted to include Vulkan headers or reference `Vk*` types.

See [ADR-0001](../../adr/0001-rhi-backend-independence.md),
[ADR-0002](../../adr/0002-presentation-rendertarget-unification.md), and
[ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md).

## Windowed vs. headless: the shared path, across platforms

```
Windows (windowed):
  Windows Platform  -->  Vulkan Surface (VK_KHR_win32_surface)
                    -->  Swapchain (Presentation)
                    -->  Atlantis RenderTarget
                    -->  Renderer

Android (windowed):
  Android Platform  -->  Android Vulkan Surface (VK_KHR_android_surface)
                    -->  Swapchain (Presentation)
                    -->  Atlantis RenderTarget
                    -->  Renderer

Future iOS (windowed, not implemented):
  iOS Platform  -->  MoltenVK  or  native Metal RHI backend
                -->  Atlantis RenderTarget
                -->  Renderer

Headless (any platform, implemented — Spec 0010):
  Offscreen Target request  -->  RenderTarget  -->  Renderer
```

Every path converges at `RenderTarget`. Renderer and RenderGraph are
identical code across all of them — they cannot tell which OS, which
Platform implementation, or which path (windowed vs. headless) produced
the `RenderTarget` they were given, by construction (the RHI
`RenderTarget` interface carries no platform- or presentation-specific
members). This is what lets:

- Windows and Android windowed rendering share one Renderer/RHI stack
  instead of forking per OS,
- headless rendering (Phase 1, after windowed per
  [AGENTS.md](../../AGENTS.md)'s sequencing — Spec 0010, `Approved`,
  implemented and merged via
  [PR #48](https://github.com/slmao/Atlantis/pull/48)) reuses that same
  stack, and
- a future iOS path — whichever of MoltenVK or a native Metal RHI backend
  is eventually chosen — plug in without Renderer or RenderGraph changing
  at all. If MoltenVK is chosen, iOS is simply another `Presentation`
  implementation inside (an extended) Vulkan Backend. If a native Metal
  backend is chosen instead, it is a new backend module implementing the
  same RHI interfaces, exactly parallel to how Vulkan Backend implements
  them today — either way, Renderer is untouched. **Neither is decided or
  implemented now.**

Windowed shipped first per Phase 1 sequencing (on Windows and/or
Android), and headless has since followed (Spec 0010, `Approved`,
implemented and merged via
[PR #48](https://github.com/slmao/Atlantis/pull/48) — see the Spec 0010
row in [specs/README.md](../../specs/README.md) for full scope and
verification detail, including its own disclosed single-GPU-vendor
verification limitation). iOS remains future and undecided (see above).
The shared `RenderTarget` boundary is what *allowed* headless to follow
windowed without a rewrite, and is what will do the same for iOS
whenever it is specced — not a reason to have built any of them early.

## Related documents

- [module_boundaries.md](module_boundaries.md) — per-module responsibility,
  dependency, ownership, and boundary detail.
- [threading.md](threading.md) — Phase 1 threading assumptions, including
  Android lifecycle considerations.
- [resource_lifetime.md](resource_lifetime.md) — ownership/lifetime model
  for RHI resources and `RenderTarget`, including per-platform
  invalidation triggers.
- [docs/rhi/README.md](../rhi/README.md),
  [docs/render_graph/README.md](../render_graph/README.md),
  [docs/renderer/README.md](../renderer/README.md) — per-module scope notes.
- ADRs: [0001](../../adr/0001-rhi-backend-independence.md),
  [0002](../../adr/0002-presentation-rendertarget-unification.md),
  [0003](../../adr/0003-resource-rendertarget-ownership-model.md),
  [0004](../../adr/0004-phase1-threading-baseline.md),
  [0005](../../adr/0005-platform-module-multi-os-windowing.md) — all
  `Proposed`, none `Accepted`. See the final report for what requires
  human review before any of this can move to `Accepted`/implementation.
