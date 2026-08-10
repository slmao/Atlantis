# Spec: Minimal Renderer

- **Status:** Approved
- **Author:** Drafted by Claude Code (AI agent) at explicit human
  direction; approved by human review — see Human Review Approval below.
- **Created:** 2026-08-11
- **Related Plan(s):** None yet — a plan may now be drafted against this
  `Approved` spec, per [AGENTS.md](../AGENTS.md); Plan 0007 has not been
  drafted by this document, and may only be drafted once this spec's own
  PR has merged into `main` (see Human Review Approval below).
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md)–[ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0009](../adr/0009-assertion.md),
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)–[ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)
  (all `Accepted`). See **Architectural Impact** below — six new
  decisions were identified and drafted alongside this spec:
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)
  (Renderer public API and resource ownership),
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  (RHI `Buffer`/`Texture` and allocation strategy),
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)
  (Vulkan dynamic rendering),
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  (RHI pipeline/binding/draw surface),
  [ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)
  (RenderGraph multi-attachment/draw-pass integration), and
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  (temporary pre-compiled SPIR-V shader sourcing) — all six `Accepted`
  alongside this spec's own approval below.
- **Human Review Approval (2026-08-11):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer for this branch) on 2026-08-11, following a joint
  architecture review of this spec and
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
  conducted across two prior review rounds (see each document's own
  revision history for the issues those rounds raised and resolved before
  this approval). Three points were confirmed explicitly as part of this
  approval, and are **accepted as-is**, per this document's own content
  as revised by that review:

  1. **Dynamic rendering is adopted via a capability-detected dual path,
     not by raising the Vulkan Backend's overall minimum supported API
     version to 1.3.** A Vulkan 1.3+ device uses core dynamic rendering; a
     lower-version device that advertises `VK_KHR_dynamic_rendering` uses
     the equivalent extension path. Both paths require the Vulkan Backend
     to explicitly query and enable the `dynamicRendering` feature at
     device creation — neither path enables it implicitly. A device with
     neither path available results in `Device` construction returning an
     explicit, recoverable `Result::Err`, never a crash or a silent
     `VkRenderPass`/`VkFramebuffer` fallback (not designed by this spec).
     See Requirements' "Minimal RHI graphics pipeline, binding, and draw
     surface" subsection and
     [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md).
  2. **Attachment format change is the caller's explicit responsibility,
     fixed here as a concrete, testable contract — not left as an open
     risk.** `Pipeline` fixes its target color/depth formats at creation.
     The caller observes a format change via
     `Presentation::metadata().format` (no new RHI query), and — after a
     `Device::waitIdle()`, exactly as every other pre-destruction wait in
     this codebase already requires — recreates every format-dependent
     resource it owns (in this spec's own scope, `Material`'s `Pipeline`).
     An extent-only change (the common resize case) is explicitly
     narrower: only the depth `Texture` is recreated, because `Pipeline`
     uses dynamic viewport/scissor state and its baked-in *formats* are
     unaffected by extent alone. `Renderer` plays no role in either case —
     it does not observe, cache, or recreate any format- or extent-
     dependent resource. See Requirements' "Resize / depth-resource and
     attachment-format lifecycle" subsection,
     [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
     and
     [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).
  3. **Every other design direction reviewed is confirmed as drafted**:
     the depth `Texture`'s combined read/write access is declared as
     exactly one `writes()` usage tagged `DepthAttachmentReadWrite`, never
     a paired `reads()` + `writes()` on the same pass (unchanged,
     pre-existing [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
     rule, not reopened); the draw pass's color output uses a distinct
     `ColorAttachmentOutput` state, never Spec 0006's clear-only
     `ColorAttachmentWrite`; the camera uses a caller-owned uniform
     `Buffer`, referenced (not copied by value) into `Renderer`'s
     per-frame call; the per-draw-item object-to-world transform uses a
     Vulkan push constant, not a second uniform buffer; this round's
     `Buffer`s are few, host-visible/host-coherent, and each individually
     allocated (no pooling, no VMA); and this spec's shader bytecode is
     checked in pre-compiled, alongside its human-readable source and a
     plain-text compiler/version note, with no compiler, reflection, or
     caching invoked by any Atlantis build target. See
     [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
     [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md),
     [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md),
     [ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md),
     and
     [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
     for each decision's full record.
  4. **Plan 0007 is authorized to be drafted, but only once this spec's
     own PR has merged into `main`** — not before, and not as part of the
     same branch/PR. **Implementation remains unauthorized.** Per
     [AGENTS.md](../AGENTS.md), drafting Plan 0007 does not itself
     authorize writing code; that future Plan must still pass its own (or
     a joint Spec+Plan) Human Review, per the same Spec → Plan → Human
     Review → Implementation → Verification → PR → Merge path every prior
     spec in this line has followed, before any source, test, shader, or
     build-configuration file for this spec's scope is written.

  Following this approval: ADR-0022 through ADR-0027 each move to
  `Accepted` (see each ADR's own header) and this spec moves to
  `Approved`. This checkbox-level approval is not itself an authorization
  to implement — see point 4 above and this spec's own Acceptance
  Criteria, which describe properties a future implementation must
  satisfy, not ones already verified.

## Summary

This spec introduces `Atlantis Renderer` (`src/renderer/`) as a real
module for the first time, built entirely on the RHI and RenderGraph
foundation Spec 0003, Spec 0005, and Spec 0006 already shipped. It closes
the gap those three specs deliberately left open: nothing in this
repository can draw an actual mesh yet, only a solid clear color. This
spec extends RHI with the minimal GPU resource (`Buffer`, `Texture`),
pipeline, binding, and draw-command surface a real triangle needs;
extends RenderGraph to scope a draw pass against a color and a depth
attachment; and introduces `Renderer` itself as the thin, stateless
orchestrator that turns a caller-supplied mesh, material, and camera into
recorded GPU work. It does **not** design a Shader System, a scene graph,
an asset system, lighting, texturing, or anything beyond the single,
minimal, solid-shaded, depth-tested mesh its own acceptance target
requires.

## Motivation / Problem Statement

Spec 0006 closed the "nothing can put a pixel on screen" gap by proving
the acquire → RenderGraph-recorded work → submit → present cycle with a
single `clearColor()` pass. It explicitly, deliberately stopped there: no
pipeline object, no vertex/index buffer, no general RHI resource, and no
Renderer — all named as future Minimal Renderer scope in that spec's own
Non-Goals and Out of Scope / Future Work.

[specs/README.md](README.md)'s Candidate Spec Backlog lists Minimal
Renderer as the very next candidate, and — following the docs-sync work
that landed after Spec 0006's own verification — records both of its
declared dependencies, Spec 0005 (RenderGraph Foundation) and Spec 0006
(RHI / RenderGraph Frame Execution Foundation), as `Approved` and
implemented. Both dependencies are now genuinely satisfied: this spec is
the first that can be drafted against a real, GPU-verified frame-execution
foundation rather than against an anticipated one.

Three architectural gaps stand between "a frame that clears to a solid
color" and "a frame that draws a real mesh," none of which any existing
`Accepted` ADR resolves:

- **RHI has no GPU resource beyond `RenderTarget`.** No vertex buffer, no
  index buffer, no uniform buffer, no depth image, no pipeline object, no
  draw call. [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)
  explicitly named "whichever future spec introduces `Buffer`/`Texture`
  resource creation" as the spec that must resolve GPU memory allocation
  strategy — this is that spec.
- **RenderGraph's execution model assumes exactly one bound resource and
  exactly one drawable operation** (Spec 0006's `clearColor()`). Binding a
  color attachment and a depth attachment to one pass, and scoping a real
  draw call to both, is not designed anywhere yet.
- **`Renderer` itself has no concrete shape.**
  [docs/architecture/module_boundaries.md](../docs/architecture/module_boundaries.md)
  and
  [docs/architecture/resource_lifetime.md](../docs/architecture/resource_lifetime.md)
  fixed *principles* (depends only on RHI/RenderGraph/Core; never owns a
  `RenderTarget`) years before any real consumer existed to validate a
  concrete API against — this spec is that first real consumer.

A fourth, narrower gap is procedural rather than architectural but still
requires an explicit, reviewed answer: this spec's minimal material needs
*some* compiled shader bytecode to exist, and Shader System — the module
that will eventually own shader compilation — has no spec yet and is
explicitly a later candidate than this one. Left unaddressed, this spec's
own implementation pressure would silently answer that question the wrong
way (see [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).

## Goals

- Introduce `Atlantis Renderer` as a real module with a reviewed public
  API, module boundary, and resource-ownership model
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)).
- Extend RHI with the minimal `Buffer`/`Texture` GPU resource types a
  mesh and a depth attachment need, with an explicit, reviewed ownership
  model and GPU memory allocation strategy
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)),
  resolving [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s
  named implementation blocker.
- Decide, as an explicit reviewed architecture choice rather than a
  silent implementation detail, how the Vulkan Backend scopes GPU work to
  a color and depth attachment
  ([ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)).
- Extend RHI with a minimal graphics pipeline, binding, and indexed-draw
  surface sufficient for one fixed-vertex-layout, depth-tested,
  unlit/solid-shaded material
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)).
- Extend RenderGraph's execution model to bind and scope a pass against
  more than one resource kind (color + depth), deriving attachment-scoping
  calls automatically from declared usage data, the same way transitions
  already are
  ([ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)).
- Fix an explicit, narrowly-bounded, temporary source for this spec's
  shader bytecode that cannot be mistaken for, or silently evolve into,
  Shader System
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).
- Verify all of the above end-to-end on Windows with a real GPU: a real,
  visible, depth-tested mesh, correctly shaded by a minimal material,
  drawn through `Renderer` → RenderGraph → RHI → Vulkan Backend, presented
  to a real window, correct across resize and minimize/restore, Vulkan
  Validation Layers clean throughout.

## Non-Goals

Explicitly excluded from this spec's design and implementation:

- **Shader System.** No shader source language is chosen, no compiler is
  invoked by any Atlantis code, no reflection is performed, no shader
  caching or hot-reload exists. See
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md).
- **Runtime (the module), Android, iOS, headless rendering, image
  regression testing.** This spec's own manual/verification composition
  (mirroring `examples/frame_execution_demo`) is not a preview of Runtime.
  Windows/Vulkan only, per [AGENTS.md](../AGENTS.md).
