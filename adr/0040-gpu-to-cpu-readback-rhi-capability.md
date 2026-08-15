# ADR 0040: GPU-to-CPU Readback — RHI Capability, Synchronization, and Error Semantics

- **Status:** Proposed
- **Date:** 2026-08-15
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md) (`In Review`)

## Context

Nothing in this codebase can move GPU-rendered pixel data back to the CPU
today. [specs/0010-headless-rendering-foundation.md](../specs/0010-headless-rendering-foundation.md)'s
acceptance target requires exactly this — a real, working
`OffscreenTarget`
([ADR-0038](0038-headless-offscreen-rendertarget-construction-and-ownership.md))
color image, drawn into by the unmodified `Renderer::drawFrame()`
([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)),
read back into CPU-visible memory and checked for basic, reproducible
correctness — as the concrete foundation a future Image Regression Testing
spec will build golden-image comparison on top of. This spec does not
design that comparison harness (see Non-Goals in the spec itself); it
designs only the RHI-level capability that makes any future readback
consumer possible at all.

Three existing decisions bound this ADR's scope tightly:

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
  whole lifetime — this ADR must decide whether a readback destination is
  a fourth instance of that same pattern or something categorically
  different.
- [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md)
  generalized `execute()`'s incoming/final resource-state handling
  specifically so a second, caller-built `execute()` call can chain after
  `Renderer::drawFrame()`'s own internal one within the same frame — this
  ADR is that second call's actual content.

## Decision

**`ResourceState` gains exactly one new variant: `TransferSource`** — the
state a color image must be in to be the source of a
`vkCmdCopyImageToBuffer`-shaped copy. No `TransferDestination` (or any
other transfer-adjacent) image state is introduced: the copy's
*destination* is a `Buffer`, not an `RenderTarget`/`Texture`, and RHI's
`ResourceState`/`transitionResource()` model applies only to image
layouts — a host-visible `Buffer` has no Vulkan image layout to track, so
it participates in no `ResourceState` bookkeeping, exactly like the
existing camera/vertex/index `Buffer`s already don't.

**`Buffer` gains a fourth, fixed purpose: readback (a host-visible,
host-coherent, once-mapped-for-its-whole-lifetime destination for a copy
out of a color image)** — created via the existing, unchanged
`Device::createBuffer(BufferCreateParams)`
([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)), no
new creation method. Sizing (tightly-packed bytes for the target's fixed
extent and format) is the caller's own responsibility, computed from
values it already controls (`OffscreenTarget`'s fixed extent/format from
its own construction) — no automatic format negotiation or size query is
introduced. This is a direct, narrow extension of ADR-0023's existing
purpose enum and allocation policy, not a new resource type or a new
allocation strategy.

**`CommandList` gains exactly one new recordable operation:
`copyTextureToBuffer(RenderTarget&, Buffer&)`** (exact name/signature left
to the Plan) — the only new drawable-adjacent primitive this ADR
introduces, deliberately narrow: it copies a color image's full extent,
tightly packed, into a readback-purpose `Buffer` sized to match; no
partial-region copy, no format conversion, no mip/array-layer selection
(none of `RenderTarget`'s color image has more than one of any of these).
Recording remains legal only from inside a RenderGraph pass execution
callback, per
[ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
existing (inspection-enforced) rule — this ADR does not relax it.
Ensuring the copy's writes are host-visible after GPU completion (whatever
memory/pipeline barrier that requires) is entirely the Vulkan Backend's
own private implementation concern, per
[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
existing "RHI decides how" split — no new public type or query is added
for this.

**RenderGraph integration:** the copy is expressed as a RenderGraph pass
declaring **exactly one `writes()` usage, tagged `ResourceState::TransferSource`**,
against the same logical resource the color `RenderTarget` is bound to —
never a paired `reads()` + `writes()`, following the identical precedent
[ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
already established for the depth `Texture`'s combined read+write access.
This is deliberate: a `reads()` usage here would trip Guard 2
([ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)),
which exists specifically to protect the always-write-only `RenderTarget`
premise — the copy pass must not be the exception that reopens it. The
readback `Buffer` itself is referenced directly by the pass's execution
callback, exactly as the existing camera/vertex/index `Buffer`s already
are, and participates in no RenderGraph dependency-derivation or
transition bookkeeping of its own.
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
before reading the readback `Buffer`'s already-mapped pointer (also
unchanged — every `Buffer` is mapped once, for its whole lifetime, at
creation, per
[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)). No new
RHI method, signal type, or fence-adjacent concept is introduced for
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
  `Buffer` purpose, `copyTextureToBuffer()`) is a narrow, single-purpose
  addition following exactly the same minimalism precedent
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)/[ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  already established — no general resource-copy, sampled-`Texture`, or
  `Sampler` capability is smuggled in alongside it.
- Reuses `Device::waitIdle()` and `Buffer`'s existing always-mapped
  contract verbatim for synchronization and CPU access — zero new
  synchronization primitives, zero new public types beyond one enum
  variant, one purpose value, and one `CommandList` method.
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
  performance-motivated spec must revisit this, per
  [ADR-0020](0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  own precedent for exactly this kind of accepted-for-now simplification.
- No format/size cross-check between the readback `Buffer`'s capacity and
  the source image's actual byte layout means a caller mistake here
  surfaces as undefined/incorrect copied bytes or a Validation Layer
  error, not a clean `Result::Err` — an accepted, documented limitation
  matching this codebase's existing precondition-violation tiering, not a
  gap unique to this decision.
- Depth-buffer readback is explicitly not designed by this decision (see
  the Spec's own Non-Goals) — a future spec wanting to validate depth
  output, not just color, must extend this ADR's pattern rather than being
  served by it unchanged.

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
  exception for "small" or "test-only" GPU work — the copy must be a
  RenderGraph pass like every other recorded operation, exactly as this
  decision requires.
- **Support asynchronous or non-blocking readback (e.g. a future/promise
  the caller polls) instead of a synchronous `waitIdle()`.** Rejected for
  this round: no current consumer needs it, and it would require
  double-buffered staging buffers and multi-frame latency tracking this
  spec's Phase 1, single-frame-in-flight scope has no use for — see
  Negative/Trade-offs above; a future spec may revisit once a concrete
  performance need exists.
- **Give the readback `Buffer` its own dedicated creation method instead
  of a fourth `BufferPurpose` value on the existing
  `Device::createBuffer()`.** Rejected: this round's readback `Buffer` is,
  in every structural respect (host-visible, host-coherent, mapped once,
  individually allocated), identical in shape to the existing three
  purposes — a new method would duplicate `Device::createBuffer()`'s
  entire contract for no behavioral difference.
