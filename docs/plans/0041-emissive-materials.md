# Plan: Emissive Materials

- **Spec:** [Spec 0041: Emissive Materials](../specs/0041-emissive-materials.md)
  (`Approved`, 2026-09-22) —
  [ADR-0089](../adr/0089-emissive-material-parameter-range-composition-and-push-constant-placement.md)
  (`Accepted`)
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0041 in full — Spec 0036 workflow ③ — a factor-only,
linear-space, `[0, 65504]` `emissiveFactor` on the four PBR kinds, added to the
final colour of all ten PBR shaders, plus the importer mapping of Requirement 8.
Spec 0041's rulings O1–O5 (2026-09-22) are binding and are not reopened here;
each is cited where it lands.

## Pre-drafting reading (cited, not restated)

Spec 0041's five investigations were re-walked at Plan granularity against
`origin/main` at `eede7dc`. Every conclusion holds: nothing under `shaders/`,
`src/renderer`, `src/shader_system` or the shader compiler has changed since the
Spec's measurement at `d9a1152`. Four findings are sharper than the Spec
recorded, and one of them changes the milestone split this Plan's brief
suggested.

1. **Version bump blast radius.** Source 6 → 7: 51 `.material.txt` files (50 in
   `assets/materials/`, 1 in `assets/_test_fixtures/`) plus v6 strings in 6 C++
   files (`material_source.cpp` and 5 test files). Artifact 6 → 7:
   `material_artifact.h:60-61` and the literal `96` in
   `material_artifact_tests.cpp:110`. Metadata 5 → 6: `material_metadata.cpp:12`
   and 13 strings in `material_metadata_tests.cpp`. **One trap:**
   `material_source_tests.cpp:206-218` uses `7` as its *unknown* version, so a
   mechanical 6 → 7 replace would turn it into a wrong test. It moves to `8` by
   hand, exactly as Plan 0035 moved it from 6 to 7 (its own comment,
   `:207-209`). The parser, and how O1's optional line fits it, is P1 below.
2. **Push-constant layout — the size is pinned in three places, not one.** The
   Spec's measurement stands: Slang's block size equals MSVC's `sizeof` at
   112/112/112/128, and the declared range is the Slang block size
   (`slang_json_transform.cpp:262-274`). But the expected size is also written
   by hand in:
   - the C++ `static_assert`s (`pbr_push_constants.h:44`,
     `pbr_clearcoat_push_constants.h:50`,
     `pbr_anisotropic_push_constants.h:31`, `pbr_sheen_push_constants.h:34`);
   - **the build-time shader compiler**, which fails the build unless each
     contract's vertex-stage range is exactly 96 or 112
     (`src/tools/shader_compiler/compile_and_validate.cpp:251-266`);
   - **Runtime's `pushConstantSizeBytesFor()`**, which returns the literals
     96/96/112/96 that become the pipeline layout's range
     (`src/runtime/src/material_realization.cpp:248-292`) — Runtime cannot see
     the Renderer's private headers.

   Plus 10 test sites: `.pushConstantSizeBytes = 96` in
   `pbr_render_gpu_tests.cpp` (×6: `:520`, `:632`, `:729`, `:806`, `:874`, `:953`)
   and `shadow_gpu_tests.cpp` (×2: `:359`, `:863`), and the expected range 96 in
   `pbr_ibl_reflection_tests.cpp:28-30` and
   `pbr_reflection_cross_check_tests.cpp:146-161`. Any one left at the old value
   is a range violation, and Validation Layers make it fatal: a pipeline whose
   range no longer contains the shader's block, or a `vkCmdPushConstants` larger
   than its range. Stage flags need no change — they are already
   `VERTEX | FRAGMENT` (`vulkan_device.cpp:1130`), because fragment shaders
   already read `baseColorFactor`. **Consequence:** the C++ structs, the three
   pinned sizes and the ten shaders must change **in one commit** (M2), or the
   tree has a window with a Layers-fatal mismatch — the Plan 0040 M1 window
   lesson. The brief's two-milestone split would put the layout in the schema
   milestone, ahead of the shaders; see Q5.
