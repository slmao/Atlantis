# Testing Strategy

No test harness exists yet — this document defines the strategy new code
must follow once one does, and constrains what a build-system/testing spec
is allowed to land as.

## Sequencing note

Windowed rendering ships before headless rendering (see
[AGENTS.md](../../AGENTS.md)). That means layers 2 and 3 below — and any
automated, CI-enforced testing of rendered output — are not available
until headless rendering lands. Until then, verification of the windowed
path is manual (run it, look at it, check validation layers) and CI is
limited to build verification; see [ci-strategy.md](ci-strategy.md). This
is a temporary gap, not a decision to skip image regression testing —
headless rendering's purpose in this project is specifically to unlock it.

## Layers

1. **Unit tests** — algorithmic and data-structure logic that doesn't
   require a GPU (render graph scheduling/validation logic, RHI resource
   bookkeeping, math, etc.). These must not require a Vulkan device to run.
   Available from the start; not blocked on windowed or headless rendering.

2. **Headless integration tests** — exercise the RHI and render graph
   against a real (or software) Vulkan device with no window/swapchain,
   rendering to offscreen targets. This is what makes CI testing of
   rendered output possible; it becomes available once headless rendering
   is implemented, after the windowed milestone.

3. **Image regression tests** — render a known scene/pass headlessly,
   compare against a golden reference image, fail on divergence beyond an
   agreed tolerance. These are the primary signal that rendering output is
   still correct after a change.

4. **Vulkan Validation Layers as a correctness gate** — every test that
   touches the GPU runs with validation layers enabled. A clean validation
   run is a pass/fail condition, not advisory output to skim. This applies
   to unit-adjacent GPU tests, integration tests, and manual dev runs
   alike.

## Golden images

- Golden images live under `tests/` once the regression harness exists
  (exact path to be fixed by that harness's spec).
- Updating a golden image is a reviewed, visible diff in a PR — never a
  silent regeneration step that a CI job runs and commits automatically.
- The comparison method (exact pixel match vs. perceptual diff vs.
  SSIM/threshold-based) is **not yet decided** and must be settled by the
  spec that introduces the regression harness — see open questions below.

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

- Image diff algorithm and tolerance thresholds
- Test framework choice (e.g. Catch2, GoogleTest, custom)
- Whether headless tests run against a real GPU, a software Vulkan
  implementation (e.g. Lavapipe/SwiftShader), or both
