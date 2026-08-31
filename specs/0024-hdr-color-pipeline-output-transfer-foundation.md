# Spec: HDR Color Pipeline & Output Transfer Foundation

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-08-31
- **Related Plan(s):** [plans/0024-hdr-color-pipeline-output-transfer-foundation.md](../plans/0024-hdr-color-pipeline-output-transfer-foundation.md)
  (**`Approved / Ready for Implementation`**, Human Review Approval
  recorded 2026-08-31 following one final review round — see that
  Plan's own Human Review Approval note. **This approval authorizes
  Implementation of that Plan only once its own Implementation PR has
  merged — not before.**)
- **Related ADR(s):** [ADR-0068](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md) (`Accepted`)
- **Human Review Approval (2026-08-31):** Reviewed and approved by
  slmao (`slmao <slmaosjtu@gmail.com>`, this repository's
  git-identified maintainer) on 2026-08-31, accepting this document's
  own "Decisions for Human Review" (D1–D10) in full, as finalized in
  commit `9771f94` (the two review rounds this PR itself records — see
  "Review History" below). Accepting ADR-0068 (`Proposed` →
  `Accepted`) in the same pass. **This approval authorizes drafting
  Plan 0024 only, once PR #113 merges — not any Implementation.**

## Summary

Introduces a scene-referred linear HDR color intermediate and a second,
shared RenderGraph pass — reading that intermediate, applying a fixed-
exposure tone-mapping curve, and encoding the result with the sRGB
transfer function into the caller's real, presentable `RenderTarget`
(windowed or offscreen, unconditionally shared) — closing the "final
clamp is the only transformation" Phase 1 limitation Spec 0019/0023
(via ADR-0062/ADR-0067) disclosed and explicitly deferred to *"a
dedicated Output Transfer Function Spec."* All three existing
`MaterialKind`s (`UnlitTextured`/`LitTextured`/`PbrDirectLit`) share the
one new output contract identically — no per-kind fork.

## Motivation / Problem Statement

Real code confirms two problems, not one:

1. **The disclosed one:** `LitTextured`/`PbrDirectLit` clamp their
   accumulated linear radiance to `[0,1]` and write it straight to the
   bound `RenderTarget` — a bright highlight or an over-`1.0` light
   intensity hard-clips rather than rolling off (ADR-0062 D-6, ADR-0067
   D-6, both an explicit, accepted Phase 1 limitation, not an
   oversight).
2. **A real, previously-undisclosed one, found during this Spec's own
   investigation:** none of the three `MaterialKind`s, and no part of
   the RHI/Vulkan Backend swapchain-format-negotiation path, ever
   performs a linear→sRGB encode of any kind.
   `VulkanPresentation::selectSurfaceFormat()`'s own fixed preference
   order tries `Bgra8Unorm` before any `_Srgb` variant
   (`vulkan_presentation.cpp:111-116`), and every image-regression
   fixture hardcodes `Format::Rgba8Unorm` explicitly
   (`tests/image_regression/fixture/minimal_cube_fixture.h:40` and
   siblings) — so none of the six existing goldens were captured
   through a target whose fixed-function output-merger performs an
   implicit encode either. Every pixel this engine has ever shipped is,
   strictly, a linear value written into a byte buffer a real display
   then decodes as if it were already sRGB-encoded — a real transfer-
   function mismatch that predates this Spec and is not limited to
   bright/HDR content.

Both problems share one root cause and one fix: there is no output-
transform stage anywhere in this engine. This Spec adds exactly one,
shared by every `MaterialKind` and both windowed/offscreen origins,
closing both problems in the same pass.

## Goals

- A new, scene-referred linear HDR color intermediate, GPU-owned,
  caller-created, fed by the existing, unmodified per-`DrawItem`
  geometry pass.
- A new, shared RenderGraph pass — reading that intermediate, applying
  a fixed-exposure tone-mapping curve and the sRGB transfer function,
  writing the caller's real, presentable `RenderTarget`.
- Exactly one output contract for all three existing `MaterialKind`s —
  never a per-kind fork, matching ADR-0062/ADR-0067's own "all three
  kinds visually consistent" principle.
- Windowed and offscreen origins share this pass unconditionally,
  matching [AGENTS.md](../AGENTS.md)'s own "windowed and headless
  rendering share the same Renderer/RHI stack" principle.
