# Spec: Bistro Finale — Scene Assembly, Whitelist and Dual-Platform Verification

- **Status:** Draft
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Created:** 2026-09-25
- **Related Plan(s):** none yet
- **Approval:** pending
- **Related ADR(s):**
  [ADR-0094](../adr/0094-imported-scene-assembly-build-step-and-authored-overlay.md) (`Proposed`) —
  how an imported, fetched-content scene becomes a cooked, whitelisted,
  runnable scene, and how its hand-authored nodes are merged in;
  [ADR-0095](../adr/0095-goldens-over-pinned-fetched-content.md) (`Proposed`) —
  an ADR-0042 golden over SHA-256-pinned, fetched, never-committed content.
  A third ADR (the emissive texture binding, extending ADR-0089) is owed
  only if Q4 is ruled "add it"; see Risks & Open Questions.

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).

## Summary

Render Bistro. [Spec 0036](0036-bistro-parity-roadmap.md) workflow ⑦
joins every earlier workflow — the glTF importer (①), uint32 indices
(①b), mip chains (①c), 64 point lights (②), emissive (③), transparency
(④), height fog (⑤), bloom (⑥), on BC7 (⓪) — into one street-at-night
scene, adds it as the fifth `--scene` whitelist entry, and verifies it on
Windows (regression + an Initial-baseline golden) and Android (a
temporary scene switch), then against Filament's `example_bistro1.jpg`
by eye ("神似而非像素级"). The work is mostly assembly, but three pieces
of it are new: the build has no way to declare a 1062-asset imported scene
from fetched content; Bistro has no lights, camera, fog or bloom of its
own, so those are authored in a small committed overlay the importer merges
in; and a golden over fetched content needs its own ruling.

## Motivation / Problem Statement

Spec 0036 ⑦ (`0036-bistro-parity-roadmap.md:611-640`) is the contract.
It says to reuse the Spec 0028, Plan 0034 M6 and Plan 0035 M6 patterns and
invent no new verification pattern, and its verification row (`:891`)
requires a full Windows Debug+Release regression, a new Bistro golden
(ADR-0042 Initial baseline), Android verification by a temporary
`BootstrapConfig` scene switch (screencap, logcat, confirmed revert, the
inherited emulator Validation-Layer gap) and a one-time human review
against `example_bistro1.jpg`/`example_bistro2.jpg`. Its ADR obligation is
"expected none … confirmed at that Spec's own drafting time" (`:635-640`,
`:839`). **Drafting finds two** — see Architectural Impact.

### Current state (read at line level, 2026-09-25, `origin/main` 7223cd3)

1. **What the importer produces, and why nothing can declare it.**
   - `importGltf()` writes one output directory
     (`import_command.cpp:390-575`): 551 `.amesh` schema-5 **artifacts**
     already (`<name>_mesh_<i>_<j>.amesh` + `.meta.txt`, logical path
     `meshes/<name>/mesh_<i>_<j>`, `scene_import.cpp:125-127`), 254
     material **sources** (`<name>/materials/<i>.material.txt`), one
     scene **source** (`<name>/<name>.scene.txt`, v6, `active_camera: none`,
     `scene_import.cpp:103-104`, `:281-293`), `cook_manifest.txt` (one
     cooker invocation per line — textures, materials, scene — with
     `{content_parent}`/`{import_dir}`/`{cooked_dir}` placeholders,
     `:540-551`), `asset_list.txt` and `import_report.txt`.
   - Bistro, measured by the standing end-to-end test
     (`bistro_end_to_end_tests.cpp:117-121`, `:258`): 551 meshes, 254
     materials, 257 referenced textures (256 DDS + the importer's white
     fallback), 5908 scene nodes of which 2909 are renderables; 1062
     declared assets. That test cooks only a sample, through `std::system`
     per manifest line (`:154-173`).
   - The Runtime resolves a scene's references through a dependency
     manifest: one `logicalPath\tartifactPath\tmetadataPath` line per
     asset (`src/asset_system/CMakeLists.txt:126-209`). The only producer
     is `atlantis_add_scene_asset()`, which builds it from dependencies
     each declared by its own `atlantis_add_*_asset()` call, all rooted at
     `${CMAKE_SOURCE_DIR}/assets` (`:153-156`). Bistro's sources live in
     the fetched, never-committed `content/bistro/` and the importer's
     output — 1062 per-asset declarations do not fit that mechanism.
   - The whitelist is a fixed `std::array<SceneWhitelistEntry, 4>`
     (`src/runtime/main.cpp:48-63`, `cli.h:25-84`).
