# Spec: Directional Shadow Bias Stability

- **Status:** Approved
- **Author:** slmao
- **Created:** 2026-09-07
- **Related Plan(s):** None yet — Plan 0030 is drafted only once
  [PR #133](https://github.com/slmao/Atlantis/pull/133) merges to
  `main` (see Human Review Approval below).
- **Related ADR(s):** [ADR-0072](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md)
  (`Accepted`; gains a 2026-09-07 Accepted Amendment alongside this
  Spec — see that ADR's own Amendment section).

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

**This raw `rgbSum < 100` count is diagnostic evidence only — it is
not, and cannot be, this Spec's own acceptance metric.** C (no shadow
casters at all) and D (no shadow casters, control material) still show
**112** and **729** pixels respectively below that same threshold —
ordinary, expected unlit-hemisphere shading has nothing to do with
self-shadow acne, but a raw count does not distinguish the two. A
requirement to drive the *raw* count to "single digits or zero" is
therefore incoherent — it could never be satisfied even by a perfect
fix, since D's own 729 baseline-shading pixels alone exceed it with no
acne present at all. Testing & Verification Plan below replaces this
raw count with a paired shadow-on/shadow-off differential, computed
only over pixels confirmed to belong to the sphere's own surface (see
Sphere Coverage Mask below), which isolates the *acne itself* from
ordinary shading.

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

## Dependency and Integration Sequencing

This section fixes **how** Plan 0030/Implementation reaches `main`,
given a real, unavoidable dependency this Spec does not get to choose
around: **two of the four shader variants this Spec requires do not
exist on `main` today.**

**The dependency, stated plainly:** `pbr_direct_lit.slang` and
`pbr_ibl.slang` are on `main`. `pbr_direct_lit_normal_map.slang` and
`pbr_ibl_normal_map.slang` exist only inside Spec 0029's own
Milestone 1–5 commits, on the unmerged
`feature/0029-tangent-space-normal-mapping-foundation` branch. This
Spec's own Testing & Verification Plan requires real-GPU measurement
against **all four** variants (no source-inspection exemption — see
above) — an Implementation branch cut from plain `main` cannot satisfy
that requirement, because two of the four files it needs to edit and
measure do not exist there yet.

**This is a delivery-sequencing problem, not an architecture
question.** It does not change the slope-aware, geometric-normal,
receiver-side bias formula, `Material`, RHI, or any other design
content of this Spec or its ADR-0072 Amendment. The sequence below is
the one, fixed answer — Plan 0030 implements it as written; it is not
Plan 0030's own choice to make a different branching decision.

1. This PR (#133 — Spec 0030 + the ADR-0072 Proposed Amendment) goes
   through Human Review and merges to `main` on its own, exactly like
   every other Spec/ADR pair in this repository's history.
2. Plan 0030 is drafted, reviewed, and merged to `main` next — against
   whatever `main` is current at that time (which does **not** yet
   include Spec 0029's own Milestone 1–5 shader files; Plan 0030's own
   text acknowledges this and does not assume otherwise).
3. **Implementation is not branched directly from plain `main`.**
4. A new branch, `feature/0030-directional-shadow-bias-stability`, is
   created with `feature/0029-tangent-space-normal-mapping-foundation`'s
   own completed Milestone 1–5 HEAD as its starting content — not a
   fresh checkout of `main` — and then brought up to date with the
   latest `main` via an ordinary `merge` (never `rebase`, never
   `force-push`), mirroring the same safe-sync pattern already used
   earlier in this same Spec 0029 effort (`git fetch` +
   fast-forward/merge, stash only for genuinely uncommitted work).
5. This one branch therefore carries, together: Spec 0029's own
   Milestone 1–5 source (including both normal-map shaders), the
   Spec/Plan 0030 governance documents once merged to `main`, and —
   critically — all four PBR shader variants this Spec's own bias fix
   must edit and measure, in one place.
6. Plan 0030's own bias-formula change is implemented on **this**
   combined branch, where all four shaders genuinely exist — the
   real-GPU four-shader-path coverage this Spec's own Testing &
   Verification Plan requires is carried out here, not deferred or
   partially substituted.
7. Spec 0029's own existing, untracked `pbr_normal_map_demo` candidate
   (generated before this fix, already found to show acne) is
   discarded outright — never approved, never committed, never reused
   as a baseline. Once the bias fix lands on the combined branch, Spec
   0029's own Milestone 5 candidate-generation step is re-run in full,
   producing a fresh candidate PNG/sidecar, which goes through its own,
   independent Human Review.
8. Only after that fresh candidate is approved does work continue:
   Spec 0029's own Milestone 6 (golden commit) and Milestone 7 (final
   verification/registry closeout), and Spec 0030's own final
   verification (all four shader paths, compare-first against every
   existing golden, Validation Layers clean) — both completed on the
   same combined branch.
