# Spec: Integrated Multi-Object Showcase Scene

- **Status:** Draft
- **Author:** slmao
- **Created:** 2026-09-06
- **Related Plan(s):** None yet — Plan follows once this Spec is Approved.
- **Related ADR(s):** None — see Architectural Impact.

## Summary

Add one fixed demo scene that renders PBR, IBL, sky, a directional
light, and directional shadows together, with several objects that
reuse the same Mesh/Material assets at different transforms, and make
it `atlantis_runtime`'s own default scene — so running the real product
binary shows this composition directly, with no new executable and no
scene-selection system. This composes already-implemented features
(Spec 0023 PBR, Spec 0025 IBL, Spec 0026 sky, Spec 0027 shadow); it
does not add a rendering feature.

## Motivation / Problem Statement

No existing demo scene combines PBR, IBL, sky, a directional light,
and shadows all at once. Some already combine two features
(`pbr_material_demo.scene.txt` has PBR plus a directional and a point
light; `ibl_material_demo.scene.txt` has PBR plus IBL), but none has
all five, and none has a scene where at least two entities share one
Mesh *and*, separately, at least two entities share one Material. This
gap blocks a straightforward check that the whole pipeline composes
correctly, and leaves "does the engine already support drawing many
objects that reuse assets" unverified by any existing test.

Separately, `atlantis_runtime` — the actual product binary — currently
defaults to `world_scene` (5 untextured cubes, no material, no light).
Its own CMake configuration already loads the `ibl_studio` environment
and compiles the sky shader pair unconditionally
(`src/runtime/CMakeLists.txt`), so the sky background is already
visible today. What running the real binary does not show is PBR
material rendering with IBL, a directional light, or a visible
shadow — none of `world_scene`'s 5 cubes carries a Material, and the
scene has no light node at all.

## Goals

- One fixed scene: camera, environment (sky + IBL), one directional
  light, a shadow-receiving ground, and multiple objects, with at
  least two objects sharing one Mesh and at least two sharing one
  Material, each at a distinct transform.
- Make this scene `atlantis_runtime`'s own default scene, so the real
  product binary shows the full composition.
- Prove, with a direct test, that this reuse maps to shared GPU
  Mesh/Material resources and one independent `DrawItem` per entity —
  behavior this codebase already implements (see Proposed Design).
- One new, independent image-regression golden for this scene, and
  real windowed visual confirmation through the existing product
  binary and its existing windowed smoke test.
- Reuse existing cooked assets, the existing Runtime extraction/
  realization path, and the existing PBR/IBL/sky/shadow pipelines
  unchanged; add new assets/fixtures only where a real gap exists.

## Non-Goals

- GPU instanced draw (`instanceCount > 1`) or indirect draw.
- Bindless resources.
- Cascaded shadow maps or soft shadows (Spec 0027's single fixed
  orthographic shadow volume and hard-edge sampling are unchanged).
- Animation or any runtime scene mutation as a feature of this scene
  itself. (The windowed smoke test's own pre-existing dynamic
  point-light exercise, FR7, is a preserved test mechanism, not a
  scene feature this Spec adds.)
- An editor or any scene-authoring UI.
- A general scene-selection system (command-line/config-driven scene
  switching for `atlantis_runtime`). This Spec fixes one default scene
  at build/config time, the same way `world_scene` is fixed today — it
  does not add a selector, and does not add a second executable.
- Any new RHI, RenderGraph, or Renderer public API.
- Any change to `world_scene.scene.txt`, its own fixture, or its own
  golden — they stay exactly as they are, independently verified.

## Requirements

### Functional

- **FR1 (Instance definition).** "Instance," in this Spec only, means
  multiple `atlantis::world::World` entities whose `Renderable`
  component references the same `meshAsset` and/or the same
  `materialAsset`, each with its own `Transform`, drawn as independent
  `DrawItem`s. It does not mean GPU instanced/indirect draw (Non-Goals).
