# Plan: Dynamic Frame Uniform Updates Foundation

- **Spec:** [specs/0022-dynamic-frame-uniform-updates-foundation.md](../specs/0022-dynamic-frame-uniform-updates-foundation.md)
  (`Approved`, corrected design — see that Spec's own "Correction —
  2026-08-30" and "Human Review Approval (corrected design)" sections)
- **Status:** In Review
- **Author:** slmao

## Objective

Implement Spec 0022's corrected, narrow scope only: remove
`RuntimeApplication::lightingDataCaptured_` so `FrameLightingData` is
re-extracted from `World`'s live state and republished, in full, every
successful windowed frame — at the exact call point already proven safe
(after `VulkanPresentation::acquireNextTarget()`'s own pre-existing Step
0 drain and `World::updateTransforms()`, before `Renderer::drawFrame()`)
— and add the real GPU/CPU verification evidence for it. No RHI change,
no ADR, no new synchronization primitive; ADR-0065 (`Rejected`) is not
revived.

## Pre-draft verification against real, current source

Confirmed directly against `main` at `b3f59ee` (PR #104's own merge
commit) at Plan-drafting time, re-reading every file this Plan touches —
not restating Spec 0022's own prose.

### `RuntimeApplication::runFrame()` — the exact block this Plan changes

`src/runtime/src/runtime_application.cpp:543-576` (full text, re-read):

```cpp
// Plan 0019 Section P9: the one-time frame lighting snapshot -- guarded
// by lightingDataCaptured_, never re-entered on any later frame for
// this RuntimeApplication instance's own lifetime. World::setLight()
// calls made after this point change World's own CPU state only; no
// code path below this guard ever reads World's light state again.
if (!lightingDataCaptured_) {
  std::vector<LightExtractionInput> lightInputs;
  for (const atlantis::world::EntityId& id : world_->lightEntities()) {
    const auto lightResult = world_->getLight(id);
    const auto lightWorldMatrixResult = world_->getWorldMatrix(id);
    ATLANTIS_CHECK_MSG(lightResult.isOk() && lightWorldMatrixResult.isOk(),
                        "runFrame(): getLight()/getWorldMatrix() failed for a handle lightEntities() just returned");
    lightInputs.push_back({lightResult.value(), lightWorldMatrixResult.value()});
  }
  const auto lightingResult = extractFrameLightingData(lightInputs);
  if (lightingResult.isErr()) {
    ATLANTIS_LOG_ERROR("runFrame(): extractFrameLightingData() failed");
    lifecycle_.markFailed();
    return;
  }
  auto* lightingData = reinterpret_cast<FrameLightingData*>(cameraData + 32);
  *lightingData = lightingResult.value();
  lightingDataCaptured_ = true;
}
```

`lightingDataCaptured_` is declared at
`src/runtime/include/atlantis/runtime/runtime_application.h:123` (`bool
lightingDataCaptured_ = false;`). A full-repository grep confirms it is
referenced in exactly these two files and nowhere else (`src/runtime/src/runtime_application.cpp:544,548,575`,
`runtime_application.h:123`) — no test, fixture, or other composition
root names it. **`LightingDemoFixture::lightingDataCaptured`
(`tests/image_regression/fixture/lighting_demo_fixture.h:79`) is a
different, unrelated field** — see "The `LightingDemoFixture` conflict"
below for why this Plan does not touch it.

Confirmed once more, precisely, for this Plan's own call-point contract
(re-verifying Spec 0022's own citations against the exact current line
numbers, which have not moved since):

- `world_->updateTransforms()` — `runtime_application.cpp:497`,
  unconditional, before this block.
- `presentation_->acquireNextTarget()`'s own Step 0 drain —
  `vulkan_presentation.cpp:527-546`, inside `acquireNextTarget()`, called
  at `runtime_application.cpp:407`, before this block.
- Camera write — `:539-541`, immediately before this block, same
  `cameraData` pointer.
- `createCommandList()` — `:612`, after this block.
- `Renderer::drawFrame()` (descriptor updates) — `:755`, after this
  block.
- `device_->submit()` — `:758`, after this block.
- Format-change candidate build (`:436-456`) and swap-in
  (`:776-779`) — entirely independent of this block; this Plan does not
  touch that code.

### `FrameLightingData` — full layout, re-confirmed

`src/runtime/include/atlantis/runtime/scene_extraction.h:77-123` (full
text already reproduced verbatim in Spec 0022's own "176-byte payload
write boundary" evidence — not re-quoted a second time here to avoid
duplicating an already-current, cited source): 176 bytes total
(`static_assert(sizeof(FrameLightingData) == 176)`, line 120),
`alignas(16)`, `directionalLightCount`/`pointLightCount` (`uint32_t`,
offsets 0/4), explicit `_pad1[2]` (offset 8, required by std140's own
vec3-array alignment rule — not an implicit compiler gap), one
`DirectionalLightGpu` (offset 16, 32 bytes) and four `PointLightGpu`
(offset 48, 128 bytes, stride 32) — every offset/size/alignment fact
locked by its own `static_assert` (lines 101-122), none of which this
Plan touches or needs to touch.

### `extractFrameLightingData()` — full body, re-confirmed

`src/runtime/src/scene_extraction.cpp:156-224` (full text already
reproduced verbatim in Spec 0022's own evidence). Line 158: `FrameLightingData
data{};` — a fresh, value-initialized local on every call. Lines
174-179/202-207: `ATLANTIS_CHECK_MSG(false, ...)` fail-fast on a 2nd
Directional or 5th Point light reaching this function — a programmer-error
path, unreachable from any cook/decode-validated scene; this Plan does
not relax it. Returns `Ok(data)` (line 223) — full struct, by value; the
caller's write is always a full 176-byte overwrite (`runtime_application.cpp:574`,
`*lightingData = lightingResult.value();`).

### `World`'s real transform/light API — full call chain, re-confirmed

- `setLight()`/`removeLight()` (`src/world/src/world.cpp:350-360`): write
  `slots_[id.index()].light` directly.
- `getLight()`/`lightEntities()` (`:362-374`): read that same field
  live, no cache — `lightEntities()` iterates `slots_[i].alive &&
  slots_[i].light.has_value()`.
- `createEntity()`/`destroyEntity()` (`:124-185`): `createEntity()`
  resets `light`/`renderable`/`camera` on a reused slot and sets
  `alive = true`; `destroyEntity()` walks the full transitive-descendant
  set (collection phase, then mutation phase) and sets `alive = false`,
  advancing `generation`. Both are already correctly reflected by the
  next `lightEntities()` call — no caching layer anywhere in this path.
- `setLocalTransform()`/`setParent()` (`:193-222`): write
  `localTransform`/`parent` only.
- `getWorldMatrix()` (`:272-274`): returns `slots_[id.index()].cachedWorldMatrix`
  — stale until the next `updateTransforms()`.
- `updateTransforms()` (`:235-270`): unconditional, full re-traversal of
  every entity, no per-entity dirty gate inside `World` itself.
- **Confirmed once more for this Plan's own record**:
  `runtime_application.cpp:497` already calls `world_->updateTransforms()`
  unconditionally, every frame, before the Lighting-extraction block —
  this Plan adds no call to it and moves nothing.

### The `LightingDemoFixture` conflict — why this Plan does not extend it

`tests/image_regression/fixture/lighting_demo_fixture.h:40-45`/`:75-79`/`:115-122`
(full text read) states explicitly that `LightingDemoFixture`'s own
`lightingDataCaptured` field is **load-bearing**, not a shape-mirroring
artifact: `tests/image_regression/lighting_demo_gpu_tests.cpp:153`
(`TEST_CASE("LightingDemoFixture: World::setLight() after the one-time
capture changes World state but never the ...")`) is a real,
currently-passing, **Spec-0019-approved negative test** that calls
`renderLightingDemoFrame()` twice against the same fixture and asserts
the *second* call does **not** re-capture Lighting data even if
`World`'s own light state changed in between — the exact opposite of
what dynamic Lighting requires. Spec 0022's own corrected scope (see its
"Correction" section) only removes `RuntimeApplication::lightingDataCaptured_`
— it never named `LightingDemoFixture`'s own, separately-declared field,
and this Plan must not conflate the two. **This Plan does not modify
`LightingDemoFixture`, `renderLightingDemoFrame()`, or any existing
`lighting_demo_gpu_tests.cpp` `TEST_CASE`** — all of it remains exactly
as Spec 0019 approved it, proving a still-true, still-approved contract
for that specific fixture/test pairing. This Plan instead adds a
**new**, separate fixture type (Milestone 2 below) that reuses the same
underlying real extraction functions `LightingDemoFixture` already calls
(`extractFrameLightingData()`, `extractCameraMatrices()`,
`checkConformalTransform()`, and the Runtime-Host-private-but-test-sanctioned
`loadAndInstantiateScene()`/`computePendingMaterialIds()`/`realizePendingMaterials()`
functions `lighting_demo_fixture.h`'s own top comment names,
`:29-38`) without ever mutating `LightingDemoFixture` itself or its own
one-time-capture contract.

### Offscreen multi-cycle `submit()`→`waitIdle()` convention — re-confirmed universal

Re-confirmed (matching Spec 0022's own exhaustive audit, re-checked fresh
for this Plan): every multi-cycle `submit()` call site in
`tests/vulkan_backend/headless_rendering_gpu_tests.cpp` and
`tests/runtime/material_realization_gpu_tests.cpp` is immediately
followed by `REQUIRE(device->waitIdle().isOk())` before the next cycle's
own writes — a `grep -n "\.submit(\|->submit(\|waitIdle()"` across both
files shows no exception. The new fixture (Milestone 2) follows this
identical, already-universal convention — it does not invent a new one.

### CMake / test-target ownership — re-confirmed, zero new dependency wiring needed

`tests/image_regression/fixture/CMakeLists.txt` (full text read): the
`atlantis_image_regression_fixture` STATIC library already links
`Atlantis::RuntimeHost` **PUBLIC** — its own comment states this is "the
ONE explicitly-sanctioned exception to `RuntimeHost`'s usual 'no other
top-level module may depend on `Atlantis::RuntimeHost`' boundary — the
whole `tests/image_regression/` tree is the sanctioned consumer here, not
a new one" — plus `Atlantis::World`, `Atlantis::RHI`, `Atlantis::Renderer`
PUBLIC and `Atlantis::VulkanBackend`/`Atlantis::RenderGraph`/`Atlantis::ShaderSystem`/`Atlantis::ShaderSystemRhiIntegration`/`Atlantis::AssetSystem`
PRIVATE. A new fixture `.cpp` added to this same library's source list
needs **zero new `target_link_libraries` entries** — every dependency the
new fixture needs already flows through this existing library.
`tests/image_regression/CMakeLists.txt` (full text read): the
`atlantis_image_regression_gpu_tests` executable's source list is a flat
list of `.cpp` files (`image_regression_gpu_tests.cpp`,
`world_scene_gpu_tests.cpp`, ..., `lighting_demo_gpu_tests.cpp`) already
linking `Atlantis::ImageRegressionFixture`; a new
`dynamic_lighting_gpu_tests.cpp` is added to that same list, no new
`target_link_libraries` entry needed there either. `tests/runtime/CMakeLists.txt`
already builds `scene_extraction_tests.cpp` as part of its own
GPU-independent executable (confirmed by its presence alongside
`error_classification_tests.cpp`/`lifecycle_state_tests.cpp` etc. in
`tests/runtime/`) — extending that file needs no CMake change at all.

### `tests/runtime/scene_extraction_tests.cpp` — existing coverage, re-confirmed

Already covers (full `TEST_CASE` list read, `:70-463`): `extractCameraMatrices()`
(5 cases), `resolveMeshAsset()`/`resolveMaterialAsset()` (8 cases),
`extractFrameLightingData()` (8 cases, including "an empty lights vector
returns a zero-count, value-initialized result", `:295`, and both
fail-fast cases, `:310`/`:338`), `checkConformalTransform()` (7 cases),
`computeLambertianDiffuse()` (3+ cases). **Not yet covered**: calling
`extractFrameLightingData()` twice, with a shrinking light set between
calls, confirming the second result's now-unused trailing slots are zero
— this Plan adds exactly that (Milestone 1).

### Five existing goldens/sidecars — re-confirmed present and untouched

`tests/image_regression/goldens/{minimal_cube,material_demo,textured_quad,world_scene,lighting_demo}/*.png`
plus each PNG's own `.sidecar.txt` — five directories, five PNGs, five
sidecars, confirmed present via a directory listing at Plan-drafting
time. This Plan's own Milestones never write to any file under
`tests/image_regression/goldens/` and never invoke
`tests/image_regression/golden_generator/`.

### RHI `Device` — full pure-virtual surface, re-confirmed unchanged

`src/rhi/include/atlantis/rhi/device.h` (full text read): nine
pure-virtual methods (`createCommandList`, `submit`, `waitIdle`,
`createBuffer`, `createTexture`, `createPipeline`,
`createOffscreenTarget`, `createSampledTexture`, `createSampler`) — the
identical set confirmed during Spec 0022's own final review round; a
repository-wide `grep -n "waitForPreviousSubmission"` under `src/`
returns no matches. `VulkanDevice` (`src/vulkan_backend/src/vulkan_device.h:103`)
remains the sole `atlantis::rhi::Device` implementer — confirmed by
`grep -rn "public atlantis::rhi::Device"`. This Plan adds no method to
either.

## Plan-level decisions

These are Plan-level implementation-detail choices within Spec 0022's
already-`Approved` scope, not new architectural decisions — each is
grounded in the Pre-draft verification above, not invented ad hoc:

- **P1 — a new, separate fixture, never `LightingDemoFixture` itself.**
  See "The `LightingDemoFixture` conflict" above. New type name:
  `DynamicLightingFixture` (struct) / `setUpDynamicLightingFixture()` /
  `renderDynamicLightingFrame()` (functions) — matching this codebase's
  own existing `*Fixture`/`setUp*Fixture()`/`render*Frame()` naming
  convention exactly (`LightingDemoFixture`/`setUpLightingDemoFixture()`/
  `renderLightingDemoFrame()`), signaling by name alone that it is a
  sibling, not a replacement.
- **P2 — new fixture lives under `tests/image_regression/fixture/`,
  new tests under `tests/image_regression/`.** Not
  `tests/vulkan_backend/`: the new fixture needs the same cooked
  `lighting_demo_scene` assets, compiled shaders, `World`/asset-system
  loading path, and `RuntimeHost` extraction functions
  `LightingDemoFixture` already uses — all already wired into
  `atlantis_image_regression_fixture`'s own `Atlantis::RuntimeHost`
  PUBLIC link (see "CMake / test-target ownership" above), needing zero
  new dependency wiring. `tests/vulkan_backend/` tests build meshes/
  materials from scratch with no scene/asset-loading path at all — using
  it here would mean duplicating asset loading Runtime already provides,
  which this Plan does not do.
- **P3 — the new fixture reuses the *same* `lighting_demo_scene` cooked
  assets `LightingDemoFixture` already loads**, not a new scene asset.
  `World::setLight()`/`setLocalTransform()`/`setParent()`/
  `destroyEntity()`/`createEntity()` calls made directly against the
  fixture's own live `World` instance (after initial scene load) produce
  every scenario this Plan's own verification needs (Directional/Point
  parameter changes, local/parent Transform changes, entity creation/
  removal) — no new scene asset, no new cook step, no new shader.
- **P4 — expected values for CPU-byte and GPU-pixel assertions are
  computed independently of production code**, per Spec 0022's own
  Testing & Verification Plan requirement — hand-computed constants or a
  from-first-principles formula in the test file itself, never a call
  into `extractFrameLightingData()`/`computeLambertianDiffuse()` to
  generate its own "expected" value (that would prove only self-consistency,
  not correctness) — matching this codebase's own established precedent
  (`scene_extraction_tests.cpp`'s existing `extractFrameLightingData()`
  tests already do this, e.g. `:192-217`, `:218-239`).
- **P5 — no new golden.** Every dynamic-correctness claim is proven by a
  programmatic, within-test comparison (CPU bytes and/or GPU readback
  pixels across cycles of the *same* test run), never a new stored
  baseline PNG — matching Spec 0022's own Testing & Verification Plan
  decision. If, during drafting, the tests genuinely cannot express a
  claim this way, that is a real, disclosed blocker for this Plan Review
  to raise, not a silent decision to add a golden anyway.
- **P6 — the windowed regression check is a structural/no-crash/
  Validation-Layers-clean proof, not a new pixel-level windowed
  assertion.** Windowed `RenderTarget`s cannot be read back
  (`VK_IMAGE_USAGE_TRANSFER_SRC_BIT` is never set on a swapchain image —
  Spec 0022's own confirmed fact); Milestone 3 below extends
  `runtime_smoke_gpu_tests.cpp`'s own existing multi-frame loop to mutate
  `World` light state mid-loop and confirm the run completes cleanly
  (Validation Layers clean, no crash, no `ATLANTIS_CHECK` fired) — real
  pixel evidence for the dynamic behavior itself comes entirely from the
  new offscreen fixture (Milestone 2).

## Milestones / Task Breakdown

**Milestone 1 — Runtime dynamic Lighting write, with its own direct
GPU-independent proof (one atomic change; the behavior change and its
own unit coverage do not split across commits).**

1. `src/runtime/src/runtime_application.cpp`: remove the
   `if (!lightingDataCaptured_) { ... lightingDataCaptured_ = true; }`
   guard around the block at `:543-576`, leaving its body (light
   enumeration, `extractFrameLightingData()` call, error handling, the
   176-byte write) running unconditionally every time this point in
   `runFrame()` is reached. Update the block's own comment (currently
   describing one-time capture, `:543-547`) to describe the corrected,
   every-successful-frame contract, citing Spec 0022.
2. `src/runtime/include/atlantis/runtime/runtime_application.h`: remove
   the now-unused `lightingDataCaptured_` member (`:123`).
3. `tests/runtime/scene_extraction_tests.cpp`: add a new `TEST_CASE`
   calling `extractFrameLightingData()` twice — first with two Point
   lights, then with one — asserting the second result's
   `pointLightCount == 1` and that `pointLights[1]`'s own bytes (the
   now-unused slot) are all zero, proving the "every call is a fresh,
   value-initialized object" property Spec 0022's own evidence already
   established from source, now locked by a real, executed test rather
   than left as a read-the-source claim. A second new `TEST_CASE` proves
   final-value semantics is not `extractFrameLightingData()`'s own
   concern (it is a pure function over its `lights` argument — "final
   value" is a property of *which* `LightExtractionInput`s the caller
   passes, decided by `World`'s own already-live `lightEntities()`/
   `getLight()`, not by this function) — this test states that
   division of responsibility explicitly, rather than asserting
   something this function was never responsible for.