9. **Exactly one Implementation PR is opened, base `main`, from the
   combined branch** — never two separate PRs waiting on each other.
   Its own description links Spec/Plan 0029, Spec/Plan 0030, the
   ADR-0072 Accepted Amendment (once accepted), and both Specs' own
   complete verification results, including the new golden's own
   Human Review approval.
10. `feature/0029-tangent-space-normal-mapping-foundation` itself is
    never separately merged and never deleted or rewritten — it
    remains, permanently, the real historical record of Milestone 1–5
    exactly as they were implemented and reviewed; the combined branch
    above starts from its content but is a distinct branch with its
    own distinct history from that point forward.

**Explicitly rejected alternatives** (each already considered and
rejected — Plan 0030 does not re-litigate this choice):

- **Merge Spec 0029 first, on its own, to unblock Spec 0030.** Rejected
  — this would require either approving the known-defective candidate
  (never acceptable) or leaving Spec 0029 merged with Milestone 6–7
  incomplete and no valid golden, which this repository's own
  Milestone-atomicity discipline does not permit.
- **Fix only the two `main`-resident shaders now, defer the two
  normal-map twins to "whenever Spec 0029 eventually merges."**
  Rejected — this Spec's own Goals require one shared bias contract
  across all four variants verified together, not a partial fix that
  leaves two shipped shaders silently unaudited against the same
  defect class this Spec exists to close.
- **A stacked PR, basing Spec 0030's own Implementation branch on
  Spec 0029's still-open feature branch/PR instead of `main`.**
  Rejected — this repository's own git workflow bases every PR on
  `main`; a PR based on another unmerged branch is exactly the
  "two Implementation PRs waiting on each other" shape this sequencing
  exists to avoid, and complicates review, CI, and merge order for no
  benefit the combined-branch approach above does not already provide.

## Testing & Verification Plan

All of the following are **Plan-stage requirements this Spec fixes now,
for Plan 0030 to carry out** — none may be silently narrowed,
skipped, or replaced with "tune until it looks good" at Implementation
time.

- **A real-GPU curved-receiver acne discriminator, computed as a paired
  shadow-on/shadow-off differential over a sphere-only pixel mask** —
  see Sphere Coverage Mask and Paired Acne Metric below for the full,
  non-circular definition. This is a **quantitative, per-pixel
  differential assertion**, not a visual/manual check and not a raw
  dark-pixel count (Motivation's own caveat above) — Plan 0030 fixes
  the exact acne ceiling, noise threshold, and baseline **before**
  sweeping any bias parameter (see Numeric-Value Measurement Method
  below), never "looks better," and never a ceiling read back off
  whichever capture happens to be chosen last.

#### Sphere Coverage Mask

To isolate "pixels that are part of the sphere's own surface" from
ground and background pixels (neither of which this Spec's own acne
metric may ever count), Plan 0030 adds one **test-only** capture mode,
never a product API, scene asset, or committed image:

- The exact same `pbr_normal_map_demo` fixture, camera, projection, and
  the sphere's own real `DrawItem` (identical `Mesh`/objectToWorld to
  every other capture in this Spec) are reused unchanged.
- A separate render draws **only** the sphere `DrawItem`, using this
  fixture's own already-existing `unlitTexturedVertexInputLayout`/
  solid-white-material path (the same kind of trivial, already-proven
  unlit draw every other fixture in this codebase already uses for a
  reference/control render — no new shader, no new Pipeline shape) —
  against a **black clear color**, with no sky Pipeline bound and no
  ground-plane `DrawItem` in the list.
