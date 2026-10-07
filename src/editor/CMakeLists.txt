# Plan 0056 P1 (Spec 0056 ruling Q7, P-a; ADR-0108 D2): Atlantis Editor -- a
# client library of a running Runtime. It knows a RuntimeConnection and a
# RuntimeControl and nothing else of the Runtime: it links Atlantis::Connection
# and the UI library (ADR-0107) only, and includes no Runtime, ECS, Platform,
# RHI, Renderer, Vulkan or OS header -- both checked by
# tests/editor/editor_boundary_tests.cpp, which also holds UI-library headers
# to src/view/. Hosted by atlantis_runtime (`--editor`); Android builds it but
# hosts no editor (Plan 0056 J11).
add_library(atlantis_editor STATIC
  src/editor.cpp
  src/view/editor_view.cpp
)
add_library(Atlantis::Editor ALIAS atlantis_editor)

target_include_directories(atlantis_editor
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(atlantis_editor
  PUBLIC
    Atlantis::Connection
  PRIVATE
    ImGui::ImGui
    atlantis_compiler_warnings
)
