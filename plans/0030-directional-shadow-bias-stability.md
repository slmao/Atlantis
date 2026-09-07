# Plan: Directional Shadow Bias Stability

- **Spec:** [specs/0030-directional-shadow-bias-stability.md](../specs/0030-directional-shadow-bias-stability.md) (`Approved`)
- **Status:** Draft — **blocked pending Human Review** (see "Blocking
  Finding" below; this Plan does not currently recommend any candidate
  for approval)
- **Author:** slmao

## Blocking Finding (read first)

A second real-GPU probe round, adding the grazing-receiver/true-occluder
discriminator Spec 0030's own Testing & Verification Plan always
required but this Plan's first draft had not yet built, found that the
first draft's own recommended candidate (`kShadowBiasMin=0.0015`,
`kShadowBiasSlopeScale=0.003`, `kShadowBiasMax=0.040`, formerly "row 17"
below) **completely erases a real, physically valid nearby occluder's
shadow** at the same grazing incidence the acne fix targets — confirmed
on real GPU hardware (P6 below), not just predicted. Further analysis
(P6) shows this is not a bad candidate choice but a **structural
conflict inherent to the currently-approved bounded slope-aware formula
at this shadow-map resolution**: the same `kShadowBiasMax` value that
must be large enough to suppress the sphere's own acne is, at the exact
same grazing angle, also large enough to swallow any real occluder whose
own light-space depth separation from its receiver is smaller than that
value. No candidate value of `kShadowBiasMax` can be simultaneously
large enough for one and small enough for the other, for an occluder in
the tested proximity range. **This Plan therefore revokes its own prior
recommendation, proposes no other candidate, and stops here for Human
Review** — see P6's own "Why no re-sweep was run" for why a further
candidate search inside the current formula is not expected to change
this outcome, and why this Plan does not switch to Spec 0030's own
Alternative 3/4 (Vulkan rasterization depth bias / normal-offset shadow
mapping) on its own authority.

## Objective

Implement Spec 0030 / ADR-0072's 2026-09-07 Accepted Amendment: replace
the single, fixed `kShadowBias = 0.0015` literal in all four PBR
shaders' `computeShadowFactor()` with a bounded, geometric-normal,
slope-aware receiver-side bias (`kShadowBiasMin`/`kShadowBiasSlopeScale`/
`kShadowBiasMax`), fix its three numeric constants from a real-GPU
Plan-stage probe (below), add the GPU-independent finiteness test and
the real-GPU paired-differential acne tests the Spec's own Testing &
Verification Plan requires, then close out Spec 0029's own blocked
Milestone 6-7 on the one combined branch Spec 0030's Dependency and
Integration Sequencing section fixes. **As of this revision, that
objective is not yet achievable — see Blocking Finding above.**

## Plan-Stage Real-GPU Probe: Method and Real, Measured Results

All values in this section were measured on real Vulkan-capable
hardware via temporary, uncommitted probes (most recently
`tests/image_regression/probe_0030_occlusion_guard_gpu_tests.cpp`,
`tests/image_regression/probe_0030_threshold_sweep_gpu_tests.cpp`, and
`tests/runtime/probe_0030_occlusion_derivation_tests.cpp`; the first
round's own `probe_0030_shadow_bias_gpu_tests.cpp` before it), built and
run inside a combined `feature/0029-...`+`main` probe worktree, never
committed, reverted before this Plan was submitted — mirroring Spec
0029's own `stb_image_write` temporary-tool precedent and Plan 0026 P3's
own `slangc` probe precedent. The probes reused
`PbrNormalMapDemoFixture`'s own already-existing `includeShadowCasters`/
`useControlMaterial` toggles unchanged, and reused the fixture's own
`pbr_normal_map_demo` scene both with `environmentArtifactPath` set (the
`pbr_ibl`/`pbr_ibl_normal_map` paths) and empty (the `pbr_direct_lit`/
`pbr_direct_lit_normal_map` paths) — one physical sphere/scene reaches
all four shader paths, exactly as Spec 0030's own Testing & Verification
Plan anticipated. The new occlusion-guard discriminator (P6) uses its
own separate, hand-built ground+occluder scene (no scene asset, no new
product API), mirroring `tests/image_regression/shadow_gpu_tests.cpp`'s
own established `ShadowTestRig` pattern almost verbatim.

### P1 — Sphere Coverage Mask: real construction and measured pixel count

A bespoke, direct RenderGraph pass (not `Renderer::drawFrame()`, whose
own `colorClear` is a fixed, non-black `(0.05,0.05,0.08,1.0)` background
constant unrelated to this mask's own black-clear requirement) — mirrors
`tests/vulkan_backend/pipeline_depth_write_gpu_tests.cpp`'s own
established "drive Pipeline/RenderGraph directly" pattern:

- Reuses the fixture's own already-loaded `unlitTexturedVertexSpirv`/
  `unlitTexturedFragmentSpirv` (`textured_quad` shader) and
  `unlitTexturedVertexInputLayout` — no new shader.
- A dedicated `Pipeline` (`colorFormat = Rgba8Unorm` — the real, final
  target format, not the HDR intermediate — `depthFormat = D32Sfloat`,
  `pushConstantSizeBytes = 64`, `sampledTextureBindingCount = 1`,
  `hasCameraUniformBinding = true`, `hasDepthAttachment = true`).
- A 1x1 solid-white `SampledTexture` (`Rgba8Srgb`, `{255,255,255,255}`),
  uploaded via its own dedicated, separate submit cycle (mirroring
  `tests/runtime/pbr_render_gpu_tests.cpp`'s own established texture-
  upload pattern), bound at binding 1.
- The fixture's own already-populated `cameraBuffer` (its leading
  128-byte view/projection prefix is shared, byte-identical, across
  every shader in this fixture) is reused unchanged — no new camera
  buffer.
- Only the sphere's own `DrawItem` is submitted (found via
  `computeAssetId("meshes/pbr_sphere.mesh.txt")`, matched against
  `World::renderableEntities()`) — no ground, no sky, no other geometry.