2. **Lights and camera.** The glTF has no `KHR_lights_punctual` and no
   camera (Spec 0037; re-measured). Its **23** materials with emissive data
   cover **118** instances: 10 factor-only (60 instances — the string
   lights at factor 8–20 in five colours, bare bulbs at 30–40), 11 with
   an emissive texture and a non-zero factor (55 — the 28 streetlight
   `bulb`s at 100, lanterns, wall and ceiling lamps, four shop signs), 2
   with a texture and a zero factor (3). The scene grammar and Runtime cap
   at 1 directional + 64 point lights (`scene_types.h:83`,
   `scene_extraction.h:73`). The importer still enforces Spec 0019's old
   1 + **4** (`scene_import.cpp:156`) — stale since Spec 0040, harmless
   today only because Bistro has none.
3. **Fog and bloom.** Scene v6 carries both on the camera node
   (`fog=<d> <h> <k> <m> fog_color=<r> <g> <b>`, `bloom=<s> <t>`);
   `fog_color` may be HDR (`kFogColorMax = 65504`, `scene_types.h:61`).
   Exposure compensation is a camera token too (Spec 0031).
4. **Cutout, blend, transmission.** 20 `MASK` (163 instances) and 3
   `BLEND` decals already import (Spec 0042). The **18 transmission**
   materials (339 instances) import `OPAQUE` — alpha 1.0,
   `transmissionFactor` 0.66–0.95. 252 of those instances are liquor
   bottles; **28 are the streetlight globes (material 214) that enclose
   the 28 `bulb`s** — opaque, they hide every streetlight bulb. ADR-0083
   (Accepted) already decided "transmission imports as `alphaMode: BLEND`,
   the closest available approximation" (`0083-…:143-153`); Spec 0042 did
   not apply it because glTF alpha is 1.0 (a BLEND at alpha 1 is opaque)
   and left the value to ⑦ (ruling O3).
5. **Emissive textures.** Spec 0041 maps the 10 factor-only materials and
   drops the 11 textured ones (ruling O3). Their emissive images are
   separate masks (`*_emi.dds`, `*_emt.dds`) — only `bakery` reuses its
   base-colour image — so they cannot be approximated through the
   existing base-colour binding. Today the 28 streetlight bulbs, the
   lanterns, the lamps and every shop sign render with no emission.
6. **Verification carriers.**
   - Goldens are 512×512 fixture renders (ADR-0042). The only existing
     Bistro GPU test explicitly declines to be a golden: "the frame
     depends on content fetched from an upstream repository, which is not
     a reference environment ADR-0042 goldens may rest on"
     (`bistro_large_mesh_gpu_tests.cpp:10-12`). The fetch is, however,
     pinned per file by SHA-256 and size (`fetch_bistro.ps1:56-60`,
     `tools/content/bistro_source.provenance.txt`).
   - Android packages every asset as a listed file in the APK and copies
     it out at start-up (`android/app/build.gradle:30-54`,
     `android_main.cpp:44-91`); BC7 on real Android devices is Spec 0038's
     disclosed gap (`0038-…:244-249`).

### Scale (measured 2026-09-25 from `content/bistro/bistro.gltf`)

