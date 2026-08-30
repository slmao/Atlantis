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
  `VK_FORMAT_R16G16B16A16_SFLOAT`'s mandatory-format-support guarantee
  for color-attachment + sampled-image + blend, optimal tiling, holds
  identically on every Vulkan 1.0-conformant implementation — no
  capability query, no fallback format, no Android-specific concern
  found.
- **Other:** the six existing goldens' own 8-bit `Rgba8Unorm` PNG
  comparison shape (`ADR-0042`) is unchanged — only their *byte
  content* is expected to shift (see Decisions for Human Review, D7).

## Real-code evidence (summary — see ADR-0068 for full citations)

| Claim | Evidence |
|---|---|
| RHI `Format` has exactly four 8-bit variants, no HDR/float format anywhere | `src/rhi/include/atlantis/rhi/types.h:26-32` |
| `LitTextured`/`PbrDirectLit` each apply exactly one post-accumulation `clamp(...,0,1)`, no gamma-encode | `lit_textured.slang:105-107`, `pbr_direct_lit.slang:179-183` |
| `UnlitTextured` applies no transformation at all | `textured_quad.slang:52-55` |
| Swapchain format negotiation's own fixed preference tries `*_Unorm` before any `*_Srgb` variant | `vulkan_presentation.cpp:111-116` |
| Every image-regression fixture hardcodes `Format::Rgba8Unorm` explicitly | `minimal_cube_fixture.h:40`, `textured_quad_fixture.h:63` |
| `RenderTarget` is write-only by its own class contract; `execute()`'s Guard 2 enforces it | `render_target.h:13-14`; `execution.h:103-107` |
| No existing RHI type is both a renderable color attachment and a later-sampled input | repo-wide read of `render_target.h`, `texture.h`, `sampled_texture.h` |
| `Renderer` is a stateless orchestrator — retains no GPU resource across calls | `renderer.h:14-25` (ADR-0022) |
| `CommandList`'s only draw call is `drawIndexed()` — no non-indexed `draw()` | `command_list.h:77` |
| The depth `Texture` is recreated on resize, at a real, fixed call site | `runtime_application.cpp:510-516` |
| The image-regression harness is 8-bit-per-channel PNG only (`stbi_load`/`stbi_write_png`, 4 channels) | `png_codec.cpp:37,72` |
| ADR-0042's comparison policy is zero channel tolerance, zero failing-pixel budget | `adr/0042-...md`, Decision |
| ADR-0067 D-6 names this Spec's own correct next step explicitly | `adr/0067-...md`, D-6 |