- **Scene graph, ECS, asset system, or a model/mesh loader.** This
  spec's mesh data is a small, fixed, hand-authored set of vertices/
  indices (e.g. a cube or a low-poly mesh) constructed directly in C++ or
  loaded from a trivial, fixed-format fixture file this spec's own
  verification composition owns — not a general asset pipeline.
- **Multiple materials, a material parameter/graph system, lighting of
  any kind, shadows, texturing/texture streaming, or any shading model
  beyond a single, fixed, solid/vertex-color material.** See "Minimal
  material" in Proposed Design for exactly what this spec's one material
  does and does not do.
- **GPU-driven rendering, bindless resources, indirect/instanced
  draws, or any multi-draw batching.** A single, fixed
  `drawIndexed()` call per draw item, per
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).
- **Hot-reload of shaders, pipelines, or any GPU resource.**
- **Multiple frames in flight.** The single-frame-in-flight baseline
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  established is unchanged and unreopened by this spec.
- **Multi-threaded command recording, resource creation, or graph
  execution; any job/task system.** Phase 1's single-logical-frame-thread
  baseline ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is
  unchanged.
- **A general GPU memory suballocator (VMA or hand-rolled).**
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  adopts a direct, unpooled, per-resource allocation policy, strictly
  confined to the Vulkan Backend's private implementation, with an
  explicit migration boundary — it does not adopt or scaffold for a
  general allocator.
- **A second graphics backend of any kind**, and no abstraction knob added
  "for" one. Windows/Vulkan only.
- **A general `Sampler` type, a general resource-format table, or a
  sampled/shader-read `Texture`.** This spec's `Texture` type is scoped
  exclusively to depth-attachment usage.
- **Resource lifetime, aliasing, or a resource-versioning model beyond
  RenderGraph's existing `ResourceState` transition bookkeeping**
  (extended, not redesigned, by
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)/[ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)).
- **A general descriptor-set/binding-slot system, push descriptors, or
  bindless textures/buffers.** A single, fixed per-object binding
  mechanism only (camera uniform + one per-draw transform).
- **Cross-owner shared ownership of `Mesh`/`Material`/`Buffer`/`Texture`/
  `Pipeline`.** All are single-owner, move-only, RAII types
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)).
  Reusing a borrowed reference to draw the same `Mesh`/`Material` more
  than once within a frame is supported and is not "sharing" in the
  cross-owner sense this Non-Goal excludes.
- **Editing [specs/README.md](README.md),
  [docs/project-blueprint.md](../docs/project-blueprint.md), or any other
  governance/roadmap document.** Reserved for a separate, later docs sync,
  per the same pattern Spec 0005 and Spec 0006 both followed.

