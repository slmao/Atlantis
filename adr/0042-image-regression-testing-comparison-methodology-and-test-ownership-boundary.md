# ADR 0042: Image Regression Testing — Comparison Methodology, Golden Provenance, and Test Ownership Boundary

- **Status:** Proposed
- **Date:** 2026-08-16
- **Deciders:** _Pending Human Review_
- **Related Spec:** [specs/0011-image-regression-testing-foundation.md](../specs/0011-image-regression-testing-foundation.md)

## Context

- Spec 0010 delivered the render-and-readback foundation but explicitly
  deferred "golden-image comparison, tolerance methodology, or CI
  image-regression gating of any kind" to a future spec — this one (see
  Spec 0010's own Non-Goals).
- [docs/process/testing-strategy.md](../docs/process/testing-strategy.md)
  already commits to a golden-image comparison layer ("layer 3") and
  names two things not yet decided: where goldens live under `tests/`
  (path "to be fixed by that harness's spec") and the comparison method
  ("exact pixel match vs. perceptual diff vs. SSIM/threshold-based ...
  not yet decided and must be settled by the spec that introduces the
  regression harness").
- [AGENTS.md](../AGENTS.md)'s Definition of Done already requires "Image
  regression tests added/updated if rendered output changed, and any
  golden-image diffs in the PR were reviewed by a human, not
  auto-accepted" — an existing, standing rule this ADR must satisfy, not
  invent.
- Atlantis's current Phase 1 rendering is a single, static,
  non-antialiased, non-time-varying, non-random, fixed-camera mesh draw
  (Spec 0007's and Spec 0010's own verification fixtures) — there is no
  currently-shipping scene with animation, transparency blending, or
  multisampling whose output is inherently non-deterministic frame to
  frame on the same hardware.
- Only one real GPU (Intel Arc B370, integrated, Windows) has ever been
  available to this project — see Spec 0010's own disclosed
  single-GPU-vendor verification limitation. No second vendor/driver
  exists to test or design cross-vendor reproducibility against.
- No CI pipeline exists (confirmed: no `.github/workflows/` directory in
  this repository) — see
  [docs/process/ci-strategy.md](../docs/process/ci-strategy.md). Today's
  actual enforcement mechanism for any GPU-touching correctness gate is
  a human (or agent) running the relevant test suite locally/
  interactively and reporting the result, exactly as Spec 0010's own
  "Manual verification record" did for its interactive windowed
  regression requirement.

## Decision

**Comparison algorithm.** Per-pixel, per-channel (R, G, B, A) absolute
byte difference between a freshly captured buffer and its golden, both
in the identical `atlantis::rhi::Format` and extent the capturing scene
declared — a format or extent mismatch between actual and golden is a
hard failure, never silently resized or reformatted. A pixel passes if
every channel's absolute difference is within a fixed tolerance
(starting value: 2, out of 255 — an explicit placeholder this ADR
proposes for empirical confirmation against real repeated-capture
evidence during the Plan/Implementation stage, not asserted as final
here). **Every pixel in the image must pass — there is no pixel-ratio/
percentage slack budget in this Phase 1 design.** A single
out-of-tolerance pixel anywhere fails the comparison.

**Failure output.** A failing comparison must produce, at minimum:
pass/fail; per-channel and combined max and mean absolute difference;
the count and percentage of pixels exceeding tolerance; the actual
captured image (written as its own PNG); and a diff image (per-pixel
absolute difference, amplified for visibility) — all written to a
predictable, documented build-output location (exact path fixed by the
Plan) so a human can inspect a failure without reproducing it
interactively, and so a future CI job can upload them as artifacts per
[ci-strategy.md](../docs/process/ci-strategy.md)'s existing "Image
regression tests" bullet.

**Golden image ownership and location.**
`tests/image_regression/goldens/<scene-slug>/<golden-name>.png`, one PNG
per golden, each accompanied by a sidecar file (exact serialization left
to the Plan; JSON is the working assumption) recording: capture date,
git commit hash, GPU vendor/model, driver version, Vulkan instance/
device API version, OS build, and the exact `OffscreenTarget` extent/
format used. No golden file may be regenerated or overwritten by an
automated test/CI run under any circumstance; regenerating one is a
distinct, explicitly-invoked, never-default-on developer action (exact
mechanism left to the Plan) whose resulting diff is reviewed like any
other code change in its own PR — matching
[testing-strategy.md](../docs/process/testing-strategy.md)'s existing
rule verbatim ("never a silent regeneration step that a CI job runs and
commits automatically"). A PR that touches any file under
`tests/image_regression/goldens/` must state why in its own
description; enforcing this is a human-review/process rule today, not a
technical control — no CI or branch-protection/CODEOWNERS mechanism
exists yet to enforce it automatically, and provisioning one is a
future, separate action this ADR does not fabricate.

**Golden set strategy.** One unified golden set, captured on and
compared only against the one real GPU vendor/family this project has
ever verified against (Intel Arc / integrated, Windows Vulkan) — no
per-vendor golden branching is designed or implemented by this
decision, because no second vendor exists to design it against. This is
a disclosed, explicit limitation carried forward from Spec 0010, not a
claim of cross-vendor stability. A future spec/ADR revisits per-vendor
golden sets if and when a second real GPU vendor becomes available to
this project.

**Reproducibility constraints on any scene used for image regression
testing.** No wall-clock time, RNG, or uninitialized-memory dependence
in camera, transform, or material state — every input a covered scene's
output depends on must be a fixed, checked-in constant. This decision
adopts Spec 0010's own existing `examples/headless_rendering_demo`
fixture (fixed cube, fixed camera, fixed material, fixed background
clear color, 512×512 `Rgba8Unorm`) as the first golden-image target
scene, rather than inventing a new one. Determinism is verified
empirically, not assumed: the same cycle repeated multiple times against
the same `OffscreenTarget` instance must produce byte-identical (within
tolerance) captures — extending Spec 0010's own established "repeated
cycle produces consistent results" verification from its coarse content
check to a real pixel comparison.

**Test/module ownership boundary.** A new test-suite area,
`tests/image_regression/`, parallel to the existing `tests/core/`,
`tests/rhi/`, `tests/render_graph/`, `tests/renderer/`,
`tests/vulkan_backend/` areas — **not a new top-level `src/` module**,
and no existing module's public dependency set changes. Internally
layered per
[testing-strategy.md](../docs/process/testing-strategy.md)'s own layer
split (exact file/target structure left to the Plan):
- **GPU-independent:** the pixel-diff/tolerance algorithm, PNG
  encode/decode round-trip correctness, and provenance-sidecar parsing,
  exercised against synthetic in-memory buffers with no Vulkan device —
  Catch2, matching every other GPU-independent suite in this repository.
- **GPU-required:** the real capture-via-`OffscreenTarget` →
  compare-against-checked-in-golden path, labeled `gpu` per the existing
  ctest convention Spec 0006/Spec 0010 already established — no new
  CI/test-category label is introduced by this decision (Spec 0010's own
  open question about a distinct headless-GPU-test label remains open,
  unresolved by this ADR).

The `stb` dependency [ADR-0041](0041-image-regression-testing-golden-image-data-format-and-codec-dependency.md)
introduces is linked only into these test-support targets.

**Gating today vs. CI gating.** This decision defines what a
**local/manual gate** looks like today: a human or agent runs the
`gpu`-labeled image regression suite against real Windows/Vulkan
hardware and reports pass/fail, exactly as every other GPU-touching
verification in this project already works absent CI (Spec 0010's own
"Manual verification record" precedent). **Automated CI enforcement of
this gate is explicitly not decided or implemented by this ADR** — it
remains blocked on prerequisites
[ci-strategy.md](../docs/process/ci-strategy.md) already logs as open
(GPU access in CI: a software Vulkan implementation vs. a self-hosted
real-GPU Windows runner; the dependency-fetch-in-CI strategy) and on
actual infrastructure provisioning, neither of which this Spec/ADR
resolves or fabricates. Once that infrastructure exists, this decision's
failure-output contract (above) is what a future CI job uploads as
artifacts.

## Consequences

### Positive

- A single, strict, unambiguous per-pixel-must-pass rule is simple to
  implement, simple to reason about, and cannot silently mask a small
  but real localized regression — directly satisfies the requirement
  that a golden-image gate must not degrade into "roughly looks the
  same."
- Reusing Spec 0010's own already-verified fixture as the first golden
  scene means the harness proves itself against known-good, already
  human-reviewed rendering output rather than a newly authored,
  unreviewed scene.
- CI-agnostic at its core (a pass/fail function plus a documented
  artifact contract) — a future build-system/CI spec can wire automatic
  invocation on top without this ADR needing revision.

### Negative / Trade-offs

- Zero pixel-ratio slack is strict: if Phase 1 ever adds anti-aliasing,
  transparency, or any other source of legitimate sub-pixel
  nondeterminism, this exact-per-pixel rule will need revisiting
  (flagged explicitly in Spec 0011's own Risks & Open Questions, not
  solved here).
- A single, unified (not per-vendor) golden set means this harness
  cannot yet be run meaningfully against any GPU other than the one this
  project has verified — a real, disclosed capability gap, not hidden by
  this decision.
- Manual/local gating (vs. CI-enforced) depends on a human or agent
  remembering to run the suite before merging — the same limitation
  every other GPU-touching test in this project already has today, not a
  new one this ADR introduces.
- Provenance sidecar files add a second file to review/maintain per
  golden, rather than embedding metadata in the PNG itself (e.g. via
  `tEXt` chunks) — chosen for easy diffability in an ordinary PR review,
  at the cost of one more file per golden.

## Alternatives Considered

- **Percentage-based pixel tolerance** (e.g., "≥99.9% of pixels within
  tolerance passes"). Rejected for Phase 1: this project's own static,
  non-antialiased scenes have no legitimate source of widespread
  sub-pixel noise that would need this slack, and any slack budget risks
  silently absorbing a real, small, localized regression — exactly what
  this ADR is designed to avoid. May be revisited by a future ADR once
  Phase 1 introduces a genuine source of pixel-level nondeterminism
  (anti-aliasing, transparency).
- **SSIM or another perceptual-similarity metric.** Rejected: perceptual
  metrics are designed to tolerate exactly the kind of small, structured
  difference (a shifted or recolored region) that a rendering regression
  often looks like — the wrong tool for a correctness gate whose entire
  purpose is catching small, real differences.
- **Per-vendor golden sets, decided now.** Rejected: no second GPU
  vendor exists in this project's environment to design or verify a
  per-vendor strategy against; deciding one now would be speculative,
  not evidence-based — left as explicit Future Work.
- **Embedding provenance directly in PNG metadata chunks instead of a
  sidecar file.** Considered; rejected for Phase 1 in favor of a
  separate, plain-diffable sidecar — a PR reviewer can read a sidecar's
  provenance change in an ordinary text diff, whereas a PNG chunk change
  is invisible in a normal `git diff` and requires tooling to inspect.
- **A new top-level `src/` module** (e.g. an "Atlantis Image
  Regression" or general "Testing" module). Rejected: this is
  test-support infrastructure with a single consumer (this project's own
  test suites), not a runtime/engine capability any product module
  depends on; [AGENTS.md](../AGENTS.md)'s module list is deliberately
  not extended for it, matching Spec 0011's explicit instruction not to
  introduce an unreviewed new top-level module.
- **Designing and asserting CI gating as already actionable.** Rejected:
  no CI pipeline, no GPU CI runner, and no resolved dependency-fetch-in-
  CI strategy currently exist (verified: `.github/workflows/` does not
  exist in this repository); asserting otherwise would fabricate
  infrastructure, contrary to [AGENTS.md](../AGENTS.md)'s Golden Rule.
