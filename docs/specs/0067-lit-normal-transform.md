# Spec: Lit Normals Under Any World Matrix — Retire the Non-Conformal Skip

- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-10-26
- **Related Plan(s):** [Plan 0067](../plans/0067-lit-normal-transform.md) (`Approved`; Joint Human Review 2026-10-26
  recorded in its header). As with Specs 0062–0066, the Plan was drafted together with this Spec.
- **Approval:** slmao, 2026-10-26 — owner rulings Q1–Q5, each as its recommendation, with Q2's thresholds stated
  numerically and Q3/Q4 refined as recorded below; recorded once in [Plan 0067](../plans/0067-lit-normal-transform.md).
  Reviewed in this Spec's own branch PR, [PR #296](https://github.com/slmao/Atlantis/pull/296), where the rulings are
  recorded before it merges. The open-question text keeps the options as the record of what was weighed.
- **Related ADR(s):** [ADR-0134](../adr/0134-lit-normal-transform-by-cofactor.md) (`Accepted` 2026-10-26): supersedes
  [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)'s Decision 4, and with
  it the conformal gate that [ADR-0074](../adr/0074-pbr-normal-map-material-descriptor-and-shader-contract.md)'s tangent
  path relies on. [ADR-0123](../adr/0123-editor-viewport-camera-and-picking-host-services.md) gains a second dated
  amendment (2026-10-26) so its picking rule names ADR-0134 D2's predicate.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

