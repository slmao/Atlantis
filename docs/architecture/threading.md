# Threading Assumptions

> **Status: PROPOSED — pending spec/ADR approval. Not as-built.** See the
> status note in [overview.md](overview.md) and
> [docs/architecture/README.md](README.md).
>
> **Revised 2026-08-02** to add a Platform/OS-lifecycle note for the
> Windows + Android (primary) / iOS (future) platform decision — see
> [ADR-0005](../../adr/0005-platform-module-multi-os-windowing.md).

This document states the threading *assumption* Phase 1 modules are
designed against, and what is deliberately left open. It is a baseline,
not a locked model — see [ADR-0004](../../adr/0004-phase1-threading-baseline.md).

## Phase 1 baseline

- **One logical frame thread.** Runtime's event loop, `Presentation`
  acquire/present, `RenderGraph` construction, and RHI command recording
  are all assumed to happen on a single thread for Phase 1. No module's
  Phase 1 design should require multi-threaded submission to function.
- **RHI is not required to be internally thread-safe for Phase 1.**
  Concurrent calls into a single `Device`/`CommandList` from multiple
  threads are out of scope; the Vulkan Backend is not required to guard
  against them.
- **Core's threading primitives are primitives, not policy.** Core may
  expose thread/mutex/atomic wrappers as foundation utilities, but Core
  does not impose a job/task system — none is decided yet (see Open
  Questions).

## Why a baseline instead of a full model

Phase 1's own sequencing (windowed rendering first, per
[AGENTS.md](../../AGENTS.md)) does not require multi-threaded command
recording to hit its milestone. Designing a full multi-threaded submission
model now, before any renderer exists to need it, would be exactly the
kind of speculative abstraction this task was told not to introduce.
Stating the single-thread assumption explicitly is what lets later specs
know what they're allowed to assume — and what they'd be changing if they
propose otherwise.

## Ownership of synchronization primitives

Fence/semaphore-equivalent synchronization objects are an RHI concept
(opaque handles), implemented concretely by Vulkan Backend
(`VkFence`/`VkSemaphore`). Renderer and RenderGraph may request
synchronization *through* RHI's interface (e.g. "this pass must wait on
that resource being ready") but never construct or reference a `Vk*` sync
object directly — same boundary rule as everywhere else in this baseline.

## Platform/OS lifecycle events and the single-frame-thread assumption

Windows and Android deliver OS-level lifecycle/window events through
different mechanisms, and this baseline does not yet reconcile them:

- **Windows Platform**: a Win32 message pump on (typically) the same
  thread driving the frame loop — fits the single-frame-thread assumption
  directly.
- **Android Platform**: Activity lifecycle callbacks (pause/resume) and
  surface lifecycle callbacks (surface created/changed/destroyed) are
  delivered by the Android framework, commonly via a native glue layer
  (e.g. `android_native_app_glue`) whose event queue may not naturally
  line up with "the" frame thread the same way a Win32 message pump does.
  A destroyed/recreated surface also directly invalidates the
  `RenderTarget` chain — see
  [resource_lifetime.md](resource_lifetime.md).

**This baseline states a requirement, not a mechanism:** whatever Android
Platform's event-delivery approach turns out to be, it must present
lifecycle events to Runtime in a way consistent with the single-logical-
frame-thread assumption above (e.g. polled/drained at a frame boundary),
not as arbitrary cross-thread callbacks into Renderer/RenderGraph/RHI.
The exact mechanism is **not decided here** — see Open Questions.

## Extension point: multi-threaded command recording

A future model where multiple threads record into separate
`CommandList`s that are submitted together is anticipated as a natural
extension of the RHI's command-recording interface, but:

- it is **not designed here**,
- it is **not a Phase 1 requirement**,
- and no Phase 1 module's public API should be shaped in a way that
  *precludes* it later (e.g. don't bake "single global command list"
  assumptions into a public signature if a per-thread `CommandList`
  parameter costs nothing now) — but also don't build the multi-threaded
  machinery itself ahead of a spec asking for it.

This is a "leave the door open, don't walk through it" note, not an
authorization to add threading abstractions in Phase 1 code.

## Open questions requiring human/spec decisions

- Whether Phase 1 ever needs a worker/job system in Core (e.g. for asset
  loading concurrent with rendering) or whether that's deferred entirely
  until multi-threaded recording is specced.
- Where the line sits between "RHI interface shape that doesn't preclude
  multi-threading" and speculative abstraction — this document asserts a
  principle, not a checklist, and concrete RHI method signatures will need
  human judgment call-by-call when that spec is written.
- **Android Platform's exact event-delivery mechanism** (native glue
  library vs. hand-rolled JNI bridge, and how it hands lifecycle events to
  Runtime without violating the single-frame-thread assumption) — not
  decided here; belongs to the Atlantis Platform spec.
- Whether Android's lifecycle model (app can be paused with its GPU
  context/surface torn down, then resumed) requires Runtime to support a
  "no RenderTarget available, don't render" state as a first-class idle
  mode, distinct from just waiting for the next frame — flagged here, not
  resolved.
