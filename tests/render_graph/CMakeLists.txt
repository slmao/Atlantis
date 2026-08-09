add_executable(atlantis_render_graph_tests
  handle_ownership_tests.cpp
  dependency_derivation_tests.cpp
  compile_algorithm_tests.cpp
)

# PRIVATE to this test target only -- needed once compile_algorithm_tests.cpp
# (a later implementation step) is added, so it can #include the private
# src/render_graph/src/compile_algorithm.h header. Added now, even though
# that file does not exist yet, so the include path is already correct when
# it is. Never added to atlantis_render_graph's own PUBLIC include interface
# -- library consumers never see this path.
target_include_directories(atlantis_render_graph_tests
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src/render_graph/src
)

target_link_libraries(atlantis_render_graph_tests
  PRIVATE
    Atlantis::RenderGraph
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

# No PROPERTIES LABELS "gpu" anywhere in this file -- this module is
# entirely GPU-independent.
catch_discover_tests(atlantis_render_graph_tests DISCOVERY_MODE PRE_TEST)