**The problem.** A lit entity (`LitTextured` or `PbrDirectLit`, and the PBR variants built on it) whose world matrix is
not conformal (not rotation times uniform scale) is never drawn. The frame's extraction skips it every frame, because
the lit shaders transform normals by the world matrix's 3×3 directly, which is wrong for a non-uniform scale or a shear
(Spec 0019 D7, [ADR-0062](../adr/0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
Decision 4).

**What it hides today.** On Bistro, 368 of 2909 Renderables are invisible this way: about 1.37 million triangles of
street lights, trees, furniture and merged geometry. Their scales are almost uniform (at most 2.4 % apart). The editor
can also make any lit entity vanish by giving it a non-uniform scale.

**This Spec proposes:** transform lit normals by the 3×3's cofactor matrix, the inverse-transpose direction computed
from three cross products in the vertex shader. That is correct for every invertible matrix. The extraction then stops
skipping all but degenerate matrices, the capture path and picking follow it, and Bistro's missing geometry is drawn.

## Motivation / Problem Statement

- **Found by** Spec 0066's M6 visual check: a Bistro pick outlined an undrawn entity. [PR #295](https://github.com/slmao/Atlantis/pull/295)
  made picking agree with the skip (ADR-0123's amendment of 2026-10-25). The skip itself remains.
- **Measured on Bistro** (2026-10-25, from the generated scene source and `bistro.gltf`), for the 368 non-conformal
  Renderables:
  - **where the scale comes from:** every one inherits its non-uniform scale from an ancestor group node; none carries
    it on its own node;
  - **no shear:** no rotation sits below the non-uniform scale, so every world 3×3 is rotation times a per-axis scale;
  - **how non-uniform:** every scale has the form (s, s, 1) with s between 1.002 and 1.0244 (median axis ratio 1.0038,
    p90 1.0124);
  - **what they are:** 271 merged-geometry chunks, 55 street lights, cypress trees, wicker furniture, napkin holders,
    doormats and menu signs. Together they are about 1.37 million triangles a frame, about +25 % on what Bistro draws
    today;
  - **mesh reuse:** 162 distinct meshes, 32 of them also drawn by conformal instances; 351 distinct (mesh, linear
    part) pairs.
- **Why now:** Bistro has been missing a quarter of its geometry since Spec 0046. Plan 0046 M3 measured it and pinned it
  in Bistro's fixture test (`2909 - 368` draw items, "pinned here as measured, not endorsed"), but it was never
  followed up. Meanwhile the editor (E2's Scale Gizmo, the future E3 editor) can author non-uniform scale on any lit entity, which
  then disappears.

## Goals

- Every lit entity with an invertible world matrix is drawn with correct normals, whatever its scale or shear.
- One predicate decides which lit entities the frame skips, now only degenerate matrices, and the frame, the capture
  path and picking all use it.
- No asset, scene-grammar, catalog or importer change; no change to unlit, shadow or overlay shaders.

## Non-Goals

- **Baking non-uniform scale into meshes at import** (the direction first named on 2026-10-25). It is answered by
  Q1 and kept as an alternative.
- **Negative-determinant tangent handedness** for normal-mapped materials (ADR-0073 defers it). This Spec keeps
  today's behaviour.
- Any change to materials, lighting math, the BRDFs or shadowing.
- A CPU-side normal-matrix library (Core or World 3×3 inverse).

## Requirements

### Functional

- **R1 — Normal transform.** Every lit vertex shader (the eleven listed in Plan 0067) transforms the vertex normal by
  `sign(det M) · cof(M)`, where `M` is `objectToWorld`'s 3×3 and `cof(M) = [c1×c2 | c2×c0 | c0×c1]` (columns `ci` of
  `M`), which equals `det(M)·(M⁻¹)ᵀ`.
  - Tangents still transform by `M`, which is correct for any `M`.
  - The fragment stage's normalization and Gram-Schmidt are unchanged.
  - For a conformal `M` the direction equals today's.
- **R2 — One skip predicate.** The extraction skips a lit entity only when its world 3×3 is degenerate: a near-zero column (`|ci| < kDegenerateLengthEpsilon`, the existing 1e-6), or near-coplanar columns (`|det M| < k·|c0||c1||c2|` with **k = 1e-4**: that ratio is 1 for any matrix with orthogonal columns whatever its scale, and falls only as the columns approach a plane — a 45° shear still gives 0.71 — while float32 cancellation makes det's sign unreliable around 1e-6, so 1e-4 keeps two orders of margin above it).
  - The frame's extraction (`runFrame()`), the capture path (`frame_capture.cpp`) and `pickRenderable()` all call the
    same predicate; ADR-0123's 2026-10-25 amendment ("the service picks what the frame draws") then holds with it.
  - `checkConformalTransform()` becomes `checkInvertibleNormalTransform()`, and
    `SceneExtractionError::NonConformalNormalTransform` becomes `DegenerateWorldTransform` (Q2).
- **R3 — Image-regression fixtures.** The fixtures that mirror the extraction's skip use R2's predicate.
- **R4 — Editor.** A lit entity scaled non-uniformly by E2's Scale Gizmo or over the wire stays drawn and shaded
  correctly. This follows from R1 and R2.

### Non-functional

- **Performance:** about 20 extra ALU operations per lit vertex (three cross products, one dot); no new push constant,
  uniform or attribute.
  - Bistro draws 368 more entities (+1.37 M triangles), a real and intended cost.
  - The gate's counters (`draw_calls`, `triangles`) change, so the gate fails on counters until the baseline is
    refreshed (Q4).
- **Memory:** none added.
- **Portability:** the same shader source compiles for Windows and Android (Shader System). No new device feature.
- **Determinism:** conformal scenes keep their normals' direction. Pixel values may move by float rounding only (Q3).

## Proposed Design

- **Shaders (R1).** In each lit `vertexMain()`, replace `mul((float3x3)objectToWorld, input.normal)` with a shared
  `cofactorNormal(objectToWorld, input.normal)`:
  1. take the 3×3's columns `c0, c1, c2`;
  2. compute `n' = (c1×c2)·n.x + (c2×c0)·n.y + (c0×c1)·n.z`;
  3. multiply by `sign(dot(c0, c1×c2))`.

  No shader in the repository includes another today (each lit variant is an exact twin carrying its own copies), so
  the helper is repeated verbatim in all eleven, and a CPU-mirror test pins the formula. Adding include support would
  be a Shader System change (Plan 0067 J2).
- **Extraction (R2).** `checkConformalTransform()` becomes a degenerate-only check (Q2).
  - The two `MaterialKind` conditions stay, so unlit entities are still never checked.
  - `runFrame()`'s extraction, `frame_capture.cpp` and `pickRenderable()` call it.
  - These are `runFrame()` lines and are enumerated in the PR.
- **Fixtures (R3).** They follow the extraction.
- **Goldens and baseline:**
  - Bistro's golden changes, because 368 entities appear: a rendering change (category 1), re-captured and
    human-reviewed in the implementation PR.
  - Every other golden is compared; any that moves by rounding is re-captured the same way, with its diff metrics.
  - The performance baseline is refreshed in its own dedicated PR (category 1, performance change) after the
    implementation merges (Q4).

## Architectural Impact

**Yes:** the Renderer's lit shader contract (how a normal is transformed) and the Runtime's skip rule change.
- [ADR-0134](../adr/0134-lit-normal-transform-by-cofactor.md) records the decision:
  - it supersedes ADR-0062 Decision 4 (normal by the 3×3 directly, skip if not conformal);
  - it amends the conformal-gate reliance in ADR-0074's tangent path.
- No module boundary, dependency, threading or ownership change. No public API shape changes beyond the renamed
  extraction error and predicate (Q2).

## Alternatives Considered

The alternatives are listed under Q1 below, with their measured costs.

## Testing & Verification Plan

- **Drift test (GPU-free):** the eleven copies of the cofactor helper are byte-identical to one reference text, as
  the generated bindings are checked for staleness, so a copy cannot drift silently.
- **Unit tests (GPU-free):**
  - the degenerate predicate: accepted are identity, rotation, uniform scale of either sign, non-uniform scale and
    shear; rejected are zero-length and coplanar columns;
  - a CPU mirror of `cofactorNormal`: it agrees with `normalize((M⁻¹)ᵀ n)` for random invertible `M`, and with
    `normalize(M n)` for conformal `M`.
- **GPU tests:**
  - a non-uniformly scaled lit sphere is drawn, and its shading matches the analytic normal at sampled pixels (it was
    skipped before);
  - the lighting demo's deliberately non-conformal entity is now drawn (its test inverts);
  - picking hits a non-conformal entity (the inverse of #295's test), and a degenerate one is neither drawn nor picked.
- **Image regression:** every golden compared on the reference machine.
  - Bistro's re-captured, with a reviewed diff showing the newly drawn geometry.
  - Any other golden that moves re-captured with per-golden diff metrics.
  - No golden changes silently.
- **VVL:** clean in every GPU test.
- **Performance gate:** run and reported. A counter `fail` on Bistro is expected and named, and the baseline refresh
  follows separately (Q4).
- **Android:** `assembleDebug`, since the shaders are packaged there.

## Risks & Open Questions

All five ruled by the owner (slmao, 2026-10-26), each as its recommendation, with the refinements noted; rulings recorded in Plan 0067.

- **Q1 — The approach.**
  - **N-a: cofactor normal in the vertex shader** (recommended). Exact for any invertible matrix, no CPU math, no new
    push constant, about 20 ALU operations per lit vertex. It also covers the editor.
  - **N-b: a CPU-computed normal matrix pushed per draw.** Exact, but needs a 3×3 inverse in Core or World and 48 more
    push-constant bytes per draw on every lit pipeline (`PbrDirectLit`'s range is 96 bytes today).
  - **N-c: bake non-uniform scale into vertices at glTF import.** Bistro-only and runtime-free, but 351 mesh variants
    (+1.43 M vertices, +82 % of the scene's mesh vertex data), new GUID-derivation rules, and nothing for the editor.
  - **N-d: widen the conformal tolerance** to about 3 % anisotropy (normal error under about 0.7°). One constant, but
    it only covers near-uniform scale, and the threshold is arbitrary.
- **Q2 — The degenerate predicate and the names.**
  - **D-a** (recommended; **ruled**): keep a per-entity skip only when the world 3×3 is degenerate: a near-zero column (`|ci| < kDegenerateLengthEpsilon`, the existing 1e-6), or near-coplanar columns (`|det M| < k·|c0||c1||c2|` with **k = 1e-4**: that ratio is 1 for any matrix with orthogonal columns whatever its scale, and falls only as the columns approach a plane — a 45° shear still gives 0.71 — while float32 cancellation makes det's sign unreliable around 1e-6, so 1e-4 keeps two orders of margin above it).
    Rename `checkConformalTransform()` to `checkInvertibleNormalTransform()` and `NonConformalNormalTransform` to
    `DegenerateWorldTransform`.
  - **D-b:** no skip at all, letting the shader normalize a near-zero vector (NaN risk).
- **Q3 — Goldens of conformal scenes.** The cofactor path computes the same direction through different arithmetic.
  - Most goldens should stay byte-identical. A few pixels may move by one 8-bit step through float rounding.
  - **G-a** (recommended): measure in Plan 0067 M1 and re-capture any golden that moves, as a rendering change with
    per-golden metrics, alongside Bistro's.
  - **G-b:** a per-golden tolerance instead (rejected: ADR-0042's 0/0 rule).
  - **Ruled: G-a, decided after M1's numbers (Plan 0067 J3).** A golden whose draw counters are identical and whose
    pixels differ only at rounding level is re-captured as a rendering change with human review, in the same PR as
    Bistro's re-capture; anything larger stops for the owner.
- **Q4 — The performance baseline.** Bistro's counters change (+368 draws, +1.37 M triangles).
  - **B-a** (recommended): the implementation PR reports the gate's counter `fail` with the expected deltas; a
    dedicated baseline PR (category 1, performance change) re-records after merge.
  - **B-b:** bundle the baseline (rejected: AGENTS.md requires a dedicated PR).
  - **Ruled: B-a.** The implementation PR's gate fails on Bistro's counters by design and says so; Bistro's frame time
    before and after is reported, not judged; the baseline refresh is its own PR after merge (Spec 0061 Stage 2's
    precedent).
- **Q5 — Picking.** #295 made `pickRenderable()` apply the extraction's conformal skip.
  - **P-a** (recommended): it calls R2's shared predicate, so it again picks exactly what the frame draws (now
    nearly everything). **Ruled: P-a.** ADR-0123's 2026-10-25 amendment names the old predicate ("a non-conformal
    world matrix"), so ADR-0123 gains a second dated amendment (2026-10-26): the rule stays ("the service picks what the
    frame draws"), and the predicate becomes ADR-0134 D2's.
- **Risks:**
  - Bistro's look changes noticeably, with objects appearing that were never seen in this engine. The human golden
    review must judge them on their own (for example the street lights' and trees' materials).
  - +25 % triangles on Bistro is real GPU and CPU cost, especially on Android.

## Out of Scope / Future Work

- Importer-side scale baking (N-c) and any GUID rule for mesh variants.
- Negative-determinant tangent handedness (ADR-0073's deferred case).
- Bistro parity work beyond drawing what it already contains (its materials, its lights).
