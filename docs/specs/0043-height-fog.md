# Spec: Height Fog

- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-24
- **Related Plan(s):** none yet
- **Approval:** pending
- **Related ADR(s):** [ADR-0091](../adr/0091-height-fog-insertion-point-uniform-layout-and-parameter-source.md)
  (`Proposed`) — the insertion point, the fog model, the uniform layout and
  the parameter source.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Add warm, exponential height fog to lit PBR surfaces: dense near the
ground and thinning with altitude, as in Bistro's night-street reference
image. This is [Spec 0036](0036-bistro-parity-roadmap.md) workflow ⑤. The
fog is one analytic per-fragment term in the ten PBR shaders, in HDR
space, with no new pass. Its parameters come from optional tokens on the
scene's camera node and reach the GPU in a new 32-byte tail on the
existing camera uniform. Absent fog is density 0, which is an exact
identity, so every existing golden stays byte-identical. ADR-0091 records
the insertion point against the two depth-based alternatives.

## Motivation / Problem Statement

Spec 0036 ⑤ records the ceiling as zero: nothing in the engine
attenuates radiance with distance or height, so a large outdoor scene has
no depth cue and no atmosphere (`0036:508-538`). Bistro's reference look
depends on it. ⑤ comes after ②–④ because fog composites over the
lit, emissive and transparent surfaces those workflows produce. It also
comes before ⑥, whose bloom should see fogged radiance.

## Goals

- A scene can declare height fog: colour, density, reference height,
  height falloff and a maximum opacity.
- Fogged output follows one documented formula, checked against a CPU
  reference implementation.
- No fog declared means an unchanged frame, byte for byte.
- ⑥ Bloom can insert after the draw pass and see fogged HDR radiance.

## Non-Goals

- **Volumetric fog and light shafts (god rays).** No in-scattering from
  scene lights; Filament's `inScatteringStart`/`inScatteringSize` are not
  adopted.
- **Spatially varying density**, noise, or local fog volumes. The density
  is a function of height only.
- **Fog on non-PBR shaders:** `lit_textured`, `unlit_textured`,
  `textured_quad`, `sky`, `shadow_cast`, output transform. Bistro's fog
  reads on its lit surfaces; the sky and clear colour stay unfogged (Q4).
- **Fog colour from the IBL** (`fogColorFromIbl`), start distance
  (`distance`) and cut-off distance (`cutOffDistance`).
- **The bloom bright-pass interaction** — ⑥ decides its own threshold. This
  Spec only guarantees that fog is already in the HDR target.
- **Importer mapping.** glTF core carries no fog, so ⑦ writes Bistro's fog
  tokens into its scene.

## Requirements

### Functional

1. **Fog model (ADR-0091 Decision 2).** For a fragment at world position
   `p` and camera position `c`:
   - `d = |p − c|`, `Δy = p.y − c.y`, `h = c.y − height`;
   - `τ = density · d · e^(−heightFalloff · h) · g(heightFalloff · Δy)`,
     where `g(x) = (1 − e^(−x)) / x` and `g(0) = 1`, evaluated stably near
     0 (the Plan picks the threshold and the series);
   - `f = min(1 − e^(−τ), maxOpacity)`;
   - fogged RGB = `lit · (1 − f) + color · f`, alpha unchanged.

   `heightFalloff = 0` is homogeneous fog. `density = 0` disables the term
   exactly (R3).
2. **Where it applies (ADR-0091 Decision 1).**
   - All ten PBR fragment shaders apply R1 after the emissive term and
     immediately before the return.
   - The Spec 0042 alpha-test `discard` keeps its earlier position.
   - Blended fragments are fogged before blending, at their own distance.
   - No other shader changes.
3. **Exact disable.**
   - The shaders evaluate R1 only when `density > 0`, a uniform branch.
   - With density 0 the output is the unfogged value bit for bit, so every
     existing golden stays byte-identical.
4. **GPU data (ADR-0091 Decision 3).**
   - A `FogData` block of 32 bytes is appended to the camera uniform at
     offset 2512: `float3 color`, `float density`, `float height`,
     `float heightFalloff`, `float maxOpacity`, `float _pad`.
   - The camera uniform grows from 2512 to 2544 bytes.
   - The ten PBR shaders declare it as the block's last member.
   - C++ mirrors it beside `CameraWorldPositionData`, with a `static_assert`
     per offset.
   - `kCameraUniformBufferSizeBytes` and its derived constants move with
     it.
