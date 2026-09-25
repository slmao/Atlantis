# Plan: Bistro Finale — Scene Assembly, Whitelist and Dual-Platform Verification

- **Spec:** [Spec 0046: Bistro Finale](../specs/0046-bistro-finale.md) (`Approved`,
  2026-09-25; rulings Q1–Q7 binding) —
  [ADR-0094](../adr/0094-imported-scene-assembly-build-step-and-authored-overlay.md),
  [ADR-0095](../adr/0095-goldens-over-pinned-fetched-content.md),
  [ADR-0096](../adr/0096-emissive-texture-always-bound-slot.md) (all `Accepted`)
- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** pending

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0046 in full — the Bistro roadmap's capstone: the emissive
texture (ADR-0096), transmission as blend, the importer's overlay and
light cap, the one-step Bistro assembly (ADR-0094), the `bistro`
whitelist entry, the human look review, Android verification, and the
content-gated golden (ADR-0095). The three ADRs and rulings Q1–Q7 are
binding and are not reopened here.

## Pre-drafting reading (cited, not restated)

Re-walked at `origin/main` `2d5a448` (PR #190 merged). Two findings the
Spec did not name: the Runtime's environment is global (reading 3e), and
the transmission change is a new branch, not the one-line fix the brief
assumed (reading 2).

1. **Emissive texture (ADR-0096).**
   - **a) Next free sampler binding per variant**
     (`descriptor_contract.cpp:45-160`; bindings are contiguous from 1):

     | Variant | Samplers today | Emissive at |
     |---|---|---|
     | `pbr_direct_lit` | 1 base, 2 shadow | **3** |
     | `pbr_direct_lit_normal_map` | 1 base, 2 shadow, 3 normal | **4** |
     | `pbr_ibl` | 1 base, 2 env, 3 DFG, 4 shadow | **5** |
     | `pbr_ibl_normal_map` | 1–4 as `pbr_ibl`, 5 normal | **6** |
     | `pbr_{clearcoat,sheen,anisotropic}_ibl` | 1 base, 2 env, 3 DFG | **4** |
     | `…_normal_map` (×3) | + 4 normal | **5** |

     The Renderer binds by the same conditional-index pattern it uses for
     the shadow map and normal map (`renderer.cpp:194-222`);
     `sampledTextureBindingCountFor()` (`material_realization.cpp:731-775`)
     gains one in every PBR arm. Reflection cross-checks
     (`pbr_reflection_cross_check_tests.cpp`) and the ten descriptor
     contracts gain the slot.
   - **b) Schema.** Material source v8 → v9: one optional `emissive_texture:
     <logical path>` entry appended to the ordered optional-line table
     (`material_source.cpp:184-188`, PBR kinds only — the table already
     rejects optional lines on `UnlitTextured`/`LitTextured`, `:249-252`).
     Artifact v8 → v9, **116 → 124 bytes**: `emissive_texture_asset_id`
     (u64, 0 = none) appended at **offset 116**, the
     `normal_map_texture_asset_id` precedent (`material_artifact.h:10-60`).
     Metadata v7 → v8 gains the same id (it mirrors `normalMapTexture`,
     `material_metadata.h:44`). No dual-version reader.
   - **c) Default texture: Runtime-owned** (recommended). One 1×1
     `Rgba8Unorm` (255,255,255,255) `SampledTexture`, created once in
     `initializeSteps()` beside the shadow map, passed by reference into
     `realizePendingMaterials()`; fixtures that realize PBR materials create
     their own the same way. A Device-level default would be an RHI public
     API addition; ADR-0096 keeps the RHI unchanged.
   - **d) Backend limits:** `kMaxTextureBindingSlots` 6 → 7
     (`vulkan_command_list.h:207`) and the pool's sampler budget 5 → 6 per
     set (`vulkan_device.cpp:463-470`).
   - **e) Importer:** the emissive block (`material_import.cpp:388-417`)
     maps factor + texture for the 11 (report line "mapped with texture"),
     adds the texture to the manifest (`addTexture`, `TextureUsage::Color`),
     and drops `emissiveTexture` from the Ruling-3 dropped list (`:451`).
   - **f)** Push constants unchanged: the factor is already in them
     (ADR-0089); the multiply is in the shader.