- **FR2 (Fixed scene composition).** One scene, `integrated_showcase_demo`:
  one camera; one environment asset (drives IBL + sky); one directional
  light; one ground entity (new `ground_plane` mesh) that receives
  shadows, reusing one existing PBR material — no new material asset;
  five `pbr_sphere` entities in the existing `pbr_material_demo`
  4-material diamond layout plus one 5th sphere reusing one of those
  four materials. Fixed totals: **6 renderable entities, 2 distinct
  Mesh assets (`pbr_sphere`, `ground_plane`), 4 distinct Material
  assets** (each PBR material used by at least one sphere; two of the
  four are each additionally reused by a second entity — one by the
  5th sphere, one by the ground — so the scene demonstrates
  mesh-sharing and material-sharing as two independent, verifiable
  facts, not just the required minimum of two).
- **FR3 (Resource sharing).** After scene load and at least one frame,
  exactly 2 GPU Mesh resources and exactly 4 GPU Material resources
  back this scene, regardless of the 6 entities referencing them —
  this is the existing `meshResourceMap_`/`materialResourceMap_`
  behavior (see Proposed Design), not new caching logic. Verified via
  two new `RuntimeSmokeTestAccess` accessor methods reading
  `meshResourceMap_.size()` / `materialResourceMap_.size()` — no new
  state on `RuntimeApplication` itself (see Proposed Design).
- **FR4 (DrawItem count).** Exactly one `DrawItem` is produced per
  renderable entity per frame; with no resolve errors,
  `drawItems.size() == 6`. The new fixture (Proposed Design) saves
  this count where its own GPU test can assert it directly.
- **FR5 (Draw-order determinism).** Draw order is the same across
  repeated runs against the same built scene (source: `World`'s own
  stable `renderableEntities()` iteration order) — required for a
  reproducible golden.
- **FR6 (Occlusion/shadow visibility).** At least one object's shadow
  is visible on the ground within the golden's camera frame: a real,
  non-degenerate footprint, checked the same way Spec 0027's own P10
  Group A did (a shadowed reference pixel vs. a lit reference pixel),
  with new coordinates for this scene/camera.
- **FR7 (Lifecycle).** The scene asset and the golden fixture are
  static during golden capture: no animation, no runtime mutation.
  `integrated_showcase_demo.scene.txt` is loaded once via the existing
  `loadAndInstantiateScene()`, and its entities' transforms are
  unchanged frame to frame, matching every existing golden fixture.
  This does not extend to the windowed smoke lifecycle (FR8): its own
  existing second half already exercises `World`'s dynamic-update path
  (creating and moving a point light entity at runtime, Spec 0022 M3).
  That existing coverage is preserved, not removed, and is adapted to
  this scene's own pre-existing directional light — the dynamically
  added point light coexists with it; `pointLightCount` assertions
  concern the added light, `directionalLightCount` assertions concern
  the scene's own light (see Proposed Design).
