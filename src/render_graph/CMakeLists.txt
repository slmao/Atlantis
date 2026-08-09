add_library(atlantis_render_graph STATIC
  src/compiled_graph.cpp
)
add_library(Atlantis::RenderGraph ALIAS atlantis_render_graph)

target_include_directories(atlantis_render_graph
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_render_graph
  PUBLIC
    Atlantis::Core
  PRIVATE
    atlantis_compiler_warnings
)