| Quantity | Value |
|---|---|
| Draw items (mesh nodes × primitives) | 2909 (× 2 with the shadow pass) |
| Vertices / indices (before the tangent split) | 1,738,262 / 5,260,890 — ~120 MiB of `.amesh` |
| Textures bound by materials | 256 DDS + 1 fallback |
| Their BC7 bytes, base mips | 1,114.4 MiB (1.169 GB) |
| … with full mip chains (Spec 0045) | **1,485.9 MiB (1.558 GB)** |
| Emissive masks (only if Q4 = add) | +9 textures, +21.8 MiB |
| Peak process memory at realization (CPU copy + staging + device) | ≈ 3 × 1.45 GiB, all at once (Phase 1 loads synchronously) |

## Goals

- `atlantis_runtime --scene bistro` renders the assembled night street on
  Windows; Android renders it under a temporary scene switch.
- Every earlier workflow is exercised in one frame: imported u32 meshes,
  BC7 with mip chains, up to 64 point lights, emissive, cutout, blend,
  fog, bloom.
- A Bistro golden guards the assembly from regressing.
- The one-time "神似" review against `example_bistro1.jpg` is recorded
  in the implementation PR.

## Non-Goals

- Pixel-level reproduction of Filament's frames (Spec 0036's bar).
- Performance work: no culling, LOD, instancing, batching or streaming;
  2909 draws per pass are accepted as they are.
- Per-light shadows (point lights stay unshadowed), SSR / wet reflections,
  refraction or physical transmission (Spec 0036 pins these out).
- Photometric units: intensities stay unitless; ⑦ records what it needed
  for the future photometric Spec (Spec 0036 ②, `:406-410`).
- A new default scene: Bistro is a whitelist entry, not the default.
- Committing any Bistro content, cooked artifact, or Filament image.

## Requirements

### Functional

- **R1 — Assembly build step (ADR-0094).** When `content/bistro/` is
  present at configure time, the build imports Bistro, merges the
  authored overlay, cooks textures, materials and the scene, and writes
  the Runtime dependency manifest — as one build step with one stamp,
  into the build tree. Without the content, configure succeeds and the
  step and the whitelist entry are absent.
- **R2 — Authored overlay (ADR-0094).** A committed
  `assets/bistro/bistro_overlay.scene.txt` (scene v6 grammar, no new
  syntax) holds the camera (exposure, `fog=`, `bloom=`), one directional
  light and ≤ 64 point lights. The importer merges it into the imported
  scene; the result is the scene the Runtime loads. The importer's light
  cap follows the scene grammar's (1 + 64).
- **R3 — Whitelist.** `bistro` is the fifth `--scene` entry (Spec 0032's
  closed-whitelist pattern), present exactly when R1's step exists.
- **R4 — Transmission (Q3).** A transmission material imports as `Blend`
  with alpha `1 − transmissionFactor` (times base-colour alpha) —
  ADR-0083's accepted mapping, with its missing value supplied.
- **R5 — Emissive textures (Q4).** Per the ruling: either the 11 textured
  emissive materials sample their mask (`factor × texture`), or they keep
  Spec 0041's drop and the look relies on R2's lights.
- **R6 — Verification** as in Testing & Verification Plan.

### Non-functional

- **Memory:** as in the Scale table. The Windows reference machine (Intel
  Arc B370, shared system memory) must hold ≈ 4.4 GiB at the realization
  peak; the Plan measures it.
- **Build time:** R1's step reads ~2.1 GB and writes ~1.6 GB once per
  change of the glTF, the overlay or the tools; never on an unrelated
  build.
- **Frame time:** reported, not gated.
- **Portability:** Vulkan-only, unchanged. Android BC7 remains Spec 0038's
  disclosed gap (see Q6).

## Proposed Design

