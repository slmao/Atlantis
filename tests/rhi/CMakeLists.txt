add_executable(atlantis_rhi_tests
  types_tests.cpp
)

target_link_libraries(atlantis_rhi_tests
  PRIVATE
    Atlantis::RHI
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# PRE_TEST: see tests/core/CMakeLists.txt's comment for why POST_BUILD
# (the default) is unsuitable for a multi-config generator.
catch_discover_tests(atlantis_rhi_tests DISCOVERY_MODE PRE_TEST)
