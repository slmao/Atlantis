# Plan 0012 Step 4b: the one static mesh asset this Plan ships --
# transcribed exactly from
# tests/image_regression/fixture/minimal_cube_fixture.cpp's own
# kCubeVertices/kCubeIndices arrays (see assets/meshes/minimal_cube.mesh.txt
# itself for the values).
#
# Plan 0017 Section D6/ADR-0058: migrated to the version-2 grammar --
# every vertex line gained a trailing, explicit "0.0 0.0" UV0 pair.
# This is a deterministic placeholder only, never a per-face texture
# unwrap: this cube's own 8-vertex, shared-corner topology (each corner
# shared by the 3 faces meeting there) cannot express a correct
# per-face UV unwrap under any UV values, since a shared vertex can
# carry exactly one UV value, not one per face. minimal_cube continues
# to be drawn exclusively by minimal_mesh.slang, which declares no UV
# input and never reads these values -- this asset is not, and is not
# claimed to be, texture-ready.
atlantis_add_static_mesh_asset(
  NAME minimal_cube
  SOURCE meshes/minimal_cube.mesh.txt
)

# Mechanical requirement (no architectural effect), matching
# shaders/minimal_renderer/CMakeLists.txt's own identical, already-
# documented fix: the PARENT_SCOPE set() inside
# atlantis_add_static_mesh_asset() itself
# (src/asset_system/CMakeLists.txt) only reaches this directory's own
# scope -- CMake's PARENT_SCOPE always targets the immediate calling
# scope, never bubbles further up on its own. Step 6's consumer
# (tests/image_regression/CMakeLists.txt) is a sibling directory, a
# child of the root CMakeLists.txt, not of this directory, so it needs
# this second, explicit propagation up to root's own scope.
set(ATLANTIS_minimal_cube_ARTIFACT_PATH "${ATLANTIS_minimal_cube_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_minimal_cube_METADATA_PATH "${ATLANTIS_minimal_cube_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_minimal_cube_TARGET "${ATLANTIS_minimal_cube_TARGET}" PARENT_SCOPE)
# Plan 0015 Section D7: same second-propagation requirement as the
# three lines above, extended to the newly-exported LOGICAL_PATH
# variable -- a MESH_DEPENDENCIES consumer of atlantis_add_scene_asset()
# (Step 5's own test-only fixture in tests/asset_system/CMakeLists.txt,
# and Step 9's own real world_scene declaration, both sibling
# directories of this one) needs it visible at root scope to record
# minimal_cube's own logical path into a scene's generated manifest.
set(ATLANTIS_minimal_cube_LOGICAL_PATH "${ATLANTIS_minimal_cube_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0015 Section D11: the one scene asset this Plan ships --
# world_scene.scene.txt reproduces buildValidationScene()'s own former,
# now-removed six-entity validation scene byte-for-byte (every
# position/rotation/camera value transcribed directly from that
# function's own former source, not re-derived -- see Plan 0015
# Section D11). Depends on minimal_cube, already declared above.
atlantis_add_scene_asset(
  NAME world_scene
  SOURCE scenes/world_scene.scene.txt
  MESH_DEPENDENCIES minimal_cube
)
set(ATLANTIS_world_scene_ARTIFACT_PATH "${ATLANTIS_world_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_METADATA_PATH "${ATLANTIS_world_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_MANIFEST_PATH "${ATLANTIS_world_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_world_scene_TARGET "${ATLANTIS_world_scene_TARGET}" PARENT_SCOPE)

# Plan 0016 Section D8/D11, corrected by the Plan's own "Human Review
# Correction -- 2026-08-24": the textured fixture's own two textures are
# two logically independent, byte-identical source PNGs (proven by
# tests/asset_system/texture_fixture_source_identity_tests.cpp), each
# with its own distinct SOURCE/NAME/COLOR_SPACE -- not one PNG cooked
# twice under a shared SOURCE. Two distinct SOURCE values give the two
# cooked artifacts two distinct logical paths and therefore two distinct
# AssetIds, restoring ADR-0044's one-asset-one-AssetId contract; the
# collision-detector bypass this used to require (a shared SOURCE
# registered only once) no longer applies and has been removed from
# atlantis_add_texture_asset() below. Declared here, unconditionally
# (like minimal_cube/world_scene above), rather than inside the
# ATLANTIS_BUILD_TESTS block tests/image_regression/fixture/ itself
# lives in -- this call site must precede
# atlantis_finalize_asset_validation()'s own call (below, this file's
# caller), which reads the accumulated declared-asset-paths property at
# CMake configure time, not build time; a declaration added only inside
# the later ATLANTIS_BUILD_TESTS block would never be seen by that read.
atlantis_add_texture_asset(
  NAME textured_quad_unorm
  SOURCE textures/textured_quad_source_unorm.png
  COLOR_SPACE Unorm
)
set(ATLANTIS_textured_quad_unorm_ARTIFACT_PATH "${ATLANTIS_textured_quad_unorm_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_unorm_METADATA_PATH "${ATLANTIS_textured_quad_unorm_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_unorm_TARGET "${ATLANTIS_textured_quad_unorm_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME textured_quad_srgb
  SOURCE textures/textured_quad_source_srgb.png
  COLOR_SPACE Srgb
)

# Spec 0038/Plan 0038 Milestone 3: the two BC7 test fixtures (dual-quad
# golden). No COLOR_SPACE -- the DDS file's own DXGI format is the sole
# color-space authority on the cooker's DDS path. Test-side assets, not
# packaged app assets (no Android lock-step entries, Plan 0038).
atlantis_add_texture_asset(
  NAME bc7_stringlights_unorm
  SOURCE textures/paris_stringlights_diff.dds
)
set(ATLANTIS_bc7_stringlights_unorm_ARTIFACT_PATH "${ATLANTIS_bc7_stringlights_unorm_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_bc7_stringlights_unorm_METADATA_PATH "${ATLANTIS_bc7_stringlights_unorm_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_bc7_stringlights_unorm_TARGET "${ATLANTIS_bc7_stringlights_unorm_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME bc7_stringlights_srgb
  SOURCE textures/paris_stringlights_diff_srgbvariant.dds
)
set(ATLANTIS_bc7_stringlights_srgb_ARTIFACT_PATH "${ATLANTIS_bc7_stringlights_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_bc7_stringlights_srgb_METADATA_PATH "${ATLANTIS_bc7_stringlights_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_bc7_stringlights_srgb_TARGET "${ATLANTIS_bc7_stringlights_srgb_TARGET}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_srgb_ARTIFACT_PATH "${ATLANTIS_textured_quad_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_srgb_METADATA_PATH "${ATLANTIS_textured_quad_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_srgb_TARGET "${ATLANTIS_textured_quad_srgb_TARGET}" PARENT_SCOPE)

# Plan 0017 Section D7: two independent, non-shared-vertex quad mesh
# assets for the textured fixture (Milestone 3 below), transcribed
# value-for-value from textured_quad_fixture.cpp's own former
# kLeftQuadVertices/kRightQuadVertices arrays (including their baked
# left/right clip-space positions) -- not one shared quad mesh plus an
# objectToWorld translation, since both DrawItems already use an
# identity objectToWorld and introducing new GPU-side matrix-multiply
# arithmetic here would risk the zero-pixel-difference goal against the
# existing textured_quad golden for no benefit. Color is a deterministic
# "1.0 1.0 1.0" placeholder -- textured_quad.slang has no color input
# and never reads it. Declared here, unconditionally (like minimal_cube/
# world_scene/the two textures above), for the same
# atlantis_finalize_asset_validation() configure-time-visibility reason
# documented on the texture declarations above.
atlantis_add_static_mesh_asset(
  NAME textured_quad_left
  SOURCE meshes/textured_quad_left.mesh.txt
)
set(ATLANTIS_textured_quad_left_ARTIFACT_PATH "${ATLANTIS_textured_quad_left_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_left_METADATA_PATH "${ATLANTIS_textured_quad_left_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_left_TARGET "${ATLANTIS_textured_quad_left_TARGET}" PARENT_SCOPE)

atlantis_add_static_mesh_asset(
  NAME textured_quad_right
  SOURCE meshes/textured_quad_right.mesh.txt
)
set(ATLANTIS_textured_quad_right_ARTIFACT_PATH "${ATLANTIS_textured_quad_right_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_right_METADATA_PATH "${ATLANTIS_textured_quad_right_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_textured_quad_right_TARGET "${ATLANTIS_textured_quad_right_TARGET}" PARENT_SCOPE)

# Plan 0018 Milestone 15: the first real material asset -- reuses Spec
# 0016's already-cooked, already-proven textured_quad_unorm texture (no
# new PNG). Declared here, unconditionally, so it is available to
# material_demo_scene below and to tests/image_regression/'s own
# Milestone 16 fixture.
atlantis_add_material_asset(
  NAME unlit_textured_quad
  SOURCE materials/unlit_textured_quad.material.txt
  TEXTURE textured_quad_unorm
)
set(ATLANTIS_unlit_textured_quad_ARTIFACT_PATH "${ATLANTIS_unlit_textured_quad_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_unlit_textured_quad_METADATA_PATH "${ATLANTIS_unlit_textured_quad_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_unlit_textured_quad_TARGET "${ATLANTIS_unlit_textured_quad_TARGET}" PARENT_SCOPE)
set(ATLANTIS_unlit_textured_quad_LOGICAL_PATH "${ATLANTIS_unlit_textured_quad_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0018 Milestone 15 (Spec 0018 D12): the first real, end-to-end
# scene->material->texture->UV-mesh scene -- two nodes reusing Spec
# 0017's own textured_quad_left/textured_quad_right UV-carrying mesh
# assets (no new mesh authored), both referencing the SAME material
# AssetId (the required proof of D10's own dedup claim: a single
# loadMaterialAsset()/loadTextureAsset() call and a single realized GPU
# resource, shared). This scene is deliberately NOT wired into
# atlantis_runtime's own BootstrapConfig (Section P15's own explicit
# decision) -- verification-only, consumed by Milestone 16's own new
# fixture.
atlantis_add_scene_asset(
  NAME material_demo_scene
  SOURCE scenes/material_demo.scene.txt
  MESH_DEPENDENCIES textured_quad_left textured_quad_right
  MATERIAL_DEPENDENCIES unlit_textured_quad
  TEXTURE_DEPENDENCIES textured_quad_unorm
)
set(ATLANTIS_material_demo_scene_ARTIFACT_PATH "${ATLANTIS_material_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_material_demo_scene_METADATA_PATH "${ATLANTIS_material_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_material_demo_scene_MANIFEST_PATH "${ATLANTIS_material_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_material_demo_scene_TARGET "${ATLANTIS_material_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0019 Section P10: the first real MaterialKind::LitTextured
# material asset -- reuses Spec 0016's already-cooked, already-proven
# textured_quad_unorm texture (no new PNG), matching unlit_textured_quad's
# own identical reuse above. Declared here, unconditionally, mirroring
# unlit_textured_quad's own placement exactly -- test-only consumer
# (the new lighting_demo_fixture, Milestone 10), never wired into
# atlantis_runtime's own BootstrapConfig (D10's own explicit "Runtime's
# own default bootstrap scene stays world_scene" decision).
atlantis_add_material_asset(
  NAME lit_textured_quad
  SOURCE materials/lit_textured_quad.material.txt
  TEXTURE textured_quad_unorm
)
set(ATLANTIS_lit_textured_quad_ARTIFACT_PATH "${ATLANTIS_lit_textured_quad_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_lit_textured_quad_METADATA_PATH "${ATLANTIS_lit_textured_quad_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_lit_textured_quad_TARGET "${ATLANTIS_lit_textured_quad_TARGET}" PARENT_SCOPE)
set(ATLANTIS_lit_textured_quad_LOGICAL_PATH "${ATLANTIS_lit_textured_quad_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0019 Section P10 (Spec 0019 D10): a new, independent verification
# scene -- never a reuse of material_demo_scene/world_scene. Reuses
# minimal_cube (already carrying real, Spec-0020-sourced per-vertex
# normals) for the one LitTextured-bound Renderable; a Directional light
# aimed via its own rotation (never axis-aligned, so a sign/swap error
# is visible) and a Point light positioned close to one distinct corner
# of the cube (never centered) -- an asymmetric composition, so a
# direction, position, or attenuation-formula error each produce a
# visibly different, wrong frame (see the scene source's own values).
atlantis_add_scene_asset(
  NAME lighting_demo_scene
  SOURCE scenes/lighting_demo.scene.txt
  MESH_DEPENDENCIES minimal_cube
  MATERIAL_DEPENDENCIES lit_textured_quad
  TEXTURE_DEPENDENCIES textured_quad_unorm
)
set(ATLANTIS_lighting_demo_scene_ARTIFACT_PATH "${ATLANTIS_lighting_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_lighting_demo_scene_METADATA_PATH "${ATLANTIS_lighting_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_lighting_demo_scene_MANIFEST_PATH "${ATLANTIS_lighting_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_lighting_demo_scene_TARGET "${ATLANTIS_lighting_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0023 Milestone 6 (Spec 0023 D16): one non-shared-vertex UV-sphere
# (16x24 bands, poles duplicated per-longitude-segment, never collapsed;
# see assets/meshes/pbr_sphere.mesh.txt's own generation for the exact
# topology) -- the one mesh all four new PbrDirectLit material assets
# below share, via four distinct transforms.
atlantis_add_static_mesh_asset(
  NAME pbr_sphere
  SOURCE meshes/pbr_sphere.mesh.txt
)
set(ATLANTIS_pbr_sphere_ARTIFACT_PATH "${ATLANTIS_pbr_sphere_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sphere_METADATA_PATH "${ATLANTIS_pbr_sphere_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sphere_TARGET "${ATLANTIS_pbr_sphere_TARGET}" PARENT_SCOPE)

# Plan 0023 Milestone 6 (ADR-0066/D17): four MaterialKind::PbrDirectLit
# material assets spanning dielectric/metallic x rough/smooth --
# reusing the already-cooked, already-Rgba8Srgb textured_quad_srgb
# texture (Spec 0016), never a new PNG. base_color_factor is left at
# its own default (1,1,1,1) for all four -- no real, disclosed reason
# to vary it was found during Implementation; metallic_factor/
# roughness_factor alone provide the four corners' own discriminating
# power (D17).
atlantis_add_material_asset(
  NAME pbr_dielectric_rough
  SOURCE materials/pbr_dielectric_rough.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_dielectric_rough_ARTIFACT_PATH "${ATLANTIS_pbr_dielectric_rough_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_dielectric_rough_METADATA_PATH "${ATLANTIS_pbr_dielectric_rough_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_dielectric_rough_TARGET "${ATLANTIS_pbr_dielectric_rough_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_dielectric_rough_LOGICAL_PATH "${ATLANTIS_pbr_dielectric_rough_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_dielectric_smooth
  SOURCE materials/pbr_dielectric_smooth.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_dielectric_smooth_ARTIFACT_PATH "${ATLANTIS_pbr_dielectric_smooth_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_dielectric_smooth_METADATA_PATH "${ATLANTIS_pbr_dielectric_smooth_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_dielectric_smooth_TARGET "${ATLANTIS_pbr_dielectric_smooth_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_dielectric_smooth_LOGICAL_PATH "${ATLANTIS_pbr_dielectric_smooth_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_metallic_rough
  SOURCE materials/pbr_metallic_rough.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_metallic_rough_ARTIFACT_PATH "${ATLANTIS_pbr_metallic_rough_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_metallic_rough_METADATA_PATH "${ATLANTIS_pbr_metallic_rough_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_metallic_rough_TARGET "${ATLANTIS_pbr_metallic_rough_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_metallic_rough_LOGICAL_PATH "${ATLANTIS_pbr_metallic_rough_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_metallic_smooth
  SOURCE materials/pbr_metallic_smooth.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_metallic_smooth_ARTIFACT_PATH "${ATLANTIS_pbr_metallic_smooth_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_metallic_smooth_METADATA_PATH "${ATLANTIS_pbr_metallic_smooth_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_metallic_smooth_TARGET "${ATLANTIS_pbr_metallic_smooth_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_metallic_smooth_LOGICAL_PATH "${ATLANTIS_pbr_metallic_smooth_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0023 Milestone 6 (Spec 0023 D17): a new, independent verification
# scene -- four pbr_sphere nodes (one per material above, an asymmetric
# 2x2 grid, never centered-and-identical) plus one Directional and one
# Point light (asymmetric direction/position, matching lighting_demo_scene's
# own identical "never axis-aligned/centered" precedent) and one camera.
# Deliberately NOT wired into atlantis_runtime's own BootstrapConfig
# (matching material_demo_scene's/lighting_demo_scene's own identical
# precedent) -- verification-only, consumed by Milestone 8's own new
# fixture.
atlantis_add_scene_asset(
  NAME pbr_material_demo_scene
  SOURCE scenes/pbr_material_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_dielectric_rough pbr_dielectric_smooth pbr_metallic_rough pbr_metallic_smooth
  TEXTURE_DEPENDENCIES textured_quad_srgb
)
set(ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH "${ATLANTIS_pbr_material_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_material_demo_scene_METADATA_PATH "${ATLANTIS_pbr_material_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_material_demo_scene_MANIFEST_PATH "${ATLANTIS_pbr_material_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_material_demo_scene_TARGET "${ATLANTIS_pbr_material_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0025/M9: the same four PBR material corners under environment-only
# lighting. No Light node is present by construction.
atlantis_add_scene_asset(
  NAME ibl_material_demo_scene
  SOURCE scenes/ibl_material_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_dielectric_rough pbr_dielectric_smooth pbr_metallic_rough pbr_metallic_smooth
  TEXTURE_DEPENDENCIES textured_quad_srgb
)
set(ATLANTIS_ibl_material_demo_scene_ARTIFACT_PATH "${ATLANTIS_ibl_material_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_ibl_material_demo_scene_METADATA_PATH "${ATLANTIS_ibl_material_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_ibl_material_demo_scene_MANIFEST_PATH "${ATLANTIS_ibl_material_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_ibl_material_demo_scene_TARGET "${ATLANTIS_ibl_material_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0024 Milestone 10: a focused HDR roll-off baseline. One existing
# rough-dielectric PBR sphere is lit by one deliberately over-range
# Directional light; the scene is verification-only and is not wired
# into Runtime's default BootstrapConfig.
atlantis_add_scene_asset(
  NAME hdr_roll_off_demo_scene
  SOURCE scenes/hdr_roll_off_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_dielectric_rough
  TEXTURE_DEPENDENCIES textured_quad_srgb
)
set(ATLANTIS_hdr_roll_off_demo_scene_ARTIFACT_PATH "${ATLANTIS_hdr_roll_off_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_hdr_roll_off_demo_scene_METADATA_PATH "${ATLANTIS_hdr_roll_off_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_hdr_roll_off_demo_scene_MANIFEST_PATH "${ATLANTIS_hdr_roll_off_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_hdr_roll_off_demo_scene_TARGET "${ATLANTIS_hdr_roll_off_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0025 Milestone 2: repository-authored linear HDR studio environment.
atlantis_add_environment_asset(
  NAME ibl_studio
  SOURCE environments/ibl_studio_source.hdr
)
set(ATLANTIS_ibl_studio_ARTIFACT_PATH "${ATLANTIS_ibl_studio_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_ibl_studio_METADATA_PATH "${ATLANTIS_ibl_studio_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_ibl_studio_TARGET "${ATLANTIS_ibl_studio_TARGET}" PARENT_SCOPE)
set(ATLANTIS_ibl_studio_LOGICAL_PATH "${ATLANTIS_ibl_studio_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0028 Milestone 1: a flat 10x10 ground plane -- the one real asset
# gap Spec 0028 found (no cooked, World-loadable floor mesh existed).
atlantis_add_static_mesh_asset(
  NAME ground_plane
  SOURCE meshes/ground_plane.mesh.txt
)
set(ATLANTIS_ground_plane_ARTIFACT_PATH "${ATLANTIS_ground_plane_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_ground_plane_METADATA_PATH "${ATLANTIS_ground_plane_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_ground_plane_TARGET "${ATLANTIS_ground_plane_TARGET}" PARENT_SCOPE)

# Plan 0028 Milestone 1: the integrated showcase scene -- one ground_plane
# entity, five pbr_sphere entities reusing the four existing PBR
# materials (two reused pairs: Sphere E/Sphere D share pbr_metallic_smooth,
# Ground/Sphere A share pbr_dielectric_rough), one directional light, and
# one camera. Becomes atlantis_runtime's own default scene (Milestone 2).
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

# Plan 0040 Milestone 3 (Spec 0040 Req 7): the multi-light verification
# scene -- eight Point lights, more than the former four-light cap, on
# the lit_textured path (one of the five point-light-iterating shaders,
# P4). ground_plane under lit_textured_quad shows the texture's 4x4
# red/white checker; each light sits 0.3 above the centre of one of the
# eight WHITE cells with range 1.2, so its pool (radius ~1.16) stays
# inside its own 2.5-unit cell: eight pure, non-overlapping, individually
# identifiable contributions, and lit_textured has no ambient term, so
# the eight red cells stay black. No directional light.
atlantis_add_scene_asset(
  NAME multi_light_demo_scene
  SOURCE scenes/multi_light_demo.scene.txt
  MESH_DEPENDENCIES ground_plane
  MATERIAL_DEPENDENCIES lit_textured_quad
  TEXTURE_DEPENDENCIES textured_quad_unorm
)
set(ATLANTIS_multi_light_demo_scene_ARTIFACT_PATH "${ATLANTIS_multi_light_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_multi_light_demo_scene_METADATA_PATH "${ATLANTIS_multi_light_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_multi_light_demo_scene_MANIFEST_PATH "${ATLANTIS_multi_light_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_multi_light_demo_scene_TARGET "${ATLANTIS_multi_light_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0029 Section P19 (ADR-0074): the fixed 4x4 RGB8 Unorm normal-map
# source texture -- all 16 pixels exactly (204,128,230), generated via a
# temporary, uncommitted stb_image_write helper (never a permanent
# generator target).
atlantis_add_texture_asset(
  NAME normal_map_tilted
  SOURCE textures/normal_map_tilted_source_unorm.png
  COLOR_SPACE Unorm
)
set(ATLANTIS_normal_map_tilted_ARTIFACT_PATH "${ATLANTIS_normal_map_tilted_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_normal_map_tilted_METADATA_PATH "${ATLANTIS_normal_map_tilted_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_normal_map_tilted_TARGET "${ATLANTIS_normal_map_tilted_TARGET}" PARENT_SCOPE)

# Plan 0029 Section P19: the normal-map demo's own two PbrDirectLit
# material assets -- pbr_normal_mapped declares the new normal_map:
# line (NORMAL_MAP arg); pbr_normal_mapped_control is byte-identical
# minus that line, the discriminative test's own "B" twin, referenced
# by zero scene nodes (see the scene declaration below).
atlantis_add_material_asset(
  NAME pbr_normal_mapped
  SOURCE materials/pbr_normal_mapped.material.txt
  TEXTURE textured_quad_srgb
  NORMAL_MAP normal_map_tilted
)
set(ATLANTIS_pbr_normal_mapped_ARTIFACT_PATH "${ATLANTIS_pbr_normal_mapped_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_mapped_METADATA_PATH "${ATLANTIS_pbr_normal_mapped_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_mapped_TARGET "${ATLANTIS_pbr_normal_mapped_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_mapped_LOGICAL_PATH "${ATLANTIS_pbr_normal_mapped_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_normal_mapped_control
  SOURCE materials/pbr_normal_mapped_control.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH "${ATLANTIS_pbr_normal_mapped_control_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_mapped_control_METADATA_PATH "${ATLANTIS_pbr_normal_mapped_control_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_mapped_control_TARGET "${ATLANTIS_pbr_normal_mapped_control_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_mapped_control_LOGICAL_PATH "${ATLANTIS_pbr_normal_mapped_control_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0029 Section P19: the normal-map demo scene -- one pbr_sphere
# node bound to pbr_normal_mapped, one ground_plane shadow receiver
# bound to the existing pbr_dielectric_rough, one Directional light,
# one camera. Both materials/textures are declared under
# MATERIAL_DEPENDENCIES/TEXTURE_DEPENDENCIES even though
# pbr_normal_mapped_control is referenced by zero scene nodes -- the
# same "declared but only transitively used" tolerance
# MESH_DEPENDENCIES/TEXTURE_DEPENDENCIES already establish elsewhere in
# this file; the fixture's own setup phase resolves and realizes the
# control material independently (see
# tests/image_regression/fixture/pbr_normal_map_demo_fixture.cpp).
atlantis_add_scene_asset(
  NAME pbr_normal_map_demo_scene
  SOURCE scenes/pbr_normal_map_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere ground_plane
  MATERIAL_DEPENDENCIES pbr_normal_mapped pbr_normal_mapped_control pbr_dielectric_rough
  TEXTURE_DEPENDENCIES textured_quad_srgb normal_map_tilted
)
set(ATLANTIS_pbr_normal_map_demo_scene_ARTIFACT_PATH "${ATLANTIS_pbr_normal_map_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_map_demo_scene_METADATA_PATH "${ATLANTIS_pbr_normal_map_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_map_demo_scene_MANIFEST_PATH "${ATLANTIS_pbr_normal_map_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_normal_map_demo_scene_TARGET "${ATLANTIS_pbr_normal_map_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0041 Milestone 3 (Spec 0041 R6): the emissive dark-scene golden --
# four emissive PbrDirectLit materials (three plain, one normal-mapped, so
# both direct-lit shader variants are covered; orange carries a component
# above 1 so HDR input reaches the tonemap) and the existing, non-emissive
# pbr_normal_mapped_control as the black control sphere. No light, no
# environment: the PBR shader output is then exactly emissiveFactor
# (pbr_direct_lit has no ambient term). Rendered by PbrNormalMapDemoFixture
# reused by alias (Plan 0041 P5/Q1); the control material doubles as that
# fixture's own required control, and every material shares its
# textured_quad_srgb base texture.
atlantis_add_material_asset(
  NAME emissive_demo_orange
  SOURCE materials/emissive_demo_orange.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_emissive_demo_orange_ARTIFACT_PATH "${ATLANTIS_emissive_demo_orange_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_orange_METADATA_PATH "${ATLANTIS_emissive_demo_orange_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_orange_TARGET "${ATLANTIS_emissive_demo_orange_TARGET}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_orange_LOGICAL_PATH "${ATLANTIS_emissive_demo_orange_LOGICAL_PATH}" PARENT_SCOPE)
atlantis_add_material_asset(
  NAME emissive_demo_green
  SOURCE materials/emissive_demo_green.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_emissive_demo_green_ARTIFACT_PATH "${ATLANTIS_emissive_demo_green_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_green_METADATA_PATH "${ATLANTIS_emissive_demo_green_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_green_TARGET "${ATLANTIS_emissive_demo_green_TARGET}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_green_LOGICAL_PATH "${ATLANTIS_emissive_demo_green_LOGICAL_PATH}" PARENT_SCOPE)
atlantis_add_material_asset(
  NAME emissive_demo_blue
  SOURCE materials/emissive_demo_blue.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_emissive_demo_blue_ARTIFACT_PATH "${ATLANTIS_emissive_demo_blue_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_blue_METADATA_PATH "${ATLANTIS_emissive_demo_blue_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_blue_TARGET "${ATLANTIS_emissive_demo_blue_TARGET}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_blue_LOGICAL_PATH "${ATLANTIS_emissive_demo_blue_LOGICAL_PATH}" PARENT_SCOPE)
atlantis_add_material_asset(
  NAME emissive_demo_normal_mapped
  SOURCE materials/emissive_demo_normal_mapped.material.txt
  TEXTURE textured_quad_srgb
  NORMAL_MAP normal_map_tilted
)
set(ATLANTIS_emissive_demo_normal_mapped_ARTIFACT_PATH "${ATLANTIS_emissive_demo_normal_mapped_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_normal_mapped_METADATA_PATH "${ATLANTIS_emissive_demo_normal_mapped_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_normal_mapped_TARGET "${ATLANTIS_emissive_demo_normal_mapped_TARGET}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_normal_mapped_LOGICAL_PATH "${ATLANTIS_emissive_demo_normal_mapped_LOGICAL_PATH}" PARENT_SCOPE)
atlantis_add_scene_asset(
  NAME emissive_demo_scene
  SOURCE scenes/emissive_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES emissive_demo_orange emissive_demo_green emissive_demo_blue emissive_demo_normal_mapped
    pbr_normal_mapped_control
  TEXTURE_DEPENDENCIES textured_quad_srgb normal_map_tilted
)
set(ATLANTIS_emissive_demo_scene_ARTIFACT_PATH "${ATLANTIS_emissive_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_scene_METADATA_PATH "${ATLANTIS_emissive_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_scene_MANIFEST_PATH "${ATLANTIS_emissive_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_emissive_demo_scene_TARGET "${ATLANTIS_emissive_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 1 (Spec 0035 Requirement 6): two new meshes for the
# showcase scene -- a dimpled test sphere (a concave spherical-cap dent
# at the +Y pole, smoothstep-blended so the transition has zero slope at
# both the pole and the dimple boundary, matching Filament's own
# example_materials1.jpg reference-image test-ball geometry) and a small
# lathed pedestal/stand. Both generated by a one-off, uncommitted script
# (generate_pbr_dimpled_sphere_and_pedestal.py, mirrors
# generate_ibl_studio_source.ps1's own "never a permanent generator
# target" precedent) emitting atlantis_static_mesh_source_version: 3
# text directly -- tangents are not authored in that text; cook.cpp's
# own generateTangents() computes them at cook time, same as every other
# mesh in this file.
atlantis_add_static_mesh_asset(
  NAME pbr_dimpled_sphere
  SOURCE meshes/pbr_dimpled_sphere.mesh.txt
)
set(ATLANTIS_pbr_dimpled_sphere_ARTIFACT_PATH "${ATLANTIS_pbr_dimpled_sphere_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_dimpled_sphere_METADATA_PATH "${ATLANTIS_pbr_dimpled_sphere_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_dimpled_sphere_TARGET "${ATLANTIS_pbr_dimpled_sphere_TARGET}" PARENT_SCOPE)

atlantis_add_static_mesh_asset(
  NAME pbr_pedestal
  SOURCE meshes/pbr_pedestal.mesh.txt
)
set(ATLANTIS_pbr_pedestal_ARTIFACT_PATH "${ATLANTIS_pbr_pedestal_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_pedestal_METADATA_PATH "${ATLANTIS_pbr_pedestal_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_pedestal_TARGET "${ATLANTIS_pbr_pedestal_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 1 (Spec 0035 Requirement 6): six CC0-sourced PBR
# albedo textures (metal/wood/stone-marble/tile-mosaic/carbon-fiber-
# weave/weathered-concrete-ground -- Poly Haven and ambientCG, both
# site-wide CC0 1.0 Universal; per-asset provenance in this directory's
# own <name>.provenance.txt sidecars, matching
# environments/ibl_studio_source.provenance.txt's own established
# format) plus one paired normal map for the carbon-fiber weave (Spec
# 0035 Requirement 1's own clearcoat-over-weave example). Only the
# albedo (and, for carbon fiber, normal) map is sourced -- this engine's
# MaterialAssetData has no per-pixel roughness/metalness map slot
# (metallicFactor/roughnessFactor are scalar-only, ADR-0066), so the
# source texture sets' own roughness/metalness/AO maps are not used.
atlantis_add_texture_asset(
  NAME metal_rust_srgb
  SOURCE textures/metal_rust_source_srgb.png
  COLOR_SPACE Srgb
)
set(ATLANTIS_metal_rust_srgb_ARTIFACT_PATH "${ATLANTIS_metal_rust_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_metal_rust_srgb_METADATA_PATH "${ATLANTIS_metal_rust_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_metal_rust_srgb_TARGET "${ATLANTIS_metal_rust_srgb_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME wood_dark_srgb
  SOURCE textures/wood_dark_source_srgb.png
  COLOR_SPACE Srgb
)
set(ATLANTIS_wood_dark_srgb_ARTIFACT_PATH "${ATLANTIS_wood_dark_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_wood_dark_srgb_METADATA_PATH "${ATLANTIS_wood_dark_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_wood_dark_srgb_TARGET "${ATLANTIS_wood_dark_srgb_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME marble_srgb
  SOURCE textures/marble_source_srgb.png
  COLOR_SPACE Srgb
)
set(ATLANTIS_marble_srgb_ARTIFACT_PATH "${ATLANTIS_marble_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_marble_srgb_METADATA_PATH "${ATLANTIS_marble_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_marble_srgb_TARGET "${ATLANTIS_marble_srgb_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME mosaic_tile_srgb
  SOURCE textures/mosaic_tile_source_srgb.png
  COLOR_SPACE Srgb
)
set(ATLANTIS_mosaic_tile_srgb_ARTIFACT_PATH "${ATLANTIS_mosaic_tile_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_mosaic_tile_srgb_METADATA_PATH "${ATLANTIS_mosaic_tile_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_mosaic_tile_srgb_TARGET "${ATLANTIS_mosaic_tile_srgb_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME ground_concrete_srgb
  SOURCE textures/ground_concrete_source_srgb.png
  COLOR_SPACE Srgb
)
set(ATLANTIS_ground_concrete_srgb_ARTIFACT_PATH "${ATLANTIS_ground_concrete_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_ground_concrete_srgb_METADATA_PATH "${ATLANTIS_ground_concrete_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_ground_concrete_srgb_TARGET "${ATLANTIS_ground_concrete_srgb_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME carbon_fiber_srgb
  SOURCE textures/carbon_fiber_source_srgb.png
  COLOR_SPACE Srgb
)
set(ATLANTIS_carbon_fiber_srgb_ARTIFACT_PATH "${ATLANTIS_carbon_fiber_srgb_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_carbon_fiber_srgb_METADATA_PATH "${ATLANTIS_carbon_fiber_srgb_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_carbon_fiber_srgb_TARGET "${ATLANTIS_carbon_fiber_srgb_TARGET}" PARENT_SCOPE)

atlantis_add_texture_asset(
  NAME carbon_fiber_normal_unorm
  SOURCE textures/carbon_fiber_normal_source_unorm.png
  COLOR_SPACE Unorm
)
set(ATLANTIS_carbon_fiber_normal_unorm_ARTIFACT_PATH "${ATLANTIS_carbon_fiber_normal_unorm_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_carbon_fiber_normal_unorm_METADATA_PATH "${ATLANTIS_carbon_fiber_normal_unorm_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_carbon_fiber_normal_unorm_TARGET "${ATLANTIS_carbon_fiber_normal_unorm_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 1 (Spec 0035 Requirement 6): the showcase scene's
# own warehouse-interior HDRI (Poly Haven "Empty Warehouse 01", CC0 1.0
# Universal -- environments/warehouse_interior_source.provenance.txt).
# Unlike ibl_studio_source.hdr (repository-authored, purely analytic,
# see that asset's own provenance.txt), this is a real, externally-
# sourced photographed environment -- the first one in this repository.
atlantis_add_environment_asset(
  NAME warehouse_interior
  SOURCE environments/warehouse_interior_source.hdr
)
set(ATLANTIS_warehouse_interior_ARTIFACT_PATH "${ATLANTIS_warehouse_interior_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_warehouse_interior_METADATA_PATH "${ATLANTIS_warehouse_interior_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_warehouse_interior_TARGET "${ATLANTIS_warehouse_interior_TARGET}" PARENT_SCOPE)
set(ATLANTIS_warehouse_interior_LOGICAL_PATH "${ATLANTIS_warehouse_interior_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0035 Milestone 2 (Spec 0035 Requirement 4/Testing Plan): four
# MaterialKind::PbrClearcoat material assets -- a clearcoat-roughness
# sweep at clearcoat_factor=1.0 (low_roughness/high_roughness), a
# clearcoat_factor=0.0 control (no_coat), and one normal-mapped variant
# -- all reusing the already-cooked textured_quad_srgb texture (and,
# for the last one, the already-cooked normal_map_tilted texture),
# never a new PNG, mirroring pbr_dielectric_rough/etc.'s own established
# "reuse the existing texture" precedent.
atlantis_add_material_asset(
  NAME pbr_clearcoat_no_coat
  SOURCE materials/pbr_clearcoat_no_coat.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_clearcoat_no_coat_ARTIFACT_PATH "${ATLANTIS_pbr_clearcoat_no_coat_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_no_coat_METADATA_PATH "${ATLANTIS_pbr_clearcoat_no_coat_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_no_coat_TARGET "${ATLANTIS_pbr_clearcoat_no_coat_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_no_coat_LOGICAL_PATH "${ATLANTIS_pbr_clearcoat_no_coat_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_clearcoat_low_roughness
  SOURCE materials/pbr_clearcoat_low_roughness.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_clearcoat_low_roughness_ARTIFACT_PATH "${ATLANTIS_pbr_clearcoat_low_roughness_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_low_roughness_METADATA_PATH "${ATLANTIS_pbr_clearcoat_low_roughness_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_low_roughness_TARGET "${ATLANTIS_pbr_clearcoat_low_roughness_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_low_roughness_LOGICAL_PATH "${ATLANTIS_pbr_clearcoat_low_roughness_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_clearcoat_high_roughness
  SOURCE materials/pbr_clearcoat_high_roughness.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_clearcoat_high_roughness_ARTIFACT_PATH "${ATLANTIS_pbr_clearcoat_high_roughness_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_high_roughness_METADATA_PATH "${ATLANTIS_pbr_clearcoat_high_roughness_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_high_roughness_TARGET "${ATLANTIS_pbr_clearcoat_high_roughness_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_high_roughness_LOGICAL_PATH "${ATLANTIS_pbr_clearcoat_high_roughness_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_clearcoat_normal_mapped
  SOURCE materials/pbr_clearcoat_normal_mapped.material.txt
  TEXTURE textured_quad_srgb
  NORMAL_MAP normal_map_tilted
)
set(ATLANTIS_pbr_clearcoat_normal_mapped_ARTIFACT_PATH "${ATLANTIS_pbr_clearcoat_normal_mapped_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_normal_mapped_METADATA_PATH "${ATLANTIS_pbr_clearcoat_normal_mapped_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_normal_mapped_TARGET "${ATLANTIS_pbr_clearcoat_normal_mapped_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_normal_mapped_LOGICAL_PATH "${ATLANTIS_pbr_clearcoat_normal_mapped_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0035 Milestone 2: the clearcoat parameter-sweep scene (no_coat/
# low_roughness/high_roughness, clearcoat_factor sweep + clearcoat_
# roughness sweep) -- IBL-only, no Light node, mirroring
# ibl_material_demo_scene's own identical "zero Light entities"
# precedent.
atlantis_add_scene_asset(
  NAME pbr_clearcoat_demo_scene
  SOURCE scenes/pbr_clearcoat_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_clearcoat_no_coat pbr_clearcoat_low_roughness pbr_clearcoat_high_roughness
  TEXTURE_DEPENDENCIES textured_quad_srgb
)
set(ATLANTIS_pbr_clearcoat_demo_scene_ARTIFACT_PATH "${ATLANTIS_pbr_clearcoat_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_demo_scene_METADATA_PATH "${ATLANTIS_pbr_clearcoat_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_demo_scene_MANIFEST_PATH "${ATLANTIS_pbr_clearcoat_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_demo_scene_TARGET "${ATLANTIS_pbr_clearcoat_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 2: the clearcoat normal-map variant's own
# dedicated scene -- one sphere, the pbr_clearcoat_ibl_normal_map
# shader pair's own real coverage.
atlantis_add_scene_asset(
  NAME pbr_clearcoat_normal_map_demo_scene
  SOURCE scenes/pbr_clearcoat_normal_map_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_clearcoat_normal_mapped
  TEXTURE_DEPENDENCIES textured_quad_srgb normal_map_tilted
)
set(ATLANTIS_pbr_clearcoat_normal_map_demo_scene_ARTIFACT_PATH "${ATLANTIS_pbr_clearcoat_normal_map_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_normal_map_demo_scene_METADATA_PATH "${ATLANTIS_pbr_clearcoat_normal_map_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_normal_map_demo_scene_MANIFEST_PATH "${ATLANTIS_pbr_clearcoat_normal_map_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_clearcoat_normal_map_demo_scene_TARGET "${ATLANTIS_pbr_clearcoat_normal_map_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 3: the sheen-roughness parameter-sweep scene's own
# three materials -- all reusing the already-cooked textured_quad_srgb
# texture (and, for the normal-mapped one, the already-cooked
# normal_map_tilted texture), mirroring the clearcoat materials' own
# identical "reuse the existing texture" precedent immediately above.
atlantis_add_material_asset(
  NAME pbr_sheen_low_roughness
  SOURCE materials/pbr_sheen_low_roughness.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_sheen_low_roughness_ARTIFACT_PATH "${ATLANTIS_pbr_sheen_low_roughness_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_low_roughness_METADATA_PATH "${ATLANTIS_pbr_sheen_low_roughness_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_low_roughness_TARGET "${ATLANTIS_pbr_sheen_low_roughness_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_low_roughness_LOGICAL_PATH "${ATLANTIS_pbr_sheen_low_roughness_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_sheen_mid_roughness
  SOURCE materials/pbr_sheen_mid_roughness.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_sheen_mid_roughness_ARTIFACT_PATH "${ATLANTIS_pbr_sheen_mid_roughness_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_mid_roughness_METADATA_PATH "${ATLANTIS_pbr_sheen_mid_roughness_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_mid_roughness_TARGET "${ATLANTIS_pbr_sheen_mid_roughness_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_mid_roughness_LOGICAL_PATH "${ATLANTIS_pbr_sheen_mid_roughness_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_sheen_high_roughness
  SOURCE materials/pbr_sheen_high_roughness.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_sheen_high_roughness_ARTIFACT_PATH "${ATLANTIS_pbr_sheen_high_roughness_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_high_roughness_METADATA_PATH "${ATLANTIS_pbr_sheen_high_roughness_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_high_roughness_TARGET "${ATLANTIS_pbr_sheen_high_roughness_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_high_roughness_LOGICAL_PATH "${ATLANTIS_pbr_sheen_high_roughness_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_sheen_normal_mapped
  SOURCE materials/pbr_sheen_normal_mapped.material.txt
  TEXTURE textured_quad_srgb
  NORMAL_MAP normal_map_tilted
)
set(ATLANTIS_pbr_sheen_normal_mapped_ARTIFACT_PATH "${ATLANTIS_pbr_sheen_normal_mapped_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_normal_mapped_METADATA_PATH "${ATLANTIS_pbr_sheen_normal_mapped_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_normal_mapped_TARGET "${ATLANTIS_pbr_sheen_normal_mapped_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_normal_mapped_LOGICAL_PATH "${ATLANTIS_pbr_sheen_normal_mapped_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0035 Milestone 3: the sheen-roughness parameter-sweep scene
# (low/mid/high sheen_roughness, same sheenColor) -- IBL-only, no Light
# node, mirroring pbr_clearcoat_demo_scene's own identical shape.
atlantis_add_scene_asset(
  NAME pbr_sheen_demo_scene
  SOURCE scenes/pbr_sheen_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_sheen_low_roughness pbr_sheen_mid_roughness pbr_sheen_high_roughness
  TEXTURE_DEPENDENCIES textured_quad_srgb
)
set(ATLANTIS_pbr_sheen_demo_scene_ARTIFACT_PATH "${ATLANTIS_pbr_sheen_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_demo_scene_METADATA_PATH "${ATLANTIS_pbr_sheen_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_demo_scene_MANIFEST_PATH "${ATLANTIS_pbr_sheen_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_demo_scene_TARGET "${ATLANTIS_pbr_sheen_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 3: the sheen normal-map variant's own dedicated
# scene -- one sphere, the pbr_sheen_ibl_normal_map shader pair's own
# real coverage.
atlantis_add_scene_asset(
  NAME pbr_sheen_normal_map_demo_scene
  SOURCE scenes/pbr_sheen_normal_map_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_sheen_normal_mapped
  TEXTURE_DEPENDENCIES textured_quad_srgb normal_map_tilted
)
set(ATLANTIS_pbr_sheen_normal_map_demo_scene_ARTIFACT_PATH "${ATLANTIS_pbr_sheen_normal_map_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_normal_map_demo_scene_METADATA_PATH "${ATLANTIS_pbr_sheen_normal_map_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_normal_map_demo_scene_MANIFEST_PATH "${ATLANTIS_pbr_sheen_normal_map_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_sheen_normal_map_demo_scene_TARGET "${ATLANTIS_pbr_sheen_normal_map_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 4: the anisotropy-rotation parameter-sweep scene's
# own three materials -- all reusing the already-cooked
# textured_quad_srgb texture (and, for the normal-mapped one, the
# already-cooked normal_map_tilted texture), mirroring the sheen
# materials' own identical "reuse the existing texture" precedent
# immediately above.
atlantis_add_material_asset(
  NAME pbr_anisotropic_rotation_0
  SOURCE materials/pbr_anisotropic_rotation_0.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_anisotropic_rotation_0_ARTIFACT_PATH "${ATLANTIS_pbr_anisotropic_rotation_0_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_0_METADATA_PATH "${ATLANTIS_pbr_anisotropic_rotation_0_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_0_TARGET "${ATLANTIS_pbr_anisotropic_rotation_0_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_0_LOGICAL_PATH "${ATLANTIS_pbr_anisotropic_rotation_0_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_anisotropic_rotation_45
  SOURCE materials/pbr_anisotropic_rotation_45.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_anisotropic_rotation_45_ARTIFACT_PATH "${ATLANTIS_pbr_anisotropic_rotation_45_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_45_METADATA_PATH "${ATLANTIS_pbr_anisotropic_rotation_45_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_45_TARGET "${ATLANTIS_pbr_anisotropic_rotation_45_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_45_LOGICAL_PATH "${ATLANTIS_pbr_anisotropic_rotation_45_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_anisotropic_rotation_90
  SOURCE materials/pbr_anisotropic_rotation_90.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_pbr_anisotropic_rotation_90_ARTIFACT_PATH "${ATLANTIS_pbr_anisotropic_rotation_90_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_90_METADATA_PATH "${ATLANTIS_pbr_anisotropic_rotation_90_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_90_TARGET "${ATLANTIS_pbr_anisotropic_rotation_90_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_rotation_90_LOGICAL_PATH "${ATLANTIS_pbr_anisotropic_rotation_90_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME pbr_anisotropic_normal_mapped
  SOURCE materials/pbr_anisotropic_normal_mapped.material.txt
  TEXTURE textured_quad_srgb
  NORMAL_MAP normal_map_tilted
)
set(ATLANTIS_pbr_anisotropic_normal_mapped_ARTIFACT_PATH "${ATLANTIS_pbr_anisotropic_normal_mapped_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_normal_mapped_METADATA_PATH "${ATLANTIS_pbr_anisotropic_normal_mapped_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_normal_mapped_TARGET "${ATLANTIS_pbr_anisotropic_normal_mapped_TARGET}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_normal_mapped_LOGICAL_PATH "${ATLANTIS_pbr_anisotropic_normal_mapped_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0035 Milestone 4: the anisotropy-rotation parameter-sweep scene
# (rotation 0/45/90 degrees, same anisotropy_factor) -- IBL-only, no
# Light node, mirroring pbr_sheen_demo_scene's own identical shape.
atlantis_add_scene_asset(
  NAME pbr_anisotropic_demo_scene
  SOURCE scenes/pbr_anisotropic_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_anisotropic_rotation_0 pbr_anisotropic_rotation_45 pbr_anisotropic_rotation_90
  TEXTURE_DEPENDENCIES textured_quad_srgb
)
set(ATLANTIS_pbr_anisotropic_demo_scene_ARTIFACT_PATH "${ATLANTIS_pbr_anisotropic_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_demo_scene_METADATA_PATH "${ATLANTIS_pbr_anisotropic_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_demo_scene_MANIFEST_PATH "${ATLANTIS_pbr_anisotropic_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_demo_scene_TARGET "${ATLANTIS_pbr_anisotropic_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 4: the anisotropic normal-map variant's own
# dedicated scene -- one sphere, the pbr_anisotropic_ibl_normal_map
# shader pair's own real coverage.
atlantis_add_scene_asset(
  NAME pbr_anisotropic_normal_map_demo_scene
  SOURCE scenes/pbr_anisotropic_normal_map_demo.scene.txt
  MESH_DEPENDENCIES pbr_sphere
  MATERIAL_DEPENDENCIES pbr_anisotropic_normal_mapped
  TEXTURE_DEPENDENCIES textured_quad_srgb normal_map_tilted
)
set(ATLANTIS_pbr_anisotropic_normal_map_demo_scene_ARTIFACT_PATH "${ATLANTIS_pbr_anisotropic_normal_map_demo_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_normal_map_demo_scene_METADATA_PATH "${ATLANTIS_pbr_anisotropic_normal_map_demo_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_normal_map_demo_scene_MANIFEST_PATH "${ATLANTIS_pbr_anisotropic_normal_map_demo_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_anisotropic_normal_map_demo_scene_TARGET "${ATLANTIS_pbr_anisotropic_normal_map_demo_scene_TARGET}" PARENT_SCOPE)

# Plan 0035 Milestone 5 (Spec 0035 Requirement 6): the showcase scene's
# own 30 material assets -- 25 PbrDirectLit parameter-sweep variants
# across the four Milestone 1 CC0 textures (metal/wood/marble/tile) plus
# one neutral reference, one PbrClearcoat/PbrSheen/PbrAnisotropic
# identifiable showcase sphere each (Requirement 6's own "at minimum one
# each" bar), the ground material, and one shared pedestal material
# reused by all 28 pedestal nodes (keeps this scene's own real
# descriptor-set demand at 30 distinct materials, not 58 -- see this
# Milestone's own PR for the real, measured N+5=35-set peak against the
# ADR-0064 60-set ceiling).

atlantis_add_material_asset(
  NAME showcase_metal_polished
  SOURCE materials/showcase_metal_polished.material.txt
  TEXTURE metal_rust_srgb
)
set(ATLANTIS_showcase_metal_polished_ARTIFACT_PATH "${ATLANTIS_showcase_metal_polished_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_polished_METADATA_PATH "${ATLANTIS_showcase_metal_polished_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_polished_TARGET "${ATLANTIS_showcase_metal_polished_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_polished_LOGICAL_PATH "${ATLANTIS_showcase_metal_polished_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_metal_satin
  SOURCE materials/showcase_metal_satin.material.txt
  TEXTURE metal_rust_srgb
)
set(ATLANTIS_showcase_metal_satin_ARTIFACT_PATH "${ATLANTIS_showcase_metal_satin_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_satin_METADATA_PATH "${ATLANTIS_showcase_metal_satin_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_satin_TARGET "${ATLANTIS_showcase_metal_satin_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_satin_LOGICAL_PATH "${ATLANTIS_showcase_metal_satin_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_metal_semi_rough
  SOURCE materials/showcase_metal_semi_rough.material.txt
  TEXTURE metal_rust_srgb
)
set(ATLANTIS_showcase_metal_semi_rough_ARTIFACT_PATH "${ATLANTIS_showcase_metal_semi_rough_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_semi_rough_METADATA_PATH "${ATLANTIS_showcase_metal_semi_rough_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_semi_rough_TARGET "${ATLANTIS_showcase_metal_semi_rough_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_semi_rough_LOGICAL_PATH "${ATLANTIS_showcase_metal_semi_rough_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_metal_rough
  SOURCE materials/showcase_metal_rough.material.txt
  TEXTURE metal_rust_srgb
)
set(ATLANTIS_showcase_metal_rough_ARTIFACT_PATH "${ATLANTIS_showcase_metal_rough_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_rough_METADATA_PATH "${ATLANTIS_showcase_metal_rough_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_rough_TARGET "${ATLANTIS_showcase_metal_rough_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_rough_LOGICAL_PATH "${ATLANTIS_showcase_metal_rough_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_metal_matte
  SOURCE materials/showcase_metal_matte.material.txt
  TEXTURE metal_rust_srgb
)
set(ATLANTIS_showcase_metal_matte_ARTIFACT_PATH "${ATLANTIS_showcase_metal_matte_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_matte_METADATA_PATH "${ATLANTIS_showcase_metal_matte_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_matte_TARGET "${ATLANTIS_showcase_metal_matte_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_matte_LOGICAL_PATH "${ATLANTIS_showcase_metal_matte_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_metal_gold_tint
  SOURCE materials/showcase_metal_gold_tint.material.txt
  TEXTURE metal_rust_srgb
)
set(ATLANTIS_showcase_metal_gold_tint_ARTIFACT_PATH "${ATLANTIS_showcase_metal_gold_tint_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_gold_tint_METADATA_PATH "${ATLANTIS_showcase_metal_gold_tint_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_gold_tint_TARGET "${ATLANTIS_showcase_metal_gold_tint_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_metal_gold_tint_LOGICAL_PATH "${ATLANTIS_showcase_metal_gold_tint_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_wood_glossy_varnish
  SOURCE materials/showcase_wood_glossy_varnish.material.txt
  TEXTURE wood_dark_srgb
)
set(ATLANTIS_showcase_wood_glossy_varnish_ARTIFACT_PATH "${ATLANTIS_showcase_wood_glossy_varnish_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_glossy_varnish_METADATA_PATH "${ATLANTIS_showcase_wood_glossy_varnish_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_glossy_varnish_TARGET "${ATLANTIS_showcase_wood_glossy_varnish_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_glossy_varnish_LOGICAL_PATH "${ATLANTIS_showcase_wood_glossy_varnish_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_wood_satin
  SOURCE materials/showcase_wood_satin.material.txt
  TEXTURE wood_dark_srgb
)
set(ATLANTIS_showcase_wood_satin_ARTIFACT_PATH "${ATLANTIS_showcase_wood_satin_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_satin_METADATA_PATH "${ATLANTIS_showcase_wood_satin_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_satin_TARGET "${ATLANTIS_showcase_wood_satin_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_satin_LOGICAL_PATH "${ATLANTIS_showcase_wood_satin_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_wood_natural
  SOURCE materials/showcase_wood_natural.material.txt
  TEXTURE wood_dark_srgb
)
set(ATLANTIS_showcase_wood_natural_ARTIFACT_PATH "${ATLANTIS_showcase_wood_natural_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_natural_METADATA_PATH "${ATLANTIS_showcase_wood_natural_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_natural_TARGET "${ATLANTIS_showcase_wood_natural_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_natural_LOGICAL_PATH "${ATLANTIS_showcase_wood_natural_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_wood_worn
  SOURCE materials/showcase_wood_worn.material.txt
  TEXTURE wood_dark_srgb
)
set(ATLANTIS_showcase_wood_worn_ARTIFACT_PATH "${ATLANTIS_showcase_wood_worn_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_worn_METADATA_PATH "${ATLANTIS_showcase_wood_worn_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_worn_TARGET "${ATLANTIS_showcase_wood_worn_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_worn_LOGICAL_PATH "${ATLANTIS_showcase_wood_worn_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_wood_reddish_stain
  SOURCE materials/showcase_wood_reddish_stain.material.txt
  TEXTURE wood_dark_srgb
)
set(ATLANTIS_showcase_wood_reddish_stain_ARTIFACT_PATH "${ATLANTIS_showcase_wood_reddish_stain_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_reddish_stain_METADATA_PATH "${ATLANTIS_showcase_wood_reddish_stain_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_reddish_stain_TARGET "${ATLANTIS_showcase_wood_reddish_stain_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_reddish_stain_LOGICAL_PATH "${ATLANTIS_showcase_wood_reddish_stain_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_wood_ash_tint
  SOURCE materials/showcase_wood_ash_tint.material.txt
  TEXTURE wood_dark_srgb
)
set(ATLANTIS_showcase_wood_ash_tint_ARTIFACT_PATH "${ATLANTIS_showcase_wood_ash_tint_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_ash_tint_METADATA_PATH "${ATLANTIS_showcase_wood_ash_tint_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_ash_tint_TARGET "${ATLANTIS_showcase_wood_ash_tint_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_wood_ash_tint_LOGICAL_PATH "${ATLANTIS_showcase_wood_ash_tint_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_marble_polished
  SOURCE materials/showcase_marble_polished.material.txt
  TEXTURE marble_srgb
)
set(ATLANTIS_showcase_marble_polished_ARTIFACT_PATH "${ATLANTIS_showcase_marble_polished_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_polished_METADATA_PATH "${ATLANTIS_showcase_marble_polished_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_polished_TARGET "${ATLANTIS_showcase_marble_polished_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_polished_LOGICAL_PATH "${ATLANTIS_showcase_marble_polished_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_marble_satin
  SOURCE materials/showcase_marble_satin.material.txt
  TEXTURE marble_srgb
)
set(ATLANTIS_showcase_marble_satin_ARTIFACT_PATH "${ATLANTIS_showcase_marble_satin_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_satin_METADATA_PATH "${ATLANTIS_showcase_marble_satin_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_satin_TARGET "${ATLANTIS_showcase_marble_satin_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_satin_LOGICAL_PATH "${ATLANTIS_showcase_marble_satin_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_marble_honed
  SOURCE materials/showcase_marble_honed.material.txt
  TEXTURE marble_srgb
)
set(ATLANTIS_showcase_marble_honed_ARTIFACT_PATH "${ATLANTIS_showcase_marble_honed_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_honed_METADATA_PATH "${ATLANTIS_showcase_marble_honed_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_honed_TARGET "${ATLANTIS_showcase_marble_honed_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_honed_LOGICAL_PATH "${ATLANTIS_showcase_marble_honed_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_marble_rough
  SOURCE materials/showcase_marble_rough.material.txt
  TEXTURE marble_srgb
)
set(ATLANTIS_showcase_marble_rough_ARTIFACT_PATH "${ATLANTIS_showcase_marble_rough_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_rough_METADATA_PATH "${ATLANTIS_showcase_marble_rough_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_rough_TARGET "${ATLANTIS_showcase_marble_rough_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_rough_LOGICAL_PATH "${ATLANTIS_showcase_marble_rough_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_marble_charcoal_tint
  SOURCE materials/showcase_marble_charcoal_tint.material.txt
  TEXTURE marble_srgb
)
set(ATLANTIS_showcase_marble_charcoal_tint_ARTIFACT_PATH "${ATLANTIS_showcase_marble_charcoal_tint_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_charcoal_tint_METADATA_PATH "${ATLANTIS_showcase_marble_charcoal_tint_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_charcoal_tint_TARGET "${ATLANTIS_showcase_marble_charcoal_tint_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_charcoal_tint_LOGICAL_PATH "${ATLANTIS_showcase_marble_charcoal_tint_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_marble_rose_tint
  SOURCE materials/showcase_marble_rose_tint.material.txt
  TEXTURE marble_srgb
)
set(ATLANTIS_showcase_marble_rose_tint_ARTIFACT_PATH "${ATLANTIS_showcase_marble_rose_tint_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_rose_tint_METADATA_PATH "${ATLANTIS_showcase_marble_rose_tint_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_rose_tint_TARGET "${ATLANTIS_showcase_marble_rose_tint_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_marble_rose_tint_LOGICAL_PATH "${ATLANTIS_showcase_marble_rose_tint_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_tile_glossy
  SOURCE materials/showcase_tile_glossy.material.txt
  TEXTURE mosaic_tile_srgb
)
set(ATLANTIS_showcase_tile_glossy_ARTIFACT_PATH "${ATLANTIS_showcase_tile_glossy_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_glossy_METADATA_PATH "${ATLANTIS_showcase_tile_glossy_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_glossy_TARGET "${ATLANTIS_showcase_tile_glossy_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_glossy_LOGICAL_PATH "${ATLANTIS_showcase_tile_glossy_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_tile_satin
  SOURCE materials/showcase_tile_satin.material.txt
  TEXTURE mosaic_tile_srgb
)
set(ATLANTIS_showcase_tile_satin_ARTIFACT_PATH "${ATLANTIS_showcase_tile_satin_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_satin_METADATA_PATH "${ATLANTIS_showcase_tile_satin_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_satin_TARGET "${ATLANTIS_showcase_tile_satin_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_satin_LOGICAL_PATH "${ATLANTIS_showcase_tile_satin_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_tile_matte
  SOURCE materials/showcase_tile_matte.material.txt
  TEXTURE mosaic_tile_srgb
)
set(ATLANTIS_showcase_tile_matte_ARTIFACT_PATH "${ATLANTIS_showcase_tile_matte_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_matte_METADATA_PATH "${ATLANTIS_showcase_tile_matte_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_matte_TARGET "${ATLANTIS_showcase_tile_matte_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_matte_LOGICAL_PATH "${ATLANTIS_showcase_tile_matte_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_tile_worn
  SOURCE materials/showcase_tile_worn.material.txt
  TEXTURE mosaic_tile_srgb
)
set(ATLANTIS_showcase_tile_worn_ARTIFACT_PATH "${ATLANTIS_showcase_tile_worn_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_worn_METADATA_PATH "${ATLANTIS_showcase_tile_worn_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_worn_TARGET "${ATLANTIS_showcase_tile_worn_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_worn_LOGICAL_PATH "${ATLANTIS_showcase_tile_worn_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_tile_teal_tint
  SOURCE materials/showcase_tile_teal_tint.material.txt
  TEXTURE mosaic_tile_srgb
)
set(ATLANTIS_showcase_tile_teal_tint_ARTIFACT_PATH "${ATLANTIS_showcase_tile_teal_tint_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_teal_tint_METADATA_PATH "${ATLANTIS_showcase_tile_teal_tint_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_teal_tint_TARGET "${ATLANTIS_showcase_tile_teal_tint_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_teal_tint_LOGICAL_PATH "${ATLANTIS_showcase_tile_teal_tint_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_tile_amber_tint
  SOURCE materials/showcase_tile_amber_tint.material.txt
  TEXTURE mosaic_tile_srgb
)
set(ATLANTIS_showcase_tile_amber_tint_ARTIFACT_PATH "${ATLANTIS_showcase_tile_amber_tint_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_amber_tint_METADATA_PATH "${ATLANTIS_showcase_tile_amber_tint_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_amber_tint_TARGET "${ATLANTIS_showcase_tile_amber_tint_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_tile_amber_tint_LOGICAL_PATH "${ATLANTIS_showcase_tile_amber_tint_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_neutral_reference
  SOURCE materials/showcase_neutral_reference.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_showcase_neutral_reference_ARTIFACT_PATH "${ATLANTIS_showcase_neutral_reference_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_neutral_reference_METADATA_PATH "${ATLANTIS_showcase_neutral_reference_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_neutral_reference_TARGET "${ATLANTIS_showcase_neutral_reference_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_neutral_reference_LOGICAL_PATH "${ATLANTIS_showcase_neutral_reference_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_clearcoat_carbon
  SOURCE materials/showcase_clearcoat_carbon.material.txt
  TEXTURE carbon_fiber_srgb
  NORMAL_MAP carbon_fiber_normal_unorm
)
set(ATLANTIS_showcase_clearcoat_carbon_ARTIFACT_PATH "${ATLANTIS_showcase_clearcoat_carbon_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_clearcoat_carbon_METADATA_PATH "${ATLANTIS_showcase_clearcoat_carbon_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_clearcoat_carbon_TARGET "${ATLANTIS_showcase_clearcoat_carbon_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_clearcoat_carbon_LOGICAL_PATH "${ATLANTIS_showcase_clearcoat_carbon_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_sheen_fabric
  SOURCE materials/showcase_sheen_fabric.material.txt
  TEXTURE textured_quad_srgb
)
set(ATLANTIS_showcase_sheen_fabric_ARTIFACT_PATH "${ATLANTIS_showcase_sheen_fabric_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_sheen_fabric_METADATA_PATH "${ATLANTIS_showcase_sheen_fabric_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_sheen_fabric_TARGET "${ATLANTIS_showcase_sheen_fabric_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_sheen_fabric_LOGICAL_PATH "${ATLANTIS_showcase_sheen_fabric_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_anisotropic_brushed
  SOURCE materials/showcase_anisotropic_brushed.material.txt
  TEXTURE metal_rust_srgb
)
set(ATLANTIS_showcase_anisotropic_brushed_ARTIFACT_PATH "${ATLANTIS_showcase_anisotropic_brushed_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_anisotropic_brushed_METADATA_PATH "${ATLANTIS_showcase_anisotropic_brushed_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_anisotropic_brushed_TARGET "${ATLANTIS_showcase_anisotropic_brushed_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_anisotropic_brushed_LOGICAL_PATH "${ATLANTIS_showcase_anisotropic_brushed_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME worn_concrete_floor
  SOURCE materials/worn_concrete_floor.material.txt
  TEXTURE ground_concrete_srgb
)
set(ATLANTIS_worn_concrete_floor_ARTIFACT_PATH "${ATLANTIS_worn_concrete_floor_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_worn_concrete_floor_METADATA_PATH "${ATLANTIS_worn_concrete_floor_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_worn_concrete_floor_TARGET "${ATLANTIS_worn_concrete_floor_TARGET}" PARENT_SCOPE)
set(ATLANTIS_worn_concrete_floor_LOGICAL_PATH "${ATLANTIS_worn_concrete_floor_LOGICAL_PATH}" PARENT_SCOPE)

atlantis_add_material_asset(
  NAME showcase_pedestal
  SOURCE materials/showcase_pedestal.material.txt
  TEXTURE ground_concrete_srgb
)
set(ATLANTIS_showcase_pedestal_ARTIFACT_PATH "${ATLANTIS_showcase_pedestal_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_pedestal_METADATA_PATH "${ATLANTIS_showcase_pedestal_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_showcase_pedestal_TARGET "${ATLANTIS_showcase_pedestal_TARGET}" PARENT_SCOPE)
set(ATLANTIS_showcase_pedestal_LOGICAL_PATH "${ATLANTIS_showcase_pedestal_LOGICAL_PATH}" PARENT_SCOPE)

# Plan 0035 Milestone 5 (Spec 0035 Requirement 6): the showcase scene
# itself -- 28 spheres (25 PbrDirectLit + 1 each PbrClearcoat/PbrSheen/
# PbrAnisotropic) in a fan/arc, each on its own pedestal, over a
# weathered-concrete ground plane, lit solely by the warehouse_interior
# HDRI (zero Light nodes -- Spec 0035 Requirement 5).
atlantis_add_scene_asset(
  NAME pbr_materials_showcase_scene
  SOURCE scenes/pbr_materials_showcase.scene.txt
  MESH_DEPENDENCIES pbr_dimpled_sphere pbr_pedestal ground_plane
  MATERIAL_DEPENDENCIES showcase_metal_polished showcase_metal_satin showcase_metal_semi_rough showcase_metal_rough showcase_metal_matte showcase_metal_gold_tint
    showcase_wood_glossy_varnish showcase_wood_satin showcase_wood_natural showcase_wood_worn showcase_wood_reddish_stain showcase_wood_ash_tint
    showcase_marble_polished showcase_marble_satin showcase_marble_honed showcase_marble_rough showcase_marble_charcoal_tint showcase_marble_rose_tint
    showcase_tile_glossy showcase_tile_satin showcase_tile_matte showcase_tile_worn showcase_tile_teal_tint showcase_tile_amber_tint
    showcase_neutral_reference showcase_clearcoat_carbon showcase_sheen_fabric showcase_anisotropic_brushed worn_concrete_floor showcase_pedestal
  TEXTURE_DEPENDENCIES carbon_fiber_normal_unorm carbon_fiber_srgb ground_concrete_srgb marble_srgb metal_rust_srgb mosaic_tile_srgb textured_quad_srgb wood_dark_srgb
)
set(ATLANTIS_pbr_materials_showcase_scene_ARTIFACT_PATH "${ATLANTIS_pbr_materials_showcase_scene_ARTIFACT_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_materials_showcase_scene_METADATA_PATH "${ATLANTIS_pbr_materials_showcase_scene_METADATA_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_materials_showcase_scene_MANIFEST_PATH "${ATLANTIS_pbr_materials_showcase_scene_MANIFEST_PATH}" PARENT_SCOPE)
set(ATLANTIS_pbr_materials_showcase_scene_TARGET "${ATLANTIS_pbr_materials_showcase_scene_TARGET}" PARENT_SCOPE)
