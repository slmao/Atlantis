# Spec: Minimal Renderer

- **Status:** Approved
- **Author:** Drafted by Claude Code at explicit human direction.
- **Created:** 2026-08-11
- **Human Review Approval:** slmao, 2026-08-11, completing a joint Spec 0007 +
  Plan 0007 Human Review across two prior review rounds. Three points confirmed
  as-is:
  1. **Dynamic rendering is adopted via a capability-detected dual path, not by
     raising the Vulkan Backend's overall minimum supported API version to
     1.3.** A Vulkan 1.3+ device uses core dynamic rendering; a lower-version
     device advertising `VK_KHR_dynamic_rendering` uses the extension path.
     Both require explicitly querying and enabling the `dynamicRendering`
     feature at device creation. A device with neither path available →
     `Device` construction returns an explicit, recoverable `Result::Err`,
     never a crash or a silent `VkRenderPass`/`VkFramebuffer` fallback. See
     [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md).
  2. **Attachment format change is the caller's explicit responsibility, a
     concrete testable contract.** `Pipeline` fixes its target color/depth
     formats at creation. The caller observes a format change via
     `Presentation::metadata().format` (no new RHI query), and — after a
     `Device::waitIdle()` — recreates every format-dependent resource it owns
     (in this Spec's scope, `Material`'s `Pipeline`). An extent-only change
     (the common resize case) is narrower: only the depth `Texture` is
     recreated, because `Pipeline` uses dynamic viewport/scissor state. `Renderer`
     plays no role in either case.
     ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
     [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).)
  3. **Every other design direction confirmed as drafted**: the depth
     `Texture`'s combined read/write access is exactly one `writes()` usage
     tagged `DepthAttachmentReadWrite`, never a paired `reads()` + `writes()`
     ([ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md),
     not reopened); the draw pass's color output uses a distinct
     `ColorAttachmentOutput` state, never Spec 0006's `ColorAttachmentWrite`;
     the camera uses a caller-owned uniform `Buffer`, referenced (not copied)
     into `Renderer`'s per-frame call; the per-draw-item transform uses a
     Vulkan push constant, not a second uniform buffer; this round's `Buffer`s
     are few, host-visible/host-coherent, each individually allocated (no
     pooling, no VMA); shader bytecode is checked in pre-compiled, alongside
     human-readable source and a compiler/version note, with no compiler,
     reflection, or caching invoked by any Atlantis build target
     ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
     [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md),
     [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md),
     [ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md),
     [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).

  Approval authorizes drafting Plan 0007 (only once this Spec's own PR has
  merged) but does not authorize implementation.
- **Related Plan(s):** [Plan 0007](../plans/0007-minimal-renderer.md)
  (`Approved`). Implementation merged via
  [PR #28](https://github.com/slmao/Atlantis/pull/28),
  [PR #30](https://github.com/slmao/Atlantis/pull/30); the dynamic-rendering
  Core-path correction is recorded in
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)'s accepted
  amendment (2026-08-13) and Plan 0007's Post-Approval Deviation Record.
- **Related ADR(s):** Builds on
  [ADR-0001](../adr/0001-rhi-backend-independence.md)–[ADR-0004](../adr/0004-phase1-threading-baseline.md),
  [ADR-0009](../adr/0009-assertion.md),
  [ADR-0014](../adr/0014-rhi-device-presentation-construction-boundary.md)–[ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md).
  Six new decisions filed as
  [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md);
  all `Accepted` alongside this Spec.
- **Editorial revision:** [Spec 0033](0033-documentation-lifecycle-and-compaction.md);
  [PR #149](https://github.com/slmao/Atlantis/pull/149) Batch 2. Original scope
  and obligations retained.

## Summary

Introduces `Atlantis Renderer` (`src/renderer/`) as a real module for the first
time, built entirely on the RHI and RenderGraph foundation Specs 0003, 0005,
and 0006 shipped. It closes the gap those left open: nothing in this repository
can draw an actual mesh yet, only a solid clear color. It extends RHI with the
minimal GPU resource (`Buffer`, `Texture`), pipeline, binding, and draw-command
surface a real triangle needs; extends RenderGraph to scope a draw pass against
a color and a depth attachment; and introduces `Renderer` as the thin,
stateless orchestrator that turns a caller-supplied mesh, material, and camera
into recorded GPU work. It does **not** design a Shader System, scene graph,
asset system, lighting, texturing, or anything beyond the single, minimal,
solid-shaded, depth-tested mesh its acceptance target requires.

## Motivation / Problem Statement

Spec 0006 proved the acquire → RenderGraph-recorded work → submit → present
cycle with a single `clearColor()` pass and deliberately stopped there. Both
its declared dependencies (Specs 0005 and 0006) are now genuinely satisfied and
GPU-verified. Three architectural gaps stand between "a frame that clears to a
solid color" and "a frame that draws a real mesh," none resolved by any
existing `Accepted` ADR:

- **RHI has no GPU resource beyond `RenderTarget`** — no vertex/index/uniform
  buffer, depth image, pipeline object, or draw call.
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md) named "whichever
  future spec introduces `Buffer`/`Texture` resource creation" as the one that
  must resolve GPU memory allocation strategy — this is that spec.
- **RenderGraph's execution model assumes exactly one bound resource and one
  drawable operation** (Spec 0006's `clearColor()`). Binding a color and a
  depth attachment to one pass, and scoping a real draw call to both, is not
  designed anywhere.
- **`Renderer` itself has no concrete shape** —
  [module_boundaries.md](../docs/architecture/module_boundaries.md) and
  [resource_lifetime.md](../docs/architecture/resource_lifetime.md) fixed
  *principles* before any real consumer existed; this Spec is the first.

A fourth, narrower gap is procedural: this Spec's minimal material needs *some*
compiled shader bytecode, and Shader System has no spec yet. Left unaddressed,
implementation pressure would silently answer that question the wrong way
([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).

## Goals

- Introduce `Atlantis Renderer` as a real module with a reviewed public API,
  module boundary, and resource-ownership model
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)).
- Extend RHI with the minimal `Buffer`/`Texture` GPU resource types a mesh and
  a depth attachment need, with an explicit reviewed ownership model and GPU
  memory allocation strategy
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)),
  resolving
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s named blocker.
- Decide, as an explicit reviewed architecture choice, how the Vulkan Backend
  scopes GPU work to a color and depth attachment
  ([ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)).