- Every pixel in the resulting capture that is non-black (any channel
  `>0`) is, by construction, a sphere-covered pixel; every pixel that
  is exactly black is not. This mask is computed **once** per fixture
  configuration and reused for every paired capture below — it does
  not itself depend on the shadow-bias fix, since it never binds the
  shadow map or samples `computeShadowFactor()` at all.
- The mask capture exists solely to drive this test's own pixel
  selection; it is never encoded to a file, never compared against a
  golden, and never reused outside this one Spec's own verification
  scope.

#### Paired Acne Metric

For a given shader path and a given bias configuration (pre-fix or a
specific post-fix parameter candidate):

1. Capture **shadow-on** (`includeShadowCasters = true`) and
   **shadow-off** (`includeShadowCasters = false`) renders of the exact
   same scene/camera/light/material — the fixture's own existing
   `includeShadowCasters` toggle, no new parameter.
2. For every pixel the Sphere Coverage Mask marks as sphere-covered
   (ground and background pixels are never included):
   `shadowDarkening = rgbSum(shadowOff) - rgbSum(shadowOn)`.
3. A pixel counts as a **suspected self-shadow-acne pixel** only when
   `shadowDarkening` exceeds a fixed, Plan-determined noise threshold —
   ordinary shading is identical between the two captures except where
   the shadow term itself fires, so a genuinely shadow-driven pixel
   shows a real, large `shadowDarkening`; acne shows the same signature
   at scattered, isolated points across the terminator; residual
   sub-threshold `shadowDarkening` (rounding, texture-filtering noise)
   is excluded by the noise threshold, never counted.
4. **Acne pixel count** for that (shader, configuration) pair is the
   number of suspected self-shadow-acne pixels from step 3. This is the
   single, non-circular quantity every ceiling/threshold below refers
   to — never the raw `rgbSum < 100` count from Motivation's own
   diagnostic table.
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
- **All four shader-path coverage — real GPU execution required, no
  exemption.** The paired acne metric (above) and the ground
  cast-shadow discriminator are both actually **run on real GPU
  hardware** against all four PBR shader variants —
  `pbr_direct_lit`, `pbr_ibl`, `pbr_direct_lit_normal_map`, and
  `pbr_ibl_normal_map` — each using the same curved receiver
  (`pbr_sphere`), the same camera, the same directional light, the same
  shadow-on/shadow-off paired capture, and the same Sphere Coverage
  Mask methodology. Plan 0030 reports a pre-fix and a post-fix acne
  pixel count for **every one of the four**, not only the two
  `pbr_normal_map_demo` already exercises. Plan 0030 may reuse or
  parameterize the existing `pbr_normal_map_demo`/`pbr_material_demo`/
  `ibl_material_demo` fixtures to reach `pbr_direct_lit` (no
  environment) and `pbr_ibl` (environment, no normal map) under a
  curved receiver — no new scene asset is required — but the coverage
  itself is **behavioral, not textual**: source-level identity of the
  four shaders' own bias formula/constants (see below) is a
  **supplementary** check only, and never substitutes for actually
  executing all four paths on real hardware and observing their own
  real acne pixel counts.
- **Four-shader formula/constants text-identity check** — in addition
  to (never instead of) the real-GPU coverage above, Plan 0030 confirms
  by direct source comparison that all four shaders declare the
  identical `kShadowBiasMin`/`kShadowBiasSlopeScale`/`kShadowBiasMax`
  constants and the identical bias-computation expression — the same
  "exact twin" textual-identity discipline ADR-0074 already established
  for Spec 0029's own shader pairs.
- **GPU-independent bias-formula finiteness test** — see Risks & Open
  Questions item 1 for the full rationale: a plain-C++ mirror of the
  `slope`/`bias` expression, evaluated across the full approved
  `NdotLGeo` range and every candidate constant, asserting every result
  is finite and within `[kShadowBiasMin, kShadowBiasMax]`. This is the
  test responsible for numeric finiteness — Vulkan Validation Layers
  (next bullet) are not relied on for it.
