add_library(atlantis_vulkan_backend STATIC
  src/vulkan_result.cpp
  src/validation.cpp
  src/vulkan_instance.cpp
  src/vulkan_device.cpp
  src/vulkan_presentation.cpp
  src/vulkan_render_target.cpp
  src/vulkan_command_list.cpp
  src/vulkan_submission_signal.cpp
  src/resource_state_mapping.cpp
  src/vulkan_memory.cpp
  src/dynamic_rendering.cpp
  src/vulkan_buffer.cpp
  src/vulkan_texture.cpp
  src/vulkan_pipeline.cpp
  src/wsi/win32_surface.cpp
)
add_library(Atlantis::VulkanBackend ALIAS atlantis_vulkan_backend)

target_include_directories(atlantis_vulkan_backend
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_vulkan_backend
  PUBLIC
    Atlantis::RHI
    Atlantis::Core
    Atlantis::Platform
  PRIVATE
    Vulkan::Vulkan
    atlantis_compiler_warnings
)
