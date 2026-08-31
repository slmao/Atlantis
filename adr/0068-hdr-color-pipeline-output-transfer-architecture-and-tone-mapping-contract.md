# ADR 0068: HDR Color Pipeline & Output Transfer Architecture and Tone-Mapping Contract

- **Status:** Accepted
- **Date:** 2026-08-31
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-31 as part of
  [specs/0024-hdr-color-pipeline-output-transfer-foundation.md](../specs/0024-hdr-color-pipeline-output-transfer-foundation.md)'s
  own Human Review Approval.
- **Related Spec:** [specs/0024-hdr-color-pipeline-output-transfer-foundation.md](../specs/0024-hdr-color-pipeline-output-transfer-foundation.md)
  (`Approved`)
- **Acceptance Record (2026-08-31):** Accepted by Human Review as part
  of Spec 0024's own Human Review Approval (2026-08-31, commit
  `9771f94`). Does not change this ADR's own Decision, Consequences, or
  Alternatives Considered above.

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

### D-1. A new, single-purpose RHI resource type — `HdrColorTarget` — its full lifecycle and Vulkan implementation boundary

A new resource type, distinct from `RenderTarget`/`Texture`/
`SampledTexture` — **inheriting from neither** (see the note at the
end of this Decision for why) — matching this codebase's own
established "one purpose-built type per resource shape" precedent
rather than relaxing `RenderTarget`'s existing write-only contract. `HdrColorTarget` owns
one GPU image created with both `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT`
and `VK_IMAGE_USAGE_SAMPLED_BIT`, exposing exactly two narrow
capabilities: bind as a color-attachment-output target for the
geometry pass, and bind as a combined-image-sampler input for the
output-transform pass.

- **Creation:** a new `Device::createHdrColorTarget(HdrColorTargetCreateParams)`
  factory method, mirroring `createOffscreenTarget()`'s/
  `createSampledTexture()`'s own existing two-outcome `Result` shape
  and error-enumerator pattern (a new `HdrColorTargetCreateError`,
  mirroring `TextureCreateError`'s/`OffscreenTargetCreateError`'s own
  `{AllocationFailed, ImageCreationFailed, ImageViewCreationFailed}`
  shape exactly).
- **Ownership:** caller-owned, `std::unique_ptr<HdrColorTarget>`,
  RAII — the same tier as `OffscreenTarget`/`SampledTexture` (must be
  destroyed before the `Device` it was constructed from; not
  guaranteed-detectable, matching every other Vulkan Backend
  destruction-order precondition in this codebase).
- **Resize:** recreated in the same resize branch that already
  recreates the depth `Texture`
  (`runtime_application.cpp:510-516`) — same trigger
  (`currentExtent != lastSeenExtent_`), same lifecycle, same
  `Result`-based failure handling (log, keep the existing resource,
  retry next frame — no new error semantic). Zero-extent (a minimized
  window) never reaches this check at all: `Presentation`'s own
  existing zero-extent contract causes the composition root to skip
  drawing that frame entirely, upstream of this resize branch —
  `HdrColorTarget` inherits this protection for free, exactly as the
  depth `Texture` already does; no new zero-extent handling is
  written for it.
- **Destruction:** ordinary RAII via the returned
  `std::unique_ptr<HdrColorTarget>` — no public `release()`/`consume()`
  method, mirroring `OffscreenTarget`'s own lifetime contract.
- **RenderGraph tracking:** `ResourceBinding`
  (`render_graph/execution.h`) gains one new field,
  `atlantis::rhi::HdrColorTarget* hdrColorTarget = nullptr;` —
  Guard 0's "exactly one of target/depthTexture/sampledTexture
  non-null" rule widens to this new quartet. Because this is a
  genuinely new field (never the existing `target` field), Guard 2
  (*"no bound `RenderTarget` may have any declared read usage"*) does
  not apply to it at all — confirmed against `execution.cpp:79-83`'s
  own real implementation, which is already scoped to
  `binding.target == nullptr → skip`, requiring **zero change to
  Guard 2's own code** to correctly exempt the new field.