- `ResourceBinding.colorClear = {0,0,0,1}` (true black) on the fixture's
  own real `offscreenTarget`/`readbackBuffer`.
- Mask predicate: a pixel is sphere-covered iff any of R/G/B `> 0` in the
  readback.

**Measured mask pixel count: 6892** (of the fixed 512x512 = 262144 total
pixels), identical across two independent captures (byte-for-byte
identical mask array both times) — confirms the mask itself is fully
deterministic. The physical scene/camera/sphere is identical across all
four shader paths (only `environmentArtifactPath` toggles which shader
pair the sphere/control materials select), so **this one 6892-pixel
mask is reused unchanged for all four paths** — not four independently
re-derived masks, since re-deriving would measure the identical
geometry/camera/sphere silhouette four times for no new information.

### P2 — Repeatability and the threshold-0-5 disclosure sweep (real GPU, with bounding boxes)

**Naming correction from this Plan's first draft:** the first draft
called the value `5` a "measured noise threshold." That name overstated
what was measured. Real repeatability (below) is exactly `0` — there is
no measured noise to threshold away. `5` is instead a small, fixed
**visibility/quantization cutoff**: a deliberately tiny margin above the
proven-`0` floor, chosen only to exclude single-`rgbSum`-unit
differences that a real render pipeline can produce at a shadow
boundary's own partial-coverage/8-bit-quantization edge (sub-visible,
never a multi-unit difference) — not a data-driven "where real signal
starts" measurement, and not something chosen to make any candidate's
own count look better. Every value this cutoff excludes is disclosed
below, not hidden.

1. **Raw repeatability.** The identical `(includeShadowCasters=true,
   useControlMaterial=false)` capture, run twice in succession against
   the same fixture (`pbr_direct_lit_normal_map` path, pre-fix shaders).
   **Measured max absolute per-pixel `rgbSum` difference across the two
   captures, over every sphere-masked pixel: 0.** This confirms this
   environment's own real GPU/driver reproduces this render byte-for-
   byte identically run to run — the same determinism ADR-0042 already
   established for golden comparison, holding here too.
2. **Threshold 0-5 disclosure, pre-fix (`kShadowBias = 0.0015`), all
   four shader paths, with bounding box of the flagged (masked,
   `darkening > t`) pixels** (`darkening = rgbSum(shadowOff) -
   rgbSum(shadowOn)`):

   | Path | t>0 | t>1 | t>2 | t>3 | t>4 | t>5 | bbox (all t, stable) |
   |---|---|---|---|---|---|---|---|
   | `pbr_direct_lit_normal_map` | 1376 | 1376 | 1376 | 1375 | 1375 | 1375 | `[213,199]`-`[277,285]` |
   | `pbr_direct_lit` (control) | 137 | 136 | 135 | 134 | 134 | 132 | `[224,199]`-`[277,281]` |
   | `pbr_ibl_normal_map` | 1375 | 1375 | 1374 | 1372 | 1370 | 1368 | `[213,199]`-`[277,285]` |
   | `pbr_ibl` (control) | 136 | 131 | 130 | 128 | 126 | 124 | `[224,199]`-`[277,281]` (`[225,...]` from t>3) |

   **Explicitly disclosed, not hidden:** raising the cutoff from `0` to
   `5` excludes only **1** pixel for `pbr_direct_lit_normal_map`, **5**
   for `pbr_direct_lit`, **7** for `pbr_ibl_normal_map`, and **12** for
   `pbr_ibl` — a small, real, sub-1%-of-baseline population in every
   case, consistent with genuine sub-visible quantization noise, not a
   meaningful fraction of the real acne signal being swept under the
   cutoff. The bounding box is stable across the whole 0-5 range in
   every path (the population added/removed by moving the cutoff sits
   inside the same region, never outside it) — confirming `5` does not
   mask a spatially distinct phenomenon.
