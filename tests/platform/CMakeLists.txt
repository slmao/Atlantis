add_executable(atlantis_platform_tests
  native_window_handle_tests.cpp
  platform_event_tests.cpp
  clock_tests.cpp
  platform_tests.cpp
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
