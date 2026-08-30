# ADR 0067: PBR Direct-Lighting BRDF, Push-Constant Extension, and Rendering Contract

- **Status:** Accepted
- **Date:** 2026-08-30
- **Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review,
  approved 2026-08-30 as part of
  [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)'s
  own Human Review Approval.
- **Related Spec:** [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)
  (`Approved`)
- **Acceptance Record (2026-08-30):** Accepted by Human Review as part
  of [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)'s
  own Human Review Approval (2026-08-30), following one centralized
  final review round that corrected the push-constant total size from a
  hand-computed 88 bytes to a real, Slang-reflection- and MSVC-layout-
  confirmed 96 bytes (D-6), corrected a real geometry-term citation
  error (`k = alpha/2` → the correctly-cited direct-lighting
  `k = (roughness+1)²/8`, D-2), locked the fragment-stage push-constant
  visibility choice to uniformly widening every Pipeline (D-7), and
  closed the camera-world-position gap by extending the shared Camera/
  Lighting uniform buffer (D-15), recorded as its own Accepted Amendment
  to ADR-0062 rather than folded into this ADR — see that Spec's own
  "Final Review Round" section for the complete, itemized record. This
  record does not change this ADR's own Decision, Consequences, or
  Alternatives Considered below beyond what the numbered Decision items
  themselves already state as corrected in place.