- **FR8 (Default scene + windowed verification, one lifecycle).**
  `atlantis_runtime`'s own CMake-injected scene/environment paths
  (`src/runtime/CMakeLists.txt`) point at `integrated_showcase_demo`
  instead of `world_scene` — running the product binary shows this
  scene directly. `tests/runtime/CMakeLists.txt` defines the identically-
  named scene macros a second time, independently, for the separate
  `atlantis_runtime_gpu_tests` target that builds
  `runtime_smoke_gpu_tests.cpp` — changing `src/runtime/CMakeLists.txt`
  alone does not affect it; both files' macro values (and each file's
  own `add_dependencies()` target list) switch together (see Proposed
  Design). No new executable, no new windowed `TEST_CASE`: the one
  existing windowed smoke lifecycle in `runtime_smoke_gpu_tests.cpp`
  is updated in place — its own `BootstrapConfig` population and its
  own existing assertions are updated to match (see Proposed Design).
  A second windowed
  `TEST_CASE` is deliberately not added: this repository's own
  `runtime_smoke_gpu_tests.cpp` already discloses that two independent
  windowed `RuntimeApplication` lifecycles in one process are
  unreliable ("an earlier draft of this test as its own separate
  `TEST_CASE` passed in isolation but crashed the process when run
  together with the `TEST_CASE` above") — reusing the one existing
  lifecycle avoids that conflict entirely rather than risking it again.
  Pass condition stays crash-free + zero Vulkan Validation Layer
  output; no pixel assertions here (that is the golden's job, FR9).
- **FR9 (New golden).** One new, independent image-regression golden
  for this scene, captured and reviewed through ADR-0042's existing
  two-phase candidate-generate → human-review process. No existing
  golden is recaptured or modified by this Spec.
- **FR10 (Reuse-first).** Reuse the existing `pbr_sphere` mesh, all
  four existing PBR material assets, the existing environment asset,
  and the existing PBR/IBL/sky/shadow shaders/pipelines unchanged. The
  one real gap (see Proposed Design) is a ground-plane mesh; add it as
  one new asset in the existing static-mesh source format. No new
  material asset is needed (FR2).

### Non-functional

- **Performance:** this scene's own 4 distinct materials produce a
  lower descriptor-set peak than the existing, already-verified N=6
  case (Spec 0021/0027); no new performance requirement, no new test
  (see Testing & Verification Plan).
- **Memory:** no new persistent resource type; the new ground mesh is
  one small GPU buffer, same lifetime model as every other mesh.
- **Portability:** the scene data, the cooked assets, and the offscreen
  fixture/golden verification path are platform-independent — they run
  through the same Runtime/Asset System/Vulkan Backend code Android
  already targets (AGENTS.md Phase 1). This Spec exercises real
  windowed verification on Windows only (FR8), because that is the
  only platform with a real-window GPU test today; it does not modify,
  test, or claim anything about Android, which stays exactly as
  untouched as it was before this Spec.
- **Other:** none.

## Proposed Design

**Reuse (verified against real code, not assumed):**

- `atlantis::world::Renderable` (`src/world/include/atlantis/world/renderable.h`)
  stores a plain `meshAsset` and an optional `materialAsset` per entity,
  with no uniqueness constraint — multiple entities can already
  reference the same `AssetId`. This is already exercised in shipped
  data: `assets/scenes/pbr_material_demo.scene.txt` and
  `ibl_material_demo.scene.txt` each already have 4 nodes that all
  reference `meshes/pbr_sphere.mesh.txt`.
- `RuntimeApplication::runFrame()` (`src/runtime/src/runtime_application.cpp`,
  the `meshResourceMap_`/`materialResourceMap_` members and the
  `for (const EntityId& id : world_->renderableEntities())` loop) already
  caches one GPU Mesh/Material per `AssetId` and builds one `DrawItem`
  per entity, each with its own `objectToWorld`. This loop is exactly
  the "Instance" mechanism FR1/FR3/FR4 describe — it is already
  implemented, this Spec only exercises it with a richer scene.
- `tests/image_regression/shadow_gpu_tests.cpp`'s own Group A rig
  already reuses one `Material` across a ground and an occluder
  `DrawItem`, confirming material-sharing across `DrawItem`s is an
  established pattern here, not a new one.
- `tests/image_regression/fixture/pbr_material_demo_fixture.{h,cpp}`
  already composes PBR + IBL + sky resources, and creates shadow-
  *compatible* resources, end to end through the same
  `atlantis_runtime_host` calls Runtime itself uses
  (`loadAndInstantiateScene`, `realizePendingMaterials`,
  `extractCameraMatrices`, `extractFrameLightingData`, ...). It never
  writes a real shadow, though — see "New fixture's real shadow path"
  below for exactly what the new fixture must do differently.
- `assets/CMakeLists.txt` already cooks meshes and scenes through a
  data-only macro (`NAME`/`SOURCE`/`MESH_DEPENDENCIES` per asset,
  `atlantis_static_mesh_source_version: 3` /
  `atlantis_scene_source_version: 3` plain-text formats). Adding one
  mesh and one scene is a data addition through this existing macro,
  not a new tool or schema version.
- `src/runtime/CMakeLists.txt` selects `world_scene`/`ibl_studio` as
  `atlantis_runtime`'s own scene/environment purely through its own
  `target_compile_definitions()` macro values
  (`ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH`, etc.). `tests/runtime/CMakeLists.txt`
  defines the identically-named macros a second time, independently,
  in its own separate `target_compile_definitions()` call for the
  `atlantis_runtime_gpu_tests` target — confirmed in both files, both
  currently set to `ATLANTIS_world_scene_*`. Changing one file's macro
  values has no effect on the other; both switch to
  `integrated_showcase_demo`'s own cooked paths, a build-config data
  change in each, not new code.
- `runtime_application.h` already declares `friend struct
  RuntimeSmokeTestAccess;` (a test-only accessor, zero production
  behavior) for exactly this purpose. New accessor methods on that
  struct (reading the already-existing `meshResourceMap_`/
  `materialResourceMap_` members) need no header/production change —
  the existing friendship already grants the access.

**The one real gap:** no cooked, `World`-loadable ground/floor mesh
exists today. `pbr_sphere`/`minimal_cube`/`textured_quad_*` are not
usable as a flat shadow-receiving floor; `shadow_gpu_tests.cpp`'s own
ground is a hand-built, in-test-code quad that bypasses the asset
pipeline entirely and cannot be referenced from a scene manifest. This
Spec adds one new asset, `assets/meshes/ground_plane.mesh.txt`: a flat
quad in the existing static-mesh text format (position/color/UV0/
normal, upward normal, no new attribute), large enough to visibly
receive the shadow described in FR6.

**New scene:** `assets/scenes/integrated_showcase_demo.scene.txt` —
one ground node (`ground_plane` mesh, reusing one existing PBR
material), one directional light, one camera, and five `pbr_sphere`
nodes: four in `pbr_material_demo`'s own proven diamond layout (one
each of the four existing PBR materials), plus a 5th node reusing one
of those same four materials at a new position. The ground reuses a
second one of the four materials (not a 5th) — this keeps the scene's
own material count fixed at exactly 4 (FR2) while giving two
independent, visible proofs of material-sharing (5th sphere + its
matching sphere; ground + its matching sphere).

**Default-scene switch (FR8), both target definitions:** in
`src/runtime/CMakeLists.txt` (target `atlantis_runtime`) *and*
`tests/runtime/CMakeLists.txt` (target `atlantis_runtime_gpu_tests`,
which builds `runtime_smoke_gpu_tests.cpp`) — two independent
`target_compile_definitions()` calls —
`ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH`/`_METADATA_PATH`/`_MANIFEST_PATH`
switch from `ATLANTIS_world_scene_*` to
`ATLANTIS_integrated_showcase_demo_*`, and each file's own
`add_dependencies()` target list switches the same way. Missing either
file leaves that target still pointed at `world_scene`.
`ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH`/`_METADATA_PATH` already
point at `ibl_studio` in both files and need no change. `world_scene`'s
own cooked asset, its own `world_scene_fixture`, and its own golden
are untouched by this switch and keep passing independently — this
only changes which scene each of these two targets compiles in.

**Existing windowed smoke lifecycle, updated (FR8):** the one
`TEST_CASE` in `runtime_smoke_gpu_tests.cpp`
(`"Runtime constructs a window and completes real windowed
acquire/draw/submit/present frames"`) reads its own
`ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH`-family macros from
`tests/runtime/CMakeLists.txt`'s own, separate
`target_compile_definitions()` call — it does not read
`src/runtime/CMakeLists.txt`'s copy, and picks up
`integrated_showcase_demo` only once *that* file's own macro values
switch too (the CMake fix above). Three things in this same test file
still need updating to match:

- Its `BootstrapConfig` C++ population currently never sets
  `environmentArtifactPath`/`environmentMetadataPath` or the
  PBR-IBL/sky shader path fields (it predates this file exercising an
  environment at all) — even though
  `ATLANTIS_RUNTIME_ENVIRONMENT_ARTIFACT_PATH`/sky-shader macros are
  already defined for this same `atlantis_runtime_gpu_tests` target
  (used by other tests in this file). These fields are added to this
  one `TEST_CASE`'s config, mirroring `main.cpp`'s own population 1:1
  — required for this scene's own sky/IBL (FR2); no new CMake macro.
- Its own existing assertions, hardcoded against `world_scene`'s
  known-zero-light, 5-renderable shape, are updated to match the new
  scene's fixed shape: `renderableEntityCount(app) == 5` becomes `== 6`;
  the pre-dynamic-light-add `beforeAnyLight.directionalLightCount == 0`
  becomes `== 1` (the scene's own directional light is already present
  before the test adds its own extra point light); the
  post-dynamic-light-add `afterLightAdded.directionalLightCount == 0`
  becomes `== 1` for the same reason. `pointLightCount` assertions are
  unaffected (this scene has no point light of its own).
- Its own existing dynamic point-light exercise (creating a point
  light entity via `World::createEntity()`/`setLight()`, then moving
  it via `setLocalTransform()`, Spec 0022 M3) is preserved unchanged —
  not removed, not treated as a lifecycle violation (FR7). It now runs
  against a `World` that already has one directional light; only the
  `pointLightCount`/`pointLights[0]` assertions concern the light this
  exercise itself adds.

Two new `RuntimeSmokeTestAccess` accessor methods are added in this
same test file (not in `runtime_application.h`/`.cpp` — the existing
friend declaration already grants access, so `RuntimeApplication`
itself gains no new state): one reading `meshResourceMap_.size()`, one
reading `materialResourceMap_.size()`, asserted as `== 2`/`== 4` (FR3)
alongside the existing `renderableEntityCount(app) == 6` check.

**New fixture's real shadow path (not a mirror of
`pbr_material_demo_fixture`):** that fixture creates shadow-*compatible*
resources (`ShadowMap`, `Sampler`, shadow `Pipeline`,
`shadowLightSpaceBuffer`) but never casts a real shadow — it writes a
fixed identity matrix into the light-space tail (`kIdentityMatrix`,
its own P11 no-light sentinel) instead of calling
`computeShadowLightSpaceMatrices()`, and passes an empty
`shadowCasterDrawItems` span to `drawFrame()`, by design, since it
never configures a directional light. This scene does (FR2), so its
own fixture must differ in exactly these ways:

