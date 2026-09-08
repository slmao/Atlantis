# Spec: Manual Camera Exposure Foundation

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-08
- **Related Plan(s):** None yet — Plan 0031 may be drafted only once
  [PR #136](https://github.com/slmao/Atlantis/pull/136) itself has
  merged to `main`, not before
- **Related ADR(s):** [ADR-0075: Manual Camera Exposure Data and Output-Transform Contract](../adr/0075-manual-camera-exposure-data-and-output-transform-contract.md) (`Accepted`); amends [ADR-0068](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md) D-10 via that ADR's own [Accepted Amendment — 2026-09-08](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md#accepted-amendment--2026-09-08) section (`Accepted`, approved independently of ADR-0075 and this Spec) — see this Spec's own [Human Review Approval — 2026-09-08](#human-review-approval--2026-09-08) below

## Summary

Adds one new scalar to `atlantis::world::Camera`, `exposureCompensationEv`
(EV, default `0.0`), and threads it from the active `Camera` through
Runtime into the existing output-transform RenderGraph pass
(`shaders/output_transform_unorm`/`output_transform_srgb`,
[ADR-0068](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
D-5), replacing the fixed `kBaselineExposure = 1.0` shader constant with
a per-frame exposure multiplier. `Renderer::drawFrame()` computes
`exposureMultiplier = std::exp2(exposureCompensationEv)` once, in C++,
and the output-transform push constant carries that already-computed
multiplier — the shader itself performs one multiply and nothing else,
never its own `exp2`. At `exposureCompensationEv = 0.0`,
`std::exp2(0.0f) == 1.0f` exactly, matching today's fixed literal. ADR-
0068 D-5 itself already names this exact follow-up ("a future spec may
add one, replacing `kBaselineExposure`'s fixed value with a computed
one without changing this Decision's own curve") — this Spec is that
follow-up, not a new architectural direction.

## Motivation / Problem Statement

Every `MaterialKind`'s accumulated linear HDR radiance is tone-mapped
with a single, permanently fixed exposure multiplier (`kBaselineExposure
= 1.0`, a `.slang`-file compile-time constant, duplicated identically in
both output-transform shader variants). There is no way — for a scene
author, a future editor tool, or a test — to make a scene render
brighter or darker without editing shader source and recompiling. A
`Camera` already carries per-camera framing data (`fovYRadians`,
`nearZ`, `farZ`); exposure is the same kind of per-camera concern and
belongs in the same place.

## Goals

- Add `exposureCompensationEv` to `atlantis::world::Camera`, default
  `0.0`.
- Define the exact scene source/artifact schema and migration for this
  new field.
- Thread the active `Camera`'s exposure value through Runtime into the
  existing output-transform RenderGraph pass, reusing the existing
  push-constant mechanism.
- Preserve Reinhard tone-mapping, the sRGB transfer function, the HDR
  intermediate format, light-intensity semantics, and every Material
  contract exactly as they are today.
- Fix a finite, bounded EV input range for this Spec's own authoring
  domain, with an explicit, real-arithmetic safety-margin justification
  — not chosen by feel, and not claimed as the unique mathematically
  possible safe range.
- Guarantee `exposureCompensationEv = 0.0` reproduces every existing
  golden byte-identically, confirmed by a real capture-compare run, not
  asserted from arithmetic alone.

## Non-Goals

- Aperture, shutter speed, ISO, or any other individual physical
  exposure parameter — `exposureCompensationEv` is a single compensation
  scalar, not a physical-camera model.
- Auto-exposure, luminance histograms, or eye-adaptation state (ADR-0068
  D-5 already excludes these; unchanged here).
- Bloom, a color-grading LUT, TAA, or depth of field.
- Any migration of light intensities or other quantities to physical
  (photometric/radiometric) units — light intensity keeps its existing,
  unitless meaning (Spec 0019) exactly as today.
- A new golden image, unless Requirements below show the existing
  discriminative tests cannot prove the feature works (see Testing &
  Verification Plan).

## Requirements

### Functional

1. **`atlantis::world::Camera`** gains one new field:
   `float exposureCompensationEv = 0.0f;`. No other `Camera` field
   changes.
2. **Exposure multiplier — computed once, in C++, in exactly one
   place:** `Renderer::drawFrame()` computes
   `exposureMultiplier = computeExposureMultiplier(outputTransformExposureCompensationEv)`
   once per call (i.e. once per frame), where `computeExposureMultiplier()`
   is a small, named, `[[nodiscard]]` free function
   (`return std::exp2(exposureCompensationEv);`) living in a new,
   private Renderer header (Requirement 9). This is the **only**
   implementation of the EV→multiplier mapping anywhere in production
   code — the shader never calls `exp2`, and Runtime never
   reimplements it. `exposedColor = linearColor * exposureMultiplier`
   replaces `exposedColor = linearColor * kBaselineExposure` in both
   `output_transform_unorm.slang` and `output_transform_srgb.slang`;
   the shader's own change is a variable rename plus reading the
   push-constant value, nothing else. The Reinhard formula and the
   sRGB OETF are byte-for-byte unchanged.
3. **Scene source schema (`.scene.txt`):** a camera node's fixed-arity
   token grammar (currently exactly 14 tokens:
   `name parent position=<3> rotation=<3> scale=<3> camera_fov_y=<f> camera_near_z=<f> camera_far_z=<f>`)
   gains one **optional**, trailing 15th token,
   `camera_exposure_ev=<f>`. A camera node with 14 tokens (the field
   omitted) means `exposureCompensationEv = 0.0`, byte-for-byte
   equivalent to today's grammar — **no existing `.scene.txt` file
   requires editing.** A camera node with 15 tokens sets the field
   explicitly. 16/17-token light-node grammar is unaffected (no
   token-count collision).
4. **Scene artifact schema (binary, `scene_artifact.cpp`):**
   `kSceneArtifactSchemaVersion` bumps from `3` to `4` (ADR-0045: a
   format change gets a mandatory version bump, an unrecognized version
   is rejected outright — no migration mechanism, matching this
   codebase's existing policy). The per-node camera block (currently
   16 bytes: `hasCameraFlag`, `fovY`, `nearZ`, `farZ`) gains one
   trailing 4-byte little-endian float, `exposureCompensationEv`,
   written unconditionally (`0.0f` when the node has no camera, mirroring
   the existing `fovY`/`nearZ`/`farZ` fields' own "written even when
   absent" convention). `kSceneArtifactNodeRecordSizeBytes` grows from
   112 to 116. Decode independently re-validates
   `std::isfinite(exposureCompensationEv)` and the fixed range (below),
   the same "never trust the cooker" pattern every other field in this
   decoder already follows.
5. **`atlantis::asset_system::DecodedCamera`** gains
   `float exposureCompensationEv = 0.0f;`. `World::fromValidatedSceneData()`
   (`scene_instantiation.cpp`) passes it through to `world::Camera`'s
   4th field at construction.
6. **Range enforcement, cook/decode:** `cookScene()` (source →
   artifact) and `decodeSceneArtifact()` (artifact → CPU) each
   independently reject a non-finite or out-of-`[kExposureCompensationEvMin,
   kExposureCompensationEvMax]` value by **reusing the existing
   `SceneCookError::NonFiniteValue` / `SceneArtifactDecodeError::NonFiniteValue`
   enumerator** — never silently clamped. No new enumerator is added:
   this exactly mirrors the established, already-shipped precedent in
   the same two functions, where a light's color/intensity/range
   *domain* violation (e.g. `colorR > 1.0`, itself a finite value) is
   already reported via the same `NonFiniteValue` enumerator as a
   genuinely non-finite value would be, rather than a dedicated
   "OutOfDomain" case.
7. **Range enforcement, `Renderer::drawFrame()`'s own public entry
   point:** because `drawFrame()` is called directly by many non-asset
   callers (Runtime is the only caller that goes through cook/decode;
   every example binary, every image-regression fixture, and every
   direct-call unit/GPU test calls `drawFrame()` with hand-written
   values, bypassing Requirement 6 entirely), `drawFrame()` documents
   and enforces its own precondition on the new parameter: `ATLANTIS_CHECK`
   that `outputTransformExposureCompensationEv` is finite and within
   `[kExposureCompensationEvMin, kExposureCompensationEvMax]`, at the
   top of the function body, before any RenderGraph pass is declared.
   A violation is a programmer error (a direct caller passing a bad
   literal), not a recoverable runtime condition — matching AGENTS.md's
   "programmer errors are assertions, not error returns" rule and this
   file's own existing `ATLANTIS_CHECK_MSG` precedent (e.g. the
   `skyPipeline`/`environmentLighting` precondition already in
   `drawFrame()`). `kExposureCompensationEvMin`/`Max` are defined
   independently in Atlantis::Renderer (a local constant, mirroring
   `kBackgroundClearColor`'s own existing file-local-constant pattern
   in `renderer.cpp`) with the *same numeric values* as
   Atlantis::AssetSystem's own copy (Requirement 6) — deliberately
   duplicated, not shared, because Atlantis::AssetSystem and
   Atlantis::Renderer must not depend on each other (module boundary
   rule); each copy's own comment cross-references the other by name,
   not by code.
8. **`Renderer::drawFrame()`** gains one new parameter,
   `float outputTransformExposureCompensationEv`, placed immediately
   after the two existing output-transform-only parameters
   (`outputTransformPipeline`, `outputTransformSampler`) — grouping all
   three output-transform-scoped inputs together at one call-site
   position. This parameter carries the raw EV (self-documenting,
   matching `Camera`'s own field name/semantics and the precondition in
   Requirement 7); `drawFrame()` derives the multiplier internally
   (Requirement 2) — callers never compute or pass a multiplier
   themselves. This is the **only** signature change; every other
   parameter, the `DrawItem` shape, and the camera uniform buffer layout
   are unchanged.
9. **Output-transform push constant:** the output-transform `Pipeline`
   gains a single 4-byte, `Fragment`-stage-read push constant,
   `ExposurePushConstants { float exposureMultiplier; }`, populated once
   per frame inside the "output_transform" pass's execute lambda with
   the value `computeExposureMultiplier()` (Requirement 2) already
   produced — never the raw EV. This reuses the existing, already-
   generic RHI push-constant mechanism
   (`PipelineCreateParams::pushConstantSizeBytes`,
   `CommandList::pushConstant()`) exactly as every `MaterialKind`
   pipeline already does — no RHI change.
   `outputTransformExpectedDescriptorContract()` (descriptor
   *bindings*) is unchanged (Requirement 11 below fixes the exact
   push-constant *contract*, confirmed by a real compile, not left
   open).
10. **Runtime** reads `exposureCompensationEv` from the same
    `atlantis::world::Camera` value it already reads `fovYRadians`/
    `nearZ`/`farZ` from (`runtime_application.cpp`'s existing
    `cameraComponentResult` local) and passes it straight through to
    `drawFrame()` as the raw EV — no transformation, no clamping at
    this call site (clamping already happened at cook/decode time,
    Requirement 6; `drawFrame()`'s own precondition, Requirement 7, is
    the second, independent gate for this and every other caller).
11. **`compile_and_validate.cpp`'s push-constant contract — fixed by a
    real `slangc` probe, not left as an open question.** A Plan-stage
    probe (real `slangc`, this repository's own toolchain, temporary
    files, never committed — see this Spec's own drafting record) added
    the final `ExposurePushConstants` declaration to temporary copies of
    both output-transform `.slang` files and compiled both stages of
    both files. Real, confirmed result: `exposurePushConstants` (a
    `pushConstantBuffer`-kind binding) appears **identically in both
    the vertex and fragment stage's own reflected JSON**, at
    `{offset: 0, size: 4}`, with **no `"used"` field on either** — the
    same "no `used` field on a `pushConstantBuffer` entry, present
    regardless of real per-stage usage" behavior
    `slang_json_transform.cpp`'s own top comment already documented for
    the unrelated PBR shader pair. Consequently:
    `validatePushConstantsForVertexStage()`'s current `"output-transform-unorm"`/
    `"output-transform-srgb"` branch (today: expects a genuinely empty
    range list) must instead expect exactly one entry,
    `PushConstantRange{.offsetBytes = 0, .sizeBytes = 4, .stage = ShaderStage::Vertex}`
    — present, stray, and never read by `vertexMain`, the same
    "harmless, unread, still present in reflection" shape this
    codebase already accepts for `PbrDirectLit`'s own stray
    fragment-side entry (mirrored, not new). A new fragment-stage check
    (mirroring `validatePushConstantsForFragmentStage()`'s existing
    PBR-only call, widened or paralleled to also cover
    `"output-transform-unorm"`/`"output-transform-srgb"`) must expect
    `PushConstantRange{.offsetBytes = 0, .sizeBytes = 4, .stage = ShaderStage::Fragment}`
    — this one is real, genuine usage.
12. Every existing `Renderer::drawFrame()` call site (Runtime, both
    example binaries, every image-regression fixture, every direct-call
    unit/GPU test) passes `0.0f` explicitly — mechanical, zero-behavior
    -change, and required for the build to compile against the widened
    signature, and to satisfy Requirement 7's own precondition (`0.0f`
    is trivially within range).

### Non-functional

- **Performance:** one `std::exp2()` call and one 4-byte
  `vkCmdPushConstants` call once per frame (not per pixel); one extra
  multiply per output pixel in an already-existing fullscreen fragment
  shader. Immaterial.
- **Memory:** +4 bytes per scene-artifact node record; +4 bytes per
  `world::Camera`/`DecodedCamera` instance. No new GPU resource, no new
  descriptor binding.
- **Portability (Vulkan-only Phase 1):** a 4-byte push constant is far
  under every Vulkan implementation's guaranteed minimum
  `maxPushConstantsSize` (128 bytes); no portability risk on Windows or
  a future Android target.
- **Other — behavioral compatibility:** `exposureCompensationEv = 0.0`
  MUST reproduce all 10 existing image-regression goldens byte-for-byte
  identically. `std::exp2(0.0f) == 1.0f` is an exact IEEE-754 identity
  and explains *why* this is expected, but it is not, by itself, proof
  that every compiler/GPU/driver combination in this codebase's real
  build/test matrix actually reproduces it end to end — the gate is a
  real, executed capture-compare run against the 10 committed goldens
  (see Testing & Verification Plan), not the arithmetic alone.

## Proposed Design

```cpp
// src/renderer/src/exposure.h (new, private -- mirrors
// pbr_push_constants.h's own private-header precedent: not a
// cross-module contract type, Renderer's own sole real consumer).
#pragma once
#include <cmath>
#include <cstddef>
#include <type_traits>

namespace atlantis::renderer {

// Requirement 7: same numeric values as AssetSystem's own independent
// copy (cook_scene.cpp/scene_artifact.cpp) -- deliberately duplicated,
// not shared, across the AssetSystem/Renderer module boundary.
inline constexpr float kExposureCompensationEvMin = -16.0f;
inline constexpr float kExposureCompensationEvMax = 16.0f;

// The one and only EV->multiplier mapping in production code
// (Requirement 2). Never called per-pixel; drawFrame() calls this
// exactly once per frame.
[[nodiscard]] inline float computeExposureMultiplier(float exposureCompensationEv) {
  return std::exp2(exposureCompensationEv);
}

struct alignas(4) ExposurePushConstants {
  float exposureMultiplier = 1.0f;
};
static_assert(std::is_standard_layout_v<ExposurePushConstants>);
static_assert(sizeof(ExposurePushConstants) == 4);

}  // namespace atlantis::renderer
```

```cpp
// renderer.cpp, top of drawFrame(), before any RenderGraphBuilder call
// (Requirement 7):
ATLANTIS_CHECK(std::isfinite(outputTransformExposureCompensationEv) &&
               outputTransformExposureCompensationEv >= kExposureCompensationEvMin &&
               outputTransformExposureCompensationEv <= kExposureCompensationEvMax);
const float exposureMultiplier = computeExposureMultiplier(outputTransformExposureCompensationEv);
```

```cpp
// output_transform pass execute lambda (renderer.cpp), replacing the
// pass's current body -- one new line, same call order as every other
// pass's own bindPipeline-then-pushConstant precedent:
cmd.bindPipeline(outputTransformPipeline);
ExposurePushConstants payload{exposureMultiplier};
cmd.pushConstant(&payload, sizeof(payload));
cmd.bindVertexBuffer(fullscreenTriangleVertexBuffer);
cmd.bindIndexBuffer(fullscreenTriangleIndexBuffer);
cmd.bindTexture(0, hdrColorTarget, outputTransformSampler);
cmd.drawIndexed(3);
```

```
// shaders/output_transform_unorm.slang and output_transform_srgb.slang,
// both variants identically (mirrors PbrPushConstants' own declaration
// idiom in pbr_direct_lit.slang; real slangc-confirmed shape, Requirement 11):
struct ExposurePushConstants {
    float exposureMultiplier;
};
[[vk::push_constant]]
ConstantBuffer<ExposurePushConstants> exposurePushConstants;

// fragmentMain, replacing the fixed kBaselineExposure literal -- no
// exp2 call anywhere in this file:
float3 exposedColor = linearColor * exposurePushConstants.exposureMultiplier;
// tonemapped = exposedColor / (1 + exposedColor)  -- unchanged
```

Data flow, source to pixel:

```
.scene.txt camera_exposure_ev=<f>  (optional, default-omitted = 0.0)
  -> parseSceneSource() -> DecodedCamera.exposureCompensationEv
  -> cookScene() (finite + range check, NonFiniteValue) -> scene artifact byte 112..115
  -> decodeSceneArtifact() (independent finite + range re-check, NonFiniteValue)
  -> World::fromValidatedSceneData() -> world::Camera.exposureCompensationEv
  -> RuntimeApplication::runFrame() reads cameraComponent.exposureCompensationEv
  -> Renderer::drawFrame(..., outputTransformExposureCompensationEv)
       ATLANTIS_CHECK(finite && in range)              -- Requirement 7
       exposureMultiplier = computeExposureMultiplier(ev)  -- Requirement 2, the ONE exp2() call
  -> output_transform pass execute lambda:
       cmd.bindPipeline(outputTransformPipeline);
       cmd.pushConstant(&ExposurePushConstants{exposureMultiplier}, 4)
  -> vkCmdPushConstants (existing, generic RHI mechanism)
  -> fragmentMain: linearColor * exposurePushConstants.exposureMultiplier,
     then unchanged Reinhard/sRGB -- no exp2 in the shader
```

**Fixed EV authoring range** — `kExposureCompensationEvMin = -16.0f`,
`kExposureCompensationEvMax = +16.0f`, equivalently a multiplier range
of `[2^-16, 2^16]`. This is an **explicit product/authoring-domain
policy this Spec selects**, not a claim of the unique mathematically
possible safe range — real IEEE-754 binary32/binary16 facts of this
exact pipeline establish that the policy leaves a large, explicitly-
computed safety margin, which is what justifies picking it now instead
of deferring the question:

- The HDR intermediate `HdrColorTarget` is `HdrFormat::Rgba16Float`
  (ADR-0068 D-2) — its own largest finite representable magnitude is
  `65504` (binary16 max). No `linearColor` value this pipeline can ever
  sample from it exceeds that.
- `exposedColor = linearColor * exposureMultiplier` must stay a finite
  binary32 value for every representable `linearColor` up to `65504`.
  Binary32's max finite value is `3.4028234663852886×10^38`. At the
  chosen ceiling, `exposureMultiplier = 2^16 = 65536`, so the worst-case
  product is `65504 * 65536 ≈ 4.29×10^9` — roughly `7.9×10^28` times
  smaller than binary32's max. The real, hard overflow ceiling for this
  exact multiplication (solving `65504 * exposureMultiplier <
  3.4028234663852886×10^38`) is `exposureMultiplier < 2^~112` — this
  Spec's own `2^16` ceiling is nowhere near that limit, and nothing
  about `Rgba16Float`'s own bit width *uniquely determines* `16` as the
  chosen exponent; a policy of `2^8`, `2^32`, or any other value well
  under `2^112` would be equally safe from overflow. `±16` is fixed
  here as this Spec's own selected, finite authoring domain — generous
  enough to be far more permissive than any photographic exposure-
  compensation dial (typically ±3 to ±5 stops), while being an explicit,
  reviewable, round policy choice rather than an open-ended range.
- The negative side carries no overflow risk at all (the product only
  shrinks); `-16` is chosen symmetrically with the positive side for a
  simple, single authoring range, not because binary32's own
  underflow-to-subnormal boundary independently demands `-16`
  specifically (that boundary is far more permissive, `exposureMultiplier`
  itself stays a normal float down to roughly `2^-126`).
- A future spec may widen or narrow this authoring range without any
  numeric-safety obstacle up to the real `~2^112` ceiling derived
  above; `[-16, +16]` is this Spec's own chosen starting policy, stated
  as such.

## Architectural Impact

**Yes** — this Spec changes a public API (`Renderer::drawFrame()`
signature), a data format (scene source grammar + binary artifact
schema + `kSceneArtifactSchemaVersion` bump), and a backend-abstraction
contract detail (the output-transform `Pipeline`'s push-constant
layout, previously fixed at zero — [ADR-0068](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
D-10). See [ADR-0075](../adr/0075-manual-camera-exposure-data-and-output-transform-contract.md)
(`Accepted`), which records the Camera-data/scene-schema/`drawFrame()`
decision, and ADR-0068's own [Accepted Amendment — 2026-09-08](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md#accepted-amendment--2026-09-08)
(`Accepted`), which amends D-10 specifically for the push-constant
change — two separate governance records because D-10 is `Accepted`
text this Spec must not silently rewrite; see that Amendment's own
Context for why it is not folded into ADR-0075. No third architectural
surface was found in the real code investigated for this Spec.

This Spec was checked against every `Accepted` ADR whose scope it
touches (ADR-0045 data-format versioning, ADR-0048 World module
boundary, ADR-0053 AssetSystem/World dependency direction, ADR-0067/
ADR-0072 push-constant precedent, ADR-0068 D-5/D-6/D-10 tone-mapping and
output-transform contract) and found **no conflict** beyond D-10's own
now-amended clause — ADR-0068 D-5 itself explicitly names "a future
spec... replacing `kBaselineExposure`'s fixed value with a computed
one" as the anticipated next step, which is exactly this Spec's own
proposal.

**Three separate Human Review approvals are required before
Implementation, not one:** this Spec (`Draft` → `Approved`), ADR-0075
(`Proposed` → `Accepted`), and ADR-0068's own new Amendment (`Proposed`
→ `Accepted`) are each reviewed and approved independently — the
Amendment is submitted now, alongside this Spec and ADR-0075, before
any implementation, not written after the fact to describe what was
already built.

## Alternatives Considered

- **A per-frame camera uniform-buffer field instead of a push
  constant** (mirroring how `CameraWorldPositionData` reaches the
  geometry pass): rejected — the output-transform `Pipeline` has no
  uniform buffer at all today (`hasCameraUniformBinding = false`,
  ADR-0068 D-10), and the existing RHI push-constant mechanism
  (`pushConstantSizeBytes`, `CommandList::pushConstant()`) already
  supports exactly this shape with zero RHI change, matching this
  Spec's own explicit direction to prefer the existing push-constant
  capability. Adding a uniform buffer would need a new descriptor
  binding, a new buffer resource, and a `hasCameraUniformBinding = true`
  change this pass has never needed — real, unnecessary surface a push
  constant avoids entirely.
- **A required (non-optional) 15th scene-source token**: rejected — it
  would force every existing `.scene.txt` file with a camera node to be
  edited before it could cook again, for a field whose entire point is
  to default to today's exact behavior. The optional-trailing-token
  design (Requirement 3) needs no such migration.
- **Computing `exp2(ev)` in the shader instead of C++** (this Spec's
  own first draft): rejected on convergence — it leaves the formula's
  one real implementation harder to unit-test without a GPU, and gives
  no benefit over computing it once, in C++, per frame, then pushing
  the already-computed multiplier — the design this Spec now fixes
  (Requirement 2).
- **A new, dedicated error enumerator for an out-of-range EV** (this
  Spec's own first draft): rejected — `cookScene()`/`decodeSceneArtifact()`
  already reuse `NonFiniteValue` for a light color/intensity *domain*
  violation that is not itself non-finite; a dedicated EV enumerator
  would be an inconsistent, one-off exception to that established
  precedent for no real benefit.
- **Framing `[-16, +16]` as "the" numerically-derived range, uniquely
  tied to `Rgba16Float`'s bit width** (this Spec's own first draft):
  rejected as an inaccurate argument — the real overflow ceiling is
  `~2^112`, and nothing about `Rgba16Float`'s own `16`-bit width
  uniquely forces `16` as the chosen exponent (see Proposed Design).
  Restated as what it actually is: an explicit, finite authoring-domain
  policy with a large, computed safety margin below the true ceiling.
- **A wider or narrower fixed EV range** (e.g. ±3 matching a
  photographic dial, or ±126 matching binary32's raw normal-exponent
  range): still rejected in favor of ±16 for this Spec's own stated
  policy reasons (Proposed Design) — neither alternative changes the
  underlying numeric-safety facts, and this Spec does not claim ±16 is
  the only workable choice.

## Testing & Verification Plan

- **GPU-independent formula test — one fixed location, one fixed
  implementation under test:** `tests/renderer/exposure_multiplier_tests.cpp`
  (new file, sibling to the existing `tests/renderer/renderer_ownership_tests.cpp`)
  includes `src/renderer/src/exposure.h` by relative path (the same
  established "test reaches a module's own private header directly"
  pattern `pbr_push_constants.h` already uses from
  `tests/runtime/pbr_reflection_cross_check_tests.cpp`) and calls
  `computeExposureMultiplier()` directly — never a hand-reimplemented
  copy — at `exposureCompensationEv = -1, 0, +1`, asserting the exact
  results `0.5`, `1.0`, `2.0`. This is the single GPU-independent test
  for the EV→multiplier formula; no second candidate location remains.
- **`tone_mapping_reference.h`'s own, separate, pre-existing purpose is
  untouched in kind, only widened in shape:** `reinhardTonemap()` gains
  a second parameter, `float exposureMultiplier = 1.0f` (default
  preserves every existing call site unchanged), so real-GPU tests can
  compute an expected tonemapped value at a non-default exposure. This
  file's own role — an independent, hand-verifiable CPU transcription
  used as a real-GPU test oracle (ADR-0068 D-5's own established
  methodology) — is unchanged; it is not a second implementation of
  `computeExposureMultiplier()` (the multiplier it is given is always
  computed by calling that same function, never re-derived).
- **Cook/decode range-check tests**: extend the existing
  `tests/asset_system/cook_scene_tests.cpp` (cook-time,
  `SceneCookError::NonFiniteValue`) and
  `tests/asset_system/decode_scene_tests.cpp` (decode-time,
  `SceneArtifactDecodeError::NonFiniteValue`) at the exact boundary
  values (`-16.0`, `+16.0`, `±16.0001`, non-finite); extend
  `tests/asset_system/scene_source_tests.cpp` for the new optional
  15th-token grammar (14-token and 15-token camera nodes both parse
  correctly).
- **`Renderer::drawFrame()`'s own precondition** (Requirement 7): a new
  GPU-independent death-test-style assertion test (mirroring this
  codebase's existing `ATLANTIS_CHECK`-triggers-abort test precedent,
  e.g. `windows_platform.cpp`'s double-shutdown test) confirming an
  out-of-range or non-finite `outputTransformExposureCompensationEv`
  aborts via `ATLANTIS_CHECK`, in `tests/renderer/`.
- **Real-GPU luminance-monotonicity test**: one new `[gpu]` test
  rendering the same fixed scene at two different
  `exposureCompensationEv` values (e.g. `0.0` and `+1.0`) and asserting
  the captured, tonemapped output is strictly brighter at the higher EV
  at a fixed sampled pixel — the same "paired capture, compare
  real-GPU output" pattern the existing HDR roll-off test
  (`tests/runtime/pbr_render_gpu_tests.cpp`) already uses, extended to
  vary exposure instead of light intensity. The expected value at the
  non-zero EV is computed by calling `computeExposureMultiplier()` and
  `reinhardTonemap(linearValue, multiplier)` — never a third,
  independent formula transcription.
- **Windowed/headless shared-path proof**: no new path is introduced —
  `Renderer::drawFrame()` is the single entry point both the windowed
  Runtime and every headless fixture/example already call; the new
  luminance test runs through the same headless capture harness the
  existing image-regression suite already uses, exercising the
  identical code the windowed path runs.
- **Vulkan Validation Layers**: clean run required (no
  errors/warnings), covering the new/changed `VkPushConstantRange` and
  `vkCmdPushConstants` call, per AGENTS.md's standing rule.
- **All existing goldens — a real, executed gate, not an arithmetic
  claim:** every one of the (at time of writing) 10 committed goldens
  must stay byte-identical, confirmed by actually re-running the full
  existing image-regression suite unmodified with every real call site
  passing `exposureCompensationEv = 0.0` (Requirement 12). `std::exp2(0.0f)
  == 1.0f` explains why this is expected; it is not treated as
  sufficient proof on its own — the Implementation's own Verification
  step must show the real, executed capture-compare result for all 10
  goldens, on real GPU hardware, exactly as every prior Spec in this
  repository already required.
- **No new golden by default.** The GPU-independent formula test plus
  the real-GPU monotonicity test together prove the feature functions
  correctly without needing a new reference image; a new golden is
  warranted only if Plan-stage discovers neither can actually observe
  the rendered effect (unlikely, given the existing HDR roll-off
  precedent already proves this exact style of paired-capture GPU test
  is sufficient for tone-mapping behavior).

## Risks & Open Questions

None remaining at Spec-drafting time: the push-constant reflection
shape (Requirement 11), the error-enumerator choice (Requirement 6),
and the GPU-independent test file location (Testing & Verification
Plan) were each open questions in this Spec's own first draft and are
now fixed, each grounded in real code or a real `slangc` compile — see
this Spec's own Alternatives Considered for what was rejected and why.

## Out of Scope / Future Work

- A future spec may widen or narrow the `[-16, +16]` authoring range
  (Proposed Design already shows this has no numeric-safety obstacle up
  to `~2^112`), or replace the manual compensation model entirely with
  true auto-exposure/histogram-based exposure — ADR-0068 D-5 already
  anticipated this general direction; this Spec's own push-constant
  plumbing (a single per-frame scalar reaching the output-transform
  pass) is compatible with, and does not block, either future change.
- A scene/material editor UI for setting `exposureCompensationEv` is not
  designed here — this Spec only fixes the data model and render path.

## Human Review Approval — 2026-09-08

**Status: Approved.** Recorded against
[PR #136](https://github.com/slmao/Atlantis/pull/136). Human Review's
own words: *"我分别认可 Spec 0031、ADR-0075，以及 ADR-0068 的 2026-09-08
Proposed Amendment"* ("I separately approve Spec 0031, ADR-0075, and
ADR-0068's 2026-09-08 Proposed Amendment").

This approval covers this Spec's complete Requirements and scope as
drafted — every functional and non-functional Requirement, the
Proposed Design (the `computeExposureMultiplier()`/push-constant data
flow, the `[-16, +16]` authoring-domain policy and its stated safety
margin, the `NonFiniteValue`-reuse decision, the `slangc`-confirmed
push-constant contract), the Architectural Impact section's own
three-separate-approvals requirement, and the Testing & Verification
Plan — with no change to this Spec's own already-reviewed design text.
[ADR-0075](../adr/0075-manual-camera-exposure-data-and-output-transform-contract.md)
and [ADR-0068's own Accepted Amendment](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md#accepted-amendment--2026-09-08)
are approved independently, each carrying its own Acceptance Record —
this is not a blanket approval of one implying the others.

**This approval authorizes drafting Plan 0031 only once
[PR #136](https://github.com/slmao/Atlantis/pull/136) itself has merged
to `main` — not before, and not Implementation, code, tests, assets, or
golden capture of any kind.**

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-08, approving this Spec in full, as drafted,
with no change.

## Plan-Stage Correction — 2026-09-08

**Requirement 3's own claim, "no existing `.scene.txt` file requires
editing," is corrected — narrowed, not reversed.** Plan-stage
investigation of the real source (`scene_source.cpp`) found a fact this
Spec's own drafting missed: `parseSceneSource()` gates every source
file behind an exact-string-match version line
(`kVersionLine`, e.g. `"atlantis_scene_source_version: 3"`), with no
version-range support. `git log -p` confirms this literal — and every
existing checked-in scene file's own first line — was bumped on **both**
prior grammar extensions (v1→v2 for material references, v2→v3 for the
light grammar), including the light grammar, which was itself just as
additive/backward-compatible at the parser-logic level as this Spec's
own new optional camera token.

Following that same, twice-established, unconditional precedent (not a
new policy invented for this Spec), `kVersionLine` bumps 3→4, and each
of the 8 existing `assets/scenes/*.scene.txt` files' first line is
edited to match — a mechanical, one-line-per-file, zero-semantic-content
edit; no node data in any of those files changes. This narrows
Requirement 3's claim: **no existing camera-node *line* needs editing
for its own sake** (the 14-token grammar stays fully valid), but the
source-file *header* line does, in every file, following this
codebase's own established versioning convention. Requirement 4's
schema-version-bump treatment (artifact side) was already correct as
written and needs no correction.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — direction given
in chat, 2026-09-08, resolving this Plan-stage finding by choosing to
follow the established v1→v2/v2→v3 precedent exactly (bump and edit
every existing file) rather than leave `kVersionLine` unbumped. See
[Plan 0031](../plans/0031-manual-camera-exposure-foundation.md)
Milestone 1 for the concrete file list.
