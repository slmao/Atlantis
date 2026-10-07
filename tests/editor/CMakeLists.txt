# Plan 0056 M1/M4/M5: Atlantis Editor -- the include/link boundary scan (Spec
# 0056 R1), the font atlas and per-editor UI context, and (later milestones)
# the models over an InProcess connection on a baked fixture scene and the
# view driven headless by scripted input. No GPU.
add_executable(atlantis_editor_tests
  editor_boundary_tests.cpp
  editor_skeleton_tests.cpp
)

# ImGui::ImGui is linked here only so the tests can observe the UI library's
# current-context state; the library under test never exposes it.
target_link_libraries(atlantis_editor_tests
  PRIVATE
    Atlantis::Editor
    ImGui::ImGui
    Catch2::Catch2WithMain
    atlantis_compiler_warnings
)

target_compile_definitions(atlantis_editor_tests PRIVATE
  ATLANTIS_EDITOR_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/editor"
  ATLANTIS_IMGUI_CMAKE="${CMAKE_SOURCE_DIR}/cmake/AtlantisImGui.cmake"
)

catch_discover_tests(atlantis_editor_tests DISCOVERY_MODE PRE_TEST)