- **Vulkan Validation Layers clean** — zero `VUID`/Validation
  Error/Warning across the full verbose real-GPU test output, Debug and
  Release, matching every prior Spec's own gate. This is an API-
  correctness gate (invalid handles, descriptor mismatches,
  synchronization errors) — it is not, and is not claimed to be, a
  shader floating-point finiteness verifier.
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

`kShadowBiasMin`, `kShadowBiasSlopeScale`, and `kShadowBiasMax` are
**not** fixed by this Spec — but every value the acceptance rule
depends on (the noise threshold, the pre-fix baseline, the acne
ceiling, and the parameter search itself) must be **fixed in Plan 0030
before that Plan's own Human Review**, in the order below, so no value
is ever defined in terms of whichever capture Implementation happens to
land on. Plan 0030 may use a temporary, uncommitted real-GPU probe
(mirroring this codebase's own established "temporary, uncommitted
tool" precedent, e.g. Spec 0029's own `stb_image_write` helper) to
derive these values — the probe itself is never committed; the values
it produces are written into Plan 0030's own document.

**Step 1 — fix the measurement apparatus (before touching any bias
value):**

1. The Sphere Coverage Mask's own exact construction (fixture/material/
   clear-color/DrawItem list, as specified above) is implemented and
   its own output visually confirmed to match the sphere's own real
   on-screen silhouette, for each of the four shader-path scenes Plan
   0030 uses.
2. The `shadowDarkening` noise threshold is fixed from a real,
   pre-fix capture: measure `shadowDarkening` across every sphere-
   masked pixel in the existing (unfixed) `pbr_normal_map_demo`
   fixture, identify the value that separates the terminator's own
   acne cluster from ordinary sub-threshold shading noise elsewhere on
   the lit hemisphere, and fix that value as the noise threshold —
   a real measurement, not an assumed round number.

**Step 2 — fix the pre-fix baseline and the acne ceiling (before
sweeping any bias parameter):**

3. Using the noise threshold from Step 1 and the current, unfixed
   shaders (`kShadowBias = 0.0015`), measure the **pre-fix acne pixel
   count** (Paired Acne Metric above) for each of the four shader
   paths, on real GPU hardware. These four integers are the fixed
   **baseline** — recorded in Plan 0030 verbatim, never re-measured
   or replaced once a bias fix exists.
4. Fix the **acne ceiling** as a rule computed from that baseline, not
   as a number read off any post-fix capture:
   `ceiling = min(10, floor(baseline * 0.01))` per shader path — i.e.,
   **at least a 99% reduction from that path's own pre-fix baseline,
   and never more than 10 pixels in absolute terms.** This ceiling is
   fixed in Plan 0030's own document *before* Step 3's own parameter
   sweep begins — it does not change based on which parameter
   candidate is later found to satisfy it.
5. Fix the ground cast-shadow preservation threshold as Spec 0029's own
   existing, already-approved `(198,273)`/`>15` `rgbSum` delta,
   unmodified — not re-derived.

**Step 3 — deterministic parameter sweep:**

6. Fix a finite, ordered candidate set for `(kShadowBiasSlopeScale,
   kShadowBiasMax)` and a fixed `kShadowBiasMin` starting point (from
   the existing `kShadowBias = 0.0015`'s own Plan 0027 P4 derivation,
   ~4.5cm world-space slack — the flat bias already proven adequate at
   near-perpendicular incidence, Evidence above, should not regress
   there) — the candidate values, the step size, and the deterministic
   scan order (e.g., ascending `kShadowBiasSlopeScale` at each fixed
   `kShadowBiasMax`) are all written into Plan 0030 **before** any
   candidate is captured, not chosen ad hoc during the sweep.
7. For each candidate, in the fixed scan order, run the Paired Acne
   Metric and the ground cast-shadow discriminator, both against real
   GPU captures, for all four shader paths.
8. **Acceptance rule (all three conditions, on the same real capture,
   for every one of the four shader paths):**
   - post-fix acne pixel count is at least a 99% reduction from that
     path's own Step 3 baseline, **and**
   - post-fix acne pixel count does not exceed 10, **and**
   - the `(198,273)` ground-shadow `rgbSum` delta stays `> 15`.

   The first candidate in the fixed scan order satisfying all three,
   for all four shader paths, is what Plan 0030 fixes as the final
   `kShadowBiasSlopeScale`/`kShadowBiasMax` value.