- **`ColorAttachmentOutput` → `ShaderRead` transition:** every frame,
  `Undefined` → `ColorAttachmentOutput` (geometry pass writes) →
  `ShaderRead` (output-transform pass samples) — structurally
  mirroring `SampledTexture`'s existing
  `Undefined`/`TransferDestination`/`ShaderRead` sequence, no new
  `ResourceState` enumerator.
- **Vulkan implementation boundary:** a new concrete
  `VulkanHdrColorTarget`, private to Vulkan Backend, owning one
  `VkImage`/`VkDeviceMemory`/two `VkImageView`s (one
  `VK_IMAGE_ASPECT_COLOR_BIT` view for each of the two usages —
  reusing one view for both is also valid and left to Implementation).
  `VulkanCommandList`'s own concrete implementations of the new
  `CommandList` methods below (D-3) perform the real `vkCmd*` calls,
  matching this codebase's own "no direct `vkCmd*` outside Vulkan
  Backend's `CommandList` implementation" rule exactly — no Vulkan
  header or `Vk*` type reaches `Renderer`, `RenderGraph`, or RHI's own
  public headers beyond the new, backend-agnostic `HdrColorTarget`
  interface itself.

`HdrColorTarget` inherits neither existing type: `RenderTarget`
(Guard 2 would then reject its own required read usage) nor
`SampledTexture` (`format()` returns `SampledTextureFormat`, which
cannot represent `HdrFormat`). See D-3 for the resulting new
`CommandList` surface.

### D-2. Format — a new, single-variant `HdrFormat` enum; a runtime capability check with real `Result` error semantics, not `ATLANTIS_CHECK`

One value this round, `HdrFormat::Rgba16Float`
(`VK_FORMAT_R16G16B16A16_SFLOAT`), mirroring `DepthFormat`'s own
established "one variant" precedent
(`src/rhi/include/atlantis/rhi/types.h:71-74`).

**Only the two `optimalTilingFeatures` bits this design actually
uses are checked** — `VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT` (the
geometry pass writes it) and `VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT`
(the output-transform pass samples it). Not checked, because not
used: `COLOR_ATTACHMENT_BLEND_BIT` (this engine has zero alpha-
blending capability anywhere, `blendEnable = VK_FALSE` on every
`Pipeline`, unchanged by this ADR); `SAMPLED_IMAGE_FILTER_LINEAR_BIT`
(the output-transform pass samples at an exact 1:1 texel mapping —
`HdrColorTarget` and the final target always share the same extent,
D-1's own resize rule — so its `Sampler` uses `Filter::Nearest`, never
`Linear`); `TRANSFER_SRC_BIT`/`TRANSFER_DST_BIT` (D-9: `HdrColorTarget`
is never read back or copied to/from directly). Real evidence — a
`vulkaninfo --show-formats` run against this repository's own real
target GPU (Intel Arc B370, the same device every existing golden's
own sidecar records) — confirms both required bits present on that
device's `optimalTilingFeatures`.

**A missing capability is a runtime error, not a programmer error —
`ATLANTIS_CHECK` is the wrong mechanism.** A device's real, physical
feature set is environmental fact, discovered at runtime, exactly like
`PresentationError::SurfaceLost`/`SwapchainCreationFailed` — not a
violated precondition within this process. `Device::createHdrColorTarget()`'s
own Vulkan Backend implementation queries
`vkGetPhysicalDeviceFormatProperties()` for `Rgba16Float` *before*
attempting any image creation; if either required bit is absent, it
returns `Result::Err(HdrColorTargetCreateError::FormatFeaturesUnsupported)`
immediately, never proceeding to `vkCreateImage`. `HdrColorTargetCreateError`
is a new, narrow RHI error enumerator (no existing one expresses this
failure mode):