3. **Threshold 0-5, the (now-revoked) candidate
   (`0.0015,0.003,0.040`), all four paths:** **`0` at every threshold
   from `t>0` through `t>5`, all four paths** — not merely "below `5`,"
   genuinely zero difference at the most stringent possible cutoff. This
   number does not change as a result of P6's own finding below; it is
   restated here only for completeness of the threshold-disclosure
   requirement.

The cutoff is fixed at **`5`**, before P3's own baseline/ceiling are
computed from it and before any candidate is swept — unchanged in
substance from the first draft, renamed for accuracy per Human Review's
own request.

### P3 — Pre-fix baseline and ceiling, per shader path (real GPU, cutoff = 5)

Measured against the current, unmodified shaders
(`kShadowBias = 0.0015`), using the Paired Acne Metric (Spec 0030's own
definition: shadow-on/shadow-off pair, `darkening > 5` over the 6892-
pixel mask) and the existing `(198,273)`/`>15` ground-shadow
discriminator, unmodified:

| Shader path | Pre-fix acne baseline | Ceiling `= min(10, floor(baseline * 0.01))` | Pre-fix ground `rgbSum` delta at `(198,273)` |
|---|---|---|---|
| `pbr_direct_lit_normal_map` | 1375 | 10 | 219 |
| `pbr_direct_lit` (control) | 132 | 1 | 219 |
| `pbr_ibl_normal_map` | 1368 | 10 | 160 |
| `pbr_ibl` (control) | 124 | 1 | 160 |

**Real, disclosed finding: the two normal-map paths' own pre-fix acne
baseline is roughly 10x the two control (non-normal-map) paths' own
baseline, at the identical cutoff, over the identical 6892-pixel mask,
against the identical geometry/shadow map.** `computeShadowFactor()`
itself takes `worldPosition` (material-independent) and — after this
Plan's own fix — `NdotLGeo` computed from the same `N_geo`/`L` in both
materials, so the *set* of pixels whose `shadowFactor` differs between
the shadow-on/off pair is expected to be materially identical between
the normal-mapped and control sphere. The measured asymmetry is
explained by `darkening`'s own magnitude, not by a different affected-
pixel set: `pbr_normal_mapped.material.txt`'s fixed, spatially-uniform
tilted normal map (Spec 0029's own `(204,128,230)` constant texel,
confirmed unchanged by this Plan) shifts the *perturbed* `N` away from
`N_geo` uniformly, changing the directional term's own `NdotL`-driven
magnitude at a shadow-affected pixel — a larger swing crosses the fixed
`darkening > 5` cutoff more often than the control material's own,
generally smaller swing at the same underlying flipped-`shadowFactor`
locations. This is a real, disclosed, per-material measurement
consequence of a fixed absolute cutoff — not evidence of two different
defects, and not a reason to use two different cutoffs (Spec 0030 fixes
one cutoff, applied uniformly).

This also means the two control paths carry a strict `ceiling = 1` —
tighter than the `10`-pixel cap that governs the two normal-map paths.
Both are measured and enforced independently, per shader path, exactly
as Spec 0030's own acceptance rule requires.

### P4 — Candidate manifest: fixed range, step size, and scan order

`kShadowBiasMin` is fixed at the existing `kShadowBias = 0.0015` value,
unchanged from Plan 0027 P4's own derivation (`~4.5cm` world-space slack
over the `29.9`-unit orthographic depth span,
`0.0015 * 29.9 = 0.04485`) — the flat bias already proven adequate at
near-perpendicular incidence (Spec 0030's own Evidence: the candidate's
own `(256,256)` pixel, `dot(N,L) > 0.6`, unaffected in every prior
capture) should not regress there, and this Plan's own candidates below
only ever raise the bias at *more* grazing angles, never lower it at
this floor.

**Fixed candidate set** (`kShadowBiasSlopeScale`, `kShadowBiasMax`),
`kShadowBiasMax` expressed both as an NDC-depth literal and its
equivalent world-space bias (`* 29.9`):

- `kShadowBiasSlopeScale ∈ {0.003, 0.006, 0.009, 0.012}`
- `kShadowBiasMax ∈ {0.020 (0.598 world units), 0.025 (0.748), 0.030 (0.897), 0.035 (1.047), 0.040 (1.196), 0.045 (1.346)}`

**Fixed scan order:** outer loop `kShadowBiasMax` ascending, inner loop
`kShadowBiasSlopeScale` ascending.