- Extend RHI with a minimal graphics pipeline, binding, and indexed-draw
  surface sufficient for one fixed-vertex-layout, depth-tested, unlit/solid
  material
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)).
- Extend RenderGraph's execution model to bind and scope a pass against more
  than one resource kind (color + depth), deriving attachment-scoping calls
  automatically from declared usage the same way transitions already are
  ([ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)).
- Fix an explicit, narrowly-bounded, temporary source for this Spec's shader
  bytecode that cannot be mistaken for, or silently evolve into, Shader System
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).
- Verify end-to-end on Windows with a real GPU: a real, visible, depth-tested
  mesh, correctly shaded by a minimal material, drawn through `Renderer` →
  RenderGraph → RHI → Vulkan Backend, presented to a real window, correct
  across resize and minimize/restore, Vulkan Validation Layers clean
  throughout.

## Non-Goals

- **Shader System** — no source language chosen, no compiler invoked by any
  Atlantis code, no reflection, no shader caching or hot-reload
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).
- **Runtime (the module), Android, iOS, headless rendering, image regression
  testing.** This Spec's verification composition is not a preview of Runtime.
  Windows/Vulkan only.
- **Scene graph, ECS, asset system, or a model/mesh loader.** The mesh data is
  a small, fixed, hand-authored set of vertices/indices constructed directly in
  C++ or loaded from a trivial fixed-format fixture the verification
  composition owns — not a general asset pipeline.
- **Multiple materials, a material parameter/graph system, lighting of any
  kind, shadows, texturing/texture streaming, or any shading model beyond a
  single fixed solid/vertex-color material.**
- **GPU-driven rendering, bindless resources, indirect/instanced draws, or any
  multi-draw batching** — a single fixed `drawIndexed()` call per draw item
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)).
- **Hot-reload of shaders, pipelines, or any GPU resource.**
- **Multiple frames in flight** — the single-frame-in-flight baseline
  ([ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md))
  is unchanged and unreopened.
- **Multi-threaded command recording, resource creation, or graph execution;
  any job/task system** — Phase 1's single-logical-frame-thread baseline
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)) is unchanged.
- **A general GPU memory suballocator (VMA or hand-rolled)** —
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  adopts a direct, unpooled, per-resource allocation policy strictly confined
  to the Vulkan Backend's private implementation, with an explicit migration
  boundary — it does not adopt or scaffold for a general allocator.
- **A second graphics backend**, and no abstraction knob added "for" one.
- **A general `Sampler` type, a general resource-format table, or a
  sampled/shader-read `Texture`** — this Spec's `Texture` type is scoped
  exclusively to depth-attachment usage.
- **Resource lifetime, aliasing, or a resource-versioning model beyond
  RenderGraph's existing `ResourceState` transition bookkeeping** (extended,
  not redesigned).
- **A general descriptor-set/binding-slot system, push descriptors, or bindless
  textures/buffers** — a single fixed per-object binding mechanism only (camera
  uniform + one per-draw transform).
- **Cross-owner shared ownership of `Mesh`/`Material`/`Buffer`/`Texture`/
  `Pipeline`** — all are single-owner, move-only, RAII types
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
  [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)).
  Reusing a borrowed reference to draw the same `Mesh`/`Material` more than once
  within a frame is supported and is not "sharing" in the cross-owner sense.
- **Editing `specs/README.md`, `docs/project-blueprint.md`, or any other
  governance/roadmap document** — reserved for a separate, later docs sync.

## Requirements

### Functional

**`Atlantis Renderer` module**

- New module `src/renderer/`, target `atlantis_renderer`, alias
  `Atlantis::Renderer`, depending only on `Atlantis::RHI`,
  `Atlantis::RenderGraph`, `Atlantis::Core`
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)).
- `Renderer` is a concrete class, not an abstract RHI-style interface, and is
  stateless across frames: it retains no GPU resource and no frame-to-frame
  state of its own.
- `Renderer`'s per-frame entry point takes, by borrowed reference: the
  caller-acquired `RenderTarget`, a caller-owned depth `Texture`, a reference to
  the caller-owned, caller-written camera uniform `Buffer` (not a raw
  camera-data value — the caller writes that frame's view/projection matrices
  into the `Buffer` before calling `Renderer`), and a caller-owned collection
  of draw items (each: a `Mesh` reference, a `Material` reference, an
  object-to-world transform). It builds, compiles, and executes a RenderGraph
  description internally, recording into the caller-provided `CommandList` — it
  never calls `Device::submit()` or `Presentation::present()`. Exact
  type/method names are a Plan-stage detail.