3. **Shader landing point.** All ten end in
   `return float4(accumulated, alphaOut);` (the lines are cited in Spec
   Investigation 3). **Recommended form:
   `return float4(accumulated + pushConstants.emissiveFactor, alphaOut);`**,
   not `accumulated += …` before the return:
   - it is Spec Requirement 6's normative text;
   - it makes "after every lighting term" structural, because nothing can be
     added to `accumulated` after the return expression;
   - it leaves `accumulated` meaning "the lighting result", untouched by a
     non-lighting term;
   - it is the same one-line, greppable diff in all ten files.

   Each shader's `PushConstants` block also gains `float3 emissiveFactor;` as
   its last field. `pbr_direct_lit`'s own shader has no ambient term
   (`accumulated` is only ever increased inside the light loops), so with no
   lights and no environment the shader output is *exactly* `emissiveFactor` —
   the basis of the analytic check in M3.
4. **The Renderer → Runtime chain.** Four stages:
   - `renderer::Material`'s constructor and accessors
     (`material.h:88-90`, `:114`; `material.cpp:12`, `:25`, `:50`, `:59`);
   - Runtime's realization call, which passes each factor explicitly
     (`material_realization.cpp:444-481`);
   - the four payload arms in `renderer.cpp:185-246`, each of which builds its
     struct and pushes `sizeof(payload)`;
   - the pipeline range from `pushConstantSizeBytesFor()`.

   `sheenColor` travels exactly this path today and is the template.
   `runtime_application.cpp` is not on it.
5. **Importer (Requirement 8).** The code is in
   `src/tools/gltf_importer/material_import.cpp`, not `import_command.cpp`:
   the material loop (`:311-414`) builds a `ParsedMaterialSource` and
   serializes it (`:406`), and emissive is dropped at `:394-398` into the
   "no v6 destination, dropped (Ruling 3)" report line (`:400-404`).
   `gltf_material_tests.cpp:205` pins that exact wording, so its expectation
   changes deliberately. `gltf_test_builder.h` has no emissive fields yet and
   gains them. The census and end-to-end tests pin nothing about emissive.
6. **Fixtures for the golden and the on/off tests.** Every PBR fixture takes a
   `BootstrapConfig`. There is a direct precedent for reusing one under a new
   scene: `hdr_roll_off_demo_fixture.h` is a `using` alias of
   `PbrMaterialDemoFixture`, "differing only in its authored scene data".
   - **Dark-scene golden:** `PbrNormalMapDemoFixture` is the one config that
     carries **both** direct-lit pairs, plain and normal-map
     (`pbr_normal_map_demo_gpu_tests.cpp`), and its environment is optional
     (`pbr_normal_map_demo_fixture.cpp:246`, `:345` — an empty path means none).
     One new scene on it covers 2 of the 10 variants.
   - **On/off differential (O5):** every fixture realizes materials *inside* its
     render call (e.g. `pbr_clearcoat_demo_fixture.cpp:549`), and the tests
     already mutate `materialDataMap` before the first render
     (`lighting_demo_gpu_tests.cpp:421-424`). So each on/off test is two fixture
     instances over an **existing, already-goldened scene**, one with
     `emissiveFactor` set on one material before its first render — **no new
     scene or material assets.** The showcase fixture cannot do it alone: its
     config carries no clearcoat, sheen, anisotropic or direct-lit normal-map
     pairs. The per-kind demo fixtures, each with a base and a normal-map
     scene, do.

## Plan-stage decisions

**P1 — the parser becomes a cursor after its fixed prefix; the emissive line
is optional (O1).** Today's gate accepts only line counts 5/8/9/10/11
(`material_source.cpp:141-151`) and infers what each line is from the count
plus the kind. With an optional emissive line, counts become ambiguous (a
9-line `pbr_direct_lit` could carry `normal_map` or emissive). The parse
becomes:
- lines 0–4 fixed, as today;
- the 5-line defaults form ends there, unchanged;
- otherwise lines 5–7 are the factors, as today, and a cursor then takes:
  1. the kind-specific pair — required for Clearcoat, Sheen and Anisotropic,
     as today;
  2. an optional line recognised by the `emissive_factor: ` prefix;
  3. an optional `normal_map:` line;
- anything left over is `TrailingContent`.

The maximum is 12 lines. Emissive is legal only in the 8+-line form (it follows
the factors) and only on the four PBR kinds; elsewhere it is the new
`EmissiveNotSupportedForKind`. **Every error the parser returns today, for every
input it sees today, must be returned unchanged.** That is gated by the existing
29 `material_source_tests.cpp` cases passing after only their version-line edit.
So the answer to the brief's question is **"optional"**, not "every file carries
(0,0,0)": the 51 sources change only their version line, and the serializer
writes the line only when a component is non-zero — the `normal_map` symmetry
(`material_source.cpp:427-431`).

