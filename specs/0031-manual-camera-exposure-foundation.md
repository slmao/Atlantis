# Spec: Manual Camera Exposure Foundation

- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-08
- **Related Plan(s):** None yet — Spec/ADR only this round
- **Related ADR(s):** [ADR-0075: Manual Camera Exposure Data and Output-Transform Contract](../adr/0075-manual-camera-exposure-data-and-output-transform-contract.md) (`Proposed`)

## Summary

Adds one new scalar to `atlantis::world::Camera`, `exposureCompensationEv`
(EV, default `0.0`), and threads it from the active `Camera` through
Runtime into the existing output-transform RenderGraph pass
(`shaders/output_transform_unorm`/`output_transform_srgb`,
[ADR-0068](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md)
D-5/D-10), replacing the fixed `kBaselineExposure = 1.0` shader constant
with a per-frame exposure multiplier `exp2(exposureCompensationEv)`. At
`exposureCompensationEv = 0.0` the multiplier is exactly `1.0` — byte-
identical to today's behavior. ADR-0068 D-5 itself already names this
exact follow-up ("a future spec may add one, replacing
`kBaselineExposure`'s fixed value with a computed one without changing
this Decision's own curve") — this Spec is that follow-up, not a new
architectural direction.

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
- Fix a finite, numerically-safe EV input range, derived from real
  floating-point facts of this exact pipeline — not chosen by feel.
- Guarantee `exposureCompensationEv = 0.0` reproduces every existing
  golden byte-identically.

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
2. **Exposure multiplier:** `exposureMultiplier = exp2(exposureCompensationEv)`,
   computed once, applied exactly where `kBaselineExposure` is applied
   today (`exposedColor = linearColor * exposureMultiplier`), in both
   `output_transform_unorm.slang` and `output_transform_srgb.slang`.
   The Reinhard formula and the sRGB OETF are byte-for-byte unchanged.
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
6. **Range enforcement:** `cookScene()` (source → artifact) and
   `decodeSceneArtifact()` (artifact → CPU) each independently reject a
   non-finite or out-of-`[kExposureCompensationEvMin, kExposureCompensationEvMax]`
   value with a new, distinct error enumerator (mirroring the existing
   `NonFiniteValue`/domain-check pattern for light color/intensity) —
   never silently clamped.
7. **`Renderer::drawFrame()`** gains one new parameter,
   `float outputTransformExposureCompensationEv`, placed immediately
   after the two existing output-transform-only parameters
   (`outputTransformPipeline`, `outputTransformSampler`) — grouping all
   three output-transform-scoped inputs together at one call-site
   position. This is the **only** signature change; every other
   parameter, the `DrawItem` shape, and the camera uniform buffer layout
   are unchanged.
8. **Runtime** reads `exposureCompensationEv` from the same
   `atlantis::world::Camera` value it already reads `fovYRadians`/
   `nearZ`/`farZ` from (`runtime_application.cpp`'s existing
   `cameraComponentResult` local) and passes it straight through to
   `drawFrame()` — no transformation, no clamping at this call site
   (clamping already happened at cook/decode time, Requirement 6).
9. **Output-transform push constant:** the output-transform `Pipeline`
   gains a single 4-byte, `Fragment`-stage-read push constant carrying
   the raw EV value (`exp2()` is evaluated in the shader, the one place
   the formula already lives, not duplicated in C++). This reuses the
   existing, already-generic RHI push-constant mechanism
   (`PipelineCreateParams::pushConstantSizeBytes`,
   `CommandList::pushConstant()`) exactly as every `MaterialKind`
   pipeline already does — no RHI change. `outputTransformExpectedDescriptorContract()`
   (descriptor *bindings*) is unchanged; only the shader pair's
   push-constant contract (checked separately, in
   `atlantis_shader_compiler`) widens from empty to one 4-byte Fragment
   entry.
10. Every existing `Renderer::drawFrame()` call site (Runtime, both
    example binaries, every image-regression fixture, every direct-call
    unit/GPU test) passes `0.0f` explicitly — mechanical, zero-behavior
    -change, and required for the build to compile against the widened
    signature.

### Non-functional

- **Performance:** one `exp2()` and one extra multiply per output pixel
  in an already-existing fullscreen fragment shader; one 4-byte
  `vkCmdPushConstants` call once per frame. Immaterial.
- **Memory:** +4 bytes per scene-artifact node record; +4 bytes per
  `world::Camera`/`DecodedCamera` instance. No new GPU resource, no new
  descriptor binding.
- **Portability (Vulkan-only Phase 1):** a 4-byte push constant is far
  under every Vulkan implementation's guaranteed minimum
  `maxPushConstantsSize` (128 bytes); no portability risk on Windows or
  a future Android target.
- **Other — behavioral compatibility:** `exposureCompensationEv = 0.0`
  MUST reproduce all 10 existing image-regression goldens byte-for-byte
  identically (Requirement 2's exact arithmetic identity,
  `exp2(0.0) == 1.0`, is what makes this a proof, not an expectation).

## Proposed Design

```
// shaders/output_transform_unorm.slang and output_transform_srgb.slang,
// both variants identically (mirrors PbrPushConstants' own declaration
// idiom in pbr_direct_lit.slang):
struct ExposurePushConstants {
    float exposureCompensationEv;
};
[[vk::push_constant]]
ConstantBuffer<ExposurePushConstants> exposurePushConstants;

// fragmentMain, replacing the fixed kBaselineExposure literal:
float exposureMultiplier = exp2(exposurePushConstants.exposureCompensationEv);
float3 exposedColor = linearColor * exposureMultiplier;
// tonemapped = exposedColor / (1 + exposedColor)  -- unchanged
```

Data flow, source to pixel:

```
.scene.txt camera_exposure_ev=<f>  (optional, default-omitted = 0.0)
  -> parseSceneSource() -> DecodedCamera.exposureCompensationEv
  -> cookScene() (finite + range check) -> scene artifact byte 112..115
  -> decodeSceneArtifact() (independent finite + range re-check)
  -> World::fromValidatedSceneData() -> world::Camera.exposureCompensationEv
  -> RuntimeApplication::runFrame() reads cameraComponent.exposureCompensationEv
  -> Renderer::drawFrame(..., outputTransformExposureCompensationEv)
  -> output_transform pass execute lambda:
       cmd.bindPipeline(outputTransformPipeline);
       ExposurePushConstants payload{outputTransformExposureCompensationEv};
       cmd.pushConstant(&payload, sizeof(payload));
  -> vkCmdPushConstants (existing, generic RHI mechanism)
  -> fragmentMain: exp2(ev) * linearColor, then unchanged Reinhard/sRGB
```

**Fixed EV range** — `kExposureCompensationEvMin = -16.0f`,
`kExposureCompensationEvMax = +16.0f` (a named constant near
`ExposurePushConstants`, referenced by both the shader clamp-free
contract statement and the cook/decode range checks — see Requirements
6). Derivation (real IEEE-754 binary32/binary16 facts of this exact
pipeline, not a photographic-convention guess):

- The HDR intermediate `HdrColorTarget` is `HdrFormat::Rgba16Float`
  (ADR-0068 D-2) — its own largest finite representable magnitude is
  `65504` (binary16 max). No `linearColor` value this pipeline can ever
  sample from it exceeds that.
- `exposedColor = linearColor * exp2(ev)` must stay a finite binary32
  value for every representable `linearColor` up to `65504`. Binary32's
  max finite value is `3.4028234663852886×10^38`. Solving
  `65504 * exp2(ev) < 3.4028234663852886×10^38` gives
  `ev < log2(3.4028234663852886×10^38 / 65504) ≈ 112.0007` — the exact,
  real, hard overflow ceiling for this pipeline's own math, independent
  of scene content.
- `[-16, +16]` is chosen **far inside** that hard ceiling — at
  `ev = +16`, `exp2(16) = 65536`, so the worst-case product is
  `65504 * 65536 ≈ 4.29×10^9`, still `≈ 7.9×10^28` times smaller than
  binary32's max — an overwhelming, explicitly-computed safety margin,
  not an assumed one. `16` is also not an arbitrary round number: it is
  the same `16` in `Rgba16Float` — the chosen ceiling deliberately
  matches the HDR intermediate format's own bit width, i.e. the maximum
  exposure multiplier this Spec permits (`2^16`) is the same order of
  magnitude as that format's own maximum representable value, a
  real-code-derived tie rather than a picked constant. The range is
  symmetric; the negative side has no overflow risk at all (the product
  only shrinks), so `-16` carries the same enormous margin from the
  binary32 underflow-to-subnormal boundary (`ev ≥ -126` for `exp2(ev)`
  itself to stay a normal, non-subnormal float).
- This range is also, incidentally, far more permissive than any
  photographic exposure-compensation dial (typically ±3 to ±5 stops) —
  it is not a restrictive artistic limit, purely a numeric-safety
  ceiling.

## Architectural Impact

**Yes** — this Spec changes a public API (`Renderer::drawFrame()`
signature), a data format (scene source grammar + binary artifact
schema + `kSceneArtifactSchemaVersion` bump), and a backend-abstraction
contract detail (the output-transform `Pipeline`'s push-constant
layout, previously fixed at zero). See
[ADR-0075](../adr/0075-manual-camera-exposure-data-and-output-transform-contract.md)
(`Proposed`), which records this as one aggregated decision — no second
architectural surface was found in the real code investigated for this
Spec that needs its own, separate ADR.

This Spec was checked against every `Accepted` ADR whose scope it
touches (ADR-0045 data-format versioning, ADR-0048 World module
boundary, ADR-0053 AssetSystem/World dependency direction, ADR-0067/
ADR-0072 push-constant precedent, ADR-0068 D-5/D-6/D-10 tone-mapping and
output-transform contract) and found **no conflict** — ADR-0068 D-5
itself explicitly names "a future spec... replacing `kBaselineExposure`'s
fixed value with a computed one" as the anticipated next step, which is
exactly this Spec's own proposal; D-10's "no push constant" clause is a
direct, foreseen consequence of D-5's then-fixed constant, not an
independent invariant.

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
- **Computing `exp2(ev)` in Runtime (C++) and pushing the multiplier
  instead of the raw EV**: rejected — it would duplicate the
  `exp2` mapping in two languages/places instead of one (the shader,
  which already owns the tone-mapping formula per ADR-0068 D-5), a real
  drift risk for no benefit; the push-constant payload is 4 bytes either
  way.
- **A wider or narrower fixed EV range** (e.g. ±3 matching a
  photographic dial, or ±126 matching binary32's raw normal-exponent
  range): rejected in favor of ±16 — ±3 has no numeric-safety basis (it
  would be a feel-based choice, which this Spec's own requirement
  forbids); ±126 ignores the real, tighter, pipeline-specific ceiling
  derived from `Rgba16Float`'s own actual max value (~112, not 127), and
  offers no additional real headroom over ±16 that this pipeline could
  ever need.

## Testing & Verification Plan

- **GPU-independent numerical tests** (`tests/shader_system/` or
  `tests/asset_system/`, exact location a Plan-stage decision): extend
  the existing CPU-side tone-mapping reference
  (`tests/image_regression/support/tone_mapping_reference.h`,
  `reinhardTonemap()`) to accept an exposure multiplier parameter, and
  add hand-computed-value tests at `exposureCompensationEv = -1, 0, +1`
  confirming `exp2(-1) = 0.5`, `exp2(0) = 1.0`, `exp2(1) = 2.0` and the
  resulting `reinhardTonemap()` outputs match by-hand arithmetic exactly
  — the same "literal, hand-verifiable" precedent ADR-0068 D-5 already
  established. Cook/decode range-check tests (Requirement 6) at the
  exact boundary values (`-16.0`, `+16.0`, `±16.0001`, non-finite) join
  the existing `scene_source_tests.cpp`/`scene_artifact` decode-error
  test files.
- **Real-GPU luminance-monotonicity test**: one new `[gpu]` test
  rendering the same fixed scene at two different
  `exposureCompensationEv` values (e.g. `0.0` and `+1.0`) and asserting
  the captured, tonemapped output is strictly brighter at the higher EV
  at a fixed sampled pixel — the same "paired capture, compare
  real-GPU output" pattern the existing HDR roll-off test
  (`tests/runtime/pbr_render_gpu_tests.cpp`) already uses, extended to
  vary exposure instead of light intensity.
- **Windowed/headless shared-path proof**: no new path is introduced —
  `Renderer::drawFrame()` is the single entry point both the windowed
  Runtime and every headless fixture/example already call; the new
  luminance test runs through the same headless capture harness the
  existing image-regression suite already uses, exercising the
  identical code the windowed path runs.
- **Vulkan Validation Layers**: clean run required (no
  errors/warnings), covering the new/changed `VkPushConstantRange` and
  `vkCmdPushConstants` call, per AGENTS.md's standing rule.
- **All existing goldens**: every one of the (at time of writing) 10
  committed goldens must stay byte-identical, proven by re-running the
  full existing image-regression suite unmodified — every real call
  site now passes `exposureCompensationEv = 0.0` explicitly (Requirement
  10), and Requirement 2's exact-identity arithmetic (`exp2(0.0) == 1.0`)
  is what makes that a mathematical certainty, not merely an empirical
  hope.
- **No new golden by default.** The GPU-independent numeric tests
  (exact push-constant/formula math) plus the real-GPU monotonicity
  test together prove the feature functions correctly without needing a
  new reference image; a new golden is warranted only if Plan-stage
  discovers neither can actually observe the rendered effect (unlikely,
  given the existing HDR roll-off precedent already proves this exact
  style of paired-capture GPU test is sufficient for tone-mapping
  behavior).

## Risks & Open Questions

- **Slang push-constant cross-stage reflection shape**: the existing,
  already-shipped precedent in this codebase
  (`compile_and_validate.cpp`'s own documented finding for the PBR
  shader pair) shows Slang's raw reflection JSON can list a
  `pushConstantBuffer` resource in every entry point that can see its
  declaration at module scope, even one that never reads it. Because
  this Spec's new push constant is declared once per output-transform
  `.slang` file and read only by `fragmentMain`, it is not yet certain
  (without a real `slangc` compile) whether `vertexMain`'s own reflected
  JSON will show a matching stray entry, requiring
  `validatePushConstantsForVertexStage()`'s current "genuinely empty for
  output-transform" expectation to change to "one stray, unread,
  4-byte entry" (mirroring the already-established, symmetric case for
  `PbrDirectLit`'s stray fragment-side entry today). This Spec fixes
  the architecture (one 4-byte Fragment-stage push constant); the exact
  expected-contract table entry is Plan-stage work, to be confirmed by
  a real compile, the same methodology every other shader contract in
  this codebase already used (see `descriptor_contract.h`'s own "sky"/
  "shadow-cast" comments).
- **Exact new C++ error enumerator names/values** (Requirement 6) and
  the exact new test file location for the GPU-independent numeric
  tests are Plan-stage detail, not fixed by this Spec.

## Out of Scope / Future Work

- A future spec may replace the fixed `[-16, +16]` range, or the manual
  compensation model entirely, with true auto-exposure/histogram-based
  exposure — ADR-0068 D-5 already anticipated this general direction;
  this Spec's own push-constant plumbing (a single per-frame scalar
  reaching the output-transform pass) is compatible with, and does not
  block, that future replacement.
- A scene/material editor UI for setting `exposureCompensationEv` is not
  designed here — this Spec only fixes the data model and render path.
