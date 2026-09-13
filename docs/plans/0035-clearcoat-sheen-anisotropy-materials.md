# Plan: Clearcoat, Sheen, and Anisotropy PBR Materials

- **Spec:** [Spec 0035: Clearcoat, Sheen, and Anisotropy PBR Materials](../specs/0035-clearcoat-sheen-anisotropy-materials.md) (`Approved`) — [ADR-0081](../adr/0081-pbr-material-brdf-extension-clearcoat-sheen-anisotropy.md) (`Accepted`)
- **Status:** Approved
- **Author:** slmao (drafted by Claude Code at explicit human direction)
- **Joint Human Review:** slmao, 2026-09-13 — Spec 0035 and this Plan
  reviewed together (basis: [PR #159](https://github.com/slmao/Atlantis/pull/159)'s
  documentation set plus that same day's chat session) and Implementation
  explicitly authorized. This round's three open points were confirmed
  as drafted, not overridden: three incremental schema bumps (v4/v5/v6,
  one per BRDF slice, not one batched bump — Sequencing & Dependencies);
  Milestone 6's temporary `android_main.cpp` scene-switch on-device
  verification mechanism (captured, then reverted, disclosed in that
  Milestone's own PR); and the Clearcoat → Sheen → Anisotropy risk
  ordering (cheapest push-constant budget first, highest shading-math
  risk last).

Authoring/lifecycle rules: [AGENTS.md](../../AGENTS.md#documentation-and-code-comments).
Describe ordered changes, file scope, and verification. Keep complete source
files, function implementations, candidate diffs, and exhaustive test inventories
in code/PRs; use small pseudocode or normative layouts only to resolve ambiguity.
Link Spec requirements and ADR decisions instead of restating them.

## Objective

Implement Spec 0035's three new BRDFs (`PbrClearcoat`, `PbrSheen`,
`PbrAnisotropic` — ADR-0081) end-to-end (asset schema → cooker → loader →
push constants → shaders → goldens), then assemble and ship the Spec's
own IBL-only showcase scene and its Spec-0032 whitelist entry, verified
on both Windows and Android per Plan 0034's own established protocol.

## Pre-drafting reading (cited, not restated)

Real, existing machinery this Plan's own milestones below build directly
on — each claim below was confirmed by reading the actual code, not
assumed:

- **Material schema versioning is exact-equality, hard-reject, no
  dual-version reader.** `kMaterialArtifactSchemaVersion = 3`,
  `src/asset_system/include/atlantis/asset_system/material_artifact.h:46-47`;
  `decodeMaterialArtifact()`'s version check is `schemaVersion !=
  kMaterialArtifactSchemaVersion` → `UnsupportedSchemaVersion`, and any
  byte-size other than the current fixed header size is separately
  rejected (`UnexpectedSize`) — `src/asset_system/src/material_artifact.cpp:98-116`.
  Both real prior bumps (v1→v2, commit `f68234b`, ADR-0066; v2→v3,
  commit `b9fe39d`, ADR-0074) **re-authored every existing
  `.material.txt` file to the new version line** rather than keeping a
  reader for the old version — there is no precedent in this codebase
  for a dual-version-tolerant loader. This Plan's schema-bump milestones
  below follow that same, established pattern.
- **The cooker reads hand-written `.material.txt` intermediate files**,
  parsed by `parseMaterialSource()`
  (`src/asset_system/src/material_source.cpp:98-200`, version line
  `atlantis_material_source_version: 3`, `:11`), invoked via
  `atlantis_asset_cooker --kind=material --source=<path>`
  (`src/tools/asset_cooker/cook_command.cpp:375-394`). Adding a kind
  means: a new `kind:` string literal in `material_source.cpp` (mirrors
  `:21-23,125-133,209-215`), new `<field>_factor:`-style prefix lines
  for that kind's own scalars (mirrors `kBaseColorFactorPrefix`/
  `kMetallicFactorPrefix`, `:16-18`), a new `cookMaterial()`
  range-validation branch (`src/asset_system/src/cook_material.cpp`),
  and a new arm in `cook_command.cpp:303-319`'s
  `materialCookErrorMessage()` switch — exactly the shape both prior
  bumps' own commits already establish.
- **CMake asset-declaration macros**, all real, all with existing usage
  to model from: `atlantis_add_static_mesh_asset` (mesh,
  `src/asset_system/CMakeLists.txt:~60`, e.g. `assets/CMakeLists.txt:17-20`),
  `atlantis_add_material_asset` (material, incl. optional `NORMAL_MAP`,
  `src/asset_system/CMakeLists.txt:346-404`, e.g.
  `assets/CMakeLists.txt:369-374`), `atlantis_add_environment_asset`
  (HDRI, `NAME`/`SOURCE` only, `src/asset_system/CMakeLists.txt:308-337`,
  e.g. `assets/CMakeLists.txt:315-318`), `atlantis_add_scene_asset`
  (scene, `NAME`/`SOURCE`/`MESH_DEPENDENCIES`/`MATERIAL_DEPENDENCIES`/
  `TEXTURE_DEPENDENCIES` — every dependency must already be declared,
  fatal-errors otherwise — `src/asset_system/CMakeLists.txt:137-197`,
  e.g. `assets/CMakeLists.txt:286-296`). Each declaration re-exports
  `ATLANTIS_<NAME>_..._PATH`/`_TARGET` variables via `PARENT_SCOPE`.
- **Environment cooking hard-requires `.hdr` (Radiance) source.**
  `src/tools/asset_cooker/cook_command.cpp:59` fixes
  `kEnvironmentAuthoringExtension = ".hdr"` and rejects anything else
  (`:343-345`); decoding is `stbi_loadf()` Radiance-HDR loading. A
  `.exr`-sourced CC0 HDRI needs conversion to `.hdr` before cooking —
  Milestone 1's own content-procurement step must account for this, not
  discover it mid-Milestone-5.
- **`atlantis_add_slang_shader_pair()`** (`src/shader_system/CMakeLists.txt:62-86`,
  `NAME`/`SOURCE`/`VERTEX_ENTRY`/`FRAGMENT_ENTRY`/`OUTPUT_DIR`/
  `EXPECTED_CONTRACT`, e.g. `shaders/pbr_ibl/CMakeLists.txt:1-10`) is the
  real macro each of the 6 new shader pairs (Requirement 4) is declared
  with. `selectShaderPair()` (`src/runtime/src/material_realization.cpp:101-142`)
  and `sampledTextureBindingCountFor()` (`:148-171`) are the two real,
  shared closed switches each new `MaterialKind` gains an arm in,
  `hasNormalMap`-nested exactly like the existing `PbrDirectLit` arm
  (`:129-139`).
- **`.ascene` is cooked from a hand-written `.scene.txt`** — flat,
  line-oriented `key: value`/`node: key=value...` grammar (real example:
  `assets/scenes/ibl_material_demo.scene.txt`, `atlantis_scene_source_version: 4`,
  one `node:` line per entity — a ~30-sphere scene is ~30 more such
  lines, no new grammar). `integrated_showcase_demo.scene.txt` shows the
  `light`- and `ground_plane`-mesh-node precedent this Plan's own
  showcase scene needs (minus the `light` node — zero `Light` entities,
  Spec 0035 Requirement 5).
- **No sphere-mesh generator tool exists in this repo.**
  `assets/meshes/pbr_sphere.mesh.txt` (the mesh `ibl_material_demo`
  already uses) is a 1196-line, hand-checked-in flat
  `vertex: px py pz r g b u v nx ny nz`-per-line ASCII file
  (`atlantis_static_mesh_source_version: 3`), evidently produced once by
  an external/throwaway script, matching this repo's own established
  precedent for `ibl_studio_source.hdr`
  (`assets/environments/generate_ibl_studio_source.ps1`, explicitly
  commented "never a permanent generator target"). **Correction to Spec
  0035's own drafting-time framing**: authoring a dimpled sphere is real,
  non-trivial scripting work (write a throwaway generator emitting the
  same 11-field-per-vertex grammar with a spherical-cap recess) — not
  the "geometrically trivial" characterization Spec 0035's Proposed
  Design used. Still zero licensing question (this Plan's own point
  stands), just not zero effort; Milestone 1 below sizes it honestly.
- **`--scene` whitelist is a real, fixed-size `std::array`.**
  `src/runtime/main.cpp:44-56` builds
  `std::array<SceneWhitelistEntry, 3>`; `tests/runtime/cli_tests.cpp:43-63`
  builds a matching test fixture. A 4th entry means: bumping both arrays
  to size 4, one new `SceneWhitelistEntry` row in each, three new
  `ATLANTIS_RUNTIME_<NAME>_SCENE_{ARTIFACT,METADATA,MANIFEST}_PATH`
  macros in `src/runtime/CMakeLists.txt:167-173`'s own pattern, and a new
  `TEST_CASE` in `cli_tests.cpp` mirroring `:112-117`/`:119-124`.
- **Image-regression CMake pattern**: a new `<scene>_gpu_tests.cpp`
  added to `atlantis_image_regression_gpu_tests`'s source list
  (`tests/image_regression/CMakeLists.txt:45-55`), scene/shader/
  environment path macros as `target_compile_definitions`
  (`:164-182`'s own pattern), a **separate** golden-generator executable
  per scene (`tests/image_regression/golden_generator/CMakeLists.txt:249-281`,
  its own `add_dependencies` on the scene's and environment's CMake
  targets, its own `*_SOURCE_SHA256`/`*_ARTIFACT_SHA256` provenance
  macros), goldens at `tests/image_regression/goldens/<scene-slug>/`.
  Exact pixel match, channel tolerance 0 (`docs/process/testing-strategy.md`).

## Milestones / Task Breakdown

1. **Content procurement** (independent — see Sequencing). Source CC0
   PBR texture sets (metal, wood, stone, tile/mosaic, carbon-fiber-weave
   — matching Spec 0035's own reference-image material variety), **one
   weathered-concrete/asphalt ground-plane PBR texture set (CC0, same
   provider class as the rest)**, matching Spec 0035 Requirement 6's own
   "rough, granular gray ground plane" description, and one CC0
   warehouse-interior HDRI, from Poly Haven/ambientCG-class providers,
   **in `.hdr` (Radiance) form** for the environment (convert from
   `.exr` if needed — see Pre-drafting reading). Record a provenance/
   attribution sidecar per imported asset, matching this repo's own
   already-established `<name>_source.provenance.txt` precedent
   (`assets/environments/ibl_studio_source.provenance.txt`) — Filament's
   own analogous `URL.txt` practice independently reaches the same idea,
   cited in Spec 0035 as precedent, not as the literal filename
   convention this repo actually uses. Author **both the dimpled test-
   sphere mesh and a small pedestal/stand mesh** (Spec 0035 Requirement
   6's own "each on its own small pedestal-like base") from the same
   one-off generator script (uncommitted, per the
   `generate_ibl_studio_source.ps1` precedent), each emitting its own
   `pbr_sphere.mesh.txt`-shaped `.mesh.txt` — the sphere with a recessed
   spherical cap, the pedestal a simple turned/cylindrical profile — both
   satisfying Spec 0029's own mandatory-tangent mesh-artifact requirement
   identically. **Risk gate**: if no suitably-licensed CC0 texture/HDRI
   set is found for a given material category (including the ground),
   or either generated mesh fails `atlantis_asset_cooker`'s own
   mesh-cooking validation (tangent/normal generation), stop and report
   rather than substituting an unlicensed asset or silently relaxing
   either mesh's own tangent requirement.
2. **Clearcoat end-to-end slice.** `MaterialKind::PbrClearcoat` +
   material-asset schema **v3→v4** (new fields:
   `clearcoatFactor`/`clearcoatRoughness`, plus the field-parsing/
   cooking/error-message changes the Pre-drafting reading above
   itemizes) + `loadMaterialAsset()` support + `PbrClearcoatPushConstants`
   (96-byte base + this kind's own new scalars, confirmed via real
   Slang reflection **and** an MSVC `static_assert` layout probe —
   ADR-0067 D-3's own required method, not assumed) + 2 new `.slang`
   pairs (`pbr_clearcoat_ibl`, `pbr_clearcoat_ibl_normal_map`) + new
   arms in `selectShaderPair()`/`sampledTextureBindingCountFor()` + at
   least one clearcoat-parameter-sweep golden scene, both variants. This
   slice's own schema bump forces re-authoring every existing
   `.material.txt` to the `v4` version line (Pre-drafting reading) —
   accepted, disclosed cost; see Sequencing for why this is not batched
   with Sheen/Anisotropy's own fields. **Risk gate**: if the real,
   measured push-constant layout for `PbrClearcoatPushConstants` exceeds
   Vulkan's 128-byte guarantee even at this cheapest slice's ~104-byte
   estimate, that falsifies ADR-0081's own core budget premise — stop
   and report back to ADR-0081 for re-decision, do not silently widen
   past 128 or switch to a uniform buffer mid-implementation.
3. **Sheen end-to-end slice.** Same shape as Milestone 2:
   `MaterialKind::PbrSheen`, schema **v4→v5** (new fields:
   `sheenColor`/`sheenRoughness`), `PbrSheenPushConstants`, 2 new
   `.slang` pairs (`pbr_sheen_ibl`, `pbr_sheen_ibl_normal_map`), new
   switch arms, sheen-roughness-sweep goldens (both variants). **Risk
   gate** (the one ADR-0081's own Context table flagged as the likely
   tipping point): if `sheenColor`'s real vec3 alignment padding pushes
   this kind's own struct past 128 bytes, stop and report with the real
   measured numbers — do not silently repack `sheenColor` into three
   scalars or drop `sheenRoughness` without that being a reported,
   reviewed decision.
4. **Anisotropy end-to-end slice.** Same shape again:
   `MaterialKind::PbrAnisotropic`, schema **v5→v6** (new fields:
   `anisotropyFactor`/`anisotropyRotation`, no new vertex attribute — Spec
   0035's own confirmed finding), `PbrAnisotropicPushConstants`, 2 new
   `.slang` pairs (`pbr_anisotropic_ibl`, `pbr_anisotropic_ibl_normal_map`)
   implementing anisotropic GGX against Filament's own cited equations
   (Spec 0035 Non-functional), new switch arms, anisotropy-rotation-sweep
   goldens (both variants). **Risk gate** (highest shading-math risk of
   the three, per Spec 0035's own Risks section): a real GPU visual
   sanity check — the highlight must visibly elongate and rotate with
   `anisotropyRotation` in the expected direction — must pass *before*
   any golden is captured and committed as ground truth; a
   plausible-looking-but-wrong highlight captured as a golden would
   silently enshrine a shading bug future changes would then "correctly"
   match.
5. **Showcase scene assembly.** New `.scene.txt` (~25-30 `node:` lines,
   using Milestone 1's meshes/textures + Milestones 2-4's three new
   kinds + existing `PbrDirectLit` for the remaining spheres), new
   `.material.txt` files for every non-golden showcase material, new
   `atlantis_add_scene_asset()`/`atlantis_add_environment_asset()`
   CMake declarations, 4th `--scene` whitelist entry (`main.cpp`,
   `cli.h`/`cli_tests.cpp`, `src/runtime/CMakeLists.txt` macros — Pre-
   drafting reading), scene-level golden test + golden-generator target.
   **Risk gate**: if the ~30-material scene's real descriptor-set demand
   does not actually absorb cleanly into Spec 0021/ADR-0064's existing
   growable pool (Spec 0035's own Non-functional claimed this needs no
   new work) — stop and report; that would falsify a Spec-level claim
   this Plan relied on, not a Plan-level surprise to quietly work around.
6. **Android asset lock-step + dual-platform verification.** Add every
   new shader pair/texture this Plan's own Milestones 1-5 produced to
   both `android_main.cpp`'s literal `extractAsset()` list and
   `android/app/build.gradle`'s `namedAssetRelativePaths` list, human-
   cross-checked against each other per both files' own existing
   comments (Spec 0035 Requirement 8). On-device verification follows
   Plan 0034 Milestone 6's own established protocol exactly
   (`assembleDebug` → install → `am start` → sustained render →
   `screencap` → logcat summary), inheriting that same Plan's own
   disclosed "no Validation Layer coverage on this translation-layer
   emulator" gap (Spec 0035 Non-functional) — not re-litigated here.
   **On-device verification scene mechanism (human-directed):**
   `android_main.cpp` hard-builds one fixed `BootstrapConfig` (Plan 0034
   Milestone 5) — there is no Android-side `--scene` equivalent (Spec
   0032's own Non-Goals), and Spec 0035 itself leaves "does the showcase
   scene become Android's default boot scene" an explicit, undecided
   Non-Goal. To actually render the new showcase scene on-device for
   this Milestone's own verification (asset packaging/extraction alone
   does not confirm it renders correctly), this Milestone **temporarily**
   points `android_main.cpp`'s `BootstrapConfig` scene fields at the new
   showcase scene's own artifact/metadata/manifest paths — a verification-
   only change, not a product decision — captures its own evidence
   (`screencap`, logcat), then **reverts** `android_main.cpp` back to
   `integrated_showcase_demo` before this Milestone's own PR is opened.
   The temporary switch and its revert are both explicitly disclosed in
   that PR's own description, matching this repository's own established
   "diagnostic-only, must-revert, disclosed" pattern (Plan 0034 Milestone
   6's own precedent). Spec 0035's own Non-Goal — Android's real default
   boot scene remains `integrated_showcase_demo`, undecided by this
   Plan — is unchanged by this verification-only detour. Windows: full
   Debug + Release build, full `ctest`, zero real Validation Layer hits.
   **Risk gate**: any packaged asset present on one side of the Android
   lock-step but not the other is a stop-and-fix item before proceeding
   to on-device verification, not a "log and continue" — exactly the
   failure mode both files' own comments exist to prevent. A second risk
   gate: if `android_main.cpp`'s temporary scene switch is not fully
   reverted (confirmed via `git diff`/`git status` showing a clean tree
   on that field before the PR is opened, mirroring Plan 0034's own
   revert-confirmation discipline), stop and fix before opening the PR —
   an unreverted temporary switch would silently change Android's real
   product behavior, not merely this Milestone's own verification.

## Files / Modules Touched (expected)

**Asset System** (`src/asset_system/`): `include/atlantis/asset_system/material_types.h`
(3 new `MaterialKind` values, new field structs), `material_artifact.h`
(schema version 3→4→5→6, one bump per Milestone 2/3/4), `src/material_artifact.cpp`,
`src/material_source.cpp` (new `kind:`/field prefixes), `src/cook_material.cpp`
(new validation branches), `src/load_material.cpp` (if needed for new
field decoding), `CMakeLists.txt` (no new macro — existing
`atlantis_add_material_asset`/`atlantis_add_scene_asset`/
`atlantis_add_environment_asset`/`atlantis_add_static_mesh_asset` reused).

**Tools** (`src/tools/asset_cooker/`): `cook_command.cpp` (new
`materialCookErrorMessage()` arms; environment-cooking path unchanged).

**Renderer** (`src/renderer/`): new `src/pbr_clearcoat_push_constants.h`,
`src/pbr_sheen_push_constants.h`, `src/pbr_anisotropic_push_constants.h`
(mirroring `src/pbr_push_constants.h`'s own private-header precedent).

**Runtime** (`src/runtime/`): `src/material_realization.cpp`
(`selectShaderPair()`/`sampledTextureBindingCountFor()` new arms),
`main.cpp` (4th whitelist entry), `cli.h` (no shape change — reused),
`CMakeLists.txt` (new scene's compile-definition macros).

**Shader System** (`shaders/`): 6 new directories, each with one
`.slang` file + one `CMakeLists.txt` (`pbr_clearcoat_ibl[/_normal_map]`,
`pbr_sheen_ibl[/_normal_map]`, `pbr_anisotropic_ibl[/_normal_map]`).

**Assets** (`assets/`): `CMakeLists.txt` (new mesh/texture/environment/
material/scene declarations), new `materials/*.material.txt` (per-BRDF
golden sweeps + showcase materials), new `meshes/*.mesh.txt` (dimpled
sphere), new `textures/*` (CC0 texture set + provenance sidecars), new
`environments/*.hdr` (+ provenance sidecar), new `scenes/*.scene.txt`
(showcase scene).

**Tests**: `tests/runtime/cli_tests.cpp` (4th whitelist entry test),
`tests/image_regression/CMakeLists.txt` + new
`*_clearcoat_gpu_tests.cpp`/`*_sheen_gpu_tests.cpp`/
`*_anisotropic_gpu_tests.cpp`/`*_materials_showcase_gpu_tests.cpp`,
`tests/image_regression/golden_generator/CMakeLists.txt` + 4 new golden-
generator executables, `tests/image_regression/goldens/<new-scene-slug>/`
(new golden PNGs + sidecars — Initial-baseline category, ADR-0042
Amendment).

**Android**: `src/runtime/android/android_main.cpp` (literal asset
list), `android/app/build.gradle` (`namedAssetRelativePaths`).

### Not-touched (stop and report if this Plan's own implementation needs
to touch any of these — an unplanned architectural expansion, not a
Plan-stage decision)

- **RHI public API** (`src/rhi/include/`) — no new abstraction; new
  pipelines/shaders are Vulkan Backend implementation detail, exactly
  like every prior `PbrDirectLit` shader variant.
- **RenderGraph** (`src/render_graph/`) — no new pass type; new BRDFs
  are a fragment-shader concern within the existing per-object draw path.
- **Atlantis Platform** (`src/platform/`) — no windowing/input change.
- **Atlantis World** (`src/world/`) — `Renderable`/`Transform`/`Light`
  component shapes unchanged; World only ever sees an opaque `AssetId`
  for a material, never `MaterialKind` itself (AGENTS.md's own
  established World↔AssetSystem boundary).
- `PbrDirectLit`'s own existing push-constant layout, shader pairs, and
  material-asset fields (`UnlitTextured`/`LitTextured` likewise) — this
  Plan's schema bumps change the *version number* every existing asset
  must re-declare, but change none of these three kinds' own fields or
  behavior.

## Sequencing & Dependencies

**Milestone 1 runs fully in parallel with Milestones 2-4** — content
procurement (CC0 textures/HDRI, dimpled-sphere mesh) touches an entirely
disjoint file set (`assets/textures/`, `assets/environments/`,
`assets/meshes/`) from the BRDF slices' own asset-system/renderer/
shader-system files, and Milestone 1 is not itself gated on any BRDF
landing first. Milestone 1 **is** a hard prerequisite for Milestone 5
(the showcase scene needs both the content and the three BRDFs).

**Milestones 2 → 3 → 4 are sequential, not parallel**, despite each
being a logically independent "BRDF slice" — reasoning, not assumption:
all three land a new arm in the *same* five shared switch/enum sites
(`MaterialKind` enum, `material_source.cpp`'s kind-string switch,
`cook_command.cpp`'s error-message switch, `selectShaderPair()`,
`sampledTextureBindingCountFor()`), so concurrent branches would collide
on every one of those five files. Sequential execution avoids repeated
rebase/merge friction on shared switches. Order (Clearcoat → Sheen →
Anisotropy) matches ADR-0081's own byte-budget risk ordering (cheapest
first) and Spec 0035's own Risks ordering (anisotropy's shading-math
risk is highest, tackled last, after the schema/cooker/pipeline
extension pattern has already been proven twice).

**Each slice gets its own schema-version bump (v4, v5, v6) rather than
one batched v3→v4 bump covering all three kinds' fields up front.**
Considered and rejected: a single batched bump would mean Milestone 2
declares schema fields for `PbrSheen`/`PbrAnisotropic` before those
kinds exist in code at all — speculative schema surface ahead of real
need, which AGENTS.md's own "No speculative abstraction" architecture
principle counsels against, even though it would mean the "re-author
every `.material.txt`" chore (Pre-drafting reading) happens once instead
of three times. This Plan accepts the 3x mechanical-re-authoring cost
deliberately, matching this codebase's own real, established precedent
(both prior bumps added exactly the fields one landing needed, never
more) over a batched-but-speculative alternative — flagged here
explicitly for Human Review to confirm or override.

**Milestone 5 depends on 1 and on all of 2-4** (needs the content and
all three kinds to assign at least one showcase sphere each — Spec 0035
Requirement 6). **Milestone 6 depends on 5** (nothing to package/verify
on-device until the showcase scene and every new shader/texture exist).

## Verification Checklist

Mapped to Spec 0035's own Testing & Verification Plan:

- [ ] **Per-BRDF GPU/image-regression goldens** — at least 6 (Spec 0035
      Requirement 4/Testing Plan: `_ibl` and `_ibl_normal_map` variant ×
      3 new kinds), each a parameter sweep, exact-pixel-match golden
      compare, Vulkan Validation Layers clean (Windows). Milestones 2-4.
- [ ] **Showcase-scene golden** — 1 new test, the full ~25-30-sphere
      scene, plus a human visual-comparability review against Filament's
      `example_materials1.jpg` (not a pixel diff — Spec 0035's own
      explicit non-goal). Milestone 5.
- [ ] **`--scene` whitelist GPU-independent test** — 4th entry's
      name-to-path mapping, extending `cli_tests.cpp`'s existing pattern.
      Milestone 5.
- [ ] **Android on-device verification** — Plan 0034 Milestone 6's own
      protocol, disclosed Validation-Layer-coverage gap inherited, not
      re-decided. **The new showcase scene must actually be rendered on
      a real device/emulator** (via Milestone 6's own temporary
      `android_main.cpp` scene-field switch, captured and reverted, not
      left in place) — asset packaging/`extractAsset()` success alone is
      not sufficient evidence for this item; a scene that extracts
      cleanly but was never actually instantiated/rendered on-device does
      not satisfy this checklist item. Milestone 6.
- [ ] **Windows regression** — full Debug + Release build, full `ctest`,
      zero real Validation Layer hits. Milestone 6 (and after every
      Milestone 2-5 landing, per AGENTS.md's "build and test after every
      implementation step" rule, not only before the final PR).
- [ ] **Android/Windows asset lock-step** — `android_main.cpp` literal
      list and Gradle's `namedAssetRelativePaths` reviewed against each
      other by a human for every new asset. Milestone 6.
- [ ] **Push-constant layout confirmation** — real Slang reflection +
      MSVC `static_assert` probe for each of the 3 new structs (ADR-0067
      D-3 method), recorded in that Milestone's own PR. Milestones 2-4.

## Rollback Plan

**Per-milestone rollback** (2-6): each milestone is its own PR; reverting
one PR reverts that milestone's own files cleanly given the sequential
dependency chain above — reverting Milestone 3 (Sheen) before Milestone
4 (Anisotropy) lands is clean; reverting Milestone 2 (Clearcoat) after
Milestone 3/4 have already landed on top of its schema bump is **not**
clean (Milestone 3/4's own v5/v6 bumps depend on v4 existing first) —
if Milestone 2 needs rollback after later milestones have landed, the
later milestones must be rolled back first, in reverse landing order.
This is a real, disclosed ordering constraint of the sequential-bump
decision above (Sequencing & Dependencies), not a new risk this Plan
introduces silently.

**Schema-bump backward compatibility — none, by design, matching
established precedent.** Per Pre-drafting reading: bumping the material
schema version (v3→v4→v5→v6 across Milestones 2-4) makes every
already-shipped `.amaterial` artifact at the *old* version fail to load
(`UnsupportedSchemaVersion`/`UnexpectedSize`) — there is no dual-version
reader, matching both real prior bumps' own precedent. Each Milestone
2/3/4 must therefore re-author (re-cook) **every** existing
`.material.txt` file to that milestone's own new version line as part of
that same milestone's own PR — not deferred, not left for a later
cleanup — exactly as commits `f68234b`/`b9fe39d` each already did in
full for their own bump. **Rollback of a schema-bump milestone therefore
also requires reverting every re-authored `.material.txt`'s own version
line back down** — a real, mechanical, but bounded cost (every existing
material source file, currently a small, fixed, already-enumerable set).

**Showcase-scene/whitelist rollback** (Milestone 5): the 4th `--scene`
entry is purely additive to `main.cpp`'s array and `cli_tests.cpp`'s
fixture — reverting it does not affect the existing 3 entries' own
behavior, matching Spec 0032's own established "closed whitelist,
additive-only" design.

**Android rollback** (Milestone 6): purely additive list entries in
`android_main.cpp`/`build.gradle` — reverting removes exactly the new
assets' own extraction/packaging, no effect on existing assets.

## Definition of Done

See [docs/process/definition-of-done.md](../process/definition-of-done.md).
Deltas specific to this Plan:

- **Golden-image review category**: every new golden this Plan's
  Milestones 2-5 produce (at least 7: 6 per-BRDF + 1 showcase) is an
  **Initial baseline** update (ADR-0042's Amendment — "Initial baseline
  bootstrap" category, no prior golden exists to diff against) — each
  PR introducing one must state this category explicitly, per ADR-0042's
  own "must not leave this categorization implicit" rule, and satisfy
  that category's own four-part evidentiary bar (not category (2)'s
  diff-evidence requirement, which does not apply to a first capture).
- **Schema-bump re-authoring is itself part of Definition of Done** for
  Milestones 2/3/4 specifically — "Implementation matches the Plan"
  (standard DoD item) includes the full re-cook of every existing
  `.material.txt`, not merely the new kind's own new files; a PR that
  bumps the schema version without re-authoring every existing material
  source is incomplete under this Plan, not a follow-up.
- **Android lock-step cross-check is a named DoD item** for Milestone 6,
  beyond the standard checklist — both files' own comments already
  establish this obligation; this Plan makes it an explicit, checked
  item rather than relying on the comments alone.
