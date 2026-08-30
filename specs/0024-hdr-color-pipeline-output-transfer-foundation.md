# Spec: HDR Color Pipeline & Output Transfer Foundation

- **Status:** In Review
- **Author:** slmao
- **Created:** 2026-08-31
- **Related Plan(s):** None yet — this Spec must reach `Approved` first
- **Related ADR(s):** [ADR-0068](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md) (`Proposed`)

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
  `VK_FORMAT_R16G16B16A16_SFLOAT`'s required feature set (color-
  attachment + sampled-image + blend, optimal tiling) is expected to
  hold on every Vulkan 1.0-conformant implementation and is confirmed
  on this repository's own real target GPU — but is not assumed
  unconditionally: a real `vkGetPhysicalDeviceFormatProperties()`
  capability check at `HdrColorTarget` creation (Decisions for Human
  Review, D1) verifies it on each real device at startup, failing
  loudly rather than silently on any device where it does not hold; no
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
| Swapchain format negotiation's own fixed preference list includes all four RHI formats, `*_Unorm` first but `*_Srgb` still selectable if that's all a real surface reports | `vulkan_presentation.cpp:111-116` |
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
| `VK_FORMAT_R16G16B16A16_SFLOAT`'s `optimalTilingFeatures` include every feature this Spec needs, confirmed on this repository's own real target GPU | `vulkaninfo --show-formats`, Intel Arc B370, this Spec's own final review |

No unresolvable architectural conflict was found. This Spec's own final
review round (2026-08-31, see "Final Review Round" below) found several
real gaps the original draft's recommendations did not close — all
corrected in place, with the original reasoning's shortfall disclosed
rather than silently overwritten. See ADR-0068's own Alternatives
Considered for the complete, itemized record of paths rejected and why.

## Proposed Design

```
Existing per-DrawItem geometry pass (UnlitTextured/LitTextured/PbrDirectLit,
  each shader's own BRDF/lighting math fully unchanged)
  -> writes ColorAttachmentOutput into a new, caller-owned HdrColorTarget
     (Rgba16Float, ADR-0068 D-1/D-2)
  -> new output-transform pass: HdrColorTarget (ShaderRead) as input
     -> exposedColor = linearHdrColor * kBaselineExposure   (fixed, ADR-0068 D-5)
     -> tonemapped = exposedColor / (1 + exposedColor)      (Reinhard, ADR-0068 D-5)
     -> encoded = sRGB_OETF(tonemapped)                     (literal formula, ADR-0068 D-6)
     -> writes ColorAttachmentOutput into the caller's real, final
        RenderTarget (*_Unorm only -- swapchain negotiation itself
        narrowed to exclude *_Srgb, ADR-0068 D-6; swapchain- or
        OffscreenTarget-backed -- no branch on which, ADR-0068 D-3)
  -> existing copyRenderTargetToBuffer()/image-regression comparison
     path, entirely unmodified in shape (ADR-0068 D-9)
```

Every existing `MaterialKind`'s own fragment-shader body is unchanged
except the removal of `lit_textured.slang`'s/`pbr_direct_lit.slang`'s
own final `clamp(...,0,1)` (ADR-0068 D-7) — a pre-HDR clamp would
defeat the entire purpose of the new intermediate.

## Architectural Impact