5. **Every writer writes it.**
   - Every composition root that fills the camera uniform (the Runtime
     and every fixture — 16 files call `extractCameraWorldPosition()`
     today) writes `FogData` every frame, density 0 when the scene has no
     fog.
   - The tail is never left as uninitialised mapped memory.
6. **Parameter source (ADR-0091 Decision 4).**
   - The camera node in `.scene.txt` takes optional fog tokens:
     - colour — linear RGB;
     - density — per metre at the reference height;
     - reference height — metres;
     - height falloff — per metre;
     - maximum opacity.
   - They sit after the optional `camera_exposure_ev=` (Spec 0031).
   - All of them absent means density 0, which is off. The exact grammar
     (one prefixed group or separate tokens, and which may be omitted) is
     Q2.
7. **Formats and data path.**
   - Scene source v4 → v5, with the matching artifact and metadata bumps
     and no dual-version reader (the ADR-0066 discipline).
   - World's `Camera` component carries the fog fields as plain data
     beside `exposureCompensationEv`, with no new World API shape.
   - The Runtime extracts them from the active camera into `FogData` each
     frame, through a pure extraction function like
     `extractCameraWorldPosition()`.
8. **Validation, at cook time and again on decode.**
   - Density and height falloff: finite and ≥ 0.
   - Height: finite.
   - Maximum opacity: in `[0, 1]`.
   - Colour: finite and in `[0, 65504]` — HDR, the emissive precedent
     (Q7).

### Non-functional

- **Performance.** When fog is on: two `exp`, one `sqrt` (for `d`) and
  about fifteen ALU ops per shaded PBR fragment, overdraw included. When
  it is off: one uniform branch. No new pass, render target, bandwidth or
  draw.
- **Memory.** 32 bytes per camera uniform. The scene artifact grows by the
  fog fields.
- **Portability.** Plain shader arithmetic and a larger uniform block, with
  no new Vulkan feature or format capability. Identical on Windows and
  Android.
- **Golden neutrality.** A stop-and-report gate: any existing golden that
  moves means the density-0 branch or the uniform layout is wrong.

## Pre-drafting Investigation (required conclusions, cited against real files)

### Investigation 1 — ADR-0068's pass structure, and the three places fog could go

- **The frame is three RenderGraph passes** (`src/renderer/src/renderer.cpp`):
  - `shadow` (ADR-0072);
  - `draw` — writes `hdr_color`, the `HdrColorTarget` (`R16G16B16A16_SFLOAT`,
    colour-attachment + sampled usage), and depth (ADR-0068 D-1/D-3);
  - `output_transform` — samples `hdr_color` through one nearest sampler,
    pushes a 4-byte exposure multiplier, writes the final target.
- The output transform's descriptor contract is one sampler and no uniform
  buffer (ADR-0068 D-10). All 20 sites that create its Pipeline pass
  `hasCameraUniformBinding = false`.
- **The three candidates, quantified:**

  | | (a) Per-fragment in PBR shaders | (b) Separate post pass | (c) Inside the output transform |
  |---|---|---|---|
  | Shaders changed | 10 PBR fragment shaders | 1 new fog shader pair | 2 output-transform variants |
  | New RHI surface | none | sampleable scene depth (usage flag, `Texture` `ShaderRead` state, `bindTexture(Texture)` overload) | same as (b) |
  | New render target | none | a second `HdrColorTarget` (the single-producer rule forbids reading and writing `hdr_color` in one pass) | none |
  | RenderGraph passes | 3 (unchanged) | 4 | 3 |
  | New Pipelines | none | 1, at every composition root (20 today create output-transform Pipelines) | none, but 20 creation sites change descriptor contract |
  | `drawFrame()` signature | unchanged | new fog Pipeline, second target, sampler and depth parameters (38 call sites) | new depth/uniform parameters |
  | Extra data | 32 B camera-uniform tail | same + inverse view-projection | same + inverse view-projection |
  | Transparent surfaces | correct (own depth, pre-blend) | wrong: blended draws write no depth (ADR-0090), so they are fogged at the depth behind them | same as (b) |
  | Sky / clear colour | not fogged | fogged unless masked | fogged unless masked |
  | ⑥ Bloom | fog already in HDR before any post pass | works if ordered before bloom | fog after bloom's input — **wrong order** |
  | ADR impact | new ADR only | new ADR + RenderGraph/RHI surface | supersedes ADR-0068 D-10 |
  | Cost model | per shaded fragment | once per pixel + a full-screen pass | once per pixel |

  **Recommendation (a)** — ADR-0091 Decision 1. It is the only option with
  no RHI, RenderGraph or `drawFrame()` change. It is the only one correct
  for Spec 0042's blended surfaces. It already satisfies ⑥'s ordering
  need. Bistro needs fog on its lit surfaces, which (a) covers exactly;
  (b)'s once-per-pixel saving does not justify its surface.