**Fixed formula, identical in all four shaders:**

```
float NdotLGeo = max(dot(N_geo, L), kMinDot);   // N_geo == N in pbr_direct_lit/pbr_ibl (no perturbed normal exists there)
float slope = sqrt(max(1.0 - NdotLGeo * NdotLGeo, 0.0)) / NdotLGeo;
float bias = clamp(kShadowBiasMin + kShadowBiasSlopeScale * slope, kShadowBiasMin, kShadowBiasMax);
return (shadowNdc.z - bias) <= storedDepth ? 1.0 : 0.0;
```

### P5 — Acne-only sweep: real, per-candidate results (superseded — see P6)

Every candidate below was captured on real GPU hardware, all four
shader paths, in the fixed scan order — build the four shaders (a
`slangc` recompile only, no C++ rebuild — SPIR-V is loaded from disk at
test run time), then re-run the same, unmodified probe binary. **This
table measures acne suppression and the existing ground discriminator
only — it does not include the occlusion-guard discriminator P6 adds,
and its own "accepted" conclusion is revoked by P6's finding below.**

| # | `kShadowBiasSlopeScale` | `kShadowBiasMax` | `pbr_direct_lit_normal_map` acne (ceiling 10) | `pbr_direct_lit` acne (ceiling 1) | `pbr_ibl_normal_map` acne (ceiling 10) | `pbr_ibl` acne (ceiling 1) | Ground delta (non-env / env, need `>15`) | Acne+ground pass? |
|---|---|---|---|---|---|---|---|---|
| 1 | 0.003 | 0.020 | 421 | 0 | 414 | 0 | 219 / 160 | No |
| 2 | 0.006 | 0.020 | 421 | 0 | 414 | 0 | 219 / 160 | No |
| 3 | 0.009 | 0.020 | 421 | 0 | 414 | 0 | 219 / 160 | No |
| 4 | 0.012 | 0.020 | 421 | 0 | 414 | 0 | 219 / 160 | No |
| 5 | 0.003 | 0.025 | 247 | 0 | 242 | 0 | 219 / 160 | No |
| 6 | 0.006 | 0.025 | 247 | 0 | 242 | 0 | 219 / 160 | No |
| 7 | 0.009 | 0.025 | 247 | 0 | 242 | 0 | 219 / 160 | No |
| 8 | 0.012 | 0.025 | 247 | 0 | 242 | 0 | 219 / 160 | No |
| 9 | 0.003 | 0.030 | 111 | 0 | 106 | 0 | 219 / 160 | No |
| 10 | 0.006 | 0.030 | 111 | 0 | 106 | 0 | 219 / 160 | No |
| 11 | 0.009 | 0.030 | 111 | 0 | 106 | 0 | 219 / 160 | No |
| 12 | 0.012 | 0.030 | 111 | 0 | 106 | 0 | 219 / 160 | No |
| 13 | 0.003 | 0.035 | 22 | 0 | 19 | 0 | 219 / 160 | No |
| 14 | 0.006 | 0.035 | 22 | 0 | 19 | 0 | 219 / 160 | No |
| 15 | 0.009 | 0.035 | 22 | 0 | 19 | 0 | 219 / 160 | No |
| 16 | 0.012 | 0.035 | 22 | 0 | 19 | 0 | 219 / 160 | No |
| 17 | 0.003 | 0.040 | 0 | 0 | 0 | 0 | 219 / 160 | **Acne+ground: Yes — but see P6: REVOKED, fails the occlusion guard** |
| 18 | 0.006 | 0.040 | 0 | 0 | 0 | 0 | 219 / 160 | (moot — see P6) |
| 19 | 0.009 | 0.040 | 0 | 0 | 0 | 0 | 219 / 160 | (moot — see P6) |
| 20 | 0.012 | 0.040 | 0 | 0 | 0 | 0 | 219 / 160 | (moot — see P6) |
| 21 | 0.003 | 0.045 | 0 | 0 | 0 | 0 | 219 / 160 | (moot — see P6) |
| 22 | 0.006 | 0.045 | 0 | 0 | 0 | 0 | 219 / 160 | (moot — see P6) |
| 23 | 0.009 | 0.045 | 0 | 0 | 0 | 0 | 219 / 160 | (moot — see P6) |
| 24 | 0.012 | 0.045 | 0 | 0 | 0 | 0 | 219 / 160 | (moot — see P6) |

Rows 1-16 already show every tested `kShadowBiasMax <= 0.035` leaves a
real, measured acne residual **above its own ceiling** — acne
suppression to within ceiling requires `kShadowBiasMax` somewhere
between `0.035` (fails, residual `22`/`19`) and `0.040` (passes,
residual `0`/`0`). This lower bound matters directly to P6's own
conclusion below.

