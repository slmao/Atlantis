# Testing Strategy

This document defines the strategy test code must follow, and constrains
what a build-system/testing spec is allowed to land as. Layers 1–3 below
are all implemented today (Catch2 unit/integration tests since Spec
0001/ADR-0007; headless integration since Spec 0010; image regression
since Spec 0011); layer 4 (Vulkan Validation Layers as a gate) has
applied since Spec 0003. **Automated, CI-enforced testing of rendered
output is not yet available** — see [ci-strategy.md](ci-strategy.md);
that is the one remaining gap this document's own layers do not close by
themselves.

## Sequencing note

Windowed rendering shipped before headless rendering (see
[AGENTS.md](../../AGENTS.md)), and headless rendering shipped before
image regression testing, per that same sequencing rule. Both have now
landed: layers 2 and 3 below are available today, run locally/manually
against real Vulkan-capable hardware (`ctest -L gpu`). What remains
unavailable is **CI enforcement** of any of this — no CI pipeline exists
in this repository, so verification of the windowed and headless paths
alike is still a human/agent running the suite and looking at the
result, not an automatic gate; see [ci-strategy.md](ci-strategy.md).

## Layers

1. **Unit tests** — algorithmic and data-structure logic that doesn't
   require a GPU (render graph scheduling/validation logic, RHI resource
   bookkeeping, math, etc.). These must not require a Vulkan device to run.
   Available from the start; not blocked on windowed or headless rendering.

2. **Headless integration tests** — exercise the RHI and render graph
   against a real Vulkan device with no window/swapchain, rendering to
   offscreen targets. Implemented (Spec 0010, `Approved`,
   `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`) — this is
   what makes image regression testing (layer 3) and, eventually, CI
   testing of rendered output possible.

3. **Image regression tests** — render a known scene/pass headlessly,
   compare against a golden reference image, fail on divergence beyond an
   agreed tolerance. These are the primary signal that rendering output is
   still correct after a change. Implemented (Spec 0011, `Approved`,
   `tests/image_regression/`) — see "Golden images" below for the
   settled format/location/tolerance this layer actually shipped with.

4. **Vulkan Validation Layers as a correctness gate** — every test that
   touches the GPU runs with validation layers enabled. A clean validation
   run is a pass/fail condition, not advisory output to skim. This applies
   to unit-adjacent GPU tests, integration tests, and manual dev runs
   alike.

## Golden images

- Golden images live under
  `tests/image_regression/goldens/<scene-slug>/<golden-name>.png`, each
  with its own provenance sidecar (capture date, source revision, GPU/
  driver/Vulkan-version fields, extent/format) — settled by Spec 0011/
  [ADR-0042](../../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md).
- Updating a golden image is a reviewed, visible diff in a PR — never a
  silent regeneration step that a CI job runs and commits automatically.
  Regeneration is performed only by a standalone, non-CTest-registered
  tool (`tests/image_regression/golden_generator/`); the ordinary
  comparison test binary is read-only with respect to golden files.
- The comparison method is **exact pixel match, per channel** — channel
  tolerance 0, failing-pixel budget 0, both confirmed by empirical
  calibration against the one reused fixture and reference GPU/driver
  (see ADR-0042's own Context) — not a perceptual/SSIM/threshold-based
  metric. This value is scoped to that fixture and reference environment,
  not generalized to a future scene or different hardware without its
  own calibration evidence.

## Debugging

- RenderDoc is the standard tool for interactively debugging a failing
  frame. This is a developer workflow, not a CI gate — captures are
  triggered manually, not stored/compared automatically.

## Future-phase considerations (not designed yet)

Pixel-exact or tight-tolerance comparison works for deterministic
rasterization. It will not directly work for:

- GPU-driven rendering with data-dependent scheduling (ordering-sensitive
  non-determinism)
- Neural rendering / neural shading (models are inherently approximate)
- 3D Gaussian Splatting (sensitive to floating-point accumulation order)

Each of these will need its own testing-strategy addendum when its phase
starts — do not assume pixel-diff testing generalizes to them, and do not
pre-design for it now.

## Open questions requiring human/spec decisions

All three questions this section originally listed are now resolved by
already-`Accepted`/`Approved` decisions, not open: the test framework is
Catch2 v3 ([ADR-0007](../../adr/0007-test-framework.md), since Spec
0001); the image diff algorithm and tolerance thresholds are exact
pixel match, channel tolerance 0, failing-pixel budget 0
([ADR-0042](../../adr/0042-image-regression-testing-comparison-methodology-and-test-ownership-boundary.md),
Spec 0011); and headless/image-regression GPU tests run against real
Vulkan-capable hardware only, never a software implementation (Spec
0010/0011, both disclosing this as a single-GPU-vendor limitation, not
cross-vendor coverage). No open question remains in this section as of
Spec 0011; a future genuinely new question belongs here when raised by
its own spec, not left as a stale placeholder.
