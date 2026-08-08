add_library(atlantis_vulkan_backend STATIC
  src/vulkan_result.cpp
  src/validation.cpp
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