- A literal, hand-verifiable tone-mapping formula and transfer function
  — directly unit-testable, never a named-but-undefined technique.
- A disclosed, honest migration story for the six existing goldens
  under ADR-0042's own zero-tolerance comparison policy.

## Non-Goals

- Bloom, color grading (LUTs, white balance, contrast/saturation
  curves), and auto exposure (histogram-based, eye-adaptation, or any
  other computed-exposure model) — a fixed exposure baseline only.
- Image-based lighting, shadows, and normal mapping/tangent-space
  input — all remain Spec 0023's own disclosed non-goals, unaffected
  by this Spec.
- A literal platform HDR *display* output — no `VK_EXT_hdr_metadata`,
  no `VK_COLOR_SPACE_HDR10_ST2084_EXT`/scRGB swapchain color space, no
  wide-gamut output. "HDR" in this Spec means an internal, scene-
  referred linear intermediate only; the final presented image stays
  8-bit, sRGB-transfer-encoded, exactly as today's display pipeline
  already assumes.
- A second HDR format, a configurable tone-mapping operator, or any
  runtime-selectable exposure — Non-functional Requirements below fix
  one operator, one format, one constant exposure for this round.
- Android implementation — architecture-only, matching every other
  Spec's own Candidate Order 1 deferral; this Spec's own format choice
  is confirmed portable to it (see Decisions for Human Review, D9) but
  nothing Android-specific is built.

## Requirements

### Functional

- Every `MaterialKind`'s existing fragment-shader BRDF/lighting math is
  unchanged — this Spec touches only what happens to the *already-
  computed* linear color afterward.
- The geometry pass writes into the new HDR intermediate instead of the
  caller's final `RenderTarget`; the new output-transform pass performs
  exactly one read of that intermediate and one write of the final
  target, per frame.
- Tone-mapping and transfer-function encoding happen exactly once, in
  the output-transform pass only — never per-light, never inside an
  existing `MaterialKind`'s own shader (mirroring ADR-0062's own "the
  one and only clamp" discipline, now relocated).
- Resize recreates the HDR intermediate alongside the existing depth
  `Texture`, at the same trigger point
  (`runtime_application.cpp:510-516`).

### Non-functional

- **Performance:** one additional fullscreen-triangle draw call per
  frame; no measurable-by-design regression target is set by this
  Spec — real measurement is a Plan/Implementation-time verification
  step, not a number fixed here.
- **Memory:** one new GPU image per live `HdrColorTarget`, sized to the
  current color-target extent × 8 bytes/pixel (`Rgba16Float`), twice
  today's 8-bit intermediate's own footprint.
- **Portability (within the Vulkan-only Phase 1 constraint):**
  `VK_FORMAT_R16G16B16A16_SFLOAT`'s only two required
  `optimalTilingFeatures` (color-attachment, sampled-image — this
  design uses neither blend nor transfer capability) are confirmed on
  this repository's own real target GPU and expected on every Vulkan
  1.0-conformant implementation — but not assumed unconditionally: a
  real `vkGetPhysicalDeviceFormatProperties()` capability check at
  `HdrColorTarget` creation (Decisions for Human Review, D1) verifies
  it on each real device at startup, returning a real `Result::Err`
  (never `ATLANTIS_CHECK`) on any device where it does not hold; no
  fallback format is defined for that failure case this round.
- **Other:** the six existing goldens' own 8-bit `Rgba8Unorm` PNG
  comparison shape (`ADR-0042`) is unchanged — only their *byte
  content* is expected to shift (see Decisions for Human Review, D7).

## Real-code evidence (summary — see ADR-0068 for full citations)