```cpp
enum class HdrColorTargetCreateError {
  FormatFeaturesUnsupported,  // vkGetPhysicalDeviceFormatProperties() missing a required bit -- checked first
  AllocationFailed,           // mirrors TextureCreateError/OffscreenTargetCreateError
  ImageCreationFailed,
  ImageViewCreationFailed,
};
```

**Vulkan Backend mapping:** `FormatFeaturesUnsupported` is returned
directly from the capability check above, never from a `VkResult`.
`AllocationFailed`/`ImageCreationFailed`/`ImageViewCreationFailed`
classify `vkCreateImage`/`vkAllocateMemory`/`vkBindImageMemory`/
`vkCreateImageView` failures exactly as `createTexture()`'s own
existing classification already does for the depth `Texture`
(`toTextureCreateError()`, `vulkan_result.cpp:109-112`): any non-
`VK_SUCCESS` `vkCreateImage`/`vkBindImageMemory` result, including
`VK_ERROR_DEVICE_LOST`, maps to `ImageCreationFailed`; a memory-type-
selection failure (no compatible `VkMemoryPropertyFlags`, found before
any `VkResult` is even returned) maps to `AllocationFailed` — the same
two-case split `createTexture()` already makes, no new
`VkResult`-classification rule. A future Spec may extend `HdrFormat`
(a second variant) the same way a future `DepthFormat` extension
would, each carrying its own capability check.

### D-3. RenderGraph/Renderer integration — two passes, both windowed and offscreen share them unconditionally; the real, complete new `CommandList`/`RenderGraph` surface

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
headless rendering share the same Renderer/RHI stack"*. Both passes
are declared, compiled, and executed by the **same, single,
unmodified** `RenderGraphBuilder::compile()`/`render_graph::execute()`
call pair `Renderer::drawFrame()` already makes once per frame,
recording both passes into the one `CommandList` the caller already
submits exactly once per frame — `Renderer` still never calls
`Device::submit()`/`Presentation::present()` itself (ADR-0022,
unchanged); no ad hoc submit, no bypass of RenderGraph, and
`RenderGraphBuilder`'s own
public surface (`declarePass`/`declareResource`/`reads`/`writes`/
`setExecute`/`compile`) needs **zero change**: it is already fully
general for an N-pass graph (Spec 0005's own original design), and its
existing single-producer-rule/dependency-derivation/cycle-detection
machinery ([ADR-0017](0017-render-graph-construction-compile-layering.md)/[ADR-0018](0018-render-graph-dependency-derivation-and-ordering.md))
already derives the correct draw-pass → output-transform-pass ordering
automatically from the shared `HdrColorTarget` resource's own
write-then-read usage declarations — the same mechanism that already
orders every existing multi-usage resource in this codebase.

**The real, complete new surface this Decision requires:**

- `ResourceBinding` gains one new field, `HdrColorTarget* hdrColorTarget`
  (D-1).
- `CommandList` gains three new/overloaded methods, verified against
  `render_graph::execute()`'s own real dispatch code
  (`execution.cpp:117-130,147`, which hardcodes a hard three-way
  branch on `target`/`depthTexture`/`sampledTexture` and cannot pass a
  non-`RenderTarget` type to the existing `beginRendering(RenderTarget&, ...)`):
  - `transitionResource(HdrColorTarget&, ResourceState, ResourceState)`
    — a fourth overload, alongside the existing `RenderTarget`/`Texture`/
    `SampledTexture` trio.
  - `beginRendering(HdrColorTarget&, Texture* depth, ClearColorValue, float)`
    — a true C++ overload (same name, distinct parameter type — no new
    method name), used only for the geometry pass writing into the
    `HdrColorTarget`. `endRendering()` itself needs no overload — it
    already takes no parameters.
  - `bindTexture(const HdrColorTarget&, const Sampler&)` — a second
    overload of the existing `bindTexture()`, used only by the
    output-transform pass to sample the `HdrColorTarget`.
- `render_graph::execute()`'s own internal implementation (not its
  public contract) gains real, disclosed logic in four places, each a
  narrow widening of an already-established three-way pattern to a
  four-way one: Guard 0's `boundCount` check; the per-usage transition
  dispatch (`execution.cpp:117-130`); the draw-pass `beginRendering()`
  call site (`execution.cpp:147`, now checks which binding field is
  populated and calls the matching overload); and the trailing
  `finalState` transition loop (`execution.cpp:180-194`). This is the
  same *kind* of change Spec 0016 already made to this exact function
  when it widened Guard 0/1 from two kinds to three for
  `SampledTexture` — not a new precedent.
