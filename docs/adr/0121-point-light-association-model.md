# ADR 0121: Point-Light Association Model

- **Status:** Accepted
- **Date:** 2026-10-18 (accepted 2026-10-18)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-18 (owner rulings on Spec 0061 Q1–Q5, recorded once in
  [Plan 0061](../plans/0061-point-light-cost.md); the ADR text was reviewed in
  [PR #261](https://github.com/slmao/Atlantis/pull/261), merged).
- **Related Spec:** [Spec 0061](../specs/0061-point-light-cost.md) (`Approved` 2026-10-18)
- **Related ADR(s):**
  - [ADR-0088](0088-frame-lighting-data-successor-structure-and-binding-strategy.md): `FrameLightingData`'s
    structure, capacity and binding. **Decision 5 ("shader-side iteration cost is unchanged … the five iterating ones
    change no logic") is superseded here for the four PBR shaders' per-fragment evaluation only.** D1–D4 and D6
    stand. So does Spec 0040's Non-Goal against per-frame light *culling*, in the sense it was written: no light is
    dropped from the upload, and no light-association structure is built.
  - [ADR-0067](0067-pbr-direct-lighting-brdf-and-push-constant-contract.md) D-1: the BRDF and its `NdotL > 0`
    evaluation guard. The formula is unchanged. The guard is extended (Decision 1).
  - [ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md): the linear finite-range
    attenuation `clamp(1 − d/range, 0, 1)`, unchanged, on which Decision 1 depends.
  - [ADR-0042](0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md): golden comparison
    (tolerance 0) and the golden-update reasons that Decision 2's fallback uses.
  - [ADR-0118](0118-performance-contract.md): the evidence standard. Unchanged.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

Every fragment drawn by the four looping PBR shaders (`pbr_direct_lit`, `pbr_direct_lit_normal_map`, `pbr_ibl`,
`pbr_ibl_normal_map`) iterates all `pointLightCount` lights in the frame uniform. It evaluates the full Cook-Torrance
BRDF for each light with `NdotL > 0`, and multiplies by the attenuation only afterwards.

On Bistro (59 lights, ranges 2–6 m, a scene about 78 × 76 m), the measured cost is about 11 of ~15 ms of `draw` GPU
time (Spec 0059 acceptance, `no-point-lights` variant). A ground point is within range of at most 3 lights (mean
0.17), while all 59 face it (Spec 0061 Inv-2). Nearly all of that BRDF work is multiplied by exactly zero.

No decision so far says *which* lights a fragment evaluates. ADR-0088 fixed what is uploaded and stated that the
loops would change no logic. That statement was about Plan 0040's scope, not a performance property anyone chose.

The decision is architectural because it fixes the **light-association model**: where the "which lights affect this
fragment" question is answered (per fragment in the shader, per draw on the CPU, per cluster on the CPU or GPU).
That choice determines:

- whether `DrawItem`, `Mesh`, push constants, the binding set or the RHI change;
- whether a pass is added;
- whether the Performance Contract's counters move;
- whether goldens can stay byte-identical.

**Forces, stated honestly:**

- The owner requires the image to be preserved under an explicit contract: byte-identical goldens, or a declared,
  reviewed update.
- Goldens are compared at channel tolerance 0 (ADR-0042).
- Every PR must keep the gated tiers (counters exact, CPU) non-regressed. The GPU tier is report-only on the
  reference machine (Spec 0060 sixth Correction).
- The RHI has no compute dispatch.
- `DrawItem` may not grow without superseding Spec 0023 D10.
- The sheen push-constant block is already at the 128-byte Vulkan guarantee.
- Whether the loop is ALU-bound or fetch/loop-bound is not yet measured. Spec 0061 Phase 1 measures it.
- A precomputed structure might still be needed after this decision. The model has to say what any such structure
  must preserve.

## Decision

1. **Association is per fragment, by exact range rejection inside the existing loop.** In the four PBR shaders, the
   point-light term is evaluated only when `NdotL > 0.0` **and** `atten > 0.0`, where `atten` is the existing
   expression `clamp(1.0 − dist / range, 0.0, 1.0)`, computed once and used both as the predicate and in the
   radiance.
   - The predicate must be that same value. A different test (for example `dist < range`) would have to be proven
     conservative under the device's division precision, and is not adopted.
   - The BRDF formula, the attenuation, the light order, the uniform layout and every other term are unchanged.
   - There is no new pass, buffer, binding, push-constant field, `DrawItem` field or `Mesh` field, and no change to
     Runtime extraction.

2. **The equivalence invariant, binding on this and on any future association structure.** For every fragment:
   - **(a)** the set of lights whose term is evaluated is a superset of
     `{ j : NdotL_j > 0 ∧ atten_j > 0 }` as the existing expressions compute them;
   - **(b)** evaluated lights are accumulated in ascending light index;
   - **(c)** the per-light expression is unchanged in source.

   Why this preserves the result at the level of floating-point semantics:
   - For a skipped light, the old term is exactly `+0`: light values are finite and non-negative by `world::Light`'s
     contract and its gates, and every other factor is finite and non-negative under ADR-0067's clamps.
   - The accumulator is never `−0`.
   - Adding `+0` is the identity.

   **Contract:** goldens stay byte-identical.
   **Sole fallback:** if a moved golden is proven to be driver code-generation drift, the update is made by a
   dedicated ADR-0042 category-1 golden update. "Proven" means a control with the predicate forced true reproduces
   the movement. The update names the change and keeps max |Δ| ≤ 1 of 255 per channel. Any other movement is a
   defect.

   Any structure that hands a fragment a light list or mask must satisfy (a)–(c) with conservative bounds and margins.
   Extra lights cost time, never correctness.

3. **No precomputed association structure is adopted.** The deferred candidates are a per-draw CPU light mask, a
   CPU-built clustered/froxel list, and a compute-built tiled/clustered list. Reopening one requires a **new ADR**,
   because each touches a surface this decision holds closed: `DrawItem`/`Mesh` and push-constant contracts, the
   binding set and upload path, or a new RHI capability and RenderGraph pass. The reopening conditions are measured,
   not preferred:
   - the residual lighting cost after Decision 1 exceeds 25 % of the pre-change cost (Spec 0061 R7);
   - **or** Spec 0061 Phase 1 finds the loop per-light fetch/loop-bound;
   - **or** a scene's light count or density makes the per-fragment loop over `pointLightCount` itself the bound.

4. **The model depends on a finite-range falloff.** Decision 1 rejects only what the attenuation makes exactly zero. A
   future attenuation model without a hard zero, such as inverse-square without a window, leaves nothing to reject. It
   must revisit this ADR, and the photometric Spec reassigned by Spec 0040 O5 inherits that obligation.

## Consequences

### Positive

- The zero-contribution BRDF work, the bulk of the measured cost if Phase 1 confirms ALU-boundness, is removed with
  a one-predicate change in four shaders.
- The image is preserved by construction. No golden is expected to move, and a moved golden has a diagnosis procedure
  and a bounded, pre-declared path.
- Data flow is untouched. No counter of the Performance Contract moves, so no baseline refresh is needed. CPU is
  unchanged.
- It is trivially reversible. Any later structure composes with it, since a list or mask still benefits from the
  in-loop rejection of the lights it conservatively includes.
- The invariant gives any future structure a precise correctness target, instead of "looks the same".

### Negative / Trade-offs

- **The per-light preamble remains.** Every fragment still loads and tests every light: two uniform loads, a distance
  and a divide. If Phase 1 finds that preamble dominant, Decision 1 alone will not meet the target, and Decision 3's
  reopening applies.
- **Waves execute the branch if any lane is in range.** The saving depends on screen-space coherence of light
  footprints. It is measured, not assumed.
- **The source-level identity argument does not bind the driver's instruction selection.** Restructuring could
  change contraction inside the evaluated branch. The goldens are the empirical check, and the fallback in
  Decision 2 exists for exactly this reason.
- **Coupling.** The optimization is coupled to the attenuation model (Decision 4).

## Alternatives Considered

- **Per-draw light mask built during extraction.** It keeps counters exact, and a 64-bit mask fits
  `PbrPushConstants`' existing 8-byte pad. However:
  - it needs a `DrawItem` field (superseding Spec 0023 D10) and `Mesh` extents;
  - the sheen block has no room left under the 128-byte guarantee;
  - it puts ≈ 150 k tests per frame on the gated CPU tier;
  - its granularity is worst for the large meshes that cover most of Bistro's pixels.

  Deferred under Decision 3.
- **CPU-built clustered/froxel list.** It has per-cluster granularity and a cheap build for 59 lights. However:
  - it adds a binding and a per-frame upload, so counters change and a baseline refresh is needed;
  - it brings depth-slice logic into four shaders;
  - it indexes the uniform non-uniformly within a wave at cluster boundaries.

  Deferred under Decision 3.
- **Compute-built tiled/clustered list.** It needs an RHI compute capability that does not exist, plus a RenderGraph
  pass. That is out of proportion for 59 lights. Rejected for now, and reopened only by Decision 3's third condition.
- **Upload only the N most influential lights per frame (CPU prioritization).** It changes which lights contribute,
  so it violates the invariant. Rejected.
- **Leave the loop as it is (ADR-0088 D5).** This is the status quo that costs ≈ 11 ms. Rejected on measurement.