| Claim | Evidence |
|---|---|
| RHI `Format` has exactly four 8-bit variants, no HDR/float format anywhere | `src/rhi/include/atlantis/rhi/types.h:26-32` |
| `LitTextured`/`PbrDirectLit` each apply exactly one post-accumulation `clamp(...,0,1)`, no gamma-encode | `lit_textured.slang:105-107`, `pbr_direct_lit.slang:179-183` |
| `UnlitTextured` applies no transformation at all | `textured_quad.slang:52-55` |
| Swapchain format negotiation's own fixed preference list includes all four RHI formats and stays that way — Windows and Android are both primary targets | `vulkan_presentation.cpp:111-116` |
| A `fallbackMaterial_` (untextured, always exists) is a real, separate geometry Pipeline, distinct from every material-asset Pipeline | `material_realization.cpp:338-366` |
| Every image-regression fixture hardcodes `Format::Rgba8Unorm` explicitly | `minimal_cube_fixture.h:40`, `textured_quad_fixture.h:63` |
| `RenderTarget` is write-only by its own class contract; `execute()`'s Guard 2 enforces it, scoped specifically to the `target` binding field | `render_target.h:13-14`; `execution.cpp:79-83` |
| No existing RHI type is both a renderable color attachment and a later-sampled input | repo-wide read of `render_target.h`, `texture.h`, `sampled_texture.h` |
| `execute()`'s own real dispatch hardcodes a three-way branch (`target`/`depthTexture`/`sampledTexture`) and calls `beginRendering(RenderTarget&,...)` directly — cannot pass a non-`RenderTarget` type without a real, disclosed code change | `execution.cpp:117-130,147` |
| `Renderer` is a stateless orchestrator — retains no GPU resource across calls | `renderer.h:14-25` (ADR-0022) |
| `CommandList`'s only draw call is `drawIndexed()` — no non-indexed `draw()` | `command_list.h:77` |
| The depth `Texture` is recreated on resize, at a real, fixed call site; zero-extent is handled upstream, before this check | `runtime_application.cpp:510-516` |
| Descriptor-pool capacity is counted in descriptor sets (one `UNIFORM_BUFFER` + one `COMBINED_IMAGE_SAMPLER` slot per `maxSets` unit), not per-binding-type separately | `vulkan_device.cpp:425-437` |
| The image-regression harness is 8-bit-per-channel PNG only (`stbi_load`/`stbi_write_png`, 4 channels) | `png_codec.cpp:37,72` |
| ADR-0042's comparison policy is zero channel tolerance, zero failing-pixel budget | `adr/0042-...md`, Decision |
| ADR-0067 D-6 names this Spec's own correct next step explicitly | `adr/0067-...md`, D-6 |
| `VK_FORMAT_R16G16B16A16_SFLOAT`'s `optimalTilingFeatures` include `COLOR_ATTACHMENT_BIT`/`SAMPLED_IMAGE_BIT` (the only two this design uses), confirmed on this repository's own real target GPU | `vulkaninfo --show-formats`, Intel Arc B370 |
| Every `Pipeline` has `blendEnable = VK_FALSE` — no alpha-blending capability exists anywhere | `vulkan_device.cpp` (Spec 0023's own real-code evidence, unchanged) |
| `toTextureCreateError()` maps every non-success `vkCreateImage`/`vkBindImageMemory` `VkResult`, including `VK_ERROR_DEVICE_LOST`, to `ImageCreationFailed` | `vulkan_result.cpp:109-112` |

No unresolvable architectural conflict was found. See ADR-0068's own
Alternatives Considered for the complete, itemized record of paths
rejected and why.

## Proposed Design

```
Existing per-DrawItem geometry pass (UnlitTextured/LitTextured/PbrDirectLit,
  each shader's own BRDF/lighting math fully unchanged)
  -> writes ColorAttachmentOutput into a new, caller-owned HdrColorTarget
     (Rgba16Float, ADR-0068 D-1/D-2)
  -> output-transform pass: HdrColorTarget (ShaderRead) as input;
     composition root already selected ONE of two closed Pipeline
     variants this frame, by the final target's real Format class
     (ADR-0068 D-6), never by MaterialKind:
     -> max(x,0) -> * kBaselineExposure -> Reinhard x/(1+x)   (shared math, ADR-0068 D-5)
     -> *_Unorm variant: + explicit sRGB OETF, write encoded byte value
     -> *_Srgb variant:  write linear value, hardware encodes on store
     -> writes ColorAttachmentOutput into the caller's real, final
        RenderTarget (swapchain- or OffscreenTarget-backed -- no
        branch on which, ADR-0068 D-3; full 4-format negotiation
        unchanged, ADR-0068 D-6)
  -> existing copyRenderTargetToBuffer()/image-regression comparison
     path, entirely unmodified in shape (ADR-0068 D-9)
```

Every existing `MaterialKind`'s own fragment-shader body is unchanged
except the removal of `lit_textured.slang`'s/`pbr_direct_lit.slang`'s
own final `clamp(...,0,1)` (ADR-0068 D-7) — a pre-HDR clamp would
defeat the entire purpose of the new intermediate.

## Architectural Impact

**Yes** — one ADR, [ADR-0068](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
(`Accepted`), covering all of it as a single, tightly-coupled decision:
a new RHI resource type with `Result`-based (never `ATLANTIS_CHECK`)
capability failure semantics, three new/overloaded `CommandList`
methods, two closed output-transform shader/Pipeline variants selected
by final-target format class (never narrowing swapchain negotiation),
`Renderer`'s own public API change, and the tone-mapping/transfer-
function math contract — unlike Spec 0023's own three-ADR split, this
Spec's own decisions all live inside the same RHI/RenderGraph/Renderer/
Shader System boundary with no natural Asset-System-style seam, so one
ADR is the minimal, not merely convenient, choice.

## Alternatives Considered

See ADR-0068's own Alternatives Considered for the full, itemized
record (reusing `SampledTexture`/`RenderTarget` via inheritance instead
of a new independent type; relaxing `RenderTarget`'s write-only
contract; ACES filmic instead of Reinhard; narrowing surface
negotiation to `*_Unorm` only instead of two closed variants; one
shader always manually encoding regardless of target format class; a
new non-indexed draw call instead of a checked-in fullscreen-triangle
mesh) — not restated here per this repository's own single-
authoritative-source rule.

## Decisions for Human Review

Ten items, matching the human-directed drafting brief exactly. Every
recommendation is a proposal for Human Review to accept, amend, or
reject; formulas and full rationale live once, in ADR-0068.

| # | Question | Recommendation | Authoritative source |
|---|---|---|---|
| D1 | Scene-referred linear HDR intermediate — format and ownership | New, single-purpose `HdrColorTarget` RHI type, inheriting neither `RenderTarget` nor `SampledTexture`; new `HdrFormat::Rgba16Float`, one variant, whose only checked `optimalTilingFeatures` are `COLOR_ATTACHMENT_BIT`/`SAMPLED_IMAGE_BIT` (the only two this design uses); an unsupported format is a real `Result::Err(HdrColorTargetCreateError::FormatFeaturesUnsupported)`, never `ATLANTIS_CHECK`; caller-created/owned (`Renderer` stays a stateless orchestrator) | ADR-0068 D-1/D-2 |
| D2 | Windowed vs. offscreen — one shared output-transform pass, or two? | Share unconditionally — the pass is origin-agnostic by construction; both passes go through the same, single, unmodified `RenderGraphBuilder`/`execute()` call pair — no ad hoc submit | ADR-0068 D-3 |
| D3 | Tone-mapping algorithm and fixed-exposure baseline — exact math contract | Fixed exposure (`kBaselineExposure = 1.0`, no auto-exposure), floored at zero, then per-channel Reinhard (`x / (1+x)`); alpha passes through unchanged | ADR-0068 D-5 |
| D4 | Linear-to-display transfer — location and target format semantics | **Two closed shader/Pipeline variants**, selected by the final target's real `Format` class, never by `MaterialKind`: `*_Unorm` applies the explicit sRGB OETF in-shader; `*_Srgb` writes the linear value and lets the target's own hardware encode on store. Both share identical tone-mapping math and produce the same displayed result; never both applied to the same pixel (no double-encode). Surface negotiation stays the existing, full four-format list — Windows and Android are both primary targets, neither ever refused startup over which format a surface reports | ADR-0068 D-6 |
| D5 | New RenderGraph/RHI public capability needed? | Yes: `HdrColorTarget` type + `HdrFormat` enum + `HdrColorTargetCreateError` (D1), one new `Device` factory method, three new/overloaded `CommandList` methods (`transitionResource`, `beginRendering`, `bindTexture` — `execute()`'s own real dispatch cannot pass `HdrColorTarget` through the existing `RenderTarget`/`SampledTexture` overloads otherwise), one new `ResourceBinding` field, and two new descriptor contracts (D4). `Renderer::drawFrame()`'s own signature gains new caller-owned parameters (`HdrColorTarget&`, the fullscreen-triangle Buffers, the already-selected output-transform `Pipeline&`/`Sampler&`) — its first change since ADR-0022's own Accepted Amendment. `RenderGraphBuilder`'s own public surface needs zero change | ADR-0068 D-1/D-3 |
| D6 | Resize, format change, readback, resource-state transitions, descriptor-pool capacity | `HdrColorTarget` recreated on resize alongside the existing depth `Texture`; every geometry Pipeline's `colorFormat` becomes the fixed HDR format, removing `N` material Pipelines and the always-present `fallbackMaterial_` from format-change churn — only the output-transform Pipeline (now two variants, D4) still varies with the final target's format. **Steady state `N+2`, peak `N+3`** (`N` = material-asset Pipeline count) vs. the existing `2×(N+1)` formula: equal at `N=1`, lower at `N≥2` — never a blanket claim; both re-confirmed against Spec 0021/ADR-0064's own real 60-descriptor-set ceiling. Readback stays final-target-only, 8-bit, unmodified; `HdrColorTarget` transitions `Undefined`→`ColorAttachmentOutput`→`ShaderRead` every frame, no new `ResourceState` enumerator | ADR-0068 D-1/D-4/D-9 |
| D7 | Existing six-golden compatibility/migration strategy | All six will shift byte values (offscreen goldens always take the explicit-OETF `*_Unorm` variant, D4) — a mandatory, human-reviewed re-capture of all six at Plan/Implementation time, each its own independent ADR-0042 two-phase capture/provenance/review cycle; never a batch/automatic acceptance | ADR-0068 Consequences |
| D8 | New verification scene and ADR-0042 golden flow | One new, dedicated scene with deliberately over-`1.0`-range light intensity/emissive value — proves the tone-map curve actually rolls off, not merely non-black; bootstrapped via ADR-0042's existing two-phase fixture-then-golden process, no new comparison methodology | this Spec |
| D9 | Windows/future-Android portability | `Rgba16Float`'s required feature set is confirmed on this repository's own real target GPU and expected identically on both platforms; portability is enforced by D1's own real, executed capability check on each real device at startup, never asserted unconditionally. Zero new platform-specific WSI/extension surface — the existing four-format negotiation is unchanged (D4) | ADR-0068 D-11 |
| D10 | Explicit exclusions | Bloom, color grading, auto exposure, IBL, shadow, normal mapping, and any literal platform HDR *display* output are all out of scope — see Non-Goals above | this Spec |

## Testing & Verification Plan

- **GPU-independent:** the tone-mapping curve (D3, including the
  zero-floor edge case) and sRGB transfer function (D4) each get a
  CPU-side reference implementation, unit-tested against hand-computed
  values at several inputs spanning negative, below-`1.0`, exactly-
  `1.0`, and well-above-`1.0`; a shader-reflection-vs-C++-layout
  cross-check for `HdrColorTarget`'s own format/usage and both new
  descriptor contracts (D4). **The `FormatFeaturesUnsupported`
  classification (D1) is unit-tested against synthetic
  `VkFormatFeatureFlags` inputs (both bits present, one missing, both
  missing) — a pure decision-function test, never a real GPU or a
  fabricated "this hardware lacks the format" condition**, which no
  real device this Spec's own test hardware presents can genuinely
  exhibit.
- **Headless/GPU integration** (positive capability and the full
  render path only — never a manufactured negative-hardware
  condition):
  - Initial `HdrColorTarget` creation/resize on real hardware,
    confirming the capability check's own positive path succeeds
    (D1).
  - The two-pass RenderGraph compiles and executes without a Vulkan
    Validation Layers warning/error on all three existing
    `MaterialKind`s, both windowed and offscreen origins, **under both
    final-target format classes** (a real `*_Unorm` target and a real
    `*_Srgb` target — two real GPU runs, not one, D4).
  - **A real display-equivalence test, with a defined, non-zero
    tolerance** (not ADR-0042's own zero-tolerance golden policy, which
    governs a different comparison — a fixed reference image on one
    known GPU/driver, not two live-computed paths against each other):
    the same linear input through the `*_Unorm` explicit-OETF path and
    the `*_Srgb` hardware-OETF path must match within that tolerance —
    a real GPU's own fixed-function sRGB encode is not guaranteed
    byte-identical to a shader-computed piecewise formula across
    vendors; the exact tolerance value is an Implementation-time
    measurement, not asserted here.
  - A real, above-`1.0`-input pixel comparison proving the tone-map
    curve actually rolled off versus the pre-Spec behavior.
  - A real N=6 descriptor-pool stress test re-confirming D6's own
    `N+2`/`N+3` formula against Spec 0021/ADR-0064's own real
    60-descriptor-set ceiling.
- **Image regression:** all six existing goldens re-captured, human-
  reviewed, and categorized per ADR-0042's golden-update-reason rule
  (D7) — never a batch/automatic acceptance; one new golden scene (D8)
  proving over-range roll-off.
- **Vulkan Validation Layers clean**, both Debug and Release, under
  both final-target format classes, matching this repository's own
  unconditional Definition of Done requirement.
- **Module/link graph:** confirm `Atlantis::Renderer`'s own dependency
  set is unchanged (still Core/RHI/RenderGraph only); confirm no
  Vulkan header/`Vk*` type leaks into `Renderer` or `RenderGraph` from
  the new `HdrColorTarget` type.

## Risks & Open Questions

- **D7's golden re-capture is a real, disclosed, non-trivial Plan/
  Implementation-time cost** — six goldens, each requiring a fresh
  human visual review, not a mechanical regeneration. Named here so a
  future Plan does not discover it as a surprise.
- **The exact byte-level behavior of the sRGB OETF's own piecewise
  formula at its linear/power-curve boundary** (`x <= 0.0031308`) needs
  a real, disclosed rounding/precision decision at Implementation
  time — this Spec fixes the formula (ADR-0068 D-6) but not a specific
  fixed-point/float32 rounding mode; left to the Plan.
- **Whether `HdrColorTarget`'s own creation API should mirror
  `OffscreenTarget`'s `Device::create...()` factory shape exactly, or
  introduce a distinct pattern**, is an Implementation-level API-
  ergonomics question, not an architectural one — ADR-0068 D-1 fixes
  the capability, not the exact C++ signature.
- **Real GPU coverage for the `*_Srgb` final-target path is limited by
  what the reviewing hardware's own surface actually reports** — the
  `*_Unorm` path is exercised by every existing fixture/golden today;
  a real `*_Srgb` run needs either a real surface reporting that
  format or an offscreen target explicitly created with
  `OffscreenTargetCreateParams::format = Bgra8Srgb`/`Rgba8Srgb` (both
  already supported, D4) — left to the Plan to select a concrete,
  reachable test path.

## Out of Scope / Future Work

- A second, higher-quality tone-mapping operator (ACES filmic or
  similar) as a named future candidate, registered in ADR-0068's own
  Alternatives Considered — not built here.
- Auto-exposure (histogram/eye-adaptation), bloom, color grading,
  image-based lighting, shadows, normal mapping, and literal platform
  HDR display output all remain named, separate future candidates —
  none is designed or scaffolded by this Spec.
- Android implementation of any part of this pipeline.

## Review History

Three passes on PR #113, all before this Spec's own Human Review
Approval above — full rationale lives once, in ADR-0068's own text and
Consequences, not restated here.

- **Round 1 (2026-08-31):** established `HdrColorTarget` inherits
  neither `RenderTarget` nor `SampledTexture` (Guard 2/`format()`-type
  conflicts); added the real, complete new `CommandList` surface (D5);
  re-derived the descriptor-pool formula instead of asserting it; added
  tone-mapping's zero-floor/alpha edge cases (D3); added the
  output-transform descriptor contract as its own Decision.
- **Round 2 (2026-08-31):** corrected the format-capability check from
  `ATLANTIS_CHECK` to a real `Result`/`HdrColorTargetCreateError` (a
  missing GPU feature is a runtime fact, not a programmer error), and
  narrowed the checked `optimalTilingFeatures` to only the two bits
  this design uses; reversed Round 1's own "narrow surface negotiation
  to `*_Unorm`" fix — Windows and Android are both primary targets, so
  D4 instead defines two closed shader/Pipeline variants, selected by
  final-target format class; corrected the descriptor-pool formula to
  the precise `N+2`/`N+3` derivation (counting the real
  `fallbackMaterial_` Pipeline explicitly), replacing the earlier,
  looser "strictly less for all `N`" claim; made `HdrColorTarget`'s
  resource contract and the fullscreen-triangle draw's own persistent
  `Buffer`/`Sampler`/Pipeline ownership fully explicit.
- **Round 3 (2026-08-31):** verified all twelve final decisions and the
  verification boundary against the then-current documents and found
  one real gap — the format-capability negative-path test moved from a
  real-GPU condition (unreachable on conformant hardware) to a
  GPU-independent synthetic classification test, with real GPU coverage
  scoped to the positive path only; the `*_Unorm`/`*_Srgb`
  display-equivalence test now states a defined, non-zero tolerance
  rather than exact equality. Human Review Approval recorded
  immediately after.