- `Device::createHdrColorTarget(...)` (D-1).
- `Renderer::drawFrame()`'s own public signature gains new required
  parameters, all caller-owned/borrowed (D-1's stateless-orchestrator
  constraint) — a real, disclosed public API change, the first to this
  signature since
  [ADR-0022](0022-minimal-renderer-public-api-and-resource-ownership.md)/its
  own [Accepted Amendment](0022-minimal-renderer-public-api-and-resource-ownership.md):
  the `HdrColorTarget&`; the fullscreen-triangle vertex/index `Buffer`
  pair; the already-selected output-transform `Pipeline&` and its
  `Sampler&` (D-6 — the caller, not `Renderer`, picks which of the two
  closed shader-contract variants applies, exactly once per format-
  negotiation event, mirroring `selectShaderPair()`'s own existing
  composition-root-level pattern for `MaterialKind`).

**Every one of these is caller-owned, `Renderer` only borrows:**

- The fullscreen-triangle vertex/index `Buffer` pair — created once,
  at composition-root startup (alongside `HdrColorTarget`'s own
  initial creation), reused unchanged every frame, never recreated on
  resize (unlike the extent-dependent `HdrColorTarget` itself, D-1)
  since its own content never varies. **Deliberately not an
  Asset-System asset** — no `.mesh.txt`, no cook step, no `AssetId` —
  its three fixed clip-space positions are a fixed implementation
  detail of the output-transform mechanism itself; a tiny, hardcoded
  C++ literal, created via the same
  `Device::createBuffer(BufferPurpose::Vertex/Index)` calls every
  other mesh already uses, reused through the existing
  `bindVertexBuffer()`/`bindIndexBuffer()`/`drawIndexed()` path — no
  new non-indexed draw call.