9. **If no candidate in the fixed set satisfies all three conditions
   for all four shader paths, Plan 0030 stops and reports this
   directly with the real, measured evidence** — it does not loosen the
   acne ceiling, the noise threshold, or the `>15` ground-shadow
   threshold after the fact, does not silently narrow the candidate
   set to find a passing value, and does not fall back to Alternative
   3/4 without first returning to Human Review.

## Risks & Open Questions

1. **The `sqrt`/division-based slope formula has a removable
   singularity as `NdotLGeo → 0`** (a fragment exactly at the
   terminator). `kMinDot` (already used elsewhere in all four shaders)
   floors `NdotLGeo` away from exactly zero before the division;
   combined with the outer `clamp(..., kShadowBiasMax)`, the bias stays
   finite and bounded even at the theoretical limit. **Vulkan
   Validation Layers do not reliably detect ordinary shader
   floating-point `NaN`/`Inf`** — they are an API-correctness gate
   (invalid handles, descriptor mismatches, synchronization errors),
   not a shader-arithmetic verifier, and this Spec does not claim
   otherwise. Instead, Plan 0030 adds a **GPU-independent numeric
   test** that evaluates the exact `slope`/`bias` expression above in
   plain C++ (mirroring this codebase's own existing GPU-independent
   coverage for other closed-form shader-mirrored formulas) across the
   full approved `NdotLGeo` input range (down to `kMinDot`) and every
   candidate constant from the Numeric-Value Measurement Method's own
   fixed sweep, and asserts every result is finite and lies within
   `[kShadowBiasMin, kShadowBiasMax]`. Real-GPU captures (the Paired
   Acne Metric and ground cast-shadow discriminator above) separately
   verify the *rendered, observable behavior* this formula produces;
   Validation Layers continue to gate real Vulkan API correctness on
   every real-GPU test, as they already do for every other Spec in this
   sequence, but are not relied on for numeric finiteness.
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

## Human Review Approval — 2026-09-07

