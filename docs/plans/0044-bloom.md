# Plan: Bloom

- **Spec:** [Spec 0044: Bloom](../specs/0044-bloom.md) (`Approved`,
  2026-09-24; rulings Q1–Q5 binding) —
  [ADR-0092](../adr/0092-bloom-pass-insertion-blur-strategy-targets-and-parameter-source.md)
  (`Accepted`)
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-09-25 — reviewed this Plan and
  [Spec 0044](../specs/0044-bloom.md) together (chat confirmation;
  document set carried by this branch's PR) and explicitly authorized
  Implementation from Milestone 1. The five open points were ruled in
  the same review: O1 the nullable-pointer input (EnvironmentLighting
  pattern); O2 bloom shader paths optional with a scene-demand error;
  O3 the firefly weight on D1 confirmed; O4 PbrNormalMapDemoFixture
  extended; O5 Android packaging in Milestone 1. The shader relocation
  to M1 (pipelines require compiled shaders) confirmed.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0044 in full — Spec 0036 workflow ⑥. The work is:
- an HDR bright-pass, a fixed six-level downsample/upsample chain and a
  separate composite pass, between the draw pass and the output transform;
- parameters from an optional camera-node `bloom=` group, through scene
  v6, World's `Camera` and a new optional `drawFrame()` input.

ADR-0092's four decisions and rulings Q1–Q5 are binding and are not
reopened here.

## Pre-drafting reading (cited, not restated)

Spec 0044's six investigations were re-walked at Plan granularity against
`origin/main` at `5347397` (PR #184 merged). Every conclusion holds.
Reading item 4 found one real obstacle.

1. **The twelve targets, their sizes, and who rebuilds them.**
   - Each is an ordinary `HdrColorTarget` from
     `Device::createHdrColorTarget({.extent})` (`types.h:236-239`), single
     mip, `COLOR_ATTACHMENT | SAMPLED` (`vulkan_device.cpp:1545,1551`).
     Only the extent differs.
   - Extents, each dimension `max(1, floor(prev / 2))` from the HDR
     target:

     | Target | 512×512 fixtures | 1920×1080 |
     |---|---|---|
     | `D1`, `U1` | 256×256 | 960×540 |
     | `D2`, `U2` | 128×128 | 480×270 |
     | `D3`, `U3` | 64×64 | 240×135 |
     | `D4`, `U4` | 32×32 | 120×67 |
     | `D5`, `U5` | 16×16 | 60×33 |
     | `D6` | 8×8 | 30×16 |
     | composite | 512×512 | 1920×1080 |

     That is 3.49 MB and 27.64 MB respectively, 1.67× the HDR target.
   - **The Runtime creates its `HdrColorTarget` lazily** in the
     extent-change branch (`runtime_application.cpp:1194-1221`), with
     keep-and-retry on failure. The bundle is created and replaced in
     that same branch, adopted only if all twelve targets succeed.
   - **Fixtures are fixed at 512²** and create their targets once in
     setup.
   - **Precedent for the bundle's factory:** `createMesh(Device&, …) →
     Result<Mesh, CreateMeshError>` (`mesh.h:53`), a caller-owned
     Renderer RAII type.
2. **The three shader pairs.**
   - `output_transform_unorm.slang` is the template:
     - a fullscreen triangle from the shared 3-vertex buffer, with UV
       from clip position;
     - one sampler at binding 0;
     - a 4-byte push constant.

     Its viewport is the attachment's own extent
     (`vulkan_command_list.cpp:287-304`), so the same vertex shape serves
     every level.
   - **Contracts:** each new pair gets a contract function beside
     `outputTransformExpectedDescriptorContract()`
     (`descriptor_contract.cpp:161`). Its name is added to
     `compile_and_validate.cpp`'s contract dispatch (`:174`) and to its
     push-constant table (`:233-236`), and it gets an
     `atlantis_add_slang_shader_pair(... EXPECTED_CONTRACT ...)` directory
     beside `shaders/output_transform_unorm` (root `CMakeLists.txt:190`).
   - **Pipelines:** `hasCameraUniformBinding = false`,
     `sampledTextureBindingCount` 1 or 2 (samplers from binding 0,
     `vulkan_device.cpp:1083-1092`), `colorFormat = HdrFormat::Rgba16Float`,
     `hasDepthAttachment = false` (`types.h:302,352`).
   - The I/O per pass is fixed by P6 below.
3. **`drawFrame()`'s new input.**
   - The current trailing parameter is
     `const std::optional<std::array<float,3>>& cameraWorldPosition =
     std::nullopt` (`renderer.h:132-145`).
   - Bloom's input is several borrowed references (a bundle, three
     Pipelines) plus two floats. That is the shape of the existing
     `const EnvironmentLighting* environmentLighting`
     (`environment_lighting.h`: a struct of `const&` members, passed as a
     nullable pointer).
   - A new last parameter `const BloomInput* bloom = nullptr` leaves all
     **38 call sites in 24 files** compiling unchanged (P5).
4. **The scene grammar — and the obstacle.**
   - M1's fog pre-pass (`scene_source.cpp:183-189`) looks for `fog=` and
     then requires the group to be **exactly the last seven tokens**
     (`:186`). A `bloom=` group after it would fail that check.
   - So the pre-pass becomes an ordered list of trailing groups:
     `[fog group][bloom group]` (P1).
   - Everything downstream follows the fog pattern exactly:
     - `DecodedCamera`/`isValidCameraFog` (`scene_types.h:31-54`);
     - the artifact slot, encode `scene_artifact.cpp:115`, decode `:215-228`;
     - the next field, now at `:240`;
     - cook (`cook_scene.cpp:192`);
     - World `CameraFog` (`camera.h:11-24`) and its copy
       (`scene_instantiation.cpp:38`).
   - The version line is in **25 asset files** (24 `assets/scenes`, 1
     `_test_fixtures`), the parser, the importer (`scene_import.cpp`), and
     **8 test sources**.
   - Scene metadata records only a value, so there is no metadata format
     bump (the Plan 0043 reading-item-2 finding).
5. **The output transform's input.**
   - Today it declares `reads(outputTransformPass, hdrResource)` and
     binds `hdrColorTarget` at binding 0 (`renderer.cpp:295-309`).
   - With bloom on it reads the composite resource and binds the
     composite target; otherwise it is unchanged.
   - **Same Pipeline, same contract, one conditional resource/binding**
     (P7). Two Pipelines would change nothing the shader sees.
6. **Who creates the bloom Pipelines (ruling Q5: the caller).**
   - **The Runtime**, from `BootstrapConfig` shader paths filled by
     `main.cpp` (`:222` precedent) and `android/android_main.cpp`
     (`:156`). Android packages each shader through its
     `android/app/build.gradle` asset list (`:71-74` precedent).
   - **`PbrNormalMapDemoFixture`** — the dark emissive fixture both new
     goldens use (P10).
   - No other fixture or test changes: they pass no bloom input.
   - Optional path groups have a precedent:
     `validateEnvironmentBootstrapConfig()` (`bootstrap_config.cpp:5-30`)
     makes the IBL/sky shader pairs required only when an environment is
     configured.

## Plan-stage decisions

**P1 — the `bloom=` group, and an ordered trailing-group pre-pass.** Per
ruling Q1:

```
... camera_far_z=<f> [camera_exposure_ev=<f>]
    [fog=<d> <h> <k> <m> fog_color=<r> <g> <b>] [bloom=<strength> <threshold>]
```

- The group is two tokens, all or nothing; each group is optional.
- They come in this fixed order, and nothing may follow them.
- The pre-pass finds the first token at index ≥ 11 whose prefix is `fog=`
  or `bloom=`. From there it consumes the fog group (7 tokens) if present,
  then the bloom group (2) if present, and must reach the end of the line.
- It strips them before the count gate, so a line with neither group
  parses and fails exactly as in v5.
- Errors:
  - out of order, truncated or trailing tokens: `InvalidComponentGroup`;
  - `bloom=` on a non-camera node: `InvalidComponentGroup`;
  - a non-number: `MalformedNumber`.
- The serializer writes the group only when `strength != 0`.
- Gate: every pre-existing parser test passes with only its version line
  changed, fog tests included.

**P2 — ranges reuse `NonFiniteValue`, at cook and decode** (the P2 of Plan
0043). `strength` finite and in `[0, 1]`, `threshold` finite and `≥ 0`
(Spec R5), through an `isValidCameraBloom()` beside `isValidCameraFog()`.

**P3 — the data types.**
- `DecodedCameraBloom {strength = 0, threshold = 1}` trails
  `DecodedCamera`; World's `CameraBloom` trails `Camera`.
- Defaults mean off. Threshold 1 is inert while strength is 0, and is
  Filament's knee.
- No existing `Camera{...}` site changes. `scene_instantiation.cpp` copies
  the group field by field, as it does fog.

**P4 — scene artifact v6.**
- The node record grows from 144 to 152 bytes.
- `bloom_strength(84)` and `bloom_threshold(88)` go right after the fog
  slot; every later field moves by 8.
- A node with no camera writes the defaults. v5 is rejected.

**P5 — the Renderer surface.**
- In a new `renderer/bloom.h`:
  - `kBloomLevelCount = 6` and `kBloomHighlight = 1000.0f` (fixed, ruling
    Q3);
  - a pure `bloomLevelExtents(Extent2D) → std::array<Extent2D, 6>`,
    GPU-independent and unit-tested;
  - `BloomTargets`: caller-owned RAII, move-only. It holds `D1…D6`,
    `U1…U5`, the composite and a linear clamp-to-edge `Sampler`, and
    exposes `extent()`.
- `createBloomTargets(Device&, Extent2D) → Result<BloomTargets,
  CreateBloomTargetsError>` is all-or-nothing.
- `struct BloomInput { BloomTargets& targets; const rhi::Pipeline&
  downsample, upsample, composite; float strength, threshold; }` — the
  `EnvironmentLighting` shape.
  - *Correction (2026-09-25, Human Review approved):* the three Pipeline
    references become `std::array<rhi::Pipeline*, 12>`, one instance per
    pass (`D1…D6`, `U1…U5`, composite) — see
    [ADR-0092's Accepted Correction](../adr/0092-bloom-pass-insertion-blur-strategy-targets-and-parameter-source.md#accepted-correction--2026-09-25-decision-3-one-pipeline-per-bloom-pass).
- `drawFrame(..., cameraWorldPosition = std::nullopt, const BloomInput*
  bloom = nullptr)`.
- A null `bloom`, or `strength == 0`, declares no bloom pass.
- Otherwise `ATLANTIS_CHECK_MSG` enforces a `targets.extent()` equal to
  the HDR target's, `strength` in `[0, 1]`, and `threshold` finite and
  `≥ 0`. This is the direct-caller gate, the exposure precedent.

**P6 — the passes and the shaders.** Each pass writes one resource
(`ColorAttachmentOutput`); `R` = reads (`ShaderRead`):

| Pass | Pipeline / samplers | Reads | Writes | Push constants (16 B) |
|---|---|---|---|---|
| `bloom_down_1` | downsample, 1 | `hdr_color` | `D1` | source texel size, threshold, bright-pass on |
| `bloom_down_k`, k = 2…6 | downsample, 1 | `D(k−1)` | `Dk` | source texel size, bright-pass off |
| `bloom_up_k`, k = 5…1 | upsample, 2 (b0 `Dk`, b1 `U(k+1)` / `D6`) | both | `Uk` | lower texel size |
| `bloom_composite` | composite, 2 (b0 `hdr_color`, b1 `U1`) | both | composite | `U1` texel size, strength, 1/6 |

- **Downsample:** the 13-tap filter (Jimenez 2014, Filament's shape).
- **Bright-pass:** on `D1` only, per tap, before the weights. It is
  `b = max(c − threshold, 0); b *= 1 / (1 + max3(b) / kBloomHighlight)`,
  then a firefly weight `1 / (1 + max3(b))` per 4-tap group,
  renormalised. This is Spec R3, and settles its "Plan's to confirm".
- **Upsample:** a 9-tap tent at the lower level's texel size, plus the
  current level's sample.
- **Composite:** the same tent on `U1`, then
  `rgb = hdr + strength · bloom / 6`, alpha from `hdr`. It samples
  `hdr_color` at its own texel centres, so the hdr term is exact.
- **Exactness (Spec R7):**
  - below the knee every tap is exactly 0, so every level is exactly 0
    and the composite is `hdr + 0`;
  - fp16 → fp32 → fp16 of an unmodified texel is exact.

  So the frame is byte-identical — a GPU gate, not an assumption.

**P7 — the output transform switches by binding, not by Pipeline.** When
bloom is on, `drawFrame()` declares the output transform's read on the
composite resource and binds the composite target; the Pipeline, its
contract (ADR-0068 D-10) and the exposure push constant are unchanged.
The resource list and `ResourceBinding`s are assembled conditionally,
keeping `resourceAt(i)` in declaration order.

**P8 — Shader System contracts.**
- `bloomDownsampleExpectedDescriptorContract()`: one sampler.
- `bloomUpsampleExpectedDescriptorContract()` and
  `bloomCompositeExpectedDescriptorContract()`: two samplers, bindings 0
  and 1, all Fragment, no uniform.
- `compile_and_validate.cpp` gains `bloom-downsample`, `bloom-upsample`
  and `bloom-composite`, with a 16-byte push-constant expectation each.

**P9 — Runtime.**
- `BootstrapConfig` gains the three pairs' 12 paths.
- `validateBloomBootstrapConfig()` requires all or none (a new
  `RuntimeInitError::BloomConfigInvalid`). It also rejects a scene whose
  active camera has `strength > 0` when the paths are absent.
- The Runtime creates the three Pipelines at startup when the paths are
  present.
  - *Correction (2026-09-25, Human Review approved):* twelve Pipeline instances
    from the three shader pairs, not three — see
    [ADR-0092's Accepted Correction](../adr/0092-bloom-pass-insertion-blur-strategy-targets-and-parameter-source.md#accepted-correction--2026-09-25-decision-3-one-pipeline-per-bloom-pass).
- It creates and replaces `BloomTargets` in the resize branch only when
  the active camera has `strength > 0`.
- It passes a `BloomInput` built from the active camera each frame.
- `main.cpp`, `android_main.cpp` and `android/app/build.gradle` supply and
  package the three pairs.

**P10 — the one fixture that renders bloom.**
- `PbrNormalMapDemoFixture` (the dark emissive fixture) gains optional
  bloom Pipelines and `BloomTargets`, created in setup only when its
  config carries the bloom paths.
- Its render passes a `BloomInput` only when the World camera's
  `strength > 0`.
- Every existing use passes no bloom paths and no bloom scene, so it is
  unchanged.
- The new goldens alias it, the `emissive_demo`/`fog_dark_demo`
  precedent.

**P11 — `bloom_reference.h`** (image-regression support, the
`fog_reference.h` precedent). It holds `bloomBrightPass(rgb, threshold)`
(Spec R3) and the `D1` firefly weight, unit-tested in
`atlantis_image_regression_tests`. It does not model the whole chain:
halo shape is checked by properties and goldens, not analytically.

**P12 — goldens.** Both are dark scenes on the P10 fixture, with knee 1.0.
- **`bloom_demo`:**
  - emissive spheres above the knee: `emissive_demo_orange` (4, 0.5, 0)
    and `emissive_demo_blue` (0.2, 0.3, 2.5);
  - `emissive_demo_green` (max 1.0, exactly at the knee, so no
    contribution);
  - the black control sphere.
- **`bloom_fog_demo`:**
  - one emissive lamp sphere;
  - a large backdrop `ground_plane` with the control material;
  - HDR fog whose colour exceeds the knee (e.g. (3, 2.4, 1.5)), so the
    fogged backdrop itself glows.
- Each golden has a discriminator: `strength` set to 0 on the World
  camera must fail against it.

## Milestones / Task Breakdown

**Two milestones, three commits.**

### Milestone 1 — scene v6, the data path, the bundle, the shaders and Pipelines (`feat:`, R4–R5, R8)

Zero rendering change: `drawFrame()` never receives a `BloomInput`, and
no committed scene declares bloom.

1. Parser pre-pass and grammar (P1), serializer, `DecodedCameraBloom`,
   cook and decode checks (P2–P3), artifact v6 (P4).
   - v5 → v6 in the 25 asset files, the importer, and the 8 test
     sources.
   - The artifact byte test moves to 152-byte records.
2. World `CameraBloom` and the instantiation copy (P3).
3. `renderer/bloom.h`/`bloom.cpp`: the constants, `bloomLevelExtents()`,
   `BloomTargets`/`createBloomTargets()`, `BloomInput`, and the
   `drawFrame()` parameter. In M1 the parameter only asserts it is null
   or off; passes arrive in M2 (P5).
4. The three shader pairs, their contracts and validation (P6, P8),
   compiled by the build, plus a reflection test in the
   `pbr_reflection_cross_check_tests.cpp` style: samplers at 0/1 and a
   16-byte push block.
5. Runtime config, validation and Pipeline creation; bundle creation in
   the resize branch; Android packaging (P9). Fixture support (P10).
6. `bloom_reference.h` and its tests (P11).

**Risk gates.**
- **Zero rendering change:** full Debug suite, every golden
  byte-identical.
- **Parser parity:** a pre-existing parser test (fog included) needing
  more than its version line changed means the pre-pass changed
  behaviour — stop and report.
- **Pipeline creation:** the three Pipelines create cleanly with
  Validation Layers on, in the Runtime smoke test and the fixture setup.

### Milestone 2 — the passes, goldens, regression (`feat:` + `test:`, R1–R3, R6–R7)

1. `drawFrame()` declares and records the twelve passes when on (P5–P7);
   final shader tuning, if any, lands here.
2. GPU tests (non-golden) in a new `bloom_demo_gpu_tests.cpp`:
   - **neutrality:** `emissive_demo` rendered with bloom on at a knee above
     its brightest channel (e.g. 10) equals bloom off, byte for byte (R7);
   - **halo:** in `bloom_demo`, pixels in a ring outside each bright
     sphere's silhouette are brighter with bloom than without. The
     at-knee green sphere's disc and ring are unchanged, except where
     another sphere's halo reaches them;
   - **radial monotonicity** of an isolated source's halo, and growth
     with `strength`;
   - **fog → bloom:** `bloom_fog_demo` with fog on vs off gives different
     halos, and fog-on glow lies outside the lamp;
   - **`drawFrame()` gates:** a bad `strength`, a bad `threshold` or a
     mismatched extent triggers `ATLANTIS_CHECK_MSG`, where the
     Renderer's fake-backed tests can observe it.
3. The Runtime smoke test with a bloom scene, so the full Runtime path
   runs once under Validation Layers.
4. **The `test:` commit:** both goldens, captured on the clean `feat:`
   tree (ADR-0042 Initial baseline, four evidence items), each with its
   capture-compare case and its `strength = 0` discriminator.
5. Full regression: Windows Debug and Release; Validation Layers clean;
   Android `assembleDebug` (ASProxy first, then local Gradle 9.5.1
   `--offline`).

**Risk gates.**
- **Every pre-existing golden byte-identical** in both configurations —
  a moved one means a bloom-off path is declaring passes; stop and
  report.
- **The neutrality test is exact.** Any difference means the knee is not
  a hard zero or the composite does not sample `hdr` exactly — fix, do
  not tolerate.
- **Validation Layers:** zero messages on the twelve-pass frame.

## Files / Modules Touched (expected)

- **Asset System:** `scene_types.h`, `scene_source.cpp`,
  `scene_artifact.{h,cpp}`, `cook_scene.cpp`, and their tests.
- **World:** `camera.h`, `scene_instantiation.cpp`, and their tests.
- **Renderer:**
  - new `include/atlantis/renderer/bloom.h` and `src/bloom.cpp`;
  - `renderer.h`/`renderer.cpp` (`drawFrame()`);
  - the renderer target's CMake;
  - `tests/renderer` (level extents, gates).
- **Shader System:** `descriptor_contract.{h,cpp}`,
  `src/tools/shader_compiler/compile_and_validate.cpp`.
- **Shaders:** new `shaders/bloom_downsample`, `shaders/bloom_upsample`,
  `shaders/bloom_composite` (`.slang` + `CMakeLists.txt`), and root
  `CMakeLists.txt` `add_subdirectory` lines.
- **Runtime:**
  - `bootstrap_config.{h,cpp}`, `runtime_application.{h,cpp}`,
    `main.cpp`;
  - `android/android_main.cpp` and `android/app/build.gradle`;
  - the Runtime smoke test and `pbr_reflection_cross_check_tests.cpp`
    (bloom reflection).
- **Importer:** `scene_import.cpp`, the version string only.
- **Assets:** the 25 version lines, and two new scenes with their
  `assets/CMakeLists.txt` registrations (existing materials only).
- **Tests:**
  - `PbrNormalMapDemoFixture` (P10) and a `bloom_demo_fixture.h` alias;
  - `bloom_reference.h` and its tests;
  - `bloom_demo_gpu_tests.cpp`, a golden generator, and two goldens;
  - the 8 test sources carrying scene version strings.

**Not touched** — needing any of these is a stop-and-report:
- the RHI, the Vulkan Backend (`hdr_color_target_capability.cpp`
  included), RenderGraph, Platform;
- the output-transform shaders and their contract (ADR-0068 D-10);
- the ten PBR shaders and the material schema;
- any existing golden;
- the scene metadata format.

## Sequencing & Dependencies

M1 → M2.
- M1 is bisectable on its own: it parses, stores, carries and validates
  bloom, builds its Pipelines and targets, and renders exactly as
  before.
- M2 turns it on. Within M2, the golden `test:` commit follows the
  `feat:` on a clean tree.
- Android packaging lands in M1, so a v6 bloom scene is loadable on both
  platforms before any pass records.

## Verification Checklist

- [ ] R4–R5 (M1): parse — group present or absent, with and without fog
      and exposure, out of order, truncated, on a non-camera node; v5
      rejected; serializer round trip; cook/decode ranges; artifact v6 at
      152 bytes; World carriage.
- [ ] R2 (M1): `bloomLevelExtents()` — 512², 1920×1080, odd sizes, 1×1
      (all six levels ≥ 1); `createBloomTargets()` extents on a GPU.
- [ ] R3 (M1): `bloom_reference` bright-pass — below or at the knee gives
      exactly 0; above gives `c − t`; highlight compression; firefly
      weight.
- [ ] Contracts (M1): three shader pairs validate; the reflection test
      finds samplers at 0/1 and 16-byte push blocks.
- [ ] R1 (M2): off ≡ today (every golden byte-identical, Debug and
      Release); the output transform reads the composite only when on.
- [ ] R7 (M2): below-knee neutrality, byte for byte.
- [ ] R1–R3 (M2): halos outside the bright spheres; the at-knee sphere
      adds none; monotone falloff; growth with `strength`.
- [ ] Fog → bloom (M2): fogged haze above the knee glows; fog on vs off
      changes the halo.
- [ ] Goldens (M2): `bloom_demo` and `bloom_fog_demo`, each with a
      `strength = 0` discriminator that fails.
- [ ] Validation Layers clean (Debug, fatal) on the twelve-pass frame and
      the Runtime smoke run.
- [ ] Android `assembleDebug` green with the new shaders packaged.

## Open points (for Joint Human Review)

- **O1 — `const BloomInput* = nullptr` vs a `std::optional`.** Recommend
  the pointer, the `EnvironmentLighting` precedent: its members are
  borrowed references, which `std::optional` cannot hold without
  `reference_wrapper`s.
- **O2 — bloom shader paths optional, with a hard error only when the
  scene needs them** (P9). The alternative makes them unconditionally
  required and edits every test `BootstrapConfig` builder (~20 files).
  Recommend optional.
- **O3 — the `D1` firefly weight** (P6). Recommend it: Bistro's bulbs
  reach 100, and a single hot texel otherwise strobes as the camera
  moves. It is in `bloom_reference.h` and tested.
- **O4 — extending `PbrNormalMapDemoFixture` (P10) rather than a new
  fixture.** Recommend extending: it already carries both direct-lit
  pairs and the control material both goldens need; bloom stays
  inactive for every existing use.
- **O5 — Android packaging in M1.** Recommend it, so ⑦ inherits a working
  path. The cost is four assets per pair in `build.gradle` and three
  extraction blocks in `android_main.cpp`.

## Rollback Plan

- M2 reverts to a tree that parses, stores and validates bloom and builds
  its Pipelines, but never records a bloom pass.
- M1 reverts the scene format to v5, World's `Camera` and `drawFrame()`
  to their Plan 0043 shapes, and removes the shaders and config fields.
  Artifacts are build outputs and recook. No committed scene carries a
  `bloom=` group until M2's two new scenes, which revert first.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas: the PR records the twelve-target extent table, the neutrality
result, the two goldens' four evidence items each, the Validation Layers
result for the twelve-pass frame, and the Android packaging check.