**Yes** — one ADR, [ADR-0068](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
(`Proposed`), covering all of it as a single, tightly-coupled decision
(a new RHI resource type, three new/overloaded `CommandList` methods,
a narrowed swapchain-format-negotiation contract, `Renderer`'s own
public API change, and the tone-mapping/transfer-function math
contract — the real, complete surface confirmed during this Spec's
own final review, larger than first drafted, see "Final Review Round"
below) — unlike Spec 0023's own three-ADR split, this Spec's own
decisions all live inside the same RHI/RenderGraph/Renderer/Shader
System boundary with no natural Asset-System-style seam, so one ADR is
still the minimal, not merely convenient, choice.

## Alternatives Considered

See ADR-0068's own Alternatives Considered for the full, itemized
record (reusing `SampledTexture` instead of a new type; relaxing
`RenderTarget`'s write-only contract instead of a new type; ACES
filmic instead of Reinhard; relying on an `*_Srgb` target's automatic
encode instead of a manual one; forking the output contract instead of
sharing it; a new non-indexed draw call instead of a checked-in
fullscreen-triangle mesh) — not restated here per this repository's own
single-authoritative-source rule.

## Decisions for Human Review

Ten items, matching the human-directed drafting brief exactly. Every
recommendation is a proposal for Human Review to accept, amend, or
reject; formulas and full rationale live once, in ADR-0068.

| # | Question | Recommendation | Authoritative source |
|---|---|---|---|
| D1 | Scene-referred linear HDR intermediate — format and ownership | New, single-purpose `HdrColorTarget` RHI type, inheriting neither `RenderTarget` nor `SampledTexture` (two narrow capabilities of its own); new `HdrFormat::Rgba16Float`, one variant, defended by a real, executed `vkGetPhysicalDeviceFormatProperties()` check at creation — not asserted from memory alone; caller-created/owned (never internal to `Renderer`, which stays a stateless orchestrator) | ADR-0068 D-1/D-2 *(corrected — see Final Review Round)* |
| D2 | Windowed vs. offscreen — one shared output-transform pass, or two? | Share unconditionally — the pass is origin-agnostic by construction, exactly like today's existing draw pass already is; both passes go through the same, single, unmodified `RenderGraphBuilder`/`execute()` call pair — no ad hoc submit | ADR-0068 D-3 |
| D3 | Tone-mapping algorithm and fixed-exposure baseline — exact math contract | Fixed exposure (`kBaselineExposure = 1.0`, named constant, no auto-exposure), floored at zero, then per-channel Reinhard (`x / (1+x)`), then the exact piecewise sRGB OETF; alpha passes through unchanged | ADR-0068 D-5 *(edge cases added — see Final Review Round)* |
| D4 | Linear-to-display transfer — location and target format semantics | Manual, literal sRGB OETF applied in the output-transform shader; written to a `*_Unorm` final target only — `selectSurfaceFormat()`'s own candidate list is narrowed to exclude `*_Srgb` entirely (a real, disclosed Vulkan-Backend-private behavior change), never merely relying on `*_Unorm` being the current first preference | ADR-0068 D-6 *(corrected — see Final Review Round)* |
| D5 | New RenderGraph/RHI public capability needed? | Yes, and larger than first drafted: the new `HdrColorTarget` type + `HdrFormat` enum (D1), one new `Device` factory method, **three new/overloaded `CommandList` methods** (`transitionResource`, `beginRendering`, `bindTexture` — required because `execute()`'s own real dispatch code cannot pass `HdrColorTarget` through the existing `RenderTarget`/`SampledTexture` overloads without either reopening Guard 2 or a type-mismatch), and one new `ResourceBinding` field; `Renderer::drawFrame()`'s own signature gains one required parameter — a real, disclosed public API change, its first since ADR-0022's own Accepted Amendment. `RenderGraphBuilder`'s own public surface needs zero change | ADR-0068 D-1/D-3 *(materially expanded — see Final Review Round)* |
| D6 | Resize, format change, readback, resource-state transitions, descriptor-pool capacity | `HdrColorTarget` recreated on resize alongside the existing depth `Texture` (same trigger, zero-extent already handled upstream); every geometry Pipeline's `colorFormat` becomes the fixed HDR format, **removing** N geometry Pipelines from format-change churn entirely and adding exactly one (output-transform) — net peak descriptor-set pressure is *lower* than today's `2×(N+1)` formula, not merely unaffected; readback stays final-target-only, 8-bit, unmodified in shape; `HdrColorTarget` transitions `Undefined`→`ColorAttachmentOutput`→`ShaderRead` every frame, no new `ResourceState` enumerator | ADR-0068 D-1/D-4/D-9 *(descriptor-pool formula added — see Final Review Round)* |
| D7 | Existing six-golden compatibility/migration strategy | All six will shift byte values (a real sRGB OETF encode where none existed is not content-dependent) — a mandatory, human-reviewed re-capture of all six at Plan/Implementation time, each its own independent ADR-0042 two-phase capture/provenance/review cycle, categorized per ADR-0042's own golden-update-reason rule; never a batch/automatic acceptance | ADR-0068 Consequences |
| D8 | New verification scene and ADR-0042 golden flow | One new, dedicated scene with deliberately over-`1.0`-range light intensity/emissive value — proves the tone-map curve actually rolls off, not merely non-black; bootstrapped via ADR-0042's existing two-phase fixture-then-golden process, no new comparison methodology | this Spec |
| D9 | Windows/future-Android portability | `Rgba16Float`'s real-hardware-confirmed feature set is expected to hold identically on both, but portability is enforced by D1's own real, executed capability check on each real device at startup — not claimed as an unconditional guarantee needing no verification; zero new platform-specific WSI/extension surface beyond D4's own narrowed format list | ADR-0068 D-11 *(corrected — see Final Review Round)* |
| D10 | Explicit exclusions | Bloom, color grading, auto exposure, IBL, shadow, normal mapping, and any literal platform HDR *display* output are all out of scope — see Non-Goals above; reconfirmed intact by this Spec's own final review, no drift found | this Spec |

## Testing & Verification Plan

- **GPU-independent:** the tone-mapping curve (D3, including the
  zero-floor edge case) and sRGB transfer function (D4) each get a
  CPU-side reference implementation, unit-tested against hand-computed
  values at several inputs spanning negative, below-`1.0`, exactly-
  `1.0`, and well-above-`1.0` — matching ADR-0062's own "literal,
  complete formula, directly unit-testable" precedent; a shader-
  reflection-vs-C++-layout cross-check for the new `HdrColorTarget`'s
  own format/usage and the new `"output-transform"` descriptor
  contract, matching Spec 0022/0023's own precedent.
- **Headless/GPU integration:** initial `HdrColorTarget` creation
  (including the new `vkGetPhysicalDeviceFormatProperties()` capability
  check, D1)/resize; the two-pass RenderGraph compiles and executes
  without a Vulkan Validation Layers warning/error on all three
  existing `MaterialKind`s, both windowed and offscreen origins; a
  real, above-`1.0`-input pixel comparison proving the tone-map curve
  actually rolled off (not merely clipped) versus the pre-Spec
  behavior; a real N=6-inclusive-of-the-output-transform-Pipeline
  descriptor-pool stress test re-confirming D6's own re-derived peak
  formula; a real surface-format negotiation test confirming the
  narrowed `*_Unorm`-only candidate list (D4).
- **Image regression:** all six existing goldens re-captured, human-
  reviewed, and categorized per ADR-0042's golden-update-reason rule
  (D7); one new golden scene (D8) proving over-range roll-off.
- **Vulkan Validation Layers clean**, both Debug and Release, matching
  this repository's own unconditional Definition of Done requirement.
- **Module/link graph:** confirm `Atlantis::Renderer`'s own dependency
  set is unchanged (still Core/RHI/RenderGraph only, D1's own new type
  living entirely in RHI); confirm no Vulkan header/`Vk*` type leaks
  into `Renderer` or `RenderGraph` from the new `HdrColorTarget` type.

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
- **A future offscreen caller explicitly requesting an `*_Srgb`
  `OffscreenTargetCreateParams::format`** is a real, disclosed gap this
  Spec's own final review found but did not close — every existing
  call site already hardcodes `Rgba8Unorm`, so this is not a live
  conflict today, but `createOffscreenTarget()`'s own validation
  against D4's `*_Unorm`-only contract is left to the Plan, not decided
  here.
- **The exact `HdrColorTargetCreateError` enumerator names and the
  precise wording of the failure this Spec's own D1 capability check
  reports** are Implementation-level naming questions, not
  architectural ones — ADR-0068 D-2 fixes that the check must exist
  and must fail loudly, not the exact enum member names.

## Out of Scope / Future Work

- A second, higher-quality tone-mapping operator (ACES filmic or
  similar) as a named future candidate, registered in ADR-0068's own
  Alternatives Considered — not built here.
- Auto-exposure (histogram/eye-adaptation), bloom, color grading,
  image-based lighting, shadows, normal mapping, and literal platform
  HDR display output all remain named, separate future candidates —
  none is designed or scaffolded by this Spec.
- Android implementation of any part of this pipeline.

## Final Review Round — 2026-08-31

A centralized final review of this Spec and ADR-0068, conducted on
PR #113 itself, checked every recommendation directly against real,
current code rather than accepting the original draft's own reasoning
at face value. It found and corrected six real gaps — none changing
this Spec's own overall shape (still one Spec, one ADR, ten Decisions,
the same fundamental design), but several materially widening what D5
in particular actually requires. **None of these corrections is
self-approved** — Spec status stays `In Review`, ADR-0068 stays
`Proposed`; every corrected recommendation below is still a proposal
for Human Review to accept, amend, or reject, not a decision this
review made unilaterally.

1. **D1/D5 — `HdrColorTarget`'s own real `CommandList` surface was
   underspecified.** The original draft implied it could reuse
   `beginRendering(RenderTarget&,...)`/`bindTexture(const
   SampledTexture&,...)` unmodified. Real code
   (`execution.cpp:117-130,147`) shows `execute()`'s own dispatch is
   hardcoded to three binding kinds and calls `beginRendering()` with
   the `target` field specifically — `HdrColorTarget` cannot reuse
   either existing overload without inheriting `RenderTarget` (which
   would reopen Guard 2's own rejected "relax the write-only contract"
   alternative) or `SampledTexture` (whose `format()` return type
   cannot represent `HdrFormat::Rgba16Float`). Corrected: `HdrColorTarget`
   inherits neither; three new/overloaded `CommandList` methods are
   required (`transitionResource`, `beginRendering`, `bindTexture`).
2. **D2 (format guarantee) — the original draft asserted Vulkan's own
   mandatory-format-support table from memory, without independently
   re-verifying its exact wording this round.** Corrected: real
   `vulkaninfo` evidence against this repository's own target GPU
   corroborates the claim, but the Spec no longer rests on the
   assertion alone — a real, executed
   `vkGetPhysicalDeviceFormatProperties()` capability check at
   `HdrColorTarget` creation is now a required part of the design,
   failing loudly on any device where the expectation turns out false.
3. **D4/D6 — the "write to a `*_Unorm` target only" rule had no real
   enforcement mechanism.** `selectSurfaceFormat()`'s own real
   preference list can and would select an `*_Srgb` format on a real
   surface reporting only those variants, silently double-encoding.
   Corrected: the candidate list itself narrows to exclude `*_Srgb`
   entirely, with a defined, existing `Result`-based failure mode if
   no `*_Unorm` variant is available.
4. **D6 — the descriptor-pool capacity claim was asserted, not
   derived.** Corrected: an exact, re-derived peak formula, showing
   this Spec's own design *reduces* worst-case format-change-churn
   pressure versus Spec 0021's own `2×(N+1)` formula, not merely
   leaves it "unaffected."
5. **D3 — numeric edge cases (negative input, alpha) were unstated.**
   Corrected: an explicit zero-floor before the tone-map curve (never
   reintroducing the removed pre-tonemap `[0,1]` clamp), and an
   explicit alpha pass-through statement.
6. **A new Decision, D-10 in ADR-0068, was needed** for the
   output-transform shader pair's own descriptor contract (one binding,
   no uniform buffer, no push constant) and an explicit confirmation
   that all three `MaterialKind`s genuinely share it — implied by the
   original draft's design but never stated as its own contract.

Also reconfirmed, with no change needed: RenderGraph's own public
builder surface (`declarePass`/`declareResource`/`reads`/`writes`/
`compile`) requires zero widening — the two-pass ordering is fully
provable against its existing, already-general dependency-derivation
machinery; Non-Goals (D10) remain intact, no drift found; readback and
the image-regression harness's own 8-bit shape are confirmed
unaffected, with resize/zero-extent/`DeviceLost` handling confirmed to
inherit the depth `Texture`'s own existing, already-safe pattern for
free.