### P6 — Real grazing-receiver/true-occluder discriminator: geometry, results, and why every remaining candidate is expected to fail

Spec 0030's own Requirements state the acceptance gate must confirm the
bias fix does not erase a real, valid nearby shadow ("peter-panning") —
P5's own ground-shadow check re-uses one already-fixed, already-shadowed
scene point and therefore cannot detect this on its own (a single
existing shadow staying dark says nothing about a *different*, closer
occluder). This section adds the missing, dedicated discriminator: a
receiver at the same kind of grazing incidence the acne fix targets,
with a real, solid occluder whose own light-space depth separation from
the receiver is deliberately fixed strictly between the pre-fix
`kShadowBias` and the (former) candidate's own `kShadowBiasMax`.

#### Geometry (mechanically derived, not hand-guessed)

Derived by a temporary, uncommitted GPU-independent tool
(`tests/runtime/probe_0030_occlusion_derivation_tests.cpp`) that calls
the real, production `computeShadowLightSpaceMatrices()` function
directly — every number below is a printed, reproducible output of that
tool, not a hand calculation:

- **Receiver:** a flat ground plane, normal `(0,1,0)`, sample point
  `P = (0,0,0)`.
- **Light direction** `D = normalize(-1, -0.069829, 0)` (the direction
  the light *travels*) — chosen so `NdotLGeo = dot((0,1,0), -D) =
  0.069829` at `P`, which is **past this formula's own saturation
  point** (`slope >= (kShadowBiasMax - kShadowBiasMin) /
  kShadowBiasSlopeScale = (0.040 - 0.0015) / 0.003 = 12.83`, i.e.
  `NdotLGeo <= ~0.0777`) — confirmed: `slope = 14.285714`, and the
  raw (unclamped) computed bias `0.0443` already exceeds every
  `kShadowBiasMax` value in P4's own candidate set, so **every**
  candidate in this Plan's own grid is fully saturated to its own
  `kShadowBiasMax` at this exact receiver point — this is deliberate:
  it is the same regime P5's own acne fix must operate in.
- **Occluder:** a thin, wide plate (half-extents `1.0 x 0.02 x 0.5`
  world units), base resting exactly on the ground (`y=0`), centered at
  `(0.285714, 0.02, 0)` — chosen so the point on the plate's own top
  surface that shares `P`'s own shadow-map texel (the exact "stored
  depth" the shader reads at `P`) sits at world `(0.571429, 0.04, 0)`,
  well inside the plate's own footprint (margin `1.286`/`0.714` world
  units to the plate's own near/far edges — genuinely interior, not a
  rasterization-boundary artifact; an earlier, narrower plate size
  placed this point only `0.015` from the plate's own edge and was
  widened before any GPU capture was taken).
- **Real, measured light-space depth separation** (`P`'s own depth minus
  the stored value at `P`'s own shadow-map texel):
  **`0.019158` NDC (`0.572827` world units)** — strictly greater than
  `kShadowBiasMin = 0.0015` (by `~12.8x`) and strictly less than the
  revoked candidate's own `kShadowBiasMax = 0.040` (using only `48%` of
  it), exactly the window Human Review's own instructions required.
- **Camera:** eye `(0,6,4)`, forward `= normalize(-eye)`, `fovY=60°`,
  `aspect=1`, `near=0.1`, `far=100` (the same
  `lookAtMatrixFromForward`/`perspectiveMatrixDirect` convention every
  other GPU test in this codebase already uses).
- **Predicted pixels** (same derivation tool, same camera): `P` →
  `(256,256)`; the shadow's own predicted **near edge** (right at the
  plate's own base, world `x=0.714286`) → `(300,256)`; the shadow's own
  predicted **far edge** (the plate's own top-face reach, world
  `x=-1.285714`) → `(177,256)`.

#### Real-GPU results: O1-O4

A hand-built ground+occluder scene (`PbrDirectLit`, no environment,
`ShadowTestRig`-style rig), captured at all three pixels above:

| Capture | Bias | Occluder | `P` (256,256) | near-edge (300,256) | far-edge (177,256) |
|---|---|---|---|---|---|
| O1 | pre-fix (`kShadowBias=0.0015`) | present | `rgbSum=0` | `rgbSum=0` | `rgbSum=0` |
| O2 | pre-fix (`kShadowBias=0.0015`) | absent | `rgbSum=183` | `rgbSum=195` | `rgbSum=189` |
| O3 | candidate (`0.0015,0.003,0.040`) | present | `rgbSum=183` | `rgbSum=195` | `rgbSum=189` |
| O4 | candidate (`0.0015,0.003,0.040`) | absent | `rgbSum=183` | `rgbSum=195` | `rgbSum=189` |

