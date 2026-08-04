add_executable(atlantis_platform_tests
  native_window_handle_tests.cpp
  platform_event_tests.cpp
  clock_tests.cpp
  platform_tests.cpp
)

if(WIN32)
  # Mirrors src/platform/CMakeLists.txt's WIN32-gated windows_platform.cpp:
  # this is the only other file allowed to include <windows.h>, and must
  # not be built for Android or any future non-Windows target.
  target_sources(atlantis_platform_tests PRIVATE windows_platform_smoke_tests.cpp)
endif()

target_link_libraries(atlantis_platform_tests
  PRIVATE
    Atlantis::Platform
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# PRE_TEST: see tests/core/CMakeLists.txt's comment for why POST_BUILD
# (the default) is unsuitable for a multi-config generator.
catch_discover_tests(atlantis_platform_tests DISCOVERY_MODE PRE_TEST)
