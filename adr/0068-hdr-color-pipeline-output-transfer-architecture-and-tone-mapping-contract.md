# ADR 0068: HDR Color Pipeline & Output Transfer Architecture and Tone-Mapping Contract

- **Status:** Proposed
- **Date:** 2026-08-31
- **Deciders:** slmao (repository maintainer)
- **Related Spec:** [specs/0024-hdr-color-pipeline-output-transfer-foundation.md](../specs/0024-hdr-color-pipeline-output-transfer-foundation.md)

## Context

Every `MaterialKind` shipped so far (`UnlitTextured`, `LitTextured`,
`PbrDirectLit`) writes its fragment shader's final color straight to
`SV_Target`, which is bound to whatever `RenderTarget` the caller
handed `Renderer::drawFrame()` — a swapchain image (windowed) or an
`OffscreenTarget`'s color image (headless), both restricted to
`atlantis::rhi::Format`'s four 8-bit variants
(`Bgra8Unorm`/`Bgra8Srgb`/`Rgba8Unorm`/`Rgba8Srgb`,
`src/rhi/include/atlantis/rhi/types.h:26-32`). `LitTextured` and
`PbrDirectLit` each apply exactly one `clamp(color, 0, 1)` after
accumulating every light and nothing else — no tone-mapping, no
gamma-encode
(`shaders/lit_textured/lit_textured.slang:105-107`,
`shaders/pbr_direct_lit/pbr_direct_lit.slang:179-183`,
[ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s
own Decision: *"the final clamp above is the only transformation
applied before the display format's own write"*). `UnlitTextured`
does not even clamp — it returns a sampled texture value unmodified
(`shaders/textured_quad/textured_quad.slang:52-55`).

Two real, previously-undisclosed consequences follow directly from
this, confirmed by reading the actual negotiation and fixture code,
not assumed:

1. **`VulkanPresentation::selectSurfaceFormat()`'s own fixed preference
   order tries `Bgra8Unorm` first**
   (`src/vulkan_backend/src/vulkan_presentation.cpp:111-116`), and
   every image-regression fixture explicitly hardcodes
   `Format::Rgba8Unorm`
   (`tests/image_regression/fixture/minimal_cube_fixture.h:40`,
   `.../textured_quad_fixture.h:63`). None of the six existing goldens
   (`minimal_cube`, `world_scene`, `textured_quad`, `material_demo`,
   `lighting_demo`, `pbr_material_demo`) were captured through an
   `_Srgb`-format target, so the GPU's fixed-function output-merger
   never performed an implicit linear→sRGB encode for any of them
   either. Every shipped pixel today is, strictly, a linear value
   written into a byte buffer a display then decodes as if it were
   sRGB-encoded — a real, silent transfer-function mismatch, present
   before this ADR and not introduced by it.
2. **`atlantis::renderer::Renderer` is a stateless orchestrator by its
   own class contract** — *"retains no GPU resource, no frame-to-frame
   state, across calls"*
   ([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)).
   `Renderer::drawFrame()` cannot allocate and privately own a new
   intermediate GPU resource internally; any new resource this ADR
   introduces must be caller-owned and passed in, exactly like
   `depthTarget`/`cameraUniformBuffer` already are
   (`src/renderer/include/atlantis/renderer/renderer.h:47-49`).

Separately, `RenderTarget`'s own class contract states plainly:
*"Write-only this round: no method here (or on `CommandList`) ever
reads its prior contents"*
(`src/rhi/include/atlantis/rhi/render_target.h:13-14`), and
`render_graph::execute()`'s own Guard 2 enforces this at the graph
level — *"no bound `RenderTarget` may have any declared read usage
anywhere in graph"*
(`src/render_graph/include/atlantis/render_graph/execution.h:103-107`).
No existing RHI resource type is both a renderable color-attachment
target and a later-sampled shader input in the same frame: `Texture`
is depth-only; `SampledTexture` is upload-only (`Undefined` →
`TransferDestination` → `ShaderRead`, never a render-pass color
attachment,
`src/render_graph/include/atlantis/render_graph/execution.h:25-30`).
Spec 0023's own ADR-0067 D-6 named the correct next step explicitly:
*"a dedicated Output Transfer Function Spec is the correct next
step — never a local, `PbrDirectLit`-only patch."* This ADR is that
Spec's own architectural decision record.

`CommandList`'s only draw call is `drawIndexed()`
(`src/rhi/include/atlantis/rhi/command_list.h:77`) — there is no
non-indexed `draw()` call a shader-generated fullscreen triangle
(`SV_VertexID`-driven, no vertex/index buffer) could use without a new
RHI method.

## Decision

### D-1. A new, single-purpose RHI resource type — `HdrColorTarget`

A new resource type, distinct from `RenderTarget`/`Texture`/
`SampledTexture`, matching this codebase's own established "one
purpose-built type per resource shape" precedent rather than relaxing
`RenderTarget`'s existing write-only contract (Guard 2 stays exactly
as it is — it never applies to the new type, because the new type is
not a `RenderTarget`). `HdrColorTarget` owns one GPU image created
with both `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT` and
`VK_IMAGE_USAGE_SAMPLED_BIT`, and exposes two narrow capabilities: bind
as a color-attachment-output target (mirroring `RenderTarget`) for the
geometry pass, and bind as a combined-image-sampler input (mirroring
`SampledTexture`) for the new output-transform pass. Vended by a new
`Device::createHdrColorTarget(HdrColorTargetCreateParams)` factory
method, mirroring `createOffscreenTarget()`'s/`createSampledTexture()`'s
own existing shape.

### D-2. Format — a new, single-variant `HdrFormat` enum

One value this round, `HdrFormat::Rgba16Float`
(`VK_FORMAT_R16G16B16A16_SFLOAT`), mirroring `DepthFormat`'s own
established "one variant, no capability query needed" precedent
(`src/rhi/include/atlantis/rhi/types.h:71-74`) — this format carries
the same Vulkan 1.0 core mandatory-format-support guarantee for
`VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT`,
`VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT`, and
`VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT` with optimal tiling
that `DepthFormat::D32Sfloat` already relies on for depth. A future
spec may extend this enum (a second HDR format, e.g. for a memory/
bandwidth trade-off) the same way a future `DepthFormat` extension
would — never by replacing the enum with a non-enum representation.

### D-3. RenderGraph/Renderer integration — two passes, both windowed and offscreen share them unconditionally

`Renderer::drawFrame()`'s own internal single-pass `RenderGraphBuilder`
becomes two passes against the same builder: an unchanged "draw" pass
(every existing `MaterialKind` dispatch, `DrawItem` loop, and Pipeline
binding logic untouched) writing `ColorAttachmentOutput` into the new
`HdrColorTarget` instead of the caller's final `RenderTarget`; and a
new "output-transform" pass reading that `HdrColorTarget`
(`ShaderRead`) and writing `ColorAttachmentOutput` into the caller's
final `RenderTarget` (swapchain-backed or `OffscreenTarget`-backed —
no branch on which). This directly continues
[AGENTS.md](../AGENTS.md)'s own architecture principle, *"windowed and
headless rendering share the same Renderer/RHI stack"* — the
output-transform pass is caller-origin-agnostic by construction, the
same way the existing draw pass already is. `Renderer::drawFrame()`'s
own public signature gains one new required parameter, the caller-
owned `HdrColorTarget&` (D-1's stateless-orchestrator constraint) — a
real, disclosed public API change, the first to this signature since
[ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)/its
own [Accepted Amendment](0022-minimal-renderer-public-api-and-resource-ownership.md).
`ResourceBinding` (`render_graph/execution.h`) gains one new binding-
shape field for the `HdrColorTarget`, alongside its existing
`target`/`depthTexture`/`sampledTexture` trio — Guard 0's "exactly one
non-null" rule widens to this new quartet.

The output-transform pass draws one fullscreen triangle via a tiny,
checked-in, non-shared 3-vertex mesh reused through the existing
`bindVertexBuffer()`/`bindIndexBuffer()`/`drawIndexed()` path — no new
`CommandList` draw method, keeping `CommandList`'s public surface
unchanged.

### D-4. Every Pipeline's geometry-pass `colorFormat` becomes the fixed HDR format

`PipelineCreateParams::colorFormat` for every `MaterialKind`'s Pipeline
(`UnlitTextured`/`LitTextured`/`PbrDirectLit` alike) now targets
`HdrFormat::Rgba16Float` unconditionally, never the final target's own
negotiated swapchain/offscreen format. This is a real simplification,
not merely a substitution: the existing format-change Pipeline-rebuild
mechanism (Spec 0018/Spec 0021) no longer applies to any geometry-pass
Pipeline, since the HDR intermediate's format never changes — only the
new output-transform pass's own Pipeline varies by the final target's
format, mirroring today's exact rebuild trigger, narrowed to one
Pipeline instead of N.

### D-5. Tone-mapping — fixed-exposure Reinhard, one literal formula

```
exposedColor = linearHdrColor * kBaselineExposure   // kBaselineExposure = 1.0, a named constant, no auto-exposure
tonemapped   = exposedColor / (float3(1,1,1) + exposedColor)   // per-channel Reinhard
```

Chosen for being a single, literal, hand-verifiable rational function
(matching this codebase's own "literal, complete formula, directly
unit-testable against hand-computed values" precedent,
[ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s
own Consequences) — never an unstated "some ACES-like curve." No
auto-exposure, no luminance histogram, no eye-adaptation state; a
future spec may add one, replacing `kBaselineExposure`'s fixed value
with a computed one without changing this Decision's own curve.

### D-6. Linear-to-display transfer — manual sRGB OETF, written to a `*_Unorm` target only

The output-transform shader applies the exact piecewise sRGB transfer
function (not a `pow(x, 1/2.2)` approximation) to `tonemapped` and
writes the encoded byte value to a `*_Unorm`-format final `RenderTarget`
— never an `*_Srgb`-format one. This is a deliberate, single-encode-
path choice: relying on an `*_Srgb` target's own automatic hardware
encode instead would work only when the negotiated swapchain format
happens to be `*_Srgb` (real code shows `*_Unorm` is `selectSurfaceFormat()`'s
own first preference, Context above) and would silently double-encode
if a caller ever bound an `*_Srgb` target while this shader also
encoded manually — picking exactly one mechanism and forbidding the
other closes that class of bug outright, at the cost of `Renderer`
(or its caller) rejecting/normalizing a `*_Srgb` final target — Human
Review Decision 4 in the Spec covers the exact rejection/normalization
mechanism.

### D-7. Existing shaders' own final `clamp(..., 0, 1)` is removed

`lit_textured.slang:107`'s and `pbr_direct_lit.slang:183`'s own
pre-HDR final clamp is removed — clamping linear radiance to `[0,1]`
*before* it reaches the tonemap operator defeats the entire purpose of
an HDR intermediate (a bright highlight above `1.0` must survive to
reach D-5's own curve, not be pre-clipped away). This is the one real
content change to existing, already-`Approved` shader files this ADR
requires — disclosed here explicitly, unlike every prior Spec building
on `ADR-0062`'s "final clamp" contract, which could truthfully claim
zero existing `.slang` file was modified. `UnlitTextured`'s own
`textured_quad.slang` needs no change (it never clamped).

### D-8. Resource-state transitions and resize

The `HdrColorTarget` transitions `Undefined` → `ColorAttachmentOutput`
(geometry pass) → `ShaderRead` (output-transform pass) every frame,
structurally mirroring `SampledTexture`'s existing
`Undefined`/`TransferDestination`/`ShaderRead` sequence — no new
`ResourceState` enumerator. It is recreated on extent change, in the
same resize branch that already recreates the depth `Texture`
(`src/runtime/src/runtime_application.cpp:510-516`) — same trigger,
same lifecycle, Pipeline untouched (dynamic viewport/scissor,
unaffected).

### D-9. Readback and the image-regression harness stay 8-bit LDR, unmodified in shape

`CommandList::copyRenderTargetToBuffer()` continues to operate only on
the final, post-output-transform `RenderTarget` — the `HdrColorTarget`
is never read back directly, and the image-regression harness's own
`stbi_load`/`stbi_write_png` 8-bit-per-channel pipeline
(`tests/image_regression/support/png_codec.cpp:37,72`) needs no format
widening. The comparison methodology itself
([ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md),
zero channel tolerance, zero failing-pixel budget) is unchanged.

### D-10. Portability

`VK_FORMAT_R16G16B16A16_SFLOAT`'s mandatory-format-support guarantee
(D-2) holds identically on Windows and future Android Vulkan
implementations — no capability query, no fallback format. No
`VK_EXT_hdr_metadata`, `VK_COLOR_SPACE_HDR10_ST2084_EXT`, or any other
literal-HDR-*display* WSI extension is introduced anywhere — this
ADR's own "HDR" is strictly an internal, scene-referred linear
intermediate; the swapchain's own negotiated (format, color space)
pair (`vulkan_presentation.cpp:111-116`) and Vulkan Backend's existing
WSI boundary are untouched.

## Consequences

### Positive

- Closes ADR-0062 D-6/ADR-0067 D-6's own disclosed Phase 1 limitation
  ("a bright specular highlight ... can hard-clip rather than roll
  off") for all three `MaterialKind`s uniformly, via one shared pass —
  never a per-`MaterialKind` patch.
- `Renderer`'s existing "stateless orchestrator" contract
  ([ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md))
  is preserved exactly — the new resource is caller-owned, matching
  `depthTarget`'s own existing pattern, not a new exception to it.
- The format-change Pipeline-rebuild mechanism (Spec 0018/0021)
  narrows from N geometry Pipelines to exactly one (the
  output-transform Pipeline) — a real simplification, not merely a
  relocation of complexity.
- A single, literal, hand-verifiable tone-mapping formula (D-5) and
  transfer function (D-6), directly unit-testable — matching this
  codebase's own established math-contract precedent.
- No new Vulkan WSI surface, extension, or platform-specific code —
  Vulkan Backend's existing WSI boundary is untouched (D-10).

### Negative / Trade-offs

- A real, disclosed public API change to `Renderer::drawFrame()`'s own
  signature — every existing caller (Runtime, every image-regression
  fixture) must be updated to create and pass the new `HdrColorTarget`.
- A real, disclosed content change to two already-`Approved` shader
  files (`lit_textured.slang`, `pbr_direct_lit.slang`) — their own
  final clamp is removed (D-7) — the first Spec unable to claim "zero
  `.slang` file modified" against this codebase's prior three
  `MaterialKind`s.
- Per ADR-0042's own zero-tolerance comparison policy, introducing a
  real sRGB OETF encode where none existed before will shift byte
  values for every one of the six existing goldens, not only
  HDR-range content — a mandatory, human-reviewed re-capture of all
  six is required at Plan/Implementation time, disclosed here so it is
  never treated as a surprise regression.
- One new RHI resource type, one new `Format`-shaped enum, and one new
  `ResourceBinding` field are real, permanent additions to RHI's
  public surface — reviewed and accepted here, not incidental.

## Alternatives Considered

- **Reuse `SampledTexture`, widened to also support
  `ColorAttachmentOutput`.** Rejected — conflates two purpose-built
  types with deliberately distinct contracts today (upload-once vs.
  render-target); a new, single-purpose type keeps both contracts
  intact and matches this codebase's own "one purpose-built type per
  resource shape" precedent.
- **Relax `RenderTarget`'s write-only contract / Guard 2** instead of
  introducing a new type. Rejected — `RenderTarget`'s write-only
  guarantee is depended on elsewhere (Guard 2's own doc comment); a new
  type sidesteps the guarantee entirely rather than narrowing an
  existing, relied-upon one.
- **ACES filmic (Narkowicz 2015 fit) instead of Reinhard.** A more
  "industry-standard" filmic look, but a degree-3 rational polynomial
  approximation — harder to hand-verify exactly and more prone to a
  byte-level regression going undetected than D-5's own single
  rational function. Registered as a named future candidate operator,
  not blocking this ADR.
- **Rely on an `*_Srgb`-format final `RenderTarget` for the OETF encode
  instead of a manual one (D-6).** Rejected — `selectSurfaceFormat()`'s
  own real preference order picks `*_Unorm` first, and a manual-vs-
  automatic encode choice that depends on which of four approved
  formats happens to be negotiated is a real, latent double-encode/
  no-encode bug waiting to happen; picking exactly one mechanism closes
  it structurally.
- **Fork the output-transform pass — real tone-mapping only for a new,
  opt-in HDR scene, existing goldens frozen byte-identical forever.**
  Rejected — the existing zero-encode behavior is a real, disclosed
  defect (Context), not a feature worth preserving permanently, and a
  fork would break ADR-0062/ADR-0067's own "all three `MaterialKind`s
  share one output contract" principle.
- **A new non-indexed `CommandList::draw()` method for the fullscreen
  triangle**, instead of a tiny checked-in 3-vertex mesh reusing
  `drawIndexed()`. Rejected for this round — a new RHI draw-call
  surface is a larger, separate API decision than this ADR's own scope
  needs; the existing indexed path already suffices for one triangle.
