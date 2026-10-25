add_executable(atlantis_renderer_tests
  bloom_tests.cpp
  draw_order_tests.cpp
  exposure_multiplier_tests.cpp
  renderer_ownership_tests.cpp
  # Plan 0056 M3: the UI overlay pass's recording, on the fake command list.
  ui_overlay_tests.cpp
  # Plan 0066 M3b (ADR-0132 D3): the Viewport overlay's draw list and passes.
  viewport_overlay_tests.cpp
  # Plan 0067 M1 (ADR-0134 D1): the cofactor normal -- its eleven shader
  # copies against one reference text, and its formula.
  cofactor_normal_tests.cpp
)
target_compile_definitions(atlantis_renderer_tests PRIVATE
  ATLANTIS_SHADER_SOURCE_DIR="${CMAKE_SOURCE_DIR}/shaders"
  ATLANTIS_COFACTOR_NORMAL_REFERENCE="${CMAKE_CURRENT_SOURCE_DIR}/cofactor_normal_reference.slang"
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
