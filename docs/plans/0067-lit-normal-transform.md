# Plan: Lit Normals Under Any World Matrix — Retire the Non-Conformal Skip

- **Spec:** [Spec 0067: Lit Normals Under Any World Matrix](../specs/0067-lit-normal-transform.md) (`Draft`) —
  [ADR-0134](../adr/0134-lit-normal-transform-by-cofactor.md) (`Proposed`).
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending. Drafted against the Spec's recommended options (Q1 N-a, Q2 D-a, Q3 G-a, Q4 B-a,
  Q5 P-a).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Transform lit normals by the world matrix's cofactor in every lit shader (Spec R1, ADR-0134 D1). Replace the conformal
skip with one degenerate-only predicate shared by the frame, the capture path and picking (R2, D2). Then re-capture
the goldens that change, with Bistro's newly drawn geometry reviewed, and leave the baseline refresh to its own PR.

## Milestones / Task Breakdown

Each milestone builds and runs the affected suites (Debug) before the next.

1. **M1 — The shader change, measured first.**
   - **Shaders:** the cofactor normal (Spec R1) in the eleven lit vertex shaders: `lit_textured`, `pbr_direct_lit`,
     `pbr_direct_lit_normal_map`, `pbr_ibl`, `pbr_ibl_normal_map`, `pbr_clearcoat_ibl`, `pbr_clearcoat_ibl_normal_map`,
     `pbr_sheen_ibl`, `pbr_sheen_ibl_normal_map`, `pbr_anisotropic_ibl`, `pbr_anisotropic_ibl_normal_map`. The helper is
     repeated verbatim in each (J2); tangents are unchanged.
   - **Pin the formula:** a GPU-free test of a CPU mirror of the helper against `normalize((M⁻¹)ᵀ n)` for random
     invertible `M`, and against `normalize(M n)` for conformal `M`.
   - **Measure (Q3):** run every image-regression test against the current goldens with the skip still in place, so
     only the arithmetic changed. Report which goldens move and by how much. The Joint Review's J3 rules on the result
     before M2 starts if any golden other than through rounding moves.
2. **M2 — One predicate (R2, D2).**
   - **Extraction:** `checkConformalTransform()` becomes `checkInvertibleNormalTransform()` (near-zero column, or
     `|det| ≤ ε·|c0||c1||c2|`), and `NonConformalNormalTransform` becomes `DegenerateNormalTransform` (Q2 D-a).
   - **Call sites:** `runFrame()`'s extraction, `frame_capture.cpp` and `pickRenderable()` call it. These are
     `runFrame()` lines, enumerated in the PR.
   - **Tests:**
     - the extraction unit tests are updated, now asserting acceptance of non-uniform scale and shear;
     - #295's picking test is inverted: a non-conformal entity is picked, and a degenerate one is neither drawn nor
       picked;
     - a new GPU test checks that a non-uniformly scaled lit sphere is drawn and its shading at sampled pixels matches
       the analytic normal.
3. **M3 — The fixtures and goldens (R3, Q3).**
   - **Fixtures:** the image-regression fixtures that mirror the skip use the predicate.
   - **Lighting demo:** its "deliberately non-conformal entity" test inverts (drawn, not skipped).
   - **Bistro:** its fixture test's pinned `2909 - 368` becomes `2909`.
   - **Goldens:** Bistro's golden is re-captured with the golden generator (category 1, rendering change), and so is
     any golden M1 found moved. The PR carries per-golden diff metrics and the Bistro before/after for the owner's
     human golden review.
4. **M4 — Normative docs and acceptance.**
   - **Docs:** `module_boundaries.md` (its Renderer/Runtime line on the conformal gate) and AGENTS.md's `runFrame()`
     guard paragraph, if M2's lines move it.
   - **Acceptance:**
     - Debug and Release full suites;
     - goldens byte-identical except the reviewed re-captures;
     - VVL clean;
     - `assembleDebug`;
     - the performance gate, reported with its expected counter `fail` on Bistro (+368 draws, +1.37 M triangles)
       and every other metric as measured (Q4 B-a). The baseline refresh is a dedicated follow-up PR.

## Files / Modules Touched (expected)

- **Shaders:** the eleven `.slang` files above.
- **Runtime:**
  - `src/runtime/include/atlantis/runtime/scene_extraction.h` and `src/runtime/src/scene_extraction.cpp` (the
    predicate and the error);
  - `src/runtime/src/runtime_application.cpp` (`runFrame()`'s extraction lines, `pickRenderable()`);
  - `src/runtime/src/frame_capture.cpp`;
  - `src/runtime/include/atlantis/runtime/runtime_application.h` (`pickRenderable()`'s contract comment).
- **Tests:**
  - `tests/runtime/scene_extraction_tests.cpp`;
  - `tests/runtime/runtime_smoke_gpu_tests.cpp`;
  - the CPU-mirror test: a new file under `tests/renderer/` or `tests/runtime/`;
  - `tests/image_regression/` fixtures (`lighting_demo`, `pbr_material_demo`, `pbr_normal_map_demo`,
    `pbr_clearcoat_demo`, `pbr_sheen_demo`, `pbr_anisotropic_demo`, `pbr_materials_showcase`,
    `integrated_showcase_demo`), `lighting_demo_gpu_tests.cpp` and `bistro_scene_gpu_tests.cpp`;
  - the re-captured goldens under `tests/image_regression/goldens/`.
- **Docs:** `docs/architecture/module_boundaries.md`; AGENTS.md if the guard lines move.
- **Not touched:** the importer, the Asset System, the scene grammar, the catalog, World, Connection, Remote, the
  editors, `tests/performance/baselines/`, `gate_policy.json`, unlit, shadow and overlay shaders.

## Sequencing & Dependencies

- M1 before M2, so that a golden's movement is attributed to the arithmetic alone, not to newly drawn entities.
- M3 needs M2.
- The baseline refresh PR (category 1) follows this Plan's PR's merge and is not part of it.

## Joint Review decisions — proposed

- **J1 — Spec Corrections:** none proposed.
- **J2 — Shader sharing:** repeat the helper verbatim in each lit shader, as the lit variants already repeat their
  shared code. A CPU-mirror test pins the formula. Proposed: accept. The alternative, include support in the Shader
  System, is a separate change with its own build-graph implications.
- **J3 — M1's golden checkpoint:** if M1 finds a golden moving by more than one 8-bit step in any channel, or in more
  than 0.1 % of its pixels, the owner rules before M2 (re-capture, or investigate the arithmetic). Proposed: accept.
- **J4 — The degenerate threshold:** `ε` is fixed in M2 from the existing `kDegenerateLengthEpsilon` and a
  coplanarity bound stated in the PR, never tuned to a scene. Proposed: accept.

## Verification Checklist

- [ ] Unit tests: the predicate's accept and reject cases; the cofactor CPU mirror (Spec Testing).
- [ ] Headless integration tests: the non-uniform sphere's shading; picking a non-conformal entity; a degenerate
      entity neither drawn nor picked.
- [ ] Image regression tests: every golden compared on the reference machine; Bistro's and any moved golden
      re-captured with reviewed diffs (Q3).
- [ ] Vulkan Validation Layers clean in every GPU test.
- [ ] Other: `assembleDebug`; the performance gate reported (Q4); the changed `runFrame()` lines enumerated.

## Rollback Plan

Revert the implementation PR. The shaders, the predicate and the goldens return together. The baseline-refresh PR,
if merged, is reverted with it.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:

- The owner's human golden review of Bistro's before/after and of every other re-captured golden.
- The gate's verdict reported with its expected counter changes; the baseline refresh named as the follow-up.