### Investigation 2 — is scene depth available to a later pass?

- **No.** The depth `Texture` is created with
  `VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT` only
  (`src/vulkan_backend/src/vulkan_device.cpp:890`). The RHI `Texture`
  interface is `extent()` and `format()` only
  (`src/rhi/include/atlantis/rhi/texture.h`), with no sampled bind.
- The output transform does not read depth (Investigation 1).
- The one sampleable depth image is `ShadowMap`
  (`vulkan_device.cpp:1642`, `DEPTH_STENCIL_ATTACHMENT_BIT |
  SAMPLED_BIT`). It has its own `transitionResource`/`beginRendering`/
  `bindTexture` overloads (`command_list.h:154-168`) and the
  `depthAttachmentReadWriteToShaderRead` barrier
  (`resource_state_mapping.cpp:167`). That is a precedent (b) and (c)
  could copy, not something they get for free.
- Reconstruction would also need the inverse view-projection. The camera
  uniform carries `view` and `projection` only, and Slang has no built-in
  4×4 inverse.
- `OffscreenTarget` is a colour target (`COLOR_ATTACHMENT | TRANSFER_SRC`,
  `:1358`) and has no depth of its own.

### Investigation 3 — the per-shader option's footprint

- **Position data is already in every PBR fragment.** All ten PBR shaders
  interpolate `float3 worldPosition` and declare the camera uniform
  through its full 2512-byte tail (both light-space matrices).
- **The camera position has moved.** Since Plan 0040 widened point lights
  to 64, `cameraWorldPosition` is at **2224–2239**, not the 304–319 some
  older text cites (`scene_extraction.h:167-182`, `static_assert`
  2224/2240/2384/2512).
- **Fog parameters cannot go in push constants.** `PbrSheen` is at exactly
  128 bytes, and the other three layouts spent their tail pad on
  `alphaCutoff` (Spec 0042).
- **The camera uniform is the natural home.** It is per-frame, already
  bound to every PBR Pipeline at binding 0, and `FogData` appends at 2512.
- **Blast radius:**
  - the 10 shaders;
  - `scene_extraction.h` (struct, offsets, size);
  - 12 files that size the buffer via `kCameraUniformBufferSizeBytes`
    (they move automatically);
  - 16 files that write the camera uniform (each must write `FogData`,
    R5);
  - the 11/11 `CameraUniform` reflection cross-check
    (`pbr_reflection_cross_check_tests.cpp:323`), extended to `FogData`
    for the 10 PBR shaders.
- **`lit_textured` stops at `pointLights`** (the cross-check's `nullptr`
  row) and stays unfogged, as do the unlit shaders (Non-Goals).

### Investigation 4 — where the parameters come from

- **Per-scene look parameters already live in the scene file.** Spec 0031
  added `camera_exposure_ev=` to the camera node (scene source v4,
  `src/asset_system/src/scene_source.cpp:12-16,45`), carried as
  `Camera::exposureCompensationEv` (`src/world/include/atlantis/world/camera.h:9`)
  through `scene_types.h`, `scene_artifact.cpp` and `cook_scene.cpp`.
- **The environment is global, not per scene.** It is one
  `BootstrapConfig` path set in `src/runtime/main.cpp:125`, shared by every
  `--scene` whitelist entry.
- **Filament models fog as a `View` option** (`FogOptions`, per camera
  view).
- **Bistro's ⑦ assembly** is one scene among several whitelist entries.
  Only it wants fog, and its `.scene.txt` is importer output that ⑦ edits
  anyway (lights, camera).
- **Recommendation: camera-node tokens in the scene file** (ADR-0091
  Decision 4). `BootstrapConfig` would fog every scene the executable can
  load. It would also force golden sweeps into code, where a fixture can
  instead set the World camera's fog fields before rendering, as Spec
  0042's tests mutate `materialDataMap`.
- **Cost:** a scene-format bump. It touches 20 `assets/scenes` files and 10
  test sources' version strings (the Spec 0041/0042 bump shape), plus the
  artifact, metadata and World `Camera`.