**P2 — the metadata line is mandatory.** The metadata format has a stated "no
optional field" discipline (`material_metadata.cpp:32`), and metadata is
machine-written only. So the v6 metadata always carries `emissive_factor:`. The
Spec's optionality applies to the hand-authored source only.

**P3 — explicit tail padding in the C++ structs.** Appending
`float emissiveFactor[3]` leaves 4 implicit tail bytes (to 112, or to 128 for
Sheen). Following the codebase's "explicit padding, never implicit" rule — the
`_pad` and `_padding0` precedents — each struct gains an explicit
`float _padEmissive` so `sizeof` is a provable sum. The shaders do not declare
it; the Slang block size reaches the same 112/128 by alignment, and the M2
cross-check pins the equality.

**P4 — test sites derive, not repeat.** The 8 GPU-test `pushConstantSizeBytes =
96` sites and the 3 reflection-test expectations become
`sizeof(PbrPushConstants)`, reaching the private header by the relative-path
include `pbr_reflection_cross_check_tests.cpp:28` already uses. The shader
compiler's and Runtime's copies stay as literals — neither may see a Renderer
private header — each with a comment naming the other two, and all three are
pinned by the M2 cross-check.

**P5 — one generator, one new scene.** The golden reuses
`PbrNormalMapDemoFixture` through a `using` alias, the `hdr_roll_off` precedent.
Its generator is a copy of `pbr_normal_map_demo_main.cpp` with the scene and
golden name swapped — the per-golden-generator precedent.

## Milestones / Task Breakdown

Three milestones, four commits. Every existing golden is byte-identical at every
milestone — a hard gate, not a checkpoint.

### Milestone 1 — schema v7, asset path, importer (`feat:`, Spec R1–R4, R8)

Asset-side only; nothing on the GPU consumes the field yet.

1. The field on `MaterialAssetData`, `ParsedMaterialSource`,
   `DecodedMaterialArtifact` and the material metadata struct (the `sheenColor`
   pattern in each).
2. The P1 parser, the serializer's conditional line, and
   `EmissiveNotSupportedForKind`.
3. The cook range check, `[0, 65504]` and finite (O2), with the new
   `MaterialCookError::EmissiveFactorOutOfRange`; `isValidFactor()` is
   deliberately not reused.
4. Artifact v7: 108 bytes, the field at offset 96, byte-by-byte little-endian;
   every other size rejected. Metadata v6 with its mandatory line (P2); the
   `load_material.cpp` agreement check gains the field (`:87-96` pattern).
5. The version bumps: 51 sources and 6 C++ files; the unknown-version test
   moves to 8 by hand; new tests that a v6 source, a 96-byte artifact and a v5
   metadata are each rejected.
6. The importer (R8, `material_import.cpp:394-404`):
   - a non-zero factor with no texture is mapped;
   - a non-zero factor with a texture is dropped, and reported as such;
   - a texture with a zero factor stays inert.

   The report wording says v7. `gltf_test_builder.h` gains emissive fields;
   `gltf_material_tests.cpp:205`'s expectation is updated; one new test covers
   the three cases (Spec verification row R8).

**Risk gates.**
- **Zero rendering change.** Nothing reads the field on the GPU, so the full
  Debug suite is green and every golden byte-identical. Any GPU-test or golden
  change means the bump reached something it should not have — stop and report.
- **Parser error parity.** Any existing `material_source_tests.cpp` case that
  needs more than its version-line edit to pass means P1 changed behaviour —
  stop and report.

### Milestone 2 — the GPU contract, atomically (`feat:`, Spec R5–R7)

One commit, because a split leaves a Layers-fatal window (reading item 2).

1. The four push-constant structs: `emissiveFactor` plus the P3 pad;
   `static_assert`s at offset 96/96/96/112 and size 112/112/112/128.
2. The shader compiler's expectations (`compile_and_validate.cpp:266`):
   96 → 112, 112 → 128.
3. `pushConstantSizeBytesFor()`: 96 → 112, 112 → 128.
4. The 11 test sites (P4).
5. The ten shaders: `float3 emissiveFactor;` last in `PushConstants`, and the
   return line from reading item 3. Nothing else.
6. `renderer::Material`'s defaulted parameter and accessor; the realization
   call passes the field; the four payload arms copy it.
