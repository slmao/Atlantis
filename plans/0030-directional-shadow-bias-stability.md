# Plan: Directional Shadow Bias Stability

- **Spec:** [specs/0030-directional-shadow-bias-stability.md](../specs/0030-directional-shadow-bias-stability.md) (`Approved`)
- **Status:** Draft
- **Author:** slmao

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
Integration Sequencing section fixes.

## Plan-Stage Real-GPU Probe: Method and Real, Measured Results

All values in this section were measured on real Vulkan-capable
hardware via a temporary, uncommitted probe
(`tests/image_regression/probe_0030_shadow_bias_gpu_tests.cpp`, built
and run inside a combined `feature/0029-...`+`main` probe worktree,
never committed, reverted before this Plan was submitted — mirroring
Spec 0029's own `stb_image_write` temporary-tool precedent and Plan
0026 P3's own `slangc` probe precedent). The probe reused
`PbrNormalMapDemoFixture`'s own already-existing `includeShadowCasters`/
`useControlMaterial` toggles unchanged, and reused the fixture's own
`pbr_normal_map_demo` scene both with `environmentArtifactPath` set (the
`pbr_ibl`/`pbr_ibl_normal_map` paths) and empty (the `pbr_direct_lit`/
`pbr_direct_lit_normal_map` paths) — one physical sphere/scene reaches
all four shader paths, exactly as Spec 0030's own Testing & Verification
Plan anticipated.

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

### P2 — Repeatability and the noise threshold: real, measured determinism

Two real-GPU checks, both against the unfixed (`kShadowBias = 0.0015`)
shaders, `pbr_direct_lit_normal_map` path:

1. **Raw repeatability.** The identical `(includeShadowCasters=true,
   useControlMaterial=false)` capture, run twice in succession against
   the same fixture. **Measured max absolute per-pixel `rgbSum`
   difference across the two captures, over every sphere-masked pixel:
   0.** This confirms this environment's own real GPU/driver reproduces
   this render byte-for-byte identically run to run — the same
   determinism ADR-0042 already established for golden comparison,
   holding here too.
2. **`shadowDarkening` histogram**, `darkening = rgbSum(shadowOff) -
   rgbSum(shadowOn)`, computed over all 6892 masked pixels, pre-fix:
   `min=0`, `p50=0`, `p90=222`, `p95=375`, `p97=408`, `p98=423`,
   `p99=441`, `p995=450`, `p999=471`, `max=486`. **Over half the masked
   pixels show `darkening == 0` exactly** (the sphere's own unlit-
   hemisphere pixels, where `NdotL <= 0` skips the directional-light
   loop — and, per P1's own determinism result, `darkening` is never a
   small nonzero rounding artifact at those pixels either).

Because (1) shows a raw, measured noise floor of exactly `0` for an
identical repeated capture, and because `computeShadowFactor()`'s own
manual comparison is binary (`shadowFactor` is exactly `0.0` or `1.0`,
never blended — no MSAA, no PCF, no soft edge in this codebase), a
`darkening` value strictly between `0` and any real, shadow-driven jump
does not occur here from noise alone. **The noise threshold is fixed at
`5`** — a small, deliberate safety margin above the measured `0` floor
(guarding only against a possible, not-yet-observed cross-run/cross-
driver jitter of a few `rgbSum` units), never chosen to make any
specific candidate's own count come out favorably: this value was fixed
*before* Step 4/5 below computed any baseline or ceiling from it, and
before Step 6 fixed any bias candidate.

### P3 — Pre-fix baseline and ceiling, per shader path (real GPU, threshold = 5)

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
baseline, at the identical threshold, over the identical 6892-pixel
mask, against the identical geometry/shadow map.** `computeShadowFactor()`
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
`darkening > 5` threshold more often than the control material's own,
generally smaller swing at the same underlying flipped-`shadowFactor`
locations. This is a real, disclosed, per-material measurement
consequence of a fixed absolute threshold — not evidence of two
different defects, and not a reason to use two different thresholds
(Spec 0030 fixes one threshold, applied uniformly).

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
`kShadowBiasSlopeScale` ascending — the smallest, least-invasive bias
combination is always tried before a larger one, so the accepted
candidate is the smallest satisfying one this fixed grid contains, not
an arbitrary later point.

**Fixed formula, identical in all four shaders** (already implemented
in the probe, mirroring Spec 0030's own Proposed Design verbatim):

```
float NdotLGeo = max(dot(N_geo, L), kMinDot);   // N_geo == N in pbr_direct_lit/pbr_ibl (no perturbed normal exists there)
float slope = sqrt(max(1.0 - NdotLGeo * NdotLGeo, 0.0)) / NdotLGeo;
float bias = clamp(kShadowBiasMin + kShadowBiasSlopeScale * slope, kShadowBiasMin, kShadowBiasMax);
return (shadowNdc.z - bias) <= storedDepth ? 1.0 : 0.0;
```

### P5 — Deterministic sweep: real, per-candidate results

Every candidate below was captured on real GPU hardware, all four
shader paths, in the fixed scan order — build the four shaders (a
`slangc` recompile only, no C++ rebuild — SPIR-V is loaded from disk at
test run time), then re-run the same, unmodified probe binary. The
probe's own driver script mechanically executed the entire fixed
24-candidate grid (it implements no early-stopping logic); rows 18-24
are therefore also real, captured measurements, but per Spec 0030's own
acceptance rule the accepted candidate is the **first** row in the
fixed scan order satisfying all conditions — row 17:

| # | `kShadowBiasSlopeScale` | `kShadowBiasMax` | `pbr_direct_lit_normal_map` acne (ceiling 10) | `pbr_direct_lit` acne (ceiling 1) | `pbr_ibl_normal_map` acne (ceiling 10) | `pbr_ibl` acne (ceiling 1) | Ground delta (non-env / env, need `>15`) | Pass? |
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
| **17** | **0.003** | **0.040** | **0** | **0** | **0** | **0** | **219 / 160** | **Yes — accepted** |
| 18 | 0.006 | 0.040 | 0 | 0 | 0 | 0 | 219 / 160 | (not needed) |
| 19 | 0.009 | 0.040 | 0 | 0 | 0 | 0 | 219 / 160 | (not needed) |
| 20 | 0.012 | 0.040 | 0 | 0 | 0 | 0 | 219 / 160 | (not needed) |
| 21 | 0.003 | 0.045 | 0 | 0 | 0 | 0 | 219 / 160 | (not needed) |
| 22 | 0.006 | 0.045 | 0 | 0 | 0 | 0 | 219 / 160 | (not needed) |
| 23 | 0.009 | 0.045 | 0 | 0 | 0 | 0 | 219 / 160 | (not needed) |
| 24 | 0.012 | 0.045 | 0 | 0 | 0 | 0 | 219 / 160 | (not needed) |

**Accepted candidate: `kShadowBiasMin = 0.0015`, `kShadowBiasSlopeScale
= 0.003`, `kShadowBiasMax = 0.040`** (`0.040 * 29.9 = 1.196` world units).
All four shader paths reach **exactly 0** acne pixels (a 100% reduction
from every path's own pre-fix baseline — comfortably inside every
ceiling), the ground cast-shadow delta is completely unaffected at both
`219` (no-environment paths) and `160` (environment paths) — byte-
identical to the pre-fix value in every single row of this table,
confirming no peter-panning at any tested candidate — and re-running
the existing `pbr_normal_map_demo_gpu_tests.cpp` `TEST_CASE`s against
this accepted candidate confirms both pre-existing discriminative tests
still pass (normal-map differential at `(256,256)`: `delta = 103 > 50`;
ground-shadow differential at `(198,273)`: `delta = 160 > 15`) and
Vulkan Validation Layers remain clean (Debug, zero `VUID`/Validation
Error/Warning).

**Disclosed trade-off:** `kShadowBiasMax = 0.040` is a real, sizable
world-space bias (`~1.2` world units) relative to `pbr_sphere`'s own
`~1`-unit radius — required because this scene's fixed, non-adaptive
`1024x1024` shadow map (`~0.0156` world units/texel over the `16`-unit-
wide orthographic volume) genuinely cannot resolve depth precisely
enough at strongly grazing incidence on a curved receiver to need a
smaller one; every candidate at `kShadowBiasMax <= 0.035` left a real,
measured acne residual (rows 1-16). This is a real, disclosed
consequence of the fixed-resolution, non-PCF shadow map ADR-0072 D-1/D-5
already accepted, not a defect in this Plan's own formula — and it is
the exact scenario Spec 0030's own Alternatives Considered (options 3/4,
Vulkan rasterization depth bias / normal-offset shadow mapping) remain
available follow-ons for, should a future scene with tighter geometry
show real peter-panning this Plan's own ground-shadow check does not
catch.

## Files / Modules Touched (expected)

- `shaders/pbr_direct_lit/pbr_direct_lit.slang`,
  `shaders/pbr_ibl/pbr_ibl.slang`,
  `shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.slang`,
  `shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.slang` — `kShadowBias`
  replaced by `kShadowBiasMin`/`kShadowBiasSlopeScale`/`kShadowBiasMax`;
  `computeShadowFactor()` gains an `NdotLGeo` parameter; each call site
  computes `NdotLGeo` from the already-local `N_geo`/`N`/`L` immediately
  before calling it. No binding, descriptor, uniform-buffer, or vertex-
  layout change in any of the four files.
- `tests/core/shadow_bias_formula_tests.cpp` (new) — the GPU-independent
  finiteness/range test (Milestone 1 below).
- `tests/image_regression/pbr_normal_map_demo_gpu_tests.cpp` (new
  `TEST_CASE`s) — the Sphere Coverage Mask + Paired Acne Metric,
  real-GPU, for all four shader paths; the ground cast-shadow
  discriminator re-run against the fixed bias; the IBL isolation
  control.
- `tests/image_regression/fixture/pbr_normal_map_demo_fixture.h/.cpp` —
  no functional change expected (the fixture's own existing
  `includeShadowCasters`/`useControlMaterial` toggles already cover
  everything Milestone 2's tests need); a deviation here is called out
  explicitly if Implementation finds otherwise.
- `tests/image_regression/goldens/pbr_normal_map_demo/*` — the old,
  untracked, pre-fix candidate is discarded; a fresh candidate PNG/
  sidecar is generated after the fix lands, per the golden strategy
  below.
- `specs/0029-tangent-space-normal-mapping-foundation.md`,
  `specs/README.md` — Spec 0029's own registry row updated once its
  Milestone 6-7 complete on the combined branch (Milestone 5 below).

## Sequencing & Dependencies

This Plan implements Spec 0030's own **Dependency and Integration
Sequencing** section verbatim — repeated here only as an operational
summary, not re-derived:

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

1. **Shader bias fix + GPU-independent finiteness test.** On the
   combined branch: apply the fixed formula with the accepted constants
   — `kShadowBiasMin = 0.0015`, `kShadowBiasSlopeScale = 0.003`,
   `kShadowBiasMax = 0.040` (P5) — identically to all four shaders; add
   `tests/core/shadow_bias_formula_tests.cpp` — a plain-C++ mirror of
   `slope`/`bias`:
   ```cpp
   float slope = std::sqrt(std::max(1.0f - ndotLGeo * ndotLGeo, 0.0f)) / ndotLGeo;
   float bias = std::clamp(kShadowBiasMin + kShadowBiasSlopeScale * slope, kShadowBiasMin, kShadowBiasMax);
   ```
   evaluated across a fixed input set covering the full approved
   `NdotLGeo` domain — `kMinDot` (`1e-4`, the floor every shader already
   applies before calling `computeShadowFactor()`), `0.001`, `0.01`,
   `0.05`, `0.1`, `0.2`, `0.3`, `0.5`, `0.7`, `0.9`, and `1.0` (exact
   perpendicular incidence) — asserting every result is finite
   (`std::isfinite`) and lies within
   `[kShadowBiasMin, kShadowBiasMax] = [0.0015, 0.040]`. Confirm all four
   `.slang` files declare byte-identical constants and formula text (the
   "exact twin" check, mirroring ADR-0074's own established discipline).
   Commit boundary: shaders + the one new GPU-independent test file,
   builds and passes `ctest -LE gpu` on its own.
2. **Real-GPU verification: mask, paired differential, ground, IBL
   isolation.** Add the Sphere Coverage Mask construction and the Paired
   Acne Metric as real `TEST_CASE`s in
   `tests/image_regression/pbr_normal_map_demo_gpu_tests.cpp` (or a new,
   sibling file if that grows unwieldy — Implementation's own call,
   disclosed if taken), covering all four shader paths (reusing the
   `environmentArtifactPath` empty/non-empty toggle this Plan's own
   probe already established reaches all four). Assert each path's own
   post-fix acne count against its own fixed ceiling (P3's table) and
   the `(198,273)`/`>15` ground discriminator. Re-run the existing IBL
   isolation control (R1/R2/R3, ADR-0072 D-7's own established pattern)
   against the fixed bias. Commit boundary: new/updated GPU tests only,
   `ctest -L gpu` green, Vulkan Validation Layers clean.
3. **Compare-first against every existing golden.** Run the full,
   unmodified capture-compare suite (all 9 committed goldens, plus
   `world_scene_loaded`/`sky_background` individually) unmodified. A
   byte-identical result needs no action. Any golden that shows a real
   difference is queued for its own human-reviewed re-capture
   (ADR-0042) — not presumed, not skipped. Commit boundary: this
   Milestone's own findings recorded in the PR description; no golden
   file changes land here unless a real difference is found and a
   separate re-capture commit is prepared.
4. **Fresh `pbr_normal_map_demo` candidate.** Discard the old, untracked
   candidate outright (never committed, never diffed against as a
   baseline). Re-run Spec 0029's own Milestone 5 candidate-generation
   step against the now-fixed shaders, producing a new, untracked
   candidate PNG/sidecar. Stop here and request Human Review of this
   candidate before proceeding — no commit in this Milestone touches the
   golden directory.
5. **Spec 0029 Milestone 6-7 closeout + final combined verification.**
   Once the fresh candidate is approved: commit the new golden (Spec
   0029 Milestone 6), update `specs/README.md` for both Spec 0029 (golden
   approved, Implementation complete) and Spec 0030 (Implementation
   complete), and run the full verification matrix below. Open the one,
   combined Implementation PR, base `main`.

## Verification Checklist

- [ ] Unit tests: `tests/core/shadow_bias_formula_tests.cpp` — finite,
      bounded `slope`/`bias` across the full `NdotLGeo` range and the
      accepted constants.
- [ ] Headless integration tests: existing headless/offscreen paths
      unaffected (no RHI/RenderGraph change).
- [ ] Image regression tests: Sphere Coverage Mask + Paired Acne Metric
      (all 4 shader paths, against each path's own fixed ceiling);
      ground cast-shadow discriminator (`(198,273)`/`>15`, all 4 paths);
      IBL isolation control; compare-first against all 9 existing
      goldens + `world_scene_loaded`/`sky_background`; fresh
      `pbr_normal_map_demo` candidate, human-reviewed and committed.
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

- The "Image regression tests added/updated" item is satisfied by both
  new real-GPU `TEST_CASE`s (Milestone 2) and the fresh, human-reviewed
  `pbr_normal_map_demo` golden (Milestone 4).
- The "approved plan exists and the implementation matches it" item
  requires the three fixed bias constants that land in the four shaders
  to be byte-identical to this Plan's own final, sweep-selected
  candidate (P5) — any deviation is a stop-and-report, not a silent
  substitution.