**Status:** Approved by Human Review, 2026-09-07, against
[PR #133](https://github.com/slmao/Atlantis/pull/133). User's own
verbatim approval text: *"I approve Spec 0030 and ADR-0072's 2026-09-07
Proposed Amendment."* This is a separate, individually-granted approval
alongside the [ADR-0072 2026-09-07 Amendment's own Acceptance
Record](../adr/0072-directional-shadow-map-resource-pass-and-pbr-integration.md#accepted-amendment--2026-09-07)
— neither approval implies the other; both were granted together in
this one review round.

**Approved scope**, as drafted, with no change to any Requirement,
Proposed Design, or Testing & Verification Plan text above:

- The bounded, geometric-normal, slope-aware receiver-side shadow bias
  (Proposed Design, "Option 2") — `kShadowBiasMin`/
  `kShadowBiasSlopeScale`/`kShadowBiasMax` replacing the single
  `kShadowBias` literal, with the three concrete constant values left
  to Plan 0030's own fixed, real-GPU probe (Numeric-value measurement
  method above) — not fixed by this Spec itself.
- No RHI, `PipelineCreateParams`, descriptor-contract, uniform-buffer
  layout, or RenderGraph change — the fix stays confined to each
  shader's own existing local computation at the existing
  `computeShadowFactor()` call site.
- The paired shadow-on/shadow-off differential acne metric, computed
  only over pixels the test-only Sphere Coverage Mask confirms belong
  to the sphere, and the non-circular ceiling procedure (fixed noise
  threshold, fixed pre-fix baseline, and a `≥99%`-reduction/`≤10`-pixel
  ceiling rule fixed *before* any parameter sweep) — never a raw
  dark-pixel count, never a ceiling read back off a chosen candidate.
- Real-GPU execution, with no text-inspection substitute, of the paired
  acne metric and the ground cast-shadow discriminator against **all
  four** PBR shader variants (`pbr_direct_lit`, `pbr_ibl`,
  `pbr_direct_lit_normal_map`, `pbr_ibl_normal_map`) — source-level
  formula/constant identity across the four remains a supplementary
  check only.
- The existing Spec 0029 ground-shadow discriminator
  (`(198,273)`/`>15`), the IBL-isolation control, and the
  always-fully-lit out-of-bounds rule staying unchanged and re-verified
  after the fix, plus the GPU-independent numeric finiteness test (not
  a claim that Vulkan Validation Layers detect shader `NaN`/`Inf`).
- Compare-first against every existing golden, with a human-reviewed
  re-capture requested only for a golden that actually differs.
- Spec 0029's own existing, untracked `pbr_normal_map_demo` candidate
  is discarded outright, never approved or reused as a baseline; a
  fresh candidate is generated only after Plan 0030's fix lands, and
  goes through its own, independent Human Review.
- The Dependency and Integration Sequencing section's fixed 10-step
  sequence — a new `feature/0030-directional-shadow-bias-stability`
  branch starting from `feature/0029-...`'s own completed Milestone
  1–5 HEAD, merged forward with `main`, carrying both Specs'
  Implementation and verification, closed by **exactly one** combined
  Implementation PR (base `main`) — and its three explicitly-rejected
  alternatives.

**This approval authorizes drafting Plan 0030 only, once [PR
#133](https://github.com/slmao/Atlantis/pull/133) itself has merged to
`main` — not before, and not any Implementation, bias-constant
selection, candidate regeneration, or golden submission.** Plan 0030's
own real-GPU parameter sweep, the fresh `pbr_normal_map_demo` candidate,
and Spec 0029's own Milestone 6–7 all remain separately gated behind
Plan 0030's own future Human Review, per the Dependency and Integration
Sequencing section above.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
Approval recorded 2026-09-07, accepting this Spec in full, as drafted,
with no change.

## Human Review Deferral Record — 2026-09-07

**Status:** Recorded by Human Review, 2026-09-07 (chat). This record
does not change this Spec's own top-level `Status: Approved` above, and
does not rewrite the Human Review Approval section immediately
preceding it — this Spec's own problem statement, Requirements, and
Proposed Design remain exactly as approved. This record documents two
subsequent, real findings and one explicit deferral decision.

**Finding 1 — the approved slope-aware receiver-depth strategy is not
implementable.** A real-GPU grazing-receiver/true-occluder discriminator
(built after this Spec's own approval, per its own Testing &
Verification Plan) found that the approved candidate
(`kShadowBiasMin=0.0015`, `kShadowBiasSlopeScale=0.003`,
`kShadowBiasMax=0.040`) **completely erases a real, physically valid
nearby occluder's own shadow**: a controlled capture at a fixed grazing
receiver, with a real occluder whose own light-space depth separation
from the receiver was measured at `0.019158` (comfortably between
`kShadowBiasMin` and the candidate's own `kShadowBiasMax`), showed the
occluder's own shadow at three sampled points (interior, near boundary,
far boundary) go from a real, deep shadow pre-fix
(`rgbSum` delta `183`/`195`/`189`) to **zero** difference under the
candidate (`0`/`0`/`0`) — the receiver never distinguishes "occluded"
from "unoccluded" once the bias needed to suppress the sphere's own
grazing-angle acne (empirically `~0.038`–`0.040`) is applied. This is a
structural conflict, not a bad candidate choice: the two requirements
(acne suppression, real-occlusion preservation for this occluder) do
not overlap anywhere in the candidate's own fixed 24-point grid.

**Finding 2 — the two named alternatives, and their combination, were
also investigated and also fail.** A follow-up, isolated real-GPU
investigation tested this Spec's own Alternatives Considered options 3
and 4, plus their combination, against the identical occlusion-guard
geometry and the existing four-path acne/threshold/ground gates:

- **Caster-side Vulkan raster depth bias** (a temporary,
  backend-neutral `PipelineCreateParams` depth-bias extension, mapped
  directly to `depthBiasEnable`/`depthBiasConstantFactor`/
  `depthBiasSlopeFactor`/`depthBiasClamp` on the `shadow_cast` Pipeline
  only, receiver compare left at the original small
  `kShadowBias=0.0015`): occlusion-safe only up to roughly
  `constantFactor<=10` (100% shadow retention at all 3 sampled points),
  but acne suppression to within ceiling requires `constantFactor~600`
  (confirmed: `2`/`1` residual pixels, ceiling `10`) — by
  `constantFactor=15` the occluder's own interior sample point is
  already fully erased while acne is still only ~34% reduced. No
  overlap (~60x gap).
- **Geometric-normal world-position offset** (an isotropic
  simplification of Filament's own real `computeLightSpacePosition()`,
  [`shaders/src/surface_shadowing.glsl`](https://github.com/google/filament/blob/main/shaders/src/surface_shadowing.glsl):
  `offset = texelWorldSize * normalBiasScale * sin(theta)` along the
  geometric normal, receiver compare left at the original small
  `kShadowBias=0.0015`): occlusion-safe only up to
  `normalBiasScale<=0.28` (acne barely reduced, ~16%), while acne
  reaches ceiling only at `normalBiasScale~15` (confirmed: `9`/`9`
  residual) — by which point all 3 occlusion sample points are already
  fully erased. No overlap (~50x gap); pushing further
  (`normalBiasScale=40`) makes acne *worse* again (non-monotonic).
- **Both combined**, each held at its own individually-safe limit
  (`constantFactor=10`/`slopeFactor=2.5` plus `normalBiasScale=0.28`):
  still only ~37% acne reduction, and *two* of the three occlusion
  sample points already lost — worse than either mechanism alone at the
  same individual settings, not better.

No Proposed Correction was drafted from this investigation — per its own
governing instructions, a Correction is only drafted once a single
option (or a combination) passes every fixed gate, which did not happen.
All temporary probe code (a `PipelineCreateParams` RHI extension, its
Vulkan Backend mapping, shader edits, GPU test files) was fully
reverted and removed; no code from this investigation was ever
committed anywhere.

**Explicit deferral decision.** The user directed (chat, 2026-09-07):
defer all shadow-bias remediation work — do not implement any new
shadow bias, PCF, normal-offset, or raster-depth-bias mechanism at this
time — and continue Spec 0029 to completion, accepting its existing,
already-generated `pbr_normal_map_demo` candidate as Spec 0029's own
initial golden, with the candidate's own visible grazing-angle acne
disclosed and accepted as a pre-existing, shared limitation of Spec
0027/ADR-0072's own fixed `kShadowBias` mechanism (confirmed, via the
same A/B/C/D control-material isolation this Spec's own Evidence
already used, to be unrelated to Spec 0029's own tangent-space/normal-
map work) — see
[Plan 0029's own Human-Approved Implementation Deviation — 2026-09-07](../plans/0029-tangent-space-normal-mapping-foundation.md#human-approved-implementation-deviation--2026-09-07)
for the complete record of that deviation.

**Consequences for this Spec.**

- **Implementation of this Spec remains deferred** — no candidate, no
  RHI change, no shader change from this investigation is adopted.
  Neither the originally-approved slope-aware receiver-depth strategy
  nor either named alternative is implemented.
- **This Spec no longer blocks Spec 0029's own Milestone 6–7** — Spec
  0029 proceeds using its own existing candidate, per the explicit
  deferral above, independently of this Spec's own eventual resolution.
- **This Spec's own problem statement remains valid and unresolved** —
  the grazing-angle self-shadow-acne defect this Spec exists to fix is
  real, reproduced, and not fixed by this deferral; it remains an open
  problem for a future round.
- **No alternative strategy (PCF, cascaded shadow maps, variance/
  exponential shadow maps, or any other technique beyond this Spec's
  own already-named Alternatives Considered) is selected or authorized
  by this record.** Resuming this work requires a new, reviewed Spec/
  ADR revision (or a fresh Spec) and its own Human Review — this
  deferral record is not itself that review.
- [Plan 0030](../plans/0030-directional-shadow-bias-stability.md) and
  its own Implementation PR
  ([PR #134](https://github.com/slmao/Atlantis/pull/134)) remain
  `Draft`/blocked — this record does not approve, merge, or advance
  either.

**Deciders:** slmao (`slmao <slmaosjtu@gmail.com>`) — Human Review
deferral direction recorded 2026-09-07, as described above, with no
further condition.