7. **The R5 cross-check** — extend `pbr_reflection_cross_check_tests.cpp` with
   a 10-shader loop, the Plan 0040 11/11 mechanism: live `slangc` reflection of
   each shader's `pushConstants` block, asserting that `emissiveFactor`'s offset
   equals the C++ `offsetof` and the block size equals the C++ `sizeof`. This is
   the test that ties the three hand-kept copies together.

**Risk gates.**
- **Every golden byte-identical, Debug and Release.** With a zero factor the
  add is exact, so a moved golden means something else changed — for example
  compiler contraction or reordering. Stop and report; do not re-baseline.
- **Validation Layers clean** in Debug (fatal on warning/error). Watch pipeline
  creation (the range must contain the block) and `vkCmdPushConstants` at 128
  bytes on Sheen.
- **The cross-check is 10/10**, including a deliberate mutation confirmed to
  fail it, as in Plan 0040 M2.

### Milestone 3 — verification assets and the golden (`feat:` + `test:`, Spec verification plan)

1. **`emissive_demo` scene** on `PbrNormalMapDemoFixture` (P5): no lights, no
   environment. Its spheres:
   - three `pbr_direct_lit` spheres with distinct emissive colours, at least
     one component above 1, so HDR input reaches the tonemap;
   - one normal-mapped `pbr_direct_lit` sphere with emissive, the second
     direct-lit variant;
   - one control sphere with no emissive.

   The new materials are committed `.material.txt` files carrying
   `emissive_factor:` lines, so the golden also exercises parse → cook → load
   end to end.
2. **Golden-independent GPU tests on it:**
   - each emissive sphere's centre pixel equals
     `tonemapAndEncodeUnorm(exposure × emissive)`
     (`tone_mapping_reference.h:45`) within 1 LSB;
   - the control sphere is exactly black against the clear colour.

   Together these prove the term needs no light and adds nothing elsewhere.
3. **On/off differentials, the 8 IBL variants (O5)**, plus both direct-lit
   variants under lighting. The fixtures:
   - `ibl_material_demo` for `pbr_ibl`;
   - `pbr_normal_map_demo` for `pbr_ibl_normal_map` (environment on), and for
     the two direct-lit variants under lighting (environment off);
   - the clearcoat, sheen and anisotropic demo fixtures, each over its base
     scene and its normal-map scene.

   Each test: two fixtures over one existing scene, one material's
   `emissiveFactor` set before first render. Every pixel is either
   byte-identical, or darker in no channel and brighter in at least one; the changed set is non-empty
   and lies within the mutated object's screen bounds. This is Spec R6's
   light-independence and additivity evidence in lit scenes, within the limit
   the 8-bit tonemapped readback allows (Q4).
4. **The golden** in its own subsequent `test:` commit, against a clean
   committed tree, with ADR-0042's four evidence items, and a negative test: the
   frame with emissive zeroed fails against it.
5. **Full regression:**
   - Windows Debug and Release builds, `ctest` in both;
   - all goldens green;
   - Validation Layers clean;
   - Android `assembleDebug`: check ASProxy first, then local Gradle 9.5.1
     `--offline` (the Plan 0037/0039/0040 precedent).

**Risk gates.**
- **The golden must be readable.** It must visibly show three coloured discs,
  a normal-mapped disc and a black control disc. If it cannot be read that way,
  that is evidence item (a) failing — stop and report.
- **An on/off test that shows any darker pixel,** or change outside the
  object's bounds, means emissive leaked into another term — stop and report.

## Files / Modules Touched (expected)

- **Asset System:**
  - `material_types.h`, `material_source.{h,cpp}`, `cook_material.cpp`;
  - `material_artifact.{h,cpp}`, `material_metadata.{h,cpp}`,
    `load_material.cpp`, `errors.h`;
  - their 5 test files (`material_source_tests`, `cook_material_tests`,
    `material_artifact_tests`, `material_metadata_tests`, `load_material_tests`).
- **Assets:** 51 `.material.txt` files (version line only); M3's new scene and
  materials plus their `assets/CMakeLists.txt` registrations.
- **Importer:** `material_import.cpp`; tests `gltf_material_tests.cpp`,
  `gltf_test_builder.h`.
- **Renderer:** the four `pbr*_push_constants.h`, `material.{h,cpp}`,
  `renderer.cpp`.
- **Runtime:** `material_realization.cpp` only — the realization argument and
  `pushConstantSizeBytesFor()`.