- Call the existing, shared production function
  `atlantis::runtime::computeShadowLightSpaceMatrices()`
  (`scene_extraction.h`, Spec 0027) with this scene's own directional
  light direction — the same function `RuntimeApplication::runFrame()`
  and the shadow discriminator tests already call. Never re-derive or
  duplicate that math.
- Write the resulting view/projection into both the camera buffer's
  own light-space tail and the dedicated `shadowLightSpaceBuffer`,
  matching `RuntimeApplication::runFrame()`'s own real, dual-write
  behavior (Spec 0027 P5) — not the identity sentinel.
- Build a real, non-empty `shadowCasterDrawItems` span from this
  scene's own compatible `DrawItem`s (all 6), matching
  `RuntimeApplication::runFrame()`'s own unconditional
  "`shadowCasterDrawItems` is `drawItems` itself whenever a
  directional light is configured" contract (Spec 0027 P6).
- Render through the existing, unmodified `Renderer::drawFrame()` path
  with these real inputs; FR6's occlusion check confirms an actual,
  visible shadow footprint on the ground in the captured image.

No engine-module change (`scene_extraction.cpp`, `Renderer`,
RenderGraph, Vulkan Backend all unchanged) — the fixture only calls
the already-public `computeShadowLightSpaceMatrices()`.

