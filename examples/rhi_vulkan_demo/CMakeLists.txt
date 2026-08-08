add_executable(atlantis_rhi_vulkan_demo main.cpp)

target_link_libraries(atlantis_rhi_vulkan_demo
  PRIVATE
    Atlantis::Core
    Atlantis::Platform
    Atlantis::RHI
    Atlantis::VulkanBackend
    atlantis_compiler_warnings
)