2. **Transmission (Q3).** Bistro's glass is glTF `alphaMode: OPAQUE`, so
   the MASK/BLEND branch (`material_import.cpp:418-445`) never runs for it;
   its transmission guard only covers MASK/BLEND + transmission. The change
   is in the transmission block itself (`:355-361`): set `alphaMode = Blend`
   and multiply `baseColorFactor[3]` by `1 − transmissionFactor`
   (a `transmissionTexture` stays dropped and reported). The later guard at
   `:430-432` becomes unreachable for Blend and is removed with it.
   Culling is already `VK_CULL_MODE_NONE` (`vulkan_device.cpp:1202`).
3. **Assembly (ADR-0094).**
   - **a) Where it lives.** The host-tool block (`CMakeLists.txt:96-122`)
     defines the cooker and importer, then `add_subdirectory(assets)`;
     `src/runtime` comes after (`:229`), so variables exported from
     `assets/` reach it. The function lives beside
     `atlantis_add_scene_asset()` (`src/asset_system/CMakeLists.txt:137`);
     the Bistro call sits in `assets/CMakeLists.txt`, gated on
     `EXISTS ${ATLANTIS_BISTRO_CONTENT_DIR}/bistro.gltf` (the cache variable
     the `[bistro]` tests already use, `tests/image_regression/CMakeLists.txt:527`).
     Bistro's 1062 assets get their own `--validate-set` inside the step;
     the global `atlantis_finalize_asset_validation()` is not fed them.
   - **b) Overlay** — authored content, table below.
   - **c) `--overlay`** — importer CLI (`main.cpp:25-33`) and
     `importGltf()` gain it; the merge per ADR-0094 Decision 3 happens where
     the scene is serialized (`scene_import.cpp:100-104`, `:281-293`).
   - **d) Light cap:** `scene_import.cpp:156` (1 + 4) → the grammar's
     `kMaxPointLightsPerScene` (`scene_types.h:83`).
   - **e) Whitelist and environment.** `main.cpp:48-63` is a fixed
     `std::array<…, 4>` passed as a span — a conditional fifth entry is
     local. **Not in the Spec:** the environment is global
     (`main.cpp:125-126` sets `ibl_studio` for every scene), so Bistro would
     render with a studio IBL, IBL shader variants and a studio sky. O1.
4. **Content-gated golden (ADR-0095).**
   - The existing content binary `atlantis_image_regression_content_gpu_tests`
     (`tests/image_regression/CMakeLists.txt:518-560`) already carries the
     `content` label and links the fixture library; the golden lives there,
     compiled against the cooked Bistro paths when they exist and SKIPping
     otherwise.
   - Sidecar: the parser already accepts additive schema variants — v2
     appends environment-capture fields by line count
     (`provenance.cpp:119-127`, `:155-170`). The content pin is a **schema 3**
     variant: the v1 fields plus `content_source_commit`,
     `content_fetch_script_sha256`, `golden_update_reason`. The 29 committed
     sidecars are untouched.
   - Fixture: `PbrNormalMapDemoFixture` realizes through
     `realizePendingMaterials()` (blend/cutout pipelines included), with
     lights, fog and bloom; optional environment; 512×512.
