# See plans/0003-rhi-vulkan-windowed-foundation.md Section 8: this
# executable is GPU-independent by construction (no source file here
# touches a real VkInstance/VkDevice) and is discovered by a bare
# ctest -LE gpu run with no label to exclude. The separate, GPU-required
# atlantis_vulkan_backend_gpu_tests executable does not exist yet -- it is
# added in a later Implementation Order step.

add_executable(atlantis_vulkan_backend_tests
  vulkan_result_tests.cpp
  presentation_logic_tests.cpp
  validation_policy_tests.cpp
)

target_include_directories(atlantis_vulkan_backend_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/vulkan_backend/src
)

target_link_libraries(atlantis_vulkan_backend_tests
  PRIVATE
    Atlantis::VulkanBackend
    # atlantis_vulkan_backend links Vulkan::Vulkan PRIVATE (Section 10), so
    # it does not propagate here -- this target needs its own link because
    # it directly compiles private headers (vulkan_result.h,
    # vulkan_presentation.h) that include Vulkan headers.
    Vulkan::Vulkan
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# PRE_TEST: see tests/core/CMakeLists.txt's comment for why POST_BUILD
# (the default) is unsuitable for a multi-config generator.
catch_discover_tests(atlantis_vulkan_backend_tests DISCOVERY_MODE PRE_TEST)