4. Grep-based check (part of this Milestone's own verification, not a
   separate step): `lightingDataCaptured_` has zero remaining references
   anywhere in the repository.

**Milestone 2 — New multi-cycle offscreen dynamic-lighting fixture and
its own GPU tests (pixel + byte evidence).**

1. `tests/image_regression/fixture/dynamic_lighting_fixture.h`: declare
   `DynamicLightingFixture` (mirroring `LightingDemoFixture`'s own field
   set minus the `lightingDataCaptured` flag — this fixture always
   re-extracts), `setUpDynamicLightingFixture()`, and
   `renderDynamicLightingFrame()`, matching P1 above.
2. `tests/image_regression/fixture/dynamic_lighting_fixture.cpp`:
   `setUpDynamicLightingFixture()` loads the same `lighting_demo_scene`
   cooked assets `setUpLightingDemoFixture()` already loads (P3), creates
   the shared `cameraBuffer`/`depthTexture`/`OffscreenTarget`/
   `readbackBuffer` once. `renderDynamicLightingFrame()` performs one
   cycle: `world.updateTransforms()` → the *unconditional* (no guard)
   equivalent of Milestone 1's own extraction-and-write block, reusing
   `extractFrameLightingData()` directly (not a duplicated
   re-implementation) → `computePendingMaterialIds()`/
   `realizePendingMaterials()` (only realizes anything new the very
   first cycle, matching `LightingDemoFixture`'s own established
   pattern) → `checkConformalTransform()` gate per `LitTextured` entity →
   build `DrawItem`s → `offscreenTarget->acquireTarget()` → `createCommandList()`
   → `Renderer::drawFrame()` → `copyRenderTargetToBuffer()` → `submit()`
   → `device->waitIdle()` (the cycle's own safety point, per Spec 0022's
   Corrected Design and the re-confirmed universal convention above) →
   read back `PixelBuffer`. The *same* `cameraBuffer`, depth `Texture`,
   `OffscreenTarget`, `Mesh`/`Material`/`Pipeline`/descriptor-set map, and
   `readbackBuffer` are reused across every cycle — never recreated —
   matching `headless_rendering_gpu_tests.cpp`'s/`material_realization_gpu_tests.cpp`'s
   own established multi-cycle reuse pattern, and directly satisfying
   Spec 0022's own "buffer handle stable, no per-frame Material/Pipeline
   rebuild" requirement.
