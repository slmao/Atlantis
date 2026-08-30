# ADR 0067: PBR Direct-Lighting BRDF, Push-Constant Extension, and Rendering Contract

- **Status:** Proposed
- **Date:** 2026-08-30
- **Deciders:** slmao — pending Human Review as part of
  [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)'s
  own Human Review Approval.
- **Related Spec:** [specs/0023-pbr-material-foundation.md](../specs/0023-pbr-material-foundation.md)
  (`In Review`)
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
  (`Proposed` — this ADR consumes the three parameter fields that ADR
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

**A new, closed `MaterialKind::PbrDirectLit`, rendering via a new
`pbr_direct_lit.slang` built-in shader pair implementing a precisely
specified metallic-roughness Cook-Torrance BRDF (Lambertian diffuse +
GGX/Smith-Schlick specular) for Directional and Point lights only,
reusing the existing, unmodified 304-byte Camera/Lighting uniform buffer
and the existing single combined-image-sampler binding. The three new
per-material parameters travel as an extended, per-Pipeline push
constant — `objectToWorld` (64 bytes, offset 0, unchanged) followed by
`baseColorFactor`/`metallicFactor`/`roughnessFactor` (24 bytes, offset
64) — fitting in 88 bytes total, well inside Vulkan's guaranteed
128-byte minimum. Fragment-stage visibility for the new bytes is granted
per-Pipeline, not globally, via a new, RHI-internal (not public-API-facing)
push-constant stage-flags concept — `CommandList::pushConstant()`'s own
public signature does not change. `UnlitTextured`/`LitTextured`'s own
`VkPipelineLayout`/`VkPushConstantRange` objects are therefore
*structurally*, not merely behaviorally, unchanged.**

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
alpha_out = texColor.a * baseColorFactor.a
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

  k  = (alpha) / 2.0                                       // direct-lighting
                                                            // k-remap; see
                                                            // D-2 below
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
with Karis's direct-lighting `k = alpha/2` remap; Schlick's Fresnel
approximation; the `kD`/energy-conserving diffuse-specular split), cited
here by its standard formulation, not invented by this codebase — see
Alternatives Considered for the specific geometry-term remap this
Decision picked over its alternatives.

### D-2. Geometry term remap