## Requirements

### Functional

**`Atlantis Renderer` module**

- New module `src/renderer/`, target `atlantis_renderer`, alias
  `Atlantis::Renderer`, depending only on `Atlantis::RHI`,
  `Atlantis::RenderGraph`, `Atlantis::Core` — see
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md).
- `Renderer` is a concrete class, not an abstract RHI-style interface, and
  is stateless across frames: it retains no GPU resource and no
  frame-to-frame state of its own.
- `Renderer`'s per-frame entry point takes, by borrowed reference: the
  caller-acquired `RenderTarget`, a caller-owned depth `Texture`, a
  reference to the caller-owned, caller-written camera uniform `Buffer`
  (not a raw camera-data value — the caller writes that frame's view/
  projection matrices into the `Buffer` before calling `Renderer`; see
  "Minimal RHI GPU resources" below and
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)),
  and a caller-owned collection of draw items (each: a `Mesh` reference, a
  `Material` reference, an object-to-world transform). It builds,
  compiles, and executes a RenderGraph description internally, recording
  into the caller-provided `CommandList` — it never calls
  `Device::submit()` or `Presentation::present()` itself. Exact type/
  method names are a Plan-stage detail.

**`Mesh` and `Material`**

- `Mesh` (Renderer-level type): owns exactly one vertex `Buffer` and one
  index `Buffer`, plus an index count and whatever fixed vertex-layout
  metadata `Pipeline` creation needs to match against. Constructed once by
  the caller (this spec's verification composition), from a small, fixed,
  hand-authored set of vertices/indices; not re-uploaded or mutated after
  construction.
- `Material` (Renderer-level type): owns exactly one `Pipeline`.
  Constructed once by the caller from this spec's fixed, pre-compiled
  vertex/fragment SPIR-V pair
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md))
  and a hand-specified vertex-input/binding layout matching `Mesh`'s
  layout.
- Neither is created, cached, deduplicated, or looked up by `Renderer`
  itself — see
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md).

**Minimal RHI GPU resources**

- `Buffer` and `Texture` RHI interfaces, `Device::createBuffer()`/
  `createTexture()`, move-only single-owner ownership, direct/unpooled
  Vulkan Backend allocation — see
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  for the full contract.
- `Buffer` supports exactly three fixed purposes: vertex, index, uniform
  (camera). `Texture` supports exactly one usage this round: depth
  attachment.
- **All three `Buffer` purposes — not only the uniform buffer — use
  host-visible, host-coherent memory this round**, avoiding a staging-
  buffer/upload-copy-command path entirely; each `Buffer` is mapped once,
  for its whole lifetime, at creation. See
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  for why this is a deliberate simplification, not an oversight, and what
  a future spec would need to add to move vertex/index data to
  device-local memory.
- The camera uniform `Buffer` is written directly by the caller once per
  frame, after `acquireNextTarget()` returns (relying on that call's
  existing drain of any previously-retained submission, per
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  and the fix landed in PR #24, to guarantee no GPU work is still reading
  the buffer's previous contents at the moment the caller writes to it).
  No double-buffering or explicit CPU/GPU synchronization beyond that
  existing guarantee is introduced.
- **The per-draw-item object-to-world transform travels as a Vulkan push
  constant, not a second uniform buffer — fixed here, not left to the
  Plan.** A shared uniform buffer, overwritten once per draw item within a
  single frame's command recording, would have every earlier draw item's
  transform silently corrupted by a later one by the time the GPU actually
  executes any of them (recording precedes submission). Push constants are
  copied into the command buffer's own recorded state at record time,
  avoiding this. See
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).

**Minimal RHI graphics pipeline, binding, and draw surface**

- `Pipeline` RHI interface, `Device::createPipeline()`, move-only
  single-owner ownership — one fixed vertex-input layout, depth-test/
  depth-write enabled, opaque rasterization, dynamic viewport/scissor
  state (so one `Pipeline` survives every resize without recreation),
  targeting attachment formats directly (no `VkRenderPass`) — see
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).
- `CommandList` gains: an attachment-scoping operation pair (called only
  by RenderGraph's `execute()`, never by a pass callback — see below;
  unconditionally clears both attachments via `VK_ATTACHMENT_LOAD_OP_CLEAR`
  each frame, so this spec's draw pass never reuses Spec 0006's
  `clearColor()` mechanism), `bindPipeline()`, `bindVertexBuffer()`,
  `bindIndexBuffer()`, a minimal per-object binding mechanism (camera
  uniform buffer binding + a push-constant per-draw transform — see
  above), and `drawIndexed()`. Recording remains legal only from inside a
  RenderGraph pass execution callback, per
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  existing (inspection-enforced, not type-enforced) rule.
- `ResourceState` gains two new variants, **each distinct in name and in
  Vulkan Backend mapping from the existing `ColorAttachmentWrite`**
  (which remains scoped to Spec 0006's transfer-based `clearColor()` and
  is never reused here — reusing it would be a genuine layout-correctness
  bug, not merely a naming ambiguity; see
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)):
  `ColorAttachmentOutput` (the real graphics-pipeline color-output-merger
  write state) and `DepthAttachmentReadWrite` (the depth-test-read-plus-
  depth-write state). The depth `Texture`'s combined read+write behavior
  is expressed as **exactly one** `writes()` usage tagged
  `DepthAttachmentReadWrite` — never a paired `reads()` + `writes()` on
  the same pass, which [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)'s
  existing, unmodified rule already rejects; that single state's Vulkan
  Backend mapping is what carries both access directions internally. See
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)/[ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md).
- The Vulkan Backend scopes every draw exclusively via dynamic rendering
  — no `VkRenderPass`/`VkFramebuffer` object is created anywhere in this
  spec's implementation. **`Device` construction detects, at the physical
  device the Vulkan Backend selects, which of two paths provides it:
  core (device reports `apiVersion >= 1.3`) or extension
  (`VK_KHR_dynamic_rendering`, on a lower core version); either way, the
  `dynamicRendering` feature is explicitly queried and enabled — a Vulkan
  1.3+ "core optional" feature is never enabled merely by requesting a 1.3
  device.** The Vulkan Backend's overall minimum supported API version
  (today, `VK_API_VERSION_1_0`, per
  `vulkan_instance.cpp`/`vulkan_device.cpp`) is **not** raised to 1.3 as
  part of this spec — a device below 1.3 that advertises the extension
  still succeeds. A device with neither path available results in
  `createDevice()` returning an explicit `Result::Err` (a new
  `DeviceCreateError` variant), never a crash or an implicit
  `VkRenderPass`/`VkFramebuffer` fallback. This detection, the feature
  enablement, and the resulting choice of entry-point family
  (`vkCmdBeginRendering` vs. `vkCmdBeginRenderingKHR`) are entirely the
  Vulkan Backend's own responsibility — no capability type or path
  indicator crosses into RHI's or RenderGraph's public surface, and no
  second graphics backend is designed or scaffolded by this decision. See
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md).