5. **Android.** Assets are APK-listed and copied out
   (`build.gradle:30-54`), and the manifest's paths are made
   APK-relative by Gradle and rewritten by `extractSceneManifest()`
   (`asset_extraction.cpp:93-140`). Bistro is pushed instead: the Plan
   0034/0035 M6 temporary switch reads a host-rewritten manifest from the
   app's external files directory. BC7 probe: `adb shell cmd gpu vkjson`
   (the device's VkJson, including format properties) — no code.
6. **Performance and memory** (Spec 0046 Scale table): 2909 draws × 2
   (shadow pass) ≈ 5818 per frame; textures 1,486 MiB with chains; the
   Runtime keeps CPU copies for its lifetime (`textureDataMap_`,
   `runtime_application.h:245`, `.cpp:1092`) and stages every new texture in
   one realization frame (`:1630-1845`) — peak ≈ 3 × 1.45 GiB.

## Plan-stage decisions

**P1 — Material v9 / metadata v8** (reading 1b).

**P2 — Material, Renderer, shaders.** `Material` gains a borrowed
`emissiveTexture` (non-null for every PBR material: the real one or the
default), a trailing constructor/`createMaterial()` argument (the
normal-map precedent). The Renderer binds it at reading 1a's index after the
normal map. The ten shaders sample it and multiply `emissiveFactor` by its
RGB where they add emissive today (ADR-0089's composition point).

**P3 — Default texture** (reading 1c). Runtime-owned; each PBR-realizing
fixture creates one. Material realization binds it when the material names
no emissive texture. Spec 0045's derived `maxLod` includes the emissive
texture; the 1×1 default contributes 0.

**P4 — Backend limits** (reading 1d).

**P5 — Importer emissive mapping** (reading 1e). The 11 materials; the two
texture-with-zero-factor materials stay inert.

**P6 — Transmission** (reading 2).

**P7 — Importer light cap and `--overlay`** (readings 3c/3d). Merge rule
per ADR-0094 Decision 3; errors: overlay with a renderable node, a parent
into the imported set, a second camera, or the merged light count past the
cap — each a named `GltfImportError`.

**P8 — Cook-manifest mode** (ADR-0094 Decision 2).
`atlantis_asset_cooker --kind=cook-manifest --import-dir=… --cooked-dir=…
--content-parent=… --manifest-out=…`: validates the import's asset list,
runs each manifest line through the same in-process entry points the
per-kind modes use, and writes the dependency manifest (meshes →
import directory; textures, materials, scene → cooked directory).

**P9 — CMake function** (reading 3a): `atlantis_add_imported_scene(NAME
bistro GLTF … CONTENT_ROOT … OVERLAY assets/bistro/bistro_overlay.scene.txt)`,
one custom command, one stamp, depends on the glTF, the overlay and the two
tool targets; exports `ATLANTIS_bistro_scene_{ARTIFACT,METADATA,MANIFEST}_PATH`
and `_TARGET`, as `atlantis_add_scene_asset()` does.

**P10 — Whitelist and environment** (reading 3e, Q7, O1). A fifth entry
compiled in only when `ATLANTIS_RUNTIME_BISTRO_SCENE_*` are defined;
`SceneWhitelistEntry` (runtime-private `cli.h`) gains a per-entry "no
environment" flag, set for `bistro` — the night street renders without IBL
or sky, lit by its own lights (recommended, O1).

**P11 — The overlay** (authored; `assets/bistro/bistro_overlay.scene.txt`).
Positions are world-space bounds centres of the emitters, measured from
`bistro.gltf` (glTF and Atlantis share conventions, `scene_transform.h:3-6`).
Point lights use the existing grammar: `light=point color=<[0,1]³>
intensity=<I> range=<R>`, linear falloff to zero at `range`
(`pbr_direct_lit.slang:258-260`). Initial values — every one tunable in M4:

| # | Light | Position (x, y, z) | Colour | Intensity | Range |
|---|---|---|---|---|---|
| camera | `camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=200.0 camera_exposure_ev=-1.0` + `fog=0.015 0.0 0.15 0.7 fog_color=0.25 0.20 0.14` + `bloom=0.15 1.5` | (8.0, 1.7, 24.0), rotation (0.05, 0.785, 0) | — | — | — |
| moon | directional | rotation (−1.0, 2.4, 0) | (0.6, 0.7, 1.0) | 0.1 | — |
| 1 | streetlight | (−39.61, 3.26, −5.03) | (1.0, 0.8, 0.55) | 8 | 12 |
| 2 | streetlight | (−37.82, 3.26, −9.27) | (1.0, 0.8, 0.55) | 8 | 12 |
| 3 | streetlight | (−35.74, 4.00, 0.03) | (1.0, 0.8, 0.55) | 8 | 12 |
| 4 | streetlight | (−31.77, 4.35, −6.17) | (1.0, 0.8, 0.55) | 8 | 12 |
| 5 | streetlight | (−31.22, 4.00, 4.77) | (1.0, 0.8, 0.55) | 8 | 12 |
| 6 | streetlight | (−28.73, 4.00, −2.87) | (1.0, 0.8, 0.55) | 8 | 12 |
| 7 | wall bulb | (−11.92, 2.54, 9.22) | (1.0, 0.85, 0.6) | 3 | 5 |
| 8 | wall bulb | (−10.56, 2.54, 12.61) | (1.0, 0.85, 0.6) | 3 | 5 |
| 9 | ceiling lamp | (−6.72, 3.65, 7.12) | (1.0, 0.9, 0.7) | 4 | 6 |
| 10 | ceiling lamp | (−5.97, 3.65, 11.55) | (1.0, 0.9, 0.7) | 4 | 6 |

The remaining ≈ 49 follow the same pattern from the same measurement:
the other 22 streetlights (x −39…38, z −30…46); the other wall bulbs
(de-duplicated to 6, x −12…−5, z 4…15) and 2 ceiling lamps; 6 lanterns
(x −12…−3, z 5…16, colour (1.0, 0.85, 0.5)); the string lights grouped to
**12** — two per colour along each string (x −22…−5, z −3…25, their own
emissive hue normalised to [0,1], intensity 2, range 4); 3 singles (the
street bulb at (−8, 3.4, 9.1), the pharmacy cross at (0, 4.2, −21.7), one
over the bakery signs). Total **≈ 59 point + 1 directional**. Camera and
moon orientation are first guesses aimed at the façade the reference image
frames; the M4 review sets them.

**P12 — Golden** (reading 4): `bistro_demo_512x512_rgba8unorm`, schema-3
sidecar; discriminators — fog off, bloom off, all point lights off (each a
World edit on the loaded scene) must fail. Captured only after M4's human
review (Spec 0046 Risks).

**P13 — Memory mitigations, gated** (reading 6). Measure the process peak
at M3's gate. If it fails on the reference machine or the emulator: (i)
release each texture's CPU `pixelBytes` once its upload frame has waited
idle (the `SampledTexture` is the only consumer after that); (ii) if still
too high, bound the bytes staged per realization frame, leaving the rest
pending to the next frame (the existing retry path). Neither changes a
format; each is its own reviewed step if needed.

## Milestones / Task Breakdown

**Five milestones.** Commits: M1 `feat:`; M2 `feat:`; M3 `feat:`; M4
`feat:` (overlay tuning, if any); M5 `test:` (golden). Order note: the
golden is last, after the look review, not beside the Android smoke as the
brief sketched — capturing before tuning would force a re-capture.

### Milestone 1 — the emissive texture (ADR-0096; P1–P5)

1. Material source/artifact v9, metadata v8, cook and load; GPU-independent
   tests (round-trip, offset 116, PBR-only rejection, v8 rejected).
2. `Material` + Renderer binding; ten shaders; descriptor contracts;
   reflection cross-checks; `sampledTextureBindingCountFor()`.
3. Backend limits; default texture in the Runtime and the PBR fixtures;
   material realization binds real-or-default.
4. Importer mapping of the 11; report lines; importer tests.

*Gate:* full Debug + Release; **every existing golden byte-identical**
(default white × factor); Validation Layers clean; Android `assembleDebug`.
*Risk gate:* a golden moving means the default is not exactly 1.0 or a
binding shifted — stop and report.

### Milestone 2 — importer and cooker tooling (P6–P8)

1. Transmission as blend (P6); importer tests.
2. Light cap and `--overlay` (P7); merge tests (renumbering, active camera,
   each rejection).
3. Cook-manifest mode (P8); tests on a synthetic import (a tiny glTF from
   `gltf_test_builder.h`), then the `[bistro]` end-to-end test switches to
   it for the full set.

*Gate:* Debug + Release green; the `[bistro]` test cooks all 1062 assets
through the new mode; build time of the full cook measured and reported.

### Milestone 3 — assembly (P9–P11)

1. `atlantis_add_imported_scene()`; the Bistro call, content-gated.
2. The overlay file (P11's initial values).
3. The fifth whitelist entry, no environment (P10).
4. A content-gated GPU test: the assembled scene loads through the Runtime
   path, every renderable resolves, one frame renders, Validation Layers
   clean; `atlantis_runtime --scene bistro` runs windowed.

*Gate:* the above, plus measured build time, frame time and process peak
memory. *Risk gate:* P13 if the peak fails.

### Milestone 4 — look review and Android (Spec 0046 Testing)

1. Look loop: full-window captures beside `example_bistro1.jpg` /
   `example_bistro2.jpg`; tune camera, exposure, fog, bloom and light
   intensities/ranges in the overlay only; each round recorded (values +
   capture) for the PR. Ends on a human "神似" sign-off.
2. Android: (1) BC7 probe via `cmd gpu vkjson`; (2) if supported: push the
   cooked set and a host-rewritten manifest, temporary
   `android_main.cpp` switch, screencap + logcat, revert, `git diff` clean;
   (3) if not: record the failure mode — the gap stays Spec 0038's.

*Gate:* human sign-off; Android outcome recorded either way.

### Milestone 5 — golden and regression (P12)

1. Generator for `bistro_demo` (content-gated), schema-3 sidecar support.
2. Capture on the clean tree; golden + discriminators (`test:`).
3. Full regression: Debug + Release, all goldens, Validation Layers,
   Android `assembleDebug`, the `[bistro]`/`content` tests.

## Files / Modules Touched (expected)

- **Asset System:** `material_source`, `material_artifact`,
  `material_metadata`, `material_types`, `cook_material`, `load_material`,
  `errors.h`; `src/asset_system/CMakeLists.txt` (P9).
- **Renderer:** `material.{h,cpp}`, `renderer.cpp`.
- **Shaders:** the ten `pbr_*` shaders and their CMake contracts.
- **Shader System:** `descriptor_contract.{h,cpp}`.
- **Vulkan Backend:** `vulkan_command_list.h`, `vulkan_device.cpp` (two
  constants).
- **Runtime:** `material_realization.{h,cpp}`, `runtime_application.{h,cpp}`
  (default texture; P13 only if gated in), `main.cpp`, `cli.{h,cpp}`,
  `src/runtime/CMakeLists.txt`.
- **Tools:** `gltf_importer/{main,import_command,material_import,scene_import}.cpp`,
  `asset_cooker/{cook_command,main}.cpp`.
- **Assets:** `assets/bistro/bistro_overlay.scene.txt`, `assets/CMakeLists.txt`,
  every committed material source (v9 version line).
- **Tests:** asset-system material tests, importer tests (incl. `[bistro]`),
  cooker tests, reflection cross-checks, renderer tests, the PBR fixtures
  (default texture), `tests/image_regression/support/provenance.{h,cpp}`
  (schema 3), the content GPU test binary, a golden generator, the
  `bistro_demo` golden.
- **Temporary, reverted:** `android_main.cpp` (M4 Android switch).

**Not touched:** RHI public headers; RenderGraph; the environment,
bloom and fog code paths; scene and texture formats; `.amesh`; any
existing golden; Bistro content or cooked artifacts (never committed).

## Sequencing & Dependencies

M1 → M2 (the importer maps emissive textures only once v9 exists) → M3
(needs both tools) → M4 (needs the running scene) → M5 (captures the
reviewed look). M4's Android half may run before the look loop ends.

## Verification Checklist

- [ ] R5/ADR-0096: v9 round-trip; default slot keeps every golden
      byte-identical; the 11 materials sample their masks (a GPU test on a
      synthetic emissive mask); Validation Layers clean.
- [ ] R4: transmission → Blend, alpha `1 − factor`, in importer tests.
- [ ] R2/ADR-0094: overlay merge and every rejection; light cap 64.
- [ ] R1/ADR-0094: cook-manifest mode on a synthetic import and on all of
      Bistro; the dependency manifest lists 1062 assets exactly once.
- [ ] R3/Q7: `bistro` listed only when the content is present.
- [ ] Runtime: `--scene bistro` renders windowed; frame time, build time
      and peak memory reported.
- [ ] Human "神似" review recorded with the final values.
- [ ] Android: BC7 probe result; screencap + logcat, or the recorded
      failure mode; temporary switch reverted.
- [ ] ADR-0095: `bistro_demo` golden with schema-3 sidecar and three
      failing discriminators; SKIPs without content.
- [ ] Full Debug + Release regression; Android `assembleDebug`.

## Open points (for Joint Human Review)

- **O1 — Bistro renders without the environment.** Recommend a per-entry
  "no environment" flag in the runtime-private whitelist (P10): the global
  `ibl_studio` would light a night street with studio daylight. Alternative:
  keep the IBL and darken by exposure (wrong mood), or author a night HDRI
  (new content, out of scope).
- **O2 — Default emissive texture ownership: Runtime-level** (reading 1c),
  not a Device/RHI default.
- **O3 — Look review before the golden** (milestone order M4 → M5), not the
  brief's golden-with-Android.
- **O4 — Memory mitigations only if measured necessary** (P13, gated at M3).
- **O5 — Android manifest by host-side rewrite** for the temporary switch
  (no committed Android code for Bistro), the M6 precedent.

## Rollback Plan

Revert by milestone. M1 reverts material v9 (the build re-cooks every
material at v8). M2's tools and M3's assembly are additive and
content-gated; reverting them removes the `bistro` entry only. The golden
reverts with its `test:` commit.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas:
- [ ] Every pre-existing golden byte-identical.
- [ ] `bistro_demo` golden (content-gated, schema-3 sidecar) with three
      failing discriminators.
- [ ] Human "神似" sign-off against `example_bistro1.jpg` recorded in the PR.
- [ ] Android outcome recorded (render or disclosed BC7 failure mode).
- [ ] Build time, frame time and peak memory reported.
