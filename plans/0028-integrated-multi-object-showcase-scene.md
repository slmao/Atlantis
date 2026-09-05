# Plan: Integrated Multi-Object Showcase Scene

- **Spec:** [specs/0028-integrated-multi-object-showcase-scene.md](../specs/0028-integrated-multi-object-showcase-scene.md) (`Approved`)
- **Status:** Draft
- **Author:** slmao

## Non-negotiable rule for Implementation

**Every numeric value fixed in this Plan (node positions/rotations, the
ground mesh's vertices/indices, the light direction, the camera
transform, every world-space sample point, and every pixel/luminance
threshold in "Shadow/Lit Reference Derivation" below) is approved as
written.** If a real GPU capture during Implementation shows any of
them needs to change, **stop and request human confirmation before
changing it** — never adjust silently and continue.

**Carve-out, stated precisely so it is not read as an escape hatch:**
mechanically re-executing this Plan's own fixed camera-projection
formula against its own fixed inputs (the scene positions, camera,
and light direction below) and getting an integer pixel that differs
from this Plan's own hand-computed value by a point or two (floating-
point rounding only) is not a value change — it is finishing the same
computation this Plan already specifies. Changing the formula itself,
the world-space points, the camera/light parameters, or a threshold's
underlying rationale is a value change and requires stopping.

## Objective

