# Plan: Point-Light Cost in the Forward PBR Path

- **Spec:** [Spec 0061: Point-Light Cost in the Forward PBR Path](../specs/0061-point-light-cost.md)
  (`Approved`, 2026-10-18; owner rulings recorded once below; review of
  [PR #261](https://github.com/slmao/Atlantis/pull/261), merged) —
  [ADR-0121](../adr/0121-point-light-association-model.md) (the point-light association model; D1–D4), `Accepted`
  2026-10-18 on the same rulings.
  - Unchanged and relied on: [ADR-0067](../adr/0067-pbr-direct-lighting-brdf-and-push-constant-contract.md) D-1 (the
    BRDF; its guard extended by ADR-0121 D1), [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
    (attenuation), [ADR-0088](../adr/0088-frame-lighting-data-successor-structure-and-binding-strategy.md) D1–D4, D6,
    [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md) (goldens),
    [ADR-0118](../adr/0118-performance-contract.md)/[ADR-0119](../adr/0119-benchmark-gate-protocol-and-verdict.md)
    (the gate), ADR-0004.
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-10-18 — reviewed this Plan and
  [Spec 0061](../specs/0061-point-light-cost.md) together; **J1–J4 ruled as recommended** (the owner's implementation
  tasking, 2026-10-18; [PR #262](https://github.com/slmao/Atlantis/pull/262), merged, is the review's close);
  implementation authorized from Milestone 1. The M1 checkpoint ruling (2026-10-18, proceed with Stage 1 after
  decision-table row B) is recorded in [PR #263](https://github.com/slmao/Atlantis/pull/263).
- **Implementation:** M1–M5 merged in [PR #263](https://github.com/slmao/Atlantis/pull/263) (Stage 1: 20.0 % of the
  point-light cost removed, R7 fired); its baseline follow-ups (Q4a, Q4b) folded into
  [PR #267](https://github.com/slmao/Atlantis/pull/267) per the Stage 2 Plan's Q11.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0061 as ruled: **Stage 1 only.** First the Phase 1 investigation (I-1 … I-6), ending in a hard owner
checkpoint. Then the exact range rejection (R3) in the point-light loop of the four looping PBR shaders and the one CPU
twin that transcribes them, with the float-identity argument made mechanical as a test. Then the golden verification
(Outcome A, with Outcome B's procedure ready), and the before/after measurement on Bistro against the ≥ 50 % target,
with the gate run on the final PR. No data-flow change (R4): no layout, binding, push-constant, `DrawItem`, `Mesh`,
extraction, RenderGraph, policy or baseline change. No counter is expected to move.

## Governance record (this PR)

**Owner rulings, 2026-10-18 (tasking for this Plan), recorded once here.** The Spec body keeps its open-question text
as the record of what was weighed; these rulings bind it.

| Q | Ruling |
|---|---|
| Q1 | **Stage 1 only.** Range rejection inside the existing loop, on the attenuation value the shader already computes. Stage 2 (a CPU-built per-draw mask or clustered list) is **not authorized**: it needs a measured residual above R7's trigger, a Spec revision and its own ADR. |
| Q2 | Thresholds as specified: target ≥ 50 % of `L` (≈ 11 → ≤ 5.5 ms); Stage 1 expectation 60–75 %; Stage 2 trigger at a 25 % residual; the 50 % row boundary in the decision table. |
| Q3 | The narrow golden fallback (R2 Outcome B) is accepted: a golden update only when a control build proves the movement is driver code generation **and** every channel's max \|Δ\| ≤ 1 of 255; anything larger is a defect fixed in code. |
| Q4 | After the implementation merges, the owner will instruct (a) a category-1 refresh of `bistro/baseline` (reason: performance change) and (b) recording a `no-point-lights` baseline, each its own PR. Named here as post-merge follow-ups, **not tasks** of this Plan. |
| Q5 | `lit_textured` stays untouched; named as a possible follow-up. |
| ADR | ADR-0121 is `Accepted` on these rulings, with [PR #261](https://github.com/slmao/Atlantis/pull/261) as evidence. |

This PR flips Spec 0061 to `Approved` and ADR-0121 to `Accepted`, and updates the registry row. The Plan stays
`Draft` pending the Joint Human Review.

## Pre-drafting reading (cited, not restated)

[AGENTS.md](../../AGENTS.md); Spec 0061 and ADR-0121; [Plan 0060](0060-performance-contract.md) (exemplar);
[docs/process/performance-contract.md](../process/performance-contract.md) and
[profiling.md](../process/profiling.md); `docs/process/definition-of-done.md`, `testing-strategy.md`; and the code this
Plan touches or relies on:

- `shaders/{pbr_direct_lit,pbr_direct_lit_normal_map,pbr_ibl,pbr_ibl_normal_map}/*.slang`: the four point-light loops
  (identical bodies). Shaders are compiled at build time by `atlantis_add_slang_shader_pair` (reflection JSON emitted
  beside the SPIR-V); nothing compiled is committed.
- `src/runtime/src/scene_extraction.cpp:437` `computePbrDirectLighting()` and its header contract
  (`scene_extraction.h:289-301`): the **CPU twin** of these loops, "line-for-line", test-only, never on a rendering
  path. Two facts matter here. It evaluates the point light through a shared lambda whose `NdotL <= 0` early return is
  the guard. And it forms radiance as `color · (intensity · atten)`, where the shader has `(color · intensity) · atten`:
  a pre-existing grouping difference this Plan neither relies on nor changes (P4). `computeLambertianDiffuse()` (the
  `lit_textured` twin) is untouched (Q5).
- `tests/runtime/scene_extraction_tests.cpp` (the twin's hand-computed tests), `pbr_reflection_cross_check_tests.cpp`
  (the PBR reflection cross-check), `tests/runtime/CMakeLists.txt` (`atlantis_runtime_tests`, GPU-free).
- `tests/image_regression/` (67 golden comparisons, channel tolerance 0, Debug and Release); `bistro_demo` (59 lights)
  and `multi_light_demo` (≥ 8) are the ones that exercise rejection.
- `src/runtime/cli.cpp`: `--benchmark … --variant --resolution --pin-process on|off --json --output`; `--gate --json`;
  `--gate --applies`. `atlantis runtime step --capture <path>` (Spec 0055) for frame captures.
- The build has no `/fp:fast` or `/fp:contract` anywhere (checked at drafting); the MSVC default `/fp:precise`
  applies to the equivalence test.

## Plan-stage decisions

**P1 — Files.**

| Where | Change | Milestone |
|---|---|---|
| `shaders/pbr_direct_lit/pbr_direct_lit.slang`, `shaders/pbr_direct_lit_normal_map/pbr_direct_lit_normal_map.slang`, `shaders/pbr_ibl/pbr_ibl.slang`, `shaders/pbr_ibl_normal_map/pbr_ibl_normal_map.slang` | The point-light loop only, to Spec 0061's normative shape (P3). One comment citing Spec 0061 R3 / ADR-0121 D1. | M3 |
| `src/runtime/src/scene_extraction.cpp`, `src/runtime/include/atlantis/runtime/scene_extraction.h` | `computePbrDirectLighting()`'s point loop takes the same predicate (P4); its header comment notes the rejection. | M3 |
| `tests/runtime/point_light_rejection_equivalence_tests.cpp` (new) | The mechanical equivalence proof (P5). | M2 |
| `tests/runtime/CMakeLists.txt` | Add the new test file; one compile definition for the Bistro overlay path; `Atlantis::AssetSystem` added to the test target's links only if `parseSceneSource()` is not already reachable through `Atlantis::RuntimeHost`. | M2 |

**Dependency-graph changes:** none. No module's link list changes (a test-target link, if needed, is not a module
dependency). No third-party dependency.

**Not touched:** every other shader (`lit_textured`, the six clearcoat/sheen/anisotropic shaders, `shadow_cast`,
`sky`, bloom, output transform); `src/{renderer,render_graph,rhi,vulkan_backend,platform,world,asset_system}/`;
everything in `src/runtime/` except the twin; `assets/`; `android/`; every golden and sidecar (unless Outcome B,
P7); `tests/performance/` (policy, applicability, baselines); `FrameLightingData` and all `static_assert`s.

**P2 — Build trees and configurations.** All trees are untracked and configured from clean checkouts.

| Tree | Configure | Used for |
|---|---|---|
| `build/` | default (config A) | Debug and Release full `ctest` at every gate |
| `build-prof/` | `-DATLANTIS_PROFILING=ON -DATLANTIS_PROFILING_TRACY=OFF` (config B) | Release; benchmark and gate runs on the branch |
| `build-prof-base/` | config B, from a worktree of the branch's merge base on `origin/main` | Release; the "before" arm of every A/B measurement and the base for Phase 1 probes |

Probe builds (P6) reuse `build-prof-base/` with a working-tree patch applied.

**P3 — The Stage 1 shader edit.** Exactly Spec 0061 R3's normative shape: `atten` computed with its existing
expression ahead of the branch, and the branch condition `NdotL > 0.0 && atten > 0.0`. `radiance` keeps
`color * intensity * atten` in that order, and the body below is untouched. The four loops stay identical to one
another. No other line of the four shaders changes. The predicate is the same value (R2 (d)); a `dist < range`
shortcut is not used.

**P4 — The CPU twin.** In `computePbrDirectLighting()`, the point loop computes `atten` as today and calls
`accumulateLight` only when `atten > 0.0f`. The lambda's own `NdotL <= 0` return supplies the other half of the
predicate, so the combined condition equals the shader's. The directional loop is untouched. The header comment gains
one sentence (rejection per ADR-0121 D1). The twin's pre-existing radiance grouping is left as it is: changing it is
not in Spec 0061's scope, and for `atten = +0` both groupings give `+0`, so the equivalence holds either way. The
existing hand-computed tests (`scene_extraction_tests.cpp:751-…`, and the HDR use in `pbr_render_gpu_tests.cpp:786`)
must stay green unchanged.

**P5 — The equivalence test (Spec T3; the float-identity argument made mechanical).** GPU-free, in
`atlantis_runtime_tests`, tagged `[point-light-rejection]`. Nothing in it calls or shares code with a shader.

- **Transcriptions.** Two test-local functions transcribe the four shaders' point-light loop with the **shader's**
  operation order and grouping: *before* (the pre-change loop, frozen) and *after* (P3). Each returns the accumulated
  RGB, and *before* can also return each light's term on its own. A third, test-local frozen copy of the pre-change
  twin's point loop is the reference for the updated `computePbrDirectLighting()`.
- **Assertions, per fragment:**
  1. *after* equals *before* **bitwise**, compared by `std::bit_cast<std::uint32_t>` per channel, not with a
     tolerance.
  2. For every light *after* rejects that *before* would have evaluated (`NdotL > 0 ∧ atten == 0`), *before*'s term is
     `+0.0` on every channel, sign bit clear.
  3. *before*'s accumulator is never `−0` at any step.
  4. The updated `computePbrDirectLighting()` equals the frozen pre-change twin bitwise.
- **Corpus** (deterministic; fixed seed):
  - *Bistro*: the 59 point lights (plus its directional light, which must pass through untouched) parsed from
    `assets/bistro/bistro_overlay.scene.txt` with the existing `parseSceneSource()`. Fragments on the planes
    y ∈ {0, 1.5, 3} over the lights' footprint ± 10 m, with normals up, ±x, ±z and seeded random directions, and with
    materials roughness {0, 0.5, 1} × metallic {0, 1}.
  - *Boundaries*: per light, fragments at distance `range` exactly and at `range` ± 1 and ± 2 ULP along several
    directions; a fragment at the light's position (the `kPointLightDistanceEpsilon` path); `N ⟂ L`; `L = V`;
    grazing `NdotL`.
  - *Random*: seeded random lights (finite, the grammar's domains) and fragments.

  At least 100 k fragment evaluations; run time is reported in the PR.
- **Self-check that the test can fail.** A deliberately non-conservative *after* (rejecting at `dist ≥ 0.99 · range`)
  must fail assertion 1 on the corpus. This runs as a negative sub-case inside the same test file, so the corpus
  provably covers boundary lights.
- **What this does and does not prove.** It proves R2 (a)–(d) under the C++ implementation's IEEE semantics. It does
  not bind the GPU compiler (Spec 0061, "What the argument does not cover"); T2 (goldens) and P8 (the full-frame
  capture) are that check.

**P6 — Phase 1 probes and measurements (never committed).**

- **Probes are working-tree patches** applied to a worktree of the base commit, built in `build-prof-base/`, and
  reverted after measuring. They are never committed, never pushed, and never touch the branch. Their diffs are
  attached as text to the Phase 1 report.
  - **P1-probe:** P3's edit.
  - **P2-probe:** the fetch-and-loop floor. Both 16-byte loads and the existing preamble (`toLight`, `length`, `max`,
    normalize, `dot`, `max`) are kept. The whole `NdotL` branch is replaced by a never-taken, data-dependent branch
    over a value folded from every loaded field. No BRDF.
- **Probe validity.**
  - The P2-probe's SPIR-V is disassembled (`slangc -target spirv-asm`, the toolchain already pinned) to confirm the
    per-iteration loads and the loop survive.
  - A frame captured from the P2-probe must equal the `no-point-lights` variant's frame. Its branch is never taken, so
    the lit result is the zero-light result. Inequality would mean the probe changed something else.
- **Interleaved A/B (all Phase 1 and M5 measurements).** Independent `atlantis_runtime --benchmark bistro
  --resolution 1920x1080 --warmup 120 --frames 300 --pin-process on --json --output <file>` processes. Arms alternate
  round-robin within one session, at least 5 runs per arm, on mains power and an idle machine. The run-level statistic
  is the run's p50; each arm is reported as the median and min–max of its run p50s.
- **`L`** = median `passes.draw.gpu_ms` (`baseline`) − median `passes.draw.gpu_ms` (`no-point-lights`) for the same
  build in the same session. Share removed = `1 − L_after / L_before`.
- **No new tool is committed.** The orchestration is a throwaway script in the scratch directory. Raw benchmark JSON
  stays local (Plan 0059 J11); only the numbers are reported.

**P7 — Outcome B, step by step** (R2; only if a golden moves at M4):

1. **Stop.** Record which goldens moved, in which configuration, with the existing failure output (diff image, max
   and mean channel delta, changed-pixel count).
2. **Control build.** Apply to the branch a local, uncommitted patch that forces the predicate true
   (`atten > 0.0 || true`): the restructured loop evaluating every facing light. Run the same golden tests.
3. **Classify.**
   - The control fails with the **same** moved pixels and values → driver code-generation drift. Continue to step 4.
   - The control passes while Stage 1 fails → an R2 (a) violation (an association bug). Fix it in code, return to M3,
     and never re-capture.
   - Any other pattern → stop and report; do not classify.
4. **Bound.** Every moved golden's max |Δ| ≤ 1 of 255 on every channel. A larger delta is a defect fixed in code (Q3),
   even if the control reproduces it.
5. **Report and stop for the owner.** The owner rules whether to proceed.
6. **The golden-update PR.** On the owner's go, a **dedicated** golden-update PR (goldens and sidecars only) is
   opened with **base = the implementation branch**. Its description carries:
   - ADR-0042 category 1 (rendering change) and Spec 0061 R2's named difference: "compiler-level rounding difference
     from restructuring the point-light loop of the four PBR shaders; no change in which lights contribute";
   - per golden, max and mean channel delta, changed-pixel count and the diff images;
   - the control-build evidence.

   The owner merges it into the implementation branch, so `main` never sees a failing golden, and the implementation
   PR then carries the reviewed update into `main` (J2).

**P8 — Full-frame A/B capture (beyond the 512² golden).** The base and branch config-B Release builds each run
`atlantis_runtime --scene bistro --listen` at the same window size on the same machine. `atlantis runtime pause`
followed by `atlantis runtime step --capture <dir>` captures a fixed frame. The two PNGs must be byte-identical. This
is PR evidence, uncommitted. It checks R2 at the benchmark camera and full resolution, where far more fragments sit
near light-range boundaries than in the golden.

**P9 — Reflection unchanged (R4 made mechanical).** The four shaders' emitted `*.refl.json` (vertex and fragment) are
byte-identical between `build-prof-base/` and `build-prof/`. The existing `pbr_reflection_cross_check_tests` stays
green. Any reflection difference is a deviation that stops the work.

**P10 — Gates.**

- **Every milestone gate (from M2):**
  - config A Debug and Release builds, full `ctest`;
  - every golden compared exactly (a moved golden enters P7, never a re-capture);
  - the fatal-VVL GPU suites;
  - Android `assembleDebug` (the shaders compile there too);
  - the **path guard**: the diff against the merge base is limited to P1's files.
- **The final gate adds:**
  - config B Release full `ctest`;
  - P8 and P9;
  - `git diff --name-only origin/main... | atlantis_runtime --gate --applies` (expected `applies: true`, matching
    `shaders/**` and `src/runtime/**`; the applicability list needs no change);
  - `atlantis_runtime --gate --json` (M5).

## Milestones / Task Breakdown

There is **one implementation PR** on `feature/0061-point-light-cost`, cut from freshly fetched `origin/main` after
this Plan's Joint Human Review. Phase 1 (M1) produces no commits. M2–M5 build and run their gate before the next
starts. A conditional dedicated golden PR (P7) exists only under Outcome B.

### M1 — Phase 1 investigation; owner checkpoint (Spec R1; I-1 … I-6) — **hard stop at the end**

1. **Preconditions.** Build `build-prof-base/` (config B, Release) at the merge base. Confirm the committed gate passes
   on it (`--gate --json` → `pass` or `inconclusive` with the reason) so the machine is in a known state, and record
   the canary-free machine-state reading the verdict gives.
2. **I-1.** Interleaved `baseline` / `no-point-lights` on the base build: `L_before`, `fragment_invocations` (equal
   across variants within 0.1 %) and `lights_point` (59 / 0).
3. **I-2.** Resolution sweep (1280×720, 1920×1080, 2560×1440), both variants, interleaved per resolution: `L` against
   `fragment_invocations`. Cost per fragment-light = `L ÷ (fragments × 59)`, set against Spec 0061 Inv-1's operation
   counts and the device's nominal FP32 rate (the report states the figure used and its source).
4. **I-3.** The P1-probe (P6) against the unmodified base build, interleaved, both variants: the share of `L` it
   removes.
5. **I-4.** The P2-probe, with the validity checks of P6, interleaved with the base build: the fetch-and-loop floor;
   `P1 − P2` is the residual BRDF work.
6. **I-5.** An uncommitted CPU harness (a local, never-committed test case in `atlantis_runtime_tests` or a scratch
   program) that loads Bistro through the existing CPU paths and reports, per `DrawItem`, the lights whose sphere meets
   its world bounds, and per froxel of a 16×9×N grid on the benchmark camera, the lights inside. Both distributions are
   weighted by triangle count. Informational only: Stage 2 is not authorized (Q1).
7. **I-6** (opportunistic). Across I-1's sessions, classify runs fast or slow by Spec 0060's `Opaque` bands and compare
   each variant's absolute shift. Reported only if the slow state occurs.
8. **The Phase 1 report** goes to the owner: commands, revisions, session layout, per-arm medians and ranges, the probe
   diffs and validity evidence, and the decision-table row with its reasoning against the pre-declared thresholds:
   - **Row A:** the P1-probe removes ≥ 50 % of `L`.
   - **Row B:** the P1-probe removes < 50 % and the P2-probe accounts for most of the remainder.
   - **Row C:** the P1-probe and the P2-probe together account for little of `L`.

**STOP.** No Stage 1 code is written until the owner rules on the report.

- On **row A or B** with a go, continue to M2. Under row B, Stage 1 still lands, and the Stage 2 trigger is noted but
  not acted on (Q1).
- On **row C**, the work stops and Spec 0061 returns to review.

*Gate:* none (no commits). The ruling is cited in the implementation PR.

### M2 — The equivalence test (Spec T3; R2)

1. Add `point_light_rejection_equivalence_tests.cpp` per P5, with the test-local *before*/*after* transcriptions, the
   frozen pre-change twin reference, the corpus and the negative self-check. Wire it into `atlantis_runtime_tests`,
   with the overlay-path compile definition (P1).
2. At this point the twin is unchanged, so assertion 4 compares the frozen copy with the current twin: it must pass,
   proving the frozen copy is faithful before M3 changes the twin.

*Gate:* P10 (config A, Debug and Release full `ctest`; goldens; VVL; `assembleDebug`; path guard).

### M3 — Stage 1: the four shaders and the twin (R3, R4; ADR-0121 D1)

1. The four shader edits (P3), as one commit, identical across the four files.
2. The twin edit (P4), in the same commit, so the twin never disagrees with the shaders at any commit.
3. P9: reflection JSON byte-identical against `build-prof-base/`.
4. The equivalence test now exercises the updated twin (assertion 4), and the existing twin tests stay green.

*Gate:* P10. Goldens are the contract: all 67 × 2 byte-identical (Outcome A), or P7 begins.

### M4 — Golden verification and the full-frame capture (R2)

1. Confirm Outcome A in Debug and Release, and that the golden directory is untouched (`git diff --stat
   tests/image_regression/goldens` empty).
2. P8: the full-frame Bistro capture is byte-identical between base and branch.
3. Only if a golden moved at M3: P7 in full, including the owner stop.

*Gate:* P10.

### M5 — Measurement, the gate, and the report against the target (R5, R6, R7)

1. **Before/after**, interleaved in one session (P6), four arms: {base, branch} × {`baseline`, `no-point-lights`}.
   Report:
   - `passes.draw.gpu_ms`, `passes.Opaque.gpu_ms`, `gpu_busy_ms`, `frame_ms` p50, `cpu_active_ms` and every counter,
     per arm;
   - `L_before`, `L_after` and the share removed, against the **≥ 50 % target** and the **60–75 % expectation**;
   - the **control**: `no-point-lights` `passes.draw.gpu_ms` unchanged within its interleaved range.

   A miss is reported as a miss.
2. **Gate** on the final commit, from a clean checkout, config B Release: `atlantis_runtime --gate --json`. Required:
   - `verdict: pass`;
   - every counter identical to the baseline (R4; `fragment_invocations` within its 0.1 %);
   - CPU metrics within tolerance;
   - GPU changes listed under `reported`.

   An `inconclusive` verdict is re-run and, if it persists, reported with its verdict and reasons. A `fail` stops the
   work. No baseline is recorded or refreshed, and `gate_policy.json` is not edited.
3. **R7 evaluation.** If `L_after > 0.25 · L_before`, the PR says so with the numbers. Stage 2 is not started (Q1).
4. `--gate --applies` on the PR's change set (P10).

*Gate:* the final gate (P10).

## Files / Modules Touched (expected)

**New:** `tests/runtime/point_light_rejection_equivalence_tests.cpp`; `docs/plans/0061-point-light-cost.md` (this
PR).

**Changed:**

- implementation: the four shaders, `src/runtime/src/scene_extraction.cpp`,
  `src/runtime/include/atlantis/runtime/scene_extraction.h` (comment), `tests/runtime/CMakeLists.txt`;
- this PR: `docs/specs/0061-point-light-cost.md` and `docs/adr/0121-point-light-association-model.md` (status and
  approval metadata only), `docs/specs/README.md` (registry row);
- under Outcome B only: the moved goldens and sidecars, in their own PR.

**Not touched:** as listed under P1.

## Sequencing & Dependencies

- This Plan's Joint Human Review authorizes implementation. M1 then needs only the base build.
- **M1 → owner checkpoint → M2.** The checkpoint is a hard stop. No implementation branch commit precedes the ruling.
- M2 before M3, so the frozen references are proven faithful against unchanged code before the change.
- M3's shaders and twin land in one commit.
- M4 needs M3. Outcome B (P7) suspends M5 until the owner rules and the golden PR merges into the branch.
- M5 needs M4 green. The gate runs on the final commit.
- Build and test run after every milestone (P10).
- **After the implementation PR merges** (follow-ups, not tasks of this Plan):
  - the owner-instructed category-1 refresh of `bistro/baseline` (Q4a);
  - the `no-point-lights` baseline recording (Q4b);
  - a docs PR updating the registry row and blueprint.

  Possible later work: `lit_textured` (Q5), and Stage 2 only through a Spec revision and ADR if R7 fired.

## Verification Checklist

Every Spec 0061 requirement and acceptance criterion, mapped to concrete tests or runs.

- [ ] **R1 investigation before technique (M1):** the Phase 1 report with I-1 … I-5 (I-6 if the state occurs), the
  probe validity evidence (SPIR-V disassembly and the P2-probe frame equal to `no-point-lights`), the decision-table
  row; the owner's checkpoint ruling cited in the implementation PR; no implementation commit dated before it.
- [ ] **R2 equivalence (M2–M4):**
  - *mechanical:* P5's bitwise test over the corpus, with its negative self-check failing as designed;
  - *GPU:* all **67 × 2 golden comparisons byte-identical in Debug and Release** with the golden directory untouched
    (Outcome A); P8's full-frame Bistro capture byte-identical;
  - *if a golden moves:* P7 executed step by step, with the control-build classification, the ≤ 1/255 bound, the owner
    stop, and the dedicated category-1 golden PR merged into the implementation branch.
- [ ] **R3 technique (M3):** diff review — the four loop bodies identical to one another and to P3's shape; the twin
  per P4; nothing else in the shaders changed.
- [ ] **R4 no data-flow change (M3, M5):** P9 reflection JSON byte-identical and the reflection cross-check green; the
  path guard (P1's files only); the gate's counters all identical to the baseline.
- [ ] **R5 measured effect (M5):** the four-arm interleaved before/after with `L` against ≥ 50 % / 60–75 %, the
  `no-point-lights` control, and downstream `gpu_busy_ms` and `frame_ms` p50 reported.
- [ ] **R6 contract verdicts (M5):** `--gate --json` → `pass`, counters exact, CPU non-regressed, GPU under `reported`;
  no baseline, policy or applicability file touched.
- [ ] **R7 trigger (M5):** evaluated and stated; Stage 2 not started.
- [ ] **Applicability:** `--gate --applies` → `applies: true` on the change set; the list already covers `shaders/**`
  and `src/runtime/**`; no contract machinery change.
- [ ] **Unit tests:** `atlantis_runtime_tests` green in Debug and Release, including the new test and the existing twin
  tests unchanged.
- [ ] **Image regression:** as R2.
- [ ] **Vulkan Validation Layers:** the fatal-VVL GPU suites clean in Debug at every gate, including one Debug Bistro
  benchmark run with validation on.
- [ ] **Android:** `assembleDebug` green at every gate.
- [ ] **Builds:** config A Debug and Release full `ctest` at every gate; config B Release full `ctest` at the final
  gate.

## Risks

- **Driver code generation moves a golden.** Handled by P7. The owner stop keeps a bounded update from becoming
  routine.
- **The probes mislead.** A P2-probe the compiler partly eliminates would understate the floor. P6's disassembly and
  frame-equality checks guard against it. A probe is evidence for the checkpoint, not a gate.
- **Machine state confounds Phase 1 or M5.** All comparisons are interleaved within one session. A session that
  straddles a state change (I-6) is reported as such and re-run rather than averaged.
- **The target is missed** (row B, or wave divergence). It is reported against honestly. Stage 1 still lands if the
  owner agrees at the checkpoint, since it is never worse. Stage 2 waits for a Spec revision.
- **The twin's pre-existing radiance grouping** differs from the shader's. It is disclosed, not changed; P5 compares
  shader-faithful transcriptions, so the proof does not depend on the twin.
- **CPU-tier noise** on a shader-only change could give an `inconclusive`. Re-run per R6; never adjust the policy.

## Rollback Plan

Revert the single Stage 1 commit (the four shaders and the twin) and, optionally, the test. There is no data, format,
layout or baseline migration, since nothing persistent changes. Under Outcome B, revert the golden-update merge
together with it.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas specific to this Plan:

- The implementation PR carries:
  - the Phase 1 report verbatim and the owner's checkpoint ruling;
  - per gate, the configuration results, the path guard and the reflection comparison;
  - the full-frame capture comparison;
  - M5's four-arm table and the gate verdict document.
- Probe diffs, benchmark JSON and captures are PR evidence, never committed.
- Post-merge follow-ups (Q4a, Q4b, the registry/blueprint docs PR) are listed in the PR as owner-instructed next
  steps. None is performed by an agent unprompted.

## Joint Review questions

Points the owner may want to rule on in the Joint Human Review; each has a recommendation.

- **J1 — Where the Phase 1 report lives before any commit exists.** (a) Sent to the owner in chat at the checkpoint,
  then carried verbatim into the implementation PR's description with the ruling; (b) a Draft implementation PR opened
  early with an empty-change placeholder commit. **Recommend (a):** no placeholder commit, and the evidence still ends
  up in the implementation PR, as AGENTS.md's table requires.
- **J2 — Outcome B's merge mechanics.** (a) A dedicated golden PR based on the implementation branch, merged into it
  by the owner before the implementation PR (P7 step 6), so `main` never sees a failing golden; (b) the golden PR
  targets `main` right after the implementation PR (`main` red in between); (c) goldens in the implementation PR
  itself (ADR-0042 allows category 1 alongside its cause, but Spec 0061 R2 says "dedicated"). **Recommend (a).**
- **J3 — P8's full-frame capture.** Recommended as required evidence. It checks R2 at full resolution, where a
  512² golden samples few range-boundary fragments. It could be dropped if the owner judges the goldens sufficient.
- **J4 — Phase 1's run count.** At least 5 per arm (the gate's R). More runs tighten I-3/I-4's attribution at about
  15 s each. **Recommend 5, raised to 10 for I-3/I-4 if an arm's range overlaps the other's.**
