add_executable(atlantis_frame_execution_demo main.cpp)

target_link_libraries(atlantis_frame_execution_demo
  PRIVATE
    Atlantis::Core
    Atlantis::Platform
    Atlantis::RHI
    Atlantis::VulkanBackend
    Atlantis::RenderGraph
    atlantis_compiler_warnings
)
