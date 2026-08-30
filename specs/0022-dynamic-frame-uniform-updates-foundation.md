# Spec: Dynamic Frame Uniform Updates Foundation

- **Status:** `In Review` (previously `Approved`; reopened — see "Correction —
  2026-08-30" below. **Do not treat the original "Human Review Approval"
  section further down this document as current** — it approved a design
  whose central justification this correction retracts. A fresh approval
  covering the corrected design follows the correction.)
- **Author:** slmao
- **Created:** 2026-08-30
- **Related Plan(s):** None yet.
- **Related ADR(s):** [ADR-0065](../adr/0065-explicit-pre-write-submission-drain-for-frame-uniform-safety.md)
  (`Rejected` — see that ADR's own "Rejected — 2026-08-30" section and the
  correction below). This Spec's corrected design needs **no** ADR — see
  the corrected Architectural Impact section.

## Correction — 2026-08-30 (found during Plan 0022 pre-drafting investigation)

Before drafting Plan 0022, this Spec's governance gate required
re-verifying, against real code, that the races described below still
exist. That re-verification found they do not — on the one path this
Spec's original investigation actually traced. This section states
plainly what was wrong, why, and what in this document is corrected as a
result. The original "Motivation," "Proposed Design," "Architectural
Impact," "Alternatives Considered," "Final Review Round," and "Human
Review Approval" content is superseded by this correction and the
"Corrected Motivation" / "Corrected Design" sections that follow it. To
keep this already-long document navigable, most of that original content
is summarized below rather than reproduced verbatim a second time — its
exact original wording is preserved unedited in this repository's git
history (commit `33e19f4`, [PR #102](https://github.com/slmao/Atlantis/pull/102),
both already merged into `main` before this correction), the
authoritative record of what was originally claimed and why it was wrong.
A reader should trust this section and what comes after it, not any
section further down this document.

**What was wrong.** The original investigation (and its "final review
round") searched `src/vulkan_backend/src/vulkan_presentation.cpp` for a
CPU-blocking wait using patterns that did not include the actual method
name being called there (`waitAndReleaseRetainedSubmission`), and a later
verification pass `Read` the file starting partway through
`acquireNextTarget()`'s own body — after its first 36 lines, not from the
function's actual start. Both searches missed
`VulkanPresentation::acquireNextTarget()`'s own **"Step 0"**
(`src/vulkan_backend/src/vulkan_presentation.cpp:527-546`):

```cpp
// Step 0 (found via GPU testing, not in the original design): wait for
// any previously-retained submission to fully complete on the GPU
// before this call does anything else. ...
const auto drainResult = device_.waitAndReleaseRetainedSubmission();
```

This runs **unconditionally, as the very first substantive action inside
`acquireNextTarget()`** — before `recreateIfNeeded()`, before the
zero-extent check, before `vkAcquireNextImageKHR` itself
(`:574-575`). `acquireNextTarget()` is called at
`src/runtime/src/runtime_application.cpp:407`, the first substantive call
in every `RuntimeApplication::runFrame()` iteration — before the
format-change candidate build (`:419` onward), before the Camera/Lighting
mapped-memory writes (`:539-541`, `:573-575`), before
`createCommandList()` (`:612`), and before `Renderer::drawFrame()`'s own
`bindUniformBuffer()`/`bindTexture()` calls (`:755`).

`VulkanDevice::waitAndReleaseRetainedSubmission()`
(`src/vulkan_backend/src/vulkan_device.h:184`) is **already a public
method** — its own header comment states it is public "specifically so
`VulkanPresentation::acquireNextTarget()` can call it," and explains why:
"Found via GPU testing (Plan 0006's own Human-Review-approved
single-frame-in-flight design assumed this was already guaranteed by
`submit()`'s own internal wait; it was not... `VK_LAYER_KHRONOS_validation`
correctly rejected the resulting 'semaphore must not have any pending
operations' reuse hazard." **This fix predates Spec 0022 entirely** — it
was added during Plan 0006's own post-implementation GPU testing, to
close a swapchain-acquire-semaphore reuse hazard, not anything to do with
Camera or Lighting. As a side effect, it already fully closes, on the
windowed `RuntimeApplication` path, both hazards this Spec's original
Motivation described:

- **Hazard A (mapped-memory overwrite)**: closed, because the Camera/
  Lighting write (`:539-541`/`:573-575`) happens after `acquireNextTarget()`
  returns, which is after its own Step 0 drain.
- **Hazard B (descriptor-set update against a still-pending submission)**:
  closed for the identical reason — `Renderer::drawFrame()` (`:755`) and
  its `bindUniformBuffer()`/`bindTexture()` calls also happen after
  `acquireNextTarget()` returns.

**The original Spec's "windowed and headless share one safety model"
claim was also inaccurate.** `RuntimeApplication::runFrame()` never
references `OffscreenTarget` — it is windowed-only; there is no headless
composition root reusing `runFrame()`. The offscreen/headless path
(`tests/image_regression/fixture/*.cpp`, `tests/vulkan_backend/headless_rendering_gpu_tests.cpp`,
`tests/runtime/material_realization_gpu_tests.cpp`) calls
`Device::createOffscreenTarget()` directly and never constructs a
`Presentation`, so it never benefits from Step 0. Every such test found
during this correction's re-investigation is single-frame (one acquire,
one write, one submit, one readback — no cross-frame reuse of a shared
uniform buffer), so none of them currently exercises Hazard A/B in
practice; a *future* multi-frame offscreen dynamic-uniform test would be
exposed, however — see "Corrected Design" below for how this is handled
without a new RHI API.

**What is retracted.** The claim that a real, currently-shipped
synchronization gap exists in the Camera update path on the windowed
`RuntimeApplication` path is **false** and is retracted. ADR-0065's
recommended new RHI method, `Device::waitForPreviousSubmission()`, was
justified entirely by that claim and is **not needed** for the reason
originally given.

**What survives, corrected.** Spec 0019's own disclosed limitation
("captured once, never updated again" — that Spec's own D1) is real and
is this Spec's sole remaining goal: let a `World::setLight()` (and
related) change become visible on the GPU at an explicit, testable
boundary. Real GPU pixel-level verification of that change requires an
offscreen fixture (readback is offscreen-only — swapchain images are
created with `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT`
only, never `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`,
`src/vulkan_backend/src/vulkan_presentation.cpp:411` — confirmed
`copyRenderTargetToBuffer()`'s own `vkCmdCopyImageToBuffer` source-layout
requirement, `vulkan_command_list.cpp:357`, is unreachable for a
windowed target), so a new multi-cycle offscreen fixture is needed for
that evidence and does need its own protection against Hazard A/B — but,
as "Corrected Design" below shows, the already-existing, already-public
`Device::waitIdle()` already suffices for that, with zero new RHI
surface, following a pattern this codebase already uses
(`tests/vulkan_backend/headless_rendering_gpu_tests.cpp:156-173`: submit,
then `waitIdle()`, before the next cycle's own writes).

See "Corrected Motivation" and "Corrected Design" immediately below for
the full, accurate account; the original "Motivation" / "Proposed
Design" / "Architectural Impact" / "Alternatives Considered" / "Final
Review Round" / "Human Review Approval" sections further down this
document are retained for the historical record (the errors made, and
how they were made, are as much a governance record as the correction
itself) but are superseded and must not be used to inform Plan 0022.

## Corrected Motivation / Problem Statement

Spec 0019 (`Approved`, implemented) ships `World::setLight()` as a real,
working mutator of `World`'s own CPU-side light state, but Runtime
captures a single `FrameLightingData` snapshot into the GPU-visible
uniform buffer exactly once, guarded by `lightingDataCaptured_`
(`src/runtime/src/runtime_application.cpp:548`), and never re-captures
it. This is the one remaining real gap this Spec closes.

**Why this is safe to close with no new synchronization mechanism.** The
Camera write already happens, every frame, at exactly the point Lighting
would need to write too (`:539-576`, the same code block, same buffer,
same pointer) — and that point is already downstream of
`VulkanPresentation::acquireNextTarget()`'s own Step 0 drain (see
Correction above). Removing `lightingDataCaptured_`'s guard so the
existing Lighting-extraction-and-write code
(`world_->lightEntities()`/`getLight()`/`getWorldMatrix()`/
`extractFrameLightingData()`, already correct, already exercised once per
process lifetime today) runs every successful frame instead of once
introduces no new hazard: it reuses the identical write point Camera's
own, already-safe, unconditional per-frame write already occupies.

**Descriptor-set update safety (the original Hazard B) is likewise
already closed on this path**, for the same reason: `Renderer::drawFrame()`'s
`bindUniformBuffer()`/`bindTexture()` calls
(`src/renderer/src/renderer.cpp:26-42`,
`src/vulkan_backend/src/vulkan_command_list.cpp:234-318`) happen at
`runtime_application.cpp:755`, after `acquireNextTarget()`'s Step 0 has
already run for this frame.

**What genuinely needs new protection: a multi-frame offscreen
verification fixture.** This Spec's own Testing & Verification Plan
requires real GPU pixel-level evidence that a Lighting change (Directional
direction/color/intensity, Point position/intensity, entity Transform,
parent-hierarchy Transform, entity creation/removal) takes visible effect.
Readback (`CommandList::copyRenderTargetToBuffer()`) requires the source
image to have been created with `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`
(`vulkan_command_list.cpp:357`'s `VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL`
requirement) — swapchain images never carry that usage flag
(`vulkan_presentation.cpp:411`: `VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
VK_IMAGE_USAGE_TRANSFER_DST_BIT` only), so real pixel-level evidence is
structurally an offscreen-only capability in this codebase, matching
every existing golden fixture's own use of `createOffscreenTarget()`. A
new fixture that renders multiple cycles against the same offscreen
target, changing `World` light state between cycles and reading pixels
back each time, needs its own protection against Hazard A/B, since it
never constructs a `Presentation` and therefore never benefits from Step
0.

That protection needs no new RHI method. The already-public
`Device::waitIdle()` — called once per cycle, after that cycle's `submit()`
returns and before the next cycle's own mapped write — already provides
it, following the exact pattern this codebase's own
`tests/vulkan_backend/headless_rendering_gpu_tests.cpp:156-173` test
already establishes for repeated offscreen acquire/submit cycles
(`submit()` → `REQUIRE(device->waitIdle().isOk())` → next cycle). Unlike
the windowed path, an offscreen-only fixture never constructs a
`Presentation`, so `waitIdle()`'s "belt-and-suspenders"
`vkDeviceWaitIdle()` (`vulkan_device.cpp:632-636`) has no
presentation-engine-internal state to drain beyond what the narrower
`waitAndReleaseRetainedSubmission()` already covers — in this specific,
concrete context, the two calls are equivalent in effect, so reaching for
the already-existing, already-public, broader method introduces no real
cost and no new API surface.

## Corrected Design

**No new RHI API. No new ADR.** The full design:

1. Remove `lightingDataCaptured_` and its guard
   (`runtime_application.cpp:548`/`:575`). The Lighting-extraction block
   (`:549-574`) runs every successful frame, at the same point it already
   runs once today — after `acquireNextTarget()` returns a non-null
   target, in the same code region as the Camera write, sharing the same
   `cameraData` pointer.
2. The extraction itself is a full re-read of `World`'s current light
   state each time (`world_->lightEntities()`/`getLight()`/
   `getWorldMatrix()`), not a diff against previously-published bytes —
   this naturally covers every source of change (`setLight()`, a Light
   entity's own `Transform`, a parent-hierarchy `Transform` change, Light
   entity creation/removal) without any new dirty-bit or revision-counter
   state, and without two authoritative data sets ever coexisting.
3. A zero-extent/deferred-acquire/acquire-failure frame already returns
   before reaching the write (existing early-return guards,
   `runtime_application.cpp:403-416`) — unchanged, and correct: the next
   successful frame re-extracts current `World` state regardless of what
   any previous frame did or skipped, so no "pending update" bookkeeping
   is needed and none is added.
4. A submit failure already causes `markFailed()` and an early return
   (`:758-762`) before any state is published as "current" — unchanged;
   the next successful frame (if the app ever resumes running frames,
   which it does not today once `Failed`) would re-extract from `World`'s
   then-current state regardless.
5. New verification fixture: a multi-cycle offscreen fixture (following
   `headless_rendering_gpu_tests.cpp`'s own established
   acquire/write/submit/`waitIdle()`/readback/reset pattern) that changes
   `World` light state (and, in separate cycles, Light/parent Transform)
   between cycles and confirms the readback pixels change as expected —
   protected by calling `device->waitIdle()` after each cycle's `submit()`
   and before the next cycle's own extraction/write, exactly matching the
   existing precedent.

**Architectural Impact: None.** No public API changes (RHI, Renderer,
World, or otherwise); no new module boundary; no new dependency; no new
threading model; no new memory-ownership model. `lightingDataCaptured_`'s
removal is a private `RuntimeApplication` implementation change. The new
fixture uses only existing, already-public RHI surface
(`createOffscreenTarget()`, `submit()`, `waitIdle()`,
`copyRenderTargetToBuffer()`), matching this codebase's own established
fixture pattern exactly. No ADR is required.

**Alternative considered and rejected: add `Device::waitForPreviousSubmission()`
anyway, for symmetry/explicitness.** Rejected under AGENTS.md's own "no
speculative abstraction" principle — every concrete, real need this Spec
has (windowed safety, offscreen multi-cycle fixture safety) is already
met by existing, already-public RHI surface (`acquireNextTarget()`'s own
Step 0; `waitIdle()`). Adding a second, narrower wait method with no
concrete consumer would be exactly the kind of "reasonable-looking but
unreviewed" abstraction AGENTS.md's Golden Rule exists to prevent.

## Goals

- A `World::setLight()` change — and every other `World` mutation that
  affects the published Lighting payload (Light `Transform`,
  parent-hierarchy `Transform`, Light entity creation/removal) — becomes
  visible on the GPU at an explicit, testable boundary: the next
  successful frame's own extraction, immediately after
  `acquireNextTarget()` returns a non-null target.
- Camera continues to update correctly every frame — unchanged behavior,
  only reconfirmed safe by this Spec's own corrected investigation.
- No multi-threaded frame orchestration is introduced; single-threaded at
  the frame level (ADR-0004).
- No new RHI/public API surface, no new ADR, no new architectural
  decision of any kind.

## Non-Goals

- PBR Material, Shadow, IBL, Post-processing — unrelated, deferred per
  human-directed priority ordering (`specs/README.md` Section B).
- Animation system; multi-threaded rendering; any job system.
- A generic, arbitrary-N-frames-in-flight framework — Plan 0006's
  single-frame-in-flight baseline is untouched and not revisited.
- Bindless rendering or descriptor indexing.
- A general-purpose GPU upload scheduler.
- Android, iOS, or Linux implementation.
- Any Editor/Client-facing API.
- Any new RHI method, ADR, or test-double/DI architecture — the corrected
  design needs none of these; introducing one anyway would be exactly the
  speculative abstraction this Spec explicitly avoids (see Corrected
  Design above).

## Requirements

### Functional

- Camera view/projection matrices continue to be written to GPU-visible
  memory once per frame, unconditionally — unchanged.
- A successful frame re-extracts and publishes the complete, current
  176-byte `FrameLightingData` from `World`'s live state, every frame —
  not only in response to `setLight()`.
- Multiple `World` mutations affecting the same frame's update resolve to
  final-value semantics (the state read at this frame's own extraction
  point) — no averaging, no queued intermediate values.
- A skipped frame (zero extent, deferred acquire, resize, submit failure)
  never publishes stale or uninitialized bytes and never "loses" a change
  — the next successful frame always re-extracts current `World` state.
- No two authoritative Lighting data sets (a removed static-snapshot flag
  and a new mechanism) ever coexist.

### Non-functional

- Performance: no new per-frame CPU wait is introduced (the existing,
  already-paid Step 0 wait is unchanged in frequency or position);
  extracting 176 bytes from already-in-memory `World` component data every
  frame instead of once is not a demonstrated cost and is not separately
  measured by this Spec.
- Memory: no new buffer allocation.
- Portability: no new Vulkan feature, extension, or API dependency of any
  kind.
- Other: no new third-party dependency; no new global mutable frame
  state; no new threads or locks; no new public API.

## Proposed Design

Superseded by "Corrected Design" above.

## Architectural Impact

Superseded by "Corrected Design" above: **None.**

## Alternatives Considered

Superseded by "Corrected Design" above (the "add a new RHI method for
symmetry" alternative and why it is rejected).

## Testing & Verification Plan

**GPU-independent:**

- `lightingDataCaptured_` has zero remaining references anywhere in the
  repository after this change (a grep-based check, trivially automatable).
- The Lighting-extraction block runs on every successful frame in a
  GPU-independent unit/integration test double for the extraction logic
  itself (matching however `extractFrameLightingData()` is already unit
  tested today, if it is — Plan 0022 confirms and extends that coverage,
  not this Spec).
- Multiple `World` mutations within one synthetic "frame" resolve to the
  final state only.

**Real Vulkan/GPU — reusing the existing multi-frame windowed pattern
where suf ficient:**

- Multi-frame Camera regression: unchanged behavior, reconfirmed via the
  existing `runtime_smoke_gpu_tests.cpp`-style multi-frame loop.
- Multi-frame Lighting regression via the new offscreen fixture (Corrected
  Design item 5): Directional Light direction/color/intensity, Point
  Light position/intensity, Light `Transform`, parent-hierarchy
  `Transform`, and Light entity creation/removal each independently
  change the readback pixels as expected, across multiple cycles, each
  cycle protected by `waitIdle()` per Corrected Design.
- Final-value semantics: multiple mutations before one cycle's own
  extraction point publish only the last state.
- No stale/undefined bytes: a cycle with no Lighting change publishes
  byte-identical Lighting data to the previous cycle.
- Zero-extent/acquire-failure/submit-failure frames never publish stale
  data (windowed) and are not applicable to the new offscreen fixture's
  own simpler acquire contract (no swapchain, no zero-extent case) — the
  fixture's own coverage is scoped to what an offscreen target can
  actually exercise.
- Format-change (Spec 0018 D9) + dynamic Lighting in the same frame:
  windowed-only scenario (format changes are a swapchain-surface concept)
  — covered by extending `runtime_smoke_gpu_tests.cpp`-style windowed
  testing, not the new offscreen fixture.
- Validation Layers clean throughout.

**Golden strategy:** the five existing goldens
(`minimal_cube`/`world_scene`/`textured_quad`/`material_demo`/`lighting_demo`)
remain byte-for-byte identical and pixel-zero-difference — none is
touched or regenerated. The new multi-cycle offscreen fixture is a
**test-only** harness producing programmatic pixel comparisons between
cycles (not a stored golden PNG) — matching this Spec's own original
golden-strategy decision, still valid: a relative "changed as expected"
claim is proven more directly by a within-test comparison than by a new
fixed baseline image. If Plan 0022 later finds a real need for a stored
baseline, it follows ADR-0042's own two-phase bootstrap with human review
— not mandated by this Spec.

**Full matrix:** Debug and Release, `ctest -L gpu` and `ctest -LE gpu`,
Vulkan Validation Layers clean, a clean `ATLANTIS_BUILD_TESTS=OFF` build,
module/link boundary scan, `git diff --check`.

## Risks & Open Questions

- Whether Plan 0022 extends `runtime_smoke_gpu_tests.cpp` itself or adds a
  new windowed multi-frame Lighting test file — an implementation-detail
  choice for the Plan, not an architectural one.
- Whether the new offscreen fixture belongs under
  `tests/vulkan_backend/` (matching `headless_rendering_gpu_tests.cpp`'s
  own location) or `tests/runtime/` (since it exercises Runtime's own
  extraction logic) — a file-organization choice for the Plan.
- Whether `extractFrameLightingData()` already has adequate
  GPU-independent unit coverage for being called every frame (versus
  once) — Plan 0022 must confirm, not assume.

No Human Review decision items remain that require choosing among real
architectural alternatives — the corrected design has exactly one shape,
grounded in already-existing, already-public RHI surface.

## Out of Scope / Future Work

Everything in Non-Goals above.

## Human Review Approval (corrected design)

**Approved by slmao <slmaosjtu@gmail.com>, 2026-08-30**, covering the
corrected design only (Correction, Corrected Motivation, Corrected
Design, and the Goals/Requirements/Testing sections that follow them).
This approval **retracts** the original "Human Review Approval" recorded
further below in this document, which covered a design this correction
found unnecessary. ADR-0065 is `Rejected` alongside this approval — see
its own "Rejected — 2026-08-30" section.

This approval authorizes drafting Plan 0022 against the corrected design
only: removing `lightingDataCaptured_` and its guard so Lighting is
re-extracted and published every successful frame at the existing Camera
write point; adding a new multi-cycle offscreen fixture, protected by the
existing `Device::waitIdle()` between cycles, for real GPU pixel evidence
that Directional/Point Light and Transform/hierarchy changes take visible
effect; and extending existing windowed multi-frame testing for Camera/
format-change-interaction coverage. **This approval authorizes drafting
Plan 0022 only — not any Implementation.**

---

## [Superseded] Original Motivation / Problem Statement (2026-08-30, first draft)

*Retained for the governance record — see "Correction — 2026-08-30" above.
Do not use this section or anything below it to inform Plan 0022.*

### The disclosed Lighting limitation this Spec was originally scoped to close

Spec 0019 (`Approved`, implemented) ships `World::setLight()` as a real,
working mutator of `World`'s own CPU-side light state, but Runtime
captures a single `FrameLightingData` snapshot into the GPU-visible
uniform buffer exactly once, guarded by `lightingDataCaptured_`
(`src/runtime/src/runtime_application.cpp:548`), and never re-captures it.
Closing this gap — making a runtime light change visible on the GPU at an
explicit, testable time — is this Spec's secondary goal; the primary goal
was believed to be the synchronization model below, which both Camera and
Lighting were believed to need — **this premise is false; see the
Correction above.**

### The gap this Spec's original investigation believed it found

*[Original text retained for the record. It described two "hazards" — a
mapped-memory overwrite and a descriptor-set update — as unguarded by any
CPU-blocking wait before the Camera write, citing
`VulkanDevice::submit()`'s own internal drain and
`VulkanPresentation::acquireNextTarget()`'s `vkAcquireNextImageKHR` call
(with its `VK_NULL_HANDLE` fence argument) as the only two candidate
synchronization points, concluding neither provided the needed guarantee.
This missed `acquireNextTarget()`'s own separate, earlier "Step 0" call to
`waitAndReleaseRetainedSubmission()`, documented in the Correction above.
The original text is not reproduced verbatim here a second time — see
this Spec's own git history for the exact original wording, preserved in
the commit that introduced this correction.]*

## [Superseded] Original Human Review Approval

*Retracted — see "Human Review Approval (corrected design)" above.
Recorded 2026-08-30, approving a design whose central justification (a
windowed-path Camera/Lighting/descriptor-set race) was retracted the same
day during Plan 0022's own governance gate.*
