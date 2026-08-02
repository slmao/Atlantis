# Renderer

> **Status: PROPOSED — pending spec/ADR approval. Not as-built.** This
> directory is new (a repository-structure addition), created directly at
> explicit human direction as part of the same bootstrap exception as
> [docs/architecture/](../architecture/overview.md). See the status note in
> [docs/architecture/README.md](../architecture/README.md). No code exists.

## Scope

Atlantis Renderer is frame orchestration built on RenderGraph + RHI. See
[docs/architecture/module_boundaries.md](../architecture/module_boundaries.md#atlantis-renderer)
for its place in the module map.

## The one hard rule

**Renderer must not directly depend on Win32, the Android NDK, GLFW/SDL,
`VkSurfaceKHR`, or `VkSwapchainKHR`** (or any `Vk*` type, the Vulkan
Backend module, or the Atlantis Platform module). It consumes only RHI
interfaces, RenderGraph, and a `RenderTarget` handed to it by its caller —
and cannot tell which OS or Platform implementation produced that
`RenderTarget`. See
[docs/architecture/overview.md](../architecture/overview.md#window-vs-platform-vs-presentation-vs-rendertarget-vs-renderer-vs-rhi-vs-vulkan-backend),
[ADR-0001](../../adr/0001-rhi-backend-independence.md), and
[ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md).

## Ownership

Renderer borrows the `RenderTarget` it draws into for one frame; it does
not own, create, resize, or track the provenance of a `RenderTarget` — see
[docs/architecture/resource_lifetime.md](../architecture/resource_lifetime.md).
This is what makes it agnostic to windowed vs. headless — see
[docs/architecture/overview.md](../architecture/overview.md#windowed-vs-headless-the-shared-path).

## Explicitly not designed here

No scene representation, material system, or submission API exists yet.
Per [AGENTS.md](../../AGENTS.md), no rendering feature is implemented
ahead of its own spec, and this document does not invent one to fill the
gap.

## Related

- [ADR-0001: RHI Backend Independence](../../adr/0001-rhi-backend-independence.md)
- [ADR-0002: Presentation/RenderTarget Unification](../../adr/0002-presentation-rendertarget-unification.md)
