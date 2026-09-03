# ADR 0071: Visible Sky Background Rendering Integration

- **Status:** Accepted
- **Date:** 2026-09-03
- **Deciders:** slmao — Human Review, approved 2026-09-04
- **Related Spec:** [specs/0026-visible-sky-foundation.md](../specs/0026-visible-sky-foundation.md) (`Approved`)
- **Acceptance Record (2026-09-04):** Accepted by Human Review as part of
  Spec 0026's approval against [PR #120](https://github.com/slmao/Atlantis/pull/120).
  Does not change this ADR's own Decision, Consequences, or Alternatives
  Considered above.

## Context

Spec 0025 added a frame-scoped `EnvironmentLighting` view and realized a
prefiltered HDR cubemap (`EnvironmentLighting::prefilteredEnvironment`,
mip 0 is the directly-resampled, un-blurred environment per Plan 0025 P1),
but deliberately left the background a flat clear color
(`kBackgroundClearColor`, `src/renderer/src/renderer.cpp`) — "a visible
skybox is intentionally separate from lighting" (Spec 0025 Proposed
Design). Spec 0026 closes that gap: when an environment is configured, the
un-blurred base cubemap should be visible behind scene geometry instead of
a flat color.

`Renderer::drawFrame()` already runs one RenderGraph "draw" pass (writes
`HdrColorTarget` + depth, iterates `DrawItem`s) followed by one
"output_transform" pass (ADR-0068 D-3). Every geometry `Pipeline` targets
the fixed `HdrFormat::Rgba16Float` intermediate (ADR-0068 D-4) and uses
`PipelineCreateParams`'s existing `hasCameraUniformBinding`/
`hasDepthAttachment`/`sampledTextureBindingCount` (0/1/3, ADR-0070 P2)
contract. `hasDepthAttachment` is currently all-or-nothing: `true` enables
both depth test and depth write with a fixed `VK_COMPARE_OP_LESS`
(`vulkan_device.cpp:1187-1189`); `false` disables both. Every geometry
Pipeline in a frame shares one `vkCmdBeginRendering` scope with one bound
depth attachment (the "draw" pass), so a sky draw sharing that pass must
either match that attachment's format or introduce a second pass.

A sky background must render only where no scene geometry covers it — the
opaque `DrawItem` draws must win depth-wise wherever they exist — and must
track camera rotation but not camera translation, so it reads as
infinitely distant.

## Decision

**One additive `PipelineCreateParams` field: `depthWriteEnabled` (bool,
default `true`), meaningful only when `hasDepthAttachment == true`.**
Every existing Pipeline keeps `hasDepthAttachment`'s current fixed
behavior unchanged (default `true` reproduces today's always-test-and-write
semantics exactly). The sky Pipeline sets `hasDepthAttachment = true`
(same fixed depth format as every other geometry Pipeline in the "draw"
pass — no `VkPipelineRenderingCreateInfo` mismatch, no second pass) and
`depthWriteEnabled = false`.

**Sky draws first, inside the existing "draw" pass, before the `DrawItem`
loop — no new RenderGraph pass or resource.** Its vertex shader outputs a
fixed device-space depth just under the far plane (a literal constant,
exact value fixed at Plan time) so its own depth test (`VK_COMPARE_OP_LESS`,
unchanged) passes against the pass's `depthClear = 1.0f` wherever nothing
has drawn yet. Because `depthWriteEnabled = false`, the sky never writes
depth: the buffer stays at the `1.0f` clear value everywhere sky drew,
so every subsequent opaque `DrawItem` depth-tests and depth-writes exactly
as it does today, unaffected by whether sky ran first. Wherever an opaque
`DrawItem` covers a pixel, its own nearer depth already overwrote the
region before or after sky drew — draw order between sky and opaque
geometry does not matter for correctness once sky never writes depth, only
that sky's own depth test still passes against the `1.0f` clear the first
time a pixel is touched, which the fixed literal guarantees.

**Sky reuses the existing `EnvironmentLighting::prefilteredEnvironment`
cubemap and `environmentSampler` at an explicit mip-0 sample** — no new
GPU resource, artifact, or Runtime realization step. Its Pipeline follows
ADR-0070's existing closed contract: `hasCameraUniformBinding = true`
(frame uniform at binding 0, for view/projection only — SH/lighting
fields are declared and read by no sky shader, matching Spec 0025 P3's
"shaders only declare/read their current prefix" convention),
`sampledTextureBindingCount = 1` (the cubemap, contiguous at binding 1).
`colorFormat` is the fixed `HdrFormat::Rgba16Float`, matching every other
geometry Pipeline (ADR-0068 D-4) — sky writes scene-linear HDR before the
shared output-transform pass, never its own tone-mapping/encode.

**Camera translation is ignored by shader-only math: the sky fragment
shader reconstructs its per-pixel view ray from screen position and the
existing view/projection matrices' rotation-only (upper-left 3×3) part.**
No new camera-buffer field, no new binding, no `World`/`Scene` change.