### Investigation 5 — HDR vs LDR, and the dependency on ⑥

- **Fog belongs in HDR, before tone mapping.** It mixes radiance with a
  fog colour that is itself radiance — a lamp-lit haze can exceed 1.0 and
  should reach ⑥'s bright-pass. In LDR, after Reinhard (ADR-0068 D-5), it
  would mix display values and would be wrong under exposure changes
  (Spec 0031).
- **(a) writes fogged radiance into the `HdrColorTarget` in the draw pass
  itself.** So the dependency for ⑥ is simply: *bloom's passes read
  `hdr_color` after the draw pass and before the output transform* —
  which is already ⑥'s shape.
- **(c) would put fog after any such pass**, inverting the order. That is
  a further reason it is rejected.
- **Recorded as a constraint on ⑥:** it must not move fog, and it must
  treat the draw pass's HDR output as already fogged.

### Reference: Filament `FogOptions` (shape only)

Filament's public `filament/include/filament/Options.h` `FogOptions` has:

| Field | Filament default | This Spec |
|---|---|---|
| `density` | 0.1 /m at `height` | adopted as a field |
| `heightFalloff` | 1.0 /m | adopted |
| `height` | 0 | adopted |
| `maximumOpacity` | 1.0 | adopted |
| `color` | white | adopted |
| `distance`, `cutOffDistance` | — | out of scope |
| `inScatteringStart`, `inScatteringSize` | — | out of scope |
| `fogColorFromIbl`, `skyColor` | — | out of scope |
| `enabled` | — | replaced by density 0 |

These are parameter-shape references only (Spec 0036 ⑤). Bistro's
tuned values are unpublished and are ⑦'s to choose.

## Proposed Design

**Per frame:**
1. The Runtime (or a fixture) reads the active camera's fog fields from
   the World.
2. A pure `extractFogData()` packs them into `FogData`.
3. `FogData` is written into the camera uniform tail beside
   `CameraWorldPositionData`.

**Per fragment, in each of the ten PBR shaders:**
1. After `accumulated + emissiveFactor`, if `camera.fog.density > 0`,
   compute R1 from `input.worldPosition` and `camera.cameraWorldPosition`.
2. Mix RGB toward `camera.fog.color`.
3. Return with the original alpha.

Nothing else in the frame changes: no pass, Pipeline, binding, resource
state or `drawFrame()` parameter.

**Scene data.** The optional camera-node tokens go through source →
artifact → metadata → World `Camera`, exactly like
`camera_exposure_ev=`.

## Architectural Impact

**Yes — [ADR-0091](../adr/0091-height-fog-insertion-point-uniform-layout-and-parameter-source.md)
(`Proposed`).** It records four coupled decisions:
1. the insertion point — a per-fragment PBR term in HDR, not a pass;
2. the fog model and its exact disable at density 0;
3. the uniform layout — the `FogData` tail at 2512, not push constants;
4. the parameter source — camera-node scene tokens, not `BootstrapConfig`.

They are coupled: the insertion point decides which data the fog term
can reach, and the data source must suit per-scene fog.

Other surfaces:
- the scene source, artifact and metadata formats bump to v5;
- World's `Camera` gains plain-data fields (its existing shape);
- the camera uniform's size constant grows by 32 bytes.

No module boundary, dependency, threading or ownership change.
RenderGraph, RHI, Vulkan Backend, Platform and `drawFrame()` are
untouched.

## Alternatives Considered

The insertion-point alternatives are compared in Investigation 1 and
recorded in ADR-0091. At Spec level:
- **Linear or `exp²` distance fog.** It has no height term, so it cannot
  give "dense at street level, thin above". Height fog with
  `heightFalloff = 0` already covers homogeneous fog.
- **Fog in LDR, after tone mapping.** Physically wrong, and invisible to
  ⑥ (Investigation 5).
- **Fixed fog constants in the Runtime.** Every scene would get Bistro's
  fog, and a golden sweep would need code changes (Investigation 4).

## Testing & Verification Plan

Spec 0036's ⑤ row is the contract: goldens that sweep density and height
falloff, plus a GPU-independent test of the fog-factor maths.

- **R1, GPU-independent:** a C++ reference `fogFactor()` in the
  image-regression support library (the `tone_mapping_reference.h`
  precedent), unit-tested for:
  - density 0 gives exactly 0;
  - falloff 0 gives `1 − e^(−density·d)`;
  - a horizontal ray gives `τ = density·d·e^(−falloff·h)`;
  - `g` is continuous across `Δy → 0`;
  - fog increases with distance and density, and decreases as the camera
    rises;
  - the `maxOpacity` clamp holds.