**`Mesh` and `Material`**

- `Mesh` (Renderer-level type): owns exactly one vertex `Buffer` and one index
  `Buffer`, plus an index count and whatever fixed vertex-layout metadata
  `Pipeline` creation needs to match against. Constructed once by the caller
  from a small fixed hand-authored set of vertices/indices; not re-uploaded or
  mutated after construction.
- `Material` (Renderer-level type): owns exactly one `Pipeline`. Constructed
  once by the caller from this Spec's fixed, pre-compiled vertex/fragment
  SPIR-V pair
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)) and
  a hand-specified vertex-input/binding layout matching `Mesh`'s layout.
- Neither is created, cached, deduplicated, or looked up by `Renderer` itself
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)).

**Minimal RHI GPU resources**

- `Buffer` and `Texture` RHI interfaces, `Device::createBuffer()`/
  `createTexture()`, move-only single-owner ownership, direct/unpooled Vulkan
  Backend allocation
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md) for
  the full contract).
- `Buffer` supports exactly three fixed purposes: vertex, index, uniform
  (camera). `Texture` supports exactly one usage this round: depth attachment.
- **All three `Buffer` purposes — not only the uniform buffer — use
  host-visible, host-coherent memory this round**, avoiding a
  staging-buffer/upload-copy-command path entirely; each `Buffer` is mapped
  once, for its whole lifetime, at creation
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md) for
  why this is a deliberate simplification and what a future spec would need to
  move vertex/index data to device-local memory).
- The camera uniform `Buffer` is written directly by the caller once per frame,
  after `acquireNextTarget()` returns (relying on that call's existing drain of
  any previously-retained submission, per
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)
  and PR #24, to guarantee no GPU work is still reading the buffer's previous
  contents). No double-buffering or explicit CPU/GPU synchronization beyond
  that existing guarantee is introduced.
- **The per-draw-item object-to-world transform travels as a Vulkan push
  constant, not a second uniform buffer — fixed here, not left to the Plan.** A
  shared uniform buffer overwritten once per draw item during a single frame's
  command recording would have every earlier draw item's transform silently
  corrupted by a later one by the time the GPU executes any of them (recording
  precedes submission). Push constants are copied into the command buffer's own
  recorded state at record time, avoiding this
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)).

**Minimal RHI graphics pipeline, binding, and draw surface**

- `Pipeline` RHI interface, `Device::createPipeline()`, move-only single-owner
  ownership — one fixed vertex-input layout, depth-test/depth-write enabled,
  opaque rasterization, dynamic viewport/scissor state (so one `Pipeline`
  survives every resize without recreation), targeting attachment formats
  directly (no `VkRenderPass`)
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)).
- `CommandList` gains: an attachment-scoping operation pair (called only by
  RenderGraph's `execute()`, never by a pass callback; unconditionally clears
  both attachments via `VK_ATTACHMENT_LOAD_OP_CLEAR` each frame, so this Spec's
  draw pass never reuses Spec 0006's `clearColor()` mechanism), `bindPipeline()`,
  `bindVertexBuffer()`, `bindIndexBuffer()`, a minimal per-object binding
  mechanism (camera uniform buffer binding + a push-constant per-draw
  transform), and `drawIndexed()`. Recording remains legal only from inside a
  RenderGraph pass execution callback, per
  [ADR-0020](../adr/0020-rhi-minimal-resource-command-recording-and-submission-interface.md)'s
  existing (inspection-enforced) rule.
- `ResourceState` gains two new variants, **each distinct in name and in Vulkan
  Backend mapping from the existing `ColorAttachmentWrite`** (which remains
  scoped to Spec 0006's transfer-based `clearColor()` and is never reused here
  — reusing it would be a genuine layout-correctness bug, not merely a naming
  ambiguity;
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)):
  `ColorAttachmentOutput` (the real graphics-pipeline color-output-merger write
  state) and `DepthAttachmentReadWrite` (the depth-test-read-plus-depth-write
  state). The depth `Texture`'s combined read+write behavior is expressed as
  **exactly one** `writes()` usage tagged `DepthAttachmentReadWrite` — never a
  paired `reads()` + `writes()` on the same pass, which
  [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)'s
  existing rule already rejects; that single state's Vulkan Backend mapping
  carries both access directions internally.
- The Vulkan Backend scopes every draw exclusively via dynamic rendering — no
  `VkRenderPass`/`VkFramebuffer` object is created anywhere. **`Device`
  construction detects, at the selected physical device, which of two paths
  provides it: core (`apiVersion >= 1.3`) or extension
  (`VK_KHR_dynamic_rendering` on a lower core version); either way the
  `dynamicRendering` feature is explicitly queried and enabled** — a 1.3+ "core
  optional" feature is never enabled merely by requesting a 1.3 device. The
  Vulkan Backend's overall minimum supported API version is **not** raised to
  1.3 as part of this Spec. A device with neither path → `createDevice()`
  returns an explicit `Result::Err` (a new `DeviceCreateError` variant), never
  a crash or an implicit `VkRenderPass`/`VkFramebuffer` fallback. This
  detection, the feature enablement, and the resulting choice of entry-point
  family are entirely the Vulkan Backend's own responsibility — no capability
  type or path indicator crosses into RHI's or RenderGraph's public surface,
  and no second graphics backend is designed or scaffolded
  ([ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)).