**`Renderer::drawFrame()` gains one new, caller-owned, nullable parameter:
`const atlantis::rhi::Pipeline* skyPipeline = nullptr`.** Sky draws only
when both `skyPipeline` and `environmentLighting` are non-null (an
`ATLANTIS_CHECK_MSG` programmer-error guard covers the mismatched case,
mirroring the existing `MaterialEnvironmentBinding::Ibl`-without-
`environmentLighting` guard). `skyPipeline == nullptr` reproduces today's
flat-clear-color background exactly — every no-environment scene and
golden is unaffected by construction. Runtime creates `skyPipeline` once,
alongside `EnvironmentLightingResources`, only when `BootstrapConfig`
names an environment — the same existing trigger Spec 0025 already uses
to select the `pbr_ibl` shader, no new lifecycle concept.

## Consequences

### Positive

- No new RenderGraph pass, resource, or `CommandList` method — sky is an
  additional draw call inside the existing "draw" pass's execute
  callback.
- No new GPU resource: reuses the already-realized environment cubemap,
  sampler, and the existing fullscreen-triangle vertex/index buffers
  (created once at startup for the output-transform pass, Plan 0024 M5).
- The one new RHI field is additive and defaults to today's exact
  behavior for every existing Pipeline — zero change to any non-sky
  Pipeline's depth semantics.
- `skyPipeline == nullptr` is the total behavior for every existing
  no-environment scene — all seven current goldens remain byte-identical
  by construction.

### Negative / Trade-offs

- A real, disclosed `Renderer::drawFrame()` public-API change — the third
  since ADR-0022 (following ADR-0068's `HdrColorTarget`/output-transform
  parameters and ADR-0070's `environmentLighting`), consistent with this
  codebase's own precedent of small, disclosed additive changes to this
  signature.
- The existing `ibl_material_demo` golden (Spec 0025) will need a
  human-reviewed re-capture once implemented: its background changes from
  the flat clear color to a visible sky. No other existing golden is
  affected.
- `depthWriteEnabled = false` combined with a fixed near-far-plane sky
  depth is a real, if standard, Vulkan technique that must be confirmed
  clean under Validation Layers at Implementation time, like every other
  new Pipeline configuration in this codebase.

## Alternatives Considered

- **A separate "sky" RenderGraph pass, before "draw", with no depth
  attachment at all.** Rejected — it would need the "draw" pass's own
  color-attachment load op changed from clear to load (to avoid erasing
  the sky), a real widening of `ResourceBinding`/`execute()` this
  narrower, same-pass integration avoids entirely.
- **A `depthCompareOp` override instead of a `depthWriteEnabled` toggle**
  (e.g., render sky exactly at the far plane with `LESS_OR_EQUAL`).
  Rejected — it would require the sky Pipeline's own `VkCompareOp` to
  differ from every other geometry Pipeline's fixed `LESS`, a second new
  field for one narrower benefit than a plain write-disable already gives.
- **Sampling the environment cubemap through a new, separate SampledTexture
  handle rather than reusing `EnvironmentLighting`'s existing one.**
  Rejected — duplicates a GPU resource Runtime already owns and keeps
  alive for the same frame; the existing borrowed view already exposes
  exactly what sky needs.
- **A per-material or per-`DrawItem` sky flag.** Rejected — the sky is a
  frame-level background, not scene content; it has no `Mesh`, no
  `Transform`, and no relationship to `World`, matching Spec 0025's own
  "environment is frame context, not material identity" precedent
  (ADR-0070).

## Proposed Correction — 2026-09-04 (draw-order claim)

**Status:** Proposed, pending Human Review. Does not rewrite the Decision
or Consequences sections above; supersedes only the draw-order claim
below, which does not hold.

The Decision section's own closing sentence — "draw order between sky
and opaque geometry does not matter for correctness once sky never writes
depth" — is incorrect and must not be relied upon. If an opaque `DrawItem`
draws **before** the sky (writing a real depth strictly between
`kSkyClipDepth` and the `1.0f` clear at its own covered pixels), the sky's
own depth test (`VK_COMPARE_OP_LESS`, comparing `kSkyClipDepth` against
that already-written, larger depth value) still **passes** — `kSkyClipDepth
< depthAlreadyWritten` — and the sky overwrites that geometry's own color,
`depthWriteEnabled = false` notwithstanding (it protects only the depth
buffer, never the color attachment). `depthWriteEnabled = false` alone
does not make draw order irrelevant; it only prevents the sky from
corrupting the depth buffer for whatever draws after it.

**Corrected requirement: the sky must draw strictly before every
`DrawItem`, every frame, with no exception.** This does not change Spec
0026's own already-approved sky-first design (Proposed Design: "sky
draws first, inside the existing 'draw' pass... before every DrawItem") —
it corrects only this ADR's own, separate claim that the ordering was
merely a convenience rather than a correctness requirement. `Renderer::
drawFrame()`'s own "draw" pass execute lambda must therefore issue the
sky draw call before entering its `for (const DrawItem& item : drawItems)`
loop unconditionally, not as an implementation detail free to move.
