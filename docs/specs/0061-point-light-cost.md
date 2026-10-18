# Spec: Point-Light Cost in the Forward PBR Path

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-18
- **Related Plan(s):** [Plan 0061](../plans/0061-point-light-cost.md) (`Draft`; implementation awaits its Joint Human
  Review).
- **Approval:** slmao, 2026-10-18 (owner rulings on Q1–Q5; review of this Spec's own branch PR,
  [PR #261](https://github.com/slmao/Atlantis/pull/261), merged). The rulings are recorded once in
  [Plan 0061](../plans/0061-point-light-cost.md); the open-question text below keeps the options as the record of what
  was weighed. Ruled scope: Stage 1 only.
- **Related ADR(s):**
  [ADR-0121](../adr/0121-point-light-association-model.md) (`Accepted` 2026-10-18): the point-light association model, i.e.
  which lights a fragment evaluates and in what order, the equivalence invariant any association must keep, and
  the conditions under which a precomputed association structure would be reopened. It supersedes
  [ADR-0088](../adr/0088-frame-lighting-data-successor-structure-and-binding-strategy.md) Decision 5 in part and
  extends [ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md) D-1's evaluation guard.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
State each requirement once; link ADR rationale and map verification to the
requirements rather than adding duplicate acceptance or review-decision lists.
Keep implementation code, diffs, review transcripts, and execution logs in PRs.

## Summary

The 59 point lights Bistro carries in the per-frame lighting uniform cost about 11 ms of the ~15 ms `draw` GPU time
on the reference machine ([Spec 0059](0059-profiling-instrumentation.md) acceptance, `no-point-lights` variant).
This Spec cuts that cost and leaves the rendered image unchanged, under an explicit equivalence contract.
The work runs in three stages:

1. **Investigation.** Using the existing benchmark, its variants and uncommitted probe builds, decide whether the
   cost comes from ALU work (BRDF terms that come out exactly zero) or from per-light fetch and loop overhead.
2. **Stage 1.** Recommended here, whatever the investigation finds. Reject out-of-range lights inside the existing
   loop, using the same attenuation value the shader already computes. The goldens stay byte-identical by
   construction, and no data flow, binding or counter changes.
3. **Stage 2, only if triggered.** If a measured residual is too large, add a precomputed light-association
   structure. That needs a revision of this Spec and its own ADR.

The [Performance Contract](0060-performance-contract.md) is the evidence standard throughout.

## Motivation / Problem Statement

**The measured cost.** Two measurement records on the reference machine (Intel Core Ultra 5 338H, Intel Arc B370
iGPU, driver 101.8509, 1920×1080, build config B):

| Signal | Spec 0059 acceptance (`baseline` → `no-point-lights`) | Committed baseline ([PR #258](https://github.com/slmao/Atlantis/pull/258), `baseline`, 15 runs) |
|---|---|---|
| `passes.draw.gpu_ms` | ~15.4 → ~4.4 ms (−72 %) | 16.16 ms center (`Opaque` 15.71, range 13.89–17.80) |
| `gpu_busy_ms` | | 20.04 ms center (18.10–22.07) |
| `frame_ms` p50 | ~28 → ~18–21 ms | 30.16 ms center |
| `cpu_active_ms` | | 9.46 ms center |
| `fragment_invocations` | | 5,928,192 = 2.86 per pixel |
| `lights_point` | 59 → 0 | 59 |

Lighting is the dominant, well-isolated GPU cost. Under the current CPU/GPU serialization (the in-`submit()` fence
wait Spec 0059 named), frame time is roughly `cpu_active_ms + gpu_busy_ms` (9.46 + 20.04 ≈ 29.5 against 30.16).
So every millisecond taken off the GPU comes off the frame.

**Why the cost exists.** The cited facts are in the Pre-drafting Investigation below. In short: the point-light loop
evaluates the full Cook-Torrance BRDF for every light whose `NdotL > 0`, and only afterwards multiplies by
`clamp(1 − d/range, 0, 1)`. Bistro's lights are short-range (2–6 m) in a scene about 78 × 76 m across, so almost
every one of those BRDF evaluations is multiplied by exactly zero.

**Why now.** Spec 0060 built the contract specifically so that this optimization, the first one, could be judged
under it (Spec 0060 Q10: "the first optimization Spec's expected effect … is far above the band").

**Corrections to the tasking's premises** (recorded so that review reads from the code, not the brief):

- Not all ten PBR shaders run the light loop. **Four** do, plus `lit_textured`: `pbr_direct_lit`,
  `pbr_direct_lit_normal_map`, `pbr_ibl` and `pbr_ibl_normal_map`. The six clearcoat, sheen and anisotropic shaders
  declare the `CameraUniform` block but contain no point-light loop. They are IBL-only by Spec 0035's scope.
- Spec 0043 is height fog. The PBR shaders come from Specs 0023, 0025 and 0029, and their point-light loop from
  Spec 0040 / ADR-0088.
- The glTF importer realizes every Bistro material as `MaterialKind::PbrDirectLit`
  (`src/tools/gltf_importer/material_import.cpp:356`). Bistro therefore shades through the four looping PBR shaders.

## Pre-drafting Investigation (cited against real files)

### Inv-1: the loop and where its work goes

`shaders/pbr_ibl/pbr_ibl.slang` (the other three are line-for-line the same) runs
`for (uint j = 0; j < camera.pointLightCount; ++j)`. Per light it first does a fixed preamble: two 16-byte uniform
loads, the vector to the light, `length`, `max` against `kPointLightDistanceEpsilon`, normalize, `dot` and `max`.
Then, under `if (NdotL > 0.0)`, it does the attenuation divide and clamp, the radiance, `H`, `NdotH`, `VdotH`,
`D` (with a `pow`), `G`, `F` (with a `pow`), specular, `kD` and the accumulate. That is roughly 15 operations of
preamble and 70–80 inside the branch. The branch exists for NaN safety (ADR-0067 D-1: "branch, not a trailing
multiply"). It does not test range.

### Inv-2: Bistro's light set

`assets/bistro/bistro_overlay.scene.txt` holds 59 point lights:

| Lights | Range | Height (y) | Reaches the ground (y = 0) |
|---|---|---|---|
| 28 | 6 m | 3.3–4.5 m | yes, a disc of radius ≈ 4.1 m each |
| 6 | 2.5 m | 2.54 m | no |
| 5 | 3 m | 3.4–3.65 m | no |
| 20 | 2 m | 2.5–5.4 m | no |

The lights span x ∈ [−39.6, 38.3] and z ∈ [−29.5, 46.4].

Sampling planes on a 0.25 m grid over that footprint (±10 m) gives the number of lights in range of a point:

| Plane | Max lights in range | Mean | Share of points with ≥ 1 |
|---|---|---|---|
| y = 0 (ground) | 3 | 0.17 | 14 % |
| y = 1.5 | 4 | 0.29 | 21 % |
| y = 3.0 | 5 | 0.37 | 23 % |

All 59 lights are above the ground, so an upward-facing ground fragment passes `NdotL > 0` for every one of them.
It evaluates 59 BRDFs, of which **at least 56 contribute exactly zero**. A wall fragment faces roughly half the lights
and has at most about 5 in range. This analytic bound is what Stage 1 is built on. The measured share is the
investigation's job (Phase 1).

### Inv-3: DRAM bandwidth cannot be the limit

If every fragment fetched every light from memory, the frame would read 5.93 M fragment invocations × 59 lights ×
32 B ≈ 11.2 GB. Over the ~11 ms the lights cost, that is ≈ 1 TB/s, several times the shared LPDDR bandwidth of an
iGPU. So the light data is served on-chip (constant cache, L1 or L3). The per-frame upload is negligible as well
(`uniform_write_bytes` = 2,672 per frame).

The hypothesis the investigation must test is therefore **on-chip per-light fetch and loop issue** against **ALU in
zero-contribution BRDF evaluations**, not DRAM traffic. The degraded-GPU-state analysis (Spec 0060 third and sixth
Corrections) found main-pass-only, bandwidth-shaped shifts. The main pass also does all the texture sampling and
framebuffer traffic, so that finding alone does not implicate the light loop. Phase 1's experiment I-6 is how the
existing variants separate the two.

### Inv-4: occupancy is not the explanation for the variant delta

The `no-point-lights` variant removes the `Light` components (Plan 0059 J3). It runs **the same pipelines** with
`pointLightCount = 0`, so register allocation and occupancy are identical in both variants. The ~11 ms delta is
therefore the dynamic cost of iterating the loop, not a compile-time side effect. Fragment counts are also identical
across the variants: lighting changes no geometry.

### Inv-5: the instruments and their limits

- The GPU tier of the gate is **report-only** (Spec 0060 sixth Correction): GPU times appear in a verdict's
  `reported` section and never decide it. The gated tiers are counters (exact) and CPU (5 % / 0.10 ms floor;
  `cpu_active_ms` 9.46 ms center).
- `--gate` judges one `(scene, variant, resolution)` against the committed store, and only `bistro/baseline/1920x1080`
  has a baseline. A `--variant no-point-lights` gate returns `incomparable`. Its document carries no metrics
  (`notJudged`, `src/runtime/gate_evaluate.cpp:218`), so it cannot supply the variant's numbers.
- The reference machine's main-pass GPU state is bimodal (baseline runs `Opaque` 12.3–12.6 against 13.2–15.3 ms), so
  measurements taken across sessions are confounded. Interleaved A/B runs within one session (named as the GPU
  escalation in Spec 0060 Q10) are the method that separates a code effect from a machine-state shift.
- `--benchmark --variant`, `--resolution`, `--json` and `--series` exist and are enough for everything Phase 1 needs.
  No new variant or tool is required.

### Inv-6: what precomputed association structures would touch

These facts bound the alternatives. Stage 1 needs none of this.

- **Per-draw data.** `DrawItem` is `{mesh, material, objectToWorld}`, and Spec 0023 D10 ("never a new DrawItem field",
  reaffirmed at `src/renderer/src/renderer.cpp:277`) forbids growing it without a superseding decision. `Mesh` stores
  only `localBoundsCentre` (ADR-0090), no extents.
- **Push constants.** `PbrPushConstants` is 112 B with an explicit 8-byte `_pad` at offset 88, enough room for a
  64-bit mask (`kMaxPointLights` = 64). `PbrSheenPushConstants` is already **128 B**, the Vulkan-guaranteed
  `maxPushConstantsSize`; clearcoat and anisotropic are 112 B.
- **Compute.** The RHI has no compute dispatch (`src/rhi/include` has no dispatch or compute surface). A compute-built
  tiled or clustered list would need a new RHI capability, its own ADR and a RenderGraph pass.
- **Per-frame CPU budget.** A per-draw light test costs 2,541 draws × 59 lights ≈ 150 k sphere-against-bounds tests
  per frame. That lands in `zones.RenderExtraction` (4.26 ms center), whose gated tolerance is about 0.21 ms.

## Goals

- Reduce the point-light share of the main-pass GPU time on Bistro by a stated, measured amount. Targets are under
  Requirements R5.
- Keep the rendered image **byte-identical** under an explicit, checkable equivalence contract (R2).
- Answer, with numbers, whether the light loop is ALU-bound or fetch/loop-bound, before any technique code is written
  (R1).
- Leave every gated tier of the Performance Contract non-regressed on every PR, and judge the work by the contract's
  reported GPU numbers and the `no-point-lights` variant delta (R6).
- Record the light-association model as a reviewed decision (ADR-0121) rather than as a shader edit nobody decided.

## Non-Goals

- **GPU-driven scene, GPU culling, indirect draw, renderer rewrite, SSAO/SSR/TAA/RT.** These remain exclusions, as in
  the instrumentation tasking.
- **Geometry culling, depth prepass, draw reordering for early-Z, job systems.** Each would cut lighting cost
  indirectly, through the 2.86 fragments per pixel. They are geometry or pass-structure decisions, not
  light-association ones, and change counters (`draw_calls`, `triangles`). See Out of Scope.
- **A new attenuation model or physical light units.** Spec 0040 O5 reassigned that question to a future photometric
  Spec. This Spec keeps `clamp(1 − d/range, 0, 1)` bit for bit, and the equivalence contract depends on it.
- **Raising `kMaxPointLights` or changing `FrameLightingData`'s layout or binding** (ADR-0088 D1–D4, D6 stand).
- **The six IBL-only BRDF-extension shaders** and **`lit_textured`**. Neither is on Bistro's path. `lit_textured`'s
  loop is mirrored line for line on the CPU (`computeLambertianDiffuse`, `src/runtime/src/scene_extraction.cpp:402`),
  and touching it would buy nothing measurable.
- **Performance Contract machinery.** No new variant, no policy change, no gate change. The applicability list already
  covers `shaders/**` and `src/renderer/**`.
- **Recording or refreshing any performance baseline or golden** as part of the technique PRs (see R6 and Q4).

## Requirements

### Functional

1. **R1: Investigation precedes technique.** Phase 1 runs the experiments under Proposed Design and reports their
   numbers before any Stage 1 code is written. It uses only the existing benchmark, variants, `--resolution` and gate,
   plus uncommitted probe builds that are never merged. Its report states which row of the decision table applies,
   and the owner checkpoints it before Stage 1 starts.
2. **R2: Equivalence contract.** For every fragment of every draw, the lit result is identical to the pre-change
   shader's under the following invariant:
   - **(a) Superset.** The set of point lights whose BRDF term is evaluated contains every light `j` for which the
     existing expressions give `NdotL_j > 0` **and** `atten_j > 0`.
   - **(b) Order.** Evaluated lights are accumulated in ascending `j`.
   - **(c) Same expression.** The per-light expression for an evaluated light (ADR-0067 D-1 with ADR-0062's
     attenuation) is unchanged in source.
   - **(d) Same predicate.** The rejection test is `atten > 0.0` on the **same** `atten` value the expression uses.
     A cheaper predicate such as `dist < range` is allowed only if it is proven conservative under Vulkan's precision
     rules, since division is not correctly rounded on the device (Vulkan allows 2.5 ULP). Stage 1 uses the same
     value.

   The proof that (a)–(d) leave the IEEE-754 result unchanged is under Proposed Design → Equivalence argument.
   **Outcome A, the contract:** all 67×2 golden comparisons (Debug and Release) stay byte-identical, and no file under
   `tests/image_regression/goldens/` is touched. **Outcome B, the only permitted fallback:** if, and only if, a moved
   golden is shown to be driver code-generation drift rather than an association bug (diagnosis procedure under
   Testing, T2), a **dedicated** golden-update PR may follow, under ADR-0042 category 1 (rendering change). It must
   name the change ("compiler-level rounding difference from restructuring the point-light loop of the four PBR
   shaders; no change in which lights contribute"), report per golden the max and mean channel delta and the count of
   changed pixels with diff images, and keep **max |Δ| ≤ 1 of 255** on every channel. A larger delta is a defect to
   fix, never a golden to update.
3. **R3: Stage 1 technique.** In the point-light loop of `pbr_direct_lit`, `pbr_direct_lit_normal_map`, `pbr_ibl` and
   `pbr_ibl_normal_map`, compute `atten` with its existing expression and evaluate the BRDF only when `NdotL > 0.0`
   and `atten > 0.0`. Nothing else in these shaders changes: the directional loop, IBL, emissive, fog and the order of
   terms stay as they are. The four loops stay identical to one another, keeping the "loops intentionally match"
   convention.
4. **R4: No data-flow change in Stage 1.** `FrameLightingData`, the camera uniform layout, push-constant layouts,
   descriptor bindings, `DrawItem`, `Mesh`, Runtime extraction and the RenderGraph are untouched. No new pass, buffer
   or binding. Every Performance Contract counter is therefore expected to be **exactly unchanged**, and no baseline
   refresh is needed.
5. **R5: Measured effect, stated up front and reported against.** Let `L` be the lighting cost:
   `passes.draw.gpu_ms(baseline) − passes.draw.gpu_ms(no-point-lights)`, measured interleaved within one session on
   the same build (≈ 11 ms before this work).
   - **Target** for this Spec's work (Stage 1, plus Stage 2 if it is triggered and approved): **reduce `L` by
     ≥ 50 %**, from ≈ 11 to ≤ 5.5 ms at 1920×1080 on the reference machine.
   - **Expectation** for Stage 1 alone: 60–75 % (residual ≈ 3–4.5 ms), from Inv-1/Inv-2's operation counts if
     Phase 1 finds the loop ALU-bound.
   - **Control:** `passes.draw.gpu_ms(no-point-lights)` does not change beyond its interleaved noise, so Stage 1 adds
     no fixed cost.
   - **Downstream (reported, not targets):** `gpu_busy_ms` and `frame_ms` p50. Under the current serialization they
     are expected to fall by about the `L` reduction (Stage 1 expectation: frame p50 ≈ 22–24 ms).

   The final PR reports the numbers against the target honestly, including a miss.
6. **R6: Contract verdicts on every technique PR.**
   - `atlantis_runtime --gate --json` on the reference profile must return `pass`: counters exact, CPU non-regressed,
     with the GPU improvement listed under `reported`.
   - An `inconclusive` result is re-run, and stated with its verdict if it persists.
   - Any intentional counter change is not part of a technique PR. It goes through the ADR-0118 baseline-refresh flow:
     a dedicated, human-instructed PR naming its reason category. Stage 1 expects none (R4).
   - Policy numbers in `gate_policy.json` are never edited.
7. **R7: Stage 2 trigger.** Stage 2 is drafted only when **both** hold:
   - the measured residual `L` after Stage 1 exceeds **25 % of the pre-work `L`** (≈ 2.75 ms), **or** Phase 1 lands
     in decision-table row B;
   - the owner directs it.

   Stage 2 is drafted as a revision of this Spec with its own ADR, since every candidate structure touches a surface
   ADR-0121 holds closed (Inv-6). It must keep R2's invariant, which ADR-0121 makes binding on any association
   structure.

### Non-functional

- **Performance:**
  - GPU: R5.
  - CPU: no change in Stage 1, since the change is in shaders only.
  - Counters: exactly unchanged (R4).
- **Memory:** no change.
- **Portability (within the Vulkan-only Phase 1 constraint):**
  - The change is shader-only and uses no extension, no wave intrinsics and no feature bit. It is valid on every
    conformant device, Android included. Android builds the shaders and runs no benchmark.
  - The equivalence argument rests on IEEE semantics that hold for any conformant implementation, not on this driver.
  - The measured effect is one machine's. Another hardware profile must measure its own.
- **Threading and module rules:** single frame thread (ADR-0004); no new GPU work, so no RenderGraph change;
  Platform/Renderer boundaries unchanged.

## Proposed Design

### Phase 1: investigation

The investigation runs on a local, uncommitted branch. It uses build config B (profiling on, Tracy off), Release,
Bistro at 1920×1080 with the default `--warmup 120 --frames 300`, mains power and an idle machine.

"**Interleaved**" means alternating independent `--benchmark --json` processes (A B A B …), at least 5 per arm, in
one session. Each arm is reported as the median and range of run-level p50s. Every probe build changes **only** the
point-light loop body of the four PBR shaders and is compared against the unmodified build in the same session.

| # | Experiment | What it measures | Reading |
|---|---|---|---|
| I-1 | `baseline` vs `no-point-lights`, interleaved; also `fragment_invocations`, `lights_point` | `L` on this machine and day | Re-establishes the ≈ 11 ms the target is stated against; fragment counts must match (Inv-4). |
| I-2 | Resolution sweep, 1280×720 / 1920×1080 / 2560×1440, both variants | `L` against `fragment_invocations` | Confirms `L` is per-fragment shading cost. Yields cost per fragment-light (`L ÷ (fragments × 59)`), checked against the device's nominal FP32 rate and the operation counts of Inv-1. |
| I-3 | Probe **P1, range-reject**: R3's change, built locally | Share of `L` spent in BRDF evaluations that contribute exactly 0 | The prototype of Stage 1, measured before it is proposed for merge. |
| I-4 | Probe **P2, fetch-and-loop floor**: the loop keeps both uniform loads and the preamble, and folds the loaded values into a term no compiler can remove but that is never taken (a data-dependent impossible branch), with no BRDF | Per-light fetch and loop cost with almost no ALU | Separates fetch/loop issue from BRDF ALU: P1 − P2 is the residual BRDF work; P2 is what only a list structure can remove. |
| I-5 | CPU-only analysis of the benchmark frame (throwaway script or test, not committed): for each `DrawItem`, the lights whose sphere meets its world bounds; for a 16×9×N froxel grid on the benchmark camera, the lights per froxel; both weighted by triangle count as a proxy for coverage | How much a per-draw list and a clustered list would cut the lights each fragment iterates, over and above Stage 1 | Chooses between Stage 2 candidates if Stage 2 is ever triggered. Large meshes (ground, façades) are expected to keep many lights in a per-draw list. |
| I-6 | Opportunistic: across the interleaved sessions of I-1, classify runs as in the fast or slow main-pass state (Spec 0060's bands) and compare each variant's absolute shift | Whether the degraded state's slowdown lives in the light loop | Equal shifts in both variants mean the state slows non-lighting work (textures, framebuffer), not the loop. A shift that appears only in `baseline` would mean the loop is sensitive to whatever the state throttles. Not required to conclude; reported if the state occurs. |

Optional corroboration, with no dependency added and nothing committed: a vendor GPU profiler's EU-stall breakdown
of the main pass on the reference machine. Report it if used.

**Decision table.** The thresholds are fixed here, before measurement:

| Row | Phase 1 finding | Meaning | Consequence |
|---|---|---|---|
| A | P1 removes ≥ 50 % of `L` | ALU-bound in zero-contribution evaluations | Stage 1 as specified. Stage 2 only if R7's residual trigger fires after Stage 1. |
| B | P1 removes < 50 % of `L` **and** P2 accounts for most of the remainder | Per-light fetch/loop-bound | Stage 1 still lands (it is never worse: Inv-1's preamble is unchanged, plus one compare). Stage 2 is triggered under R7, and I-5 informs the structure. |
| C | P1 and P2 together account for little of `L` | The cost is not where this Spec looks | Stop and report. The Spec returns to review before any code. |

### Stage 1: exact range rejection in the existing loop

```
// per point light j, in the four PBR shaders (normative shape; names as in the shader)
float atten = clamp(1.0 - dist / camera.pointLights[j].range, 0.0, 1.0);   // existing expression, hoisted
if (NdotL > 0.0 && atten > 0.0) {
    float3 radiance = camera.pointLights[j].color * camera.pointLights[j].intensity * atten;
    ... the existing BRDF and accumulate, unchanged ...
}
```

**Equivalence argument** (why R2 (a)–(d) give the identical IEEE-754 result):

- For a skipped light, `atten = +0` exactly.
- Light values are finite and non-negative: `world::Light`'s documented contract (`src/world/include/atlantis/world/light.h`),
  enforced at load by the scene gates (`src/asset_system/src/scene_source.cpp:388-413`) and at run time by the
  operation boundary's finiteness check (`src/world/src/access/property_access.cpp:109,142`). So
  `radiance = (color · intensity) · 0 = +0`. If a value were non-finite, the old code would turn every out-of-range
  light into NaN; that is the only case in which the outputs could differ.
- ADR-0067's clamps keep every other factor finite and non-negative: `α ≥ 10⁻³` bounds `D`'s denominator away from 0,
  the specular denominator is `max(…, kMinDot)`, and `kD`, `diffuseColor`, `NdotL` and `F` are all ≥ 0.
- So the skipped term the old code would have added is exactly `+0`.
- `accumulated` starts at `+0` and only ever receives non-negative terms, so it is never `−0`. For such `x`,
  `x + (+0) = x` exactly.
- Evaluated lights are added in the same ascending order through the same expression.

The result is therefore identical at the level of the language's floating-point semantics.

**What the argument does not cover.** SPIR-V lets a driver contract or reassociate operations not decorated
`NoContraction`. Restructuring the loop could, in principle, change instruction selection inside the evaluated
branch. The goldens (channel tolerance 0, ADR-0042) are the empirical check of that, and R2 Outcome B is the
pre-declared path if it happens.

**Expected behaviour on the GPU.** A wave skips the branch only when all of its lanes reject. Bistro's light
footprints (2–6 m) are large relative to a 16- or 32-pixel wave at 1080p, so divergence is expected to be confined
to range boundaries. Phase 1's P1 measures the real effect, which is why the target is stated as a reduction of `L`
rather than as an operation count.

### Stage 2: precomputed association (conditional; not specified here)

Stage 2 is named so that its trigger and constraints are on the record. It is designed only by a revision of this
Spec with its own ADR, when R7 fires.

The candidates, which I-5 discriminates, are a per-draw 64-bit light mask built during extraction, and a CPU-built
clustered (froxel) light list. Both must keep R2's invariant: conservative bounds with a margin that absorbs
interpolation and device division precision, since an extra light costs only time, and ascending-index iteration.
A mask iterated low bit first is ascending by construction.

## Architectural Impact

**Yes.** There is one decision, recorded in [ADR-0121](../adr/0121-point-light-association-model.md) (`Accepted` 2026-10-18):
**the point-light association model.** Which lights a fragment evaluates is decided per fragment, inside the existing
loop, by exact range rejection on the shader's own attenuation value. The ADR also does three other things:

- It makes R2's equivalence invariant binding on any future association structure.
- It records that no precomputed structure is adopted, together with the measured conditions that reopen one.
- It records that this is a lighting data-flow decision with **no** data-flow change: no new pass, buffer, binding,
  per-draw field or push-constant field.

Relationship to existing decisions:

- **ADR-0088 Decision 5** ("shader-side iteration cost is unchanged … the five iterating ones change no logic") and
  Spec 0040's Non-Goal of "dynamic per-frame light culling of any kind" are superseded **for the four PBR shaders'
  per-fragment evaluation only**. ADR-0088 D1–D4 and D6 (structure, capacity, binding, the single constant, the
  derived buffer size) stand.
- **ADR-0067 D-1**: the BRDF formula and ADR-0062's attenuation are unchanged. The evaluation guard
  `if (NdotL > 0.0)` is extended with `atten > 0.0`. Its NaN-safety rationale is kept and strengthened.
- Not touched: ADR-0062's stage visibility, the push-constant ADRs (0067, 0081, 0089), Spec 0023 D10 (`DrawItem`),
  ADR-0090 (`Mesh` bounds, draw order), ADR-0004 (threading), and the RHI. Stage 2 would touch some of them, which is
  why it needs its own ADR.

No module boundary, dependency, public API, threading model or ownership model changes.

## Alternatives Considered

| | **(C) Range rejection in the loop: recommended, Stage 1** | **(P) Per-draw light mask, CPU-built in extraction** | **(K) Clustered/froxel list, CPU-built** | **(G) Tiled/clustered list, compute-built** |
|---|---|---|---|---|
| Removes | Zero-contribution BRDF ALU | The per-light preamble for lights far from the whole draw, plus (C)'s saving if combined | The preamble for lights outside the fragment's cluster | Same as (K) |
| Granularity | Per fragment (exact) | Per draw: large meshes such as the ground and façades keep many lights | Per froxel | Per tile or froxel |
| New data / bindings | None | Mask in the push constants (fits `PbrPushConstants._pad`, but sheen is already at the 128 B guarantee); a `DrawItem` field (against Spec 0023 D10); `Mesh` extents | A second uniform or storage binding in four shaders, about 14–27 KB uploaded per frame | Storage buffers, **RHI compute dispatch (absent)**, a RenderGraph pass |
| Counters | Unchanged | Unchanged (`push_constants` counts calls) | `uniform_write_bytes` and `descriptor_writes` change, so a baseline refresh is needed | New pass, new counters, refresh needed |
| CPU (gated tier) | None | ≈ 150 k tests per frame in `RenderExtraction` (gated tolerance ≈ 0.21 ms); needs SIMD or caching | 59 lights × froxels touched; small | None |
| Shader | One predicate | Bit-iteration loop | Froxel index from depth, then non-uniform UBO indexing across a wave at froxel boundaries | As (K) |
| Equivalence (R2) | By construction | Needs conservative bounds and margins | Needs conservative bounds and margins | Needs conservative bounds and margins |
| ADR surface | ADR-0121 only | + `DrawItem`/`Mesh` API, push-constant contracts | + binding set and upload path | + RHI capability, RenderGraph pass |
| Size and risk | Smallest; trivially reversible | Medium | Large | Largest |

**Recommendation: (C) as Stage 1, whatever Phase 1 finds**, because it is never worse and is exact by construction.
(P) and (K) are deferred to a conditional Stage 2, decided by I-5's numbers, and (G) is rejected for this Spec.

Of the deferred candidates, (P) composes with (C) and keeps counters exact, but its granularity is worst exactly where
Bistro's pixels are. (K) has the better granularity at the cost of a binding and a baseline refresh.

(G) buys GPU-side building that 59 lights do not need, at the cost of a new RHI capability and a pass. It is reopened
only by a light count or a light-assignment cost that the CPU cannot carry.

Other alternatives:

- **Sort or prioritize lights on the CPU and upload the top N per frame.** This changes which lights contribute, so
  it is not equivalence-preserving. Rejected.
- **Bake static lighting** for Bistro's static lights. Rejected: it is an image change and a different feature.
- **Depth prepass or front-to-back opaque order.** This would cut shaded fragments from 2.86 per pixel toward 1, and
  `L` with them, but it is a pass-structure and geometry decision that changes `draw_calls`/`triangles`. It is outside
  this Spec's subject and named in Out of Scope.
- **Lower `kMaxPointLights` or shorten ranges.** Rejected: both change content and image.

## Testing & Verification Plan

Each item maps to a requirement. Verification follows [testing-strategy.md](../process/testing-strategy.md) and the
Definition of Done.

- **T1: Phase 1 report (R1, R5).** I-1 to I-5, plus I-6 if the state occurs, with commands, build revision, session
  layout, per-arm medians and ranges, and the decision-table row with its reasoning. Checkpointed by the owner before
  Stage 1 code.
- **T2: Goldens (R2).**
  - Debug and Release full suites, all 67×2 golden comparisons byte-identical. This is Outcome A, and the PR touches
    no golden file.
  - If any golden moves, stop. Build a **control**: the Stage 1 shader with the predicate forced true
    (`atten > 0.0 || true`), i.e. the restructured loop that evaluates every facing light.
    - If the control reproduces the same moved pixels, the movement is code-generation drift: proceed only by
      Outcome B, in a dedicated PR, with the named difference and the bound.
    - If the control matches the golden while Stage 1 does not, R2 (a) is violated: fix the bug and never re-capture.
  - `bistro_demo` (59 lights) and `multi_light_demo` (≥ 8 lights) are the comparisons that exercise rejection. The
    zero- and one-light scenes guard against collateral change.
- **T3: Unit tests (R2, R3).** A GPU-independent CPU transcription of the four shaders' point-light loop, before and
  after, run over randomized fragments and Bistro's light set. It asserts bitwise-equal accumulation, and that every
  skipped light's old-code term is exactly `+0.0`. This is the CPU-side proof that complements T2 (not a shader test:
  the shaders are verified by T2). Its placement (beside the existing `computeLambertianDiffuse` mirror tests in
  `tests/runtime/`, or with the image-regression support code) is the Plan's choice.
- **T4: Performance evidence (R5, R6).**
  - The final technique PR reports its `--gate --json` verdict: `pass`, counters identical, CPU within tolerance, GPU
    under `reported`.
  - It also reports interleaved before/after measurements in one session of `baseline` and `no-point-lights`:
    `passes.draw.gpu_ms`, `passes.Opaque.gpu_ms`, `gpu_busy_ms`, `frame_ms` p50, `cpu_active_ms` and every counter,
    with `L` before and after against the ≥ 50 % target and the 60–75 % expectation.
  - The `no-point-lights` control must show no change beyond noise.
- **T5: Validation Layers.** The fatal-VVL GPU suites are clean in Debug, including one Debug Bistro benchmark run
  with validation on.
- **T6: Dual platform.** Windows Debug and Release builds plus `ctest`. Android `assembleDebug` green, since the
  shaders compile there too.
- **T7: Diff review.** Only the four shaders' loop bodies, plus T3's test, change in Stage 1. No layout, binding,
  `DrawItem`, extraction or contract file changes.

## Risks & Open Questions

**Risks.**

- **R-1: driver code generation moves a golden** (Proposed Design, "What the argument does not cover"). Mitigation:
  T2's control isolates the cause, and R2 Outcome B is narrow and bounded.
- **R-2: wave divergence eats the saving.** If Bistro's in-range lanes are scattered, waves still run the branch.
  Phase 1's P1 measures exactly this before the target is reported against.
- **R-3: the measurement is one bimodal iGPU.** Interleaved arms in one session control for the state. The
  ≥ 5.5 ms target is above the observed state spread (`Opaque` ≈ 3 ms).
- **R-4: a future attenuation change breaks the exact-zero premise.** A smooth falloff without a hard zero (such as
  inverse square) would leave nothing for the predicate to reject. ADR-0121 records that the model depends on a
  finite-range falloff, so the photometric Spec has to revisit it.

**Open questions for the owner.**

- **Q1: Is Stage 1 alone an acceptable scope for this Spec?** The alternative is to authorize Stage 2 up front.
  - (a) Stage 1, with Stage 2 by a revision when R7 fires;
  - (b) authorize (P) or (K) now, to be chosen by I-5.
  - **Recommend (a).** Stage 2's cost and API surface are only worth paying against a measured residual.
- **Q2: The thresholds** are the ≥ 50 % target, R7's 25 % residual trigger and the decision table's 50 % row
  boundary. They are fixed before measurement on purpose. Are they the right numbers?
- **Q3: R2 Outcome B.** Is a bounded (max |Δ| ≤ 1), category-1 golden update for proven code-generation drift
  acceptable? Or should any golden movement send the change back for a code-level fix (for example, forcing the
  structure the compiler emits)?
- **Q4: Baselines after merge.**
  - Should a dedicated category-1 refresh of `bistro/baseline` follow the final PR, so that the reported GPU reference
    reflects the new cost? Counters are unchanged, so it is not required.
  - Should a `no-point-lights` baseline be recorded (Spec 0060 Q3(c), "a one-file addition once the first
    optimization lands")?
  - Each is a separate, human-instructed PR. Neither is made by an agent unprompted.
- **Q5: `lit_textured`.** Left untouched (Non-Goals). Should its loop take the same predicate for consistency, at the
  cost of its CPU mirror no longer being a line-for-line transcription?

## Out of Scope / Future Work

- **Stage 2 light-association structures** (P), (K) and (G), under R7 and ADR-0121's reopening conditions.
- **Overdraw reduction:** a depth prepass, or front-to-back opaque ordering. 2.86 shaded fragments per pixel
  multiply every per-fragment cost, lighting included. This needs its own Spec, since it changes pass structure and
  counters.
- **The CPU/GPU serialization** (the in-`submit()` fence wait), which makes frame time ≈ CPU + GPU.
- **A photometric Spec** (Spec 0040 O5). It must account for ADR-0121's dependence on a finite-range falloff.
- **A GPU main-pass state control**, so the GPU tier can gate again (Spec 0060 sixth Correction).