**RenderGraph multi-attachment and draw-pass execution integration**

- `render_graph::execute()`'s binding mechanism accepts a color `RenderTarget`
  binding and a depth `Texture` binding simultaneously. Guard 1 (every
  `ResourceState`-tagged usage must have a binding) applies uniformly to both;
  Guard 2 (no declared read usage on a bound resource) continues to apply only
  to the bound `RenderTarget`, not the bound depth `Texture`
  ([ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)).
- `execute()` recognizes a **draw pass** from a declared usage carrying
  `ColorAttachmentOutput` or `DepthAttachmentReadWrite` **and only these two
  states** — never `ColorAttachmentWrite`, so Spec 0006's existing
  `clearColor()` pass is structurally unaffected. `execute()` automatically
  brackets a recognized draw pass's execution callback with the
  attachment-scoping operation pair — a pass author never calls it directly.
  This Spec's scope needs exactly one draw pass per frame; `execute()`'s
  derivation rule is not required to support more than one this round.
- Transition-insertion (per-bound-resource "most-recently-recorded state"
  tracking, automatic `transitionResource()` insertion on a state change) is
  unchanged in mechanism, now running once per bound resource. **Every bound
  resource — including, newly, the depth `Texture` — is treated as entering
  each `execute()` call from `ResourceState::Undefined`**, extending (not
  modifying)
  [ADR-0019](../adr/0019-presentation-acquire-present-and-rendertarget-frame-borrow-contract.md)'s
  existing `RenderTarget`-specific rule; valid here because this Spec's single
  draw pass unconditionally clears both attachments via load-op every frame.
  The trailing `PresentSource` transition remains specific to the bound
  `RenderTarget`; no trailing transition for the bound depth `Texture` this
  round.

**Resize / depth-resource and attachment-format lifecycle**

- **Extent-only change:** the caller (this Spec's verification composition)
  checks, once per frame after a successful `acquireNextTarget()`, whether its
  owned depth `Texture`'s extent still matches the acquired `RenderTarget`'s
  extent; if not, it destroys and recreates the depth `Texture` at the new
  extent before calling `Renderer`'s per-frame entry point. `Pipeline` is
  untouched (dynamic viewport/scissor). `Renderer` itself has no resize-driven
  internal state.
- **Format change** (the swapchain's color format selection changes): the
  caller observes it by comparing `Presentation::metadata().format` against the
  value it last saw, at the same point each frame. On a change, the caller
  calls `Device::waitIdle()`, then destroys and recreates every
  format-dependent resource it owns — in this Spec's scope, `Material`'s
  `Pipeline` — before calling `Renderer` again. This is a distinct case from an
  extent-only change, not a superset: an extent-only change never requires
  `Pipeline` recreation, and a format change requires it regardless of whether
  the extent also changed. `Renderer` plays no role
  ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md),
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)).
- A zero-extent frame (from `acquireNextTarget()`'s existing `Ok(std::nullopt)`
  outcome) → the caller skips `Renderer`'s per-frame call entirely for that
  frame, exactly as Spec 0006's zero-extent handling already requires.

**Minimal material**

- Exactly one material this round: a single fixed vertex + fragment shader
  pair, taking the camera's view/projection and each draw item's
  object-to-world transform, and either a per-vertex color attribute or a
  single fixed solid color (exact choice left to the Plan) as the only visual
  differentiator — no lighting term, no texture sample, no normal, no material
  parameter beyond what is fixed at shader-authoring time. Sufficient to
  visually confirm a real, depth-tested 3D mesh is being drawn correctly
  (including correct depth ordering across its own front/back-facing geometry),
  and nothing more.

**Phase 1 single-threaded orchestration and thread-safety contracts**

- Every new public type (`Renderer`, `Mesh`, `Material`, `Buffer`, `Texture`,
  `Pipeline`, and every extended RHI/RenderGraph method) documents its
  thread-safety contract at its public API — "not thread-safe;
  caller-thread-only," on the single Phase 1 logical frame thread
  ([ADR-0004](../adr/0004-phase1-threading-baseline.md)). No mutex, atomic,
  job/task system, or lock-free structure is introduced anywhere in this Spec's
  scope.

### Non-functional

- **Performance:** "does not stall, leak, or busy-spin unnecessarily" — the
  same bar every prior spec in this line has set. Direct per-resource
  allocation and single-frame-in-flight are explicit simplifications, not
  performance claims.
- **Memory:** no general GPU memory suballocation strategy is introduced or
  assumed
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)).
- **Portability:** implemented and verified on Windows only; RHI's and
  RenderGraph's public interface shapes must not preclude Android's future
  implementation, verified by inspection.
- **Other:** no new third-party dependency — no shader compiler library, no
  allocator library. Unit tests use Catch2 v3.

## Proposed Design

### Module boundaries