- **Shader compiler:** `compile_and_validate.cpp` (the expected sizes).
- **Shaders:** the ten `shaders/pbr_*/*.slang` files.
- **Tests:**
  - `pbr_reflection_cross_check_tests.cpp`, `pbr_ibl_reflection_tests.cpp`;
  - `pbr_render_gpu_tests.cpp`, `shadow_gpu_tests.cpp`;
  - `runtime/scene_load_tests.cpp`, `scene_manifest_tests.cpp`,
    `material_realization_gpu_tests.cpp` (version strings);
  - M3's new GPU test file, its golden generator and the image-regression
    CMake wiring.

**Not touched** — if implementation needs any of these, stop and report:
- the RHI public API, the Vulkan backend, RenderGraph, Platform, World;
- `runtime_application.cpp`;
- `lit_textured`, `textured_quad`, `sky`, `shadow_cast` and the
  output-transform shaders;
- `.amesh`, `.scene.txt`, texture and environment formats;
- any existing golden;
- any fixture's rendering code (M3 reuses fixtures by alias and by
  `materialDataMap` mutation only).

## Sequencing & Dependencies

M1 → M2 → M3, each gated as above.
- M1 is independent of the GPU and can be bisected on its own.
- M2 depends on M1's `MaterialAssetData` field, and is one commit by necessity.
- M3 depends on both, and its golden commit depends on M3's `feat:` commit
  being on a clean tree.

## Verification Checklist

- [ ] R1–R4 (M1): parse present/absent/malformed/wrong-kind; every existing
      shape still accepted with its current errors; cook `NaN`, `inf`, negative
      and 65,504.5 rejected, 0/1/100/65,504 accepted; artifact v7 round-trip at
      108 bytes and v6 rejected; metadata round-trip and the agreement check;
      serializer round-trip.
- [ ] R5 (M2): the 10/10 live-reflection push-constant cross-check; the shader
      compiler builds all ten; `static_assert`s at 112/112/112/128.
- [ ] R6/R7 (M3): dark-scene analytic check; control sphere black; on/off
      differentials for all ten variants; the dark-scene golden with four
      evidence items plus its negative test.
- [ ] R8 (M1): the three-case importer test; the updated report wording.
- [ ] Every existing golden byte-identical at M1, M2 and M3 — Debug and Release.
- [ ] Vulkan Validation Layers clean (Debug, fatal).
- [ ] Android `assembleDebug` green.

## Open points (for Joint Human Review)

- **Q1 — the dark-scene fixture.** `PbrNormalMapDemoFixture` (covers both
  direct-lit variants) versus `PbrMaterialDemoFixture` (the exact `hdr_roll_off`
  precedent, plain variant only). **Recommend the former**; the latter would
  leave `pbr_direct_lit_normal_map` covered only by its lit on/off test.
- **Q2 — the P3 explicit pad.** Recommend yes. The alternative is implicit
  tail padding with a comment.
- **Q3 — out-of-range glTF emissive at import.** An importer that passes a
  negative or >65,504 factor through fails later at cook. **Recommend the
  importer validate and drop-and-report**, like its other unmappable content,
  so one bad material does not fail the whole cook. Bistro is unaffected (max
  100).
- **Q4 — how strong can light independence be?** The readback is 8-bit and
  tonemapped, so exact HDR additivity is not observable. Recommend the
  combination above: the analytic dark-scene value (no light needed), the
  structural placement at the return, and lit on/off differentials that are
  monotone and confined to the object. The alternative is a new HDR-target
  readback path, a fixture change this Plan otherwise avoids.
- **Q5 — three milestones, not the brief's two.** The brief put the
  push-constant layout into the schema milestone. Reading item 2 shows that
  would leave a commit where the C++ pushes 112 bytes into a 96-byte range —
  fatal under Validation Layers — until the shaders land. **Recommend the
  layout moves with the shaders**, which is what makes a third milestone: the
  asset path (M1) and the GPU contract (M2) each stay green on their own.

## Rollback Plan

- M3 reverts cleanly, taking its scene, materials, tests and golden with it.
- M2 reverts to a tree where emissive is stored but not rendered.
- M1's revert restores v6 in all three formats. Cooked artifacts are
  build-time outputs and recook, and no committed asset holds a v7-only field
  except M3's, which revert first.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas: the R5 push-constant cross-check result (10/10) and the four golden evidence items
are recorded in the PR.