No unresolvable architectural conflict was found; see ADR-0068's own
Alternatives Considered for the paths this Spec rejected and why.

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
        RenderTarget (*_Unorm only, swapchain- or OffscreenTarget-backed —
        no branch on which, ADR-0068 D-3)
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
(a new RHI resource type, a new RenderGraph capability, `Renderer`'s
own public API change, and the tone-mapping/transfer-function math
contract) — unlike Spec 0023's own three-ADR split, this Spec's own
decisions all live inside the same RHI/RenderGraph/Renderer/Shader
System boundary with no natural Asset-System-style seam, so one ADR is
the minimal, not merely convenient, choice.

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
| D1 | Scene-referred linear HDR intermediate — format and ownership | New, single-purpose `HdrColorTarget` RHI type (color-attachment-output + later-sampled, two narrow capabilities); new `HdrFormat::Rgba16Float`, one variant; caller-created/owned (never internal to `Renderer`, which stays a stateless orchestrator) | ADR-0068 D-1/D-2 |
| D2 | Windowed vs. offscreen — one shared output-transform pass, or two? | Share unconditionally — the pass is origin-agnostic by construction, exactly like today's existing draw pass already is | ADR-0068 D-3 |
| D3 | Tone-mapping algorithm and fixed-exposure baseline — exact math contract | Fixed exposure (`kBaselineExposure = 1.0`, named constant, no auto-exposure) then per-channel Reinhard (`x / (1+x)`) — a single, literal, hand-verifiable rational function | ADR-0068 D-5 |
| D4 | Linear-to-display transfer — location and target format semantics | Manual, literal sRGB OETF applied in the output-transform shader; written to a `*_Unorm` final target only — never relies on an `*_Srgb` target's automatic hardware encode (closes a real double-encode/no-encode risk) | ADR-0068 D-6 |
| D5 | New RenderGraph/RHI public capability needed? | Yes — the new `HdrColorTarget` type (D1) plus one new `ResourceBinding` field; `Renderer::drawFrame()`'s own signature gains one required parameter — a real, disclosed public API change, its first since ADR-0022's own Accepted Amendment | ADR-0068 D-1/D-3 |
| D6 | Resize, format change, readback, resource-state transitions | `HdrColorTarget` recreated on resize alongside the existing depth `Texture` (same trigger); every geometry Pipeline's `colorFormat` becomes the fixed HDR format, narrowing the existing format-change rebuild to one Pipeline (the output-transform one); readback stays final-target-only, 8-bit, unmodified in shape; `HdrColorTarget` transitions `Undefined`→`ColorAttachmentOutput`→`ShaderRead` every frame, no new `ResourceState` enumerator | ADR-0068 D-4/D-8/D-9 |
| D7 | Existing six-golden compatibility/migration strategy | All six will shift byte values (a real sRGB OETF encode where none existed is not content-dependent) — a mandatory, human-reviewed re-capture of all six at Plan/Implementation time, categorized per ADR-0042's own golden-update-reason rule; never silently accepted as "no visible difference" | ADR-0068 Consequences |
| D8 | New verification scene and ADR-0042 golden flow | One new, dedicated scene with deliberately over-`1.0`-range light intensity/emissive value — proves the tone-map curve actually rolls off, not merely non-black; bootstrapped via ADR-0042's existing two-phase fixture-then-golden process, no new comparison methodology | this Spec |
| D9 | Windows/future-Android portability | `Rgba16Float`'s mandatory-format-support guarantee holds identically on both; zero new platform-specific WSI/extension surface — Vulkan Backend's existing WSI boundary untouched | ADR-0068 D-10 |
| D10 | Explicit exclusions | Bloom, color grading, auto exposure, IBL, shadow, normal mapping, and any literal platform HDR *display* output are all out of scope — see Non-Goals above | this Spec |

## Testing & Verification Plan

- **GPU-independent:** the tone-mapping curve (D3) and sRGB transfer
  function (D4) each get a CPU-side reference implementation, unit-
  tested against hand-computed values at several inputs spanning below-
  `1.0`, exactly-`1.0`, and well-above-`1.0` — matching ADR-0062's own
  "literal, complete formula, directly unit-testable" precedent; a
  shader-reflection-vs-C++-layout cross-check for the new
  `HdrColorTarget`'s own format/usage, matching Spec 0022/0023's own
  precedent.
- **Headless/GPU integration:** initial `HdrColorTarget` creation/
  resize; the two-pass RenderGraph compiles and executes without a
  Vulkan Validation Layers warning/error on all three existing
  `MaterialKind`s, both windowed and offscreen origins; a real,
  above-`1.0`-input pixel comparison proving the tone-map curve
  actually rolled off (not merely clipped) versus the pre-Spec
  behavior.
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

## Out of Scope / Future Work

- A second, higher-quality tone-mapping operator (ACES filmic or
  similar) as a named future candidate, registered in ADR-0068's own
  Alternatives Considered — not built here.
- Auto-exposure (histogram/eye-adaptation), bloom, color grading,
  image-based lighting, shadows, normal mapping, and literal platform
  HDR display output all remain named, separate future candidates —
  none is designed or scaffolded by this Spec.
- Android implementation of any part of this pipeline.
