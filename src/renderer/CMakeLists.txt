add_library(atlantis_renderer STATIC
  src/mesh.cpp
  src/material.cpp
  src/renderer.cpp
)
add_library(Atlantis::Renderer ALIAS atlantis_renderer)

target_include_directories(atlantis_renderer
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_renderer
  PUBLIC
    Atlantis::Core
    Atlantis::RHI
    Atlantis::RenderGraph
  PRIVATE
    atlantis_compiler_warnings
)

# Plan 0023 Milestone 5 / final verification matrix (real, disclosed gap
# found and fixed during that verification pass): real, compile-time
# enum-exhaustiveness protection for Renderer::drawFrame()'s own
# no-default switch over MaterialPushConstantLayout requires MSVC's
# C4062 ("enumerator ... not handled"), which is off by default even at
# /W4 -- Milestone 5's own text assumed this target already had it
# (mirroring src/runtime/CMakeLists.txt's own atlantis_runtime_host
# precedent, Plan 0013 Section D1/D3), but atlantis_renderer had never
# opted in. Scoped to this target only -- cmake/CompilerWarnings.cmake
# (shared by every target in this repository) is deliberately not
# touched. The repository's existing, already-present /WX promotes
# C4062 to a hard build error once this target opts into seeing it.
target_compile_options(atlantis_renderer PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/w14062>
)
# Deliberately absent: Atlantis::Platform, Atlantis::VulkanBackend,
# Vulkan::Vulkan -- linking any of these would itself be an
# Acceptance-Criteria violation (Spec 0007), not merely undesirable.