- **(a) Declaration — one build step per imported scene (ADR-0094).** A
  CMake function (`atlantis_add_imported_scene`, name in the Plan) adds
  one custom command: run `atlantis_gltf_importer` with `--overlay`, then
  one new `atlantis_asset_cooker` mode that executes the import's own
  `cook_manifest.txt` in-process (257 textures, 254 materials, the scene)
  and writes the dependency manifest — every declared asset's cooked
  artifact, meshes pointing into the import directory. Recommended over
  generating 1062 `atlantis_add_*` calls at configure time (configure-time
  cost, a 1000-target graph, and assets rooted outside `assets/`) and
  over an out-of-build script (not reproducible from a clean build).
- **(b) Lights — an authored overlay merged by the importer.** Point
  lights at the emitters, one directional "moon", the camera with exposure,
  fog and bloom. Budget, from the emitter census: 28 streetlights, ~7 wall
  lights, ~7 lanterns, 4 ceiling lamps, the string lights grouped to ~12,
  a few singles — **≈ 60 of 64**. Recommended over editing the imported
  scene (regenerated on every import), over injecting into the glTF
  (breaks its pinned hashes) and over Runtime-side code (unreviewable,
  scene-specific). The overlay is plain scene text: every light is one
  reviewable line.
- **(c) Transmission as blend (R4).** Makes the streetlight globes, window
  glass and bottles see-through with a faint tint; no refraction.
- **(d) Emissive textures (R5, Q4).** Recommended: add — one always-bound
  emissive slot in the PBR pipelines with a 1×1 white default, so
  `factor × texture` reproduces every existing factor-only material
  exactly; a material schema v9 field; importer maps the 11. The
  alternative avoids a schema change but leaves the most common light
  source in the scene dark unless hand lights imitate it.
- **Initial look values (tunable in ⑦, recorded in the PR):**
  - exposure compensation −1 EV;
  - fog: density 0.015, height 0 (street level), falloff 0.15, max
    opacity 0.7, colour (0.25, 0.20, 0.14) — warm, below the bloom knee so
    the haze itself does not glow;
  - bloom: strength 0.15, threshold 1.5 (bulbs at 8–100 and lamp-lit
    surfaces exceed it);
  - moon: direction from above and behind the camera, colour (0.6, 0.7,
    1.0), intensity 0.1;
  - point lights: warm white (1.0, 0.8, 0.55) for lamps and bulbs, each
    string light's own colour; intensities set in the review loop.
- **Golden (ADR-0095):** 512×512 through the dark PBR fixture
  (`PbrNormalMapDemoFixture`, which realizes materials through the
  Runtime path and already carries lights, fog and bloom — the Plan
  confirms blend and cutout on it), camera from the overlay; content-gated — SKIP
  when `content/bistro/` is absent, exactly as today's `[bistro]` tests.
  The human review uses a full-window Runtime capture, not the golden.
- **Android:** the cooked Bistro set is pushed with `adb` into the app's
  external files directory and a temporary `BootstrapConfig` switch
  points at it (the Plan 0034/0035 M6 pattern — not APK-packaged);
  screencap + logcat; switch reverted and the revert confirmed.

## Architectural Impact

Yes — beyond Spec 0036's expectation of none:

- **ADR-0094** — a new build mechanism (a CMake function and a new
  `atlantis_asset_cooker` mode that executes an importer's cook manifest
  and emits a dependency manifest) and a new importer option
  (`--overlay`) with its merge rule; the importer's light cap moves to the
  scene grammar's.
- **ADR-0095** — extends ADR-0042: a golden may rest on fetched content
  that is pinned per file by SHA-256, provided the test SKIPs when the
  content is absent and the sidecar records the content pin.
- **Conditional (Q4 = add):** an emissive-texture ADR extending ADR-0089
  (material schema v9, one always-bound slot in ten PBR pipelines).

R4 needs no ADR: it applies ADR-0083's accepted transmission decision and
fills in the value it left open.

## Alternatives Considered

- **Declaration:** configure-time generated per-asset calls; a PowerShell
  cook script outside the build; committing cooked Bistro. In ADR-0094.