Realizes exactly the dependency edges
[module_boundaries.md](../docs/architecture/module_boundaries.md) already
anticipated for Renderer (depends on RHI, RenderGraph, Core only) and extends
RHI/RenderGraph/Vulkan Backend along their existing, unchanged dependency
directions. The per-frame verification flow: `acquireNextTarget()` (skip on
`Ok(std::nullopt)`) → check depth `Texture` extent, recreate via
`createTexture()` if it differs → write this frame's camera view/projection
directly into the camera uniform `Buffer`'s mapped memory (caller-owned;
`Renderer` never touches raw camera matrices) → `createCommandList()` →
`Renderer::drawFrame(commandList, *renderTarget, *depthTexture, cameraBuffer,
drawItems)` (internally: one draw pass with `ColorAttachmentOutput` +
`DepthAttachmentReadWrite` usages, an execution callback that binds
`Mesh`/`Material`/`cameraBuffer` state, pushes each draw item's transform as a
push constant, and calls `drawIndexed()` once per draw item; compiles; calls
`render_graph::execute()`) → `submit()` → `present()`. On every exit path,
including a mid-frame exit: `Device::waitIdle()` before destroying
`Presentation`/`Device`/the depth `Texture`/`Mesh`/`Material`.

### RHI resource types, attachment scoping, pipeline/binding/draw, RenderGraph execution, shader sourcing

See
[ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
(`Buffer`/`Texture` shape, move-only ownership,
`createBuffer()`/`createTexture()`, direct/unpooled
Vulkan-Backend-private allocation resolving
[ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s named blocker),
[ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md) (Vulkan
core dynamic rendering, no `VkRenderPass`/`VkFramebuffer`, entirely private to
the Vulkan Backend's `CommandList` implementation),
[ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
(`Pipeline`'s shape, `CommandList`'s new bind/draw operations, the
`ResourceState` extension),
[ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)
(multi-resource binding, Guard 1/Guard 2's generalized/unchanged scope
respectively, draw-pass-derived attachment-scoping insertion), and
[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)
(pre-compiled checked-in SPIR-V, no compiler invocation, no reflection,
explicit migration boundary to a future Shader System) for each full decision.

### Threading and error handling

Unchanged from Spec 0006: single logical frame thread
([ADR-0004](../adr/0004-phase1-threading-baseline.md)); every new call happens
on that thread. Recoverable runtime errors (resource creation failure, pipeline
creation failure) use `atlantis::Result<T, E>`. Programmer errors — a
`bindVertexBuffer()`/`bindIndexBuffer()` call with a `Buffer` of the wrong
purpose; a draw-pass usage with no matching binding (Guard 1, generalized); a
`RenderTarget` binding with a declared read usage (Guard 2, unchanged scope) —
use `ATLANTIS_CHECK`/`ATLANTIS_ASSERT`
([ADR-0009](../adr/0009-assertion.md)).
`Mesh`/`Material`/`Buffer`/`Texture`/`Pipeline` misuse outside their valid
lifetime window is a **lifetime precondition violation**, the same tier as
every other borrowed/owned-handle misuse case in this codebase — not claimed
guaranteed-detectable, not tested for detection. Every `VkResult` along
resource creation, pipeline creation, binding, and drawing is checked. Vulkan
Validation Layers are enabled unconditionally in Debug builds and any
GPU-touching CI job; a validation warning or error is a build/test failure.

## Architectural Impact

Introduces architecture across six distinct, independently-reviewable
decisions, filed as six new ADRs — none decided by this Spec's prose. All
`Accepted` alongside this Spec:

| Decision | ADR |
|---|---|
| Minimal Renderer public API, module boundary, and resource ownership — `Renderer`'s concrete (non-interface) shape, its per-frame contract, and `Mesh`/`Material`'s explicit no-hidden-cache ownership | [ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md) |
| RHI minimal GPU resource types and allocation strategy — `Buffer`/`Texture`, move-only ownership (resolving [resource_lifetime.md](../docs/architecture/resource_lifetime.md)'s open question), and a direct/unpooled allocation policy (resolving [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md)'s named blocker) | [ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md) |
| Vulkan dynamic rendering for attachment management — a long-term Vulkan Backend implementation-strategy decision, explicitly reviewed even though it never crosses RHI's public surface | [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md) |
| RHI minimal graphics pipeline, binding, and draw command surface — `Pipeline`, the new `CommandList` bind/draw operations, and the `ResourceState` extension | [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md) |
| RenderGraph multi-attachment and draw-pass execution integration — generalizing [ADR-0021](../adr/0021-render-graph-rhi-execution-integration-and-barrier-responsibility.md)'s dependency-to-barrier split to a second bound-resource kind and to attachment-scoping derivation | [ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md) |
| Temporary pre-compiled SPIR-V shader artifact sourcing — an explicit, narrowly-bounded procedural decision preventing this Spec's own implementation pressure from silently deciding Shader System's shape | [ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md) |

No existing `Accepted` ADR's conclusions are restated, reopened, or modified.
Architectural Impact was not "None" — `Renderer`, `Buffer`, `Texture`,
`Pipeline`, and RenderGraph's multi-attachment execution capability are each a
new public API surface. This Spec's approval is not itself an authorization to
implement.

## Alternatives Considered

- **Split this Spec into two or more smaller specs** (e.g. "RHI graphics
  resources" separately from "Renderer + RenderGraph draw integration").
  Rejected — the six decisions are genuinely interdependent
  (`Buffer`/`Texture`'s shape has no real validation target without a
  `Pipeline` and a RenderGraph draw pass to use them), mirroring Spec 0006's
  reasoning. Filing six separate ADRs already gives Human Review the ability to
  accept, reject, or send back any one decision independently.
- **Fold shader artifact sourcing
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md))
  into the pipeline/binding ADR
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
  rather than filing it separately.** Rejected — the shader-sourcing question's
  purpose is to draw an explicit, auditable boundary against a future Shader
  System; bundling would make it harder to find and review on its own terms.
- **Decide GPU memory allocation strategy generally (VMA or hand-rolled) in
  this Spec.** Rejected
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)
  Alternatives) — this Spec's resource count does not create a concrete pooling
  need, and adopting either without one repeats the "scaffold for later"
  mistake [AGENTS.md](../AGENTS.md) and
  [ADR-0015](../adr/0015-vulkan-memory-allocation-deferred.md) warn against.
- **Design a general material/shader-parameter system now.** Rejected
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  Alternatives) — no second material exists in this Spec's acceptance target to
  validate a general system against.