**RenderGraph multi-attachment and draw-pass execution integration**

- `render_graph::execute()`'s binding mechanism accepts a color
  `RenderTarget` binding and a depth `Texture` binding simultaneously.
  Guard 1 (every `ResourceState`-tagged usage must have a binding) applies
  uniformly to both kinds; Guard 2 (no declared read usage on a bound
  resource) continues to apply only to the bound `RenderTarget`, not to
  the bound depth `Texture` — see
  [ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)
  for why.
- `execute()` recognizes a **draw pass** from a declared usage carrying
  `ColorAttachmentOutput` or `DepthAttachmentReadWrite` **and only these
  two states** — never `ColorAttachmentWrite`, so Spec 0006's existing
  `clearColor()` pass (which does declare `ColorAttachmentWrite`) is
  structurally unaffected by this new derivation rule. `execute()`
  automatically brackets a recognized draw pass's execution callback with
  the attachment-scoping operation pair — a pass author never calls it
  directly. This spec's own scope needs exactly one draw pass per frame;
  `execute()`'s derivation rule is not required to support more than one
  in this round.
- Transition-insertion (per-bound-resource "most-recently-recorded state"
  tracking, automatic `transitionResource()` insertion on a state change)
  is unchanged in mechanism, now running once per bound resource. **Every
  bound resource — including, newly, the depth `Texture` — is treated as
  entering each `execute()` call from `ResourceState::Undefined`**,
  extending (not modifying) [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
  existing `RenderTarget`-specific rule; valid here because this spec's
  single draw pass unconditionally clears both attachments via load-op
  every frame, so discarding prior contents is always safe. The trailing
  `PresentSource` transition remains specific to the bound `RenderTarget`;
  no trailing transition is inserted for the bound depth `Texture` this
  round.

**Resize / depth-resource and attachment-format lifecycle**

- **Extent-only change:** the caller (this spec's verification
  composition) is responsible for checking, once per frame after a
  successful `acquireNextTarget()`, whether its owned depth `Texture`'s
  extent still matches the acquired `RenderTarget`'s extent; if not, it
  destroys and recreates the depth `Texture` at the new extent before
  calling `Renderer`'s per-frame entry point. `Pipeline` is untouched by
  an extent-only change (dynamic viewport/scissor state, per
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)).
  `Renderer` itself has no resize-driven internal state — see
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md).
- **Format change** (the swapchain's color format selection, per
  [ADR-0016](../adr/0016-presentation-acquire-present-and-recreation-contract.md),
  changes — e.g. the window moves to a monitor with different surface
  capabilities): the caller observes this by comparing
  `Presentation::metadata().format` against the value it last saw, at the
  same point each frame it already checks the depth `Texture`'s extent.
  On a change, the caller calls `Device::waitIdle()`, then destroys and
  recreates every format-dependent resource it owns — in this spec's own
  scope, `Material`'s `Pipeline` — before calling `Renderer`'s per-frame
  entry point again. This is a distinct case from an extent-only change,
  not a superset of it: an extent-only change never requires
  `Pipeline` recreation, and a format change requires it regardless of
  whether the extent also changed in the same recreation. `Renderer`
  plays no role in detecting or acting on either case — see
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)
  and
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  for the full contract.
- A zero-extent frame (from `acquireNextTarget()`'s existing
  `Ok(std::nullopt)` outcome, per
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md))
  results in the caller skipping `Renderer`'s per-frame call entirely for
  that frame, exactly as Spec 0006's own zero-extent handling already
  requires for the whole acquire/execute/submit/present cycle.

**Minimal material**

- Exactly one material this round: a single fixed vertex shader and
  fragment shader pair, taking the camera's view/projection and each
  draw item's object-to-world transform, and either a per-vertex color
  attribute or a single fixed solid color (exact choice left to the
  Plan) as the only visual differentiator — no lighting term, no texture
  sample, no normal, no material parameter beyond what is fixed at shader-
  authoring time. Sufficient to visually confirm a real, depth-tested 3D
  mesh is being drawn correctly (including correct depth ordering across
  its own front/back-facing geometry), and nothing more.

**Phase 1 single-threaded orchestration and thread-safety contracts**

- Every new public type this spec introduces (`Renderer`, `Mesh`,
  `Material`, `Buffer`, `Texture`, `Pipeline`, and every extended RHI/
  RenderGraph method) documents its thread-safety contract at its public
  API — "not thread-safe; caller-thread-only," on the single Phase 1
  logical frame thread, per
  [ADR-0004](../adr/0004-phase1-threading-baseline.md). No mutex, atomic,
  job/task system, or lock-free structure is introduced anywhere in this
  spec's scope.

### Non-functional

- **Performance:** not a goal beyond "does not stall, leak, or busy-spin
  unnecessarily," the same bar every prior spec in this line has set. No
  frame-pacing or micro-benchmark target; direct per-resource allocation
  and single-frame-in-flight are explicit simplifications, not performance
  claims.
- **Memory:** no general GPU memory suballocation strategy is introduced
  or assumed — see
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md).
- **Portability (within the Vulkan-only Phase 1 constraint):**
  implemented and verified on Windows only. RHI's and RenderGraph's public
  interface shapes must not preclude Android's future implementation,
  verified by inspection.
- **Other:** no new third-party dependency — no shader compiler library,
  no allocator library. Unit tests use the existing Catch2 v3 framework
  ([ADR-0007](../adr/0007-test-framework.md)).

## Proposed Design

### Module boundaries (realizing, not moving, existing ones)

Realizes exactly the dependency edges
[module_boundaries.md](../docs/architecture/module_boundaries.md) already
anticipated for Renderer (depends on RHI, RenderGraph, Core only) and
extends RHI/RenderGraph/Vulkan Backend along their existing, unchanged
dependency directions. See
[ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)
for the full Renderer-boundary decision.