**New test surface (all data/composition, no engine-module `src/`
change):**

- `tests/image_regression/fixture/integrated_showcase_demo_fixture.{h,cpp}`
  — reuses `pbr_material_demo_fixture.{h,cpp}`'s own resource-creation
  shape but replaces its identity-sentinel/empty-shadow-caster path
  with the real one above; exposes its own built `drawItems.size()`
  for FR4's `== 6` assertion.
- `tests/image_regression/golden_generator/integrated_showcase_demo_main.cpp`
  and `tests/image_regression/integrated_showcase_demo_gpu_tests.cpp` —
  mirror the existing per-demo pair; the golden lands at
  `tests/image_regression/goldens/integrated_showcase_demo/`.
- New CMake entries in `assets/CMakeLists.txt` (mesh + scene cook),
  `src/runtime/CMakeLists.txt` *and* `tests/runtime/CMakeLists.txt`
  (default-scene switch, both files, above), and the
  corresponding `tests/image_regression/fixture/CMakeLists.txt` /
  `tests/image_regression/golden_generator/CMakeLists.txt` wiring,
  following the exact existing per-demo pattern.

No changes to any RHI, RenderGraph, Renderer, Shader System, Asset
System, World, or Vulkan Backend source. No changes to
`runtime_application.h`/`.cpp` (its existing private members and
existing test-only friend declaration already cover every new
accessor this Spec's tests need). The only `src/` changes are the two
independent `src/runtime/CMakeLists.txt` / `tests/runtime/CMakeLists.txt`
macro-value swaps above — build configuration selecting which
already-cooked scene/environment each of these two targets compiles
in, not new logic. `tests/runtime/runtime_smoke_gpu_tests.cpp` (a test
file, not an engine module) is modified in place, not added to.

## Architectural Impact

**None — no new ADR required.** This Spec introduces no new public
API, module boundary, dependency, threading model, or backend-
abstraction contract:

1. Multi-entity Mesh/Material sharing (FR1, FR3, FR4) is existing,
   shipped behavior in `Renderable`, `RuntimeApplication::runFrame()`,
   and already-shipped scene data (`pbr_material_demo.scene.txt`) —
   verified directly against the current source above, not assumed.
2. The new ground mesh and scene use the existing static-mesh/scene
   plain-text source formats and the existing CMake cooking macros —
   no new asset format, version, or tool.
3. The new fixture/golden_generator/gpu_tests trio reuses existing,
   already-Approved mechanisms (`atlantis_runtime_host`'s public
   composition functions, including `computeShadowLightSpaceMatrices()`)
   with new data, not new code paths. The windowed verification path
   is the existing, unchanged smoke `TEST_CASE` mechanism (one file,
   modified in place), given new config values and updated assertions
   — not a new mechanism, and not a new file.
4. `atlantis_runtime`'s and `atlantis_runtime_gpu_tests`'s default-scene
   switch is a product-content/build-configuration change (which
   already-cooked scene/environment paths each of these two CMake
   targets compiles in) — it changes no public API, no module
   boundary, and no dependency. `RuntimeApplication`'s own public
   surface and `BootstrapConfig`'s own field set are unchanged.
5. No RHI/RenderGraph/Renderer signature changes; no new `Vk*`
   exposure; no new dependency edge between top-level modules.

## Alternatives Considered

- **Add a second windowed `TEST_CASE`** for the new scene, keeping
  `world_scene` as `atlantis_runtime`'s own default (an earlier draft
  of this Spec's own approach). Rejected: `runtime_smoke_gpu_tests.cpp`
  already discloses that two independent windowed `RuntimeApplication`
  lifecycles in one process are unreliable — a second `TEST_CASE`
  risks reviving that exact, already-documented conflict for no
  benefit once the one existing lifecycle can just point at the new
  scene (FR8).
- **A new, separate windowed demo executable** next to
  `atlantis_runtime`. Rejected: `atlantis_runtime` already is the one
  production windowed composition root; switching its own default
  scene gives real, product-visible verification for free, with no
  second binary to build or maintain.
- **Keep `world_scene` as the default and verify the showcase only
  off-screen** (fixture + golden, no real window). Rejected: windowed
  visible confirmation through the actual product binary is an
  explicit Goal, not only a captured PNG.
- **Real GPU instanced/indirect draw** for the repeated objects.
  Rejected as out of scope (Non-Goals) — this Spec is about resource
  sharing and `DrawItem` composition, not a new draw path.

## Testing & Verification Plan

- New fixture-based golden: candidate generated by
  `integrated_showcase_demo_main.cpp`, reviewed by the human
  maintainer, then locked as a byte-compare test — ADR-0042's existing
  two-phase process, unchanged. The new fixture's own GPU test also
  asserts `drawItems.size() == 6` directly (FR4).
- Existing windowed smoke `TEST_CASE`, updated (not duplicated) after
  both `src/runtime/CMakeLists.txt` and `tests/runtime/CMakeLists.txt`
  switch their own scene macros (Proposed Design): real window,
  several frames, crash-free, zero Vulkan Validation Layer output;
  `renderableEntityCount(app) == 6`; new
  `meshResourceMapSize(app) == 2` / `materialResourceMapSize(app) == 4`
  (FR3); `directionalLightCount` assertions updated to `== 1` at both
  existing check points; its own pre-existing dynamic point-light
  creation/movement checks (Spec 0022 M3) preserved unchanged and
  re-verified alongside the scene's own directional light (FR7).
- `world_scene`'s own fixture/gpu_tests/golden re-verified byte-
  identical and passing, unchanged — proving the default-scene switch
  does not disturb `world_scene`'s own independent verification.
- No new descriptor-peak test: the existing
  `"N=6 HDR pipeline descriptor-set peak is exactly N+4/N+5 with both
  a sky and a shadow-cast Pipeline present"` test
  (`material_realization_gpu_tests.cpp`) already proves the ceiling
  holds at N=6, strictly above this scene's own N=4 distinct
  materials. Plan re-derives N=4's own values from the same formula
  (steady = N+4 = 8, peak = N+5 = 9, both well under the 60-set
  ceiling) and re-runs the existing test unchanged as confirmation —
  no new test case.
- All other existing goldens re-verified byte-identical (no
  recapture).
- Full `ctest -LE gpu` and `ctest -L gpu`, Debug and Release, Vulkan
  Validation Layers clean.
- `git diff --check`; module/link/include scan (no new `Vk*` outside
  Vulkan Backend; no new inter-module dependency edge).

## Risks & Open Questions

None. The three prior open items are now fixed as part of this Spec:
name (`integrated_showcase_demo`), layout (4-sphere diamond + 5th
sphere reusing one material + ground reusing a second material,
6 renderables / 2 meshes / 4 materials total), and golden scope (one
golden, light + shadow + sky + IBL simultaneously active — no separate
no-light variant, already covered by `ibl_material_demo`'s existing
golden). No open design items remain; awaiting Human Review.

## Out of Scope / Future Work

- GPU instanced/indirect draw, bindless resources, cascaded/soft
  shadows, animation, an editor, and a general scene-selection system
  all remain explicitly out of scope (Non-Goals) and are not unblocked
  by this Spec.
- A future scene-selection mechanism for `atlantis_runtime` (if ever
  wanted) is its own Spec, with its own module-boundary/API questions
  — not a byproduct of this one, which keeps exactly one fixed,
  compiled-in default scene, the same way `world_scene` was fixed
  before it.