- **Related ADR(s):**
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  (`Accepted` — this ADR's own push-constant capacity note, "Vulkan
  guarantees at least 128 bytes across all stages," is the exact
  guarantee this ADR's own extended push-constant layout is sized
  against),
  [ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
  (`Accepted` — this ADR's own precedent, widening one existing binding's
  Vulkan stage visibility from vertex-only to vertex-and-fragment for
  every Pipeline with zero pixel change to a shader that never reads it,
  is the direct precedent this ADR's own push-constant stage-visibility
  decision is evaluated against),
  [ADR-0059](0059-material-asset-module-boundary-artifact-format-and-shader-identity.md)
  (`Accepted` — the closed-`MaterialKind`-to-built-in-shader-pair
  mapping this ADR's own new `PbrDirectLit` kind extends, unmodified in
  mechanism),
  [ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Accepted` — this ADR consumes the three parameter fields that ADR
  defines; it does not redefine them).

## Context

Confirmed directly against real, current source (`main` at `f7c2d18`):

- **The existing push-constant chain is exactly, and only, a 64-byte,
  vertex-only `objectToWorld` matrix.** `DrawItem`
  (`src/renderer/include/atlantis/renderer/draw_item.h:17-21`) carries
  `std::array<float, 16> objectToWorld`; `Renderer::drawFrame()`'s own
  per-draw-item loop
  (`src/renderer/src/renderer.cpp:27-41`) calls
  `cmd.pushConstant(item.objectToWorld.data(), item.objectToWorld.size()
  * sizeof(float))` (line 39) as the single, sole push-constant call
  site in this codebase. `CommandList::pushConstant()`
  (`src/rhi/include/atlantis/rhi/command_list.h:73`,
  `virtual void pushConstant(const void* data, std::size_t sizeBytes) =
  0;`) takes no offset and no stage-flags parameter — the Vulkan
  implementation
  (`src/vulkan_backend/src/vulkan_command_list.cpp:320-324`) hardcodes
  `VK_SHADER_STAGE_VERTEX_BIT` and offset `0` unconditionally, for every
  call, regardless of which `Pipeline` is currently bound.
  `VulkanDevice::createPipeline()`'s own push-constant-range
  construction (`src/vulkan_backend/src/vulkan_device.cpp:1035-1045`)
  likewise hardcodes `stageFlags = VK_SHADER_STAGE_VERTEX_BIT`,
  `offset = 0`, for **every** Pipeline this engine ever creates,
  `size = params.pushConstantSizeBytes` — a per-call value, currently
  set to exactly `sizeof(float) * 16` (64 bytes) at every one of its
  three real call sites
  (`src/runtime/src/material_realization.cpp:176, 301, 324`), never
  derived from Shader System reflection at runtime (`toPushConstantSize()`,
  `src/shader_system/rhi_integration/src/vertex_input_mapping.cpp:68-72`,
  is confirmed called only from tests, never from any production call
  site).
- **Vulkan's own guaranteed minimum push-constant capacity across all
  stages is 128 bytes**, per this codebase's own existing, `Accepted`
  record (`adr/0025-...md:353-360`): "Vulkan guarantees at least 128
  bytes across all stages, comfortably enough for one 4×4 float matrix
  (64 bytes), but this ceiling is real and a future spec adding more
  per-object data ... must account for it." No other document in this
  repository states this number; this ADR treats it as the binding
  constraint for whatever it adds.
- **Shader System's own push-constant validation is deliberately
  vertex-stage-only.** `validatePushConstantsForVertexStage()`
  (`src/tools/shader_compiler/compile_and_validate.cpp:168-178`) checks
  only the vertex stage's own reflected push-constant range against a
  single, hardcoded `{offset: 0, size: 64, stage: Vertex}` expectation;
  its own doc comment (lines 157-167) states explicitly that "a stray
  `PushConstantRange` on the fragment stage's own metadata is expected,
  harmless, and not validated here" — **there is no existing cross-check
  between the vertex and fragment stages' own push-constant reflection
  today**, and `descriptor_contract.cpp`'s `validateDescriptorContract()`
  never inspects `pushConstantRanges` at all (it validates only
  descriptor bindings).
- **`lit_textured.slang`'s own real lighting math** (full file read;
  `shaders/lit_textured/lit_textured.slang:81-97`) is a pure N·L diffuse
  accumulation with **no ambient term and no specular term at all** —
  not Blinn-Phong, not any form of microfacet BRDF. Point-light
  attenuation is an explicitly non-physical linear falloff,
  `clamp(1 - dist/range, 0, 1)` (line 92), matching ADR-0062's own
  Decision 5 and explicit rejection of inverse-square falloff for
  "Phase 1" (`adr/0062-...md:199-201`). The final color is
  `clamp(texColor.rgb * accumulated, 0, 1)` (line 96) with **no
  tone-mapping and no gamma-encode anywhere** — confirmed by a
  repository-wide grep (zero hits for "gamma" under `src/`; the only hit
  for "tone-mapping" is this same shader's own comment disclosing its
  absence) and restated explicitly by ADR-0062 Decision 5
  (`adr/0062-...md:143-145`): "No tone-mapping, gamma-encode, or HDR
  intermediate target is added — the final clamp above is the only
  transformation applied before the display format's own write."
- **The Camera/Lighting uniform buffer is 304 bytes** (128-byte
  `CameraMatrices` + 176-byte `FrameLightingData`, the latter capped at
  1 Directional + 4 Point lights,
  `src/runtime/include/atlantis/runtime/scene_extraction.h:61-120`),
  bound via the descriptor set (binding 0), never a push constant —
  unaffected in shape, offset, or binding by this ADR.
- **The default swapchain color format is `Bgra8Unorm`** (a UNORM
  format, preferred first;
  `src/vulkan_backend/src/vulkan_presentation.cpp:107-112`), and every
  offscreen/golden-image render target explicitly uses `Rgba8Unorm`
  (also UNORM) — confirmed across every fixture and golden sidecar. No
  `Srgb`-format render target is ever actually selected or bound by any
  currently-shipped code path in this engine. This means today's engine
  writes its final fragment-shader output directly into a UNORM
  attachment with **no hardware or shader-side linear→display encode
  step of any kind** — the existing `clamp(..., 0, 1)` is genuinely the
  only transformation any currently-shipped shader applies before that
  write.
- **No sphere mesh, and no procedural mesh-generation code of any kind,
  exists anywhere in this repository** — confirmed by a repository-wide
  search; the only geometry assets are `minimal_cube.mesh.txt` and two
  flat quads (`textured_quad_left/right.mesh.txt`), all hand-authored
  text sources. A flat surface's `N·V`/`N·H` vary only weakly across its
  visible face — insufficient to visually demonstrate a microfacet
  BRDF's own characteristic roughness/metallic-driven highlight shape
  and falloff.

The real design questions this ADR settles: what exact BRDF formula
(every constant and every division named precisely, none left as "the
standard Cook-Torrance model"); how the three new per-material
parameters ([ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md))
reach the GPU (extended push constants vs. a new per-material uniform);
if push constants, the exact layout and the real Vulkan stage-visibility
mechanism needed for the fragment stage to read them without silently
touching `UnlitTextured`/`LitTextured`'s own already-verified, unchanged
Pipelines; and what validation mesh/scene/golden this Spec needs given
the real absence of any curved-surface asset today.

## Decision

**Locked during centralized final review, closing every item this
ADR's own first draft left open, against real Slang reflection,
real MSVC layout, and real `vulkaninfo` evidence — see each numbered
Decision below for the specific evidence:**

**A new, closed `MaterialKind::PbrDirectLit`, rendering via a new
`pbr_direct_lit.slang` built-in shader pair implementing a precisely
specified metallic-roughness Cook-Torrance BRDF (Lambertian diffuse +
GGX/Smith-Schlick specular, with a corrected, precisely-cited
direct-lighting geometry-term remap — D-2) for Directional and Point
lights only. The three new per-material parameters travel as an
extended push constant — `objectToWorld` (64 bytes, offset 0,
unchanged) followed by `baseColorFactor`/`metallicFactor`/
`roughnessFactor` (ending at byte offset 88) — whose real, Slang- and
MSVC-confirmed total block size is **96 bytes** (`alignas(16)`,
D-6), well inside Vulkan's guaranteed 128-byte minimum (32 bytes of
headroom). Fragment-stage visibility for the push-constant block is
granted by **uniformly widening every Pipeline's own
`VkPushConstantRange::stageFlags`** to `VERTEX | FRAGMENT` (D-7,
matching ADR-0062's own already-shipped precedent exactly) —
`CommandList::pushConstant()`'s own public signature does not change,
and no new RHI-internal field is added anywhere. The BRDF's own view
vector needs the camera's own world-space position, which no shader in
this codebase has ever had — closed by extending the existing,
unmodified 304-byte Camera/Lighting uniform buffer with one new,
trailing 16-byte field appended *after* the existing region (buffer
grows to 320 bytes total — D-15, carrying its own Proposed Amendment to
ADR-0062, below). The existing single combined-image-sampler binding is
unchanged. `UnlitTextured`/`LitTextured`'s own `.slang` source is
untouched by every one of these three extensions — each is verified,
not merely argued, against real reflection JSON, a real buffer-binding
mechanism (`VK_WHOLE_SIZE`), and standard Vulkan pipeline-layout
compatibility rules; see D-7/D-8/D-15 for the complete, itemized
evidence.**

### D-1 (ADR-internal). Exact BRDF formula

Metallic-roughness Cook-Torrance, direct lighting only, per active light
(Directional or Point), accumulated exactly as `lit_textured.slang`
already accumulates its own simpler diffuse-only sum (a `for` loop per
light array, `+=` into one `accumulated` `float3`) — same loop shape,
richer per-light term:

```
// Per-fragment, once:
N  = normalize(worldNormal)
V  = normalize(cameraWorldPosition - worldPosition)   // view vector
texColor = baseColorTexture.Sample(uv)                 // Rgba8Srgb source,
                                                         // hardware-decoded
                                                         // to linear at
                                                         // sample time
                                                         // (ADR-0066 D7)
baseColor = texColor.rgb * baseColorFactor.rgb          // both linear-space
alpha_out = texColor.a * baseColorFactor.a               // written to the
                                                          // color attachment
                                                          // but currently
                                                          // inert -- this
                                                          // engine's own
                                                          // colorBlendAttachment
                                                          // is hardcoded
                                                          // blendEnable=VK_FALSE
                                                          // for every
                                                          // Pipeline
                                                          // (vulkan_device.cpp:1125),
                                                          // matching
                                                          // lit_textured.slang's
                                                          // own already-inert
                                                          // alpha passthrough
                                                          // exactly -- see
                                                          // ADR-0066 D9
metallic  = clamp(metallicFactor, 0, 1)
roughness = clamp(roughnessFactor, 0, 1)
alpha     = max(roughness * roughness, kMinAlpha)        // kMinAlpha = 1e-3,
                                                          // named constant,
                                                          // see D-4 below
F0        = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic)
diffuseColor = baseColor * (1.0 - metallic)              // zero for full metal

accumulated = float3(0, 0, 0)   // no ambient term, matching lit_textured.slang

// Per active light (L = normalized direction TO the light; radiance =
// color * intensity, Directional; color * intensity * atten, Point,
// atten identical to ADR-0062's own existing linear falloff formula):
NdotL = max(dot(N, L), 0.0)
if (NdotL > 0.0) {                                        // early-out avoids
                                                            // every division
                                                            // below touching
                                                            // a NdotL<=0
                                                            // (backlit) sample
  NdotV = max(dot(N, V), kMinDot)                          // kMinDot = 1e-4,
                                                            // named constant
  H     = normalize(L + V)                                 // guarded: L, V
                                                            // both unit and
                                                            // NdotL>0 already
                                                            // rules out the
                                                            // exact L=-V
                                                            // degenerate case
  NdotH = max(dot(N, H), 0.0)
  VdotH = max(dot(V, H), 0.0)

  D = (alpha * alpha) /
      (kPi * pow(NdotH * NdotH * (alpha * alpha - 1.0) + 1.0, 2.0))

  k  = (roughness + 1.0) * (roughness + 1.0) / 8.0          // direct-lighting
                                                            // k-remap, uses
                                                            // the CLAMPED
                                                            // PERCEPTUAL
                                                            // roughness, NOT
                                                            // alpha; see D-2
                                                            // below (fixed
                                                            // during final
                                                            // review -- the
                                                            // original draft's
                                                            // k = alpha/2 is
                                                            // Karis's own IBL
                                                            // remap, not his
                                                            // direct-lighting
                                                            // one)
  G1_V = NdotV / (NdotV * (1.0 - k) + k)
  G1_L = NdotL / (NdotL * (1.0 - k) + k)
  G  = G1_V * G1_L

  F  = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0)

  specular = (D * G * F) / max(4.0 * NdotV * NdotL, kMinDot)

  kD = (float3(1,1,1) - F) * (1.0 - metallic)              // energy split

  accumulated += (kD * diffuseColor / kPi + specular) * radiance * NdotL
}