```
Runtime-equivalent verification composition, once per frame:
  Presentation::acquireNextTarget()
    -> Ok(std::nullopt): skip this frame entirely (unchanged from Spec 0006)
    -> Err: propagate/log
    -> Ok(RenderTarget): continue below

  Check depth Texture extent against RenderTarget::extent();
    recreate depth Texture via Device::createTexture() if it differs

  Write this frame's camera view/projection directly into the camera
    uniform Buffer's mapped memory (caller-owned; Renderer never touches
    raw camera matrices, only binds this Buffer -- see Requirements)

  Device::createCommandList() -> CommandList

  Renderer::drawFrame(commandList, *renderTarget, *depthTexture,
                       cameraBuffer, drawItems)
    -- internally: builds a RenderGraphBuilder description (one draw
       pass, ColorAttachmentOutput + DepthAttachmentReadWrite usages,
       execution callback that binds Mesh/Material/cameraBuffer state,
       pushes each draw item's transform as a push constant, and calls
       drawIndexed() once per draw item), compiles it, calls
       render_graph::execute() --

  Device::submit(commandList, target's-acquire-complete-signal)
  Presentation::present(target, submissionSignal)

  -- On every exit path, including a mid-frame exit: drain Device's
     outstanding submission (Device::waitIdle()) before destroying
     Presentation/Device/the depth Texture/Mesh/Material --
```

### RHI resource types, ownership, and allocation

