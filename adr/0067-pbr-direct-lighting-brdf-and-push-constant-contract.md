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
  of Spec 0023's own Human Review Approval (2026-08-30, commit
  `0fc6a14`), in the same pass as ADR-0062's own Amendment below (not
  independently approvable without it — see this ADR's own
  Consequences). Does not change this ADR's own Decision, Consequences,
  or Alternatives Considered above.
- **Related ADR(s):**
  [ADR-0025](0025-rhi-minimal-pipeline-binding-and-draw-command-surface.md)
  (`Accepted` — states Vulkan's own guaranteed minimum push-constant
  capacity, 128 bytes across all stages, which this ADR's own layout is
  sized against),
  [ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
  (`Accepted`, carrying its own **Accepted Amendment — 2026-08-30**,
  the sole authoritative source for the shared Camera/Lighting/
  CameraWorldPosition buffer layout; this ADR only summarizes and links
  it, never redefines it),
  [ADR-0066](0066-pbr-material-asset-parameter-set-and-color-space-contract.md)
  (`Accepted` — the three parameter fields this ADR consumes).

## Context

Confirmed directly against real, current source: the only existing
push-constant chain is a 64-byte, vertex-only `objectToWorld` matrix
(`DrawItem::objectToWorld`, `renderer.cpp:39`,
`CommandList::pushConstant(const void*, std::size_t)` — no offset/
stage-flags parameter; the Vulkan implementation hardcodes
`VK_SHADER_STAGE_VERTEX_BIT`/offset `0` for every Pipeline,
`vulkan_command_list.cpp:320-324`, `vulkan_device.cpp:1035-1045`).
Vulkan's own guaranteed minimum push-constant capacity is 128 bytes
across all stages (ADR-0025). Shader System's own push-constant
validation is deliberately vertex-stage-only
(`compile_and_validate.cpp:157-178`); a `pushConstantBuffer` reflection
entry appears in every entry point's own bindings list even when that
entry's compiled SPIR-V never references it — confirmed "harmless and
unread by any consumer" (`slang_json_transform.cpp:244-255`).
`lit_textured.slang`'s real, shipped BRDF is pure N·L diffuse — no
ambient, no specular, no tone-mapping; its final color is
`clamp(texColor.rgb * accumulated, 0, 1)`, the only transformation
before the UNORM render target's write (ADR-0062 Decision 5). No shader
in this codebase has ever had the camera's own world-space position.

## Decision

**A new, closed `MaterialKind::PbrDirectLit`, rendering via a new
`pbr_direct_lit.slang` built-in shader pair implementing a metallic-
roughness Cook-Torrance BRDF (Lambertian diffuse + GGX/Smith-Schlick
specular) for Directional and Point lights only. The three new PBR
parameters travel as an extended, 96-byte push constant. Fragment-stage
visibility is granted by uniformly widening every Pipeline's own
push-constant `stageFlags`. The BRDF's view vector uses a camera
world-space position added via
[ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s
own Amendment.**

### D-1. Exact BRDF formula

Per active light (Directional or Point), accumulated with the same
`for`-loop-and-`+=` shape `lit_textured.slang` already uses:

```
N = normalize(worldNormal)
V = normalize(cameraWorldPosition - worldPosition)
texColor  = baseColorTexture.Sample(uv)          // Rgba8Srgb source,
                                                   // hardware-decoded to
                                                   // linear at sample
                                                   // time (ADR-0066)
baseColor = texColor.rgb * baseColorFactor.rgb    // both linear-space
alpha_out = texColor.a * baseColorFactor.a        // stored, currently
                                                   // inert -- this
                                                   // engine has no
                                                   // blending anywhere
                                                   // (blendEnable =
                                                   // VK_FALSE, every
                                                   // Pipeline)
metallic  = clamp(metallicFactor, 0, 1)
roughness = clamp(roughnessFactor, 0, 1)
alpha     = max(roughness * roughness, kMinAlpha)  // kMinAlpha = 1e-3
F0        = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic)
diffuseColor = baseColor * (1.0 - metallic)

accumulated = float3(0, 0, 0)   // no ambient term, matching lit_textured.slang

NdotL = max(dot(N, L), 0.0)     // L per-light, exactly as
                                 // lit_textured.slang derives it
                                 // (Directional: -direction; Point:
                                 // normalize(position - worldPosition),
                                 // radiance including ADR-0062's
                                 // existing, unchanged linear
                                 // attenuation)
if (NdotL > 0.0) {               // branch, not a trailing multiply --
                                 // D/G/F can independently produce NaN
                                 // (0/0, degenerate H) that a trailing
                                 // "* NdotL" would not zero out
  NdotV = max(dot(N, V), kMinDot)      // kMinDot = 1e-4, matches the
                                       // existing kPointLightDistanceEpsilon
  H     = normalize(L + V)
  NdotH = max(dot(N, H), 0.0)
  VdotH = max(dot(V, H), 0.0)

  D = (alpha * alpha) / (kPi * pow(NdotH * NdotH * (alpha * alpha - 1.0) + 1.0, 2.0))

  k  = (roughness + 1.0) * (roughness + 1.0) / 8.0   // Karis's own
                                                      // DIRECT-LIGHTING
                                                      // remap, using
                                                      // roughness
                                                      // directly, never
                                                      // alpha (alpha/2
                                                      // is his IBL
                                                      // remap -- not
                                                      // used here)
  G1_V = NdotV / (NdotV * (1.0 - k) + k)
  G1_L = NdotL / (NdotL * (1.0 - k) + k)
  G  = G1_V * G1_L

  F  = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0)

  specular = (D * G * F) / max(4.0 * NdotV * NdotL, kMinDot)
  kD = (float3(1,1,1) - F) * (1.0 - metallic)

  accumulated += (kD * diffuseColor / kPi + specular) * radiance * NdotL
}

finalRgb = clamp(accumulated, 0.0, 1.0)   // same clamp policy as
                                           // lit_textured.slang; no
                                           // tone-mapping (D-4)
return float4(finalRgb, alpha_out)
```

`kPi = 3.14159265358979323846`. Point-light `radiance`/attenuation
reuses ADR-0062's existing, unchanged linear falloff verbatim
(`clamp(1 - dist/range, 0, 1)`, `kPointLightDistanceEpsilon = 1e-4`
floor on `dist`) — including its own pre-existing, well-defined
`range = 0` edge case (`dist/0 → +Inf → clamp(-Inf,...) = 0`, never
`NaN`), inherited unchanged, not introduced by this ADR.
`FrameLightingData`'s own 176-byte layout and 1-Directional/4-Point cap
are entirely unchanged.

### D-2. Numerical stability

`kMinAlpha` (1e-3) guards only `alpha` (used by `D`); it is never
applied to `k`, which uses the clamped, un-squared `roughness` directly
and needs no floor of its own for any `roughness ∈ [0, 1]`. Every
division's denominator is floored (`NdotV`, `D`'s `pow(...)` term,
`specular`'s `max(4·NdotV·NdotL, kMinDot)`). `roughness = 0` (a
legitimate, authorable mirror-like surface) is never rejected at cook
time — only `alpha` is clamped inside the shader.

### D-3. Push-constant layout — real Slang and MSVC evidence

A real `slangc` compile of the candidate block (both stages, using this
repository's own real `buildSlangcArgv()` invocation) and a real MSVC
`alignas(16)` layout probe both confirm:

```cpp
struct alignas(16) PbrPushConstants {
  float objectToWorld[16];    // offset 0,  64 bytes -- unchanged
  float baseColorFactor[4];   // offset 64, 16 bytes
  float metallicFactor;       // offset 80,  4 bytes
  float roughnessFactor;      // offset 84,  4 bytes
  float _pad[2] = {0, 0};     // offset 88,  8 bytes, explicit
};                             // sizeof == 96, alignof == 16
```

`96 ≤ 128` (Vulkan's guaranteed minimum, ADR-0025) — **32 bytes of
headroom.** This session's own one reference GPU (Intel Arc B370, real
`vulkaninfo` query) reports `maxPushConstantsSize = 256`, recorded here
as environment context only — the design is sized against the 128-byte
portable guarantee, never against this one device's larger value.
`objectToWorld` keeps its existing offset/size; only `PbrDirectLit`'s
own Pipeline gets the wider (96-byte) range.

### D-4. Push-constant fragment-stage visibility

**Recommendation, pending Human Review approval: uniformly widen every
Pipeline's own `VkPushConstantRange::stageFlags` to `VERTEX |
FRAGMENT`** (`vulkan_device.cpp:1036`) and the matching
`vkCmdPushConstants` call (`vulkan_command_list.cpp:322`) — matching
ADR-0062's own already-shipped precedent for the uniform-buffer
binding. No new `PipelineCreateParams` field, no new RHI-internal
state, `CommandList::pushConstant()`'s own public signature unchanged.
Safe for `UnlitTextured`/`LitTextured`: neither shader's fragment stage
declares or reads `pushConstants` (confirmed directly against their
real, unmodified `.slang` source); a wider `stageFlags` only widens
which stage is *permitted* to read the range, never forces a read, and
Vulkan's own pipeline-layout compatibility rules require a declared
range only for a stage that *statically uses* push-constant data
outside it — not the reverse.

### D-5. Camera world-space position

`V` needs the camera's own world-space position. Supplied by
[ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s
own Amendment (not yet accepted — see that ADR for the complete,
authoritative buffer layout, real evidence, and rejected alternatives):
a new, tail-only `CameraWorldPositionData` (16 bytes) appended after
the existing, unmodified 304-byte Camera+Lighting region — buffer
grows to 320 bytes; `pbr_direct_lit.slang` alone declares the full
320-byte struct. This ADR does not restate that layout — accepting this
ADR requires accepting ADR-0062's own Amendment in the same pass.

### D-6. Output color-space — no change, with a binding golden condition

No new gamma-encode, tone-mapping, or HDR target — `pbr_direct_lit.slang`
reuses the exact "final clamp is the only transformation" contract
every `MaterialKind` in this engine already uses (ADR-0062 Decision 5),
keeping all three kinds visually consistent in a mixed scene. Real,
disclosed Phase 1 limitation: a bright specular highlight on a smooth
surface can hard-clip rather than roll off. **Binding condition on
Spec 0023's own new golden's human-review step:** the four corner cases
(dielectric/metallic × rough/smooth) must be confirmed visually
distinguishable under this exact contract, not merely non-black; if not,
Implementation must stop and a dedicated Output Transfer Function Spec
is the correct next step — never a local, `PbrDirectLit`-only patch.

### D-7. Compatibility, portability, error domain, ownership

- **Compatibility:** `pushConstantSizeBytes` stays `64` at every
  existing `UnlitTextured`/`LitTextured` call site
  (`material_realization.cpp:176, 301, 324`); neither `.slang` file is
  modified. The five existing goldens are re-verified byte-for-byte/
  pixel-for-pixel unchanged as a real regression check (D-4's own
  `stageFlags` widening is a real, uniform Vulkan object change, not a
  no-op, even though it has no functional effect on either shader).
- **Portability:** every Vulkan surface touched (`vkCmdPushConstants`
  with a wider `stageFlags`, a 96-byte `VkPushConstantRange`, a 320-byte
  `vkCreateBuffer`) is core Vulkan 1.0, no new extension or device
  feature — Android Platform (Candidate Order 1) unaffected.
- **Error domain:** `selectShaderPair()`'s existing closed,
  `default:`-free switch gains one new arm — `/w14062`/`/WX` already
  makes a missed case a compile error.
- **Ownership/threading:** `PbrDirectLit` follows ADR-0060's existing
  two-phase realization model and Phase 1's single-threaded baseline —
  no new resource type, map, or synchronization primitive.

## Consequences

### Positive

- Every "no `.slang` source change to `UnlitTextured`/`LitTextured`"
  claim is verified against real reflection JSON and real code, not
  merely predicted.
- The exact BRDF formula, every constant, and every division's
  denominator floor are stated in full — nothing deferred to "the Plan
  will figure out the math."
- Reuses the existing single combined-image-sampler binding and the
  existing linear Point-light attenuation entirely unchanged — no
  reopening of Spec 0021's descriptor-pool-capacity proof.
- 96 of 128 guaranteed push-constant bytes, with 32 documented bytes of
  headroom, confirmed by real Slang reflection and a real MSVC layout
  probe, not a hand-computed assumption.

### Negative / Trade-offs

- A future spec needing a fourth per-object push-constant scalar has
  32 bytes of headroom left, not today's 64.
- The shared Camera/Lighting buffer grows from 304 to 320 bytes for
  every Pipeline that binds it — a small, one-time memory cost paid
  even by draws that never read the new 16 bytes.
- This ADR is not independently approvable without ADR-0062's own
  Amendment also being accepted in the same Human Review pass
  (matching ADR-0057's own precedent for exactly this kind of
  cross-document dependency).
- No tone-mapping means a real, visible hard-clip artifact for bright
  specular highlights — disclosed, with a binding golden-review
  condition (D-6), not merely a soft aspiration.

## Alternatives Considered

- **A new per-material uniform buffer** instead of extended push
  constants. Rejected: 96 of 128 guaranteed bytes already suffice with
  zero new descriptor binding, zero new `Buffer` lifecycle, zero
  interaction with Spec 0021's pool-capacity proof.
- **A new, per-Pipeline, RHI-internal push-constant stage-flags field**
  instead of D-4's uniform widening. Rejected: real evidence (the
  Shader System's own documented "stray, harmless, unread
  `pushConstantBuffer`" fact, plus standard Vulkan pipeline-layout
  compatibility rules) already proves uniform widening is safe without
  it.
- **Height-correlated Smith geometry term** instead of Schlick-GGX.
  Considered; not recommended for this foundation round — legitimate,
  slightly more accurate, deferred as unnecessary complexity with no
  IBL to also feed.
- **Shader-side inverse-view extraction, or a per-draw push constant,**
  for camera position, instead of extending the uniform buffer.
  Rejected — see ADR-0062's own Amendment for the full comparison.
- **Physically-based inverse-square Point-light attenuation**, replacing
  ADR-0062's linear falloff for this BRDF only. Rejected: would make
  Point-light behavior inconsistent between `LitTextured` and
  `PbrDirectLit` for the identical `Light` component.
- **Scoped tone-mapping for this one shader only.** Rejected: Non-Goals
  exclude tone mapping; scoping it here alone would produce a visibly
  inconsistent brightness response between `MaterialKind`s in the same
  scene.