**Recommendation: Karis's direct-lighting remap, `k = alpha / 2`**
(as used above), over Disney/UE4's alternative `k = (roughness + 1)^2 /
8` (the "IBL-tuned" remap) or a height-correlated Smith visibility term.
`k = alpha/2` is the simpler, more commonly cited form for direct
(non-IBL) lighting specifically — appropriate given this Spec's own
Non-Goals exclude IBL entirely — and keeps the geometry term's own
derivation traceable to `alpha` (already computed for `D`) without a
second, differently-shaped roughness remap. **Open for Human Review**:
the height-correlated Smith term is a real, slightly more physically
accurate alternative at a small added ALU cost; this Decision does not
claim `k = alpha/2` is uniquely correct, only that it is the standard,
well-documented choice for a direct-lighting-only foundation.

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
authored `range` itself).

### D-5. Point-light attenuation — reused unchanged

**Recommendation: no new attenuation formula.** This ADR's own
Point-light `radiance` term reuses ADR-0062's existing, `Accepted`
linear falloff (`clamp(1 - dist/range, 0, 1)`,
`adr/0062-...md:130-135`) verbatim, including its own existing
`kPointLightDistanceEpsilon` (`1e-4`) floor on `dist` — this ADR does
not reopen, re-derive, or replace that decision; it only adds a second,
richer BRDF that consumes the identical per-light `radiance` value
`lit_textured.slang`'s own diffuse-only accumulation already consumes.
An inverse-square alternative remains explicitly out of scope, for the
identical reason ADR-0062 already gave (`adr/0062-...md:199-201`): no
natural finite cutoff, in tension with the authored, testable `range`
field.

### D-6. Push-constant layout, exact

**Recommendation, per the Context section's own real-Vulkan-guarantee
citation:**

```cpp
// C++ side (Renderer/Runtime), Slang side (pbr_direct_lit.slang) --
// identical field order/offsets on both, cross-checked by real Slang
// reflection at Plan/build time, never self-certified by a shared
// literal alone (matching Plan 0022's own "independent byte-layout
// cross-check" precedent, Post-Merge Status Update).
struct PbrPushConstants {
  float objectToWorld[16];    // offset 0,  64 bytes -- byte-identical in
                               // name, offset, and meaning to today's
                               // sole push-constant field
  float baseColorFactor[4];   // offset 64, 16 bytes
  float metallicFactor;       // offset 80,  4 bytes
  float roughnessFactor;      // offset 84,  4 bytes
};                             // total: 88 bytes, packed
```

`88 ≤ 128` (Vulkan's own guaranteed minimum, ADR-0025) with 40 bytes to
spare — no platform-specific push-constant capacity query is needed for
any of this codebase's Phase 1 target platforms (Windows now; Android,
Candidate Order 1, unaffected — see D-11). The real, final struct size
(whether the toolchain inserts any trailing alignment padding beyond the
88 bytes shown) is confirmed against real Slang reflection JSON at Plan
time, not assumed from this hand-computed layout alone — matching this
codebase's own repeated "never self-certify a shared byte layout"
discipline. `objectToWorld` keeps its exact existing offset (0) and size
(64 bytes) — `PbrDirectLit`'s own Pipeline is the only one with a wider
push-constant range; `UnlitTextured`/`LitTextured` keep `size = 64`
exactly as today.

### D-7. Fragment-stage visibility — per-Pipeline, not global; RHI-internal only

**Recommendation:** `VulkanPipeline` gains a new, private field recording
its own push-constant `VkShaderStageFlags` (today implicitly always
`VK_SHADER_STAGE_VERTEX_BIT` for every Pipeline; `PbrDirectLit`'s own
Pipeline is created with `VK_SHADER_STAGE_VERTEX_BIT |
VK_SHADER_STAGE_FRAGMENT_BIT` instead, since `baseColorFactor`/
`metallicFactor`/`roughnessFactor` are consumed by this BRDF's own
fragment-stage code, per D-1). `VulkanCommandList::bindPipeline()`
reads this value from the just-bound `VulkanPipeline` into a new private
field (mirroring how `boundPipelineLayout_` is already captured at
exactly this point,
`src/vulkan_backend/src/vulkan_command_list.cpp:213-218`);
`VulkanCommandList::pushConstant()` uses that per-Pipeline value instead
of its own current hardcoded `VK_SHADER_STAGE_VERTEX_BIT` constant
(`vulkan_command_list.cpp:320-324`). `PipelineCreateParams`
(`src/rhi/include/atlantis/rhi/types.h`) gains one new field carrying
this stage-flags intent (exact shape/name a Plan-time detail — e.g. a
`bool pushConstantFragmentVisible = false`, defaulting to today's
vertex-only behavior for every existing call site that does not set
it). **`CommandList::pushConstant(const void*, std::size_t)`'s own
public signature is unchanged** — stage visibility becomes a
Pipeline-associated property (set once, at Pipeline creation, per
Material kind), not a per-call caller-supplied argument, so every
existing call site (`Renderer::drawFrame()`'s own single call,
`renderer.cpp:39`) needs no change at all. Because
`UnlitTextured`/`LitTextured`'s own `createPipeline()` call sites never
set the new field, their own `VkPipelineLayout`/`VkPushConstantRange`
objects are constructed with the exact same `VK_SHADER_STAGE_VERTEX_BIT`
value as today — **structurally identical Vulkan objects, not merely
"functionally unaffected."**

**Open for Human Review — a real, disclosed alternative exists and is
not silently rejected:** ADR-0062's own precedent (widen one existing
binding's stage visibility globally, for every Pipeline, relying on the
fact that a shader which never reads a binding is unaffected by that
binding becoming reachable) could be applied here too — uniformly
widening every Pipeline's push-constant range to
`VERTEX | FRAGMENT`, with no new `PipelineCreateParams` field at all,
is simpler and has a real, already-shipped, already-`Accepted` precedent
directly on point. This Decision's own recommendation (D-7's per-Pipeline
field) is more conservative — it makes the "byte-for-byte unchanged"
claim for `UnlitTextured`/`LitTextured` structurally true rather than
"true because the shader happens not to read it" — at the cost of one
small, new, RHI-internal field and a few more lines in
`VulkanCommandList`/`VulkanPipeline`. Human Review may prefer the
simpler, precedent-matching global-widening alternative instead; either
choice keeps `CommandList::pushConstant()`'s own public signature
unchanged and touches no file outside `rhi`/`vulkan_backend`.

### D-8. Compatibility with existing Unlit/Lit pipelines

**Recommendation: total, structural non-interference**, per D-6/D-7
above — `pushConstantSizeBytes` stays exactly `64` at every existing
`UnlitTextured`/`LitTextured` call site
(`material_realization.cpp:176, 301, 324`, unmodified by this ADR); only
`PbrDirectLit`'s own new call site (added by this ADR's own Related
Spec's own Plan) passes `88` and (per D-7's chosen alternative) either a
new, explicitly-set stage-visibility field or nothing (if the simpler,
global-widening alternative is chosen by Human Review instead). Neither
existing `.slang` source file is modified. The five existing goldens are
therefore expected to remain byte-for-byte/pixel-for-pixel unchanged —
verified, not merely predicted, by this Spec's own Testing &
Verification Plan.

### D-9. Output color-space/encoding — no change to the existing contract

**Recommendation: no new gamma-encode, tone-mapping, or HDR
intermediate target.** `pbr_direct_lit.slang`'s own final
`clamp(accumulated, 0, 1)` (D-1) is the exact same "clamp is the only
transformation" policy `lit_textured.slang` already uses today
(confirmed, ADR-0062 Decision 5) and every currently-bound render target
in this engine already assumes (UNORM, no hardware sRGB-encode-on-write
anywhere real, per Context). This is a disclosed, real Phase 1
limitation, not a claim of physical correctness: a bright direct light
reflecting off a smooth, low-roughness surface at a glancing/near-mirror
angle can legitimately produce a specular term well above `1.0` before
this clamp, which this Decision hard-clips rather than tone-maps —
visible as a flat, blown-out highlight rather than a smoothly rolled-off
one. This is named explicitly as a real, disclosed rendering-quality
limitation (not hidden inside "matches existing behavior"), with real
tone-mapping/HDR-target work named as this Spec's own explicit future
candidate (Non-Goals).

### D-10. Direct-light cap and `FrameLightingData` — unchanged

**Recommendation: no change.** The existing 176-byte
`FrameLightingData` (1 Directional + 4 Point lights, unchanged offsets)
is reused exactly as `lit_textured.slang` already consumes it — this
BRDF only changes what happens with each light's already-existing
`color`/`intensity`/derived-`radiance` value inside the per-light loop
body (D-1), never the light data's own layout, cap, or extraction path.

### D-11. Windows/future-Android Vulkan portability

**Recommendation:** every Vulkan entry point and feature this ADR's own
design touches (`vkCmdPushConstants` with a wider `stageFlags`
argument, a `VkPushConstantRange` with `size = 88`) is core Vulkan 1.0
surface, already in use elsewhere in this exact file, with no new
extension or device-feature requirement — the 128-byte push-constant
guarantee this Decision sizes against (D-6) is itself the Vulkan
specification's own portable minimum, not a value queried from this
one reference GPU. A future Android Vulkan Backend (Candidate Order 1,
unaffected — D-14) inherits this exact design unchanged.

### D-12. Error domain and C4062

**Recommendation:** `selectShaderPair()`'s own existing closed,
`default:`-free `switch (kind)`
(`src/runtime/src/material_realization.cpp:107-112`) gains one new
`case atlantis::asset_system::MaterialKind::PbrDirectLit:` arm — this
codebase's own `/w14062`/`/WX` build configuration already turns a
missed case into a hard compile error for every future `MaterialKind`
addition, including this one; no new discipline is introduced, this
Decision only exercises the existing one. New error enumerators (ADR-
0066's own D5/D6, this ADR's own new
`PipelineCreateParams`/`VulkanPipeline` field) are additive only —
no existing enumerator's meaning changes.

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

## Consequences

### Positive

- Every claim about "no change to `UnlitTextured`/`LitTextured`" is
  structural (unchanged `VkPipelineLayout`/`VkPushConstantRange` byte
  values), not merely "the new capability happens not to be read" —
  stronger than ADR-0062's own already-accepted precedent, at a small,
  disclosed added-field cost (D-7).
- The exact BRDF formula, every constant, and every division's own
  denominator floor are stated in full in this document — nothing is
  deferred to "the Plan will figure out the math."
- Reuses the existing 304-byte Camera/Lighting buffer, the existing
  single combined-image-sampler binding, and the existing linear
  Point-light attenuation entirely unchanged — no new descriptor
  binding, no new RHI resource type, no reopening of Spec 0021's
  descriptor-pool-capacity proof.
- Sized comfortably (88 of 128 guaranteed bytes) with 40 bytes of
  documented headroom for a disclosed future need, without requiring
  this Spec to design that need now.

### Negative / Trade-offs

- `VulkanCommandList` and `VulkanPipeline` each gain one small new
  field (D-7) purely to make the "structurally unchanged" claim
  literally true — a real, if small, RHI-internal shape addition that
  the simpler, ADR-0062-precedent alternative (global widening) would
  have avoided; Human Review may prefer that simpler alternative
  instead (D-7's own "Open for Human Review" note).
- The chosen geometry-term remap (`k = alpha/2`, D-2) is one of several
  legitimate published choices — not claimed as uniquely correct, only
  as the standard, well-documented choice for this Spec's own
  direct-lighting-only scope.
- No tone-mapping means a real, visible hard-clip artifact for bright
  specular highlights on smooth surfaces — a disclosed, not hidden,
  Phase 1 rendering-quality limitation (D-9).
- 88 of 128 guaranteed push-constant bytes are now committed to exactly
  two Material kinds' own combined needs (`objectToWorld` +
  `PbrDirectLit`'s three factors) — a future spec needing a fourth
  per-object scalar (e.g. a material index for a texture array) has
  less remaining guaranteed headroom (40 bytes) than today's 64 bytes
  of headroom, a real, disclosed, incremental cost of this Decision.

## Alternatives Considered

- **A new per-material uniform buffer (a third descriptor binding)
  instead of extending push constants.** Rejected for this round: the
  extended push-constant layout fits comfortably (88 of 128 guaranteed
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
- **Uniformly widen every Pipeline's push-constant stage visibility to
  `VERTEX | FRAGMENT`, matching ADR-0062's own precedent exactly, with
  no new `PipelineCreateParams` field.** A real, legitimate alternative
  to D-7's own recommendation — not rejected outright, presented as an
  explicit, Human-Review-decidable choice (see D-7's own "Open for
  Human Review" note) rather than silently discarded.
- **Height-correlated Smith visibility term instead of Schlick-GGX
  (`k = alpha/2`).** Considered (D-2); not recommended for this
  foundation round — a real, slightly more accurate alternative at
  small added cost, not rejected as wrong, deferred as unnecessary
  complexity for a first PBR pass with no IBL to also feed.
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
