# ADR 0026: RenderGraph Multi-Attachment (Color + Depth) and Draw-Pass Execution Integration

- **Status:** Proposed
- **Date:** 2026-08-11
- **Deciders:** Pending Human Review
- **Related Spec:** [specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)

## Context

[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
fixed RenderGraph's execution model for exactly one bound resource per
`execute()` call (a single color `RenderTarget`) and exactly one
drawable operation (`clearColor()`). Spec 0007 needs a pass to bind
*two* resources — a color `RenderTarget` and a depth `Texture`
([ADR-0023](0023-rhi-minimal-gpu-resource-types-and-allocation.md)) — and
to scope a real draw call
([ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
to both via the attachment-scoping mechanism
[ADR-0024](0024-vulkan-dynamic-rendering-for-attachments.md) fixed.

Nothing in ADR-0021's existing model prevents multiple bound resources per
`execute()` call in principle — its binding mechanism is already a
`std::vector<ResourceBinding>` — but its two guard checks and its
transition-insertion algorithm were written and reasoned about against
exactly one bound `RenderTarget`. Someone must decide, as a reviewed
extension rather than an implementation-time assumption, how that model
generalizes to a second bound resource of a genuinely different kind
(a depth attachment, not a second color target), and — new to this
spec — who is responsible for calling the attachment-scoping
`beginRendering()`/`endRendering()` pair
([ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
around a pass's draw work.

## Decision

**RenderGraph's binding mechanism accepts more than one bound resource
per `execute()` call**, unchanged in shape (`std::vector<ResourceBinding>`
already supports this) but newly exercised with a second, distinct kind:
a depth `Texture` binding alongside a color `RenderTarget` binding.
`ResourceBinding` (or a small, additive extension of it, left to the
Plan) carries enough type information to distinguish which RHI resource
kind a given binding targets, since color and depth bindings are
validated and transitioned differently (see below).

**Guard 1 (every `ResourceState`-tagged usage must have a binding)** is
unchanged in principle, extended in scope: it now applies uniformly to
every bound resource kind (color `RenderTarget`, depth `Texture`), not
only `RenderTarget`. **Guard 2 (a bound `RenderTarget` must carry no
declared read usage)** is unchanged, and unchanged in scope — it
continues to apply only to color `RenderTarget` bindings, because it
exists specifically to protect
[ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
always-`Undefined`-incoming-layout premise, which is a `RenderTarget`-
specific simplification that does not apply to a depth `Texture`'s
usage pattern (a depth attachment this spec introduces is legitimately
both read — depth test — and written — depth write — within the same
pass, by the same fixed-function depth-test/depth-write pipeline state
[ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
fixed; RenderGraph's own single-producer-per-*logical*-resource model,
[ADR-0018](0018-render-graph-dependency-derivation-and-ordering.md), is
unaffected by this — the depth `Texture`'s read+write behavior happens
inside one pass's fixed-function pipeline state, not as two separate
declared graph usages).

**Transition-insertion generalizes per bound-resource kind, unchanged in
mechanism.** RenderGraph continues to track "most-recently-recorded
state" per bound resource and continues to call
`CommandList::transitionResource()` whenever a pass's declared usage
state differs from it — exactly [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
existing algorithm, now simply running once per bound resource instead of
assuming exactly one. The **trailing transition** — inserted once, after
the last pass that uses a given bound resource — is unchanged for a color
`RenderTarget` (`PresentSource`, per ADR-0021); for a bound depth
`Texture`, this decision introduces no trailing transition requirement of
its own this round (a depth `Texture` is never presented, and this spec
does not read it back after the frame — see Non-Goals in
[specs/0007-minimal-renderer.md](../specs/0007-minimal-renderer.md)), so
none is inserted.

**Attachment scoping (`beginRendering()`/`endRendering()`,
[ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
is derived and inserted by RenderGraph's `execute()`, not called by a
pass's own execution callback.** This is the core new decision this ADR
adds, directly extending
[ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
"RenderGraph decides *when*, RHI decides *how*" split to a second kind of
scoping call, not only resource-state transitions:

- A pass is recognized as a **draw pass** if any of its declared usages
  targets a resource bound to a color `RenderTarget` or a depth `Texture`
  with an attachment-shaped `ResourceState` (e.g.
  `ColorAttachmentWrite`/the new depth-attachment state
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  introduces).
- For each draw pass, `execute()` calls `beginRendering()` (passing
  whichever bound color/depth resources that pass's usages reference, after
  any transition this pass's usages require has already been recorded),
  invokes the pass's execution callback exactly as
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  already does, then calls `endRendering()` before moving to the next pass
  or inserting a trailing transition.
- This spec's own scope needs exactly one draw pass per frame — Spec 0007
  does not require, and this decision does not design, consecutive draw
  passes sharing one begin/end scope, or a pass touching more than one
  color attachment. A future spec extending Minimal Renderer to multiple
  passes (e.g. a depth pre-pass, a post-process pass) is expected to widen
  this derivation rule, not this ADR.
- A pass's own execution callback continues to record only drawable
  operations (`clearColor()`, the new bind/draw calls
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  introduces) — never a transition, and now, never a
  `beginRendering()`/`endRendering()` call either. This keeps every
  scoping/scheduling decision inside RenderGraph and every mechanism
  decision inside a pass's callback delegating to `CommandList`, unchanged
  from [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
  existing split.

## Consequences

### Positive

- Extends, rather than replaces, every one of
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
  Human-Review-approved decisions — none of that ADR's Decision content is
  reopened, only widened from "exactly one bound resource" to "one or
  more, of more than one kind."
- Deriving `beginRendering()`/`endRendering()` placement from declared
  usages, the same way transitions are already derived, keeps exactly one
  scheduling mechanism (usage-driven derivation) for both concerns instead
  of introducing a second, differently-shaped one for attachment scoping —
  a pass author still never hand-calls a scheduling-relevant RHI method.
- Guard 2's scope staying narrowly on color `RenderTarget` bindings (not
  widened to depth `Texture` bindings) is a deliberate, reasoned choice
  that avoids over-generalizing a guard whose entire justification
  ([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
  write-only premise) is specific to `RenderTarget`.

### Negative / Trade-offs

- RenderGraph's execution-phase bookkeeping grows again — it must now
  additionally recognize which passes are "draw passes" and compute
  attachment-scope boundaries, on top of the transition bookkeeping
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  already introduced. Real, accepted complexity, not incidental — the
  alternative (a pass author manually bracketing its own draw calls) was
  rejected for the same reasons ADR-0021 already rejected caller-authored
  transitions.
- This decision's draw-pass recognition rule and single-scope-per-pass
  assumption are explicitly narrow (this spec's own one-pass acceptance
  target) and are expected to need revisiting, not merely extending
  unchanged, once a future spec introduces multiple draw passes in one
  frame — flagged here so that future work does not mistake this round's
  scope for a general design.
- A depth `Texture`'s read+write-in-one-pass behavior being handled
  entirely inside fixed-function pipeline state (not as two separate
  RenderGraph usage declarations) means RenderGraph's own dependency model
  never actually "sees" the depth-test read — acceptable because nothing
  in this spec's scope needs to schedule anything relative to that
  implicit read, but a future spec introducing genuine multi-pass depth
  usage (e.g. a depth pre-pass followed by a depth-read pass) would need
  to design that relationship explicitly, not inherit it from this
  decision.

## Alternatives Considered

- **Require every draw pass to explicitly call
  `beginRendering()`/`endRendering()` itself, inside its own execution
  callback.** Rejected: reintroduces exactly the caller-authored,
  hand-scheduled GPU work
  [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  already rejected for transitions, for a structurally identical
  scheduling decision — attachment scoping is exactly as derivable from
  declared usage data as a transition is.
- **Treat depth `Texture` binding identically to `RenderTarget` binding**,
  including Guard 2's no-read-usage restriction. Rejected: a depth
  attachment's whole purpose this round is to be read (depth-tested)
  *and* written by the same pass's fixed-function state — banning any
  declared read usage on it would make depth testing inexpressible, unlike
  `RenderTarget`, whose write-only premise is a deliberate, justified
  simplification specific to swapchain image handoff
  ([ADR-0019](0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)),
  not a general property every attachment kind should share.
- **Introduce a trailing "depth read-only" transition for the bound depth
  `Texture`**, anticipating a future pass that samples depth (e.g.
  SSAO, soft particles). Rejected for this round: no such consumer exists
  in this spec's own scope; a future spec introducing one should decide
  the correct trailing state against that real need, not against a
  guess made here.
- **Support multiple simultaneous draw passes sharing one attachment
  scope in this round**, to avoid redesigning the derivation rule later.
  Rejected: this spec's own acceptance target needs exactly one draw pass;
  designing multi-pass scope-sharing now, with no second pass to validate
  the design against, is exactly the premature generalization
  [AGENTS.md](../AGENTS.md)'s "No speculative abstraction" principle warns
  against.
