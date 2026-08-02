# RHI (Render Hardware Interface)

> **Status: PROPOSED — pending spec/ADR approval. Not as-built.** This
> directory is new (a repository-structure addition), created directly at
> explicit human direction as part of the same bootstrap exception as
> [docs/architecture/](../architecture/overview.md). See the status note in
> [docs/architecture/README.md](../architecture/README.md). No code exists.

## Scope

Atlantis RHI is the backend-agnostic contract that everything above it
(RenderGraph, Renderer, Runtime) programs against, and that Vulkan Backend
implements. See [docs/architecture/overview.md](../architecture/overview.md)
and [docs/architecture/module_boundaries.md](../architecture/module_boundaries.md#atlantis-rhi)
for its place in the module map.

## Anticipated public interface categories (not yet designed)

These are named to communicate expected shape, not to authorize
implementation — each needs its own spec:

- `Device` — backend/adapter handle, resource and command-list factory.
- `CommandList` / `CommandBuffer` — recorded GPU work.
- Resources — `Buffer`, `Texture`, `Sampler`.
- Pipeline / pipeline-state objects.
- `RenderTarget` — see
  [docs/architecture/overview.md](../architecture/overview.md#window-vs-presentation-vs-rendertarget-vs-renderer-vs-rhi-vs-vulkan-backend).
- `Presentation` — swapchain-equivalent abstraction; concrete only in
  Vulkan Backend. Accepts an opaque native-surface handle produced by
  Atlantis Platform (Windows/Android/future iOS) and threaded through
  Runtime — see
  [ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md). RHI
  itself never depends on Atlantis Platform.
- Synchronization primitives (fence/semaphore-equivalent) — see
  [docs/architecture/threading.md](../architecture/threading.md).

## Explicitly out of RHI's surface

- No `Vk*` type or Vulkan header in any RHI public header.
- No GLFW/SDL, Win32, Android NDK, or other windowing/platform-library
  type — and no Atlantis Platform module type either (see
  [ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md)).
- No scene-level or material concept — RHI has no idea what a "mesh" is.

## Backend independence

RHI is defined so that Vulkan Backend is Phase 1's *only* implementation,
but nothing in its interface should require Vulkan specifically. A second
backend is not being built in Phase 1 — see
[AGENTS.md](../../AGENTS.md) — this statement is about interface hygiene,
not a roadmap commitment.

## Related

- [ADR-0001: RHI Backend Independence](../../adr/0001-rhi-backend-independence.md)
- [ADR-0003: Resource & RenderTarget Ownership Model](../../adr/0003-resource-rendertarget-ownership-model.md)