3. `tests/image_regression/fixture/CMakeLists.txt`: add
   `dynamic_lighting_fixture.cpp` to `atlantis_image_regression_fixture`'s
   source list — no new `target_link_libraries` entry (confirmed above).
4. `tests/image_regression/dynamic_lighting_gpu_tests.cpp` (new file),
   registered in `tests/image_regression/CMakeLists.txt`'s
   `atlantis_image_regression_gpu_tests` source list. Test cases (each
   independently identifying a specific, real hazard — not a single
   "bufferA != bufferB" catch-all, per Spec 0022's own requirement):
   - Multi-cycle mechanics: three cycles with no `World` mutation between
     them produce byte-identical readback pixels each time — proves the
     cycle machinery itself (reuse + `waitIdle()`) is sound before any
     test relies on it to prove a *change*.
   - Directional Light direction change: a deliberate 90°-rotation
     mutation between cycles; assert the readback pixel at a known,
     hand-picked cube face changes in the sign/magnitude
     `computeLambertianDiffuse()`'s own documented formula predicts for
     that specific rotation (an independently-computed expected
     value, P4 — not a call into `computeLambertianDiffuse()` itself for
     the "expected" side).
   - Directional Light color/intensity change: same shape, an
     independently-computed expected color/brightness ratio.
   - Point Light position (distance-attenuation) change: an
     independently-computed expected attenuation ratio at the fixed
     sample point.
   - Point Light color/intensity change: same shape as Directional.
   - A Light entity's own local `Transform` change (`setLocalTransform()`
     directly on the light entity) — reflected next cycle.
   - A Light's parent entity's `Transform` change (`setParent()`
     establishing the relationship if the scene's own light is not
     already parented, then a parent `setLocalTransform()`) — reflected
     next cycle, distinct from the light's own local-transform case
     above (proves `updateTransforms()`'s own hierarchy propagation, not
     only a leaf-entity change).
   - Light entity creation: `world.createEntity()` plus `setLight()` for
     a *new* Point light not present in the original scene — its
     contribution appears next cycle.
   - Light entity destruction: `world.destroyEntity()` on an existing
     light — its contribution disappears (readback returns to the
     pre-creation baseline) next cycle, and the CPU-side
     `pointLightCount` decreases with the freed slot's own bytes zeroed
     (dual CPU-byte + GPU-pixel evidence for the same event, per Spec
     0022's own requirement).
   - Same-cycle multiple mutations: two `setLight()` calls against the
     same entity before one `renderDynamicLightingFrame()` call — only
     the final call's value is reflected.
   - CPU-byte snapshot comparison: for at least the Directional-direction
     and Point-light-creation/destruction cases above, additionally
     capture and diff the raw 176-byte `FrameLightingData` region (via a
     small CPU-visible readback of `cameraBuffer` itself, or an
     independent second call to `extractFrameLightingData()` against the
     fixture's own `World` state at that point — whichever the Milestone
     finds simpler; both are dual evidence for the identical claim the
     pixel test already makes) — not a substitute for the GPU-pixel
     evidence, an addition to it.
   - Validation Layers clean across the entire multi-cycle run.

**Milestone 3 — Windowed regression extension (structural, not
pixel-level — P6).**

1. `tests/runtime/runtime_smoke_gpu_tests.cpp`: extend (or add a sibling
   `TEST_CASE` in the same file, matching its own existing
   `kSmokeTestFrameCount`-loop shape, `:91-92`) to call `World::setLight()`
   (and, in a second scenario, `setLocalTransform()` on a light entity)
   between two `app.runFrame()` calls inside the loop, asserting the run
   completes cleanly (`app.shouldContinue()` stays true throughout, no
   `ATLANTIS_CHECK` fires, exit is clean) — confirming the windowed path
   tolerates a mid-run dynamic Lighting change with no crash and no
   Validation Layers hit, without claiming pixel-level proof this test
   cannot produce (P6).
2. No change to `runtime_smoke_gpu_tests.cpp`'s own existing Camera/
   format-change coverage — re-run as-is to confirm no regression
   (Verification Checklist, not a code change).

**Milestone 4 — Full verification and documentation sync.**

1. Fresh Debug and Release builds; `ctest -LE gpu` and `ctest -L gpu`
   both configurations; a clean `ATLANTIS_BUILD_TESTS=OFF` configure+build.
2. Vulkan Validation Layers grep-clean across full verbose GPU test
   output (zero `VUID`/Validation Error/Validation Warning).
3. Module/link boundary scan: confirm no module gained an unapproved new
   dependency (in particular, confirm nothing outside
   `tests/image_regression/` gained a new `Atlantis::RuntimeHost`
   dependency — the existing sanctioned exception is unchanged, not
   widened).
4. All five existing goldens confirmed byte-for-byte unchanged
   (`git diff main --quiet` against each PNG/sidecar) and pixel-zero-difference
   (existing comparison tooling, not re-run through the generator).
5. `git diff --check` clean; working tree clean at the end.
6. `specs/README.md`: Spec 0022's own row, Related Plan column — record
   this Plan (`In Review`) — this Milestone updates the registry, it does
   not restate PR #101/#102's own already-landed closeout content (per
   this round's own explicit instruction not to duplicate it).
7. `specs/0022-dynamic-frame-uniform-updates-foundation.md`: **Related
   Plan(s)** field in the header updated to link this Plan — a one-line,
   purely-additive registry-style edit, not a rewrite of the Spec's own
   `Approved` body.

## Files / Modules Touched (expected)

- `src/runtime/src/runtime_application.cpp` (Milestone 1)
- `src/runtime/include/atlantis/runtime/runtime_application.h`
  (Milestone 1)
- `tests/runtime/scene_extraction_tests.cpp` (Milestone 1)
- `tests/image_regression/fixture/dynamic_lighting_fixture.h` (new,
  Milestone 2)
- `tests/image_regression/fixture/dynamic_lighting_fixture.cpp` (new,
  Milestone 2)
- `tests/image_regression/fixture/CMakeLists.txt` (Milestone 2)
- `tests/image_regression/dynamic_lighting_gpu_tests.cpp` (new,
  Milestone 2)
- `tests/image_regression/CMakeLists.txt` (Milestone 2)
- `tests/runtime/runtime_smoke_gpu_tests.cpp` (Milestone 3)
- `specs/README.md` (Milestone 4)
- `specs/0022-dynamic-frame-uniform-updates-foundation.md` (Milestone 4,
  Related Plan field only)

**Explicitly not touched, and why:** `src/rhi/`, `src/vulkan_backend/`
(no RHI/backend change of any kind — confirmed by Pre-draft verification
above); `tests/image_regression/fixture/lighting_demo_fixture.{h,cpp}`,
`tests/image_regression/lighting_demo_gpu_tests.cpp` (the
`LightingDemoFixture` conflict, above — left exactly as Spec 0019
approved); `tests/image_regression/golden_generator/` and
`tests/image_regression/goldens/` (P5 — no new/regenerated golden);
`adr/0065-...md` (`Rejected`, historical record only — not revived, not
edited this round); `README.md`, `src/README.md`,
`docs/architecture/*.md`, `docs/project-blueprint.md` (no post-merge
closeout content this round — this Plan does not repeat PR #101/#102's
own already-landed documentation).

## Sequencing & Dependencies

Milestone 1 has no dependency on Milestone 2 or 3 (the `RuntimeApplication`
change and the new offscreen fixture are independent consumers of the
same already-existing `extractFrameLightingData()`) and could land first
in isolation, but this Plan sequences them 1 → 2 → 3 → 4 so that
Milestone 2's own fixture — which reuses the *identical* extraction call
Milestone 1 removes the guard from — is reviewed against Milestone 1's
own already-landed, unconditional version, not a hypothetical one.
Milestone 3 depends on Milestone 1 (it exercises the real, changed
`runFrame()`). Milestone 4 depends on all three.

## Verification Checklist

Maps to Spec 0022's own Testing & Verification Plan section by name.

**GPU-independent (V1-V10):**

- [ ] V1: `lightingDataCaptured_` has zero remaining references anywhere
      in the repository (`grep -r lightingDataCaptured_`).
- [ ] V2: `extractFrameLightingData()` called twice with a shrinking
      Point-light set zeros the second call's now-unused trailing slot
      (Milestone 1's new test).
- [ ] V3: Existing `extractFrameLightingData()`/`checkConformalTransform()`/
      `computeLambertianDiffuse()`/`extractCameraMatrices()` tests in
      `scene_extraction_tests.cpp` still pass unmodified.
- [ ] V4: `atlantis::rhi::Device`'s pure-virtual method set is unchanged
      from `main` at this Plan's own base commit (a diff of `device.h`).
- [ ] V5: `VulkanPresentation::acquireNextTarget()`'s Step 0 code
      (`vulkan_presentation.cpp:527-546`) has zero diff against `main`.
- [ ] V6: `World::updateTransforms()`/`setLight()`/`setLocalTransform()`/
      `setParent()`/`createEntity()`/`destroyEntity()` in `world.cpp` have
      zero diff against `main`.
- [ ] V7: `runFrame()`'s Camera write (`:539-541`), `createCommandList()`
      call site, `Renderer::drawFrame()` call site, and `submit()`/
      `present()` call sites are unchanged in position and content —
      only the Lighting block's own guard is removed.
- [ ] V8: `FrameLightingData`'s own `static_assert` set
      (`scene_extraction.h:101-122`) is unchanged.
- [ ] V9: `LightingDemoFixture`, `renderLightingDemoFrame()`, and every
      existing `lighting_demo_gpu_tests.cpp` `TEST_CASE` are unchanged
      (zero diff).
- [ ] V10: Module/link boundary scan: no new dependency edge anywhere
      outside `tests/image_regression/`'s own already-sanctioned
      `Atlantis::RuntimeHost` link.

**Real GPU (V11-V24):**

- [ ] V11: The new fixture's multi-cycle mechanics test (three cycles, no
      mutation, byte-identical readback each time) passes.
- [ ] V12: Directional Light direction change reflected next cycle,
      matching an independently-computed expected sign/magnitude.
- [ ] V13: Directional Light color/intensity change reflected next
      cycle, matching an independently-computed expected value.
- [ ] V14: Point Light position (attenuation) change reflected next
      cycle, matching an independently-computed expected ratio.
- [ ] V15: Point Light color/intensity change reflected next cycle.
- [ ] V16: A Light's own local `Transform` change reflected next cycle.
- [ ] V17: A Light's parent `Transform` change reflected next cycle,
      distinct from V16.
- [ ] V18: Light entity creation reflected next cycle (new contribution
      appears).
- [ ] V19: Light entity destruction reflected next cycle (contribution
      disappears, CPU `pointLightCount`/trailing-slot bytes confirm the
      freed slot is zeroed).
- [ ] V20: Same-cycle multiple `setLight()` calls resolve to final value
      only.
- [ ] V21: Dual CPU-byte + GPU-pixel evidence present for at least the
      direction-change and creation/destruction cases.
- [ ] V22: `windowed` smoke-test extension (Milestone 3): a mid-run
      `World` light mutation completes the run cleanly, zero Validation
      Layers hits.
- [ ] V23: Existing `runtime_smoke_gpu_tests.cpp` Camera/format-change
      coverage unchanged and still passing.
- [ ] V24: Spec 0018's format-change candidate submit-safe swap-in and
      Spec 0021's descriptor-pool growth (N=2/N=6/reuse) regression
      suites unchanged and still passing.

**Full matrix (V25-V31):**

- [ ] V25: Fresh Debug build clean; fresh Release build clean.
- [ ] V26: `ctest -LE gpu` — both configurations, no new failures.
- [ ] V27: `ctest -L gpu` — both configurations, no new failures.
- [ ] V28: Clean `ATLANTIS_BUILD_TESTS=OFF` configure+build.
- [ ] V29: All five existing goldens byte-for-byte unchanged and
      pixel-zero-difference; golden generator never invoked.
- [ ] V30: Vulkan Validation Layers grep-clean across full verbose GPU
      test output.
- [ ] V31: `git diff --check` clean; working tree clean.

## Rollback Plan

Every file this Plan touches is additive or a small, isolated removal
(one `bool` member, one `if` guard) inside `RuntimeApplication` — a
single `git revert` of this Plan's own merge commit fully restores the
one-time-capture behavior with no other module affected. The new fixture/
test files are net-new and can be deleted independently if only they need
reverting. No golden, no RHI surface, no ADR is touched, so no
migration or forward-compatibility concern exists for a rollback.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan: none — every applicable item (build clean,
tests pass, Validation Layers clean, goldens unchanged, module boundaries
respected, PR links Spec/ADR) is already covered by the Verification
Checklist above.