- **Silently amend `specs/README.md`'s backlog or
  `docs/project-blueprint.md`'s Milestone 4 entry.** Rejected — governance/
  roadmap documents change only through their own review; a separate later
  docs-sync PR is expected.

## Testing & Verification Plan

- **Unit tests** (testing-strategy.md layer 1, no real device), against a
  fake/mock `CommandList`: RenderGraph's generalized transition-insertion
  algorithm across two simultaneously bound resources (color + depth), each
  resource's most-recently-recorded state tracked independently; `execute()`'s
  draw-pass recognition and attachment-scoping-call insertion (begin before the
  callback, end immediately after) for color-only and color+depth passes;
  draw-pass recognition **not** firing for a pass whose only attachment-shaped
  usage is `ColorAttachmentWrite` (Spec 0006's clear-pass shape) — confirming
  the new rule is scoped exactly to `ColorAttachmentOutput`/
  `DepthAttachmentReadWrite`; every bound resource treated as entering each
  `execute()` from `Undefined`, including on a second, otherwise-identical
  `execute()` against the same bound depth `Texture`; Guard 1 exercised against
  a depth `Texture` binding as well as a `RenderTarget` binding; Guard 2
  holding for a `RenderTarget` and *not* firing for an equivalent read usage on
  a bound depth `Texture`; `Buffer`/`Texture`/`Pipeline` construction-parameter
  validation logic not requiring a real device.
- **GPU integration tests (Windows/Vulkan)**, Validation-Layers-enabled,
  mirroring the existing `atlantis_vulkan_backend_gpu_tests`/
  `atlantis_render_graph_tests` pattern: creating and destroying a `Buffer` of
  each of the three purposes; a depth `Texture` including at a resized extent; a
  `Pipeline` from this Spec's fixed SPIR-V pair; one full draw-pass execution
  (bind, draw, attachment scope begin/end) against a real acquired
  `RenderTarget` and real depth `Texture`, Validation Layers reporting zero
  warnings/errors; and a frame with **more than one draw item**, each with a
  distinct object-to-world transform, confirming every draw item ends up at its
  own correct position (not all at the last item's position) — the concrete
  regression test for the push-constant-vs-shared-uniform-buffer correctness
  argument
  ([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)).
  `Pipeline` creation succeeding, drawing without a Validation Layer error, and
  the manually-observed correct visual output are, together, this round's only
  signal that the hand-specified vertex-input/binding layout matches the
  checked-in shader bytecode's own interface — no automated reflection-based
  cross-check exists this round
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).
  Also cover `createDevice()`'s dynamic-rendering capability detection on
  whichever path the test machine's actual hardware/driver provides
  ([ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)) — **this
  Spec's own test environment is expected to exercise exactly one of the two
  paths**; exercising the other and the explicit-error case remains verified by
  code inspection only where a second real device/driver cannot be obtained,
  stated explicitly in any verification report.
- **Headless integration tests:** not applicable — headless rendering remains
  unimplemented, per
  [testing-strategy.md](../docs/process/testing-strategy.md)'s sequencing note;
  flagged, not resolved.
- **Image regression tests:** not applicable — this Spec's manual verification
  checks for a visible, correctly-shaped, correctly-depth-ordered mesh by
  direct observation, not automated pixel comparison, gated on headless
  rendering per [AGENTS.md](../AGENTS.md) sequencing. This is a real, accepted
  limitation for this Spec's own claim of "correct output": a human visually
  confirming a mesh looks right is not equivalent to a pixel-exact regression
  gate, and this Spec does not claim otherwise.
- **Vulkan Validation Layers:** mandatory and must run clean for every manual
  and automated exercise of resource creation, pipeline creation, binding,
  attachment scoping, transition, drawing, submission, and present.
- **Manual verification:** a minimal, non-shipping composition (mirroring
  `examples/frame_execution_demo`) creates a Windows Platform window, constructs
  a `Device`/`Presentation`, this Spec's fixed
  `Mesh`/`Material`/camera uniform `Buffer`/depth `Texture`, and — driven by the
  existing non-blocking Platform event loop — runs the full acquire →
  recreate-depth-if-needed → update-camera → `Renderer::drawFrame()` → submit →
  present cycle every frame. It confirms: a visible window shows a recognizable,
  correctly-shaded, correctly depth-ordered 3D mesh (front-facing geometry
  occludes back-facing geometry correctly; no visible z-fighting or inverted
  depth test), continuously across repeated frames; interactive resize
  continues to show the mesh correctly (including depth correctness) at the new
  size, with the depth `Texture` visibly/measurably recreated at the matching
  extent — no stretched, corrupted, or stale depth buffer — and with `Pipeline`
  demonstrably *not* recreated for an extent-only change (e.g. by a
  log/assertion the composition emits, confirming the dynamic viewport/scissor
  path is actually exercised, not merely declared); format-change handling is
  exercised **if the test environment allows it** (e.g. a second monitor with
  different surface capabilities): `Presentation::metadata().format` changing is
  correctly detected, `Device::waitIdle()` is called, and `Material`'s
  `Pipeline` is recreated, with no Validation Layer warning or error across the
  transition — **if the environment has no second monitor/format to trigger
  this against, this case is verified by code inspection only** and the report
  must state explicitly which of the two (genuinely exercised vs. inspected-
  only) applied; minimizing the window results in no crash, no busy-spin, and
  no Vulkan call while minimized; restoring resumes correct rendering (mesh,
  depth, camera) with no special recovery step; the application exits cleanly
  at any point in this sequence, including mid-resize, minimized, and after a
  deliberate mid-frame exit (acquired but not yet submitted/presented), with no
  outstanding acquired `RenderTarget`, no leaked
  `CommandList`/`Buffer`/`Texture`/`Pipeline`, and no Validation Layer warning
  or error at any point, including at shutdown.

## Acceptance Criteria

- [ ] RHI's and RenderGraph's public headers contain no `Vk*` type and no
      `#include <vulkan/...>`.
- [ ] No direct `vkCmd*` call, no `VkImageMemoryBarrier`/`vkCmdPipelineBarrier`
      construction, and no `VkRenderPass`/`VkFramebuffer` object, exists
      anywhere outside the Vulkan Backend's `CommandList` implementation — the
      last clause is new this round
      ([ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md)).
- [ ] `createDevice()` selects between the core and extension dynamic-rendering
      paths correctly for whichever physical device is selected, explicitly
      queries and enables the `dynamicRendering` feature on whichever path
      applies, and returns an explicit `Result::Err` (never a crash, never an
      implicit render-pass/framebuffer fallback) when neither path is available.
- [ ] No Vulkan capability/feature-detection type, and no indication of which
      dynamic-rendering path a `Device` resolved to, appears in any RHI or
      RenderGraph public header.
- [ ] `Buffer`, `Texture`, and `Pipeline` are each move-only (movable,
      non-copyable) — a compile-time property.
- [ ] Every `Buffer`/`Texture` this Spec's implementation creates is backed by
      its own individual `vkAllocateMemory` call, released by its own
      individual `vkFreeMemory` call at destruction — no shared
      `VkDeviceMemory` block backs more than one resource anywhere.
- [ ] `Renderer` retains no `RenderTarget`, `Texture`, `Mesh`, `Material`, or
      any other GPU resource across two separate calls to its per-frame entry
      point — verifiable by inspection that `Renderer` holds no such member
      state.
- [ ] `Mesh`/`Material` are never created, cached, or looked up by `Renderer`
      itself anywhere — verifiable by inspection that `Renderer` has no such
      factory method or internal registry.
- [ ] A `ResourceState`-tagged usage against any bound resource kind (color
      `RenderTarget` or depth `Texture`) with no supplied binding is rejected
      as a programmer error at `execute()` time, in every tested case.
- [ ] Binding a `RenderTarget` to a logical resource with any declared read
      usage is rejected as a programmer error at `execute()` time, unchanged
      from Spec 0006; the equivalent declared read usage on a bound depth
      `Texture` is *not* rejected, in every tested case.
- [ ] `execute()` correctly brackets every recognized draw pass's execution
      callback with attachment-scoping begin/end calls, and never inserts one
      for a pass with no attachment-shaped usage.
- [ ] `execute()`'s draw-pass recognition never triggers on a
      `ColorAttachmentWrite`-tagged usage — Spec 0006's existing
      `examples/frame_execution_demo` clear pass continues to compile and
      execute with no attachment-scoping call inserted around it.
- [ ] The depth `Texture`'s combined read/write usage is declared as exactly
      one `writes()` call tagged `DepthAttachmentReadWrite` anywhere this Spec's
      implementation declares it — no pass anywhere declares both a `reads()`
      and a `writes()` usage against the same logical resource (unchanged,
      pre-existing
      [ADR-0018](../adr/0018-render-graph-dependency-derivation-and-ordering.md)
      rule, not reopened).
- [ ] The per-draw-item object-to-world transform is recorded as a Vulkan push
      constant in every draw — no second uniform buffer is written more than
      once per frame for this purpose.
- [ ] `Pipeline` objects use dynamic viewport/scissor state — no pipeline is
      recreated solely because the window was resized.
- [ ] No shader compiler, and no SPIR-V reflection code, is invoked by any
      CMake target or any Atlantis Core/RHI/RenderGraph/Renderer/Tools source
      file this Spec's implementation adds.
- [ ] Every checked-in `.spv` file this Spec adds has a corresponding
      checked-in, human-readable shader source file and a plain-text note of
      the compiler/version used to produce it.
- [ ] Every `VkResult` along resource creation, pipeline creation, binding,
      attachment scoping, drawing, submission, and present is checked; no
      `VkResult` is discarded.
- [ ] Debug builds and any GPU-touching CI job run with Vulkan Validation
      Layers enabled; a validation warning or error fails the run.
- [ ] The manual verification composition shows a visible, correctly-shaded,
      correctly depth-ordered mesh; continues across interactive resize
      including a correctly-recreated depth `Texture`; makes zero Vulkan calls
      while minimized; and resumes correctly on restore.
- [ ] An extent-only recreation (ordinary interactive resize) never recreates
      `Pipeline` anywhere — only the depth `Texture` is recreated.
- [ ] A format change is detected by comparing `Presentation::metadata().format`
      against the caller's last-seen value (no new RHI query introduced for
      this purpose), and results in `Device::waitIdle()` being called before
      every format-dependent resource the caller owns (`Material`'s `Pipeline`)
      is destroyed and recreated — verifiable by inspection of the verification
      composition, and, if the environment allows genuinely triggering a format
      change, by observing correct, Validation-Layers-clean behavior across it;
      if the environment cannot, this remains verified by inspection/code
      review only and that limitation is reported as such.
- [ ] `Renderer` contains no code path that reads `Presentation::metadata()`,
      compares a format, or recreates `Pipeline`/`Material`/`Texture` — the
      format-change contract is entirely caller-side, verifiable by inspection.
- [ ] No `src/renderer/` code depends on Atlantis Platform, Win32, the Android
      NDK, or any `Vk*` type.
- [ ] No scene graph, ECS, asset system, model loader, texture, lighting term,
      second material, instanced/indirect draw, or multi-frame-in-flight
      machinery is implemented anywhere this Spec's implementation touches.
- [x] All six ADRs
      ([ADR-0022](../adr/0022-minimal-renderer-public-api-and-resource-ownership.md)–[ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md))
      reach `Accepted` before this Spec is marked `Approved` — satisfied
      2026-08-11; this checkbox gated Spec approval, not implementation.

## Risks & Open Questions

- **Exact vertex-input attribute set** (position + one additional attribute,
  e.g. per-vertex color, vs. position + normal for a simple lighting-adjacent
  check) — left to the Plan; this Spec fixes only that the material must
  visually distinguish geometry and confirm correct depth ordering.
- **Exact mesh content** (a hand-authored cube, a low-poly sample mesh, an
  equivalent fixed shape) — left to the Plan, provided it is non-planar enough
  to genuinely exercise depth testing.
- **Exact struct-level `ResourceState` naming/spelling** for
  `ColorAttachmentOutput`/`DepthAttachmentReadWrite` and the buffer-purpose
  bookkeeping states — left to the Plan; this Spec and
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  fix the semantics and each state's distinctness from `ColorAttachmentWrite`,
  not the exact enumerator spelling.
- ~~Whether Vulkan Backend raises its minimum core API version to 1.3, or
  requires `VK_KHR_dynamic_rendering` on an older version~~ — **resolved by
  Human Review (2026-08-11): neither, exclusively — a capability-detected dual
  path is adopted instead, and the overall minimum version is not raised.** See
  [ADR-0024](../adr/0024-vulkan-dynamic-rendering-for-attachments.md).
- ~~`Pipeline` attachment-format staleness across a swapchain format change~~ —
  **resolved by Human Review (2026-08-11): a concrete, caller-owned contract,
  not an open risk.** See the Resize / format-change Requirements subsection and
  [ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md).
- **Exact checked-in `.spv` file location and naming convention** — left to the
  Plan
  ([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)).
- **Whether a GPU-integration test category distinct from the existing
  `gpu`-labeled pattern is needed** for resource/pipeline creation tests that
  need a real device but do not fit
  [testing-strategy.md](../docs/process/testing-strategy.md)'s existing layer
  boundaries — the same open question Spec 0006 flagged, now recurring for this
  Spec's own new GPU-dependent test surface; flagged, not resolved.
- Whether a future spec revisiting the single-frame-in-flight baseline will
  also need to revisit this Spec's direct-write-to-uniform-buffer approach —
  left open; the current design relies on single-frame-in-flight's existing
  acquire-time drain guarantee.
- Whether the direct, unpooled per-resource allocation policy
  ([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md))
  will need revisiting sooner than expected if a future spec's resource count
  approaches a driver's `maxMemoryAllocationCount` limit — left open per that
  ADR's own stated migration boundary.

## Out of Scope / Future Work

Shader System (next backlog candidate after this Spec), Android Platform and
Vulkan presentation, headless rendering, and image regression testing all
remain later, separately-specced work per
[docs/project-blueprint.md](../docs/project-blueprint.md), not advanced by this
Spec beyond satisfying this minimal-renderer foundation as their own future
dependency. A future Shader System spec is expected to be the first consumer
that needs `Device::createPipeline()`'s bytecode-plus-layout contract fed by
real compilation and reflection rather than this Spec's hand-authored,
checked-in `.spv` files
([ADR-0027](../adr/0027-temporary-precompiled-spirv-shader-artifacts.md)). A
future spec introducing a second material, a texture, lighting, or multiple
draw passes is expected to need to extend — not merely reuse unchanged — this
Spec's binding mechanism
([ADR-0025](../adr/0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md))
and RenderGraph draw-pass derivation rule
([ADR-0026](../adr/0026-render-graph-multi-attachment-draw-pass-integration.md)).
A future performance-motivated spec may revisit both the
single-frame-in-flight baseline and this Spec's direct/unpooled GPU memory
allocation policy
([ADR-0023](../adr/0023-rhi-minimal-gpu-resource-types-and-allocation.md)). A
future asset-system spec is expected to be the first real consumer that needs
cross-owner shared ownership of `Mesh`/`Material`/GPU resources, which this Spec
deliberately does not design.