- **O1 vs O2 (pre-fix): `delta = 183/195/189` at all three points —
  real, deep, complete shadow, confirming this is a genuine, physically
  valid occlusion** (not a degenerate/contrived setup) at every sampled
  point across the entire predicted footprint, not only its own deepest
  interior.
- **O3 vs O4 (candidate): `delta = 0/0/0` at all three points — the
  occluder's presence makes literally zero difference.** The real, solid
  occluder's own shadow has been **completely erased**, not merely
  shrunk at its own boundary — `O3 == O4` exactly, and both equal `O2`
  (the pre-fix unshadowed value) — a total light leak, confirmed on real
  GPU hardware, Vulkan Validation Layers clean throughout both captures.
- **Boundary/contact conclusion:** because O3/O4 are identical at *all
  three* points (interior `P`, the shadow's own near edge, and its own
  far edge), this is not a boundary-detachment artifact (a shadow that
  merely shrinks inward at its own edge) — the *entire* footprint is
  gone. `0.040`'s own answer to "does this cause real occlusion light
  leak" is unambiguously **yes**.

#### Why no further re-sweep was run

`kShadowBiasSlopeScale` was already shown (P5, rows 1-16 vs. their own
`kShadowBiasMax`-matched siblings) to have negligible effect once the
formula saturates — every row sharing the same `kShadowBiasMax` produces
the *identical* acne count regardless of `kShadowBiasSlopeScale`, and
P6's own receiver point is, by construction, saturated for every
candidate in P4's grid. This means the *effective* bias P6 measures at
this receiver is simply each candidate's own `kShadowBiasMax` value,
independent of `kShadowBiasSlopeScale` — so the occlusion-guard's own
pass/fail line is exactly `kShadowBiasMax` vs. the real, fixed separation
`0.019158`:

- **Every `kShadowBiasMax` value in P4's own candidate set (`0.020`
  through `0.045`) already exceeds `0.019158`** — the smallest,
  `0.020`, exceeds it by only `0.000842`, but still exceeds it. **Every
  one of the 24 candidates in this Plan's own fixed grid is therefore
  predicted to fail this occlusion guard identically to the row-17
  candidate actually tested** (all fully saturate at this receiver, all
  read a stored depth from the identical plate, all compare against the
  identical `P.z`).
- Independently, P5 already showed acne suppression to within ceiling
  requires `kShadowBiasMax >= ~0.038`-`0.040` (interpolated: `0.035`
  fails with residual `22`/`19`; `0.040` passes with residual `0`/`0`).
- **These two requirements do not overlap**: occlusion preservation (for
  this occluder) needs `kShadowBiasMax < 0.019158`; acne suppression
  needs `kShadowBiasMax >= ~0.038`. The gap between them (`~0.019`
  NDC, `~0.57` world units) is roughly **2x**, not a close call decided
  by which exact candidate is picked. Running the remaining 23 rows of
  P4's own grid through the real O1-O4 capture would, on this reasoning,
  reconfirm the same failure 23 more times at real GPU cost, without
  changing the conclusion — this Plan does not spend that cost and
  instead reports the reasoning plus the one real, GPU-confirmed data
  point (row 17) directly to Human Review.
- **This conclusion is scoped to the currently-approved formula shape**
  (a single, global, receiver-side `kShadowBiasMax` clamp, uniform
  across the whole scene) **and to occluders whose own light-space depth
  separation from their receiver is below that clamp, at grazing
  incidence** — it does not claim every possible scene/occluder
  configuration fails, and it does not evaluate Spec 0030's own
  Alternative 3 (Vulkan rasterization depth bias) or Alternative 4
  (normal-offset shadow mapping), which operate on a different
  mechanism (caster-side rasterization bias, or a world-space position
  offset) not subject to this exact clamp-vs-separation argument. This
  Plan does not select either alternative on its own authority — that
  is a new design decision for Human Review, not an Implementation-time
  substitution.

## Files / Modules Touched (expected)

**Not applicable until Human Review resolves the Blocking Finding
above** — no shader, test, asset, or golden file is touched by this
Plan's own Implementation while it remains blocked. The list below
records what Milestone 1-5 would touch once a real, non-conflicting
formula/candidate exists, and is kept for reference:

- `shaders/pbr_direct_lit/pbr_direct_lit.slang`,
  `shaders/pbr_ibl/pbr_ibl.slang`,
  `shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.slang`,
  `shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.slang` — `kShadowBias`
  replaced by `kShadowBiasMin`/`kShadowBiasSlopeScale`/`kShadowBiasMax`;
  `computeShadowFactor()` gains an `NdotLGeo` parameter; each call site
  computes `NdotLGeo` from the already-local `N_geo`/`N`/`L` immediately
  before calling it. No binding, descriptor, uniform-buffer, or vertex-
  layout change in any of the four files.
