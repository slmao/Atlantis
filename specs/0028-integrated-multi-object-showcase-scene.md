# Spec: Integrated Multi-Object Showcase Scene

- **Status:** Draft
- **Author:** slmao
- **Created:** 2026-09-06
- **Related Plan(s):** None yet — Plan follows once this Spec is Approved.
- **Related ADR(s):** None — see Architectural Impact.

## Summary

Add one fixed demo scene that renders PBR, IBL, sky, a directional
light, and directional shadows together, with several objects that
reuse the same Mesh/Material assets at different transforms. This
composes already-implemented features (Spec 0023 PBR, Spec 0025 IBL,
Spec 0026 sky, Spec 0027 shadow); it does not add a rendering feature.

## Motivation / Problem Statement

Every existing demo scene isolates one feature (PBR alone, IBL alone,
sky alone, shadow alone) or uses at most 4-7 nodes with no repeated
Mesh/Material pairing. There is no scene that exercises the full stack
together, and no scene that proves multiple World entities correctly
share one Mesh/Material GPU resource while drawing independently. This
gap blocks a straightforward visual/regression check that the whole
pipeline composes correctly, and leaves "does the engine already
support drawing many objects that reuse assets" unverified by any
existing test.

## Goals

- One fixed scene: camera, environment (sky + IBL), one directional
  light, a shadow-receiving ground, and multiple objects, with at
  least two objects sharing one Mesh and at least two sharing one
  Material, each at a distinct transform.
- Prove, with a direct test, that this reuse maps to shared GPU
  Mesh/Material resources and one independent `DrawItem` per entity —
  behavior this codebase already implements (see Proposed Design).
- One new, independent image-regression golden for this scene, and one
  windowed run for human visual confirmation.
- Reuse existing cooked assets, the existing Runtime extraction/
  realization path, and the existing PBR/IBL/sky/shadow pipelines
  unchanged; add new assets/fixtures only where a real gap exists.

## Non-Goals

- GPU instanced draw (`instanceCount > 1`) or indirect draw.
- Bindless resources.
- Cascaded shadow maps or soft shadows (Spec 0027's single fixed
  orthographic shadow volume and hard-edge sampling are unchanged).
- Animation or any runtime scene mutation.
- An editor or any scene-authoring UI.
- A general scene-selection system (command-line/config-driven scene
  switching for `atlantis_runtime`). This Spec adds one more fixed,
  compiled-in scene next to the existing ones, not a selector.
- Any new RHI, RenderGraph, or Renderer public API.

## Requirements

### Functional

- **FR1 (Instance definition).** "Instance," in this Spec only, means
  multiple `atlantis::world::World` entities whose `Renderable`
  component references the same `meshAsset` and/or the same
  `materialAsset`, each with its own `Transform`, drawn as independent
  `DrawItem`s. It does not mean GPU instanced/indirect draw (Non-Goals).
- **FR2 (Fixed scene composition).** One scene with: one camera, one
  environment asset (drives IBL + sky, per the existing
  `config.environmentArtifactPath`-gated path), one directional light,
  one ground entity that receives shadows, and multiple PBR objects.
  At least two objects share one `meshAsset`; at least two objects
  share one `materialAsset`; every shared reference has a distinct
  `Transform` (position at minimum).
- **FR3 (Resource sharing).** Exactly one GPU Mesh resource per
  distinct `meshAsset` and one GPU Material resource per distinct
  `materialAsset` back this scene, regardless of how many entities
  reference them — this is the existing `meshResourceMap_`/
  `materialResourceMap_` behavior (see Proposed Design), not new
  caching logic.
- **FR4 (DrawItem count).** Exactly one `DrawItem` is produced per
  successfully-resolved renderable entity per frame; with no resolved
  errors, `drawItems.size()` equals this scene's own renderable-entity
  count. Verified directly by a new test, not just visually.
- **FR5 (Draw-order determinism).** Draw order is the same across
  repeated runs against the same built scene (source: `World`'s own
  stable `renderableEntities()` iteration order) — required for a
  reproducible golden.
- **FR6 (Occlusion/shadow visibility).** At least one object's shadow
  is visible on the ground within the golden's camera frame: a real,
  non-degenerate footprint, checked the same way Spec 0027's own P10
  Group A did (a shadowed reference pixel vs. a lit reference pixel),
  with new coordinates for this scene/camera.
- **FR7 (Lifecycle).** Static scene: no runtime mutation, no animation.
  Loaded once via the existing `loadAndInstantiateScene()`; transforms
  are unchanged frame to frame, matching every existing fixture.
