# Plan: Height Fog

- **Spec:** [Spec 0043: Height Fog](../specs/0043-height-fog.md)
  (`Approved`, 2026-09-24; rulings Q1–Q7 binding) —
  [ADR-0091](../adr/0091-height-fog-insertion-point-uniform-layout-and-parameter-source.md)
  (`Accepted`)
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-09-24 — reviewed this Plan and
  [Spec 0043](../specs/0043-height-fog.md) together (chat confirmation;
  document set carried by this branch's PR) and explicitly authorized
  Implementation from Milestone 1. The five open points were ruled in
  the same review: Q1 the three goldens land in one commit (ADR-0042
  precedent); Q2 the P1 grammar confirmed; Q3 NonFiniteValue reused;
  Q4 the two count corrections recorded here and in the implementation
  PR, not by revising the approved Spec; Q5 the fog function is copied
  into all ten shaders (emissive/alpha-test precedent, no shared Slang
  module).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0043 in full — Spec 0036 workflow ⑤ — analytic exponential
height fog as a per-fragment term in the ten PBR shaders, in HDR space.
Parameters come from an optional fog group on the scene's camera node and
travel through scene source/artifact v5 and World's `Camera` into a
32-byte `FogData` tail on the camera uniform. ADR-0091's four decisions
and rulings Q1–Q7 are binding and are not reopened here.

## Pre-drafting reading (cited, not restated)

Spec 0043's five investigations were re-walked at Plan granularity against
`origin/main` at `718860f` (PR #181 merged). Every conclusion holds. Three
facts are sharper than the Spec stated, and one of them is a real
obstacle.

1. **The `FogData` tail and who writes the camera uniform.**
   - Today's regions end at `kCameraUniformBufferSizeBytes == 2512`
     (`scene_extraction.h:166-182`), after `CameraWorldPositionData`
     (`:138-150`).
   - `FogData` appends at 2512, in the layout below — `float3` then a
     `float` share one 16-byte slot under Slang's constant-buffer rules;
     M2's reflection cross-check confirms it.

     ```
     color(2512,12) density(2524,4) height(2528,4) heightFalloff(2532,4)
     maxOpacity(2536,4) _pad(2540,4)   -> block and buffer 2544
     ```
   - **The real writer count is 10, not the Spec's 16.** R5's figure
     counted every file naming `extractCameraWorldPosition()`, including
     three fixture headers, the extraction source and its unit test. The
     files that allocate a camera uniform sized by
     `kCameraUniformBufferSizeBytes` and bind it to PBR Pipelines are:
     - `runtime_application.cpp` (allocation `:791`, per-frame write
       `:1380-1382`);
     - seven fixture sources: `pbr_material`, `pbr_normal_map`,
       `pbr_clearcoat`, `pbr_sheen`, `pbr_anisotropic`,
       `pbr_materials_showcase`, `integrated_showcase`;
     - `shadow_gpu_tests.cpp` (`writeCamera`, `:505`);
     - `pbr_render_gpu_tests.cpp` (`writeCameraBuffer`, `:216`; six
       allocations).
   - **Both test writers fill only some regions, and `createBuffer()` does
     not zero memory.** An unwritten fog tail is therefore real garbage,
     not zeros. R5 is a hard requirement on all ten.
   - The other uniform buffers (`lighting_demo`, `material_demo`,
     `minimal_cube`, `textured_quad`, `world_scene*`, the vulkan_backend
     tests, the examples) are sized for non-PBR blocks and are untouched.
   - Only the reflection cross-check pins the size (through the constant,
     `pbr_reflection_cross_check_tests.cpp:280,405`), and its
     `CameraUniform` 11/11 test sits at `:323`.
2. **The scene path, and the obstacle.**
   - The camera DTO is `DecodedCamera {fov, near, far, exposure}`
     (`scene_types.h:26-31`). It is encoded at `scene_artifact.cpp:113`,
     decoded at `:200-210`, and checked at cook time at
     `cook_scene.cpp:186-194`. Exposure out of range reuses
     `NonFiniteValue` at both points.
   - It reaches World by positional aggregate init in
     `scene_instantiation.cpp:26-31`
     (`Camera{fov, near, far, exposure}`, `world/camera.h:5-10`).
   - The Runtime reads it at `runtime_application.cpp:1286-1289`.
   - **The obstacle: the node grammar tells a camera from a light by
     token count alone** (`scene_source.cpp:167-169`, `:237`, `:260`).
     Camera is 14 or 15 tokens, light 16 or 17. Any camera suffix of one
     or two tokens collides with the light shapes — P1.
   - **No scene metadata bump is needed**, contrary to R7's wording. The
     metadata stays at version 1: it records only `schema_version` and
     `node_count` (`scene_metadata.cpp:12-14`), so v5 changes a recorded
     value, not the format.
   - The artifact node record is 116 bytes (`scene_artifact.h:28-34`);
     v5 grows it — P4.
3. **The shader landing is uniform across all ten.**
   - Each of the ten interpolates `float3 worldPosition` and reads it as
     `input.worldPosition`.
   - Each ends in the one line
     `return float4(accumulated + pushConstants.emissiveFactor, alphaOut);`:

     | Shader | Return line | `lightSpaceProjection` (last block member) |
     |---|---|---|
     | pbr_direct_lit | 254 | 72 |
     | pbr_direct_lit_normal_map | 215 | 49 |
     | pbr_ibl | 245 | 48 |
     | pbr_ibl_normal_map | 245 | 45 |
     | pbr_clearcoat_ibl | 205 | 56 |
     | pbr_clearcoat_ibl_normal_map | 194 | 50 |
     | pbr_sheen_ibl | 195 | 68 |
     | pbr_sheen_ibl_normal_map | 182 | 49 |
     | pbr_anisotropic_ibl | 247 | 89 |
     | pbr_anisotropic_ibl_normal_map | 208 | 57 |

   - `FogData` is declared after `lightSpaceProjection`. The fog is
     applied between computing `accumulated + emissive` and that return.
   - No shader shares a Slang module with another (no
     `import`/`#include`), so the fog function is duplicated ten times —
     the Spec 0041/0042 precedent. P6 covers the compile shape.
4. **The reference precedent.**
   - `tests/image_regression/support/tone_mapping_reference.h` is
     header-only and inline, unit-tested in `tone_mapping_reference_tests.cpp`
     inside the GPU-independent `atlantis_image_regression_tests`
     target.
   - It is also used by the GPU tests for analytic pixel expectations
     (`emissive_demo_gpu_tests.cpp`).
   - `fog_reference.h` follows it exactly — P7.
5. **Fixtures for the goldens.**
   - **Visual (lit):** `PbrMaterialDemoFixture` (lit, no environment,
     `pbr_direct_lit`), by alias — the `cutout_demo`/`transparency_demo`
     precedent.
   - **Exact:** the dark emissive fixture (`PbrNormalMapDemoFixture`, the
     `emissive_demo` alias). With no light the unfogged output is exactly
     `emissiveFactor`, so a fogged sphere is exactly
     `E·(1 − f) + C·f` (the Plan 0041 dark-scene precedent).
   - **Both are needed** (P8). Neither alone covers all ten shaders, so
     the 10/10 differential reuses the existing IBL/clearcoat/sheen/
     anisotropic/normal-map fixtures (P9).

## Plan-stage decisions

**P1 — the fog group's grammar, and a pre-pass instead of a new count
shape.** The grammar is fixed by the Plan under ruling Q2:

```
... camera_fov_y=<f> camera_near_z=<f> camera_far_z=<f> [camera_exposure_ev=<f>]
    [fog=<density> <height> <height_falloff> <max_opacity> fog_color=<r> <g> <b>]
```

- The group is seven tokens, all-or-nothing, in the prefix-then-bare-values
  style of `position=` and `color=`.
- It may follow the camera fields with or without the exposure token, so
  a camera line has 14, 15, 21 or 22 tokens.
- **The group is recognised by its `fog=` prefix and removed before the
  count gate** (the Plan 0042 P1 pattern). So the count-dispatch machine,
  and every error it returns, is byte-for-byte v4 for any line without
  the group.
- Parse errors:
  - a `fog=` group on a non-camera node, or a truncated group, is
    `InvalidComponentGroup`;
  - a non-numeric value is `MalformedNumber`.
- Gate: every pre-existing `scene_source_tests` case passes with only its
  version line changed.
- The serializer writes the group only when `density != 0`.

**P2 — range checks reuse `NonFiniteValue`, at cook and decode.**
- The ranges are Spec R8.
- This follows `camera_exposure_ev=`'s own reuse (`cook_scene.cpp:186-194`,
  `scene_artifact.cpp:206-208`).
- The grammar only rejects non-numbers.

**P3 — the data types.**
- The Asset System's `DecodedCamera` gains a trailing
  `DecodedCameraFog fog`, and World's `Camera` a trailing
  `CameraFog fog`.
- The defaults are color (1, 1, 1), density 0, height 0, falloff 0,
  maxOpacity 1 — Filament's shape, with density 0 meaning off.
- Trailing, defaulted members keep all eight `Camera{...}` sites
  compiling unchanged: one positional, in `scene_instantiation.cpp:29`;
  seven default-constructed, in `camera_tests`/`renderable_tests`.
- `scene_instantiation.cpp` copies the fog through.

**P4 — scene artifact v5.**
- The node record grows from 116 to 144 bytes.
- 28 bytes are inserted immediately after `exposure_compensation_ev`
  (offset 52), before `has_renderable`:
  `fog_color(56,12) fog_density(68,4) fog_height(72,4)
  fog_height_falloff(76,4) fog_max_opacity(80,4)`.
- Every later field moves by +28. The fog is written as the defaults when
  the node has no camera.
- v4 is rejected; there is no dual-version reader.

**P5 — `FogData` and its extraction.**
- A C++ `FogData` (32 bytes, `alignas(16)`, the reading item 1 layout,
  explicit `_pad`) goes in `scene_extraction.h`, with a
  `static_assert` per offset.
- A new constant `kCameraUniformFogOffsetBytes == 2512`, and
  `kCameraUniformBufferSizeBytes` 2512 → 2544.
- A pure `extractFogData(const world::CameraFog&)` — the same narrow
  World-type exception this header already discloses for the lights.
- Writers:
  - the Runtime and every fixture with a World write
    `extractFogData(activeCamera.fog)` each frame;
  - the two World-less test writers write `FogData{}` (density 0).

**P6 — the shader form: a uniform branch, not multiply-by-zero.**

```
float3 color = accumulated + pushConstants.emissiveFactor;
if (camera.fog.density > 0.0) color = applyHeightFog(color, input.worldPosition);
return float4(color, alphaOut);
```

- The branch is dynamically uniform (one value per draw), so there is no
  divergence.
- Multiplying by a zero factor instead would still evaluate `exp`, and
  can produce `0 · ∞ = NaN` when `e^(−falloff·h)` overflows. That would
  break Spec R3's bit-exact identity.
- Inside `applyHeightFog`, `g(x)` uses the series `1 − x/2 + x²/6` for
  `|x| < 1e-3`, and `(1 − e^(−x))/x` otherwise.
- The mix is written `color·(1 − f) + fogColor·f`, literally the Spec R1
  form, so the reference can mirror it.

**P7 — `fog_reference.h`.** Header-only and inline, beside
`tone_mapping_reference.h`:
- `FogParams` holds the five fields;
- `fogOpticalDepth(params, camera, point)`, `fogFactor(...)` and
  `applyFog(lit, ...)` follow R1 with P6's series threshold;
- `fog_reference_tests.cpp` goes in `atlantis_image_regression_tests`.

GPU tests use it for analytic expectations. It is the executable form of
Spec R1 that the shaders are checked against.

**P8 — three goldens (ruling Q3), one fog-off discriminator each.**
- **`fog_distance_demo`** (lit, `PbrMaterialDemoFixture`): a row of
  identical opaque spheres receding along a floor at constant height,
  with warm fog. It shows density's effect over distance — the density
  sweep.
- **`fog_height_demo`** (lit, same fixture): a column of spheres at
  increasing height at one distance. It shows the falloff — the falloff
  sweep.
- **`fog_dark_demo`** (dark, emissive fixture alias): emissive spheres at
  several distances and heights, plus the black
  `pbr_normal_mapped_control` sphere, which becomes pure `C·f`. It is the
  exact analytic golden.
- The **analytic sweep** is on the dark scene: a grid of density ×
  falloff (at least 3 × 3), set on the World camera before the first
  render. Each sphere's front point at its centre pixel must equal
  `tonemapAndEncodeUnorm(applyFog(E, …))` within 1 LSB.

**P9 — ten-variant coverage and neutrality.**
- Existing fixtures render each variant with fog set on the World camera
  versus off: `pbr_material` and `pbr_normal_map` (direct-lit and IBL),
  and `pbr_clearcoat`, `pbr_sheen` and `pbr_anisotropic` with their
  normal-map twins. The frame must change on the material's surface and
  stay byte-identical on clear-colour and sky pixels (the Plan 0041 M3
  differential shape; the sky is unfogged, ruling Q4).
- **Neutrality:** a scene whose camera carries `fog=0 …` renders
  byte-identically to the same scene without the group.

## Milestones / Task Breakdown

**Two milestones, three commits.**

### Milestone 1 — scene v5, the `FogData` tail, the reference, the data path (`feat:`, R4–R8)

Zero rendering change: no shader reads the tail yet, and no committed
scene declares fog.

1. `scene_source.cpp`: P1's prefixes and pre-pass, and the serializer. The
   v4 → v5 version line changes in:
   - the 20 `assets/scenes` files;
   - the glTF importer's emitted scene
     (`src/tools/gltf_importer/scene_import.cpp`, version string only —
     glTF carries no fog);
   - 8 test sources: `cook_scene`, `decode_scene`, `scene_source`,
     `validated_scene_data`, `lighting_demo_gpu`,
     `material_realization_gpu`, `scene_load`, `scene_instantiation`.
2. `DecodedCameraFog` (P3), cook-time checks (P2), and artifact v5
   (P4: encode, decode and re-check). The scene artifact byte test moves
   to 144-byte records.
3. World `CameraFog` + `Camera::fog` (P3), and the instantiation copy.
4. `FogData`, its offsets, the buffer size and `extractFogData()` (P5).
   All ten writers write the tail every frame.
5. `fog_reference.h` + `fog_reference_tests.cpp` (P7).

**Risk gates.**
- **Zero rendering change:** full Debug suite green with every golden
  byte-identical.
- **Scene-parser error parity** (P1's gate): any pre-existing parser test
  needing more than its version line changed means the pre-pass changed
  behaviour — stop and report.
- **Writer completeness:** every one of the ten writers is changed. A
  grep of `kCameraUniformBufferSizeBytes` allocations against the
  `FogData` writes must match.

### Milestone 2 — the shader term, goldens, regression (`feat:` + `test:`, R1–R3)

1. All ten PBR shaders: a `FogData fog` member after
   `lightSpaceProjection`, `applyHeightFog`, and P6's branch before the
   return (reading item 3 table).
2. Extend the `CameraUniform` reflection cross-check (`:323`) with
   `FogData`'s offsets (2512/2524/2528/2532/2536) and the 2544 block size
   in the ten PBR shaders. `lit_textured` stays at 2224.
3. The three P8 scenes, their fixture aliases and golden generators; the
   analytic sweep; P9's 10/10 differentials and the neutrality test.
4. **The `test:` commit:** all three goldens, captured on the clean
   `feat:` tree (ADR-0042 Initial-baseline, four evidence items), each
   with its capture-compare case and a fog-off discriminator that must
   fail against it — see Q1.
5. Full regression: Windows Debug and Release; Validation Layers clean;
   Android `assembleDebug` (ASProxy first, then local Gradle 9.5.1
   `--offline`).

**Risk gates.**
- **Every pre-existing golden byte-identical** in both configurations.
  A moved golden means the density-0 branch is wrong — stop and report.
- **Reflection:** 10/10 agree on `FogData` offsets and block size, or stop.
- **The analytic sweep holds within 1 LSB** across the grid. A larger
  error means shader and reference disagree — stop and report rather
  than loosen the tolerance.
- **Clear and sky pixels byte-identical** with fog on (Q4, Q6).

## Files / Modules Touched (expected)

- **Asset System:**
  - `scene_types.h`;
  - `scene_source.{h,cpp}`;
  - `scene_artifact.{h,cpp}`;
  - `cook_scene.cpp`;
  - their tests (`scene_source`, `scene_artifact`, `cook_scene`,
    `load_scene`).
- **World:** `camera.h`, `scene_instantiation.cpp`, and their tests.
- **Runtime:** `scene_extraction.{h,cpp}`, `runtime_application.cpp`,
  `scene_extraction_tests.cpp`.
- **Importer:** `src/tools/gltf_importer/scene_import.cpp`, the scene
  version string only.
- **Shaders:** the ten `shaders/pbr_*/*.slang`.
- **Assets:**
  - 20 `.scene.txt` version lines;
  - three new scenes;
  - any new emissive materials the dark scene needs;
  - the `assets/CMakeLists.txt` registrations.
- **Tests:**
  - the ten writers (seven fixtures, `shadow_gpu_tests.cpp`,
    `pbr_render_gpu_tests.cpp`, plus the Runtime above);
  - `pbr_reflection_cross_check_tests.cpp`;
  - `fog_reference.h` and its tests;
  - the new GPU test files, fixture aliases, generators, goldens;
  - the 8 test sources carrying v4 scene strings (M1 item 1).

**Not touched** — needing any of these is a stop-and-report:
- RenderGraph, RHI, Vulkan Backend, Renderer's C++ (`drawFrame()`,
  `Material`, push constants), Platform;
- the material schema;
- `lit_textured`, `textured_quad`, `sky`, `shadow_cast`,
  `output_transform_*` shaders;
- any existing golden;
- the scene metadata format.

## Sequencing & Dependencies

M1 → M2. M1 is GPU-independent in effect and bisectable on its own. It
must land first because M2's shaders read a tail that every writer must
already fill: the reverse order would put garbage density in front of the
new branch. Within M2, the golden `test:` commit follows the `feat:`
commit on a clean tree.

## Verification Checklist

- [ ] R6–R8 (M1): scene source parse — group present/absent, malformed,
      on a non-camera node, with and without exposure; v4 rejected;
      serializer round trip; cook and decode range checks; artifact v5
      round trip at 144 bytes; World carriage.
- [ ] R4–R5 (M1): `FogData` offset `static_assert`s; `extractFogData()`
      tests; all ten writers write the tail.
- [ ] R1 (M1): `fog_reference_tests` — density 0 → 0 exactly;
      falloff 0 → `1 − e^(−density·d)`; horizontal ray; continuity of
      `g` across the series threshold; monotonicity; `maxOpacity` clamp.
- [ ] R4 (M2): 10/10 reflection cross-check on `FogData` and 2544.
- [ ] R1–R2 (M2): the dark-scene analytic sweep within 1 LSB; the 10/10
      differentials.
- [ ] R3 (M2): neutrality (`fog=0` ≡ no group, byte-identical); clear
      and sky pixels unchanged with fog on; every pre-existing golden
      byte-identical at M1 and M2, Debug and Release.
- [ ] Goldens (M2): three, each with a fog-off discriminator that fails.
- [ ] Validation Layers clean (Debug, fatal).
- [ ] Android `assembleDebug` green.

## Open points (for Joint Human Review)

- **Q1 — one `test:` commit for three goldens, or three.** Spec 0043's
  Testing says the goldens are "each in its own Initial-baseline commit".
  ADR-0042 requires only that goldens be committed separately from the
  rendering change they capture. Several goldens in one `test:` commit
  have precedent (`f20f9fe` sheen goldens, `3e2ca47` anisotropic goldens),
  and the brief asks for one. **Recommend one `test:` commit**, reading
  the Spec's "its own" as "separate from the `feat:`". The alternative is
  three `test:` commits, five in all.
- **Q2 — the P1 grammar.** A seven-token all-or-nothing `fog=` +
  `fog_color=` group, allowed with or without the exposure token.
  **Recommend as written.** The alternative is separate individually
  optional tokens, which multiplies the legal shapes.
- **Q3 — range errors reuse `NonFiniteValue` (P2).** Recommend it,
  mirroring exposure. The alternative is a new `FogOutOfRange`
  enumerator at cook and decode.
- **Q4 — two factual corrections to the Spec's counts, disclosed rather
  than edited:**
  - the camera uniform has 10 writers, not 16 (reading item 1);
  - scene v5 needs no metadata format bump (reading item 2).

  Neither changes a requirement. **Recommend recording them here and in
  the implementation PR** rather than a Spec revision.
- **Q5 — duplicating the fog function in ten shaders.** Recommend it, as
  the emissive and alpha-test terms were: a shared Slang module would be
  a new Shader System surface. Consistency is enforced by the 10/10
  differentials and the reference.

## Rollback Plan

- M2 reverts to a tree that parses, stores and uploads fog while every
  frame stays unfogged: the shaders never read the tail.
- M1 reverts the scene format to v4, World `Camera` to four fields, and
  the camera uniform to 2512 bytes. Scene artifacts are build outputs and
  recook. No committed scene carries a fog group until M2's three new
  scenes, which revert first.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas: the PR records the 10/10 reflection table, the analytic-sweep
worst-case LSB, the three goldens' four evidence items each, and the
10-writer list with its completeness check.