- `tests/shader_system/shadow_bias_formula_tests.cpp` (new — **module
  corrected from this Plan's first draft's own `tests/core/`; see
  "Test-ownership correction" below**) — the GPU-independent
  finiteness/range test (Milestone 1 below).
- `tests/image_regression/pbr_normal_map_demo_gpu_tests.cpp` (new
  `TEST_CASE`s) — the Sphere Coverage Mask + Paired Acne Metric,
  real-GPU, for all four shader paths; the ground cast-shadow
  discriminator re-run against the fixed bias; the IBL isolation
  control.
- `tests/image_regression/shadow_gpu_tests.cpp` or a new sibling file
  (new `TEST_CASE`s) — the grazing-receiver/true-occluder discriminator
  (P6), made a permanent, real-GPU regression test — not only a Plan-
  stage probe — since it is the acceptance gate for peter-panning this
  Plan's own bias formula must clear before any candidate can be
  accepted.
- `tests/image_regression/fixture/pbr_normal_map_demo_fixture.h/.cpp` —
  no functional change expected (the fixture's own existing
  `includeShadowCasters`/`useControlMaterial` toggles already cover
  everything Milestone 2's tests need); a deviation here is called out
  explicitly if Implementation finds otherwise.
- `tests/image_regression/goldens/pbr_normal_map_demo/*` — the old,
  untracked, pre-fix candidate is discarded; a fresh candidate PNG/
  sidecar is generated after a real fix lands, per the golden strategy
  below.
- `specs/0029-tangent-space-normal-mapping-foundation.md`,
  `specs/README.md` — Spec 0029's own registry row updated once its
  Milestone 6-7 complete on the combined branch (Milestone 5 below).

### Test-ownership correction

This Plan's first draft placed the GPU-independent finiteness/range test
in `tests/core/`. On review: that logic is a test-local C++ mirror of a
**shader's own** bias formula — it has no relationship to Atlantis Core
(logging/assertions/result types; `tests/core/CMakeLists.txt` links only
`Atlantis::Core`, no shader/shadow dependency of any kind). It belongs
grouped with this codebase's other shader-domain, GPU-independent tests:
`tests/shader_system/CMakeLists.txt`'s own `atlantis_shader_system_tests`
executable already hosts exactly this class of test (`descriptor_contract_tests.cpp`,
`shadow_cast_reflection_tests.cpp`, `pbr_ibl_reflection_tests.cpp`, etc.)
— GPU-independent, grouped by domain, not by which library a given test
happens to link. `tests/shader_system/shadow_bias_formula_tests.cpp`
needs no new `target_link_libraries` entry (the test is pure C++ math,
no Atlantis library dependency at all), so this move does not give
`atlantis_shader_system_tests` any dependency it does not already carry.

## Sequencing & Dependencies

This Plan implements Spec 0030's own **Dependency and Integration
Sequencing** section verbatim — repeated here only as an operational
summary, not re-derived. **Unaffected by the Blocking Finding above**:
this sequencing describes how Implementation reaches `main` once a real
candidate exists; it does not itself require a change.

1. This Plan (`plan/0030-directional-shadow-bias-stability`, cut from
   `origin/main`) goes through Human Review and merges to `main` on its
   own.
2. **Implementation is not branched from plain `main`.** A new branch,
   `feature/0030-directional-shadow-bias-stability`, starts from
   `feature/0029-tangent-space-normal-mapping-foundation`'s own
   completed Milestone 1-5 HEAD, then merges the latest `main` (ordinary
   `merge`, never `rebase`/`force-push`).
3. Milestones 1-4 below (shader fix, tests, compare-first, fresh
   candidate) execute on this combined branch — all four shader files
   genuinely exist there.
4. Milestone 5 (Spec 0029's own Milestone 6-7 closeout) executes on the
   same combined branch, once the fresh `pbr_normal_map_demo` candidate
   from Milestone 4 is independently Human-Review-approved.
5. **Exactly one Implementation PR**, base `main`, from the combined
   branch, closes both Spec 0029 and Spec 0030.
   `feature/0029-tangent-space-normal-mapping-foundation` itself is
   never separately merged, deleted, or rewritten.

## Milestones / Task Breakdown

**Blocked — none of the below may start until Human Review resolves the
Blocking Finding above** (either a real candidate/formula that also
clears P6's own occlusion guard is found and fixed here, or Human Review
directs a different path, e.g. evaluating Spec 0030's own Alternative
3/4). Recorded here as the sequence that applies once unblocked — the
sequence itself is not what changed:

1. **Shader bias fix + GPU-independent finiteness test.** On the
   combined branch: apply the fixed formula with the accepted constants
   (once real ones exist) identically to all four shaders; add
   `tests/shader_system/shadow_bias_formula_tests.cpp` — a plain-C++
   mirror of `slope`/`bias`:
   ```cpp
   float slope = std::sqrt(std::max(1.0f - ndotLGeo * ndotLGeo, 0.0f)) / ndotLGeo;
   float bias = std::clamp(kShadowBiasMin + kShadowBiasSlopeScale * slope, kShadowBiasMin, kShadowBiasMax);
   ```
   evaluated across a fixed input set covering the full approved
   `NdotLGeo` domain — `kMinDot` (`1e-4`, the floor every shader already
   applies before calling `computeShadowFactor()`), `0.001`, `0.01`,
   `0.05`, `0.1`, `0.2`, `0.3`, `0.5`, `0.7`, `0.9`, and `1.0` (exact
   perpendicular incidence) — asserting every result is finite
   (`std::isfinite`) and lies within `[kShadowBiasMin, kShadowBiasMax]`.
   Confirm all four `.slang` files declare byte-identical constants and
   formula text (the "exact twin" check, mirroring ADR-0074's own
   established discipline). Commit boundary: shaders + the one new
   GPU-independent test file, builds and passes `ctest -LE gpu` on its
   own.
2. **Real-GPU verification: mask, paired differential, ground,
   occlusion guard, IBL isolation.** Add the Sphere Coverage Mask
   construction and the Paired Acne Metric as real `TEST_CASE`s in
   `tests/image_regression/pbr_normal_map_demo_gpu_tests.cpp`, covering
   all four shader paths. Assert each path's own post-fix acne count
   against its own fixed ceiling (P3's table) and the `(198,273)`/`>15`
   ground discriminator. **Add P6's own grazing-receiver/true-occluder
   discriminator as a permanent regression `TEST_CASE`**, asserting
   `O1`/`O2` (pre-fix reference, unaffected by this Plan's own fix)
   remain a real shadow and `O3`/`O4` (the fixed formula) preserve a
   measurable shadow at all three sampled points — not merely at the
   deepest interior point. Re-run the existing IBL isolation control
   (R1/R2/R3, ADR-0072 D-7's own established pattern) against the fixed
   bias. Commit boundary: new/updated GPU tests only, `ctest -L gpu`
   green, Vulkan Validation Layers clean.
3. **Compare-first against every existing golden.** Unchanged from the
   first draft.
4. **Fresh `pbr_normal_map_demo` candidate.** Unchanged from the first
   draft.
5. **Spec 0029 Milestone 6-7 closeout + final combined verification.**
   Unchanged from the first draft.

## Verification Checklist

- [ ] **Blocking Finding resolved by Human Review** — a formula/candidate
      exists that passes acne suppression (P3 ceilings), the existing
      ground discriminator, *and* the P6 occlusion guard simultaneously,
      or Human Review has directed an alternative path.
- [ ] Unit tests: `tests/shader_system/shadow_bias_formula_tests.cpp` —
      finite, bounded `slope`/`bias` across the full `NdotLGeo` range and
      the accepted constants.
- [ ] Headless integration tests: existing headless/offscreen paths
      unaffected (no RHI/RenderGraph change).
- [ ] Image regression tests: Sphere Coverage Mask + Paired Acne Metric
      (all 4 shader paths, against each path's own fixed ceiling);
      ground cast-shadow discriminator (`(198,273)`/`>15`, all 4 paths);
      **grazing-receiver/true-occluder discriminator (P6), all 3 sampled
      points, real shadow preserved**; IBL isolation control; compare-
      first against all 9 existing goldens + `world_scene_loaded`/
      `sky_background`; fresh `pbr_normal_map_demo` candidate, human-
      reviewed and committed.
- [ ] Vulkan Validation Layers clean: Debug and Release, every real-GPU
      test in this Plan's own scope.
- [ ] Other: `ctest -LE gpu` and `ctest -L gpu` both green, Debug and
      Release, on the combined branch, including every pre-existing test
      this branch now carries (Spec 0029's own Milestone 1-5 suite).

## Rollback Plan

Revert the one combined Implementation PR. Because the bias fix is
confined to four `.slang` files' own local arithmetic (no RHI/API/
descriptor/uniform-buffer change), reverting is a plain shader-source
revert plus the golden commit's own revert — no migration, no data-
format rollback, no caller-site changes to undo.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan:

- The "Image regression tests added/updated" item is satisfied by the
  new real-GPU `TEST_CASE`s (Milestone 2, including the P6 occlusion
  guard as a permanent regression test) and the fresh, human-reviewed
  `pbr_normal_map_demo` golden (Milestone 4).
- The "approved plan exists and the implementation matches it" item
  requires the three fixed bias constants that land in the four shaders
  to be byte-identical to this Plan's own final, sweep-selected
  candidate — any deviation is a stop-and-report, not a silent
  substitution.
- **This Plan is not "Ready for Implementation" as submitted** — see
  Blocking Finding.
