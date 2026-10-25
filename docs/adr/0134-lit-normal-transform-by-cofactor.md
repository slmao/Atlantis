# ADR 0134: Lit Normals Transform by the World Matrix's Cofactor; Only a Degenerate Matrix Is Skipped

- **Status:** Accepted
- **Date:** 2026-10-26 (accepted 2026-10-26)
- **Deciders:** slmao
- **Acceptance:** slmao, 2026-10-26 (owner rulings on Spec 0067 Q1–Q5, each as recommended, with Q2's thresholds
  stated numerically; recorded once in [Plan 0067](../plans/0067-lit-normal-transform.md); the ADR text was reviewed
  in [PR #296](https://github.com/slmao/Atlantis/pull/296)).
- **Related Spec:** [Spec 0067: Lit Normals Under Any World Matrix](../specs/0067-lit-normal-transform.md)
- **Related ADR(s):**
  - **Supersedes** [ADR-0062](0062-runtime-frame-lighting-data-and-rhi-uniform-buffer-stage-visibility.md)
    Decision 4: a normal transformed by the world 3×3 directly, and a lit entity with a non-conformal world matrix
    skipped. ADR-0062's other decisions are unchanged.
  - **Amends the reliance of** [ADR-0074](0074-pbr-normal-map-material-descriptor-and-shader-contract.md)'s tangent path
    on that gate. Tangents still transform by the world 3×3; the gate it named becomes this ADR's D2.
  - **Amends in part** [ADR-0123](0123-editor-viewport-camera-and-picking-host-services.md)'s amendment of 2026-10-25:
    its rule ("the service picks what the frame draws") stays, and its predicate ("a non-conformal world matrix")
    becomes D2's. ADR-0123 records this as its own second dated amendment (2026-10-26), so the two ADRs agree.

Record one decision and its rationale. Follow the
[ADR lifecycle](README.md); keep approval discussion and execution evidence in
PRs. After acceptance, a changed decision requires a new superseding ADR.

## Context

- **The old rule.** ADR-0062 Decision 4 (Spec 0019 D7) chose the cheapest normal transform that is correct for the
  verification scenes of that time: the world 3×3 `M` applied directly.
  - That is correct only when `M` is conformal (rotation times uniform scale), so a lit entity with any other matrix
    is skipped every frame rather than shaded wrong.
  - ADR-0062 named the inverse-transpose as the disclosed alternative "if non-uniform-scale-or-sheared lit entities
    are expected to be common".
- **What the skip hides on Bistro** (measured 2026-10-25): 368 of 2909 Renderables, about 1.37 M triangles (+25 % of
  the frame).
  - Each inherits a near-uniform scale (s, s, 1), s ≤ 1.0244, from an ancestor group node.
  - Plan 0046 M3 measured and pinned the count ("not endorsed") without a follow-up. Spec 0066's visual check surfaced
    it again, and [PR #295](https://github.com/slmao/Atlantis/pull/295) then made picking skip them too.
- **What the editor can do:** E2's Scale Gizmo and any client can author a non-uniform scale on a lit entity, which
  then vanishes.
- **What a fix must preserve:**
  - no CPU-side matrix-inverse library exists (Core and World have no 3×3 inverse);
  - `PbrDirectLit`'s push-constant range is already 96 bytes;
  - the same shaders compile for Android;
  - goldens of conformal scenes are held to 0/0 (ADR-0042).

## Decision

- **D1 — Normal transform.** A lit vertex shader transforms a vertex normal `n` by the cofactor of the world 3×3 `M`.
  - With `M`'s columns `c0, c1, c2`, the shader computes `n' = sign(det M) · [(c1×c2)·n.x + (c2×c0)·n.y + (c0×c1)·n.z]`,
    where `det M = c0·(c1×c2)`.
  - The matrix `[c1×c2 | c2×c0 | c0×c1]` is `det(M)·(M⁻¹)ᵀ`, so `n'` has the inverse-transpose direction for every
    invertible `M`, including non-uniform scale, shear and reflections.
  - For a conformal `M = s·R` it has the direction of `M·n`, today's result.
  - It is computed in the vertex shader from `objectToWorld`: no new push constant, uniform, attribute or CPU math.
  - Tangents still transform by `M` (a surface direction transforms by `M` for any `M`). The fragment stage's
    normalization and Gram-Schmidt are unchanged.
- **D2 — One predicate, degenerate only.** A lit (`LitTextured`- or `PbrDirectLit`-bound) entity is skipped only when
  its world 3×3 is degenerate:
  - a near-zero column: `|ci| < kDegenerateLengthEpsilon` (1e-6, the existing constant);
  - or near-coplanar columns: `|det M| < k·|c0||c1||c2|` with **k = 1e-4**. The ratio is 1 for any matrix with
    orthogonal columns whatever its scale, and falls only as the columns approach a plane (a 45° shear gives 0.71).
    float32 cancellation makes det's sign unreliable around 1e-6, so 1e-4 keeps two orders of margin above it.
  - The frame's extraction, the capture path and `pickRenderable()` call the one predicate,
    `checkInvertibleNormalTransform()`, which replaces `checkConformalTransform()`; its error,
    `DegenerateWorldTransform`, replaces `NonConformalNormalTransform`.
  - Unlit and material-less entities are still never checked.

## Consequences

### Positive

- Every invertible lit entity is drawn with correct normals: Bistro gains its 368 missing entities, and the editor's
  non-uniform scale no longer makes an entity disappear.
- No asset, scene-grammar, catalog, importer, push-constant or CPU-math change; the fix is the same on Android.
- One predicate decides drawing, capturing and picking, so the three cannot disagree.

### Negative / Trade-offs

- About 20 more ALU operations per lit vertex in eleven shaders.
- Bistro draws +1.37 M triangles: a real frame cost, and the gate's counters change, which needs a dedicated baseline
  refresh.
- Conformal scenes compute the same direction through different arithmetic, so a golden may move by one 8-bit step in
  a few pixels. Any such golden is re-captured as a reviewed rendering change.
- The formula is repeated verbatim in each lit shader (no shader include exists today). A drift test holds the eleven
  copies byte-identical to one reference text, and a CPU mirror test pins the formula.

## Alternatives Considered

- **A CPU-computed normal matrix per draw.** Exact, but needs a 3×3 inverse in Core or World and grows every lit
  pipeline's push-constant range by 48 bytes.
- **Baking non-uniform scale into vertices at glTF import.** Measured on Bistro:
  - it needs 351 mesh variants, +1.43 M vertices (+82 % of the scene's mesh vertex data), and a new GUID-derivation
    rule for variants;
  - it does nothing for scales authored in the editor.
- **Widening the conformal tolerance** to about 3 % anisotropy (normal error under about 0.7°). One constant, but it
  covers only near-uniform scale, and the threshold is arbitrary.
- **Keeping the skip** (ADR-0062 Decision 4 as is). Bistro stays a quarter short, and the editor keeps losing entities.
