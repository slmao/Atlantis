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
# Deliberately absent: Atlantis::Platform, Atlantis::VulkanBackend,
# Vulkan::Vulkan -- linking any of these would itself be an
# Acceptance-Criteria violation (Spec 0007), not merely undesirable.