- **Lights:** hand-editing the imported scene; glTF-side injection;
  Runtime hard-coding; emissive-only (no point lights — surfaces would
  glow but light nothing). In ADR-0094.
- **Transmission:** keep opaque (hides the streetlight bulbs); a new
  transmission model (pinned out by Spec 0036).
- **Emissive:** add the texture; factor-only for the textured 11 (bulbs
  right, signs solid glowing plates); keep dropping.
- **Golden:** no golden (a non-degeneracy check only, the
  `bistro_large_mesh` precedent) — fails Spec 0036's contract row. In
  ADR-0095.

## Testing & Verification Plan

Mapped to Spec 0036 ⑦'s row (`:891`); nothing new is invented — Spec 0028
(scene assembly), Plan 0034 M6 and Plan 0035 M6 (Android) are the
patterns.

- **GPU-independent:** overlay merge (node renumbering, active camera,
  light cap 64, rejection past it); the cooker's manifest mode (every line
  executed, placeholders substituted, the dependency manifest lists every
  declared asset exactly once); transmission alpha mapping; (Q4 = add)
  emissive texture schema round-trip and importer mapping.
- **GPU (content-gated `[bistro]`):** the assembled scene loads through the
  Runtime path, every renderable resolves, Validation Layers clean; the
  Bistro golden (Initial baseline) with discriminators — fog off, bloom
  off, and point lights off must each fail against it.
- **Regression:** full Windows Debug and Release, every existing golden
  byte-identical (R4 and R5 change only imported Bistro materials; R5's
  default slot keeps factor-only materials exact).
- **Android:** the temporary switch, screencap + logcat, revert confirmed;
  the emulator Validation-Layer gap inherited.
- **Human review:** a full-window Windows capture beside
  `example_bistro1.jpg` (and `example_bistro2.jpg`), "神似而非像素级",
  recorded in the PR with the final look values.

## Risks & Open Questions

- **Q1 — The declaration mechanism.** Recommend (a): one build step, a
  cooker manifest mode, the dependency manifest generated.
- **Q2 — Lights by committed overlay, merged by the importer.** Recommend
  (b), ≈ 60 point lights + 1 directional, with the importer cap raised to
  the grammar's 64.
- **Q3 — Transmission as blend, alpha `1 − transmissionFactor`.**
  Recommend yes: ADR-0083 already chose blend; without it every
  streetlight bulb is hidden.
- **Q4 — Emissive textures.** Recommend **add** (d): 55 of 118 emissive
  instances need it, including the scene's most numerous light source.
  Size M; owes one ADR (extends ADR-0089), drafted with the approval if
  ruled so. Fallback if not: factor-only for the textured 11.
- **Q5 — A golden over pinned fetched content (ADR-0095).** Recommend yes,
  content-gated; `bistro_large_mesh`'s comment is superseded.
- **Q6 — Android content and BC7.** Recommend `adb push` + temporary
  switch. The emulator's BC7 support is unmeasured: the Plan probes it
  first; if unsupported, the Android run records the failure mode and the
  gap stays Spec 0038's, not closed here.
- **Q7 — Whitelist entry only when content is present.** Recommend yes,
  so `--list-scenes` never offers a scene that cannot load.
- **Risk — memory.** ≈ 4.4 GiB peak on a shared-memory iGPU; if it fails,
  the fallback is freeing CPU texture copies after upload (a Plan-level
  change, no format impact).
- **Risk — look.** Initial values are estimates; the review loop may take
  several passes. The golden is captured only after the human review.
- **Risk — build time.** Once per content/overlay/tool change; the Plan
  measures it.

## Out of Scope / Future Work

- The photometric Spec (units, attenuation) — informed by ⑦'s lights.
- Texture streaming, upload budgeting, culling, LOD.
- Point-light shadows; transmission/refraction; SSR.
- ASTC or other mobile block formats (Spec 0038's gap).
- Making Bistro the default scene.
