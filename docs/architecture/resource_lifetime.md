# Resource & RenderTarget Lifetime

> **Status: PROPOSED — pending spec/ADR approval. Not as-built.** See the
> status note in [overview.md](overview.md) and
> [docs/architecture/README.md](README.md).
>
> **Revised 2026-08-02** to generalize windowed-path invalidation triggers
> across Windows and Android for the primary-platform decision — see
> [ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md).

This document states the ownership/lifetime model RHI resources and
`RenderTarget` are designed against. See
[ADR-0003](../../adr/0003-resource-rendertarget-ownership-model.md).

## Principle: explicit ownership, borrowed use

- **RHI resources** (`Buffer`, `Texture`, `Sampler`, pipeline objects,
  `RenderTarget`) are explicitly owned by whoever created them through
  RHI, with RAII-style teardown (destroying the owning handle releases
  the underlying backend object). There is no hidden global cache or
  refcounted resource pool in Phase 1 — if a resource needs to be shared,
  the *caller* decides that and holds the shared ownership explicitly
  (e.g. via a shared-ownership handle type Core provides), RHI does not
  impose sharing policy on its own.
- **Renderer never owns a `RenderTarget`.** It receives a non-owning
  reference/view for the duration of a single frame's draw call and does
  not retain it past that frame. This is what keeps Renderer ignorant of
  presentation lifecycle (resize, recreation, present-mode changes).

## Windowed path (Windows, Android, future iOS)

- `Presentation` (RHI interface, Vulkan Backend implementation) owns the
  swapchain and the `RenderTarget`s backed by its images, on every
  platform — Renderer's contract does not vary by OS.
- Runtime acquires a `RenderTarget` from `Presentation` each frame, passes
  it to Renderer (borrowed), then returns it to `Presentation` via present.
- `Presentation` recreates its internal swapchain and the `RenderTarget`s
  it vends whenever the underlying surface becomes invalid — the trigger
  differs by platform, but the effect on Renderer does not:
  - **Windows:** window resize, or the swapchain reporting
    out-of-date/suboptimal.
  - **Android:** surface resize, and — distinctly from Windows — the
    surface can be **destroyed entirely** (app backgrounded, Activity
    paused) and later **recreated** with no guarantee it's the same
    native handle. Atlantis Platform (Android) is responsible for
    surfacing this as an invalidation event to Runtime (see
    [threading.md](threading.md)); `Presentation` treats it the same way
    it treats a Windows resize — tear down and recreate — but Runtime may
    additionally need to suspend frame submission entirely while no valid
    surface exists (a Runtime-level "no RenderTarget available" idle
    state — see Open Questions in [threading.md](threading.md)).
  - **Future iOS:** not designed; whichever backend (MoltenVK vs. native
    Metal) is eventually chosen will define its own equivalent trigger.
- Renderer is not involved in and does not need to know about
  recreation on any platform — it just receives whatever `RenderTarget`
  it's given next frame, or isn't invoked at all if Runtime decides no
  frame should be submitted.

## Headless path

- The caller (Runtime in a headless entry point, or Tools) explicitly
  creates an offscreen `RenderTarget` through RHI and owns it for as long
  as needed (e.g. the lifetime of a test or a batch job).
- No `Presentation` object is involved. Renderer's contract is identical
  to the windowed case: it borrows the `RenderTarget` for the draw, does
  not own it.

## What this document does NOT decide

- The exact handle type (raw opaque handle vs. a Core-provided
  reference-counted wrapper vs. a unique-ownership RAII wrapper) is an API
  detail for the RHI spec, not this document — this document fixes the
  *ownership model* (explicit, no hidden caching, Renderer borrows), not
  the C++ type used to express it.
- Resource pooling/suballocation strategy (e.g. a `VkDeviceMemory`
  suballocator) is a Vulkan Backend implementation detail, not part of
  the RHI-level ownership contract, and is not decided here.

## Open questions requiring human/spec decisions

- Whether RHI resource handles are move-only (single owner, matches the
  "explicit ownership" principle most directly) or support explicit
  shared ownership as a first-class handle type, vs. leaving all sharing
  to be built on top in Core. This is exactly the kind of API-shape
  decision this document defers to the RHI spec/ADR rather than deciding
  here.
- Whether `RenderTarget` recreation (windowed resize) needs an explicit
  invalidation signal back to Runtime/Renderer (e.g. "your borrowed
  RenderTarget from last frame is now stale") or whether the borrow-per-
  frame model makes that moot by construction. Current assumption is the
  latter (moot) for simple resize, but it hasn't been reviewed against a
  concrete RHI interface sketch yet — and Android's "surface destroyed
  entirely, no `RenderTarget` obtainable at all until recreated" case
  (above) may need more than the borrow-per-frame model alone: Runtime
  plausibly needs a way to know "don't call Renderer this frame," not
  just "here's a new RenderTarget." Not resolved here.
- Whether Android's destroyed-surface window needs its own explicit state
  on the Runtime/Presentation seam (distinct from "recreate and continue
  as normal," which is sufficient for Windows resize) is a new open
  question introduced by the multi-platform decision, and is left to the
  Atlantis Platform / RHI specs rather than decided by this document.
