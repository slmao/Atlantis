# ADR 0133: Loading an Attachment's Contents — Drawing over a Finished Frame

- **Status:** Accepted
- **Date:** 2026-10-24 (accepted 2026-10-24)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-24 (Plan 0066 J12 accepted as proposed, with two review notes folded in: D5's
  incoming states come from the previous graph's reported end states, and the synchronization-validation run records
  its layer settings; recorded once in [Plan 0066](../plans/0066-editor-overlay-and-input.md); reviewed in its own
  docs PR, [PR #288](https://github.com/slmao/Atlantis/pull/288))
- **Related Spec:** [Spec 0066: Viewport Overlay and Editor Input over the Wire](../specs/0066-editor-overlay-and-input.md)
  (`Approved` 2026-10-24; R3, R4; its Correction of 2026-10-24 names this ADR)
- **Related ADR(s):**
  - **Extends** [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md): `beginRendering()` on a
    `RenderTarget` may load an attachment's contents instead of clearing them. Clearing stays the default.
  - **Extends** [ADR-0026](0026-render-graph-multi-attachment-draw-pass-integration.md)'s depth rule (a depth
    binding enters `execute()` `Undefined`) — the revisit it names for "a future spec that stops fully re-clearing
    the depth attachment".
  - **Extends** [ADR-0039](0039-render-graph-execution-caller-specified-resource-state-boundaries.md):
    `incomingState` is honoured for depth bindings too, and `execute()` reports the state it leaves each bound
    resource in.
  - **Extends** [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md): `drawFrame()` returns the end
    states of its colour target and depth (D3); its parameters, passes and ownership rules are unchanged.
  - **Keeps** [ADR-0021](0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md) (RenderGraph
    records every barrier) and [ADR-0108](0108-editor-host-and-viewport-composition.md) (the UI overlay pass is
    unchanged).
  - **Enables** [ADR-0132](0132-viewport-overlay-surface.md) D3 as written: passes recorded after the scene's, into
    the frame's target, against the frame's depth.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

ADR-0132 D3 records the Viewport overlay as RenderGraph passes after `drawFrame()`: the outline and the
primitives drawn over the frame's finished image, depth-tested ones against the frame's depth. Starting Plan 0066
M3 showed that no pass can do that today:

- **Every attachment is cleared.** The Vulkan Backend's `beginRendering()` (`vulkan_command_list.cpp`) uses
  `VK_ATTACHMENT_LOAD_OP_CLEAR` for the colour and the depth attachment of every scope, as ADR-0025 decided ("a
  draw pass always fully repaints both attachments"). The RHI has no way to ask for anything else.
- **The RenderGraph always passes a clear.** `render_graph::execute()` begins every draw pass with its binding's
  `colorClear` and `depthClear`. A second pass on the same target, in the same graph or a later one, erases the
  first.
- **Depth always enters `Undefined`.** ADR-0026 seeds a depth binding's state from `Undefined` in every `execute()`
  (ADR-0039's `incomingState` is ignored for depth). Transitioning from `Undefined` lets the driver discard the
  contents, so the scene's depth cannot be read by a later graph even if the clear were skipped.

The frame's final image is written by `drawFrame()`'s `output_transform` pass into a `RenderTarget` (the swapchain
image, the `--editor` Viewport target, or the capture target), and the frame's depth is the caller's depth
`Texture`, stored (`STORE_OP_STORE`) and left in `DepthAttachmentReadWrite`. Both survive the scene's graph; only
the inability to begin a pass without clearing them stands in the way.

Two `CommandList` implementations exist: the Vulkan Backend's and the RenderGraph tests' fake. The validation layers
the GPU tests run fatal do not include synchronization validation, so a missing barrier between two passes on one
attachment would not be caught by them.

## Decision

1. **The RHI's vocabulary.** `atlantis/rhi/types.h` gains `enum class AttachmentLoad : std::uint8_t { Clear, Load }`.
   - `CommandList` gains a `beginRendering()` overload on a `RenderTarget` taking a load choice per attachment:
     `beginRendering(RenderTarget& color, Texture* depth, ClearColorValue colorClear, float depthClear,
     AttachmentLoad colorLoad, AttachmentLoad depthLoad)`. A clear value is ignored for an attachment that loads.
   - The existing four-parameter call keeps its meaning (both `Clear`), so no caller changes.
   - The `HdrColorTarget` and `ShadowMap` scopes are unchanged: nothing needs to load them.
2. **The Vulkan Backend.** `Load` is `VK_ATTACHMENT_LOAD_OP_LOAD`; the store stays `STORE_OP_STORE`.
   - The transition table gains two same-state entries, `ColorAttachmentOutput → ColorAttachmentOutput` and
     `DepthAttachmentReadWrite → DepthAttachmentReadWrite`.
   - They change no layout. They are memory dependencies: an earlier pass's attachment writes are made available
     to the load and to the next pass's writes.
3. **The RenderGraph.** `ResourceBinding` gains `atlantis::rhi::AttachmentLoad load = Clear`, meaningful for
   `target` and `depthTexture` entries.
   - **Every draw pass** that uses a `Load` binding as its colour or depth attachment begins with `Load` for it.
   - `incomingState` is honoured for `depthTexture` entries as it is for `target` entries; the default `Undefined`
     keeps ADR-0026's behaviour.
   - **Barriers stay the RenderGraph's** (ADR-0021): before each draw pass on a `Load` binding whose state is
     unchanged since its previous writer (or since `incomingState`), `execute()` records the same-state transition
     of item 2.
   - **A new guard:** a `Load` binding whose `incomingState` is `Undefined` is a programmer error
     (`ATLANTIS_CHECK_MSG`): there is nothing to load.
   - **End states are reported.** `execute()` returns the state it left each bound resource in, one entry per
     binding in the bindings' order: for a resource some pass used, its `finalState` when it has one, else its last
     declared usage's state; for one no pass used, its `incomingState` (no trailing transition is recorded for it).
     Today it returns nothing, and ADR-0039's `incomingState`/`finalState` are both supplied by the caller, so a later
     graph has had no way to learn where an earlier one left a resource that had no `finalState`.
   - **The Renderer passes them on.** `drawFrame()` returns its colour target's and its depth `Texture`'s end states
     (from its `execute()`), in place of returning nothing. Its parameters and passes are unchanged, and a caller
     that ignores the result is unaffected.
4. **Nothing else changes.** Every existing binding is `Clear` with its current `incomingState`. The recorded
   commands, the counters and every golden are unchanged; only a caller that asks for `Load` sees a difference.
5. **How ADR-0132 D3 uses it** (the Plan's details, stated here to show that no further RHI change is needed):
   - **Incoming states are reported, never written down.** The overlay's graph binds the frame's target and depth
     with the end states `drawFrame()` returned (item 3), not with literal states. A later change to the state
     `drawFrame()` leaves its depth in therefore cannot leave the overlay's barrier silently wrong. The fatal
     validation layers would not catch such a mismatch, and item 3's guard catches only `Undefined`.
   - **The frame's target.** When an overlay follows, the Runtime asks `drawFrame()` to leave its target in
     `ColorAttachmentOutput` (its `finalColorState`, which the Renderer passes through uninterpreted).
     The overlay's graph binds that target with the colour end state `drawFrame()` reported (equal to
     `finalColorState` by construction; the reported value is used all the same, so colour and depth take one path),
     `Load`, and the frame's real final state. The existing table already covers `PresentSource`, `ShaderRead` and
     `TransferSource`.
   - **The frame's depth.** The depth `Texture` is bound with the depth end state `drawFrame()` reported
     (`DepthAttachmentReadWrite` today) and `Load`. Depth-tested primitives use a pipeline with
     `depthWriteEnabled = false`. On-top primitives and the outline composite are separate passes without a depth
     attachment: a pipeline's depth attachment must match its scope's.
   - **The selection mask.** The mask is written in its own graph, which ends in `ShaderRead` (Guard 2 forbids
     reading a `RenderTarget` inside a graph). The outline pass samples it with `bindTexture()`, as the UI pass
     samples the Viewport (ADR-0108).

## Consequences

### Positive

- **ADR-0132 D3 holds as written:** the overlay is drawn after the scene, over its image, against its depth,
  without touching `drawFrame()`'s passes or parameters (it only gains a return value).
- **Small and opt-in:** one enum, one overload, one binding field, two table entries and the reported end states.
  The default reproduces today's command stream exactly.
- **No state is written down twice:** a graph that continues another's resources starts from what that graph
  reported, so the two cannot drift apart unnoticed.
- **The RenderGraph still owns synchronization:** a caller never records a barrier, and the barrier between two
  passes on a loaded attachment is recorded where every other barrier is.
- **Reusable:** any later pass that composes over a finished frame (a debug view, a measurement overlay) uses the
  same field instead of re-rendering.

### Negative / Trade-offs

- **A public RHI and RenderGraph surface grows,** and the gate applies to the change (`src/rhi/**`,
  `src/render_graph/**`, `src/vulkan_backend/**`).
- **A missing barrier would not be caught by the fatal validation layers** (no synchronization validation).
  Mitigation: the RenderGraph tests assert the same-state barrier is recorded before every pass on a `Load`
  binding, and M3 runs its GPU tests once with synchronization validation enabled. The implementing PR records that
  run with the exact layer settings used (the environment variables or the `vk_layer_settings.txt` keys), so later
  Renderer passes can repeat it.
- **`execute()` and `drawFrame()` gain return values.** Every existing call ignores them and is unaffected; a
  caller that continues a graph's resources must use them.
- **Loading costs bandwidth on tile-based GPUs.** It is paid only when an overlay is drawn; a frame without one
  clears exactly as before.
- **`AttachmentLoad` sits beside `ClearColorValue`,** so a call can carry a clear value it ignores. The overload
  keeps that explicit rather than overloading the meaning of the clear.

## Alternatives Considered

- **Record the overlay inside `drawFrame()`'s own graph.** Lost: a second pass on the same target clears it there
  too, so the gap stays. Drawing inside the `output_transform` pass's scope instead has no depth attachment (the
  depth would have to be sampled and compared by hand, which needs a sampled depth texture, another RHI change),
  and it ties the overlay into `drawFrame()`'s already long signature.
- **Draw the primitives inside the scene's HDR `draw` pass,** which has the depth. Lost: exposure, tone mapping and
  bloom would change the overlay's fixed sRGB colours, and the outline still needs a pass over the final target.
- **Render the overlay offscreen and composite it.** Lost: the composite still begins a pass on the target, which
  clears it; and a `RenderTarget` cannot be read to blend in a shader.
- **Load implicitly whenever `incomingState` is not `Undefined`.** Lost: a binding's incoming state and whether its
  contents matter are separate facts (a reused target may be fully repainted). An implicit rule would also silently
  change any existing draw pass that supplies an incoming state.
- **A depth read-only resource state** (`DepthAttachmentReadOnly`). Lost: not needed. A pipeline with
  `depthWriteEnabled = false` tests without writing, and the layout stays the one the scene left.
- **A single-channel mask format (`R8Unorm`).** Not needed for this decision: the mask is an `Rgba8Unorm`
  offscreen target (Plan 0066's J4, amended). A new `Format` value would touch every switch over formats for a
  transient target.