- **R4–R5, GPU-independent:** `extractFogData()` tests; `FogData` offset
  `static_assert`s; the `CameraUniform` reflection cross-check extended to
  `FogData`'s offset and size in the 10 PBR shaders.
- **R6–R8, GPU-independent:**
  - scene source parse — present, absent, malformed, out of range;
  - v4 rejected;
  - artifact and metadata round trips and agreement;
  - World camera carriage.
- **R1–R2, GPU, exact:**
  - A dark scene of emissive-only PBR spheres at several distances and
    heights, with no light (the Plan 0041 dark-scene precedent). Each
    sphere centre must equal
    `tonemap(E·(1 − f) + C·f)` computed from the reference `fogFactor()`.
  - This covers each of the four shader families (direct-lit, IBL,
    clearcoat/sheen/anisotropic) the way Plan 0041's differentials did.
- **R3 and neutrality, GPU:**
  - The fog scene with density 0 must render byte-identically to the same
    scene with no fog tokens.
  - Fog on versus fog off must differ only where PBR surfaces are drawn:
    clear-colour and sky pixels stay byte-identical.
  - Every existing golden stays unchanged.
- **Goldens (the sweep):** a `fog_demo` scene (floor, spheres at several
  heights and distances, a warm fog colour) captured at three parameter
  sets — reference, higher density, lower falloff — each in its own
  Initial-baseline commit (ADR-0042). Each has a discriminator: the same
  frame with fog off fails against it. Whether the sweep is three goldens
  or one golden plus the analytic checks is Q3.
- **Validation Layers:** zero warnings or errors (Debug, fatal).
- **Full regression:** Debug and Release; Android `assembleDebug`.

## Risks & Open Questions

- **Q1 — the insertion point.** Recommend (a), per-fragment in the ten PBR
  shaders (ADR-0091 Decision 1). The alternatives are (b), a depth-based
  post pass, and (c), a term in the output transform; Investigation 1
  quantifies all three.
- **Q2 — the token shape and home.** Recommend optional tokens on the
  camera node (Filament's per-view model, the `camera_exposure_ev=`
  precedent). The alternative is a scene-level fog line or a new World
  component. The Plan also fixes the grammar: one prefixed group, e.g.
  `fog=<density> <height> <falloff> <maxOpacity> fog_color=<r> <g> <b>`,
  or separate tokens, and which may be omitted.
- **Q3 — how many goldens.** Spec 0036 asks for goldens "sweeping
  density/height-falloff". Recommend three goldens of one scene, plus the
  exact analytic sweep. The cheaper alternative is one golden plus the
  analytic sweep.
- **Q4 — the sky stays unfogged.** Under (a) the sky pass and the clear
  colour get no fog, so a fogged street meets a clear horizon. Recommend
  leaving it to ⑦, which can judge against Bistro's own reference image;
  fogging the sky shader later is a small, separate change.
- **Q5 — the parameter set.** Recommend density, height, falloff, colour
  and maximum opacity. Filament's start and cut-off distances,
  in-scattering and IBL colour are left out as unneeded for Bistro.
- **Q6 — non-PBR shaders unfogged.** Recommend yes. No Bistro material
  imports as `lit_textured` or `unlit_textured`.
- **Q7 — the colour range.** Recommend HDR `[0, 65504]`, like emissive, so
  a lamp-lit haze can exceed 1 and reach ⑥'s bright-pass. The
  alternative is `[0, 1]`, a pure tint.
- **Risk — golden movement.** Treated as a stop-and-report gate (R3).
- **Risk — uninitialised tail.** A writer that forgets `FogData` would
  read garbage density. R5 makes writing it mandatory; the Plan
  enumerates the 16 writers.
- **Risk — cost under heavy overdraw.** Bistro's foliage overdraws. If ⑦
  measures a real cost, a depth pre-pass or option (b) are the levers —
  Out of Scope here.

## Out of Scope / Future Work

- Volumetric fog, light shafts, local fog volumes, noise.
- A fogged sky and horizon (Q4).
- Fog on `lit_textured`/`unlit_textured`.
- Start and cut-off distances, IBL-derived fog colour, in-scattering.
- ⑥ Bloom's bright-pass behaviour with fogged radiance.
- Bistro's tuned fog values (⑦).