- The output-transform `Sampler` — `{Filter::Nearest, AddressMode::ClampToEdge}`
  (D-2's own 1:1-mapping rationale), created once via the existing
  `Device::createSampler()`, reused every frame.
- **Both** output-transform `Pipeline`s (D-6's own `*_Unorm`/`*_Srgb`
  variants) — created once per real negotiated-format class change (the
  same trigger as today's existing format-change rebuild, D-4), the
  composition root selects which one to pass into `drawFrame()` this
  frame; `Renderer` itself never creates, owns, or selects between
  them.

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

**Exact, re-derived effect on Spec 0021/ADR-0064's own descriptor-pool
capacity proof:** `createDescriptorPoolOfSize()` (`vulkan_device.cpp:425-437`)
sizes one `UNIFORM_BUFFER` and one `COMBINED_IMAGE_SAMPLER` descriptor
slot per `maxSets` unit — capacity is counted in descriptor *sets*
(one per live `Pipeline`), matching Spec 0021's own "at most one
combined-image-sampler descriptor per Pipeline" premise (D-10 below
holds this for the output-transform Pipeline too). Let `N` be the
count of distinct material-asset Pipelines in the current scene
(`fallbackMaterial_`, `material_realization.cpp:338-366`, always
exists and is counted separately, floor 1). Because every geometry
Pipeline — `N` material Pipelines plus the fallback — now targets the
fixed HDR format, none of them participates in format-change rebuild
any longer; only the output-transform Pipeline's own `colorFormat`
still varies with the final target's negotiated format (D-6), and at
most 2 of its own descriptor sets (old + new) briefly coexist during
the existing safe swap.

**Steady state: `N + 2`** (`N` material Pipelines + 1 fallback + 1
output-transform). **Peak, during a format-change event: `N + 3`**
(steady state + 1 transient output-transform set). Compared against
Spec 0021's own existing `2×(N+1)` formula: **equal at `N = 1`
(`4 = 4`), lower at `N ≥ 2`** — not a blanket improvement at every `N`,
only from `N = 2` on. At the existing N=6 stress test, `9` vs. `14`,
both well within the real, `Accepted` 4-pool/60-descriptor-set ceiling
(ADR-0064). This derivation must be reconfirmed with a real N=6 GPU
stress test at Implementation time (Testing & Verification Plan) —
not merely trusted from this written derivation alone.

### D-5. Tone-mapping — fixed-exposure Reinhard, one literal formula

```
linearHdrColor.rgb = max(linearHdrColor.rgb, float3(0,0,0))          // floor only -- see note below
exposedColor       = linearHdrColor.rgb * kBaselineExposure          // kBaselineExposure = 1.0, named constant, no auto-exposure
tonemapped         = exposedColor / (float3(1,1,1) + exposedColor)   // per-channel Reinhard
alphaOut           = linearHdrColor.a                                // pass-through, unchanged
```

Chosen for being a single, literal, hand-verifiable rational function
(matching this codebase's own "literal, complete formula, directly
unit-testable against hand-computed values" precedent,
[ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s
own Consequences) — never an unstated "some ACES-like curve." No
auto-exposure, no luminance histogram, no eye-adaptation state; a
future spec may add one, replacing `kBaselineExposure`'s fixed value
with a computed one without changing this Decision's own curve.

**Numeric edge cases:** the `max(...,0)` floor is the *only* new clamp
this Decision introduces, and it applies to raw linear radiance
*before* exposure/Reinhard — a defensive guard
against a theoretically-impossible-but-unverified negative geometry-
pass output (radiance is never negative by construction, but
Reinhard's own `x/(1+x)` is not monotonic for `x < -1` and could
reintroduce an out-of-range value the transfer function's own domain,
D-6, does not expect). This floor never reintroduces D-7's own removed
pre-tonemap `[0,1]` clamp in a different guise — any positive value,
however large, still reaches the curve unclipped. Alpha is passed
through from the `HdrColorTarget`'s own sampled `.a` channel unchanged
(each `MaterialKind`'s own geometry-pass shader already writes its
existing alpha value there, e.g. `PbrDirectLit`'s `alphaOut`) — this
engine has zero alpha-blending capability anywhere (`blendEnable =
VK_FALSE`, every `Pipeline`, unchanged by this ADR), so the final
target's own alpha channel stays inert, written for format-completeness
only, exactly as every existing shader's own `float4(finalRgb,
alphaOut)` return already does. NaN cannot reach this stage: every
existing `MaterialKind`'s own NaN guards (`kMinDot`/`kMinAlpha`/
`kPointLightDistanceEpsilon` floors, ADR-0067 D-1) are unchanged by
this ADR and continue to prevent NaN at its own source.

### D-6. Linear-to-display transfer — two closed shader/Pipeline variants, selected by final-target format class, never narrowing surface negotiation

`selectSurfaceFormat()`'s existing four-format candidate list
(`Bgra8Unorm`/`Bgra8Srgb`/`Rgba8Unorm`/`Rgba8Srgb`,
`vulkan_presentation.cpp:111-116`) is **unchanged** — Windows and
Android are both primary targets ([AGENTS.md](../AGENTS.md)); this ADR
never rejects startup because a real surface happens to report only
`*_Srgb` variants.

Two closed output-transform shader/Pipeline contracts — never branched
by `MaterialKind`, never a new global — cover the two format classes:

- **`*_Unorm` final target:** the shader applies D-5's tone-mapping
  *and* the exact piecewise sRGB OETF — `linear <= 0.0031308 ? linear
  * 12.92 : 1.055 * pow(linear, 1.0/2.4) - 0.055`, per channel — then
  writes the encoded byte value directly.
- **`*_Srgb` final target:** the shader applies D-5's tone-mapping and
  writes the **linear** `tonemapped` value unencoded; the target
  image's own `VK_FORMAT_*_SRGB` view causes the GPU's fixed-function
  output-merger to perform the identical sRGB encode in hardware on
  store. **The shader never applies the manual OETF here** — doing so
  would double-encode.

Both variants share the *same* tone-mapping math (D-5) up to
`tonemapped`; they differ in exactly one place — whether the OETF runs
in the shader or in fixed-function hardware — and are defined to
produce the same final displayed pixel. `OffscreenTarget`'s own
`format` stays caller-supplied; every real call site (Runtime, every
image-regression fixture) already hardcodes `Rgba8Unorm`, so goldens
always take the explicit-OETF variant — unaffected by this Decision.

**Selection, disclosed exactly:** the composition root (Runtime, or an
image-regression fixture) already knows the final target's real,
negotiated `Format` (`presentation_->metadata().format`, or the
literal `OffscreenTargetCreateParams::format` it supplied) at the same
point it already detects a format change (D-4's own existing trigger).
A new, exhaustive, C4062-guarded classification —
`isSrgbFormat(atlantis::rhi::Format) -> bool`, covering all four
`Format` enumerators, no `default:` — selects which of the two
pre-compiled output-transform `Pipeline`/`Sampler` pairs to pass into
`Renderer::drawFrame()` this frame (D-3). `Renderer` itself performs no
classification and holds no format-dependent state — it draws whatever
`Pipeline` it is handed, identically either way.

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

See D-1's own "`ColorAttachmentOutput` → `ShaderRead` transition" and
"Resize" bullets for the complete, exact statement — stated once
there, not repeated here, per this repository's own single-
authoritative-source rule. Pipeline itself is untouched by resize
(dynamic viewport/scissor, unaffected), unchanged from today.

### D-9. Readback and the image-regression harness stay 8-bit LDR, unmodified in shape

`CommandList::copyRenderTargetToBuffer()` continues to operate only on
the final, post-output-transform `RenderTarget` — the `HdrColorTarget`
is never read back directly, and the image-regression harness's own
`stbi_load`/`stbi_write_png` 8-bit-per-channel pipeline
(`tests/image_regression/support/png_codec.cpp:37,72`) needs no format
widening. The comparison methodology itself
([ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md),
zero channel tolerance, zero failing-pixel budget) is unchanged.

### D-10. The output-transform shader pair's own descriptor contract — two closed variants (D-6), identical shape, shared across all three `MaterialKind`s

Two `.slang` files (`output_transform_unorm`, `output_transform_srgb`,
D-6), each with its own vertex shader (identical — the checked-in
fullscreen-triangle positions, D-3) and its own fragment shader
(identical up to `tonemapped`; the `*_Unorm` variant alone applies the
sRGB OETF). Both share the *same* descriptor contract — mirroring
`descriptor_contract.h`'s established per-kind-function pattern, one
new `outputTransformExpectedDescriptorContract()` reused by both:
exactly one binding — `{set 0, binding 0, CombinedImageSampler,
Fragment}`, sampling the `HdrColorTarget` — no uniform buffer (fixed
exposure, D-5, is a shader-compile-time constant) and no push constant
(no per-draw transform). Validated by `atlantis_shader_compiler` via
two new contract names, `"output-transform-unorm"`/
`"output-transform-srgb"`, mirroring `"pbr-direct-lit"`'s own addition
to `compile_and_validate.cpp` (`pushConstantSizeBytes == 0` for both,
unlike every `MaterialKind` contract).

**All three existing `MaterialKind`s genuinely share whichever one
variant applies this frame, identically** — neither variant has any
`MaterialKind`-specific branch anywhere; the output-transform pass
runs exactly once per frame, downstream of every geometry-pass
`DrawItem`, reading whatever the geometry pass accumulated into the
shared `HdrColorTarget` regardless of which `MaterialKind`s
contributed to it. Selection between the two variants (D-6) depends
only on the final target's format class, never on scene content.

### D-11. Portability

`VK_FORMAT_R16G16B16A16_SFLOAT`'s real-hardware-confirmed feature set
(D-2) is expected to hold identically on Windows and future Android
Vulkan implementations — this format is Vulkan's own universal choice
for exactly this purpose — but this ADR does not claim that
expectation as an unconditional guarantee needing no verification:
D-2's own `vkGetPhysicalDeviceFormatProperties()` check runs on every
platform identically, including a future Android implementation, so
portability is enforced by a real, executed check on each real device
at startup, not merely asserted once here for all devices everywhere.
No `VK_EXT_hdr_metadata`, `VK_COLOR_SPACE_HDR10_ST2084_EXT`, or any
other literal-HDR-*display* WSI extension is introduced anywhere —
this ADR's own "HDR" is strictly an internal, scene-referred linear
intermediate; the swapchain's own existing four-format negotiation
(`selectSurfaceFormat()`, unchanged, D-6) and Vulkan Backend's existing
WSI boundary are untouched — startup never fails because of which of
the four RHI-approved formats a real surface happens to report.

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
  Vulkan Backend's existing WSI boundary is untouched (D-11).

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
- The real, complete new RHI public surface (D-1/D-3): one new
  resource type (`HdrColorTarget`), one new `HdrFormat` enum, one new
  `HdrColorTargetCreateError` enum, one new `Device` factory method,
  three new/overloaded `CommandList` methods (`transitionResource`,
  `beginRendering`, `bindTexture`), and one new `ResourceBinding`
  field — reviewed and accepted here in full, not incidental.
- Two closed output-transform shader/Pipeline variants (D-6) instead of
  one — real, disclosed additional shader-compiler/asset surface (two
  new `.slang` files, two new descriptor contracts, D-10), and a real
  new composition-root-level classification (`isSrgbFormat()`) that
  must be kept exhaustive as this repository's own C4062/`WX`
  discipline already enforces elsewhere.

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
- **Narrow `selectSurfaceFormat()`'s own candidate list to `*_Unorm`
  only**, refusing startup on a real surface reporting only `*_Srgb`
  variants. Rejected — Windows and Android are both primary targets;
  neither should ever fail to start over which of four already-
  approved formats a real surface happens to report. Superseded by
  D-6's own two-closed-variant design, which handles both format
  classes correctly instead of excluding one.
- **One shader always applying the manual OETF, regardless of the
  final target's format class.** Rejected — would double-encode on an
  `*_Srgb` target (hardware encode on top of the shader's own manual
  one); the two-variant split (D-6) is the only way to keep exactly
  one encode operation per pixel on both format classes.
- **`HdrColorTarget` publicly inheriting `RenderTarget` and/or
  `SampledTexture`**, to reuse `beginRendering()`/`bindTexture()`
  unmodified, instead of new/overloaded `CommandList` methods (D-1/D-3).
  Rejected — inheriting `RenderTarget` would make it visible to Guard 2
  via the same `target`-field check that already rejects any bound
  `RenderTarget` with a declared read usage, reopening the "relax
  `RenderTarget`'s write-only contract" alternative above; inheriting
  `SampledTexture` would force `HdrFormat::Rgba16Float` through
  `SampledTexture::format()`'s own `SampledTextureFormat` return type,
  conflating authored-texture color-space with internal render-target
  format. Three new/overloaded `CommandList` methods is a larger public
  surface than the original draft implied, but the only option that
  keeps both existing types' own contracts genuinely intact.
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