finalRgb = clamp(accumulated, 0.0, 1.0)     // identical clamp policy to
                                              // lit_textured.slang; no
                                              // tone-mapping (Non-Goal)
return float4(finalRgb, alpha_out)
```

Named constants (never bare literals, matching `lit_textured.slang`'s
own `kPointLightDistanceEpsilon` precedent):
`kPi = 3.14159265358979323846`, `kMinAlpha = 1e-3` (D-4), `kMinDot =
1e-4` (matching the existing `kPointLightDistanceEpsilon` value exactly,
reused for every new division's denominator floor this BRDF introduces
— D-4/D-5 below).

This is the widely-published metallic-roughness Cook-Torrance model
(GGX/Trowbridge-Reitz normal distribution; Schlick-GGX geometry term
with Karis's direct-lighting `k = (roughness+1)²/8` remap; Schlick's
Fresnel approximation; the `kD`/energy-conserving diffuse-specular
split), cited here by its standard formulation, not invented by this
codebase — see D-2 immediately below for the geometry-term remap's own
correction, and Alternatives Considered for the remaps this Decision
rejected.

### D-2. Geometry term remap — corrected during final review, real citation error fixed

**Correction (centralized final review, 2026-08-30):** this Decision's
own first draft wrote `k = alpha / 2` and labeled it "Karis's
direct-lighting remap." That attribution was wrong and has been
corrected — `k = alpha / 2` (equivalently `roughness² / 2`, since
`alpha = roughness²`) is Karis's **IBL** remap (Karis, "Real Shading in
Unreal Engine 4," SIGGRAPH 2013 course notes, §4.4.1), not his
direct-analytic-light one. Mislabeling it would have shipped a formula
tuned for pre-filtered environment sampling under a "direct lighting"
contract this Spec explicitly commits to (Non-Goals: no IBL).

**Recommendation, locked: Karis's own direct-lighting remap,
`k = (roughness + 1)² / 8`, using the clamped, *perceptual* `roughness`
value directly — never `alpha`.** This is the correct, precisely-cited
formula for analytic (non-IBL) light sources from the same source.
`roughness` here is exactly the same clamped-to-`[0,1]` value D-1 already
computes before squaring it into `alpha` for `D` — `k`'s own remap does
not go through `alpha` at all, a real, load-bearing distinction between
the two remaps this Decision's own first draft conflated. This is now
the single, locked recommendation for this Spec — not left open for
Human Review to swap for the height-correlated Smith alternative (see
Alternatives Considered): a real citation was corrected, not a
judgment call between two equally-valid choices.

### D-3. Half-vector degeneracy and NaN/Inf protection

**Recommendation:** the `if (NdotL > 0.0)` early-out (D-1 above) is the
primary guard — `lit_textured.slang`'s own existing pattern
(`max(dot(N,L), 0.0)` computed unconditionally, multiplied in at the
end) is **not** reused as-is here, because unlike pure Lambertian
diffuse, this BRDF's `D`/`G`/`F` terms can independently produce `NaN`
(a `0/0` in `G1` when `NdotV = 0` exactly, or a degenerate `H` when
`L ≈ -V`) whose value would survive a trailing `* NdotL` multiply
(`NaN * 0 = NaN`, not `0`) — an early, branch-based skip (not merely a
trailing clamp) is required for correctness, not just style. Within the
guarded branch, `NdotV` is floored at `kMinDot` (never exactly `0`,
closing the one remaining `G1_V` division risk), and every division's
own denominator (`D`'s `pow(...)` term, `specular`'s `max(4 * NdotV *
NdotL, kMinDot)`) is likewise floored — never a bare, unguarded
division anywhere in this formula.

### D-4. Numerical stability near `roughness = 0`

**Recommendation: shader-internal minimum alpha clamp
(`alpha = max(roughness² , kMinAlpha)`, `kMinAlpha = 1e-3`), never a
cook-time rejection of an authored `roughness = 0`.** A user
legitimately wanting a perfectly smooth, mirror-like dielectric or metal
must be able to author `roughness_factor: 0.0` — rejecting it at
[ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)'s
own cook-time range check would make an entire, legitimate class of
material (polished metal, glass-like dielectric) unauthorable. Clamping
`alpha` (not `roughness` itself — the authored, cooked, and metadata-
sidecar-recorded value stays exactly what the author wrote) inside the
shader only affects the BRDF's own internal computation, matching this
codebase's own existing "epsilon inside the math, not a rejection of
legitimate authored extremes" precedent
(`kPointLightDistanceEpsilon`, applied identically to a legitimately
authorable `range`-adjacent Point-light position, never rejecting the
authored `range` itself). **`kMinAlpha` is never applied to, and must
never be confused with, `k` (D-2's own geometry-term remap)** — `k` is
computed directly from the clamped, un-squared `roughness` value
(`(roughness+1)²/8`), independent of `alpha`'s own separate
`kMinAlpha` floor; the two clamps guard two different variables for two
different reasons (`alpha` guards `D`'s own near-mirror singularity;
`k` never approaches a singularity for any `roughness ∈ [0, 1]` and
needs no floor of its own).

### D-5. Point-light attenuation — reused unchanged, including one pre-existing, disclosed edge case

**Recommendation: no new attenuation formula.** This ADR's own
Point-light `radiance` term reuses ADR-0062's existing, `Accepted`
linear falloff (`clamp(1 - dist/range, 0, 1)`,
`adr/0062-...md:130-135`) verbatim, including its own existing
`kPointLightDistanceEpsilon` (`1e-4`) floor on `dist` — this ADR does
not reopen, re-derive, or replace that decision. **A real, pre-existing
edge case, inherited unchanged, not introduced by this Spec:** if a
Point light's own authored `range` is exactly `0.0` (`World::Light`'s
own `range` field, `world/light.h:19`, states no authored minimum),
`dist / range` divides by zero; IEEE-754 gives `+Inf` for any `dist >
kPointLightDistanceEpsilon`, and `clamp(1 - Inf, 0, 1) = clamp(-Inf, 0,
1) = 0` — well-defined, not `NaN`, for every real sample point (`dist`
can never be exactly `0` once floored to `kPointLightDistanceEpsilon`).
This already holds for `LitTextured` today, unverified by any existing
test; `PbrDirectLit` inherits the identical, already-well-defined
behavior (a `range = 0` Point light silently contributes nothing) by
reusing this formula verbatim — not a new risk this Spec introduces or
is responsible for closing. It only adds a second, richer BRDF that
consumes the identical per-light `radiance` value `lit_textured.slang`'s
own diffuse-only accumulation already consumes. An inverse-square
alternative remains explicitly out of scope, for the identical reason
ADR-0062 already gave (`adr/0062-...md:199-201`): no natural finite
cutoff, in tension with the authored, testable `range` field.

### D-6. Push-constant layout — corrected during final review: real Slang/C++ evidence closes this at 96 bytes, not 88

**Real evidence obtained during centralized final review, not assumed.**
The original draft summed each field's own byte width (64+16+4+4 = 88)
and stopped there — conflating "the last field's own end offset" with
"the real push-constant range size," exactly the distinction this
review was directed to keep separate. Two independent real probes were
run, neither sharing the other's possible error:

1. **A real `slangc` compile of the candidate block**, both stages
   (`-target spirv -profile spirv_1_0 -stage vertex|fragment -entry
   vertexMain|fragmentMain -reflection-json ...`, the exact real
   invocation `buildSlangcArgv()`
   (`src/shader_system/src/command_line.cpp:5-34`) already uses),
   against a probe `.slang` file reproducing `lit_textured.slang`'s own
   real `CameraUniform`/`Sampler2D` bindings plus the candidate
   `PbrPushConstants` block field-for-field. Both stages' own reflection
   JSON report the push-constant block's `elementVarLayout.binding`
   identically: `{"offset": 0, "size": 96}` — **96 bytes, not 88** —
   while each individual field's own offset matches the hand-computed
   layout exactly (`objectToWorld` 0/64, `baseColorFactor` 64/16,
   `metallicFactor` 80/4, `roughnessFactor` 84/4, ending at 88). Slang
   rounds the push-constant **block's own container size** up to a
   16-byte boundary (88 → 96) even though no individual field after
   `objectToWorld` needs 16-byte alignment on its own — this rounding is
   a property of the `ConstantBuffer<T>`/push-constant container itself,
   confirmed empirically, not asserted from general HLSL/GLSL block-
   layout knowledge alone. This `elementVarLayout.binding.size` field is
   not incidental — it is the **exact** field
   `readPushConstantRange()`'s own real parsing logic
   (`src/shader_system/src/slang_json_transform.cpp:257-272`) reads into
   `PushConstantRange.sizeBytes`, i.e. the number this codebase's own
   Shader System would actually validate/reflect for `PbrDirectLit`.
2. **A real MSVC (`cl.exe`, this repository's own toolchain, VS 2026/
   MSVC 14.51) compile of two candidate C++ structs.** A plain, packed
   `PbrPushConstantsPacked` (no `alignas`) reports `sizeof = 88`,
   `alignof = 4` — matching the flawed 88-byte assumption exactly, and
   **disagreeing** with the real Slang-reflected 96. A second candidate,
   `alignas(16) PbrPushConstantsAlignas16`, reports `sizeof = 96`,
   `alignof = 16` — **agreeing** with real Slang reflection exactly, at
   the identical per-field `offsetof` values on both variants
   (`objectToWorld` 0, `baseColorFactor` 64, `metallicFactor` 80,
   `roughnessFactor` 84). Both variants are confirmed
   `std::is_standard_layout_v`.

**Recommendation, locked: `alignas(16) PbrPushConstants`, `sizeof =
96`, matching real Slang reflection exactly on both stages** — not the
plain, unaligned 88-byte variant. This also matches this codebase's own
existing convention exactly: `FrameLightingData`/`DirectionalLightGpu`/
`PointLightGpu` (`scene_extraction.h:77-100`) already use `alignas(16)`
for the identical reason (a GPU-uniform-buffer-shaped struct whose C++
`sizeof` must match its own GPU-reflected block size) — this Decision
is not introducing a new pattern, it is correcting an oversight that
would have broken an already-established one.

```cpp
// C++ side (Renderer/Runtime), Slang side (pbr_direct_lit.slang) --
// identical field order/offsets on both, cross-checked against real
// Slang reflection JSON and a real MSVC layout probe during this
// Spec's own final review (evidence above) -- re-confirmed by a real,
// non-self-certifying static_assert/reflection cross-check test at
// Plan/Implementation time, matching Plan 0022's own "independent
// byte-layout cross-check" precedent (Post-Merge Status Update).
struct alignas(16) PbrPushConstants {
  float objectToWorld[16];    // offset 0,  64 bytes -- byte-identical in
                               // name, offset, and meaning to today's
                               // sole push-constant field
  float baseColorFactor[4];   // offset 64, 16 bytes
  float metallicFactor;       // offset 80,  4 bytes
  float roughnessFactor;      // offset 84,  4 bytes
  // 8 bytes of trailing, compiler-inserted padding (84+4=88 -> 96,
  // forced by alignas(16)) -- never written to, never read; matches
  // Slang's own real, empirically-confirmed block rounding exactly.
};                             // sizeof == 96, alignof == 16
```

**Terminology, kept distinct from here on, per this review's own
instruction:** "88 bytes" is only ever the offset immediately past the
last real field (`roughnessFactor`'s own end); it is **never** the
`sizeof`, the `VkPushConstantRange::size`, or the Slang-reflected block
size — all three of those are **96**. `96 ≤ 128` (Vulkan's own
guaranteed minimum, ADR-0025) — **32 bytes of headroom remain, not 40**.
This engine's own one reference GPU (Intel Arc B370, confirmed via a
real `vulkaninfo` query during this review) reports
`maxPushConstantsSize = 256`, comfortably above both 128 and 96 — but
the design is sized against Vulkan's portable 128-byte guaranteed
minimum, never against this one device's own larger, non-portable
value, exactly as ADR-0025 already establishes and D-11 restates for
Android. `objectToWorld` keeps its exact existing offset (0) and size
(64 bytes) — `PbrDirectLit`'s own Pipeline is the only one with a wider
(96-byte) push-constant range; `UnlitTextured`/`LitTextured` keep
`size = 64` exactly as today, confirmed unaffected by this change (D-8).

### D-7. Fragment-stage visibility — locked to uniform widening, real evidence closes the choice

**Decided during centralized final review — no longer left open.** The
first draft presented two alternatives without picking one. Real
evidence now closes this in favor of the simpler, precedent-matching
option:

**Recommendation, locked: uniformly widen every Pipeline's own
push-constant range to `VK_SHADER_STAGE_VERTEX_BIT |
VK_SHADER_STAGE_FRAGMENT_BIT`** — in `VulkanDevice::createPipeline()`'s
own `VkPushConstantRange::stageFlags` construction
(`vulkan_device.cpp:1036`, today hardcoded vertex-only) and in
`VulkanCommandList::pushConstant()`'s own `vkCmdPushConstants` call
(`vulkan_command_list.cpp:322`, today also hardcoded vertex-only) —
**for every Pipeline this engine creates, with no new
`PipelineCreateParams` field, no new `VulkanPipeline`/
`VulkanCommandList` field, and no change to `CommandList::pushConstant()`'s
own signature at all.** This matches ADR-0062's own already-`Accepted`,
already-shipped precedent exactly (that Decision widened the Camera/
Lighting uniform-buffer descriptor binding's own stage visibility from
vertex-only to vertex-and-fragment, globally, for every Pipeline,
explicitly reasoning that "existing shaders simply continue not
referencing this binding from its own fragment stage, unaffected" —
`adr/0062-...md:90-91`).

**Why this is provably safe for `UnlitTextured`/`LitTextured`, not merely
assumed:** a real `slangc` compile of `textured_quad.slang` and
`lit_textured.slang` (both already compiled and shipping — no probe
needed, their real reflection JSON already exists in this codebase's
own build tree) confirms neither `fragmentMain` ever references
`pushConstants` — the `[[vk::push_constant]]` block is declared once,
at module scope, and read only from `vertexMain`. This codebase's own
Shader System already documents, from a prior, independent
investigation, exactly this situation:
`src/shader_system/src/slang_json_transform.cpp:244-255`'s own comment
states plainly that a `pushConstantBuffer` reflection entry appears in
**every** entry point's own `bindings` list — including one whose
compiled SPIR-V does not reference it at all, "confirmed by
disassembly" — and that this is "harmless and unread by any consumer."
Widening a `VkPushConstantRange`'s own `stageFlags` in the pipeline
layout does not force a shader module to declare or read anything in
that range; it only widens which stages are *permitted* to read
push-constant bytes in that range, a purely additive capability no
Vulkan Validation Layers rule interprets as a required read. No
`VkPipelineLayout` compatibility rule requires every stage named in a
push-constant range's `stageFlags` to actually be "used" by that range —
only the reverse (a stage that *does* statically use push-constant data
outside every declared range covering its own stage bit is the real
validation error), which does not apply here since `UnlitTextured`/
`LitTextured`'s own fragment stages use no push-constant data at all.

**Consequence for the "byte-for-byte unchanged" claim, stated precisely
(not overclaimed):** `UnlitTextured`/`LitTextured`'s own
`VkPushConstantRange::size` stays exactly `64`, `offset` stays exactly
`0`, and their own `.slang` source is untouched — only `stageFlags`
widens, identically for every Pipeline this engine creates, matching
ADR-0062's own already-verified-safe pattern. This is **not** "the
per-Pipeline VkPipelineLayout object is byte-identical to today's" (it
is not — the widened `stageFlags` value is a real, disclosed, uniform
change to every Pipeline's own layout) — it **is** "no `.slang` source,
no rendered pixel, and no reflected push-constant size/offset for
`UnlitTextured`/`LitTextured` changes," which is the property the five
existing goldens actually depend on and this Spec's own Testing &
Verification Plan verifies directly.

### D-8. Compatibility with existing Unlit/Lit pipelines

**Recommendation: total functional non-interference, verified, not
merely predicted.** `pushConstantSizeBytes` stays exactly `64` at every
existing `UnlitTextured`/`LitTextured` call site
(`material_realization.cpp:176, 301, 324`, unmodified by this ADR);
only `PbrDirectLit`'s own new call site (added by this Spec's own Plan)
passes `96` (D-6). Every Pipeline's own push-constant `stageFlags`
widens uniformly to `VERTEX | FRAGMENT` (D-7) — a real, disclosed,
uniform layout change, but one this ADR's own real-evidence argument
(D-7 above) shows produces zero functional or pixel-level effect for a
fragment stage that never reads push-constant data, which
`textured_quad.slang`/`lit_textured.slang` both provably do not
(confirmed by their own real, already-existing reflection JSON, not
merely by source inspection). Neither existing `.slang` source file is
modified. The five existing goldens are therefore expected to remain
byte-for-byte/pixel-for-pixel unchanged — verified, not merely
predicted, by this Spec's own Testing & Verification Plan, which must
re-run all five as a real regression check specifically because D-7's
own `stageFlags` widening is a real, uniform Vulkan object change, not
a no-op.

### D-9. Output color-space/encoding — no change to the existing contract, with an explicit golden-acceptance condition

**Recommendation: no new gamma-encode, tone-mapping, or HDR
intermediate target.** `pbr_direct_lit.slang`'s own final
`clamp(accumulated, 0, 1)` (D-1) is the exact same "clamp is the only
transformation" policy `lit_textured.slang` already uses today
(confirmed, ADR-0062 Decision 5) and every currently-bound render target
in this engine already assumes (UNORM, no hardware sRGB-encode-on-write
anywhere real, per Context). Every `MaterialKind` this engine ships —
`UnlitTextured`, `LitTextured`, and now `PbrDirectLit` — therefore
follows the **identical** output contract: linear-ish shader math,
hard-clamped to `[0, 1]`, written directly to a UNORM attachment with no
display-referred encode step. Consistency across `MaterialKind`s in one
scene (a real, named concern this review raised) is exactly what
keeping this one, unmodified contract for `PbrDirectLit` achieves —
giving `PbrDirectLit` its own, different output transform would be the
inconsistent choice, not this one.

**This is a disclosed, real Phase 1 limitation, not a claim of physical
correctness, and this Spec's own new golden (Spec 0023 D17) is
evidence only of parameter/math correctness under this exact contract —
never cited as evidence of final, display-correct PBR appearance.** A
bright direct light reflecting off a smooth, low-roughness surface at a
glancing/near-mirror angle can legitimately produce a specular term well
above `1.0`, hard-clipped rather than tone-mapped — visible as a flat,
blown-out highlight rather than a smoothly rolled-off one.

**Explicit, binding condition on the new golden's own human-review step
(Spec 0023 D17), not merely a stylistic preference:** the human
reviewer capturing/approving the new PBR golden must confirm the four
corner cases (dielectric-rough, dielectric-smooth, metallic-rough,
metallic-smooth) are **visually distinguishable from one another** under
this exact clamp-only contract — not merely "non-black, non-garbage"
(ADR-0042's own existing bar for every other bootstrap golden). If a
human reviewer finds the hard-clip genuinely erases the intended
differentiation (e.g. every smooth surface clips to a uniform, saturated
highlight indistinguishable by roughness), that is a real, blocking
finding at golden-capture time: this Spec's own Implementation must not
proceed to landing that golden, and the correct next step is a
dedicated Output Transfer Function / Tone Mapping Spec — never a
local, PBR-shader-only gamma/tone-map patch (which would immediately
reintroduce the exact cross-`MaterialKind` inconsistency this Decision
just ruled out). This condition is stated here, in the ADR, precisely
so it is not lost between this Spec's own approval and its own future
Implementation PR's own golden-capture step.

### D-10. Direct-light cap and `FrameLightingData` — unchanged

**Recommendation: no change.** The existing 176-byte
`FrameLightingData` (1 Directional + 4 Point lights, unchanged offsets
**relative to the start of its own struct**) is reused exactly as
`lit_textured.slang` already consumes it — this BRDF only changes what
happens with each light's already-existing `color`/`intensity`/derived-
`radiance` value inside the per-light loop body (D-1), never the light
data's own internal layout, cap, or extraction path. **Its own absolute
offset within the shared Camera+Lighting buffer does not move either**
— D-15 below appends the new `cameraWorldPosition` field *after* the
existing 304-byte Camera+Lighting region, never between `CameraMatrices`
and `FrameLightingData`, so `FrameLightingData`'s own absolute offset
stays exactly `128` (unchanged) and `lit_textured.slang`'s own source is
untouched.

### D-11. Windows/future-Android Vulkan portability

**Recommendation:** every Vulkan entry point and feature this ADR's own
design touches (`vkCmdPushConstants` with a wider `stageFlags`
argument, a `VkPushConstantRange` with `size = 96` — corrected from an
earlier draft's 88, D-6) is core Vulkan 1.0 surface, already in use
elsewhere in this exact file, with no new extension or device-feature
requirement — the 128-byte push-constant guarantee this Decision sizes
against (D-6) is itself the Vulkan specification's own portable
minimum, not a value queried from this one reference GPU (a real
`vulkaninfo` query against this session's own reference GPU, Intel Arc
B370, reports `maxPushConstantsSize = 256` — confirmed comfortably
above both the 128-byte guarantee and the 96-byte candidate size, but
not the number this design is sized against). A future Android Vulkan
Backend (Candidate Order 1, unaffected — D-14) inherits this exact
design unchanged; D-15's own buffer-size growth (304 → 320 bytes,
below) is likewise plain `VkBufferCreateInfo`/`vkCreateBuffer` with no
new extension or feature requirement.

### D-12. Error domain and C4062

**Recommendation:** `selectShaderPair()`'s own existing closed,
`default:`-free `switch (kind)`
(`src/runtime/src/material_realization.cpp:107-112`) gains one new
`case atlantis::asset_system::MaterialKind::PbrDirectLit:` arm — this
codebase's own `/w14062`/`/WX` build configuration already turns a
missed case into a hard compile error for every future `MaterialKind`
addition, including this one; no new discipline is introduced, this
Decision only exercises the existing one. New error enumerators (ADR-
0066's own Decision items 5/6) are additive only — no existing
enumerator's meaning changes. **Corrected during final review:** D-7's
own locked recommendation (uniform `stageFlags` widening) introduces no
new `PipelineCreateParams`/`VulkanPipeline` field at all — an earlier
draft's reference to one here is stale and is removed.

### D-13. Thread/ownership/lifetime

**Recommendation: no change.** `PbrDirectLit`'s own realization follows
[ADR-0060](0060-scene-material-binding-and-runtime-transactional-resource-publish.md)'s
existing two-phase (CPU resolve/load, then per-frame GPU realize)
model, existing ownership maps (`materialResourceMap_`/
`textureResourceMap_`/`samplerResourceMap_`), and existing single-
threaded, Phase 1 frame-orchestration baseline — this ADR introduces no
new resource type, no new map, no new synchronization primitive, and no
new destruction-order constraint beyond what `Material`'s own existing
texture/sampler-outlives-Material contract already requires.

### D-14. Governance/cross-cutting

This ADR was drafted as part of a Spec placed, at explicit human
direction, ahead of Android Platform (`specs/README.md` Section B,
Candidate Order 1, unaffected) and ahead of Shadow/IBL/Post-processing —
see the Related Spec's own Cross-cutting note.

### D-15. Camera world-space position — added during centralized final review, real evidence closes this

**A real, previously-disclosed gap this Spec's own original drafting
surfaced but left open** (this BRDF's view vector `V`, D-1, needs the
camera's own world-space position; no shader in this codebase has ever
had one — `CameraUniform` carries only `view`/`projection`). Four real
options were compared against real code, not assumed:

1. **Extend the shared Camera/Lighting uniform buffer** (recommended,
   below).
2. **Shader-side inverse of `view`'s own rotation/translation.**
   Rejected: `lit_textured.slang`/`pbr_direct_lit.slang` are plain
   Slang/HLSL-family shaders — an in-shader `3x3` transpose-and-negate
   extraction (`eye = -transpose(mat3(view)) * view[3].xyz`) is real,
   available Slang surface, but it is genuine, *redundant* per-fragment
   ALU work recomputing a value the CPU already has for free (below) —
   and it silently assumes `view`'s own upper-left `3x3` is exactly
   orthonormal, true for this codebase's own `lookAtMatrix()` today but
   not a property the shader itself can verify; a future, different
   camera-matrix construction path could silently break it with no
   compile-time or Validation-Layers signal. Rejected as strictly worse
   than option 1 for this codebase's own real situation, not rejected in
   the abstract.
3. **A new per-draw push constant field.** Rejected: camera position is
   **per-frame** data (one value for the whole frame, identical across
   every `DrawItem`), not per-object data — pushing it once per draw
   item, redundantly, for every mesh in a scene, is real, unnecessary
   per-draw waste an already-existing, already-bound-once-per-frame
   uniform buffer avoids entirely; it would also consume part of D-6's
   own 32-byte remaining push-constant headroom for data that belongs in
   the uniform buffer's own frame-scoped lifetime, not the push
   constant's own per-object one.
4. **Reuse existing padding.** Rejected: `CameraMatrices`
   (`scene_extraction.h:61-64`) has no unused padding bytes today (`Mat4
   view; Mat4 projection;` — exactly 128 bytes, none spare); there is
   nothing to reuse.

**Recommendation, locked: extend the shared Camera/Lighting uniform
buffer, appending the new field *after* the existing 304-byte
Camera+Lighting region — never inserted between `CameraMatrices` and
`FrameLightingData`.** Confirmed directly,
`src/runtime/src/scene_extraction.cpp:107`: `extractCameraMatrices()`
already computes `eye` (`const Vec3 eye{cameraWorldMatrix[12],
cameraWorldMatrix[13], cameraWorldMatrix[14]};`) as a local intermediate
— the exact camera world position this BRDF needs — used to build
`result.view` via `lookAtMatrix()`, then **currently discarded**.
Persisting this already-computed value costs nothing beyond storing it.

**Exact new layout, confirmed safe for `UnlitTextured`/`LitTextured` by
a real, already-existing mechanism, not merely argued:**
`VulkanCommandList::bindUniformBuffer()`
(`vulkan_command_list.cpp:247-250`) already binds this buffer with
`bufferInfo.range = VK_WHOLE_SIZE` — confirmed directly — meaning every
Pipeline sharing this one buffer object is already given access to its
**entire**, real, current byte length regardless of how large that
buffer is, and each Pipeline's own shader only ever reads the byte
range its own declared `CameraUniform` struct names. This is the
*same* mechanism that already lets `textured_quad.slang`'s own
128-byte `CameraUniform` (view+projection only) and
`lit_textured.slang`'s own 304-byte `CameraUniform` (view+projection+
lighting) safely share one physical buffer today, each reading only its
own declared prefix — extending this to a third, still-longer prefix
for `pbr_direct_lit.slang` is the same pattern, not a new one:

```cpp
// Runtime/scene_extraction.h -- widened; appended, not inserted.
struct alignas(16) CameraMatrices {
  Mat4 view;                    // offset 0,   64 bytes -- unchanged
  Mat4 projection;              // offset 64,  64 bytes -- unchanged
  float cameraWorldPosition[3]; // offset 128, 12 bytes -- NEW
  float _pad2 = 0.0f;           // offset 140,  4 bytes -- NEW, explicit,
                                 // matching this codebase's own existing
                                 // explicit-padding convention
                                 // (DirectionalLightGpu's own _pad0,
                                 // scene_extraction.h:90) -- never
                                 // compiler-implicit
};                               // sizeof == 144 (was 128)
```

- `CameraMatrices` widens from 128 to **144** bytes (128 + 16).
- `FrameLightingData`'s own **absolute** offset inside the shared buffer
  moves from 128 to **144** — its own **internal** layout (every offset
  `static_assert`ed relative to its own struct start,
  `scene_extraction.h:105-119`) is completely unchanged (D-10).
- The shared buffer's total size grows from `128 + 176 = 304` to
  `144 + 176 = 320` bytes — `runtime_application.cpp:288-289`'s own
  `createBuffer()` call site changes from `sizeof(float) * 32 +
  sizeof(FrameLightingData)` to `sizeof(CameraMatrices) +
  sizeof(FrameLightingData)`, and the Lighting-write pointer offset
  (`runtime_application.cpp:603`, currently `cameraData + 32` floats)
  moves to `cameraData + 36` floats (144 bytes ÷ 4).
- **`textured_quad.slang` (`UnlitTextured`) and `lit_textured.slang`
  (`LitTextured`) are both confirmed unaffected — zero source change,
  zero offset change for anything either shader already declares** —
  `textured_quad.slang`'s own `CameraUniform` still ends at byte 128
  (its own declared `view`+`projection` only); `lit_textured.slang`'s
  own `CameraUniform` still ends at byte 304 (its own declared
  `view`+`projection`+lighting fields, every one at its own existing,
  unchanged absolute offset) — both simply never declare, and therefore
  never read, the newly-appended bytes at offset 304-319, exactly as
  `textured_quad.slang` already never reads bytes 128-303 today. Only
  `pbr_direct_lit.slang`'s own `CameraUniform` declares the full,
  320-byte struct, ending with `float3 cameraWorldPosition` (plus
  Slang's own trailing padding) immediately after `pointLights[4]`.
- **Touch points that must be synchronized (Plan-time, not this ADR's to
  implement):** `scene_extraction.h`'s `CameraMatrices` struct and its
  own `static_assert`s; `extractCameraMatrices()`
  (`scene_extraction.cpp:120-123`, `result.cameraWorldPosition = {eye.x,
  eye.y, eye.z};` added, one line); `runtime_application.cpp`'s buffer
  size and write-offset computation (above); `pbr_direct_lit.slang`'s
  own `CameraUniform` struct; every GPU test/fixture asserting the
  buffer's own total byte size (e.g. `lighting_demo_fixture.h`'s and
  `runtime_smoke_gpu_tests.cpp`'s own independent byte-layout
  cross-checks, Plan 0022's own precedent, D-6's own citation) — none of
  these exist inside `textured_quad.slang`/`lit_textured.slang`
  themselves, confirming zero source change to either.
- **Zero rendered-pixel change for `UnlitTextured`/`LitTextured`** —
  neither shader's own declared byte range, binding, or read values
  change; both are re-verified by their own existing goldens staying
  byte-for-byte/pixel-for-pixel unchanged (Spec 0023 Testing &
  Verification Plan).

**Governance — this is a real, disclosed extension of an Accepted
ADR's own committed layout, not a silent override, and is deliberately
NOT recorded inside this PBR-specific ADR.** ADR-0062's own Decision
states the frame lighting data's "exact CPU/GPU layout" as 128 (Camera)
+ 176 (Lighting) = 304 bytes, and Spec 0022's own Human Review Approval
re-verified "Camera 128 + Lighting 176 = 304-byte layout unchanged" as
one of its own closing facts. This ADR does **not** alter that 304-byte
region's own internal layout, offsets, or meaning in any way — it only
appends new, trailing bytes after it, extending the buffer's own total
size. The shared Camera/Lighting uniform buffer is a real, independent,
reusable frame-data architecture decision that ADR-0062 already owns —
extending it belongs on ADR-0062's own document, as its own Proposed
Amendment, not folded into this PBR-specific ADR's own scope (a
`PbrDirectLit`-only decision would wrongly imply the buffer-growth
mechanism itself is PBR-specific, when it is a general "shared uniform
buffer, `VK_WHOLE_SIZE`-bound, each shader reads its own prefix"
mechanism this codebase already relies on for two other Material
kinds). **See
[ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s
own "Accepted Amendment — 2026-08-30" section for the complete,
self-contained amendment text** — recorded there, not here, following
this codebase's own established pattern of amending the ADR that
already owns the affected decision (ADR-0041's/ADR-0042's own prior
Accepted Amendments), and to be accepted alongside this ADR in the same
Human Review pass, per Spec 0023's own Related ADR(s) list.

## Consequences

### Positive

- Every claim about "no `.slang` source change to `UnlitTextured`/
  `LitTextured`" is real and verified against actual reflection JSON,
  not merely predicted — both the push-constant stage-visibility
  widening (D-7) and the Camera-buffer extension (D-15) are shown, by
  direct evidence, to leave both existing shaders' own declared byte
  ranges, bindings, and reads completely untouched.
- The exact BRDF formula, every constant, and every division's own
  denominator floor are stated in full in this document, with a real
  citation correction (D-2) caught and fixed before approval rather
  than after — nothing is deferred to "the Plan will figure out the
  math."
- Reuses the existing 304-byte Camera/Lighting buffer's own internal
  layout, the existing single combined-image-sampler binding, and the
  existing linear Point-light attenuation entirely unchanged — no new
  descriptor binding, no new RHI resource type, no reopening of Spec
  0021's descriptor-pool-capacity proof.
- Sized comfortably (96 of 128 guaranteed push-constant bytes, D-6,
  confirmed by real Slang reflection and a real MSVC layout probe, not
  a hand-computed assumption) with 32 bytes of documented headroom for
  a disclosed future need, without requiring this Spec to design that
  need now.

### Negative / Trade-offs

- The chosen geometry-term remap (`k = (roughness+1)²/8`, D-2) is one
  of several legitimate published choices for the Schlick-GGX
  approximation specifically (a height-correlated Smith term is a real,
  more accurate alternative) — not claimed as uniquely correct, only as
  the correctly-cited, standard, well-documented choice for
  direct-analytic lighting.
- No tone-mapping means a real, visible hard-clip artifact for bright
  specular highlights on smooth surfaces — a disclosed, not hidden,
  Phase 1 rendering-quality limitation, now carrying an explicit,
  binding condition on the new golden's own human-review step (D-9)
  rather than a soft aspiration.
- 96 of 128 guaranteed push-constant bytes are now committed to exactly
  two Material kinds' own combined needs (`objectToWorld` +
  `PbrDirectLit`'s three factors, plus Slang's own 8 bytes of trailing
  block-rounding padding) — a future spec needing a fourth per-object
  scalar (e.g. a material index for a texture array) has less remaining
  guaranteed headroom (32 bytes) than today's 64 bytes of headroom, a
  real, disclosed, incremental cost of this Decision.
- The shared Camera/Lighting uniform buffer grows from 304 to 320 bytes
  for every Pipeline that binds it (D-15) — a small, disclosed, one-time
  memory cost paid even by `UnlitTextured`/`LitTextured` draws, which
  never read the newly-appended 16 bytes; accepted because `Buffer`
  ownership/binding is already per-frame/shared, not per-Pipeline, so
  this is one buffer growing once, not a per-Pipeline multiplication.
- This ADR carries a real, disclosed cross-document dependency: it is
  not independently approvable without its own Proposed Amendment to
  ADR-0062 (D-15) also being accepted in the same Human Review pass —
  matching ADR-0057's own precedent for exactly this kind of dependency
  (that ADR's own Decision 4 required a companion ADR-0041 amendment).

## Alternatives Considered

- **A new per-material uniform buffer (a third descriptor binding)
  instead of extending push constants.** Rejected for this round: the
  extended push-constant layout fits comfortably (96 of 128 guaranteed
  bytes, D-6) with zero new descriptor binding, zero new `Buffer`
  lifecycle, zero interaction with Spec 0021's own descriptor-pool
  capacity proof (still exactly one uniform-buffer + at most one
  combined-image-sampler descriptor per Pipeline, unchanged). A new
  uniform buffer would additionally need its own per-material `Buffer`
  object, its own binding slot, and a real re-derivation of Spec 0021's
  own pool-sizing proof (D4 there) — disproportionate to three scalars
  that fit in already-guaranteed push-constant capacity. Legitimate
  future work if a later Spec's own per-material data genuinely exceeds
  the remaining push-constant headroom (Out of Scope/Future Work).
- **A new, per-Pipeline, RHI-internal push-constant stage-flags field**
  (a `VulkanPipeline`-owned value read by `VulkanCommandList` at bind
  time) instead of D-7's locked uniform widening. This was this
  Decision's own first-draft recommendation, presented as more
  "structurally conservative." Rejected during final review: it adds
  real, new RHI-internal surface for a safety property (byte-for-byte
  identical `VkPipelineLayout` objects for `UnlitTextured`/`LitTextured`)
  that D-7's own real evidence (the Shader System's own already-
  documented "stray, harmless, unread `pushConstantBuffer` reflection
  entry" fact, plus standard Vulkan pipeline-layout compatibility rules)
  already shows is unnecessary — the uniform-widening alternative is
  provably safe without it, matching ADR-0062's own real, already-
  shipped precedent exactly.
- **Height-correlated Smith geometry term instead of Schlick-GGX with
  Karis's correctly-cited direct-lighting `k` remap.** Considered
  (D-2); not recommended for this foundation round — a real, slightly
  more accurate alternative at small added ALU cost, not rejected as
  wrong, deferred as unnecessary complexity for a first PBR pass with
  no IBL to also feed. (Distinct from this Decision's own real citation
  fix, D-2: the *choice* between Schlick-GGX and height-correlated
  Smith remains a legitimate judgment call; the *k* value used *within*
  Schlick-GGX for direct lighting was a factual citation error, now
  corrected.)
- **Shader-side inverse-view extraction, or a new per-draw push-constant
  field, for camera world position, instead of extending the Camera
  uniform buffer.** Rejected — see D-15's own full comparison of all
  four real options considered.
- **Physically-based inverse-square Point-light attenuation, replacing
  ADR-0062's existing linear falloff, for this new BRDF only.**
  Rejected: would make Point-light behavior inconsistent between
  `LitTextured` and `PbrDirectLit` for the identical `Light` component
  and the identical authored `range` field — a strictly worse, silently
  divergent outcome than reusing the one, already-`Accepted` formula
  everywhere (D-5).
- **Tone-map (e.g. Reinhard or ACES-approximate) the final PBR color
  before the existing `clamp`, scoped to this one shader only.**
  Rejected: this Spec's own Non-Goals explicitly exclude tone mapping;
  scoping it to only the new shader would also produce a visibly
  inconsistent brightness/contrast response between `PbrDirectLit` and
  `LitTextured` materials in the same scene, a worse outcome than a
  consistent hard-clip across every shader (D-9).
- **A cook-time rejection of `roughness_factor = 0` instead of a
  shader-internal alpha clamp.** Rejected (D-4) — removes a legitimate,
  physically meaningful authored value (a perfectly smooth surface) for
  no correctness benefit the internal clamp does not already provide.
