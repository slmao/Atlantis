add_executable(atlantis_platform_tests
  native_window_handle_tests.cpp
  platform_event_tests.cpp
  clock_tests.cpp
  platform_tests.cpp
  # Plan 0034 Milestone 2: whitebox tests for src/platform/src/android/'s
  # host-testable translation logic -- unconditional, like the sources
  # it tests (see src/platform/CMakeLists.txt).
  android_app_command_mapping_tests.cpp
)

if(WIN32)
  # Mirrors src/platform/CMakeLists.txt's WIN32-gated windows_platform.cpp:
  # this is the only other file allowed to include <windows.h>, and must
  # not be built for Android or any future non-Windows target.
  target_sources(atlantis_platform_tests PRIVATE windows_platform_smoke_tests.cpp)
endif()

# Plan 0034 Milestone 2: grants this whitebox test target visibility into
# Atlantis Platform's own private src/ headers (app_command.h,
# app_command_mapping.h, android_app_injection.h) -- mirrors
# tests/vulkan_backend/CMakeLists.txt's identical, already-established
# pattern for that module's own private headers. Atlantis::Platform's own
# PUBLIC target_include_directories (include/ only) is unchanged; this
# grant is local to this one test executable, per ADR-0010's include-path
# hygiene rule.
target_include_directories(atlantis_platform_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/platform/src/android
)

target_link_libraries(atlantis_platform_tests
  PRIVATE
    Atlantis::Platform
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# PRE_TEST: see tests/core/CMakeLists.txt's comment for why POST_BUILD
# (the default) is unsuitable for a multi-config generator.
catch_discover_tests(atlantis_platform_tests DISCOVERY_MODE PRE_TEST)