Implement Spec 0028 as approved: one new, fixed `integrated_showcase_demo`
scene composing PBR, IBL, sky, one directional light, and a real
shadow, with 6 renderable entities sharing 2 GPU Meshes and 4 GPU
Materials; make it `atlantis_runtime`'s own default scene; add one new,
independent image-regression golden. No new rendering feature, no new
public API, no new ADR (Spec 0028's own Architectural Impact).

## Fixed Scene Specification

All positions/rotations below use this codebase's own Euler convention
(`src/world/include/atlantis/world/transform.h:7-14`): a `Transform`'s
world rotation is `Ry(yaw) · Rx(pitch) · Rz(roll)`, and a scene node's
`rotation=X Y Z` field is `(pitch, yaw, roll)` in that order. A
`Renderable`/`Light`/`Camera` node's own world "forward" is `R ·
(0,0,-1)` (confirmed independently against `world_scene.scene.txt`'s
own camera node — see Camera below).

### Ground mesh: `assets/meshes/ground_plane.mesh.txt` (new)

The one real asset gap Spec 0028 identified — no cooked, `World`-
loadable flat floor mesh exists today (`pbr_sphere`/`minimal_cube`/
`textured_quad_*` are not flat floors; `shadow_gpu_tests.cpp`'s own
ground is hand-built in test code, bypassing the asset pipeline
entirely). New file, in the existing plain-text static-mesh format
(`atlantis_static_mesh_source_version: 3`, `vertex: px py pz r g b u v
nx ny nz`, `index: i0 i1 i2` — confirmed against
`assets/meshes/textured_quad_left.mesh.txt`):

```
atlantis_static_mesh_source_version: 3
vertex_count: 4
index_count: 6
vertex: -5.0 0.0 -5.0 1.0 1.0 1.0 0.0 0.0 0.0 1.0 0.0
vertex: -5.0 0.0 5.0 1.0 1.0 1.0 0.0 1.0 0.0 1.0 0.0
vertex: 5.0 0.0 5.0 1.0 1.0 1.0 1.0 1.0 0.0 1.0 0.0
vertex: 5.0 0.0 -5.0 1.0 1.0 1.0 1.0 0.0 0.0 1.0 0.0
index: 0 1 2
index: 2 3 0
```

- **Size:** a flat 10×10 quad, `x,z ∈ [-5,5]`, `y = 0`.
- **Normal:** `(0,1,0)` (up) at every vertex, flat-shaded like every
  other mesh in this repository (no per-vertex normal variation).
- **Color:** `(1,1,1)` placeholder — `pbr_ibl.slang`/`pbr_direct_lit.slang`
  never read per-vertex color (matches `pbr_sphere.mesh.txt`'s own
  identical placeholder).
- **UV0:** planar `((x+5)/10, (z+5)/10)` — `(0,0)`, `(0,1)`, `(1,1)`,
  `(1,0)` for the four corners in declaration order.
- **Winding:** vertex order `(-5,0,-5) → (-5,0,5) → (5,0,5) → (5,0,-5)`,
  triangles `(0,1,2)`/`(2,3,0)`. Verified to produce an outward `+Y`
  normal via `cross(v1-v0, v2-v0)` — the same convention
  `textured_quad_left.mesh.txt`'s own 4 vertices/2 triangles produce
  its own stated `(0,0,1)` normal under (confirmed by hand: `cross(v1-v0,
  v2-v0) = cross((0.8,0,0),(0.8,1,0)) = (0,0,0.8) → (0,0,1)`, matching
  that file's own normal field exactly). For this mesh:
  `v1-v0=(0,0,10)`, `v2-v0=(10,0,10)`,
  `cross = (0·10-10·0, 10·10-0·10, 0·0-0·10) = (0,100,0) → (0,1,0)`.

**Cook (in `assets/CMakeLists.txt`, mirrors `pbr_sphere`'s own
unexported-`LOGICAL_PATH` shape at lines 207-213 — its only consumer is
`atlantis_add_scene_asset()` in this same file, no external directory
needs its `LOGICAL_PATH` directly):**

```cmake
atlantis_add_static_mesh_asset(
  NAME ground_plane
  SOURCE meshes/ground_plane.mesh.txt
)
set(ATLANTIS_ground_plane_ARTIFACT_PATH "${ATLANTIS_ground_plane_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_ground_plane_METADATA_PATH "${ATLANTIS_ground_plane_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_ground_plane_TARGET "${ATLANTIS_ground_plane_TARGET}" PARENT_SCOPE)
```

### Scene: `assets/scenes/integrated_showcase_demo.scene.txt` (new)

8 nodes: 1 ground + 5 `pbr_sphere` entities + 1 directional light + 1
camera. `pbr_sphere` (`assets/meshes/pbr_sphere.mesh.txt`) is confirmed
a unit sphere (radius 1.0 — every vertex's position equals its own
normal, e.g. `(-0.69351992, 0.19509032, 0.69351992)`, length ≈ 1.0),
so every sphere node sits at `y=1.0` (resting exactly on the `y=0`
ground). Layout: `pbr_material_demo.scene.txt`'s own proven 4-sphere
diamond (`±1.3` in the two horizontal axes, one each of the four
existing PBR materials), re-planted in the `x,z` ground plane instead
of that scene's own `x,y` wall plane, plus a 5th sphere reusing one
material and the ground reusing a second — two independent,
verifiable material-sharing proofs, per Spec 0028 FR2:

| # | Entity | Position | Mesh | Material |
|---|---|---|---|---|
| 1 | Ground | `(0, 0, 0)` | `ground_plane` | `pbr_dielectric_rough` |
| 2 | Sphere A | `(-1.3, 1.0, 1.3)` | `pbr_sphere` | `pbr_dielectric_rough` (shared with Ground) |
| 3 | Sphere B | `(1.3, 1.0, 1.3)` | `pbr_sphere` | `pbr_dielectric_smooth` |
| 4 | Sphere C | `(-1.3, 1.0, -1.3)` | `pbr_sphere` | `pbr_metallic_rough` |
| 5 | Sphere D | `(1.3, 1.0, -1.3)` | `pbr_sphere` | `pbr_metallic_smooth` |
| 6 | Sphere E | `(0.0, 1.0, -3.2)` | `pbr_sphere` | `pbr_metallic_smooth` (shared with Sphere D) |
| 7 | Light | `(0,0,0)`, see below | — | directional |
| 8 | Camera | `(0, 6, 10)`, see below | — | — |

Node declaration order 1→8 is `World`'s own slot-creation order;
`World::renderableEntities()` iterates `slots_` by ascending index
(`src/world/src/world.cpp:339-348`) and only ever grows during scene
load — so `DrawItem` order is deterministic and always
`[Ground, A, B, C, D, E]` (FR5), 6 entities total (FR2/FR3/FR4): **2
distinct Mesh assets** (`pbr_sphere` ×5, `ground_plane` ×1), **4
distinct Material assets** (`pbr_dielectric_rough` ×2,
`pbr_dielectric_smooth`/`pbr_metallic_rough` ×1 each,
`pbr_metallic_smooth` ×2).

**Directional light (node 7):** direction is derived, not authored
directly — the scene format only stores `rotation`; Runtime computes
`direction = normalize(-column2)` of the light's own world matrix
(`src/runtime/src/scene_extraction.cpp:282-289`), i.e. `R·(0,0,-1)`.
Chosen so the resulting `direction` is an exact, hand-verifiable unit
vector, not an approximation:

- `rotation = (-0.6435011, 2.4980915, 0.0)` (pitch `= -arcsin(0.6)`,
  yaw `= π - arcsin(0.6)`, roll `= 0`).
- Derivation: `Rx(pitch)·(0,0,-1) = (0, sin(pitch), -cos(pitch)) =
  (0, -0.6, -0.8)` (`sin(-arcsin(0.6)) = -0.6`; `cos(arcsin(0.6)) =
  0.8` regardless of sign). `Ry(yaw)` applied to `(x,y,z)` is `(x
  cosθ + z sinθ, y, -x sinθ + z cosθ)`; with `θ = π - arcsin(0.6)`,
  `sinθ = 0.6`, `cosθ = -0.8`: `x' = 0·(-0.8) + (-0.8)·0.6 = -0.48`,
  `y' = -0.6`, `z' = -0·0.6 + (-0.8)·(-0.8) = 0.64`.
- **`direction = (-0.48, -0.6, 0.64)`** — exact unit length
  (`0.48² + 0.6² + 0.64² = 0.2304 + 0.36 + 0.4096 = 1.0`).
- `color = (1.0, 1.0, 1.0)`, `intensity = 3.0` (reuses
  `shadow_gpu_tests.cpp`'s own already-validated Group A/B light
  intensity, not a new untested value).
- Sanity check against Spec 0027's own fixed shadow-volume
  degeneracy rule (`scene_extraction.cpp:169-172`): `cross(direction,
  (0,1,0))` has length `0.8`, far above the `1e-6` threshold — no
  up-vector fallback triggers.

**Camera (node 8):** `position = (0, 6, 10)`, looking toward
`(0, 1, 0)` (sphere-center height). `rotation = (-0.4636476, 0.0,
0.0)` (`pitch = -arctan(0.5)`), `camera_fov_y = 1.0472` (60°,
matching `pbr_material_demo`/`shadow_gpu_tests.cpp` Group A's own
convention), `camera_near_z = 0.1`, `camera_far_z = 100.0`. Derivation:
`Rx(-arctan(0.5))·(0,0,-1) = (0, sin(pitch), -cos(pitch))`; a
`1:2:√5` right triangle gives `sin(pitch) = -1/√5 = -0.4472136`,
`cos(pitch) = 2/√5 = 0.8944272` — **`forward = (0, -0.4472136,
-0.8944272)`**, i.e. `eye + t·forward` reaches `y=0` (ground level) at
`t = 6/0.4472136 = 13.42`, `z = 10 - 13.42·0.8944272 = -2.0`, safely
inside the ground's own `z ∈ [-5,5]` extent and near the object
cluster — confirming the chosen angle actually frames the scene, the
same cross-check `world_scene.scene.txt`'s own camera
(`rotation=-0.3054 0 0` from `(0,2.2,7)`) independently confirmed
this Plan's Euler convention against (hits `(0,~0,0)` at `t≈7.34`).

**Full scene file:**

```
atlantis_scene_source_version: 3
node_count: 8
active_camera: 8
node: node_id=1 parent=none position=0.0 0.0 0.0 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/ground_plane.mesh.txt material=materials/pbr_dielectric_rough.material.txt
node: node_id=2 parent=none position=-1.3 1.0 1.3 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/pbr_sphere.mesh.txt material=materials/pbr_dielectric_rough.material.txt
node: node_id=3 parent=none position=1.3 1.0 1.3 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/pbr_sphere.mesh.txt material=materials/pbr_dielectric_smooth.material.txt
node: node_id=4 parent=none position=-1.3 1.0 -1.3 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/pbr_sphere.mesh.txt material=materials/pbr_metallic_rough.material.txt
node: node_id=5 parent=none position=1.3 1.0 -1.3 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/pbr_sphere.mesh.txt material=materials/pbr_metallic_smooth.material.txt
node: node_id=6 parent=none position=0.0 1.0 -3.2 rotation=0.0 0.0 0.0 scale=1.0 1.0 1.0 mesh=meshes/pbr_sphere.mesh.txt material=materials/pbr_metallic_smooth.material.txt
node: node_id=7 parent=none position=0.0 0.0 0.0 rotation=-0.6435011 2.4980915 0.0 scale=1.0 1.0 1.0 light=directional color=1.0 1.0 1.0 intensity=3.0
node: node_id=8 parent=none position=0.0 6.0 10.0 rotation=-0.4636476 0.0 0.0 scale=1.0 1.0 1.0 camera_fov_y=1.0472 camera_near_z=0.1 camera_far_z=100.0
```

**Cook (in `assets/CMakeLists.txt`, mirrors `pbr_material_demo_scene`'s
own shape at lines 272-282):**

```cmake
atlantis_add_scene_asset(
  NAME integrated_showcase_demo_scene
  SOURCE scenes/integrated_showcase_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere ground_plane
  MATERIAL_DEPENDENCIES pbr_dielectric_rough pbr_dielectric_smooth pbr_metallic_rough pbr_metallic_smooth
  TEXTURE_DEPENDENCIES textured_quad_srgb
)
set(ATLANTIS_integrated_showcase_demo_scene_ARTIFACT_PATH "${ATLANTIS_integrated_showcase_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_integrated_showcase_demo_scene_METADATA_PATH "${ATLANTIS_integrated_showcase_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_integrated_showcase_demo_scene_MANIFEST_PATH "${ATLANTIS_integrated_showcase_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_integrated_showcase_demo_scene_TARGET "${ATLANTIS_integrated_showcase_demo_scene_TARGET}" PARENT_SCOPE)
```

Both mesh and scene declarations are placed unconditionally (outside
any `ATLANTIS_BUILD_TESTS` guard), matching `pbr_sphere`'s/
`pbr_material_demo_scene`'s own placement — `atlantis_runtime` (M2)
depends on the scene regardless of whether tests are built.

### Shadow/Lit-Reference Derivation (FR6)

Reuses Spec 0027 P10's own two fixed formulas verbatim (no
re-derivation of the formulas themselves):

- **Shadow footprint** (`computeShadowLightSpaceMatrices()`'s own
  light-eye/basis derivation, `scene_extraction.cpp:163-198`, applied
  to a `y=0` ground plane): for an occluder center `(cx,cy,cz)` and
  unit light direction `d`, `t = -cy/d.y`, `footprint = (cx+t·dx, 0,
  cz+t·dz)`.
- **Camera pixel projection** (mirrors `lookAtMatrix()`'s own
  `right = normalize(cross(forward, worldUp))`, `up = cross(right,
  forward)`, `scene_extraction.cpp:79-90`): `viewX = dot(p-eye,
  right)`, `viewY = dot(p-eye, up)`, `viewZ = -dot(p-eye, forward)`,
  `clipX = f·viewX`, `clipY = -f·viewY` (`f = 1/tan(30°) = 1.7320508`,
  `aspect=1`), `clipW = -viewZ`, `pixel = round((clip/clipW+1)/2 ·
  512)`.

Camera basis for `eye=(0,6,10)`, `forward=(0,-0.4472136,-0.8944272)`:
`right = cross(forward,(0,1,0)) = (0.8944272,0,0) → (1,0,0)` (exact —
`forward.x=0`); `up = cross(right,forward) = (0, 0.8944272,
-0.4472136)` (exact, unit already).

**Shadowed point:** footprint of Sphere A (`(-1.3,1,1.3)`, treated as
a point occluder, matching Plan 0027 P10's own convention) under
`d=(-0.48,-0.6,0.64)`: `t = -1/-0.6 = 1.66667`, `footprint = (-1.3 +
1.66667·(-0.48), 0, 1.3 + 1.66667·0.64) = (-2.1, 0, 2.3667)`.

*Self-occlusion check (why this footprint, not another):* the light's
own horizontal travel direction is `(-0.48,0.64)` (toward `-X,+Z`),
the same general direction as the camera position offset from origin
(`+Z`) — so this footprint falls on the **near** side of Sphere A
(between the sphere and the camera), not the far side. Verified by
computing the closest approach of the camera→footprint ray to Sphere
A's own center: parameter `t* = dot(C-eye,rayDir)/|rayDir|² ≈ 1.005`
— **past the footprint endpoint** (`t*>1`), meaning the ray from
`eye` to `footprint` never passes near Sphere A at all, so Sphere A
does not occlude its own shadow from the camera. (The mirror-image
footprint on the far side, checked first during this Plan's own
drafting, gave `t*≈0.89` with a closest-approach distance of `≈0.6`,
well inside the sphere's own radius `1.0` — self-occluded, and is why
the light's `z`-component is `+0.64`, not `-0.64`.)

Pixel projection: `P-eye=(-2.1,-6,-7.6333)`; `viewX=-2.1`,
`viewY=dot(P-eq,up)≈-1.9528`, `viewZ=-dot(P-eq,forward)≈-9.5107`;
`clipX≈-3.6373`, `clipY≈3.3824`, `clipW≈9.5107`; `ndc≈(-0.3825,
0.3556)` → **pixel (158, 347)**.

**Lit reference point:** `Q = (3.5, 0, 3.5)` — clear of all 5 spheres'
own footprints (`A(-2.1,2.367)`, `B(0.5,2.367)`, `C(-2.1,-0.233)`,
`D(0.5,-0.233)`, `E(-0.8,-2.133)`, all computed by the same formula,
none within `2` world units of `Q`) and of the spheres themselves.
`Q-eye=(3.5,-6,-6.5)`; `viewX=3.5`, `viewY≈-2.4597`, `viewZ≈-8.4971`;
`clipX≈6.0622`, `clipY≈4.2603`, `clipW≈8.4971`; `ndc≈(0.7136,
0.5014)` → **pixel (439, 384)**.

**Threshold.** This scene has an environment configured, so every
`PbrDirectLit` material is realized against `pbr_ibl.slang`, not
`pbr_direct_lit.slang` (`selectShaderPair()`,
`src/runtime/src/material_realization.cpp:101-126`: `environmentEnabled
== true` selects the IBL shader pair unconditionally for
`PbrDirectLit`) — unlike Spec 0027 P10 Group A's own no-environment
rig, a shadowed pixel here is **not** exactly zero; it still receives
an IBL ambient term. This is exactly `shadow_gpu_tests.cpp`'s own
Group B situation (same `pbr_ibl.slang` path, same reason), whose own
proven floor this Plan reuses rather than inventing a new one:

```
luminance(pixel(439,384)) - luminance(pixel(158,347)) > 15
```

(RGB8 sum, `0-765` range — `luminance()`/`pixelAt()` already exist in
`tests/image_regression/support/pixel_diff.h`, reused unchanged;
`15` is `shadow_gpu_tests.cpp:979`'s own already-validated `R2-R1`
floor for this identical shader/lighting situation, not a new,
unvalidated number.)

## Milestones / Task Breakdown

1. **New assets.** `assets/meshes/ground_plane.mesh.txt`,
   `assets/scenes/integrated_showcase_demo.scene.txt`, and the two
   `assets/CMakeLists.txt` cook declarations above. Independently
   buildable (asset cooking only, no consumer yet) —
   `atlantis_finalize_asset_validation()`'s own configure-time check
   passes.
2. **Default-scene switch, both targets, atomic with the smoke-test
   update.** In one commit (avoids a red window between the CMake
   switch and the test code it invalidates):
   - `src/runtime/CMakeLists.txt` (lines 87-89, 150): switch
     `ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH`/`_METADATA_PATH`/
     `_MANIFEST_PATH` and the `add_dependencies()` target from
     `ATLANTIS_world_scene_*` to `ATLANTIS_integrated_showcase_demo_scene_*`.
   - `tests/runtime/CMakeLists.txt` (lines 72-74, 117): the identical
     switch, independently — confirmed a **separate**
     `target_compile_definitions()`/`add_dependencies()` call for the
     `atlantis_runtime_gpu_tests` target; changing only
     `src/runtime/CMakeLists.txt` leaves this target still pointed at
     `world_scene`.
   - `tests/runtime/runtime_smoke_gpu_tests.cpp`: add the
     `environmentArtifactPath`/`environmentMetadataPath` and
     `pbrIbl*`/`sky*` `BootstrapConfig` fields this file's own
     `TEST_CASE` currently never sets (mirroring `main.cpp`'s
     population 1:1 — the CMake macros themselves already exist for
     this target, added by Plan 0026/0027 for this file's own sibling
     descriptor-pool tests, just never consumed by this one
     `TEST_CASE`). Update existing assertions:
     `renderableEntityCount(app) == 5` → `== 6`; the pre-dynamic-light
     `beforeAnyLight.directionalLightCount == 0` → `== 1`; the
     post-dynamic-light `afterLightAdded.directionalLightCount == 0`
     → `== 1`. Add two `RuntimeSmokeTestAccess` accessor methods
     (same file, same struct — no `runtime_application.h`/`.cpp`
     change, the existing `friend struct RuntimeSmokeTestAccess;`
     already grants access to the private `meshResourceMap_`/
     `materialResourceMap_` members): `meshResourceMapSize()`,
     `materialResourceMapSize()`. Assert `== 2`/`== 4`. The existing
     dynamic point-light creation/movement block (unchanged) now runs
     against a `World` that already has one directional light —
     verify its own `pointLightCount`/`pointLights[0]` assertions
     still pass unmodified (they concern only the light this block
     itself adds).
3. **New fixture.** `tests/image_regression/fixture/integrated_showcase_demo_fixture.{h,cpp}`
   — same struct/function shape as `PbrMaterialDemoFixture`/
   `setUpPbrMaterialDemoFixture()`/`renderPbrMaterialDemoFrame()`
   (`tests/image_regression/fixture/pbr_material_demo_fixture.{h,cpp}`),
   pointed at `integrated_showcase_demo`'s own cooked paths, with two
   real differences from that fixture:
   - Replace the identity light-space sentinel
     (`pbr_material_demo_fixture.cpp:99,500-501`) with a real call to
     `atlantis::runtime::computeShadowLightSpaceMatrices(direction)`,
     `direction` taken from this scene's own loaded directional light
     (the same `FrameLightingData::directionalLights[0].direction`
     this fixture already extracts via `extractFrameLightingData()`).
     Write the result into both the camera buffer's light-space tail
     (`cameraData + 116`) and the dedicated `shadowLightSpaceBuffer`
     — the exact dual-write `shadow_gpu_tests.cpp:894-902` already
     performs against the same two destinations.
   - Replace the empty `shadowCasterDrawItems` argument
     (`pbr_material_demo_fixture.cpp:606`, trailing `{}`) with the
     frame's own `drawItems` span whenever a directional light is
     present — mirroring `RuntimeApplication::runFrame()`'s own
     unconditional "`shadowCasterDrawItems` is `drawItems` itself"
     contract (Spec 0027 P6).
   - Add one field, e.g. `std::size_t lastDrawItemCount = 0;`, set to
     `drawItems.size()` right after that vector is built, for FR4's
     direct assertion (no engine-module change — a fixture-local
     field, same pattern `environmentUploadCount` already
     establishes).
   Wiring: add `integrated_showcase_demo_fixture.cpp` to
   `tests/image_regression/fixture/CMakeLists.txt`'s source list
   (mirrors line 8-9's `pbr_material_demo_fixture.cpp`/
   `ibl_material_demo_fixture.cpp` entries).
4. **Golden generator + GPU test (candidate stage — no golden
   committed yet).** `tests/image_regression/golden_generator/integrated_showcase_demo_main.cpp`
   and `tests/image_regression/integrated_showcase_demo_gpu_tests.cpp`,
   mirroring the existing per-demo pair's own shape (e.g.
   `pbr_material_demo_main.cpp`/`pbr_material_demo_gpu_tests.cpp`).
   The GPU test asserts, against this fixture, without yet requiring a
   committed golden: `renderableEntityCount`-equivalent `== 6` (via
   `fixture.world->renderableEntities().size()`),
   `meshResourceMap.size() == 2`, `materialResourceMap.size() == 4`,
   `lastDrawItemCount == 6`, and the shadow/lit-reference luminance
   check above. Wiring, mirroring lines 143-145/162-164/206-207 of
   `tests/image_regression/CMakeLists.txt` and lines 233-245/256-276
   of `tests/image_regression/golden_generator/CMakeLists.txt`: new
   source file in each directory's target, new
   `ATLANTIS_integrated_showcase_demo_scene_*`/environment compile
   definitions, new `add_dependencies()` entries
   (`${ATLANTIS_integrated_showcase_demo_scene_TARGET}`,
   `${ATLANTIS_ground_plane_TARGET}` alongside the existing
   `${ATLANTIS_pbr_sphere_TARGET}`-style entries already present for
   sibling PBR demos, `${ATLANTIS_ibl_studio_TARGET}`,
   `shadow_cast_shaders`, `sky_shaders`). Run the golden_generator
   executable to produce a candidate PNG — not committed in this
   Milestone.
5. **Golden review and commit (separate stage/commit from Milestone
   4, per ADR-0042's two-phase process).** Human reviews the
   candidate PNG generated by Milestone 4; once approved, commit
   `tests/image_regression/goldens/integrated_showcase_demo/integrated_showcase_demo_512x512_rgba8unorm.png`
   plus its auto-generated `.sidecar.txt` provenance (GPU/driver/OS
   metadata, produced by the existing capture tooling, not hand-
   authored), and flip the GPU test's own comparison from
   candidate-only to the real byte-compare mode every other demo's
   test already uses.
6. **Full verification pass.** Debug/Release, GPU-independent and
   GPU `ctest` suites; Vulkan Validation Layers clean; fresh
   `ATLANTIS_BUILD_TESTS=OFF` configure+build confirms
   `atlantis_runtime.exe` builds and its own newly-required
   `integrated_showcase_demo`/`ground_plane` cook targets are built
   as real (non-test) dependencies; a human runs `atlantis_runtime.exe`
   and visually confirms the composition (ground, 5 spheres, sky,
   shadow); module/link/`Vk*` isolation scan; `git diff --check`.

## Files / Modules Touched (expected)

- `assets/meshes/ground_plane.mesh.txt` (new)
- `assets/scenes/integrated_showcase_demo.scene.txt` (new)
- `assets/CMakeLists.txt` (two new cook declarations + PARENT_SCOPE exports)
- `src/runtime/CMakeLists.txt` (scene macro + dependency switch, lines 87-89/150)
- `tests/runtime/CMakeLists.txt` (scene macro + dependency switch, lines 72-74/117)
- `tests/runtime/runtime_smoke_gpu_tests.cpp` (config fields, updated assertions, two new `RuntimeSmokeTestAccess` methods)
- `tests/image_regression/fixture/integrated_showcase_demo_fixture.h` (new)
- `tests/image_regression/fixture/integrated_showcase_demo_fixture.cpp` (new)
- `tests/image_regression/fixture/CMakeLists.txt` (new source file)
- `tests/image_regression/golden_generator/integrated_showcase_demo_main.cpp` (new)
- `tests/image_regression/golden_generator/CMakeLists.txt` (new target + wiring)
- `tests/image_regression/integrated_showcase_demo_gpu_tests.cpp` (new)
- `tests/image_regression/CMakeLists.txt` (new source file + compile defs + dependencies)
- `tests/image_regression/goldens/integrated_showcase_demo/integrated_showcase_demo_512x512_rgba8unorm.png` (new, Milestone 5 only)
- `tests/image_regression/goldens/integrated_showcase_demo/integrated_showcase_demo_512x512_rgba8unorm.sidecar.txt` (new, auto-generated, Milestone 5 only)
- `specs/README.md` (Plan column for Spec 0028 — this Plan's own PR)

No `src/rhi/`, `src/render_graph/`, `src/renderer/`, `src/shader_system/`,
`src/asset_system/`, `src/world/`, or `src/vulkan_backend/` file is
touched. `src/runtime/include/atlantis/runtime/runtime_application.h`/`.cpp`
are not touched (Milestone 2's new accessors live entirely in the test
file, using the already-existing `friend struct RuntimeSmokeTestAccess;`).

## Sequencing & Dependencies

Milestone 1 → 2 → 3 → 4 → 5 → 6, strictly — each depends on the cooked
outputs or code the previous one adds:

- Milestone 2's CMake switch names `ATLANTIS_integrated_showcase_demo_scene_*`
  targets that only exist after Milestone 1.
- Milestone 3's fixture loads the same cooked scene Milestone 1 adds.
- Milestone 4 links against Milestone 3's fixture.
- Milestone 5 requires Milestone 4's own candidate-generation path to
  exist first.
- Milestone 6 is the final, whole-repository check, after every prior
  Milestone has landed.

## Verification Checklist

- [ ] `meshResourceMap.size() == 2`, `materialResourceMap.size() == 4`,
      `renderableEntities().size() == 6`, `lastDrawItemCount == 6` —
      asserted directly (Milestone 4's GPU test; Milestone 2's
      windowed smoke test for the first three).
- [ ] Shadow/lit-reference pixel check: `luminance(439,384) -
      luminance(158,347) > 15` (Milestone 4's GPU test).
- [ ] Windowed smoke `TEST_CASE` (the one, unchanged lifecycle):
      `renderableEntityCount == 6`; `directionalLightCount == 1` at
      both existing check points; the existing dynamic point-light
      creation/movement checks unmodified and passing.
- [ ] Real windowed visual confirmation: a human runs
      `atlantis_runtime.exe` and confirms the ground, all 5 spheres,
      sky, and a visible shadow are present.
- [ ] New golden: candidate generated (Milestone 4), human-reviewed
      and committed (Milestone 5) per ADR-0042's two-phase process —
      never auto-accepted.
- [ ] All 8 existing goldens (`minimal_cube`, `world_scene`,
      `world_scene_loaded`, `textured_quad`, `material_demo`,
      `lighting_demo`, `sky_background`, `ibl_material_demo`,
      `pbr_material_demo`, `hdr_roll_off_demo`) re-verified byte-
      identical — no recapture.
- [ ] No new descriptor-peak test: the existing `"N=6 HDR pipeline
      descriptor-set peak is exactly N+4/N+5 with both a sky and a
      shadow-cast Pipeline present"` test
      (`tests/runtime/material_realization_gpu_tests.cpp:993`) re-run
      unchanged and still passing — it already proves the ceiling
      holds at `N=6`, above this scene's own `N=4` (`steady = N+4 =
      8`, `peak = N+5 = 9`, both well under the 60-set ceiling, Spec
      0021).
- [ ] Full `ctest -LE gpu` and `ctest -L gpu`, Debug and Release.
- [ ] Vulkan Validation Layers: zero Warning/Error throughout.
- [ ] Fresh `ATLANTIS_BUILD_TESTS=OFF` configure+build: `atlantis_runtime.exe`
      builds; its own newly-required `ground_plane`/
      `integrated_showcase_demo_scene` cook targets build as real
      (non-test) dependencies, confirming the default-scene switch
      does not silently depend on test-only infrastructure.
- [ ] Module/link/include scan: no new `Vk*` symbol outside
      `src/vulkan_backend/`/`tests/vulkan_backend/`; no new inter-
      module dependency edge; `Atlantis::Renderer`/`Atlantis::RHI`
      link graphs unchanged.
- [ ] `git diff --check`: zero whitespace errors.

## Rollback Plan

Every change is additive data (new mesh/scene assets, a new
fixture/golden_generator/gpu_test trio) plus two small, mechanical
CMake macro-value substitutions and a bounded edit to one existing
test file. Reverting the two CMake macro switches
(`src/runtime/CMakeLists.txt`, `tests/runtime/CMakeLists.txt`) alone
restores `world_scene` as the default scene and the smoke test's own
prior assertions immediately; the new assets/fixture/golden files can
be deleted independently without affecting any other demo, since none
of them are referenced anywhere else. `world_scene`'s own asset,
fixture, and golden are never modified, so no separate restoration
step is needed for them.

## Definition of Done

See [docs/process/definition-of-done.md](../docs/process/definition-of-done.md).
Deltas specific to this Plan: the golden-update gate (ADR-0042) applies
to a **new** golden, not a recapture — no prior golden requires re-
review. Otherwise, none.
