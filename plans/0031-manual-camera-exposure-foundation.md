# Plan: Manual Camera Exposure Foundation

- **Spec:** [specs/0031-manual-camera-exposure-foundation.md](../specs/0031-manual-camera-exposure-foundation.md) (`Approved`)
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Related ADR(s):** [ADR-0075](../adr/0075-manual-camera-exposure-data-and-output-transform-contract.md) (`Accepted`); [ADR-0068's own Accepted Amendment — 2026-09-08](../adr/0068-hdr-color-pipeline-output-transfer-architecture-and-tone-mapping-contract.md#accepted-amendment--2026-09-08) (`Accepted`, D-10 only)

## Objective

Implement Spec 0031 exactly as approved: `Camera.exposureCompensationEv`
threaded from scene data through Runtime into the output-transform
pass via a single, C++-computed exposure multiplier, reusing the
existing RHI push-constant mechanism, with `exposureCompensationEv =
0.0` reproducing every existing golden byte-for-byte.

## Plan-Stage Finding, Resolved by Human Direction (2026-09-08)

Real-code investigation (`scene_source.cpp`) found `parseSceneSource()`
gates every `.scene.txt` file behind an exact-string-match version line
(`kVersionLine`), no version-range support — and `git log -p` confirmed
this literal, and every existing checked-in scene file's own first
line, was bumped on **both** prior grammar extensions (v1→v2, v2→v3),
including the equally-additive light grammar. This directly conflicts
with Spec 0031 Requirement 3's own claim, "no existing `.scene.txt`
file requires editing." Human direction (chat, 2026-09-08) resolved
this by choosing to follow the established precedent exactly — see
Spec 0031's own new [Plan-Stage Correction — 2026-09-08](../specs/0031-manual-camera-exposure-foundation.md#plan-stage-correction--2026-09-08)
section. Milestone 1 below reflects that resolution: `kVersionLine`
bumps 3→4, and every existing scene source file (real assets and
inline test fixtures alike) gets its version line mechanically updated
alongside it.

## Milestones / Task Breakdown

Three atomic, strictly-ordered milestones. M1 must land before M2
(Runtime's own `drawFrame()` call site reads
`cameraComponent.exposureCompensationEv`, which requires M1's `Camera`
field to exist). M2 must land before M3 (the new tests exercise the
API/shader/pipeline surface M2 creates). Each milestone leaves the
repository fully buildable and every *existing* test passing
unmodified in behavior.

### Milestone 1 — Scene/World data model and versioning

**Production code:**

- `src/asset_system/include/atlantis/asset_system/scene_types.h`:
  `DecodedCamera` gains `float exposureCompensationEv = 0.0f;`. Two new
  named constants next to it,
  `inline constexpr float kExposureCompensationEvMin = -16.0f;` /
  `kExposureCompensationEvMax = 16.0f;` (Atlantis::AssetSystem's own
  independent copy — Requirement 7 of the Spec; Renderer's own copy is
  M2's concern, in a different module, deliberately not shared).
- `src/world/include/atlantis/world/camera.h`: `Camera` gains
  `float exposureCompensationEv = 0.0f;` (4th field).
- `src/asset_system/src/scene_source.cpp`:
  - `kVersionLine` (line 16): `"atlantis_scene_source_version: 3"` →
    `"...: 4"`.
  - **The node-line total-token-count whitelist (line 164-165),
    `tokens.size() != 11 && ... != 14 && ... != 16 && ... != 17`, is
    the real, earlier gate every node line must pass before any
    per-component branch below it is even reached — 15 is not in this
    list today.** It widens first, to also accept `15`
    (`tokens.size() != 11 && ... != 14 && ... != 15 && ... != 16 &&
    ... != 17`); only once 15 is accepted here does the camera branch
    below ever see a 15-token line. Skipping this step would make the
    camera-branch widening below unreachable dead code for any
    15-token line, rejected upstream as `InvalidComponentGroup`.
  - The `tokens.size() == 14` camera branch (line 234) widens to
    `tokens.size() == 14 || tokens.size() == 15`; when 15, token 14
    must match `camera_exposure_ev=<f>` (a new prefix constant,
    `kCameraExposureEvPrefix = "camera_exposure_ev="`, alongside the
    three existing camera-field prefixes at line 40-42), parsed the
    same `consumePrefixedFloat()` way as `fovYRadians`/`nearZ`/`farZ`;
    when 14, `camera.exposureCompensationEv` stays its default `0.0f`.
    Verified through the public `parseSceneSource()` entry point only
    (never a private/friend shortcut) — the new grammar test in
    Milestone 1's own test list below asserts a real 15-token camera
    line parses successfully via this exact function.
    No finite/range check here — mirrors `fovYRadians`/`nearZ`/`farZ`'s
    own existing precedent of deferring that check to `cookScene()`.
  - `serializeSceneSource()` (line 387-391): always emits
    `camera_exposure_ev=<f>` as a 4th camera field (never omitted) —
    producing the 15-token grammar unconditionally. Safe: the existing
    round-trip tests compare decoded *values*, never exact source text
    (confirmed by reading `scene_source_tests.cpp`'s own comparison
    loop), so this is not a behavior change for any existing test.
- `src/asset_system/src/cook_scene.cpp`: the camera finite-check block
  (line 186-192) gains a 4th condition,
  `!std::isfinite(parsedNode.camera->exposureCompensationEv) ||
  parsedNode.camera->exposureCompensationEv < kExposureCompensationEvMin ||
  parsedNode.camera->exposureCompensationEv > kExposureCompensationEvMax`,
  reusing `SceneCookError::NonFiniteValue` — no new enumerator, matching
  the established precedent that a light's own finite-but-out-of-domain
  color/intensity/range violation already reuses this exact enumerator
  (Spec 0031 Requirement 6).
- `src/asset_system/include/atlantis/asset_system/scene_artifact.h`:
  `kSceneArtifactSchemaVersion`: `3` → `4`.
  `kSceneArtifactNodeRecordSizeBytes`: `112` → `116`. Header comment
  (line 26-30) updated to name the new field. See the exact new offset
  table below.
- `src/asset_system/src/scene_artifact.cpp`:
  - `encodeSceneArtifact()`: one new `appendFloatLE(out,
    node.camera.has_value() ? node.camera->exposureCompensationEv :
    0.0f);` inserted immediately after the existing `farZ` append (line
    112), before `has_renderable` — matching the "written even when the
    node has no camera" convention `fovY`/`nearZ`/`farZ` already
    establish.
  - `decodeSceneArtifact()`: `const float exposureEv =
    readFloatLE(record + 52);` added; the existing camera finite-check
    (line 203-206) gains the same 4th condition as `cookScene()` above
    (finite + `[kExposureCompensationEvMin, kExposureCompensationEvMax]`),
    reusing `SceneArtifactDecodeError::NonFiniteValue`. **The
    construction immediately below the check, currently `node.camera =
    DecodedCamera{fovY, nearZ, farZ};` (line 207), changes to the
    4-argument `DecodedCamera{fovY, nearZ, farZ, exposureEv};`** — the
    one line that actually delivers the decoded value into the returned
    struct; reading `exposureEv` into a local without also widening
    this construction would silently discard it back to the field's own
    default `0.0f`, the exact class of defect the new end-to-end test
    below exists to catch. Every subsequent `record + N` read (line 210
    onward) shifts by `+4` per the offset table below.
- `src/world/src/scene_instantiation.cpp:27`: `Camera{n.camera->fovYRadians,
  n.camera->nearZ, n.camera->farZ}` → 4-arg,
  `..., n.camera->exposureCompensationEv}`.

**Exact new v4 record-offset table** (116 bytes; every field before
`exposureCompensationEv` keeps its v3 offset unchanged):

| Field | v3 offset | v4 offset |
|---|---|---|
| positionX/Y/Z, rotation, scale | 0–35 | 0–35 (unchanged) |
| has_camera | 36 | 36 (unchanged) |
| fov_y / near_z / far_z | 40/44/48 | 40/44/48 (unchanged) |
| **exposure_compensation_ev** | — | **52 (new)** |
| has_renderable | 52 | 56 |
| mesh_asset_id (u64) | 56 | 60 |
| has_material | 64 | 68 |
| material_asset_id (u64) | 68 | 72 |
| has_light | 76 | 80 |
| light_kind | 80 | 84 |
| color_r/g/b | 84/88/92 | 88/92/96 |
| intensity | 96 | 100 |
| range | 100 | 104 |
| has_parent | 104 | 108 |
| parent_index | 108 | 112 |
| **record size** | **112** | **116** |

**Real asset files (mechanical, version-line only — rg-confirmed,
exactly these 8, no other content changes):**
`assets/scenes/hdr_roll_off_demo.scene.txt`,
`assets/scenes/ibl_material_demo.scene.txt`,
`assets/scenes/integrated_showcase_demo.scene.txt`,
`assets/scenes/lighting_demo.scene.txt`,
`assets/scenes/material_demo.scene.txt`,
`assets/scenes/pbr_material_demo.scene.txt`,
`assets/scenes/pbr_normal_map_demo.scene.txt`,
`assets/scenes/world_scene.scene.txt`,
plus `assets/_test_fixtures/cmake_scene_declaration_test.scene.txt` — 9
files total, each a one-line `atlantis_scene_source_version: 3` → `4`
edit; no node/camera/light/mesh data in any of them changes.

**Tests (existing files — mechanical version-line bump plus the real
logic fixes rg found; exact occurrence counts confirmed via
`grep -c "atlantis_scene_source_version: 3"`):**

- `tests/asset_system/scene_source_tests.cpp` (35 occurrences): bump
  34 of them 3→4 (every occurrence *except* the two deliberate
  old/wrong-version literals at "version 1"/"version 2", which stay as
  they are — those test permanently-rejected versions, independent of
  which version is current). The "rejects an unrecognized future
  version line" test's own probe literal moves `"...version: 4"` →
  `"...version: 5"` (line ~142) — 4 is about to become valid. A new
  TEST_CASE, "parseSceneSource rejects the superseded version 3
  outright, with no dual-version reader", mirrors the existing v1/v2
  rejection tests exactly. New tests for the 15-token grammar: a
  14-token camera node still parses with `exposureCompensationEv ==
  0.0f`; a 15-token camera node with `camera_exposure_ev=<f>` parses
  that exact value; a malformed/wrong-prefix 15th token is rejected
  (`InvalidComponentGroup`, matching the existing wrong-prefix pattern
  for the other three camera fields). The existing round-trip
  comparison loop (`a.camera->fovYRadians == b.camera->fovYRadians`
  etc.) gains one more line comparing `exposureCompensationEv`.
- `tests/asset_system/cook_scene_tests.cpp` (11 occurrences): bump
  3→4. New TEST_CASEs mirroring the existing "V7: rejects a non-finite
  camera field" test: rejects a non-finite `exposureCompensationEv`;
  rejects a value below `kExposureCompensationEvMin`/above
  `kExposureCompensationEvMax`; accepts exactly `-16.0`/`+16.0` (the
  closed boundary).
- `tests/asset_system/decode_scene_tests.cpp` (1 occurrence of the
  version line, plus the byte-offset fixes rg found by reading every
  hardcoded absolute-offset literal in this file): bump the one version
  occurrence 3→4 (line ~300). Fix `hasMaterialOffset` (line 360) from
  `kSceneArtifactHeaderSizeBytes + 64` to `+ 68`. Fix the light-field
  corruption tests' absolute offsets: `colorR` 108→112 (line 509),
  `intensity` 120→124 (line 524), `range` 124→128 (line 539). Fix the
  parent-slot test (line 546-562): comment and absolute offset
  `244`→`252` (node1 now starts at `24 + 116 = 140`; relative
  offset 112; `140 + 112 = 252`), title updated from "(offset
  104/108)" to "(offset 108/112)". Fix the schema-version-probe test
  (`bytes[4] = std::byte{0x04}`) to `0x05` (4 is about to become
  valid). New TEST_CASE, "decodeSceneArtifact rejects the superseded
  schema version 3 outright", mirrors the existing v1/v2 rejection
  precedent (note: unlike the source-text version, this codebase has
  no existing v1/v2-superseded-schema-version test to mirror exactly —
  this Plan checked and confirmed decode-side version rejection is
  covered only by the single "unknown schema version" test above;
  adding the explicit "version 3 now superseded" case is this
  Milestone's own new coverage, matching the source-side symmetry
  Spec 0031 does not otherwise require but which costs one small test).
  Rewrite the pinned-byte-vector test (line 385-449): `schema_version`
  byte `0x03`→`0x04`; insert 4 zero bytes (`0.0f` little-endian) for
  `exposure_compensation_ev` immediately after the `far_z` bytes,
  before `has_renderable`; `REQUIRE(expected.size() == 136)` →
  `140` (24-byte header + 116-byte node record). New
  finite/range-boundary decode tests for `exposureCompensationEv`,
  mirroring the existing light color/intensity/range independent
  re-validation tests (line 498-544).
- `tests/asset_system/validated_scene_data_tests.cpp` (1 occurrence):
  bump 3→4, mechanical only — confirmed via rg no other camera-specific
  assertion in this file needs a change.
- `tests/runtime/scene_load_tests.cpp` (3 occurrences): bump 3→4,
  mechanical.
- `tests/world/scene_instantiation_tests.cpp` (4 occurrences): bump
  3→4, mechanical; the existing camera-field assertion (line 175-177,
  `fovYRadians`/`nearZ`/`farZ`) gains one more line,
  `CHECK(cameraNode2.value().exposureCompensationEv == 0.0f);` —
  confirms the default flows end-to-end through `World` for a 14-token
  source line, at negligible cost. **New, non-zero-EV end-to-end
  `TEST_CASE`, in this same file** (fixed location — this file already
  owns the one shared helper capable of exercising the complete
  source→cook→artifact→decode→`World` chain in a single call): reuses
  the existing `cookAndDecodeScene(sourceText)` helper (line 74, which
  already calls the real `cookScene()` then the real `decodeScene()` —
  the sole path to a `ValidatedSceneData`) with a new scene-source
  constant identical to the existing `kThreeNodeSceneSource` except
  node 3's camera line carries the 15-token form,
  `... camera_far_z=100.0 camera_exposure_ev=1.0` (and the version line
  reads `4`, per this Milestone's own bump). After
  `fromValidatedSceneData()` and `world.getCamera(*world.activeCamera())`,
  asserts `exposureCompensationEv == 1.0f` — an exact, non-default,
  non-zero value that only survives if every one of parse, cook,
  encode, decode, and `World` construction correctly threads it; a
  missed assignment at *any* one of those layers (e.g. the exact
  `DecodedCamera{fovY, nearZ, farZ}`-stays-3-argument defect this
  Milestone's own `scene_artifact.cpp` fix above corrects) would
  silently fall back to `0.0f` and fail this exact assertion, not
  merely a "did it parse" check. The existing 14-token/default-`0.0f`
  coverage (the assertion added just above, and the pre-existing
  `cookAndDecodeScene(kThreeNodeSceneSource)`-based `TEST_CASE` it
  belongs to) stays exactly as it is — both cases live side by side in
  this one file.
- `tests/image_regression/lighting_demo_gpu_tests.cpp` (1 occurrence):
  bump 3→4, mechanical.
- `tests/runtime/material_realization_gpu_tests.cpp` (1 occurrence of
  the version line): bump 3→4, mechanical — this file is touched again,
  independently, in Milestone 2 for its 3 output-transform pipeline
  creations; the two edits are unrelated and both land here in M1/M2
  respectively without conflict.

### Milestone 2 — Renderer/output-transform atomic migration

Ships the shader ABI change, the `Pipeline` layout change, and the
`Renderer::drawFrame()` signature change together — the only way to
keep every intermediate state buildable, since a shader declaring a
push constant a `Pipeline` doesn't allocate room for (or vice versa) is
a real Vulkan Validation error, not merely a style issue.

**New file**, `src/renderer/src/exposure.h` (private — mirrors
`pbr_push_constants.h`'s own precedent exactly: no CMake registration
needed, `atlantis_renderer`'s existing source list only lists `.cpp`
files):

```cpp
#pragma once
#include <cmath>
#include <type_traits>

namespace atlantis::renderer {

// Renderer's own independent copy of the same values
// scene_types.h's kExposureCompensationEvMin/Max carry — deliberately
// duplicated, not shared, across the AssetSystem/Renderer module
// boundary (Spec 0031 Requirement 7).
inline constexpr float kExposureCompensationEvMin = -16.0f;
inline constexpr float kExposureCompensationEvMax = 16.0f;

// The sole EV->multiplier implementation in production code.
[[nodiscard]] inline float computeExposureMultiplier(float exposureCompensationEv) {
  return std::exp2(exposureCompensationEv);
}

struct alignas(4) ExposurePushConstants {
  float exposureMultiplier = 1.0f;
};
static_assert(std::is_standard_layout_v<ExposurePushConstants>);
static_assert(sizeof(ExposurePushConstants) == 4);

}  // namespace atlantis::renderer
```

**`src/renderer/include/atlantis/renderer/renderer.h`**: `drawFrame()`
gains `float outputTransformExposureCompensationEv` immediately after
`outputTransformSampler`.

**`src/renderer/src/renderer.cpp`**: `#include "exposure.h"` and
`#include <cmath>` added. At the top of `drawFrame()`'s body, before
`RenderGraphBuilder builder;`:

```cpp
ATLANTIS_CHECK_MSG(std::isfinite(outputTransformExposureCompensationEv) &&
                    outputTransformExposureCompensationEv >= kExposureCompensationEvMin &&
                    outputTransformExposureCompensationEv <= kExposureCompensationEvMax,
                    "outputTransformExposureCompensationEv must be finite and within "
                    "[kExposureCompensationEvMin, kExposureCompensationEvMax]");
const float exposureMultiplier = computeExposureMultiplier(outputTransformExposureCompensationEv);
```

(Using `_MSG` rather than the Spec's own bare-`ATLANTIS_CHECK`
pseudocode is a non-substantive Plan-stage refinement — it gives the
new precondition test in Milestone 3 a real message to assert against,
matching this file's own existing `skyPipeline`/`environmentLighting`
precondition's use of `_MSG`.) The `output_transform` pass's execute
lambda (line 172-180) captures `exposureMultiplier` by value and, right
after `cmd.bindPipeline(outputTransformPipeline);`, adds:

```cpp
const ExposurePushConstants payload{exposureMultiplier};
cmd.pushConstant(&payload, sizeof(payload));
```

**Both output-transform `.slang` files** (`shaders/output_transform_unorm/output_transform_unorm.slang`,
`shaders/output_transform_srgb/output_transform_srgb.slang`): remove
`static const float kBaselineExposure = 1.0;`; add, mirroring
`pbr_direct_lit.slang`'s own `[[vk::push_constant]]` declaration idiom:

```
struct ExposurePushConstants {
    float exposureMultiplier;
};
[[vk::push_constant]]
ConstantBuffer<ExposurePushConstants> exposurePushConstants;
```

`fragmentMain`'s `float3 exposedColor = linearColor * kBaselineExposure;`
becomes `float3 exposedColor = linearColor *
exposurePushConstants.exposureMultiplier;` — no `exp2()` call in either
file.

**`src/tools/shader_compiler/compile_and_validate.cpp`**:
`validatePushConstantsForVertexStage()`'s `expectedContract ==
"output-transform-unorm" || ... == "output-transform-srgb"` branch
(line 205-208, currently "expected stays empty") changes to
`expected = {PushConstantRange{.offsetBytes = 0, .sizeBytes = 4,
.stage = ShaderStage::Vertex}};` — per ADR-0075 Decision 8's own real
`slangc`-confirmed reflection result (recorded during Spec/ADR
drafting: a stray, unread, present-in-both-stages entry, no `used`
field on either). `validatePushConstantsForFragmentStage()` (line 232)
gains a second parameter, `std::uint32_t expectedSizeBytes`, replacing
its hardcoded `96` with the parameter (its one existing PBR call site,
line 382, passes `96` explicitly). The call site (line 379-383) gains
an `else if` branch:
`(request.expectedContract == "output-transform-unorm" ||
request.expectedContract == "output-transform-srgb")` →
`validatePushConstantsForFragmentStage(fragmentResult->metadata, 4)`.

**`tests/shader_system/output_transform_reflection_cross_check_tests.cpp`**:
line 52 (`REQUIRE(vertexContract.empty())`) is unrelated (descriptor
*bindings*, unchanged) and stays. Lines 61 and 68
(`CHECK(...pushConstantRanges.empty())`, both stages) become:

```cpp
const std::vector<PushConstantRange> expectedVertexPushConstants = {
    PushConstantRange{.offsetBytes = 0, .sizeBytes = 4, .stage = ShaderStage::Vertex}};
CHECK(vertexResult.value().pushConstantRanges == expectedVertexPushConstants);
...
const std::vector<PushConstantRange> expectedFragmentPushConstants = {
    PushConstantRange{.offsetBytes = 0, .sizeBytes = 4, .stage = ShaderStage::Fragment}};
CHECK(fragmentResult.value().pushConstantRanges == expectedFragmentPushConstants);
```

**Mechanical call-site migration — every real occurrence, confirmed by
`rg '\.drawFrame\('` and `rg 'hasCameraUniformBinding = false'`
restricted to `*.cpp`, no other pattern used, nothing added or dropped
by hand:**

*Every occurrence below is `outputTransformSampler`-then-new-argument,
matching the fixed parameter position Spec 0031/ADR-0075 already
approved.*

`drawFrame()` call sites needing `0.0f` inserted (31 occurrences across
19 files) — every file passes the literal `0.0f` **except** Runtime,
which passes the real, live value:

- `src/runtime/src/runtime_application.cpp:1396` — passes
  `cameraComponent.exposureCompensationEv` (the one non-mechanical call
  site; every other one below passes `0.0f`).
- `examples/minimal_renderer_demo/main.cpp:833`
- `examples/headless_rendering_demo/main.cpp:715`
- `tests/vulkan_backend/headless_rendering_gpu_tests.cpp:510`
- `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp:560`
- `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp:480`
- `tests/runtime/pbr_render_gpu_tests.cpp:459` (inside `renderOneFrame()`
  — see Milestone 3's own widening of this same helper)
- `tests/renderer/renderer_ownership_tests.cpp` — 11 call sites (lines
  217, 321, 327, 389, 449, 522, 540, 592, 663, 708, 788)
- `tests/image_regression/sky_background_gpu_tests.cpp:277`
- `tests/image_regression/shadow_gpu_tests.cpp:571,943` (2 sites)
- `tests/image_regression/fixture/world_scene_loaded_fixture.cpp:547`
- `tests/image_regression/fixture/world_scene_fixture.cpp:556`
- `tests/image_regression/fixture/material_demo_fixture.cpp:467`
- `tests/image_regression/fixture/textured_quad_fixture.cpp:482,570`
  (2 sites)
- `tests/image_regression/fixture/pbr_normal_map_demo_fixture.cpp:705`
- `tests/image_regression/fixture/lighting_demo_fixture.cpp:476`
- `tests/image_regression/fixture/minimal_cube_fixture.cpp:431`
- `tests/image_regression/fixture/pbr_material_demo_fixture.cpp:612`
- `tests/image_regression/fixture/integrated_showcase_demo_fixture.cpp:591`

Output-transform `Pipeline` creation sites needing `.pushConstantSizeBytes
= 4` added (20 occurrences across 18 files — confirmed via
`hasCameraUniformBinding = false`, the one field this codebase's real
`createPipeline()` contract already uses exclusively for the
output-transform `Pipeline`; `tests/vulkan_backend/shadow_map_render_gpu_tests.cpp`
was checked and confirmed **not** in this set — its own `hasDepthAttachment
= false` occurrence is a `textured_quad` color-debug `Pipeline`,
unrelated):

- `src/runtime/src/runtime_application.cpp` (1)
- `tests/runtime/pbr_render_gpu_tests.cpp` (1, inside `renderOneFrame()`)
- `tests/runtime/material_realization_gpu_tests.cpp` (3 — three
  separate `makeOutputTransformPipeline` lambda definitions, lines
  ~1002, ~1152, ~1338; these tests never call `drawFrame()` — they
  exist solely to count descriptor-set allocations — but still need
  this field so the created `Pipeline`'s layout matches the real,
  rebuilt shader's own compiled push-constant usage, avoiding a
  Validation Layers mismatch)
- `tests/image_regression/shadow_gpu_tests.cpp` (1)
- `tests/image_regression/fixture/world_scene_loaded_fixture.cpp` (1)
- `tests/image_regression/fixture/world_scene_fixture.cpp` (1)
- `tests/image_regression/fixture/textured_quad_fixture.cpp` (1)
- `tests/image_regression/fixture/pbr_material_demo_fixture.cpp` (1)
- `tests/image_regression/fixture/pbr_normal_map_demo_fixture.cpp` (1)
- `tests/image_regression/fixture/minimal_cube_fixture.cpp` (1)
- `tests/image_regression/fixture/lighting_demo_fixture.cpp` (1)
- `tests/image_regression/fixture/material_demo_fixture.cpp` (1)
- `tests/image_regression/fixture/integrated_showcase_demo_fixture.cpp` (1)
- `tests/vulkan_backend/minimal_renderer_gpu_tests.cpp` (1)
- `tests/vulkan_backend/descriptor_pool_growth_gpu_tests.cpp` (1)
- `tests/vulkan_backend/headless_rendering_gpu_tests.cpp` (1)
- `examples/minimal_renderer_demo/main.cpp` (1)
- `examples/headless_rendering_demo/main.cpp` (1)

Two files need only one of the two mechanical edits, confirmed by rg,
not by omission: `tests/renderer/renderer_ownership_tests.cpp` and
`tests/image_regression/sky_background_gpu_tests.cpp` call `drawFrame()`
but never call `createPipeline()` themselves (the former uses a
`FakePipeline` test double; the latter reuses `PbrMaterialDemoFixture`'s
already-built pipeline) — only the `drawFrame()` argument list changes
in those two.

**A pre-existing assertion this Milestone must fix, not defer to
Milestone 3 — the output-transform pass's own new `pushConstant()` call
changes a real, already-committed count this codebase already checks.**
`tests/renderer/renderer_ownership_tests.cpp`'s `TEST_CASE` at line 160
("records a full bind/draw sequence per DrawItem with distinct
push-constant data") is the *only* place in the repository asserting on
`FakeCommandList::pushConstants`/`pushConstantData` (confirmed via rg
across the whole tree — no other file references either member):

- Line 236, `REQUIRE(commandList.pushConstants.size() == 2);` → `3`
  (the two `DrawItem`s' own object-to-world push constants, plus the
  output-transform pass's own new one — which lands strictly after
  both, since "draw" must execute before "output_transform" in this
  file's own already-established RenderGraph dependency order, so
  indices `[0]`/`[1]` below are unaffected).
- Lines 225-226's own comment, *"pushConstant counts are unchanged --
  the output-transform pass calls neither [`bindUniformBuffer` nor
  `pushConstant`]"*, is now half wrong (`bindUniformBuffer` stays
  correct — output-transform still binds none — `pushConstant` does
  not) and is corrected to say so.
- Lines 244/246-249 (`pushConstantData[0]`/`[1]`, the two `DrawItem`s'
  own distinct `objectToWorld` bytes) are untouched — same indices,
  same values, same assertions.
- One new assertion added, proving the new payload's own exact shape
  and value at this call site (`0.0f` EV, per this Milestone's own
  mechanical migration, so `computeExposureMultiplier(0.0f) == 1.0f`):
  ```cpp
  REQUIRE(commandList.pushConstantData[2].size() == 4);
  const float outputTransformMultiplier =
      *reinterpret_cast<const float*>(commandList.pushConstantData[2].data());
  REQUIRE(outputTransformMultiplier == 1.0f);
  ```

This repository-wide scan (rg for `pushConstants.size()` /
`pushConstantData[` / `boundPipelines.size()` / `boundVertexBuffers.size()`
/ `boundIndexBuffers.size()` / `boundUniformBuffers.size()` /
`drawIndexedCounts.size()` / `.events` across every test file) found no
other assertion this Milestone's own changes break: the output-transform
pass already called `bindPipeline`/`bindVertexBuffer`/`bindIndexBuffer`/
`bindTexture`/`drawIndexed` once per frame *before* this Plan (only
`pushConstant` is new), so every other bound-resource-count assertion in
this file and elsewhere is unaffected; the `FakeCommandList::events`
combined log gains one new `PushConstant` entry, but every existing
`events`-based assertion in this file uses either equality between two
equally-affected recordings, a "never contains X" check unrelated to
`PushConstant`, or `std::find`-based relative-order checks that do not
depend on a fixed total count or index — all confirmed to still hold
after tracing the new entry's exact insertion point.

### Milestone 3 — New verification coverage and full matrix

**New file**, `tests/renderer/exposure_multiplier_tests.cpp` (added to
`tests/renderer/CMakeLists.txt`'s `add_executable(atlantis_renderer_tests
...)` source list — the only CMake edit this Plan makes):
`#include "../../src/renderer/src/exposure.h"` (the exact relative-path
precedent `tests/runtime/pbr_reflection_cross_check_tests.cpp` already
established for `pbr_push_constants.h`). Three `TEST_CASE`s call
`computeExposureMultiplier()` directly:

```cpp
CHECK(computeExposureMultiplier(-1.0f) == 0.5f);
CHECK(computeExposureMultiplier(0.0f) == 1.0f);
CHECK(computeExposureMultiplier(1.0f) == 2.0f);
```

plus boundary checks at exactly `kExposureCompensationEvMin`/`Max`
(finite, well-defined) and a `std::isfinite()` check that
`computeExposureMultiplier(NaN)` itself propagates NaN (proving the
function does not silently clamp — the clamping/rejection is
`drawFrame()`'s own job, Milestone 2).

**`tests/renderer/renderer_ownership_tests.cpp`**: new `TEST_CASE`s
(separate from the existing, Milestone-2-fixed push-constant-count
`TEST_CASE` at line 160) using the file's own already-established
`ScopedFailureHandler`/`FakeCommandList` pattern (the exact same one
the existing `skyPipeline`-without-`environmentLighting` precondition
test at line 537-544 already uses — mirrored, not invented):

- An out-of-range (`17.0f`) and a non-finite (`NAN`) EV each fire
  exactly one recorded failure via `ATLANTIS_CHECK_MSG`, matching the
  message this Plan's own Milestone 2 text fixes; `drawFrame()` still
  completes against the fakes afterward (this codebase's own
  `ATLANTIS_CHECK` never aborts when a test handler is installed —
  confirmed via `tests/core/assert_tests.cpp`'s own precedent).
- Exactly `kExposureCompensationEvMin`/`Max` (`-16.0f`/`16.0f`) and
  `0.0f` fire **no** failure (`failures.empty()`).
- For a valid EV (e.g. `1.0f`), `FakeCommandList::pushConstantData`
  (already recorded by the existing fake, confirmed real) decodes to
  exactly `computeExposureMultiplier(1.0f)` (`2.0f`) as the
  output-transform pass's own 4-byte push constant — the most direct
  possible proof the new C++ multiplier actually reaches the push
  constant, without needing a GPU.

**`tests/runtime/pbr_render_gpu_tests.cpp`**: `renderOneFrame()` (line
328) gains a fifth parameter, `float exposureCompensationEv = 0.0f`
(default preserves every existing call site's exact behavior — an
mechanical, zero-behavior-change widening, confirmed via rg that every
existing call in this file passes only 3-4 positional arguments today);
its own internal `drawFrame()` call forwards it in place of the
Milestone 2 mechanical `0.0f`. One new `TEST_CASE`, real-GPU:

- Reuses this file's own existing rig/geometry/material exactly
  (`computePbrDirectLighting(Vec3{0,0,0}, Vec3{0,0,1}, Vec3{0,0,5},
  baseColor, 0.0f, 0.5f, oneDirectionalLight(32.0f))`, the same
  `kLowerHdrIntensity = 32.0f` case the existing "Above-1.0 PBR
  radiance..." `TEST_CASE` already proved `> 1.0f`).
- Renders three frames varying only `exposureCompensationEv` (`-1.0f`,
  `0.0f`, `1.0f`, i.e. multiplier `0.5`/`1.0`/`2.0`), reads
  `readCenterPixel()` for each.
- Asserts the real captured ordering `capturedAt(-1) < capturedAt(0) <
  capturedAt(+1)` (elementwise, all three channels) — mirroring the
  existing roll-off test's own "compare two real captures, never an
  exact analytical value" methodology exactly (confirmed by reading
  that its own comparisons are `>`/`<`, never `==` against
  `tonemapAndEncodeUnorm()`).
- Asserts `capturedAt(+1) < 255` (not saturated) and
  `capturedAt(-1) > 0` (not black) — the ordering check is only
  meaningful if neither end is clipped.

  **What is, and is not, provable analytically here, stated precisely:**
  the *real-valued* (pre-quantization) exposure/Reinhard/sRGB chain is
  strictly increasing in the multiplier for any fixed, positive linear
  radiance (Reinhard's `x/(1+x)` and the sRGB OETF are both strictly
  increasing on their real domain, ADR-0068 D-5/D-6). 8-bit quantization
  (rounding that real curve to the nearest of 256 levels) is only
  guaranteed **non-decreasing**, not strictly increasing — two close
  enough real values could in principle round to the same byte. This
  Plan does not claim otherwise: the test's own strict (`<`, not `<=`)
  assertions for the three specific, real, captured bytes are confirmed
  by the real-GPU test itself running and observing genuinely distinct
  values (a full-octave multiplier step apart each time — `0.5×`,
  `1.0×`, `2.0×` — chosen precisely to make that distinctness likely,
  not merely hoped for), not asserted as a separate mathematical
  certainty.

  **What *is* provable analytically, and is what this Plan relies on
  without a new probe, is only the non-saturation bound**
  `capturedAt(+1) < 255`: `computePbrDirectLighting()`
  (`scene_extraction.cpp:434, 469-474`) has no ambient term and, for a
  single directional light, its own output is exactly linear in
  `intensity` (`radiance = color × intensity`; every other BRDF term is
  intensity-independent). So `linearRadiance(intensity=32) ×
  multiplier(+1 EV = 2.0) = linearRadiance(intensity=64) × 1.0`, and
  since the real-valued chain above is strictly increasing (never
  merely non-decreasing, on its own real domain, before rounding),
  `encoded(64, ×1) < encoded(128, ×1)` as real numbers — and `encoded(128,
  ×1)`'s own *quantized* value is **already** empirically proven `< 255`
  by this file's own existing, already-passing "Above-1.0 PBR
  radiance..." `TEST_CASE` (`CHECK(higherCenter[0] < 255)` at
  `kHigherHdrIntensity = 128.0f`). Because quantization is
  non-decreasing, a strictly-smaller real value quantizes to at most
  that same already-`<255` byte: `quantize(encoded(32, ×2.0)) =
  quantize(encoded(64, ×1.0)) ≤ quantize(encoded(128, ×1.0)) < 255`.
  This chain alone only proves `≤`, which is exactly the non-saturation
  headroom the ordering assertion needs to be meaningful — it does not,
  and is not used to, prove the strict ordering itself.

**No `slangc`/GPU probe was run or left uncommitted for this
Milestone** — the push-constant reflection contract was already fixed,
with real evidence, during Spec/ADR drafting (recorded in ADR-0075
Decision 8); this Milestone's own pixel/threshold derivation is
analytical, per above.

**Full verification matrix** (no new files beyond those already
listed):

- Debug build, Release build — both full.
- `ctest -C Debug -LE gpu` / `-C Release -LE gpu` — every existing
  GPU-independent test unmodified in behavior (all M1/M2 edits above
  are either purely additive tests or mechanical, default-preserving
  signature widenings), plus Milestone 3's own new GPU-independent
  tests.
- `ctest -C Debug -L gpu` / `-C Release -L gpu` — every existing `[gpu]`
  test, plus Milestone 3's own new real-GPU monotonicity test.
- Vulkan Validation Layers clean (`ctest -L gpu -V`, both configs,
  grepped for VUID/Validation Error/Warning) — covering the new/changed
  `VkPushConstantRange`, the widened `Pipeline` layouts, and
  `vkCmdPushConstants`.
- All 10 existing image-regression goldens confirmed byte-identical —
  every real `drawFrame()` call site now passes `exposureCompensationEv
  = 0.0f` except Runtime (which reads it from a `Camera` whose default
  is also `0.0f`, and none of the 8 real scene files' camera nodes set
  it explicitly), and `computeExposureMultiplier(0.0f) == 1.0f` exactly
  — confirmed by a real, executed capture-compare run against all 10
  goldens, not asserted from arithmetic alone (Spec 0031's own
  Non-functional Requirement).
- A separate `ATLANTIS_BUILD_TESTS=OFF` build: `atlantis_runtime.exe`
  builds and runs, all assets (including the 9 now-version-4 scene
  files) cook correctly, zero test executables produced.
- Module/link boundary re-check: `Atlantis::AssetSystem` still links
  `Atlantis::Core` only; `Atlantis::Renderer` still links no
  Platform/VulkanBackend/`Vulkan::Vulkan`; `Vk*` isolation unchanged
  (this Milestone touches no Vulkan Backend file).
- `/w14062` exhaustiveness: `atlantis_renderer`, `atlantis_asset_system`
  already carry this flag (confirmed via their own `CMakeLists.txt`);
  this Milestone adds no new `switch` statement.
- `git diff --check`.

## Files / Modules Touched (expected)

Exact, rg-verified lists are given inline under each Milestone above
(offset table for the artifact format; the full 19+18-file call-site/
pipeline-creation migration list for Milestone 2; the 9-file asset/
test-fixture version-line list and the 8 asset-system test files for
Milestone 1). Summary by module:

- **Atlantis::AssetSystem**: `scene_types.h`, `scene_source.cpp`,
  `cook_scene.cpp`, `scene_artifact.h`, `scene_artifact.cpp` (M1).
- **Atlantis::World**: `camera.h`, `scene_instantiation.cpp` (M1).
- **Atlantis::Renderer**: new `exposure.h`, `renderer.h`, `renderer.cpp`
  (M2).
- **Shaders**: `output_transform_unorm.slang`,
  `output_transform_srgb.slang` (M2).
- **Atlantis::Tools (shader_compiler)**: `compile_and_validate.cpp`
  (M2).
- **Assets**: 9 `.scene.txt` files, version line only (M1).
- **Tests**: 8 `tests/asset_system/*`-and-adjacent files (M1, listed
  above, including the new non-zero-EV end-to-end `TEST_CASE` in
  `tests/world/scene_instantiation_tests.cpp`);
  `tests/shader_system/output_transform_reflection_cross_check_tests.cpp`
  (M2); 19 `drawFrame()` call-site files + `tests/runtime/material_realization_gpu_tests.cpp`
  (M2, mechanical — one of the 19, `tests/renderer/renderer_ownership_tests.cpp`,
  additionally gets its pre-existing push-constant-count assertion
  fixed in this same Milestone, not deferred to M3); new
  `tests/renderer/exposure_multiplier_tests.cpp`,
  `tests/renderer/renderer_ownership_tests.cpp` (new precondition
  cases), `tests/runtime/pbr_render_gpu_tests.cpp` (new case + helper
  widening) (M3); `tests/renderer/CMakeLists.txt` (one line, M3).
- **`specs/README.md`**: Plan link updated to point at this file
  (this Plan's own governance commit, not a Milestone).

No parameter-object refactor of `drawFrame()`, no unrelated cleanup —
every file above is touched only for the reason stated in its own
Milestone.

## Sequencing & Dependencies

M1 → M2 → M3, strictly. M1 alone builds and passes every existing test
unmodified. M1+M2 together build and pass every existing test
unmodified (mechanically updated call sites/pipelines, unchanged
behavior at `0.0f`/default). M1+M2+M3 adds and passes the new coverage
and completes the full verification matrix. No milestone is
independently mergeable ahead of this order — M2 depends on M1's
`Camera` field for Runtime's own call site; M3 depends on M2's API/
shader/pipeline surface existing to test it.

## Verification Checklist

- [ ] Unit tests (GPU-independent): `tests/asset_system/*` (M1, new +
      fixed existing), `tests/renderer/exposure_multiplier_tests.cpp`
      and `renderer_ownership_tests.cpp` (M3, new).
- [ ] Headless integration / real-GPU tests: `tests/runtime/pbr_render_gpu_tests.cpp`'s
      new monotonicity `TEST_CASE` (M3); every existing `[gpu]` test
      unmodified in behavior (M2's mechanical migration).
- [ ] Image regression tests: all 10 existing goldens byte-identical,
      confirmed by a real capture-compare run; no new golden.
- [ ] Vulkan Validation Layers clean: Debug and Release, `ctest -L gpu
      -V`, grepped for VUID/Validation Error/Warning.
- [ ] Other: `ATLANTIS_BUILD_TESTS=OFF` build; module/link/`Vk*`
      boundary re-check; `/w14062`; `git diff --check`.

## Rollback Plan

Revert the Implementation PR's own commits on `feature/0031-manual-
camera-exposure-foundation` (or the merge commit on `main`, if already
merged) — every change is additive/mechanical with no data migration
beyond the version-line bump, and no golden is touched, so a revert
restores the exact prior behavior with no follow-up cleanup needed.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
No delta beyond: this repository has no CI pipeline yet, so "CI green"
is reported as not applicable, honestly, exactly as every prior PR in
this repository already does.
