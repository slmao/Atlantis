# Plan: Dynamic Frame Uniform Updates Foundation

- **Spec:** [specs/0022-dynamic-frame-uniform-updates-foundation.md](../specs/0022-dynamic-frame-uniform-updates-foundation.md)
  (`Approved`, corrected design — see that Spec's own "Correction —
  2026-08-30" and "Human Review Approval (corrected design)" sections)
- **Status:** Approved / Ready for Implementation
- **Author:** slmao
- **Human Review Approval (2026-08-30):** Reviewed and approved by slmao
  (`slmao <slmaosjtu@gmail.com>`, this repository's git-identified
  maintainer) on 2026-08-30, following the centralized final review round
  recorded below — see "Final Review Round" for the complete, itemized
  record. **This approval authorizes Implementation of this Plan only
  once this PR itself has merged — not before.**

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

### `LightingDemoFixture` — corrected decision: refactor it, do not fork it (Plan Review finding, 2026-08-30)

**This section replaces an earlier draft of this Plan, which proposed
adding a second, separate `DynamicLightingFixture` and leaving
`LightingDemoFixture` untouched. A centralized Plan Review found that
decision wrong** — it would have left a real, currently-passing test
(`lighting_demo_gpu_tests.cpp:153`) asserting, as a correctness claim,
the exact behavior contract Spec 0022 (`Approved`) exists to remove —
two simultaneously-passing, semantically-opposite authorities over the
identical real question ("does a `World` mutation between two renders
reach the published bytes?"), not a difference confined to two
unrelated fixtures. Full re-investigation below.

**`LightingDemoFixture` is a test-private composition root, not a
historical public API.** It lives under `tests/image_regression/fixture/`,
is never referenced outside `tests/image_regression/`, and its own
header comment (`lighting_demo_fixture.h:29-38`) states plainly it calls
`RuntimeHost`'s *real* `loadAndInstantiateScene()`/
`computePendingMaterialIds()`/`realizePendingMaterials()`/
`extractCameraMatrices()`/`extractFrameLightingData()`/
`checkConformalTransform()` directly — "never a fixture-private
reimplementation." `renderLightingDemoFrame()`'s own body
(`lighting_demo_fixture.cpp:189-236`, read in full) is line-for-line
structurally identical to `RuntimeApplication::runFrame()`'s own
corresponding block: `fixture.world->updateTransforms()` (`:198`,
unconditional, matching `runtime_application.cpp:497`), the Camera write
(`:212-214`, matching `:539-541`), then the *identical* guarded Lighting
block (`:216-236`, matching `:543-576` variable-for-variable — even the
local names `lightInputs`/`lightingResult`/`lightingData` match). This
is the same real Lighting payload this Plan already establishes as
authoritative for `RuntimeApplication` — not a second, independent
implementation.

**Spec 0022 has the authority to, and explicitly does, supersede Spec
0019's "captured once" contract.** Spec 0022's own Corrected Motivation
names Spec 0019's D1 ("captured once, never updated again") as "real and
... this Spec's sole remaining goal" to remove — a later, `Approved` spec
revising an earlier one's contract, exactly the mechanism AGENTS.md's own
workflow provides for ("a spec that turns out to be wrong gets a revision
or follow-up spec, reviewed like any other change"). `TEST_CASE("LightingDemoFixture:
World::setLight() after the one-time capture changes World state but
never the already-published GPU FrameLightingData bytes", ...)`
(`lighting_demo_gpu_tests.cpp:153-207`, read in full) is not a test of
some unrelated fixture detail — every one of its own assertions
(`REQUIRE(fixture.lightingDataCaptured)`, `:162`; the pre/post-mutation
byte-identity checks, `:190-206`; the comment at `:196-197`, "lightingDataCaptured
is already true, so no recapture happens") is a direct, load-bearing
claim that the static-snapshot behavior is *correct*. Spec 0022's own
approval directly contradicts that claim for the real system this test
exists to validate.

**Exhaustively re-read every `TEST_CASE` in `lighting_demo_gpu_tests.cpp`
(all nine, `:132-608`) to confirm which ones actually depend on the
guard, rather than assuming:**

| Test (`:` line) | Calls `renderLightingDemoFrame()` | Depends on the guard? |
|---|---|---|
| "renders a non-degenerate frame..." (`:132`) | once | No |
| **"World::setLight() after the one-time capture..." (`:153`)** | **twice, on the same fixture** | **Yes — its entire premise** |
| "non-conformal world transform is skipped..." (`:209`) | once per fixture (two fixtures) | No |
| "wrong MaterialKind..." (`:243`) | once per fixture (two fixtures) | No |
| "known cube vertex matches computeLambertianDiffuse()..." (`:279`) | once | No |
| "repeated render cycles... succeed independently" (`:401`) | twice, **no `World` mutation between calls** | No — asserts byte-identical pixels across two calls with nothing changed, which holds regardless of whether extraction is guarded or unconditional (identical input, identical output either way); this test is really a general multi-cycle-mechanics proof already, and needs no change |
| "Full capture-compare cycle against the committed golden..." (`:513`) | once | No |
| "deliberate Directional light direction-sign error..." (`:550`) | once (mutated scene) | No |
| "deliberate Point light attenuation error..." (`:581`) | once (mutated scene) | No |

**Exactly one of nine test cases depends on the guard.** The golden
comparison test and both deliberate-error negative tests each call
`renderLightingDemoFrame()` **exactly once** — the committed
`lighting_demo` golden is provably a function of the *first* render cycle
only, confirming making later cycles unconditional cannot move a single
golden pixel, without needing to re-run the generator to prove it. A
full-repository grep for `lightingDataCaptured\b` (fixture-local,
no trailing underscore, distinct from `lightingDataCaptured_`) confirms
exactly three files reference it — `lighting_demo_fixture.h`,
`lighting_demo_fixture.cpp`, `lighting_demo_gpu_tests.cpp` — all within
this Plan's own new, corrected scope; `golden_generator/lighting_demo_main.cpp`
also calls `renderLightingDemoFrame()` exactly once (confirmed by
reading it), unaffected either way and never invoked by this Plan (P5).

**Corrected decision: refactor `LightingDemoFixture` in place — no
second fixture.** This Plan now:

1. Removes `LightingDemoFixture::lightingDataCaptured`
   (`lighting_demo_fixture.h:79`) and its guard in
   `renderLightingDemoFrame()` (`lighting_demo_fixture.cpp:221`/`:235`)
   — the exact same mechanical change Milestone 1 makes to
   `RuntimeApplication`, applied to its structurally-identical fixture
   counterpart. Updates the now-stale header/body comments describing
   one-time capture (`:40-45`/`:75-79`/`:115-122` in the header;
   `:216-220` in the body) to describe the corrected, every-call
   contract.
2. Replaces `lighting_demo_gpu_tests.cpp:153-207`'s own `TEST_CASE`
   (the one test whose premise the guard's removal invalidates) with a
   new one proving the opposite, positive claim — reusing that test's
   own already-established "snapshot the 176 raw bytes directly from
   `mappedData()`, mutate `World`, snapshot again" technique (a real,
   reusable pattern, not reinvented), now asserting the bytes *do*
   change after a second render call, and that the rendered pixels
   change too.
3. Leaves every other `TEST_CASE` in the file untouched — each already
   confirmed unaffected above — and extends the same file with the new
   dynamic-behavior test cases (Milestone 2 below), all against this one,
   now-dynamic fixture.
4. Never creates `dynamic_lighting_fixture.{h,cpp}` or
   `dynamic_lighting_gpu_tests.cpp` — an earlier draft of this Plan
   named these; they are removed from the Files/Modules Touched list
   below.

This gives windowed `RuntimeApplication` and the offscreen fixture a
genuinely single authoritative dynamic code path — both call the
identical `World::updateTransforms()`/`extractFrameLightingData()`,
write the identical 176-byte region the identical way, and neither
duplicates the Lighting math or hand-writes its own slot-zeroing logic —
while each keeps its own, structurally-necessary frame-orchestration
glue (acquire/submit mechanics differ between a live swapchain and an
`OffscreenTarget`), exactly as Spec 0022's own Goals require ("windowed
and headless share one safety model," not one literal function body).

### Offscreen multi-cycle `submit()`→`waitIdle()` convention — re-confirmed universal

Re-confirmed (matching Spec 0022's own exhaustive audit, re-checked fresh
for this Plan): every multi-cycle `submit()` call site in
`tests/vulkan_backend/headless_rendering_gpu_tests.cpp` and
`tests/runtime/material_realization_gpu_tests.cpp` is immediately
followed by `REQUIRE(device->waitIdle().isOk())` before the next cycle's
own writes — a `grep -n "\.submit(\|->submit(\|waitIdle()"` across both
files shows no exception. The refactored `LightingDemoFixture` (Milestone
2) already follows this identical, already-universal convention today
(via its own already-existing `waitIdle()` call inside
`renderLightingDemoFrame()`) — this Plan invents no new one.

### CMake / test-target ownership — re-confirmed, zero CMake changes needed

`tests/image_regression/fixture/CMakeLists.txt` (full text read): the
`atlantis_image_regression_fixture` STATIC library already links
`Atlantis::RuntimeHost` **PUBLIC** — its own comment states this is "the
ONE explicitly-sanctioned exception to `RuntimeHost`'s usual 'no other
top-level module may depend on `Atlantis::RuntimeHost`' boundary — the
whole `tests/image_regression/` tree is the sanctioned consumer here, not
a new one" — plus `Atlantis::World`, `Atlantis::RHI`, `Atlantis::Renderer`
PUBLIC and `Atlantis::VulkanBackend`/`Atlantis::RenderGraph`/`Atlantis::ShaderSystem`/`Atlantis::ShaderSystemRhiIntegration`/`Atlantis::AssetSystem`
PRIVATE — every dependency this Plan's own Milestone 2 change to
`lighting_demo_fixture.cpp` needs already flows through this existing
library, with no CMake edit at all (this Plan modifies existing files
already in both libraries'/executables' own source lists — see "`LightingDemoFixture`
— corrected decision" above for why no new file is created).
`tests/image_regression/CMakeLists.txt` (full text read): the
`atlantis_image_regression_gpu_tests` executable's source list is a flat
list of `.cpp` files (`image_regression_gpu_tests.cpp`,
`world_scene_gpu_tests.cpp`, ..., `lighting_demo_gpu_tests.cpp`) already
linking `Atlantis::ImageRegressionFixture` — `lighting_demo_gpu_tests.cpp`
is already in that list; this Plan's own new `TEST_CASE`s land inside it,
not a new file. `tests/runtime/CMakeLists.txt`
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

- **P1 — refactor `LightingDemoFixture` in place; no second fixture.**
  See "`LightingDemoFixture` — corrected decision" above for the full,
  test-by-test evidence. `LightingDemoFixture`/`setUpLightingDemoFixture()`/
  `renderLightingDemoFrame()` keep their own existing names — only the
  guard is removed and one `TEST_CASE` is replaced.
- **P2 — the new dynamic-behavior test cases live in the same file,
  `tests/image_regression/lighting_demo_gpu_tests.cpp`, against the same
  fixture.** Not a new file, not a new location: these tests exercise the
  identical fixture every other `TEST_CASE` in this file already uses: no
  new CMake target, no new dependency wiring, and every reader of this
  file sees the fixture's full behavior — static-scene baseline, golden
  comparison, negative error cases, and now dynamic updates — in one
  place, matching the "single authoritative fixture" principle this
  correction round established. `tests/vulkan_backend/` (no scene/asset-
  loading path at all) is not used, for the same reason the original
  draft already ruled it out for a hypothetical second fixture.
- **P3 — the dynamic test cases mutate the *same* `lighting_demo_scene`
  cooked assets/`World` instance `LightingDemoFixture` already loads**,
  not a new scene asset. `World::setLight()`/`setLocalTransform()`/
  `setParent()`/`destroyEntity()`/`createEntity()` calls made directly
  against the fixture's own live `World` instance (after initial scene
  load, between `renderLightingDemoFrame()` calls) produce every scenario
  this Plan's own verification needs (Directional/Point parameter
  changes, local/parent Transform changes, entity creation/removal) — no
  new scene asset, no new cook step, no new shader.
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
- **P6 — the windowed regression check gives real byte-level proof
  against `RuntimeApplication` itself, but never a pixel-level
  claim.** Windowed `RenderTarget`s cannot be read back
  (`VK_IMAGE_USAGE_TRANSFER_SRC_BIT` is never set on a swapchain image —
  Spec 0022's own confirmed fact), so Milestone 3 cannot and does not
  claim pixel evidence — that remains entirely the refactored
  `LightingDemoFixture`'s own responsibility (Milestone 2). What
  Milestone 3 *does* give, corrected from an earlier draft's weaker
  "the run didn't crash" framing: a direct, raw-byte observation of
  `RuntimeApplication`'s own real `cameraBuffer_`, via one new accessor
  on the already-existing, already-approved `RuntimeSmokeTestAccess`
  test-only friend struct — proving the changed code path executes on a
  genuine second windowed frame, not only that the structurally-identical
  fixture path does.
- **P7 — no new public layout API for Camera/Lighting byte offsets.**
  Spec 0022's own evidence already locks the 128/176/304-byte boundary
  via `FrameLightingData`'s own `static_assert`s and the buffer's own
  `sizeof(float) * 32 + sizeof(FrameLightingData)` construction
  (`runtime_application.cpp:288-289`, unchanged by this Plan). If a test
  file's own readability benefits from a local `constexpr` naming these
  offsets (e.g. `constexpr std::size_t kLightingByteOffset =
  sizeof(float) * 32;`), that constant is file-local/anonymous-namespace
  scoped in the test file that needs it — never a new public header
  declaration, and never a production-code change beyond what Milestone 1
  already makes.
- **P8 — a Lighting-extraction failure never partially overwrites the
  mapped buffer, today or after this Plan.** `extractFrameLightingData()`
  builds its own complete, local `FrameLightingData` value and returns it
  by value only on success (`scene_extraction.cpp:156-224`); the caller's
  own write (`*lightingData = lightingResult.value();`) is reached only
  after an explicit `if (lightingResult.isErr()) { ...; return; }` check
  — already true of the existing, guarded code, structurally unchanged by
  removing the guard (the write statement itself is not touched, only the
  `if (!lightingDataCaptured_)` wrapped around the whole block is
  removed). No new error path, no new partial-write risk.

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

**Milestone 2 — Refactor `LightingDemoFixture` to dynamic, replace its
one now-obsolete negative test, add the new dynamic-behavior tests
(pixel + byte evidence) — one authoritative fixture, no fork.**

1. `tests/image_regression/fixture/lighting_demo_fixture.h`: remove the
   `lightingDataCaptured` field (`:79`); rewrite the header comments that
   describe one-time capture (`:40-45`, `:75-79`, `:115-122`) to describe
   the corrected, every-call contract, citing Spec 0022 — the same class
   of comment update Milestone 1 makes in `runtime_application.cpp`.
2. `tests/image_regression/fixture/lighting_demo_fixture.cpp`: remove
   the `if (!fixture.lightingDataCaptured) { ... }` guard around
   `renderLightingDemoFrame()`'s own Lighting block (`:221`/`:235`),
   leaving its body — light enumeration, `extractFrameLightingData()`
   call, error handling, the 176-byte write — unconditional, exactly
   mirroring Milestone 1's own change to `RuntimeApplication`. No other
   line in this function changes: acquire, `updateTransforms()`, the
   Camera write, material realization, `checkConformalTransform()`,
   draw/copy/submit remain byte-for-byte as they are today. The *same*
   `cameraBuffer`/`depthTexture`/`offscreenTarget`/`readbackBuffer`/
   `Mesh`/`Material`/`Pipeline`/descriptor-set map this fixture already
   owns across its own already-existing repeated-call capability (proven
   today by the untouched "repeated render cycles... succeed
   independently" test, `lighting_demo_gpu_tests.cpp:401`) continue to be
   reused across cycles — nothing here is recreated per cycle; this
   Milestone adds no new resource-creation call.
3. `tests/image_regression/lighting_demo_gpu_tests.cpp`:
   - **Replace** `TEST_CASE("LightingDemoFixture: World::setLight() after
     the one-time capture changes World state but never the
     already-published GPU FrameLightingData bytes", ...)` (`:153-207`)
     with a new `TEST_CASE` proving the corrected, opposite contract —
     reusing the exact same "snapshot the raw 176 bytes from
     `cameraBuffer->mappedData()` at offset `sizeof(float) * 32`, mutate
     `World`, snapshot again" technique that test already established
     (`:165-170`/`:192-193`), now asserting: the pre-mutation-only bytes
     are unchanged (a mutation alone, with no render call, still changes
     nothing — this part of the old test's own logic is still correct
     and is kept), but the bytes captured **after** the second
     `renderLightingDemoFrame()` call **do** change, and the second
     call's own rendered pixels differ from the first's (the light
     actually got darker/brighter/recolored, matching the deliberate
     mutation applied) — the direct logical negation of the old test's
     own final assertions.
   - Every other existing `TEST_CASE` in this file is left untouched —
     each independently confirmed above to depend only on a single
     `renderLightingDemoFrame()` call, or (for the "repeated render
     cycles" test) two calls with no `World` mutation between them, both
     unaffected by removing the guard.
   - Add new `TEST_CASE`s (each identifying a specific, real hazard, not
     a single "bufferA != bufferB" catch-all, per Spec 0022's own
     requirement), all against the same, now-dynamic
     `LightingDemoFixture`:
     - Multi-cycle mechanics, no mutation: three cycles with no `World`
       change between them produce byte-identical readback pixels each
       time — proves the cycle machinery (reuse + `waitIdle()`) is sound
       before any test relies on it to prove a *change*. (The existing
       "repeated render cycles" test already partially covers this with
       two cycles; this adds a third-cycle variant and states the
       "machinery, not correctness of a change" framing explicitly.)
     - Directional Light direction change: a deliberate 90°-rotation
       mutation between cycles; assert the readback pixel at the same
       known, hand-picked cube-face sample point this file's own "known
       cube vertex" test already established (`:279-399`) changes in the
       sign/magnitude an independently-computed expected value predicts
       for that specific rotation (P4 — never a call into
       `computeLambertianDiffuse()` for the "expected" side).
     - Directional Light color/intensity change: same shape, an
       independently-computed expected color/brightness ratio.
     - Point Light position (distance-attenuation) change: an
       independently-computed expected attenuation ratio at the fixed
       sample point.
     - Point Light color/intensity change: same shape as Directional.
     - A Light entity's own local `Transform` change (`setLocalTransform()`
       directly on the scene's light entity) — reflected next cycle.
     - A Light's parent entity's `Transform` change (`setParent()`
       establishing the relationship if the scene's own light is not
       already parented, then a parent `setLocalTransform()`) —
       reflected next cycle, distinct from the light's own
       local-transform case above (proves `updateTransforms()`'s own
       hierarchy propagation, not only a leaf-entity change).
     - Light entity creation: `fixture.world->createEntity()` plus
       `setLight()` for a *new* Point light not present in the original
       scene — its contribution appears next cycle.
     - Light entity destruction: `fixture.world->destroyEntity()` on an
       existing light — its contribution disappears (readback returns to
       the pre-creation baseline) next cycle, and the CPU-side
       `pointLightCount` decreases with the freed slot's own bytes
       zeroed (dual CPU-byte + GPU-pixel evidence for the same event,
       per Spec 0022's own requirement).
     - Same-cycle multiple mutations: two `setLight()` calls against the
       same entity before one `renderLightingDemoFrame()` call — only
       the final call's value is reflected.
     - CPU-byte snapshot comparison: for at least the direction-change
       and creation/destruction cases above, additionally capture and
       diff the raw 176-byte region directly from `cameraBuffer->mappedData()`
       (the same technique the replaced test already established) — not
       a substitute for the GPU-pixel evidence, an addition to it.
     - Validation Layers clean across the entire multi-cycle run.
   - No change to `tests/image_regression/fixture/CMakeLists.txt` or
     `tests/image_regression/CMakeLists.txt` — the fixture and test
     executable both already build/link everything this Milestone needs.

**Milestone 3 — Windowed regression extension, with real byte-level
proof the changed `RuntimeApplication` code path executes on a real
second frame (not only "no crash") — P6, refined per Plan Review.**

`runtime_smoke_gpu_tests.cpp` already declares a narrow, existing,
already-approved test-only friend, `RuntimeSmokeTestAccess`
(`:38-42`, granted access via `runtime_application.h:84`'s own `friend
struct RuntimeSmokeTestAccess;`), today exposing exactly one accessor,
`renderableEntityCount()`. This Milestone extends that *same* struct
with one more narrow, read-only accessor — not a new pattern, not a new
production API, the identical mechanism this file already uses for the
identical class of need — rather than settling for a "the run didn't
crash" proof alone, which the Plan Review round found insufficient on
its own (a crash-free run does not, by itself, prove the second frame's
Lighting write actually happened).

1. `tests/runtime/runtime_smoke_gpu_tests.cpp`: add
   `RuntimeSmokeTestAccess::lightingPayloadBytes(const RuntimeApplication&
   app)`, returning a copy of the 176 raw bytes at `app.cameraBuffer_`'s
   own `mappedData()` offset `sizeof(float) * 32` — the identical
   "snapshot the mapped bytes directly" technique already established in
   `lighting_demo_gpu_tests.cpp` (Milestone 2), applied here to
   `RuntimeApplication`'s own real buffer instead of the fixture's.
2. Confirmed: the default `world_scene` scene this test already loads
   (`ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH`) has **zero** `light` nodes
   (`grep -c light assets/scenes/world_scene.scene.txt` returns no
   matches) — this Milestone must not claim the *existing* smoke test
   scenario already exercises a light change; it must actively add one.
3. New `TEST_CASE`, matching the existing test's own `kSmokeTestFrameCount`-loop
   shape (`:91-92`): after the first `runFrame()` call, snapshot
   `lightingPayloadBytes()` (176 zero bytes — no light in this scene
   yet, `directionalLightCount == 0 && pointLightCount == 0`, matching
   `FrameLightingData`'s own value-initialized default), then call
   `world_->createEntity()` plus `World::setLight()` to add one real
   Point light directly against the running app's own live `World`
   (a legitimate use of `World`'s own already-public API, not a new
   Runtime-level API), then call one more `runFrame()`, then snapshot
   `lightingPayloadBytes()` again — asserting the second snapshot's
   `pointLightCount == 1` and its own light fields match the values just
   set, **direct, real, byte-level evidence that the changed
   `RuntimeApplication` code path — not only the structurally-identical
   `LightingDemoFixture` path — actually executes the Lighting write on a
   genuine second windowed frame.** A second scenario, in the same style,
   calls `setLocalTransform()` on that same light entity between two
   further `runFrame()` calls and confirms the position field changes
   accordingly (proving `updateTransforms()`'s own already-every-frame
   call, `:497`, is what makes this observable — Milestone 1 adds no new
   call to it).
4. The existing "no crash" properties (`app.shouldContinue()` stays true
   throughout, Validation Layers clean, resize/minimize/restore/close
   behavior unaffected) are still asserted, but this Milestone's own
   headline claim is the byte-level proof in step 3-4 above, not the
   absence of a crash alone.
5. No change to `runtime_smoke_gpu_tests.cpp`'s own existing Camera/
   format-change coverage — re-run as-is to confirm no regression
   (Verification Checklist, not a code change).
6. **Explicitly not claimed**: pixel-level windowed proof (P6, unchanged
   — swapchain images carry no `TRANSFER_SRC_BIT`) — real GPU pixel
   evidence for the dynamic behavior remains entirely Milestone 2's own
   responsibility, via the refactored `LightingDemoFixture`.

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
- `tests/image_regression/fixture/lighting_demo_fixture.h` (Milestone 2
  — guard field removed, comments corrected; **not** a new file)
- `tests/image_regression/fixture/lighting_demo_fixture.cpp` (Milestone
  2 — guard removed from `renderLightingDemoFrame()`)
- `tests/image_regression/lighting_demo_gpu_tests.cpp` (Milestone 2 —
  one `TEST_CASE` replaced, new dynamic-behavior `TEST_CASE`s added,
  every other existing `TEST_CASE` untouched)
- `tests/runtime/runtime_smoke_gpu_tests.cpp` (Milestone 3 — extends the
  already-existing `RuntimeSmokeTestAccess` friend struct with one new
  accessor; `runtime_application.h`'s own already-generic `friend struct
  RuntimeSmokeTestAccess;` declaration, `:84`, needs no change)
- `specs/README.md` (Milestone 4)
- `specs/0022-dynamic-frame-uniform-updates-foundation.md` (Milestone 4,
  Related Plan field only)

**No new files.** An earlier draft of this Plan proposed
`tests/image_regression/fixture/dynamic_lighting_fixture.{h,cpp}` and
`tests/image_regression/dynamic_lighting_gpu_tests.cpp`; a Plan Review
found the correct design refactors `LightingDemoFixture` in place
instead (see "`LightingDemoFixture` — corrected decision" above) — none
of those three files are created, and neither
`tests/image_regression/fixture/CMakeLists.txt` nor
`tests/image_regression/CMakeLists.txt` needs any change (both already
build/link everything this Plan's own Milestone 2 needs).

**Explicitly not touched, and why:** `src/rhi/`, `src/vulkan_backend/`
(no RHI/backend change of any kind — confirmed by Pre-draft verification
above); `tests/image_regression/golden_generator/` and
`tests/image_regression/goldens/` (P5 — no new/regenerated golden — the
existing `lighting_demo` golden is provably a function of
`renderLightingDemoFrame()`'s own *first* call only, per the
`LightingDemoFixture` decision's own test-by-test table, so Milestone 2's
change cannot move it); `adr/0065-...md` (`Rejected`, historical record
only — not revived, not edited this round); `README.md`, `src/README.md`,
`docs/architecture/*.md`, `docs/project-blueprint.md` (no post-merge
closeout content this round — this Plan does not repeat PR #101/#102's
own already-landed documentation).

## Sequencing & Dependencies

Milestone 1 has no strict dependency on Milestone 2 (the `RuntimeApplication`
change and `LightingDemoFixture`'s own change are independent consumers
of the same already-existing `extractFrameLightingData()`, in two
separate composition roots) and could land first in isolation, but this
Plan sequences them 1 → 2 → 3 → 4 so that Milestone 2's own refactor —
mechanically identical to Milestone 1's own — is reviewed immediately
after the pattern it mirrors has already landed, not before. Milestone 3
depends on Milestone 1 (it exercises the real, changed `runFrame()`).
Milestone 4 depends on all three.

## Verification Checklist

Maps to Spec 0022's own Testing & Verification Plan section by name.

**GPU-independent (V1-V11):**

- [ ] V1: `RuntimeApplication::lightingDataCaptured_` has zero remaining
      references anywhere in the repository (`grep -r lightingDataCaptured_`).
- [ ] V2: `LightingDemoFixture::lightingDataCaptured` (fixture-local, no
      trailing underscore) has zero remaining references anywhere in the
      repository — a separate check from V1, since these are two
      different, formerly-coexisting flags this Plan removes from two
      different composition roots.
- [ ] V3: `extractFrameLightingData()` called twice with a shrinking
      Point-light set zeros the second call's now-unused trailing slot
      (Milestone 1's new test).
- [ ] V4: Existing `extractFrameLightingData()`/`checkConformalTransform()`/
      `computeLambertianDiffuse()`/`extractCameraMatrices()` tests in
      `scene_extraction_tests.cpp` still pass unmodified.
- [ ] V5: `atlantis::rhi::Device`'s pure-virtual method set is unchanged
      from `main` at this Plan's own base commit (a diff of `device.h`).
- [ ] V6: `VulkanPresentation::acquireNextTarget()`'s Step 0 code
      (`vulkan_presentation.cpp:527-546`) has zero diff against `main`.
- [ ] V7: `World::updateTransforms()`/`setLight()`/`setLocalTransform()`/
      `setParent()`/`createEntity()`/`destroyEntity()` in `world.cpp` have
      zero diff against `main`.
- [ ] V8: `runFrame()`'s Camera write (`:539-541`), `createCommandList()`
      call site, `Renderer::drawFrame()` call site, and `submit()`/
      `present()` call sites are unchanged in position and content —
      only the Lighting block's own guard is removed.
- [ ] V9: `FrameLightingData`'s own `static_assert` set
      (`scene_extraction.h:101-122`) is unchanged.
- [ ] V10: `LightingDemoFixture`/`renderLightingDemoFrame()`: the guard is
      removed (matching V2); exactly one `TEST_CASE` — the former
      "World::setLight() after the one-time capture..." negative test —
      is absent from `lighting_demo_gpu_tests.cpp`'s own diff, replaced by
      its positive counterpart; every *other* pre-existing `TEST_CASE` in
      that file (eight of the original nine) has zero diff and still
      passes unmodified — confirming this Plan changed exactly what
      "`LightingDemoFixture` — corrected decision" says it changes, no
      more, no less.
- [ ] V11: Module/link boundary scan: zero new files, zero new CMake
      targets or `target_link_libraries` entries, no dependency edge
      anywhere outside `tests/image_regression/`'s own already-sanctioned
      `Atlantis::RuntimeHost` link — confirming no second fixture/test
      executable was created.

**Real GPU (V12-V26):**

- [ ] V12: The refactored `LightingDemoFixture`'s own multi-cycle
      mechanics test (three cycles, no mutation, byte-identical readback
      each time) passes.
- [ ] V13: Directional Light direction change reflected next cycle,
      matching an independently-computed expected sign/magnitude.
- [ ] V14: Directional Light color/intensity change reflected next
      cycle, matching an independently-computed expected value.
- [ ] V15: Point Light position (attenuation) change reflected next
      cycle, matching an independently-computed expected ratio.
- [ ] V16: Point Light color/intensity change reflected next cycle.
- [ ] V17: A Light's own local `Transform` change reflected next cycle.
- [ ] V18: A Light's parent `Transform` change reflected next cycle,
      distinct from V17.
- [ ] V19: Light entity creation reflected next cycle (new contribution
      appears).
- [ ] V20: Light entity destruction reflected next cycle (contribution
      disappears, CPU `pointLightCount`/trailing-slot bytes confirm the
      freed slot is zeroed).
- [ ] V21: Same-cycle multiple `setLight()` calls resolve to final value
      only.
- [ ] V22: Dual CPU-byte + GPU-pixel evidence present for at least the
      direction-change and creation/destruction cases.
- [ ] V23: Windowed byte-level proof (Milestone 3): a real second
      `app.runFrame()` call, after a `World::createEntity()`/`setLight()`
      mutation made directly against the running app's own live `World`,
      changes `RuntimeSmokeTestAccess::lightingPayloadBytes(app)`'s own
      raw 176-byte snapshot from all-zero (`world_scene`'s own real,
      confirmed light-free baseline) to the values just set — direct
      evidence against `RuntimeApplication` itself, not only the
      structurally-identical `LightingDemoFixture` path.
- [ ] V24: A further windowed byte-level proof: `setLocalTransform()` on
      that same light entity between two more `runFrame()` calls changes
      the published position field accordingly.
- [ ] V25: Existing `runtime_smoke_gpu_tests.cpp` Camera/format-change
      coverage unchanged and still passing.
- [ ] V26: Spec 0018's format-change candidate submit-safe swap-in and
      Spec 0021's descriptor-pool growth (N=2/N=6/reuse) regression
      suites unchanged and still passing.

**Full matrix (V27-V33):**

- [ ] V27: Fresh Debug build clean; fresh Release build clean.
- [ ] V28: `ctest -LE gpu` — both configurations, no new failures.
- [ ] V29: `ctest -L gpu` — both configurations, no new failures.
- [ ] V30: Clean `ATLANTIS_BUILD_TESTS=OFF` configure+build.
- [ ] V31: All five existing goldens byte-for-byte unchanged and
      pixel-zero-difference; golden generator never invoked.
- [ ] V32: Vulkan Validation Layers grep-clean across full verbose GPU
      test output.
- [ ] V33: `git diff --check` clean; working tree clean.

## Final Review Round

A centralized final review, conducted at explicit human direction before
approval, re-examined this Plan's own first draft against the real,
current codebase — specifically whether it truly established one
authoritative dynamic Lighting code path, rather than leaving a
now-superseded static-snapshot test standing as a contradictory second
authority alongside a newly-proposed, duplicate fixture.

**Central finding, corrected in this round:** the first draft's own
decision (P1: add a new, separate `DynamicLightingFixture`, leave
`LightingDemoFixture` and its own static-snapshot negative test
untouched) was wrong. Re-reading `LightingDemoFixture`'s own header
comments and `lighting_demo_gpu_tests.cpp`'s own
`TEST_CASE("LightingDemoFixture: World::setLight() after the one-time
capture...")` in full confirmed that test's every assertion is a direct,
load-bearing claim that the static-snapshot behavior is *correct* —
directly contradicting Spec 0022's own `Approved` design, which exists
specifically to remove that behavior. `LightingDemoFixture` is a
test-private composition root (never referenced outside
`tests/image_regression/`), and its own `renderLightingDemoFrame()` was
confirmed, line-for-line, to run the identical `World::updateTransforms()`/
`extractFrameLightingData()`/176-byte-write sequence
`RuntimeApplication::runFrame()` runs — the same real code, not a
reimplementation. An exhaustive, test-by-test re-read of all nine
`TEST_CASE`s in `lighting_demo_gpu_tests.cpp` found exactly one depends
on the guard; the golden-comparison test and both deliberate-error
negative tests each call `renderLightingDemoFrame()` exactly once,
confirming the committed `lighting_demo` golden is provably a function
of the first render cycle only, and cannot move by making later cycles
unconditional.

**Correction made in this same round:** this Plan now refactors
`LightingDemoFixture` in place (removing its own `lightingDataCaptured`
field and guard, the exact mechanical twin of Milestone 1's own
`RuntimeApplication` change), replaces the one now-invalidated negative
test with its positive counterpart (reusing that test's own established
byte-snapshot technique), and adds every new dynamic-behavior test case
against this same, now-dynamic fixture — never a second one. Every other
pre-existing `TEST_CASE` in that file is confirmed, by the same
test-by-test table, to need no change. `dynamic_lighting_fixture.{h,cpp}`
and `dynamic_lighting_gpu_tests.cpp` are removed from this Plan's own
file list entirely.

**Also corrected in this round:** Milestone 3's own windowed regression
proof was strengthened from "the run didn't crash" to real, direct
byte-level evidence against `RuntimeApplication` itself — extending the
already-existing, already-approved `RuntimeSmokeTestAccess` test-only
friend struct with one new accessor into `cameraBuffer_`'s own raw bytes
(the identical mechanism `runtime_smoke_gpu_tests.cpp` already uses for
`renderableEntityCount()`, not a new pattern), and confirming — rather
than assuming — that the default `world_scene` scene this test already
loads has zero light nodes, so the new test scenario must actively add
one, not merely mutate an existing one.

**Confirmed unchanged and still correct from the first draft:** all
Pre-draft verification against `RuntimeApplication`/`FrameLightingData`/
`extractFrameLightingData()`/`World`'s real API/the RHI `Device` surface;
Milestone 1's own exact scope; P4/P5 (independently-computed expected
values, no new golden); the CMake/dependency-wiring facts (now correctly
stated as requiring zero changes for an in-place refactor, rather than
zero *new* wiring for a second fixture); the 31-then-33-item Verification
Checklist's own full-matrix coverage.

No blocking objection remains: the refactor is proven safe by direct,
exhaustive evidence (the test-by-test table above), not by assertion; the
windowed proof no longer rests on absence-of-crash alone; and no new
public API, RHI surface, ADR, golden, or second authoritative fixture
survives into this Plan's own final scope.

## Rollback Plan

No file this Plan touches is new — every change is a small, isolated
removal (a `bool` member, an `if` guard, in each of `RuntimeApplication`
and `LightingDemoFixture`) or an addition of new `TEST_CASE`s to
already-existing test files. A single `git revert` of this Plan's own
merge commit fully restores both the `RuntimeApplication` and
`LightingDemoFixture` one-time-capture behavior, including the original
static-snapshot negative test this Plan replaces, with no other module
affected and no file left orphaned. No golden, no RHI surface, no ADR is
touched, so no migration or forward-compatibility concern exists for a
rollback.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan: none — every applicable item (build clean,
tests pass, Validation Layers clean, goldens unchanged, module boundaries
respected, PR links Spec/ADR) is already covered by the Verification
Checklist above.

## Implementation Status Update (2026-08-30)

**Code complete on Implementation PR [#106](https://github.com/slmao/Atlantis/pull/106),
OPEN, not yet merged.** This section is purely additive — the "Human
Review Approval" note and every Milestone/decision recorded above it are
left unedited, matching this repository's own established post-merge
status-update convention (e.g. Plan 0021's own "Post-Merge Status
Update" section).

All four Milestones landed as planned, as three commits on
`feature/0022-dynamic-frame-uniform-updates-foundation` (M1; M2; M3 —
Milestone 4 is verification/documentation only, not a separate code
commit):

- **M1**: `RuntimeApplication::lightingDataCaptured_` and its guard
  removed; the Lighting-extraction block runs unconditionally, at its
  existing, unmoved position. Two new GPU-independent `TEST_CASE`s added
  to `tests/runtime/scene_extraction_tests.cpp`.
- **M2**: `LightingDemoFixture::lightingDataCaptured` and its guard
  removed — the exact mechanical twin of M1. The one now-invalidated
  `TEST_CASE` (the old "second render never recaptures" negative test)
  replaced with its positive counterpart; every other pre-existing
  `TEST_CASE` in `lighting_demo_gpu_tests.cpp` confirmed unchanged.
- **M3**: ten new dynamic multi-cycle `TEST_CASE`s added to
  `lighting_demo_gpu_tests.cpp` (Directional direction/color/intensity,
  Point position/color/intensity, local/parent `Transform`, `setParent()`,
  light creation/destruction, same-cycle multiple mutations); the
  already-existing `RuntimeSmokeTestAccess` test-only friend extended
  with two accessors, and the existing windowed smoke `TEST_CASE`
  extended (not duplicated) with real byte-level dynamic-Lighting proof.
- **M4**: full verification matrix executed (below); `specs/README.md`
  and this section updated.

**Deviations from this Plan's own original text, both found and
resolved during Milestone 3's own implementation, disclosed here rather
than silently absorbed:**

1. The real scene's own authored Point light position does not
   illuminate this file's own established sample point at all —
   confirmed empirically (every in-place mutation published
   byte-identical pixels before/after, regardless of which property was
   mutated), not assumed at Plan-drafting time. Every affected `TEST_CASE`
   now repositions the light first, to a location a different,
   already-passing test's own placement had already proven works — a
   `TEST_CASE`-level implementation detail, not a change to this Plan's
   own Milestone scope, decisions, or file list.
2. A second, independent windowed `RuntimeApplication` lifecycle in a
   sibling `TEST_CASE` (as this Plan's own M3 text originally described)
   passed in isolation but crashed the test process when run together
   with the pre-existing windowed smoke test — the same-process multiple
   windowed `initialize()`/`shutdown()` Platform limitation an earlier,
   unrelated Spec's own final review already found and explicitly
   disclosed as out of scope. Resolved by extending the existing single
   `TEST_CASE`'s own lifecycle instead of adding a second one — same
   file, same friend struct, same accessors, no scope change, no attempt
   to fix the underlying Platform limitation (still out of this Plan's
   own approved scope).
3. An earlier, intermediate verification pass on this branch reported a
   failure in `textured_quad_gpu_tests.cpp` (a file this Plan never
   touches) and disclosed it as a pre-existing, unrelated issue. Further
   investigation found this was a false alarm: the failure was an
   artifact of invoking the test executable directly from the wrong
   working directory during ad hoc verification — the same binary passes
   cleanly (276/276 assertions) when run from its own directory, matching
   exactly how `ctest` itself invokes it, and every `ctest -L gpu` run
   throughout this Plan's own implementation already reported it passing.
   No real pre-existing bug exists; that earlier disclosure is retracted
   here.

**Final, as-built verification:** fresh Debug and Release builds clean;
`ctest -LE gpu` 761/761 (Debug, +2 over the pre-Plan-0022 baseline of
759) and 760/760 (Release, +2 over 758); `ctest -L gpu` 62/62 both
configurations (+10 over the pre-Plan-0022 baseline of 52 — the ten new
dynamic Lighting `TEST_CASE`s; the windowed smoke test count is
unchanged at one, extended rather than duplicated); Vulkan Validation
Layers grepped clean (zero `VUID`/Validation Error/Validation Warning)
across full verbose GPU test output, both configurations; a fresh
`ATLANTIS_BUILD_TESTS=OFF` configure+build produced a working
`atlantis_runtime.exe` with zero test executables; `git diff --stat`
against `src/rhi/`, `src/vulkan_backend/`, every `CMakeLists.txt`,
`adr/`, and `tests/image_regression/goldens/` all returned empty —
zero touches, confirming no RHI/Vulkan-Backend/CMake/ADR/golden change
anywhere; all five existing goldens confirmed byte-for-byte unchanged
and, via the still-passing golden-comparison `TEST_CASE` itself,
pixel-zero-difference; `git diff --check` clean; working tree clean.

All 14 of this Plan's own Plan-Review self-check items (Runtime frame
order unmoved; `World` transforms still precede Lighting extraction;
Camera/Lighting byte ranges non-overlapping and unchanged; every frame's
own payload fully initialized; extraction failure never writes a partial
payload; the old static-snapshot fixture contract fully removed, not
merely shadowed; no two semantically-opposite fixtures coexisting;
multi-cycle tests genuinely reuse the same `Buffer`/`Pipeline`/descriptor
set; `waitIdle()` genuinely precedes each next cycle's own CPU write; the
windowed test performs no unsynchronized host write; GPU assertions
identify a specific direction/channel/count, never a bare inequality;
five goldens zero-diff; zero RHI/Vulkan-Backend/CMake/API change; zero
new thread/lock/dependency/platform code) were re-verified against real
diffs and real test runs immediately before opening
[PR #106](https://github.com/slmao/Atlantis/pull/106), not merely
asserted from memory.
