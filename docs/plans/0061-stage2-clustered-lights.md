# Plan: Point-Light Cost, Stage 2 — Clustered Light Association

- **Spec:** [Spec 0061: Point-Light Cost in the Forward PBR Path](../specs/0061-point-light-cost.md),
  **Revision 1 (Stage 2)**, `Approved` 2026-10-19 (owner rulings Q6–Q11 recorded once below; review of
  [PR #264](https://github.com/slmao/Atlantis/pull/264), merged) —
  [ADR-0122](../adr/0122-clustered-light-association.md) (the clustered light association; D1–D5), `Accepted`
  2026-10-19 on the same rulings.
  - Built on, unchanged: [ADR-0121](../adr/0121-point-light-association-model.md) D1, D2, D4 (Stage 1's rejection,
    the equivalence invariant, the finite-range dependence); Spec 0061 Stage 1 as implemented in
    [PR #263](https://github.com/slmao/Atlantis/pull/263) under [Plan 0061](0061-point-light-cost.md) (the structural
    exemplar for this Plan).
  - Relied on: [ADR-0088](../adr/0088-frame-lighting-data-successor-structure-and-binding-strategy.md) D1, D2, D6 and
    D3/D4 as amended by ADR-0122, [ADR-0042](../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md),
    [ADR-0118](../adr/0118-performance-contract.md)/[ADR-0119](../adr/0119-benchmark-gate-protocol-and-verdict.md),
    ADR-0004, ADR-0062.
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending — the owner's review of this Plan together with Spec 0061 Revision 1, explicitly
  authorizing implementation.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0061 Revision 1 (S2-R1 … S2-R7) as ruled. The work runs in this order:

1. An uncommitted prototype measured against the decision table, ending in a hard owner checkpoint.
2. The CPU bitwise equivalence test extended to the mask-walking loop.
3. The implementation:
   - a per-frame, CPU-built 64-bit light mask per cluster on a **12×7×16** grid, beside `extractFrameLightingData()`;
   - carried in a region appended to the camera uniform block;
   - walked low bit first by the four looping PBR shaders, with Stage 1's predicate unchanged;
   - the CPU twin updated.
4. The picture contract: goldens and three-viewpoint captures byte-identical.
5. The measurement, and the gate's pre-declared verdict.

There is no RHI, binding, descriptor, RenderGraph or Renderer-API change, no wave intrinsics, no job system and no
geometry culling.

## Governance record (this PR)

**Owner rulings, 2026-10-19 (tasking for this Plan), recorded once here.** Revision 1 keeps its open-question text as
the record of what was weighed; these rulings bind it.

| Q | Ruling |
|---|---|
| Q6 | The carrier is the camera uniform block: the mask region is appended. No RHI change, no new binding. |
| Q7 | Grid **12×7×16**, with **one** retune at the prototype checkpoint. |
| Q8 | No wave-wide (subgroup) iteration. |
| Q9 | Thresholds as fixed in Revision 1: target ≤ 2.0 ms, expectation 0.7–1.5 ms, abort > 4.0 ms; no-point-lights control ≤ +0.15 ms; all-lights control ≤ +0.5 ms (frames byte-identical); CPU build ≤ 0.10 ms per frame. |
| Q10 | The implementation PR merges with the declared gate `fail` on `uniform_write_bytes` alone, at a value this Plan states in advance (P2). The category-1 refresh follows after merge. |
| Q11 | The pending Stage 1 baseline follow-ups (the category-1 refresh and the `no-point-lights` baseline) fold into **one** post-Stage-2 refresh PR. |
| ADR | ADR-0122 is `Accepted` on these rulings, with [PR #264](https://github.com/slmao/Atlantis/pull/264) as evidence. |

This PR flips Revision 1 to `Approved` and ADR-0122 to `Accepted`, and updates the registry row. The Plan stays
`Draft` pending the Joint Human Review.

## Pre-drafting reading and findings (cited, not restated)

Read: AGENTS.md; Spec 0061 with Revision 1; ADR-0121 and ADR-0122; [Plan 0061](0061-point-light-cost.md) and PR #263;
`docs/process/performance-contract.md`; and the code below.

- **The uniform byte counter.** `src/runtime/src/runtime_application.cpp:1841` reports `uniform_write_bytes` as
  `kCameraUniformBufferSizeBytes + 128` (camera block plus light-space buffer). The counter follows the size
  constant, so its new value is exact by construction (P2).
- **Block writers.** The camera block is written inside `RuntimeApplication::runFrame()` (`runtime_application.cpp`,
  the region writes at ≈ 1600–1620) and in `frame_capture.cpp` (≈ 195–210). The test writers are Spec S2-Inv-3's
  list.
- **Finding 1 — the goldens' render paths differ from Revision 1's parenthetical.** S2-R3 says the Runtime path is
  what "the Bistro and multi-light goldens render through". In the code:
  - the **Bistro** golden renders through `pbr_normal_map_demo_fixture` (via the `emissive_demo_fixture.h` alias), a
    test-side writer of the camera block;
  - **`multi_light_demo`** renders through `lighting_demo_fixture`, i.e. **`lit_textured`**, whose block is a
    prefix and which neither Stage 1 nor Stage 2 touches. (Spec 0061's Stage 1 text listed it as exercising
    rejection; it did not.)

  S2-R3's intent, that the golden which exercises clustering uses the real builder, is met by P5: every writer calls
  the real builder. The Spec text is not edited (J4).
- **Finding 2 — `runFrame()` must change.** The region write belongs beside the other region writes inside
  `runFrame()`. AGENTS.md records Plan 0059's `runFrame()` diff guard ("identical to its base text except added
  `ATL_PROFILE_*` lines and `debugName` members"). No test enforces it. J1 asks the owner to confirm the guard was
  Plan 0059's instrumentation-PR check, not a standing ban on frame-path changes.
- **Transform properties** for the capture viewpoints: `Transform.localPosition` and
  `Transform.localEulerAnglesRadians` (pitch, yaw, roll; `src/world/src/world_schema.cpp:73-75`). The overlay camera
  is node 1, guid `2c5aa0de-a6b6-49cb-b5dc-7610fd2a5afb`, parent none.

## Plan-stage decisions

**P1 — Files.**

| Where | Change | Milestone |
|---|---|---|
| `src/runtime/include/atlantis/runtime/scene_extraction.h` | Grid constants, the region layout (P2) with offsets pinned by `static_assert`, `kCameraUniformBufferSizeBytes` re-derived, a `static_assert` that the block is ≤ 16,384 bytes, a `static_assert(kMaxPointLights == 64)` coupling to the mask width, the builder's declaration, the twin's mask parameter (P6). | M3 |
| `src/runtime/src/scene_extraction.cpp` | The builder (P4); the twin's mask walk (P6). | M3 |
| `src/runtime/src/runtime_application.cpp` | `runFrame()`: call the builder inside `RenderExtraction` under its own zone `ClusterBuild`, and write the region beside the fog write. The counter line is unchanged (it follows the constant). | M3 |
| `src/runtime/src/frame_capture.cpp` | Fill the region from the same builder output, so captures equal frames. | M3 |
| `shaders/` — the ten PBR shaders | Declare the extended block (P2). | M3a |
| `shaders/{pbr_direct_lit,pbr_direct_lit_normal_map,pbr_ibl,pbr_ibl_normal_map}/*.slang` | Cluster lookup plus the bit-walk loop (P3); Stage 1's body unchanged. | M3b |
| `tests/image_regression/fixture/*` (the seven PBR fixtures), `tests/image_regression/shadow_gpu_tests.cpp`, `tests/runtime/{pbr_render_gpu_tests,pbr_reflection_cross_check_tests,scene_extraction_tests}.cpp` | Size and fill the region with the real builder (P5); extend the 11/11 cross-check to the region. | M3a |
| `tests/runtime/point_light_rejection_equivalence_tests.cpp` | The clustered extension of the bitwise test (P7). | M2; M3 adds the builder-against-oracle cases |
| `tests/runtime/CMakeLists.txt` | Only if a new definition is needed (none expected). | — |

**Not touched:** `lit_textured` and its fixture; the six IBL-only BRDF-extension shaders' logic (they declare the
block only); `src/{renderer,render_graph,rhi,vulkan_backend,platform,world,asset_system}/`; push constants;
`DrawItem`; `Mesh`; goldens and sidecars (unless Outcome B, P9); `tests/performance/` (policy, applicability,
baselines); `assets/`; `android/`. **Dependency graph:** unchanged.

**P2 — The region layout (normative) and the declared counter value.** Appended at `kCameraUniformFogOffsetBytes +
sizeof(FogData)` = **2,544**; std140; cluster index `c = x + Cx·(y + Cy·z)`.

```
offset 2544  uint4  clusterGrid     { Cx = 12, Cy = 7, Cz = 16, 0 }
offset 2560  float4 clusterSlicing  { zNear, z1 = 3.0, zFar, logScale = (Cz - 1) / ln(zFar / z1) }
offset 2576  uint4  clusterMasks[ceil(Cx*Cy*Cz / 2)]
             // cluster c: element c / 2; words 2*(c % 2) (bits 0-31) and 2*(c % 2) + 1 (bits 32-63)
```

- **12×7×16:** 1,344 clusters, 672 `uint4`.
- **Block total:** 2,576 + 10,752 = **13,328 bytes** (≤ 16,384).
- **Capacity bound:** `ceil(N / 2) · 16 ≤ 16,384 − 2,576`, so **N ≤ 1,726 clusters**.
- **Declared `uniform_write_bytes`:** 13,328 + 128 = **13,456**, up from 2,672.
- **Pre-declared retune (Q7, J3):** 8×5×40 (1,600 clusters, 800 `uint4`). That gives a block of 15,376 bytes and
  `uniform_write_bytes` **15,504**.

`zNear`/`zFar` are the active camera's. `z1 = 3.0 m` is the drafting value of Revision 1's S2-Inv-2 table.

**P3 — Fragment-side association (normative shape).**
- `ndc = clip.xy / clip.w`, with `clip = projection · view · worldPosition`.
- `x = clamp(floor((ndc.x · 0.5 + 0.5) · Cx), 0, Cx − 1)`, and `y` likewise from `ndc.y` as the projection produces
  it. The builder uses the same convention, and P7's transcription pins it.
- `viewZ = −(view · worldPosition).z`.
- `z = viewZ < z1 ? 0 : clamp(1 + floor(ln(viewZ / z1) · logScale), 1, Cz − 1)`.
- The loop walks the 64-bit mask low word then high word, low bit first (`firstbitlow`, then clear that bit).
- The loop body is Stage 1's, byte for byte, including `if (NdotL > 0.0 && atten > 0.0)`.
- No wave intrinsics.

**P4 — The CPU builder** (`scene_extraction.cpp`, beside `extractFrameLightingData()`, frame thread).
- **Inputs:** `FrameLightingData`, the frame's camera matrices, `zNear` and `zFar`.
- **Steps:**
  1. Compute each cluster's world-space AABB from its eight frustum corners. The near slice starts at `zNear` and
     the last slice ends at `zFar`.
  2. For each point light `j`, inflate the radius to **`range · (1 + 10⁻³) + 0.01 m`** (ADR-0122 D4). Find the
     candidate cluster range: xy from the inflated sphere's conservative screen-space bounds, or the full xy range
     when the sphere crosses the near plane; depth from `[d − r′, d + r′]` mapped through P3's slice function.
  3. Set bit `j` in every candidate cluster whose AABB the inflated sphere intersects.
- **Output:** the P2 header plus masks. The writer copies them into the block.
- **Budget:** ≤ 0.10 ms per frame, measured by the `ClusterBuild` zone.

**P5 — Every writer uses the real builder.** All writers in S2-Inv-3 compute masks with P4 from their own camera and
lighting. That includes the Runtime, `frame_capture.cpp`, every PBR fixture and the GPU tests. This is stricter
than Revision 1's permitted all-lights default, and it makes the Bistro golden (via `pbr_normal_map_demo_fixture`)
exercise real clustering (Finding 1). The **all-lights fill** exists only as the control build's patch (P9) and as
a test oracle.

**P6 — The CPU twin.** `computePbrDirectLighting()` gains a trailing `std::uint64_t pointLightMask` parameter whose
default is all lights. Its point loop walks the mask's set bits ascending and keeps Stage 1's `atten > 0` guard
plus the lambda's `NdotL` return. Existing callers and hand-computed tests are unchanged. Its header comment notes
the mask (ADR-0122 D3). `computeLambertianDiffuse()` is untouched.

**P7 — The equivalence test, extended** (GPU-free, `[point-light-rejection]` plus `[clustered-lights]`). M2 adds
test-local transcriptions only:
- (a) P3's cluster assignment from `worldPosition` and test-local camera matrices;
- (b) a brute-force reference builder: for every cluster, every light whose inflated sphere meets the cluster's
  corner AABB;
- (c) the mask-walking loop, with Stage 1's body.

Grid constants are test-local in M2. M3 adds a `static_assert` that the header constants equal them.

**The three conditions, encoded.**
- *Condition 1, margin.* For every corpus fragment, every light with transcribed `atten > 0` is set in the mask of
  the transcribed cluster. **Negative self-check:** a reference builder with radius `0.99 · range` must violate this
  on the corpus.
- *Condition 2, mapping.* The corpus adds fragments sampled inside every cluster of the benchmark camera and three
  seeded random cameras, including points on cluster faces and corners at ±1 and ±2 ULP in ndc and on slice
  boundaries. The property must hold there. **Negative self-check:** an assignment shifted by one cluster in x must
  violate it.
- *Condition 3, index order.* The mask walk with reference masks equals Stage 1's loop transcription **bitwise**, and
  the mask walk with the all-lights mask equals it too. **Negative self-check:** a descending walk must produce at
  least one bitwise mismatch.

**Frozen copies shown faithful before M3.** Stage 1's before/after transcriptions and the frozen twin copy stay
green against unchanged code at M2. The all-lights mask walk equals Stage 1 at M2.

M3 adds:
- the real builder against the oracle: **bit-for-bit equal masks** on the corpus cameras;
- the twin with mask against the mask-walk transcription, bitwise;
- the layout `static_assert`s.

**P8 — Capture viewpoints** (all through `atlantis runtime pause`, `property set`, `runtime step --capture`, as in
Plan 0061 P8):
- **V1:** the benchmark camera: position (−22, 1.7, 22), Euler (0, −0.86, 0).
- **V2:** street level inside the string-light runs: (−15, 1.7, 9), Euler (0, −1.5708, 0), looking +x toward lights
  31–40.
- **V3:** facing the neon signage: (−13, 2.0, 5), Euler (0.3, 0, 0), looking −z and up toward lights 47–57.

Each is captured from the Stage 1 build, the Stage 2 build and the all-lights control at the same window size. The
PNGs must be byte-identical across the three.

**P9 — The all-lights control and Outcome B, step by step.** The control is the Stage 2 build with one local,
uncommitted patch: the builder's output replaced by the all-lights fill (bits 0 … `pointLightCount`−1 in every
cluster). If a golden or capture moves:

1. **Stop.** Record the moved goldens or viewpoints, the configuration, and the failure output (diff image, max and
   mean |Δ|, changed-pixel count).
2. **Build and run the control** on the same tests and viewpoints.
3. **Classify:**
   - the control reproduces the same pixels and values → code-generation drift in the new loop structure; go to 4;
   - the control matches Stage 1 while Stage 2 does not → the association is not conservative; fix it in code
     (builder, margin or mapping), return to M3, and never re-capture;
   - any other pattern → stop and report, unclassified.
4. **Bound:** max |Δ| ≤ 1 of 255 on every channel of every moved golden. Anything larger is a defect fixed in code.
5. **Stop for the owner's ruling.**
6. **On the owner's go,** a dedicated category-1 golden-update PR (goldens and sidecars only), cut from and merged
   into the implementation branch (Plan 0061 J2). It names the difference: "compiler-level rounding difference from
   the cluster bit-walk loop of the four PBR shaders; no change in which lights contribute". It carries the metrics,
   the diff images and the control evidence.

**P10 — Build trees and measurement protocol** (as Plan 0061 P2/P6, with J4's run counts carried).
- **Trees:** worktrees with their own config-B trees, since shader paths are absolute in the executable:
  - **S1:** `origin/main` after this Plan merges, i.e. Stage 1;
  - **PROTO:** S1 plus the uncommitted prototype patch;
  - **PROTO-ALL:** PROTO with the all-lights fill.

  At M5: **S2**, the implementation branch head, plus its all-lights control tree.
- **Runs:** alternating independent `--benchmark bistro --resolution 1920x1080 --warmup 120 --frames 300
  --pin-process on --json` processes, 5 rounds per arm, **10 where two arms' ranges overlap**.
- **Sessions:** one session per comparison, on mains power and an idle machine. A session that straddles a machine
  state change (for example bimodal `cpu_active_ms`) is re-run, not averaged.
- **`L` per build** = median `passes.draw.gpu_ms`(baseline) − median (`no-point-lights`).
- **Retention:** raw JSON stays local (Plan 0059 J11).

**P11 — Gates.**
- **Every milestone that commits:**
  - config A Debug and Release builds with full `ctest`;
  - every golden compared exactly (a move enters P9);
  - the fatal-validation GPU suites;
  - Android `assembleDebug`, which compiles the block's ≤ 16,384-byte `static_assert` on the Android toolchain
    (logging the emulator's `maxUniformBufferRange` if cheap);
  - `--gate --applies`;
  - the path guard (P1's files only).
- **The final gate adds:**
  - config B Release full `ctest`;
  - P8;
  - one Debug Bistro benchmark with validation on;
  - M5's gate run.

## Milestones / Task Breakdown

There is one implementation PR on `feature/0061-stage2-clustered-lights`, cut from freshly fetched `origin/main` after
the Joint Human Review. M1 makes no commits. M2 and M3 commit, and each runs its gate before the next starts.

### M1 — Prototype checkpoint (S2-T1; the decision table) — **hard stop at the end**

1. Build **S1** and record its `L` baseline in the checkpoint session itself, not from PR #263.
2. Write the **prototype patch** on the PROTO worktree, uncommitted and Runtime path only:
   - P2 layout with the 12×7×16 grid;
   - P4 builder inside the `ClusterBuild` zone;
   - the four shaders' P3 loop, with the other six PBR shaders declaring the block;
   - `frame_capture.cpp` filled.

   Fixtures are not updated: the prototype is measured and captured, not tested.
3. Build **PROTO-ALL** (PROTO plus the all-lights fill).
4. **One measurement session** with five arms: S1 baseline, S1 no-point-lights, PROTO baseline, PROTO
   no-point-lights, PROTO-ALL baseline. Report:
   - `L` for S1 and PROTO;
   - the no-point-lights control (PROTO − S1, ≤ +0.15 ms);
   - the all-lights control (PROTO-ALL − S1, baseline arm, ≤ +0.5 ms);
   - the `ClusterBuild` zone median (≤ 0.10 ms);
   - `frame_ms` p50, `cpu_active_ms`, and every counter, with `uniform_write_bytes` = 13,456 checked.
5. **Captures** at V1–V3 from S1, PROTO and PROTO-ALL. PROTO and PROTO-ALL must match S1 byte for byte. A mismatch
   enters P9's classification, applied to the prototype.
6. **Decision table** (Revision 1, thresholds fixed):
   - **Row A:** `L` ≤ 2.0 ms, the all-lights control frames identical, and the all-lights control ≤ +0.5 ms →
     proceed.
   - **Row B:** 2.0 < `L` ≤ 4.0 ms → the **one retune**: 8×5×40 (P2, J3), re-measured in a fresh session. Report
     both grids; continue only on the owner's go.
   - **Row C:** `L` > 4.0 ms, **or** the all-lights control > +0.5 ms, **or** `ClusterBuild` > 0.10 ms → stop. No
     merge-ready work; Revision 1 returns to review.
7. **The checkpoint report** goes to the owner (in chat, then carried verbatim into the implementation PR as in
   Plan 0061 J1): commands, revisions, session layout, per-arm medians and ranges, the captures' hashes, the
   prototype diff as text, the row and its reasoning, and **the final grid with its declared `uniform_write_bytes`**
   (13,456, or 15,504 after the retune).

**STOP.** No commit on the implementation branch until the owner rules.

*Gate:* none (no commits).

### M2 — Equivalence first (S2-R2, S2-T2)

1. Extend `point_light_rejection_equivalence_tests.cpp` per P7: the three transcriptions; the frustum-sampled corpus
   for four cameras, with faces, corners and slice boundaries at ±1 and ±2 ULP; Conditions 1–3 with their negative
   self-checks.
2. Confirm the frozen copies are faithful against unchanged code: Stage 1's transcriptions, the frozen twin, and the
   all-lights mask walk all equal to Stage 1.

*Gate:* P11.

### M3 — The implementation (S2-R1, S2-R3, S2-R4; ADR-0122 D1–D5)

Two commits, each bisectable:

- **M3a — the carrier, behaviour-preserving.**
  - P2 layout and constants with their `static_assert`s;
  - the P4 builder, and its call in `runFrame()` under `ClusterBuild`;
  - the region write in `runFrame()` and `frame_capture.cpp`;
  - every test writer filling the region with the builder (P5);
  - the ten PBR shaders declaring the extended block, with **no loop change yet**;
  - the 11/11 cross-check extended to the region;
  - P7's builder-against-oracle cases.

  Expected: goldens identical; `uniform_write_bytes` 13,456.
- **M3b — the association.**
  - the four shaders' P3 cluster lookup and bit-walk loop;
  - the twin's mask walk (P6), in the same commit;
  - P7's twin-against-transcription case.

  Reflection JSON is compared against M3a: the block layout is unchanged by M3b.

*Gate:* P11 after each commit.

### M4 — The picture contract (S2-R2, S2-T3, S2-T4, S2-T5)

1. **Outcome A:** all 67×2 golden comparisons byte-identical in config A Debug and Release, and in config B Release.
   `git diff --stat tests/image_regression/goldens` is empty.
2. **P8 captures** at V1–V3: Stage 2, S1 and the all-lights control all byte-identical.
3. The 11/11 reflection cross-check green, with the region included.
4. **Only if anything moved:** P9 in full, including the owner stop.

*Gate:* P11.

### M5 — Measurement and gate (S2-R5, S2-R6, S2-T6, S2-T7)

1. **Before/after,** one session, five arms: S1 baseline, S1 no-point-lights, S2 baseline, S2 no-point-lights,
   S2-ALL baseline (the all-lights control). Report:
   - `L` for S1 and S2 against the **target ≤ 2.0 ms** and the **expectation 0.7–1.5 ms**;
   - both controls against their bounds;
   - the `ClusterBuild` zone;
   - `gpu_busy_ms`, `frame_ms` p50 (expected 19–20 ms), `cpu_active_ms`, and every counter.

   If `L` > 4.0 ms (the abort line), stop: no merge. A miss between 2.0 and 4.0 is reported as a miss.
2. **Gate,** `atlantis_runtime --gate --json` on the final commit from a clean configure (config B Release). The
   required, pre-declared outcome:
   - **`verdict: fail`**;
   - `summary.regressed` **exactly `["uniform_write_bytes"]`**, candidate **13,456** (or the checkpoint-declared
     15,504), baseline 2,672;
   - every other counter identical; `fragment_invocations` within its 0.1 %;
   - `cpu_active_ms` and `zones.RenderExtraction` within tolerance;
   - GPU changes under `reported`.

   **Anything else stops the work** and is reported, including `fragment_invocations` leaving its tolerance. No
   baseline is recorded and `gate_policy.json` is not edited.
3. `--gate --applies` on the change set (expected `applies: true`), the Debug Bistro validation run, Android
   `assembleDebug`, and the final diff review against P1.

*Gate:* the final gate (P11).

## Files / Modules Touched (expected)

As in P1.

- **New:** none in the implementation (the test file is extended); `docs/plans/0061-stage2-clustered-lights.md` (this
  PR).
- **Changed in this PR:** `docs/specs/0061-point-light-cost.md` (status and approval metadata only),
  `docs/adr/0122-clustered-light-association.md` (status and acceptance only), `docs/specs/README.md` (row).
- **Under Outcome B only:** goldens and sidecars, in their own PR.

## Sequencing & Dependencies

- Joint Review → **M1 → owner checkpoint (hard stop)** → M2 → M3a → M3b → M4 → M5. Build and test after every commit.
- M2 precedes M3, so the oracle and transcriptions are proven against unchanged code.
- M3a precedes M3b, so the carrier is shown behaviour-preserving before the association switches on.
- **Post-merge follow-ups** (named, not tasks; each owner-instructed only):
  - **one folded category-1 baseline refresh PR**, which refreshes `bistro/baseline` (reason: performance change —
    Stage 1's −20 % and Stage 2's clustered association, `uniform_write_bytes` 2,672 → 13,456 and `L` −X ms) and
    records the **`no-point-lights` baseline** (Q11);
  - the **registry/blueprint docs update**, which also records Plan 0061's (Stage 1) and this Plan's Joint Reviews in
    their headers.

## Verification Checklist

- [ ] **S2-R1 structure (M1, M3):** builder per P4 in `ClusterBuild`; the real builder equals the oracle bit for bit
  (P7); budget ≤ 0.10 ms (M1, M5).
- [ ] **S2-R2 equivalence (M2–M4):**
  - mechanical: P7's three conditions with their negative self-checks;
  - GPU: **67 × 2 goldens byte-identical, Debug and Release** (plus config B), the golden directory untouched; V1–V3
    captures byte-identical across S1, S2 and the all-lights control;
  - if anything moved: P9 step by step, ending in the owner's ruling.
- [ ] **S2-R3 carrier (M3a):** the P2 layout pinned by `static_assert`s; block ≤ 16,384 (also compiled on Android);
  the 11/11 cross-check extended; every S2-Inv-3 writer fills the region with the builder; no RHI, binding or pass
  change (path guard).
- [ ] **S2-R4 association (M3b):** diff review — the four loops identical to one another, the P3 shape, Stage 1's
  body unchanged; the twin per P6; reflection unchanged against M3a.
- [ ] **S2-R5 effect (M1, M5):** `L` against target, expectation and abort line; both controls; frame p50 reported.
- [ ] **S2-R6 verdicts (M5):** the gate's `fail` is exactly `uniform_write_bytes` = 13,456 (or 15,504), with
  everything else ok; CPU tier ok; no baseline or policy touched.
- [ ] **S2-R7 scope:** no wave intrinsics, RHI change, RenderGraph pass, job system or geometry culling (diff
  review).
- [ ] **Validation Layers:** fatal-validation GPU suites at every gate; one Debug Bistro run with validation on.
- [ ] **Android:** `assembleDebug` at every gate.
- [ ] **Applicability:** `--gate --applies` → `applies: true`.

## Risks

- **The prototype lands in row B or C.** That is a finding: one retune is pre-declared, and row C returns Revision 1
  to review.
- **Wave divergence across clusters** costs more than estimated. It is caught by the all-lights control and `L` at
  M1; Q8's remedy is excluded by ruling, so only a Spec revision could add it.
- **The real builder in every writer** (P5) means more test churn than the all-lights default would. It is
  deliberate, and keeps the Bistro golden meaningful.
- **`fragment_invocations` drift.** Stage 1 moved it +0.086 %. If Stage 2 pushes it past 0.1 %, the declared verdict
  is not met and the work stops for the owner (M5).
- **Budget.** The block's ≤ 16,384-byte `static_assert` makes any future overrun a compile error.
- **Machine state.** Every comparison runs in one alternating session, and straddling sessions are re-run.

## Rollback Plan

Revert M3b alone to restore Stage 1's loop: the carrier stays inert and behaviour-preserving. Reverting M3a removes
the region, the builder and the counter change. No data, format or baseline migration is involved, as long as the
post-merge refresh has not landed. If it has, the refresh is reverted with it.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md). Deltas:

- The implementation PR carries:
  - the M1 checkpoint report verbatim and the owner's ruling;
  - per-gate results;
  - the P7 counts;
  - the V1–V3 capture hashes;
  - the M5 five-arm table;
  - the gate document showing the declared `fail` and nothing else.
- The prototype and control patches, benchmark JSON and captures are PR evidence, never committed.
- The post-merge follow-ups are listed in the PR as owner-instructed next steps.

## Joint Review questions

- **J1 — The `runFrame()` diff guard.** Stage 2 adds the builder call and the region write inside `runFrame()`
  (Finding 2). **Recommend** confirming the guard was Plan 0059's instrumentation-PR check, not a standing ban. The
  change is confined to the existing region-write block, and the PR shows `runFrame()`'s diff explicitly.
- **J2 — The real builder in every writer** (P5), instead of the all-lights default Revision 1 permits for fixtures.
  **Recommend P5:** it is stricter, and it is what makes the Bistro golden exercise clustering.
- **J3 — The pre-declared retune grid** is 8×5×40, the lowest mean lights per cluster within the budget, with coarse
  tiles that limit xy-boundary divergence. Fixing it now avoids choosing after seeing the numbers. **Recommend
  8×5×40.**
- **J4 — Finding 1.** Revision 1's S2-R3 parenthetical misnames the goldens' render paths, and Stage 1's text claimed
  `multi_light_demo` exercised rejection. **Recommend** recording the correction here only. The requirement's intent
  is met by P5, and no Spec semantics change. Alternatively, add a Spec correction note in the post-merge docs PR.
