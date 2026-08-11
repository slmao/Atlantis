add_executable(atlantis_renderer_tests
  renderer_ownership_tests.cpp
)
target_include_directories(atlantis_renderer_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/tests/render_graph
)
target_link_libraries(atlantis_renderer_tests
  PRIVATE
    Atlantis::Renderer
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)
catch_discover_tests(atlantis_renderer_tests DISCOVERY_MODE PRE_TEST)
