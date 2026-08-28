# Plan 0013 Step 1 / ADR-0047: atlantis_runtime_host is a PRIVATE
# composition library -- it exists solely so this module's own
# lifecycle/composition logic is unit-testable and so main.cpp stays
# thin. No other top-level module may depend on Atlantis::RuntimeHost,
# and no install()/export rule is added for it (Plan 0013 Section D1a).
add_library(atlantis_runtime_host STATIC
  src/lifecycle_state.cpp
  src/exit_reason.cpp
  src/init_error.cpp
  src/error_classification.cpp
  src/platform_session.cpp
  src/scene_extraction.cpp
  src/scene_manifest.cpp
  src/scene_load.cpp
  src/material_realization.cpp
  src/runtime_application.cpp
)
add_library(Atlantis::RuntimeHost ALIAS atlantis_runtime_host)

target_include_directories(atlantis_runtime_host
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Plan 0013 Section D1: the full dependency list this module's real
# composition needs -- Core, Platform, RHI, Vulkan Backend, Renderer,
# Shader System (both targets), Asset System. Deliberately NOT
# Atlantis::RenderGraph: Renderer::drawFrame() already owns RenderGraph
# construction/compilation/execution internally (Spec 0013's own
# Independent Review finding).
target_link_libraries(atlantis_runtime_host
  PUBLIC
    Atlantis::Core
    Atlantis::Platform
    Atlantis::RHI
    Atlantis::VulkanBackend
    Atlantis::Renderer
    Atlantis::ShaderSystem
    Atlantis::ShaderSystemRhiIntegration
    Atlantis::AssetSystem
    # Plan 0014 Section D1: Runtime's own World-driven validation scene
    # and Runtime-private extraction adapter.
    Atlantis::World
  PRIVATE
    atlantis_compiler_warnings
)

# Plan 0013 Section D1/D3 (2026-08-21 amendment): real, compile-time
# enum-exhaustiveness protection for classifyPresentationError()/
# classifySubmitError()'s own no-default switches requires MSVC's C4062
# ("enumerator ... not handled"), which is off by default even at /W4.
# Scoped to this target only -- cmake/CompilerWarnings.cmake (shared by
# every target in this repository) is deliberately not touched. The
# repository's existing, already-present /WX promotes C4062 to a hard
# build error once this target opts into seeing it.
target_compile_options(atlantis_runtime_host PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)

# Plan 0013 Step 4: the thin, per-OS entry point -- no composition logic
# of its own beyond building a BootstrapConfig from build-tree paths and
# driving the object atlantis_runtime_host's own public API already
# vends. Depends on Atlantis::RuntimeHost only.
add_executable(atlantis_runtime main.cpp)

target_link_libraries(atlantis_runtime
  PRIVATE
    Atlantis::RuntimeHost
    atlantis_compiler_warnings
)

# Plan 0013 Section D9: absolute, configuration-independent build-tree
# paths, reusing the already-exported ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR
# (src/shader_system/CMakeLists.txt's atlantis_add_slang_shader_pair())
# and ATLANTIS_minimal_cube_{ARTIFACT_PATH,METADATA_PATH,TARGET}
# (src/asset_system/CMakeLists.txt's atlantis_add_static_mesh_asset())
# variables -- never a working-directory-relative path, never a
# hardcoded machine path. Plan 0015 Section D11: the three
# ATLANTIS_world_scene_* variables, same sourcing, from
# atlantis_add_scene_asset() (assets/CMakeLists.txt).
target_compile_definitions(atlantis_runtime PRIVATE
  ATLANTIS_RUNTIME_SHADER_DIR="${ATLANTIS_minimal_mesh_SHADER_OUTPUT_DIR}"
  ATLANTIS_RUNTIME_ASSET_ARTIFACT_PATH="${ATLANTIS_minimal_cube_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_ASSET_METADATA_PATH="${ATLANTIS_minimal_cube_METADATA_PATH}"
  ATLANTIS_RUNTIME_SCENE_ARTIFACT_PATH="${ATLANTIS_world_scene_ARTIFACT_PATH}"
  ATLANTIS_RUNTIME_SCENE_METADATA_PATH="${ATLANTIS_world_scene_METADATA_PATH}"
  ATLANTIS_RUNTIME_SCENE_MANIFEST_PATH="${ATLANTIS_world_scene_MANIFEST_PATH}"
  # Plan 0018 Section P10: the second, MaterialKind::UnlitTextured
  # built-in shader pair's own output directory -- reuses the already-
  # exported ATLANTIS_textured_quad_SHADER_OUTPUT_DIR
  # (shaders/textured_quad/CMakeLists.txt's own atlantis_add_slang_shader_pair()
  # call, now unconditional), mirroring ATLANTIS_RUNTIME_SHADER_DIR's own
  # sourcing exactly.
  ATLANTIS_RUNTIME_UNLIT_TEXTURED_SHADER_DIR="${ATLANTIS_textured_quad_SHADER_OUTPUT_DIR}"
)

# Guarantees the shader-compile and asset-cook targets have already run
# before this executable builds -- matching
# tests/image_regression/CMakeLists.txt's own identical pattern. Plan
# 0018 Section P10: textured_quad_shaders added alongside
# minimal_mesh_shaders, now that shaders/textured_quad is built
# unconditionally.
add_dependencies(atlantis_runtime minimal_mesh_shaders textured_quad_shaders ${ATLANTIS_minimal_cube_TARGET}
  ${ATLANTIS_world_scene_TARGET})
