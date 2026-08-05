# ADR 0014: RHI Interface Mechanism and Vulkan Backend Construction Boundary

- **Status:** Accepted
- **Date:** 2026-08-06
- **Deciders:** _Human approval confirmed 2026-08-06_
- **Related Spec:** [specs/0003-rhi-vulkan-windowed-foundation.md](../specs/0003-rhi-vulkan-windowed-foundation.md)

## Context

[ADR-0001](0001-rhi-backend-independence.md) fixed that RHI's public
surface consists of backend-agnostic interfaces/opaque handles, and that
only Vulkan Backend may include Vulkan headers or reference `Vk*` types —
but explicitly left "the exact mechanism (vtable interface vs.
compile-time backend selection)" to whichever spec first needs a concrete
answer. [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
states Vulkan Backend's public surface is "RHI's interfaces (via
implementation) plus whatever narrow construction API Runtime needs to
stand up a `Device`/`Presentation` at startup," without fixing what that
construction API looks like.

Separately, [ADR-0011](0011-native-window-handle-representation.md)
requires that `NativeWindowHandle` be accepted "only at the active
graphics backend's private WSI construction entry point — never as a
parameter on generic RHI's public `Presentation` interface itself." That
requirement cannot be satisfied without first deciding *where* a
`Presentation` gets constructed and what accepts the handle at that point.

`specs/0003-rhi-vulkan-windowed-foundation.md` is the first spec that
actually needs to construct a real `Device` and `Presentation`, so it is
the first point at which these two previously-deferred questions must be
answered together, consistently.

## Decision

- RHI's public interfaces (`Device`, `Presentation`, `RenderTarget`, and
  any other interface this or a future RHI-touching spec introduces) are
  defined as **abstract C++ base classes** — pure virtual methods, virtual
  destructor — declared in RHI's public headers. This is the "vtable
  interface" mechanism [ADR-0001](0001-rhi-backend-independence.md) left
  open, chosen over compile-time/templated backend selection (see
  Alternatives Considered).
- Consumers (this spec's verification demo; later, Runtime) hold and call
  these only through an owning smart pointer to the abstract base (e.g.
  `std::unique_ptr<atlantis::rhi::Device>`), never a concrete,
  backend-specific type name.
- **Vulkan Backend exposes construction as free factory functions in its
  own public header(s)** — not an RHI header — returning RHI-typed owning
  pointers. Illustratively (exact names/signatures are a Plan-stage
  detail, not fixed by this ADR):
  ```cpp
  namespace atlantis::vulkan_backend {

  atlantis::Result<std::unique_ptr<atlantis::rhi::Device>, DeviceCreateError>
  createDevice(const DeviceCreateParams& params);

  atlantis::Result<std::unique_ptr<atlantis::rhi::Presentation>, PresentationCreateError>
  createPresentation(atlantis::rhi::Device& device,
                      atlantis::platform::NativeWindowHandle windowHandle,
                      const PresentationCreateParams& params);

  }  // namespace atlantis::vulkan_backend
  ```
  `createPresentation` is the **only** function anywhere in RHI/Vulkan
  Backend's public surface that accepts a `NativeWindowHandle` — satisfying
  [ADR-0011](0011-native-window-handle-representation.md)'s constraint
  exactly, by construction rather than by convention.
- `DeviceCreateParams`/`PresentationCreateParams` (or equivalent) are
  expressed purely in RHI-level terms (e.g. "enable validation layers,"
  "application name," requested queue capabilities) — never a `Vk*` type,
  regardless of which header declares the struct.
- Whoever calls these factory functions (this spec's verification demo;
  Runtime, once that module exists) is the only code permitted to include
  Vulkan Backend's construction-API header, consistent with
  [module_boundaries.md](../docs/architecture/module_boundaries.md)'s
  existing statement that only Runtime depends on Vulkan Backend, and only
  for construction. After construction, all interaction happens through
  the returned RHI abstract-interface pointer; no Vulkan-Backend-typed
  reference is retained.
- Vulkan Backend's private WSI boundary (already established by amended
  [ADR-0005](0005-platform-module-multi-os-windowing.md)) lives entirely
  inside the implementation of `createPresentation()`/the concrete
  `Presentation` subclass's construction and recreation path. It is never
  exposed through any function signature outside Vulkan Backend.

## Consequences

### Positive

- Resolves [ADR-0001](0001-rhi-backend-independence.md)'s explicitly
  flagged open mechanism question with the option that fits "exactly one
  backend in Phase 1, no scaffolding for a second."
- Keeps `NativeWindowHandle`'s exposure exactly as narrow as
  [ADR-0011](0011-native-window-handle-representation.md) requires,
  structurally: there is exactly one function signature in the entire
  codebase that can accept it outside Platform itself.
- Gives Runtime-equivalent code a single, small, reviewable seam (two
  factory functions) instead of requiring broader Vulkan Backend
  knowledge, and requires no change to that seam if RenderGraph/Renderer
  are later built on top of the resulting `Device`/`Presentation` objects.

### Negative / Trade-offs

- Virtual dispatch through the RHI abstract interfaces carries a (Phase-1
  acceptable) per-call overhead versus a template-based static-dispatch
  alternative — not measured or optimized here; revisit only if a future
  spec demonstrates it matters.
- A future second backend (not Phase 1 scope) would add a parallel factory
  function set implementing the same abstract interfaces — this is the
  intended extension point, not a sign this decision needs rework, but it
  is a cost paid once that day comes, not now.
- `DeviceCreateParams`/`PresentationCreateParams`'s exact fields are not
  fixed by this ADR (deliberately — see Spec 0003's Risks & Open
  Questions on queue selection); a future Plan or ADR amendment may need
  to extend them.

## Alternatives Considered

- **Compile-time backend selection** (e.g. `Device<VulkanBackend>`,
  templated RHI types). Rejected: would force every RHI consumer signature
  (RenderGraph, Renderer, Runtime, this spec's own verification demo) to
  be templated on a backend type, reintroducing backend awareness
  everywhere RHI is used — the opposite of
  [ADR-0001](0001-rhi-backend-independence.md)'s goal — to serve a second
  backend that does not exist in Phase 1.
- **Have RHI itself depend on Vulkan Backend to provide a default
  factory** (e.g. `rhi::createDefaultDevice()` implemented inside RHI with
  implicit Vulkan knowledge). Rejected: this is exactly the forbidden
  "RHI → Vulkan Backend" dependency direction
  [module_boundaries.md](../docs/architecture/module_boundaries.md)
  already states, and would make RHI's public headers implicitly
  Vulkan-aware.
- **Accept `NativeWindowHandle` directly on `rhi::Presentation`'s own
  (abstract) interface**, rather than only at the Vulkan Backend factory
  function. Rejected outright by
  [ADR-0011](0011-native-window-handle-representation.md), which requires
  the handle to cross only the active backend's private WSI construction
  entry point, never generic RHI's public `Presentation` interface.
- **A single monolithic `createDeviceAndPresentation()` factory** instead
  of two separate calls. Rejected for this ADR's scope: a future headless
  path needs a `Device` with no `Presentation` at all
  ([ADR-0002](0002-presentation-rendertarget-unification.md)'s headless
  case), so collapsing them into one call would need to be undone the
  moment headless rendering is specced.