- **FR8 (Windowed visible verification).** A new real-window Catch2
  `[gpu]` `TEST_CASE`, built on the same mechanism
  `runtime_smoke_gpu_tests.cpp` already uses (a real `Platform` window,
  a real `RuntimeApplication`, several real windowed frames), pointed
  at this scene's own `BootstrapConfig` paths. Crash-free and zero
  Vulkan Validation Layer output is the pass condition — no pixel
  assertions here (that is the golden's job, FR9).
- **FR9 (New golden).** One new, independent image-regression golden
  for this scene, captured and reviewed through ADR-0042's existing
  two-phase candidate-generate → human-review process. No existing
  golden is recaptured or modified by this Spec.
- **FR10 (Reuse-first).** Reuse the existing `pbr_sphere` mesh, the
  existing PBR material assets, the existing environment asset, and
  the existing PBR/IBL/sky/shadow shaders/pipelines unchanged. The one
  real gap (see Proposed Design) is a ground-plane mesh; add it as one
  new asset in the existing static-mesh source format, not a new
  format or a hand-built fixture mesh.

### Non-functional

- **Performance:** the scene's own material/Pipeline/descriptor-set
  count must stay within the already-proven descriptor pool ceiling
  (Spec 0021); no new performance requirement.
- **Memory:** no new persistent resource type; the new ground mesh is
  one small GPU buffer, same lifetime model as every other mesh.
- **Portability:** Windows only, matching every other GPU-tested demo
  in this repository today; Android/iOS untouched.
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
  the "Instance" mechanism FR1-FR4 describe — it is already
  implemented, this Spec only exercises it with a richer scene.
- `tests/image_regression/shadow_gpu_tests.cpp`'s own Group A rig
  already reuses one `Material` across a ground and an occluder
  `DrawItem` (`groundItem.material = &rig.material;`
  `occluderItem.material = &rig.material;`), confirming
  material-sharing across `DrawItem`s is an established pattern here,
  not a new one.
- `tests/image_regression/fixture/pbr_material_demo_fixture.{h,cpp}`
  already composes PBR + IBL + sky + shadow resources end to end
  (environment-gated sky Pipeline, unconditional shadow Pipeline/
  Sampler/ShadowMap/light-space Buffer) through the same
  `atlantis_runtime_host` calls Runtime itself uses
  (`loadAndInstantiateScene`, `realizePendingMaterials`,
  `extractCameraMatrices`, `extractFrameLightingData`, ...). The new
  fixture for this Spec's scene is a data variation of this same
  fixture, not a new composition.
- `runtime_smoke_gpu_tests.cpp`'s existing
  `TEST_CASE("Runtime constructs a window and completes real windowed
  acquire/draw/submit/present frames", ...)` already builds a real
  window and drives `RuntimeApplication` through it, parameterized on
  `BootstrapConfig`'s scene/environment path fields. FR8 reuses this
  exact mechanism with a second `TEST_CASE` and a different config,
  not a new executable.
- `assets/CMakeLists.txt` already cooks meshes and scenes through a
  data-only macro (`NAME`/`SOURCE`/`MESH_DEPENDENCIES` per asset,
  `atlantis_static_mesh_source_version: 3` /
  `atlantis_scene_source_version: 3` plain-text formats). Adding one
  mesh and one scene is a data addition through this existing macro,
  not a new tool or schema version.

**The one real gap:** no cooked, `World`-loadable ground/floor mesh
exists today. `pbr_sphere`/`minimal_cube`/`textured_quad_*` are not
usable as a flat shadow-receiving floor; `shadow_gpu_tests.cpp`'s own
ground is a hand-built, in-test-code quad that bypasses the asset
pipeline entirely and cannot be referenced from a scene manifest. This
Spec adds one new asset, `assets/meshes/ground_plane.mesh.txt`: a flat
quad in the existing static-mesh text format (position/color/UV0/
normal, upward normal, no new attribute), large enough to visibly
receive the shadow described in FR6.

**New scene:** `assets/scenes/integrated_showcase_demo.scene.txt`
(name recommended in Risks & Open Questions, Q3) — one ground node
(`ground_plane` mesh, an existing PBR material), one directional light,
one camera, and several `pbr_sphere` nodes reusing the existing PBR
material assets (`pbr_dielectric_rough` / `_smooth`,
`pbr_metallic_rough` / `_smooth`) with at least one material repeated
across two nodes at different positions — satisfying FR2's two sharing
requirements without any new material asset. Exact node count/layout
is Plan's responsibility, resolved by Q1's recommendation below rather
than left open.

**New test surface (all data/composition, no `src/` change):**

- `tests/image_regression/fixture/integrated_showcase_demo_fixture.{h,cpp}`
  — mirrors `pbr_material_demo_fixture.{h,cpp}` exactly, pointed at the
  new scene/environment.
- `tests/image_regression/golden_generator/integrated_showcase_demo_main.cpp`
  and `tests/image_regression/integrated_showcase_demo_gpu_tests.cpp` —
  mirror the existing per-demo pair; the golden lands at
  `tests/image_regression/goldens/integrated_showcase_demo/`.
- A new `TEST_CASE` in `tests/runtime/runtime_smoke_gpu_tests.cpp`
  (FR8), and a new GPU-independent or GPU test asserting FR3/FR4's
  resource-sharing/`DrawItem`-count invariants directly (mirrors the
  existing descriptor-pool-peak test style).
- New CMake entries in `assets/CMakeLists.txt` (mesh + scene cook) and
  the corresponding `tests/image_regression/fixture/CMakeLists.txt` /
  `tests/image_regression/golden_generator/CMakeLists.txt` /
  `tests/runtime/CMakeLists.txt` wiring, following the exact existing
  per-demo pattern (e.g. `ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH`).

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
3. The new fixture/golden_generator/gpu_tests trio and the new
   windowed `TEST_CASE` both reuse existing, already-Approved
   mechanisms (`atlantis_runtime_host`'s public composition functions;
   the existing real-window smoke-test pattern) with new data, not new
   code paths.
4. No RHI/RenderGraph/Renderer signature changes; no new `Vk*`
   exposure; no new dependency edge between top-level modules.

## Alternatives Considered

- **Reuse `atlantis_runtime`'s own compiled-in scene** by temporarily
  pointing its CMake-injected scene/environment path at the new scene.
  Rejected: this would remove the current default Runtime scene
  (`world_scene`) for every developer running the real product binary,
  for the sake of one demo — disproportionate, and unnecessary once
  FR8's own windowed `TEST_CASE` gives windowed verification without
  touching `atlantis_runtime`'s own configuration.
- **A new, separate windowed demo executable** (a second `main.cpp` +
  CMake target next to `atlantis_runtime`). Rejected in favor of FR8's
  `TEST_CASE`: it would duplicate `runtime_smoke_gpu_tests.cpp`'s own
  already-proven real-window mechanism instead of reusing it, for no
  additional coverage.
- **Real GPU instanced/indirect draw** for the repeated objects.
  Rejected as out of scope (Non-Goals) — this Spec is about resource
  sharing and `DrawItem` composition, not a new draw path.

## Testing & Verification Plan

- New fixture-based golden: candidate generated by
  `integrated_showcase_demo_main.cpp`, reviewed by the human
  maintainer, then locked as a byte-compare test — ADR-0042's existing
  two-phase process, unchanged.
- New windowed `[gpu]` `TEST_CASE` (FR8): real window, several frames,
  crash-free, zero Vulkan Validation Layer output.
- New direct test(s) for FR3 (one Mesh/Material GPU resource per
  distinct `AssetId`) and FR4 (`DrawItem` count equals renderable-
  entity count).
- All existing goldens re-verified byte-identical (no recapture).
- Full `ctest -LE gpu` and `ctest -L gpu`, Debug and Release, Vulkan
  Validation Layers clean.
- Descriptor-set peak for this scene's own Pipeline set confirmed
  under the existing 60-set ceiling (Spec 0021), using the existing
  peak-counting test style.
- `git diff --check`; module/link/include scan (no new `Vk*` outside
  Vulkan Backend; no new inter-module dependency edge).

## Risks & Open Questions

- **Q1 (scene layout).** How many objects, and which mesh/material
  reuse pattern, fixes FR2 concretely? **Recommendation:** reuse
  `pbr_material_demo.scene.txt`'s own proven 4-sphere diamond layout
  (4 distinct PBR materials) and add one 5th `pbr_sphere` node reusing
  one of those 4 materials at a new position — this alone satisfies
  both the mesh-sharing (5 nodes, 1 mesh) and material-sharing (2
  nodes, 1 material) requirements with no new material asset, plus the
  ground node (FR2/FR6) and the directional light.
- **Q2 (golden scope).** Should this Spec also capture a no-light/
  IBL-only variant golden of the same scene? **Recommendation:** no —
  `ibl_material_demo`'s existing golden already proves the no-light
  fallback path byte-for-byte; this Spec's one golden keeps light +
  shadow + sky + IBL simultaneously active, matching its own
  "integrated" goal directly.
- **Q3 (naming).** What is this scene/demo called in file and
  identifier names? **Recommendation:** `integrated_showcase_demo`,
  matching this Spec's own title and the existing `snake_case`
  `*_demo` convention (`pbr_material_demo`, `ibl_material_demo`).

## Out of Scope / Future Work

- GPU instanced/indirect draw, bindless resources, cascaded/soft
  shadows, animation, an editor, and a general scene-selection system
  all remain explicitly out of scope (Non-Goals) and are not unblocked
  by this Spec.
- A future scene-selection mechanism for `atlantis_runtime` (if ever
  wanted) is its own Spec, with its own module-boundary/API questions
  — not a byproduct of this one.
