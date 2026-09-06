# Spec: Directional Shadow Bias Stability

- **Status:** Draft
- **Author:** slmao
- **Created:** 2026-09-07
- **Related Plan(s):** None yet — drafted after this Spec is approved.
- **Related ADR(s):** [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
  (`Accepted`; gains a `Proposed / Pending Human Review` Amendment
  alongside this Spec — see that ADR's own Amendment section).

## Summary

Replaces the single, fixed `kShadowBias = 0.0015` literal in
`computeShadowFactor()` — shared, byte-identical, across
`pbr_direct_lit.slang`, `pbr_ibl.slang`, and Spec 0029's two normal-map
twins `pbr_direct_lit_normal_map.slang`/`pbr_ibl_normal_map.slang` —
with a bounded, geometric-normal, slope-aware receiver-side bias. The
fixed literal was proven insufficient by a real, reproduced defect: a
smooth, curved receiver (the `pbr_sphere` mesh) self-shadows with
visible salt-and-pepper acne across its own grazing-angle/terminator
region under a directional light, discovered while reviewing Spec
0029's `pbr_normal_map_demo` golden candidate.

## Motivation / Problem Statement

While generating and reviewing the `pbr_normal_map_demo` golden
candidate (Spec 0029 Milestone 5), the sphere's own lower-left region —
where its surface normal approaches perpendicular to the directional
light's own direction (`dot(N, L) → 0`, the day/night terminator) —
showed a dense field of near-black, high-frequency speckle, clearly
visible at a glance and confirmed by direct pixel measurement (see
Evidence below). This is self-shadow acne: the shadow map's own stored
depth and the receiving fragment's own light-space depth, computed from
the *same* underlying surface, disagree by more than the fixed
`kShadowBias` at shallow angles, because a flat bias cannot compensate
for a surface whose depth changes rapidly across a shadow-map texel at
grazing incidence — exactly the failure mode `kShadowBias`'s own
Plan 0027 P4 derivation already named as untested ("a reasoned, not yet
empirically-tuned, starting value") and ADR-0072 D-5 already flagged as
future work ("no slope-scaled or normal-offset bias ... all explicitly
out of scope" — of *that* Spec, not permanently).

### Evidence (real, reproduced; not re-derived by this Spec)

A controlled four-way capture (`pbr_normal_map_demo` fixture, same
scene/camera/light/render-target/warm-up flow, only the sphere's own
`Material*` and `shadowCasterDrawItems` varied) isolated the cause:

| Capture | Material | Shadow casters | Sphere-region `rgbSum < 100` pixel count | Visual |
|---|---|---|---|---|
| A | normal-mapped | real (non-empty) | 1018 | granular/broken |
| B | control (no normal map) | real (non-empty) | 1176 | granular/broken |
| C | normal-mapped | empty | 112 | smooth |
| D | control (no normal map) | empty | 729 | smooth |

(Region: sphere bounding box `x∈[205,305], y∈[195,295]` in the fixed
512×512 render; "granular/broken" independently confirmed by direct
visual inspection of all four captures, not inferred from the count
alone — C and D show only a smooth, expected unlit-hemisphere gradient,
with no salt-and-pepper structure anywhere in the region.)

**A and B both show the artifact; C and D both do not — regardless of
which Material the sphere uses.** The **control** material never
samples the normal map and never builds a TBN basis at all; it shows
the identical defect whenever real shadow casters are present. This
directly rules out Spec 0029's tangent-generation algorithm, the
per-fragment TBN reorthogonalization, and the normal-map texel content
as the cause — the variable that toggles the defect on and off is
`shadowCasterDrawItems`' own emptiness, i.e., whether the receiver
compares its own light-space depth against a shadow map the *same*
mesh also rendered into. This is the textbook definition of
self-shadow acne, not a tangent-space or normal-mapping defect.

The candidate's own `(256,256)` pixel — vertex 200, the sphere's own
light-facing point, `dot(N,L) > 0.6` — is unaffected in all four
captures (this pixel sits well away from the terminator), confirming
the defect is specifically a grazing-angle phenomenon, not a
uniform-brightness or base-color problem.

## Goals

- Eliminate visible self-shadow acne on a smooth, curved receiver at
  grazing angles to a directional light, specifically reproduced on
  `pbr_sphere` under the `pbr_normal_map_demo` scene's fixed camera/
  light.
- Preserve real, visible cast-shadow coverage — the sphere's own
  shadow on the ground plane, and every existing shadow-discriminative
  test's own measured delta, must not be erased or substantially
  weakened by an over-corrected bias (the "peter-panning" failure
  mode).
- Leave IBL/ambient contribution completely unaffected by the shadow
  factor — unchanged from ADR-0072 D-5's own existing multiply-only-
  the-directional-term contract.
- Apply the exact same bias policy, computed the exact same way, to
  all four PBR shader variants that call `computeShadowFactor()`:
  `pbr_direct_lit`, `pbr_ibl`, `pbr_direct_lit_normal_map`,
  `pbr_ibl_normal_map` — one shared contract, never a per-shader
  divergence.
- Fix this without moving the approved camera, light, geometry, or
  material values of any existing or in-flight demo scene (including
  Spec 0029's own `pbr_normal_map_demo`) — the defect is a shadow-
  sampling precision issue, not a scene-composition choice, and must
  not be hidden by picking new camera/light angles or a different
  sample pixel.

## Non-Goals

- Percentage-closer filtering (PCF), any soft-shadow technique,
  cascaded shadow maps, or variance/exponential shadow maps (VSM/EVSM).
- Shadow-map resolution changes (`1024×1024`, `D32Sfloat` stay fixed).
- A scene-adaptive or camera-fitted light volume — the fixed
  orthographic volume (center `(0,0,0)`, half-extent `8.0`, near `0.1`,
  far `30.0`, ADR-0072 D-6/Plan 0027 P4) is unchanged.
- Any bindless, descriptor-system, or material-system restructuring.
- Any change to Spec 0029/ADR-0073's own tangent-generation algorithm,
  thresholds, or fallback rule — already independently confirmed
  unaffected (Evidence above; the defect reproduces with no tangent
  data in play at all).
- Handling the pre-existing, separately-tracked mirrored/negative-
  determinant tangent-space handedness limitation (ADR-0074, deferred,
  unrelated to this Spec).
- Re-deriving or moving Spec 0029's own fixed discriminative pixels or
  thresholds (`(256,256)`/`>50`, `(198,273)`/`>15`) — those stay
  exactly as Plan 0029 fixed them; only the underlying shadow-bias
  mechanism they measure changes.

## Requirements

### Functional

- `computeShadowFactor()` (or its call site) computes a per-fragment
  bias as a bounded, monotonically-increasing function of the
  receiver's own **geometric** surface normal's angle to the light —
  never the perturbed, normal-mapped normal, even in the two
  normal-map shader variants. "Geometric normal" means the same
  per-vertex, cook-time mesh normal (`input.worldNormal`, normalized)
  every non-normal-map shader already uses as its only `N` — in the
  two normal-map shaders this is the existing local `N_geo`, already
  computed before the TBN blend, at the exact point
  `computeShadowFactor()` is called (see Real Shader Input
  Confirmation below).
- The bias is clamped to a fixed, named minimum and a fixed, named
  maximum — it must never grow unbounded as the angle approaches 90°
  (the terminator itself), which would otherwise let real, intended
  shadows silently detach from their own casters (peter-panning).
- Bias computation happens on the **receiver** side, inside the
  existing PBR fragment shaders' own `computeShadowFactor()`-equivalent
  logic — not on the caster side (no Vulkan rasterization
  `depthBias*` fields, no `PipelineCreateParams` change for
  `shadow_cast`'s own Pipeline).
- The out-of-bounds rule is unchanged: a fragment outside the shadow
  map's own fixed UV/depth coverage remains always fully lit
  (`shadowFactor = 1.0`), independent of the new bias formula.
- The manual depth-compare formula's own shape is unchanged
  (`(shadowNdc.z - bias) <= storedDepth ? 1.0 : 0.0`) — only `bias`
  changes from a single literal to the new bounded function's result.
- No RHI, `PipelineCreateParams`, descriptor-contract, uniform-buffer
  layout, or RenderGraph change of any kind — the fix is confined to
  each shader's own existing local computation, using data already
  available at the existing `computeShadowFactor()` call site (see
  below). `CameraUniform`'s byte layout, `lightSpaceView`/
  `lightSpaceProjection`, and every existing binding stay byte-for-byte
  unchanged.
- All four PBR shader variants that declare `computeShadowFactor()`
  today (`pbr_direct_lit.slang`, `pbr_ibl.slang`,
  `pbr_direct_lit_normal_map.slang`, `pbr_ibl_normal_map.slang`) adopt
  the identical formula, the identical bounds, and the identical
  geometric-normal input rule — verified by direct source comparison,
  not merely "should be the same."

### Real shader input confirmation (not assumed — read from the real, current source)

Confirmed by direct inspection of all four shader files (two already on
`main`, two on the unmerged `feature/0029-tangent-space-normal-mapping-foundation`
branch — both read in full for this Spec):

- In `pbr_direct_lit.slang`/`pbr_ibl.slang`, `computeShadowFactor(input.worldPosition)`
  is called inside the directional-light loop, immediately after
  `float3 L = -camera.directionalLights[i].direction;` and
  `float NdotL = max(dot(N, L), 0.0);` — both already computed as plain
  local variables, and `N` here **is** the geometric normal (these two
  shaders declare no perturbed normal at all).
- In `pbr_direct_lit_normal_map.slang`/`pbr_ibl_normal_map.slang`, the
  same call site exists at the same position in the same loop, but by
  that point `N` has already been reassigned to the **perturbed**,
  texel-sampled normal (`N = normalize(texelN.x*T + texelN.y*B + texelN.z*N_geo)`).
  The **geometric** normal is still in scope under its own name,
  `N_geo` (`float3 N_geo = normalize(input.worldNormal);`), computed a
  few lines earlier in the same function, before the TBN blend — never
  reassigned or shadowed afterward.
- In every one of the four shaders, `L` (the light direction) is
  already a local variable at the call site, computed identically in
  all four (`-camera.directionalLights[i].direction`).
- Therefore, a geometric-normal slope-aware bias needs only two already
  -in-scope local values at the call site — `N_geo` (named `N` in the
  two non-normal-map shaders) and `L` — passed as new parameters to
  `computeShadowFactor()`, or a `NdotL_geo` scalar computed from them
  immediately before the call. No new uniform field, no new binding, no
  new vertex attribute, no new `Varying` field is required in any of
  the four shaders.

### Non-functional

- **Performance:** one bounded, closed-form scalar expression (a
  handful of ALU ops: one `dot`, one `sqrt` or `acos`/`tan` pair, one
  `clamp`) per shaded fragment inside the existing directional-light
  branch — no new texture sample, no new branch depth beyond what
  `computeShadowFactor()` already has.
- **Memory:** zero. No new buffer, no new binding, no new vertex
  attribute.
- **Portability:** no RHI or Vulkan capability change; nothing here
  interacts with device feature/format support.
- **Determinism:** the bias remains a pure function of the receiving
  fragment's own geometric normal and the light's own fixed direction
  — repeated frames with unchanged inputs still produce identical
  shadowed pixels, matching ADR-0072's own existing determinism
  requirement.

## Proposed Design

Replace the single literal:

```
static const float kShadowBias = 0.0015;
...
return (shadowNdc.z - kShadowBias) <= storedDepth ? 1.0 : 0.0;
```

with a bounded, geometric-normal slope-aware bias, computed at the
existing call site and passed in:

```
// At the existing call site, inside the directional-light loop, using
// N_geo (the two normal-map shaders) or N (the two non-normal-map
// shaders, already geometric) and the already-local L:
float NdotLGeo = max(dot(N_geo, L), kMinDot);   // N_geo == N in the two non-normal-map shaders
float shadowFactor = computeShadowFactor(input.worldPosition, NdotLGeo);
```

```
// Inside computeShadowFactor(), replacing the single literal:
float slope = sqrt(max(1.0 - NdotLGeo * NdotLGeo, 0.0)) / NdotLGeo;  // tan(acos(NdotLGeo)), NdotLGeo already floored away from 0
float bias = clamp(kShadowBiasMin + kShadowBiasSlopeScale * slope, kShadowBiasMin, kShadowBiasMax);
return (shadowNdc.z - bias) <= storedDepth ? 1.0 : 0.0;
```

`kShadowBiasMin`, `kShadowBiasSlopeScale`, and `kShadowBiasMax` replace
the single `kShadowBias` literal — three fixed, named constants,
identical across all four shaders (see Numeric Values below for how
their concrete values are fixed at Plan time). `kMinDot` already exists
in all four shaders (Spec 0023's own existing constant) and is reused,
not redefined, to floor `NdotLGeo` away from exactly `0` before the
division.

This directly targets the diagnosed mechanism: `slope` grows without
bound as `NdotLGeo → 0` (the terminator), so a flat bias is provably
insufficient there (Evidence above), while `clamp(..., kShadowBiasMax)`
prevents the same growth from over-correcting into peter-panning on a
real caster/receiver pair at a merely-shallow (not near-zero) angle.

### Why the geometric normal, never the perturbed one

Using the perturbed, texel-sampled normal to size the bias would make
the bias itself noisy at texel resolution — exactly the frequency the
acne already appears at — potentially trading one high-frequency
artifact for another, correlated one. The geometric normal varies
smoothly across a triangle (or, for `pbr_sphere`, smoothly across its
own tessellation), giving a smoothly-varying bias that tracks the
*receiver mesh's own real depth-comparison slope* — the actual
quantity self-shadow acne depends on — rather than a cosmetic,
per-texel surface-detail signal that has no bearing on where the
depth-comparison actually breaks down.

### Alternatives Considered

1. **Keep the current fixed receiver-side bias, retune the single
   literal.** Rejected: the mechanism itself (Evidence above) is
   fundamentally angle-dependent — no single flat value can be both
   large enough to suppress acne at the terminator and small enough
   not to peter-pan a merely-tilted real shadow; ADR-0072 D-5 already
   anticipated this ("no slope-scaled ... bias" was an explicit
   simplification for that Spec's own first slice, not a permanent
   position).
2. **Bounded, geometric-normal slope-aware receiver bias (recommended;
   see below).**
3. **Vulkan rasterization depth bias
   (`depthBiasConstantFactor`/`depthBiasSlopeFactor`/`depthBiasClamp`
   on the `shadow_cast` Pipeline's own `VkPipelineRasterizationStateCreateInfo`).**
   Rejected: this repository's `PipelineCreateParams` (RHI) does not
   expose any depth-bias field today — checked directly against the
   real struct and `VulkanDevice::createPipeline()`'s own rasterization-
   state construction, neither declares one. Adopting this option
   requires a new, additive `PipelineCreateParams` field and a new
   `VulkanDevice::createPipeline()` code path — a real, if small,
   RHI/public-API surface change this Spec's own Goals explicitly rule
   out avoiding. It would also compute bias from the *caster* triangle's
   own light-space rasterization slope, not the *receiver* fragment's
   real shading normal — a different, real signal, less directly tied to
   the diagnosed grazing-angle mechanism (Evidence above measured the
   defect via the receiver's own `dot(N,L)`, not caster-triangle
   geometry). Left as a candidate follow-on if Plan-stage measurement
   (see Testing & Verification Plan) finds the recommended option
   insufficient — not chosen now, without that evidence.
4. **Normal-offset shadow mapping** (offset the world-space position by
   `N_geo * texelWorldSize` before the light-space transform, instead
   of biasing the compared depth). Rejected as the *primary* mechanism:
   it is real-code-implementable with the same already-in-scope
   `N_geo` (no new input either), but requires a new fixed constant
   (the shadow map's own world-space texel size, itself derivable from
   the already-fixed orthographic half-extent and resolution — `2 *
   8.0 / 1024`) and changes *which world position* is compared, not
   only the threshold — a strictly larger, harder-to-reason-about
   change for a defect the bounded slope-aware bias already explains
   and, pending Plan-stage measurement, is expected to resolve on its
   own. Not ruled out permanently; not needed unless Plan-stage
   measurement shows the recommended option's own `kShadowBiasMax`
   cannot simultaneously suppress the acne and preserve the ground
   shadow (see Testing & Verification Plan's own stop condition).

### Recommendation

**Option 2 — bounded, geometric-normal slope-aware receiver bias** is
this Spec's sole recommendation, confirmed implementable against the
real, current shader source (all four variants) with zero RHI,
`PipelineCreateParams`, descriptor, uniform-buffer, or RenderGraph
change. It directly targets the diagnosed mechanism, uses only data
already local to the existing call site, and keeps one shared formula
and one shared set of named constants across all four PBR shader
variants — satisfying every constraint this Spec's own Goals state.

## Architectural Impact

**None requiring a new module boundary, public API, dependency,
threading model, or backend-abstraction contract change.** This is a
shader-internal algorithm change to an already-`Accepted`,
already-shipped ADR-0072 shadow-sampling contract — the *bias policy*
D-5 named as a Plan-stage value is being replaced, not the mechanism
D-5 itself records (manual comparison, `Sampler2D`, out-of-bounds rule,
multiply-only-the-directional-term). Per ADR-0072's own D-5 text ("a
single, fixed, named literal ... Plan-stage value"), this is exactly
the kind of change that Decision already anticipated living outside a
fixed number — but because `kShadowBias` is a **Decision-level detail
of an `Accepted` ADR**, not a Plan-only value never recorded in the
ADR itself, this Spec's own Architectural Impact is: **ADR-0072 gains
a Proposed Amendment** (see that ADR's own Amendment section) recording
the new bias policy, its normal-space/normal-type choice, and its
uniform application to all four shader variants — the same class of
change ADR-0072's own 2026-09-06 Accepted Amendment (Spec 0029's
5-binding widening) already used this mechanism for. No new ADR is
opened; this amends ADR-0072, not ADR-0074 — see the "Why ADR-0072, not
ADR-0074" note in that ADR's own Amendment.

## Testing & Verification Plan

All of the following are **Plan-stage requirements this Spec fixes now,
for Plan 0030 to carry out** — none may be silently narrowed,
skipped, or replaced with "tune until it looks good" at Implementation
time.

- **A real-GPU curved-receiver acne discriminator**, run against the
  existing `pbr_normal_map_demo` fixture/scene (no new scene, no camera/
  light change): captures the sphere region (`x∈[205,305], y∈[195,295]`,
  the same bounding box Evidence above used) with real shadow casters
  enabled, counts pixels below a fixed `rgbSum` threshold within that
  region, and asserts the count drops from the diagnosed baseline
  (**1018** for the normal-mapped material, **1176** for the control
  material, both at threshold `rgbSum < 100`) to a Plan-fixed, much
  lower ceiling. This is a **quantitative pixel-count assertion**, not
  a visual/manual check — Plan 0030 fixes the exact ceiling value
  (derived from a real post-fix capture, following the measurement
  method below), never "looks better."
- **Shadow-on/shadow-off differential**, reusing the exact same
  `includeShadowCasters` toggle the `pbr_normal_map_demo` fixture
  already exposes — the acne discriminator above must be run both ways
  and show the sphere-region dark-pixel count collapse to the same
  low ceiling in the shadow-off capture as the shadow-on capture (today
  it does not need to, since shadow-off already has no acne by
  construction) *and* the shadow-on capture must independently show a
  genuinely occluded ground pixel darker than its own shadow-off
  counterpart (see next item) — proving the fix suppresses acne without
  silently disabling real shadowing.
- **Independent ground cast-shadow positive discriminator** — reuses
  Spec 0029's own existing `(198,273)` shadow pixel and `>15` `rgbSum`
  delta threshold (`pbr_normal_map_demo_gpu_tests.cpp`'s own existing
  `TEST_CASE`, unmodified) as the peter-panning guard: if the new
  bounded bias's own `kShadowBiasMax` is too large, this delta shrinks
  or vanishes. Plan 0030 re-runs this exact, already-existing test
  after the fix and must observe the delta stay comfortably above `15`
  — a real measurement, not an assumption that a bias fix cannot affect
  it.
- **IBL isolation control** — reuses ADR-0072 D-7's own established
  three-render pattern (shadowed vs. unshadowed-control vs. light-off
  IBL/ambient reference at the same sample point, `shadow_gpu_tests.cpp`'s
  own existing precedent) to confirm the new bias formula still leaves
  IBL/ambient contribution untouched — the multiply-only-the-
  directional-term contract (ADR-0072 D-5) is unchanged by this Spec,
  and this test proves it stays that way after the fix.
- **All four shader-path coverage** — the acne discriminator and the
  ground cast-shadow discriminator both run against `pbr_direct_lit`
  (no environment) and `pbr_ibl`/`pbr_direct_lit_normal_map`/
  `pbr_ibl_normal_map` (IBL-enabled paths, `pbr_normal_map_demo`'s own
  scene already exercises the two normal-map variants) — Plan 0030
  must show the identical formula/constants produce comparable acne
  suppression on all four, not only the two the current candidate
  scene happens to exercise. Where an existing fixture/scene does not
  already exercise a given variant under a grazing-angle receiver
  (e.g., `pbr_direct_lit` with no environment), Plan 0030 identifies
  the existing scene/fixture that does (real code, not a new one) or
  states plainly that no existing curved/grazing-angle receiver
  exercises that specific variant and confirms via direct code
  inspection that the shared formula is textually identical across all
  four shaders instead.
- **Vulkan Validation Layers clean** — zero `VUID`/Validation
  Error/Warning across the full verbose real-GPU test output, Debug and
  Release, matching every prior Spec's own gate.
- **Compare-first against every existing golden** — Plan 0030 runs the
  full, unmodified capture-compare suite (all 9 goldens currently
  committed to `main`, plus `world_scene_loaded`/`sky_background`
  individually) before requesting any re-capture. A byte-identical
  result needs no action. **Only a golden that shows a real, non-zero
  difference under the new bias formula is queued for a human-reviewed
  re-capture** (ADR-0042's own established process) — never presumed
  in advance, and never re-captured "just in case." Given the bias
  formula only changes fragments inside the directional-light branch
  at grazing incidence, most existing goldens (flat/axis-aligned
  geometry, or geometry never near the terminator at its own fixed
  camera/light) are expected to stay byte-identical, but this
  expectation is explicitly **not** a substitute for the real
  comparison.
- **Spec 0029's own candidate is never approved as-is.** The current,
  untracked `pbr_normal_map_demo` golden candidate (generated before
  this fix) is not committed under any circumstance — after Plan 0030
  implements the fix, Spec 0029's own Milestone 5 candidate-generation
  step is re-run in full against the fixed shaders, producing a **new**
  candidate PNG/sidecar, which itself goes through its own, fresh,
  independent Human Review before Spec 0029 Milestone 6 (golden commit)
  may proceed. The old candidate's bytes are discarded, never reused or
  diffed against as if it were a baseline.

### Numeric-value measurement method (Plan-stage; fixed procedure now, not the numbers themselves)

`kShadowBiasMin`, `kShadowBiasSlopeScale`, `kShadowBiasMax`, and the
acne discriminator's own dark-pixel-count ceiling are **not** fixed by
this Spec — but the method to fix them is, so Plan 0030 cannot devolve
into "tune until it looks good":

1. Start from the existing `kShadowBias = 0.0015`'s own derivation
   (Plan 0027 P4: ~4.5cm world-space slack over the fixed orthographic
   volume's `29.9`-unit near-far range) as `kShadowBiasMin`'s own
   starting point — the flat bias already proven adequate at
   near-perpendicular incidence (Evidence above: `(256,256)`,
   `dot(N,L)>0.6`, shows no acne in any of the four captures) should
   not regress there.
2. Using the real `pbr_normal_map_demo` fixture (already committed on
   the Spec 0029 feature branch; no new scene), sweep
   `kShadowBiasSlopeScale` and `kShadowBiasMax` against real captures,
   re-running the acne discriminator (sphere-region dark-pixel count)
   after each real GPU render — never a predicted/simulated value.
3. **Stop condition (both must hold simultaneously on the same real
   capture, not two separate runs):** the acne discriminator's
   sphere-region dark-pixel count falls to single digits or zero
   *and* the ground cast-shadow discriminator's own `(198,273)` delta
   stays `>15` (its own already-approved Spec 0029 threshold,
   unmodified). The first parameter combination satisfying both,
   found by real measurement, is what Plan 0030 fixes as the final
   value — not the first one that merely suppresses the acne, and not
   a value chosen before both are actually checked together.
4. If no `(kShadowBiasSlopeScale, kShadowBiasMax)` pair satisfies both
   conditions simultaneously, Plan 0030 stops and reports this
   directly — it does not silently loosen the acne ceiling, weaken the
   `>15` shadow threshold, or fall back to Alternative 3/4 above
   without first returning to Human Review with the real, measured
   evidence for why the recommended option was insufficient.
5. The acne discriminator's own dark-pixel-count ceiling (the ~1018/
   ~1176-to-"single digits" target above) is fixed as the exact integer
   observed in the real capture that satisfies step 3, not a
   round number chosen in advance.

## Risks & Open Questions

1. **The `sqrt`/division-based slope formula has a removable
   singularity as `NdotLGeo → 0`** (a fragment exactly at the
   terminator). `kMinDot` (already used elsewhere in all four shaders)
   floors `NdotLGeo` away from exactly zero before the division;
   combined with the outer `clamp(..., kShadowBiasMax)`, the bias stays
   finite and bounded even at the theoretical limit. Plan 0030 confirms
   this numerically (no `NaN`/`Inf` in a real capture) as part of its
   own Validation Layers/correctness pass — a shader producing `NaN`
   would itself likely surface as a Validation Layers or visibly broken
   pixel, not a silent pass.
2. **Whether the recommended option alone is sufficient is not
   guaranteed until Plan-stage real measurement** — this Spec's own
   Evidence and code-level analysis support it strongly (the mechanism
   match is direct: bounded slope-scaling targets exactly the
   `NdotL→0` region Evidence isolated), but the Testing & Verification
   Plan's own stop condition (both acne suppression and shadow-delta
   preservation on the same real capture) is the actual gate, not this
   Spec's own confidence.
3. **Existing-golden drift is possible but not yet known** — any
   existing scene with a curved receiver near grazing incidence to its
   own directional light (if any exist beyond `pbr_normal_map_demo`,
   not yet cross-checked exhaustively here) could show a small, real
   pixel change once the bias formula changes. Compare-first (Testing &
   Verification Plan) is the only source of truth for this, not this
   Spec's own prediction.

## Out of Scope / Future Work

PCF/soft shadows, cascaded shadow maps, VSM/EVSM, shadow-map resolution
changes, and scene-adaptive light volumes remain future, separately-
scoped work, unaffected and unblocked by this Spec either way. Vulkan
rasterization depth bias and normal-offset shadow mapping (Alternatives
3/4) remain available as a follow-on only if Plan-stage measurement
shows the recommended option insufficient — see the stop condition
above.
