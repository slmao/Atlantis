# See plans/0003-rhi-vulkan-windowed-foundation.md Section 8: this
# executable is GPU-independent by construction (no source file here
# touches a real VkInstance/VkDevice) and is discovered by a bare
# ctest -LE gpu run with no label to exclude. The separate, GPU-required
# atlantis_vulkan_backend_gpu_tests executable below requires a real
# Vulkan-capable GPU and a real window; every case it registers carries
# the CTest "gpu" label, so it is excluded from `ctest -LE gpu` and run
# only via `ctest -L gpu`.

add_executable(atlantis_vulkan_backend_tests
  vulkan_result_tests.cpp
  presentation_logic_tests.cpp
  validation_policy_tests.cpp
  resource_state_mapping_tests.cpp
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

# Separate executable, separate registration -- not a second
# catch_discover_tests() call or a TEST_SPEC filter against the target
# above, so there is no risk of the same Catch2 test case being
# registered twice. This test drives Presentation entirely through
# Atlantis's public API (atlantis/platform/platform.h,
# atlantis/vulkan_backend/vulkan_backend.h, atlantis/rhi/presentation.h)
# and includes no Vulkan header itself, so it needs no Vulkan::Vulkan
# link and no private src/vulkan_backend/src include path.
add_executable(atlantis_vulkan_backend_gpu_tests
  vulkan_presentation_gpu_tests.cpp
)

target_link_libraries(atlantis_vulkan_backend_gpu_tests
  PRIVATE
    Atlantis::VulkanBackend
    Atlantis::Platform
    Atlantis::RHI
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# LABELS "gpu": the CMake test-property mechanism that actually makes
# `ctest -L gpu` / `ctest -LE gpu` work -- not a Catch2-level tag filter.
# PRE_TEST: same multi-config rationale as atlantis_vulkan_backend_tests
# above.
catch_discover_tests(atlantis_vulkan_backend_gpu_tests DISCOVERY_MODE PRE_TEST PROPERTIES LABELS "gpu")
