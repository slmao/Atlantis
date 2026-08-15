# ADR 0040: GPU-to-CPU Readback — RHI Capability, Synchronization, and Error Semantics

- **Status:** Accepted
- **Date:** 2026-08-15
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
  Approval recorded 2026-08-16; see
  [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)'s
  Human Review Approval note for the full, three-round approval record
  this ADR's Decision is part of.
- **Related Spec:** [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md) (`Approved`)

## Context

Nothing in this codebase can move GPU-rendered pixel data back to the CPU
today. [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)'s
acceptance target requires exactly this — a real, working
`OffscreenTarget`
([ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md))
color image, drawn into by `Renderer::drawFrame()`
([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md),
including its own Proposed Amendment), read back into CPU-visible memory
and checked for basic, reproducible correctness — as the concrete
foundation a future Image Regression Testing spec will build golden-image
comparison on top of. This spec does not design that comparison harness
(see Non-Goals in the spec itself); it designs only the RHI-level
capability that makes any future readback consumer possible at all.

**This ADR's own transition requirements depend directly on
[ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
Proposed Amendment and
[ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)'s
final design, not on an independent assumption of its own** — a headless
caller passes `ResourceState::TransferSource` as
`Renderer::drawFrame()`'s new `finalColorState` argument
([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
Proposed Amendment), so the color `RenderTarget` is already in
`TransferSource` state by the time `Renderer::drawFrame()` returns —
**this ADR's copy pass never observes, and never needs to transition
through, `ResourceState::PresentSource`.** See "RenderGraph integration"
below for exactly how this ADR's own pass declaration relies on that
guarantee.

Three existing decisions further bound this ADR's scope:

- [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  fixed `CommandList` as recording exactly two operations
  (`transitionResource`, `clearColor`), each deliberately minimal and
  purpose-built rather than general; Spec 0007 later added a third
  purpose-built family (bind/draw). This ADR follows the same discipline:
  one new, narrow, purpose-built recording operation, not a general
  resource-copy API.
- [ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md) fixed
  `Buffer`'s three purposes (vertex, index, uniform) as an explicit,
  closed set, each host-visible/host-coherent and mapped once for its
  whole lifetime via `Buffer::mappedData()`
  (`src/rhi/include/atlantis/rhi/buffer.h`) — this ADR must decide
  whether a readback destination is a fourth instance of that same
  pattern or something categorically different.

## Decision

**`ResourceState` gains exactly one new variant: `TransferSource`** — the
state a color image must be in to be the source of a
`vkCmdCopyImageToBuffer`-shaped copy. No `TransferDestination` (or any
other transfer-adjacent) image state is introduced: the copy's
*destination* is a `Buffer`, not a `RenderTarget`/`Texture`, and RHI's
`ResourceState`/`transitionResource()` model applies only to image
layouts — a host-visible `Buffer` has no Vulkan image layout to track, so
it participates in no `ResourceState` bookkeeping, exactly like the
existing camera/vertex/index `Buffer`s already don't.

**No new `planTransition()` table entry is required by this ADR itself**
— the one new entry this spec's overall design needs
(`ColorAttachmentOutput → TransferSource`) is required by
[ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
Proposed Amendment / [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)'s
generalized trailing transition (`Renderer::drawFrame()`'s own internal
binding), not by this ADR's copy pass, which — per "RenderGraph
integration" below — records no `transitionResource()` call of its own
at all under the corrected design.

**`Buffer` gains a fourth, fixed purpose: readback (a host-visible,
host-coherent, once-mapped-for-its-whole-lifetime destination for a copy
out of a color image)** — created via the existing, unchanged
`Device::createBuffer(BufferCreateParams)`
([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)), no
new creation method. Sizing (tightly-packed bytes for the target's fixed
extent and format) is the caller's own responsibility, computed from
values it already controls (`OffscreenTarget`'s fixed extent/format from
its own construction, per
[ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md)) —
no automatic format negotiation or size query is introduced. This is a
direct, narrow extension of ADR-0023's existing purpose enum and
allocation policy, not a new resource type or a new allocation strategy.

**`CommandList` gains exactly one new recordable operation:
`copyRenderTargetToBuffer(RenderTarget&, Buffer&)`** (exact
signature/name detail left to the Plan; named to match its actual
parameter type — a `RenderTarget&`, never a `Texture&`, consistent with
every other color-attachment-shaped `CommandList` method already in this
codebase, e.g. `transitionResource(RenderTarget&, ...)`,
`beginRendering(RenderTarget& color, Texture* depth, ...)`) — the only
new drawable-adjacent primitive this ADR introduces, deliberately narrow:
it copies a color image's full extent, tightly packed, into a
readback-purpose `Buffer` sized to match; no partial-region copy, no
format conversion, no mip/array-layer selection (none of `RenderTarget`'s
color image has more than one of any of these). Recording remains legal
only from inside a RenderGraph pass execution callback, per
[ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
existing (inspection-enforced) rule — this ADR does not relax it.
Ensuring the copy's writes are host-visible after GPU completion
(whatever memory/pipeline barrier that requires) is entirely the Vulkan
Backend's own private implementation concern, per
[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
existing "RHI decides how" split — no new public type or query is added
for this. **`copyRenderTargetToBuffer()` only borrows `RenderTarget&`
to record a command against — it does not take ownership of, and does
not end, the caller's borrow of that `RenderTarget`.** Ending the
borrow is entirely
[ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md)'s
RAII-based ownership contract; this ADR neither participates in nor is
required for it (correcting an earlier draft of ADR-0038, which
incorrectly stated this ADR would define a "consuming call").

**RenderGraph integration — depends directly on the `TransferSource`
guarantee established above:** the copy is expressed as a RenderGraph
pass declaring **exactly one `writes()` usage, tagged
`ResourceState::TransferSource`**, against the same logical resource the
color `RenderTarget` is bound to — never a paired `reads()` + `writes()`,
following the identical precedent
[ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
already established for the depth `Texture`'s combined read+write
access. This is deliberate: a `reads()` usage here would trip Guard 2
([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)),
which exists specifically to protect the always-write-only `RenderTarget`
premise. The pass's binding
([ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md))
supplies `incomingState = ResourceState::TransferSource` — the exact
state `Renderer::drawFrame()` was already told to leave the target in —
and `finalState = std::nullopt` (no further transition needed).
**Because the resource's tracked incoming state already equals this
pass's own declared state, `execute()` inserts no `transitionResource()`
call for this pass at all** — the copy pass's execution callback
consists solely of `commandList.copyRenderTargetToBuffer(target,
readbackBuffer)`, nothing else. The readback `Buffer` itself is
referenced directly by the pass's execution callback, exactly as the
existing camera/vertex/index `Buffer`s already are, and participates in
no RenderGraph dependency-derivation or transition bookkeeping of its
own.
`execute()`'s existing draw-pass recognition rule
([ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md):
fires only for `ColorAttachmentOutput`/`DepthAttachmentReadWrite`) is
**unchanged and does not fire for this pass** — a `TransferSource`-tagged
usage is not one of the two recognized states, so the copy pass receives
no attachment-scoping (`vkCmdBeginRendering`/`vkCmdEndRendering`)
bracketing, structurally identical in shape to Spec 0006's original
single-`writes()`-usage `clearColor()` pass, not to a draw pass.

**Synchronization and access are synchronous and blocking, with no new
primitive:** the caller submits the `CommandList` (`Device::submit()`,
unchanged), then calls the existing `Device::waitIdle()`
([ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
before reading the readback `Buffer`'s already-mapped pointer
(`Buffer::mappedData()`, also unchanged — every `Buffer` is mapped once,
for its whole lifetime, at creation, per
[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)). No
new RHI method, signal type, or fence-adjacent concept is introduced for
readback specifically — this ADR reuses `waitIdle()` exactly as it
already exists. This is a deliberate Phase 1 simplification: readback is
synchronous from the caller's perspective, costing a full GPU-idle stall;
no asynchronous, double-buffered, or N-frame-latency-amortized readback
path is designed.

**Error semantics:** every `VkResult` along copy recording, submission,
and wait is checked, unchanged from every existing spec's convention.
Buffer/image size or format mismatch between what the caller computed and
what `OffscreenTarget` actually holds is a caller precondition violation,
not a guaranteed-detectable error — the same tier as this codebase's
existing hand-specified-vertex-layout-must-match-shader precedent
([ADR-0027](0027-temporary-precompiled-spirv-shader-artifacts.md)); this
ADR does not add a runtime format/size cross-check.

## Consequences

### Positive

- Every new piece of public surface (`TransferSource`, the readback
  `Buffer` purpose, `copyRenderTargetToBuffer()`) is a narrow,
  single-purpose addition following exactly the same minimalism
  precedent
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)/[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  already established — no general resource-copy, sampled-`Texture`, or
  `Sampler` capability is smuggled in alongside it.
- Reuses `Device::waitIdle()` and `Buffer::mappedData()`'s existing
  always-mapped contract verbatim for synchronization and CPU access —
  zero new synchronization primitives, zero new public types beyond one
  enum variant, one purpose value, and one `CommandList` method.
- Because `Renderer::drawFrame()` already leaves the target in
  `TransferSource` (per
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment), this ADR's own copy pass never records a
  `transitionResource()` call at all — the simplest possible outcome,
  and direct evidence that the design correctly avoids the "meaningless
  intermediate presentation layout" the review that prompted this
  revision flagged.
- The `writes()`-only, no-`reads()` pass shape keeps Guard 2 fully intact
  and reuses an already-`Accepted` precedent
  ([ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
  rather than inventing a new "how do you express combined read+write"
  pattern.

### Negative / Trade-offs

- Fully synchronous, `waitIdle()`-blocking readback is real, deliberate
  overhead — unsuitable for any future real-time or high-throughput
  readback use case. Accepted because this spec's own scope (prove the
  capability works, support a future test harness that runs case-by-case,
  not per-frame in a live render loop) has no such requirement; a future
  performance-motivated spec must revisit this.
- No format/size cross-check between the readback `Buffer`'s capacity and
  the source image's actual byte layout means a caller mistake here
  surfaces as undefined/incorrect copied bytes or a Validation Layer
  error, not a clean `Result::Err` — an accepted, documented limitation
  matching this codebase's existing precondition-violation tiering.
- Depth-buffer readback is explicitly not designed by this decision (see
  the Spec's own Non-Goals) — a future spec wanting to validate depth
  output, not just color, must extend this ADR's pattern rather than
  being served by it unchanged.
- This ADR's own correctness now has a real, explicit dependency on
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)'s
  Proposed Amendment and
  [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  reaching `Accepted` first — if either is revised in a way that changes
  what state `Renderer::drawFrame()` leaves a headless target in, this
  ADR's own "no transition needed" claim would need re-verification, not
  silently assumed to still hold.

## Alternatives Considered

- **Add a general, sampled/shader-readable `Texture` usage now, so the
  offscreen color image could also be re-sampled by a later pass, not
  only copied out.** Rejected: no concrete consumer needs shader-sampling
  this round — Spec 0007's "no general `Sampler` type, no sampled
  `Texture`" Non-Goal remains true here — and adding one now would be
  exactly the premature generalization
  [AGENTS.md](../AGENTS.md) forbids.
- **Introduce a single convenience method (e.g.
  `Device::readbackTexture()`) that internally creates the staging
  `Buffer`, records the copy, submits, waits, and returns bytes, bypassing
  RenderGraph entirely.** Rejected: recording a copy command is GPU work,
  and AGENTS.md's mandatory-RenderGraph-path rule does not carve out an
  exception for "small" or "test-only" GPU work.
- **Support asynchronous or non-blocking readback (e.g. a future/promise
  the caller polls) instead of a synchronous `waitIdle()`.** Rejected for
  this round: no current consumer needs it, and it would require
  double-buffered staging buffers and multi-frame latency tracking this
  spec's Phase 1, single-frame-in-flight scope has no use for.
- **Give the readback `Buffer` its own dedicated creation method instead
  of a fourth `BufferPurpose` value on the existing
  `Device::createBuffer()`.** Rejected: this round's readback `Buffer` is,
  in every structural respect (host-visible, host-coherent, mapped once,
  individually allocated), identical in shape to the existing three
  purposes — a new method would duplicate `Device::createBuffer()`'s
  entire contract for no behavioral difference.
- **Name the new `CommandList` method `copyTextureToBuffer` (an earlier
  draft's name), matching a hypothetical future generalization rather
  than its actual `RenderTarget&` parameter.** Rejected on review: the
  method's only parameter is `RenderTarget&`, never `Texture&` — naming
  it after a type it does not take is confusing and, worse, was
  symptomatic of the same `Texture`-vs-`RenderTarget` ambiguity
  [ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md)
  had to resolve explicitly. `copyRenderTargetToBuffer` names the actual
  parameter type, consistent with every other `RenderTarget`-shaped
  `CommandList` method already in this codebase.