See
[ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
for the full decision: `Buffer`/`Texture`'s shape, move-only ownership,
`Device::createBuffer()`/`createTexture()`, and the direct/unpooled,
Vulkan-Backend-private allocation policy that resolves
[ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s named
blocker.

### Attachment scoping mechanism

See
[ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md) for
the full decision: Vulkan core dynamic rendering, no `VkRenderPass`/
`VkFramebuffer`, entirely private to the Vulkan Backend's `CommandList`
implementation.

### Pipeline, binding, and draw surface

See
[ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
for the full decision: `Pipeline`'s shape, `CommandList`'s new bind/draw
operations, and the `ResourceState` extension.

### RenderGraph execution generalization

See
[ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)
for the full decision: multi-resource binding, Guard 1/Guard 2's
generalized/unchanged scope respectively, and draw-pass-derived
attachment-scoping insertion.

### Shader artifact sourcing

See
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
for the full decision: pre-compiled, checked-in SPIR-V, no compiler
invocation, no reflection, explicit migration boundary to a future Shader
System.

### Threading

Unchanged from Spec 0006: single logical frame thread, per
[ADR-0004](../adr/0004-phase1-threading-baseline.md). Every new call this
spec introduces (`Renderer::drawFrame()`, every new RHI method, every
extended RenderGraph method) happens on that same thread.

### Error handling

- Recoverable runtime errors (resource creation failure, pipeline creation
  failure) use `atlantis::Result<T, E>`, consistent with every prior
  spec's convention.
- Programmer errors — a `bindVertexBuffer()`/`bindIndexBuffer()` call with
  a `Buffer` of the wrong purpose; a draw-pass usage with no matching
  binding (Guard 1, generalized); a `RenderTarget` binding with a declared
  read usage (Guard 2, unchanged scope) — use `ATLANTIS_CHECK`/
  `ATLANTIS_ASSERT`, per [ADR-0009](../adr/0009-assertion.md)'s existing
  convention.
- `Mesh`/`Material`/`Buffer`/`Texture`/`Pipeline` misuse outside their
  valid lifetime window (using one after the object that owns it has been
  destroyed; destroying `Device` while any of these it backed are still
  alive) is a **lifetime precondition violation**, the same tier as every
  other borrowed/owned-handle misuse case already established in this
  codebase — not claimed to be guaranteed-detectable, and not tested for
  detection.
- Every `VkResult` along resource creation, pipeline creation, binding,
  and drawing is checked; no `VkResult` is discarded.
- Vulkan Validation Layers are enabled unconditionally in Debug builds and
  any GPU-touching CI job; a validation warning or error is a build/test
  failure.

## Architectural Impact

This spec introduces architecture across six distinct, independently-
reviewable decisions, filed as six new `Proposed` ADRs — none decided by
this spec's prose alone:

1. **Minimal Renderer public API, module boundary, and resource
   ownership** — `Renderer`'s concrete (non-interface) shape, its
   per-frame contract, and `Mesh`/`Material`'s explicit, no-hidden-cache
   ownership. Filed as
   [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md).
2. **RHI minimal GPU resource types and allocation strategy** —
   `Buffer`/`Texture`, move-only ownership (resolving
   [resource_lifetime.md](../docs/architecture/resource_lifetime.md)'s
   open question), and a direct/unpooled allocation policy (resolving
   [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s named
   blocker). Filed as
   [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md).
3. **Vulkan dynamic rendering for attachment management** — a long-term
   Vulkan Backend implementation-strategy decision, explicitly reviewed
   per this spec's own instruction even though it never crosses RHI's
   public surface. Filed as
   [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md).
4. **RHI minimal graphics pipeline, binding, and draw command surface** —
   `Pipeline`, the new `CommandList` bind/draw operations, and the
   `ResourceState` extension. Filed as
   [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).
5. **RenderGraph multi-attachment and draw-pass execution integration** —
   generalizing [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s
   dependency-to-barrier split to a second bound-resource kind and to
   attachment-scoping derivation. Filed as
   [ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md).
6. **Temporary pre-compiled SPIR-V shader artifact sourcing** — an
   explicit, narrowly-bounded procedural decision preventing this spec's
   own implementation pressure from silently deciding Shader System's
   shape. Filed as
   [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md).

No existing `Accepted` ADR's conclusions are restated, reopened, or
modified by this spec or by the six new ADRs above — each new ADR
references and extends the existing ones (particularly ADR-0001,
ADR-0003, ADR-0015, ADR-0018, ADR-0019, ADR-0020, ADR-0021) without
altering them. Architectural Impact was not "None" — `Renderer`, `Buffer`,
`Texture`, `Pipeline`, and RenderGraph's multi-attachment execution
capability are each new public API surface, exactly what
[AGENTS.md](../AGENTS.md)'s "What counts as significant" section requires
the full Spec → Plan → Human Review path for. **This spec's approval is
not itself an authorization to implement** — see the Human Review
Approval note above and the Acceptance Criteria's own checklist item on
this point: a Plan may now be drafted per [AGENTS.md](../AGENTS.md), but
only once this spec's own PR has merged into `main`, and that future
Plan must still pass its own Human Review before any code is written.

## Alternatives Considered

- **Split this spec into two or more smaller specs** (e.g. "RHI graphics
  resources" separately from "Renderer + RenderGraph draw integration").
  Considered, and rejected for this round: the six decisions above are
  genuinely interdependent — `Buffer`/`Texture`'s shape
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md))
  has no real validation target without a `Pipeline`
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
  and a RenderGraph draw pass
  ([ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md))
  to actually use them, mirroring Spec 0006's own reasoning for keeping
  its RHI-execution and RenderGraph-execution halves together. Filing six
  separate ADRs (rather than six separate specs) already gives Human
  Review the ability to accept, reject, or send back any one decision
  independently, without needing six separate spec documents to do it.
- **Fold shader artifact sourcing
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md))
  into the pipeline/binding ADR
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
  rather than filing it separately.** Rejected: the shader-sourcing
  question's entire purpose is to draw an explicit, auditable boundary
  against a future Shader System — bundling it into a general RHI-surface
  ADR would make that boundary harder to find and review on its own
  terms, and risks it being treated as "just an implementation detail" of
  pipeline creation rather than the deliberate non-decision it is.
- **Decide GPU memory allocation strategy generally (VMA or a hand-rolled
  suballocator) in this spec**, rather than continuing to defer it behind
  a direct, unpooled policy. Rejected — see
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  Alternatives Considered: this spec's own resource count does not create
  a concrete pooling/suballocation need, and adopting either strategy
  without one repeats the exact "scaffold for later" mistake
  [AGENTS.md](../AGENTS.md) and [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)
  both warn against.
- **Design a general material/shader-parameter system now**, so a future
  spec adding a second material would not need to extend this round's
  binding mechanism. Rejected — see
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  Alternatives Considered: no second material exists in this spec's own
  acceptance target to validate a general system against, and this
  repeats the exact bundling mistake Spec 0005's and Spec 0006's own
  Alternatives Considered already rejected for their respective rounds.
- **Silently amend `specs/README.md`'s backlog or
  `docs/project-blueprint.md`'s Milestone 4 entry to record this spec's
  own existence/scope.** Rejected: per AGENTS.md, governance/roadmap
  documents change only through their own review, per this spec's own
  Non-Goals — a separate, later docs-sync PR is expected, following the
  same pattern Spec 0005 and Spec 0006 both used.

## Testing & Verification Plan

- **Unit tests:** GPU-independent bookkeeping and validation logic,
  exercised against a fake/mock `CommandList` where a real device is not
  required, per
  [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  layer 1. At minimum, tests must cover:
  - RenderGraph's generalized transition-insertion algorithm across two
    simultaneously bound resources (color + depth), including that each
    resource's own most-recently-recorded state is tracked independently.
  - `execute()`'s draw-pass recognition and attachment-scoping-call
    insertion (begin before the pass's callback runs, end immediately
    after), for a pass with color-only, and for a pass with color+depth,
    attachment-shaped usages.
  - `execute()`'s draw-pass recognition does *not* fire for a pass whose
    only attachment-shaped usage is `ColorAttachmentWrite` (Spec 0006's
    existing clear-pass shape) — confirming this spec's new derivation
    rule is scoped exactly to `ColorAttachmentOutput`/
    `DepthAttachmentReadWrite` and leaves Spec 0006's own pass shape
    unaffected.
  - Every bound resource (color and depth) is treated as entering each
    `execute()` call from `ResourceState::Undefined`, including on a
    second, otherwise-identical `execute()` call against the same bound
    depth `Texture` — confirming no unintended cross-call state leaks
    into the "most-recently-recorded state" bookkeeping.
  - Guard 1 (every `ResourceState`-tagged usage must have a binding),
    exercised against a depth `Texture` binding as well as a
    `RenderTarget` binding.
  - Guard 2 (no declared read usage on a bound `RenderTarget`) continuing
    to hold, and *not* firing for an equivalent declared read usage on a
    bound depth `Texture` — confirming the guard's scope is exactly as
    narrow as
    [ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)
    fixes it.
  - `Buffer`/`Texture`/`Pipeline` construction-parameter validation logic
    that does not require a real Vulkan device (e.g. purpose/usage
    mismatch checks), where such logic exists independent of the Vulkan
    Backend's own device-dependent creation path.
- **GPU integration tests (Windows/Vulkan):** real `Device`/`Buffer`/
  `Texture`/`Pipeline`/`CommandList` construction and destruction,
  Validation-Layers-enabled, mirroring the existing
  `atlantis_vulkan_backend_gpu_tests`/`atlantis_render_graph_tests`
  pattern this repository already uses. Must cover, at minimum: creating
  and destroying a `Buffer` of each of the three purposes; creating and
  destroying a depth `Texture`, including at a resized extent; creating
  and destroying a `Pipeline` from this spec's fixed SPIR-V pair; one full
  draw-pass execution (bind, draw, attachment scope begin/end) against a
  real acquired `RenderTarget` and a real depth `Texture`, with Validation
  Layers reporting zero warnings/errors; and a frame with **more than one
  draw item**, each with a distinct object-to-world transform, confirming
  every draw item ends up at its own correct position (not all at the
  last item's position) — this is the concrete regression test for the
  push-constant-vs-shared-uniform-buffer correctness argument
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  makes. `Pipeline` creation succeeding, drawing without a Validation
  Layer error, and the manually-observed correct visual output (below)
  are, together, this round's only available signal that the hand-
  specified vertex-input/binding layout actually matches the checked-in
  shader bytecode's own interface — no automated reflection-based
  cross-check exists this round (see
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).
  Also cover `createDevice()`'s dynamic-rendering capability detection on
  whichever path the test machine's actual hardware/driver provides
  ([ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)) —
  confirming `Device` construction succeeds and the resolved path's entry
  points work correctly. **This spec's own test environment is expected
  to exercise exactly one of the two paths** (whichever the available
  GPU/driver reports); exercising the other path, and the explicit-error
  case when neither is available, requires hardware/driver combinations
  this spec does not assume are available, and remains verified by code
  inspection only where a second real device/driver cannot be obtained —
  this limitation must be stated explicitly in any verification report,
  not silently treated as fully covered.
- **Headless integration tests:** not applicable — headless rendering
  remains unimplemented, per
  [testing-strategy.md](../docs/process/testing-strategy.md)'s sequencing
  note; flagged, not resolved, consistent with every prior spec's
  equivalent flag.
- **Image regression tests:** not applicable — this spec's manual
  verification checks for a visible, correctly-shaped, correctly-depth-
  ordered mesh by direct observation, not automated pixel comparison,
  gated on headless rendering per [AGENTS.md](../AGENTS.md) sequencing.
  This is a real, accepted limitation for this spec's own claim of
  "correct output": a human visually confirming a mesh looks right is not
  equivalent to a pixel-exact regression gate, and this spec does not
  claim otherwise.
- **Vulkan Validation Layers:** mandatory and must run clean for every
  manual and automated exercise of resource creation, pipeline creation,
  binding, attachment scoping, transition, drawing, submission, and
  present — per [AGENTS.md](../AGENTS.md) and
  [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
- **Manual verification:** a minimal, non-shipping composition (mirroring
  `examples/frame_execution_demo`'s own structure and disclaimer) creates
  a Windows Platform window, constructs a `Device`/`Presentation`, this
  spec's fixed `Mesh`/`Material`/camera uniform `Buffer`/depth `Texture`,
  and — driven by the existing non-blocking Platform event loop — runs
  the full acquire → recreate-depth-if-needed → update-camera →
  `Renderer::drawFrame()` → submit → present cycle every frame. It
  confirms:
  - A visible window shows a recognizable, correctly-shaded, correctly
    depth-ordered 3D mesh (front-facing geometry occludes back-facing
    geometry correctly; no visible z-fighting or inverted depth test),
    continuously across repeated frames.
  - Interactive resize continues to show the mesh correctly (including
    depth correctness) at the new window size, with the depth `Texture`
    visibly/measurably recreated at the matching extent — no stretched,
    corrupted, or stale depth buffer — and with `Pipeline` demonstrably
    *not* recreated for an extent-only change (e.g. by a log/assertion
    the verification composition emits, confirming the dynamic viewport/
    scissor path is actually exercised, not merely declared).
  - **Format-change handling is exercised if the test environment allows
    it** (e.g. dragging the window to a second monitor with different
    surface capabilities): `Presentation::metadata().format` changing is
    correctly detected by the caller, `Device::waitIdle()` is called, and
    `Material`'s `Pipeline` is recreated, with no Validation Layer
    warning or error across the transition. **If the test environment has
    no second monitor/format to trigger this against, this case is
    verified by code inspection only** — this spec does not claim a
    format change was genuinely observed on hardware if it was not; the
    manual-verification report must state explicitly which of the two
    (genuinely exercised vs. inspected-only) applied.
  - Minimizing the window results in no crash, no busy-spin, and no
    Vulkan call being made while minimized; restoring resumes correct
    rendering (mesh, depth, camera) with no special recovery step visible
    to the user — mirroring Spec 0006's own equivalent guarantee.
  - The application exits cleanly at any point in this sequence,
    including mid-resize, minimized, and after a deliberate mid-frame
    exit (acquired but not yet submitted/presented), with no outstanding
    acquired `RenderTarget`, no leaked `CommandList`/`Buffer`/`Texture`/
    `Pipeline`, and no Validation Layer warning or error at any point,
    including at shutdown — extending Spec 0006's own destruction-
    precondition discipline to this spec's new owned resources.

## Acceptance Criteria

- [ ] RHI's and RenderGraph's public headers contain no `Vk*` type and no
      `#include <vulkan/...>` — verifiable by inspection/grep, unchanged
      from Spec 0006's own equivalent criterion.
- [ ] No direct `vkCmd*` call, no `VkImageMemoryBarrier`/
      `vkCmdPipelineBarrier` construction, and no `VkRenderPass`/
      `VkFramebuffer` object, exists anywhere outside the Vulkan Backend's
      `CommandList` implementation — the last clause is new this round,
      per [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md).
- [ ] `createDevice()` selects between the core and extension dynamic-
      rendering paths correctly for whichever physical device is
      selected, explicitly queries and enables the `dynamicRendering`
      feature on whichever path applies, and returns an explicit
      `Result::Err` (never a crash, never an implicit render-pass/
      framebuffer fallback) when neither path is available — verifiable
      by inspection of the capability-detection code path, and, where the
      test environment's hardware allows exercising it, by a GPU
      integration test.
- [ ] No Vulkan capability/feature-detection type, and no indication of
      which dynamic-rendering path a `Device` resolved to, appears in any
      RHI or RenderGraph public header — verifiable by inspection/grep,
      per [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md).
- [ ] `Buffer`, `Texture`, and `Pipeline` are each move-only (movable,
      non-copyable) — a compile-time property, verified as such.
- [ ] Every `Buffer`/`Texture` this spec's implementation creates is
      backed by its own individual `vkAllocateMemory` call, released by
      its own individual `vkFreeMemory` call at destruction — no shared
      `VkDeviceMemory` block backs more than one resource anywhere in this
      spec's implementation.
- [ ] `Renderer` retains no `RenderTarget`, `Texture`, `Mesh`, `Material`,
      or any other GPU resource across two separate calls to its per-frame
      entry point — verifiable by inspection that `Renderer` holds no such
      member state.
- [ ] `Mesh`/`Material` are never created, cached, or looked up by
      `Renderer` itself anywhere in this spec's implementation —
      verifiable by inspection that `Renderer` has no such factory method
      or internal registry.
- [ ] A `ResourceState`-tagged usage against any bound resource kind
      (color `RenderTarget` or depth `Texture`) with no supplied binding
      is rejected as a programmer error at `execute()` time, in every
      tested case.
- [ ] Binding a `RenderTarget` to a logical resource with any declared
      read usage is rejected as a programmer error at `execute()` time,
      unchanged from Spec 0006; the equivalent declared read usage on a
      bound depth `Texture` is *not* rejected, in every tested case.
- [ ] `execute()` correctly brackets every recognized draw pass's
      execution callback with attachment-scoping begin/end calls, and
      never inserts one for a pass with no attachment-shaped usage.
- [ ] `execute()`'s draw-pass recognition never triggers on a
      `ColorAttachmentWrite`-tagged usage — Spec 0006's existing
      `examples/frame_execution_demo` clear pass continues to compile and
      execute with no attachment-scoping call inserted around it.
- [ ] The depth `Texture`'s combined read/write usage is declared as
      exactly one `writes()` call tagged `DepthAttachmentReadWrite`
      anywhere this spec's implementation declares it — no pass anywhere
      in this spec's implementation declares both a `reads()` and a
      `writes()` usage against the same logical resource (unchanged,
      pre-existing [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
      rule, not reopened by this spec).
- [ ] The per-draw-item object-to-world transform is recorded as a Vulkan
      push constant in every draw this spec's implementation records —
      no second uniform buffer is written more than once per frame for
      this purpose.
- [ ] `Pipeline` objects created by this spec's implementation use dynamic
      viewport/scissor state — no pipeline is recreated solely because the
      window was resized.
- [ ] No shader compiler, and no SPIR-V reflection code, is invoked by any
      CMake target or any Atlantis Core/RHI/RenderGraph/Renderer/Tools
      source file this spec's implementation adds — verifiable by
      inspection of the build configuration and source tree.
- [ ] Every checked-in `.spv` file this spec adds has a corresponding
      checked-in, human-readable shader source file and a plain-text note
      of the compiler/version used to produce it — verifiable by
      inspection of the added files.
- [ ] Every `VkResult` along resource creation, pipeline creation,
      binding, attachment scoping, drawing, submission, and present is
      checked; no `VkResult` is discarded.
- [ ] Debug builds and any GPU-touching CI job run with Vulkan Validation
      Layers enabled; a validation warning or error fails the run.
- [ ] The manual verification composition shows a visible, correctly-
      shaded, correctly depth-ordered mesh; continues to do so across
      interactive resize, including a correctly-recreated depth `Texture`;
      makes zero Vulkan calls while minimized; and resumes correctly on
      restore.
- [ ] An extent-only recreation (ordinary interactive resize) never
      recreates `Pipeline` anywhere in this spec's implementation — only
      the depth `Texture` is recreated.
- [ ] A format change is detected by comparing
      `Presentation::metadata().format` against the caller's last-seen
      value (no new RHI query introduced for this purpose), and results
      in `Device::waitIdle()` being called before every format-dependent
      resource the caller owns (`Material`'s `Pipeline`, in this spec's
      own scope) is destroyed and recreated — verifiable by inspection of
      the verification composition's own code, and, if the test
      environment allows genuinely triggering a format change (e.g. via a
      second monitor with different surface capabilities), by observing
      correct, Validation-Layers-clean behavior across it; if the test
      environment cannot genuinely trigger a format change, this remains
      verified by inspection/code review only, and that limitation is
      reported as such rather than claimed as fully exercised.
- [ ] `Renderer` contains no code path that reads
      `Presentation::metadata()`, compares a format, or recreates
      `Pipeline`/`Material`/`Texture` — the format-change contract is
      entirely caller-side, verifiable by inspection.
- [ ] No `src/renderer/` code depends on Atlantis Platform, Win32, the
      Android NDK, or any `Vk*` type — verifiable by inspection/grep.
- [ ] No scene graph, ECS, asset system, model loader, texture, lighting
      term, second material, instanced/indirect draw, or multi-frame-in-
      flight machinery is implemented anywhere this spec's implementation
      touches.
- [x] All six ADRs listed in Architectural Impact
      ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md))
      reach `Accepted` before this spec is marked `Approved` — satisfied
      2026-08-11 (see Human Review Approval note above); this checkbox
      gated spec approval, not implementation — every other checkbox in
      this section still describes a property the future implementation
      must satisfy, not one already verified.

## Risks & Open Questions

- **Exact vertex-input attribute set** (position + one additional
  attribute, e.g. per-vertex color, vs. position + normal for a simple
  lighting-adjacent visual check) is left to the Plan — this spec fixes
  only that the material must visually distinguish geometry and confirm
  correct depth ordering, not the concrete attribute layout.
- **Exact mesh content** (a hand-authored cube, a low-poly sample mesh, or
  an equivalent fixed shape) is left to the Plan, provided it is non-planar
  enough to genuinely exercise depth testing (a flat quad alone would not).
- **Exact struct-level `ResourceState` naming/spelling** for
  `ColorAttachmentOutput`/`DepthAttachmentReadWrite` and for the buffer-
  purpose bookkeeping states is left to the Plan — this spec and
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  fix the semantics and each state's distinctness from
  `ColorAttachmentWrite`, not the exact enumerator spelling.
- ~~Whether Vulkan Backend raises its minimum core API version to 1.3, or
  requires `VK_KHR_dynamic_rendering` on an older version~~ — **resolved
  by Human Review (2026-08-11): neither, exclusively — a capability-
  detected dual path is adopted instead, and the overall minimum version
  is not raised.** See this spec's own Human Review Confirmations
  Received note above and
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md).
- ~~`Pipeline` attachment-format staleness across a swapchain format
  change~~ — **resolved by Human Review (2026-08-11): a concrete,
  caller-owned contract, not an open risk.** See this spec's own Human
  Review Confirmations Received note above, Requirements' "Resize /
  depth-resource and attachment-format lifecycle" subsection, and
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).
- **Exact checked-in `.spv` file location and naming convention** is left
  to the Plan, per
  [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md).
- **Whether a GPU-integration test category distinct from the existing
  `gpu`-labeled pattern is needed** for resource/pipeline creation tests
  that need a real device but do not fit
  [testing-strategy.md](../docs/process/testing-strategy.md)'s existing
  layer boundaries — the same open question Spec 0006 already flagged,
  now recurring for this spec's own new GPU-dependent test surface;
  flagged, not resolved.
- Whether a future spec revisiting the single-frame-in-flight baseline
  (per [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  own Negative/Trade-offs) will also need to revisit this spec's
  direct-write-to-uniform-buffer approach is left open — the current
  design relies specifically on single-frame-in-flight's existing
  acquire-time drain guarantee, and would need re-examination alongside
  any future multi-frame-in-flight work.
- Whether the direct, unpooled per-resource allocation policy
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md))
  will need revisiting sooner than expected, if a future spec's resource
  count approaches a driver's `maxMemoryAllocationCount` limit before a
  performance-motivated suballocator spec would otherwise have been
  written, is left open per that ADR's own stated migration boundary.

## Out of Scope / Future Work

Shader System (next backlog candidate after this spec, per
[specs/README.md](README.md)'s Section B), Android Platform and Vulkan
presentation, headless rendering, and image regression testing all remain
later, separately-specced work per
[docs/project-blueprint.md](../docs/project-blueprint.md) and are not
advanced, designed, or unblocked by this spec beyond satisfying this
minimal-renderer foundation as their own future dependency. A future
Shader System spec is expected to be the first consumer that needs
`Device::createPipeline()`'s bytecode-plus-layout contract fed by real
compilation and reflection rather than this spec's hand-authored,
checked-in `.spv` files — see
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md).
A future spec introducing a second material, a texture, lighting, or
multiple draw passes is expected to need to extend — not merely reuse
unchanged — this spec's binding mechanism
([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
and RenderGraph draw-pass derivation rule
([ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)).
A future performance-motivated spec may revisit both the single-frame-in-
flight baseline and this spec's direct/unpooled GPU memory allocation
policy ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)).
A future asset-system spec is expected to be the first real consumer that
needs cross-owner shared ownership of `Mesh`/`Material`/GPU resources,
which this spec deliberately does not design.
